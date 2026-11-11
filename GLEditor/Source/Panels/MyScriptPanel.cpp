// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Panels/MyScriptPanel.hpp"

#include "Graph/GraphStyle.hpp"
#include "NodeTexts.hpp"

#include <FlowGraph/TypeScope.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/Action.hpp>
#include <GLWidgets/Widgets/CollapsiblePanel.hpp>
#include <GLWidgets/Widgets/Icon.hpp>
#include <GLWidgets/Widgets/ItemModel.hpp>
#include <GLWidgets/Widgets/Layout.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/ListView.hpp>
#include <GLWidgets/Widgets/Menu.hpp>
#include <GLWidgets/Widgets/ScrollArea.hpp>
#include <GLWidgets/Widgets/TableView.hpp>
#include <GLWidgets/Widgets/ToolBar.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace fged::gl {

using glw::tr;

namespace {

constexpr float rowHeight = 24;
constexpr float dragThreshold = 5;

// The + of a section: the Qt editor's (Editor/Resources/Icons/add.svg), in its green.
constexpr const char* addIcon = R"(<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 24 24">
  <path d="M12 3.5 V20.5 M3.5 12 H20.5" stroke="#34d17c" stroke-width="4.4" stroke-linecap="round"/>
</svg>)";

std::string lowered(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A member, a row of a section's table.
struct Entry {
    std::string owner;                      // a local's function; an event's graph
    std::string name;
    std::string detail;                     // what it is: a variable's type, a struct's fields
    std::optional<glw::Color> color;        // its type's (a dot before its name)
    fg::NodeId node = 0;                    // an event's handler
    bool missing = false;                   // an event the application does not have
    std::string dragFormat, dragContent;    // what a drag of it onto a graph carries (none: it is not dragged)
};

// A section's rows: the name (editable, but an event's) and, when its members have one, what it is.
class MemberModel final : public glw::TableModel {
public:
    MemberModel(bool renamable, bool details) : renamable_(renamable), details_(details) {}
    std::vector<Entry> rows;

    void set(std::vector<Entry> entries) {
        rows = std::move(entries);
        notifyReset();
    }
    std::size_t rowCount() const override { return rows.size(); }
    std::size_t columnCount() const override { return details_ ? 2 : 1; }
    glw::ItemFlags flags(std::size_t, std::size_t column) const override {
        return column == 0 && renamable_ ? glw::ItemFlags::Editable : glw::ItemFlags::None;
    }

private:
    bool renamable_;
    bool details_;
};

// A cell: the name, after its color, or (muted) what it is.
class MemberCell final : public glw::Widget {
public:
    MemberCell() { setHitTestVisible(false); }      // (the table's row takes the mouse)

    void show(const Entry& entry, bool detail) {
        text_ = detail ? entry.detail : entry.name;
        color_ = detail ? std::nullopt : entry.color;
        detail_ = detail;
        missing_ = entry.missing;
        event_ = entry.node != 0;
        layout_.reset();
        invalidateLayout();         // measured again: one cell measures every row of a fitting column
        invalidatePaint();
    }

protected:
    // as wide as its text (what the table's fitting column is measured by), after the color's room
    glw::Size measureContent(glw::Size) override {
        const glw::UiContext* ui = context();
        if (!ui)
            return {80, rowHeight};
        const glw::Theme& t = ui->theme;
        const glw::text::TextLayout text(text_, glw::text::TextStyle{{&ui->fonts.defaultFont()}, detail_ ? t.fontSize - 1 : t.fontSize});
        return {std::ceil(text.size().width) + (detail_ ? 6.0f : 20.0f) + 8, rowHeight};
    }
    void paint(glw::PaintContext& context) override {
        glw::paint::Painter& p = context.painter;
        const glw::Theme& t = context.ui.theme;
        float x = 6;
        if (!detail_) {
            if (color_)
                p.fillCircle({x + 4, size().height / 2}, 4, *color_);
            else if (event_)
                p.fillCircle({x + 4, size().height / 2}, 3, missing_ ? glw::Color{0.9f, 0.3f, 0.3f, 1} : headerColor(fg::NodeStyle::Event));
            x += 14;
        }
        if (!layout_)
            layout_.emplace(text_, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, detail_ ? t.fontSize - 1 : t.fontSize});
        p.pushClip({x, 0, std::max(0.0f, size().width - x - 4), size().height});
        context.drawText(*layout_, {x, (size().height - layout_->size().height) / 2 + layout_->opticalOffset()},
                         detail_ || missing_ ? t.palette.mutedText : t.palette.text);
        p.popClip();
    }

private:
    std::string text_;
    std::optional<glw::Color> color_;
    bool detail_ = false, missing_ = false, event_ = false;
    std::optional<glw::text::TextLayout> layout_;
};

} // namespace

