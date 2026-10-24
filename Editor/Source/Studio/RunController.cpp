// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "RunController.hpp"
#include "../Graph/Style.hpp"

#include <QMetaObject>

#include <algorithm>

namespace fged {

RunController::RunController(Document* doc, QObject* parent) : QObject(parent), doc_(doc) {
    connect(doc_, &Document::changed, this, [this](bool structural) { onDocumentChanged(structural); });
    connect(doc_, &Document::scriptOpened, this, [this] { onScriptOpened(); });
    pendingTimer_.setInterval(30);
    connect(&pendingTimer_, &QTimer::timeout, this, [this] { drainPending(); });
}

RunController::~RunController() {
    detach();
    ax_.reset();
}

fg::Value RunController::parseArgument(const fg::Registry& reg, const fg::TypeDesc* type, const QString& text) {
    if (!type) return fg::Value(ss(text));
    switch (type->kind) {
    case fg::TypeKind::Bool: {
        QString t = text.trimmed().toLower();
        return fg::Value(t == "1" || t == "true" || t == "yes" || t == "on");
    }
    case fg::TypeKind::Int: return fg::Value(int64_t(text.toLongLong()));
    case fg::TypeKind::Float: return fg::Value(text.toDouble());
    case fg::TypeKind::String: return fg::Value(ss(text));
    case fg::TypeKind::Enum:
        for (auto& e : type->enumValues)
            if (qs(e.name) == text) return fg::Value(e.value);
        return fg::Value(int64_t(text.toLongLong()));
    case fg::TypeKind::Byte: return reg.convert(fg::Value(int64_t(text.toLongLong())), type);     // wraps around: 300 -> 44
    case fg::TypeKind::Bytes: return bytesFromHex(text);
    default: return reg.defaultValue(type);
    }
}

void RunController::setState(State s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

// Callbacks run on the worker thread: hand them to the GUI thread, dropping those of a previous run.
void RunController::post(uint64_t generation, std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [this, generation, fn = std::move(fn)] {
        if (generation == generation_) fn();
    }, Qt::QueuedConnection);
}

void RunController::reportLog(uint64_t generation, int level, const std::string& text) {
    std::lock_guard lk(pendingMu_);
    if (pendingLogs_.size() >= kMaxPendingLogs) { ++droppedLogs_; return; }
    pendingLogs_.push_back({generation, level, text});
}

// Called for every link the script follows, on the worker thread: that can be millions per second. A link that was already reported
// in the current tick is dropped without taking a lock; the animation only needs to know that it fired.
void RunController::reportLink(uint64_t generation, const std::string& graph, int from, int out, int to) {
    uint64_t h = reinterpret_cast<uintptr_t>(&graph) * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t(uint32_t(from)) * 0xC2B2AE3D27D4EB4Full) ^ (uint64_t(uint32_t(out)) * 0x165667B19E3779F9ull) ^ (uint64_t(uint32_t(to)) * 0x27D4EB2F165667C5ull);
    h ^= h >> 29;
    const uint64_t tag = (h & 0xFFFFFFFF00000000ull) | linkTick_.load(std::memory_order_relaxed);
    std::atomic<uint64_t>& seen = linkSeen_[h & (linkSeen_.size() - 1)];
    if (seen.load(std::memory_order_relaxed) == tag) return;
    seen.store(tag, std::memory_order_relaxed);
    std::lock_guard lk(pendingMu_);
    if (pendingLinks_.size() < 1024) pendingLinks_.push_back({generation, graph, from, out, to});
}

void RunController::drainPending() {
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
        if (l.generation == generation_) emit logMessage(l.level, qs(l.text));
    if (dropped) emit logMessage(int(fg::LogLevel::Warning), QString("%1 log lines were skipped: the script printed faster than they can be shown.").arg(dropped));
    for (auto& l : links)
        if (l.generation == generation_) emit linkFired(qs(l.graph), l.from, l.out, l.to);
}

void RunController::clearPending() {
    std::lock_guard lk(pendingMu_);
    pendingLinks_.clear();
    pendingLogs_.clear();
    droppedLogs_ = 0;
}

void RunController::setStepDelay(int ms) {
    delayMs_ = ms;
    if (ax_) ax_->setStepDelay(ms);
}

