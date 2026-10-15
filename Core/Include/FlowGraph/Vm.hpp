#pragma once
// The interpreter. A Task is a resumable execution context with an explicit stack, so it can be
// paused by a debugger at any node, suspended by latent nodes (Delay) and time sliced by the host -
// no threads required. An Instance owns the variables of one running script.
#include <FlowGraph/Program.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fg {

class Instance;
class Task;
struct Frame;

enum class LogLevel : uint8_t { Info, Warning, Error };
using LogHandler = std::function<void(LogLevel, const std::string&)>;

enum class TaskState : uint8_t {
    Ready,      // created, not started or resumed after a pause
    Running,
    Paused,     // stopped before a node by the debugger
    Waiting,    // suspended by a latent node
    Finished,
    Failed,     // a script error occurred
    Aborted,    // stopped by the host / debugger
};
const char* toString(TaskState s);

// Shared storage of a variable. Function parameters passed by reference alias the cell of the
// caller's variable.
using Cell = std::shared_ptr<Value>;

struct Continuation {
    int node = -1;          // node to re-enter when the exec chain it started has ended
    int64_t state = 0;
    Value aux;
    bool loop = false;      // can be unwound by a Break node
};

struct Frame {
    int graphIndex = -1;
    const CompiledGraph* graph = nullptr;
    std::vector<Value> slotValues;           // outputs of the nodes of this graph
    std::vector<uint32_t> stamps;       // per node: epoch in which a pure node was last evaluated
    uint32_t epoch = 1;
    std::vector<Cell> params;           // by reference parameters alias the caller's variable cell
    std::vector<Cell> locals;
    std::vector<Continuation> conts;
    std::vector<Value> results;         // function outputs written by Return
    int pc = -1;                        // node that executes next, -1 = the exec chain has ended
    int callerNode = -1;                // call node of the calling frame
    bool isEvent = false;
    bool returned = false;
    bool resuming = false;              // pc is being re-entered from a continuation
    Continuation resumeCont;
};

// Passed to the implementation of a node (NodeDef::run).
class NodeCtx {
public:
    // ---- data inputs (by ordinal among the data input pins) -----------------------------------
    size_t inputCount() const { return inCount_; }
    const Value& in(size_t i) const { return inputs_[i]; }
    Value* inputs() const { return inputs_; }
    bool inBool(size_t i) const { return inputs_[i].toBool(); }
    int64_t inInt(size_t i) const { return inputs_[i].toInt(); }
    double inFloat(size_t i) const { return inputs_[i].toFloat(); }
    std::string inString(size_t i) const;
    // ---- data outputs --------------------------------------------------------------------------
    void out(size_t i, Value v);
    // ---- exec flow (impure nodes) -------------------------------------------------------------
    // By default the execution continues with the first exec output.
    void next(size_t execOut);             // continue with this exec output
    void end();                            // end the exec chain here
    void call(size_t execOut);             // run the chain of this exec output, then re-enter this node (resumed() == true)
    void breakLoop();                      // unwind to the innermost loop node and continue after it
    void wait(double seconds);             // suspend the task, then continue as chosen by next()
    void waitFor(std::function<bool(double dt)> poll);   // suspend until poll(dt) returns true
    // Per activation storage of looping nodes: valid while the node is re-entered through call().
    bool resumed() const { return resumed_; }
    int64_t& state() { return state_; }
    Value& aux() { return aux_; }
    // ---- environment -----------------------------------------------------------------------------
    const CompiledNode& node() const { return *node_; }
    const std::string& prop(const std::string& key) const;
    Instance& instance() const { return *inst_; }
    const Registry& registry() const;
    void log(LogLevel level, const std::string& msg) const;
    [[noreturn]] void error(const std::string& msg) const;

private:
    friend class Task;
    friend class Instance;
    enum class Action : uint8_t { Default, Next, End, Call, Break, Wait };
    Instance* inst_ = nullptr;
    Task* task_ = nullptr;
    Frame* frame_ = nullptr;
    const CompiledNode* node_ = nullptr;
    Value* inputs_ = nullptr;
    size_t inCount_ = 0;
    bool resumed_ = false;
    int64_t state_ = 0;
    Value aux_;
    Action action_ = Action::Default;
    bool waiting_ = false;
    size_t pin_ = 0;
    double waitSeconds_ = 0;
    std::function<bool(double)> poll_;
};

