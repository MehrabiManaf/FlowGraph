#include "MyScriptPanel.hpp"
#include "../Common/CollapsiblePanel/CollapsiblePanel.hpp"
#include "../Graph/GraphView.hpp"
#include "../Graph/Style.hpp"
#include "FitTree.hpp"

#include <FlowGraph/Compatibility.hpp>

#include <QCursor>
#include <QMenu>
#include <QTreeWidgetItemIterator>
#include <map>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

namespace fged {

namespace {
constexpr int kName = Qt::UserRole;
constexpr int kKind = Qt::UserRole + 1;
constexpr int kMime = Qt::UserRole + 2;
constexpr int kNode = Qt::UserRole + 3;

QIcon dotIcon(const QColor& c) {
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, 10, 10);
    return QIcon(pm);
}
} // namespace

MyScriptPanel::MyScriptPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    lay->setAlignment(Qt::AlignTop);
    graphs_ = makeSection("Event Graphs", false, graphsPanel_);       // no "+": events are not created here
    graphs_->setRootIsDecorated(true);
    functions_ = makeSection("Functions", true, functionsPanel_);
    vars_ = makeSection("Variables", true, varsPanel_);
    locals_ = makeSection("Local Variables", true, localsPanel_);
    for (auto* p : {graphsPanel_, functionsPanel_, varsPanel_, localsPanel_}) {
        lay->addWidget(p);
        p->SetContentsExpaned(true);
    }
    vars_->setDragEnabled(true);
    vars_->setDragMimeType(GraphView::variableMime());
    locals_->setDragEnabled(true);
    locals_->setDragMimeType(GraphView::variableMime());

    connect(functionsPanel_, &CCollapsiblePanel::PlusButtonClicked, this, [this] { addFunction(); });
    connect(varsPanel_, &CCollapsiblePanel::PlusButtonClicked, this, [this] { addVariable(); });
    connect(localsPanel_, &CCollapsiblePanel::PlusButtonClicked, this, [this] { addLocal(); });
    connect(doc_, &Document::changed, this, [this] { scheduleRefresh(); });
    connect(doc_, &Document::scriptReplaced, this, [this] { scheduleRefresh(); });
    refresh();
}

FitTree* MyScriptPanel::makeSection(const QString& title, bool plus, CCollapsiblePanel*& panel) {
    panel = new CCollapsiblePanel(title, this, plus);
    auto* tree = new FitTree(panel);
    tree->setColumnCount(2);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    panel->AddWidget(tree, true);
    connect(tree, &QTreeWidget::itemChanged, this, [this, tree](QTreeWidgetItem* it, int col) {
        if (col == 0 && !building_) onItemChanged(tree, it);
    });
    connect(tree, &QTreeWidget::currentItemChanged, this, [this, tree](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (cur && !building_) onSelection(tree);
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this, tree](QTreeWidgetItem* it, int) {
        if (!it) return;
        QString name = it->data(0, kName).toString();
        if (tree == functions_) emit openGraph(name);
        else if (tree == graphs_) {
            if (it->data(0, kKind).toInt() == int(Kind::Event)) emit eventActivated(it->data(0, kMime).toString(), it->data(0, kNode).toInt());
            else if (it->data(0, kKind).toInt() == int(Kind::EventGraph)) emit openGraph(name);
        }
    });
    connect(tree, &QWidget::customContextMenuRequested, this, [this, tree](const QPoint& p) { contextMenu(tree, p); });
    return tree;
}

void MyScriptPanel::setCurrentGraph(const QString& name) {
    if (current_ == name) return;
    current_ = name;
    // local variables belong to a function: they are shown while a function graph is the active one
    const fg::Graph* g = doc_->script().findGraph(ss(name));
    localsGraph_ = (g && g->kind == fg::GraphKind::Function) ? name : QString();
    scheduleRefresh();
}

void MyScriptPanel::scheduleRefresh() {
    if (refreshQueued_) return;
    refreshQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshQueued_ = false;
        refresh();
    });
}

// The events the script handles (the application owns the events; the script only has handlers for them).
std::vector<MyScriptPanel::EventEntry> MyScriptPanel::collectEvents() const {
    std::vector<EventEntry> out;
    const fg::Registry& reg = doc_->registry();
    for (auto& g : doc_->script().graphs) {
        if (g.kind != fg::GraphKind::Event) continue;
        for (auto& n : g.nodes) {
            if (n.def.rfind("event:", 0) != 0) continue;
            EventEntry e;
            e.graph = qs(g.name);
            e.node = n.id;
            e.name = qs(n.def.substr(6));
            if (const fg::EventDesc* ev = reg.findEvent(n.def.substr(6))) {
                e.params = qs(fg::describeParams(*ev));
                e.doc = qs(ev->doc);
            } else {
                e.params = qs(n.prop("sig"));
                e.missing = true;
            }
            out.push_back(std::move(e));
        }
    }
    std::sort(out.begin(), out.end(), [](const EventEntry& a, const EventEntry& b) { return a.name < b.name; });
    return out;
}

