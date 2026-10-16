#pragma once
// The compiled form of a Script: resolved node definitions, pins with concrete types, data links
// with frame slots, exec targets. Editors read it to draw nodes; the interpreter executes it.
#include <FlowGraph/Script.hpp>

#include <memory>
#include <unordered_map>

namespace fg {

struct Diagnostic {
    enum class Severity : uint8_t { Info, Warning, Error };
    Severity severity = Severity::Error;
    std::string graph;
    NodeId node = 0;
    std::string message;
};

struct CompiledPin {
    std::string name;
    PinKind kind = PinKind::Data;
    PinDir dir = PinDir::In;
    const TypeDesc* type = nullptr;   // null for exec pins
    bool wildcard = false;            // type depends on a type parameter that is not resolved yet
    bool byRef = false;
    bool isConst = false;
    bool hidden = false;
    bool advanced = false;
    bool connected = false;
    int ordinal = 0;                  // index among the pins with the same kind and direction
    Value def;                        // default literal, already converted to `type` (data inputs)
    std::string doc;
};

// Where a data input gets its value from.
struct DataSource {
    enum class Kind : uint8_t { Default, Slot, Param } kind = Kind::Default;
    int node = -1;                    // source node index in the graph
    int pin = -1;                     // source pin index (in node.pins)
    int slot = -1;                    // frame slot (Kind::Slot) or parameter index (Kind::Param)
    bool pure = false;                // the source node is evaluated on demand
    bool convert = false;             // value must be converted (types differ)
    const TypeDesc* from = nullptr;
    const TypeDesc* to = nullptr;
};

enum class VarScope : uint8_t { Global, Local, Param };

struct CompiledNode {
    NodeId id = 0;
    int index = 0;                    // position in CompiledGraph::nodes
    std::string defId;
    const NodeDef* def = nullptr;     // null when the definition is unknown (e.g. missing plugin)
    NodeOp op = NodeOp::Native;
    bool pure = false;
    double x = 0, y = 0;
    std::string title;                // display title (function name, variable name, ...)
    std::string comment;
    std::map<std::string, std::string> props;
    std::map<std::string, std::string> typeArgs;   // resolved type parameters
    std::vector<CompiledPin> pins;

    // execution tables
    std::vector<int> dataIn, dataOut, execIn, execOut;   // pin indices by ordinal
    std::vector<DataSource> inputs;                       // per data input ordinal
    std::vector<int> execTarget;                          // per exec output ordinal: node index or -1
    std::vector<int> execTargetPin;                       // per exec output ordinal: pin index of the exec input hit
    int outBase = 0;                                      // first frame slot (one per data output ordinal)
    int resolved = -1;                                    // variable index / function graph index
    VarScope scope = VarScope::Global;

    const CompiledPin* findPin(const std::string& name, PinDir dir, PinKind kind) const {
        for (auto& p : pins) if (p.dir == dir && p.kind == kind && p.name == name) return &p;
        return nullptr;
    }
    int findPinIndex(const std::string& name, PinDir dir) const {
        for (size_t i = 0; i < pins.size(); ++i) if (pins[i].dir == dir && pins[i].name == name) return static_cast<int>(i);
        return -1;
    }
    std::string prop(const std::string& k, const std::string& d = "") const {
        auto it = props.find(k);
        return it == props.end() ? d : it->second;
    }
};

struct CompiledParam {
    ParamDecl decl;
    const TypeDesc* type = nullptr;
};
struct CompiledVariable {
    VariableDecl decl;
    const TypeDesc* type = nullptr;
    Value def;                        // converted default
};

struct CompiledGraph {
    std::string name;
    GraphKind kind = GraphKind::Event;
    std::vector<CompiledNode> nodes;
    std::unordered_map<NodeId, int> nodeIndex;
    std::vector<CompiledParam> inputs, outputs;
    std::vector<CompiledVariable> locals;
    int slotCount = 0;
    std::vector<const TypeDesc*> slotTypes;   // type of every frame slot (used to initialise frames)
    int entry = -1;                   // function entry node index
    bool hasRefParams = false;

    const CompiledNode* findNode(NodeId id) const {
        auto it = nodeIndex.find(id);
        return it == nodeIndex.end() ? nullptr : &nodes[static_cast<size_t>(it->second)];
    }
};

class Program {
public:
    const Registry* registry = nullptr;
    std::string name;
    std::vector<CompiledVariable> variables;
    std::vector<CompiledGraph> graphs;
    // event name -> (graph index, node index)
    std::unordered_map<std::string, std::pair<int, int>> events;
    std::unordered_map<std::string, int> functions;   // function name -> graph index
    std::vector<Diagnostic> diagnostics;
    bool hasErrors = false;

    const CompiledGraph* findGraph(const std::string& n) const {
        for (auto& g : graphs) if (g.name == n) return &g;
        return nullptr;
    }
    int graphIndex(const std::string& n) const {
        for (size_t i = 0; i < graphs.size(); ++i) if (graphs[i].name == n) return static_cast<int>(i);
        return -1;
    }
    int variableIndex(const std::string& n) const {
        for (size_t i = 0; i < variables.size(); ++i) if (variables[i].decl.name == n) return static_cast<int>(i);
        return -1;
    }
    bool ok() const { return !hasErrors; }
};

} // namespace fg
