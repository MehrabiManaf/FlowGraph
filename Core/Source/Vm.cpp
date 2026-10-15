#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Vm.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>

namespace fg {

namespace {
thread_local Instance* g_current = nullptr;

struct CurrentScope {
    Instance* prev;
    explicit CurrentScope(Instance* i) : prev(g_current) { g_current = i; }
    ~CurrentScope() { g_current = prev; }
};

// Value buffer that lives on the stack for the common case of a few inputs.
struct InputBuf {
    static constexpr size_t N = 8;
    Value small[N];
    std::vector<Value> big;
    Value* data;
    explicit InputBuf(size_t n) {
        if (n > N) { big.resize(n); data = big.data(); }
        else data = small;
    }
};

const std::string& emptyString() {
    static const std::string s;
    return s;
}
} // namespace

Instance* currentInstance() { return g_current; }

const char* toString(TaskState s) {
    switch (s) {
    case TaskState::Ready: return "Ready";
    case TaskState::Running: return "Running";
    case TaskState::Paused: return "Paused";
    case TaskState::Waiting: return "Waiting";
    case TaskState::Finished: return "Finished";
    case TaskState::Failed: return "Failed";
    case TaskState::Aborted: return "Aborted";
    }
    return "?";
}

// ================================================================== NodeCtx
std::string NodeCtx::inString(size_t i) const {
    const CompiledPin& p = node_->pins[static_cast<size_t>(node_->dataIn[i])];
    return registry().toString(inputs_[i], p.type);
}
void NodeCtx::out(size_t i, Value v) {
    size_t idx = static_cast<size_t>(node_->outBase) + i;
    if (i >= node_->dataOut.size()) throw ScriptError("node '" + node_->title + "' has no output " + std::to_string(i));
    frame_->slotValues[idx] = std::move(v);
}
void NodeCtx::next(size_t execOut) { action_ = Action::Next; pin_ = execOut; }
void NodeCtx::end() { action_ = Action::End; }
void NodeCtx::call(size_t execOut) { action_ = Action::Call; pin_ = execOut; }
void NodeCtx::breakLoop() { action_ = Action::Break; }
void NodeCtx::wait(double seconds) { waiting_ = true; waitSeconds_ = seconds; poll_ = nullptr; }
void NodeCtx::waitFor(std::function<bool(double)> poll) { waiting_ = true; waitSeconds_ = 0; poll_ = std::move(poll); }
const std::string& NodeCtx::prop(const std::string& key) const {
    auto it = node_->props.find(key);
    return it == node_->props.end() ? emptyString() : it->second;
}
const Registry& NodeCtx::registry() const { return inst_->registry(); }
void NodeCtx::log(LogLevel level, const std::string& msg) const { inst_->log(level, msg); }
void NodeCtx::error(const std::string& msg) const { throw ScriptError(msg); }

// ================================================================== Instance
Instance::Instance(std::shared_ptr<const Program> program) : program_(std::move(program)) { resetVariables(); }
Instance::~Instance() {
    hooks_ = nullptr;     // the debugger may already be gone
    abortAll();
}

void Instance::resetVariables() {
    globals_.clear();
    for (auto& v : program_->variables) globals_.push_back(std::make_shared<Value>(v.def.deepCopy()));
}

Value Instance::getVariable(const std::string& name) const {
    int i = program_->variableIndex(name);
    if (i < 0) throw ScriptError("unknown variable '" + name + "'");
    return *globals_[static_cast<size_t>(i)];
}

void Instance::setVariable(const std::string& name, const Value& v) {
    int i = program_->variableIndex(name);
    if (i < 0) throw ScriptError("unknown variable '" + name + "'");
    *globals_[static_cast<size_t>(i)] = registry().convert(v, program_->variables[static_cast<size_t>(i)].type).deepCopy();
}

void Instance::log(LogLevel level, const std::string& msg) const {
    if (log_) { log_(level, msg); return; }
    if (level == LogLevel::Info) std::fprintf(stdout, "%s\n", msg.c_str());
    else std::fprintf(stderr, "[%s] %s\n", level == LogLevel::Warning ? "warning" : "error", msg.c_str());
}

std::shared_ptr<Task> Instance::makeTask(std::string name) {
    std::shared_ptr<Task> t(new Task(*this, nextTaskId_++, std::move(name)));
    t->self_ = t;
    tasks_.push_back(t);
    return t;
}

void Instance::forget(Task* t) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [&](const std::shared_ptr<Task>& p) { return p.get() == t; }), tasks_.end());
}