void MyScriptPanel::refresh() {
    const fg::Script& s = doc_->script();
    const fg::Registry& reg = doc_->registry();
    if (!localsGraph_.isEmpty()) {
        const fg::Graph* lg = s.findGraph(ss(localsGraph_));
        if (!lg || lg->kind != fg::GraphKind::Function) localsGraph_.clear();
    }
    auto events = collectEvents();
    int eventGraphCount = 0;
    for (auto& g : s.graphs) eventGraphCount += g.kind == fg::GraphKind::Event;
    // signature of everything that is displayed: skip the rebuild when nothing changed (keeps editors open)
    QString sig = localsGraph_ + "#";
    for (auto& g : s.graphs) sig += qs(g.name) + (g.kind == fg::GraphKind::Event ? "E" : "F") + "|";
    for (auto& v : s.variables) sig += qs(v.name) + ":" + qs(v.type) + "|";
    if (auto* g = s.findGraph(ss(localsGraph_)))
        for (auto& v : g->locals) sig += "L" + qs(v.name) + ":" + qs(v.type) + "|";
    for (auto& e : events) sig += "e" + e.graph + "/" + e.name + e.params + QString::number(e.node) + (e.missing ? "m" : "") + "|";
    if (sig == lastSignature_) return;
    lastSignature_ = sig;

    building_ = true;
    QString keepName;
    FitTree* keepTree = nullptr;
    for (auto* t : {graphs_, functions_, vars_, locals_})
        if (t->currentItem()) { keepTree = t; keepName = t->currentItem()->data(0, kName).toString() + "@" + t->currentItem()->data(0, kMime).toString(); }
    for (auto* t : {graphs_, functions_, vars_, locals_}) t->clear();

    auto mk = [&](FitTree* tree, QTreeWidgetItem* parent, const QString& name, const QString& type, Kind kind, bool editable, const QColor* dot = nullptr) {
        auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        it->setText(0, name);
        it->setText(1, type);
        it->setForeground(1, qApp->palette().color(QPalette::PlaceholderText));
        it->setData(0, kName, name);
        it->setData(0, kKind, int(kind));
        if (editable) it->setFlags(it->flags() | Qt::ItemIsEditable);
        if (dot) it->setIcon(0, dotIcon(*dot));
        return it;
    };
    // event graphs: with a single graph its events are listed directly, otherwise they are grouped below their graph
    std::map<QString, QTreeWidgetItem*> graphItems;
    for (auto& g : s.graphs) {
        if (g.kind == fg::GraphKind::Function) {
            QStringList in;
            for (auto& p : g.inputs) in << qs(p.name);
            mk(functions_, nullptr, qs(g.name), "(" + in.join(", ") + ")", Kind::Function, true);
        } else if (eventGraphCount > 1) {
            graphItems[qs(g.name)] = mk(graphs_, nullptr, qs(g.name), "", Kind::EventGraph, true);
        }
    }
    for (auto& e : events) {
        QTreeWidgetItem* parent = eventGraphCount > 1 ? graphItems[e.graph] : nullptr;
        auto* it = mk(graphs_, parent, e.name, e.missing ? e.params + "  - not provided by this application" : e.params, Kind::Event, false);
        it->setData(0, kMime, e.graph);
        it->setData(0, kNode, e.node);
        it->setToolTip(0, e.doc);
        it->setForeground(0, e.missing ? QColor(255, 120, 120) : qApp->palette().color(QPalette::Link));
    }
    graphs_->expandAll();
    for (auto& v : s.variables) {
        QColor c = rgb(reg.typeColor(reg.typeOrAny(v.type)));
        auto* it = mk(vars_, nullptr, qs(v.name), qs(v.type), Kind::Variable, true, &c);
        it->setData(0, kMime, "global|" + qs(v.name));
    }
    if (auto* g = s.findGraph(ss(localsGraph_)); g && g->kind == fg::GraphKind::Function) {
        for (auto& v : g->locals) {
            QColor c = rgb(reg.typeColor(reg.typeOrAny(v.type)));
            auto* it = mk(locals_, nullptr, qs(v.name), qs(v.type), Kind::Local, true, &c);
            it->setData(0, kMime, "local|" + qs(v.name));
        }
        localsPanel_->SetTitle("Local Variables (" + localsGraph_ + ")");
    }
    localsPanel_->setVisible(!localsGraph_.isEmpty());
    for (auto* t : {graphs_, functions_, vars_, locals_}) t->fit();
    for (auto* p : {graphsPanel_, functionsPanel_, varsPanel_, localsPanel_}) p->UpdateContentsHeight();
    if (keepTree) {
        for (QTreeWidgetItemIterator it(keepTree); *it; ++it)
            if ((*it)->data(0, kName).toString() + "@" + (*it)->data(0, kMime).toString() == keepName) { keepTree->setCurrentItem(*it); break; }
    }
    building_ = false;
}

