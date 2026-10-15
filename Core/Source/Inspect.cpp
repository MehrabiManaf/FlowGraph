#include <FlowGraph/Inspect.hpp>
#include <FlowGraph/Registry.hpp>

namespace fg {

namespace {

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

const TypeDesc* dynamicType(const Registry& reg, const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Bool: return reg.tBool();
    case Value::Kind::Int: return reg.tInt();
    case Value::Kind::Float: return reg.tFloat();
    case Value::Kind::String: return reg.tString();
    case Value::Kind::Object: return v.objectPtr()->type;
    case Value::Kind::Array: return reg.arrayOf(v.arrayPtr()->elem);
    case Value::Kind::Map: return reg.mapOf(v.mapPtr()->key, v.mapPtr()->val);
    default: return nullptr;
    }
}

void fill(const Registry& reg, ValueView& out, const Value& v, const TypeDesc* declared, const InspectOptions& opt, int depth) {
    const TypeDesc* t = dynamicType(reg, v);
    if (!t) t = declared;
    out.type = t ? t->name : (declared ? declared->name : "null");
    switch (v.kind()) {
    case Value::Kind::Null: out.text = "null"; return;
    case Value::Kind::String: out.text = quoted(v.str()); return;
    case Value::Kind::Object: {
        const ObjectHandle* h = v.objectPtr();
        out.text = v.toString();
        if (depth >= opt.maxDepth || !h->type) return;
        size_t n = 0;
        for (const PropertyDesc* p : h->type->allProperties()) {
            if (n++ >= opt.maxItems) { out.truncated = true; break; }
            ValueView c;
            c.name = p->name;
            try {
                fill(reg, c, p->get(*h), p->type, opt, depth + 1);
            } catch (const std::exception& e) {
                c.type = p->type ? p->type->name : "";
                c.text = std::string("<error: ") + e.what() + ">";
            }
            out.children.push_back(std::move(c));
        }
        return;
    }
    case Value::Kind::Array: {
        auto* a = v.arrayPtr();
        out.text = "array[" + std::to_string(a->items.size()) + "]";
        if (depth >= opt.maxDepth) return;
        for (size_t i = 0; i < a->items.size(); ++i) {
            if (i >= opt.maxItems) { out.truncated = true; break; }
            ValueView c;
            c.name = "[" + std::to_string(i) + "]";
            fill(reg, c, a->items[i], a->elem, opt, depth + 1);
            out.children.push_back(std::move(c));
        }
        return;
    }
    case Value::Kind::Map: {
        auto* m = v.mapPtr();
        out.text = "map[" + std::to_string(m->items.size()) + "]";
        if (depth >= opt.maxDepth) return;
        size_t i = 0;
        for (auto& [k, val] : m->items) {
            if (i++ >= opt.maxItems) { out.truncated = true; break; }
            ValueView c;
            c.name = k.isString() ? quoted(k.str()) : reg.toString(k, m->key);
            fill(reg, c, val, m->val, opt, depth + 1);
            out.children.push_back(std::move(c));
        }
        return;
    }
    default:
        out.text = reg.toString(v, declared ? declared : t);
        return;
    }
}

} // namespace

ValueView inspect(const Registry& reg, const Value& v, const TypeDesc* declared, const InspectOptions& opt) {
    ValueView out;
    fill(reg, out, v, declared, opt, 0);
    return out;
}

std::string toText(const ValueView& v, int indent) {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    std::string s = pad;
    if (!v.name.empty()) s += v.name + " = ";
    s += v.text;
    if (!v.type.empty() && v.type != v.text) s += "  (" + v.type + ")";
    s += "\n";
    for (auto& c : v.children) s += toText(c, indent + 1);
    if (v.truncated) s += pad + "  ...\n";
    return s;
}

std::vector<VariableView> inspectGlobals(const Instance& inst, const InspectOptions& opt) {
    std::vector<VariableView> out;
    const Program& p = inst.program();
    for (size_t i = 0; i < p.variables.size() && i < inst.variableCount(); ++i) {
        VariableView vv;
        vv.scope = VariableView::Scope::Global;
        vv.value = inspect(inst.registry(), *inst.variableCell(i), p.variables[i].type, opt);
        vv.value.name = p.variables[i].decl.name;
        out.push_back(std::move(vv));
    }
    return out;
}

std::vector<VariableView> inspectFrame(const Task& task, size_t frameIndex, const InspectOptions& opt) {
    std::vector<VariableView> out;
    if (frameIndex >= task.depth()) return out;
    const Frame& f = task.frame(frameIndex);
    const Registry& reg = task.instance().registry();
    for (size_t i = 0; i < f.params.size() && i < f.graph->inputs.size(); ++i) {
        if (!f.params[i]) continue;
        VariableView vv;
        vv.scope = VariableView::Scope::Param;
        vv.value = inspect(reg, *f.params[i], f.graph->inputs[i].type, opt);
        vv.value.name = f.graph->inputs[i].decl.name;
        out.push_back(std::move(vv));
    }
    for (size_t i = 0; i < f.locals.size() && i < f.graph->locals.size(); ++i) {
        VariableView vv;
        vv.scope = VariableView::Scope::Local;
        vv.value = inspect(reg, *f.locals[i], f.graph->locals[i].type, opt);
        vv.value.name = f.graph->locals[i].decl.name;
        out.push_back(std::move(vv));
    }
    return out;
}

} // namespace fg