// A section: its panel, its table and what it shows.
struct MyScriptPanel::Section {
    core::Member member = core::Member::Function;
    bool events = false;                    // the events the event graphs handle (none renamed, added, dragged)
    glw::CollapsiblePanel* panel = nullptr;
    glw::TableView* table = nullptr;
    std::shared_ptr<MemberModel> model;
};

namespace {

// How a section's cells are made, shown and renamed (the name's, in a field).
class MemberDelegate final : public glw::TableDelegate {
public:
    MemberDelegate(MyScriptPanel& panel, MyScriptPanel::Section& section) : panel_(panel), section_(section) {}

    std::unique_ptr<glw::Widget> makeCell(std::size_t) override { return std::make_unique<MemberCell>(); }
    void bindCell(glw::Widget& cell, std::size_t row, std::size_t column) override {
        if (row < section_.model->rows.size())
            static_cast<MemberCell&>(cell).show(section_.model->rows[row], column == 1);
    }
    std::unique_ptr<glw::Widget> makeEditor(std::size_t row, std::size_t column) override {
        if (column != 0 || row >= section_.model->rows.size())
            return nullptr;
        auto field = std::make_unique<glw::LineEdit>(glw::LineEdit::Props{.text = section_.model->rows[row].name});
        field->selectAll();
        return field;
    }
    void commitEdit(glw::Widget& editor, std::size_t row, std::size_t) override {
        panel_.renamed(section_, row, static_cast<glw::LineEdit&>(editor).text());
    }

private:
    MyScriptPanel& panel_;
    MyScriptPanel::Section& section_;
};

// Around a section's table: the right button (its menu) and dragging a member onto a graph, which the table does not do.
class SectionTable final : public glw::Widget {
public:
    SectionTable(MyScriptPanel& panel, MyScriptPanel::Section& section, std::unique_ptr<glw::TableView> table)
        : panel_(panel), section_(section) {
        table_ = &add(std::move(table));
    }

protected:
    void onPreviewMouse(glw::MouseEvent& event) override {
        if (event.type != glw::MouseEventType::Press)
            return;
        glw::ListView& list = table_->list();
        const std::optional<std::size_t> row = list.itemAt(list.mapFromRoot(mapToRoot(event.position)));
        if (!row || *row >= section_.model->rows.size())
            return;
        if (event.button == glw::platform::MouseButton::Right) {
            event.handled = true;
            table_->setCurrent(*row);
            panel_.menu(section_, *row, table_->mapFromRoot(mapToRoot(event.position)));
            return;
        }
        if (event.button != glw::platform::MouseButton::Left || event.clicks != 1)
            return;
        // the table chooses it; a drag that follows is ours (the mouse ours, once the press is handled)
        const Entry& entry = section_.model->rows[*row];
        if (entry.dragFormat.empty())
            return;
        drag_ = Drag{entry.dragFormat, entry.dragContent, event.position};
        if (glw::UiRoot* r = root())
            r->defer([this, r] { r->captureMouse(*this); });
    }

    void onMouse(glw::MouseEvent& event) override {
        if (!drag_)
            return;
        if (event.type == glw::MouseEventType::Move) {
            event.handled = true;
            if (std::hypot(event.position.x - drag_->from.x, event.position.y - drag_->from.y) > dragThreshold && root()) {
                const Drag d = *std::exchange(drag_, std::nullopt);
                root()->startDrag({.data = {d.format, d.content}});
            }
        } else if (event.type == glw::MouseEventType::Release) {
            event.handled = true;
            drag_.reset();
        }
    }

private:
    struct Drag {
        std::string format, content;
        glw::Point from;
    };
    MyScriptPanel& panel_;
    MyScriptPanel::Section& section_;
    glw::TableView* table_ = nullptr;
    std::optional<Drag> drag_;
};

} // namespace

