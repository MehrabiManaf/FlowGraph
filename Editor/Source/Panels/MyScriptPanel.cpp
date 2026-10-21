#include "MyScriptPanel.hpp"
#include "../Common/Theme.hpp"
#include "../Common/CollapsiblePanel/CollapsiblePanel.hpp"
#include "../Common/SearchField.hpp"
#include "../Graph/GraphView.hpp"
#include "../Graph/Style.hpp"
#include "FitTree.hpp"

#include <FlowGraph/Compatibility.hpp>

#include <QComboBox>
#include <QCursor>
#include <QImage>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QMenu>
#include <QTreeWidgetItemIterator>
#include <cmath>
#include <map>
#include <memory>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOption>
#include <QStylePainter>
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

// The drop-down arrow of a combo box of the current style, cut out of a rendered combo box (so that it has exactly the
// size and shape of the arrow of the launch box). The pixels that differ from the background of the box are kept,
// with the strongest one as the colour and the difference as the opacity.
QImage comboArrow(qreal dpr) {
    QComboBox box;
    box.ensurePolished();
    box.resize(120, box.sizeHint().height());    QImage full(box.size() * dpr, QImage::Format_ARGB32_Premultiplied);
    full.setDevicePixelRatio(dpr);
    full.fill(Qt::transparent);
    box.render(&full);

    QStyleOptionComboBox opt;
    opt.initFrom(&box);
    opt.rect = box.rect();
    opt.editable = false;
    const QRect cell = box.style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, &box);
    const QRect px = QRect(cell.topLeft() * dpr, cell.size() * dpr).intersected(full.rect()).adjusted(0, 3, -2, -3);   // not the frame
    auto lum = [](QRgb c) { return qRed(c) * 3 + qGreen(c) * 6 + qBlue(c); };
    // the box has a vertical gradient: the background of a row is the pixel at the left edge of the cell
    auto diff = [&](int x, int y) { return std::abs(lum(full.pixel(x, y)) - lum(full.pixel(px.left(), y))); };
    int best = 0;
    QRgb arrowColor = 0;
    for (int y = px.top(); y <= px.bottom(); ++y)
        for (int x = px.left(); x <= px.right(); ++x)
            if (diff(x, y) > best) { best = diff(x, y); arrowColor = full.pixel(x, y); }
    QImage out(px.size(), QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    if (best == 0) return out;
    for (int y = 0; y < px.height(); ++y)
        for (int x = 0; x < px.width(); ++x) {
            const double a = std::clamp((double(diff(px.left() + x, px.top() + y)) / best - 0.04) / 0.96, 0.0, 1.0);
            QColor c(arrowColor);
            c.setAlphaF(a);
            out.setPixelColor(x, y, c);
        }
    return out;
}

// The add button: a tool button that opens its menu, drawn without the style's small menu arrow (the arrow is part of its icon).
// Not done with a style sheet ("::menu-indicator { image: none }"): a widget with a style sheet keeps the palette it was polished
// with, so the button stayed in the colours of the old theme when the theme changed.
class AddButton : public QToolButton {
public:
    using QToolButton::QToolButton;
protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter p(this);
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        opt.features &= ~QStyleOptionToolButton::HasMenu;
        p.drawComplexControl(QStyle::CC_ToolButton, opt);
    }
};

// The icon of the add button: the green plus and, behind a gap, the drop-down arrow of the combo boxes (the launch
// box of the toolbar), so the button reads as "opens a list".
QIcon addMenuIcon(const QWidget* ref, int height, QSize* logicalSize) {
    const qreal dpr = ref->devicePixelRatioF();
    QImage arrow = comboArrow(dpr);
    arrow.setDevicePixelRatio(1.0);
    // everything in device pixels, so that nothing is resampled when the icon is drawn
    const int plusPx = qRound(14 * dpr), gapPx = qRound(12 * dpr), heightPx = qRound(height * dpr);
    const int logicalWidth = int(std::ceil((plusPx + gapPx + arrow.width()) / dpr));
    QImage img(qRound(logicalWidth * dpr), heightPx, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.drawPixmap(QRect(0, (heightPx - plusPx) / 2, plusPx, plusPx), themedIcon(":/Icons/add.svg").pixmap(QSize(plusPx, plusPx)));
    p.drawImage(QPoint(plusPx + gapPx, (heightPx - arrow.height()) / 2), arrow);
    p.end();
    img.setDevicePixelRatio(dpr);
    if (logicalSize) *logicalSize = QSize(logicalWidth, height);
    return QIcon(QPixmap::fromImage(img));
}

// A row of one of the lists: name, type (greyed) and the data the panel finds it by.
QTreeWidgetItem* mk(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& name, const QString& type, MyScriptPanel::Kind kind, bool editable,
                    const QColor* dot = nullptr) {
    auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    it->setText(0, name);
    it->setText(1, type);
    it->setForeground(1, qApp->palette().color(QPalette::PlaceholderText));
    it->setData(0, kName, name);
    it->setData(0, kKind, int(kind));
    if (editable) it->setFlags(it->flags() | Qt::ItemIsEditable);
    if (dot) it->setIcon(0, dotIcon(*dot));
    return it;
}
} // namespace

