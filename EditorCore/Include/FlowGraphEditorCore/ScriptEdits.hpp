// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// What an editor does to the script's members, without a user interface: functions, event graphs, variables, the local
// variables of a function, structs - added, renamed (with what refers to them), duplicated, deleted. Each is one undoable
// step of the document called `label`, the editor's words.
#include <FlowGraphEditorCore/Document.hpp>

#include <string>

namespace fged::core {

// A member of the script, as an editor lists it.
enum class Member {
    EventGraph,
    Function,
    Variable,
    Local,                  // of a function: its owner
    Struct,
};

// Why a member cannot have a name.
enum class NameProblem {
    None,
    Empty,
    GraphExists,            // a function or an event graph has it
    VariableExists,
    LocalExists,            // a local variable of the same function
    NotAName,               // a struct's: not one word
    StructExists,
    TypeExists,             // a type of the application has it
};

class ScriptEdits {
public:
    explicit ScriptEdits(Document& doc) : doc_(doc) {}

    // A new member, its name made from `base` (one not taken: base, base_1...): its name. A function gets an entry node and a
    // return node; a variable is an int of 0; a struct has a field Value of int. A local needs a function (its owner).
    std::string add(Member member, const std::string& base, const std::string& label, const std::string& owner = {});

    NameProblem renameProblem(Member member, const std::string& owner, const std::string& from, const std::string& to) const;
    // Renamed, with every node that refers to it (calls, gets and sets, Make and Break...). False (and why) when it cannot be.
    bool rename(Member member, const std::string& owner, const std::string& from, const std::string& to, const std::string& label,
                NameProblem* why = nullptr);

    // A copy of a function, beside it (its name: name_Copy, ...): the copy's name; empty when there is no such function.
    std::string duplicateFunction(const std::string& name, const std::string& label);
    // Deleted (what refers to it stays: the compiler reports it). An event graph is not deleted: its events are the
    // application's.
    void remove(Member member, const std::string& owner, const std::string& name, const std::string& label);

private:
    Document& doc_;
};

} // namespace fged::core
