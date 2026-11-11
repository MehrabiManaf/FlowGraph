// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A graph of the script, on a canvas: its nodes where the script places them, the links between their pins, its comments
// behind them and the comments of its nodes above them. It follows the document: what an edit changes, what the compiler
// says (a node's pins and their types, its errors), what is shown as it changes.
#include <FlowGraph/Program.hpp>
#include <FlowGraphEditorCore/Document.hpp>
#include <GLWidgets/Widgets/Canvas.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace fged::gl {

class NodeView;
class CommentView;

class GraphView final : public glw::Canvas {
public:
    GraphView(core::Document& doc, std::string graph);
    ~GraphView() override;

    const std::string& graphName() const { return graph_; }
    // The graph was renamed: its view goes on with it.
    void setGraphName(std::string name);
    const fg::Registry& registry() const { return doc_.registry(); }

    // The node a paused run is at (0: none), shown so; a node with a breakpoint.
    fg::NodeId debugNode() const { return debugNode_; }
    void setDebugNode(fg::NodeId node);
    bool isBreakpoint(fg::NodeId node) const;

    NodeView* nodeView(fg::NodeId id) const;
    // What the document has of the graph, shown again: its nodes (made, changed, gone), their places, its comments.
    void refresh();

protected:
    void paintUnderItems(glw::PaintContext& context) override;
    void paintOverItems(glw::PaintContext& context) override;
    void onRootChanged() override;

private:
    // Where the middle of a pin's connector is on the canvas; none when its node or the pin is not shown.
    std::optional<glw::Point> pinPoint(fg::NodeId node, const std::string& pin, fg::PinDir dir) const;

    core::Document& doc_;
    std::string graph_;
    std::unordered_map<fg::NodeId, NodeView*> nodes_;
    std::vector<CommentView*> comments_;
    fg::NodeId debugNode_ = 0;
    bool framed_ = false;                   // shown whole the first time it has items
    int compiledConnection_ = 0, changedConnection_ = 0;
};

} // namespace fged::gl
