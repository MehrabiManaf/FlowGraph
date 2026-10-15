#pragma once
#include "../Document.hpp"

#include <QWidget>

class QTreeWidgetItem;
class CCollapsiblePanel;

namespace fged {

class FitTree;

// Overview of the script: graphs, functions, variables, local variables and events.
class MyScriptPanel : public QWidget {
    Q_OBJECT
public:
    enum class Kind { None, EventGraph, Function, Variable, Local, Event };

    explicit MyScriptPanel(Document* doc, QWidget* parent = nullptr);
    void setCurrentGraph(const QString& name);

signals:
    void openGraph(const QString& name);
    void eventActivated(const QString& graph, int node);
    // Kind::Variable -> owner empty; Kind::Local -> owner is the function; Kind::Function/EventGraph -> name is the graph
    void selectionChanged(int kind, const QString& owner, const QString& name);
    void graphRenamed(const QString& from, const QString& to);

private:
    void scheduleRefresh();
    void refresh();
    FitTree* makeSection(const QString& title, bool plus, CCollapsiblePanel*& panel);
    void onItemChanged(FitTree* tree, QTreeWidgetItem* item);
    void onSelection(FitTree* tree);
    void contextMenu(FitTree* tree, const QPoint& pos);
    void addGraph();
    void addFunction();
    void addVariable();
    void addLocal();
    void beginRename(FitTree* tree, const QString& name);

    Document* doc_;
    QString current_;
    FitTree *graphs_ = nullptr, *functions_ = nullptr, *vars_ = nullptr, *locals_ = nullptr, *events_ = nullptr;
    CCollapsiblePanel *graphsPanel_ = nullptr, *functionsPanel_ = nullptr, *varsPanel_ = nullptr, *localsPanel_ = nullptr, *eventsPanel_ = nullptr;
    bool building_ = false;
    bool refreshQueued_ = false;
    QString lastSignature_;
};

} // namespace fged
