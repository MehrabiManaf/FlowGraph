// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// The script data model: what the editor edits and what is saved to disk. It is plain data;
// the Compiler turns it into an executable Program.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Json.hpp>
#include <FlowGraph/NodeDef.hpp>

#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fg {

using NodeId = int;

// Function / event parameter.
//   byRef  (function inputs only, default false = by value): the argument is a reference to the caller's
//          variable - reads see the variable's current value and "Set <param>" writes through to it.
//   isConst (function inputs only): the function may not modify the parameter. A const reference
//          ("const T&") also accepts values that are not variables and makes no copy.
struct ParamDecl {
    std::string name, type;
    bool byRef = false;
    bool isConst = false;
    Value def;
    bool operator==(const ParamDecl& o) const {
        return name == o.name && type == o.type && byRef == o.byRef && isConst == o.isConst;
    }
};

struct VariableDecl {
    std::string name, type;
    Value def;
    std::string category, doc;
};

class Node {
public:
    NodeId id = 0;
    std::string def;                               // NodeDef id
    double x = 0, y = 0;
    std::string comment;                           // text bubble shown above the node
    std::map<std::string, std::string> props;      // node specific settings (variable, function, event name, ...)
    std::map<std::string, std::string> typeArgs;   // resolved type parameters ("T" -> "float")
    std::map<std::string, Value> defaults;         // literal values of unconnected input pins (by pin name)
    std::vector<ParamDecl> inputs, outputs;        // custom data pins the user added (only for NodeDef::customInputs / customOutputs)

    std::string prop(const std::string& k, const std::string& d = "") const {
        auto it = props.find(k);
        return it == props.end() ? d : it->second;
    }
};

struct Connection {
    NodeId fromNode = 0;
    std::string fromPin;
    NodeId toNode = 0;
    std::string toPin;
    bool operator==(const Connection& o) const {
        return fromNode == o.fromNode && fromPin == o.fromPin && toNode == o.toNode && toPin == o.toPin;
    }
};

// A resizable, colored rectangle with a caption behind a group of nodes (editor decoration).
struct CommentBox {
    int id = 0;
    std::string text;
    double x = 0, y = 0, w = 300, h = 160;
    uint32_t color = 0x4a6fa5;
};

enum class GraphKind : uint8_t { Event, Function };

class FG_CORE_EXPORT Graph {
public:
    std::string name;
    GraphKind kind = GraphKind::Event;
    std::vector<ParamDecl> inputs, outputs;        // function signature
    std::vector<VariableDecl> locals;              // function local variables
    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::vector<CommentBox> comments;
    NodeId nextId = 1;
    int nextCommentId = 1;
    std::string doc;

    Node& addNode(const std::string& defId, double x = 0, double y = 0);
    Node* findNode(NodeId id);
    const Node* findNode(NodeId id) const;
    int nodeIndex(NodeId id) const;
    VariableDecl* findLocal(const std::string& n);
    const VariableDecl* findLocal(const std::string& n) const;
    CommentBox& addComment(double x, double y, double w = 300, double h = 160);
    CommentBox* findComment(int id);
};

class FG_CORE_EXPORT Script {
public:
    std::string name = "Script";
    std::string doc;
    std::vector<VariableDecl> variables;
    std::deque<Graph> graphs;

    int graphIndex(const std::string& name) const;
    int variableIndex(const std::string& name) const;
    Graph* findGraph(const std::string& name);
    const Graph* findGraph(const std::string& name) const;
    VariableDecl* findVariable(const std::string& name);
    const VariableDecl* findVariable(const std::string& name) const;

    Graph& addEventGraph(const std::string& name = "EventGraph");
    // Adds a function graph with an entry node and one return node.
    Graph& addFunction(const std::string& name, std::vector<ParamDecl> inputs = {}, std::vector<ParamDecl> outputs = {});
    bool removeGraph(const std::string& name);
    std::string uniqueGraphName(const std::string& base) const;
    std::string uniqueVariableName(const std::string& base) const;

    // Structural rename helpers: they also update every node that refers to the old name.
    void renameVariable(const std::string& from, const std::string& to);
    void renameFunction(const std::string& from, const std::string& to);
    void renameLocal(const std::string& graph, const std::string& from, const std::string& to);
    // A variable gets another type: its default and the values typed into its Set nodes (their Value pin) are cleared, as they
    // were values of the old type (Sunday of a Weekday would be a 7 in another enum, which has no such value).
    void retypeVariable(const std::string& name, const std::string& type);
    void retypeLocal(const std::string& graph, const std::string& name, const std::string& type);

    // ---- serialization (JSON, extension .flow)
    // Version of the file format this build writes. Older files are read (and migrated); a file written by a newer
    // FlowGraph is refused, because reading it would silently drop what this build does not understand.
    static constexpr int kFileVersion = 1;
    Json toJson() const;
    static Script fromJson(const Json& j);              // throws JsonError on malformed documents and newer file versions
    std::string toText(int indent = 2) const { return toJson().dump(indent); }
    static Script parse(const std::string& text) { return fromJson(Json::parse(text)); }
    static Script load(const std::string& path);        // throws std::runtime_error / JsonError
    void save(const std::string& path) const;
};

FG_CORE_EXPORT Json valueToJson(const Value& v);
FG_CORE_EXPORT Value jsonToValue(const Json& j);

} // namespace fg
