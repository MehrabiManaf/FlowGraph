// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Panels/DetailsPanel.hpp"
#include "Icons.hpp"

#include "Graph/GraphStyle.hpp"
#include "NodeTexts.hpp"

#include <FlowGraph/Compatibility.hpp>
#include <FlowGraph/TypeScope.hpp>
#include <FlowGraphEditorCore/GraphEdits.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/Action.hpp>
#include <GLWidgets/Widgets/Animation.hpp>
#include <GLWidgets/Widgets/CheckBox.hpp>
#include <GLWidgets/Widgets/CollapsiblePanel.hpp>
#include <GLWidgets/Widgets/ComboBox.hpp>
#include <GLWidgets/Widgets/Icon.hpp>
#include <GLWidgets/Widgets/ItemModel.hpp>
#include <GLWidgets/Widgets/Label.hpp>
#include <GLWidgets/Widgets/Layout.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/SearchField.hpp>
#include <GLWidgets/Widgets/ListView.hpp>
#include <GLWidgets/Widgets/Menu.hpp>
#include <GLWidgets/Widgets/ScrollArea.hpp>
#include <GLWidgets/Widgets/SpinBox.hpp>
#include <GLWidgets/Widgets/TableView.hpp>
#include <GLWidgets/Widgets/TextEdit.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace fged::gl {

using glw::tr;

namespace {

constexpr float rowHeight = 26;

// The + of a section: the Qt editor's (Editor/Resources/Icons/add.svg), in its green.
constexpr const char* addIcon = R"(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 24 24">
  <path d="M12 3.5 V20.5 M3.5 12 H20.5" stroke="#34d17c" stroke-width="4.4" stroke-linecap="round"/>
</svg>)";

const glw::Color errorColor{0.92f, 0.36f, 0.36f, 1};
const glw::Color warningColor{0.95f, 0.70f, 0.30f, 1};

std::string lowered(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(std::string s) {
    while (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

// A field of text that is done when Enter is pressed or it loses the focus, and what it has changed; Escape puts back what
// it had.
class FieldEdit final : public glw::LineEdit {
public:
    FieldEdit(std::string text, std::function<void(const std::string&)> done) : original_(text), done_(std::move(done)) {
        setText(std::move(text));
        setOnSubmit([this](const std::string&) { finish(); });
    }
    void finish() {
        if (text() == original_)
            return;
        original_ = text();
        if (done_)
            done_(original_);
    }

protected:
    void onKey(glw::KeyEvent& event) override {
        if (event.pressed && event.key == glw::platform::Key::Escape && text() != original_) {
            event.handled = true;
            setText(original_);
            return;
        }
        glw::LineEdit::onKey(event);
    }
    void onFocusChanged(bool focused) override {
        glw::LineEdit::onFocusChanged(focused);
        if (!focused)
            finish();
    }

private:
    std::string original_;
    std::function<void(const std::string&)> done_;
};

// Lines of text (a setting of a node: the cases of a switch), done when it loses the focus: a case being typed does not
// rename its pin at every key.
class LinesEdit final : public glw::TextEdit {
public:
    std::function<void(const std::string&)> done;
    void finish() {
        const std::string now = text();
        if (now != original_ && done) {
            original_ = now;
            done(now);
        }
    }
    void start(const std::string& text) {
        original_ = text;
        setText(text);
    }

protected:
    void onFocusChanged(bool focused) override {
        glw::TextEdit::onFocusChanged(focused);
        if (!focused)
            finish();
    }

private:
    std::string original_;
};

// A cell's combo box: flat (its text and a small arrow, lit under the mouse, as the Qt editor's), as wide as what it shows
// now (a table's fitting column is measured by it), not as its widest item.
class FlatCombo final : public glw::ComboBox {
public:
    FlatCombo() { setPadding({6, 2, 16, 2}); }
    // The cell shows the glyph of a pin of the container chosen (its list shows the items' names): none, the item.
    void setGlyph(std::optional<PinGlyph> glyph) {
        glyph_ = glyph;
        if (!children().empty())
            children()[0]->setVisible(!glyph_);         // its label
        invalidateLayout();
        invalidatePaint();
    }

protected:
    glw::Size measureContent(glw::Size available) override {
        const glw::UiContext* ui = context();
        if (glyph_)
            return {arrayConnectorWidth + 2, 14};
        if (!ui || current() >= items().size())
            return glw::ComboBox::measureContent(available);
        const glw::text::TextLayout text(ui->translator.resolve(items()[current()]), glw::text::TextStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize});
        return {std::ceil(text.size().width) + 2, std::ceil(text.size().height)};
    }
    void paint(glw::PaintContext& context) override {
        const glw::Palette& c = context.ui.theme.palette;
        glw::paint::Painter& p = context.painter;
        if (hovered() || isOpen())
            p.fillRoundedRect({1, 1, size().width - 2, size().height - 2}, 3, c.hover);
        const float x = rightToLeft() ? 8 : size().width - 8, y = size().height / 2;
        const glw::Point points[] = {{x - 3.5f, y - 1.5f}, {x, y + 2}, {x + 3.5f, y - 1.5f}};
        p.drawPolyline(points, 1.3f, hovered() || isOpen() ? c.text : c.mutedText);
        if (glyph_) {
            PinGlyph g = *glyph_;
            g.container = int(current());
            g.hollow = c.base;
            const float half = (arrayConnectorWidth + 2) / 2;
            drawPinGlyph(p, {rightToLeft() ? size().width - 6 - half : 6 + half, size().height / 2}, g);
        }
    }

private:
    std::optional<PinGlyph> glyph_;
};

// A column of a parameter table.
enum class Col { Name, Type, Container, Key, Ref, Const };

glw::LocalizedText containerText(core::TypeParts::Container c) {
    switch (c) {
    case core::TypeParts::Container::Array: return tr("Array");
    case core::TypeParts::Container::Map: return tr("Map");
    case core::TypeParts::Container::Single: break;
    }
    return tr("Single");
}

// A row of a parameter table: a parameter, a field, a pin.
struct ParamRow {
    std::string name;
    std::string shown;                  // a pin of a node's definition: its label (it keeps its name)
    core::TypeParts parts;
    bool byRef = false, isConst = false;
    std::string doc;                    // a pin of a node's definition: what it is
};

} // namespace

// A table of parameters (a function's inputs or outputs, a struct's fields, a node's pins) and what its rows are.
struct DetailsPanel::Table {
    enum class Kind { Params, Fields, NodePins, Fixed };
    Kind kind = Kind::Params;
    bool inputs = true;
    std::string owner;                  // the function, the struct, the node's graph
    fg::NodeId node = 0;
    std::set<std::string> fixed;        // a node's own pins: names a custom pin may not take
    std::vector<ParamRow> rows;
    std::vector<Col> columns;
    const std::vector<std::string>* types = nullptr;    // what a type is chosen from
    std::function<glw::Color(const std::string&)> color;  // a type's (the glyph of a container)
    glw::TableView* view = nullptr;

    bool editable() const { return kind != Kind::Fixed; }
    std::vector<fg::ParamDecl> decls() const {
        std::vector<fg::ParamDecl> out;
        for (const ParamRow& r : rows)
            out.push_back({trimmed(r.name), r.parts.str(), r.byRef, r.isConst, fg::Value()});
        return out;
    }
};

namespace {

class ParamModel final : public glw::TableModel {
public:
    explicit ParamModel(DetailsPanel::Table& table) : table_(table) {}
    std::size_t rowCount() const override { return table_.rows.size(); }
    std::size_t columnCount() const override { return table_.columns.size(); }
    glw::LocalizedText header(std::size_t column) const override {
        switch (table_.columns[column]) {
        case Col::Name: return tr("Name");
        case Col::Type: return tr("Type");
        case Col::Container: return tr("Container");
        case Col::Key: return tr("Key");
        case Col::Ref: return tr("Ref");
        case Col::Const: return tr("Const");
        }
        return {};
    }
    glw::ItemFlags flags(std::size_t, std::size_t column) const override {
        return table_.editable() && table_.columns[column] == Col::Name ? glw::ItemFlags::Editable : glw::ItemFlags::None;
    }

private:
    DetailsPanel::Table& table_;
};

// A cell of text: a name, or (muted) a pin of a node's definition.
class TextCell final : public glw::Widget {
public:
    TextCell() { setHitTestVisible(false); }
    void show(std::string text, bool muted) {
        text_ = std::move(text);
        muted_ = muted;
        layout_.reset();
        invalidateLayout();
        invalidatePaint();
    }

protected:
    glw::Size measureContent(glw::Size) override {
        const glw::UiContext* ui = context();
        if (!ui)
            return {60, rowHeight};
        const glw::text::TextLayout text(text_, glw::text::TextStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize});
        return {std::ceil(text.size().width) + 14, rowHeight};
    }
    void paint(glw::PaintContext& context) override {
        const glw::Theme& t = context.ui.theme;
        if (!layout_)
            layout_.emplace(text_, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, t.fontSize});
        context.painter.pushClip({6, 0, std::max(0.0f, size().width - 10), size().height});
        context.drawText(*layout_, {6, (size().height - layout_->size().height) / 2 + layout_->opticalOffset()}, muted_ ? t.palette.mutedText : t.palette.text);
        context.painter.popClip();
    }

private:
    std::string text_;
    bool muted_ = false;
    std::optional<glw::text::TextLayout> layout_;
};

