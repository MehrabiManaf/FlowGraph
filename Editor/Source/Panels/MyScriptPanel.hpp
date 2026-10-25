// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../Document.hpp"

#include <QWidget>

class QLineEdit;
class QToolButton;
class QTreeWidgetItem;
class CCollapsiblePanel;

namespace fged {

class FitTree;

// Overview of the script: event graphs with their events, functions, variables, local variables and structs.
class MyScriptPanel : public QWidget {
    Q_OBJECT
public:
    enum class Kind { None, Function, Variable, Local, Event, Struct };

    explicit MyScriptPanel(Document* doc, QWidget* parent = nullptr);
    void setCurrentGraph(const QString& name);

signals:
    void openGraph(const QString& name);
    void eventSelected(const QString& graph, int node);     // click on an event: select its node (shows its details)
    void eventActivated(const QString& graph, int node);    // double click: also bring it into view
    // Kind::Variable -> owner empty; Kind::Local -> owner is the function; Kind::Function -> name is the graph; Kind::Struct -> name is the struct
    void selectionChanged(int kind, const QString& owner, const QString& name);
    void graphRenamed(const QString& from, const QString& to);
    // "Find References" of a function / variable / local variable (same kind / owner / name as selectionChanged)
    void referencesRequested(int kind, const QString& owner, const QString& name);

public:
    // The function / variable / local variable selected in the panel (Kind::None when there is none).
    Kind selected(QString* owner, QString* name) const;
    // The nodes selected in `graph` changed (the Details panel shows them now): the list follows, so it never shows an item as
    // selected while the Details panel shows something else. A single event node selects its row in Event Graphs; anything else
    // (other nodes, none) clears the selection of the list.
    void showNodeSelection(const QString& graph, const QList<int>& ids);

private:
    // Events belong to the application; the list shows the ones this script handles.
    struct EventEntry {
        QString graph, name, params, doc;
        int node = 0;             // the handler node
        bool missing = false;     // the script handles an event this application does not provide
    };

    void scheduleRefresh();
    void applyFilter();          // hides the items that do not match the search text (and the sections without a match)
    void showAddMenu();
    void refresh();
    void refreshLocals();        // only the Local Variables section (another function chosen, or its locals changed)
    std::vector<EventEntry> collectEvents() const;
    FitTree* makeSection(const QString& title, bool plus, CCollapsiblePanel*& panel);
    void onItemChanged(FitTree* tree, QTreeWidgetItem* item);
    void onSelection(FitTree* tree);
    void contextMenu(FitTree* tree, const QPoint& pos);
    void addFunction();
    void addVariable();
    void addLocal();
    void addStruct();
    void beginRename(FitTree* tree, const QString& name);
    CCollapsiblePanel* panelOf(FitTree* tree) const;
    void scrollTo(FitTree* tree, QTreeWidgetItem* item);      // in the scroll area of the dock

    Document* doc_;
    QString current_;
    QString localsGraph_;      // the function whose local variables are listed (empty = section hidden)
    FitTree *graphs_ = nullptr, *functions_ = nullptr, *vars_ = nullptr, *locals_ = nullptr, *structs_ = nullptr;
    CCollapsiblePanel *graphsPanel_ = nullptr, *functionsPanel_ = nullptr, *varsPanel_ = nullptr, *localsPanel_ = nullptr, *structsPanel_ = nullptr;
    QLineEdit* search_ = nullptr;
    QToolButton* addButton_ = nullptr;
    bool building_ = false;
    bool detailsShown_ = false;   // the Details panel shows the item selected here (a click on it again does not need to show it)
    bool refreshQueued_ = false;
    QString lastSignature_;      // what the lists of events, functions, variables and structs show
    QString lastLocalsSignature_;   // what the Local Variables section shows
};

} // namespace fged
