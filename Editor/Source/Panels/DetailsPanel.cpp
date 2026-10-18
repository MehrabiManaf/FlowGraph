#include "DetailsPanel.hpp"
#include "../Graph/Style.hpp"
#include "../Studio/RunController.hpp"

#include <FlowGraph/Compatibility.hpp>
#include "../Common/CollapsiblePanel/CollapsiblePanel.hpp"
#include "TypeEdit.hpp"
#include "TypeSpec.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCompleter>
#include <QElapsedTimer>
#include <QComboBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QHBoxLayout>
#include <set>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace fged {

namespace {

QLabel* heading(const QString& t) {
    auto* l = new QLabel("<b>" + t.toHtmlEscaped() + "</b>");
    l->setProperty("fgHeading", true);
    return l;
}

QLabel* dim(const QString& t) {
    auto* l = new QLabel(t);
    l->setWordWrap(true);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

// After a parameter of a function was renamed, the pins of the nodes that use it must follow.
void renamePins(fg::Script& s, const std::string& graphName, bool isInput, const std::string& from, const std::string& to) {
    auto renameOnNode = [&](fg::Graph& g, fg::NodeId nodeId, bool asOutput) {
        for (auto& c : g.connections) {
            if (asOutput && c.fromNode == nodeId && c.fromPin == from) c.fromPin = to;
            if (!asOutput && c.toNode == nodeId && c.toPin == from) c.toPin = to;
        }
        if (!asOutput)
            if (auto* n = g.findNode(nodeId)) {
                auto it = n->defaults.find(from);
                if (it != n->defaults.end()) { n->defaults[to] = it->second; n->defaults.erase(from); }
            }
    };
    for (auto& g : s.graphs)
        for (auto& n : g.nodes) {
            if (g.name == graphName && isInput && n.def == "func.entry") renameOnNode(g, n.id, true);
            if (g.name == graphName && !isInput && n.def == "func.return") renameOnNode(g, n.id, false);
            if (n.def == "func.call" && n.prop("function") == graphName) renameOnNode(g, n.id, !isInput);
        }
}

void removePins(fg::Script& s, const std::string& graphName, bool isInput, const std::string& pin) {
    auto dropOnNode = [&](fg::Graph& g, fg::NodeId id, bool asOutput) {
        auto& v = g.connections;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const fg::Connection& c) {
            return asOutput ? (c.fromNode == id && c.fromPin == pin) : (c.toNode == id && c.toPin == pin);
        }), v.end());
    };
    for (auto& g : s.graphs)
        for (auto& n : g.nodes) {
            if (g.name == graphName && isInput && n.def == "func.entry") dropOnNode(g, n.id, true);
            if (g.name == graphName && !isInput && n.def == "func.return") dropOnNode(g, n.id, false);
            if (n.def == "func.call" && n.prop("function") == graphName) dropOnNode(g, n.id, !isInput);
        }
}

// A custom pin of a node (fg::Node::inputs / outputs) was renamed, or removed (`to` empty): its links, and the literal value of an
// input, follow it.
void moveNodePin(fg::Graph& g, fg::NodeId id, fg::PinDir dir, const std::string& from, const std::string& to) {
    auto& v = g.connections;
    if (to.empty()) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const fg::Connection& c) {
            return dir == fg::PinDir::In ? (c.toNode == id && c.toPin == from) : (c.fromNode == id && c.fromPin == from);
        }), v.end());
    } else {
        for (auto& c : v) {
            if (dir == fg::PinDir::In && c.toNode == id && c.toPin == from) c.toPin = to;
            if (dir == fg::PinDir::Out && c.fromNode == id && c.fromPin == from) c.fromPin = to;
        }
    }
    if (dir == fg::PinDir::In)
        if (auto* n = g.findNode(id)) {
            auto it = n->defaults.find(from);
            if (it != n->defaults.end()) {
                if (!to.empty()) n->defaults[to] = it->second;
                n->defaults.erase(from);
            }
        }
}

} // namespace

DetailsPanel::DetailsPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setAlignment(Qt::AlignTop);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search");
    search_->setClearButtonEnabled(true);
    search_->setToolTip("Shows only the properties whose name or value contains this text");
    outer->addWidget(search_);
    connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    body_ = new QWidget(this);
    form_ = new QFormLayout(body_);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    outer->addWidget(body_);
    noMatch_ = dim("Nothing matches the search.");
    noMatch_->hide();
    outer->addWidget(noMatch_);
    outer->addStretch(1);
    connect(doc_, &Document::scriptReplaced, this, [this] { rebuild(); });
    connect(doc_, &Document::compiled, this, [this] {
        if (selfEdit_ || rebuildQueued_ || mode_ != Mode::Nodes) return;
        rebuildQueued_ = true;
        QTimer::singleShot(0, this, [this] { rebuildQueued_ = false; rebuild(); });
    });
    rebuild();
}

void DetailsPanel::showNothing() { mode_ = Mode::None; rebuild(); }
void DetailsPanel::showVariable(const QString& owner, const QString& name) { mode_ = Mode::Variable; owner_ = owner; name_ = name; rebuild(); }
void DetailsPanel::showGraph(const QString& name) { mode_ = Mode::Graph; name_ = name; rebuild(); }
void DetailsPanel::showNodes(const QString& graph, const QList<int>& ids) {
    if (mode_ == Mode::Nodes && owner_ == graph && ids_ == ids && !ids.isEmpty()) return;
    mode_ = ids.isEmpty() ? Mode::None : Mode::Nodes;
    owner_ = graph;
    ids_ = ids;
    rebuild();
}