// A cell chosen from a list: a type, a container, a map's key.
class ChoiceCell final : public glw::Widget {
public:
    ChoiceCell() {
        setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Horizontal));
        setPadding({0, 1, 2, 1});
        combo_ = &add(std::make_unique<FlatCombo>());
        combo_->setStretch(1);
        combo_->setOnChanged([this](std::size_t i) {
            if (onChanged_)
                onChanged_(i);
        });
    }
    void show(std::vector<glw::LocalizedText> items, std::size_t current, bool searchable, std::function<void(std::size_t)> changed,
              std::optional<PinGlyph> glyph = std::nullopt) {
        combo_->setGlyph(glyph);
        combo_->setVisible(!items.empty());
        combo_->setItems(std::move(items));
        combo_->setCurrent(current);
        combo_->setSearchable(searchable, tr("Search types"));
        onChanged_ = std::move(changed);
        invalidateLayout();
    }

private:
    FlatCombo* combo_ = nullptr;
    std::function<void(std::size_t)> onChanged_;
};

// A cell checked or not: by reference, const.
class CheckCell final : public glw::Widget {
public:
    CheckCell() {
        setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Horizontal));
        box_ = &add(std::make_unique<glw::CheckBox>());
        box_->setAlignment(glw::Alignment::Center, glw::Alignment::Center);
        box_->setOnToggled([this](bool on) {
            if (onToggled_)
                onToggled_(on);
        });
    }
    void show(bool checked, std::function<void(bool)> toggled) {
        box_->setChecked(checked);
        onToggled_ = std::move(toggled);
    }

private:
    glw::CheckBox* box_ = nullptr;
    std::function<void(bool)> onToggled_;
};

class ParamDelegate final : public glw::TableDelegate {
public:
    ParamDelegate(DetailsPanel& panel, DetailsPanel::Table& table) : panel_(panel), table_(table) {}

    std::size_t cellKind(std::size_t column) const override {
        switch (table_.columns[column]) {
        case Col::Name: return 0;
        case Col::Type:
        case Col::Container:
        case Col::Key: return table_.editable() ? 1 : 0;
        case Col::Ref:
        case Col::Const: return 2;
        }
        return 0;
    }
    std::unique_ptr<glw::Widget> makeCell(std::size_t kind) override {
        if (kind == 1)
            return std::make_unique<ChoiceCell>();
        if (kind == 2)
            return std::make_unique<CheckCell>();
        return std::make_unique<TextCell>();
    }
    void bindCell(glw::Widget& cell, std::size_t row, std::size_t column) override {
        if (row >= table_.rows.size())
            return;
        const ParamRow& r = table_.rows[row];
        DetailsPanel::Table* t = &table_;
        DetailsPanel* panel = &panel_;
        switch (table_.columns[column]) {
        case Col::Name: static_cast<TextCell&>(cell).show(r.shown.empty() ? r.name : r.shown, !table_.editable()); break;
        case Col::Type:
        case Col::Key: {
            const bool key = table_.columns[column] == Col::Key;
            if (!table_.editable()) {
                static_cast<TextCell&>(cell).show(r.parts.str(), true);
                break;
            }
            if (key && r.parts.container != core::TypeParts::Container::Map) {
                static_cast<ChoiceCell&>(cell).show({}, 0, false, nullptr);         // only a map has a key
                break;
            }
            // the types to choose from, and this one when it is none of them (any, a type not known)
            std::vector<std::string> names = *table_.types;
            const std::string now = key ? r.parts.key : r.parts.value;
            auto at = std::find(names.begin(), names.end(), now);
            if (at == names.end()) {
                names.push_back(now);
                at = names.end() - 1;
            }
            std::vector<glw::LocalizedText> items(names.begin(), names.end());
            static_cast<ChoiceCell&>(cell).show(std::move(items), std::size_t(at - names.begin()), true, [panel, t, row, key, names](std::size_t i) {
                if (row >= t->rows.size() || i >= names.size())
                    return;
                (key ? t->rows[row].parts.key : t->rows[row].parts.value) = names[i];
                panel->tableChanged(*t);
            });
            break;
        }
        case Col::Container:
            static_cast<ChoiceCell&>(cell).show({containerText(core::TypeParts::Container::Single), containerText(core::TypeParts::Container::Array),
                                                 containerText(core::TypeParts::Container::Map)},
                                                std::size_t(r.parts.container), false, [panel, t, row](std::size_t i) {
                                                    if (row >= t->rows.size())
                                                        return;
                                                    t->rows[row].parts.container = core::TypeParts::Container(i);
                                                    panel->tableChanged(*t);
                                                },
                                                PinGlyph{.byRef = r.byRef,
                                                         .connected = true,
                                                         .color = table_.color ? table_.color(r.parts.value) : glw::Color{0.8f, 0.8f, 0.8f, 1},
                                                         .keyColor = table_.color ? table_.color(r.parts.key) : glw::Color{0.8f, 0.8f, 0.8f, 1}});
            break;
        case Col::Ref:
        case Col::Const: {
            const bool ref = table_.columns[column] == Col::Ref;
            static_cast<CheckCell&>(cell).show(ref ? r.byRef : r.isConst, [panel, t, row, ref](bool on) {
                if (row >= t->rows.size())
                    return;
                (ref ? t->rows[row].byRef : t->rows[row].isConst) = on;
                panel->tableChanged(*t);
            });
            break;
        }
        }
    }
    std::unique_ptr<glw::Widget> makeEditor(std::size_t row, std::size_t column) override {
        if (!table_.editable() || table_.columns[column] != Col::Name || row >= table_.rows.size())
            return nullptr;
        auto field = std::make_unique<glw::LineEdit>(glw::LineEdit::Props{.text = table_.rows[row].name});
        field->selectAll();
        return field;
    }
    void commitEdit(glw::Widget& editor, std::size_t row, std::size_t) override {
        if (row >= table_.rows.size())
            return;
        const std::string to = trimmed(static_cast<glw::LineEdit&>(editor).text());
        if (to == table_.rows[row].name)
            return;
        table_.rows[row].name = to;
        panel_.tableChanged(table_);
    }

private:
    DetailsPanel& panel_;
    DetailsPanel::Table& table_;
};

// Around a table: its right button menu and its rows chosen, which the table leaves to whoever has it.
class TableFrame final : public glw::Widget {
public:
    TableFrame(DetailsPanel& panel, DetailsPanel::Table& table, std::unique_ptr<glw::TableView> view) : panel_(panel), table_(table) {
        view_ = &add(std::move(view));
    }

