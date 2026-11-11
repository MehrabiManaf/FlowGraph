// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A graph of the script, on a canvas: its nodes where the script places them, the links between their pins, its comments
// behind them and the comments of its nodes above them. It follows the document: what an edit changes, what the compiler
// says (a node's pins and their types, its errors), what is shown as it changes.
//
// And it is edited there (every change one undoable step of the document, core::GraphEdits):
//   a click selects a node (Ctrl or Shift: adds it, or takes it out), a link, or a comment by its header; dragging the
//   empty canvas selects what the rectangle touches;
//   the nodes selected are dragged together (on the grid when it snaps: a node alone level with a pin it is linked to, Alt
//   moving freely); a comment by its header, with the nodes in it, and resized by its corner;
//   a pin dragged to another links them (why not, when they cannot be: told); Alt and a click breaks a pin's links;
//   a link of data clicked twice gets a reroute there;
//   Delete removes what is selected, F frames it (or everything), Escape lets go of a link being dragged;
//   Space or Tab searches the nodes to add one where the mouse is; a link let go on the empty canvas searches those that
//   fit its pin, and links the one added; the right button: what can be done there (on a node, a comment, the canvas);
//   a value of an input: its check box toggled, its box typed into (Enter or another click takes it, Escape not), an
//   enum's chosen from its values; a comment's header clicked twice: its text.
// Cut, copy, paste, duplicate and select all are the editor's (its Edit menu).
#include <FlowGraph/Program.hpp>
#include <FlowGraphEditorCore/Document.hpp>
#include <FlowGraphEditorCore/GraphEdits.hpp>
#include "Graph/NodeView.hpp"
#include <GLWidgets/Widgets/Animation.hpp>
#include <GLWidgets/Widgets/Canvas.hpp>
#include <GLWidgets/Widgets/Localization.hpp>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fged::gl {


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

    // ---- what a run shows ----

    // A link of execution taken by the run (from `from`'s `execOut`th output to `to`): it lights up for a moment, dots
    // running along it, and the node it goes to flashes.
    void fireLink(fg::NodeId from, int execOut, fg::NodeId to);
    // The node in view (panned to it, when it is not).
    void showNode(fg::NodeId id);
    // While a run is paused: the text of a data pin's last value (false: none), shown with its tooltip.
    void setPinValues(std::function<bool(fg::NodeId, const std::string&, std::string&)> provider) { pinValues_ = std::move(provider); }
    bool pinValue(fg::NodeId node, const std::string& pin, std::string& out) const { return pinValues_ && pinValues_(node, pin, out); }
    // A node clicked twice (a call of a function: its graph opened).
    void setOnNodeActivated(std::function<void(fg::NodeId)> handler) { onNodeActivated_ = std::move(handler); }
    // The time of the animations (the scheduler's).
    glw::TimePoint now() const;
    // What the document has of the graph, shown again: its nodes (made, changed, gone), their places, its comments.
    void refresh();

    // ---- what is selected ----

    const std::set<fg::NodeId>& selectedNodes() const { return selectedNodes_; }
    void select(const std::set<fg::NodeId>& nodes, const std::set<int>& comments = {});
    void selectAll();
    void clearSelection();
    bool hasSelection() const { return !selectedNodes_.empty() || !selectedComments_.empty() || selectedLink_.has_value(); }
    // What is selected framed, or every node when nothing is.
    void frameSelection();

    // ---- the editor's commands ----

    void deleteSelection();
    void copySelection();
    void cutSelection();
    // What was copied, where the mouse is on the canvas (its middle when the mouse is not on it).
    void paste();
    // A copy of the selection beside it.
    void duplicateSelection();
    // The nodes to add at `at` (the canvas's), searched by name; those that fit `from` (a pin a link was dragged from), and
    // the one added linked to it.
    void openPalette(glw::Point at, const core::PinEnd* from = nullptr);
    // A comment around the selected nodes, or at `at` (the canvas's) when none is.
    void addComment(glw::Point at);
    void toggleBreakpoints();

    // Nodes moved by the user land on a grid of `step` dips (Alt moves them freely).
    void setSnapToGrid(bool snap) { snap_ = snap; }
    bool snapToGrid() const { return snap_; }

    // What to tell the user (why two pins cannot be linked): the status bar's.
    void setOnMessage(std::function<void(glw::LocalizedText)> handler) { onMessage_ = std::move(handler); }
    // The nodes selected changed: what the Details panel shows.
    void setOnSelectionChanged(std::function<void()> handler) { onSelectionChanged_ = std::move(handler); }

    // ---- what its nodes and comments pass on (the mouse on them) ----

    void nodeMouse(NodeView& node, glw::MouseEvent& event);
    // `part`: 0 its header (it moves, with the nodes in it), 1 its corner (it is resized).
    void commentMouse(CommentView& comment, glw::MouseEvent& event, int part);

