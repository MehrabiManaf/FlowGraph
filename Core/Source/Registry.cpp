#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Vm.hpp>

#include <algorithm>
#include <cmath>

namespace fg {

namespace {

uint32_t hashColor(const std::string& name) {
    size_t h = std::hash<std::string>()(name);
    double hue = static_cast<double>(h % 360);
    double s = 0.55, v = 0.9;
    double c = v * s;
    double x = c * (1 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1));
    double m = v - c;
    double r = 0, g = 0, b = 0;
    if (hue < 60) { r = c; g = x; }
    else if (hue < 120) { r = x; g = c; }
    else if (hue < 180) { g = c; b = x; }
    else if (hue < 240) { g = x; b = c; }
    else if (hue < 300) { r = x; b = c; }
    else { r = c; b = x; }
    auto to8 = [&](double f) { return static_cast<uint32_t>(std::lround((f + m) * 255)); };
    return (to8(r) << 16) | (to8(g) << 8) | to8(b);
}

} // namespace

namespace {
uint64_t nextRegistryUid() {
    static std::atomic<uint64_t> next{1};
    return next++;
}
} // namespace

Registry::Registry() : uid_(nextRegistryUid()) { registerBuiltinTypes(); }
Registry::~Registry() = default;

// ------------------------------------------------------------------ types
TypeDesc* Registry::newType(const std::string& name) {
    std::lock_guard lk(typeMu_);
    types_.emplace_back();
    TypeDesc* t = &types_.back();
    t->name = name;
    t->source = source_;
    byName_[name] = t;
    return t;
}

void Registry::registerBuiltinTypes() {
    auto make = [&](const char* name, TypeKind k, uint32_t color) {
        TypeDesc* t = newType(name);
        t->kind = k;
        t->color = color;
        t->hasColor = true;
        t->category = "Basic";
        return t;
    };
    void_ = make("void", TypeKind::Void, 0x808080);
    bool_ = make("bool", TypeKind::Bool, 0xc23b3b);
    int_ = make("int", TypeKind::Int, 0x2fd0c0);
    float_ = make("float", TypeKind::Float, 0x9ee04a);
    string_ = make("string", TypeKind::String, 0xe052c8);
    any_ = make("any", TypeKind::Any, 0xa0a0a8);
}

TypeDesc* Registry::createClassType(const std::string& name, const std::string& category, std::type_index cpp) {
    {
        std::lock_guard lk(typeMu_);
        if (byName_.count(name)) throw std::logic_error("type '" + name + "' is already registered");
        if (byCpp_.count(cpp)) throw std::logic_error("C++ type of '" + name + "' is already registered");
    }
    TypeDesc* t = newType(name);
    t->kind = TypeKind::Class;
    t->category = category;
    t->cpp = cpp;
    std::lock_guard lk(typeMu_);
    byCpp_[cpp] = t;
    return t;
}

TypeDesc* Registry::createEnumType(const std::string& name, const std::string& category, std::type_index cpp) {
    {
        std::lock_guard lk(typeMu_);
        if (byName_.count(name)) throw std::logic_error("type '" + name + "' is already registered");
        if (byCpp_.count(cpp)) throw std::logic_error("C++ type of '" + name + "' is already registered");
    }
    TypeDesc* t = newType(name);
    t->kind = TypeKind::Enum;
    t->category = category;
    t->cpp = cpp;
    std::lock_guard lk(typeMu_);
    byCpp_[cpp] = t;
    return t;
}

const TypeDesc* Registry::arrayOf(const TypeDesc* elem) const {
    if (!elem) elem = any_;
    std::string name = "array<" + elem->name + ">";
    std::lock_guard lk(typeMu_);
    auto it = byName_.find(name);
    if (it != byName_.end()) return it->second;
    types_.emplace_back();
    TypeDesc* t = &types_.back();
    t->name = name;
    t->kind = TypeKind::Array;
    t->elem = elem;
    t->category = "Containers";
    byName_[name] = t;
    return t;
}