void DetailsPanel::edit(const QString& label, const std::function<void(fg::Script&)>& fn, const QString& key, bool rebuildAfter) {
    selfEdit_ = true;
    doc_->edit(label, fn, EditKind::Structural, key);
    selfEdit_ = false;
    if (rebuildAfter) QTimer::singleShot(0, this, [this] { rebuild(); });
}

void DetailsPanel::clearForm() {
    while (form_->rowCount() > 0) form_->removeRow(0);
}

void DetailsPanel::rebuild() {
    clearForm();
    switch (mode_) {
    case Mode::None: form_->addRow(dim("Select a variable, function or node to see its properties.")); break;
    case Mode::Variable: buildVariable(); break;
    case Mode::Graph: buildGraph(); break;
    case Mode::Nodes: buildNodes(); break;
    }
    applyFilter();
}

namespace {
// Everything a user can read in a widget (and its children): labels, edit fields, the selected entry of combo boxes, table cells.
QString searchableText(QWidget* w) {
    QString out;
    auto add = [&](const QString& t) { out += t + QChar('\n'); };
    auto collect = [&](QWidget* x) {
        if (auto* l = qobject_cast<QLabel*>(x)) add(l->text());
        else if (auto* e = qobject_cast<QLineEdit*>(x)) add(e->text());
        else if (auto* c = qobject_cast<QComboBox*>(x)) add(c->currentText());
        else if (auto* b = qobject_cast<QAbstractButton*>(x)) add(b->text());
        else if (auto* s = qobject_cast<QSpinBox*>(x)) add(s->text());
        else if (auto* t = qobject_cast<QTableWidget*>(x))
            for (int r = 0; r < t->rowCount(); ++r)
                for (int col = 0; col < t->columnCount(); ++col) {
                    if (auto* it = t->item(r, col)) add(it->text());
                    if (auto* cw = t->cellWidget(r, col)) {
                        if (auto* cb = qobject_cast<QComboBox*>(cw)) add(cb->currentText());
                        else if (auto* le = qobject_cast<QLineEdit*>(cw)) add(le->text());
                    }
                }
    };
    collect(w);
    for (auto* child : w->findChildren<QWidget*>()) collect(child);
    return out;
}
} // namespace

void DetailsPanel::applyFilter() {
    const QString text = search_ ? search_->text().trimmed() : QString();
    int shown = 0, total = 0;
    for (int row = 0; row < form_->rowCount(); ++row) {
        QLayoutItem* label = form_->itemAt(row, QFormLayout::LabelRole);
        QLayoutItem* field = form_->itemAt(row, QFormLayout::FieldRole);
        QLayoutItem* span = form_->itemAt(row, QFormLayout::SpanningRole);
        QWidget* lw = label ? label->widget() : nullptr;
        QWidget* fw = field ? field->widget() : nullptr;
        QWidget* sw = span ? span->widget() : nullptr;
        // the headings tell what is shown: they stay while something below them matches
        const bool isHeading = sw && sw->property("fgHeading").toBool();
        bool match = text.isEmpty() || isHeading;
        if (!match) {
            QString all;
            for (QWidget* w : {lw, fw, sw}) if (w) all += searchableText(w);
            match = all.contains(text, Qt::CaseInsensitive);
        }
        form_->setRowVisible(row, match);
        if (!isHeading) { ++total; shown += match; }
    }
    noMatch_->setVisible(!text.isEmpty() && total > 0 && shown == 0);
}

