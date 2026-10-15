#pragma once
// Built-in nodes: script level nodes (events, functions, variables), flow control, math, strings,
// containers and utilities.
#include <FlowGraph/Registry.hpp>

namespace fg {

void registerBuiltins(Registry& reg);

} // namespace fg
