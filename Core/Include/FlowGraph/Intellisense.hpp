// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Palette / context menu support for editors: which nodes can be created, which ones fit a pin the
// user is dragging from, and ranked text search.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>

namespace fg {

// Every node that can be created in the graph described by `ctx` (variables, functions, events and
// enum values are expanded into one entry each). Category paths use '|' as separator.
FG_CORE_EXPORT std::vector<NodeTemplate> buildPalette(const NodeBuildCtx& ctx);

// The pin a connection is being dragged from.
struct PinFilter {
    PinKind kind = PinKind::Data;
    PinDir dir = PinDir::Out;          // direction of the dragged pin
    const TypeDesc* type = nullptr;    // data pins only
};

// True when `t` has a pin that can be connected to the dragged pin; `pinName` receives the first match. `types`: those of the
// script (Program::types), so that its structs are found; null: the registry's only.
FG_CORE_EXPORT bool templateAccepts(const Registry& reg, const NodeTemplate& t, const PinFilter& f, std::string* pinName = nullptr,
                                    const TypeScope* types = nullptr);
FG_CORE_EXPORT std::vector<NodeTemplate> filterPalette(const Registry& reg, const std::vector<NodeTemplate>& all, const PinFilter& f,
                                                       const TypeScope* types = nullptr);

// Case insensitive search over title, keywords and category; every word of `query` must match.
// Results are ordered best match first. An empty query returns everything sorted by category and title.
FG_CORE_EXPORT std::vector<NodeTemplate> searchPalette(const std::vector<NodeTemplate>& all, const std::string& query);

// Creates a node in `graph` from a palette entry (type arguments and properties applied).
FG_CORE_EXPORT Node& instantiate(Graph& graph, const NodeTemplate& t, double x, double y);

} // namespace fg
