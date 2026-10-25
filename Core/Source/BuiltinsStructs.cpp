// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// The nodes of the structs a script declares (Script::structs): Make <Struct> (a struct from its fields), Break <Struct> (its
// fields) and Set Field (changes one field in place). One definition each; Node::props "struct" (and "field") say which struct,
// and the pins come from the script's declaration. The palette offers them for every struct of the script.
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>
#include <FlowGraph/Vm.hpp>

namespace fg {

namespace {

const StructDecl* structOf(const Node& n, const NodeBuildCtx& c) {
    return c.script ? c.script->findStruct(n.prop("struct")) : nullptr;
}

PinDef documented(PinDef p, std::string doc) {
    p.doc = std::move(doc);
    return p;
}

PinDef fieldIn(const FieldDecl& f) {
    PinDef p = dataIn(f.name, f.type, f.def);
    p.doc = f.doc;
    return p;
}
PinDef fieldOut(const FieldDecl& f) {
    PinDef p = aliases(dataOut(f.name, f.type));             // a field that is a container or a struct is not copied
    p.doc = f.doc;
    return p;
}

// The struct of the first data input (Break, Set Field): a script error when there is none.
StructData& structArg(NodeCtx& c) {
    StructData* s = c.in(0).structPtr();
    if (!s) c.error("the struct is null");
    return *s;
}

// The "Struct" pin is one of the definition (its label and doc are translated like any other); it gets the struct's type here.
void typeStructPin(std::vector<PinDef>& pins, const StructDecl& s) {
    for (auto& p : pins)
        if (p.name == "Struct") p.type = s.name;
}

// A template of the palette for a struct of the script.
NodeTemplate structTemplate(const std::string& def, const StructDecl& s, const std::string& title, NodeStyle style) {
    NodeTemplate t;
    t.defId = def;
    t.props["struct"] = s.name;
    t.title = title;
    t.category = "Structs|" + s.name;
    t.keywords = "struct record " + s.name;
    t.doc = s.doc;
    t.style = style;
    return t;
}

} // namespace

void registerStructNodes(Registry& r) {
    {
        NodeDef d;
        d.id = "struct.make";
        d.title = "Make Struct";
        d.category = "Structs";
        d.doc = "A struct from the values of its fields.";
        d.pure = true;
        d.style = NodeStyle::Pure;
        d.pins.push_back(documented(dataOut("Struct", "any"), "The new struct."));
        d.dynamicPins = [](const Node& n, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            const StructDecl* s = structOf(n, c);
            if (!s) return;
            typeStructPin(pins, *s);
            for (auto& f : s->fields) pins.push_back(fieldIn(f));
        };
        d.expand = [](const NodeBuildCtx& c, std::vector<NodeTemplate>& out) {
            if (!c.script) return;
            for (auto& s : c.script->structs) {
                NodeTemplate t = structTemplate("struct.make", s, "Make " + s.name, NodeStyle::Pure);
                t.pins.push_back(dataOut("Struct", s.name));
                for (auto& f : s.fields) t.pins.push_back(fieldIn(f));
                out.push_back(std::move(t));
            }
        };
        d.run = [](NodeCtx& c) {
            const CompiledNode& n = c.node();
            const TypeDesc* t = n.dataOut.empty() ? nullptr : n.pins[static_cast<size_t>(n.dataOut[0])].type;
            if (!t || !t->isStruct()) c.error("the struct '" + c.prop("struct") + "' is not declared");
            auto s = std::make_shared<StructData>();
            s->type = std::shared_ptr<const TypeDesc>(t->owner.lock(), t);
            s->fields.reserve(c.inputCount());
            for (size_t i = 0; i < c.inputCount(); ++i) s->fields.push_back(c.in(i).deepCopy());
            c.out(0, Value(std::move(s)));
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d;
        d.id = "struct.break";
        d.title = "Break Struct";
        d.category = "Structs";
        d.doc = "The values of the fields of a struct.";
        d.pure = true;
        d.style = NodeStyle::Pure;
        d.pins.push_back(documented(reads(dataIn("Struct", "any")), "The struct whose fields are read."));
        d.dynamicPins = [](const Node& n, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            const StructDecl* s = structOf(n, c);
            if (!s) return;
            typeStructPin(pins, *s);
            for (auto& f : s->fields) pins.push_back(fieldOut(f));
        };
        d.expand = [](const NodeBuildCtx& c, std::vector<NodeTemplate>& out) {
            if (!c.script) return;
            for (auto& s : c.script->structs) {
                NodeTemplate t = structTemplate("struct.break", s, "Break " + s.name, NodeStyle::Pure);
                t.pins.push_back(reads(dataIn("Struct", s.name)));
                for (auto& f : s.fields) t.pins.push_back(fieldOut(f));
                out.push_back(std::move(t));
            }
        };
        d.run = [](NodeCtx& c) {
            const StructData& s = structArg(c);
            for (size_t i = 0; i < s.fields.size(); ++i) c.out(i, s.fields[i]);
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d;
        d.id = "struct.set";
        d.title = "Set Field";
        d.category = "Structs";
        d.doc = "Changes one field of a struct, in place: the struct of the variable (or of the array element...) it is connected to.";
        d.mutatesFirstInput = true;
        d.pins.push_back(execIn());
        d.pins.push_back(execOut());
        d.pins.push_back(documented(modifies(dataIn("Struct", "any")), "The struct that is changed: a variable, an element of an array..."));
        d.dynamicPins = [](const Node& n, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            const StructDecl* s = structOf(n, c);
            if (!s) return;
            typeStructPin(pins, *s);
            if (const FieldDecl* f = s->findField(n.prop("field"))) {
                PinDef v = dataIn("Value", f->type, f->def);
                v.doc = f->doc;
                pins.push_back(std::move(v));
            }
        };
        d.expand = [](const NodeBuildCtx& c, std::vector<NodeTemplate>& out) {
            if (!c.script) return;
            for (auto& s : c.script->structs)
                for (auto& f : s.fields) {
                    NodeTemplate t = structTemplate("struct.set", s, "Set " + s.name + "." + f.name, NodeStyle::Function);
                    t.props["field"] = f.name;
                    t.keywords += " set field " + f.name;
                    t.pins = {execIn(), execOut(), modifies(dataIn("Struct", s.name)), dataIn("Value", f.type, f.def)};
                    out.push_back(std::move(t));
                }
        };
        d.run = [](NodeCtx& c) {
            StructData& s = structArg(c);
            const int i = s.type ? s.type->fieldIndex(c.prop("field")) : -1;
            if (i < 0) c.error("the struct '" + c.prop("struct") + "' has no field '" + c.prop("field") + "'");
            s.fields[static_cast<size_t>(i)] = c.in(1).deepCopy();
        };
        r.addNode(std::move(d));
    }
}

} // namespace fg