    // The cell under `point`: what its column is, what it has, what a click does.
    std::optional<glw::ToolTip> toolTipAt(glw::Point point) const override {
        glw::ListView& list = view_->list();
        const std::optional<std::size_t> row = list.itemAt(list.mapFromRoot(mapToRoot(point)));
        if (!row || *row >= table_.rows.size())
            return std::nullopt;
        const glw::Point at = mapToRoot(point);
        for (std::size_t column = 0; column < table_.columns.size(); ++column) {
            const glw::Widget* cell = view_->cellWidget(*row, column);
            if (!cell)
                continue;
            const glw::Point in = cell->mapFromRoot(at);
            if (in.x >= 0 && in.y >= 0 && in.x < cell->size().width && in.y < cell->size().height)
                return tipOf(table_.rows[*row], table_.columns[column]);
        }
        return std::nullopt;
    }

protected:
    void onPreviewMouse(glw::MouseEvent& event) override {
        if (event.type != glw::MouseEventType::Press || event.button != glw::platform::MouseButton::Right || !table_.editable())
            return;
        glw::ListView& list = view_->list();
        const std::optional<std::size_t> row = list.itemAt(list.mapFromRoot(mapToRoot(event.position)));
        if (!row || *row >= table_.rows.size())
            return;
        event.handled = true;
        view_->setCurrent(*row);
        panel_.tableMenu(table_, *row, view_->mapFromRoot(mapToRoot(event.position)));
    }

private:
    glw::ToolTip tipOf(const ParamRow& r, Col column) const {
        const bool editable = table_.editable();
        switch (column) {
        case Col::Name:
            if (!editable)
                return {.title = glw::LocalizedText(r.shown.empty() ? r.name : r.shown),
                        .text = r.doc.empty() ? tr("Part of the definition of the node: it cannot be changed.") : glw::LocalizedText(r.doc)};
            return {.title = tr("Name"), .text = glw::LocalizedText(r.name), .note = tr("Double-click or F2 to rename it.")};
        case Col::Type:
            return {.title = tr("Type"), .text = glw::LocalizedText(editable ? r.parts.value : r.parts.str()), .note = editable ? tr("Click to change it.") : glw::LocalizedText()};
        case Col::Container:
            return {.title = tr("Container"),
                    .text = containerText(r.parts.container),
                    .note = tr("A single value, an array of it, or a map from a key type to it. Click to change it.")};
        case Col::Key:
            return {.title = tr("Key"),
                    .text = r.parts.container == core::TypeParts::Container::Map ? glw::LocalizedText(r.parts.key) : tr("(none)"),
                    .note = tr("The key type of a map; only used when the container is a Map.")};
        case Col::Ref:
            return {.title = tr("Ref"),
                    .text = r.byRef ? tr("yes") : tr("no"),
                    .note = tr("Passed by reference: the function reads and writes the caller's variable.")};
        case Col::Const:
            return {.title = tr("Const"),
                    .text = r.isConst ? tr("yes") : tr("no"),
                    .note = tr("The function may not modify the parameter (a const reference needs no variable and makes no copy).")};
        }
        return {};
    }

    DetailsPanel& panel_;
    DetailsPanel::Table& table_;
    glw::TableView* view_ = nullptr;
};

glw::LocalizedText nameProblemText(core::NameProblem why) {
    switch (why) {
    case core::NameProblem::Empty: return tr("A name cannot be empty");
    case core::NameProblem::GraphExists: return tr("A graph named '{name}' already exists");
    case core::NameProblem::VariableExists: return tr("A variable named '{name}' already exists");
    case core::NameProblem::LocalExists: return tr("The function has a local variable named '{name}' already");
    case core::NameProblem::NotAName: return tr("A struct's name is one word: letters, digits and _");
    case core::NameProblem::StructExists: return tr("A struct named '{name}' already exists");
    case core::NameProblem::TypeExists: return tr("The application has a type named '{name}' already");
    case core::NameProblem::None: break;
    }
    return {};
}

} // namespace

DetailsPanel::DetailsPanel(core::Document& doc) : doc_(doc), props_(doc), script_(doc) {
    setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 6.0f));
    setPadding(glw::Insets::all(6));
    search_ = &add(std::make_unique<glw::SearchField>(glw::SearchField::Props{.onChanged = [this](const std::string&) { applyFilter(); }}));
    auto scroll = std::make_unique<glw::ScrollArea>();
    scroll->setStretch(1);
    scroll_ = &add(std::move(scroll));
    changedConnection_ = doc_.changed.connect([this](bool) { scheduleRebuild(); });
    replacedConnection_ = doc_.scriptReplaced.connect([this] { scheduleRebuild(); });
    compiledConnection_ = doc_.compiled.connect([this] { scheduleRebuild(); });
    rebuild();
}

DetailsPanel::~DetailsPanel() {
    doc_.changed.disconnect(changedConnection_);
    doc_.scriptReplaced.disconnect(replacedConnection_);
    doc_.compiled.disconnect(compiledConnection_);
    ++generation_;
}

void DetailsPanel::onRootChanged() {
    scheduleRebuild();
}

std::string DetailsPanel::words(const glw::LocalizedText& text) const {
    const glw::UiContext* ui = context();
    return ui ? ui->translator.resolve(text) : text.source;
}

void DetailsPanel::tell(glw::LocalizedText text) const {
    if (onMessage_)
        onMessage_(std::move(text));
}

void DetailsPanel::tellListProblem(core::ListProblem problem) {
    if (problem == core::ListProblem::None)
        return;
    tell(problem == core::ListProblem::EmptyName ? tr("A name cannot be empty") : tr("The names must be different from each other"));
    scheduleRebuild();
    shown_.clear();             // what the table shows is not the script's: made again
}

glw::Widget* DetailsPanel::field(const std::string& id) const {
    auto it = fields_.find(id);
    return it == fields_.end() ? nullptr : it->second;
}

// ---- what is shown

void DetailsPanel::showNothing() {
    mode_ = Mode::None;
    nodes_.clear();
    rebuild();
}

void DetailsPanel::showVariable(const std::string& owner, const std::string& name) {
    mode_ = Mode::Variable;
    owner_ = owner;
    name_ = name;
    rebuild();
}

void DetailsPanel::showGraph(const std::string& name) {
    mode_ = Mode::Graph;
    owner_.clear();
    name_ = name;
    rebuild();
}

void DetailsPanel::showStruct(const std::string& name) {
    if (mode_ != Mode::Struct || name_ != name)
        fieldRow_ = 0;
    mode_ = Mode::Struct;
    owner_.clear();
    name_ = name;
    rebuild();
}

void DetailsPanel::showNodes(const std::string& graph, std::vector<fg::NodeId> nodes) {
    if (mode_ == Mode::Nodes && owner_ == graph && nodes_ == nodes && !nodes.empty())
        return;
    mode_ = nodes.empty() ? Mode::None : Mode::Nodes;
    owner_ = graph;
    nodes_ = std::move(nodes);
    rebuild();
}

bool DetailsPanel::shownExists() const {
    const fg::Script& s = doc_.script();
    switch (mode_) {
    case Mode::Variable: {
        if (owner_.empty())
            return s.findVariable(name_) != nullptr;
        const fg::Graph* g = s.findGraph(owner_);
        return g && g->findLocal(name_);
    }
    case Mode::Graph: return s.findGraph(name_) != nullptr;
    case Mode::Struct: return s.findStruct(name_) != nullptr;
    case Mode::Nodes: {
        const fg::Graph* g = s.findGraph(owner_);
        return g && std::all_of(nodes_.begin(), nodes_.end(), [g](fg::NodeId id) { return g->findNode(id) != nullptr; });
    }
    case Mode::None: break;
    }
    return true;
}