std::shared_ptr<Task> Instance::startEvent(const std::string& name, std::vector<Value> args, std::string* error) {
    auto it = program_->events.find(name);
    if (it == program_->events.end()) {
        if (error) *error = "Unknown event '" + name + "'";
        return nullptr;
    }
    if (program_->hasErrors) {
        if (error) *error = "The script has compile errors";
        return nullptr;
    }
    auto task = makeTask(name);
    Frame* f = task->pushFrame(it->second.first);
    f->isEvent = true;
    f->pc = it->second.second;
    const CompiledNode& en = f->graph->nodes[static_cast<size_t>(f->pc)];
    try {
        for (size_t i = 0; i < en.dataOut.size(); ++i) {
            const CompiledPin& p = en.pins[static_cast<size_t>(en.dataOut[i])];
            f->slotValues[static_cast<size_t>(en.outBase) + i] = i < args.size() ? registry().convert(args[i], p.type).deepCopy() : registry().defaultValue(p.type);
        }
    } catch (const ScriptError& e) {
        if (error) *error = e.what();
        forget(task.get());
        return nullptr;
    }
    return task;
}

std::shared_ptr<Task> Instance::startFunction(const std::string& name, std::vector<Value> args, std::string* error) {
    auto it = program_->functions.find(name);
    if (it == program_->functions.end()) {
        if (error) *error = "Unknown function '" + name + "'";
        return nullptr;
    }
    if (program_->hasErrors) {
        if (error) *error = "The script has compile errors";
        return nullptr;
    }
    const CompiledGraph& cg = program_->graphs[static_cast<size_t>(it->second)];
    auto task = makeTask(name);
    Frame* f = task->pushFrame(it->second);
    f->pc = cg.entry;
    try {
        for (size_t i = 0; i < cg.inputs.size(); ++i) {
            Value v = i < args.size() ? registry().convert(args[i], cg.inputs[i].type).deepCopy() : registry().defaultValue(cg.inputs[i].type);
            f->params[i] = std::make_shared<Value>(std::move(v));
        }
    } catch (const ScriptError& e) {
        if (error) *error = e.what();
        forget(task.get());
        return nullptr;
    }
    return task;
}

RunResult Instance::callEvent(const std::string& name, std::vector<Value> args) {
    std::string err;
    auto task = startEvent(name, std::move(args), &err);
    if (!task) {
        RunResult r;
        r.state = TaskState::Failed;
        r.error = err;
        return r;
    }
    return task->run();
}

RunResult Instance::callFunction(const std::string& name, std::vector<Value> args) {
    std::string err;
    auto task = startFunction(name, std::move(args), &err);
    if (!task) {
        RunResult r;
        r.state = TaskState::Failed;
        r.error = err;
        return r;
    }
    return task->run();
}

void Instance::tick(double dt) {
    auto copy = tasks_;
    for (auto& t : copy)
        if (t->state() == TaskState::Waiting && t->advanceWait(dt)) t->run();
}

void Instance::abortAll() {
    auto copy = tasks_;
    for (auto& t : copy) t->abort();
    tasks_.clear();
}

// ================================================================== Task
Task::Task(Instance& inst, int id, std::string name) : inst_(&inst), id_(id), name_(std::move(name)) {}
Task::~Task() = default;

