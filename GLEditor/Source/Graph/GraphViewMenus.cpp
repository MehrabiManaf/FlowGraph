// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// The graph's search of nodes to add, its menus of the right button, the boxes its values are typed in, its dialogs.
#include "Graph/CommentView.hpp"
#include "Icons.hpp"
#include "Graph/GraphStyle.hpp"
#include "Graph/GraphView.hpp"
#include "Graph/NodeView.hpp"
#include "NodeTexts.hpp"
#include "Panels/MyScriptPanel.hpp"

#include <FlowGraph/Intellisense.hpp>
#include <FlowGraph/Vm.hpp>
#include <GLWidgets/Widgets/Dialog.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/Menu.hpp>
#include <GLWidgets/Widgets/SearchPopup.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace fged::gl {

using glw::tr;

namespace {

// The box a value is typed in: Enter or the focus leaving it takes what was typed, Escape does not.
class ValueEditor final : public glw::LineEdit {
public:
    ValueEditor(std::string text, std::function<void(bool take)> done, std::function<void(const std::string&)> changed) : done_(std::move(done)) {
        setText(std::move(text));
        selectAll();
        setOnSubmit([this](const std::string&) { finish(true); });
        setOnChanged(std::move(changed));
    }

protected:
    void onKey(glw::KeyEvent& event) override {
        if (event.pressed && event.key == glw::platform::Key::Escape) {
            event.handled = true;
            finish(false);
            return;
        }
        glw::LineEdit::onKey(event);
    }
    void onFocusChanged(bool focused) override {
        glw::LineEdit::onFocusChanged(focused);
        if (!focused)
            finish(true);
    }

private:
    void finish(bool take) {
        if (auto done = std::exchange(done_, nullptr))
            done(take);
    }
    std::function<void(bool)> done_;
};

// The path of a category of the palette, as menus show it: "Math|Float" as "Math › Float".
std::string categoryShown(const std::string& path) {
    std::string out = nodeTexts().category(path);
    for (std::size_t at = out.find('|'); at != std::string::npos; at = out.find('|', at))
        out.replace(at, 1, " \xE2\x80\xBA ");
    return out;
}

} // namespace

// ---- adding nodes ----

