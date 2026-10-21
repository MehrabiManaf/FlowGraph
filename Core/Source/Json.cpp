// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Json.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fg {

const Json& Json::null() { static const Json n; return n; }
const std::string& Json::emptyString() { static const std::string s; return s; }

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (auto& m : o_) if (m.first == key) return &m.second;
    return nullptr;
}
Json* Json::find(const std::string& key) {
    if (type_ != Type::Object) return nullptr;
    for (auto& m : o_) if (m.first == key) return &m.second;
    return nullptr;
}
Json& Json::set(const std::string& key, Json v) {
    if (type_ != Type::Object) become(Type::Object);
    if (auto* p = find(key)) { *p = std::move(v); return *p; }
    o_.emplace_back(key, std::move(v));
    return o_.back().second;
}
Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) become(Type::Object);
    if (auto* p = find(key)) return *p;
    o_.emplace_back(key, Json());
    return o_.back().second;
}

bool operator==(const Json& a, const Json& b) {
    if (a.type_ != b.type_) {
        if (a.isNumber() && b.isNumber()) return a.asFloat() == b.asFloat();
        return false;
    }
    switch (a.type_) {
        case Json::Type::Null: return true;
        case Json::Type::Bool:
        case Json::Type::Int: return a.i_ == b.i_;
        case Json::Type::Float: return a.d_ == b.d_;
        case Json::Type::String: return a.s_ == b.s_;
        case Json::Type::Array: return a.a_ == b.a_;
        case Json::Type::Object: return a.o_ == b.o_;
    }
    return false;
}

// ------------------------------------------------------------------ writer
static void escapeInto(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
}

static void newline(std::string& out, int indent, int level) {
    if (indent <= 0) return;
    out.push_back('\n');
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(level), ' ');
}

void Json::write(std::string& out, int indent, int level) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += i_ ? "true" : "false"; break;
        case Type::Int: out += std::to_string(i_); break;
        case Type::Float: {
            if (!std::isfinite(d_)) { out += "null"; break; }
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof buf, d_);
            std::string t(buf, r.ptr);
            if (t.find_first_of(".eE") == std::string::npos) t += ".0";
            out += t;
            break;
        }
        case Type::String: escapeInto(out, s_); break;
        case Type::Array: {
            if (a_.empty()) { out += "[]"; break; }
            // arrays of scalars stay on one line
            bool flat = true;
            for (auto& v : a_) if (v.isArray() || v.isObject()) { flat = false; break; }
            out.push_back('[');
            for (size_t i = 0; i < a_.size(); ++i) {
                if (i) out += flat && indent > 0 ? ", " : ",";
                if (!flat) newline(out, indent, level + 1);
                a_[i].write(out, indent, level + 1);
            }
            if (!flat) newline(out, indent, level);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (o_.empty()) { out += "{}"; break; }
            out.push_back('{');
            for (size_t i = 0; i < o_.size(); ++i) {
                if (i) out.push_back(',');
                newline(out, indent, level + 1);
                escapeInto(out, o_[i].first);
                out += indent > 0 ? ": " : ":";
                o_[i].second.write(out, indent, level + 1);
            }
            newline(out, indent, level);
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    return out;
}

// ------------------------------------------------------------------ parser
namespace {
struct Parser {
    const char* p;
    const char* end;
    const char* begin;

    [[noreturn]] void fail(const std::string& msg) const {
        // compute line/column for helpful diagnostics
        int line = 1, col = 1;
        for (const char* c = begin; c < p; ++c) { if (*c == '\n') { ++line; col = 1; } else ++col; }
        throw JsonError("JSON error at line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + msg);
    }
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool eat(char c) { ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    void expect(char c) { if (!eat(c)) fail(std::string("expected '") + c + "'"); }

    static void appendUtf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) s.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    uint32_t hex4() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= end) fail("bad \\u escape");
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }
    std::string str() {
        expect('"');
        std::string s;
        while (true) {
            if (p >= end) fail("unterminated string");
            char c = *p++;
            if (c == '"') break;
            if (c != '\\') { s.push_back(c); continue; }
            if (p >= end) fail("unterminated string");
            char e = *p++;
            switch (e) {
                case '"': s.push_back('"'); break;
                case '\\': s.push_back('\\'); break;
                case '/': s.push_back('/'); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < end && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        uint32_t lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(s, cp);
                    break;
                }
                default: fail("bad escape sequence");
            }
        }
        return s;
    }
    Json number() {
        const char* s = p;
        bool isFloat = false;
        if (p < end && *p == '-') ++p;
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        if (p < end && *p == '.') { isFloat = true; ++p; while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p; }
        if (p < end && (*p == 'e' || *p == 'E')) {
            isFloat = true; ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        }
        if (s == p) fail("unexpected character");
        std::string t(s, p);
        if (!isFloat) {
            int64_t v = 0;
            auto r = std::from_chars(t.data(), t.data() + t.size(), v);
            if (r.ec == std::errc()) return Json(v);
        }
        return Json(std::strtod(t.c_str(), nullptr));
    }
    bool lit(const char* w) {
        size_t n = std::strlen(w);
        if (static_cast<size_t>(end - p) >= n && std::strncmp(p, w, n) == 0) { p += n; return true; }
        return false;
    }
    Json value(int depth) {
        if (depth > 200) fail("nesting too deep");
        ws();
        if (p >= end) fail("unexpected end of input");
        char c = *p;
        if (c == '{') {
            ++p;
            Json o = Json::object();
            if (eat('}')) return o;
            while (true) {
                ws();
                std::string k = str();
                expect(':');
                o.members().emplace_back(std::move(k), value(depth + 1));
                if (eat(',')) continue;
                expect('}');
                return o;
            }
        }
        if (c == '[') {
            ++p;
            Json a = Json::array();
            if (eat(']')) return a;
            while (true) {
                a.items().push_back(value(depth + 1));
                if (eat(',')) continue;
                expect(']');
                return a;
            }
        }
        if (c == '"') return Json(str());
        if (lit("true")) return Json(true);
        if (lit("false")) return Json(false);
        if (lit("null")) return Json();
        return number();
    }
};
} // namespace

Json Json::parse(const std::string& text) {
    Parser ps{text.data(), text.data() + text.size(), text.data()};
    // skip UTF-8 BOM
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        ps.p += 3;
    Json v = ps.value(0);
    ps.ws();
    if (ps.p != ps.end) ps.fail("trailing characters");
    return v;
}

} // namespace fg