const TypeDesc* Registry::mapOf(const TypeDesc* key, const TypeDesc* val) const {
    if (!key) key = string_;
    if (!val) val = any_;
    std::string name = "map<" + key->name + "," + val->name + ">";
    std::lock_guard lk(typeMu_);
    auto it = byName_.find(name);
    if (it != byName_.end()) return it->second;
    types_.emplace_back();
    TypeDesc* t = &types_.back();
    t->name = name;
    t->kind = TypeKind::Map;
    t->keyType = key;
    t->elem = val;
    t->category = "Containers";
    byName_[name] = t;
    return t;
}

Value Registry::makeArray(const TypeDesc* elem) const {
    auto a = std::make_shared<ArrayData>();
    a->elem = elem;
    return Value(std::move(a));
}

Value Registry::makeMap(const TypeDesc* key, const TypeDesc* val) const {
    auto m = std::make_shared<MapData>();
    m->key = key;
    m->val = val;
    return Value(std::move(m));
}

const TypeDesc* Registry::resolveType(const TypeExpr& e) const {
    if (!e.valid) return nullptr;
    if (e.isArray()) {
        auto* el = resolveType(e.args[0]);
        return el ? arrayOf(el) : nullptr;
    }
    if (e.isMap()) {
        auto* k = resolveType(e.args[0]);
        auto* v = resolveType(e.args[1]);
        return (k && v) ? mapOf(k, v) : nullptr;
    }
    if (!e.args.empty()) return nullptr;
    std::lock_guard lk(typeMu_);
    auto it = byName_.find(e.name);
    return it == byName_.end() ? nullptr : it->second;
}

const TypeDesc* Registry::findType(const std::string& expr) const {
    {
        std::lock_guard lk(typeMu_);
        auto it = byName_.find(expr);
        if (it != byName_.end()) return it->second;
    }
    return resolveType(TypeExpr::parse(expr));
}

const TypeDesc* Registry::typeOrAny(const std::string& expr) const {
    auto* t = findType(expr);
    return t ? t : any_;
}

const TypeDesc* Registry::findTypeByCpp(std::type_index idx) const {
    std::lock_guard lk(typeMu_);
    auto it = byCpp_.find(idx);
    return it == byCpp_.end() ? nullptr : it->second;
}

const TypeDesc* Registry::wrapType(std::type_index staticType, std::type_index dynamicType) const {
    std::lock_guard lk(typeMu_);
    auto it = byCpp_.find(dynamicType);
    if (it != byCpp_.end()) return it->second;
    it = byCpp_.find(staticType);
    return it == byCpp_.end() ? nullptr : it->second;
}

std::vector<const TypeDesc*> Registry::types() const {
    std::lock_guard lk(typeMu_);
    std::vector<const TypeDesc*> out;
    for (auto& t : types_)
        if (!t.isContainer() && t.kind != TypeKind::Void) out.push_back(&t);
    return out;
}

std::vector<const TypeDesc*> Registry::derivedTypes(const TypeDesc* base) const {
    std::lock_guard lk(typeMu_);
    std::vector<const TypeDesc*> out;
    for (auto& t : types_)
        if (t.isClass() && t.hasDirectBase(base)) out.push_back(&t);
    return out;
}

void Registry::setTypeColor(const std::string& typeName, uint32_t rgb) {
    std::lock_guard lk(typeMu_);
    auto it = byName_.find(typeName);
    if (it == byName_.end()) return;
    it->second->color = rgb;
    it->second->hasColor = true;
}

uint32_t Registry::typeColor(const TypeDesc* t) const {
    if (!t) return 0xa0a0a8;
    if (t->kind == TypeKind::Array || t->kind == TypeKind::Map) return typeColor(t->elem);
    if (t->hasColor) return t->color;
    return hashColor(t->name);
}

