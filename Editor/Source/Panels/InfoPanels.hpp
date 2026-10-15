#pragma once
#include "../Document.hpp"
#include "../Studio/RunController.hpp"

#include <QPlainTextEdit>
#include <QWidget>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace fged {

// "Compile Results": errors and warnings of the last compile.
class DiagnosticsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(Document* doc, QWidget* parent = nullptr);
signals:
    void activated(const QString& graph, int node);
    void summaryChanged(int errors, int warnings);
private:
    void refresh();
    Document* doc_;
    QLabel* summary_;
    QTreeWidget* tree_;
};

// Output of Print String nodes, run messages and editor notifications.
class LogPanel : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);
    void append(int level, const QString& text);   // level: fg::LogLevel
};

// Call stack and variables of the running / paused task.
class DebugPanel : public QWidget {
    Q_OBJECT
public:
    DebugPanel(Document* doc, RunController* run, QWidget* parent = nullptr);
signals:
    void locationActivated(const QString& graph, int node);
private:
    void refresh();
    void fillVariables();
    Document* doc_;
    RunController* run_;
    QTreeWidget* stack_;
    QTreeWidget* vars_;
    bool updating_ = false;
};

} // namespace fged