Frame* Task::pushFrame(int graphIndex) {
    std::unique_ptr<Frame> fp;
    if (!pool_.empty()) { fp = std::move(pool_.back()); pool_.pop_back(); }
    else fp = std::make_unique<Frame>();
    Frame& f = *fp;
    const Program& prog = inst_->program();
    const CompiledGraph& cg = prog.graphs[static_cast<size_t>(graphIndex)];
    f.graphIndex = graphIndex;
    f.graph = &cg;
    f.slotValues.resize(static_cast<size_t>(cg.slotCount));
    for (size_t i = 0; i < f.slotValues.size(); ++i) {
        const TypeDesc* t = cg.slotTypes[i];
        f.slotValues[i] = t ? prog.registry->defaultValue(t) : Value();
    }
    f.stamps.assign(cg.nodes.size(), 0);
    f.epoch = 1;
    f.params.assign(cg.inputs.size(), nullptr);
    f.locals.clear();
    for (auto& l : cg.locals) f.locals.push_back(std::make_shared<Value>(l.def.deepCopy()));
    f.conts.clear();
    f.results.clear();
    f.pc = -1;
    f.callerNode = -1;
    f.isEvent = false;
    f.returned = false;
    f.resuming = false;
    frames_.push_back(std::move(fp));
    return &f;
}

void Task::popFrame() {
    auto fp = std::move(frames_.back());
    frames_.pop_back();
    fp->graph = nullptr;
    if (pool_.size() < 16) pool_.push_back(std::move(fp));
}

void Task::fail(const std::string& msg, const CompiledNode* at) {
    state_ = TaskState::Failed;
    error_ = msg;
    errorGraph_ = frames_.empty() ? std::string() : frames_.back()->graph->name;
    errorNode_ = at ? at->id : 0;
    std::string where = errorGraph_.empty() ? "" : "[" + errorGraph_ + (at ? " #" + std::to_string(at->id) + " " + at->title : "") + "] ";
    inst_->log(LogLevel::Error, where + msg);
}

RunResult Task::makeResult() {
    RunResult r;
    r.state = state_;
    r.ok = state_ == TaskState::Finished;
    r.error = error_;
    r.errorGraph = errorGraph_;
    r.errorNode = errorNode_;
    r.returns = results_;
    r.refResults = refResults_;
    r.task = self_.lock();
    return r;
}

void Task::abort() {
    if (done()) return;
    state_ = TaskState::Aborted;
    error_ = "Execution aborted";
    waitPoll_ = nullptr;
    if (auto* h = inst_->debugHooks()) h->onTaskStateChanged(*this);
}

bool Task::advanceWait(double dt) {
    if (state_ != TaskState::Waiting) return false;
    bool ready;
    if (waitPoll_) {
        ready = waitPoll_(dt);
        if (ready) waitPoll_ = nullptr;
    } else {
        waitRemaining_ -= dt;
        ready = waitRemaining_ <= 0;
    }
    if (ready) state_ = TaskState::Ready;
    return ready;
}

RunResult Task::run(uint64_t maxNodes) {
    if (done() || state_ == TaskState::Waiting || state_ == TaskState::Running) return makeResult();
    CurrentScope scope(inst_);
    auto keepAlive = self_.lock();
    DebugHooks* hooks = inst_->debugHooks();
    state_ = TaskState::Running;
    const bool explicitLimit = maxNodes != 0;
    const uint64_t limit = explicitLimit ? maxNodes : inst_->stepLimit_;
    uint64_t count = 0;
    try {
        while (true) {
            if (inst_->abort_) {
                state_ = TaskState::Aborted;
                error_ = "Execution aborted";
                break;
            }
            if (frames_.empty()) {
                state_ = TaskState::Finished;
                break;
            }
            Frame& f = *frames_.back();
            if (f.pc < 0) {
                if (!f.conts.empty()) {
                    f.resumeCont = std::move(f.conts.back());
                    f.conts.pop_back();
                    f.pc = f.resumeCont.node;
                    f.resuming = true;
                    continue;
                }
                finishFrame();
                continue;
            }
            const CompiledNode& n = f.graph->nodes[static_cast<size_t>(f.pc)];
            if (count >= limit) {
                if (explicitLimit) state_ = TaskState::Ready;
                else fail("Step limit exceeded (infinite loop?)", &n);
                break;
            }
            if (hooks && !f.resuming) {
                if (!skipHook_ && hooks->shouldBreak(*this, *f.graph, n)) {
                    state_ = TaskState::Paused;
                    skipHook_ = true;
                    break;
                }
            }
            skipHook_ = false;
            ++count;
            executeNode(f, n);
            if (state_ != TaskState::Running) break;
        }
    } catch (const AbortError&) {
        state_ = TaskState::Aborted;
        error_ = "Execution aborted";
    } catch (const ScriptError& e) {
        fail(e.what(), active_);
    } catch (const std::exception& e) {
        fail(std::string("internal error: ") + e.what(), active_);
    }
    if (done()) inst_->forget(this);
    if (hooks && state_ != TaskState::Ready) hooks->onTaskStateChanged(*this);
    return makeResult();
}