MyScriptPanel::MyScriptPanel(core::Document& doc) : doc_(doc), edits_(doc) {
    setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 6.0f));
    setPadding(glw::Insets::all(6));
    search_ = &add(std::make_unique<glw::LineEdit>(glw::LineEdit::Props{.placeholder = tr("Search"), .onChanged = [this](const std::string&) { applyFilter(); }}));

    auto list = std::make_unique<glw::Widget>();
    glw::Widget& column = *list;
    column.setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 4.0f));
    const auto section = [&](glw::LocalizedText title, core::Member member, bool events, bool adds) {
        auto s = std::make_unique<Section>();
        s->member = member;
        s->events = events;
        // what is not a function, an event: what it is, in a second column
        const bool details = member == core::Member::Variable || member == core::Member::Local || member == core::Member::Struct;
        s->model = std::make_shared<MemberModel>(!events, details);
        auto panel = std::make_unique<glw::CollapsiblePanel>(glw::CollapsiblePanel::Props{.title = std::move(title), .expanded = true});
        if (adds) {
            auto plus = std::make_shared<glw::Action>(glw::LocalizedText(), [this, member] { addMember(member); });
            plus->setIcon(glw::Icon::fromSvg(addIcon));
            plus->setToolTip(member == core::Member::Function   ? tr("Add a function")
                             : member == core::Member::Variable ? tr("Add a variable")
                             : member == core::Member::Local    ? tr("Add a local variable")
                                                                : tr("Add a struct"));
            panel->addAction(plus);
        }
        Section* raw = s.get();
        auto table = std::make_unique<glw::TableView>();
        glw::TableView& t = *table;
        t.setModel(s->model);
        t.setDelegate(std::make_unique<MemberDelegate>(*this, *raw));
        t.setHeaderVisible(false);
        t.setRowHeight(rowHeight);
        t.setSelectionMode(glw::SelectionMode::Single);
        t.setEditTriggers(glw::EditTriggers::F2);           // a double click opens it
        // the name all the room what it is leaves: that as wide as the widest (measured again as they change)
        t.setColumnSizing(0, glw::ColumnSizing::Stretch);
        if (details)
            t.setColumnSizing(1, glw::ColumnSizing::FitContent);
        t.setOnCurrentChanged([this, raw](std::optional<std::size_t>) { selected(*raw); });
        t.setOnActivated([this, raw](std::size_t row) { activated(*raw, row); });
        s->table = &t;
        s->panel = &column.add(std::move(panel));
        s->panel->addContent(std::make_unique<SectionTable>(*this, *raw, std::move(table)));
        sections_.push_back(std::move(s));
    };
    section(tr("Event Graphs"), core::Member::EventGraph, true, false);
    section(tr("Functions"), core::Member::Function, false, true);
    section(tr("Variables"), core::Member::Variable, false, true);
    section(tr("Local Variables"), core::Member::Local, false, true);
    section(tr("Structs"), core::Member::Struct, false, true);
    auto scroll = std::make_unique<glw::ScrollArea>();
    scroll->setContent(std::move(list));
    scroll->setStretch(1);
    add(std::move(scroll));

    changedConnection_ = doc_.changed.connect([this](bool) { scheduleRebuild(); });
    replacedConnection_ = doc_.scriptReplaced.connect([this] { scheduleRebuild(); });
    rebuild();
}

MyScriptPanel::~MyScriptPanel() {
    doc_.changed.disconnect(changedConnection_);
    doc_.scriptReplaced.disconnect(replacedConnection_);
}

void MyScriptPanel::onRootChanged() {
    // the tables as the theme's lists are (no cells' borders)
    if (const glw::UiContext* ui = context()) {
        for (const auto& s : sections_)
            s->table->setStyle(ui->theme.itemView);
    }
    scheduleRebuild();
}

std::string MyScriptPanel::words(const glw::LocalizedText& text) const {
    const glw::UiContext* ui = context();
    return ui ? ui->translator.resolve(text) : text.source;
}