// ------------------------------------------------------------------ type rules
bool Registry::canConnect(const TypeDesc* from, const TypeDesc* to) const {
    if (!from || !to) return false;
    if (from == to) return true;
    if (from->kind == TypeKind::Void || to->kind == TypeKind::Void) return false;
    if (from->kind == TypeKind::Any || to->kind == TypeKind::Any) return true;
    auto numeric = [](const TypeDesc* t) {
        return t->kind == TypeKind::Bool || t->kind == TypeKind::Int || t->kind == TypeKind::Float || t->kind == TypeKind::Enum;
    };
    if (to->kind == TypeKind::String) return true;     // everything can be printed
    if (numeric(from) && numeric(to)) return !(from->kind == TypeKind::Enum && to->kind == TypeKind::Enum);
    if (from->isClass() && to->isClass()) return from->isA(to);
    if (from->kind == TypeKind::Array && to->kind == TypeKind::Array)
        return from->elem == to->elem || from->elem->kind == TypeKind::Any || to->elem->kind == TypeKind::Any;
    if (from->kind == TypeKind::Map && to->kind == TypeKind::Map)
        return (from->keyType == to->keyType && from->elem == to->elem) || from->elem->kind == TypeKind::Any || to->elem->kind == TypeKind::Any;
    return false;
}

Value Registry::defaultValue(const TypeDesc* t) const {
    if (!t) return Value();
    switch (t->kind) {
    case TypeKind::Bool: return Value(false);
    case TypeKind::Int:
    case TypeKind::Enum: return Value(int64_t(0));
    case TypeKind::Float: return Value(0.0);
    case TypeKind::String: return Value(std::string());
    case TypeKind::Array: return makeArray(t->elem);
    case TypeKind::Map: return makeMap(t->keyType, t->elem);
    default: return Value();
    }
}

Value Registry::convert(const Value& v, const TypeDesc* to) const {
    if (!to) return v;
    switch (to->kind) {
    case TypeKind::Void: return Value();
    case TypeKind::Any: return v;
    case TypeKind::Bool: return v.isBool() ? v : Value(v.toBool());
    case TypeKind::Int: return v.isInt() ? v : Value(v.toInt());
    case TypeKind::Enum:
        if (v.isString()) {                                    // scripts may name the value: "Happy"
            for (auto& e : to->enumValues)
                if (e.name == v.str()) return Value(e.value);
        }
        return v.isInt() ? v : Value(v.toInt());
    case TypeKind::Float: return v.isFloat() ? v : Value(v.toFloat());
    case TypeKind::String: return v.isString() ? v : Value(toString(v));
    case TypeKind::Class: {
        if (v.isNull()) return v;
        auto* h = v.objectPtr();
        if (!h) throw ScriptError("cannot convert " + v.toString() + " to " + to->name);
        if (!castPtr(*h, to)) throw ScriptError("object of type '" + (h->type ? h->type->name : std::string("?")) + "' is not a '" + to->name + "'");
        return v;
    }
    case TypeKind::Array: {
        if (v.isNull()) return makeArray(to->elem);
        auto* a = v.arrayPtr();
        if (!a) throw ScriptError("cannot convert " + v.toString() + " to " + to->name);
        if (a->elem == to->elem || to->elem->kind == TypeKind::Any || a->elem == nullptr) return v;
        auto out = std::make_shared<ArrayData>();
        out->elem = to->elem;
        for (auto& e : a->items) out->items.push_back(convertFrom(e, a->elem, to->elem));
        return Value(std::move(out));
    }
    case TypeKind::Map: {
        if (v.isNull()) return makeMap(to->keyType, to->elem);
        auto* m = v.mapPtr();
        if (!m) throw ScriptError("cannot convert " + v.toString() + " to " + to->name);
        if ((m->key == to->keyType && m->val == to->elem) || to->elem->kind == TypeKind::Any) return v;
        auto out = std::make_shared<MapData>();
        out->key = to->keyType;
        out->val = to->elem;
        for (auto& [k, val] : m->items) out->items.emplace(convertFrom(k, m->key, to->keyType), convertFrom(val, m->val, to->elem));
        return Value(std::move(out));
    }
    }
    return v;
}