// ------------------------------------------------------------------ variables
void DetailsPanel::buildVariable() {
    const fg::Script& s = doc_->script();
    const bool local = !owner_.isEmpty();
    const fg::VariableDecl* v = nullptr;
    if (local) {
        if (auto* g = s.findGraph(ss(owner_))) v = g->findLocal(ss(name_));
    } else v = s.findVariable(ss(name_));
    if (!v) { form_->addRow(dim("The variable no longer exists.")); return; }
    form_->addRow(heading(local ? "Local variable" : "Variable"));

    auto mutate = [this, local](const std::function<void(fg::VariableDecl&)>& fn, const QString& label, const QString& key) {
        QString owner = owner_, name = name_;
        edit(label, [=](fg::Script& sc) {
            fg::VariableDecl* d = nullptr;
            if (!owner.isEmpty()) { if (auto* g = sc.findGraph(ss(owner))) d = g->findLocal(ss(name)); }
            else d = sc.findVariable(ss(name));
            if (d) fn(*d);
        }, key);
        (void)local;
    };

    auto* nameEdit = new QLineEdit(qs(v->name));
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit, local] {
        QString nn = nameEdit->text().trimmed();
        if (nn.isEmpty() || nn == name_) { nameEdit->setText(name_); return; }
        const fg::Script& sc = doc_->script();
        bool clash = local ? (sc.findGraph(ss(owner_)) && sc.findGraph(ss(owner_))->findLocal(ss(nn))) : sc.findVariable(ss(nn)) != nullptr;
        if (clash) { QMessageBox::warning(this, "Rename", "That name is already used."); nameEdit->setText(name_); return; }
        QString old = name_;
        edit("Rename variable", [&](fg::Script& s2) {
            if (local) s2.renameLocal(ss(owner_), ss(old), ss(nn)); else s2.renameVariable(ss(old), ss(nn));
        });
        name_ = nn;
    });
    form_->addRow("Name", nameEdit);

    {
        TypeSpec spec = TypeSpec::parse(qs(v->type));
        auto* valueEdit = new TypeEdit(doc_->registry());
        valueEdit->setType(spec.value);
        auto* containerBox = new QComboBox;
        containerBox->addItems({"Single", "Array", "Map"});
        containerBox->setCurrentIndex(int(spec.container));
        auto* keyEdit = new TypeEdit(doc_->registry());
        keyEdit->setType(spec.key);
        auto apply = [=, this]() {
            TypeSpec ns;
            ns.container = TypeSpec::Container(containerBox->currentIndex());
            ns.key = keyEdit->type();
            ns.value = valueEdit->type();
            mutate([&](fg::VariableDecl& d) { d.type = ss(ns.str()); d.def = fg::Value(); }, "Change variable type", QString());
            QTimer::singleShot(0, this, [this] { rebuild(); });
        };
        connect(valueEdit, &TypeEdit::typeEdited, this, apply);
        connect(keyEdit, &TypeEdit::typeEdited, this, apply);
        connect(containerBox, qOverload<int>(&QComboBox::activated), this, apply);
        form_->addRow("Type", valueEdit);
        form_->addRow("Container", containerBox);
        if (spec.container == TypeSpec::Container::Map) form_->addRow("Key type", keyEdit);
    }

    const fg::TypeDesc* t = doc_->registry().findType(v->type);
    if (t && (t->isPrimitive() || t->isEnum())) {
        fg::CompiledPin cp;
        cp.type = t;
        try { cp.def = doc_->registry().convert(v->def, t); } catch (...) { cp.def = doc_->registry().defaultValue(t); }
        if (t->kind == fg::TypeKind::Bool) {
            auto* cb = new QCheckBox;
            cb->setChecked(cp.def.toBool());
            connect(cb, &QCheckBox::toggled, this, [=, this](bool on) { mutate([&](fg::VariableDecl& d) { d.def = fg::Value(on); }, "Edit default value", "vardef:" + name_); });
            form_->addRow("Default", cb);
        } else {
            auto* le = new QLineEdit(t->isEnum() ? qs(t->enumName(cp.def.toInt())) : qs(cp.def.toString() == "null" ? "" : (cp.def.isString() ? cp.def.str() : cp.def.toString())));
            connect(le, &QLineEdit::editingFinished, this, [=, this] {
                fg::Value nv = RunController::parseArgument(doc_->registry(), t, le->text());
                mutate([&](fg::VariableDecl& d) { d.def = nv; }, "Edit default value", "vardef:" + name_);
            });
            form_->addRow("Default", le);
        }
    } else {
        form_->addRow("Default", dim(t && t->isContainer() ? "Containers start empty." : "Objects start as null."));
    }
    auto* cat = new QLineEdit(qs(v->category));
    connect(cat, &QLineEdit::editingFinished, this, [=, this] { mutate([&](fg::VariableDecl& d) { d.category = ss(cat->text()); }, "Edit category", "varcat:" + name_); });
    form_->addRow("Category", cat);
    auto* doc = new QLineEdit(qs(v->doc));
    connect(doc, &QLineEdit::editingFinished, this, [=, this] { mutate([&](fg::VariableDecl& d) { d.doc = ss(doc->text()); }, "Edit description", "vardoc:" + name_); });
    form_->addRow("Description", doc);
}