bool RunController::start(const QString& entry, const std::vector<fg::Value>& args) {
    detach();
    if (isActive()) stop();
    auto program = doc_->program();
    if (!program || program->hasErrors) {
        emit logMessage(int(fg::LogLevel::Error), tr("Cannot run: the script has compile errors (see Compile Results)."));
        return false;
    }
    ax_.reset();
    clearPending();
    const uint64_t gen = ++generation_;
    fg::AsyncExecutor::Callbacks cb;
    cb.log = [this, gen](fg::LogLevel l, const std::string& m) { reportLog(gen, int(l), m); };
    cb.linkFired = [this, gen](const std::string& g, fg::NodeId from, int out, fg::NodeId to) { reportLink(gen, g, from, out, to); };
    cb.taskState = [this, gen](fg::AsyncExecutor::Handle h, fg::TaskState st) {
        post(gen, [this, h, st] { onTaskState(h, st); });
    };
    cb.completed = [this, gen](const fg::Executor::Completion& c) {
        QStringList rs;
        for (auto& r : c.result.returns) rs << qs(r.toString());
        bool ok = c.result.state == fg::TaskState::Finished;
        bool exited = c.result.exited;
        int code = c.result.exitCode;
        QString err = qs(c.result.error);
        QString returns = rs.join(", ");
        post(gen, [this, ok, exited, code, err, returns] { onCompleted(ok, exited, code, err, returns); });
    };
    ax_ = std::make_unique<fg::AsyncExecutor>(program, cb, &doc_->debugger());
    ax_->setStepDelay(delayMs_);
    pendingTimer_.start();
    emit logMessage(int(fg::LogLevel::Info), "--- running " + entry + " ---");
    frame_ = 0;
    setState(State::Running);
    handle_ = program->events.count(ss(entry)) ? ax_->postEvent(ss(entry), args) : ax_->postFunction(ss(entry), args);
    return true;
}

void RunController::stop() {
    if (attached_) {
        detach();
        return;
    }
    if (!isActive() || !ax_) return;
    ax_->abortAll();
    if (!ax_->waitIdle(2000)) ax_.reset();         // a native function that never returns: abandon the worker
    ++generation_;                                  // ignore whatever the stopped run still reports
    clearPending();
    pendingTimer_.stop();
    setState(State::Idle);
    emit logMessage(int(fg::LogLevel::Info), "--- stopped ---");
    emit finished(false, "stopped");
}

void RunController::pause() {
    if (attached_) {
        if (state_ == State::Running) doc_->debugger().pauseNext();     // the next node any task is about to run is not run
        return;
    }
    if ((state_ == State::Running || state_ == State::Waiting) && ax_) ax_->pause(handle_);
}

void RunController::resume() {
    if (attached_) {
        if (state_ == State::Paused) commandAttached(fg::Debugger::Command::Continue);
        return;
    }
    if (state_ != State::Paused || !ax_) return;
    emit resumed();
    setState(State::Running);
    ax_->resume(handle_);
}

void RunController::step(fg::Debugger::Command c) {
    if (attached_) {
        if (state_ == State::Paused) commandAttached(c);
        return;
    }
    if (state_ != State::Paused || !ax_) return;
    emit resumed();
    setState(State::Running);
    ax_->step(handle_, c);
}

// ---- attached to the executor of the application
bool RunController::attach(std::shared_ptr<fg::AsyncExecutor> ax) {
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
    fg::Debugger* dbg = &doc_->debugger();
    fg::DebugHooks* previous = nullptr;
    // on the worker thread, between two slices: the executor's instance reports to the debugger of the document from now on
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
    pendingTimer_.start();
    setState(State::Running);
    emit attachedChanged(true);
    return true;
}

void RunController::detach() {
    if (!attached_) return;
    std::shared_ptr<fg::AsyncExecutor> ax = std::move(attached_);
    ++generation_;                                  // what the session still reports is ignored
    fg::Debugger* dbg = &doc_->debugger();
    std::vector<fg::AsyncExecutor::Handle> stopped(pausedQueue_.begin(), pausedQueue_.end());
    if (state_ == State::Paused && handle_) stopped.push_back(handle_);
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
    pendingTimer_.stop();
    handle_ = 0;
    frame_ = 0;
    emit resumed();
    setState(State::Idle);
    emit attachedChanged(false);
}

void RunController::showPaused(fg::AsyncExecutor::Handle handle) {
    handle_ = handle;
    QString graph;
    int node = 0;
    if (auto* ax = executor()) ax->inspect([&](fg::Executor& ex) {
        if (auto t = ex.task(handle)) { graph = qs(t->currentGraph()); node = t->currentNode(); }
    });
    frame_ = 0;
    setState(State::Paused);
    emit paused(graph, node);
}

void RunController::commandAttached(fg::Debugger::Command c) {
    const auto h = handle_;
    emit resumed();
    // From now on this task is the one we follow: its next stop (the next node of a step, a breakpoint) or its end decides what is shown next.
    // Other tasks that are stopped wait until then.
    awaiting_ = h;
    handle_ = 0;
    setState(State::Running);
    executor()->inspect([&](fg::Executor& ex) {
        if (auto t = ex.task(h)) {
            doc_->debugger().command(*t, c);
            ex.resume(h);
        }
    });
}

