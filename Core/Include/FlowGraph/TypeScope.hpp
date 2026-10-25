// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// The types a program uses: those of the registry (shared by every script) and the structs its script declares (its own: two
// scripts may each have a "Point", and editing one changes nothing for the other). The compiler makes one for each program
// (Program::types); its struct types live as long as it - or as long as a value of them (StructData::type keeps them).
#include <FlowGraph/Export.hpp>
#include <FlowGraph/TypeExpr.hpp>
#include <FlowGraph/Types.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fg {

class Registry;
class Script;
struct StructDecl;

class FG_CORE_EXPORT TypeScope : public std::enable_shared_from_this<TypeScope> {
public:
    explicit TypeScope(const Registry& reg);

    // The scope of a script: its structs, checked. What is wrong (an unknown field type, a struct that contains itself...) goes to
    // `errors`, one message each; the struct is made all the same (a field of an unknown type is `any`).
    static std::shared_ptr<TypeScope> forScript(const Registry& reg, const Script& script, std::vector<std::string>* errors = nullptr);

    const Registry& registry() const { return *reg_; }

    // Like the registry's, with the structs of the script: "Point", "array<Point>", "map<string,Point>".
    const TypeDesc* findType(const std::string& expr) const;            // null when unknown
    const TypeDesc* resolveType(const TypeExpr& e) const;
    const TypeDesc* typeOrAny(const std::string& expr) const;
    const TypeDesc* arrayOf(const TypeDesc* elem) const;
    const TypeDesc* mapOf(const TypeDesc* key, const TypeDesc* val) const;

    const TypeDesc* findStruct(const std::string& name) const;
    const std::vector<const TypeDesc*>& structs() const { return structs_; }    // in the order the script declares them
    // What a variable can be: the registry's types (Registry::types()), then the structs.
    std::vector<const TypeDesc*> types() const;

private:
    void declare(const std::vector<StructDecl>& decls, std::vector<std::string>* errors);
    bool owns(const TypeDesc* t) const;          // one of its structs, or a container that holds one

    const Registry* reg_;
    mutable std::recursive_mutex mu_;            // containers are made when they are first asked for (also while scripts run)
    mutable std::deque<TypeDesc> types_;
    mutable std::unordered_map<std::string, TypeDesc*> byName_;
    mutable std::unordered_set<const TypeDesc*> owned_;
    std::vector<const TypeDesc*> structs_;
};

} // namespace fg