// ------------------------------------------------------------------ parameter tables
namespace {

enum ParamColumn { ColName, ColType, ColContainer, ColKey, ColRef, ColConst, ColCount };

// What the table shows for a parameter and what it says about it in its tooltip (the table has no header: the tooltip names the column).
const char* const kContainerNames[] = {"Single", "Array", "Map"};

QString columnTip(int col, const QString& value) {
    switch (col) {
    case ColName: return "<b>Name</b>: " + value.toHtmlEscaped();
    case ColType: return "<b>Type</b>: " + value.toHtmlEscaped() + "<br><i>Double-click to change it.</i>";
    case ColContainer: return "<b>Container</b>: " + value + "<br>A single value, an array of it, or a map from a key type to it.<br><i>Double-click to change it.</i>";
    case ColKey: return "<b>Key</b>: " + (value.isEmpty() ? QString("(none)") : value.toHtmlEscaped()) + "<br>The key type of a map; only used when the container is a Map.";
    case ColRef: return "<b>Ref</b>: " + value + "<br>Passed by reference: the function reads and writes the caller's variable.";
    default: return "<b>Const</b>: " + value + "<br>The function may not modify the parameter (a const reference needs no variable and makes no copy).";
    }
}

// A combo box that knows whether its list is open (opening the list moves the focus away from the line edit, which is not the end of the edit).
class PopupCombo : public QComboBox {
public:
    using QComboBox::QComboBox;
    bool popupOpen() const { return open_; }
    // The list is about to open: the editor must not end before. It opens once the mouse buttons are up - the release of the
    // double click that started the edit would otherwise hit the new list and close it (or choose an item) at once.
    void expectPopup() {
        open_ = true;
        setProperty("popupOpen", true);
        openWhenReleased();
    }
    // A choice that arrives right after the list opened is Qt's reaction to that mouse release, not the user's.
    bool justOpened() const { return open_ && shown_.isValid() && shown_.elapsed() < 250; }
    void showPopup() override {
        open_ = true;
        setProperty("popupOpen", true);
        shown_.start();
        QComboBox::showPopup();
    }
    void hidePopup() override {
        open_ = false;
        setProperty("popupOpen", false);
        QComboBox::hidePopup();
    }
private:
    void openWhenReleased() {
        QTimer::singleShot(0, this, [this] {
            if (!open_) return;                                                 // closed meanwhile
            if (QApplication::mouseButtons() != Qt::NoButton) openWhenReleased();
            else showPopup();
        });
    }
    bool open_ = false;
    QElapsedTimer shown_;
};

// Edits the cells of a parameter table. Nothing but text is in the cells until the user asks to edit one: then a combo box (types,
// containers) or a line edit (names) appears, and the check columns toggle on a click.
class ParamDelegate : public QStyledItemDelegate {
public:
    ParamDelegate(const fg::Registry& reg, QObject* parent) : QStyledItemDelegate(parent), reg_(reg) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        switch (index.column()) {
        case ColType: return typeEditor(parent);
        case ColKey:
            if (index.sibling(index.row(), ColContainer).data(Qt::DisplayRole).toString() != kContainerNames[2]) return nullptr;
            return typeEditor(parent);
        case ColContainer: {
            auto* box = new PopupCombo(parent);
            for (auto* n : kContainerNames) box->addItem(n);
            QObject::connect(box, qOverload<int>(&QComboBox::activated), box, [this, box](int) { if (!box->justOpened()) finish(box); });
            box->expectPopup();                                         // the choice is made right away, like a menu
            return box;
        }
        case ColRef:
        case ColConst: return nullptr;
        default: return QStyledItemDelegate::createEditor(parent, option, index);
        }
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        if (auto* box = qobject_cast<QComboBox*>(editor)) {
            QString text = index.data(Qt::DisplayRole).toString();
            if (box->isEditable()) box->setCurrentText(text);
            else box->setCurrentIndex(std::max(0, box->findText(text)));
            if (box->isEditable()) box->lineEdit()->selectAll();
        } else {
            QStyledItemDelegate::setEditorData(editor, index);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        auto* box = qobject_cast<QComboBox*>(editor);
        if (!box) return QStyledItemDelegate::setModelData(editor, model, index);
        QString text = box->currentText().trimmed();
        if (box->isEditable() && !reg_.findType(ss(text))) return;       // only types the registry knows
        model->setData(index, text, Qt::EditRole);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override { editor->setGeometry(option.rect); }

    // The delegate ends an edit when the editor loses the focus - which is what happens when its list opens. Not then.
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::FocusOut)
            if (auto* box = dynamic_cast<PopupCombo*>(object))
                if (box->popupOpen()) return false;
        return QStyledItemDelegate::eventFilter(object, event);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (index.column() < ColRef) return QStyledItemDelegate::paint(painter, option, index);
        // a check box centered in its cell
        QStyleOptionViewItem o = option;
        initStyleOption(&o, index);
        const bool checked = o.checkState == Qt::Checked;
        o.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        o.text.clear();
        QStyle* style = o.widget ? o.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &o, painter, o.widget);
        QStyleOptionButton b;
        const int w = style->pixelMetric(QStyle::PM_IndicatorWidth, &o, o.widget), h = style->pixelMetric(QStyle::PM_IndicatorHeight, &o, o.widget);
        b.rect = QRect(o.rect.center().x() - w / 2, o.rect.center().y() - h / 2, w, h);
        b.state = (o.state & QStyle::State_Enabled) | (checked ? QStyle::State_On : QStyle::State_Off);
        b.palette = o.palette;
        style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &b, painter, o.widget);
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        if (index.column() == ColType || index.column() == ColKey) s.setWidth(std::max(s.width() + 8, 64));
        return s;
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override {
        if (index.column() < ColRef || !(index.flags() & Qt::ItemIsUserCheckable) || !(index.flags() & Qt::ItemIsEnabled))
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        bool toggle = false;
        if (event->type() == QEvent::MouseButtonRelease) toggle = static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && option.rect.contains(static_cast<QMouseEvent*>(event)->pos());
        else if (event->type() == QEvent::KeyPress) toggle = static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space || static_cast<QKeyEvent*>(event)->key() == Qt::Key_Select;
        if (!toggle) return event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick;
        bool on = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        return model->setData(index, on ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }

private:
    PopupCombo* typeEditor(QWidget* parent) const {
        auto* box = new PopupCombo(parent);
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        for (auto* t : reg_.types()) box->addItem(qs(t->name));
        if (box->completer()) box->completer()->setCaseSensitivity(Qt::CaseInsensitive);
        QObject::connect(box, qOverload<int>(&QComboBox::activated), box, [this, box](int) { if (!box->justOpened()) finish(box); });
        // A choice or Return ends the edit; so does moving the focus to another widget - but not while the list is open, and not
        // into the list itself (opening it takes the focus away from the line edit too).
        QObject::connect(box->lineEdit(), &QLineEdit::returnPressed, box, [this, box] { finish(box); });
        QObject::connect(qApp, &QApplication::focusChanged, box, [this, box](QWidget*, QWidget* now) {
            if (!now || box->popupOpen() || now == box || box->isAncestorOf(now)) return;
            finish(box);
        });
        box->expectPopup();                                             // the list of types opens at once, like the container list
        return box;
    }
    void finish(QWidget* editor) const {
        auto* self = const_cast<ParamDelegate*>(this);
        emit self->commitData(editor);
        emit self->closeEditor(editor, QAbstractItemDelegate::NoHint);
    }
    const fg::Registry& reg_;
};

