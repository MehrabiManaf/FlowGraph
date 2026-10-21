// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/TypeExpr.hpp>

#include <cctype>

namespace fg {

std::string TypeExpr::str() const {
    if (args.empty()) return name;
    std::string s = name + "<";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) s += ",";
        s += args[i].str();
    }
    return s + ">";
}

namespace {
struct P {
    const std::string& s;
    size_t i = 0;
    void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    bool parse(TypeExpr& out) {
        ws();
        size_t b = i;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.' || s[i] == ':' ||
                                 static_cast<unsigned char>(s[i]) >= 0x80))
            ++i;
        if (b == i) return false;
        out.name = s.substr(b, i - b);
        ws();
        if (i < s.size() && s[i] == '<') {
            ++i;
            while (true) {
                TypeExpr a;
                if (!parse(a)) return false;
                out.args.push_back(std::move(a));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '>') { ++i; break; }
                return false;
            }
        }
        return true;
    }
};
} // namespace

TypeExpr TypeExpr::parse(const std::string& text) {
    TypeExpr e;
    P p{text};
    if (!p.parse(e)) { e = TypeExpr(); e.valid = false; return e; }
    p.ws();
    if (p.i != text.size()) { e = TypeExpr(); e.valid = false; return e; }
    if ((e.name == "array" && e.args.size() != 1) || (e.name == "map" && e.args.size() != 2) ||
        (e.name != "array" && e.name != "map" && !e.args.empty())) {
        e.valid = false;
    }
    return e;
}

TypeExpr TypeExpr::substitute(const std::map<std::string, std::string>& subst) const {
    if (args.empty()) {
        auto it = subst.find(name);
        if (it != subst.end()) return TypeExpr::parse(it->second);
        return *this;
    }
    TypeExpr r;
    r.name = name;
    r.valid = valid;
    for (auto& a : args) r.args.push_back(a.substitute(subst));
    return r;
}

bool TypeExpr::mentions(const std::string& param) const {
    if (args.empty()) return name == param;
    for (auto& a : args) if (a.mentions(param)) return true;
    return false;
}

} // namespace fg
