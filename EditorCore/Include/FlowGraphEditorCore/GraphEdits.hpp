// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// What an editor does to a graph of the document, without a user interface: linking pins (and why two cannot be linked),
// breaking links, adding, deleting, moving, copying and pasting nodes, values of pins, comments. Each is one undoable step
// of the document (Document::edit) called `label` - the editor's words, in its language.
#include <FlowGraph/Program.hpp>
#include <FlowGraph/Refactor.hpp>
#include <FlowGraphEditorCore/Document.hpp>

#include <set>
#include <string>
#include <vector>

namespace fged::core {

// A pin of a node of the graph, as the editor knows it (from the compiled node).
struct PinEnd {
    fg::NodeId node = 0;
    std::string pin;
    fg::PinKind kind = fg::PinKind::Data;
    fg::PinDir dir = fg::PinDir::In;
    const fg::TypeDesc* type = nullptr;
    bool wildcard = false;          // its type is open (resolved by what it is linked to)

    bool valid() const { return node != 0; }
    // That pin of `node` (compiled); an invalid one when it has none.
    static PinEnd of(const fg::CompiledNode& node, const std::string& pin, fg::PinDir dir);
};

// Why two pins cannot be linked.
enum class LinkProblem {
    None,
    SameDirection,          // two inputs, or two outputs
    SameNode,
    KindsDiffer,            // execution to data
    TypesDiffer,            // the types do not convert (the editor says which: the pins' types)
    DataCycle,              // the value would be made of itself
    EndlessLoop,            // an execution loop with no way out (no Branch, Delay...)
};

class GraphEdits {
public:
    GraphEdits(Document& doc, std::string graph) : doc_(doc), graph_(std::move(graph)) {}

    const std::string& graph() const { return graph_; }
    void setGraph(std::string graph) { graph_ = std::move(graph); }

    // ---- links ----

    LinkProblem linkProblem(const PinEnd& a, const PinEnd& b) const;
    // `a` and `b` linked (an output and an input, in either order); the link an input of data, or an output of execution,
    // had goes. False (and why) when they cannot be.
    bool link(const PinEnd& a, const PinEnd& b, const std::string& label, LinkProblem* why = nullptr);
    void breakLinks(const PinEnd& pin, const std::string& label);
    // A reroute node at (x, y) - its top left corner - in the middle of the data link `c`. False for a link of execution.
    bool insertReroute(const fg::Connection& c, double x, double y, const std::string& label);

    // ---- nodes ----

    // A node made from a palette's entry at (x, y), linked to `from` (a pin a link was dragged from, let go on the empty
    // canvas) when it has a pin that fits: its id, and that pin's name in `linkedPin`. `coalesceKey`: the step it is one of.
    fg::NodeId addNode(const fg::NodeTemplate& t, double x, double y, const PinEnd* from, const std::string& label,
                       std::string* linkedPin = nullptr, const std::string& coalesceKey = {});
    // A Get (or a Set) of a variable at (x, y): `scope` "global", or "local" (of the function the graph is); its id.
    fg::NodeId addVariableNode(const std::string& name, const std::string& scope, bool set, double x, double y, const std::string& label);
    // A call of a function of the script at (x, y); 0 when there is no such function.
    fg::NodeId addCall(const std::string& function, double x, double y, const std::string& label);
    // Whether a node can be deleted, copied: the entry of a function and the events of the application are fixed.
    static bool removable(const fg::CompiledNode& node);
    // The nodes (those removable), links and comments, gone with the links of the nodes.
    void remove(const std::set<fg::NodeId>& nodes, const std::vector<fg::Connection>& links, const std::set<int>& comments,
                const std::string& label);

    struct NodePlace {
        fg::NodeId node = 0;
        double x = 0, y = 0;
    };
    struct CommentPlace {
        int comment = 0;
        double x = 0, y = 0, w = 0, h = 0;
    };
    // Where nodes and comments are now (dragged): one step of the layout (not compiled). Nothing when nothing moved.
    void place(const std::vector<NodePlace>& nodes, const std::vector<CommentPlace>& comments, const std::string& label,
               const std::string& coalesceKey = {});

    // ---- the clipboard: as the text of a script of one graph ----

    // The nodes (those removable) and the links between them, as text; empty when there are none.
    std::string copy(const std::set<fg::NodeId>& nodes) const;
    // Whether `text` is what copy gives.
    static bool isClip(const std::string& text);
    // What copy gave, put in: its top left node at (x, y), or every node moved by (x, y) when `byOffset`. An event the
    // script handles already is left out (one handler each). The ids of the nodes made; none when it is not a clip.
    std::vector<fg::NodeId> paste(const std::string& text, double x, double y, bool byOffset, const std::string& label);

    // ---- values, comments ----

    // The value of an input without a link; edits of the same pin that follow each other quickly are one step (typing).
    void setPinValue(fg::NodeId node, const std::string& pin, const fg::Value& value, const std::string& label);
    // A comment around (x, y, w, h); its id.
    int addComment(double x, double y, double w, double h, const std::string& text, const std::string& label);
    void setCommentText(int comment, const std::string& text, const std::string& label);
    void setNodeComment(fg::NodeId node, const std::string& text, const std::string& label);

    // ---- refactoring ----

    // Collapse to Function (fg::collapseToFunction): the nodes moved into a new function `function` (made unique), a call
    // of it in their place. Tried on a copy first: nodes that cannot be collapsed leave no step (the result says why).
    fg::CollapseResult collapse(const std::set<fg::NodeId>& nodes, const std::string& function, const std::string& label);

private:
    fg::Graph* graphOf(fg::Script& s) const { return s.findGraph(graph_); }

    Document& doc_;
    std::string graph_;
};

} // namespace fged::core
