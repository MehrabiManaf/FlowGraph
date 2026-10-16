#pragma once
// fg::Value - the dynamically typed value that flows through script pins and variables.
//
// Layout: kind tag + 8 byte payload (bool / int / float) + one shared_ptr for the reference kinds
// (string, object, array, map). Copying a number never touches a reference count.
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#define FG_FORCEINLINE __forceinline
#define FG_NOINLINE __declspec(noinline)
#else
#define FG_FORCEINLINE inline __attribute__((always_inline))
#define FG_NOINLINE __attribute__((noinline))
#endif

namespace fg {

struct TypeDesc;

// A reference to a native C++ object living in the script world. `type` is the *dynamic* (most
// derived registered) type and `ptr` points at the most derived object; it either owns the object
// or is a non-owning alias (see Registry::wrapRef).
struct ObjectHandle {
    const TypeDesc* type = nullptr;
    std::shared_ptr<void> ptr;
};
using ObjectRef = std::shared_ptr<ObjectHandle>;

// Containers have reference semantics inside a graph (an `Add` node changes the container in
// place); variable assignment, function arguments and results copy them.
struct ArrayData;
struct MapData;
using ArrayRef = std::shared_ptr<ArrayData>;
using MapRef = std::shared_ptr<MapData>;

class ScriptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// Thrown internally when a task is aborted.
class AbortError : public ScriptError {
public:
    AbortError() : ScriptError("Execution aborted") {}
};

class Value {
public:
    enum class Kind : uint8_t { Null, Bool, Int, Float, String, Object, Array, Map };

    Value() = default;
    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    FG_FORCEINLINE Value& operator=(const Value& o) {
        kind_ = o.kind_;
        u_ = o.u_;
        if (ref_ || o.ref_) ref_ = o.ref_;
        return *this;
    }
    FG_FORCEINLINE Value& operator=(Value&& o) noexcept {
        kind_ = o.kind_;
        u_ = o.u_;
        if (ref_ || o.ref_) ref_ = std::move(o.ref_);
        return *this;
    }
    Value(bool b) : kind_(Kind::Bool) { u_.i = b ? 1 : 0; }
    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T i) : kind_(Kind::Int) { u_.i = static_cast<int64_t>(i); }
    template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    Value(T d) : kind_(Kind::Float) { u_.d = static_cast<double>(d); }
    Value(const char* s) : kind_(Kind::String), ref_(std::make_shared<std::string>(s ? s : "")) {}
    Value(std::string s) : kind_(Kind::String), ref_(std::make_shared<std::string>(std::move(s))) {}
    Value(ObjectRef o) : kind_(o ? Kind::Object : Kind::Null), ref_(std::move(o)) {}
    Value(ArrayRef a) : kind_(a ? Kind::Array : Kind::Null), ref_(std::move(a)) {}
    Value(MapRef m) : kind_(m ? Kind::Map : Kind::Null), ref_(std::move(m)) {}

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isInt() const { return kind_ == Kind::Int; }
    bool isFloat() const { return kind_ == Kind::Float; }
    bool isNumber() const { return kind_ == Kind::Int || kind_ == Kind::Float; }
    bool isString() const { return kind_ == Kind::String; }
    bool isObject() const { return kind_ == Kind::Object; }
    bool isArray() const { return kind_ == Kind::Array; }
    bool isMap() const { return kind_ == Kind::Map; }

    // Conversions. The "slow" paths throw ScriptError when the conversion makes no sense.
    bool toBool() const { return kind_ == Kind::Bool ? u_.i != 0 : toBoolSlow(); }
    int64_t toInt() const { return kind_ == Kind::Int ? u_.i : toIntSlow(); }
    double toFloat() const {
        return kind_ == Kind::Float ? u_.d : kind_ == Kind::Int ? static_cast<double>(u_.i) : toFloatSlow();
    }
    std::string toString() const;          // human readable form (used by Print String / To String)

    // Raw accessors, valid only for the matching kind() (no reference counting).
    const std::string& str() const { return *static_cast<const std::string*>(ref_.get()); }
    const ObjectHandle* objectPtr() const { return kind_ == Kind::Object ? static_cast<const ObjectHandle*>(ref_.get()) : nullptr; }
    ArrayData* arrayPtr() const { return kind_ == Kind::Array ? static_cast<ArrayData*>(ref_.get()) : nullptr; }
    MapData* mapPtr() const { return kind_ == Kind::Map ? static_cast<MapData*>(ref_.get()) : nullptr; }
    // Shared accessors.
    ObjectRef object() const { return kind_ == Kind::Object ? std::static_pointer_cast<ObjectHandle>(ref_) : ObjectRef{}; }
    ArrayRef array() const { return kind_ == Kind::Array ? std::static_pointer_cast<ArrayData>(ref_) : ArrayRef{}; }
    MapRef map() const { return kind_ == Kind::Map ? std::static_pointer_cast<MapData>(ref_) : MapRef{}; }

    // In place assignment of a plain value (no temporary, no reference count traffic for the common case).
    FG_FORCEINLINE void setBool(bool b) { if (ref_) ref_.reset(); kind_ = Kind::Bool; u_.i = b ? 1 : 0; }
    FG_FORCEINLINE void setInt(int64_t i) { if (ref_) ref_.reset(); kind_ = Kind::Int; u_.i = i; }
    FG_FORCEINLINE void setFloat(double d) { if (ref_) ref_.reset(); kind_ = Kind::Float; u_.d = d; }
    // Arrays and maps have reference semantics (see deepCopy).
    bool isContainer() const { return kind_ == Kind::Array || kind_ == Kind::Map; }
    // Valid for Kind::Int / Kind::Bool only.
    int64_t rawInt() const { return u_.i; }

    // Copies containers (recursively); every other kind is returned as is.
    Value deepCopy() const { return (kind_ == Kind::Array || kind_ == Kind::Map) ? deepCopySlow() : *this; }

    // Identity of the referenced storage (used to detect aliasing); null for plain values.
    const void* identity() const { return ref_.get(); }

    friend bool operator==(const Value& a, const Value& b) {
        if (a.kind_ == Kind::Int && b.kind_ == Kind::Int) return a.u_.i == b.u_.i;
        return equalsSlow(a, b);
    }
    friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }

private:
    static bool equalsSlow(const Value& a, const Value& b);
    bool toBoolSlow() const;
    int64_t toIntSlow() const;
    double toFloatSlow() const;
    Value deepCopySlow() const;

    Kind kind_ = Kind::Null;
    union Payload {
        int64_t i;
        double d;
        Payload() : i(0) {}
    } u_;
    std::shared_ptr<void> ref_;
};

// Total order used for map keys (null < bool < numbers < strings < objects).
struct ValueLess {
    bool operator()(const Value& a, const Value& b) const {
        if (a.isInt() && b.isInt()) return a.rawInt() < b.rawInt();
        return slow(a, b);
    }
    static bool slow(const Value& a, const Value& b);
};

struct ArrayData {
    const TypeDesc* elem = nullptr;     // element type (null = unconstrained)
    std::vector<Value> items;
};
struct MapData {
    const TypeDesc* key = nullptr;
    const TypeDesc* val = nullptr;
    std::map<Value, Value, ValueLess> items;
};

} // namespace fg