// Tooltips (they replace the header) and the look of the Key cell, which only means something for maps.
void styleParamRow(QTableWidget* table, int r) {
    const bool isMap = table->item(r, ColContainer)->text() == kContainerNames[2];
    QSignalBlocker block(table);
    const QColor dimColor = table->palette().color(QPalette::PlaceholderText);
    // only a map has a key type: the cell is empty for the other containers (the type is remembered for when it becomes a map again)
    QTableWidgetItem* keyItem = table->item(r, ColKey);
    if (isMap) {
        if (keyItem->text().isEmpty()) keyItem->setText(keyItem->data(Qt::UserRole).toString().isEmpty() ? QString("string") : keyItem->data(Qt::UserRole).toString());
        keyItem->setData(Qt::UserRole, keyItem->text());
    } else if (!keyItem->text().isEmpty()) {
        keyItem->setData(Qt::UserRole, keyItem->text());
        keyItem->setText(QString());
    }
    for (int c = 0; c < ColCount; ++c) {
        QTableWidgetItem* it = table->item(r, c);
        if (!it) continue;
        QString value = c >= ColRef ? (it->checkState() == Qt::Checked ? "yes" : "no") : it->text();
        it->setToolTip(columnTip(c, value));
    }
    QTableWidgetItem* key = table->item(r, ColKey);
    key->setFlags(isMap ? (key->flags() | Qt::ItemIsEditable) : (key->flags() & ~Qt::ItemIsEditable));
    key->setForeground(isMap ? table->palette().color(QPalette::Text) : dimColor);
}

} // namespace

QTableWidget* DetailsPanel::buildParamTable(bool inputs, const std::vector<fg::ParamDecl>& params, std::function<void(std::vector<fg::ParamDecl>)> apply) {
    constexpr int kRowHeight = 24;
    const int columns = inputs ? ColCount : ColRef;
    auto* table = new QTableWidget(int(params.size()), columns);
    table->setItemDelegate(new ParamDelegate(doc_->registry(), table));
    table->horizontalHeader()->hide();                  // the column names are in the tooltips of the cells
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    table->horizontalHeader()->setMinimumSectionSize(28);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    for (int c = ColType; c < columns; ++c) table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);       // editors only on request
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setFixedHeight(std::max<int>(int(params.size()), 1) * kRowHeight + 2 * table->frameWidth() + 2);

    auto collect = [table, inputs]() {
        std::vector<fg::ParamDecl> out;
        for (int r = 0; r < table->rowCount(); ++r) {
            fg::ParamDecl p;
            p.name = ss(table->item(r, ColName)->text().trimmed());
            TypeSpec spec;
            spec.value = table->item(r, ColType)->text();
            spec.container = TypeSpec::Container(std::max(0, int(QStringList{kContainerNames[0], kContainerNames[1], kContainerNames[2]}.indexOf(table->item(r, ColContainer)->text()))));
            if (spec.container == TypeSpec::Container::Map) spec.key = table->item(r, ColKey)->text();
            p.type = ss(spec.str());
            if (inputs) {
                p.byRef = table->item(r, ColRef)->checkState() == Qt::Checked;
                p.isConst = table->item(r, ColConst)->checkState() == Qt::Checked;
            }
            out.push_back(std::move(p));
        }
        return out;
    };
    for (int r = 0; r < table->rowCount(); ++r) {
        const fg::ParamDecl& p = params[size_t(r)];
        TypeSpec spec = TypeSpec::parse(qs(p.type));
        const Qt::ItemFlags editable = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
        auto text = [&](int col, const QString& s) {
            auto* it = new QTableWidgetItem(s);
            it->setFlags(editable);
            table->setItem(r, col, it);
        };
        text(ColName, qs(p.name));
        text(ColType, spec.value);
        text(ColContainer, kContainerNames[int(spec.container)]);
        text(ColKey, spec.container == TypeSpec::Container::Map ? spec.key : QString());
        table->item(r, ColKey)->setData(Qt::UserRole, spec.key);
        if (inputs) {
            for (int col : {int(ColRef), int(ColConst)}) {
                auto* it = new QTableWidgetItem;
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                it->setCheckState((col == ColRef ? p.byRef : p.isConst) ? Qt::Checked : Qt::Unchecked);
                table->setItem(r, col, it);
            }
        }
        styleParamRow(table, r);
    }
    connect(table, &QTableWidget::itemChanged, this, [=](QTableWidgetItem* it) {
        styleParamRow(table, it->row());
        apply(collect());
    });
    return table;
}

// ------------------------------------------------------------------ graphs / functions
void DetailsPanel::buildGraph() { buildGraphFor(name_); }

