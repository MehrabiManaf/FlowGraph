// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Finding things in a script: the nodes that use a variable, call a function, are of a definition or use a struct ("Find
// References"), and the nodes whose title, comment or values contain a text ("Find in Script").
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Program.hpp>

#include <string>
#include <vector>

namespace fg {

// A place in a script. `node` is 0 for a comment box (`title` is its text) - only the graph can be shown then - and for a
// declaration (`declaration` set).
struct ScriptRef {
    // A declaration that uses the target (a struct): a variable, local variable, input or output of a function, or a field.
    enum class Declaration : uint8_t { None, Variable, Local, Input, Output, Field };
    std::string graph;      // the function of a local variable, input or output; empty for a variable or a field
    NodeId node = 0;
    std::string title;      // the node's title as editors show it; the name of a declaration (Struct.Field for a field)
    std::string detail;     // what matched / how the node uses the target ("Get", "Set", "Call", "comment: ...", "variable: Point")
    Declaration declaration = Declaration::None;
};

// What to find the references of.
struct FG_CORE_EXPORT RefTarget {
    enum class Kind : uint8_t {
        Variable,     // `name`; `graph` empty = a variable of the script, otherwise a local variable or parameter of that function
        Function,     // `name` = a function graph: the nodes that call it
        Definition,   // `name` = a node definition id ("fn:Math.Sin", "flow.gate", "event:Tick"): every node of it
        Struct,       // `name` = a struct of the script: what is of its type (array<Point> too) and its Make / Break / Set nodes
    };
    Kind kind = Kind::Definition;
    std::string name;
    std::string graph;
    std::string describe() const;          // "variable Score", "function Add", "Math.Sin nodes", "struct Point"
};

// The target a node refers to: the variable of a Get / Set node, the function of a call node, the struct of a Make / Break / Set
// Field node, otherwise the node's definition.
FG_CORE_EXPORT RefTarget referenceTargetOf(const Script& script, const std::string& graph, const Node& node);

// `program` (may be null) gives the titles; without it the definition ids are used. Results are in script order.
FG_CORE_EXPORT std::vector<ScriptRef> findReferences(const Script& script, const Program* program, const RefTarget& target);
// Case insensitive search of node titles, definition ids, comments, settings and literal values, and of comment boxes.
FG_CORE_EXPORT std::vector<ScriptRef> searchScript(const Script& script, const Program* program, const std::string& text);

} // namespace fg