struct RunResult {
    bool ok = false;                    // finished without error
    TaskState state = TaskState::Ready;
    std::string error;
    std::string errorGraph;
    NodeId errorNode = 0;
    std::vector<Value> returns;         // function results
    std::vector<Value> refResults;      // final values of by reference parameters (Null for by value)
    std::shared_ptr<Task> task;         // the task that executed (inspect it while paused / after the run)
    bool paused() const { return state == TaskState::Paused; }
    bool waiting() const { return state == TaskState::Waiting; }
    bool finished() const { return state == TaskState::Finished; }
};

// One entry of the call stack of a task.
struct StackEntry {
    std::string graph;
    int graphIndex = -1;
    NodeId node = 0;                    // node being executed / about to execute (0 = none)
    bool isEvent = false;
};

class Task {
public:
    ~Task();
    TaskState state() const { return state_; }
    const std::string& error() const { return error_; }
    bool done() const { return state_ == TaskState::Finished || state_ == TaskState::Failed || state_ == TaskState::Aborted; }
    Instance& instance() const { return *inst_; }
    int id() const { return id_; }
    const std::string& name() const { return name_; }

    // Runs until the task finishes, fails, is paused by the debugger, or waits. `maxNodes` limits the
    // number of nodes executed in this call (0 = unlimited); the state stays Ready when it is exhausted.
    RunResult run(uint64_t maxNodes = 0);
    void abort();

    // ---- inspection (valid while paused and after the task finished) ---------------------------
    size_t depth() const { return frames_.size(); }
    const Frame& frame(size_t i) const { return *frames_[i]; }
    std::vector<StackEntry> callStack() const;       // innermost first
    // Last computed value of a data pin, or false when the node has not produced one in that frame.
    bool pinValue(size_t frameIndex, NodeId node, const std::string& pin, Value& out) const;
    NodeId currentNode() const;                       // node about to execute in the top frame (0 = none)
    const std::string& currentGraph() const;
    // The first frame's graph / node (the event or function that started the task).
    std::vector<Value> results() const { return results_; }

private:
    friend class Instance;
    Task(Instance& inst, int id, std::string name);
    Frame* pushFrame(int graphIndex);
    void popFrame();
    void executeNode(Frame& f, const CompiledNode& n);
    void finishFrame();
    void follow(Frame& f, const CompiledNode& n, size_t execOut);
    Value evalInput(Frame& f, const CompiledNode& n, size_t ordinal);
    void evalPure(Frame& f, const CompiledNode& n);
    void gatherInputs(Frame& f, const CompiledNode& n, Value* buf);
    void initCtx(NodeCtx& ctx, Frame& f, const CompiledNode& n, Value* in, const Continuation* rc);
    Cell cellFor(Frame& f, const CompiledNode& var);
    const CompiledNode* originNode(const Frame& f, const CompiledNode& n, size_t ordinal) const;
    void callGraph(Frame& caller, const CompiledNode& n);
    void fail(const std::string& msg, const CompiledNode* at);
    RunResult makeResult();
    bool advanceWait(double dt);

    Instance* inst_;
    int id_;
    std::string name_;
    TaskState state_ = TaskState::Ready;
    std::vector<std::unique_ptr<Frame>> frames_;
    std::vector<std::unique_ptr<Frame>> pool_;
    std::string error_;
    std::string errorGraph_;
    NodeId errorNode_ = 0;
    std::vector<Value> results_;
    std::vector<Value> refResults_;
    double waitRemaining_ = 0;
    std::function<bool(double)> waitPoll_;
    bool skipHook_ = false;        // do not consult the debugger before the next node (resuming after a pause)
    int pendingNextPin_ = -1;      // exec output to follow when a wait ends
    bool waitingNext_ = false;
    std::weak_ptr<Task> self_;
    const CompiledNode* active_ = nullptr;   // node being executed / evaluated (error attribution)
    int evalDepth_ = 0;
};

// Debugger integration -----------------------------------------------------------------------
class DebugHooks {
public:
    virtual ~DebugHooks() = default;
    // Called before an impure node executes; returning true pauses the task before that node.
    virtual bool shouldBreak(Task& task, const CompiledGraph& graph, const CompiledNode& node) { return false; }
    // Called when an exec link is followed (used for the link animation).
    virtual void onExecLink(Task& task, const CompiledGraph& graph, int fromNode, int execOutOrdinal, int toNode) {}
    // Called when a task has paused, finished, failed or started waiting.
    virtual void onTaskStateChanged(Task& task) {}
};