void GraphView::openPalette(glw::Point at, const core::PinEnd* from) {
    const fg::Script& script = doc_.script();
    const auto types = doc_.types();
    fg::NodeBuildCtx ctx{registry(), &script, script.findGraph(graph_)};
    ctx.types = types.get();
    auto entries = std::make_shared<std::vector<fg::NodeTemplate>>(fg::buildPalette(ctx));
    if (from && from->valid()) {
        fg::PinFilter filter;
        filter.kind = from->kind;
        filter.dir = from->dir;
        filter.type = from->wildcard ? nullptr : from->type;
        *entries = fg::filterPalette(registry(), *entries, filter, types.get());
    }
    if (entries->empty()) {
        tell(tr("No node fits that pin"));
        return;
    }
    // by their titles in the language of the editor, their folders after them; found as Core's palette finds them (its
    // ranking: the title, then the keywords - "+" for Add - then the folder), the English title and folder among the words
    // of a translated one (it is found in either language)
    auto searched = std::make_shared<std::vector<fg::NodeTemplate>>(*entries);
    std::vector<glw::SearchItem> items;
    items.reserve(entries->size());
    for (fg::NodeTemplate& t : *searched) {
        const fg::NodeDef* def = registry().findNode(t.defId);
        const std::string english = t.title, englishCategory = t.category;
        t.title = nodeTexts().nodeTitle(def, t.props, t.title);
        t.category = nodeTexts().category(t.category);
        t.keywords += " " + english + " " + englishCategory;
        // in a tree of its category's folders ("Math|Float": Math, then Float in it)
        std::vector<glw::LocalizedText> path;
        const std::string folders = nodeTexts().category(englishCategory);
        for (std::size_t start = 0; start <= folders.size();) {
            const std::size_t end = std::min(folders.find('|', start), folders.size());
            if (end > start)
                path.emplace_back(folders.substr(start, end - start));
            start = end + 1;
        }
        items.push_back({glw::LocalizedText(t.title), glw::LocalizedText(categoryShown(englishCategory)), std::move(path)});
    }
    const core::PinEnd pin = from ? *from : core::PinEnd{};
    const glw::Point shown = fromCanvas(at);
    glw::showSearchPopup(*this, {shown.x, shown.y, 0, 0},
                         {.items = std::move(items),
                          .placeholder = pin.valid() ? tr("Search the nodes that fit the pin") : tr("Search the nodes"),
                          .onChosen =
                              [this, entries, pin, at](std::size_t i) {
                                  // from an output, it goes after the point; from an input, before it
                                  glw::Point place = pin.valid() && pin.dir == fg::PinDir::In ? glw::Point{at.x - 220, at.y - 20} : glw::Point{at.x, at.y - 20};
                                  if (snap_)
                                      place = {std::round(place.x / snapStep_) * snapStep_, std::round(place.y / snapStep_) * snapStep_};
                                  const std::string step = "add-node:" + graph_ + ":" + std::to_string(++addSerial_);
                                  std::string linked;
                                  const fg::NodeId made = edits_.addNode((*entries)[i], place.x, place.y, pin.valid() ? &pin : nullptr,
                                                                         words(tr("Add node")), &linked, step);
                                  if (!made)
                                      return;
                                  // snapping: level with the pin it was made from, its link straight (one step with it)
                                  const NodeView* view = nodeView(made);
                                  if (snap_ && view && !linked.empty()) {
                                      const auto mine = view->pinCenter(linked, pin.dir == fg::PinDir::Out ? fg::PinDir::In : fg::PinDir::Out);
                                      const auto theirs = pinPoint(pin.node, pin.pin, pin.dir);
                                      if (mine && theirs)
                                          edits_.place({{made, place.x, theirs->y - mine->y}}, {}, words(tr("Add node")), step);
                                  }
                                  select({made});
                                  focus();
                              },
                          .search = [searched](const std::string& query) { return fg::searchPaletteIndices(*searched, query); },
                          // the nodes in a tree of their categories (those that fit a pin: all of it open)
                          .tree = true,
                          .expanded = pin.valid()});
}

// ---- what is dropped on it ----

glw::DropAction GraphView::onDragOver(const glw::DragEvent& event) {
    const bool ours = event.data.format == functionDragFormat || event.data.format == variableDragFormat;
    return ours ? glw::DropAction::Copy : glw::DropAction::None;
}

glw::DropAction GraphView::onDrop(const glw::DragEvent& event) {
    glw::Point at = toCanvas(event.position);
    if (snap_)
        at = {std::round(at.x / snapStep_) * snapStep_, std::round(at.y / snapStep_) * snapStep_};
    if (event.data.format == functionDragFormat) {
        // a call of the function
        if (const fg::NodeId made = edits_.addCall(event.data.content, at.x, at.y,
                                                   words(tr("Add call to {function}").arg("function", event.data.content)))) {
            select({made});
            focus();
        }
        return glw::DropAction::Copy;
    }
    if (event.data.format != variableDragFormat)
        return glw::DropAction::None;
    const std::size_t bar = event.data.content.find('|');
    if (bar == std::string::npos)
        return glw::DropAction::None;
    const std::string scope = event.data.content.substr(0, bar), name = event.data.content.substr(bar + 1);
    const auto make = [this, scope, name, at](bool set) {
        if (const fg::NodeId made = edits_.addVariableNode(name, scope, set, at.x, at.y,
                                                           words(set ? tr("Add Set variable") : tr("Add Get variable")))) {
            select({made});
            focus();
        }
    };
    // Ctrl: a Get, Alt: a Set; neither: chosen in a menu, once the drop is over
    if (glw::platform::hasAny(event.modifiers, glw::platform::shortcutModifier))
        make(false);
    else if (glw::platform::hasAny(event.modifiers, glw::platform::KeyModifiers::Alt))
        make(true);
    else if (glw::UiRoot* r = root())
        r->defer([this, make, name, shown = event.position] {
            std::vector<glw::MenuItem> items{{.text = tr("Get {name}").arg("name", name), .onTriggered = [make] { make(false); }},
                                             {.text = tr("Set {name}").arg("name", name), .onTriggered = [make] { make(true); }}};
            giveIcons(items);
            glw::showContextMenu(*this, shown, std::move(items));
        });
    return glw::DropAction::Copy;
}

