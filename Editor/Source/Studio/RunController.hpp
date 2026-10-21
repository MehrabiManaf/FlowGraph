// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../Document.hpp"

#include <FlowGraph/Executor.hpp>

#include <QObject>
#include <QTimer>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fged {

// Runs the document's script on a worker thread (fg::AsyncExecutor), so the editor never blocks while a script
// computes, waits or is stopped at a breakpoint. All signals are delivered on the GUI thread.
class RunController : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Running, Waiting, Paused };

    explicit RunController(Document* doc, QObject* parent = nullptr);
    ~RunController() override;

    State state() const { return state_; }
    bool isActive() const { return state_ != State::Idle; }

    // Starts an event of the compiled program (or a function when `entry` names one).
    bool start(const QString& entry, const std::vector<fg::Value>& args);
    void stop();                                  // returns when the script has stopped (an attached debugger detaches)

    // Debugging a script that the application runs: the debugger of the document takes over the executor (breakpoints stop its tasks,
    // pause / resume / step control them, the debug panels read it) until detach(). The state is Running while attached and Paused while a
    // task is stopped. The application may run many tasks (one per message...): the debugger follows ONE task at a time. Continue / Step
    // resume the task that is shown and the debugger waits for what happens to it: it stops again (the next node of a step, or a
    // breakpoint) and that is what is shown, or it ends and the debugger goes on to the next task that was stopped (or waits for the next
    // one to stop), so a script that is stepped to its end is followed by the same script for the next message.
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
    void setSelectedFrame(int f) { frame_ = f; emit frameSelected(f); }
    int selectedFrame() const { return frame_; }
    // Text of a data pin's last value in the innermost frame running `graph` (paused scripts only).
    bool pinText(const QString& graph, fg::NodeId node, const std::string& pin, QString& out) const;

    struct StackFrame {
        QString graph;
        int node = 0;
        bool isEvent = false;
    };
    struct DebugSnapshot {
        std::vector<StackFrame> stack;                    // innermost first (only while paused)
        std::vector<fg::VariableView> frameVariables;     // parameters and locals of the selected frame
        std::vector<fg::VariableView> globals;
    };
    // Reads the debugger state from the worker thread; false when no script was started.
    bool snapshot(DebugSnapshot& out) const;
    // Evaluates watch expressions (see fg::evaluateExpression) on the worker thread, in the selected frame while the script is
    // paused and among the globals otherwise. `out` gets one result per expression; false when no script was started.
    bool evaluate(const std::vector<std::string>& expressions, const fg::InspectOptions& opt, std::vector<fg::EvalResult>& out) const;

    static fg::Value parseArgument(const fg::Registry& reg, const fg::TypeDesc* type, const QString& text);

signals:
    void stateChanged(State s);
    void paused(const QString& graph, int node);
    void resumed();
    void finished(bool ok, const QString& message);
    void logMessage(int level, const QString& text);
    void linkFired(const QString& graph, int from, int execOut, int to);
    void frameSelected(int frame);
    void attachedChanged(bool attached);

private:
    void setState(State s);
    void step(fg::Debugger::Command c);
    void post(uint64_t generation, std::function<void()> fn);
    void onTaskState(fg::AsyncExecutor::Handle handle, fg::TaskState st);
    void showPaused(fg::AsyncExecutor::Handle handle);                 // the paused task `handle` is the one the user looks at
    void commandAttached(fg::Debugger::Command c);
    fg::AsyncExecutor* executor() const { return attached_ ? attached_.get() : ax_.get(); }
    void onCompleted(bool ok, bool exited, int exitCode, const QString& error, const QString& returns);
    void onDocumentChanged(bool structural);
    void onScriptOpened();

    // The worker reports log lines and fired links through these buffers; the GUI thread empties them every few milliseconds
    // (and before it handles any other report), so a script that executes millions of nodes per second cannot flood the event queue.
    void drainPending();
    void clearPending();
    void reportLink(uint64_t generation, const std::string& graph, int from, int out, int to);
    void reportLog(uint64_t generation, int level, const std::string& text);
    struct LinkEvent { uint64_t generation; std::string graph; int from, out, to; };
    struct LogEvent { uint64_t generation; int level; std::string text; };
    static constexpr size_t kMaxPendingLogs = 200000;
    std::mutex pendingMu_;
    std::vector<LinkEvent> pendingLinks_;
    std::vector<LogEvent> pendingLogs_;
    size_t droppedLogs_ = 0;
    std::atomic<uint32_t> linkTick_{1};                          // advances whenever the GUI has taken the pending links
    std::array<std::atomic<uint64_t>, 256> linkSeen_{};          // per link (hashed): the tick in which it was last reported
    QTimer pendingTimer_;

    Document* doc_;
    std::unique_ptr<fg::AsyncExecutor> ax_;
    std::shared_ptr<fg::AsyncExecutor> attached_;                       // an executor of the application, see attach()
    fg::DebugHooks* previousHooks_ = nullptr;                           // what the attached executor used before we took it over
    std::deque<fg::AsyncExecutor::Handle> pausedQueue_;                 // attached: the other tasks that are stopped (`handle_` is the one shown)
    fg::AsyncExecutor::Handle awaiting_ = 0;                            // attached: the task that was resumed; its next stop or its end is what comes next
    fg::AsyncExecutor::Handle handle_ = 0;
    uint64_t generation_ = 0;          // callbacks of an older executor are ignored
    State state_ = State::Idle;
    int delayMs_ = 0;
    int frame_ = 0;
};

} // namespace fged
