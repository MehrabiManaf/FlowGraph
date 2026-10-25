// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Inspect.hpp>
#include <FlowGraph/Registry.hpp>

#include <algorithm>
#include <cctype>
#include <deque>
#include <iterator>

namespace fg {

namespace {

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

// ["text"] with \" and \\ escaped
std::string quotedKeyPath(const std::string& s) {
    std::string out = "[\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"]";
}

const TypeDesc* dynamicType(const Registry& reg, const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Bool: return reg.tBool();
    case Value::Kind::Int: return reg.tInt();
    case Value::Kind::Float: return reg.tFloat();
    case Value::Kind::String: return reg.tString();
    case Value::Kind::Bytes: return reg.tBytes();
    case Value::Kind::Struct: return v.structPtr()->type.get();
    case Value::Kind::Object: return v.objectPtr()->type;
    case Value::Kind::Array: return reg.arrayOf(v.arrayPtr()->elem);
    case Value::Kind::Map: return reg.mapOf(v.mapPtr()->key, v.mapPtr()->val);
    default: return nullptr;
    }
}

void fill(const Registry& reg, ValueView& out, const Value& v, const TypeDesc* declared, const InspectOptions& opt, int depth, const std::string& path);

// Collects the children [from, to) of a container or object. `childDepth` is the depth of the children.
void fillChildren(const Registry& reg, ValueView& out, const Value& v, const InspectOptions& opt, int childDepth, const std::string& path, size_t from, size_t to) {
    out.offset = from;
    switch (v.kind()) {
    case Value::Kind::Object: {
        const ObjectHandle* h = v.objectPtr();
        if (!h->type) return;
        auto props = h->type->allProperties();
        to = std::min(to, props.size());
        for (size_t i = from; i < to; ++i) {
            const PropertyDesc* p = props[i];
            ValueView c;
            c.name = p->name;
            std::string childPath = path.empty() ? std::string() : path + "." + p->name;
            try {
                fill(reg, c, p->get(*h), p->type, opt, childDepth, childPath);
            } catch (const std::exception& e) {
                c.type = p->type ? p->type->name : "";
                c.text = std::string("<error: ") + e.what() + ">";
                c.path = childPath;
            }
            out.children.push_back(std::move(c));
        }
        out.truncated = to < props.size();
        return;
    }
    case Value::Kind::Array: {
        auto* a = v.arrayPtr();
        to = std::min(to, a->items.size());
        for (size_t i = from; i < to; ++i) {
            ValueView c;
            c.name = "[" + std::to_string(i) + "]";
            fill(reg, c, a->items[i], a->elem, opt, childDepth, path.empty() ? std::string() : path + c.name);
            out.children.push_back(std::move(c));
        }
        out.truncated = to < a->items.size();
        return;
    }
    case Value::Kind::Map: {
        auto* m = v.mapPtr();
        to = std::min(to, m->items.size());
        if (from >= to) return;
        auto it = m->items.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(from));
        for (size_t i = from; i < to; ++i, ++it) {
            const Value& k = it->first;
            ValueView c;
            c.name = k.isString() ? quoted(k.str()) : reg.toString(k, m->key);
            std::string childPath;
            if (!path.empty()) {
                if (k.isString()) childPath = path + quotedKeyPath(k.str());
                else if (k.isInt()) childPath = path + "[" + std::to_string(k.toInt()) + "]";
            }
            fill(reg, c, it->second, m->val, opt, childDepth, childPath);
            out.children.push_back(std::move(c));
        }
        out.truncated = to < m->items.size();
        return;
    }
    case Value::Kind::Struct: {                              // its fields, by name
        const StructData* d = v.structPtr();
        if (!d->type) return;
        to = std::min(to, d->fields.size());
        for (size_t i = from; i < to && i < d->type->fields.size(); ++i) {
            const FieldDesc& f = d->type->fields[i];
            ValueView c;
            c.name = f.name;
            fill(reg, c, d->fields[i], f.type, opt, childDepth, path.empty() ? std::string() : path + "." + f.name);
            out.children.push_back(std::move(c));
        }
        out.truncated = to < d->fields.size();
        return;
    }
    default: return;
    }
}

