// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Graph/GraphView.hpp"

#include "Graph/CommentView.hpp"
#include "Graph/GraphStyle.hpp"
#include "Graph/NodeView.hpp"

#include <FlowGraph/Vm.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Widgets/Bevel.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <chrono>
#include <unordered_set>

namespace fged::gl {

using glw::tr;

namespace {
constexpr float dragThreshold = 4;
} // namespace

GraphView::GraphView(core::Document& doc, std::string graph) : doc_(doc), graph_(graph), edits_(doc, std::move(graph)) {
    setFocusable(true);
    setOnContextRequested([this](glw::Point at) { contextMenu(at); });
    compiledConnection_ = doc_.compiled.connect([this] { refresh(); });
    // an edit of places (a node moved) is not compiled: the places follow it
    changedConnection_ = doc_.changed.connect([this](bool structural) {
        if (!structural)
            refresh();
    });
    refresh();
}

GraphView::~GraphView() {
    doc_.compiled.disconnect(compiledConnection_);
    doc_.changed.disconnect(changedConnection_);
}

void GraphView::setGraphName(std::string name) {
    graph_ = name;
    edits_.setGraph(std::move(name));
    refresh();
}

void GraphView::setDebugNode(fg::NodeId node) {
    if (node == debugNode_)
        return;
    const fg::NodeId before = std::exchange(debugNode_, node);
    if (NodeView* v = nodeView(before))
        v->invalidatePaint();
    if (NodeView* v = nodeView(node))
        v->invalidatePaint();
}

bool GraphView::isBreakpoint(fg::NodeId node) const {
    return doc_.debugger().hasBreakpoint(graph_, node);
}

NodeView* GraphView::nodeView(fg::NodeId id) const {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second;
}

CommentView* GraphView::commentView(int id) const {
    for (CommentView* c : comments_) {
        if (c->box().id == id)
            return c;
    }
    return nullptr;
}

std::string GraphView::words(const glw::LocalizedText& text) const {
    const glw::UiContext* ui = context();
    return ui ? ui->translator.resolve(text) : text.source;
}

void GraphView::tell(glw::LocalizedText text) const {
    if (onMessage_)
        onMessage_(std::move(text));
}

void GraphView::onRootChanged() {
    Canvas::onRootChanged();
    // the first time it shows with its nodes: all of them in view
    if (!framed_ && root() && !nodes_.empty()) {
        framed_ = true;
        frameItems(48, false);
    }
}

void GraphView::refresh() {
    const fg::Graph* graph = doc_.graph(graph_);
    const fg::CompiledGraph* compiled = doc_.compiledGraph(graph_);
    if (graph)
        kind_ = graph->kind;

    // the nodes: as the compiler made them, where the script places them
    std::unordered_set<fg::NodeId> seen;
    if (graph && compiled) {
        for (const fg::CompiledNode& cn : compiled->nodes) {
            const fg::Node* node = graph->findNode(cn.id);
            if (!node)
                continue;
            seen.insert(cn.id);
            NodeView* view = nodeView(cn.id);
            if (!view) {
                view = &addItem(std::make_unique<NodeView>(*this, cn.id), {float(node->x), float(node->y)});
                nodes_[cn.id] = view;
            } else if (drag_ != Drag::Move) {
                setItemPosition(*view, {float(node->x), float(node->y)});
            }
            bool error = false, warning = false;
            std::string text;
            for (const fg::Diagnostic& d : doc_.diagnosticsFor(graph_, cn.id)) {
                error = error || d.severity == fg::Diagnostic::Severity::Error;
                warning = warning || d.severity == fg::Diagnostic::Severity::Warning;
                text += (text.empty() ? "" : "\n") + d.message;
            }
            view->setNode(cn, error, warning, std::move(text));
        }
    }
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        std::erase_if(moving_, [&](const auto& m) { return m.first == it->second; });
        removeItem(*it->second);
        it = nodes_.erase(it);
    }

    // the comments: made again, behind the nodes (none is dragged: an edit comes after a drag)
    if (drag_ != Drag::Move && drag_ != Drag::Resize) {
        for (CommentView* c : comments_)
            removeItem(*c);
        comments_.clear();
        if (graph) {
            for (auto it = graph->comments.rbegin(); it != graph->comments.rend(); ++it) {
                CommentView& view = addItem(std::make_unique<CommentView>(*this, *it), {float(it->x), float(it->y)});
                lowerItem(view);
                comments_.push_back(&view);
            }
        }
    }

    // what is selected and gone: no longer selected
    const std::size_t before = selectedNodes_.size() + selectedComments_.size() + (selectedLink_ ? 1 : 0);
    std::erase_if(selectedNodes_, [&](fg::NodeId id) { return !nodes_.contains(id); });
    std::erase_if(selectedComments_, [&](int id) { return !commentView(id); });
    if (selectedLink_ && (!graph || std::find(graph->connections.begin(), graph->connections.end(), *selectedLink_) == graph->connections.end()))
        selectedLink_.reset();
    showSelection();
    if (selectedNodes_.size() + selectedComments_.size() + (selectedLink_ ? 1 : 0) != before)
        selectionChanged();

