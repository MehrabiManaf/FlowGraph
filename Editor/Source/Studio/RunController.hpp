#pragma once
#include "../Document.hpp"

#include <FlowGraph/Executor.hpp>

#include <QObject>

#include <memory>

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
    void stop();                                  // returns when the script has stopped
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

    static fg::Value parseArgument(const fg::Registry& reg, const fg::TypeDesc* type, const QString& text);

signals:
    void stateChanged(State s);
    void paused(const QString& graph, int node);
    void resumed();
    void finished(bool ok, const QString& message);
    void logMessage(int level, const QString& text);
    void linkFired(const QString& graph, int from, int execOut, int to);
    void frameSelected(int frame);

private:
    void setState(State s);
    void step(fg::Debugger::Command c);
    void post(uint64_t generation, std::function<void()> fn);
    void onTaskState(fg::TaskState st);
    void onCompleted(bool ok, bool exited, int exitCode, const QString& error, const QString& returns);
    void onDocumentChanged(bool structural);

    Document* doc_;
    std::unique_ptr<fg::AsyncExecutor> ax_;
    fg::AsyncExecutor::Handle handle_ = 0;
    uint64_t generation_ = 0;          // callbacks of an older executor are ignored
    State state_ = State::Idle;
    int delayMs_ = 0;
    int frame_ = 0;
};

} // namespace fged
