// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraphEditorCore/PropertyEdits.hpp>

#include <FlowGraph/TypeExpr.hpp>

#include <algorithm>

namespace fged::core {

namespace {

fg::VariableDecl* variableOf(fg::Script& s, const std::string& owner, const std::string& name) {
    if (owner.empty())
        return s.findVariable(name);
    fg::Graph* g = s.findGraph(owner);
    return g ? g->findLocal(name) : nullptr;
}

std::string keyOf(const char* what, const std::string& owner, const std::string& name) {
    return std::string(what) + ":" + owner + "/" + name;
}

ListProblem problemOf(const std::vector<fg::ParamDecl>& list, std::set<std::string> taken) {
    for (const fg::ParamDecl& p : list) {
        if (p.name.empty() || p.name.find_first_not_of(' ') == std::string::npos)
            return ListProblem::EmptyName;
        if (!taken.insert(p.name).second)
            return ListProblem::DuplicateName;
    }
    return ListProblem::None;
}

// A name no member has, for one being renamed while another takes its name.
std::string temporaryName(std::size_t i) {
    return "" + std::to_string(i);
}

// A name from `base` that `taken` says is free: base, base1, base2...
template <typename Taken>
std::string freeName(const std::string& base, Taken taken) {
    std::string n = base;
    for (int i = 1; taken(n); ++i)
        n = base + std::to_string(i);
    return n;
}

// A parameter of a function renamed (`to`), or removed (`to` empty): the pins of its entry / return node and of its calls
// follow, with their links and the values typed into them.
void moveParamPins(fg::Script& s, const std::string& function, bool inputs, const std::string& from, const std::string& to) {
    const auto onNode = [&](fg::Graph& g, fg::NodeId id, bool asOutput) {
        auto& links = g.connections;
        if (to.empty()) {
            links.erase(std::remove_if(links.begin(), links.end(), [&](const fg::Connection& c) {
                            return asOutput ? (c.fromNode == id && c.fromPin == from) : (c.toNode == id && c.toPin == from);
                        }),
                        links.end());
        } else {
            for (fg::Connection& c : links) {
                if (asOutput && c.fromNode == id && c.fromPin == from)
                    c.fromPin = to;
                if (!asOutput && c.toNode == id && c.toPin == from)
                    c.toPin = to;
            }
        }
        if (!asOutput)
            if (fg::Node* n = g.findNode(id)) {
                auto it = n->defaults.find(from);
                if (it != n->defaults.end()) {
                    if (!to.empty())
                        n->defaults[to] = it->second;
                    n->defaults.erase(from);
                }
            }
    };
    for (fg::Graph& g : s.graphs)
        for (const fg::Node& n : g.nodes) {
            if (g.name == function && inputs && n.def == "func.entry")
                onNode(g, n.id, true);
            if (g.name == function && !inputs && n.def == "func.return")
                onNode(g, n.id, false);
            if (n.def == "func.call" && n.prop("function") == function)
                onNode(g, n.id, !inputs);
        }
}

// A custom pin of a node renamed (`to`), or removed (`to` empty): its links and its value follow.
void moveNodePin(fg::Graph& g, fg::NodeId id, bool inputs, const std::string& from, const std::string& to) {
    auto& links = g.connections;
    if (to.empty()) {
        links.erase(std::remove_if(links.begin(), links.end(), [&](const fg::Connection& c) {
                        return inputs ? (c.toNode == id && c.toPin == from) : (c.fromNode == id && c.fromPin == from);
                    }),
                    links.end());
    } else {
        for (fg::Connection& c : links) {
            if (inputs && c.toNode == id && c.toPin == from)
                c.toPin = to;
            if (!inputs && c.fromNode == id && c.fromPin == from)
                c.fromPin = to;
        }
    }
    if (inputs)
        if (fg::Node* n = g.findNode(id)) {
            auto it = n->defaults.find(from);
            if (it != n->defaults.end()) {
                if (!to.empty())
                    n->defaults[to] = it->second;
                n->defaults.erase(from);
            }
        }
}

} // namespace

TypeParts TypeParts::parse(const std::string& type) {
    TypeParts parts;
    const fg::TypeExpr e = fg::TypeExpr::parse(type);
    if (e.valid && e.isArray()) {
        parts.container = Container::Array;
        parts.value = e.args[0].str();
    } else if (e.valid && e.isMap()) {
        parts.container = Container::Map;
        parts.key = e.args[0].str();
        parts.value = e.args[1].str();
    } else {
        parts.value = type;
    }
    return parts;
}

std::string TypeParts::str() const {
    switch (container) {
    case Container::Array: return "array<" + value + ">";
    case Container::Map: return "map<" + key + "," + value + ">";
    case Container::Single: break;
    }
    return value;
}

// ---- variables

void PropertyEdits::setVariableType(const std::string& owner, const std::string& name, const std::string& type, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (owner.empty())
            s.retypeVariable(name, type);
        else
            s.retypeLocal(owner, name, type);
    });
}