void fill(const Registry& reg, ValueView& out, const Value& v, const TypeDesc* declared, const InspectOptions& opt, int depth, const std::string& path) {
    const TypeDesc* t = dynamicType(reg, v);
    if (!t || (declared && declared->kind == TypeKind::Byte && v.isInt())) t = declared;   // a byte is held as an int
    out.type = t ? t->name : (declared ? declared->name : "null");
    out.path = path;
    switch (v.kind()) {
    case Value::Kind::Null: out.text = "null"; return;
    case Value::Kind::String: out.text = quoted(v.str()); return;
    case Value::Kind::Object: {
        const ObjectHandle* h = v.objectPtr();
        out.text = v.toString();
        if (!h->type) return;
        out.total = h->type->allProperties().size();
        if (depth >= opt.maxDepth) return;
        fillChildren(reg, out, v, opt, depth + 1, path, 0, opt.maxItems);
        return;
    }
    case Value::Kind::Array:
        out.total = v.arrayPtr()->items.size();
        out.text = "array[" + std::to_string(out.total) + "]";
        if (depth >= opt.maxDepth) return;
        fillChildren(reg, out, v, opt, depth + 1, path, 0, opt.maxItems);
        return;
    case Value::Kind::Map:
        out.total = v.mapPtr()->items.size();
        out.text = "map[" + std::to_string(out.total) + "]";
        if (depth >= opt.maxDepth) return;
        fillChildren(reg, out, v, opt, depth + 1, path, 0, opt.maxItems);
        return;
    case Value::Kind::Struct: {                              // Point{X: 1, Y: 2}, or its name when that is long
        out.total = v.structPtr()->fields.size();
        out.text = v.toString();
        if (out.text.size() > 80) out.text = out.type;
        if (depth >= opt.maxDepth) return;
        fillChildren(reg, out, v, opt, depth + 1, path, 0, opt.maxItems);
        return;
    }
    default:
        out.text = reg.toString(v, declared ? declared : t);
        return;
    }
}