// The function / graph editor: name, description and the parameter tables. Shown for a selected function and
// also while the entry or a return node of a function graph is selected.
void DetailsPanel::buildGraphFor(const QString& graphName) {
    graphTarget_ = graphName;
    const fg::Graph* g = doc_->script().findGraph(ss(graphName));
    if (!g) { form_->addRow(dim("The graph no longer exists.")); return; }
    const bool fn = g->kind == fg::GraphKind::Function;
    form_->addRow(heading(fn ? "Function" : "Event graph"));
    auto* nameEdit = new QLineEdit(qs(g->name));
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit, fn] {
        QString nn = nameEdit->text().trimmed();
        if (nn.isEmpty() || nn == graphTarget_) { nameEdit->setText(graphTarget_); return; }
        if (doc_->script().graphIndex(ss(nn)) >= 0) { QMessageBox::warning(this, "Rename", "A graph with that name exists."); nameEdit->setText(graphTarget_); return; }
        QString old = graphTarget_;
        edit("Rename graph", [&](fg::Script& s) {
            if (fn) s.renameFunction(ss(old), ss(nn));
            else if (auto* gr = s.findGraph(ss(old))) gr->name = ss(nn);
        });
        graphTarget_ = nn;
        if (name_ == old) name_ = nn;
        if (owner_ == old) owner_ = nn;
        emit graphRenamed(old, nn);
    });
    form_->addRow("Name", nameEdit);
    auto* docEdit = new QLineEdit(qs(g->doc));
    connect(docEdit, &QLineEdit::editingFinished, this, [this, docEdit] {
        QString name = graphTarget_, text = docEdit->text();
        edit("Edit description", [=](fg::Script& s) { if (auto* gr = s.findGraph(ss(name))) gr->doc = ss(text); }, "graphdoc:" + graphTarget_);
    });
    form_->addRow("Description", docEdit);
    if (!fn) return;

    auto section = [&](bool inputs) {
        const auto& params = inputs ? g->inputs : g->outputs;
        QString gname = graphTarget_;
        auto apply = [this, gname, inputs](std::vector<fg::ParamDecl> np) {
            const fg::Graph* cur = doc_->script().findGraph(ss(gname));
            if (!cur) return;
            const auto& old = inputs ? cur->inputs : cur->outputs;
            std::set<std::string> names;
            for (auto& p : np)
                if (p.name.empty() || !names.insert(p.name).second) {
                    QMessageBox::warning(this, "Parameters", "Parameter names must be unique and not empty.");
                    QTimer::singleShot(0, this, [this] { rebuild(); });
                    return;
                }
            std::vector<fg::ParamDecl> oldCopy = old;
            for (auto& p : np)       // keep defaults of unchanged rows
                for (auto& o : oldCopy) if (o.name == p.name) p.def = o.def;
            edit("Edit parameters", [&](fg::Script& s) {
                auto* gr = s.findGraph(ss(gname));
                if (!gr) return;
                auto& dst = inputs ? gr->inputs : gr->outputs;
                if (oldCopy.size() == np.size())
                    for (size_t i = 0; i < np.size(); ++i)
                        if (oldCopy[i].name != np[i].name) renamePins(s, ss(gname), inputs, oldCopy[i].name, np[i].name);
                dst = np;
            });
        };
        auto* table = buildParamTable(inputs, params, apply);
        // a panel like the ones of My Script: "+" in its header adds a parameter, "Remove" is in the right click menu
        auto* panel = new CCollapsiblePanel(inputs ? "Inputs" : "Outputs", nullptr, true);
        panel->AddWidget(table);
        panel->ExpandImmediately();
        connect(panel, &CCollapsiblePanel::PlusButtonClicked, this, [this, gname, inputs] {
            edit("Add parameter", [&](fg::Script& s) {
                auto* gr = s.findGraph(ss(gname));
                if (!gr) return;
                auto& dst = inputs ? gr->inputs : gr->outputs;
                std::string base = inputs ? "Input" : "Output", n = base;
                for (int i = 1;; ++i) {
                    bool used = false;
                    for (auto& p : dst) used |= p.name == n;
                    if (!used) break;
                    n = base + std::to_string(i);
                }
                dst.push_back({n, "int", false, false, fg::Value()});
            }, QString(), true);
        });
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(table, &QWidget::customContextMenuRequested, this, [this, gname, inputs, table](const QPoint& pos) {
            int row = table->rowAt(pos.y());
            if (row < 0) return;
            table->selectRow(row);
            QMenu menu;
            QAction* remove = menu.addAction("Remove");
            if (menu.exec(table->viewport()->mapToGlobal(pos)) != remove) return;
            edit("Remove parameter", [&](fg::Script& s) {
                auto* gr = s.findGraph(ss(gname));
                if (!gr) return;
                auto& dst = inputs ? gr->inputs : gr->outputs;
                if (size_t(row) >= dst.size()) return;
                removePins(s, ss(gname), inputs, dst[size_t(row)].name);
                dst.erase(dst.begin() + row);
            }, QString(), true);
        });
        form_->addRow(panel);
    };
    section(true);
    section(false);
}

