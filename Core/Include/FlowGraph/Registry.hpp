#pragma once
// Registry: every type, function, event and node definition the runtime knows about.
// Hosts and plugins extend the system by calling registerClass / registerEnum / addFunction / addNode.
//
// Thread safety: register everything before running scripts. Looking things up and running
// scripts from several threads is fine afterwards (container type creation is internally locked).
#include <FlowGraph/Export.hpp>
#include <FlowGraph/NodeDef.hpp>
#include <FlowGraph/TypeExpr.hpp>
#include <FlowGraph/Types.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace fg {

template <class T> class ClassBuilder;
template <class E> class EnumBuilder;

// An event the host raises (BeginPlay, Tick, ...). Scripts get one entry node per declared event.
struct EventDesc {
    std::string name, doc, source;
    std::vector<ParamDesc> params;
};

class FG_CORE_EXPORT Registry {
public:
    Registry();
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // ---- primitive types -------------------------------------------------------------------
    const TypeDesc* tVoid() const { return void_; }
    const TypeDesc* tBool() const { return bool_; }
    const TypeDesc* tInt() const { return int_; }
    const TypeDesc* tFloat() const { return float_; }
    const TypeDesc* tString() const { return string_; }
    const TypeDesc* tAny() const { return any_; }

    // ---- containers: array<T>, map<K,V> (nesting is allowed) --------------------------------
    const TypeDesc* arrayOf(const TypeDesc* elem) const;
    const TypeDesc* mapOf(const TypeDesc* key, const TypeDesc* val) const;
    Value makeArray(const TypeDesc* elem) const;                         // empty array value
    Value makeMap(const TypeDesc* key, const TypeDesc* val) const;       // empty map value

    // ---- lookup ----------------------------------------------------------------------------
    const TypeDesc* findType(const std::string& expr) const;             // parses "array<int>" etc.; null when unknown
    const TypeDesc* resolveType(const TypeExpr& e) const;
    const TypeDesc* typeOrAny(const std::string& expr) const;            // unknown => any
    const TypeDesc* findTypeByCpp(std::type_index idx) const;
    uint64_t uid() const { return uid_; }                                // never reused by another registry of this process
    std::vector<const TypeDesc*> types() const;                          // non container types
    std::vector<const TypeDesc*> derivedTypes(const TypeDesc* base) const;  // direct children
    void setTypeColor(const std::string& typeName, uint32_t rgb);
    uint32_t typeColor(const TypeDesc* t) const;                         // containers use their element's color

    // ---- type system rules -----------------------------------------------------------------
    bool canConnect(const TypeDesc* from, const TypeDesc* to) const;
    Value convert(const Value& v, const TypeDesc* to) const;             // throws ScriptError
    Value convertFrom(const Value& v, const TypeDesc* from, const TypeDesc* to) const;  // enum aware
    Value defaultValue(const TypeDesc* t) const;
    std::string toString(const Value& v, const TypeDesc* declared = nullptr) const;

    // ---- extension API (used by built-ins and by plugins) ----------------------------------
    // Reflect a C++ class: registerClass<Actor>("Actor").base<Base>().method(...).property(...)
    template <class T>
    ClassBuilder<T> registerClass(const std::string& name, const std::string& category = "Classes");
    // Reflect a C++ enum: registerEnum<Color>("Color").value("Red", Color::Red)...
    template <class E>
    EnumBuilder<E> registerEnum(const std::string& name, const std::string& category = "Enums");
    // Register a free function / lambda as a callable node. Id convention: "Category.Name".
    template <class F>
    FunctionDesc& addFunction(const std::string& id, F fn, std::vector<std::string> paramNames = {});
    // Declare an event the host will raise: addEvent("Tick", {{"DeltaTime", "float"}})
    EventDesc& addEvent(const std::string& name, std::vector<std::pair<std::string, std::string>> params = {});
    // Register a completely custom node definition.
    NodeDef& addNode(NodeDef def);

    // Wrap host objects so they can be handed to scripts (as variables or event arguments).
    template <class T> Value wrap(std::shared_ptr<T> sp) const;             // owning (keeps the object alive)
    template <class T> Value wrapRef(T& ref) const;                        // non-owning
    template <class T> Value wrapPtr(T* ptr) const;                        // non-owning, null => null value
    template <class T> std::shared_ptr<T> unwrap(const Value& v) const;    // null when not a T

    // Source tag (plugin name) assigned to everything registered while set.
    void beginSource(const std::string& s) { source_ = s; }
    void endSource() { source_ = "core"; }
    const std::string& source() const { return source_; }

    // ---- functions, events, nodes ----------------------------------------------------------
    const FunctionDesc* findFunction(const std::string& id) const;
    FunctionDesc* mutableFunction(const std::string& id);
    std::vector<const FunctionDesc*> functions() const;
    const EventDesc* findEvent(const std::string& name) const;
    std::vector<const EventDesc*> events() const;

    const NodeDef* findNode(const std::string& id) const;
    NodeDef* mutableNode(const std::string& id);       // for hand registered nodes
    std::vector<const NodeDef*> nodes() const;

    // ---- internal (used by the templates in Bind.hpp) --------------------------------------
    TypeDesc* createClassType(const std::string& name, const std::string& category, std::type_index cpp);
    TypeDesc* createEnumType(const std::string& name, const std::string& category, std::type_index cpp);
    FunctionDesc& createFunction(const std::string& id);
    void markDirty() { dirty_ = true; }
    const TypeDesc* wrapType(std::type_index staticType, std::type_index dynamicType) const;

private:
    const uint64_t uid_;
    void ensureBuilt() const;
    void rebuildGenerated() const;
    TypeDesc* newType(const std::string& name);
    void registerBuiltinTypes();

    const TypeDesc *void_ = nullptr, *bool_ = nullptr, *int_ = nullptr, *float_ = nullptr, *string_ = nullptr, *any_ = nullptr;
    mutable std::deque<TypeDesc> types_;
    mutable std::unordered_map<std::string, TypeDesc*> byName_;
    std::unordered_map<std::type_index, TypeDesc*> byCpp_;
    std::deque<FunctionDesc> functions_;
    std::map<std::string, FunctionDesc*> funcById_;
    std::deque<EventDesc> events_;
    std::map<std::string, EventDesc*> eventByName_;
    mutable std::map<std::string, std::unique_ptr<NodeDef>> nodes_;
    std::string source_ = "core";
    mutable std::recursive_mutex typeMu_;
    mutable std::atomic<bool> dirty_{true};
};

// The instance that is currently executing script code (set by the interpreter), so host functions
// can reach their context. Null when called outside of a script run.
class Instance;
FG_CORE_EXPORT Instance* currentInstance();

} // namespace fg

#include <FlowGraph/Bind.hpp>