// ---- comments, breakpoints ----

void GraphView::addComment(glw::Point at) {
    glw::Rect area;
    bool any = false;
    for (fg::NodeId id : selectedNodes_) {
        if (NodeView* v = nodeView(id)) {
            area = any ? area.united(itemRect(*v)) : itemRect(*v);
            any = true;
        }
    }
    area = any ? glw::Rect{area.x - 30, area.y - 50, area.width + 60, area.height + 80} : glw::Rect{at.x, at.y, 320, 180};
    const int id = edits_.addComment(area.x, area.y, area.width, area.height, words(tr("Comment")), words(tr("Add comment")));
    select({}, {id});
}

void GraphView::toggleBreakpoints() {
    for (fg::NodeId id : selectedNodes_) {
        doc_.debugger().toggleBreakpoint(graph_, id);
        if (NodeView* v = nodeView(id))
            v->invalidatePaint();
    }
}

// ---- the menus of the right button ----

void GraphView::contextMenu(glw::Point at) {
    // what is there: a node (selected with it, unless it is selected already), a comment's header, or the canvas
    NodeView* node = nullptr;
    for (const auto& [id, view] : nodes_) {
        if (itemRect(*view).contains(at))
            node = view;
    }
    CommentView* comment = nullptr;
    if (!node) {
        for (CommentView* c : comments_) {
            const glw::Rect r = itemRect(*c);
            if (glw::Rect{r.x, r.y, r.width, commentHeader}.contains(at))
                comment = c;
        }
    }
    // a link (not on a node or a comment): what is done with it alone
    if (!node && !comment) {
        if (const std::optional<fg::Connection> link = linkAt(at)) {
            selectedNodes_.clear();
            selectedComments_.clear();
            selectedLink_ = link;
            showSelection();
            selectionChanged();
            const fg::Connection c = *link;
            std::vector<glw::MenuItem> items{
                {.text = tr("Delete Link"), .onTriggered = [this, c] { edits_.remove({}, {c}, {}, words(tr("Delete link"))); }, .shortcut = tr("Del")},
                {.text = tr("Add Reroute Node"), .onTriggered = [this, c, at] {
                     if (!edits_.insertReroute(c, at.x - 22, at.y - 10, words(tr("Add reroute"))))
                         tell(tr("Reroute nodes are only for links of data"));
                 }}};
            giveIcons(items);
            glw::showContextMenu(*this, fromCanvas(at), std::move(items));
            return;
        }
    }
    if (node && !selectedNodes_.contains(node->id()))
        select({node->id()});
    std::vector<glw::MenuItem> items;
    const bool selection = !selectedNodes_.empty();
    if (node) {
        const fg::NodeId id = node->id();
        // a pin there: its links broken (Alt and a click does it too)
        if (const std::optional<NodeView::PinHit> pin = node->pinAt(at - itemPosition(*node))) {
            const core::PinEnd end = core::PinEnd::of(node->compiled(), pin->name, pin->dir);
            const fg::CompiledNode& cn = node->compiled();
            const int index = cn.findPinIndex(pin->name, pin->dir);
            const bool linked = index >= 0 && cn.pins[std::size_t(index)].connected;
            items.push_back({.text = tr("Break All Links"), .onTriggered = [this, end] { edits_.breakLinks(end, words(tr("Break links"))); },
                             .shortcut = tr("Alt+Click"), .enabled = linked});
            items.push_back(glw::MenuItem::separatorItem());
        }
        items.push_back({.text = tr("Edit Node Comment..."), .onTriggered = [this, id] {
                             const fg::Node* n = doc_.graph(graph_) ? doc_.graph(graph_)->findNode(id) : nullptr;
                             askText(tr("Node Comment"), n ? n->comment : std::string(), [this, id](std::string text) {
                                 edits_.setNodeComment(id, text, words(tr("Edit node comment")));
                             });
                         }});
        items.push_back({.text = tr("Toggle Breakpoint"), .onTriggered = [this] { toggleBreakpoints(); }, .shortcut = tr("F9"),
                         .checked = isBreakpoint(id)});
        if (selectedNodes_.size() >= 2)
            items.push_back({.text = tr("Collapse to Function..."), .onTriggered = [this] { collapseSelection(); }});
        if (onFindReferences_)
            items.push_back({.text = tr("Find References"), .onTriggered = [this, id] {
                                 if (onFindReferences_)
                                     onFindReferences_(id);
                             }, .shortcut = tr("Shift+F12")});
        items.push_back(glw::MenuItem::separatorItem());
    } else if (comment) {
        const int id = comment->box().id;
        items.push_back({.text = tr("Edit Comment Text..."), .onTriggered = [this, id] {
                             std::string text;
                             if (const fg::Graph* g = doc_.graph(graph_)) {
                                 for (const fg::CommentBox& c : g->comments) {
                                     if (c.id == id)
                                         text = c.text;
                                 }
                             }
                             askText(tr("Comment"), text,
                                     [this, id](std::string text) { edits_.setCommentText(id, text, words(tr("Edit comment"))); });
                         }});
        items.push_back({.text = tr("Delete Comment"), .onTriggered = [this, id] { edits_.remove({}, {}, {id}, words(tr("Delete"))); }});
        items.push_back(glw::MenuItem::separatorItem());
    } else {
        items.push_back({.text = tr("Add Node..."), .onTriggered = [this, at] { openPalette(at); }, .shortcut = tr("Space")});
        items.push_back({.text = tr("Add Comment"), .onTriggered = [this, at] { addComment(at); }});
        items.push_back(glw::MenuItem::separatorItem());
    }
    if (selection) {
        items.push_back({.text = tr("Cut"), .onTriggered = [this] { cutSelection(); }});
        items.push_back({.text = tr("Copy"), .onTriggered = [this] { copySelection(); }});
        items.push_back({.text = tr("Duplicate"), .onTriggered = [this] { duplicateSelection(); }});
        items.push_back({.text = tr("Delete"), .onTriggered = [this] { deleteSelection(); }, .shortcut = tr("Del")});
        if (node)
            items.push_back({.text = tr("Add Comment Around"), .onTriggered = [this, at] { addComment(at); }});
    }
    glw::UiHost* host = root() ? root()->host() : nullptr;
    const bool pasteable = host && core::GraphEdits::isClip(host->clipboardText());
    items.push_back({.text = tr("Paste"), .onTriggered = [this, at] {
                         mouse_ = fromCanvas(at);
                         mouseIn_ = true;
                         paste();
                     }, .enabled = pasteable});
    items.push_back(glw::MenuItem::separatorItem());
    items.push_back({.text = tr("Frame"), .onTriggered = [this] { frameSelection(); }, .shortcut = tr("F")});
    giveIcons(items);
    glw::showContextMenu(*this, fromCanvas(at), std::move(items));
}