Value Registry::convertFrom(const Value& v, const TypeDesc* from, const TypeDesc* to) const {
    if (from == to || !to) return v;
    if (from && from->isEnum() && to->kind == TypeKind::String) return Value(from->enumName(v.toInt()));
    if (to->isEnum() && v.isString()) {
        for (auto& e : to->enumValues)
            if (e.name == v.str()) return Value(e.value);
    }
    return convert(v, to);
}

std::string Registry::toString(const Value& v, const TypeDesc* declared) const {
    if (declared && declared->isEnum() && v.isNumber()) return declared->enumName(v.toInt());
    return v.toString();
}

// ------------------------------------------------------------------ functions / events / nodes
FunctionDesc& Registry::createFunction(const std::string& id) {
    if (funcById_.count(id)) throw std::logic_error("function '" + id + "' is already registered");
    functions_.emplace_back();
    FunctionDesc& fd = functions_.back();
    fd.name = id;
    fd.source = source_;
    funcById_[id] = &fd;
    dirty_ = true;
    return fd;
}

const FunctionDesc* Registry::findFunction(const std::string& id) const {
    auto it = funcById_.find(id);
    return it == funcById_.end() ? nullptr : it->second;
}
FunctionDesc* Registry::mutableFunction(const std::string& id) {
    auto it = funcById_.find(id);
    if (it != funcById_.end()) dirty_ = true;
    return it == funcById_.end() ? nullptr : it->second;
}
std::vector<const FunctionDesc*> Registry::functions() const {
    std::vector<const FunctionDesc*> out;
    for (auto& [id, f] : funcById_) out.push_back(f);
    return out;
}

EventDesc& Registry::addEvent(const std::string& name, std::vector<std::pair<std::string, std::string>> params) {
    auto it = eventByName_.find(name);
    EventDesc* ed;
    if (it != eventByName_.end()) ed = it->second;
    else {
        events_.emplace_back();
        ed = &events_.back();
        eventByName_[name] = ed;
    }
    ed->name = name;
    ed->source = source_;
    ed->params.clear();
    for (auto& [n, t] : params) {
        ParamDesc p;
        p.name = n;
        p.type = typeOrAny(t);
        p.def = defaultValue(p.type);
        ed->params.push_back(std::move(p));
    }
    dirty_ = true;
    return *ed;
}

const EventDesc* Registry::findEvent(const std::string& name) const {
    auto it = eventByName_.find(name);
    return it == eventByName_.end() ? nullptr : it->second;
}
std::vector<const EventDesc*> Registry::events() const {
    std::vector<const EventDesc*> out;
    for (auto& [n, e] : eventByName_) out.push_back(e);
    return out;
}

NodeDef& Registry::addNode(NodeDef def) {
    std::lock_guard lk(typeMu_);
    if (def.source.empty()) def.source = source_;
    auto& slot = nodes_[def.id];
    if (slot) *slot = std::move(def);
    else slot = std::make_unique<NodeDef>(std::move(def));
    return *slot;
}

const NodeDef* Registry::findNode(const std::string& id) const {
    ensureBuilt();
    std::lock_guard lk(typeMu_);
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}
NodeDef* Registry::mutableNode(const std::string& id) {
    ensureBuilt();
    std::lock_guard lk(typeMu_);
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}
std::vector<const NodeDef*> Registry::nodes() const {
    ensureBuilt();
    std::lock_guard lk(typeMu_);
    std::vector<const NodeDef*> out;
    for (auto& [id, n] : nodes_) out.push_back(n.get());
    return out;
}

void Registry::ensureBuilt() const {
    if (!dirty_.load()) return;
    std::lock_guard lk(typeMu_);
    if (!dirty_.load()) return;
    rebuildGenerated();
    dirty_ = false;
}