std::string DetailsPanel::signature() const {
    const fg::Script& s = doc_.script();
    std::string sig = std::to_string(int(mode_)) + "|" + owner_ + "|" + name_ + "|" + std::to_string(fieldRow_) + "\n";
    const auto params = [&sig](const std::vector<fg::ParamDecl>& list) {
        for (const fg::ParamDecl& p : list)
            sig += " " + p.name + ":" + p.type + (p.byRef ? "&" : "") + (p.isConst ? "c" : "");
        sig += "\n";
    };
    const auto graph = [&](const std::string& name) {
        if (const fg::Graph* g = s.findGraph(name)) {
            sig += "g:" + g->name + "|" + g->doc;
            params(g->inputs);
            params(g->outputs);
        }
    };
    switch (mode_) {
    case Mode::Variable:
        if (const fg::VariableDecl* v = owner_.empty() ? s.findVariable(name_) : (s.findGraph(owner_) ? s.findGraph(owner_)->findLocal(name_) : nullptr))
            sig += v->name + "|" + v->type + "|" + v->def.toString() + "|" + v->category + "|" + v->doc;
        break;
    case Mode::Graph: graph(name_); break;
    case Mode::Struct:
        if (const fg::StructDecl* st = s.findStruct(name_)) {
            sig += st->name + "|" + st->doc;
            for (const fg::FieldDecl& f : st->fields)
                sig += " " + f.name + ":" + f.type + "=" + f.def.toString() + "|" + f.doc;
        }
        break;
    case Mode::Nodes: {
        const fg::Graph* g = s.findGraph(owner_);
        for (fg::NodeId id : nodes_) {
            const fg::Node* n = g ? g->findNode(id) : nullptr;
            if (!n)
                continue;
            sig += std::to_string(id) + n->def + "|" + n->comment;
            for (const auto& [k, v] : n->props)
                sig += " " + k + "=" + v;
            for (const auto& [k, v] : n->typeArgs)
                sig += " " + k + ":" + v;
            params(n->inputs);
            params(n->outputs);
            if (n->def == "func.call")
                graph(n->prop("function"));
            for (const fg::Diagnostic& d : doc_.diagnosticsFor(owner_, id))
                sig += "!" + d.message;
            if (const fg::CompiledGraph* cg = doc_.compiledGraph(owner_))
                if (const fg::CompiledNode* cn = cg->findNode(id))
                    for (const fg::CompiledPin& p : cn->pins)
                        sig += " " + p.name + ":" + (p.type ? p.type->name : "");
        }
        graph(owner_);
        break;
    }
    case Mode::None: break;
    }
    return sig;
}

void DetailsPanel::scheduleRebuild() {
    if (rebuildQueued_)
        return;
    glw::UiRoot* r = root();
    if (!r) {
        if (signature() != shown_)
            rebuild();
        return;
    }
    rebuildQueued_ = true;
    const auto later = [this, alive = std::weak_ptr<bool>(alive_)] {
        if (alive.expired())
            return;
        rebuildQueued_ = false;
        if (!shownExists() || signature() != shown_)
            rebuild();
    };
    // after the event that changed it, once its widgets are done with it: a popup's choice (a combo box's) comes in an
    // event of the popup's root, not of this one
    if (glw::Scheduler* s = scheduler())
        s->post(later);
    else
        r->defer(later);
}

void DetailsPanel::edited(bool rebuildAfter) {
    if (rebuildAfter)
        scheduleRebuild();
    else
        shown_ = signature();   // its field shows it already: it stays (and keeps the focus)
}

// ---- the form

void DetailsPanel::rebuild() {
    // a field being typed in, done first; the one with the focus has it again in the new form
    std::string focused;
    if (glw::UiRoot* r = root())
        if (glw::Widget* f = r->focusWidget()) {
            if (auto* edit = dynamic_cast<FieldEdit*>(f))
                edit->finish();
            else if (auto* lines = dynamic_cast<LinesEdit*>(f))
                lines->finish();
            for (const auto& [id, w] : fields_)
                for (glw::Widget* p = f; p; p = p->parent())
                    if (p == w)
                        focused = id;
        }
    if (!shownExists())
        mode_ = Mode::None;
    ++generation_;
    rows_.clear();
    fields_.clear();
    std::vector<std::unique_ptr<Table>> old = std::move(tables_);
    std::vector<std::shared_ptr<std::vector<std::string>>> oldTypes = std::move(typeLists_);
    tables_.clear();
    typeLists_.clear();

    auto body = std::make_unique<glw::Widget>();
    auto grid = std::make_unique<glw::GridLayout>(std::vector<glw::GridColumn>{{}, {.stretch = 1}}, 8.0f, 6.0f);
    grid_ = grid.get();
    body->setLayout(std::move(grid));
    body->setPadding({0, 2, 4, 2});
    body_ = body.get();

    switch (mode_) {
    case Mode::None: note(tr("Select a variable, function, struct or node to see its properties.")); break;
    case Mode::Variable: buildVariable(); break;
    case Mode::Graph: buildGraph(name_); break;
    case Mode::Struct: buildStruct(); break;
    case Mode::Nodes: buildNodes(); break;
    }
    auto nothing = std::make_unique<glw::Label>(glw::Label::Props{.text = tr("Nothing matches the search."), .muted = true, .wrap = true});
    noMatch_ = &body_->add(std::move(nothing));
    grid_->setSpan(*noMatch_, 2);
    noMatch_->setVisible(false);

    scroll_->setContent(std::move(body));
    old.clear();
    // the sections whose + added to them while they were closed: they open now, animated, as the theme moves (the plain
    // style: at once)
    opening_.clear();
    for (glw::CollapsiblePanel* panel : std::exchange(toOpen_, {}))
        panel->setExpanded(true);
    shown_ = signature();
    applyFilter();
    if (!focused.empty())
        if (glw::Widget* w = field(focused))
            if (glw::UiRoot* r = root()) {
                r->layout();
                r->setFocus(w);
            }
}

void DetailsPanel::applyFilter() {
    const std::string query = lowered(trimmed(search_ ? search_->text() : std::string()));
    int shown = 0, total = 0;
    for (Row& row : rows_) {
        bool match = query.empty() || row.heading;
        if (!match) {
            std::string all = row.text ? row.text() : std::string();
            if (row.label)
                if (auto* l = dynamic_cast<glw::Label*>(row.label))
                    all += "\n" + words(l->text());
            match = lowered(all).find(query) != std::string::npos;
        }
        if (row.label)
            row.label->setVisible(match);
        if (row.field)
            row.field->setVisible(match);
        if (!row.heading) {
            ++total;
            shown += match ? 1 : 0;
        }
    }
    if (noMatch_)
        noMatch_->setVisible(!query.empty() && total > 0 && shown == 0);
}

void DetailsPanel::heading(glw::LocalizedText text) {
    const glw::UiContext* ui = context();
    auto label = std::make_unique<glw::Label>(glw::Label::Props{.text = std::move(text), .fontSize = ui ? ui->theme.fontSize + 2 : 15.0f});
    label->setMargin({0, 6, 0, 0});
    glw::Widget& w = body_->add(std::move(label));
    grid_->setSpan(w, 2);
    rows_.push_back({.field = &w, .heading = true});
}

void DetailsPanel::note(glw::LocalizedText text, std::optional<glw::Color> color) {
    const std::string plain = words(text);
    glw::Widget& w = body_->add(std::make_unique<glw::Label>(glw::Label::Props{.text = std::move(text), .color = color, .muted = !color, .wrap = true}));
    grid_->setSpan(w, 2);
    rows_.push_back({.field = &w, .text = [plain] { return plain; }});
}

void DetailsPanel::addRow(glw::LocalizedText label, std::unique_ptr<glw::Widget> field, const std::string& id, std::function<std::string()> text) {
    auto l = std::make_unique<glw::Label>(std::move(label));
    l->setAlignment(glw::Alignment::Start, glw::Alignment::Center);
    glw::Widget& lw = body_->add(std::move(l));
    glw::Widget& fw = body_->add(std::move(field));
    if (!id.empty())
        fields_[id] = &fw;
    if (!text) {
        glw::Widget* f = &fw;
        text = [f]() -> std::string {
            if (auto* e = dynamic_cast<glw::LineEdit*>(f))
                return e->text();
            if (auto* c = dynamic_cast<glw::ComboBox*>(f))
                return c->current() < c->items().size() ? c->items()[c->current()].source : std::string();
            if (auto* l = dynamic_cast<glw::Label*>(f))
                return l->text().source;
            return {};
        };
    }
    rows_.push_back({.label = &lw, .field = &fw, .text = std::move(text)});
}

void DetailsPanel::addWide(std::unique_ptr<glw::Widget> widget, const std::string& id, std::function<std::string()> text) {
    glw::Widget& w = body_->add(std::move(widget));
    grid_->setSpan(w, 2);
    if (!id.empty())
        fields_[id] = &w;
    rows_.push_back({.field = &w, .text = std::move(text)});
}

std::unique_ptr<glw::LineEdit> DetailsPanel::textField(const std::string& text, std::function<void(const std::string&)> done) {
    return std::make_unique<FieldEdit>(text, [this, gen = generation_, done = std::move(done)](const std::string& typed) {
        if (gen == generation_)
            done(typed);
    });
}