void PropertyEdits::setVariableDefault(const std::string& owner, const std::string& name, const fg::Value& value, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::VariableDecl* v = variableOf(s, owner, name))
            v->def = value;
    }, EditKind::Structural, keyOf("vardef", owner, name));
}

void PropertyEdits::setVariableCategory(const std::string& owner, const std::string& name, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::VariableDecl* v = variableOf(s, owner, name))
            v->category = text;
    }, EditKind::Structural, keyOf("varcat", owner, name));
}

void PropertyEdits::setVariableDoc(const std::string& owner, const std::string& name, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::VariableDecl* v = variableOf(s, owner, name))
            v->doc = text;
    }, EditKind::Structural, keyOf("vardoc", owner, name));
}

// ---- graphs and functions

void PropertyEdits::setGraphDoc(const std::string& graph, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = s.findGraph(graph))
            g->doc = text;
    }, EditKind::Structural, keyOf("graphdoc", {}, graph));
}

ListProblem PropertyEdits::setParams(const std::string& function, bool inputs, std::vector<fg::ParamDecl> params, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(function);
    if (!g)
        return ListProblem::None;
    if (const ListProblem problem = problemOf(params, {}); problem != ListProblem::None)
        return problem;
    const std::vector<fg::ParamDecl> old = inputs ? g->inputs : g->outputs;
    for (fg::ParamDecl& p : params)         // the defaults of those still there
        for (const fg::ParamDecl& o : old)
            if (o.name == p.name)
                p.def = o.def;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* gr = s.findGraph(function);
        if (!gr)
            return;
        if (old.size() == params.size()) {
            // two renamed into each other's names: through names no pin has
            for (std::size_t i = 0; i < params.size(); ++i)
                if (old[i].name != params[i].name)
                    moveParamPins(s, function, inputs, old[i].name, temporaryName(i));
            for (std::size_t i = 0; i < params.size(); ++i)
                if (old[i].name != params[i].name)
                    moveParamPins(s, function, inputs, temporaryName(i), params[i].name);
        }
        (inputs ? gr->inputs : gr->outputs) = params;
    });
    return ListProblem::None;
}

std::string PropertyEdits::addParam(const std::string& function, bool inputs, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(function);
    if (!g)
        return {};
    const auto& list = inputs ? g->inputs : g->outputs;
    const std::string name = freeName(inputs ? "Input" : "Output", [&](const std::string& n) {
        return std::any_of(list.begin(), list.end(), [&](const fg::ParamDecl& p) { return p.name == n; });
    });
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* gr = s.findGraph(function))
            (inputs ? gr->inputs : gr->outputs).push_back({name, "int", false, false, fg::Value()});
    });
    return name;
}

void PropertyEdits::removeParam(const std::string& function, bool inputs, std::size_t index, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(function);
    if (!g || index >= (inputs ? g->inputs : g->outputs).size())
        return;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* gr = s.findGraph(function);
        auto& list = inputs ? gr->inputs : gr->outputs;
        moveParamPins(s, function, inputs, list[index].name, {});
        list.erase(list.begin() + std::ptrdiff_t(index));
    });
}

