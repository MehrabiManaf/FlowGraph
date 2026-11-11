// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "RunController.hpp"

#include <QMetaObject>

namespace fged {

RunController::RunController(Document* doc, QObject* parent) : QObject(parent) {
    connect(&repeat_, &QTimer::timeout, this, [this] {
        if (repeated_) repeated_();
    });
    session_ = std::make_unique<core::RunSession>(doc->core(), static_cast<core::UiThread&>(*this));
    core::RunSession& s = *session_;
    s.stateChanged.connect([this](State st) { emit stateChanged(st); });
    s.paused.connect([this](const std::string& graph, int node) { emit paused(QString::fromStdString(graph), node); });
    s.resumed.connect([this] { emit resumed(); });
    s.finished.connect([this](bool ok, const std::string& message) { emit finished(ok, QString::fromStdString(message)); });
    s.logMessage.connect([this](int level, const std::string& text) { emit logMessage(level, QString::fromStdString(text)); });
    s.linkFired.connect([this](const std::string& graph, int from, int out, int to) {
        emit linkFired(QString::fromStdString(graph), from, out, to);
    });
    s.frameSelected.connect([this](int f) { emit frameSelected(f); });
    s.attachedChanged.connect([this](bool on) { emit attachedChanged(on); });
    s.notice.connect([this](core::RunNotice n) {
        switch (n) {
        case core::RunNotice::CompileErrors:
            emit logMessage(int(fg::LogLevel::Error), tr("Cannot run: the script has compile errors (see Compile Results)."));
            break;
        case core::RunNotice::StoppedByEdit:
            emit logMessage(int(fg::LogLevel::Warning), tr("The script was modified: the run was stopped."));
            break;
        }
    });
}

RunController::~RunController() { session_.reset(); }     // while this object can still take what it posts

// From the worker thread too: queued to this object, so that nothing runs once it is gone.
void RunController::post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
}

void RunController::startRepeating(int intervalMs, std::function<void()> fn) {
    repeated_ = std::move(fn);
    repeat_.start(intervalMs);
}

void RunController::stopRepeating() {
    repeat_.stop();
    repeated_ = nullptr;
}

bool RunController::snapshot(DebugSnapshot& out) const {
    core::RunSession::DebugSnapshot s;
    if (!session_->snapshot(s)) return false;
    for (auto& f : s.stack) out.stack.push_back({QString::fromStdString(f.graph), f.node, f.isEvent});
    out.frameVariables = std::move(s.frameVariables);
    out.globals = std::move(s.globals);
    return true;
}

bool RunController::pinText(const QString& graph, fg::NodeId node, const std::string& pin, QString& out) const {
    std::string text;
    if (!session_->pinText(graph.toStdString(), node, pin, text)) return false;
    out = QString::fromStdString(text);
    return true;
}

} // namespace fged
