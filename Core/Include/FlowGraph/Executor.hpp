#pragma once
// Running scripts without blocking the caller.
//
//   Executor       cooperative scheduler. The host calls pump() from its own loop (game frame, Qt timer, ...);
//                  every call advances Delay nodes and runs each ready task for a bounded number of nodes.
//                  Single threaded: never call it from two threads.
//   Both can run SERIALLY (setSerial / AsyncExecutor::Options): one task at a time, in the order the events were posted. See Executor::setSerial.
//   AsyncExecutor  an Executor that lives on a thread of its own. Events are posted from any thread, results
//                  and log lines arrive through callbacks (on the worker thread), and the script can be paused,
//                  stepped or stopped from outside. Use it when a blocked script or a slow native function must
//                  never freeze the host (editors, servers). Programs that only want to run a script to the end
//                  can keep using Instance::callEvent directly.
#include <FlowGraph/Vm.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fg {

class Executor {
public:
    using Handle = uint64_t;
    struct Completion {
        Handle handle = 0;
        std::string name;               // event / function that was run
        RunResult result;
    };
    using CompletionHandler = std::function<void(const Completion&)>;

    explicit Executor(std::shared_ptr<const Program> program, DebugHooks* hooks = nullptr);

    // Serial mode (off by default): ONE task at a time. The events that are posted wait in a queue, in the order they were posted, and the next
    // one starts when the previous task has ended; a task that waits (Delay...) or is stopped by a debugger holds the others up. So a
    // script is followed from its first node to its last for one event before the next event begins: a breakpoint stops the whole
    // sequence, and stepping to the end of the script goes on with the next event. The variables of the instance are shared, as always:
    // with serial tasks a script can count, sum or keep the last value without any two handlers touching them at the same time.
    // Without it all posted tasks run together, each a slice at a time. Switching it on or off while tasks are queued is allowed.
    void setSerial(bool on) { serial_ = on; }
    bool serial() const { return serial_; }

    Instance& instance() { return *inst_; }
    const Instance& instance() const { return *inst_; }

    // Queue an event / function call. Returns 0 (and sets `error`) when it cannot be started. `forced` lets a
    // wrapper choose the handle (AsyncExecutor hands out handles before the worker thread sees the request).
    Handle postEvent(const std::string& name, std::vector<Value> args = {}, std::string* error = nullptr, Handle forced = 0);
    Handle postFunction(const std::string& name, std::vector<Value> args = {}, std::string* error = nullptr, Handle forced = 0);
    void setCompletionHandler(CompletionHandler h) { completion_ = std::move(h); }
    // Called when a task whose wait ended is about to run again.
    void setWakeHandler(std::function<void(Handle)> h) { wake_ = std::move(h); }

    struct PumpStats {
        size_t ran = 0;                 // tasks that executed nodes in this call
        size_t waiting = 0;             // tasks suspended by Delay and similar nodes
        size_t paused = 0;              // tasks stopped by the debugger
        double nextWake = -1;           // seconds until the next timed wait ends (-1 = none)
        bool runnable = false;          // more work is ready right now
    };
    // Advances waits by `dt` seconds, then lets every ready task execute up to `nodesPerTask` nodes.
    PumpStats pump(double dt, uint64_t nodesPerTask = 2000);

    // Debugging: a paused task stays paused until it is resumed (after Debugger::command chose how).
    void resume(Handle h);
    void abort(Handle h);
    void abortAll();
    std::shared_ptr<Task> task(Handle h) const;
    Handle handleOf(const Task* t) const;
    bool idle() const { return entries_.empty(); }       // nothing queued, running, waiting or paused
    bool hasRunnable() const;

private:
    struct Entry {
        Handle handle = 0;
        std::string name;
        std::shared_ptr<Task> task;
        bool resume = false;
    };
    Handle add(std::shared_ptr<Task> t, const std::string& name, Handle forced);
    void finish(size_t index);
    void pumpSerial(PumpStats& st, uint64_t nodesPerTask);

    std::shared_ptr<const Program> program_;
    std::unique_ptr<Instance> inst_;
    std::vector<Entry> entries_;
    CompletionHandler completion_;
    std::function<void(Handle)> wake_;
    Handle next_ = 1;
    bool serial_ = false;
};

class AsyncExecutor {
public:
    using Handle = Executor::Handle;
    // All callbacks run on the worker thread: marshal them to your own thread (e.g. QMetaObject::invokeMethod).
    struct Callbacks {
        std::function<void(const Executor::Completion&)> completed;
        std::function<void(LogLevel, const std::string&)> log;
        std::function<void(Handle, TaskState)> taskState;        // paused / waiting / finished ...
        std::function<void(const std::string& graph, NodeId from, int execOut, NodeId to)> linkFired;
    };

    struct Options {
        bool serial = false;           // one task at a time, in the order the events were posted (see Executor::setSerial)
    };

    // `debugger` (optional, not owned) keeps its breakpoints across runs; its callbacks are taken over.
    AsyncExecutor(std::shared_ptr<const Program> program, Callbacks callbacks = {}, Debugger* debugger = nullptr, Options options = {});
    ~AsyncExecutor();
    AsyncExecutor(const AsyncExecutor&) = delete;
    AsyncExecutor& operator=(const AsyncExecutor&) = delete;

    // Thread safe. A request that cannot be started is reported through Callbacks::completed (state Failed).
    Handle postEvent(const std::string& name, std::vector<Value> args = {});
    Handle postFunction(const std::string& name, std::vector<Value> args = {});

    void pause(Handle h);                            // stop before the next node
    void resume(Handle h);
    void step(Handle h, Debugger::Command cmd);      // StepInto / StepOver / StepOut
    void abort(Handle h);
    void abortAll();
    void setSerial(bool on);                         // see Executor::setSerial
    void setStepDelay(int milliseconds);             // slow motion: one node per task every `milliseconds`

    // Runs `fn` on the worker thread between two slices and waits for it. This is the safe way to look at
    // tasks, frames and variables while the script is paused or waiting. Do not call it from a callback.
    bool inspect(const std::function<void(Executor&)>& fn);
    bool waitIdle(int timeoutMs = -1);               // until every posted task has finished
    bool idle() const { return idle_.load(); }
    Debugger& debugger() { return *debugger_; }
    // Aborts all tasks and joins the worker. Called by the destructor.
    void stop();

private:
    void run();
    void post(std::function<void()> cmd);

    std::unique_ptr<Debugger> ownedDebugger_;
    Debugger* debugger_;
    Callbacks cb_;
    Executor exec_;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_, idleCv_;
    std::deque<std::function<void()>> cmds_;
    bool stop_ = false;
    std::atomic<bool> idle_{true};
    std::atomic<int> delayMs_{0};
    std::atomic<Handle> nextHandle_{1};
};

} // namespace fg