// ---- structs

void PropertyEdits::setStructDoc(const std::string& name, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::StructDecl* st = s.findStruct(name))
            st->doc = text;
    }, EditKind::Structural, keyOf("structdoc", {}, name));
}

ListProblem PropertyEdits::setFields(const std::string& name, const std::vector<fg::ParamDecl>& fields, const std::string& label) {
    const fg::StructDecl* st = doc_.script().findStruct(name);
    if (!st || st->fields.size() != fields.size())
        return ListProblem::None;
    if (const ListProblem problem = problemOf(fields, {}); problem != ListProblem::None)
        return problem;
    const std::vector<fg::FieldDecl> old = st->fields;
    bool changed = false;
    for (std::size_t i = 0; i < fields.size(); ++i)
        changed = changed || old[i].name != fields[i].name || old[i].type != fields[i].type;
    if (!changed)
        return ListProblem::None;
    doc_.edit(label, [&](fg::Script& s) {
        // two renamed into each other's names: through names no field has
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (old[i].name != fields[i].name)
                s.renameField(name, old[i].name, "\x01" + std::to_string(i));
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (old[i].name != fields[i].name)
                s.renameField(name, "\x01" + std::to_string(i), fields[i].name);
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (old[i].type != fields[i].type)
                s.retypeField(name, fields[i].name, fields[i].type);
    });
    return ListProblem::None;
}

std::string PropertyEdits::addField(const std::string& name, const std::string& label) {
    const fg::StructDecl* st = doc_.script().findStruct(name);
    if (!st)
        return {};
    const std::string field = freeName("Field", [&](const std::string& n) { return st->findField(n) != nullptr; });
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::StructDecl* d = s.findStruct(name))
            d->fields.push_back({field, "int", fg::Value(), ""});
    });
    return field;
}

void PropertyEdits::removeField(const std::string& name, std::size_t index, const std::string& label) {
    const fg::StructDecl* st = doc_.script().findStruct(name);
    if (!st || index >= st->fields.size())
        return;
    const std::string field = st->fields[index].name;
    doc_.edit(label, [&](fg::Script& s) { s.removeField(name, field); });
}

void PropertyEdits::moveField(const std::string& name, std::size_t index, bool up, const std::string& label) {
    const fg::StructDecl* st = doc_.script().findStruct(name);
    if (!st || index >= st->fields.size() || (up && index == 0) || (!up && index + 1 >= st->fields.size()))
        return;
    const std::size_t to = up ? index - 1 : index + 1;
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::StructDecl* d = s.findStruct(name))
            std::swap(d->fields[index], d->fields[to]);
    });
}

void PropertyEdits::setFieldDefault(const std::string& name, const std::string& field, const fg::Value& value, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::StructDecl* d = s.findStruct(name))
            if (fg::FieldDecl* f = d->findField(field))
                f->def = value;
    }, EditKind::Structural, keyOf("fielddef", name, field));
}

void PropertyEdits::setFieldDoc(const std::string& name, const std::string& field, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::StructDecl* d = s.findStruct(name))
            if (fg::FieldDecl* f = d->findField(field))
                f->doc = text;
    }, EditKind::Structural, keyOf("fielddoc", name, field));
}

// ---- nodes

void PropertyEdits::setNodeSetting(const std::string& graph, fg::NodeId node, const std::string& key, const std::string& value,
                                   const std::string& label, bool coalesce) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = s.findGraph(graph))
            if (fg::Node* n = g->findNode(node))
                n->props[key] = value;
    }, EditKind::Structural, coalesce ? keyOf("setting", graph, std::to_string(node) + "." + key) : std::string());
}

void PropertyEdits::setNodeTypeArg(const std::string& graph, fg::NodeId node, const std::string& param, const std::string& type,
                                   const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = s.findGraph(graph))
            if (fg::Node* n = g->findNode(node)) {
                if (type.empty())
                    n->typeArgs.erase(param);
                else
                    n->typeArgs[param] = type;
            }
    });
}

