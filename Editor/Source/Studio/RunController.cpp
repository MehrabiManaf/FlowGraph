#include "RunController.hpp"
#include "../Graph/Style.hpp"

#include <QMetaObject>

namespace fged {

RunController::RunController(Document* doc, QObject* parent) : QObject(parent), doc_(doc) {
    connect(doc_, &Document::changed, this, [this](bool structural) { onDocumentChanged(structural); });
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
    const uint64_t gen = ++generation_;
    fg::AsyncExecutor::Callbacks cb;
    cb.log = [this, gen](fg::LogLevel l, const std::string& m) {
        QString text = qs(m);
        post(gen, [this, l, text] { emit logMessage(int(l), text); });
    };
    cb.linkFired = [this, gen](const std::string& g, fg::NodeId from, int out, fg::NodeId to) {
        QString graph = qs(g);
        post(gen, [this, graph, from, out, to] { emit linkFired(graph, from, out, to); });
    };
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