MyScriptPanel::MyScriptPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    lay->setAlignment(Qt::AlignTop);

    // search box on the left, "add" button with its menu on the right
    auto* top = new QHBoxLayout;
    top->setContentsMargins(4, 4, 4, 2);
    top->setSpacing(4);
    search_ = new QLineEdit(this);
    makeSearchField(search_, "Search");
    search_->setToolTip("Shows only the events, functions and variables whose name contains this text");
    addButton_ = new AddButton(this);
    QSize iconSize;
    addButton_->setIcon(addMenuIcon(search_, search_->sizeHint().height(), &iconSize));       // a plus and a drop-down arrow: the button opens a menu
    addButton_->setIconSize(iconSize);
    // the arrow of that icon is cut from a combo box in the colours of the theme, and the lists colour missing events: both follow it
    connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        QSize size;
        addButton_->setIcon(addMenuIcon(search_, search_->sizeHint().height(), &size));
        addButton_->setIconSize(size);
        scheduleRefresh();
    });
    addButton_->setText("+");                         // not shown (icon only), but names the button
    addButton_->setToolTip("Add a function or a variable");
    addButton_->setPopupMode(QToolButton::InstantPopup);
    addButton_->setFixedSize(iconSize.width() + 12, search_->sizeHint().height());
    top->addWidget(search_, 1);
    top->addWidget(addButton_);
    lay->addLayout(top);
    auto* menu = new QMenu(addButton_);
    addButton_->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this] { showAddMenu(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });

    graphs_ = makeSection("Event Graphs", false, graphsPanel_);       // no "+": events are not created here
    graphs_->setRootIsDecorated(true);
    functions_ = makeSection("Functions", true, functionsPanel_);
    vars_ = makeSection("Variables", true, varsPanel_);
    locals_ = makeSection("Local Variables", true, localsPanel_);
    for (auto* p : {graphsPanel_, functionsPanel_, varsPanel_, localsPanel_}) {
        lay->addWidget(p);
        p->SetContentsExpaned(true);
    }
    functions_->setDragEnabled(true);                     // a function dropped on a graph makes a node that calls it
    functions_->setDragMimeType(GraphView::functionMime());
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
    // a click on the item that is already selected shows its details again when the Details panel shows something else
    connect(tree, &QTreeWidget::itemClicked, this, [this, tree](QTreeWidgetItem* it, int) {
        if (it && !building_ && !detailsShown_ && it == tree->currentItem()) onSelection(tree);
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this, tree](QTreeWidgetItem* it, int) {
        if (!it) return;
        QString name = it->data(0, kName).toString();
        if (tree == functions_) emit openGraph(name);
        else if (tree == graphs_ && it->data(0, kKind).toInt() == int(Kind::Event))
            emit eventActivated(it->data(0, kMime).toString(), it->data(0, kNode).toInt());
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
    // signature of everything the lists of events, functions and variables display: skip their rebuild when nothing changed (keeps
    // editors open). The Local Variables section has its own: choosing another function (a press on it) must not rebuild the list
    // of functions, or the pressed item is gone before the mouse moves and the drag of the function does not start.
    QString sig;
    for (auto& g : s.graphs) {
        sig += qs(g.name) + (g.kind == fg::GraphKind::Event ? "E" : "F") + "|";
        if (g.kind == fg::GraphKind::Function)      // the "(...)" shown next to the function's name follows its inputs
            for (auto& p : g.inputs) sig += qs(p.name) + ":" + qs(p.type) + ",";
    }
    for (auto& v : s.variables) sig += qs(v.name) + ":" + qs(v.type) + "|";
    for (auto& e : events) sig += "e" + e.graph + "/" + e.name + e.params + QString::number(e.node) + (e.missing ? "m" : "") + "|";
    if (sig == lastSignature_) {
        refreshLocals();
        return;
    }
    lastSignature_ = sig;

    building_ = true;
    QString keepName;
    FitTree* keepTree = nullptr;
    for (auto* t : {graphs_, functions_, vars_})
        if (t->currentItem()) { keepTree = t; keepName = t->currentItem()->data(0, kName).toString() + "@" + t->currentItem()->data(0, kMime).toString(); }
    for (auto* t : {graphs_, functions_, vars_}) t->clear();

    for (auto& g : s.graphs) {
        if (g.kind != fg::GraphKind::Function) continue;
        QStringList in;
        for (auto& p : g.inputs) in << qs(p.name);
        auto* it = mk(functions_, nullptr, qs(g.name), "(" + in.join(", ") + ")", Kind::Function, true);
        it->setData(0, kMime, qs(g.name));                // what a drag of the function carries
    }
    // event graphs: only the events the script handles (the graphs themselves are not listed and cannot be created)
    for (auto& e : events) {
        auto* it = mk(graphs_, nullptr, e.name, e.missing ? e.params + "  - not provided by this application" : e.params, Kind::Event, false);
        it->setData(0, kMime, e.graph);
        it->setData(0, kNode, e.node);
        it->setToolTip(0, e.doc);
        it->setForeground(0, e.missing ? themeColors().error : qApp->palette().color(QPalette::Link));
    }
    graphs_->expandAll();
    for (auto& v : s.variables) {
        QColor c = rgb(reg.typeColor(reg.typeOrAny(v.type)));
        auto* it = mk(vars_, nullptr, qs(v.name), qs(v.type), Kind::Variable, true, &c);
        it->setData(0, kMime, "global|" + qs(v.name));
    }
    if (keepTree) {
        for (QTreeWidgetItemIterator it(keepTree); *it; ++it)
            if ((*it)->data(0, kName).toString() + "@" + (*it)->data(0, kMime).toString() == keepName) { keepTree->setCurrentItem(*it); break; }
    }
    building_ = false;
    applyFilter();
    refreshLocals();
}

void MyScriptPanel::refreshLocals() {
    const fg::Script& s = doc_->script();
    const fg::Registry& reg = doc_->registry();
    const fg::Graph* g = s.findGraph(ss(localsGraph_));
    if (g && g->kind != fg::GraphKind::Function) g = nullptr;
    QString sig = localsGraph_ + "#";
    if (g)
        for (auto& v : g->locals) sig += qs(v.name) + ":" + qs(v.type) + "|";
    if (sig == lastLocalsSignature_) return;
    lastLocalsSignature_ = sig;

    building_ = true;
    QString keepName;
    if (locals_->currentItem()) keepName = locals_->currentItem()->data(0, kName).toString();
    locals_->clear();
    if (g) {
        for (auto& v : g->locals) {
            QColor c = rgb(reg.typeColor(reg.typeOrAny(v.type)));
            auto* it = mk(locals_, nullptr, qs(v.name), qs(v.type), Kind::Local, true, &c);
            it->setData(0, kMime, "local|" + qs(v.name));
            if (!keepName.isEmpty() && qs(v.name) == keepName) locals_->setCurrentItem(it);
        }
        localsPanel_->SetTitle("Local Variables (" + localsGraph_ + ")");
    }
    localsPanel_->setVisible(g != nullptr);
    applyFilter();
    building_ = false;
}

void MyScriptPanel::applyFilter() {
    const QString text = search_ ? search_->text().trimmed() : QString();
    const std::pair<FitTree*, CCollapsiblePanel*> sections[] = {{graphs_, graphsPanel_}, {functions_, functionsPanel_}, {vars_, varsPanel_}, {locals_, localsPanel_}};
    for (auto& [tree, panel] : sections) {
        int shown = 0;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const bool match = text.isEmpty() || (*it)->text(0).contains(text, Qt::CaseInsensitive);
            (*it)->setHidden(!match);
            shown += match;
        }
        // local variables only exist while a function is selected; a section without a match is not shown while searching
        const bool available = tree != locals_ || !localsGraph_.isEmpty();
        panel->setVisible(available && (text.isEmpty() || shown > 0));
        tree->fit();
        panel->UpdateContentsHeight();
    }
}

