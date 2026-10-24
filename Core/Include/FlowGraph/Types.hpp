// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Runtime type descriptions: primitives, enums, containers and reflected C++ classes.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Value.hpp>

#include <deque>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace fg {

// Byte: an int from 0 to 255 (a Value of Kind::Int; a value out of range wraps around, like int overflow). Bytes: raw data (fg::Bytes).
enum class TypeKind : uint8_t { Void, Bool, Int, Float, String, Any, Class, Enum, Array, Map, Byte, Bytes };

struct ParamDesc {
    std::string name;
    const TypeDesc* type = nullptr;
    Value def;                       // default literal for unconnected pins
    std::string doc;
};

// A call of an asynchronous (latent) host function that has not finished yet. The script waits - the thread does not - and polls
// ready(); when it returns true, result() gives the value, or throws: that is a script error at the node, like any other.
struct PendingCall {
    std::function<bool()> ready;
    std::function<Value()> result;
};

// A callable exposed to scripts. Free functions and class methods share this description:
// for methods `owner` is the class and `args[0]` of `invoke` is the target object.
struct FunctionDesc {
    std::string name;                // unique id, e.g. "Math.Sin" or "Animal.Speak"
    std::string title, category, doc, keywords, source;
    const TypeDesc* owner = nullptr; // class for methods, null for free functions
    std::vector<ParamDesc> params;   // does not include the target object of methods
    const TypeDesc* ret = nullptr;   // never null (void type when nothing is returned)
    std::string retName = "Result";
    bool pure = false;               // no exec pins in the editor
    bool isConst = false;            // methods: does not modify the object
    // Asynchronous: the callable returns a std::future / std::shared_future (see Registry::addFunction). The node starts the call
    // (`start`), the task waits without blocking the thread, and the script goes on with the result. Never pure. `invoke` still works:
    // it waits for the result.
    bool latent = false;
    std::function<Value(Value* args)> invoke;
    std::function<PendingCall(Value* args)> start;     // latent functions only

    bool isMethod() const { return owner != nullptr; }
    FunctionDesc& makePure(bool p = true) { pure = p; return *this; }
    FunctionDesc& withCategory(std::string c) { category = std::move(c); return *this; }
    FunctionDesc& withTitle(std::string t) { title = std::move(t); return *this; }
    FunctionDesc& withDoc(std::string d) { doc = std::move(d); return *this; }
    FunctionDesc& withKeywords(std::string k) { keywords = std::move(k); return *this; }
    FunctionDesc& withResultName(std::string n) { retName = std::move(n); return *this; }
    FunctionDesc& withDefault(size_t paramIndex, Value v) {
        if (paramIndex < params.size()) params[paramIndex].def = std::move(v);
        return *this;
    }
};

struct PropertyDesc {
    std::string name, doc;
    const TypeDesc* owner = nullptr;
    const TypeDesc* type = nullptr;
    std::function<Value(const ObjectHandle& self)> get;
    std::function<void(const ObjectHandle& self, const Value&)> set;   // empty => read only
};

struct CtorDesc {
    std::vector<ParamDesc> params;
    std::function<Value(Value* args)> create;
};

struct EnumValueDesc {
    std::string name;
    int64_t value = 0;
    std::string doc;
};

struct FG_CORE_EXPORT TypeDesc {
    std::string name, category, doc, source;
    uint32_t color = 0;                     // 0xRRGGBB pin color used by editors
    bool hasColor = false;
    TypeKind kind = TypeKind::Class;
    const TypeDesc* elem = nullptr;         // Array: element type, Map: value type
    const TypeDesc* keyType = nullptr;      // Map: key type
    std::vector<EnumValueDesc> enumValues;  // Enum
    const TypeDesc* base = nullptr;         // primary base class
    std::function<void*(void*)> toBase;     // Derived* -> Base* (pointer adjusting upcast)
    struct BaseLink {
        const TypeDesc* type;
        std::function<void*(void*)> up;
    };
    std::vector<BaseLink> extraBases;       // additional (multiple inheritance) bases
    bool abstract = false;
    bool polymorphic = false;
    std::type_index cpp = typeid(void);
    std::deque<PropertyDesc> properties;
    std::vector<CtorDesc> ctors;
    std::function<std::string(const void*)> toStringFn;  // optional pretty printer

    bool isClass() const { return kind == TypeKind::Class; }
    bool isEnum() const { return kind == TypeKind::Enum; }
    bool isContainer() const { return kind == TypeKind::Array || kind == TypeKind::Map; }
    bool isPrimitive() const {
        return kind == TypeKind::Bool || kind == TypeKind::Int || kind == TypeKind::Float || kind == TypeKind::String ||
               kind == TypeKind::Byte || kind == TypeKind::Bytes;
    }
    bool isNumeric() const {
        return kind == TypeKind::Int || kind == TypeKind::Float || kind == TypeKind::Enum || kind == TypeKind::Bool || kind == TypeKind::Byte;
    }

    // Whether `v` is one of the values of this enum.
    bool hasEnumValue(int64_t v) const {
        for (auto& e : enumValues) if (e.value == v) return true;
        return false;
    }
    // Name of an enum value (or its number when there is none).
    std::string enumName(int64_t v) const {
        for (auto& e : enumValues) if (e.value == v) return e.name;
        return std::to_string(v);
    }
    // True when this type is `other` or derives from it (classes only).
    bool isA(const TypeDesc* other) const;
    bool hasDirectBase(const TypeDesc* b) const;
    // Looks a property up on this class first, then its bases, so derived types override.
    const PropertyDesc* findProperty(const std::string& n) const;
    std::vector<const PropertyDesc*> allProperties() const;
};

// Converts the object held by `h` to a pointer of registered type `target` (walking the registered
// base chain and applying pointer adjustments). Returns nullptr when `h` is not a `target`.
FG_CORE_EXPORT void* castPtr(const ObjectHandle& h, const TypeDesc* target);

} // namespace fg