void DetailsPanel::addDefault(const fg::TypeDesc* t, const fg::Value& stored, std::function<void(const fg::Value&)> set) {
    if (!t || !(t->isPrimitive() || t->isEnum())) {
        auto note = std::make_unique<glw::Label>(glw::Label::Props{.text = t && t->isStruct()      ? tr("Each field starts with the default of the struct.")
                                                                           : t && t->isContainer() ? tr("Containers start empty.")
                                                                                                   : tr("Objects start as null."),
                                                                   .muted = true,
                                                                   .wrap = true});
        addRow(tr("Default"), std::move(note), "default");
        return;
    }
    const fg::Registry& reg = doc_.registry();
    fg::CompiledPin cp;
    cp.type = t;
    try {
        cp.def = stored.isNull() ? reg.defaultValue(t) : reg.convert(stored, t);
    } catch (...) {
        cp.def = reg.defaultValue(t);
    }
    if (t->isEnum() && !t->hasEnumValue(cp.def.toInt()))
        cp.def = reg.defaultValue(t);       // what the compiler uses too
    const unsigned gen = generation_;
    if (t->isEnum() && !t->enumValues.empty()) {
        // the names of the values to choose from, never a bare number
        std::vector<glw::LocalizedText> items;
        std::vector<int64_t> values;
        std::size_t current = 0;
        for (const auto& e : t->enumValues) {
            if (e.value == cp.def.toInt())
                current = items.size();
            items.emplace_back(enumValueText(t, e.value));
            values.push_back(e.value);
        }
        auto box = std::make_unique<glw::ComboBox>(glw::ComboBox::Props{.items = std::move(items), .current = current});
        box->setOnChanged([this, gen, values, set](std::size_t i) {
            if (gen == generation_ && i < values.size())
                set(fg::Value(values[i]));
        });
        addRow(tr("Default"), std::move(box), "default");
        return;
    }
    if (t->kind == fg::TypeKind::Bool) {
        auto box = std::make_unique<glw::CheckBox>(glw::CheckBox::Props{.checked = cp.def.toBool(), .onToggled = [this, gen, set](bool on) {
                                                                            if (gen == generation_)
                                                                                set(fg::Value(on));
                                                                        }});
        box->setAlignment(glw::Alignment::Start, glw::Alignment::Center);
        const std::string text = cp.def.toBool() ? "true" : "false";
        addRow(tr("Default"), std::move(box), "default", [text] { return text; });
        return;
    }
    auto edit = textField(defaultText(cp), [t, set](const std::string& typed) { set(parseValue(t, typed)); });
    if (t->kind == fg::TypeKind::Bytes)
        edit->setPlaceholder(tr("Hex, e.g. 48 65 6C 6C 6F"));
    addRow(tr("Default"), std::move(edit), "default");
}

glw::CollapsiblePanel& DetailsPanel::section(glw::LocalizedText title, const std::string& key, std::function<void()> add) {
    auto panel = std::make_unique<glw::CollapsiblePanel>(glw::CollapsiblePanel::Props{
        .title = std::move(title), .expanded = !closed_.count(key) && !opening_.count(key), .onToggled = [this, key](bool open) {
            if (open)
                closed_.erase(key);
            else
                closed_.insert(key);
            if (onSectionsChanged_)
                onSectionsChanged_(closed_);
        }});
    glw::CollapsiblePanel* raw = panel.get();
    if (add) {
        // "+" adds at once; a closed section opens to show what was added (it stays open from then on)
        auto plus = std::make_shared<glw::Action>(glw::LocalizedText(), [this, gen = generation_, key, raw, add] {
            if (gen != generation_)
                return;
            if (!raw->expanded()) {
                opening_.insert(key);           // made again closed, then opened, animated (opened())
                closed_.erase(key);
                if (onSectionsChanged_)
                    onSectionsChanged_(closed_);
            }
            add();
        });
        plus->setIcon(glw::Icon::fromSvg(addIcon));
        plus->setToolTip(tr("Add"));
        panel->addAction(plus);
    }
    if (opening_.erase(key) > 0)
        toOpen_.push_back(raw);
    glw::Widget& w = body_->add(std::move(panel));
    grid_->setSpan(w, 2);
    return *raw;
}

DetailsPanel::Table& DetailsPanel::addTable(glw::CollapsiblePanel* in, const std::string& id) {
    Table& t = *tables_.back();
    t.color = [this](const std::string& type) { return typeColor(type); };
    auto view = std::make_unique<glw::TableView>();
    glw::TableView& v = *view;
    t.view = &v;
    v.setModel(std::make_shared<ParamModel>(t));
    v.setDelegate(std::make_unique<ParamDelegate>(*this, t));
    v.setThemeStyle(&glw::Theme::itemView);    // as the theme's lists are (no cells' borders), as it changes
    v.setHeaderVisible(false);              // the cells' tooltips name their columns
    v.setRowHeight(rowHeight);
    v.setSelectionMode(t.editable() ? glw::SelectionMode::Single : glw::SelectionMode::None);
    v.setEditTriggers(t.editable() ? glw::EditTriggers::DoubleClick | glw::EditTriggers::F2 : glw::EditTriggers::None);
    v.setColumnSizing(0, glw::ColumnSizing::Stretch);
    // the names: at least as wide as the widest (the table scrolls across when it is narrower), the rest of the room theirs
    float names = glw::TableView::minimumColumnWidth;
    if (const glw::UiContext* ui = context())
        for (const ParamRow& r : t.rows) {
            const glw::text::TextLayout text(r.shown.empty() ? r.name : r.shown, glw::text::TextStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize});
            names = std::max(names, std::ceil(text.size().width) + 14);     // a TextCell's room around its text
        }
    v.setColumnWidth(0, names);
    for (std::size_t c = 1; c < t.columns.size(); ++c)
        v.setColumnSizing(c, glw::ColumnSizing::FitContent);
    Table* raw = &t;
    v.setOnCurrentChanged([this, raw, gen = generation_](std::optional<std::size_t> row) {
        if (gen == generation_ && row)
            tableSelected(*raw, *row);
    });
    // as tall as its rows: the panel scrolls, not each table
    const float bar = !t.editable() ? 0.0f : context() ? context()->theme.scrollBar.width : 8.0f;     // room for the bar across, when it is narrow
    const float h = float(std::max<std::size_t>(t.rows.size(), 1)) * rowHeight + bar + 2;
    v.setMinimumSize({0, h});
    v.setMaximumSize({glw::unbounded, h});
    auto frame = std::make_unique<TableFrame>(*this, t, std::move(view));
    std::function<std::string()> text = [raw] {
        std::string all;
        for (const ParamRow& r : raw->rows)
            all += (r.shown.empty() ? r.name : r.shown) + " " + r.parts.str() + "\n";
        return all;
    };
    if (in) {
        glw::Widget& w = in->addContent(std::move(frame));
        fields_[id] = &v;
        rows_.push_back({.field = in, .text = std::move(text)});
        (void)w;
    } else {
        addWide(std::move(frame), {}, std::move(text));
        fields_[id] = &v;
    }
    return t;
}

// ---- variables

