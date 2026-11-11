// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraphEditorCore/RunSession.hpp>

#include <FlowGraph/Encoding.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace fged::core {

namespace {

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// What is not a whole number is 0, as an argument left empty is.
int64_t toInt(const std::string& text) {
    const std::string t = trimmed(text);
    int64_t v = 0;
    const char* first = t.data();
    if (!t.empty() && t.front() == '+') ++first;
    auto [end, ec] = std::from_chars(first, t.data() + t.size(), v);
    return ec == std::errc() && end == t.data() + t.size() ? v : 0;
}

double toFloat(const std::string& text) {
    const std::string t = trimmed(text);
    double v = 0;
    const char* first = t.data();
    if (!t.empty() && t.front() == '+') ++first;
    auto [end, ec] = std::from_chars(first, t.data() + t.size(), v);
    return ec == std::errc() && end == t.data() + t.size() ? v : 0.0;
}

} // namespace

RunSession::RunSession(Document& doc, UiThread& ui) : doc_(doc), ui_(ui) {
    // before the views: what they show of a run is gone, or stopped, by the time they hear of the change
    docChanged_ = doc_.changed.connectFirst([this](bool structural) { onDocumentChanged(structural); });
    docOpened_ = doc_.scriptOpened.connectFirst([this] { onScriptOpened(); });
}

RunSession::~RunSession() {
    doc_.changed.disconnect(docChanged_);
    doc_.scriptOpened.disconnect(docOpened_);
    detach();
    ax_.reset();
    ui_.stopRepeating();
}

fg::Value RunSession::parseArgument(const fg::Registry& reg, const fg::TypeDesc* type, const std::string& text) {
    if (!type) return fg::Value(text);
    switch (type->kind) {
    case fg::TypeKind::Bool: {
        std::string t = trimmed(text);
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return fg::Value(t == "1" || t == "true" || t == "yes" || t == "on");
    }
    case fg::TypeKind::Int: return fg::Value(toInt(text));
    case fg::TypeKind::Float: return fg::Value(toFloat(text));
    case fg::TypeKind::String: return fg::Value(text);
    case fg::TypeKind::Enum:
        for (auto& e : type->enumValues)
            if (e.name == text) return fg::Value(e.value);
        return fg::Value(toInt(text));
    case fg::TypeKind::Byte: return reg.convert(fg::Value(toInt(text)), type);     // wraps around: 300 -> 44
    case fg::TypeKind::Bytes: {
        std::string data;
        fg::hexDecode(text, data);              // what is not hex (an odd digit while typing) gives no bytes
        return fg::Value(fg::Bytes(std::move(data)));
    }
    default: return reg.defaultValue(type);
    }
}

void RunSession::setState(RunState s) {
    if (state_ == s) return;
    state_ = s;
    stateChanged(s);
}

// Callbacks run on the worker thread: hand them to the editor's thread, dropping those of a previous run.
void RunSession::post(uint64_t generation, std::function<void()> fn) {
    ui_.post([this, generation, fn = std::move(fn)] {
        if (generation == generation_) fn();
    });
}

void RunSession::reportLog(uint64_t generation, int level, const std::string& text) {
    std::lock_guard lk(pendingMu_);
    if (pendingLogs_.size() >= kMaxPendingLogs) {
        ++droppedLogs_;
        return;
    }
    pendingLogs_.push_back({generation, level, text});
}

// Called for every link the script follows, on the worker thread: that can be millions a second. A link already reported in
// the current tick is dropped without taking a lock; the animation only needs to know that it fired.
void RunSession::reportLink(uint64_t generation, const std::string& graph, int from, int out, int to) {
    uint64_t h = reinterpret_cast<uintptr_t>(&graph) * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t(uint32_t(from)) * 0xC2B2AE3D27D4EB4Full) ^ (uint64_t(uint32_t(out)) * 0x165667B19E3779F9ull) ^
         (uint64_t(uint32_t(to)) * 0x27D4EB2F165667C5ull);
    h ^= h >> 29;
    const uint64_t tag = (h & 0xFFFFFFFF00000000ull) | linkTick_.load(std::memory_order_relaxed);
    std::atomic<uint64_t>& seen = linkSeen_[h & (linkSeen_.size() - 1)];
    if (seen.load(std::memory_order_relaxed) == tag) return;
    seen.store(tag, std::memory_order_relaxed);
    std::lock_guard lk(pendingMu_);
    if (pendingLinks_.size() < 1024) pendingLinks_.push_back({generation, graph, from, out, to});
}

