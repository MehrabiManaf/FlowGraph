// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Translations.hpp>

#include <FlowGraph/Json.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>

#include <fstream>
#include <set>
#include <sstream>

namespace fg {

namespace {

const char* kGetVariable = "Get {Name}";
const char* kSetVariable = "Set {Name}";
const char* kSwitchOnEnum = "Switch on {Enum}";

std::string replaceAll(std::string s, const std::string& what, const std::string& with) {
    for (size_t p = 0; (p = s.find(what, p)) != std::string::npos; p += with.size()) s.replace(p, what.size(), with);
    return s;
}

std::string propOr(const std::map<std::string, std::string>& props, const std::string& key, const std::string& def = std::string()) {
    auto it = props.find(key);
    return it == props.end() ? def : it->second;
}

} // namespace

bool Translations::loadJson(const std::string& text, std::string* error) {
    try {
        Json j = Json::parse(text);
        const Json& texts = j["texts"];
        if (!j.isObject() || !texts.isObject()) {
            if (error) *error = "not a catalog: an object with \"texts\" is expected";
            return false;
        }
        for (auto& [key, value] : texts.members()) {
            if (!value.isArray() || value.size() < 2) continue;
            const std::string& translation = value[size_t(1)].asString();
            if (translation.empty()) continue;
            texts_[key] = {value[size_t(0)].asString(), translation};
        }
        if (j["language"].isString()) language_ = j["language"].asString();
        return true;
    } catch (const JsonError& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool Translations::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return loadJson(ss.str(), error);
}

void Translations::clear() {
    texts_.clear();
    language_.clear();
}

std::string Translations::text(const std::string& key, const std::string& english) const {
    auto it = texts_.find(key);
    // a translation made for another English text is outdated: the English text shows
    return it != texts_.end() && it->second.first == english ? it->second.second : english;
}

std::string Translations::nodeTitle(const NodeDef* def, const std::map<std::string, std::string>& props, const std::string& english) const {
    if (!def) return english;
    // the same titles nodeTitle (Compiler.hpp) gives, in the language of the catalog; the names of the script stay as they are
    if (def->op == NodeOp::CallFunction) return english;
    if (def->op == NodeOp::GetVariable) return replaceAll(text("template:get-variable", kGetVariable), "{Name}", propOr(props, "name"));
    if (def->op == NodeOp::SetVariable) return replaceAll(text("template:set-variable", kSetVariable), "{Name}", propOr(props, "name"));
    if (def->id.rfind("enum:", 0) == 0) {
        std::string v = propOr(props, "value");
        if (v.empty() && !def->settings.empty()) v = def->settings.front().def;
        return v.empty() ? def->title : def->title + "." + enumValue(def->id.substr(5), v);      // the type's name is code: it stays
    }
    if (def->id == "flow.switch.enum" && !propOr(props, "enum").empty())
        return replaceAll(text("template:switch-on-enum", kSwitchOnEnum), "{Enum}", propOr(props, "enum"));
    // the definition's title is translated; a title of its own (a palette entry made by `expand`) stays
    return english.empty() || english == def->title ? text("node:" + def->id, def->title) : english;
}

std::string Translations::nodeDoc(const NodeDef& def) const { return text("node:" + def.id + "#doc", def.doc); }

namespace {
// "Then 3" -> "Then {N}" with number "3": the pins a node makes from a count (Sequence) share one text; "" when it is not numbered
std::string numberedPattern(const std::string& pin, std::string* number = nullptr) {
    const size_t space = pin.find_last_of(' ');
    if (space == std::string::npos || space + 1 >= pin.size()) return {};
    for (size_t i = space + 1; i < pin.size(); ++i)
        if (pin[i] < '0' || pin[i] > '9') return {};
    if (number) *number = pin.substr(space + 1);
    return pin.substr(0, space) + " {N}";
}
} // namespace

std::string Translations::pin(const std::string& defId, const std::string& pin) const {
    const std::string key = "pin:" + defId + "/" + pin;
    if (texts_.count(key)) return text(key, pin);
    std::string number;
    const std::string pattern = numberedPattern(pin, &number);
    if (!pattern.empty()) return replaceAll(text("pin:" + defId + "/" + pattern, pattern), "{N}", number);
    return pin;
}

std::string Translations::pin(const NodeDef* def, const std::map<std::string, std::string>& props, const std::string& pin) const {
    // the outputs of a Switch on Enum are the values of its enum
    if (def && def->id == "flow.switch.enum" && pin != "Selection" && pin != "In") return enumValue(propOr(props, "enum"), pin);
    return this->pin(def ? def->id : std::string(), pin);
}

std::string Translations::pinDoc(const std::string& defId, const std::string& pin, const std::string& english) const {
    return text("pin:" + defId + "/" + pin + "#doc", english);
}

std::string Translations::setting(const std::string& defId, const NodeSetting& s) const {
    return text("setting:" + defId + "/" + s.key, s.label.empty() ? s.key : s.label);
}

std::string Translations::settingDoc(const std::string& defId, const NodeSetting& s) const {
    return text("setting:" + defId + "/" + s.key + "#doc", s.doc);
}

std::string Translations::category(const std::string& path) const {
    std::string out;
    size_t start = 0;
    for (;;) {
        const size_t bar = path.find('|', start);
        const std::string part = path.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        out += text("category:" + part, part);
        if (bar == std::string::npos) break;
        out += '|';
        start = bar + 1;
    }
    return out;
}

std::string Translations::enumValue(const std::string& type, const std::string& value) const {
    return text("enum:" + type + "." + value, value);
}

std::string Translations::enumValueDoc(const std::string& type, const std::string& value, const std::string& english) const {
    return text("enum:" + type + "." + value + "#doc", english);
}

// ------------------------------------------------------------------ catalogs

std::vector<std::pair<std::string, std::string>> translatableTexts(const Registry& reg) {
    std::vector<std::pair<std::string, std::string>> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& key, const std::string& english) {
        if (!english.empty() && seen.insert(key).second) out.emplace_back(key, english);
    };
    add("template:get-variable", kGetVariable);
    add("template:set-variable", kSetVariable);
    add("template:switch-on-enum", kSwitchOnEnum);
    std::set<std::string> categories;
    for (const NodeDef* def : reg.nodes()) {
        const std::string& id = def->id;
        // an enum literal is titled with its type (code) and its value (enum:<Type>.<Value>)
        if (id.rfind("enum:", 0) != 0) add("node:" + id, def->title);
        add("node:" + id + "#doc", def->doc);
        for (const PinDef& p : def->pins) {
            if (p.name.empty()) continue;
            add("pin:" + id + "/" + p.name, p.name);
            add("pin:" + id + "/" + p.name + "#doc", p.doc);
        }
        // the pins a node makes from its settings (with their defaults): "Then 0", "Then 1" give one text "Then {N}"; pins without
        // letters ("0", "1") need none; the outputs of a Switch on Enum are enum values (translated as such)
        if (def->dynamicPins) {
            Node n;
            n.def = id;
            const TypeDesc* cases = nullptr;
            if (id == "flow.switch.enum")
                for (const TypeDesc* t : reg.types())
                    if (t->isEnum() && !cases) cases = t;
            if (cases) n.props["enum"] = cases->name;
            std::vector<PinDef> made;
            def->dynamicPins(n, NodeBuildCtx{reg, nullptr, nullptr}, made);
            for (const PinDef& p : made) {
                if (p.name.empty() || (cases && p.dir == PinDir::Out)) continue;
                const std::string pattern = numberedPattern(p.name);
                const std::string name = pattern.empty() ? p.name : pattern;
                if (name.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") == std::string::npos) continue;
                add("pin:" + id + "/" + name, name);
                if (pattern.empty()) add("pin:" + id + "/" + name + "#doc", p.doc);
            }
        }
        for (const NodeSetting& s : def->settings) {
            add("setting:" + id + "/" + s.key, s.label.empty() ? s.key : s.label);
            add("setting:" + id + "/" + s.key + "#doc", s.doc);
        }
        size_t start = 0;
        for (;;) {
            const size_t bar = def->category.find('|', start);
            categories.insert(def->category.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    }
    for (const std::string& c : categories) add("category:" + c, c);
    for (const TypeDesc* t : reg.types()) {
        if (!t->isEnum()) continue;
        for (const EnumValueDesc& v : t->enumValues) {
            add("enum:" + t->name + "." + v.name, v.name);
            add("enum:" + t->name + "." + v.name + "#doc", v.doc);
        }
    }
    return out;
}

std::string updateCatalog(const Registry& reg, const std::string& existing, const std::string& language, CatalogReport* report) {
    CatalogReport r;
    std::map<std::string, std::pair<std::string, std::string>> old;      // key -> (English, translation)
    if (!existing.empty()) {
        Json j = Json::parse(existing);
        for (auto& [key, value] : j["texts"].members())
            if (value.isArray() && value.size() >= 2) old[key] = {value[size_t(0)].asString(), value[size_t(1)].asString()};
    }
    const auto texts = translatableTexts(reg);
    std::set<std::string> keys;
    // one text per line, so that a catalog is easy to edit and its changes are easy to read
    std::string out = "{\n  \"language\": " + Json(language).dump(0) + ",\n  \"texts\": {\n";
    for (size_t i = 0; i < texts.size(); ++i) {
        const auto& [key, english] = texts[i];
        keys.insert(key);
        std::string translation, previous;
        if (auto it = old.find(key); it == old.end()) ++r.added;
        else if (it->second.first == english) translation = it->second.second;
        else if (!it->second.second.empty()) {
            previous = it->second.second;              // kept as a hint for the translator: it translated the old English text
            ++r.outdated;
            r.outdatedKeys.push_back(key);
        }
        if (!translation.empty()) ++r.translated;
        out += "    " + Json(key).dump(0) + ": [" + Json(english).dump(0) + ", " + Json(translation).dump(0);
        if (!previous.empty()) out += ", " + Json(previous).dump(0);
        out += i + 1 < texts.size() ? "],\n" : "]\n";
    }
    out += "  }\n}\n";
    for (auto& [key, value] : old)
        if (!keys.count(key)) ++r.removed;
    r.texts = texts.size();
    if (report) *report = r;
    return out;
}

} // namespace fg