protected:
    void paintUnderItems(glw::PaintContext& context) override;
    void paintOverItems(glw::PaintContext& context) override;
    void onRootChanged() override;
    void onPreviewMouse(glw::MouseEvent& event) override;
    void onMouse(glw::MouseEvent& event) override;
    void onKey(glw::KeyEvent& event) override;
    // a variable or a function of My Script dropped on it: a node there (a Get or a Set, a call)
    glw::DropAction onDragOver(const glw::DragEvent& event) override;
    glw::DropAction onDrop(const glw::DragEvent& event) override;

private:
    enum class Drag { None, Move, Resize, Link, Band };
    struct Bezier {
        glw::Point a, c1, c2, b;
    };

    // Where the middle of a pin's connector is on the canvas; none when its node or the pin is not shown.
    std::optional<glw::Point> pinPoint(fg::NodeId node, const std::string& pin, fg::PinDir dir) const;
    std::optional<Bezier> linkCurve(const fg::Connection& c) const;
    static Bezier curve(glw::Point a, glw::Point b);
    // The link at `point` of the canvas.
    std::optional<fg::Connection> linkAt(glw::Point point) const;
    CommentView* commentView(int id) const;
    // `text` in the language of the editor (an undo step's name).
    std::string words(const glw::LocalizedText& text) const;
    // What the right button offers at `at` of the canvas.
    void contextMenu(glw::Point at);
    // A dialog with a field for a text: `done` given what was typed, when it is taken.
    void askText(glw::LocalizedText title, std::string text, std::function<void(std::string)> done);
    // ---- the value of an input, typed in a box over it ----
    void editValue(NodeView& node, const NodeView::ValueHit& hit);
    void finishValue(bool take);
    void tell(glw::LocalizedText text) const;
    void selectionChanged();
    void showSelection();

    // dragging
    void beginMove(glw::Point at, std::vector<NodeView*> nodes, std::vector<CommentView*> comments);
    void moveTo(glw::Point at, bool free);
    void endDrag();
    // Where a node moved alone goes: on the grid, or level with a pin it is linked to (snapping).
    glw::Point snappedAlone(const NodeView& node, glw::Point proposed) const;

    core::Document& doc_;
    std::string graph_;
    core::GraphEdits edits_;
    std::unordered_map<fg::NodeId, NodeView*> nodes_;
    std::vector<CommentView*> comments_;
    fg::NodeId debugNode_ = 0;
    bool framed_ = false;                   // shown whole the first time it has items
    int compiledConnection_ = 0, changedConnection_ = 0;

    std::set<fg::NodeId> selectedNodes_;
    std::set<int> selectedComments_;
    std::optional<fg::Connection> selectedLink_;
    bool snap_ = false;
    float snapStep_ = 20;

    Drag drag_ = Drag::None;
    glw::Point dragFrom_;                   // on the canvas
    bool dragMoved_ = false;                // past the threshold: a drag, not a click
    fg::NodeId pressedAlone_ = 0;           // pressed without Ctrl or Shift: selected alone if not dragged
    std::vector<std::pair<NodeView*, glw::Point>> moving_;          // and where each was
    std::vector<std::pair<CommentView*, glw::Rect>> movingComments_;
    core::PinEnd linkFrom_;
    glw::Point linkTo_;                     // the end of the link being dragged
    std::optional<glw::Rect> band_;
    std::set<fg::NodeId> bandBefore_;       // what was selected before the band, kept (Ctrl, Shift)
    glw::Point mouse_;                      // its own coordinates, the last the mouse was at
    bool mouseIn_ = false;

    glw::Widget* valueEditor_ = nullptr;    // the box a value is typed in, on the canvas over the input's
    fg::NodeId editNode_ = 0;
    std::string editPin_;
    const fg::TypeDesc* editType_ = nullptr;
    std::string editText_;                  // what it showed when it opened
    int addSerial_ = 0;                     // the steps of nodes added (one each, with where they are put after)

    // links fired, and when (they fade a while after the last time)
    struct Firing {
        fg::Connection link;
        glw::TimePoint first, last;
    };
    std::vector<Firing> fired_;
    glw::Timer animation_;                  // repaints while a link or a node is lit
    void animate();
    void paintFiring(glw::PaintContext& context, const Bezier& curve, const Firing& firing, bool exec, glw::Color color);
    std::function<bool(fg::NodeId, const std::string&, std::string&)> pinValues_;
    std::function<void(fg::NodeId)> onNodeActivated_;

    std::function<void(glw::LocalizedText)> onMessage_;
    std::function<void()> onSelectionChanged_;
};

} // namespace fged::gl
