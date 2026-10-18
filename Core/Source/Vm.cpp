#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Vm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <new>
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

// Value buffer for the inputs of one node: lives on the stack, and only the values that are needed are constructed.
struct InputBuf {
    static constexpr size_t N = 8;
    alignas(Value) unsigned char raw[N * sizeof(Value)];
    Value* data;
    size_t count;
    explicit InputBuf(size_t n) : count(n) {
        data = n > N ? static_cast<Value*>(::operator new(n * sizeof(Value))) : reinterpret_cast<Value*>(raw);
        for (size_t i = 0; i < n; ++i) new (data + i) Value();
    }
    ~InputBuf() {
        for (size_t i = 0; i < count; ++i) data[i].~Value();
        if (count > N) ::operator delete(data);
    }
    InputBuf(const InputBuf&) = delete;
    InputBuf& operator=(const InputBuf&) = delete;
};

// Cold paths of the interpreter loop. They are separate functions so that the hot ones hold no std::string temporaries.
[[noreturn]] FG_NOINLINE void throwUnknownNode(const CompiledNode& n) { throw ScriptError("unknown node '" + n.defId + "'"); }
[[noreturn]] FG_NOINLINE void throwUnknownVariable(const CompiledNode& var) { throw ScriptError("unknown variable '" + var.prop("name") + "'"); }
[[noreturn]] FG_NOINLINE void throwDivisionByZero() { throw ScriptError("division by zero"); }
[[noreturn]] FG_NOINLINE void throwTooDeep() { throw ScriptError("expression is nested too deeply"); }
[[noreturn]] FG_NOINLINE void throwArrayNull() { throw ScriptError("array is null"); }
[[noreturn]] FG_NOINLINE void throwMapNull() { throw ScriptError("map is null"); }
[[noreturn]] FG_NOINLINE void throwBadIndex(int64_t idx, size_t size) {
    throw ScriptError("index " + std::to_string(idx) + " is out of range (size " + std::to_string(size) + ")");
}
[[noreturn]] FG_NOINLINE void throwBreakOutsideLoop() { throw ScriptError("'Break' used outside of a loop"); }

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
void NodeCtx::outOfRange(size_t i) const { throw ScriptError("node '" + node_->title + "' has no output " + std::to_string(i)); }
void NodeCtx::next(size_t execOut) { action_ = Action::Next; pin_ = execOut; }
void NodeCtx::end() { action_ = Action::End; }
void NodeCtx::call(size_t execOut) { action_ = Action::Call; pin_ = execOut; }
void NodeCtx::breakLoop() { action_ = Action::Break; }
void NodeCtx::wait(double seconds) { waiting_ = true; resumeAfterWait_ = false; waitSeconds_ = seconds; poll_.reset(); }
void NodeCtx::exit(int code) { exit_ = true; exitCode_ = code; }
size_t NodeCtx::entryPin() const {
    if (resumed_) return 0;
    const int pin = frame_->entryPin;
    if (pin < 0 || static_cast<size_t>(pin) >= node_->pins.size()) return 0;
    const CompiledPin& p = node_->pins[static_cast<size_t>(pin)];
    return p.kind == PinKind::Exec && p.dir == PinDir::In ? static_cast<size_t>(p.ordinal) : 0;
}
void NodeCtx::checkLimit(const Value& v) const { inst_->checkLimits(v); }

void Instance::containerTooBig(size_t size) const {
    throw ScriptError("a container would have " + std::to_string(size) + " elements; this application allows " +
                      std::to_string(limits_.maxContainerSize));
}

void Instance::checkLimits(const Value& v) const {
    if (limits_.maxStringLength && v.isString() && v.str().size() > limits_.maxStringLength)
        throw ScriptError("a string of " + std::to_string(v.str().size()) + " bytes; this application allows " + std::to_string(limits_.maxStringLength));
    if (limits_.maxContainerSize) {
        if (auto* a = v.arrayPtr()) checkContainerSize(a->items.size());
        else if (auto* m = v.mapPtr()) checkContainerSize(m->items.size());
    }
}

Value& NodeCtx::nodeState() const {
    return inst_->nodeState_[(static_cast<uint64_t>(static_cast<uint32_t>(frame_->graphIndex)) << 32) | static_cast<uint32_t>(node_->index)];
}
void NodeCtx::waitFor(std::function<bool(double)> poll) { waiting_ = true; resumeAfterWait_ = false; waitSeconds_ = 0; poll_ = std::make_unique<std::function<bool(double)>>(std::move(poll)); }
void NodeCtx::waitThenResume(std::function<bool(double)> poll) {
    waitFor(std::move(poll));
    resumeAfterWait_ = true;
}
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
    nodeState_.clear();
}

