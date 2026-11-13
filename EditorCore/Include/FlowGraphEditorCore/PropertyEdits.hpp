// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// What a Details panel changes, without a user interface: a variable's type, default, category and description; a
// function's description and parameters; a struct's description and fields; a node's settings, type arguments, custom
// pins. Each is one undoable step of the document called `label` (the editor's words); edits of one value that follow
// each other quickly (typing) are one step.
//
// Parameters, fields and custom pins are edited as a list (setParams...): what the editor's table shows, as it is after
// a change. A row renamed takes its links and values with it (matched by its place in the list).
#include <FlowGraph/Script.hpp>
#include <FlowGraphEditorCore/Document.hpp>

#include <set>
#include <string>
#include <vector>

namespace fged::core {

// Why a list of parameters (fields, pins) cannot be.
enum class ListProblem {
    None,
    EmptyName,
    DuplicateName,          // two of the list, or one of the node's own pins
};

// A type expression split as an editor shows it: one value, an array of it, or a map from a key type to it.
struct TypeParts {
    enum class Container { Single, Array, Map };
    Container container = Container::Single;
    std::string key = "string";         // a map's
    std::string value = "int";          // the type itself, for a single value

    static TypeParts parse(const std::string& type);
    std::string str() const;
};

class PropertyEdits {
public:
    explicit PropertyEdits(Document& doc) : doc_(doc) {}

    // ---- variables: `owner` empty for the script's, a function's name for its local ----

    // Another type: its default, and the values typed into its Set nodes, are cleared (they were of the old one).
    void setVariableType(const std::string& owner, const std::string& name, const std::string& type, const std::string& label);
    void setVariableDefault(const std::string& owner, const std::string& name, const fg::Value& value, const std::string& label);
    void setVariableCategory(const std::string& owner, const std::string& name, const std::string& text, const std::string& label);
    void setVariableDoc(const std::string& owner, const std::string& name, const std::string& text, const std::string& label);

    // ---- graphs and functions ----

    void setGraphDoc(const std::string& graph, const std::string& text, const std::string& label);
    // A function's inputs (or outputs) as `params` (their defaults kept by name); a parameter renamed renames the pins of its
    // entry (return) node and of the calls, with their links.
    ListProblem setParams(const std::string& function, bool inputs, std::vector<fg::ParamDecl> params, const std::string& label);
    // A new int parameter at the end (Input, Input1...): its name.
    std::string addParam(const std::string& function, bool inputs, const std::string& label);
    // Removed, with the links of its pins.
    void removeParam(const std::string& function, bool inputs, std::size_t index, const std::string& label);

    // ---- structs ----

    void setStructDoc(const std::string& name, const std::string& text, const std::string& label);
    // The fields' names and types as `fields` (as many as the struct has): renamed and retyped with what uses them.
    ListProblem setFields(const std::string& name, const std::vector<fg::ParamDecl>& fields, const std::string& label);
    std::string addField(const std::string& name, const std::string& label);
    void removeField(const std::string& name, std::size_t index, const std::string& label);
    // `index` swapped with its neighbour (the order of the pins of Make and Break; their links are by name).
    void moveField(const std::string& name, std::size_t index, bool up, const std::string& label);
    void setFieldDefault(const std::string& name, const std::string& field, const fg::Value& value, const std::string& label);
    void setFieldDoc(const std::string& name, const std::string& field, const std::string& text, const std::string& label);

    // ---- nodes of a graph ----

    // A setting of the node (NodeDef::settings): `coalesce` for a value typed or stepped.
    void setNodeSetting(const std::string& graph, fg::NodeId node, const std::string& key, const std::string& value, const std::string& label,
                        bool coalesce = false);
    // A type argument (empty: chosen by the links).
    void setNodeTypeArg(const std::string& graph, fg::NodeId node, const std::string& param, const std::string& type, const std::string& label);
    // The field a Set Field node changes (its value cleared: it was of the old field's type).
    void setStructSetField(const std::string& graph, fg::NodeId node, const std::string& field, const std::string& label);
    // The custom pins (NodeDef::customInputs / customOutputs) as `pins`; `fixed`: the names of the node's other pins of that
    // direction, which a custom pin may not take. A pin renamed takes its links (and its value) with it.
    ListProblem setCustomPins(const std::string& graph, fg::NodeId node, bool inputs, std::vector<fg::ParamDecl> pins,
                              const std::set<std::string>& fixed, const std::string& label);
    std::string addCustomPin(const std::string& graph, fg::NodeId node, bool inputs, const std::set<std::string>& fixed, const std::string& label);
    void removeCustomPin(const std::string& graph, fg::NodeId node, bool inputs, std::size_t index, const std::string& label);
    void moveCustomPin(const std::string& graph, fg::NodeId node, bool inputs, std::size_t index, bool up, const std::string& label);

private:
    Document& doc_;
};

} // namespace fged::core