void Task::follow(Frame& f, const CompiledNode& n, size_t execOut) {
    int target = execOut < n.execTarget.size() ? n.execTarget[execOut] : -1;
    f.pc = target;
    if (target >= 0)
        if (auto* h = inst_->debugHooks()) h->onExecLink(*this, *f.graph, n.index, static_cast<int>(execOut), target);
}

Cell Task::cellFor(Frame& f, const CompiledNode& var) {
    if (var.resolved < 0) throw ScriptError("unknown variable '" + var.prop("name") + "'");
    size_t i = static_cast<size_t>(var.resolved);
    switch (var.scope) {
    case VarScope::Global: return inst_->globals_[i];
    case VarScope::Local: return f.locals[i];
    case VarScope::Param:
        if (!f.params[i]) f.params[i] = std::make_shared<Value>();
        return f.params[i];
    }
    return nullptr;
}

const CompiledNode* Task::originNode(const Frame& f, const CompiledNode& n, size_t ordinal) const {
    const CompiledNode* cur = &n;
    size_t ord = ordinal;
    for (int guard = 0; guard < 64; ++guard) {
        if (ord >= cur->inputs.size()) return nullptr;
        const DataSource& ds = cur->inputs[ord];
        if (ds.kind == DataSource::Kind::Default) return nullptr;
        const CompiledNode& src = f.graph->nodes[static_cast<size_t>(ds.node)];
        if (src.op == NodeOp::Reroute || (src.def && src.def->aliasesFirstInput)) { cur = &src; ord = 0; continue; }
        return &src;
    }
    return nullptr;
}

Value Task::evalInput(Frame& f, const CompiledNode& n, size_t ordinal) {
    const DataSource& ds = n.inputs[ordinal];
    Value v;
    switch (ds.kind) {
    case DataSource::Kind::Default: {
        const Value& d = n.pins[static_cast<size_t>(n.dataIn[ordinal])].def;
        return d.deepCopy();
    }
    case DataSource::Kind::Slot:
        if (ds.pure) evalPure(f, f.graph->nodes[static_cast<size_t>(ds.node)]);
        v = f.slotValues[static_cast<size_t>(ds.slot)];
        break;
    case DataSource::Kind::Param: {
        const Cell& c = f.params[static_cast<size_t>(ds.slot)];
        if (c) v = *c;
        break;
    }
    }
    if (ds.convert) v = inst_->registry().convertFrom(v, ds.from, ds.to);
    return v;
}

void Task::gatherInputs(Frame& f, const CompiledNode& n, Value* buf) {
    for (size_t i = 0; i < n.dataIn.size(); ++i) buf[i] = evalInput(f, n, i);
}

void Task::initCtx(NodeCtx& ctx, Frame& f, const CompiledNode& n, Value* in, const Continuation* rc) {
    ctx.inst_ = inst_;
    ctx.task_ = this;
    ctx.frame_ = &f;
    ctx.node_ = &n;
    ctx.inputs_ = in;
    ctx.inCount_ = n.dataIn.size();
    if (rc) {
        ctx.resumed_ = true;
        ctx.state_ = rc->state;
        ctx.aux_ = rc->aux;
    }
}

