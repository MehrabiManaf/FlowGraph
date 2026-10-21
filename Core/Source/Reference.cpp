// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Reference.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace fg {

namespace {

// Text for a Markdown table cell.
std::string cell(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += "<br>";
        else out += c;
    }
    return out;
}

// A GitHub style heading anchor.
std::string anchor(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (c == ' ' || c == '-') out += '-';
    }
    return out;
}

std::string categoryTitle(const std::string& c) {
    if (c.empty()) return "Other";
    std::string out = c;
    for (size_t p; (p = out.find('|')) != std::string::npos;) out.replace(p, 1, " / ");
    return out;
}

std::string literal(const Value& v) {
    if (v.isNull()) return "";
    if (v.isString()) return "\"" + v.str() + "\"";
    return v.toString();
}

std::string pinType(const PinDef& p) {
    if (p.kind == PinKind::Exec) return "exec";
    std::string t = "`" + p.type + "`";
    if (p.byRef) t = (p.isConst ? "const " : "") + t + " &";
    else if (p.access == PinAccess::Read) t += " (read)";
    else if (p.access == PinAccess::Modify) t += " (changed in place)";
    return t;
}

std::string kindOf(const NodeDef& d) {
    std::vector<std::string> parts;
    parts.push_back(d.pure ? "pure" : "exec");
    if (d.keywords.find("latent") != std::string::npos || d.id == "flow.delay") parts.push_back("waits");
    if (d.eventGraphOnly) parts.push_back("event graphs only");
    if (d.functionGraphOnly) parts.push_back("function graphs only");
    if (d.customInputs || d.customOutputs) parts.push_back("custom pins");
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? ", " : "") + parts[i];
    return s;
}

void writeNode(std::ostringstream& o, const NodeDef& d) {
    o << "### " << (d.title.empty() ? d.id : d.title) << "\n\n";
    o << "`" << d.id << "` · " << kindOf(d);
    if (!d.source.empty() && d.source != "core") o << " · from " << d.source;
    o << "\n\n";
    if (!d.doc.empty()) o << d.doc << "\n\n";
    if (!d.typeParams.empty()) {
        o << "Type parameters: ";
        for (size_t i = 0; i < d.typeParams.size(); ++i) {
            const TypeParam& t = d.typeParams[i];
            o << (i ? "; " : "") << "`" << t.name << "` ";
            if (t.allowed.empty()) o << "any type";
            else {
                o << "one of ";
                for (size_t k = 0; k < t.allowed.size(); ++k) o << (k ? ", " : "") << "`" << t.allowed[k] << "`";
            }
        }
        o << " (from what is connected)\n\n";
    }
    if (!d.settings.empty()) {
        o << "Settings (Details panel): ";
        for (size_t i = 0; i < d.settings.size(); ++i) {
            const NodeSetting& s = d.settings[i];
            o << (i ? "; " : "") << "**" << s.label << "**";
            if (s.kind == NodeSetting::Kind::Count) o << " (" << s.min << " to " << s.max << ", default " << s.def << ")";
            else if (s.kind == NodeSetting::Kind::Choice) {
                o << " (one of ";
                for (size_t k = 0; k < s.choices.size(); ++k) o << (k ? ", " : "") << s.choices[k];
                o << ")";
            } else o << " (one per line)";
            if (!s.doc.empty()) o << " - " << s.doc;
        }
        o << "\n\n";
    }
    std::vector<const PinDef*> pins;
    for (auto& p : d.pins)
        if (!p.hidden) pins.push_back(&p);
    if (d.dynamicPins) o << "Some pins depend on the node's settings or on the script; the static ones are listed.\n\n";
    if (pins.empty()) return;
    o << "| | Pin | Type | Default | |\n|-|-|-|-|-|\n";
    for (auto* p : pins)
        o << "| " << (p->dir == PinDir::In ? "in" : "out") << " | " << cell(p->name) << " | " << cell(pinType(*p)) << " | "
          << cell(p->dir == PinDir::In ? literal(p->def) : std::string()) << " | " << cell(p->doc) << " |\n";
    o << "\n";
}

} // namespace

