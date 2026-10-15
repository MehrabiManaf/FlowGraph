#include "RunController.hpp"
#include "../Graph/Style.hpp"

namespace fged {

RunController::RunController(Document* doc, QObject* parent) : QObject(parent), doc_(doc) {
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    connect(doc_, &Document::changed, this, [this](bool structural) { onDocumentChanged(structural); });
    fg::Debugger& dbg = doc_->debugger();
    dbg.linkFired = [this](const std::string& g, fg::NodeId from, int out, fg::NodeId to) {
        emit linkFired(qs(g), from, out, to);
    };
}

std::vector<RunController::Entry> RunController::entries() const {
    std::vector<Entry> out;
    auto prog = doc_->program();
    if (!prog) return out;
    const fg::Script& script = doc_->script();
    for (auto& [name, loc] : prog->events) {
        Entry e;
        e.name = qs(name);
        e.isEvent = true;
        const fg::CompiledNode& n = prog->graphs[size_t(loc.first)].nodes[size_t(loc.second)];
        QStringList sig;
        for (int pi : n.dataOut) {
            const fg::CompiledPin& p = n.pins[size_t(pi)];
            e.params.push_back({p.name, p.type ? p.type->name : "any", false, false, fg::Value()});
            sig << qs(p.name) + ": " + (p.type ? qs(p.type->name) : "any");
        }
        e.signature = e.name + "(" + sig.join(", ") + ")";
        out.push_back(std::move(e));
    }
    for (auto& g : script.graphs) {
        if (g.kind != fg::GraphKind::Function) continue;
        Entry e;
        e.name = qs(g.name);
        e.isEvent = false;
        e.params = g.inputs;
        QStringList sig;
        for (auto& p : g.inputs) sig << qs(p.name) + ": " + qs(p.type);
        e.signature = e.name + "(" + sig.join(", ") + ")";
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isEvent != b.isEvent) return a.isEvent;
        return a.name < b.name;
    });
    return out;
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
    default: return reg.defaultValue(type);
    }
}

void RunController::setState(State s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

bool RunController::start(const QString& entry, const std::vector<fg::Value>& args) {
    if (isActive()) stop();
    program_ = doc_->program();
    if (!program_ || program_->hasErrors) {
        emit logMessage(int(fg::LogLevel::Error), "Cannot run: the script has compile errors (see Compile Results).");
        return false;
    }
    instance_ = std::make_shared<fg::Instance>(program_);
    instance_->setLogHandler([this](fg::LogLevel l, const std::string& m) { emit logMessage(int(l), qs(m)); });
    instance_->setDebugHooks(&doc_->debugger());
    doc_->debugger().taskStateChanged = nullptr;
    std::string err;
    std::string name = ss(entry);
    task_ = program_->events.count(name) ? instance_->startEvent(name, args, &err) : instance_->startFunction(name, args, &err);
    if (!task_) {
        emit logMessage(int(fg::LogLevel::Error), "Cannot start '" + entry + "': " + qs(err));
        instance_.reset();
        return false;
    }
    emit logMessage(int(fg::LogLevel::Info), "--- running " + entry + " ---");
    frame_ = 0;
    pendingPause_ = false;
    clock_.restart();
    setState(State::Running);
    timer_.start(delayMs_ > 0 ? delayMs_ : 0);
    return true;
}

void RunController::stop() {
    if (!isActive()) return;
    timer_.stop();
    if (task_) task_->abort();
    if (instance_) instance_->abortAll();
    setState(State::Idle);
    emit logMessage(int(fg::LogLevel::Info), "--- stopped ---");
    emit finished(false, "stopped");
}

void RunController::pause() {
    if (state_ == State::Running || state_ == State::Waiting) {
        doc_->debugger().command(*task_, fg::Debugger::Command::Pause);
        pendingPause_ = true;
    }
}

void RunController::resume() {
    if (state_ != State::Paused) return;
    doc_->debugger().command(*task_, fg::Debugger::Command::Continue);
    emit resumed();
    setState(State::Running);
    clock_.restart();
    timer_.start(delayMs_ > 0 ? delayMs_ : 0);
}

void RunController::step(fg::Debugger::Command c) {
    if (state_ != State::Paused) return;
    doc_->debugger().command(*task_, c);
    emit resumed();
    setState(State::Running);
    clock_.restart();
    timer_.start(0);
}

void RunController::tick() {
    if (!task_) { timer_.stop(); return; }
    double dt = clock_.restart() / 1000.0;
    fg::TaskState st = task_->state();
    if (st == fg::TaskState::Waiting) {
        setState(State::Waiting);
        timer_.setInterval(16);
        instance_->tick(dt);      // runs the task when the wait is over
        st = task_->state();
    } else if (st == fg::TaskState::Ready || st == fg::TaskState::Paused) {
        setState(State::Running);
        task_->run(delayMs_ > 0 ? 1 : 4000);
        st = task_->state();
    }
    switch (st) {
    case fg::TaskState::Ready:
        timer_.setInterval(delayMs_ > 0 ? delayMs_ : 0);
        break;
    case fg::TaskState::Running: break;
    case fg::TaskState::Waiting:
        setState(State::Waiting);
        timer_.setInterval(16);
        break;
    case fg::TaskState::Paused: {
        timer_.stop();
        frame_ = 0;
        setState(State::Paused);
        emit paused(qs(task_->currentGraph()), task_->currentNode());
        break;
    }
    case fg::TaskState::Finished:
    case fg::TaskState::Failed:
    case fg::TaskState::Aborted: finishRun(); break;
    }
}

void RunController::finishRun() {
    timer_.stop();
    bool ok = task_ && task_->state() == fg::TaskState::Finished;
    QString msg = task_ ? qs(task_->error()) : QString();
    std::vector<fg::Value> results = task_ ? task_->results() : std::vector<fg::Value>();
    QStringList rs;
    for (auto& r : results) rs << qs(r.toString());
    setState(State::Idle);
    if (ok) emit logMessage(int(fg::LogLevel::Info), rs.isEmpty() ? "--- finished ---" : "--- finished, returned " + rs.join(", ") + " ---");
    else emit logMessage(int(fg::LogLevel::Error), "--- failed: " + msg + " ---");
    emit finished(ok, msg);
}

void RunController::onDocumentChanged(bool structural) {
    if (structural && isActive()) {
        timer_.stop();
        if (task_) task_->abort();
        if (instance_) instance_->abortAll();
        setState(State::Idle);
        emit logMessage(int(fg::LogLevel::Warning), "The script was modified: the run was stopped.");
        emit finished(false, "script modified");
    }
}

bool RunController::pinText(const QString& graph, fg::NodeId node, const std::string& pin, QString& out) const {
    if (!task_ || state_ != State::Paused) return false;
    std::string g = ss(graph);
    for (size_t i = task_->depth(); i-- > 0;) {
        if (task_->frame(i).graph->name != g) continue;
        fg::Value v;
        if (!task_->pinValue(i, node, pin, v)) return false;
        const fg::CompiledNode* cn = task_->frame(i).graph->findNode(node);
        const fg::TypeDesc* t = nullptr;
        if (cn) {
            int pi = cn->findPinIndex(pin, fg::PinDir::Out);
            if (pi < 0) pi = cn->findPinIndex(pin, fg::PinDir::In);
            if (pi >= 0) t = cn->pins[size_t(pi)].type;
        }
        out = qs(fg::inspect(instance_->registry(), v, t).text);
        return true;
    }
    return false;
}

} // namespace fged