// Creates (or refreshes in place, so pointers held by compiled programs stay valid) generated nodes.
void Registry::rebuildGenerated() const {
    auto put = [&](NodeDef nd) {
        nd.generated = true;
        auto& slot = nodes_[nd.id];
        if (slot) *slot = std::move(nd);
        else slot = std::make_unique<NodeDef>(std::move(nd));
    };

    // ---- functions and methods
    for (auto& [id, fdp] : funcById_) {
        const FunctionDesc* fd = fdp;
        NodeDef nd;
        nd.id = (fd->isMethod() ? "m:" : "fn:") + fd->name;
        nd.title = fd->title.empty() ? fd->name : fd->title;
        nd.category = fd->category;
        nd.doc = fd->doc;
        nd.keywords = fd->keywords;
        nd.source = fd->source;
        const bool latent = fd->latent && fd->start;
        const bool pure = fd->pure && !latent;           // a pure node cannot wait (makePure() of an asynchronous function is ignored)
        nd.pure = pure;
        nd.style = pure ? NodeStyle::Pure : NodeStyle::Function;
        nd.mutatesFirstInput = fd->isMethod() && !fd->isConst;
        if (latent) nd.keywords += (nd.keywords.empty() ? "" : " ") + std::string("async wait latent");
        nd.waits = latent;
        if (!pure) {
            nd.pins.push_back(execIn());
            nd.pins.push_back(execOut());
        }
        if (fd->isMethod()) {
            PinDef t = dataIn("Target", fd->owner->name);
            t.doc = "The object the method is called on";
            nd.pins.push_back(t);
        }
        for (auto& p : fd->params) {
            PinDef pd = dataIn(p.name, p.type ? p.type->name : "any", p.def);
            pd.doc = p.doc;
            nd.pins.push_back(std::move(pd));
        }
        bool hasRet = fd->ret && fd->ret->kind != TypeKind::Void;
        if (hasRet) nd.pins.push_back(dataOut(fd->retName, fd->ret->name));
        if (latent) {
            // Starts the call, waits (the task, not the thread) until it is done, then runs again to hand out the result.
            nd.run = [fd, hasRet](NodeCtx& c) {
                std::shared_ptr<PendingCall> call;
                if (c.resumed()) call = std::static_pointer_cast<PendingCall>(c.native());
                else {
                    call = std::make_shared<PendingCall>(fd->start(c.inputs()));
                    if (!call->ready()) {
                        c.native() = call;
                        c.waitThenResume([call](double) { return call->ready(); });
                        return;
                    }
                }
                if (!call) c.error("lost the pending call of '" + fd->name + "'");
                Value r = call->result();
                if (hasRet) c.out(0, std::move(r));
            };
        } else {
            nd.run = [fd, hasRet](NodeCtx& c) {
                Value r = fd->invoke(c.inputs());
                if (hasRet) c.out(0, std::move(r));
            };
        }
        put(std::move(nd));
    }

    // ---- classes: properties, constructors, casts; enums
    for (auto& t : types_) {
        const TypeDesc* td = &t;
        if (td->isClass()) {
            std::string cat = (td->category.empty() ? td->name : td->category + "|" + td->name);
            for (auto& p : td->properties) {
                const PropertyDesc* pd = &p;
                NodeDef g;
                g.id = "get:" + td->name + "." + p.name;
                g.title = "Get " + p.name;
                g.category = cat;
                g.doc = p.doc;
                g.source = td->source;
                g.pure = true;
                g.style = NodeStyle::Pure;
                g.pins.push_back(dataIn("Target", td->name));
                g.pins.push_back(dataOut(p.name, p.type->name));
                g.run = [pd](NodeCtx& c) {
                    auto* h = c.in(0).objectPtr();
                    if (!h) c.error("null object reference");
                    c.out(0, pd->get(*h));
                };
                put(std::move(g));
                if (p.set) {
                    NodeDef s;
                    s.id = "set:" + td->name + "." + p.name;
                    s.title = "Set " + p.name;
                    s.category = cat;
                    s.doc = p.doc;
                    s.source = td->source;
                    s.mutatesFirstInput = true;
                    s.pins.push_back(execIn());
                    s.pins.push_back(execOut());
                    s.pins.push_back(dataIn("Target", td->name));
                    s.pins.push_back(dataIn(p.name, p.type->name, defaultValue(p.type)));
                    s.run = [pd](NodeCtx& c) {
                        auto* h = c.in(0).objectPtr();
                        if (!h) c.error("null object reference");
                        pd->set(*h, c.in(1));
                    };
                    put(std::move(s));
                }
            }
            if (!td->abstract) {
                for (size_t i = 0; i < td->ctors.size(); ++i) {
                    const CtorDesc* cd = &td->ctors[i];
                    NodeDef n;
                    n.id = "new:" + td->name + (td->ctors.size() > 1 ? "#" + std::to_string(i) : "");
                    n.title = "New " + td->name;
                    n.category = cat;
                    n.source = td->source;
                    n.pins.push_back(execIn());
                    n.pins.push_back(execOut());
                    for (auto& p : cd->params) n.pins.push_back(dataIn(p.name, p.type->name, defaultValue(p.type)));
                    n.pins.push_back(dataOut("Object", td->name));
                    n.run = [cd](NodeCtx& c) { c.out(0, cd->create(c.inputs())); };
                    put(std::move(n));
                }
            }
            NodeDef cast;
            cast.id = "cast:" + td->name;
            cast.title = "Cast To " + td->name;
            cast.category = "Casting";
            cast.keywords = "cast as convert";
            cast.source = td->source;
            cast.pins.push_back(execIn());
            cast.pins.push_back(execOut("Success"));
            cast.pins.push_back(execOut("Failed"));
            cast.pins.push_back(dataIn("Object", "any"));
            cast.pins.push_back(dataOut("As " + td->name, td->name));
            cast.run = [td](NodeCtx& c) {
                auto* h = c.in(0).objectPtr();
                if (h && castPtr(*h, td)) { c.out(0, c.in(0)); c.next(0); }
                else { c.out(0, Value()); c.next(1); }
            };
            put(std::move(cast));
        } else if (td->isEnum()) {
            NodeDef e;
            e.id = "enum:" + td->name;
            e.title = td->name;
            e.category = "Enums|" + td->name;
            e.source = td->source;
            e.pure = true;
            e.style = NodeStyle::Pure;
            e.pins.push_back(dataOut("Value", td->name));
            e.doc = td->doc.empty() ? "A value of the enum " + td->name + "." : td->doc;
            {   // the value is chosen in the Details panel; the node's title shows it (nodeTitle: "Weekday.Friday")
                NodeSetting value;
                value.key = "value";
                value.label = "Value";
                value.kind = NodeSetting::Kind::Choice;
                for (auto& v : td->enumValues) value.choices.push_back(v.name);
                if (!value.choices.empty()) value.def = value.choices.front();
                e.settings.push_back(std::move(value));
            }
            e.expand = [td](const NodeBuildCtx&, std::vector<NodeTemplate>& out) {
                for (auto& v : td->enumValues) {
                    NodeTemplate nt;
                    nt.defId = "enum:" + td->name;
                    nt.props["value"] = v.name;
                    nt.title = td->name + "." + v.name;
                    nt.category = "Enums|" + td->name;
                    nt.doc = v.doc;
                    nt.style = NodeStyle::Pure;
                    nt.pins.push_back(dataOut("Value", td->name));
                    out.push_back(std::move(nt));
                }
            };
            e.run = [td](NodeCtx& c) {
                const std::string& n = c.prop("value");
                for (auto& v : td->enumValues)
                    if (v.name == n) { c.out(0, Value(v.value)); return; }
                c.out(0, Value(td->enumValues.empty() ? int64_t(0) : td->enumValues.front().value));
            };
            put(std::move(e));
        }
    }

    // ---- host events
    for (auto& [name, edp] : eventByName_) {
        const EventDesc* ed = edp;
        NodeDef n;
        n.id = "event:" + name;
        n.title = name;
        n.category = "Events";
        n.doc = ed->doc;
        n.source = ed->source;
        n.style = NodeStyle::Event;
        n.op = NodeOp::Event;
        n.eventGraphOnly = true;
        n.pins.push_back(execOut());
        for (auto& p : ed->params) n.pins.push_back(dataOut(p.name, p.type ? p.type->name : "any"));
        put(std::move(n));
    }
}

} // namespace fg
