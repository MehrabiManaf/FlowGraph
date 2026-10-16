#include "RunController.hpp"
#include "../Graph/Style.hpp"

#include <QMetaObject>

namespace fged {

RunController::RunController(Document* doc, QObject* parent) : QObject(parent), doc_(doc) {
    connect(doc_, &Document::changed, this, [this](bool structural) { onDocumentChanged(structural); });
    pendingTimer_.setInterval(30);
    connect(&pendingTimer_, &QTimer::timeout, this, [this] { drainPending(); });
}

RunController::~RunController() { ax_.reset(); }

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
    if (isActive()) stop();
    auto program = doc_->program();
    if (!program || program->hasErrors) {
        emit logMessage(int(fg::LogLevel::Error), "Cannot run: the script has compile errors (see Compile Results).");
        return false;
    }
    ax_.reset();
    clearPending();
    const uint64_t gen = ++generation_;
    fg::AsyncExecutor::Callbacks cb;
    cb.log = [this, gen](fg::LogLevel l, const std::string& m) { reportLog(gen, int(l), m); };
    cb.linkFired = [this, gen](const std::string& g, fg::NodeId from, int out, fg::NodeId to) { reportLink(gen, g, from, out, to); };
    cb.taskState = [this, gen](fg::AsyncExecutor::Handle, fg::TaskState st) {
        post(gen, [this, st] { onTaskState(st); });
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
    if ((state_ == State::Running || state_ == State::Waiting) && ax_) ax_->pause(handle_);
}

void RunController::resume() {
    if (state_ != State::Paused || !ax_) return;
    emit resumed();
    setState(State::Running);
    ax_->resume(handle_);
}

void RunController::step(fg::Debugger::Command c) {
    if (state_ != State::Paused || !ax_) return;
    emit resumed();
    setState(State::Running);
    ax_->step(handle_, c);
}

void RunController::onTaskState(fg::TaskState st) {
    drainPending();                                 // what the script printed before it changed state comes first
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

void RunController::onDocumentChanged(bool structural) {
    if (structural && isActive()) {
        stop();
        emit logMessage(int(fg::LogLevel::Warning), "The script was modified: the run was stopped.");
    }
}

bool RunController::snapshot(DebugSnapshot& out) const {
    if (!ax_) return false;
    const bool paused = state_ == State::Paused;
    const int frameSel = frame_;
    return ax_->inspect([&](fg::Executor& ex) {
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
    if (!ax_) return false;
    const bool paused = state_ == State::Paused;
    const int frameSel = frame_;
    return ax_->inspect([&](fg::Executor& ex) {
        std::shared_ptr<fg::Task> task = paused ? ex.task(handle_) : nullptr;
        size_t idx = 0;
        if (task && task->depth() > 0) idx = task->depth() - 1 - size_t(std::min<int>(frameSel, int(task->depth()) - 1));
        else task = nullptr;
        for (auto& e : expressions) out.push_back(fg::evaluateExpression(ex.instance(), task.get(), idx, e, opt));
    });
}

bool RunController::pinText(const QString& graph, fg::NodeId node, const std::string& pin, QString& out) const {
    if (!ax_ || state_ != State::Paused) return false;
    std::string g = ss(graph);
    bool found = false;
    ax_->inspect([&](fg::Executor& ex) {
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