bool GraphView::canCollapseSelection() const {
    if (selectedNodes_.size() < 2)
        return false;
    fg::Script trial = doc_.script();
    return fg::collapseToFunction(trial, doc_.registry(), graph_, {selectedNodes_.begin(), selectedNodes_.end()}).ok;
}

void GraphView::collapseSelection() {
    const std::set<fg::NodeId> nodes = selectedNodes_;
    if (nodes.size() < 2) {
        tell(tr("Select at least two nodes to collapse them into a function"));
        return;
    }
    // what cannot be collapsed is said before the name is asked
    fg::Script trial = doc_.script();
    const fg::CollapseResult probe = fg::collapseToFunction(trial, doc_.registry(), graph_, {nodes.begin(), nodes.end()});
    if (!probe.ok) {
        glw::showMessage(*this, tr("Collapse to Function"), tr("These nodes cannot be collapsed into a function: {why}").arg("why", probe.error),
                         {tr("Close")});
        return;
    }
    askText(tr("Name of the New Function"), doc_.script().uniqueGraphName("NewFunction"), [this, nodes](std::string name) {
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        while (!name.empty() && name.front() == ' ')
            name.erase(name.begin());
        if (name.empty())
            return;
        const fg::CollapseResult result = edits_.collapse(nodes, name, words(tr("Collapse to function")));
        if (!result.ok) {
            tell(glw::LocalizedText(result.error));
            return;
        }
        for (const std::string& w : result.warnings)
            tell(tr("Collapse to Function: {warning}").arg("warning", w));
        if (result.warnings.empty())
            tell(tr("Collapsed {count} node(s) into {function}").arg("count", nodes.size()).arg("function", result.function));
        select({result.callNode});
    });
}

