// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// A reference of everything a registry offers to scripts - nodes, events, classes, enums - written from the descriptions the
// registry holds (titles, docs, pins, types, defaults). `flow --reference` writes the one of the standard applications.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>

#include <string>

namespace fg {

struct ReferenceOptions {
    std::string title = "FlowGraph node reference";
    std::string intro;                 // a paragraph under the title (Markdown)
    bool includeGenerated = true;      // nodes generated from classes and functions (fn:, m:, get:, set:, new:, cast:, enum:)
    bool includeHidden = false;        // nodes that are not offered in the palette (userCreatable == false)
};

// Markdown: a table of contents, then one section per category (nodes sorted by title), then events, classes and enums.
FG_CORE_EXPORT std::string nodeReferenceMarkdown(const Registry& reg, const ReferenceOptions& options = {});

} // namespace fg