std::string ReloadReport::toText() const {
    if (!ok()) return "not reloaded: " + error;
    auto list = [](const char* what, const std::vector<std::string>& names) {
        if (names.empty()) return std::string();
        std::string s = std::string(what) + ": ";
        for (size_t i = 0; i < names.size(); ++i) s += (i ? ", " : "") + names[i];
        return s + "\n";
    };
    std::string s = "reloaded\n" + list("kept", kept) + list("converted", converted) + list("reset (type changed)", reset) +
                    list("added", added) + list("removed", removed);
    if (abortedTasks) s += std::to_string(abortedTasks) + " waiting / paused task(s) aborted\n";
    return s;
}

ReloadReport Instance::reload(std::shared_ptr<const Program> program) {
    ReloadReport rep;
    if (!program) { rep.error = "no program"; return rep; }
    if (program->hasErrors) { rep.error = "the program has compile errors"; return rep; }
    if (program->registry != program_->registry) { rep.error = "the program was compiled against another registry"; return rep; }
    if (currentInstance() == this) { rep.error = "a script of this instance is running"; return rep; }

    // Tasks of the old program: their frames refer to its graphs. They are aborted; each keeps the old program alive for inspection.
    rep.abortedTasks = tasks_.size();
    abortAll();
    spareFrames_.clear();

    const Program& from = *program_;
    const Registry& reg = *program->registry;
    std::vector<Cell> globals;
    globals.reserve(program->variables.size());
    for (auto& nv : program->variables) {
        const std::string& name = nv.decl.name;
        const int old = from.variableIndex(name);
        if (old < 0) {
            globals.push_back(std::make_shared<Value>(nv.def.deepCopy()));
            rep.added.push_back(name);
            continue;
        }
        const CompiledVariable& ov = from.variables[static_cast<size_t>(old)];
        const Value& value = *globals_[static_cast<size_t>(old)];
        if (ov.type == nv.type) {
            globals.push_back(globals_[static_cast<size_t>(old)]);      // the same cell: hosts that hold it see the same value
            rep.kept.push_back(name);
            continue;
        }
        try {
            globals.push_back(std::make_shared<Value>(reg.convertFrom(value, ov.type, nv.type)));
            rep.converted.push_back(name);
        } catch (const ScriptError&) {
            globals.push_back(std::make_shared<Value>(nv.def.deepCopy()));
            rep.reset.push_back(name);
        }
    }
    for (auto& ov : from.variables)
        if (program->variableIndex(ov.decl.name) < 0) rep.removed.push_back(ov.decl.name);

    // Node state (Do Once, Gate...): found again by graph name and node id, when the node is still of the same kind.
    std::unordered_map<uint64_t, Value> state;
    for (auto& [key, v] : nodeState_) {
        const size_t gi = static_cast<size_t>(key >> 32), ni = static_cast<size_t>(key & 0xffffffffu);
        if (gi >= from.graphs.size() || ni >= from.graphs[gi].nodes.size()) continue;
        const CompiledNode& on = from.graphs[gi].nodes[ni];
        const int ngi = program->graphIndex(from.graphs[gi].name);
        if (ngi < 0) continue;
        const CompiledNode* nn = program->graphs[static_cast<size_t>(ngi)].findNode(on.id);
        if (!nn || nn->defId != on.defId) continue;
        state[(static_cast<uint64_t>(static_cast<uint32_t>(ngi)) << 32) | static_cast<uint32_t>(nn->index)] = v;
    }

    program_ = std::move(program);
    globals_ = std::move(globals);
    nodeState_ = std::move(state);
    return rep;
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
    struct Make : Task {            // lets make_shared build the task and its control block in one allocation
        Make(Instance& i, int id, std::string n) : Task(i, id, std::move(n)) {}
    };
    std::shared_ptr<Task> t = std::make_shared<Make>(*this, nextTaskId_++, std::move(name));
    t->self_ = t;
    t->program_ = program_;
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
            if (f->params[i]) *f->params[i] = std::move(v);
            else f->params[i] = std::make_shared<Value>(std::move(v));
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

std::vector<std::shared_ptr<Task>> Instance::advanceWaits(double dt) {
    std::vector<std::shared_ptr<Task>> ready;
    auto copy = tasks_;
    for (auto& t : copy)
        if (t->state() == TaskState::Waiting && t->advanceWait(dt)) ready.push_back(t);
    return ready;
}

double Instance::nextWakeIn() const {
    double best = -1;
    for (auto& t : tasks_)
        if (t->state() == TaskState::Waiting) {
            double w = t->waitRemaining();
            if (best < 0 || w < best) best = w;
        }
    return best;
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
    auto& spare = inst_->spareFrames_;
    if (!spare.empty()) { fp = std::move(spare.back()); spare.pop_back(); }
    else fp = std::make_unique<Frame>();
    Frame& f = *fp;
    const Program& prog = inst_->program();
    const CompiledGraph& cg = prog.graphs[static_cast<size_t>(graphIndex)];
    f.graphIndex = graphIndex;
    f.graph = &cg;
    const size_t slotCount = static_cast<size_t>(cg.slotCount);
    if (f.slotValues.size() != slotCount) f.slotValues.resize(slotCount);
    for (size_t i = 0; i < slotCount; ++i) {
        const Value& init = cg.slotInit[i];
        if (init.isContainer()) f.slotValues[i] = prog.registry->defaultValue(cg.slotTypes[i]);    // containers are never shared between frames
        else f.slotValues[i] = init;
    }
    if (f.stampGraph != graphIndex || f.stamps.size() != cg.nodes.size()) {
        f.stamps.assign(cg.nodes.size(), 0);
        f.epoch = 1;
        f.stampGraph = graphIndex;
    }
    if (f.params.size() != cg.inputs.size()) f.params.resize(cg.inputs.size());     // cells are empty or clean (see popFrame)
    if (f.locals.size() != cg.locals.size()) f.locals.resize(cg.locals.size());
    for (size_t i = 0; i < cg.locals.size(); ++i) {
        if (f.locals[i]) *f.locals[i] = cg.locals[i].def.deepCopy();
        else f.locals[i] = std::make_shared<Value>(cg.locals[i].def.deepCopy());
    }
    if (!f.conts.empty()) f.conts.clear();
    if (!f.results.empty()) f.results.clear();
    f.pc = -1;
    f.entryPin = -1;
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
    // Cells that nobody else holds are kept for the next activation (emptied, so nothing stays alive); shared ones are let go.
    for (auto* cells : {&fp->params, &fp->locals})
        for (auto& c : *cells) {
            if (!c) continue;
            if (c.use_count() == 1) *c = Value();
            else c.reset();
        }
    if (inst_->spareFrames_.size() < 32) inst_->spareFrames_.push_back(std::move(fp));
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
    r.exited = exited_;
    r.exitCode = exitCode_;
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
    hooks_ = hooks;
    state_ = TaskState::Running;
    const bool explicitLimit = maxNodes != 0;
    const uint64_t limit = explicitLimit ? maxNodes : (inst_->stepLimit_ ? inst_->stepLimit_ : UINT64_MAX);
    uint64_t count = 0;
    // Limits::maxTaskSeconds: the clock is read when the run starts and every 1024 nodes
    using Clock = std::chrono::steady_clock;
    const double timeLimit = inst_->limits_.maxTaskSeconds;
    const Clock::time_point started = timeLimit > 0 ? Clock::now() : Clock::time_point();
    auto ranFor = [&] { return runSeconds_ + std::chrono::duration<double>(Clock::now() - started).count(); };
    const bool expired = timeLimit > 0 && runSeconds_ > timeLimit && !frames_.empty();     // used up in earlier slices
    if (expired) {
        Frame& f = *frames_.back();
        fail("Time limit of " + std::to_string(timeLimit) + " s exceeded", f.pc >= 0 ? &f.graph->nodes[static_cast<size_t>(f.pc)] : nullptr);
    }
    try {
        while (!expired) {
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
                    f.pc = f.conts.back().node;      // the continuation stays on the stack until that node takes it over
                    f.entryPin = -1;
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
            if (timeLimit > 0 && (count & 1023) == 1023 && ranFor() > timeLimit) {
                fail("Time limit of " + std::to_string(timeLimit) + " s exceeded", &n);
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
    } catch (...) {                        // a native function may throw anything; it must not end the host process
        fail("internal error: a native function threw an unknown exception", active_);
    }
    if (timeLimit > 0) runSeconds_ = ranFor();
    if (done()) inst_->forget(this);
    if (hooks && state_ != TaskState::Ready) hooks->onTaskStateChanged(*this);
    return makeResult();
}

// ---- hot path helpers (defined before their users so they are inlined)
namespace {
const Value kNullValue;
} // namespace

FG_FORCEINLINE void Task::follow(Frame& f, const CompiledNode& n, size_t execOut) {
    int target = -1, pin = -1;
    if (execOut < n.execTarget.size()) {
        target = n.execTarget[execOut];
        pin = n.execTargetPin[execOut];
    }
    f.pc = target;
    f.entryPin = pin;
    if (target >= 0 && hooks_) hooks_->onExecLink(*this, *f.graph, n.index, static_cast<int>(execOut), target);
}

Cell Task::cellFor(Frame& f, const CompiledNode& var) {
    if (var.resolved < 0) throwUnknownVariable(var);
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

FG_FORCEINLINE Value* Task::cellPtr(Frame& f, const CompiledNode& var) {
    if (var.resolved < 0) throwUnknownVariable(var);
    size_t i = static_cast<size_t>(var.resolved);
    switch (var.scope) {
    case VarScope::Global: return inst_->globals_[i].get();
    case VarScope::Local: return f.locals[i].get();
    case VarScope::Param:
        if (!f.params[i]) f.params[i] = std::make_shared<Value>();
        return f.params[i].get();
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

// The value of a data input without copying it, or null when it cannot be referenced in place (a container default that
// needs a private copy, a type conversion): the caller then takes its slow path. Pure sources are evaluated on the way.
// The less common ways of reading an input (kept out of line so the inlined fast cases stay small).
FG_NOINLINE const Value* Task::peekRare(Frame& f, const DataSource& ds) {
    if (ds.mode != DataSource::Mode::VarLocal) return nullptr;
    if (!hooks_) return f.locals[static_cast<size_t>(ds.varIndex)].get();
    if (f.stamps[static_cast<size_t>(ds.node)] != f.epoch) evalNode(f, f.graph->nodes[static_cast<size_t>(ds.node)]);
    return &f.slotValues[static_cast<size_t>(ds.slot)];
}

FG_FORCEINLINE const Value* Task::peek(Frame& f, const CompiledNode& n, size_t ordinal) {
    const DataSource& ds = n.inputs[ordinal];
    switch (ds.mode) {
    case DataSource::Mode::Const: return &ds.constant;
    case DataSource::Mode::Slot: return &f.slotValues[static_cast<size_t>(ds.slot)];
    case DataSource::Mode::Param: {
        const Cell& c = f.params[static_cast<size_t>(ds.slot)];
        return c ? c.get() : &kNullValue;
    }
    case DataSource::Mode::VarParam:
        if (!hooks_) {                      // a debugger shows the Get Variable node's value, so it is evaluated then
            const Cell& c = f.params[static_cast<size_t>(ds.varIndex)];
            return c ? c.get() : &kNullValue;
        }
        goto viaNode;
    case DataSource::Mode::VarGlobal:
        if (!hooks_) return inst_->globals_[static_cast<size_t>(ds.varIndex)].get();
        [[fallthrough]];
    case DataSource::Mode::PureSlot:
    viaNode:
        if (f.stamps[static_cast<size_t>(ds.node)] != f.epoch) evalNode(f, f.graph->nodes[static_cast<size_t>(ds.node)]);
        return &f.slotValues[static_cast<size_t>(ds.slot)];
    default: return peekRare(f, ds);
    }
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
    ctx.checkLimits_ = inst_->valueLimits_;
    if (rc) {
        ctx.resumed_ = true;
        ctx.state_ = rc->state;
        ctx.aux_ = rc->aux;
        ctx.native_ = rc->native;
    }
}

// ---- pure nodes
void Task::evalPure(Frame& f, const CompiledNode& n) {
    if (f.stamps[static_cast<size_t>(n.index)] != f.epoch) evalNode(f, n);
}

// Everything that is not an intrinsic (or an intrinsic that declined): the NodeDef's run function with a NodeCtx.
void Task::evalGeneric(Frame& f, const CompiledNode& n) {
    switch (n.op) {
    case NodeOp::GetVariable:
        f.slotValues[static_cast<size_t>(n.outBase)] = *cellPtr(f, n);
        break;
    case NodeOp::Reroute:
        f.slotValues[static_cast<size_t>(n.outBase)] = evalInput(f, n, 0);
        break;
    default:
        if (n.def->run) {
            InputBuf buf(n.dataIn.size());
            gatherInputs(f, n, buf.data);
            NodeCtx ctx;
            initCtx(ctx, f, n, buf.data, nullptr);
            n.def->run(ctx);
        }
        break;
    }
}

// Evaluates a pure node that has not been evaluated in the current epoch. The intrinsics mirror the `run` functions
// registered in Builtins.cpp; they decline (and the node goes through evalGeneric) when an input cannot be peeked.
void Task::evalNode(Frame& f, const CompiledNode& n) {
    if (++evalDepth_ > 1000) {
        evalDepth_ = 0;
        throwTooDeep();
    }
    // When an error propagates, active_ and evalDepth_ are left as they are: active_ names the culprit and the task is finished anyway.
    const CompiledNode* prev = active_;
    active_ = &n;
    if (!n.def) throwUnknownNode(n);
    Value& out = f.slotValues[static_cast<size_t>(n.outBase)];
    switch (n.fast) {
    case Intrinsic::GetVar:
        out = *cellPtr(f, n);
        break;
    case Intrinsic::Add:
    case Intrinsic::Sub:
    case Intrinsic::Mul:
    case Intrinsic::Div:
    case Intrinsic::Mod:
    case Intrinsic::Min:
    case Intrinsic::Max: {
        const Value* a = peek(f, n, 0);
        const Value* b = peek(f, n, 1);
        if (!a || !b) goto generic;
        if (a->isInt() && b->isInt()) {
            const int64_t x = a->rawInt(), y = b->rawInt();
            switch (n.fast) {
            case Intrinsic::Add: out.setInt(addInt(x, y)); break;
            case Intrinsic::Sub: out.setInt(subInt(x, y)); break;
            case Intrinsic::Mul: out.setInt(mulInt(x, y)); break;
            case Intrinsic::Div:
                if (y == 0) throwDivisionByZero();
                out.setInt(divideInt(x, y));
                break;
            case Intrinsic::Mod:
                if (y == 0) throwDivisionByZero();
                out.setInt(moduloInt(x, y));
                break;
            case Intrinsic::Min: out.setInt(std::min(x, y)); break;
            default: out.setInt(std::max(x, y)); break;
            }
        } else {
            const double x = a->toFloat(), y = b->toFloat();
            switch (n.fast) {
            case Intrinsic::Add: out.setFloat(x + y); break;
            case Intrinsic::Sub: out.setFloat(x - y); break;
            case Intrinsic::Mul: out.setFloat(x * y); break;
            case Intrinsic::Div: out.setFloat(x / y); break;
            case Intrinsic::Mod: out.setFloat(std::fmod(x, y)); break;
            case Intrinsic::Min: out.setFloat(std::min(x, y)); break;
            default: out.setFloat(std::max(x, y)); break;
            }
        }
        break;
    }
    case Intrinsic::Eq:
    case Intrinsic::Ne:
    case Intrinsic::Lt:
    case Intrinsic::Le:
    case Intrinsic::Gt:
    case Intrinsic::Ge: {
        const Value* a = peek(f, n, 0);
        const Value* b = peek(f, n, 1);
        if (!a || !b) goto generic;
        bool r;
        if (a->isInt() && b->isInt()) {
            const int64_t x = a->rawInt(), y = b->rawInt();
            switch (n.fast) {
            case Intrinsic::Eq: r = x == y; break;
            case Intrinsic::Ne: r = x != y; break;
            case Intrinsic::Lt: r = x < y; break;
            case Intrinsic::Le: r = x <= y; break;
            case Intrinsic::Gt: r = x > y; break;
            default: r = x >= y; break;
            }
        } else {
            static const ValueLess less;
            switch (n.fast) {
            case Intrinsic::Eq: r = *a == *b; break;
            case Intrinsic::Ne: r = *a != *b; break;
            case Intrinsic::Lt: r = less(*a, *b); break;
            case Intrinsic::Le: r = !less(*b, *a); break;
            case Intrinsic::Gt: r = less(*b, *a); break;
            default: r = !less(*a, *b); break;
            }
        }
        out.setBool(r);
        break;
    }
    case Intrinsic::And:
    case Intrinsic::Or:
    case Intrinsic::Xor: {
        const Value* a = peek(f, n, 0);
        const Value* b = peek(f, n, 1);
        if (!a || !b) goto generic;
        const bool x = a->toBool(), y = b->toBool();
        out.setBool(n.fast == Intrinsic::And ? (x && y) : n.fast == Intrinsic::Or ? (x || y) : x != y);
        break;
    }
    case Intrinsic::Not: {
        const Value* a = peek(f, n, 0);
        if (!a) goto generic;
        out.setBool(!a->toBool());
        break;
    }
    case Intrinsic::ArrayGet: {
        const Value* av = peek(f, n, 0);
        const Value* iv = peek(f, n, 1);
        if (!av || !iv) goto generic;
        const ArrayData* a = av->arrayPtr();
        if (!a) throwArrayNull();
        const int64_t idx = iv->toInt();
        if (idx < 0 || static_cast<size_t>(idx) >= a->items.size()) throwBadIndex(idx, a->items.size());
        out = a->items[static_cast<size_t>(idx)];
        break;
    }
    case Intrinsic::ArrayLength: {
        const Value* av = peek(f, n, 0);
        if (!av) goto generic;
        const ArrayData* a = av->arrayPtr();
        if (!a) throwArrayNull();
        out.setInt(static_cast<int64_t>(a->items.size()));
        break;
    }
    case Intrinsic::MapGet: {
        const Value* mv = peek(f, n, 0);
        const Value* kv = peek(f, n, 1);
        if (!mv || !kv) goto generic;
        const MapData* m = mv->mapPtr();
        if (!m) throwMapNull();
        auto it = m->items.find(*kv);
        const bool found = it != m->items.end();
        if (found) out = it->second;
        else out = inst_->registry().defaultValue(m->val);
        f.slotValues[static_cast<size_t>(n.outBase) + 1].setBool(found);
        break;
    }
    case Intrinsic::MapContains: {
        const Value* mv = peek(f, n, 0);
        const Value* kv = peek(f, n, 1);
        if (!mv || !kv) goto generic;
        const MapData* m = mv->mapPtr();
        if (!m) throwMapNull();
        out.setBool(m->items.count(*kv) > 0);
        break;
    }
    case Intrinsic::MapLength: {
        const Value* mv = peek(f, n, 0);
        if (!mv) goto generic;
        const MapData* m = mv->mapPtr();
        if (!m) throwMapNull();
        out.setInt(static_cast<int64_t>(m->items.size()));
        break;
    }
    default:
    generic:
        evalGeneric(f, n);
        break;
    }
    f.stamps[static_cast<size_t>(n.index)] = f.epoch;
    active_ = prev;
    --evalDepth_;
}

// ---- exec nodes
// The loop intrinsics keep their continuation on the stack and update it in place; `resumed` means f.conts.back() is the
// continuation of this node. Mirrors Builtins.cpp. Returns false (nothing changed) when an input cannot be peeked.
bool Task::execFast(Frame& f, const CompiledNode& n, bool resumed) {
    switch (n.fast) {
    case Intrinsic::Branch: {
        const Value* c = peek(f, n, 0);
        if (!c) return false;
        follow(f, n, c->toBool() ? 0 : 1);
        return true;
    }
    case Intrinsic::For: {
        const Value* first = resumed ? nullptr : peek(f, n, 0);
        const Value* last = peek(f, n, 1);
        if (!last || (!resumed && !first)) return false;
        const int64_t i = resumed ? f.conts.back().state + 1 : first->toInt();
        if (i < last->toInt()) {
            f.slotValues[static_cast<size_t>(n.outBase)].setInt(i);
            if (resumed) {
                f.conts.back().state = i;
            } else {
                Continuation& c = f.conts.emplace_back();
                c.node = n.index;
                c.state = i;
                c.loop = true;
            }
            follow(f, n, 1);
        } else {
            if (resumed) f.conts.pop_back();
            follow(f, n, 0);
        }
        return true;
    }
    case Intrinsic::While: {
        const Value* c = peek(f, n, 0);
        if (!c) return false;
        if (c->toBool()) {
            if (!resumed) {
                Continuation& k = f.conts.emplace_back();
                k.node = n.index;
                k.state = 0;
                k.loop = true;
            }
            follow(f, n, 1);
        } else {
            if (resumed) f.conts.pop_back();
            follow(f, n, 0);
        }
        return true;
    }
    case Intrinsic::ForEach: {
        const Value* arr = peek(f, n, 0);
        if (!arr) return false;
        const int64_t i = resumed ? f.conts.back().state + 1 : 0;
        const ArrayData* a = arr->arrayPtr();
        if (a && static_cast<size_t>(i) < a->items.size()) {
            f.slotValues[static_cast<size_t>(n.outBase)] = a->items[static_cast<size_t>(i)];
            f.slotValues[static_cast<size_t>(n.outBase) + 1].setInt(i);
            if (resumed) {
                f.conts.back().state = i;
            } else {
                Continuation& c = f.conts.emplace_back();
                c.node = n.index;
                c.state = i;
                c.loop = true;
            }
            follow(f, n, 1);
        } else {
            if (resumed) f.conts.pop_back();
            follow(f, n, 0);
        }
        return true;
    }
    case Intrinsic::ArrayAdd: {
        const Value* av = peek(f, n, 0);
        const Value* item = peek(f, n, 1);
        if (!av || !item) return false;
        ArrayData* a = av->arrayPtr();
        if (!a) throwArrayNull();
        inst_->checkContainerSize(a->items.size() + 1);
        a->items.push_back(item->deepCopy());
        follow(f, n, 0);
        return true;
    }
    case Intrinsic::ArraySet: {
        const Value* av = peek(f, n, 0);
        const Value* iv = peek(f, n, 1);
        const Value* item = peek(f, n, 2);
        if (!av || !iv || !item) return false;
        ArrayData* a = av->arrayPtr();
        if (!a) throwArrayNull();
        const int64_t idx = iv->toInt();
        if (idx < 0 || static_cast<size_t>(idx) >= a->items.size()) throwBadIndex(idx, a->items.size());
        a->items[static_cast<size_t>(idx)] = item->deepCopy();
        follow(f, n, 0);
        return true;
    }
    case Intrinsic::MapSet: {
        const Value* mv = peek(f, n, 0);
        const Value* kv = peek(f, n, 1);
        const Value* vv = peek(f, n, 2);
        if (!mv || !kv || !vv) return false;
        MapData* m = mv->mapPtr();
        if (!m) throwMapNull();
        if (inst_->limits_.maxContainerSize && m->items.size() >= inst_->limits_.maxContainerSize && !m->items.count(*kv))
            inst_->containerTooBig(m->items.size() + 1);
        m->items[*kv] = vv->deepCopy();
        follow(f, n, 0);
        return true;
    }
    default: return false;
    }
}

// A native node: inputs are copied into a buffer and the run function talks to the interpreter through a NodeCtx.
void Task::execGeneric(Frame& f, const CompiledNode& n, bool resumed) {
    if (resumed) {                          // the continuation being re-entered is taken off the stack
        f.resumeCont = std::move(f.conts.back());
        f.conts.pop_back();
    }
    InputBuf buf(n.dataIn.size());
    gatherInputs(f, n, buf.data);
    NodeCtx ctx;
    initCtx(ctx, f, n, buf.data, resumed ? &f.resumeCont : nullptr);
    if (n.def->run) n.def->run(ctx);
    if (ctx.exit_) {                      // Exit: unwind everything, the task ends successfully with a code
        exited_ = true;
        exitCode_ = ctx.exitCode_;
        results_.clear();
        frames_.clear();
        state_ = TaskState::Finished;
        return;
    }

    using A = NodeCtx::Action;
    if (ctx.waiting_ && ctx.resumeAfterWait_) {
        // waitThenResume: when the wait is over the run loop finds this continuation and runs the node again
        Continuation c;
        c.node = n.index;
        c.state = ctx.state_;
        c.aux = std::move(ctx.aux_);
        c.native = std::move(ctx.native_);
        f.conts.push_back(std::move(c));
        f.pc = -1;
        state_ = TaskState::Waiting;
        waitRemaining_ = 0;
        waitPoll_ = ctx.poll_ ? std::move(*ctx.poll_) : nullptr;
        return;
    }
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
        c.native = std::move(ctx.native_);
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
        if (!found) throwBreakOutsideLoop();
        break;
    }
    }
    if (ctx.waiting_) {
        state_ = TaskState::Waiting;
        waitRemaining_ = ctx.waitSeconds_;
        waitPoll_ = ctx.poll_ ? std::move(*ctx.poll_) : nullptr;
    }
}

void Task::setVariableSlow(Frame& f, const CompiledNode& n) {
    Value v = evalInput(f, n, 0).deepCopy();
    *cellPtr(f, n) = std::move(v);
    follow(f, n, 0);
}

void Task::returnSlow(Frame& f, const CompiledNode& n) {
    std::vector<Value> out;
    out.reserve(n.dataIn.size());
    for (size_t i = 0; i < n.dataIn.size(); ++i) out.push_back(evalInput(f, n, i).deepCopy());
    f.results = std::move(out);
}

void Task::executeNode(Frame& f, const CompiledNode& n) {
    active_ = &n;
    if (++f.epoch == 0) { std::fill(f.stamps.begin(), f.stamps.end(), 0u); f.epoch = 1; }
    const bool resumed = f.resuming;       // the continuation of this node is still on f.conts
    f.resuming = false;
    if (!n.def) throwUnknownNode(n);

    switch (n.op) {
    case NodeOp::Event:
    case NodeOp::FunctionEntry:
    case NodeOp::GetVariable:
    case NodeOp::Reroute:
        follow(f, n, 0);
        return;
    case NodeOp::Return: {
        f.results.clear();
        for (size_t i = 0; i < n.dataIn.size(); ++i) {
            const Value* v = peek(f, n, i);
            if (!v) { returnSlow(f, n); break; }
            f.results.push_back(v->deepCopy());
        }
        f.returned = true;
        f.conts.clear();
        f.pc = -1;
        return;
    }
    case NodeOp::SetVariable: {
        const Value* v = peek(f, n, 0);
        if (!v) { setVariableSlow(f, n); return; }
        Value* c = cellPtr(f, n);
        if (v->isContainer()) *c = v->deepCopy();
        else *c = *v;
        follow(f, n, 0);
        return;
    }
    case NodeOp::CallFunction:
        callGraph(f, n);
        return;
    case NodeOp::Native:
        break;
    }

    if (n.fast != Intrinsic::None && execFast(f, n, resumed)) return;
    execGeneric(f, n, resumed);
}

void Task::callGraph(Frame& f, const CompiledNode& n) {
    if (frames_.size() >= inst_->maxDepth_) throw ScriptError("call stack overflow (recursion too deep?)");
    int gi = n.resolved;
    if (gi < 0) throw ScriptError("unknown function '" + n.prop("function") + "'");
    const CompiledGraph& cg = inst_->program().graphs[static_cast<size_t>(gi)];

    // The arguments are evaluated in the caller's frame straight into the cells of the new frame. When one of them fails the
    // new frame is dropped again, so the error is attributed to the caller.
    const size_t count = n.dataIn.size();
    Frame* nf = pushFrame(gi);
    try {
        if (nf->params.size() != count) nf->params.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const CompiledPin& p = n.pins[static_cast<size_t>(n.dataIn[i])];
            Cell& cell = nf->params[i];
            if (p.byRef) {
                const CompiledNode* src = originNode(f, n, i);
                if (src && src->op == NodeOp::GetVariable) {      // by reference parameters alias the variable of the caller
                    cell = cellFor(f, *src);
                    continue;
                }
            }
            if (!cell) cell = std::make_shared<Value>();
            if (const Value* v = peek(f, n, i)) {
                if (p.isConst) *cell = *v;
                else if (v->isContainer()) *cell = v->deepCopy();
                else *cell = *v;
            } else {
                Value v2 = evalInput(f, n, i);
                *cell = p.isConst ? std::move(v2) : v2.deepCopy();
            }
        }
    } catch (...) {
        popFrame();
        throw;
    }
    follow(f, n, 0);                       // the caller continues here once the callee is done
    nf->callerNode = n.index;
    nf->pc = cg.entry;
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
        results_ = std::move(f.results);
        refResults_.assign(cg.inputs.size(), Value());
        for (size_t i = 0; i < cg.inputs.size(); ++i)
            if (cg.inputs[i].decl.byRef && f.params[i]) refResults_[i] = *f.params[i];
    } else if (cg.kind == GraphKind::Function) {
        Frame& caller = *frames_[frames_.size() - 2];
        const CompiledNode& callNode = caller.graph->nodes[static_cast<size_t>(f.callerNode)];
        for (size_t i = 0; i < callNode.dataOut.size() && i < f.results.size(); ++i)
            caller.slotValues[static_cast<size_t>(callNode.outBase) + i] = std::move(f.results[i]);
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
void Debugger::updateArmed() { armed_.store(breakpoints_.size() + steps_.size(), std::memory_order_release); }

void Debugger::setBreakpoint(const std::string& graph, NodeId node, bool on) {
    std::lock_guard lk(mu_);
    if (on) breakpoints_.insert({graph, node});
    else breakpoints_.erase({graph, node});
    updateArmed();
}
bool Debugger::hasBreakpoint(const std::string& graph, NodeId node) const {
    std::lock_guard lk(mu_);
    return breakpoints_.count({graph, node}) > 0;
}
void Debugger::clearBreakpoints() {
    std::lock_guard lk(mu_);
    breakpoints_.clear();
    updateArmed();
}
std::set<std::pair<std::string, NodeId>> Debugger::breakpoints() const {
    std::lock_guard lk(mu_);
    return breakpoints_;
}

void Debugger::command(Task& task, Command cmd) {
    size_t depth = task.depth();
    std::lock_guard lk(mu_);
    if (cmd == Command::Continue) steps_.erase(&task);
    else steps_[&task] = Step{cmd, depth};
    updateArmed();
}

bool Debugger::shouldBreak(Task& task, const CompiledGraph& graph, const CompiledNode& node) {
    if (pauseAll_.load(std::memory_order_relaxed) && pauseAll_.exchange(false)) return true;
    if (armed_.load(std::memory_order_acquire) == 0) return false;      // no breakpoint, nobody stepping: nothing to look up
    std::lock_guard lk(mu_);
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
            updateArmed();
            return true;
        }
    }
    return breakpoints_.count({graph.name, node.id}) > 0;
}

void Debugger::onExecLink(Task&, const CompiledGraph& graph, int fromNode, int execOutOrdinal, int toNode) {
    if (linkFired) linkFired(graph.name, graph.nodes[static_cast<size_t>(fromNode)].id, execOutOrdinal, graph.nodes[static_cast<size_t>(toNode)].id);
}

void Debugger::onTaskStateChanged(Task& task) {
    if (task.done()) {
        std::lock_guard lk(mu_);
        steps_.erase(&task);
        updateArmed();
    }
    if (taskStateChanged) taskStateChanged(task);
}

} // namespace fg
