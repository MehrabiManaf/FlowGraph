// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Changes to a script that keep what it does: the editor's refactoring commands.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>

#include <string>
#include <vector>

namespace fg {

struct CollapseResult {
    bool ok = false;
    std::string error;                     // why nothing was changed
    std::vector<std::string> warnings;     // changed, but read this
    std::string function;                  // the new function graph
    NodeId callNode = 0;                   // the node in `graph` that calls it, where the selection was
};

// Collapse to Function: moves the nodes `nodes` of `graph` into a new function graph called `functionName` (made unique) and puts a node
// that calls it in their place.
//   - data links that come into the selection become inputs of the function, links that leave it become outputs (set at its Return);
//   - the execution must enter the selection at one input pin and leave it towards one place: that is the call node's In and Then;
//   - event, function entry and return nodes cannot be moved, nor nodes that use the local variables or parameters of their function.
// On failure the script is not changed.
FG_CORE_EXPORT CollapseResult collapseToFunction(Script& script, const Registry& reg, const std::string& graph, const std::vector<NodeId>& nodes,
                                  const std::string& functionName = "NewFunction");

} // namespace fg