// ------------------------------------------------------------------ nodes
// "Inputs" and "Outputs" of a node. Every node shows its pins, read only: they belong to the definition of the node. A definition that
// allows custom pins (NodeDef::customInputs / customOutputs) also gets a list of the pins the user added, edited like the inputs and
// outputs of a function: "+" adds a pin, the cells are edited by double-click, "Remove" / "Move" are in the right click menu.
// Renaming a pin moves its links with it; removing a pin removes them.
void DetailsPanel::buildNodePins(const fg::Node& node, const fg::CompiledNode& cn, const QString& graph) {
    constexpr int kRowHeight = 24;
    const fg::NodeId id = node.id;
    for (bool inputs : {true, false}) {
        const fg::PinDir dir = inputs ? fg::PinDir::In : fg::PinDir::Out;
        const bool custom = cn.def && (inputs ? cn.def->customInputs : cn.def->customOutputs);
        const std::vector<fg::ParamDecl>& mine = inputs ? node.inputs : node.outputs;
        std::set<std::string> customNames;
        if (custom)
            for (auto& p : mine) customNames.insert(p.name);
        // the pins of the definition: everything of this direction except the custom pins, which come last
        std::vector<const fg::CompiledPin*> fixed;
        std::set<std::string> fixedNames;                  // all of them, hidden or not: a custom pin may not take one of these names
        for (auto& p : cn.pins) {
            if (p.dir != dir) continue;
            if (custom && p.kind == fg::PinKind::Data && customNames.count(p.name)) continue;
            fixedNames.insert(p.name);
            if (!p.hidden) fixed.push_back(&p);
        }
        if (fixed.empty() && !custom) continue;

        auto* panel = new CCollapsiblePanel(inputs ? "Inputs" : "Outputs", nullptr, custom);
        if (!fixed.empty()) {
            auto* table = new QTableWidget(int(fixed.size()), 2);
            table->setObjectName(inputs ? "fixedInputs" : "fixedOutputs");
            table->horizontalHeader()->hide();
            table->verticalHeader()->hide();
            table->verticalHeader()->setDefaultSectionSize(kRowHeight);
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            table->setShowGrid(false);
            table->setSelectionMode(QAbstractItemView::NoSelection);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setFocusPolicy(Qt::NoFocus);
            table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            table->setFixedHeight(int(fixed.size()) * kRowHeight + 2 * table->frameWidth() + 2);
            const QBrush dimmed = table->palette().brush(QPalette::Disabled, QPalette::Text);
            for (int r = 0; r < int(fixed.size()); ++r) {
                const fg::CompiledPin& p = *fixed[size_t(r)];
                auto* name = new QTableWidgetItem(qs(p.name));
                auto* type = new QTableWidgetItem(p.kind == fg::PinKind::Exec ? QString("exec") : p.type ? qs(p.type->name) : QString("any"));
                name->setToolTip(qs(p.doc.empty() ? std::string("Part of the definition of the node: it cannot be changed.") : p.doc));
                for (auto* it : {name, type}) {
                    it->setFlags(Qt::ItemIsEnabled);
                    it->setForeground(dimmed);
                }
                table->setItem(r, 0, name);
                table->setItem(r, 1, type);
            }
            panel->AddWidget(table);
        }

        if (custom) {
            // every change is one undoable edit of the node and of the links that end at its custom pins
            const QString gname = graph;
            auto change = [this, gname, id, inputs](const QString& label, const std::function<void(std::vector<fg::ParamDecl>&, fg::Graph&)>& fn) {
                edit(label, [=](fg::Script& s) {
                    auto* gr = s.findGraph(ss(gname));
                    auto* n = gr ? gr->findNode(id) : nullptr;
                    if (n) fn(inputs ? n->inputs : n->outputs, *gr);
                }, QString(), true);
            };
            auto apply = [this, fixedNames, dir, id, change](std::vector<fg::ParamDecl> np) {
                std::set<std::string> names = fixedNames;
                for (auto& p : np)
                    if (p.name.empty() || !names.insert(p.name).second) {
                        QMessageBox::warning(this, "Pins", "Pin names must be unique and not empty.");
                        QTimer::singleShot(0, this, [this] { rebuild(); });
                        return;
                    }
                change("Edit node pins", [&](std::vector<fg::ParamDecl>& list, fg::Graph& gr) {
                    for (size_t i = 0; i < np.size(); ++i) {
                        if (list.size() == np.size() && list[i].name != np[i].name) moveNodePin(gr, id, dir, list[i].name, np[i].name);
                        const fg::ParamDecl* old = nullptr;       // keep the default of a pin that is still the same
                        if (list.size() == np.size()) old = &list[i];
                        else for (auto& o : list) if (o.name == np[i].name) old = &o;
                        if (old) np[i].def = old->def;
                    }
                    list = np;
                });
            };
            auto* table = buildParamTable(false, mine, apply);
            table->setObjectName(inputs ? "customInputs" : "customOutputs");
            panel->AddWidget(table);
            connect(panel, &CCollapsiblePanel::PlusButtonClicked, this, [=] {
                change("Add pin", [&](std::vector<fg::ParamDecl>& list, fg::Graph&) {
                    const std::string base = inputs ? "Input" : "Output";
                    std::string n = base;
                    auto taken = [&](const std::string& name) {
                        return fixedNames.count(name) || std::any_of(list.begin(), list.end(), [&](const fg::ParamDecl& p) { return p.name == name; });
                    };
                    for (int i = 1; taken(n); ++i) n = base + std::to_string(i);
                    list.push_back({n, "any", false, false, fg::Value()});
                });
            });
            table->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(table, &QWidget::customContextMenuRequested, this, [=](const QPoint& pos) {
                const int row = table->rowAt(pos.y());
                if (row < 0) return;
                table->selectRow(row);
                QMenu menu;
                QAction* remove = menu.addAction(QIcon(":/Icons/delete.svg"), "Remove");
                QAction* up = menu.addAction("Move Up");
                QAction* down = menu.addAction("Move Down");
                up->setEnabled(row > 0);
                down->setEnabled(row + 1 < table->rowCount());
                QAction* chosen = menu.exec(table->viewport()->mapToGlobal(pos));
                if (!chosen) return;
                change(chosen == remove ? "Remove pin" : "Move pin", [&](std::vector<fg::ParamDecl>& list, fg::Graph& gr) {
                    if (size_t(row) >= list.size()) return;
                    if (chosen == remove) {
                        moveNodePin(gr, id, dir, list[size_t(row)].name, std::string());
                        list.erase(list.begin() + row);
                    } else {
                        const int to = chosen == up ? row - 1 : row + 1;
                        if (to >= 0 && size_t(to) < list.size()) std::swap(list[size_t(row)], list[size_t(to)]);
                    }
                });
            });
        }
        panel->ExpandImmediately();
        form_->addRow(panel);
    }
}