void RunController::onTaskState(fg::AsyncExecutor::Handle handle, fg::TaskState st) {
    drainPending();                                 // what the script printed before it changed state comes first
    if (attached_) {
        const bool followed = handle == awaiting_;
        auto showNext = [this] {                          // the task we followed is out of the way: the next stopped task, if there is one
            awaiting_ = 0;
            if (pausedQueue_.empty()) return;
            const auto next = pausedQueue_.front();
            pausedQueue_.pop_front();
            showPaused(next);
        };
        switch (st) {
        case fg::TaskState::Paused:
            if (followed) {                               // the task we followed stopped again: this is where it is now
                awaiting_ = 0;
                showPaused(handle);
            } else if (state_ != State::Paused && awaiting_ == 0) {
                showPaused(handle);
            } else if (std::find(pausedQueue_.begin(), pausedQueue_.end(), handle) == pausedQueue_.end()) {
                pausedQueue_.push_back(handle);           // another task, while one is shown or followed: it waits its turn
            }
            break;
        case fg::TaskState::Finished:
        case fg::TaskState::Failed:
        case fg::TaskState::Aborted:
        case fg::TaskState::Waiting:                      // a task that waits (Delay...) is not running through its nodes: do not hold the others up
            if (followed) showNext();
            break;
        default: break;
        }
        return;
    }
    switch (st) {
    case fg::TaskState::Paused: {
        QString graph;
        int node = 0;
        if (ax_) ax_->inspect([&](fg::Executor& ex) {
            if (auto t = ex.task(handle_)) { graph = qs(t->currentGraph()); node = t->currentNode(); }
        });
        frame_ = 0;
        setState(State::Paused);
        emit paused(graph, node);
        break;
    }
    case fg::TaskState::Waiting: setState(State::Waiting); break;
    case fg::TaskState::Running: setState(State::Running); break;
    default: break;                       // end states are reported by onCompleted
    }
}

void RunController::onCompleted(bool ok, bool exited, int exitCode, const QString& error, const QString& returns) {
    drainPending();
    pendingTimer_.stop();
    setState(State::Idle);
    if (ok) {
        QString text = "--- finished";
        if (exited) text += ", exit code " + QString::number(exitCode);
        else if (!returns.isEmpty()) text += ", returned " + returns;
        emit logMessage(int(fg::LogLevel::Info), text + " ---");
    } else {
        emit logMessage(int(fg::LogLevel::Error), "--- failed: " + error + " ---");
    }
    emit finished(ok, error);
}

// Another script was loaded (or a new one made): nothing of the previous run - its variables, its call stack - belongs to it.
void RunController::onScriptOpened() {
    if (isActive()) stop();
    ax_.reset();
    ++generation_;
    clearPending();
    pendingTimer_.stop();
    frame_ = 0;
    handle_ = 0;
}

void RunController::onDocumentChanged(bool structural) {
    if (structural && isActive() && !attached_) {            // an attached application keeps running the script it has, whatever is edited
        stop();
        emit logMessage(int(fg::LogLevel::Warning), tr("The script was modified: the run was stopped."));
    }
}

bool RunController::snapshot(DebugSnapshot& out) const {
    if (!executor()) return false;
    const bool paused = state_ == State::Paused;
    const int frameSel = frame_;
    return executor()->inspect([&](fg::Executor& ex) {
        if (paused) {
            if (auto t = ex.task(handle_)) {
                for (auto& e : t->callStack()) out.stack.push_back({qs(e.graph), e.node, e.isEvent});
                if (t->depth() > 0) {
                    size_t idx = t->depth() - 1 - size_t(std::min<int>(frameSel, int(t->depth()) - 1));
                    out.frameVariables = fg::inspectFrame(*t, idx);
                }
            }
        }
        out.globals = fg::inspectGlobals(ex.instance());
    });
}

bool RunController::evaluate(const std::vector<std::string>& expressions, const fg::InspectOptions& opt, std::vector<fg::EvalResult>& out) const {
    if (!executor()) return false;
    const bool paused = state_ == State::Paused;
    const int frameSel = frame_;
    return executor()->inspect([&](fg::Executor& ex) {
        std::shared_ptr<fg::Task> task = paused ? ex.task(handle_) : nullptr;
        size_t idx = 0;
        if (task && task->depth() > 0) idx = task->depth() - 1 - size_t(std::min<int>(frameSel, int(task->depth()) - 1));
        else task = nullptr;
        for (auto& e : expressions) out.push_back(fg::evaluateExpression(ex.instance(), task.get(), idx, e, opt));
    });
}

bool RunController::pinText(const QString& graph, fg::NodeId node, const std::string& pin, QString& out) const {
    if (!executor() || state_ != State::Paused) return false;
    std::string g = ss(graph);
    bool found = false;
    executor()->inspect([&](fg::Executor& ex) {
        auto task = ex.task(handle_);
        if (!task) return;
        for (size_t i = task->depth(); i-- > 0;) {
            if (task->frame(i).graph->name != g) continue;
            fg::Value v;
            if (!task->pinValue(i, node, pin, v)) return;
            const fg::CompiledNode* cn = task->frame(i).graph->findNode(node);
            const fg::TypeDesc* t = nullptr;
            if (cn) {
                int pi = cn->findPinIndex(pin, fg::PinDir::Out);
                if (pi < 0) pi = cn->findPinIndex(pin, fg::PinDir::In);
                if (pi >= 0) t = cn->pins[size_t(pi)].type;
            }
            out = qs(fg::inspect(ex.instance().registry(), v, t).text);
            found = true;
            return;
        }
    });
    return found;
}

} // namespace fged