void MyScriptPanel::tell(glw::LocalizedText text) const {
    if (onMessage_)
        onMessage_(std::move(text));
}

void MyScriptPanel::setCurrentGraph(const std::string& graph) {
    if (graph == current_)
        return;
    current_ = graph;
    scheduleRebuild();
}

void MyScriptPanel::scheduleRebuild() {
    if (rebuildQueued_)
        return;
    glw::UiRoot* r = root();
    if (!r) {
        rebuild();
        return;
    }
    rebuildQueued_ = true;
    r->defer([this] {
        rebuildQueued_ = false;
        if (signature() != shown_)
            rebuild();
    });
}

std::string MyScriptPanel::signature() const {
    const fg::Script& s = doc_.script();
    std::string sig = current_ + "\n";
    for (const fg::Graph& g : s.graphs) {
        sig += (g.kind == fg::GraphKind::Function ? "f:" : "g:") + g.name + "\n";
        if (g.kind == fg::GraphKind::Event) {
            for (const fg::Node& n : g.nodes)
                if (n.def.rfind("event:", 0) == 0)
                    sig += " e:" + n.def + std::to_string(n.id) + "\n";
        }
        if (g.name == current_)
            for (const fg::VariableDecl& v : g.locals)
                sig += " l:" + v.name + ":" + v.type + "\n";
    }
    for (const fg::VariableDecl& v : s.variables)
        sig += "v:" + v.name + ":" + v.type + "\n";
    for (const fg::StructDecl& st : s.structs) {
        sig += "s:" + st.name;
        for (const fg::FieldDecl& f : st.fields)
            sig += "," + f.name;
        sig += "\n";
    }
    return sig;
}

MyScriptPanel::Section* MyScriptPanel::sectionOf(core::Member member) const {
    for (const auto& s : sections_) {
        if (s->member == member)
            return s.get();
    }
    return nullptr;
}

void MyScriptPanel::rebuild() {
    shown_ = signature();
    const fg::Script& s = doc_.script();
    const fg::Registry& reg = doc_.registry();
    const auto types = doc_.types();
    std::vector<Entry> events, functions, variables, locals, structs;
    for (const fg::Graph& g : s.graphs) {
        if (g.kind == fg::GraphKind::Function) {
            functions.push_back({.name = g.name, .color = headerColor(fg::NodeStyle::Function), .dragFormat = functionDragFormat, .dragContent = g.name});
            continue;
        }
        // the events the event graphs handle
        for (const fg::Node& n : g.nodes) {
            if (n.def.rfind("event:", 0) == 0)
                events.push_back({.owner = g.name, .name = n.def.substr(6), .node = n.id, .missing = reg.findNode(n.def) == nullptr});
        }
    }
    for (const fg::VariableDecl& v : s.variables)
        variables.push_back({.name = v.name,
                             .detail = v.type,
                             .color = rgb(reg.typeColor(types->typeOrAny(v.type))),
                             .dragFormat = variableDragFormat,
                             .dragContent = "global|" + v.name});
    // the local variables of the function shown (none: the section hidden)
    const fg::Graph* function = s.findGraph(current_);
    const bool hasLocals = function && function->kind == fg::GraphKind::Function;
    if (hasLocals) {
        for (const fg::VariableDecl& v : function->locals)
            locals.push_back({.owner = current_,
                              .name = v.name,
                              .detail = v.type,
                              .color = rgb(reg.typeColor(types->typeOrAny(v.type))),
                              .dragFormat = variableDragFormat,
                              .dragContent = "local|" + v.name});
    }
    for (const fg::StructDecl& st : s.structs) {
        std::string fields;
        for (const fg::FieldDecl& f : st.fields)
            fields += (fields.empty() ? "" : ", ") + f.name;
        structs.push_back({.name = st.name, .detail = "{" + fields + "}", .color = rgb(reg.typeColor(types->findStruct(st.name)))});
    }
    selecting_ = true;          // (the tables' rows made again: no member chosen by it)
    sectionOf(core::Member::EventGraph)->model->set(std::move(events));
    sectionOf(core::Member::Function)->model->set(std::move(functions));
    sectionOf(core::Member::Variable)->model->set(std::move(variables));
    sectionOf(core::Member::Local)->model->set(std::move(locals));
    sectionOf(core::Member::Struct)->model->set(std::move(structs));
    Section& localSection = *sectionOf(core::Member::Local);
    localSection.panel->setVisible(hasLocals);
    if (hasLocals)
        localSection.panel->setTitle(tr("Local Variables of {function}").arg("function", current_));
    selecting_ = false;

    // what was selected, still there: selected
    if (selected_) {
        Section* section = sectionOf(selected_->member);
        const std::optional<std::size_t> row = section ? rowIn(*section, selected_->owner, selected_->name) : std::nullopt;
        if (row) {
            selecting_ = true;
            section->table->setCurrent(*row);
            selecting_ = false;
        } else {
            selected_.reset();
            if (onSelectionChanged_)
                onSelectionChanged_();
        }
    }
    applyFilter();
}

