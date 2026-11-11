// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Graph/GraphView.hpp"

#include "Graph/GraphStyle.hpp"
#include "Graph/NodeView.hpp"

#include <FlowGraph/Vm.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace fged::gl {

using glw::tr;

namespace {
constexpr float commentHeader = 26;     // a comment's header: what moves it
constexpr float commentGrip = 16;       // its corner: what resizes it
constexpr float dragThreshold = 4;
} // namespace

// A comment of the graph: a colored frame behind a group of nodes, its text in its header. Its header moves it (with the
// nodes in it), its corner resizes it; the rest of it is the canvas's (a click there selects with a rectangle).
class CommentView final : public glw::Widget {
public:
    CommentView(GraphView& graph, const fg::CommentBox& box) : graph_(graph), box_(box) {}

    const fg::CommentBox& box() const { return box_; }
    void setSize(float w, float h) {
        box_.w = w;
        box_.h = h;
        invalidateLayout();
    }
    void setSelected(bool selected) {
        if (selected != selected_) {
            selected_ = selected;
            invalidatePaint();
        }
    }

protected:
    glw::Size measureContent(glw::Size) override { return {float(box_.w), float(box_.h)}; }

    void paint(glw::PaintContext& context) override {
        glw::paint::Painter& p = context.painter;
        const glw::Rect r{0, 0, size().width, size().height};
        const float radius = std::min(context.ui.theme.node.cornerRadius, commentHeader / 2);     // as the theme's nodes are rounded
        p.fillRoundedRect(r, radius, rgb(box_.color, 0.22f));
        p.pushClip({0, 0, r.width, commentHeader});
        p.fillRoundedRect({0, 0, r.width, commentHeader + radius}, radius, rgb(box_.color, 0.75f));
        p.popClip();
        p.strokeRoundedRect(r, radius, selected_ ? 2.0f : 1.0f, selected_ ? context.ui.theme.node.selection : rgb(box_.color, 0.9f));
        if (!text_ || shown_ != box_.text) {
            shown_ = box_.text;
            text_.emplace(box_.text, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, context.ui.theme.fontSize + 1});
        }
        p.pushClip({6, 0, std::max(0.0f, r.width - 12), commentHeader});
        context.drawText(*text_, {8, (commentHeader - text_->size().height) / 2}, {1, 1, 1, 1});
        p.popClip();
        // the grip that resizes it, at its bottom right corner
        for (int i = 1; i <= 3; ++i)
            p.drawLine({r.width - 4 - 4.0f * i, r.height - 4}, {r.width - 4, r.height - 4 - 4.0f * i}, 1, {1, 1, 1, 0.35f});
    }

    void onMouse(glw::MouseEvent& event) override {
        const bool corner = glw::Rect{size().width - commentGrip, size().height - commentGrip, commentGrip, commentGrip}.contains(event.position);
        const bool header = event.position.y < commentHeader;
        if (event.type == glw::MouseEventType::Press && event.button == glw::platform::MouseButton::Left && !corner && !header)
            return;     // the canvas's
        if (event.type == glw::MouseEventType::Press && event.button != glw::platform::MouseButton::Left)
            return;
        if (event.type == glw::MouseEventType::Wheel)
            return;
        graph_.commentMouse(*this, event, corner ? 1 : 0);
    }

    std::optional<glw::platform::Cursor> cursorAt(glw::Point point) const override {
        if (glw::Rect{size().width - commentGrip, size().height - commentGrip, commentGrip, commentGrip}.contains(point))
            return glw::platform::Cursor::ResizeDiagonal;
        return std::nullopt;
    }

private:
    GraphView& graph_;
    fg::CommentBox box_;
    bool selected_ = false;
    std::optional<glw::text::TextLayout> text_;
    std::string shown_;
};

GraphView::GraphView(core::Document& doc, std::string graph) : doc_(doc), graph_(graph), edits_(doc, std::move(graph)) {
    setFocusable(true);
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
    if (!event.pressed)
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
        context.painter.drawBezier(bz->a, bz->c1, bz->c2, bz->b, width, color);
    }
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
    // the link being dragged, from its pin to the mouse
    if (drag_ == Drag::Link) {
        if (const std::optional<glw::Point> pin = pinPoint(linkFrom_.node, linkFrom_.pin, linkFrom_.dir)) {
            glw::Color color = linkFrom_.kind == fg::PinKind::Exec ? colors.exec
                               : linkFrom_.type && !linkFrom_.wildcard ? rgb(registry().typeColor(linkFrom_.type))
                                                                        : rgb(160, 160, 168);
            color.a *= 0.75f;
            const Bezier bz = linkFrom_.dir == fg::PinDir::Out ? curve(*pin, linkTo_) : curve(linkTo_, *pin);
            p.drawBezier(bz.a, bz.c1, bz.c2, bz.b, linkFrom_.kind == fg::PinKind::Exec ? 3.0f : 2.0f, color);
        }
    }
    // the rectangle that selects
    if (band_) {
        const glw::Color accent = context.ui.theme.palette.accent;
        p.fillRect(*band_, {accent.r, accent.g, accent.b, 0.12f});
        p.strokeRoundedRect(*band_, 0, 1 / std::max(zoom(), 0.1f), accent);
    }
}

} // namespace fged::gl