void RunSession::drainPending() {
    std::vector<LinkEvent> links;
    std::vector<LogEvent> logs;
    size_t dropped;
    {
        std::lock_guard lk(pendingMu_);
        links.swap(pendingLinks_);
        logs.swap(pendingLogs_);
        dropped = droppedLogs_;
        droppedLogs_ = 0;
    }
    linkTick_.fetch_add(1, std::memory_order_relaxed);
    for (auto& l : logs)
        if (l.generation == generation_) logMessage(l.level, l.text);
    if (dropped)
        logMessage(int(fg::LogLevel::Warning),
                   std::to_string(dropped) + " log lines were skipped: the script printed faster than they can be shown.");
    for (auto& l : links)
        if (l.generation == generation_) linkFired(l.graph, l.from, l.out, l.to);
}

void RunSession::clearPending() {
    std::lock_guard lk(pendingMu_);
    pendingLinks_.clear();
    pendingLogs_.clear();
    droppedLogs_ = 0;
}

void RunSession::startDraining() { ui_.startRepeating(30, [this] { drainPending(); }); }
void RunSession::stopDraining() { ui_.stopRepeating(); }

void RunSession::setStepDelay(int ms) {
    delayMs_ = ms;
    if (ax_) ax_->setStepDelay(ms);
}

bool RunSession::start(const std::string& entry, const std::vector<fg::Value>& args) {
    detach();
    if (isActive()) stop();
    auto program = doc_.program();
    if (!program || program->hasErrors) {
        notice(RunNotice::CompileErrors);
        return false;
    }
    ax_.reset();
    clearPending();
    const uint64_t gen = ++generation_;
    fg::AsyncExecutor::Callbacks cb;
    cb.log = [this, gen](fg::LogLevel l, const std::string& m) { reportLog(gen, int(l), m); };
    cb.linkFired = [this, gen](const std::string& g, fg::NodeId from, int out, fg::NodeId to) { reportLink(gen, g, from, out, to); };
    cb.taskState = [this, gen](fg::AsyncExecutor::Handle h, fg::TaskState st) { post(gen, [this, h, st] { onTaskState(h, st); }); };
    cb.completed = [this, gen](const fg::Executor::Completion& c) {
        std::string returns;
        for (auto& r : c.result.returns) returns += (returns.empty() ? "" : ", ") + r.toString();
        const bool ok = c.result.state == fg::TaskState::Finished;
        const bool exited = c.result.exited;
        const int code = c.result.exitCode;
        std::string err = c.result.error;
        post(gen, [this, ok, exited, code, err, returns] { onCompleted(ok, exited, code, err, returns); });
    };
    ax_ = std::make_unique<fg::AsyncExecutor>(program, cb, &doc_.debugger());
    ax_->setStepDelay(delayMs_);
    startDraining();
    logMessage(int(fg::LogLevel::Info), "--- running " + entry + " ---");
    frame_ = 0;
    setState(RunState::Running);
    handle_ = program->events.count(entry) ? ax_->postEvent(entry, args) : ax_->postFunction(entry, args);
    return true;
}

void RunSession::stop() {
    if (attached_) {
        detach();
        return;
    }
    if (!isActive() || !ax_) return;
    ax_->abortAll();
    if (!ax_->waitIdle(2000)) ax_.reset();         // a native function that never returns: abandon the worker
    ++generation_;                                  // ignore whatever the stopped run still reports
    clearPending();
    stopDraining();
    setState(RunState::Idle);
    logMessage(int(fg::LogLevel::Info), "--- stopped ---");
    finished(false, "stopped");
}

void RunSession::pause() {
    if (attached_) {
        if (state_ == RunState::Running) doc_.debugger().pauseNext();     // the next node any task is about to run is not run
        return;
    }
    if ((state_ == RunState::Running || state_ == RunState::Waiting) && ax_) ax_->pause(handle_);
}

void RunSession::resume() {
    if (attached_) {
        if (state_ == RunState::Paused) commandAttached(fg::Debugger::Command::Continue);
        return;
    }
    if (state_ != RunState::Paused || !ax_) return;
    resumed();
    setState(RunState::Running);
    ax_->resume(handle_);
}

void RunSession::step(fg::Debugger::Command c) {
    if (attached_) {
        if (state_ == RunState::Paused) commandAttached(c);
        return;
    }
    if (state_ != RunState::Paused || !ax_) return;
    resumed();
    setState(RunState::Running);
    ax_->step(handle_, c);
}