    if (!framed_ && root() && !nodes_.empty()) {
        framed_ = true;
        frameItems(48, false);
    }
    invalidatePaint();
}

// ---- selection ----

void GraphView::showSelection() {
    for (const auto& [id, view] : nodes_)
        view->setSelected(selectedNodes_.contains(id));
    for (CommentView* c : comments_)
        c->setSelected(selectedComments_.contains(c->box().id));
    invalidatePaint();
}

void GraphView::selectionChanged() {
    if (onSelectionChanged_)
        onSelectionChanged_();
}

void GraphView::select(const std::set<fg::NodeId>& nodes, const std::set<int>& comments) {
    selectedNodes_.clear();
    for (fg::NodeId id : nodes) {
        if (nodes_.contains(id))
            selectedNodes_.insert(id);
    }
    selectedComments_ = comments;
    selectedLink_.reset();
    showSelection();
    selectionChanged();
}

void GraphView::selectAll() {
    std::set<fg::NodeId> all;
    for (const auto& [id, view] : nodes_)
        all.insert(id);
    std::set<int> comments;
    for (CommentView* c : comments_)
        comments.insert(c->box().id);
    select(all, comments);
}

void GraphView::clearSelection() {
    if (hasSelection())
        select({});
}

void GraphView::frameSelection() {
    glw::Rect area;
    bool any = false;
    for (fg::NodeId id : selectedNodes_) {
        if (NodeView* v = nodeView(id)) {
            area = any ? area.united(itemRect(*v)) : itemRect(*v);
            any = true;
        }
    }
    for (int id : selectedComments_) {
        if (CommentView* c = commentView(id)) {
            area = any ? area.united(itemRect(*c)) : itemRect(*c);
            any = true;
        }
    }
    if (any)
        frame(area, 64);
    else
        frameItems(48);
}

void GraphView::frameAll() {
    frameItems(48);
}

// ---- commands ----

void GraphView::deleteSelection() {
    std::set<fg::NodeId> nodes;
    bool fixed = false;
    for (fg::NodeId id : selectedNodes_) {
        if (const NodeView* v = nodeView(id); v && !core::GraphEdits::removable(v->compiled()))
            fixed = true;
        else
            nodes.insert(id);
    }
    if (fixed)
        tell(tr("The events of the application and the entry of a function cannot be deleted"));
    std::vector<fg::Connection> links;
    if (selectedLink_)
        links.push_back(*selectedLink_);
    edits_.remove(nodes, links, selectedComments_, words(tr("Delete")));
}

void GraphView::copySelection() {
    const std::string text = edits_.copy(selectedNodes_);
    if (text.empty())
        return;
    if (glw::UiHost* host = root() ? root()->host() : nullptr)
        host->setClipboardText(text);
}

void GraphView::cutSelection() {
    copySelection();
    deleteSelection();
}

void GraphView::paste() {
    glw::UiHost* host = root() ? root()->host() : nullptr;
    if (!host)
        return;
    const std::string text = host->clipboardText();
    if (!core::GraphEdits::isClip(text)) {
        tell(tr("The clipboard has no nodes"));
        return;
    }
    const glw::Rect shown = visibleArea();
    const glw::Point at = mouseIn_ ? toCanvas(mouse_) : glw::Point{shown.x + shown.width / 3, shown.y + shown.height / 3};
    const std::vector<fg::NodeId> made = edits_.paste(text, at.x, at.y, false, words(tr("Paste")));
    select({made.begin(), made.end()});
}

void GraphView::duplicateSelection() {
    const std::string text = edits_.copy(selectedNodes_);
    if (text.empty())
        return;
    const std::vector<fg::NodeId> made = edits_.paste(text, 40, 40, true, words(tr("Duplicate")));
    select({made.begin(), made.end()});
}

// ---- the links ----

GraphView::Bezier GraphView::curve(glw::Point a, glw::Point b) {
    // as the Qt editor's: leaving its output to the right, coming into its input from the left
    float dx = b.x - a.x;
    if (dx < 80)
        dx = 160 - dx;
    return {a, {a.x + dx / 2, a.y}, {b.x - dx / 2, b.y}, b};
}

std::optional<GraphView::Bezier> GraphView::linkCurve(const fg::Connection& c) const {
    const std::optional<glw::Point> a = pinPoint(c.fromNode, c.fromPin, fg::PinDir::Out);
    const std::optional<glw::Point> b = pinPoint(c.toNode, c.toPin, fg::PinDir::In);
    if (!a || !b)
        return std::nullopt;
    return curve(*a, *b);
}

