#include <FlowGraph/Compiler.hpp>
#include <FlowGraph/Refactor.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace fg {

namespace {

struct End {
    NodeId node = 0;
    std::string pin;
    bool operator<(const End& o) const { return node != o.node ? node < o.node : pin < o.pin; }
    bool operator==(const End& o) const { return node == o.node && pin == o.pin; }
};

std::string uniqueName(std::string base, std::set<std::string>& taken) {
    if (base.empty()) base = "Value";
    std::string name = base;
    for (int i = 2; taken.count(name); ++i) name = base + std::to_string(i);
    taken.insert(name);
    return name;
}

} // namespace

CollapseResult collapseToFunction(Script& script, const Registry& reg, const std::string& graphName, const std::vector<NodeId>& ids,
                                  const std::string& functionName) {
    CollapseResult res;
    auto fail = [&](std::string why) {
        res.error = std::move(why);
        return res;
    };
    Graph* g = script.findGraph(graphName);
    if (!g) return fail("there is no graph '" + graphName + "'");
    const std::set<NodeId> sel(ids.begin(), ids.end());
    if (sel.empty()) return fail("select the nodes to collapse");

    const auto before = compile(script, reg);
    const CompiledGraph* cg = before->findGraph(graphName);
    if (!cg) return fail("the graph '" + graphName + "' cannot be compiled");

    // ---- what may be moved
    bool hasExec = false;
    for (NodeId id : sel) {
        const CompiledNode* cn = cg->findNode(id);
        if (!g->findNode(id) || !cn) return fail("node " + std::to_string(id) + " does not exist");
        if (!cn->def) return fail("'" + cn->title + "' is of a kind this application does not know");
        if (cn->op == NodeOp::Event) return fail("the event '" + cn->title + "' cannot be moved into a function");
        if (cn->op == NodeOp::FunctionEntry || cn->op == NodeOp::Return) return fail("the entry and return nodes of a function cannot be moved");
        if (cn->def->eventGraphOnly) return fail("'" + cn->title + "' can only be used in an event graph");
        if ((cn->op == NodeOp::GetVariable || cn->op == NodeOp::SetVariable) && cn->scope != VarScope::Global)
            return fail("'" + cn->title + "' uses a local variable or parameter of '" + graphName + "'");
        if (!cn->pure && !cn->execIn.empty()) hasExec = true;
    }
    if (!hasExec) return fail("the selection has no node with execution pins: a function call needs a place in the execution flow");

    auto pinOf = [&](const End& e, PinDir dir) -> const CompiledPin* {
        const CompiledNode* cn = cg->findNode(e.node);
        if (!cn) return nullptr;
        for (auto& p : cn->pins)
            if (p.dir == dir && p.name == e.pin) return &p;
        return nullptr;
    };

    // ---- the links that cross the border of the selection
    std::vector<Connection> internal;
    std::set<End> entryTargets, exitTargets;
    std::vector<End> entrySources, exitSources;
    std::map<End, size_t> inputOf;                     // outside source -> input of the function
    std::vector<std::pair<size_t, End>> inputLinks;    // input -> inner pin it feeds
    std::map<End, size_t> outputOf;                    // inner source -> output of the function
    std::vector<std::pair<size_t, End>> outputLinks;   // output -> outside pin it feeds
    std::vector<ParamDecl> inputs, outputs;
    std::set<std::string> inNames, outNames;
    // A Get of a variable of the script that feeds the selection is copied into the function instead of becoming an input: the nodes
    // keep reading the variable when they run (an input would be a copy taken at the call, and the selection may change it).
    std::vector<NodeId> copiedGets;
    std::vector<std::pair<NodeId, End>> copiedLinks;   // outside Get -> inner pin it feeds
    // The variables a pure value reads (through the pure nodes it is computed from).
    auto readsOf = [&](NodeId start) {
        std::set<std::string> vars;
        std::vector<NodeId> todo{start};
        std::set<NodeId> seen;
        while (!todo.empty()) {
            NodeId id = todo.back();
            todo.pop_back();
            if (!seen.insert(id).second) continue;
            const CompiledNode* cn = cg->findNode(id);
            if (!cn || !cn->pure) continue;
            if (cn->op == NodeOp::GetVariable && cn->scope == VarScope::Global) vars.insert(cn->prop("name"));
            for (auto& c : g->connections)
                if (c.toNode == id) todo.push_back(c.fromNode);
        }
        return vars;
    };
    std::set<std::string> setInside;                   // the variables of the script the selection sets
    for (NodeId id : sel)
        if (const CompiledNode* cn = cg->findNode(id); cn && cn->op == NodeOp::SetVariable && cn->scope == VarScope::Global) setInside.insert(cn->prop("name"));
    for (const Connection& c : g->connections) {
        const bool fromIn = sel.count(c.fromNode) > 0, toIn = sel.count(c.toNode) > 0;
        if (fromIn == toIn) {
            if (fromIn) internal.push_back(c);
            continue;
        }
        const End from{c.fromNode, c.fromPin}, to{c.toNode, c.toPin};
        const CompiledPin* fp = pinOf(from, PinDir::Out);
        if (!fp) return fail("a link of the selection refers to a pin that does not exist (" + c.fromPin + ")");
        const bool exec = fp->kind == PinKind::Exec;
        if (toIn) {                                     // comes in
            if (exec) {
                entryTargets.insert(to);
                entrySources.push_back(from);
                continue;
            }
            const CompiledNode* srcNode = cg->findNode(c.fromNode);
            if (srcNode && srcNode->op == NodeOp::GetVariable && srcNode->scope == VarScope::Global) {
                if (std::find(copiedGets.begin(), copiedGets.end(), c.fromNode) == copiedGets.end()) copiedGets.push_back(c.fromNode);
                copiedLinks.emplace_back(c.fromNode, to);
                continue;
            }
            auto it = inputOf.find(from);
            if (it == inputOf.end()) {
                const Node* src = g->findNode(c.fromNode);
                if (srcNode && srcNode->pure)
                    for (auto& v : readsOf(c.fromNode))
                        if (setInside.count(v))
                            res.warnings.push_back("The input from '" + srcNode->title + "' reads '" + v +
                                                   "', which the function changes: the function gets the value of the moment of the call.");
                std::string base = src && src->def == "var.get" ? src->prop("name") : c.toPin;
                inputs.push_back({uniqueName(base, inNames), fp->type ? fp->type->name : "any", false, false, Value()});
                it = inputOf.emplace(from, inputs.size() - 1).first;
            }
            inputLinks.emplace_back(it->second, to);
        } else {                                        // goes out
            if (exec) {
                exitTargets.insert(to);
                exitSources.push_back(from);
                continue;
            }
            auto it = outputOf.find(from);
            if (it == outputOf.end()) {
                outputs.push_back({uniqueName(c.fromPin, outNames), fp->type ? fp->type->name : "any", false, false, Value()});
                it = outputOf.emplace(from, outputs.size() - 1).first;
            }
            outputLinks.emplace_back(it->second, to);
        }
    }
    if (entryTargets.size() > 1) return fail("the execution enters the selection at more than one place; a function has one entry");
    if (exitTargets.size() > 1) return fail("the execution leaves the selection towards more than one place; a function returns to one");
    if (!outputs.empty() && exitSources.empty())
        return fail("values leave the selection but the execution does not: the function would never reach its Return");

    // a chain that ended inside the selection now goes on after the call
    if (!exitSources.empty()) {
        bool endsInside = false;
        for (NodeId id : sel) {
            const CompiledNode* cn = cg->findNode(id);
            for (auto& p : cn->pins)
                if (p.kind == PinKind::Exec && p.dir == PinDir::Out && !p.connected) endsInside = true;
        }
        if (endsInside)
            res.warnings.push_back("Execution that ended inside the selection (an exec output without a link) now continues after the call.");
    }

    // ---- the new function: the nodes, their links and the ones of the border to its entry and return
    const Script backup = script;
    const std::string name = script.uniqueGraphName(functionName.empty() ? "NewFunction" : functionName);
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (NodeId id : sel) {
        const Node* n = g->findNode(id);
        minX = std::min(minX, n->x);
        minY = std::min(minY, n->y);
        maxX = std::max(maxX, n->x);
        maxY = std::max(maxY, n->y);
    }
    std::vector<Node> moved;
    for (NodeId id : sel) moved.push_back(*g->findNode(id));
    Graph& fn = script.addFunction(name, inputs, outputs);       // a deque: `g` stays valid
    NodeId entryId = 0, returnId = 0;
    for (auto& n : fn.nodes) {
        if (n.def == "func.entry") { entryId = n.id; n.x = 0; n.y = 100; }
        if (n.def == "func.return") { returnId = n.id; n.x = (maxX - minX) + 600; n.y = 100; }
    }
    std::map<NodeId, NodeId> newId;
    for (Node n : moved) {
        const NodeId old = n.id;
        n.id = fn.nextId++;
        n.x = n.x - minX + 300;
        n.y = n.y - minY + 100;
        newId[old] = n.id;
        fn.nodes.push_back(std::move(n));
    }
    for (NodeId id : copiedGets) {
        Node n = *g->findNode(id);
        n.id = fn.nextId++;
        n.x = std::max(100.0, n.x - minX + 300);
        n.y = n.y - minY + 100;
        newId[id] = n.id;
        fn.nodes.push_back(std::move(n));
    }
    for (auto& [src, to] : copiedLinks) fn.connections.push_back({newId[src], "Value", newId[to.node], to.pin});
    for (const Connection& c : internal) fn.connections.push_back({newId[c.fromNode], c.fromPin, newId[c.toNode], c.toPin});
    if (!entryTargets.empty()) fn.connections.push_back({entryId, "Then", newId[entryTargets.begin()->node], entryTargets.begin()->pin});
    for (const End& s : exitSources) fn.connections.push_back({newId[s.node], s.pin, returnId, "In"});
    for (auto& [i, to] : inputLinks) fn.connections.push_back({entryId, inputs[i].name, newId[to.node], to.pin});
    for (auto& [from, i] : outputOf) fn.connections.push_back({newId[from.node], from.pin, returnId, outputs[i].name});

    // ---- the old graph: the nodes go, a call takes their place and their links at the border
    auto& conns = g->connections;
    conns.erase(std::remove_if(conns.begin(), conns.end(), [&](const Connection& c) { return sel.count(c.fromNode) || sel.count(c.toNode); }),
                conns.end());
    g->nodes.erase(std::remove_if(g->nodes.begin(), g->nodes.end(), [&](const Node& n) { return sel.count(n.id) > 0; }), g->nodes.end());
    Node& call = g->addNode("func.call", (minX + maxX) / 2, (minY + maxY) / 2);
    call.props["function"] = name;
    const NodeId callId = call.id;
    for (const End& s : entrySources) conns.push_back({s.node, s.pin, callId, "In"});
    if (!exitTargets.empty()) conns.push_back({callId, "Then", exitTargets.begin()->node, exitTargets.begin()->pin});
    for (auto& [from, i] : inputOf) conns.push_back({from.node, from.pin, callId, inputs[i].name});
    for (auto& [i, to] : outputLinks) conns.push_back({callId, outputs[i].name, to.node, to.pin});
    // an outside Get that only fed the selection has nothing left to do
    for (NodeId id : copiedGets) {
        const bool used = std::any_of(conns.begin(), conns.end(), [&](const Connection& c) { return c.fromNode == id || c.toNode == id; });
        if (!used) g->nodes.erase(std::remove_if(g->nodes.begin(), g->nodes.end(), [&](const Node& n) { return n.id == id; }), g->nodes.end());
    }

    // a safety net: the script must not get compile errors it did not have
    const auto after = compile(script, reg);
    if (after->hasErrors && !before->hasErrors) {
        std::string why = "the result would not compile";
        for (auto& d : after->diagnostics)
            if (d.severity == Diagnostic::Severity::Error) { why += ": " + d.message; break; }
        script = backup;
        return fail(why);
    }
    res.ok = true;
    res.function = name;
    res.callNode = callId;
    return res;
}

} // namespace fg
