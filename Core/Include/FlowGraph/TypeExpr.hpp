#pragma once
// Type expressions: "int", "Actor", "array<T>", "map<string,array<float>>".
#include <map>
#include <string>
#include <vector>

namespace fg {

struct TypeExpr {
    std::string name;                 // "int", "array", "map", "Actor", "T", ...
    std::vector<TypeExpr> args;       // container arguments
    bool valid = true;

    bool isArray() const { return name == "array" && args.size() == 1; }
    bool isMap() const { return name == "map" && args.size() == 2; }
    bool isContainer() const { return isArray() || isMap(); }
    bool isPlain() const { return args.empty(); }

    std::string str() const;
    static TypeExpr parse(const std::string& text);
    static TypeExpr make(const std::string& name) { TypeExpr e; e.name = name; return e; }
    static TypeExpr arrayOf(TypeExpr e) { TypeExpr r; r.name = "array"; r.args.push_back(std::move(e)); return r; }
    static TypeExpr mapOf(TypeExpr k, TypeExpr v) { TypeExpr r; r.name = "map"; r.args.push_back(std::move(k)); r.args.push_back(std::move(v)); return r; }

    // Replaces every plain name found in `subst` (type parameters) by its value.
    TypeExpr substitute(const std::map<std::string, std::string>& subst) const;
    // True when the expression mentions one of the given type parameter names.
    bool mentions(const std::string& param) const;
};

inline std::string containerTypeName(const std::string& kind, const std::string& key, const std::string& val) {
    return kind == "map" ? "map<" + key + "," + val + ">" : "array<" + key + ">";
}

} // namespace fg