// ------------------------------------------------------------------ expressions
struct Step {
    enum class Kind { Property, Index, Key, Range } kind = Kind::Property;
    std::string name;                    // Property: the name; Key: the text
    int64_t index = 0;                   // Index
    bool hasFrom = false, hasTo = false; // Range
    int64_t from = 0, to = 0;            // Range (inclusive)
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool parseInt(const std::string& text, int64_t& out) {
    std::string t = trim(text);
    if (t.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (t[0] == '-' || t[0] == '+') { neg = t[0] == '-'; i = 1; }
    if (i >= t.size() || t.size() - i > 18) return false;
    int64_t v = 0;
    for (; i < t.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        v = v * 10 + (t[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

bool parseExpression(const std::string& expr, std::string& root, std::vector<Step>& steps, std::string& error) {
    size_t i = 0, n = expr.size();
    auto readName = [&] {
        size_t start = i;
        while (i < n && expr[i] != '.' && expr[i] != '[') ++i;
        return trim(expr.substr(start, i - start));
    };
    root = readName();
    if (root.empty()) { error = "expected a variable name"; return false; }
    while (i < n) {
        if (std::isspace(static_cast<unsigned char>(expr[i]))) { ++i; continue; }
        if (expr[i] == '.') {
            ++i;
            Step s;
            s.kind = Step::Kind::Property;
            s.name = readName();
            if (s.name.empty()) { error = "expected a property name after '.'"; return false; }
            steps.push_back(std::move(s));
        } else if (expr[i] == '[') {
            ++i;
            size_t start = i;
            bool inString = false;
            while (i < n && (inString || expr[i] != ']')) {
                if (expr[i] == '\\' && inString && i + 1 < n) ++i;
                else if (expr[i] == '"') inString = !inString;
                ++i;
            }
            if (i >= n) { error = "missing ']'"; return false; }
            std::string content = trim(expr.substr(start, i - start));
            ++i;
            Step s;
            if (!content.empty() && content[0] == '"') {
                if (content.size() < 2 || content.back() != '"') { error = "unterminated string"; return false; }
                s.kind = Step::Kind::Key;
                for (size_t k = 1; k + 1 < content.size(); ++k) {
                    if (content[k] == '\\' && k + 2 < content.size()) ++k;
                    s.name += content[k];
                }
            } else if (size_t dots = content.find(".."); dots != std::string::npos) {
                s.kind = Step::Kind::Range;
                std::string a = trim(content.substr(0, dots)), b = trim(content.substr(dots + 2));
                if (!a.empty()) { if (!parseInt(a, s.from)) { error = "'" + a + "' is not a number"; return false; } s.hasFrom = true; }
                if (!b.empty()) { if (!parseInt(b, s.to)) { error = "'" + b + "' is not a number"; return false; } s.hasTo = true; }
            } else if (parseInt(content, s.index)) {
                s.kind = Step::Kind::Index;
            } else if (!content.empty()) {
                s.kind = Step::Kind::Key;       // a bare word is a string key
                s.name = content;
            } else {
                error = "empty []";
                return false;
            }
            steps.push_back(std::move(s));
        } else {
            error = std::string("unexpected '") + expr[i] + "'";
            return false;
        }
    }
    for (size_t k = 0; k + 1 < steps.size(); ++k)
        if (steps[k].kind == Step::Kind::Range) { error = "a range can only be the last step"; return false; }
    return true;
}

std::string sizeText(size_t n) { return std::to_string(n); }

} // namespace

ValueView inspect(const Registry& reg, const Value& v, const TypeDesc* declared, const InspectOptions& opt) {
    ValueView out;
    fill(reg, out, v, declared, opt, 0, opt.rootPath);
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
        InspectOptions o = opt;
        o.rootPath = p.variables[i].decl.name;
        vv.value = inspect(inst.registry(), *inst.variableCell(i), p.variables[i].type, o);
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
        InspectOptions o = opt;
        o.rootPath = f.graph->inputs[i].decl.name;
        vv.value = inspect(reg, *f.params[i], f.graph->inputs[i].type, o);
        vv.value.name = f.graph->inputs[i].decl.name;
        out.push_back(std::move(vv));
    }
    for (size_t i = 0; i < f.locals.size() && i < f.graph->locals.size(); ++i) {
        VariableView vv;
        vv.scope = VariableView::Scope::Local;
        InspectOptions o = opt;
        o.rootPath = f.graph->locals[i].decl.name;
        vv.value = inspect(reg, *f.locals[i], f.graph->locals[i].type, o);
        vv.value.name = f.graph->locals[i].decl.name;
        out.push_back(std::move(vv));
    }
    return out;
}

EvalResult evaluateExpression(const Instance& inst, const Task* task, size_t frameIndex, const std::string& expr, const InspectOptions& opt) {
    EvalResult res;
    std::string root;
    std::vector<Step> steps;
    if (!parseExpression(expr, root, steps, res.error)) return res;
    const Registry& reg = inst.registry();

    // the variable: this frame's parameters and locals first, then the globals
    const Value* cur = nullptr;
    const TypeDesc* declared = nullptr;
    if (task && frameIndex < task->depth()) {
        const Frame& f = task->frame(frameIndex);
        for (size_t i = 0; !cur && i < f.params.size() && i < f.graph->inputs.size(); ++i)
            if (f.params[i] && f.graph->inputs[i].decl.name == root) { cur = f.params[i].get(); declared = f.graph->inputs[i].type; }
        for (size_t i = 0; !cur && i < f.locals.size() && i < f.graph->locals.size(); ++i)
            if (f.locals[i] && f.graph->locals[i].decl.name == root) { cur = f.locals[i].get(); declared = f.graph->locals[i].type; }
    }
    if (!cur) {
        const Program& p = inst.program();
        for (size_t i = 0; i < p.variables.size() && i < inst.variableCount(); ++i)
            if (p.variables[i].decl.name == root) { cur = inst.variableCell(i).get(); declared = p.variables[i].type; break; }
    }
    if (!cur) {
        res.error = "unknown variable '" + root + "'";
        return res;
    }

    std::deque<Value> temporaries;          // values produced by property getters
    std::string path = root;
    const size_t walk = !steps.empty() && steps.back().kind == Step::Kind::Range ? steps.size() - 1 : steps.size();
    try {
        for (size_t s = 0; s < walk; ++s) {
            const Step& st = steps[s];
            if (st.kind == Step::Kind::Property && cur->isStruct()) {     // a field of a struct
                const StructData* d = cur->structPtr();
                const int i = d->type ? d->type->fieldIndex(st.name) : -1;
                if (i < 0) { res.error = "'" + path + "' has no field '" + st.name + "'"; return res; }
                cur = &d->fields[static_cast<size_t>(i)];
                declared = d->type->fields[static_cast<size_t>(i)].type;
                path += "." + st.name;
            } else if (st.kind == Step::Kind::Property) {
                const ObjectHandle* h = cur->objectPtr();
                if (!h || !h->type) { res.error = "'" + path + "' has no properties"; return res; }
                const PropertyDesc* found = nullptr;
                for (const PropertyDesc* p : h->type->allProperties())
                    if (p->name == st.name) { found = p; break; }
                if (!found) { res.error = "'" + path + "' has no property '" + st.name + "'"; return res; }
                temporaries.push_back(found->get(*h));
                cur = &temporaries.back();
                declared = found->type;
                path += "." + st.name;
            } else if (const ArrayData* a = cur->arrayPtr()) {
                if (st.kind != Step::Kind::Index) { res.error = "'" + path + "' is an array: use a number as index"; return res; }
                if (st.index < 0 || static_cast<size_t>(st.index) >= a->items.size()) {
                    res.error = "index " + std::to_string(st.index) + " is out of range (size " + sizeText(a->items.size()) + ")";
                    return res;
                }
                cur = &a->items[static_cast<size_t>(st.index)];
                declared = a->elem;
                path += "[" + std::to_string(st.index) + "]";
            } else if (const MapData* m = cur->mapPtr()) {
                Value key = st.kind == Step::Kind::Index ? Value(st.index) : Value(st.name);
                key = reg.convert(key, m->key);
                auto it = m->items.find(key);
                if (it == m->items.end()) { res.error = "'" + path + "' has no entry " + (st.kind == Step::Kind::Index ? std::to_string(st.index) : quoted(st.name)); return res; }
                cur = &it->second;
                declared = m->val;
                path += st.kind == Step::Kind::Index ? "[" + std::to_string(st.index) + "]" : quotedKeyPath(st.name);
            } else {
                res.error = "'" + path + "' is not an array, map or object";
                return res;
            }
        }

        if (walk == steps.size()) {
            InspectOptions o = opt;
            o.rootPath = path;
            res.view = inspect(reg, *cur, declared, o);
            res.ok = true;
            return res;
        }

        // a window of the children of a container
        const Step& range = steps.back();
        InspectOptions header = opt;
        header.rootPath = path;
        header.maxDepth = 0;                // only the summary of the container itself
        res.view = inspect(reg, *cur, declared, header);
        if (cur->kind() != Value::Kind::Array && cur->kind() != Value::Kind::Map && cur->kind() != Value::Kind::Object) {
            res.error = "'" + path + "' is not an array, map or object";
            return res;
        }
        size_t total = res.view.total;
        int64_t from = range.hasFrom ? range.from : 0;
        int64_t to = range.hasTo ? range.to : static_cast<int64_t>(total) - 1;
        if (from < 0) from = 0;
        if (to >= static_cast<int64_t>(total)) to = static_cast<int64_t>(total) - 1;
        if (to >= from && static_cast<size_t>(to - from) + 1 > opt.maxRange) to = from + static_cast<int64_t>(opt.maxRange) - 1;
        res.view.offset = static_cast<size_t>(from);
        if (to >= from) {
            InspectOptions o = opt;
            o.rootPath = path;
            fillChildren(reg, res.view, *cur, o, 1, path, static_cast<size_t>(from), static_cast<size_t>(to) + 1);
        }
        res.view.truncated = static_cast<size_t>(std::max<int64_t>(to + 1, 0)) < total;
        res.ok = true;
    } catch (const std::exception& e) {
        res.ok = false;
        res.error = e.what();
    }
    return res;
}

} // namespace fg