void MyScriptPanel::showAddMenu() {
    QMenu* menu = addButton_->menu();
    menu->clear();
    menu->addAction(themedIcon(":/Icons/function.svg"), "Function", this, [this] { addFunction(); });
    menu->addAction(themedIcon(":/Icons/variable.svg"), "Variable", this, [this] { addVariable(); });
    if (!localsGraph_.isEmpty()) menu->addAction(themedIcon(":/Icons/local-variable.svg"), "Local Variable (" + localsGraph_ + ")", this, [this] { addLocal(); });
}

void MyScriptPanel::onSelection(FitTree* tree) {
    building_ = true;
    for (auto* t : {graphs_, functions_, vars_, locals_})
        if (t != tree) { t->clearSelection(); t->setCurrentItem(nullptr); }
    building_ = false;
    auto* it = tree->currentItem();
    if (!it) return;
    detailsShown_ = true;
    QString name = it->data(0, kName).toString();
    if (tree == graphs_) {
        if (!localsGraph_.isEmpty()) { localsGraph_.clear(); scheduleRefresh(); }
        if (it->data(0, kKind).toInt() == int(Kind::Event)) emit eventSelected(it->data(0, kMime).toString(), it->data(0, kNode).toInt());
        else emit selectionChanged(int(Kind::None), "", "");
    } else if (tree == functions_) {
        if (localsGraph_ != name) { localsGraph_ = name; scheduleRefresh(); }
        emit selectionChanged(int(Kind::Function), "", name);
    }
    else if (tree == vars_) emit selectionChanged(int(Kind::Variable), "", name);
    else if (tree == locals_) emit selectionChanged(int(Kind::Local), localsGraph_, name);
    else emit selectionChanged(int(Kind::None), "", "");
}