void DetailsPanel::buildNodes() {
    const fg::Graph* g = doc_->script().findGraph(ss(owner_));
    const fg::CompiledGraph* cg = doc_->compiledGraph(owner_);
    if (!g || !cg) { form_->addRow(dim("")); return; }
    if (ids_.size() > 1) {
        form_->addRow(heading(QString::number(ids_.size()) + " nodes selected"));
        return;
    }
    const fg::Node* node = g->findNode(ids_.front());
    const fg::CompiledNode* cn = cg->findNode(ids_.front());
    if (!node || !cn) { form_->addRow(dim("")); return; }
    if (g->kind == fg::GraphKind::Function && (cn->op == fg::NodeOp::FunctionEntry || cn->op == fg::NodeOp::Return)) {
        buildGraphFor(owner_);                 // the function's name and its inputs / outputs
        form_->addRow(new QLabel(QString()));
    }
    form_->addRow(heading(cn->op == fg::NodeOp::FunctionEntry ? QString("Function entry node") : qs(cn->title)));
    if (cn->def && !cn->def->doc.empty()) form_->addRow(dim(qs(cn->def->doc)));
    form_->addRow("Definition", new QLabel(qs(cn->defId) + (cn->def && !cn->def->source.empty() ? "  [" + qs(cn->def->source) + "]" : QString())));

    for (auto& d : doc_->diagnosticsFor(owner_, node->id)) {
        auto* l = new QLabel(qs(d.message));
        l->setWordWrap(true);
        l->setStyleSheet(d.severity == fg::Diagnostic::Severity::Error ? "color:#ff6b6b" : "color:#e6be3c");
        form_->addRow(l);
    }

    QString graph = owner_;
    fg::NodeId id = node->id;
    auto mutateNode = [this, graph, id](const std::function<void(fg::Node&)>& fn, const QString& label, const QString& key) {
        edit(label, [=](fg::Script& s) {
            if (auto* gr = s.findGraph(ss(graph)))
                if (auto* n = gr->findNode(id)) fn(*n);
        }, key);
    };

    if (cn->op == fg::NodeOp::Event) {
        // events belong to the application: they can be handled (or not), but never created, renamed or changed
        const fg::EventDesc* ev = cn->defId.rfind("event:", 0) == 0 ? doc_->registry().findEvent(cn->defId.substr(6)) : nullptr;
        form_->addRow("Event", new QLabel(ev ? qs(ev->name) : qs(cn->defId.substr(6))));
        form_->addRow("Arguments", new QLabel(ev ? qs(fg::describeParams(*ev)) : QString("unknown")));
        if (ev && !ev->doc.empty()) form_->addRow(dim(qs(ev->doc)));
        form_->addRow(dim("This event is raised by the application. A script can only handle it."));
    }
    if (cn->op == fg::NodeOp::GetVariable || cn->op == fg::NodeOp::SetVariable)
        form_->addRow("Variable", new QLabel(qs(node->prop("name")) + "  (" + qs(node->prop("scope")) + ")"));
    if (cn->defId == "flow.sequence" || cn->defId == "array.make") {
        auto* sp = new QSpinBox;
        sp->setRange(cn->defId == "flow.sequence" ? 1 : 0, cn->defId == "flow.sequence" ? 32 : 64);
        sp->setValue(qs(node->prop("count", "2")).toInt());
        connect(sp, qOverload<int>(&QSpinBox::valueChanged), this, [=](int v) {
            mutateNode([&](fg::Node& n) { n.props["count"] = std::to_string(v); }, "Change pin count", "count:" + graph + QString::number(id));
        });
        form_->addRow(cn->defId == "flow.sequence" ? "Output pins" : "Elements", sp);
    }
    // type parameters
    if (cn->def)
        for (auto& tp : cn->def->typeParams) {
            auto* combo = new QComboBox;
            auto it = cn->typeArgs.find(tp.name);
            QString resolved = it != cn->typeArgs.end() ? qs(it->second) : QString();
            combo->addItem("Auto" + (resolved.isEmpty() ? QString() : " (" + resolved + ")"), QString());
            QStringList choices;
            if (!tp.allowed.empty()) for (auto& a : tp.allowed) choices << qs(a);
            else for (auto* t : doc_->registry().types()) choices << qs(t->name);
            for (auto& c : choices) combo->addItem(c, c);
            auto ex = node->typeArgs.find(tp.name);
            if (ex != node->typeArgs.end()) combo->setCurrentIndex(std::max(0, combo->findData(qs(ex->second))));
            QString pname = qs(tp.name);
            connect(combo, qOverload<int>(&QComboBox::activated), this, [=](int idx) {
                QString v = combo->itemData(idx).toString();
                mutateNode([&](fg::Node& n) {
                    if (v.isEmpty()) n.typeArgs.erase(ss(pname));
                    else n.typeArgs[ss(pname)] = ss(v);
                }, "Change node type", QString());
            });
            form_->addRow("Type " + pname, combo);
        }
    // the inputs and outputs of the node (the function's own are shown above, where they can be edited)
    if (!(g->kind == fg::GraphKind::Function && (cn->op == fg::NodeOp::FunctionEntry || cn->op == fg::NodeOp::Return))) buildNodePins(*node, *cn, graph);
    auto* comment = new QLineEdit(qs(node->comment));
    connect(comment, &QLineEdit::editingFinished, this, [=] {
        QString t = comment->text();
        mutateNode([&](fg::Node& n) { n.comment = ss(t); }, "Edit node comment", "comment:" + graph + QString::number(id));
    });
    form_->addRow("Comment", comment);
}

} // namespace fged
