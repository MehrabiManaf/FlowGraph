#include "MyScriptPanel.hpp"
#include "../Common/CollapsiblePanel/CollapsiblePanel.hpp"
#include "../Graph/GraphView.hpp"
#include "../Graph/Style.hpp"
#include "FitTree.hpp"

#include <QMenu>
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
    graphs_ = makeSection("Event Graphs", true, graphsPanel_);
    functions_ = makeSection("Functions", true, functionsPanel_);
    vars_ = makeSection("Variables", true, varsPanel_);
    locals_ = makeSection("Local Variables", true, localsPanel_);
    events_ = makeSection("Events", false, eventsPanel_);
    for (auto* p : {graphsPanel_, functionsPanel_, varsPanel_, localsPanel_, eventsPanel_}) {
        lay->addWidget(p);
        p->SetContentsExpaned(true);
    }
    vars_->setDragEnabled(true);
    vars_->setDragMimeType(GraphView::variableMime());
    locals_->setDragEnabled(true);
    locals_->setDragMimeType(GraphView::variableMime());

    connect(graphsPanel_, &CCollapsiblePanel::PlusButtonClicked, this, [this] { addGraph(); });
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
        if (tree == graphs_ || tree == functions_) emit openGraph(name);
        else if (tree == events_) {
            QString graph = it->data(0, kMime).toString();
            if (!graph.isEmpty()) emit eventActivated(graph, it->data(0, kKind).toInt());
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

void MyScriptPanel::refresh() {
    const fg::Script& s = doc_->script();
    const fg::Registry& reg = doc_->registry();
    if (!localsGraph_.isEmpty()) {
        const fg::Graph* lg = s.findGraph(ss(localsGraph_));
        if (!lg || lg->kind != fg::GraphKind::Function) localsGraph_.clear();
    }
    // signature of everything that is displayed: skip the rebuild when nothing changed (keeps editors open)
    QString sig = localsGraph_ + "#";
    for (auto& g : s.graphs) sig += qs(g.name) + (g.kind == fg::GraphKind::Event ? "E" : "F") + "|";
    for (auto& v : s.variables) sig += qs(v.name) + ":" + qs(v.type) + "|";
    if (auto* g = s.findGraph(ss(localsGraph_)))
        for (auto& v : g->locals) sig += "L" + qs(v.name) + ":" + qs(v.type) + "|";
    for (auto& e : s.customEvents()) sig += "e" + qs(e.name) + qs(e.graph) + QString::number(e.node) + "|";
    for (auto* e : reg.events()) sig += "h" + qs(e->name) + "|";
    if (sig == lastSignature_) return;
    lastSignature_ = sig;

    building_ = true;
    QString keepName;
    FitTree* keepTree = nullptr;
    for (auto* t : {graphs_, functions_, vars_, locals_, events_})
        if (t->currentItem()) { keepTree = t; keepName = t->currentItem()->data(0, kName).toString(); }
    for (auto* t : {graphs_, functions_, vars_, locals_, events_}) t->clear();

    auto mk = [&](FitTree* tree, const QString& name, const QString& type, Kind kind, bool editable, const QColor* dot = nullptr) {
        auto* it = new QTreeWidgetItem(tree);
        it->setText(0, name);
        it->setText(1, type);
        it->setForeground(1, qApp->palette().color(QPalette::PlaceholderText));
        it->setData(0, kName, name);
        it->setData(0, kKind, int(kind));
        if (editable) it->setFlags(it->flags() | Qt::ItemIsEditable);
        if (dot) it->setIcon(0, dotIcon(*dot));
        return it;
    };
    for (auto& g : s.graphs) {
        if (g.kind == fg::GraphKind::Event) mk(graphs_, qs(g.name), "", Kind::EventGraph, true);
        else {
            QStringList in;
            for (auto& p : g.inputs) in << qs(p.name);
            mk(functions_, qs(g.name), "(" + in.join(", ") + ")", Kind::Function, true);
        }
    }
    for (auto& v : s.variables) {
        QColor c = rgb(reg.typeColor(reg.typeOrAny(v.type)));
        auto* it = mk(vars_, qs(v.name), qs(v.type), Kind::Variable, true, &c);
        it->setData(0, kMime, "global|" + qs(v.name));
    }
    if (auto* g = s.findGraph(ss(localsGraph_)); g && g->kind == fg::GraphKind::Function) {
        for (auto& v : g->locals) {
            QColor c = rgb(reg.typeColor(reg.typeOrAny(v.type)));
            auto* it = mk(locals_, qs(v.name), qs(v.type), Kind::Local, true, &c);
            it->setData(0, kMime, "local|" + qs(v.name));
        }
        localsPanel_->SetTitle("Local Variables (" + localsGraph_ + ")");
    }
    localsPanel_->setVisible(!localsGraph_.isEmpty());
    for (auto& e : s.customEvents()) {
        QStringList ps;
        for (auto& p : e.params) ps << qs(p.name);
        auto* it = mk(events_, qs(e.name), "(" + ps.join(", ") + ")", Kind::Event, false);
        it->setData(0, kMime, qs(e.graph));
        it->setData(0, kKind, e.node);
    }
    for (auto* e : reg.events()) {
        auto* it = mk(events_, qs(e->name), "host", Kind::Event, false);
        it->setForeground(0, qApp->palette().color(QPalette::PlaceholderText));
    }
    for (auto* t : {graphs_, functions_, vars_, locals_, events_}) t->fit();
    for (auto* p : {graphsPanel_, functionsPanel_, varsPanel_, localsPanel_, eventsPanel_}) p->UpdateContentsHeight();
    if (keepTree)
        for (int i = 0; i < keepTree->topLevelItemCount(); ++i)
            if (keepTree->topLevelItem(i)->data(0, kName).toString() == keepName) keepTree->setCurrentItem(keepTree->topLevelItem(i));
    building_ = false;
}

void MyScriptPanel::onSelection(FitTree* tree) {
    building_ = true;
    for (auto* t : {graphs_, functions_, vars_, locals_, events_})
        if (t != tree) { t->clearSelection(); t->setCurrentItem(nullptr); }
    building_ = false;
    auto* it = tree->currentItem();
    if (!it) return;
    QString name = it->data(0, kName).toString();
    if (tree == graphs_) {
        if (!localsGraph_.isEmpty()) { localsGraph_.clear(); scheduleRefresh(); }
        emit selectionChanged(int(Kind::EventGraph), "", name);
    }
    else if (tree == functions_) {
        if (localsGraph_ != name) { localsGraph_ = name; scheduleRefresh(); }
        emit selectionChanged(int(Kind::Function), "", name);
    }
    else if (tree == vars_) emit selectionChanged(int(Kind::Variable), "", name);
    else if (tree == locals_) emit selectionChanged(int(Kind::Local), localsGraph_, name);
    else emit selectionChanged(int(Kind::None), "", "");
}

void MyScriptPanel::beginRename(FitTree* tree, const QString& name) {
    QTimer::singleShot(0, this, [tree, name] {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, kName).toString() == name) {
                tree->setCurrentItem(tree->topLevelItem(i));
                tree->editItem(tree->topLevelItem(i), 0);
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
    if (tree == graphs_ || tree == functions_) {
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
}

void MyScriptPanel::contextMenu(FitTree* tree, const QPoint& pos) {
    auto* item = tree->itemAt(pos);
    if (!item) return;
    tree->setCurrentItem(item);
    QString name = item->data(0, kName).toString();
    QMenu m;
    QAction* open = (tree == graphs_ || tree == functions_) ? m.addAction("Open") : nullptr;
    QAction* rename = tree != events_ ? m.addAction("Rename") : nullptr;
    QAction* dup = tree == functions_ ? m.addAction("Duplicate") : nullptr;
    QAction* del = tree != events_ ? m.addAction("Delete") : nullptr;
    if (m.isEmpty()) return;
    QAction* a = m.exec(tree->viewport()->mapToGlobal(pos));
    if (!a) return;
    if (a == open) emit openGraph(name);
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
        if (tree == graphs_ && doc_->script().graphs.size() <= 1) return;
        doc_->edit("Delete", [&](fg::Script& s) {
            if (tree == graphs_ || tree == functions_) s.removeGraph(ss(name));
            else if (tree == vars_) s.variables.erase(std::remove_if(s.variables.begin(), s.variables.end(), [&](const fg::VariableDecl& v) { return v.name == ss(name); }), s.variables.end());
            else if (tree == locals_)
                if (auto* g = s.findGraph(ss(localsGraph_)))
                    g->locals.erase(std::remove_if(g->locals.begin(), g->locals.end(), [&](const fg::VariableDecl& v) { return v.name == ss(name); }), g->locals.end());
        });
    }
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