MyScriptPanel::Kind MyScriptPanel::selected(QString* owner, QString* name) const {
    auto pick = [&](FitTree* tree, Kind kind, const QString& o) {
        QTreeWidgetItem* it = tree ? tree->currentItem() : nullptr;
        if (!it || !it->isSelected()) return false;
        if (owner) *owner = o;
        if (name) *name = it->data(0, kName).toString();
        return true;
    };
    if (pick(functions_, Kind::Function, QString())) return Kind::Function;
    if (pick(vars_, Kind::Variable, QString())) return Kind::Variable;
    if (pick(locals_, Kind::Local, localsGraph_)) return Kind::Local;
    return Kind::None;
}

void MyScriptPanel::showNodeSelection(const QString& graph, const QList<int>& ids) {
    QTreeWidgetItem* event = nullptr;
    if (ids.size() == 1)
        for (QTreeWidgetItemIterator it(graphs_); *it; ++it)
            if ((*it)->data(0, kKind).toInt() == int(Kind::Event) && (*it)->data(0, kMime).toString() == graph &&
                (*it)->data(0, kNode).toInt() == ids.front()) {
                event = *it;
                break;
            }
    building_ = true;
    for (auto* t : {graphs_, functions_, vars_, locals_})
        if (!(event && t == graphs_)) { t->clearSelection(); t->setCurrentItem(nullptr); }
    if (event) graphs_->setCurrentItem(event);
    building_ = false;
    detailsShown_ = event != nullptr;
}

CCollapsiblePanel* MyScriptPanel::panelOf(FitTree* tree) const {
    if (tree == graphs_) return graphsPanel_;
    if (tree == functions_) return functionsPanel_;
    if (tree == vars_) return varsPanel_;
    if (tree == locals_) return localsPanel_;
    return nullptr;
}

void MyScriptPanel::scrollTo(FitTree* tree, QTreeWidgetItem* item) {
    for (QWidget* w = parentWidget(); w; w = w->parentWidget())
        if (auto* area = qobject_cast<QScrollArea*>(w)) {
            if (!area->widget() || !area->widget()->isAncestorOf(tree)) return;
            const QRect r = tree->visualItemRect(item);
            const QPoint c = tree->viewport()->mapTo(area->widget(), r.center());
            area->ensureVisible(c.x(), c.y(), 0, r.height());
            return;
        }
}