// ---- attached to the executor of the application
bool RunSession::attach(std::shared_ptr<fg::AsyncExecutor> ax) {
    if (!ax) return false;
    detach();
    if (isActive()) stop();
    ax_.reset();
    clearPending();
    const uint64_t gen = ++generation_;
    frame_ = 0;
    handle_ = 0;
    awaiting_ = 0;
    pausedQueue_.clear();
    fg::Debugger* dbg = &doc_.debugger();
    fg::DebugHooks* previous = nullptr;
    // on the worker thread, between two slices: the executor's instance reports to the document's debugger from now on
    const bool ok = ax->inspect([&](fg::Executor& ex) {
        previous = ex.instance().debugHooks();
        dbg->linkFired = [this, gen](const std::string& g, fg::NodeId from, int out, fg::NodeId to) { reportLink(gen, g, from, out, to); };
        fg::Executor* exec = &ex;
        dbg->taskStateChanged = [this, gen, exec](fg::Task& t) {
            const auto h = exec->handleOf(&t);
            const auto st = t.state();
            post(gen, [this, h, st] { onTaskState(h, st); });
        };
        ex.instance().setDebugHooks(dbg);
    });
    if (!ok) return false;                          // the executor has stopped
    previousHooks_ = previous;
    attached_ = std::move(ax);
    startDraining();
    setState(RunState::Running);
    attachedChanged(true);
    return true;
}

void RunSession::detach() {
    if (!attached_) return;
    std::shared_ptr<fg::AsyncExecutor> ax = std::move(attached_);
    ++generation_;                                  // what the session still reports is ignored
    fg::Debugger* dbg = &doc_.debugger();
    std::vector<fg::AsyncExecutor::Handle> stopped(pausedQueue_.begin(), pausedQueue_.end());
    if (state_ == RunState::Paused && handle_) stopped.push_back(handle_);
    fg::DebugHooks* previous = previousHooks_;
    ax->inspect([&](fg::Executor& ex) {
        for (auto h : stopped)
            if (auto t = ex.task(h)) {
                dbg->command(*t, fg::Debugger::Command::Continue);
                ex.resume(h);
            }
        dbg->taskStateChanged = nullptr;
        dbg->linkFired = nullptr;
        ex.instance().setDebugHooks(previous);
    });
    previousHooks_ = nullptr;
    pausedQueue_.clear();
    awaiting_ = 0;
    clearPending();
    stopDraining();
    handle_ = 0;
    frame_ = 0;
    resumed();
    setState(RunState::Idle);
    attachedChanged(false);
}

void RunSession::showPaused(fg::AsyncExecutor::Handle handle) {
    handle_ = handle;
    std::string graph;
    int node = 0;
    if (auto* ax = executor())
        ax->inspect([&](fg::Executor& ex) {
            if (auto t = ex.task(handle)) {
                graph = t->currentGraph();
                node = t->currentNode();
            }
        });
    frame_ = 0;
    setState(RunState::Paused);
    paused(graph, node);
}

void RunSession::commandAttached(fg::Debugger::Command c) {
    const auto h = handle_;
    resumed();
    // From now on this task is the one we follow: its next stop (the next node of a step, a breakpoint) or its end decides
    // what is shown next. Other tasks that are stopped wait until then.
    awaiting_ = h;
    handle_ = 0;
    setState(RunState::Running);
    executor()->inspect([&](fg::Executor& ex) {
        if (auto t = ex.task(h)) {
            doc_.debugger().command(*t, c);
            ex.resume(h);
        }
    });
}

void RunSession::onTaskState(fg::AsyncExecutor::Handle handle, fg::TaskState st) {
    drainPending();                                 // what the script printed before it changed state comes first
    if (attached_) {
        const bool followed = handle == awaiting_;
        auto showNext = [this] {                    // the task we followed is out of the way: the next stopped task, if any
            awaiting_ = 0;
            if (pausedQueue_.empty()) return;
            const auto next = pausedQueue_.front();
            pausedQueue_.pop_front();
            showPaused(next);
        };
        switch (st) {
        case fg::TaskState::Paused:
            if (followed) {                         // the task we followed stopped again: this is where it is now
                awaiting_ = 0;
                showPaused(handle);
            } else if (state_ != RunState::Paused && awaiting_ == 0) {
                showPaused(handle);
            } else if (std::find(pausedQueue_.begin(), pausedQueue_.end(), handle) == pausedQueue_.end()) {
                pausedQueue_.push_back(handle);     // another task, while one is shown or followed: it waits its turn
            }
            break;
        case fg::TaskState::Finished:
        case fg::TaskState::Failed:
        case fg::TaskState::Aborted:
        case fg::TaskState::Waiting:                // a task that waits (Delay...) runs no nodes: do not hold the others up
            if (followed) showNext();
            break;
        default: break;
        }
        return;
    }
    switch (st) {
    case fg::TaskState::Paused: {
        std::string graph;
        int node = 0;
        if (ax_)
            ax_->inspect([&](fg::Executor& ex) {
                if (auto t = ex.task(handle_)) {
                    graph = t->currentGraph();
                    node = t->currentNode();
                }
            });
        frame_ = 0;
        setState(RunState::Paused);
        paused(graph, node);
        break;
    }
    case fg::TaskState::Waiting: setState(RunState::Waiting); break;
    case fg::TaskState::Running: setState(RunState::Running); break;
    default: break;                       // end states are reported by onCompleted
    }
}