void DetailsPanel::buildVariable() {
    const fg::Script& s = doc_.script();
    const bool local = !owner_.empty();
    const fg::VariableDecl* v = local ? (s.findGraph(owner_) ? s.findGraph(owner_)->findLocal(name_) : nullptr) : s.findVariable(name_);
    if (!v) {
        note(tr("The variable no longer exists."));
        return;
    }
    heading(local ? tr("Local variable") : tr("Variable"));
    const std::string owner = owner_, name = name_;
    addRow(tr("Name"), textField(v->name, [this, local, owner, name](const std::string& typed) {
               const std::string to = trimmed(typed);
               core::NameProblem why = core::NameProblem::None;
               if (script_.rename(local ? core::Member::Local : core::Member::Variable, owner, name, to,
                                  words(local ? tr("Rename local variable") : tr("Rename variable")), &why)) {
                   name_ = to;
                   edited(true);
                   return;
               }
               tell(nameProblemText(why).arg("name", to));
               shown_.clear();
               scheduleRebuild();
           }), "name");

    // the type: a value, an array of it or a map to it from a key
    const core::TypeParts parts = core::TypeParts::parse(v->type);
    const auto typeNames = std::make_shared<std::vector<std::string>>();
    for (const fg::TypeDesc* t : doc_.types()->types())
        typeNames->push_back(t->name);
    const auto retype = [this, owner, name, parts](core::TypeParts to) {
        if (to.str() == parts.str())
            return;
        props_.setVariableType(owner, name, to.str(), words(tr("Change variable type")));
        edited(true);
    };
    const auto typeBox = [&](const std::string& now, std::function<void(const std::string&)> chosen) {
        std::vector<std::string> names = *typeNames;
        auto at = std::find(names.begin(), names.end(), now);
        if (at == names.end()) {
            names.push_back(now);
            at = names.end() - 1;
        }
        auto box = std::make_unique<glw::ComboBox>(glw::ComboBox::Props{.items = std::vector<glw::LocalizedText>(names.begin(), names.end()),
                                                                        .current = std::size_t(at - names.begin()),
                                                                        .searchable = true,
                                                                        .placeholder = tr("Search types")});
        box->setOnChanged([this, gen = generation_, names, chosen](std::size_t i) {
            if (gen == generation_ && i < names.size())
                chosen(names[i]);
        });
        return box;
    };
    addRow(tr("Type"), typeBox(parts.value, [retype, parts](const std::string& t) {
               core::TypeParts to = parts;
               to.value = t;
               retype(to);
           }), "type");
    auto container = std::make_unique<glw::ComboBox>(glw::ComboBox::Props{
        .items = {containerText(core::TypeParts::Container::Single), containerText(core::TypeParts::Container::Array), containerText(core::TypeParts::Container::Map)},
        .current = std::size_t(parts.container)});
    container->setOnChanged([this, gen = generation_, retype, parts](std::size_t i) {
        if (gen != generation_)
            return;
        core::TypeParts to = parts;
        to.container = core::TypeParts::Container(i);
        retype(to);
    });
    addRow(tr("Container"), std::move(container), "container");
    if (parts.container == core::TypeParts::Container::Map)
        addRow(tr("Key type"), typeBox(parts.key, [retype, parts](const std::string& t) {
                   core::TypeParts to = parts;
                   to.key = t;
                   retype(to);
               }), "key");

    addDefault(doc_.types()->findType(v->type), v->def, [this, owner, name](const fg::Value& value) {
        props_.setVariableDefault(owner, name, value, words(tr("Edit default value")));
        edited(false);
    });
    addRow(tr("Category"), textField(v->category, [this, owner, name](const std::string& text) {
               props_.setVariableCategory(owner, name, text, words(tr("Edit category")));
               edited(false);
           }), "category");
    addRow(tr("Description"), textField(v->doc, [this, owner, name](const std::string& text) {
               props_.setVariableDoc(owner, name, text, words(tr("Edit description")));
               edited(false);
           }), "description");
}

// ---- graphs and functions

// A function or an event graph: its name, description and (a function's) parameters. Shown for a function chosen, and while
// its entry or a return node is selected, or a node that calls it (the function it calls).
void DetailsPanel::buildGraph(const std::string& graphName) {
    const fg::Graph* g = doc_.script().findGraph(graphName);
    if (!g) {
        note(tr("The graph no longer exists."));
        return;
    }
    const bool function = g->kind == fg::GraphKind::Function;
    heading(function ? tr("Function") : tr("Event graph"));
    addRow(tr("Name"), textField(g->name, [this, function, graphName](const std::string& typed) {
               const std::string to = trimmed(typed);
               core::NameProblem why = core::NameProblem::None;
               if (script_.rename(function ? core::Member::Function : core::Member::EventGraph, {}, graphName, to,
                                  words(function ? tr("Rename function") : tr("Rename graph")), &why)) {
                   if (name_ == graphName)
                       name_ = to;
                   if (owner_ == graphName)
                       owner_ = to;
                   edited(true);
                   if (onGraphRenamed_)
                       onGraphRenamed_(graphName, to);
                   return;
               }
               tell(nameProblemText(why).arg("name", to));
               shown_.clear();
               scheduleRebuild();
           }), "name");
    addRow(tr("Description"), textField(g->doc, [this, graphName](const std::string& text) {
               props_.setGraphDoc(graphName, text, words(tr("Edit description")));
               edited(false);
           }), "description");
    if (!function)
        return;

    auto names = std::make_shared<std::vector<std::string>>();
    for (const fg::TypeDesc* t : doc_.types()->types())
        names->push_back(t->name);
    for (bool inputs : {true, false}) {
        const std::vector<fg::ParamDecl>& params = inputs ? g->inputs : g->outputs;
        glw::CollapsiblePanel& panel = section(inputs ? tr("Inputs") : tr("Outputs"), inputs ? "function/inputs" : "function/outputs",
                                               [this, graphName, inputs] {
                                                   props_.addParam(graphName, inputs, words(tr("Add parameter")));
                                                   edited(true);
                                               });
        auto t = std::make_unique<Table>();
        t->kind = Table::Kind::Params;
        t->inputs = inputs;
        t->owner = graphName;
        t->types = names.get();
        bool anyMap = false;
        for (const fg::ParamDecl& p : params) {
            ParamRow r{.name = p.name, .parts = core::TypeParts::parse(p.type), .byRef = p.byRef, .isConst = p.isConst};
            anyMap = anyMap || r.parts.container == core::TypeParts::Container::Map;
            t->rows.push_back(std::move(r));
        }
        t->columns = {Col::Name, Col::Type, Col::Container};
        if (anyMap)
            t->columns.push_back(Col::Key);
        if (inputs) {
            t->columns.push_back(Col::Ref);
            t->columns.push_back(Col::Const);
        }
        tables_.push_back(std::move(t));
        typeLists_.push_back(names);
        addTable(&panel, inputs ? "inputs" : "outputs");
    }
}

// ---- structs

// A struct of the script: its name, description and fields; the default and description of the field chosen in the table
// below it.
void DetailsPanel::buildStruct() {
    const fg::StructDecl* st = doc_.script().findStruct(name_);
    if (!st) {
        note(tr("The struct no longer exists."));
        return;
    }
    heading(tr("Struct"));
    const std::string sname = name_;
    addRow(tr("Name"), textField(st->name, [this, sname](const std::string& typed) {
               const std::string to = trimmed(typed);
               core::NameProblem why = core::NameProblem::None;
               if (script_.rename(core::Member::Struct, {}, sname, to, words(tr("Rename struct")), &why)) {
                   name_ = to;
                   edited(true);
                   return;
               }
               tell(nameProblemText(why).arg("name", to));
               shown_.clear();
               scheduleRebuild();
           }), "name");
    addRow(tr("Description"), textField(st->doc, [this, sname](const std::string& text) {
               props_.setStructDoc(sname, text, words(tr("Edit description")));
               edited(false);
           }), "description");

    auto names = std::make_shared<std::vector<std::string>>();
    for (const fg::TypeDesc* t : doc_.types()->types())
        if (t->name != sname)
            names->push_back(t->name);
    glw::CollapsiblePanel& panel = section(tr("Fields"), "struct/fields", [this, sname] {
        const fg::StructDecl* cur = doc_.script().findStruct(sname);
        if (!cur)
            return;
        fieldRow_ = cur->fields.size();
        props_.addField(sname, words(tr("Add field")));
        edited(true);
    });
    auto t = std::make_unique<Table>();
    t->kind = Table::Kind::Fields;
    t->owner = sname;
    t->types = names.get();
    bool anyMap = false;
    for (const fg::FieldDecl& f : st->fields) {
        ParamRow r{.name = f.name, .parts = core::TypeParts::parse(f.type)};
        anyMap = anyMap || r.parts.container == core::TypeParts::Container::Map;
        t->rows.push_back(std::move(r));
    }
    t->columns = {Col::Name, Col::Type, Col::Container};
    if (anyMap)
        t->columns.push_back(Col::Key);
    tables_.push_back(std::move(t));
    typeLists_.push_back(names);
    Table& table = addTable(&panel, "fields");

    if (st->fields.empty()) {
        note(tr("Add a field with the + of Fields."));
        return;
    }
    fieldRow_ = std::min(fieldRow_, st->fields.size() - 1);
    table.view->setCurrent(fieldRow_);
    const fg::FieldDecl& f = st->fields[fieldRow_];
    heading(tr("Field: {name}").arg("name", f.name));
    const std::string field = f.name;
    addDefault(doc_.types()->findType(f.type), f.def, [this, sname, field](const fg::Value& value) {
        props_.setFieldDefault(sname, field, value, words(tr("Edit default value")));
        edited(false);
    });
    addRow(tr("Description"), textField(f.doc, [this, sname, field](const std::string& text) {
               props_.setFieldDoc(sname, field, text, words(tr("Edit description")));
               edited(false);
           }), "fieldDescription");
}