void Task::evalPure(Frame& f, const CompiledNode& n) {
    if (f.stamps[static_cast<size_t>(n.index)] == f.epoch) return;
    if (++evalDepth_ > 1000) {
        evalDepth_ = 0;
        throw ScriptError("expression is nested too deeply");
    }
    struct Guard {
        Task* t;
        const CompiledNode* prev;
        int unwinding = std::uncaught_exceptions();
        ~Guard() {
            --t->evalDepth_;
            if (std::uncaught_exceptions() == unwinding) t->active_ = prev;   // keep the culprit when an error propagates
        }
    } guard{this, active_};
    active_ = &n;
    if (!n.def) throw ScriptError("unknown node '" + n.defId + "'");
    InputBuf buf(n.dataIn.size());
    gatherInputs(f, n, buf.data);
    switch (n.op) {
    case NodeOp::GetVariable: {
        Cell c = cellFor(f, n);
        f.slotValues[static_cast<size_t>(n.outBase)] = *c;
        break;
    }
    case NodeOp::Reroute:
        f.slotValues[static_cast<size_t>(n.outBase)] = buf.data[0];
        break;
    default:
        if (n.def->run) {
            NodeCtx ctx;
            initCtx(ctx, f, n, buf.data, nullptr);
            n.def->run(ctx);
        }
        break;
    }
    f.stamps[static_cast<size_t>(n.index)] = f.epoch;
}

void Task::executeNode(Frame& f, const CompiledNode& n) {
    active_ = &n;
    if (++f.epoch == 0) { std::fill(f.stamps.begin(), f.stamps.end(), 0u); f.epoch = 1; }
    bool resumed = f.resuming;
    Continuation rc;
    if (resumed) {
        rc = std::move(f.resumeCont);
        f.resuming = false;
    }
    if (!n.def) throw ScriptError("unknown node '" + n.defId + "'");

    switch (n.op) {
    case NodeOp::Event:
    case NodeOp::FunctionEntry:
    case NodeOp::GetVariable:
    case NodeOp::Reroute:
        follow(f, n, 0);
        return;
    case NodeOp::Return: {
        InputBuf buf(n.dataIn.size());
        gatherInputs(f, n, buf.data);
        f.results.clear();
        for (size_t i = 0; i < n.dataIn.size(); ++i) f.results.push_back(buf.data[i].deepCopy());
        f.returned = true;
        f.conts.clear();
        f.pc = -1;
        return;
    }
    case NodeOp::SetVariable: {
        InputBuf buf(n.dataIn.size());
        gatherInputs(f, n, buf.data);
        Cell c = cellFor(f, n);
        *c = buf.data[0].deepCopy();
        follow(f, n, 0);
        return;
    }
    case NodeOp::CallFunction:
    case NodeOp::CallEvent:
        callGraph(f, n);
        return;
    case NodeOp::Native:
        break;
    }

    InputBuf buf(n.dataIn.size());
    gatherInputs(f, n, buf.data);
    NodeCtx ctx;
    initCtx(ctx, f, n, buf.data, resumed ? &rc : nullptr);
    if (n.def->run) n.def->run(ctx);

    using A = NodeCtx::Action;
    switch (ctx.action_) {
    case A::Default:
    case A::Wait:
        follow(f, n, 0);
        break;
    case A::Next: follow(f, n, ctx.pin_); break;
    case A::End: f.pc = -1; break;
    case A::Call: {
        Continuation c;
        c.node = n.index;
        c.state = ctx.state_;
        c.aux = std::move(ctx.aux_);
        c.loop = n.def->breakPin >= 0;
        f.conts.push_back(std::move(c));
        follow(f, n, ctx.pin_);
        break;
    }
    case A::Break: {
        bool found = false;
        while (!f.conts.empty()) {
            Continuation c = std::move(f.conts.back());
            f.conts.pop_back();
            if (c.loop) {
                const CompiledNode& loopNode = f.graph->nodes[static_cast<size_t>(c.node)];
                follow(f, loopNode, static_cast<size_t>(loopNode.def->breakPin));
                found = true;
                break;
            }
        }
        if (!found) throw ScriptError("'Break' used outside of a loop");
        break;
    }
    }
    if (ctx.waiting_) {
        state_ = TaskState::Waiting;
        waitRemaining_ = ctx.waitSeconds_;
        waitPoll_ = std::move(ctx.poll_);
    }
}