void MyScriptPanel::applyFilter() {
    const std::string query = lowered(search_ ? search_->text() : std::string());
    for (const auto& s : sections_) {
        MemberModel* model = s->model.get();
        if (query.empty())
            s->table->setFilter(nullptr);
        else
            s->table->setFilter([model, query](std::size_t row) {
                return row < model->rows.size() && lowered(model->rows[row].name).find(query) != std::string::npos;
            });
        s->table->reorder();
        fit(*s);
    }
}

void MyScriptPanel::fit(Section& section) {
    // as tall as its rows that show: the panel scrolls, not each table
    const float h = float(section.table->shownRowCount()) * rowHeight;
    section.table->setMinimumSize({0, h});
    section.table->setMaximumSize({glw::unbounded, h});
}

std::optional<std::size_t> MyScriptPanel::rowIn(const Section& section, const std::string& owner, const std::string& name) const {
    const std::vector<Entry>& rows = section.model->rows;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].owner == owner && rows[i].name == name)
            return i;
    }
    return std::nullopt;
}

glw::Widget* MyScriptPanel::rowOf(core::Member member, const std::string& owner, const std::string& name) const {
    Section* section = sectionOf(member);
    const std::optional<std::size_t> row = section ? rowIn(*section, owner, name) : std::nullopt;
    return row ? section->table->cellWidget(*row, 0) : nullptr;
}

void MyScriptPanel::clearSelection() {
    selecting_ = true;
    for (const auto& s : sections_)
        s->table->setCurrent(std::nullopt);
    selecting_ = false;
    selected_.reset();
}

void MyScriptPanel::selected(Section& section) {
    if (selecting_)
        return;
    const std::optional<std::size_t> row = section.table->current();
    if (!row || *row >= section.model->rows.size())
        return;
    // one member chosen in the panel: the other tables let go of theirs
    selecting_ = true;
    for (const auto& s : sections_) {
        if (s.get() != &section)
            s->table->setCurrent(std::nullopt);
    }
    selecting_ = false;
    const Entry& entry = section.model->rows[*row];
    if (section.events) {
        selected_.reset();
        if (onEventChosen_)
            onEventChosen_(entry.owner, entry.node, false);
        return;
    }
    selected_ = Selection{section.member, entry.owner, entry.name};
    if (onSelectionChanged_)
        onSelectionChanged_();
}

void MyScriptPanel::activated(Section& section, std::size_t row) {
    if (row >= section.model->rows.size())
        return;
    const Entry& entry = section.model->rows[row];
    if (section.events) {
        if (onEventChosen_)
            onEventChosen_(entry.owner, entry.node, true);
        return;
    }
    if (section.member == core::Member::Function && onOpenGraph_)
        onOpenGraph_(entry.name);
}

