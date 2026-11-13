// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraphEditorCore/ScriptEdits.hpp>

#include <algorithm>

namespace fged::core {

std::string ScriptEdits::add(Member member, const std::string& base, const std::string& label, const std::string& owner) {
    const fg::Script& s = doc_.script();
    std::string name;
    switch (member) {
    case Member::EventGraph:
    case Member::Function: name = s.uniqueGraphName(base); break;
    case Member::Variable: name = s.uniqueVariableName(base); break;
    case Member::Struct: name = s.uniqueStructName(base); break;
    case Member::Local: {
        const fg::Graph* g = s.findGraph(owner);
        if (!g || g->kind != fg::GraphKind::Function)
            return {};
        name = base;
        for (int i = 1; g->findLocal(name); ++i)
            name = base + "_" + std::to_string(i);
        break;
    }
    }
    doc_.edit(label, [&](fg::Script& sc) {
        switch (member) {
        case Member::EventGraph: sc.addEventGraph(name); break;
        case Member::Function: sc.addFunction(name); break;
        case Member::Variable: sc.variables.push_back({name, "int", fg::Value(int64_t(0)), "", ""}); break;
        case Member::Struct: sc.structs.push_back({name, "", "", {{"Value", "int", fg::Value(), ""}}}); break;
        case Member::Local:
            if (fg::Graph* g = sc.findGraph(owner))
                g->locals.push_back({name, "int", fg::Value(int64_t(0)), "", ""});
            break;
        }
    });
    return name;
}

NameProblem ScriptEdits::renameProblem(Member member, const std::string& owner, const std::string& from, const std::string& to) const {
    if (to.empty() || to.find_first_not_of(' ') == std::string::npos)
        return NameProblem::Empty;
    if (to == from)
        return NameProblem::None;
    const fg::Script& s = doc_.script();
    switch (member) {
    case Member::EventGraph:
    case Member::Function: return s.graphIndex(to) >= 0 ? NameProblem::GraphExists : NameProblem::None;
    case Member::Variable: return s.variableIndex(to) >= 0 ? NameProblem::VariableExists : NameProblem::None;
    case Member::Local: {
        const fg::Graph* g = s.findGraph(owner);
        return g && g->findLocal(to) ? NameProblem::LocalExists : NameProblem::None;
    }
    case Member::Struct:
        switch (doc_.structNameProblem(to, from)) {
        case StructNameProblem::None: return NameProblem::None;
        case StructNameProblem::NotAName: return NameProblem::NotAName;
        case StructNameProblem::StructExists: return NameProblem::StructExists;
        case StructNameProblem::TypeExists: return NameProblem::TypeExists;
        }
    }
    return NameProblem::None;
}

bool ScriptEdits::rename(Member member, const std::string& owner, const std::string& from, const std::string& to, const std::string& label,
                         NameProblem* why) {
    const NameProblem problem = renameProblem(member, owner, from, to);
    if (why)
        *why = problem;
    if (problem != NameProblem::None)
        return false;
    if (to == from)
        return true;
    doc_.edit(label, [&](fg::Script& sc) {
        switch (member) {
        case Member::EventGraph:
            if (fg::Graph* g = sc.findGraph(from))
                g->name = to;
            break;
        case Member::Function: sc.renameFunction(from, to); break;
        case Member::Variable: sc.renameVariable(from, to); break;
        case Member::Local: sc.renameLocal(owner, from, to); break;
        case Member::Struct: sc.renameStruct(from, to); break;
        }
    });
    return true;
}

std::string ScriptEdits::duplicateFunction(const std::string& name, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(name);
    if (!g || g->kind != fg::GraphKind::Function)
        return {};
    const std::string copyName = doc_.script().uniqueGraphName(name + "_Copy");
    doc_.edit(label, [&](fg::Script& s) {
        const fg::Graph* original = s.findGraph(name);
        if (!original)
            return;
        fg::Graph copy = *original;
        copy.name = copyName;
        s.graphs.push_back(std::move(copy));
    });
    return copyName;
}

void ScriptEdits::remove(Member member, const std::string& owner, const std::string& name, const std::string& label) {
    if (member == Member::EventGraph)
        return;
    doc_.edit(label, [&](fg::Script& s) {
        switch (member) {
        case Member::Function: s.removeGraph(name); break;
        case Member::Variable: std::erase_if(s.variables, [&](const fg::VariableDecl& v) { return v.name == name; }); break;
        case Member::Struct: std::erase_if(s.structs, [&](const fg::StructDecl& d) { return d.name == name; }); break;
        case Member::Local:
            if (fg::Graph* g = s.findGraph(owner))
                std::erase_if(g->locals, [&](const fg::VariableDecl& v) { return v.name == name; });
            break;
        case Member::EventGraph: break;
        }
    });
}

} // namespace fged::core