std::optional<fg::Connection> GraphView::linkAt(glw::Point point) const {
    const fg::Graph* graph = doc_.graph(graph_);
    if (!graph)
        return std::nullopt;
    const float reach = 5 / std::max(zoom(), 0.1f);
    for (auto it = graph->connections.rbegin(); it != graph->connections.rend(); ++it) {
        const std::optional<Bezier> bz = linkCurve(*it);
        if (!bz)
            continue;
        // the curve as short lines, and the mouse near one of them
        glw::Point last = bz->a;
        for (int i = 1; i <= 32; ++i) {
            const float t = i / 32.0f, u = 1 - t;
            const glw::Point p{u * u * u * bz->a.x + 3 * u * u * t * bz->c1.x + 3 * u * t * t * bz->c2.x + t * t * t * bz->b.x,
                               u * u * u * bz->a.y + 3 * u * u * t * bz->c1.y + 3 * u * t * t * bz->c2.y + t * t * t * bz->b.y};
            const float dx = p.x - last.x, dy = p.y - last.y;
            const float len2 = dx * dx + dy * dy;
            const float k = len2 > 0 ? std::clamp(((point.x - last.x) * dx + (point.y - last.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
            const float ex = last.x + k * dx - point.x, ey = last.y + k * dy - point.y;
            if (ex * ex + ey * ey <= reach * reach)
                return *it;
            last = p;
        }
    }
    return std::nullopt;
}

std::optional<glw::Point> GraphView::pinPoint(fg::NodeId node, const std::string& pin, fg::PinDir dir) const {
    const NodeView* view = nodeView(node);
    if (!view)
        return std::nullopt;
    const std::optional<glw::Point> local = view->pinCenter(pin, dir);
    if (!local)
        return std::nullopt;
    return itemPosition(*view) + *local;
}

// ---- the mouse ----

void GraphView::nodeMouse(NodeView& node, glw::MouseEvent& event) {
    const glw::Point at = itemPosition(node) + event.position;
    switch (event.type) {
    case glw::MouseEventType::Press: {
        if (event.button != glw::platform::MouseButton::Left)
            return;
        event.handled = true;
        focus();
        // the box of a value: changed there
        if (const std::optional<NodeView::ValueHit> value = node.valueAt(event.position);
            value && !glw::platform::hasAny(event.modifiers, glw::platform::KeyModifiers::Alt)) {
            if (!selectedNodes_.contains(node.id()))
                select({node.id()});
            editValue(node, *value);
            return;
        }
        if (const std::optional<NodeView::PinHit> pin = node.pinAt(event.position)) {
            const core::PinEnd end = core::PinEnd::of(node.compiled(), pin->name, pin->dir);
            if (glw::platform::hasAny(event.modifiers, glw::platform::KeyModifiers::Alt)) {
                edits_.breakLinks(end, words(tr("Break links")));
                return;
            }
            drag_ = Drag::Link;
            linkFrom_ = end;
            linkTo_ = at;
            invalidatePaint();
            return;
        }
        // clicked twice: what it opens (a call of a function: its graph)
        if (event.clicks == 2 && onNodeActivated_) {
            select({node.id()});
            onNodeActivated_(node.id());
            return;
        }
        // a click selects it (Ctrl, Shift: adds it or takes it out); pressed, the selection moves with it
        const bool add = glw::platform::hasAny(event.modifiers, glw::platform::shortcutModifier | glw::platform::KeyModifiers::Shift);
        if (add) {
            if (!selectedNodes_.erase(node.id()))
                selectedNodes_.insert(node.id());
            selectedLink_.reset();
            showSelection();
            selectionChanged();
        } else if (!selectedNodes_.contains(node.id())) {
            select({node.id()});
        }
        if (!selectedNodes_.contains(node.id()))
            return;
        // one of several selected, clicked (not dragged): it alone, when let go
        pressedAlone_ = add ? 0 : node.id();
        std::vector<NodeView*> moving;
        for (fg::NodeId id : selectedNodes_) {
            if (NodeView* v = nodeView(id))
                moving.push_back(v);
        }
        std::vector<CommentView*> comments;
        for (int id : selectedComments_) {
            if (CommentView* c = commentView(id))
                comments.push_back(c);
        }
        raiseItem(node);
        beginMove(at, std::move(moving), std::move(comments));
        return;
    }
    case glw::MouseEventType::Move:
        if (drag_ == Drag::Move) {
            moveTo(at, glw::platform::hasAny(event.modifiers, glw::platform::KeyModifiers::Alt));
            event.handled = true;
        } else if (drag_ == Drag::Link) {
            linkTo_ = at;
            invalidatePaint();
            event.handled = true;
        }
        return;
    case glw::MouseEventType::Release:
        if (event.button != glw::platform::MouseButton::Left || drag_ == Drag::None)
            return;
        event.handled = true;
        if (drag_ == Drag::Link) {
            linkTo_ = at;
            const core::PinEnd from = linkFrom_;
            drag_ = Drag::None;
            invalidatePaint();
            // let go on a pin of a node: linked, or why not
            for (const auto& [id, view] : nodes_) {
                const glw::Rect r = itemRect(*view);
                if (!r.inflated(4).contains(at))
                    continue;
                const std::optional<NodeView::PinHit> pin = view->pinAt(at - itemPosition(*view));
                if (!pin)
                    continue;
                const core::PinEnd to = core::PinEnd::of(view->compiled(), pin->name, pin->dir);
                if (to.node == from.node && to.pin == from.pin && to.dir == from.dir)
                    return;
                core::LinkProblem why = core::LinkProblem::None;
                if (!edits_.link(from, to, words(tr("Connect pins")), &why)) {
                    switch (why) {
                    case core::LinkProblem::SameDirection: tell(tr("Connect an output pin to an input pin")); break;
                    case core::LinkProblem::SameNode: tell(tr("A node cannot be connected to itself")); break;
                    case core::LinkProblem::KindsDiffer: tell(tr("Execution pins only connect to execution pins")); break;
                    case core::LinkProblem::TypesDiffer: {
                        const fg::TypeDesc* out = from.dir == fg::PinDir::Out ? from.type : to.type;
                        const fg::TypeDesc* in = from.dir == fg::PinDir::Out ? to.type : from.type;
                        tell(tr("Cannot connect {from} to {to}").arg("from", out ? out->name : "?").arg("to", in ? in->name : "?"));
                        break;
                    }
                    case core::LinkProblem::DataCycle: tell(tr("This link would make the value depend on itself (the data links would form a cycle)")); break;
                    case core::LinkProblem::EndlessLoop:
                        tell(tr("This link would make an endless loop: add a Branch (a way out) or a Delay to the loop first"));
                        break;
                    case core::LinkProblem::None: break;
                    }
                }
                return;
            }
            // let go on the empty canvas: the nodes that fit, to add there linked; on a node, nothing
            for (const auto& [id, view] : nodes_) {
                if (itemRect(*view).contains(at))
                    return;
            }
            openPalette(at, &from);
            return;
        }
        if (!dragMoved_ && pressedAlone_ && selectedNodes_.size() + selectedComments_.size() > 1) {
            drag_ = Drag::None;
            moving_.clear();
            movingComments_.clear();
            select({pressedAlone_});
            return;
        }
        endDrag();
        return;
    default: return;
    }
}

void GraphView::commentMouse(CommentView& comment, glw::MouseEvent& event, int part) {
    const glw::Point at = itemPosition(comment) + event.position;
    switch (event.type) {
    case glw::MouseEventType::Press: {
        event.handled = true;
        focus();
        const int id = comment.box().id;
        if (event.clicks == 2 && part == 0) {
            // its header clicked twice: its text
            askText(tr("Comment"), comment.box().text, [this, id](std::string text) { edits_.setCommentText(id, text, words(tr("Edit comment"))); });
            return;
        }
        const bool add = glw::platform::hasAny(event.modifiers, glw::platform::shortcutModifier | glw::platform::KeyModifiers::Shift);
        if (add) {
            if (!selectedComments_.erase(id))
                selectedComments_.insert(id);
            showSelection();
            selectionChanged();
        } else if (!selectedComments_.contains(id)) {
            select({}, {id});
        }
        if (part == 1) {
            drag_ = Drag::Resize;
            dragFrom_ = at;
            movingComments_ = {{&comment, itemRect(comment)}};
            return;
        }
        // its header: with the nodes in it
        const glw::Rect area = itemRect(comment);
        std::vector<NodeView*> inside;
        for (const auto& [nid, view] : nodes_) {
            const glw::Rect n = itemRect(*view);
            if (n.x >= area.x && n.y >= area.y && n.right() <= area.right() && n.bottom() <= area.bottom())
                inside.push_back(view);
        }
        std::vector<CommentView*> comments{&comment};
        for (int other : selectedComments_) {
            if (CommentView* c = commentView(other); c && c != &comment)
                comments.push_back(c);
        }
        for (fg::NodeId nid : selectedNodes_) {
            if (NodeView* v = nodeView(nid); v && std::find(inside.begin(), inside.end(), v) == inside.end())
                inside.push_back(v);
        }
        beginMove(at, std::move(inside), std::move(comments));
        return;
    }
    case glw::MouseEventType::Move:
        if (drag_ == Drag::Move) {
            moveTo(at, glw::platform::hasAny(event.modifiers, glw::platform::KeyModifiers::Alt));
            event.handled = true;
        } else if (drag_ == Drag::Resize && !movingComments_.empty()) {
            const glw::Rect from = movingComments_.front().second;
            float w = std::max(160.0f, from.width + at.x - dragFrom_.x);
            float h = std::max(80.0f, from.height + at.y - dragFrom_.y);
            if (snap_ && !glw::platform::hasAny(event.modifiers, glw::platform::KeyModifiers::Alt)) {
                w = std::max(160.0f, std::round((from.x + w) / snapStep_) * snapStep_ - from.x);
                h = std::max(80.0f, std::round((from.y + h) / snapStep_) * snapStep_ - from.y);
            }
            movingComments_.front().first->setSize(w, h);
            event.handled = true;
        }
        return;
    case glw::MouseEventType::Release:
        if (event.button == glw::platform::MouseButton::Left && drag_ != Drag::None) {
            event.handled = true;
            endDrag();
        }
        return;
    default: return;
    }
}

void GraphView::beginMove(glw::Point at, std::vector<NodeView*> nodes, std::vector<CommentView*> comments) {
    drag_ = Drag::Move;
    dragFrom_ = at;
    dragMoved_ = false;
    moving_.clear();
    for (NodeView* v : nodes)
        moving_.emplace_back(v, itemPosition(*v));
    movingComments_.clear();
    for (CommentView* c : comments)
        movingComments_.emplace_back(c, itemRect(*c));
}

void GraphView::moveTo(glw::Point at, bool free) {
    glw::Point by = at - dragFrom_;
    if (!dragMoved_ && std::abs(by.x) < dragThreshold && std::abs(by.y) < dragThreshold)
        return;     // a click, not a drag yet
    dragMoved_ = true;
    const bool snapping = snap_ && !free;
    if (snapping && moving_.size() == 1 && movingComments_.empty()) {
        // alone: on the grid, or level with a pin it is linked to
        const glw::Point to = snappedAlone(*moving_.front().first, moving_.front().second + by);
        setItemPosition(*moving_.front().first, to);
        invalidatePaint();
        return;
    }
    if (snapping)       // with others: by whole steps, so that they keep where they are to each other
        by = {std::round(by.x / snapStep_) * snapStep_, std::round(by.y / snapStep_) * snapStep_};
    for (const auto& [view, from] : moving_)
        setItemPosition(*view, from + by);
    for (const auto& [comment, from] : movingComments_)
        setItemPosition(*comment, glw::Point{from.x, from.y} + by);
    invalidatePaint();
}

glw::Point GraphView::snappedAlone(const NodeView& node, glw::Point proposed) const {
    glw::Point at{std::round(proposed.x / snapStep_) * snapStep_, std::round(proposed.y / snapStep_) * snapStep_};
    const fg::Graph* g = doc_.graph(graph_);
    if (!g)
        return at;
    // the link that can be straight closest to where it is dragged, if it is close enough
    float best = std::max(snapStep_ * 0.75f, 8.0f);
    for (const fg::Connection& c : g->connections) {
        const bool out = c.fromNode == node.id(), in = c.toNode == node.id();
        if (out == in)
            continue;
        const NodeView* other = nodeView(out ? c.toNode : c.fromNode);
        const std::optional<glw::Point> mine = node.pinCenter(out ? c.fromPin : c.toPin, out ? fg::PinDir::Out : fg::PinDir::In);
        const std::optional<glw::Point> theirs = other ? other->pinCenter(out ? c.toPin : c.fromPin, out ? fg::PinDir::In : fg::PinDir::Out) : std::nullopt;
        if (!mine || !theirs)
            continue;
        const float y = itemPosition(*other).y + theirs->y - mine->y;
        if (std::abs(y - proposed.y) < best) {
            best = std::abs(y - proposed.y);
            at.y = y;
        }
    }
    return at;
}

void GraphView::endDrag() {
    const Drag was = std::exchange(drag_, Drag::None);
    if (was != Drag::Move && was != Drag::Resize)
        return;
    // where they are now: one step (nothing when nothing moved)
    std::vector<core::GraphEdits::NodePlace> nodes;
    for (const auto& [view, from] : moving_) {
        const glw::Point p = itemPosition(*view);
        nodes.push_back({view->id(), p.x, p.y});
    }
    std::vector<core::GraphEdits::CommentPlace> comments;
    for (const auto& [comment, from] : movingComments_) {
        const glw::Rect r = itemRect(*comment);
        comments.push_back({comment->box().id, r.x, r.y, comment->box().w, comment->box().h});
    }
    moving_.clear();
    movingComments_.clear();
    edits_.place(nodes, comments, words(was == Drag::Resize ? tr("Resize comment") : tr("Move")));
}

void GraphView::onPreviewMouse(glw::MouseEvent& event) {
    if (event.type == glw::MouseEventType::Move) {
        mouse_ = event.position;
        mouseIn_ = true;
    }
}

void GraphView::onMouse(glw::MouseEvent& event) {
    if (event.button == glw::platform::MouseButton::Left && event.type != glw::MouseEventType::Wheel) {
        const glw::Point at = toCanvas(event.position);
        switch (event.type) {
        case glw::MouseEventType::Press: {
            focus();
            event.handled = true;
            // a link: selected; clicked twice, a reroute there (links of data)
            if (const std::optional<fg::Connection> link = linkAt(at)) {
                if (event.clicks == 2) {
                    if (!edits_.insertReroute(*link, at.x - 18, at.y - 9, words(tr("Add reroute"))))
                        tell(tr("Reroute nodes are only for links of data"));
                    return;
                }
                selectedNodes_.clear();
                selectedComments_.clear();
                selectedLink_ = link;
                showSelection();
                selectionChanged();
                return;
            }
            // the empty canvas: a rectangle that selects what it touches
            const bool add = glw::platform::hasAny(event.modifiers, glw::platform::shortcutModifier | glw::platform::KeyModifiers::Shift);
            bandBefore_ = add ? selectedNodes_ : std::set<fg::NodeId>{};
            if (!add)
                clearSelection();
            drag_ = Drag::Band;
            band_ = glw::Rect{at.x, at.y, 0, 0};
            dragFrom_ = at;
            return;
        }
        case glw::MouseEventType::Move:
            if (drag_ == Drag::Band) {
                band_ = glw::Rect{std::min(dragFrom_.x, at.x), std::min(dragFrom_.y, at.y), std::abs(at.x - dragFrom_.x), std::abs(at.y - dragFrom_.y)};
                selectedNodes_ = bandBefore_;
                for (const auto& [id, view] : nodes_) {
                    if (itemRect(*view).intersects(*band_))
                        selectedNodes_.insert(id);
                }
                showSelection();
                event.handled = true;
                return;
            }
            break;
        case glw::MouseEventType::Release:
            if (drag_ == Drag::Band) {
                drag_ = Drag::None;
                band_.reset();
                selectionChanged();
                invalidatePaint();
                event.handled = true;
                return;
            }
            break;
        default: break;
        }
    }
    if (event.type == glw::MouseEventType::Move)
        mouseIn_ = true;
    Canvas::onMouse(event);
}

void GraphView::onKey(glw::KeyEvent& event) {
    // its own keys only: not those a field on it lets through (a space typed in a value's box)
    if (!event.pressed || (root() && root()->focusWidget() != this))
        return;
    using glw::platform::Key;
    const bool command = glw::platform::hasAny(event.modifiers, glw::platform::shortcutModifier | glw::platform::KeyModifiers::Alt);
    switch (event.key) {
    case Key::Delete:
    case Key::Backspace: deleteSelection(); break;
    case Key::F:
        if (command)
            return;
        frameSelection();
        break;
    case Key::Space:
    case Key::Tab:
        if (command)
            return;
        openPalette(toCanvas(mouseIn_ ? mouse_ : glw::Point{size().width / 2, size().height / 2}));
        break;
    case Key::F9: toggleBreakpoints(); break;
    case Key::Home:
        if (command)
            return;
        frameAll();
        break;
    case Key::C:
        // a comment under the mouse (around the selection, when there is one); Ctrl+C copies
        if (command)
            return;
        addComment(toCanvas(mouseIn_ ? mouse_ : glw::Point{size().width / 2, size().height / 2}));
        break;
    case Key::Escape:
        if (drag_ == Drag::Link) {
            drag_ = Drag::None;
            invalidatePaint();
        } else {
            clearSelection();
        }
        break;
    default: return;
    }
    event.handled = true;
}

// ---- painting ----

void GraphView::paintUnderItems(glw::PaintContext& context) {
    const fg::Graph* graph = doc_.graph(graph_);
    const fg::CompiledGraph* compiled = doc_.compiledGraph(graph_);
    if (!graph)
        return;
    const GraphColors colors = GraphColors::of(context.ui.theme);
    const glw::Rect shown = visibleArea();
    // every link under the nodes (paintUnderItems), the links of data under those of execution: two passes, the link being
    // dragged in the pass of its kind, last
    // where a paused run is: a glow around its node, under it (its banner is over the nodes)
    if (const NodeView* paused = nodeView(debugNode_)) {
        const glw::Rect r = itemRect(*paused);
        context.painter.drawShadow(r.inflated(6), 10, 18, {colors.debug.r, colors.debug.g, colors.debug.b, 0.85f});
    }
    for (const bool execPass : {false, true}) {
        for (const fg::Connection& c : graph->connections) {
            const std::optional<Bezier> bz = linkCurve(c);
            if (!bz)
                continue;
            // the color of its output (execution: the exec color), thicker for execution
            bool exec = false;
            glw::Color color = colors.mutedText;
            if (compiled) {
                if (const fg::CompiledNode* from = compiled->findNode(c.fromNode)) {
                    const int i = from->findPinIndex(c.fromPin, fg::PinDir::Out);
                    if (i >= 0) {
                        const fg::CompiledPin& pin = from->pins[std::size_t(i)];
                        exec = pin.kind == fg::PinKind::Exec;
                        color = pinColor(registry(), pin, colors);
                    }
                }
            }
            if (exec != execPass)
                continue;
            // what does not show is not drawn
            const float left = std::min({bz->a.x, bz->b.x, bz->c1.x, bz->c2.x}), right = std::max({bz->a.x, bz->b.x, bz->c1.x, bz->c2.x});
            const float top = std::min(bz->a.y, bz->b.y), bottom = std::max(bz->a.y, bz->b.y);
            if (!glw::Rect{left, top, right - left, bottom - top}.inflated(8).intersects(shown))
                continue;
            const bool selected = selectedLink_ && *selectedLink_ == c;
            float width = exec ? 3.0f : 2.0f;
            if (selected) {
                color = colors.selection;
                width += 1;
            }
            const auto firing = std::find_if(fired_.begin(), fired_.end(), [&](const Firing& f) { return f.link == c; });
            if (firing != fired_.end())
                paintFiring(context, *bz, *firing, exec, color);
            else
                context.painter.drawBezier(bz->a, bz->c1, bz->c2, bz->b, width, color);
        }
        // the link being dragged, from its pin to the mouse: with those of its kind
        if (drag_ == Drag::Link && (linkFrom_.kind == fg::PinKind::Exec) == execPass) {
            if (const std::optional<glw::Point> pin = pinPoint(linkFrom_.node, linkFrom_.pin, linkFrom_.dir)) {
                glw::Color color = linkFrom_.kind == fg::PinKind::Exec ? colors.exec
                                   : linkFrom_.type && !linkFrom_.wildcard ? rgb(registry().typeColor(linkFrom_.type))
                                                                            : rgb(160, 160, 168);
                color.a *= 0.75f;
                const Bezier bz = linkFrom_.dir == fg::PinDir::Out ? curve(*pin, linkTo_) : curve(linkTo_, *pin);
                context.painter.drawBezier(bz.a, bz.c1, bz.c2, bz.b, linkFrom_.kind == fg::PinKind::Exec ? 3.0f : 2.0f, color);
            }
        }
    }
}

// ---- what a run shows ----

namespace {
constexpr auto fireTotal = std::chrono::milliseconds(1000);     // a link lit, after it was last taken
constexpr auto fireFade = std::chrono::milliseconds(250);       // its coming and going
constexpr float dotSpacing = 200;                               // dips between the dots running along it

float seconds(glw::Duration d) {
    return std::chrono::duration<float>(d).count();
}
glw::Color blend(glw::Color a, glw::Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
glw::Point bezierAt(glw::Point a, glw::Point c1, glw::Point c2, glw::Point b, float t) {
    const float u = 1 - t;
    return {u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x,
            u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y};
}
} // namespace

glw::TimePoint GraphView::now() const {
    const glw::UiContext* ui = context();
    return ui && ui->scheduler ? ui->scheduler->now() : glw::TimePoint{};
}

void GraphView::fireLink(fg::NodeId from, int execOut, fg::NodeId to) {
    const NodeView* source = nodeView(from);
    if (!source)
        return;
    const fg::CompiledNode& cn = source->compiled();
    if (execOut < 0 || std::size_t(execOut) >= cn.execOut.size())
        return;
    const std::string& pin = cn.pins[std::size_t(cn.execOut[std::size_t(execOut)])].name;
    const fg::Graph* graph = doc_.graph(graph_);
    if (!graph)
        return;
    const glw::TimePoint t = now();
    for (const fg::Connection& c : graph->connections) {
        if (c.fromNode != from || c.fromPin != pin || c.toNode != to)
            continue;
        const auto it = std::find_if(fired_.begin(), fired_.end(), [&](const Firing& f) { return f.link == c; });
        if (it == fired_.end())
            fired_.push_back({c, t, t});
        else
            it->last = t;
    }
    if (NodeView* target = nodeView(to))
        target->flash();
    animate();
}

void GraphView::animate() {
    const glw::UiContext* ui = context();
    if (!ui || !ui->scheduler || animation_.active())
        return;
    animation_.start(*ui->scheduler, std::chrono::milliseconds(16), [this] {
        const glw::TimePoint t = now();
        std::erase_if(fired_, [&](const Firing& f) { return t - f.last > fireTotal; });
        bool lit = !fired_.empty();
        for (const auto& [id, view] : nodes_)
            lit = lit || view->flashing();
        invalidatePaint();
        if (!lit)
            animation_.stop();
    }, true);
}

void GraphView::paintFiring(glw::PaintContext& context, const Bezier& curve, const Firing& firing, bool exec, glw::Color color) {
    const glw::TimePoint t = now();
    const glw::Duration since = t - firing.last;
    // coming (from its first time), going (to the end of its last)
    const float k = since > fireTotal - fireFade ? seconds(fireTotal - since) / seconds(fireFade) : seconds(t - firing.first) / seconds(fireFade);
    const glw::Color lit = blend(color, context.ui.theme.palette.accent, k);
    const float width = (exec ? 3.0f : 2.0f) + (6.0f - (exec ? 3.0f : 2.0f)) * std::clamp(k, 0.0f, 1.0f);
    glw::paint::Painter& p = context.painter;
    p.drawBezier(curve.a, curve.c1, curve.c2, curve.b, width, lit);
    // dots running along it, dotSpacing apart: where they are along its length
    constexpr int samples = 48;
    std::array<glw::Point, samples + 1> points;
    std::array<float, samples + 1> along{};
    for (int i = 0; i <= samples; ++i) {
        points[std::size_t(i)] = bezierAt(curve.a, curve.c1, curve.c2, curve.b, float(i) / samples);
        if (i > 0) {
            const glw::Point d = points[std::size_t(i)] - points[std::size_t(i - 1)];
            along[std::size_t(i)] = along[std::size_t(i - 1)] + std::sqrt(d.x * d.x + d.y * d.y);
        }
    }
    const float length = along[samples];
    if (length < 10)
        return;
    const float phase = std::fmod(seconds(t - firing.first), seconds(fireTotal)) / seconds(fireTotal);
    // larger than the line is wide, with a light core: seen on it
    const float radius = 0.5f + 5.5f * std::clamp(k, 0.0f, 1.0f);
    const glw::Color core = blend(lit, glw::Color{1, 1, 1, lit.a}, 0.6f);
    std::size_t at = 1;
    for (float d = phase * dotSpacing; d <= length; d += dotSpacing) {
        while (at < samples && along[at] < d)
            ++at;
        const float span = along[at] - along[at - 1];
        const float f = span > 0 ? (d - along[at - 1]) / span : 0;
        const glw::Point a = points[at - 1], b = points[at];
        const glw::Point dot{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
        p.fillCircle(dot, radius, lit);
        p.fillCircle(dot, radius * 0.55f, core);
    }
}

void GraphView::showNode(fg::NodeId id) {
    const NodeView* view = nodeView(id);
    if (!view)
        return;
    const glw::Rect r = itemRect(*view);
    if (size().width <= 0 || size().height <= 0 || !visibleArea().inflated(-40).contains(r.center()))
        centerOn(r.center(), true);         // eased there; a view not laid out yet: when it is
}

void GraphView::paintOverItems(glw::PaintContext& context) {
    const GraphColors colors = GraphColors::of(context.ui.theme);
    glw::paint::Painter& p = context.painter;
    // the comments of nodes, above them
    for (const auto& [id, view] : nodes_) {
        const std::string& comment = view->compiled().comment;
        if (comment.empty())
            continue;
        const glw::text::TextLayout text(comment, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, context.ui.theme.fontSize});
        const glw::Point at = itemPosition(*view);
        const glw::Size s = text.size();
        const glw::Rect bubble{at.x, at.y - s.height - 10, s.width + 12, s.height + 4};
        if (!bubble.intersects(visibleArea()))
            continue;
        p.fillRoundedRect(bubble, 4, colors.bubble);
        context.drawText(text, {bubble.x + 6, bubble.y + 2}, colors.bubbleText);
    }
    // where a paused run is: a banner over its node (over its comment, when it has one), pointing at it - as big on the
    // screen whatever the zoom (the canvas's dips divided by it), to be seen from afar
    if (const NodeView* paused = nodeView(debugNode_)) {
        const float k = 1 / std::max(zoom(), 0.25f);
        const glw::Point at = itemPosition(*paused);
        const glw::Rect node = itemRect(*paused);
        float top = at.y;
        if (const std::string& comment = paused->compiled().comment; !comment.empty()) {
            const glw::text::TextLayout text(comment, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, context.ui.theme.fontSize});
            top -= text.size().height + 10;
        }
        const glw::text::TextLayout label(context.ui.translator.resolve(tr("Paused here")),
                                          glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, (context.ui.theme.fontSize + 1) * k});
        const glw::Size s = label.size();
        const float w = s.width + 34 * k, h = s.height + 10 * k;
        const glw::Rect banner{node.x + (node.width - w) / 2, top - h - 12 * k, w, h};
        // its arrow down to the node, under it; then the banner, a button of the style in use (its corners, its relief,
        // its gradient and shadow), in the debugger's color, as large on the screen however far it is zoomed
        const float tip = banner.x + banner.width / 2;
        const glw::Point arrow[] = {{tip - 7 * k, banner.bottom() - 2 * k}, {tip + 7 * k, banner.bottom() - 2 * k}, {tip, banner.bottom() + 8 * k}};
        p.fillConvexPolygon(arrow, colors.debug);
        const glw::ButtonStyle& button = context.ui.theme.button;
        glw::Bevel bevel = button.bevel;
        bevel.relief *= k;
        bevel.roundness *= k;
        bevel.edge *= k;
        const float radius = std::min(button.cornerRadius * k, h / 2);
        glw::paintRaised(p, banner, radius, colors.debug, glw::lit(colors.debug, -0.3f), bevel);
        // a pause mark, then the words
        const glw::Color ink{0.08f, 0.06f, 0.02f, 1};
        const float mark = banner.x + 12 * k, middle = banner.y + h / 2;
        p.fillRoundedRect({mark, middle - 5 * k, 3 * k, 10 * k}, k, ink);
        p.fillRoundedRect({mark + 5 * k, middle - 5 * k, 3 * k, 10 * k}, k, ink);
        context.drawText(label, {banner.x + 24 * k, banner.y + (h - s.height) / 2 + label.opticalOffset()}, ink);
    }
    // the rectangle that selects
    if (band_) {
        const glw::Color accent = context.ui.theme.palette.accent;
        p.fillRect(*band_, {accent.r, accent.g, accent.b, 0.12f});
        p.strokeRoundedRect(*band_, 0, 1 / std::max(zoom(), 0.1f), accent);
    }
}

} // namespace fged::gl