void GraphView::askText(glw::LocalizedText title, std::string text, std::function<void(std::string)> done) {
    auto typed = std::make_shared<std::string>(text);
    auto field = std::make_unique<glw::LineEdit>(glw::LineEdit::Props{.text = text, .onChanged = [typed](const std::string& t) { *typed = t; }});
    field->setMinimumSize({320, 0});
    glw::showDialog(*this, {.title = std::move(title),
                            .content = std::move(field),
                            .buttons = {tr("OK"), tr("Cancel")},
                            .onClosed = [typed, done = std::move(done)](std::optional<std::size_t> button) {
                                if (button == 0u)
                                    done(*typed);
                            }});
}

// What a box of a value of `type` may have typed in it on the way to one (an empty one, "-" before the digits): the
// digits of an int, of a byte (at most 255), a float's (a point, an exponent); any text for the others.
static std::function<bool(std::string_view)> typedAccepts(const fg::TypeDesc* type) {
    const auto digits = [](std::string_view t) { return std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    switch (type ? type->kind : fg::TypeKind::Any) {
    case fg::TypeKind::Int:
        return [digits](std::string_view t) {
            if (!t.empty() && (t.front() == '-' || t.front() == '+'))
                t.remove_prefix(1);
            return t.size() <= 19 && digits(t);
        };
    case fg::TypeKind::Byte:
        return [digits](std::string_view t) { return t.size() <= 3 && digits(t) && (t.empty() || std::stoi(std::string(t)) <= 255); };
    case fg::TypeKind::Float:
        return [digits](std::string_view t) {
            if (!t.empty() && (t.front() == '-' || t.front() == '+'))
                t.remove_prefix(1);
            // digits, a point and digits, then an exponent: e, a sign, digits
            const std::size_t e = t.find_first_of("eE");
            std::string_view mantissa = t.substr(0, e), exponent = e == std::string_view::npos ? std::string_view() : t.substr(e + 1);
            if (e != std::string_view::npos && mantissa.empty())
                return false;
            const std::size_t point = mantissa.find('.');
            if (point != std::string_view::npos && (!digits(mantissa.substr(0, point)) || !digits(mantissa.substr(point + 1))))
                return false;
            if (point == std::string_view::npos && !digits(mantissa))
                return false;
            if (!exponent.empty() && (exponent.front() == '-' || exponent.front() == '+'))
                exponent.remove_prefix(1);
            return t.size() <= 40 && digits(exponent);
        };
    default:
        return {};
    }
}

// ---- values ----

void GraphView::editValue(NodeView& node, const NodeView::ValueHit& hit) {
    switch (hit.kind) {
    case NodeView::ValueHit::Kind::Check:
        edits_.setPinValue(node.id(), hit.pin, fg::Value(hit.text.empty()), words(tr("Edit value")));
        return;
    case NodeView::ValueHit::Kind::Choice: {
        if (!hit.type)
            return;
        // the enum's values, the one it has checked
        std::vector<glw::MenuItem> items;
        const fg::NodeId id = node.id();
        const std::string pin = hit.pin;
        for (const fg::EnumValueDesc& e : hit.type->enumValues) {
            const fg::Value value(e.value);
            items.push_back({.text = glw::LocalizedText(enumValueText(hit.type, e.value)),
                             .onTriggered = [this, id, pin, value] { edits_.setPinValue(id, pin, value, words(tr("Edit value"))); },
                             .checked = hit.value.toInt() == e.value});
        }
        const glw::Point a = fromCanvas(itemPosition(node) + glw::Point{hit.rect.x, hit.rect.y});
        const glw::Point b = fromCanvas(itemPosition(node) + glw::Point{hit.rect.right(), hit.rect.bottom()});
        glw::showMenu(*this, {a.x, a.y, b.x - a.x, b.y - a.y}, std::move(items));
        return;
    }
    case NodeView::ValueHit::Kind::Line: {
        finishValue(true);          // another one being typed: taken first
        editNode_ = node.id();
        editPin_ = hit.pin;
        editType_ = hit.type;
        // the whole value (the box shows a long text cut)
        const fg::CompiledNode& cn = node.compiled();
        const int i = cn.findPinIndex(hit.pin, fg::PinDir::In);
        editText_ = i >= 0 ? defaultText(cn.pins[std::size_t(i)]) : hit.text;
        // as wide as the box, its text where the box's is; the box (and the node) as wide as what is typed, as it is typed
        auto editor = std::make_unique<ValueEditor>(editText_, [this](bool take) { finishValue(take); }, [this](const std::string& typed) {
            NodeView* edited = nodeView(editNode_);
            const glw::UiContext* ui = context();
            if (!edited || !ui || !valueEditor_)
                return;
            edited->setValuePreview(editPin_, typed);
            const float w = NodeView::valueBoxWidth(*ui, typed, false), h = valueEditor_->minimumSize().height;
            // (its minimum never past its maximum, on the way)
            valueEditor_->setMaximumSize({glw::unbounded, glw::unbounded});
            valueEditor_->setMinimumSize({w, h});
            valueEditor_->setMaximumSize({w, h});
        });
        editor->setAccepts(typedAccepts(editType_));
        if (const glw::UiContext* ui = context()) {
            glw::LineEditStyle style = ui->theme.lineEdit;
            style.padding = {6, 0, 6, 0};
            style.width = hit.rect.width;
            editor->setStyle(style);
        }
        editor->setMinimumSize({hit.rect.width, hit.rect.height});
        editor->setMaximumSize({hit.rect.width, hit.rect.height});
        valueEditor_ = &addItem(std::move(editor), itemPosition(node) + glw::Point{hit.rect.x, hit.rect.y});
        raiseItem(*valueEditor_);
        valueEditor_->focus();
        return;
    }
    }
}

void GraphView::finishValue(bool take) {
    glw::Widget* editor = std::exchange(valueEditor_, nullptr);
    if (!editor)
        return;
    const std::string typed = static_cast<glw::LineEdit*>(editor)->text();
    // it goes after the event (it may be the one handling it)
    if (glw::UiRoot* r = root())
        r->defer([this, editor] { removeItem(*editor); });
    else
        removeItem(*editor);
    if (NodeView* edited = nodeView(editNode_))
        edited->setValuePreview({}, std::nullopt);
    if (take && typed != editText_)
        edits_.setPinValue(editNode_, editPin_, parseValue(editType_, typed), words(tr("Edit value")));
    editNode_ = 0;
}

} // namespace fged::gl