void MyScriptPanel::onSelection(FitTree* tree) {
    building_ = true;
    for (auto* t : {graphs_, functions_, vars_, locals_})
        if (t != tree) { t->clearSelection(); t->setCurrentItem(nullptr); }
    building_ = false;
    auto* it = tree->currentItem();
    if (!it) return;
    QString name = it->data(0, kName).toString();
    if (tree == graphs_) {
        if (!localsGraph_.isEmpty()) { localsGraph_.clear(); scheduleRefresh(); }
        if (it->data(0, kKind).toInt() == int(Kind::Event)) emit eventSelected(it->data(0, kMime).toString(), it->data(0, kNode).toInt());
        else if (it->data(0, kKind).toInt() == int(Kind::EventGraph)) emit selectionChanged(int(Kind::EventGraph), "", name);
        else emit selectionChanged(int(Kind::None), "", "");
    } else if (tree == functions_) {
        if (localsGraph_ != name) { localsGraph_ = name; scheduleRefresh(); }
        emit selectionChanged(int(Kind::Function), "", name);
    }
    else if (tree == vars_) emit selectionChanged(int(Kind::Variable), "", name);
    else if (tree == locals_) emit selectionChanged(int(Kind::Local), localsGraph_, name);
    else emit selectionChanged(int(Kind::None), "", "");
}

void MyScriptPanel::beginRename(FitTree* tree, const QString& name) {
    QTimer::singleShot(0, this, [tree, name] {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, kName).toString() == name) {
                tree->setCurrentItem(*it);
                tree->editItem(*it, 0);
                break;
            }
    });
}

void MyScriptPanel::onItemChanged(FitTree* tree, QTreeWidgetItem* item) {
    QString oldName = item->data(0, kName).toString();
    QString newName = item->text(0).trimmed();
    auto revert = [&] {
        building_ = true;
        item->setText(0, oldName);
        building_ = false;
    };
    if (newName == oldName) return;
    const fg::Script& s = doc_->script();
    if (newName.isEmpty()) return revert();
    if (tree == graphs_ && item->data(0, kKind).toInt() == int(Kind::Event)) {
        return revert();                                // events belong to the application
    } else if (tree == graphs_ || tree == functions_) {
        if (s.graphIndex(ss(newName)) >= 0) { QMessageBox::warning(this, "Rename", "A graph named '" + newName + "' already exists."); return revert(); }
        bool fn = tree == functions_;
        doc_->edit(fn ? "Rename function" : "Rename graph", [&](fg::Script& sc) {
            if (fn) sc.renameFunction(ss(oldName), ss(newName));
            else if (auto* g = sc.findGraph(ss(oldName))) g->name = ss(newName);
        });
        emit graphRenamed(oldName, newName);
    } else if (tree == vars_) {
        if (s.variableIndex(ss(newName)) >= 0) { QMessageBox::warning(this, "Rename", "A variable named '" + newName + "' already exists."); return revert(); }
        doc_->edit("Rename variable", [&](fg::Script& sc) { sc.renameVariable(ss(oldName), ss(newName)); });
    } else if (tree == locals_) {
        const fg::Graph* g = s.findGraph(ss(localsGraph_));
        if (!g || g->findLocal(ss(newName))) return revert();
        doc_->edit("Rename local variable", [&](fg::Script& sc) { sc.renameLocal(ss(localsGraph_), ss(oldName), ss(newName)); });
    }
    item->setData(0, kName, newName);
    // the details panel still shows the old name: tell it about the renamed item
    if (tree->currentItem() == item) {
        if (tree == functions_) localsGraph_ = newName;
        onSelection(tree);
    }
}

