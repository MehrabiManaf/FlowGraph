#pragma once
// Built-in nodes: script level nodes (events, functions, variables), flow control, math, strings,
// containers and utilities.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>

namespace fg {

FG_CORE_EXPORT void registerBuiltins(Registry& reg);

} // namespace fg