std::string nodeReferenceMarkdown(const Registry& reg, const ReferenceOptions& opt) {
    std::map<std::string, std::vector<const NodeDef*>> byCategory;
    size_t count = 0;
    for (const NodeDef* d : reg.nodes()) {
        if (!opt.includeHidden && !d->userCreatable) continue;
        if (!opt.includeGenerated && d->generated) continue;
        if (d->op == NodeOp::Event) continue;                     // listed with the events
        byCategory[categoryTitle(d->category)].push_back(d);
        ++count;
    }
    for (auto& [c, list] : byCategory)
        std::sort(list.begin(), list.end(), [](const NodeDef* a, const NodeDef* b) { return a->title < b->title; });

    std::ostringstream o;
    o << "# " << opt.title << "\n\n";
    if (!opt.intro.empty()) o << opt.intro << "\n\n";
    o << count << " nodes in " << byCategory.size() << " categories.\n\n## Contents\n\n";
    for (auto& [c, list] : byCategory) o << "- [" << c << "](#" << anchor(c) << ") (" << list.size() << ")\n";
    o << "- [Events](#events)\n- [Classes](#classes)\n- [Enums](#enums)\n\n";
    for (auto& [c, list] : byCategory) {
        o << "## " << c << "\n\n";
        for (const NodeDef* d : list) writeNode(o, *d);
    }

    o << "## Events\n\nRaised by the application; a script handles each at most once.\n\n";
    auto events = reg.events();
    std::sort(events.begin(), events.end(), [](const EventDesc* a, const EventDesc* b) { return a->name < b->name; });
    if (events.empty()) o << "This application declares no events.\n\n";
    for (const EventDesc* e : events) {
        o << "### " << e->name << "\n\n";
        if (!e->doc.empty()) o << e->doc << "\n\n";
        if (e->params.empty()) o << "No arguments.\n\n";
        else {
            o << "| Argument | Type |\n|-|-|\n";
            for (auto& p : e->params) o << "| " << cell(p.name) << " | `" << cell(p.type ? p.type->name : "any") << "` |\n";
            o << "\n";
        }
    }

    std::vector<const TypeDesc*> classes, enums;
    for (const TypeDesc* t : reg.types()) {
        if (t->isClass()) classes.push_back(t);
        else if (t->isEnum()) enums.push_back(t);
    }
    auto byName = [](const TypeDesc* a, const TypeDesc* b) { return a->name < b->name; };
    std::sort(classes.begin(), classes.end(), byName);
    std::sort(enums.begin(), enums.end(), byName);
    o << "## Classes\n\n";
    if (classes.empty()) o << "This application reflects no classes.\n\n";
    for (const TypeDesc* t : classes) {
        o << "### " << t->name << "\n\n";
        if (t->base) o << "Derives from " << t->base->name << ".\n\n";
        if (!t->doc.empty()) o << t->doc << "\n\n";
        if (!t->properties.empty()) {
            o << "| Property | Type | |\n|-|-|-|\n";
            for (auto& p : t->properties)
                o << "| " << cell(p.name) << " | `" << cell(p.type ? p.type->name : "any") << "` | " << (p.set ? "" : "read only") << " |\n";
            o << "\n";
        }
        std::vector<std::string> methods;
        for (const FunctionDesc* f : reg.functions())
            if (f->owner == t) methods.push_back(f->title.empty() ? f->name : f->title);
        std::sort(methods.begin(), methods.end());
        if (!methods.empty()) {
            o << "Methods: ";
            for (size_t i = 0; i < methods.size(); ++i) o << (i ? ", " : "") << methods[i];
            o << "\n\n";
        }
    }
    o << "## Enums\n\n";
    if (enums.empty()) o << "This application reflects no enums.\n\n";
    for (const TypeDesc* t : enums) {
        o << "### " << t->name << "\n\n";
        if (!t->doc.empty()) o << t->doc << "\n\n";
        o << "| Value | Number |\n|-|-|\n";
        for (auto& v : t->enumValues) o << "| " << cell(v.name) << " | " << v.value << " |\n";
        o << "\n";
    }
    return o.str();
}

} // namespace fg