void Task::callGraph(Frame& f, const CompiledNode& n) {
    if (frames_.size() >= inst_->maxDepth_) throw ScriptError("call stack overflow (recursion too deep?)");
    const Program& prog = inst_->program();
    const bool isEvent = n.op == NodeOp::CallEvent;
    int gi = -1, entryNode = -1;
    if (isEvent) {
        auto it = prog.events.find(n.prop("event"));
        if (it == prog.events.end()) throw ScriptError("unknown event '" + n.prop("event") + "'");
        gi = it->second.first;
        entryNode = it->second.second;
    } else {
        gi = n.resolved;
        if (gi < 0) throw ScriptError("unknown function '" + n.prop("function") + "'");
    }
    const CompiledGraph& cg = prog.graphs[static_cast<size_t>(gi)];

    std::vector<Cell> cells;
    std::vector<Value> values;
    if (isEvent) {
        for (size_t i = 0; i < n.dataIn.size(); ++i) values.push_back(evalInput(f, n, i).deepCopy());
    } else {
        cells.resize(n.dataIn.size());
        for (size_t i = 0; i < n.dataIn.size(); ++i) {
            const CompiledPin& p = n.pins[static_cast<size_t>(n.dataIn[i])];
            if (p.byRef) {
                const CompiledNode* src = originNode(f, n, i);
                if (src && src->op == NodeOp::GetVariable) {
                    cells[i] = cellFor(f, *src);
                    continue;
                }
            }
            Value v = evalInput(f, n, i);
            cells[i] = std::make_shared<Value>(p.isConst ? std::move(v) : v.deepCopy());
        }
    }

    follow(f, n, 0);                       // the caller continues here once the callee is done
    Frame* nf = pushFrame(gi);
    nf->callerNode = n.index;
    if (isEvent) {
        nf->isEvent = true;
        nf->pc = entryNode;
        const CompiledNode& en = cg.nodes[static_cast<size_t>(entryNode)];
        for (size_t i = 0; i < en.dataOut.size() && i < values.size(); ++i)
            nf->slotValues[static_cast<size_t>(en.outBase) + i] = values[i];
    } else {
        nf->params = std::move(cells);
        nf->pc = cg.entry;
    }
}

void Task::finishFrame() {
    Frame& f = *frames_.back();
    const CompiledGraph& cg = *f.graph;
    const Registry& reg = inst_->registry();
    if (cg.kind == GraphKind::Function) {
        if (f.results.size() < cg.outputs.size()) {
            f.results.clear();
            for (auto& o : cg.outputs) f.results.push_back(reg.defaultValue(o.type));
        }
    }
    if (frames_.size() == 1) {
        results_ = f.results;
        refResults_.assign(cg.inputs.size(), Value());
        for (size_t i = 0; i < cg.inputs.size(); ++i)
            if (cg.inputs[i].decl.byRef && f.params[i]) refResults_[i] = *f.params[i];
    } else if (cg.kind == GraphKind::Function) {
        Frame& caller = *frames_[frames_.size() - 2];
        const CompiledNode& callNode = caller.graph->nodes[static_cast<size_t>(f.callerNode)];
        for (size_t i = 0; i < callNode.dataOut.size() && i < f.results.size(); ++i)
            caller.slotValues[static_cast<size_t>(callNode.outBase) + i] = f.results[i];
    }
    popFrame();
}