// ---- nodes

void DetailsPanel::buildNodes() {
    const fg::Graph* g = doc_.script().findGraph(owner_);
    const fg::CompiledGraph* cg = doc_.compiledGraph(owner_);
    if (!g || !cg || nodes_.empty())
        return;
    if (nodes_.size() > 1) {
        heading(tr("{count} nodes selected").arg("count", std::to_string(nodes_.size())));
        return;
    }
    const fg::Node* node = g->findNode(nodes_.front());
    const fg::CompiledNode* cn = cg->findNode(nodes_.front());
    if (!node || !cn)
        return;
    const std::string graph = owner_;
    const fg::NodeId id = node->id;
    const bool entryOrReturn = g->kind == fg::GraphKind::Function && (cn->op == fg::NodeOp::FunctionEntry || cn->op == fg::NodeOp::Return);
    // a call edits the signature of the function it calls, as its entry or return node does
    const std::string called = cn->op == fg::NodeOp::CallFunction ? node->prop("function") : std::string();
    if (entryOrReturn || !called.empty())
        buildGraph(called.empty() ? graph : called);

    heading(cn->op == fg::NodeOp::FunctionEntry ? tr("Function entry node") : glw::LocalizedText(nodeTitle(*cn)));
    if (cn->def && !cn->def->doc.empty())
        note(glw::LocalizedText(nodeDoc(cn->def)));
    addRow(tr("Definition"),
           std::make_unique<glw::Label>(glw::LocalizedText(cn->defId + (cn->def && !cn->def->source.empty() ? "  [" + cn->def->source + "]" : std::string()))),
           "definition");
    for (const fg::Diagnostic& d : doc_.diagnosticsFor(graph, id))
        note(glw::LocalizedText(d.message), d.severity == fg::Diagnostic::Severity::Error ? errorColor : warningColor);

    if (cn->op == fg::NodeOp::Event) {
        // the application's: handled (or not), never made, renamed or changed
        const fg::EventDesc* ev = cn->defId.rfind("event:", 0) == 0 ? doc_.registry().findEvent(cn->defId.substr(6)) : nullptr;
        addRow(tr("Event"), std::make_unique<glw::Label>(glw::LocalizedText(ev ? ev->name : cn->defId.substr(std::min<std::size_t>(6, cn->defId.size())))), "event");
        addRow(tr("Arguments"), std::make_unique<glw::Label>(ev ? glw::LocalizedText(fg::describeParams(*ev)) : tr("unknown")), "arguments");
        note(tr("This event is raised by the application. A script can only handle it."));
    }
    if (cn->op == fg::NodeOp::GetVariable || cn->op == fg::NodeOp::SetVariable)
        addRow(tr("Variable"), std::make_unique<glw::Label>(glw::LocalizedText(node->prop("name") + "  (" + node->prop("scope") + ")")), "variable");
    if (cn->defId.rfind("struct.", 0) == 0) {
        addRow(tr("Struct"), std::make_unique<glw::Label>(glw::LocalizedText(node->prop("struct"))), "struct");
        const fg::StructDecl* st = doc_.script().findStruct(node->prop("struct"));
        if (cn->defId == "struct.set" && st) {
            // the field it changes: another one keeps the struct's link, its value is cleared (it was of the old field's type)
            std::vector<std::string> fields;
            std::vector<glw::LocalizedText> items;
            for (const fg::FieldDecl& f : st->fields) {
                fields.push_back(f.name);
                items.emplace_back(f.name + "  (" + f.type + ")");
            }
            const std::string current = node->prop("field");
            auto at = std::find(fields.begin(), fields.end(), current);
            if (at == fields.end()) {
                fields.push_back(current);
                items.emplace_back(current);
                at = fields.end() - 1;
            }
            auto box = std::make_unique<glw::ComboBox>(glw::ComboBox::Props{.items = std::move(items), .current = std::size_t(at - fields.begin())});
            box->setOnChanged([this, gen = generation_, graph, id, fields, current](std::size_t i) {
                if (gen != generation_ || i >= fields.size() || fields[i] == current)
                    return;
                props_.setStructSetField(graph, id, fields[i], words(tr("Change field")));
                edited(true);
            });
            addRow(tr("Field"), std::move(box), "field");
        }
    }
    // the settings of the node: pin counts, the cases of a switch...
    if (cn->def)
        for (const fg::NodeSetting& setting : cn->def->settings) {
            const std::string key = setting.key;
            const std::string label = nodeTexts().setting(cn->defId, setting);
            const std::string value = node->prop(key, setting.def);
            const glw::LocalizedText what = tr("Change {setting}").arg("setting", lowered(label));
            if (setting.kind == fg::NodeSetting::Kind::Count) {
                auto spin = std::make_unique<glw::SpinBox>(glw::SpinBox::Props{.value = std::atof(value.c_str()),
                                                                               .minimum = double(setting.min),
                                                                               .maximum = double(setting.max)});
                spin->setOnChanged([this, gen = generation_, graph, id, key, what](double v) {
                    if (gen != generation_)
                        return;
                    props_.setNodeSetting(graph, id, key, std::to_string(int(std::lround(v))), words(what), true);
                    edited(true);
                });
                addRow(glw::LocalizedText(label), std::move(spin), "setting:" + key, [value] { return value; });
            } else if (setting.kind == fg::NodeSetting::Kind::Choice) {
                // shown translated (an enum literal: its values), stored as they are
                const std::string enumType = cn->defId.rfind("enum:", 0) == 0 ? cn->defId.substr(5) : std::string();
                std::vector<std::string> choices = setting.choices;
                auto at = std::find(choices.begin(), choices.end(), value);
                if (at == choices.end()) {          // a value the definition does not have (any more)
                    choices.push_back(value);
                    at = choices.end() - 1;
                }
                std::vector<glw::LocalizedText> items;
                for (const std::string& c : choices)
                    items.emplace_back(enumType.empty() ? c : nodeTexts().enumValue(enumType, c));
                auto box = std::make_unique<glw::ComboBox>(glw::ComboBox::Props{.items = std::move(items), .current = std::size_t(at - choices.begin())});
                box->setOnChanged([this, gen = generation_, graph, id, key, what, choices](std::size_t i) {
                    if (gen != generation_ || i >= choices.size())
                        return;
                    props_.setNodeSetting(graph, id, key, choices[i], words(what));
                    edited(true);
                });
                addRow(glw::LocalizedText(label), std::move(box), "setting:" + key);
            } else {
                // a text per line, applied when it loses the focus
                auto lines = std::make_unique<LinesEdit>();
                lines->setLineNumbers(false);
                lines->start(value);
                lines->done = [this, gen = generation_, graph, id, key, what](const std::string& text) {
                    if (gen != generation_)
                        return;
                    props_.setNodeSetting(graph, id, key, text, words(what));
                    edited(true);
                };
                const float h = (context() ? context()->theme.fontSize * 1.4f : 18.0f) * 6 + 12;
                lines->setMinimumSize({0, h});
                lines->setMaximumSize({glw::unbounded, h});
                LinesEdit* raw = lines.get();
                addRow(glw::LocalizedText(label), std::move(lines), "setting:" + key, [raw] { return raw->text(); });
            }
        }
    // the type parameters
    if (cn->def)
        for (const fg::TypeParam& tp : cn->def->typeParams) {
            const auto resolved = cn->typeArgs.find(tp.name);
            std::vector<std::string> choices{""};
            std::vector<glw::LocalizedText> items{resolved == cn->typeArgs.end() || resolved->second.empty()
                                                      ? tr("Auto")
                                                      : tr("Auto ({type})").arg("type", resolved->second)};
            if (!tp.allowed.empty())
                choices.insert(choices.end(), tp.allowed.begin(), tp.allowed.end());
            else
                for (const fg::TypeDesc* t : doc_.types()->types())
                    choices.push_back(t->name);
            for (std::size_t i = 1; i < choices.size(); ++i)
                items.emplace_back(choices[i]);
            std::size_t current = 0;
            if (auto set = node->typeArgs.find(tp.name); set != node->typeArgs.end()) {
                auto at = std::find(choices.begin(), choices.end(), set->second);
                current = at == choices.end() ? 0 : std::size_t(at - choices.begin());
            }
            auto box = std::make_unique<glw::ComboBox>(glw::ComboBox::Props{.items = std::move(items), .current = current, .searchable = tp.allowed.empty() || tp.allowed.size() > 12});
            const std::string param = tp.name;
            box->setOnChanged([this, gen = generation_, graph, id, param, choices](std::size_t i) {
                if (gen != generation_ || i >= choices.size())
                    return;
                props_.setNodeTypeArg(graph, id, param, choices[i], words(tr("Change node type")));
                edited(true);
            });
            addRow(tr("Type {name}").arg("name", param), std::move(box), "typeArg:" + param);
        }
    // its inputs and outputs (a function's own are above, where they are edited)
    if (!entryOrReturn && called.empty())
        buildNodePins(*node, *cn);
    addRow(tr("Comment"), textField(node->comment, [this, graph, id](const std::string& text) {
               core::GraphEdits(doc_, graph).setNodeComment(id, text, words(tr("Edit node comment")));
               edited(false);
           }), "comment");
}

