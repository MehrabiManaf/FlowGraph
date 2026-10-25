// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/TypeScope.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>

#include <algorithm>
#include <functional>
#include <set>

namespace fg {

TypeScope::TypeScope(const Registry& reg) : reg_(&reg) {}

std::shared_ptr<TypeScope> TypeScope::forScript(const Registry& reg, const Script& script, std::vector<std::string>* errors) {
    auto scope = std::make_shared<TypeScope>(reg);
    scope->declare(script.structs, errors);
    return scope;
}

bool TypeScope::owns(const TypeDesc* t) const {
    std::lock_guard lk(mu_);
    return t && owned_.count(t);
}

const TypeDesc* TypeScope::findStruct(const std::string& name) const {
    std::lock_guard lk(mu_);
    auto it = byName_.find(name);
    return it != byName_.end() && it->second->isStruct() ? it->second : nullptr;
}

std::vector<const TypeDesc*> TypeScope::types() const {
    std::vector<const TypeDesc*> out = reg_->types();
    out.insert(out.end(), structs_.begin(), structs_.end());
    return out;
}

const TypeDesc* TypeScope::resolveType(const TypeExpr& e) const {
    if (!e.valid) return nullptr;
    if (e.isArray()) {
        const TypeDesc* el = resolveType(e.args[0]);
        return el ? arrayOf(el) : nullptr;
    }
    if (e.isMap()) {
        const TypeDesc* k = resolveType(e.args[0]);
        const TypeDesc* v = resolveType(e.args[1]);
        return k && v ? mapOf(k, v) : nullptr;
    }
    if (e.args.empty())
        if (const TypeDesc* s = findStruct(e.name)) return s;
    return reg_->resolveType(e);
}

const TypeDesc* TypeScope::findType(const std::string& expr) const {
    if (const TypeDesc* s = findStruct(expr)) return s;
    return resolveType(TypeExpr::parse(expr));
}

const TypeDesc* TypeScope::typeOrAny(const std::string& expr) const {
    const TypeDesc* t = findType(expr);
    return t ? t : reg_->tAny();
}

// A container of a struct of the script is the script's too: array<Point> of another script is another type.
const TypeDesc* TypeScope::arrayOf(const TypeDesc* elem) const {
    if (!owns(elem)) return reg_->arrayOf(elem);
    std::lock_guard lk(mu_);
    const std::string name = "array<" + elem->name + ">";
    if (auto it = byName_.find(name); it != byName_.end()) return it->second;
    TypeDesc& t = types_.emplace_back();
    t.name = name;
    t.kind = TypeKind::Array;
    t.elem = elem;
    t.category = "Containers";
    byName_[name] = &t;
    owned_.insert(&t);
    return &t;
}

const TypeDesc* TypeScope::mapOf(const TypeDesc* key, const TypeDesc* val) const {
    if (!owns(key) && !owns(val)) return reg_->mapOf(key, val);
    std::lock_guard lk(mu_);
    const std::string name = "map<" + key->name + "," + val->name + ">";
    if (auto it = byName_.find(name); it != byName_.end()) return it->second;
    TypeDesc& t = types_.emplace_back();
    t.name = name;
    t.kind = TypeKind::Map;
    t.keyType = key;
    t.elem = val;
    t.category = "Containers";
    byName_[name] = &t;
    owned_.insert(&t);
    return &t;
}

void TypeScope::declare(const std::vector<StructDecl>& decls, std::vector<std::string>* errors) {
    auto error = [&](const std::string& m) {
        if (errors) errors->push_back(m);
    };
    // 1. the struct types (so that the fields can name any of them, in any order)
    std::vector<TypeDesc*> made;
    for (auto& d : decls) {
        if (d.name.empty()) {
            error("A struct has no name");
            continue;
        }
        if (TypeExpr::parse(d.name).str() != d.name || !TypeExpr::parse(d.name).isPlain()) {
            error("'" + d.name + "' is not a valid name for a struct");
            continue;
        }
        if (byName_.count(d.name)) {
            error("Duplicate struct '" + d.name + "'");
            continue;
        }
        if (reg_->findType(d.name)) {
            error("The struct '" + d.name + "' has the name of a type of this application");
            continue;
        }
        TypeDesc& t = types_.emplace_back();
        t.name = d.name;
        t.kind = TypeKind::Struct;
        t.doc = d.doc;
        t.category = d.category.empty() ? "Structs" : d.category;
        t.source = "script";
        t.owner = weak_from_this();
        byName_[d.name] = &t;
        owned_.insert(&t);
        structs_.push_back(&t);
        made.push_back(&t);
    }
    // 2. their fields
    size_t mi = 0;
    for (auto& d : decls) {
        if (mi >= made.size() || made[mi]->name != d.name) continue;
        TypeDesc& t = *made[mi++];
        std::set<std::string> names;
        for (auto& f : d.fields) {
            FieldDesc fd;
            fd.name = f.name;
            fd.doc = f.doc;
            fd.def = f.def;
            if (f.name.empty()) error("A field of the struct '" + d.name + "' has no name");
            else if (!names.insert(f.name).second) error("Duplicate field '" + f.name + "' in the struct '" + d.name + "'");
            fd.type = findType(f.type);
            if (!fd.type) {
                error("Unknown type '" + f.type + "' of the field '" + d.name + "." + f.name + "'");
                fd.type = reg_->tAny();
            } else if (fd.type->kind == TypeKind::Void) {
                error("The field '" + d.name + "." + f.name + "' cannot be void");
                fd.type = reg_->tAny();
            }
            t.fields.push_back(std::move(fd));
        }
    }
    // 3. a struct that contains itself (directly, or through other structs) would be infinite: only an array or a map may hold it
    for (TypeDesc* start : made) {
        std::vector<const TypeDesc*> path;
        std::function<bool(const TypeDesc*)> reaches = [&](const TypeDesc* t) {
            for (auto& f : t->fields) {
                if (!f.type || !f.type->isStruct()) continue;
                if (f.type == start) return true;
                if (std::find(path.begin(), path.end(), f.type) != path.end()) continue;
                path.push_back(f.type);
                if (reaches(f.type)) return true;
                path.pop_back();
            }
            return false;
        };
        for (auto& f : start->fields) {
            if (!f.type || !f.type->isStruct()) continue;
            path.assign(1, f.type);
            if (f.type == start || reaches(f.type)) {
                error("The struct '" + start->name + "' contains itself (field '" + f.name +
                      "'): a struct can only hold itself in an array or a map");
                f.type = reg_->tAny();                   // so that a new one is not infinite
            }
        }
    }
    // 4. the defaults, as values of their field's types
    for (TypeDesc* t : made) {
        for (auto& f : t->fields) {
            if (f.def.isNull()) continue;
            try {
                f.def = reg_->convert(f.def, f.type);
            } catch (const ScriptError& e) {
                error("The default of the field '" + t->name + "." + f.name + "' does not fit its type: " + e.what());
                f.def = Value();
            }
        }
    }
}

} // namespace fg
