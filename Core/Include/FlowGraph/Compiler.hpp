#pragma once
// Compiler: Script (editable data) -> Program (resolved, executable). Never throws for problems in
// the script; they are reported as diagnostics and `Program::hasErrors` is set.
#include <FlowGraph/Program.hpp>
#include <FlowGraph/Registry.hpp>

namespace fg {

std::shared_ptr<Program> compile(const Script& script, const Registry& reg);

// The pins of `node` with the type parameters it currently has (Node::typeArgs). Unresolved type
// parameters produce pins with `wildcard == true` that use the parameter's fallback type. Editors use
// this to draw nodes that are not (yet) part of a compiled program.
std::vector<CompiledPin> buildNodePins(const Node& node, const Registry& reg, const Script* script, const Graph* graph);

// Raw pin definitions (static + dynamic) of a node.
std::vector<PinDef> collectPinDefs(const Node& node, const NodeDef& def, const Registry& reg, const Script* script, const Graph* graph);

// Display title of a node (function / variable / event name for the script level nodes).
std::string nodeTitle(const Node& node, const NodeDef* def);

} // namespace fg
