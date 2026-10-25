// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Search.hpp>
#include <FlowGraph/TypeExpr.hpp>

#include <algorithm>
#include <cctype>

namespace fg {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string titleOf(const Program* program, const std::string& graph, const Node& n) {
    if (program)
        if (const CompiledGraph* cg = program->findGraph(graph))
            if (const CompiledNode* cn = cg->findNode(n.id))
                if (!cn->title.empty()) return cn->title;
    return n.def;
}

bool isVariableNode(const Node& n) { return n.def == "var.get" || n.def == "var.set"; }

// The scope a Get / Set node resolves its variable in. An explicit scope is taken as it is; nodes without one look for a local
// variable first, then a parameter, then a variable of the script (like the compiler).
std::string resolvedScope(const Graph& g, const Node& n) {
    std::string scope = n.prop("scope");
    if (!scope.empty()) return scope;
    const std::string name = n.prop("name");
    if (g.findLocal(name)) return "local";
    for (auto& p : g.inputs)
        if (p.name == name) return "param";
    return "global";
}

// A type expression that names the struct: "Point", "array<Point>", "map<string,Point>".
bool mentions(const std::string& type, const std::string& structName) { return TypeExpr::parse(type).mentions(structName); }

// The declarations of the script that are of the struct's type, in the order My Script lists them.
void structDeclarations(const Script& script, const std::string& name, std::vector<ScriptRef>& out) {
    using D = ScriptRef::Declaration;
    for (auto& v : script.variables)
        if (mentions(v.type, name)) out.push_back({"", 0, v.name, "variable: " + v.type, D::Variable});
    for (auto& s : script.structs)
        for (auto& f : s.fields)
            if (mentions(f.type, name)) out.push_back({"", 0, s.name + "." + f.name, "field: " + f.type, D::Field});
    for (auto& g : script.graphs) {
        if (g.kind != GraphKind::Function) continue;
        for (auto& p : g.inputs)
            if (mentions(p.type, name)) out.push_back({g.name, 0, p.name, "input: " + p.type, D::Input});
        for (auto& p : g.outputs)
            if (mentions(p.type, name)) out.push_back({g.name, 0, p.name, "output: " + p.type, D::Output});
        for (auto& v : g.locals)
            if (mentions(v.type, name)) out.push_back({g.name, 0, v.name, "local variable: " + v.type, D::Local});
    }
}

// How a node uses the struct ("" when it does not): one of its nodes, or a type of the node that names it.
std::string structUse(const Node& n, const std::string& name) {
    if (n.prop("struct") == name) {
        if (n.def == "struct.make") return "Make";
        if (n.def == "struct.break") return "Break";
        if (n.def == "struct.set") return "Set " + n.prop("field");
    }
    for (auto& [param, type] : n.typeArgs)
        if (mentions(type, name)) return param + " = " + type;
    for (auto* pins : {&n.inputs, &n.outputs})
        for (auto& p : *pins)
            if (mentions(p.type, name)) return "pin " + p.name + ": " + p.type;
    return {};
}

} // namespace

std::string RefTarget::describe() const {
    switch (kind) {
    case Kind::Variable: return (graph.empty() ? "variable " : "local variable " + graph + ".") + name;
    case Kind::Function: return "function " + name;
    case Kind::Definition: return (name.rfind("fn:", 0) == 0 || name.rfind("m:", 0) == 0 ? name.substr(name.find(':') + 1) : name) + " nodes";
    case Kind::Struct: return "struct " + name;
    }
    return name;
}

RefTarget referenceTargetOf(const Script& script, const std::string& graph, const Node& node) {
    RefTarget t;
    if (isVariableNode(node)) {
        t.kind = RefTarget::Kind::Variable;
        t.name = node.prop("name");
        const Graph* g = script.findGraph(graph);
        if (g && resolvedScope(*g, node) != "global") t.graph = graph;
    } else if (node.def == "func.call") {
        t.kind = RefTarget::Kind::Function;
        t.name = node.prop("function");
    } else if (node.def.rfind("struct.", 0) == 0 && !node.prop("struct").empty()) {
        t.kind = RefTarget::Kind::Struct;
        t.name = node.prop("struct");
    } else {
        t.kind = RefTarget::Kind::Definition;
        t.name = node.def;
    }
    return t;
}

std::vector<ScriptRef> findReferences(const Script& script, const Program* program, const RefTarget& target) {
    std::vector<ScriptRef> out;
    if (target.kind == RefTarget::Kind::Struct) structDeclarations(script, target.name, out);      // first what is of its type
    for (const Graph& g : script.graphs) {
        if (target.kind == RefTarget::Kind::Variable && !target.graph.empty() && g.name != target.graph) continue;
        for (const Node& n : g.nodes) {
            std::string detail;
            switch (target.kind) {
            case RefTarget::Kind::Variable:
                if (!isVariableNode(n) || n.prop("name") != target.name) continue;
                if ((resolvedScope(g, n) == "global") != target.graph.empty()) continue;   // a local of the same name hides the variable
                detail = n.def == "var.get" ? "Get" : "Set";
                break;
            case RefTarget::Kind::Function:
                if (n.def != "func.call" || n.prop("function") != target.name) continue;
                detail = "Call";
                break;
            case RefTarget::Kind::Definition:
                if (n.def != target.name) continue;
                break;
            case RefTarget::Kind::Struct:
                detail = structUse(n, target.name);
                if (detail.empty()) continue;
                break;
            }
            out.push_back({g.name, n.id, titleOf(program, g.name, n), detail});
        }
    }
    return out;
}

std::vector<ScriptRef> searchScript(const Script& script, const Program* program, const std::string& text) {
    std::vector<ScriptRef> out;
    const std::string needle = lower(text);
    if (needle.empty()) return out;
    auto has = [&](const std::string& s) { return lower(s).find(needle) != std::string::npos; };
    for (const Graph& g : script.graphs) {
        for (const Node& n : g.nodes) {
            const std::string title = titleOf(program, g.name, n);
            std::string detail;
            if (has(title)) detail = "title";
            else if (has(n.def)) detail = "node " + n.def;
            else if (!n.comment.empty() && has(n.comment)) detail = "comment: " + n.comment;
            else {
                for (auto& [k, v] : n.props)
                    if (k != "sig" && has(v)) { detail = k + ": " + v; break; }
                if (detail.empty())
                    for (auto& [pin, v] : n.defaults) {
                        const std::string s = v.isString() ? v.str() : v.toString();
                        if (has(s)) { detail = pin + " = " + s; break; }
                    }
                if (detail.empty())
                    for (auto* pins : {&n.inputs, &n.outputs})
                        for (auto& p : *pins)
                            if (detail.empty() && has(p.name)) detail = "pin " + p.name;
            }
            if (!detail.empty()) out.push_back({g.name, n.id, title, detail});
        }
        for (const CommentBox& c : g.comments)
            if (has(c.text)) out.push_back({g.name, 0, c.text, "comment box"});
    }
    return out;
}

} // namespace fg