void RunSession::onCompleted(bool ok, bool exited, int exitCode, const std::string& error, const std::string& returns) {
    drainPending();
    stopDraining();
    setState(RunState::Idle);
    if (ok) {
        std::string text = "--- finished";
        if (exited) text += ", exit code " + std::to_string(exitCode);
        else if (!returns.empty()) text += ", returned " + returns;
        logMessage(int(fg::LogLevel::Info), text + " ---");
    } else {
        logMessage(int(fg::LogLevel::Error), "--- failed: " + error + " ---");
    }
    finished(ok, error);
}

// Another script was loaded (or a new one made): nothing of the previous run - its variables, its call stack - is its.
void RunSession::onScriptOpened() {
    if (isActive()) stop();
    ax_.reset();
    ++generation_;
    clearPending();
    stopDraining();
    frame_ = 0;
    handle_ = 0;
}

void RunSession::onDocumentChanged(bool structural) {
    if (structural && isActive() && !attached_) {   // an attached application keeps running the script it has, whatever is edited
        stop();
        notice(RunNotice::StoppedByEdit);
    }
}

bool RunSession::snapshot(DebugSnapshot& out) const {
    if (!executor()) return false;
    const bool isPaused = state_ == RunState::Paused;
    const int frameSel = frame_;
    return executor()->inspect([&](fg::Executor& ex) {
        if (isPaused) {
            if (auto t = ex.task(handle_)) {
                for (auto& e : t->callStack()) out.stack.push_back({e.graph, e.node, e.isEvent});
                if (t->depth() > 0) {
                    size_t idx = t->depth() - 1 - size_t(std::min<int>(frameSel, int(t->depth()) - 1));
                    out.frameVariables = fg::inspectFrame(*t, idx);
                }
            }
        }
        out.globals = fg::inspectGlobals(ex.instance());
    });
}

bool RunSession::evaluate(const std::vector<std::string>& expressions, const fg::InspectOptions& opt, std::vector<fg::EvalResult>& out) const {
    if (!executor()) return false;
    const bool isPaused = state_ == RunState::Paused;
    const int frameSel = frame_;
    return executor()->inspect([&](fg::Executor& ex) {
        std::shared_ptr<fg::Task> task = isPaused ? ex.task(handle_) : nullptr;
        size_t idx = 0;
        if (task && task->depth() > 0) idx = task->depth() - 1 - size_t(std::min<int>(frameSel, int(task->depth()) - 1));
        else task = nullptr;
        for (auto& e : expressions) out.push_back(fg::evaluateExpression(ex.instance(), task.get(), idx, e, opt));
    });
}

bool RunSession::pinText(const std::string& graph, fg::NodeId node, const std::string& pin, std::string& out) const {
    if (!executor() || state_ != RunState::Paused) return false;
    bool found = false;
    executor()->inspect([&](fg::Executor& ex) {
        auto task = ex.task(handle_);
        if (!task) return;
        for (size_t i = task->depth(); i-- > 0;) {
            if (task->frame(i).graph->name != graph) continue;
            fg::Value v;
            if (!task->pinValue(i, node, pin, v)) return;
            const fg::CompiledNode* cn = task->frame(i).graph->findNode(node);
            const fg::TypeDesc* t = nullptr;
            if (cn) {
                int pi = cn->findPinIndex(pin, fg::PinDir::Out);
                if (pi < 0) pi = cn->findPinIndex(pin, fg::PinDir::In);
                if (pi >= 0) t = cn->pins[size_t(pi)].type;
            }
            out = fg::inspect(ex.instance().registry(), v, t).text;
            found = true;
            return;
        }
    });
    return found;
}

} // namespace fged::core