void MyScriptPanel::contextMenu(FitTree* tree, const QPoint& pos) {
    auto* item = tree->itemAt(pos);
    if (!item) return;
    tree->setCurrentItem(item);
    QString name = item->data(0, kName).toString();
    const bool isEvent = tree == graphs_ && item->data(0, kKind).toInt() == int(Kind::Event);
    if (tree == graphs_ && !isEvent && item->data(0, kKind).toInt() != int(Kind::EventGraph)) return;      // the folder of available events
    QString graph = item->data(0, kMime).toString();
    int node = item->data(0, kNode).toInt();
    QMenu m;
    QAction *open = nullptr, *remove = nullptr, *rename = nullptr, *dup = nullptr, *del = nullptr, *newGraph = nullptr;
    if (isEvent) {
        open = m.addAction("Go to handler");
        remove = m.addAction("Remove handler");
    } else {
        open = m.addAction("Open");
        rename = m.addAction("Rename");
        if (tree == functions_) dup = m.addAction("Duplicate");
        del = m.addAction("Delete");
    }
    if (tree == graphs_) {
        m.addSeparator();
        newGraph = m.addAction("New event graph");
    }
    QAction* a = m.exec(tree->viewport()->mapToGlobal(pos));
    if (!a) return;
    if (a == open) {
        if (isEvent) emit eventActivated(graph, node);
        else emit openGraph(name);
    } else if (a == newGraph) addGraph();
    else if (a == remove) removeHandler(graph, node);
    else if (a == rename) tree->editItem(item, 0);
    else if (a == dup) {
        doc_->edit("Duplicate function", [&](fg::Script& s) {
            const fg::Graph* g = s.findGraph(ss(name));
            if (!g) return;
            fg::Graph copy = *g;
            copy.name = s.uniqueGraphName(ss(name) + "_Copy");
            s.graphs.push_back(std::move(copy));
        });
    } else if (a == del) {
        int eventGraphs = 0;
        for (auto& g : doc_->script().graphs) eventGraphs += g.kind == fg::GraphKind::Event;
        if (tree == graphs_ && eventGraphs <= 1) return;
        doc_->edit("Delete", [&](fg::Script& s) {
            if (tree == graphs_ || tree == functions_) s.removeGraph(ss(name));
            else if (tree == vars_) s.variables.erase(std::remove_if(s.variables.begin(), s.variables.end(), [&](const fg::VariableDecl& v) { return v.name == ss(name); }), s.variables.end());
            else if (tree == locals_)
                if (auto* g = s.findGraph(ss(localsGraph_)))
                    g->locals.erase(std::remove_if(g->locals.begin(), g->locals.end(), [&](const fg::VariableDecl& v) { return v.name == ss(name); }), g->locals.end());
        });
    }
}

void MyScriptPanel::removeHandler(const QString& graph, int node) {
    doc_->edit("Remove event handler", [&](fg::Script& s) {
        auto* g = s.findGraph(ss(graph));
        if (!g) return;
        g->connections.erase(std::remove_if(g->connections.begin(), g->connections.end(), [&](const fg::Connection& c) { return c.fromNode == node || c.toNode == node; }), g->connections.end());
        g->nodes.erase(std::remove_if(g->nodes.begin(), g->nodes.end(), [&](const fg::Node& n) { return n.id == node; }), g->nodes.end());
    });
}

void MyScriptPanel::addGraph() {
    QString name = qs(doc_->script().uniqueGraphName("EventGraph"));
    doc_->edit("Add event graph", [&](fg::Script& s) { s.addEventGraph(ss(name)); });
    emit openGraph(name);
    beginRename(graphs_, name);
}

void MyScriptPanel::addFunction() {
    QString name = qs(doc_->script().uniqueGraphName("NewFunction"));
    doc_->edit("Add function", [&](fg::Script& s) { s.addFunction(ss(name)); });
    emit openGraph(name);
    beginRename(functions_, name);
}

void MyScriptPanel::addVariable() {
    QString name = qs(doc_->script().uniqueVariableName("NewVar"));
    doc_->edit("Add variable", [&](fg::Script& s) { s.variables.push_back({ss(name), "int", fg::Value(int64_t(0)), "", ""}); });
    beginRename(vars_, name);
}

void MyScriptPanel::addLocal() {
    const fg::Graph* g = doc_->script().findGraph(ss(localsGraph_));
    if (!g || g->kind != fg::GraphKind::Function) return;
    QString name = "NewLocal";
    for (int i = 1; g->findLocal(ss(name)); ++i) name = "NewLocal_" + QString::number(i);
    doc_->edit("Add local variable", [&](fg::Script& s) {
        if (auto* gr = s.findGraph(ss(localsGraph_))) gr->locals.push_back({ss(name), "int", fg::Value(int64_t(0)), "", ""});
    });
    beginRename(locals_, name);
}

} // namespace fged
