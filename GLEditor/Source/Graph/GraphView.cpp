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
#include <unordered_set>

namespace fged::gl {

// A comment of the graph: a colored frame behind a group of nodes, its text in its header.
class CommentView final : public glw::Widget {
public:
    explicit CommentView(const fg::CommentBox& box) : box_(box) { setHitTestVisible(false); }

    const fg::CommentBox& box() const { return box_; }

protected:
    glw::Size measureContent(glw::Size) override { return {float(box_.w), float(box_.h)}; }

    void paint(glw::PaintContext& context) override {
        glw::paint::Painter& p = context.painter;
        const glw::Rect r{0, 0, size().width, size().height};
        constexpr float header = 26;
        const float radius = std::min(context.ui.theme.node.cornerRadius, header / 2);     // as the theme's nodes are rounded
        p.fillRoundedRect(r, radius, rgb(box_.color, 0.22f));
        p.pushClip({0, 0, r.width, header});
        p.fillRoundedRect({0, 0, r.width, header + radius}, radius, rgb(box_.color, 0.75f));
        p.popClip();
        p.strokeRoundedRect(r, radius, 1, rgb(box_.color, 0.9f));
        if (!text_ || shown_ != box_.text) {
            shown_ = box_.text;
            text_.emplace(box_.text, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, context.ui.theme.fontSize + 1});
        }
        p.pushClip({6, 0, std::max(0.0f, r.width - 12), header});
        context.drawText(*text_, {8, (header - text_->size().height) / 2}, {1, 1, 1, 1});
        p.popClip();
    }

private:
    fg::CommentBox box_;
    std::optional<glw::text::TextLayout> text_;
    std::string shown_;
};

GraphView::GraphView(core::Document& doc, std::string graph) : doc_(doc), graph_(std::move(graph)) {
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
    graph_ = std::move(name);
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
            } else {
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
        removeItem(*it->second);
        it = nodes_.erase(it);
    }

    // the comments: made again, behind the nodes
    for (CommentView* c : comments_)
        removeItem(*c);
    comments_.clear();
    if (graph) {
        for (const fg::CommentBox& box : graph->comments) {
            CommentView& view = addItem(std::make_unique<CommentView>(box), {float(box.x), float(box.y)});
            lowerItem(view);
            comments_.push_back(&view);
        }
    }

    if (!framed_ && root() && !nodes_.empty()) {
        framed_ = true;
        frameItems(48, false);
    }
    invalidatePaint();
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

void GraphView::paintUnderItems(glw::PaintContext& context) {
    const fg::Graph* graph = doc_.graph(graph_);
    const fg::CompiledGraph* compiled = doc_.compiledGraph(graph_);
    if (!graph)
        return;
    const GraphColors colors = GraphColors::of(context.ui.theme);
    const glw::Rect shown = visibleArea();
    for (const fg::Connection& c : graph->connections) {
        const std::optional<glw::Point> a = pinPoint(c.fromNode, c.fromPin, fg::PinDir::Out);
        const std::optional<glw::Point> b = pinPoint(c.toNode, c.toPin, fg::PinDir::In);
        if (!a || !b)
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
        // as the Qt editor's: leaving its output to the right, coming into its input from the left
        glw::Point d = *b - *a;
        if (d.x < 80)
            d.x = 160 - d.x;
        d.y = 0;
        const glw::Point c1{a->x + d.x / 2, a->y}, c2{b->x - d.x / 2, b->y};
        // what does not show is not drawn
        const float left = std::min({a->x, b->x, c1.x, c2.x}), right = std::max({a->x, b->x, c1.x, c2.x});
        const float top = std::min(a->y, b->y), bottom = std::max(a->y, b->y);
        if (!glw::Rect{left, top, right - left, bottom - top}.inflated(8).intersects(shown))
            continue;
        context.painter.drawBezier(*a, c1, c2, *b, exec ? 3.0f : 2.0f, color);
    }
}

void GraphView::paintOverItems(glw::PaintContext& context) {
    // the comments of nodes, above them
    const GraphColors colors = GraphColors::of(context.ui.theme);
    glw::paint::Painter& p = context.painter;
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
}

} // namespace fged::gl
