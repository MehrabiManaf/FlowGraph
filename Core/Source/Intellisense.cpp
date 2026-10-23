// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Intellisense.hpp>

#include <algorithm>
#include <cctype>

namespace fg {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::vector<NodeTemplate> buildPalette(const NodeBuildCtx& ctx) {
    std::vector<NodeTemplate> out;
    for (const NodeDef* def : ctx.reg.nodes()) {
        if (!def->userCreatable) continue;
        if (ctx.graph) {
            if (def->eventGraphOnly && ctx.graph->kind != GraphKind::Event) continue;
            if (def->functionGraphOnly && ctx.graph->kind != GraphKind::Function) continue;
        }
        if (def->op == NodeOp::Event && ctx.script) {       // an event has one handler: offer only the events that have none yet
            bool handled = false;
            for (auto& g : ctx.script->graphs)
                for (auto& n : g.nodes) handled |= n.def == def->id;
            if (handled) continue;
        }
        size_t first = out.size();
        if (def->expand) {
            def->expand(ctx, out);
        } else {
            NodeTemplate t;
            t.defId = def->id;
            Node probe;
            probe.def = def->id;
            for (auto& p : def->pins) t.pins.push_back(p);
            if (def->dynamicPins) def->dynamicPins(probe, ctx, t.pins);
            out.push_back(std::move(t));
        }
        for (size_t i = first; i < out.size(); ++i) {
            NodeTemplate& t = out[i];
            if (t.defId.empty()) t.defId = def->id;
            if (t.title.empty()) t.title = def->title;
            if (t.category.empty()) t.category = def->category;
            if (t.keywords.empty()) t.keywords = def->keywords;
            if (t.doc.empty()) t.doc = def->doc;
            t.style = def->style;
        }
    }
    return out;
}

static bool pinAccepts(const Registry& reg, const NodeDef* def, const PinDef& p, const PinFilter& f) {
    if (p.kind != f.kind) return false;
    // the template pin must be the opposite end of the dragged one
    if (p.dir == f.dir) return false;
    if (f.kind == PinKind::Exec) return true;
    if (!f.type) return true;
    TypeExpr e = TypeExpr::parse(p.type);
    auto paramOf = [&](const TypeExpr& x) { return def ? def->typeParam(x.name) : nullptr; };
    auto allowedFor = [&](const TypeParam* tp, const TypeDesc* t) {
        return tp->allowed.empty() || std::find(tp->allowed.begin(), tp->allowed.end(), t->name) != tp->allowed.end();
    };
    if (e.isPlain()) {
        if (const TypeParam* tp = paramOf(e)) return allowedFor(tp, f.type) || f.type->kind == TypeKind::Any;
    } else if (e.isArray()) {
        if (f.type->kind == TypeKind::Any) return true;
        if (f.type->kind != TypeKind::Array) return false;
        if (e.args[0].isPlain())
            if (const TypeParam* tp = paramOf(e.args[0])) return allowedFor(tp, f.type->elem);
    } else if (e.isMap()) {
        return f.type->kind == TypeKind::Map || f.type->kind == TypeKind::Any;
    }
    const TypeDesc* t = reg.findType(p.type);
    if (!t) return false;
    const TypeDesc* from = f.dir == PinDir::Out ? f.type : t;
    const TypeDesc* to = f.dir == PinDir::Out ? t : f.type;
    // Every value converts to a string; offered for the primary types, otherwise only by the nodes that print anything (a File
    // would bring every string node with it)
    auto primary = [](const TypeDesc* x) {
        return x->kind == TypeKind::Bool || x->kind == TypeKind::Int || x->kind == TypeKind::Float || x->kind == TypeKind::String ||
               x->kind == TypeKind::Any;
    };
    if (to->kind == TypeKind::String && !primary(from) && !(def && def->printsAnything)) return false;
    return reg.canConnect(from, to);
}

bool templateAccepts(const Registry& reg, const NodeTemplate& t, const PinFilter& f, std::string* pinName) {
    const NodeDef* def = reg.findNode(t.defId);
    for (auto& p : t.pins) {
        if (p.hidden) continue;
        if (pinAccepts(reg, def, p, f)) {
            if (pinName) *pinName = p.name;
            return true;
        }
    }
    return false;
}

std::vector<NodeTemplate> filterPalette(const Registry& reg, const std::vector<NodeTemplate>& all, const PinFilter& f) {
    std::vector<NodeTemplate> out;
    for (auto& t : all)
        if (templateAccepts(reg, t, f)) out.push_back(t);
    return out;
}

std::vector<NodeTemplate> searchPalette(const std::vector<NodeTemplate>& all, const std::string& query) {
    std::vector<std::string> words;
    {
        std::string cur;
        for (char c : lower(query)) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!cur.empty()) words.push_back(std::move(cur));
                cur.clear();
            } else cur.push_back(c);
        }
        if (!cur.empty()) words.push_back(std::move(cur));
    }
    struct Scored { int score; size_t index; };
    std::vector<Scored> hits;
    for (size_t i = 0; i < all.size(); ++i) {
        const NodeTemplate& t = all[i];
        std::string title = lower(t.title), kw = lower(t.keywords), cat = lower(t.category);
        int total = 0;
        bool ok = true;
        for (auto& w : words) {
            int s;
            size_t pos = title.find(w);
            if (pos == 0) s = 0;
            else if (pos != std::string::npos) s = 1;
            else if (kw.find(w) != std::string::npos) s = 2;
            else if (cat.find(w) != std::string::npos) s = 3;
            else { ok = false; break; }
            total += s;
        }
        if (ok) hits.push_back({total, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [&](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score < b.score;
        const NodeTemplate &x = all[a.index], &y = all[b.index];
        if (words.empty() && x.category != y.category) return x.category < y.category;
        if (x.title.size() != y.title.size() && !words.empty()) return x.title.size() < y.title.size();
        return x.title < y.title;
    });
    std::vector<NodeTemplate> out;
    out.reserve(hits.size());
    for (auto& h : hits) out.push_back(all[h.index]);
    return out;
}

Node& instantiate(Graph& graph, const NodeTemplate& t, double x, double y) {
    Node& n = graph.addNode(t.defId, x, y);
    n.props = t.props;
    n.typeArgs = t.typeArgs;
    return n;
}

} // namespace fg