void PropertyEdits::setStructSetField(const std::string& graph, fg::NodeId node, const std::string& field, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = s.findGraph(graph))
            if (fg::Node* n = g->findNode(node)) {
                n->props["field"] = field;
                n->defaults.erase("Value");
            }
    });
}

ListProblem PropertyEdits::setCustomPins(const std::string& graph, fg::NodeId node, bool inputs, std::vector<fg::ParamDecl> pins,
                                         const std::set<std::string>& fixed, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(graph);
    const fg::Node* n = g ? g->findNode(node) : nullptr;
    if (!n)
        return ListProblem::None;
    if (const ListProblem problem = problemOf(pins, fixed); problem != ListProblem::None)
        return problem;
    const std::vector<fg::ParamDecl> old = inputs ? n->inputs : n->outputs;
    for (std::size_t i = 0; i < pins.size(); ++i) {      // the value of a pin that is still the same
        if (old.size() == pins.size()) {
            pins[i].def = old[i].def;
            continue;
        }
        for (const fg::ParamDecl& o : old)
            if (o.name == pins[i].name)
                pins[i].def = o.def;
    }
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* gr = s.findGraph(graph);
        fg::Node* nn = gr ? gr->findNode(node) : nullptr;
        if (!nn)
            return;
        if (old.size() == pins.size()) {
            for (std::size_t i = 0; i < pins.size(); ++i)
                if (old[i].name != pins[i].name)
                    moveNodePin(*gr, node, inputs, old[i].name, temporaryName(i));
            for (std::size_t i = 0; i < pins.size(); ++i)
                if (old[i].name != pins[i].name)
                    moveNodePin(*gr, node, inputs, temporaryName(i), pins[i].name);
        }
        (inputs ? nn->inputs : nn->outputs) = pins;
    });
    return ListProblem::None;
}

std::string PropertyEdits::addCustomPin(const std::string& graph, fg::NodeId node, bool inputs, const std::set<std::string>& fixed,
                                        const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(graph);
    const fg::Node* n = g ? g->findNode(node) : nullptr;
    if (!n)
        return {};
    const auto& list = inputs ? n->inputs : n->outputs;
    const std::string name = freeName(inputs ? "Input" : "Output", [&](const std::string& x) {
        return fixed.count(x) > 0 || std::any_of(list.begin(), list.end(), [&](const fg::ParamDecl& p) { return p.name == x; });
    });
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* gr = s.findGraph(graph))
            if (fg::Node* nn = gr->findNode(node))
                (inputs ? nn->inputs : nn->outputs).push_back({name, "any", false, false, fg::Value()});
    });
    return name;
}

void PropertyEdits::removeCustomPin(const std::string& graph, fg::NodeId node, bool inputs, std::size_t index, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(graph);
    const fg::Node* n = g ? g->findNode(node) : nullptr;
    if (!n || index >= (inputs ? n->inputs : n->outputs).size())
        return;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* gr = s.findGraph(graph);
        fg::Node* nn = gr->findNode(node);
        auto& list = inputs ? nn->inputs : nn->outputs;
        moveNodePin(*gr, node, inputs, list[index].name, {});
        list.erase(list.begin() + std::ptrdiff_t(index));
    });
}

void PropertyEdits::moveCustomPin(const std::string& graph, fg::NodeId node, bool inputs, std::size_t index, bool up, const std::string& label) {
    const fg::Graph* g = doc_.script().findGraph(graph);
    const fg::Node* n = g ? g->findNode(node) : nullptr;
    const std::size_t count = n ? (inputs ? n->inputs : n->outputs).size() : 0;
    if (index >= count || (up && index == 0) || (!up && index + 1 >= count))
        return;
    const std::size_t to = up ? index - 1 : index + 1;
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* gr = s.findGraph(graph))
            if (fg::Node* nn = gr->findNode(node)) {
                auto& list = inputs ? nn->inputs : nn->outputs;
                std::swap(list[index], list[to]);
            }
    });
}

} // namespace fged::core
