#pragma once
// Node definitions: the "classes" of graph nodes. Built-ins, generated nodes and plugins all use these.
#include <FlowGraph/Types.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fg {

enum class PinKind : uint8_t { Exec, Data };
enum class PinDir : uint8_t { In, Out };

// Type expressions are plain strings: "int", "string", "Actor", "array<T>", "map<string,float>".
// A name that is declared as a type parameter of the node (see NodeDef::typeParams) is a
// wildcard that gets resolved when the node is connected ("T", "K", "V").
// How a built-in node uses an array / map on a pin. Containers are references inside a graph: a node does not copy the one it is
// connected to. The editor draws such pins as references (diamonds; a padlock too for Read); a pin whose type is not a container is
// not affected, so the same annotation serves "array<T>" pins and "T" pins that may or may not resolve to a container.
enum class PinAccess : uint8_t {
    Value,      // an ordinary value
    Read,       // input: reads the connected container without copying it - a const reference
    Modify,     // input: modifies the connected container in place - a reference
    Alias,      // output: refers to a container that exists elsewhere (an element, a variable, an input) instead of a new one - a reference
};

struct PinDef {
    std::string name;
    PinKind kind = PinKind::Data;
    PinDir dir = PinDir::In;
    std::string type;                 // type expression; empty for exec pins
    Value def;                        // default literal of unconnected data inputs
    std::string doc;
    bool byRef = false;               // function parameter passed by reference
    bool isConst = false;             // const function parameter
    PinAccess access = PinAccess::Value;
    bool hidden = false;              // not shown in the editor (still evaluated)
    bool advanced = false;
};

inline PinDef execIn(std::string name = "In") {
    PinDef p; p.name = std::move(name); p.kind = PinKind::Exec; p.dir = PinDir::In; return p;
}
inline PinDef execOut(std::string name = "Then") {
    PinDef p; p.name = std::move(name); p.kind = PinKind::Exec; p.dir = PinDir::Out; return p;
}
inline PinDef dataIn(std::string name, std::string type, Value def = Value()) {
    PinDef p; p.name = std::move(name); p.type = std::move(type); p.def = std::move(def); return p;
}
inline PinDef dataOut(std::string name, std::string type) {
    PinDef p; p.name = std::move(name); p.dir = PinDir::Out; p.type = std::move(type); return p;
}

inline PinDef reads(PinDef p) { p.access = PinAccess::Read; return p; }
inline PinDef modifies(PinDef p) { p.access = PinAccess::Modify; return p; }
inline PinDef aliases(PinDef p) { p.access = PinAccess::Alias; return p; }

// A node type parameter ("T"). `allowed` restricts the types it can take (empty = anything);
// `fallback` is used while the parameter is still unresolved.
struct TypeParam {
    std::string name;
    std::vector<std::string> allowed;
    std::string fallback = "int";
};

class Registry;
class NodeCtx;
class Script;
class Graph;
class Node;
struct NodeTemplate;
struct CompiledNode;

// Passed to dynamic pin builders and palette expanders.
struct NodeBuildCtx {
    const Registry& reg;
    const Script* script = nullptr;   // may be null (e.g. palette preview)
    const Graph* graph = nullptr;     // may be null
};

enum class NodeStyle : uint8_t { Function, Pure, Event, Flow, Variable, Macro, Custom, Reroute };

// Special behaviour implemented by the interpreter itself.
enum class NodeOp : uint8_t {
    Native,         // def.run
    Event,          // handler of a host event: the entry point of an event graph chain
    FunctionEntry,  // entry point of a function graph
    Return,         // leaves a function graph
    CallFunction,   // calls a function graph of the script
    GetVariable,
    SetVariable,
    Reroute,        // editor helper; passes its input through
};

// Built-in nodes the interpreter evaluates inline: it reads the inputs in place and skips the NodeCtx and the std::function call.
// An intrinsic mirrors the node's `run` function exactly (results, errors, order of evaluation) and declines whatever it cannot
// read in place (conversions, container defaults); `run` then does the work, so it must stay in sync with the intrinsic.
// A NodeDef copied from a built-in one keeps its intrinsic: reset it when `run` is replaced.
enum class Intrinsic : uint8_t {
    None,
    GetVar,                                      // reads a variable (set by the compiler for NodeOp::GetVariable)
    Add, Sub, Mul, Div, Mod, Min, Max,          // math, two inputs
    Eq, Ne, Lt, Le, Gt, Ge,                      // comparisons
    And, Or, Xor, Not,                           // logic
    Branch, For, ForEach, While,                 // flow
    ArrayGet, ArraySet, ArrayAdd, ArrayLength,   // containers
    MapGet, MapSet, MapContains, MapLength,
};

struct NodeDef {
    std::string id;                   // unique, e.g. "flow.branch", "fn:Math.Sin", "m:Actor.Move"
    std::string title, category, doc, keywords, source;
    NodeStyle style = NodeStyle::Function;
    NodeOp op = NodeOp::Native;
    Intrinsic intrinsic = Intrinsic::None;
    bool pure = false;                // evaluated lazily when an output is pulled; no exec pins
    bool eventGraphOnly = false;
    bool functionGraphOnly = false;
    bool userCreatable = true;        // shown in the palette
    bool generated = false;           // produced by the registry from types / functions
    // Const checking: the node modifies the object / container connected to its first data input
    // (so it may not be applied to a const parameter), or its first output aliases its first input.
    bool mutatesFirstInput = false;
    bool aliasesFirstInput = false;
    // Loop nodes: ordinal of the exec output that is taken when a Break node unwinds to this node.
    int breakPin = -1;

    std::vector<TypeParam> typeParams;
    std::vector<PinDef> pins;         // static pins
    // Append pins that depend on node properties or on the script (variables, signatures...).
    std::function<void(const Node&, const NodeBuildCtx&, std::vector<PinDef>&)> dynamicPins;
    // Produce several palette entries for this definition (e.g. one per variable).
    std::function<void(const NodeBuildCtx&, std::vector<NodeTemplate>&)> expand;
    // The implementation. Reads inputs / writes outputs / chooses the next exec pin via NodeCtx.
    std::function<void(NodeCtx&)> run;

    const TypeParam* typeParam(const std::string& n) const {
        for (auto& t : typeParams) if (t.name == n) return &t;
        return nullptr;
    }
};

// A node that can be created from the palette: a definition plus initial properties.
struct NodeTemplate {
    std::string defId;
    std::map<std::string, std::string> props;
    std::map<std::string, std::string> typeArgs;
    std::string title, category, keywords, doc;
    // Signature used by editors to filter the palette when dragging from a pin.
    std::vector<PinDef> pins;
    NodeStyle style = NodeStyle::Function;
};

} // namespace fg