// ------------------------------------------------------------------ inspection
std::vector<StackEntry> Task::callStack() const {
    std::vector<StackEntry> out;
    for (size_t i = frames_.size(); i-- > 0;) {
        const Frame& f = *frames_[i];
        StackEntry e;
        e.graph = f.graph->name;
        e.graphIndex = f.graphIndex;
        e.isEvent = f.isEvent;
        if (i + 1 < frames_.size()) {
            int cn = frames_[i + 1]->callerNode;
            e.node = cn >= 0 ? f.graph->nodes[static_cast<size_t>(cn)].id : 0;
        } else {
            e.node = f.pc >= 0 ? f.graph->nodes[static_cast<size_t>(f.pc)].id : 0;
        }
        out.push_back(std::move(e));
    }
    return out;
}

NodeId Task::currentNode() const {
    if (frames_.empty()) return 0;
    const Frame& f = *frames_.back();
    return f.pc >= 0 ? f.graph->nodes[static_cast<size_t>(f.pc)].id : 0;
}

const std::string& Task::currentGraph() const {
    return frames_.empty() ? emptyString() : frames_.back()->graph->name;
}

bool Task::pinValue(size_t frameIndex, NodeId node, const std::string& pin, Value& out) const {
    if (frameIndex >= frames_.size()) return false;
    const Frame& f = *frames_[frameIndex];
    const CompiledNode* cn = f.graph->findNode(node);
    if (!cn) return false;
    for (size_t pi = 0; pi < cn->pins.size(); ++pi) {
        const CompiledPin& p = cn->pins[pi];
        if (p.name != pin || p.kind != PinKind::Data) continue;
        size_t ord = static_cast<size_t>(p.ordinal);
        if (p.dir == PinDir::Out) {
            size_t slot = static_cast<size_t>(cn->outBase) + ord;
            if (slot >= f.slotValues.size()) return false;
            out = f.slotValues[slot];
            return true;
        }
        const DataSource& ds = cn->inputs[ord];
        switch (ds.kind) {
        case DataSource::Kind::Default: out = p.def; return true;
        case DataSource::Kind::Slot: out = f.slotValues[static_cast<size_t>(ds.slot)]; return true;
        case DataSource::Kind::Param:
            if (!f.params[static_cast<size_t>(ds.slot)]) return false;
            out = *f.params[static_cast<size_t>(ds.slot)];
            return true;
        }
    }
    return false;
}

// ================================================================== Debugger
void Debugger::setBreakpoint(const std::string& graph, NodeId node, bool on) {
    if (on) breakpoints_.insert({graph, node});
    else breakpoints_.erase({graph, node});
}
bool Debugger::hasBreakpoint(const std::string& graph, NodeId node) const { return breakpoints_.count({graph, node}) > 0; }

void Debugger::command(Task& task, Command cmd) {
    if (cmd == Command::Continue) steps_.erase(&task);
    else steps_[&task] = Step{cmd, task.depth()};
}

bool Debugger::shouldBreak(Task& task, const CompiledGraph& graph, const CompiledNode& node) {
    if (pauseAll_) {
        pauseAll_ = false;
        return true;
    }
    auto it = steps_.find(&task);
    if (it != steps_.end()) {
        Step s = it->second;
        bool hit = false;
        switch (s.cmd) {
        case Command::StepInto:
        case Command::Pause: hit = true; break;
        case Command::StepOver: hit = task.depth() <= s.depth; break;
        case Command::StepOut: hit = task.depth() < s.depth; break;
        case Command::Continue: break;
        }
        if (hit) {
            steps_.erase(it);
            return true;
        }
    }
    return breakpoints_.count({graph.name, node.id}) > 0;
}

void Debugger::onExecLink(Task&, const CompiledGraph& graph, int fromNode, int execOutOrdinal, int toNode) {
    if (linkFired) linkFired(graph.name, graph.nodes[static_cast<size_t>(fromNode)].id, execOutOrdinal, graph.nodes[static_cast<size_t>(toNode)].id);
}

void Debugger::onTaskStateChanged(Task& task) {
    if (task.done()) steps_.erase(&task);
    if (taskStateChanged) taskStateChanged(task);
}

} // namespace fg
