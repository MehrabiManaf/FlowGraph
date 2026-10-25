// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Compiler: Script (editable data) -> Program (resolved, executable). Never throws for problems in
// the script; they are reported as diagnostics and `Program::hasErrors` is set.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Program.hpp>
#include <FlowGraph/Registry.hpp>

namespace fg {

struct CompileOptions {
    // Which nodes a script may use (null = all). A node the filter refuses is a compile error ("'Print String' is not allowed here"):
    // an application that runs scripts it does not trust can keep them away from built-in or registered nodes it did not mean for them.
    std::function<bool(const NodeDef&)> allowNode;
};

FG_CORE_EXPORT std::shared_ptr<Program> compile(const Script& script, const Registry& reg, const CompileOptions& options = {});

// The pins of `node` with the type parameters it currently has (Node::typeArgs). Unresolved type
// parameters produce pins with `wildcard == true` that use the parameter's fallback type. Editors use
// this to draw nodes that are not (yet) part of a compiled program.
// `types`: the types of the script (Program::types); null makes them from `script`.
FG_CORE_EXPORT std::vector<CompiledPin> buildNodePins(const Node& node, const Registry& reg, const Script* script, const Graph* graph,
                                                     const TypeScope* types = nullptr);

// Raw pin definitions (static + dynamic) of a node.
FG_CORE_EXPORT std::vector<PinDef> collectPinDefs(const Node& node, const NodeDef& def, const Registry& reg, const Script* script, const Graph* graph,
                                                  const TypeScope* types = nullptr);

// ---- loops ----------------------------------------------------------------------------------------
// A node that cannot leave a loop it is part of: an exec node with a single exec output that is not flow control (Branch, Delay,
// loops, Gate...), does not wait and does not call a function (which may end the task). An execution loop made only of such
// nodes runs until the step limit stops it.
FG_CORE_EXPORT bool isPlainExecNode(const CompiledNode& n);
// Would a new exec link from node `fromIndex` to node `toIndex` close a loop made only of plain exec nodes?
FG_CORE_EXPORT bool closesEndlessExecLoop(const CompiledGraph& g, int fromIndex, int toIndex);
// Would a new data link from node `fromIndex` (its output) to node `toIndex` (its input) close a cycle of pure nodes (a compile
// error: the values would depend on themselves)?
FG_CORE_EXPORT bool closesPureDataCycle(const CompiledGraph& g, int fromIndex, int toIndex);

// Display title of a node (function / variable / event name for the script level nodes).
FG_CORE_EXPORT std::string nodeTitle(const Node& node, const NodeDef* def);

} // namespace fg
