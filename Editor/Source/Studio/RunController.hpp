// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Running and debugging the document's script - the Qt face of the editors' shared fged::core::RunSession (EditorCore): its
// reports as Qt signals on the GUI thread, QString in and out, its notices in the editor's language.
#include "../Document.hpp"

#include <FlowGraphEditorCore/RunSession.hpp>

#include <QObject>
#include <QTimer>

#include <memory>
#include <string>
#include <vector>

namespace fged {

class RunController : public QObject, private core::UiThread {
    Q_OBJECT
public:
    using State = core::RunState;

    explicit RunController(Document* doc, QObject* parent = nullptr);
    ~RunController() override;

    core::RunSession& session() { return *session_; }

    State state() const { return session_->state(); }
    bool isActive() const { return session_->isActive(); }

    // Starts an event of the compiled program (or a function when `entry` names one).
    bool start(const QString& entry, const std::vector<fg::Value>& args) { return session_->start(entry.toStdString(), args); }
    void stop() { session_->stop(); }              // returns when the script has stopped (an attached debugger detaches)

    // Debugging a script that the application runs (see core::RunSession::attach).
    bool attach(std::shared_ptr<fg::AsyncExecutor> executor) { return session_->attach(std::move(executor)); }
    void detach() { session_->detach(); }
    bool isAttached() const { return session_->isAttached(); }
    void pause() { session_->pause(); }
    void resume() { session_->resume(); }
    void stepOver() { session_->stepOver(); }
    void stepInto() { session_->stepInto(); }
    void stepOut() { session_->stepOut(); }

    // Delay between two executed nodes in milliseconds (0 = full speed). Makes link animations visible.
    void setStepDelay(int ms) { session_->setStepDelay(ms); }
    int stepDelay() const { return session_->stepDelay(); }

    // Frame (0 = innermost) shown in the variables panel.
    void setSelectedFrame(int f) { session_->setSelectedFrame(f); }
    int selectedFrame() const { return session_->selectedFrame(); }
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
    // Evaluates watch expressions (see fg::evaluateExpression); false when no script was started.
    bool evaluate(const std::vector<std::string>& expressions, const fg::InspectOptions& opt, std::vector<fg::EvalResult>& out) const {
        return session_->evaluate(expressions, opt, out);
    }

    static fg::Value parseArgument(const fg::Registry& reg, const fg::TypeDesc* type, const QString& text) {
        return core::RunSession::parseArgument(reg, type, text.toStdString());
    }

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
    // core::UiThread: the session's reports come through Qt's event loop
    void post(std::function<void()> fn) override;
    void startRepeating(int intervalMs, std::function<void()> fn) override;
    void stopRepeating() override;

    QTimer repeat_;
    std::function<void()> repeated_;
    std::unique_ptr<core::RunSession> session_;
};

} // namespace fged