class Instance {
public:
    Instance(std::shared_ptr<const Program> program);
    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    const Program& program() const { return *program_; }
    std::shared_ptr<const Program> programPtr() const { return program_; }
    const Registry& registry() const { return *program_->registry; }

    // ---- variables -------------------------------------------------------------------------------
    // Not synchronized: access them while no task is running or from a debug hook callback.
    bool hasVariable(const std::string& name) const { return program_->variableIndex(name) >= 0; }
    Value getVariable(const std::string& name) const;
    void setVariable(const std::string& name, const Value& v);   // converts to the declared type
    const Cell& variableCell(size_t index) const { return globals_[index]; }
    size_t variableCount() const { return globals_.size(); }
    void resetVariables();

    // ---- running ---------------------------------------------------------------------------------
    // Raise a host event / call a function of the script and run it until it finishes or blocks.
    RunResult callEvent(const std::string& name, std::vector<Value> args = {});
    RunResult callFunction(const std::string& name, std::vector<Value> args = {});
    // Create a task without running it (for debugger driven runs). Returns null and sets `error` on failure.
    std::shared_ptr<Task> startEvent(const std::string& name, std::vector<Value> args, std::string* error = nullptr);
    std::shared_ptr<Task> startFunction(const std::string& name, std::vector<Value> args, std::string* error = nullptr);
    // Advances tasks suspended by latent nodes (Delay ...) by `dt` seconds and runs the ones that are ready.
    void tick(double dt);
    size_t pendingTaskCount() const { return tasks_.size(); }
    std::vector<std::shared_ptr<Task>> pendingTasks() const { return tasks_; }
    void abortAll();

    // ---- configuration ---------------------------------------------------------------------------
    void setLogHandler(LogHandler h) { log_ = std::move(h); }
    void setDebugHooks(DebugHooks* h) { hooks_ = h; }
    DebugHooks* debugHooks() const { return hooks_; }
    void setStepLimit(uint64_t n) { stepLimit_ = n; }          // nodes per task run, 0 = unlimited
    void setMaxCallDepth(size_t n) { maxDepth_ = n; }
    void requestAbort() { abort_ = true; }                      // callable from another thread
    void clearAbort() { abort_ = false; }
    bool abortRequested() const { return abort_; }
    void setUserData(void* p) { userData_ = p; }
    void* userData() const { return userData_; }
    void log(LogLevel level, const std::string& msg) const;

private:
    friend class Task;
    friend class NodeCtx;
    std::shared_ptr<Task> makeTask(std::string name);
    void forget(Task* t);

    std::shared_ptr<const Program> program_;
    std::vector<Cell> globals_;
    std::vector<std::shared_ptr<Task>> tasks_;     // waiting or paused tasks started through callEvent/callFunction
    LogHandler log_;
    DebugHooks* hooks_ = nullptr;
    uint64_t stepLimit_ = 50'000'000;
    size_t maxDepth_ = 256;
    std::atomic<bool> abort_{false};
    void* userData_ = nullptr;
    int nextTaskId_ = 1;
};

// ---- Debugger -------------------------------------------------------------------------------------
// Breakpoints and stepping on top of DebugHooks.
class Debugger : public DebugHooks {
public:
    enum class Command { Continue, StepInto, StepOver, StepOut, Pause };

    void setBreakpoint(const std::string& graph, NodeId node, bool on);
    bool hasBreakpoint(const std::string& graph, NodeId node) const;
    void toggleBreakpoint(const std::string& graph, NodeId node) { setBreakpoint(graph, node, !hasBreakpoint(graph, node)); }
    void clearBreakpoints() { breakpoints_.clear(); }
    const std::set<std::pair<std::string, NodeId>>& breakpoints() const { return breakpoints_; }

    // Sets how `task` proceeds the next time it is run.
    void command(Task& task, Command cmd);
    // Pause every task before its next node (also those that have not started yet).
    void pauseNext() { pauseAll_ = true; }

    bool shouldBreak(Task& task, const CompiledGraph& graph, const CompiledNode& node) override;
    void onExecLink(Task& task, const CompiledGraph& graph, int fromNode, int execOutOrdinal, int toNode) override;
    void onTaskStateChanged(Task& task) override;

    std::function<void(const std::string& graph, NodeId from, int execOut, NodeId to)> linkFired;
    std::function<void(Task&)> taskStateChanged;

private:
    struct Step { Command cmd = Command::Continue; size_t depth = 0; };
    std::set<std::pair<std::string, NodeId>> breakpoints_;
    std::map<const Task*, Step> steps_;
    bool pauseAll_ = false;
};

} // namespace fg
