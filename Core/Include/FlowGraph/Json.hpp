#pragma once
// Minimal, dependency free JSON document model used for script files.
// Object members keep their insertion order so files diff nicely.
#include <FlowGraph/Export.hpp>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fg {

class FG_CORE_EXPORT JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class FG_CORE_EXPORT Json {
public:
    enum class Type : uint8_t { Null, Bool, Int, Float, String, Array, Object };
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), i_(b ? 1 : 0) {}
    Json(int v) : type_(Type::Int), i_(v) {}
    Json(unsigned v) : type_(Type::Int), i_(v) {}
    Json(int64_t v) : type_(Type::Int), i_(v) {}
    Json(uint64_t v) : type_(Type::Int), i_(static_cast<int64_t>(v)) {}
    Json(double v) : type_(Type::Float), d_(v) {}
    Json(const char* s) : type_(Type::String), s_(s ? s : "") {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isInt() const { return type_ == Type::Int; }
    bool isNumber() const { return type_ == Type::Int || type_ == Type::Float; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return type_ == Type::Bool ? i_ != 0 : (type_ == Type::Int ? i_ != 0 : def); }
    int64_t asInt(int64_t def = 0) const {
        return type_ == Type::Int ? i_ : (type_ == Type::Float ? static_cast<int64_t>(d_) : (type_ == Type::Bool ? i_ : def));
    }
    double asFloat(double def = 0) const {
        return type_ == Type::Float ? d_ : (type_ == Type::Int ? static_cast<double>(i_) : def);
    }
    const std::string& asString() const { return type_ == Type::String ? s_ : emptyString(); }
    std::string asString(const std::string& def) const { return type_ == Type::String ? s_ : def; }

    // ---- array access
    size_t size() const { return type_ == Type::Array ? a_.size() : (type_ == Type::Object ? o_.size() : 0); }
    const Array& items() const { return a_; }
    Array& items() { return a_; }
    const Json& operator[](size_t i) const { return i < a_.size() ? a_[i] : null(); }
    Json& push(Json v) { if (type_ != Type::Array) become(Type::Array); a_.push_back(std::move(v)); return a_.back(); }

    // ---- object access
    const Object& members() const { return o_; }
    Object& members() { return o_; }
    bool has(const std::string& key) const { return find(key) != nullptr; }
    const Json* find(const std::string& key) const;
    Json* find(const std::string& key);
    const Json& operator[](const std::string& key) const { auto* p = find(key); return p ? *p : null(); }
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }
    Json& set(const std::string& key, Json v);          // insert or replace
    Json& operator[](const std::string& key);           // insert-or-get (turns null into object)
    Json& operator[](const char* key) { return (*this)[std::string(key)]; }

    // ---- text
    std::string dump(int indent = 2) const;
    static Json parse(const std::string& text);         // throws JsonError

    friend FG_CORE_EXPORT bool operator==(const Json& a, const Json& b);
    friend bool operator!=(const Json& a, const Json& b) { return !(a == b); }

    static const Json& null();

private:
    static const std::string& emptyString();
    void become(Type t) { type_ = t; a_.clear(); o_.clear(); }
    void write(std::string& out, int indent, int level) const;

    Type type_ = Type::Null;
    int64_t i_ = 0;
    double d_ = 0;
    std::string s_;
    Array a_;
    Object o_;
};

} // namespace fg
