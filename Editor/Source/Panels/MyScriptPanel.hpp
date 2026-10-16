#pragma once
#include "../Document.hpp"

#include <QWidget>

class QTreeWidgetItem;
class CCollapsiblePanel;

namespace fged {

class FitTree;

// Overview of the script: event graphs with their events, functions, variables and local variables.
class MyScriptPanel : public QWidget {
    Q_OBJECT
public:
    enum class Kind { None, EventGraph, Function, Variable, Local, Event };

    explicit MyScriptPanel(Document* doc, QWidget* parent = nullptr);
    void setCurrentGraph(const QString& name);

signals:
    void openGraph(const QString& name);
    void eventSelected(const QString& graph, int node);     // click on an event: select its node (shows its details)
    void eventActivated(const QString& graph, int node);    // double click: also bring it into view
    // Kind::Variable -> owner empty; Kind::Local -> owner is the function; Kind::Function/EventGraph -> name is the graph
    void selectionChanged(int kind, const QString& owner, const QString& name);
    void graphRenamed(const QString& from, const QString& to);

private:
    // Events belong to the application; the list shows the ones this script handles.
    struct EventEntry {
        QString graph, name, params, doc;
        int node = 0;             // the handler node
        bool missing = false;     // the script handles an event this application does not provide
    };

    void scheduleRefresh();
    void refresh();
    std::vector<EventEntry> collectEvents() const;
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
    QString localsGraph_;      // the function whose local variables are listed (empty = section hidden)
    FitTree *graphs_ = nullptr, *functions_ = nullptr, *vars_ = nullptr, *locals_ = nullptr;
    CCollapsiblePanel *graphsPanel_ = nullptr, *functionsPanel_ = nullptr, *varsPanel_ = nullptr, *localsPanel_ = nullptr;
    bool building_ = false;
    bool refreshQueued_ = false;
    QString lastSignature_;
};

} // namespace fged