void MyScriptPanel::menu(Section& section, std::size_t row, glw::Point at) {
    if (row >= section.model->rows.size())
        return;
    const Entry entry = section.model->rows[row];
    Section* s = &section;
    std::vector<glw::MenuItem> items;
    if (section.events) {
        items.push_back({.text = tr("Go to the Handler"), .onTriggered = [this, entry] {
                             if (onEventChosen_)
                                 onEventChosen_(entry.owner, entry.node, true);
                         }});
    } else {
        if (section.member == core::Member::Function)
            items.push_back({.text = tr("Open"), .onTriggered = [this, entry] {
                                 if (onOpenGraph_)
                                     onOpenGraph_(entry.name);
                             }});
        items.push_back({.text = tr("Rename"), .onTriggered = [this, s, entry] {
                             if (const std::optional<std::size_t> r = rowIn(*s, entry.owner, entry.name))
                                 s->table->edit(*r, 0);
                         }, .shortcut = tr("F2")});
        if (section.member == core::Member::Function)
            items.push_back({.text = tr("Duplicate"), .onTriggered = [this, entry] { edits_.duplicateFunction(entry.name, words(tr("Duplicate function"))); }});
        const core::Member member = section.member;
        items.push_back({.text = tr("Delete"), .onTriggered = [this, member, entry] { edits_.remove(member, entry.owner, entry.name, words(tr("Delete"))); }});
    }
    glw::showContextMenu(*section.table, at, std::move(items));
}

void MyScriptPanel::addMember(core::Member member) {
    search_->setText("");           // the new one would be hidden by the filter
    applyFilter();
    const glw::LocalizedText label = member == core::Member::Function   ? tr("Add function")
                                     : member == core::Member::Variable ? tr("Add variable")
                                     : member == core::Member::Local    ? tr("Add local variable")
                                                                        : tr("Add struct");
    const std::string base = member == core::Member::Function   ? "NewFunction"
                             : member == core::Member::Variable ? "NewVar"
                             : member == core::Member::Local    ? "NewLocal"
                                                                : "NewStruct";
    const std::string owner = member == core::Member::Local ? current_ : std::string();
    const std::string name = edits_.add(member, base, words(label), owner);
    if (name.empty())
        return;
    if (member == core::Member::Function && onOpenGraph_)
        onOpenGraph_(name);
    beginRename(member, owner, name);
}

void MyScriptPanel::beginRename(core::Member member, const std::string& owner, const std::string& name) {
    if (signature() != shown_)
        rebuild();
    Section* section = sectionOf(member);
    if (!section)
        return;
    section->panel->setExpanded(true);      // animated, as the theme moves (the plain style: at once)
    const std::optional<std::size_t> row = rowIn(*section, owner, name);
    if (!row)
        return;
    section->table->setCurrent(*row);
    // its cell, laid out, edited
    if (glw::UiRoot* r = root())
        r->layout();
    section->table->edit(*row, 0);
}

void MyScriptPanel::renamed(Section& section, std::size_t row, const std::string& typed) {
    if (row >= section.model->rows.size())
        return;
    std::string to = typed;
    while (!to.empty() && to.front() == ' ')
        to.erase(to.begin());
    while (!to.empty() && to.back() == ' ')
        to.pop_back();
    const Entry entry = section.model->rows[row];
    if (to == entry.name)
        return;
    const glw::LocalizedText label = section.member == core::Member::Function   ? tr("Rename function")
                                     : section.member == core::Member::Variable ? tr("Rename variable")
                                     : section.member == core::Member::Local    ? tr("Rename local variable")
                                                                                : tr("Rename struct");
    core::NameProblem why = core::NameProblem::None;
    if (edits_.rename(section.member, entry.owner, entry.name, to, words(label), &why)) {
        if (selected_ && selected_->name == entry.name && selected_->member == section.member)
            selected_->name = to;
        if (onSelectionChanged_)
            onSelectionChanged_();
        return;
    }
    switch (why) {
    case core::NameProblem::Empty: tell(tr("A name cannot be empty")); break;
    case core::NameProblem::GraphExists: tell(tr("A graph named '{name}' already exists").arg("name", to)); break;
    case core::NameProblem::VariableExists: tell(tr("A variable named '{name}' already exists").arg("name", to)); break;
    case core::NameProblem::LocalExists: tell(tr("The function has a local variable named '{name}' already").arg("name", to)); break;
    case core::NameProblem::NotAName: tell(tr("A struct's name is one word: letters, digits and _")); break;
    case core::NameProblem::StructExists: tell(tr("A struct named '{name}' already exists").arg("name", to)); break;
    case core::NameProblem::TypeExists: tell(tr("The application has a type named '{name}' already").arg("name", to)); break;
    case core::NameProblem::None: break;
    }
}

} // namespace fged::gl
