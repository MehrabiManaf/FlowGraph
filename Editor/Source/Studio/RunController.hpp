#pragma once
#include "../Document.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace fged {

// Runs the document's script on the GUI thread, sliced by a timer so the UI stays responsive, and
// drives the debugger (breakpoints, stepping, slow motion).
class RunController : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Running, Waiting, Paused };

    explicit RunController(Document* doc, QObject* parent = nullptr);

    State state() const { return state_; }
    bool isActive() const { return state_ != State::Idle; }
    fg::Instance* instance() const { return instance_.get(); }
    fg::Task* task() const { return task_.get(); }

    struct Entry { QString name; bool isEvent; std::vector<fg::ParamDecl> params; QString signature; };
    std::vector<Entry> entries() const;           // events and functions of the compiled program

    bool start(const QString& entry, const std::vector<fg::Value>& args);
    void stop();
    void pause();
    void resume();
    void stepOver() { step(fg::Debugger::Command::StepOver); }
    void stepInto() { step(fg::Debugger::Command::StepInto); }
    void stepOut() { step(fg::Debugger::Command::StepOut); }

    // Delay between two executed nodes in milliseconds (0 = full speed). Makes link animations visible.
    void setStepDelay(int ms) { delayMs_ = ms; }
    int stepDelay() const { return delayMs_; }

    // Frame (0 = innermost) shown in the variables panel / used for value tooltips.
    void setSelectedFrame(int f) { frame_ = f; emit frameSelected(f); }
    int selectedFrame() const { return frame_; }
    // Text of a data pin's last value in the innermost frame running `graph`.
    bool pinText(const QString& graph, fg::NodeId node, const std::string& pin, QString& out) const;

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
    void tick();
    void finishRun();
    void onDocumentChanged(bool structural);

    Document* doc_;
    std::shared_ptr<const fg::Program> program_;
    std::shared_ptr<fg::Instance> instance_;
    std::shared_ptr<fg::Task> task_;
    QTimer timer_;
    QElapsedTimer clock_;
    State state_ = State::Idle;
    int delayMs_ = 0;
    int frame_ = 0;
    bool pendingPause_ = false;
};

} // namespace fged
