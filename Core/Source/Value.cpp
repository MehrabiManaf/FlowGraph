#include <FlowGraph/Types.hpp>
#include <FlowGraph/Value.hpp>

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fg {

namespace {

std::string formatFloat(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), d);
    std::string s(buf, res.ptr);
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

int rank(const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Null: return 0;
    case Value::Kind::Bool: return 1;
    case Value::Kind::Int:
    case Value::Kind::Float: return 2;
    case Value::Kind::String: return 3;
    case Value::Kind::Object: return 4;
    case Value::Kind::Array: return 5;
    case Value::Kind::Map: return 6;
    }
    return 0;
}

} // namespace

std::string Value::toString() const {
    switch (kind_) {
    case Kind::Null: return "null";
    case Kind::Bool: return u_.i ? "true" : "false";
    case Kind::Int: return std::to_string(u_.i);
    case Kind::Float: return formatFloat(u_.d);
    case Kind::String: return str();
    case Kind::Object: {
        auto* h = objectPtr();
        if (h->type && h->type->toStringFn && h->ptr) return h->type->toStringFn(h->ptr.get());
        return "<" + std::string(h->type ? h->type->name : "object") + ">";
    }
    case Kind::Array: {
        std::string s = "[";
        bool first = true;
        for (auto& e : arrayPtr()->items) {
            if (!first) s += ", ";
            first = false;
            s += e.isString() ? "\"" + e.str() + "\"" : e.toString();
        }
        return s + "]";
    }
    case Kind::Map: {
        std::string s = "{";
        bool first = true;
        for (auto& [k, v] : mapPtr()->items) {
            if (!first) s += ", ";
            first = false;
            s += (k.isString() ? "\"" + k.str() + "\"" : k.toString()) + ": " + (v.isString() ? "\"" + v.str() + "\"" : v.toString());
        }
        return s + "}";
    }
    }
    return {};
}

bool Value::toBoolSlow() const {
    switch (kind_) {
    case Kind::Null: return false;
    case Kind::Bool:
    case Kind::Int: return u_.i != 0;
    case Kind::Float: return u_.d != 0.0;
    case Kind::String: return !str().empty();
    default: return true;
    }
}

int64_t Value::toIntSlow() const {
    switch (kind_) {
    case Kind::Null: return 0;
    case Kind::Bool:
    case Kind::Int: return u_.i;
    case Kind::Float:
        if (std::isnan(u_.d)) return 0;
        if (u_.d >= 9.2e18) return INT64_MAX;
        if (u_.d <= -9.2e18) return INT64_MIN;
        return static_cast<int64_t>(u_.d);
    case Kind::String: {
        const std::string& s = str();
        char* end = nullptr;
        long long v = std::strtoll(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0') {
            double d = std::strtod(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0') throw ScriptError("cannot convert \"" + s + "\" to int");
            return static_cast<int64_t>(d);
        }
        return v;
    }
    default: throw ScriptError("cannot convert " + toString() + " to int");
    }
}

double Value::toFloatSlow() const {
    switch (kind_) {
    case Kind::Null: return 0.0;
    case Kind::Bool: return u_.i ? 1.0 : 0.0;
    case Kind::String: {
        const std::string& s = str();
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0') throw ScriptError("cannot convert \"" + s + "\" to float");
        return d;
    }
    default: throw ScriptError("cannot convert " + toString() + " to float");
    }
}

Value Value::deepCopySlow() const {
    if (kind_ == Kind::Array) {
        auto src = arrayPtr();
        auto dst = std::make_shared<ArrayData>();
        dst->elem = src->elem;
        dst->items.reserve(src->items.size());
        for (auto& e : src->items) dst->items.push_back(e.deepCopy());
        return Value(std::move(dst));
    }
    auto src = mapPtr();
    auto dst = std::make_shared<MapData>();
    dst->key = src->key;
    dst->val = src->val;
    for (auto& [k, v] : src->items) dst->items.emplace(k, v.deepCopy());
    return Value(std::move(dst));
}

bool Value::equalsSlow(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        if (a.isInt() && b.isInt()) return a.u_.i == b.u_.i;
        return a.toFloat() == b.toFloat();
    }
    if (a.kind_ != b.kind_) return false;
    switch (a.kind_) {
    case Value::Kind::Null: return true;
    case Value::Kind::Bool: return a.u_.i == b.u_.i;
    case Value::Kind::String: return a.str() == b.str();
    case Value::Kind::Object: {
        auto* x = a.objectPtr();
        auto* y = b.objectPtr();
        return x == y || (x->ptr.get() == y->ptr.get() && x->type == y->type);
    }
    case Value::Kind::Array: {
        auto* x = a.arrayPtr();
        auto* y = b.arrayPtr();
        if (x == y) return true;
        if (x->items.size() != y->items.size()) return false;
        for (size_t i = 0; i < x->items.size(); ++i)
            if (x->items[i] != y->items[i]) return false;
        return true;
    }
    case Value::Kind::Map: {
        auto* x = a.mapPtr();
        auto* y = b.mapPtr();
        if (x == y) return true;
        if (x->items.size() != y->items.size()) return false;
        auto i = x->items.begin();
        auto j = y->items.begin();
        for (; i != x->items.end(); ++i, ++j)
            if (i->first != j->first || i->second != j->second) return false;
        return true;
    }
    default: return false;
    }
}

bool ValueLess::slow(const Value& a, const Value& b) {
    int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb;
    switch (a.kind()) {
    case Value::Kind::Bool: return a.toInt() < b.toInt();
    case Value::Kind::Int:
    case Value::Kind::Float:
        if (a.isInt() && b.isInt()) return a.toInt() < b.toInt();
        return a.toFloat() < b.toFloat();
    case Value::Kind::String: return a.str() < b.str();
    case Value::Kind::Object: {
        auto* x = a.objectPtr();
        auto* y = b.objectPtr();
        return std::less<const void*>()(x->ptr.get(), y->ptr.get());
    }
    case Value::Kind::Array:
    case Value::Kind::Map: return std::less<const void*>()(a.identity(), b.identity());
    default: return false;
    }
}

} // namespace fg