// "Inputs" and "Outputs" of a node: its pins, which belong to its definition (shown, not edited); and those the user added to
// a definition that allows it (NodeDef::customInputs / customOutputs), edited as a function's parameters.
void DetailsPanel::buildNodePins(const fg::Node& node, const fg::CompiledNode& cn) {
    auto names = std::make_shared<std::vector<std::string>>();
    for (const fg::TypeDesc* t : doc_.types()->types())
        names->push_back(t->name);
    names->insert(names->begin(), "any");
    for (bool inputs : {true, false}) {
        const fg::PinDir dir = inputs ? fg::PinDir::In : fg::PinDir::Out;
        const bool custom = cn.def && (inputs ? cn.def->customInputs : cn.def->customOutputs);
        const std::vector<fg::ParamDecl>& mine = inputs ? node.inputs : node.outputs;
        std::set<std::string> customNames;
        if (custom)
            for (const fg::ParamDecl& p : mine)
                customNames.insert(p.name);
        std::vector<const fg::CompiledPin*> fixed;
        std::set<std::string> fixedNames;       // all of them, hidden or not: a custom pin may not take one of these names
        for (const fg::CompiledPin& p : cn.pins) {
            if (p.dir != dir)
                continue;
            if (custom && p.kind == fg::PinKind::Data && customNames.count(p.name))
                continue;
            fixedNames.insert(p.name);
            if (!p.hidden)
                fixed.push_back(&p);
        }
        if (fixed.empty() && !custom)
            continue;
        const std::string graph = owner_;
        const fg::NodeId id = node.id;
        std::function<void()> add;
        if (custom)
            add = [this, graph, id, inputs, fixedNames] {
                props_.addCustomPin(graph, id, inputs, fixedNames, words(tr("Add pin")));
                edited(true);
            };
        glw::CollapsiblePanel& panel = section(inputs ? tr("Inputs") : tr("Outputs"), inputs ? "node/inputs" : "node/outputs", add);
        if (!fixed.empty()) {
            auto t = std::make_unique<Table>();
            t->kind = Table::Kind::Fixed;
            t->inputs = inputs;
            t->columns = {Col::Name, Col::Type};
            for (const fg::CompiledPin* p : fixed) {
                ParamRow r{.name = p->name, .shown = pinLabel(cn, p->name), .doc = p->doc.empty() ? std::string() : pinDoc(cn.defId, p->name, p->doc)};
                r.parts.value = p->kind == fg::PinKind::Exec ? "exec" : p->type ? p->type->name : "any";
                t->rows.push_back(std::move(r));
            }
            tables_.push_back(std::move(t));
            addTable(&panel, inputs ? "fixedInputs" : "fixedOutputs");
        }
        if (custom) {
            auto t = std::make_unique<Table>();
            t->kind = Table::Kind::NodePins;
            t->inputs = inputs;
            t->owner = graph;
            t->node = id;
            t->fixed = fixedNames;
            t->types = names.get();
            bool anyMap = false;
            for (const fg::ParamDecl& p : mine) {
                ParamRow r{.name = p.name, .parts = core::TypeParts::parse(p.type)};
                anyMap = anyMap || r.parts.container == core::TypeParts::Container::Map;
                t->rows.push_back(std::move(r));
            }
            t->columns = {Col::Name, Col::Type, Col::Container};
            if (anyMap)
                t->columns.push_back(Col::Key);
            tables_.push_back(std::move(t));
            typeLists_.push_back(names);
            addTable(&panel, inputs ? "nodeInputs" : "nodeOutputs");
        }
    }
}

// ---- the tables

glw::Color DetailsPanel::typeColor(const std::string& type) const {
    return rgb(doc_.registry().typeColor(doc_.types()->typeOrAny(type)));
}

void DetailsPanel::tableChanged(Table& t) {
    core::ListProblem problem = core::ListProblem::None;
    switch (t.kind) {
    case Table::Kind::Params: problem = props_.setParams(t.owner, t.inputs, t.decls(), words(tr("Edit parameters"))); break;
    case Table::Kind::Fields: problem = props_.setFields(t.owner, t.decls(), words(tr("Edit fields"))); break;
    case Table::Kind::NodePins: problem = props_.setCustomPins(t.owner, t.node, t.inputs, t.decls(), t.fixed, words(tr("Edit node pins"))); break;
    case Table::Kind::Fixed: return;
    }
    if (problem != core::ListProblem::None) {
        tellListProblem(problem);
        return;
    }
    edited(true);
}

void DetailsPanel::tableSelected(Table& t, std::size_t row) {
    if (t.kind != Table::Kind::Fields || row == fieldRow_)
        return;
    fieldRow_ = row;
    scheduleRebuild();          // the field's default and description below
}

void DetailsPanel::tableMenu(Table& t, std::size_t row, glw::Point at) {
    if (row >= t.rows.size())
        return;
    Table* raw = &t;
    const auto inForm = [this, gen = generation_](std::function<void()> fn) {
        return [this, gen, fn] {
            if (gen == generation_)
                fn();
        };
    };
    std::vector<glw::MenuItem> items;
    items.push_back({.text = tr("Rename"), .onTriggered = inForm([raw, row] { raw->view->edit(row, 0); }), .shortcut = tr("F2")});
    if (t.kind != Table::Kind::Params) {
        const bool canUp = row > 0, canDown = row + 1 < t.rows.size();
        const auto move = [this, raw, row](bool up) {
            if (raw->kind == Table::Kind::Fields) {
                props_.moveField(raw->owner, row, up, words(tr("Move field")));
                fieldRow_ = up ? row - 1 : row + 1;
            } else {
                props_.moveCustomPin(raw->owner, raw->node, raw->inputs, row, up, words(tr("Move pin")));
            }
            edited(true);
        };
        items.push_back({.text = tr("Move Up"), .onTriggered = inForm([move] { move(true); }), .enabled = canUp});
        items.push_back({.text = tr("Move Down"), .onTriggered = inForm([move] { move(false); }), .enabled = canDown});
        items.push_back({.separator = true});
    }
    items.push_back({.text = tr("Remove"), .onTriggered = inForm([this, raw, row] {
                         switch (raw->kind) {
                         case Table::Kind::Params: props_.removeParam(raw->owner, raw->inputs, row, words(tr("Remove parameter"))); break;
                         case Table::Kind::Fields: props_.removeField(raw->owner, row, words(tr("Remove field"))); break;
                         case Table::Kind::NodePins: props_.removeCustomPin(raw->owner, raw->node, raw->inputs, row, words(tr("Remove pin"))); break;
                         case Table::Kind::Fixed: return;
                         }
                         edited(true);
                     })});
    giveIcons(items);
    glw::showContextMenu(*t.view, at, std::move(items));
}

} // namespace fged::gl