void MyScriptPanel::beginRename(FitTree* tree, const QString& name) {
    // a collapsed section opens first: the new item and its name editor must be seen
    CCollapsiblePanel* panel = panelOf(tree);
    const bool opening = panel && !panel->IsContentsExpanded();
    if (opening) panel->SetContentsExpaned(true);
    auto find = [tree, name]() -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, kName).toString() == name) return *it;
        return nullptr;
    };
    QTimer::singleShot(0, this, [this, tree, panel, opening, find] {
        QTreeWidgetItem* item = find();
        if (!item) return;
        tree->setCurrentItem(item);
        tree->editItem(item, 0);
        if (!opening) { scrollTo(tree, item); return; }
        // the section is still growing: scroll when it has its full height (the item is looked up again then)
        auto once = std::make_shared<QMetaObject::Connection>();
        *once = connect(panel, &CCollapsiblePanel::ExpandFinished, this, [this, tree, find, once] {
            disconnect(*once);
            if (QTreeWidgetItem* again = find()) scrollTo(tree, again);
        });
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
    if (tree == graphs_ && !isEvent) return;
    QString graph = item->data(0, kMime).toString();
    int node = item->data(0, kNode).toInt();
    QMenu m;
    QAction *open = nullptr, *rename = nullptr, *dup = nullptr, *del = nullptr, *refs = nullptr;
    if (isEvent) {
        open = m.addAction(themedIcon(":/Icons/goto.svg"), "Go to handler");        // events belong to the application: they cannot be removed
    } else {
        if (tree == functions_) open = m.addAction(themedIcon(":/Icons/open-function.svg"), "Open");     // only a function has a graph to open
        rename = m.addAction(themedIcon(":/Icons/rename.svg"), "Rename");
        if (tree == functions_) dup = m.addAction(themedIcon(":/Icons/duplicate.svg"), "Duplicate");
        del = m.addAction(themedIcon(":/Icons/delete.svg"), "Delete");
        m.addSeparator();
        refs = m.addAction(themedIcon(":/Icons/find-references.svg"), "Find References");
        refs->setShortcut(QKeySequence("Shift+F12"));
    }
    QAction* a = m.exec(tree->viewport()->mapToGlobal(pos));
    if (!a) return;
    if (a == refs) {
        const Kind kind = tree == functions_ ? Kind::Function : tree == vars_ ? Kind::Variable : Kind::Local;
        emit referencesRequested(int(kind), kind == Kind::Local ? localsGraph_ : QString(), name);
        return;
    }
    if (a == open) {
        if (isEvent) emit eventActivated(graph, node);
        else emit openGraph(name);
    }
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
        doc_->edit("Delete", [&](fg::Script& s) {
            if (tree == functions_) s.removeGraph(ss(name));
            else if (tree == vars_) s.variables.erase(std::remove_if(s.variables.begin(), s.variables.end(), [&](const fg::VariableDecl& v) { return v.name == ss(name); }), s.variables.end());
            else if (tree == locals_)
                if (auto* g = s.findGraph(ss(localsGraph_)))
                    g->locals.erase(std::remove_if(g->locals.begin(), g->locals.end(), [&](const fg::VariableDecl& v) { return v.name == ss(name); }), g->locals.end());
        });
    }
}

void MyScriptPanel::addFunction() {
    search_->clear();                // the new item would be hidden by the filter
    QString name = qs(doc_->script().uniqueGraphName("NewFunction"));
    doc_->edit("Add function", [&](fg::Script& s) { s.addFunction(ss(name)); });
    emit openGraph(name);
    beginRename(functions_, name);
}

void MyScriptPanel::addVariable() {
    search_->clear();
    QString name = qs(doc_->script().uniqueVariableName("NewVar"));
    doc_->edit("Add variable", [&](fg::Script& s) { s.variables.push_back({ss(name), "int", fg::Value(int64_t(0)), "", ""}); });
    beginRename(vars_, name);
}

void MyScriptPanel::addLocal() {
    const fg::Graph* g = doc_->script().findGraph(ss(localsGraph_));
    if (!g || g->kind != fg::GraphKind::Function) return;
    search_->clear();
    QString name = "NewLocal";
    for (int i = 1; g->findLocal(ss(name)); ++i) name = "NewLocal_" + QString::number(i);
    doc_->edit("Add local variable", [&](fg::Script& s) {
        if (auto* gr = s.findGraph(ss(localsGraph_))) gr->locals.push_back({ss(name), "int", fg::Value(int64_t(0)), "", ""});
    });
    beginRename(locals_, name);
}

} // namespace fged
