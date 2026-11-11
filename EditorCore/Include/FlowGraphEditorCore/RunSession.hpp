// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Running and debugging the document's script: on a worker thread (fg::AsyncExecutor), so that the editor never blocks while
// a script computes, waits or is stopped at a breakpoint; or attached to an executor the application runs. What it reports
// comes on the editor's thread, through the UiThread the editor gives it.
#include <FlowGraphEditorCore/Document.hpp>

#include <FlowGraph/Executor.hpp>
#include <FlowGraph/Inspect.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fged::core {

// What the editor's toolkit provides for the session: its own thread's event loop.
class UiThread {
public:
    virtual ~UiThread() = default;
    // From any thread: runs `fn` on the editor's thread, later, in the order posted. A function posted must not run once
    // this object is gone.
    virtual void post(std::function<void()> fn) = 0;
    // Calls `fn` on the editor's thread every `intervalMs` until stopRepeating (one at a time: a new one replaces it).
    virtual void startRepeating(int intervalMs, std::function<void()> fn) = 0;
    virtual void stopRepeating() = 0;
};

enum class RunState { Idle, Running, Waiting, Paused };

// What the session tells the user in the editor's words (the editor writes them in its language).
enum class RunNotice {
    CompileErrors,      // the script has compile errors: it cannot run
    StoppedByEdit,      // the script was changed: the run was stopped
};

class RunSession {
public:
    RunSession(Document& doc, UiThread& ui);
    ~RunSession();
    RunSession(const RunSession&) = delete;
    RunSession& operator=(const RunSession&) = delete;

    RunState state() const { return state_; }
    bool isActive() const { return state_ != RunState::Idle; }

    // Starts an event of the compiled program (or a function when `entry` names one).
    bool start(const std::string& entry, const std::vector<fg::Value>& args);
    void stop();                                  // returns when the script has stopped (an attached debugger detaches)

    // Debugging a script that the application runs: the document's debugger takes over the executor (breakpoints stop its
    // tasks, pause / resume / step control them, the debug panels read it) until detach(). The state is Running while
    // attached and Paused while a task is stopped. The application may run many tasks (one per message...): the debugger
    // follows ONE task at a time. Continue / Step resume the task that is shown and the debugger waits for what happens to
    // it: it stops again (the next node of a step, or a breakpoint) and that is what is shown, or it ends and the debugger
    // goes on to the next task that was stopped (or waits for the next one to stop), so a script stepped to its end is
    // followed by the same script for the next message.
    bool attach(std::shared_ptr<fg::AsyncExecutor> executor);
    void detach();                                // everything that was stopped continues; the executor is as it was before
    bool isAttached() const { return attached_ != nullptr; }
    void pause();
    void resume();
    void stepOver() { step(fg::Debugger::Command::StepOver); }
    void stepInto() { step(fg::Debugger::Command::StepInto); }
    void stepOut() { step(fg::Debugger::Command::StepOut); }

    // Delay between two executed nodes in milliseconds (0 = full speed). Makes link animations visible.
    void setStepDelay(int ms);
    int stepDelay() const { return delayMs_; }

    // Frame (0 = innermost) shown in the variables panel.
    void setSelectedFrame(int f) {
        frame_ = f;
        frameSelected(f);
    }
    int selectedFrame() const { return frame_; }
    // The text of a data pin's last value in the innermost frame running `graph` (paused scripts only).
    bool pinText(const std::string& graph, fg::NodeId node, const std::string& pin, std::string& out) const;

    struct StackFrame {
        std::string graph;
        int node = 0;
        bool isEvent = false;
    };
    struct DebugSnapshot {
        std::vector<StackFrame> stack;                    // innermost first (only while paused)
        std::vector<fg::VariableView> frameVariables;     // parameters and locals of the selected frame
        std::vector<fg::VariableView> globals;
    };
    // Reads the debugger's state from the worker thread; false when no script was started.
    bool snapshot(DebugSnapshot& out) const;
    // Evaluates watch expressions (fg::evaluateExpression) on the worker thread, in the selected frame while the script is
    // paused and among the globals otherwise. `out` gets one result per expression; false when no script was started.
    bool evaluate(const std::vector<std::string>& expressions, const fg::InspectOptions& opt, std::vector<fg::EvalResult>& out) const;

    // A value of `type` from what a user typed (an argument of a launch, a default): "true", "42", an enum value's name, hex
    // for bytes...
    static fg::Value parseArgument(const fg::Registry& reg, const fg::TypeDesc* type, const std::string& text);

    Signal<RunState> stateChanged;
    Signal<const std::string&, int> paused;                       // graph, node
    Signal<> resumed;
    Signal<bool, const std::string&> finished;                    // ok, message
    Signal<int, const std::string&> logMessage;                   // fg::LogLevel, text
    Signal<RunNotice> notice;
    Signal<const std::string&, int, int, int> linkFired;          // graph, from, exec output, to
    Signal<int> frameSelected;
    Signal<bool> attachedChanged;

private:
    void setState(RunState s);
    void step(fg::Debugger::Command c);
    void post(uint64_t generation, std::function<void()> fn);
    void onTaskState(fg::AsyncExecutor::Handle handle, fg::TaskState st);
    void showPaused(fg::AsyncExecutor::Handle handle);                 // the paused task `handle` is the one the user looks at
    void commandAttached(fg::Debugger::Command c);
    fg::AsyncExecutor* executor() const { return attached_ ? attached_.get() : ax_.get(); }
    void onCompleted(bool ok, bool exited, int exitCode, const std::string& error, const std::string& returns);
    void onDocumentChanged(bool structural);
    void onScriptOpened();

    // The worker reports log lines and fired links through these buffers; the editor's thread empties them every few
    // milliseconds (and before it handles any other report), so a script that runs millions of nodes a second cannot flood
    // the editor's event queue.
    void drainPending();
    void clearPending();
    void reportLink(uint64_t generation, const std::string& graph, int from, int out, int to);
    void reportLog(uint64_t generation, int level, const std::string& text);
    void startDraining();
    void stopDraining();
    struct LinkEvent { uint64_t generation; std::string graph; int from, out, to; };
    struct LogEvent { uint64_t generation; int level; std::string text; };
    static constexpr size_t kMaxPendingLogs = 200000;
    std::mutex pendingMu_;
    std::vector<LinkEvent> pendingLinks_;
    std::vector<LogEvent> pendingLogs_;
    size_t droppedLogs_ = 0;
    std::atomic<uint32_t> linkTick_{1};                          // advances whenever the editor has taken the pending links
    std::array<std::atomic<uint64_t>, 256> linkSeen_{};          // per link (hashed): the tick in which it was last reported

    Document& doc_;
    UiThread& ui_;
    int docChanged_ = 0, docOpened_ = 0;                         // our handlers of the document's signals
    std::unique_ptr<fg::AsyncExecutor> ax_;
    std::shared_ptr<fg::AsyncExecutor> attached_;                // an executor of the application, see attach()
    fg::DebugHooks* previousHooks_ = nullptr;                    // what the attached executor used before we took it over
    std::deque<fg::AsyncExecutor::Handle> pausedQueue_;          // attached: the other tasks that are stopped (handle_ is shown)
    fg::AsyncExecutor::Handle awaiting_ = 0;                     // attached: the task resumed; its next stop or its end comes next
    fg::AsyncExecutor::Handle handle_ = 0;
    uint64_t generation_ = 0;          // callbacks of an older executor are ignored
    RunState state_ = RunState::Idle;
    int delayMs_ = 0;
    int frame_ = 0;
};

} // namespace fged::core
