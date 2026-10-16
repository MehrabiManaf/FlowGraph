#include <FlowGraph/Script.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>

namespace fg {

// ------------------------------------------------------------------ Graph
Node& Graph::addNode(const std::string& defId, double x, double y) {
    Node n;
    n.id = nextId++;
    n.def = defId;
    n.x = x;
    n.y = y;
    nodes.push_back(std::move(n));
    return nodes.back();
}

Node* Graph::findNode(NodeId id) {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}
const Node* Graph::findNode(NodeId id) const {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}
int Graph::nodeIndex(NodeId id) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id) return static_cast<int>(i);
    return -1;
}
VariableDecl* Graph::findLocal(const std::string& n) {
    for (auto& v : locals)
        if (v.name == n) return &v;
    return nullptr;
}
const VariableDecl* Graph::findLocal(const std::string& n) const {
    for (auto& v : locals)
        if (v.name == n) return &v;
    return nullptr;
}
CommentBox& Graph::addComment(double x, double y, double w, double h) {
    CommentBox c;
    c.id = nextCommentId++;
    c.x = x; c.y = y; c.w = w; c.h = h;
    comments.push_back(std::move(c));
    return comments.back();
}
CommentBox* Graph::findComment(int id) {
    for (auto& c : comments)
        if (c.id == id) return &c;
    return nullptr;
}

// ------------------------------------------------------------------ Script
int Script::graphIndex(const std::string& n) const {
    for (size_t i = 0; i < graphs.size(); ++i)
        if (graphs[i].name == n) return static_cast<int>(i);
    return -1;
}
int Script::variableIndex(const std::string& n) const {
    for (size_t i = 0; i < variables.size(); ++i)
        if (variables[i].name == n) return static_cast<int>(i);
    return -1;
}
Graph* Script::findGraph(const std::string& n) {
    int i = graphIndex(n);
    return i < 0 ? nullptr : &graphs[static_cast<size_t>(i)];
}
const Graph* Script::findGraph(const std::string& n) const {
    int i = graphIndex(n);
    return i < 0 ? nullptr : &graphs[static_cast<size_t>(i)];
}
VariableDecl* Script::findVariable(const std::string& n) {
    int i = variableIndex(n);
    return i < 0 ? nullptr : &variables[static_cast<size_t>(i)];
}
const VariableDecl* Script::findVariable(const std::string& n) const {
    int i = variableIndex(n);
    return i < 0 ? nullptr : &variables[static_cast<size_t>(i)];
}

Graph& Script::addEventGraph(const std::string& n) {
    graphs.emplace_back();
    Graph& g = graphs.back();
    g.name = uniqueGraphName(n);
    g.kind = GraphKind::Event;
    return g;
}

Graph& Script::addFunction(const std::string& n, std::vector<ParamDecl> inputs, std::vector<ParamDecl> outputs) {
    std::string unique = uniqueGraphName(n);
    graphs.emplace_back();
    Graph& g = graphs.back();
    g.name = unique;
    g.kind = GraphKind::Function;
    g.inputs = std::move(inputs);
    g.outputs = std::move(outputs);
    NodeId entry = g.addNode("func.entry", 0, 0).id;
    NodeId ret = g.addNode("func.return", 420, 0).id;
    g.connections.push_back({entry, "Then", ret, "In"});
    return g;
}

bool Script::removeGraph(const std::string& n) {
    int i = graphIndex(n);
    if (i < 0) return false;
    graphs.erase(graphs.begin() + i);
    return true;
}

static std::string uniqueName(const std::string& base, const std::function<bool(const std::string&)>& taken) {
    if (!taken(base)) return base;
    for (int i = 1;; ++i) {
        std::string c = base + "_" + std::to_string(i);
        if (!taken(c)) return c;
    }
}

std::string Script::uniqueGraphName(const std::string& base) const {
    return uniqueName(base, [&](const std::string& n) { return graphIndex(n) >= 0; });
}
std::string Script::uniqueVariableName(const std::string& base) const {
    return uniqueName(base, [&](const std::string& n) { return variableIndex(n) >= 0; });
}

void Script::renameVariable(const std::string& from, const std::string& to) {
    if (from == to) return;
    if (auto* v = findVariable(from)) v->name = to;
    for (auto& g : graphs) {
        bool shadowed = g.findLocal(from) != nullptr;
        for (auto& n : g.nodes) {
            if ((n.def != "var.get" && n.def != "var.set") || n.prop("name") != from) continue;
            std::string scope = n.prop("scope");
            if (scope == "global" || (scope.empty() && !shadowed)) n.props["name"] = to;
        }
    }
}

void Script::renameFunction(const std::string& from, const std::string& to) {
    if (from == to) return;
    if (auto* g = findGraph(from)) g->name = to;
    for (auto& g : graphs)
        for (auto& n : g.nodes)
            if (n.def == "func.call" && n.prop("function") == from) n.props["function"] = to;
}

void Script::renameLocal(const std::string& graph, const std::string& from, const std::string& to) {
    if (from == to) return;
    Graph* g = findGraph(graph);
    if (!g) return;
    if (auto* v = g->findLocal(from)) v->name = to;
    for (auto& n : g->nodes)
        if ((n.def == "var.get" || n.def == "var.set") && n.prop("name") == from && n.prop("scope") == "local") n.props["name"] = to;
}

// ------------------------------------------------------------------ JSON
Json valueToJson(const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Null:
    case Value::Kind::Object: return Json();
    case Value::Kind::Bool: return Json(v.toBool());
    case Value::Kind::Int: return Json(static_cast<int64_t>(v.toInt()));
    case Value::Kind::Float: return Json(v.toFloat());
    case Value::Kind::String: return Json(v.str());
    case Value::Kind::Array: {
        Json a = Json::array();
        for (auto& e : v.arrayPtr()->items) a.push(valueToJson(e));
        return a;
    }
    case Value::Kind::Map: {
        Json pairs = Json::array();
        for (auto& [k, val] : v.mapPtr()->items) {
            Json p = Json::array();
            p.push(valueToJson(k));
            p.push(valueToJson(val));
            pairs.push(std::move(p));
        }
        Json o = Json::object();
        o.set("$map", std::move(pairs));
        return o;
    }
    }
    return Json();
}

Value jsonToValue(const Json& j) {
    switch (j.type()) {
    case Json::Type::Null: return Value();
    case Json::Type::Bool: return Value(j.asBool());
    case Json::Type::Int: return Value(j.asInt());
    case Json::Type::Float: return Value(j.asFloat());
    case Json::Type::String: return Value(j.asString());
    case Json::Type::Array: {
        auto a = std::make_shared<ArrayData>();
        for (auto& e : j.items()) a->items.push_back(jsonToValue(e));
        return Value(std::move(a));
    }
    case Json::Type::Object: {
        auto m = std::make_shared<MapData>();
        if (auto* pairs = j.find("$map"))
            for (auto& p : pairs->items()) m->items.emplace(jsonToValue(p[size_t(0)]), jsonToValue(p[1]));
        return Value(std::move(m));
    }
    }
    return Value();
}

namespace {

Json paramToJson(const ParamDecl& p) {
    Json j = Json::object();
    j.set("name", p.name);
    j.set("type", p.type);
    if (p.byRef) j.set("byRef", true);
    if (p.isConst) j.set("const", true);
    if (!p.def.isNull()) j.set("default", valueToJson(p.def));
    return j;
}
ParamDecl paramFromJson(const Json& j) {
    ParamDecl p;
    p.name = j["name"].asString();
    p.type = j["type"].asString("any");
    p.byRef = j["byRef"].asBool();
    p.isConst = j["const"].asBool();
    if (auto* d = j.find("default")) p.def = jsonToValue(*d);
    return p;
}
Json variableToJson(const VariableDecl& v) {
    Json j = Json::object();
    j.set("name", v.name);
    j.set("type", v.type);
    if (!v.def.isNull()) j.set("default", valueToJson(v.def));
    if (!v.category.empty()) j.set("category", v.category);
    if (!v.doc.empty()) j.set("doc", v.doc);
    return j;
}
VariableDecl variableFromJson(const Json& j) {
    VariableDecl v;
    v.name = j["name"].asString();
    v.type = j["type"].asString("any");
    if (auto* d = j.find("default")) v.def = jsonToValue(*d);
    v.category = j["category"].asString();
    v.doc = j["doc"].asString();
    return v;
}
Json stringMapToJson(const std::map<std::string, std::string>& m) {
    Json o = Json::object();
    for (auto& [k, v] : m) o.set(k, v);
    return o;
}
void stringMapFromJson(const Json& j, std::map<std::string, std::string>& m) {
    for (auto& [k, v] : j.members()) m[k] = v.asString();
}

Json nodeToJson(const Node& n) {
    Json j = Json::object();
    j.set("id", n.id);
    j.set("def", n.def);
    j.set("x", n.x);
    j.set("y", n.y);
    if (!n.comment.empty()) j.set("comment", n.comment);
    if (!n.props.empty()) j.set("props", stringMapToJson(n.props));
    if (!n.typeArgs.empty()) j.set("typeArgs", stringMapToJson(n.typeArgs));
    if (!n.defaults.empty()) {
        Json o = Json::object();
        for (auto& [k, v] : n.defaults) o.set(k, valueToJson(v));
        j.set("defaults", std::move(o));
    }
    return j;
}
Node nodeFromJson(const Json& j) {
    Node n;
    n.id = static_cast<NodeId>(j["id"].asInt());
    n.def = j["def"].asString();
    n.x = j["x"].asFloat();
    n.y = j["y"].asFloat();
    n.comment = j["comment"].asString();
    stringMapFromJson(j["props"], n.props);
    stringMapFromJson(j["typeArgs"], n.typeArgs);
    for (auto& [k, v] : j["defaults"].members()) n.defaults[k] = jsonToValue(v);
    return n;
}

Json graphToJson(const Graph& g) {
    Json j = Json::object();
    j.set("name", g.name);
    j.set("kind", g.kind == GraphKind::Event ? "event" : "function");
    if (!g.doc.empty()) j.set("doc", g.doc);
    if (g.kind == GraphKind::Function) {
        Json in = Json::array(), out = Json::array(), loc = Json::array();
        for (auto& p : g.inputs) in.push(paramToJson(p));
        for (auto& p : g.outputs) out.push(paramToJson(p));
        for (auto& v : g.locals) loc.push(variableToJson(v));
        j.set("inputs", std::move(in));
        j.set("outputs", std::move(out));
        if (loc.size()) j.set("locals", std::move(loc));
    }
    Json nodes = Json::array();
    for (auto& n : g.nodes) nodes.push(nodeToJson(n));
    j.set("nodes", std::move(nodes));
    Json conns = Json::array();
    for (auto& c : g.connections) {
        Json cj = Json::object();
        Json from = Json::array(), to = Json::array();
        from.push(c.fromNode); from.push(c.fromPin);
        to.push(c.toNode); to.push(c.toPin);
        cj.set("from", std::move(from));
        cj.set("to", std::move(to));
        conns.push(std::move(cj));
    }
    j.set("connections", std::move(conns));
    if (!g.comments.empty()) {
        Json cs = Json::array();
        for (auto& c : g.comments) {
            Json cj = Json::object();
            cj.set("id", c.id);
            cj.set("text", c.text);
            cj.set("x", c.x); cj.set("y", c.y); cj.set("w", c.w); cj.set("h", c.h);
            cj.set("color", static_cast<int64_t>(c.color));
            cs.push(std::move(cj));
        }
        j.set("comments", std::move(cs));
    }
    return j;
}

Graph graphFromJson(const Json& j) {
    Graph g;
    g.name = j["name"].asString("Graph");
    g.kind = j["kind"].asString() == "function" ? GraphKind::Function : GraphKind::Event;
    g.doc = j["doc"].asString();
    for (auto& p : j["inputs"].items()) g.inputs.push_back(paramFromJson(p));
    for (auto& p : j["outputs"].items()) g.outputs.push_back(paramFromJson(p));
    for (auto& v : j["locals"].items()) g.locals.push_back(variableFromJson(v));
    for (auto& n : j["nodes"].items()) {
        g.nodes.push_back(nodeFromJson(n));
        g.nextId = std::max(g.nextId, g.nodes.back().id + 1);
    }
    for (auto& c : j["connections"].items()) {
        Connection cn;
        cn.fromNode = static_cast<NodeId>(c["from"][size_t(0)].asInt());
        cn.fromPin = c["from"][1].asString();
        cn.toNode = static_cast<NodeId>(c["to"][size_t(0)].asInt());
        cn.toPin = c["to"][1].asString();
        g.connections.push_back(std::move(cn));
    }
    for (auto& c : j["comments"].items()) {
        CommentBox cb;
        cb.id = static_cast<int>(c["id"].asInt());
        cb.text = c["text"].asString();
        cb.x = c["x"].asFloat(); cb.y = c["y"].asFloat();
        cb.w = c["w"].asFloat(300); cb.h = c["h"].asFloat(160);
        cb.color = static_cast<uint32_t>(c["color"].asInt(0x4a6fa5));
        g.nextCommentId = std::max(g.nextCommentId, cb.id + 1);
        g.comments.push_back(std::move(cb));
    }
    return g;
}

} // namespace

Json Script::toJson() const {
    Json j = Json::object();
    j.set("format", "flowgraph");
    j.set("version", 1);
    j.set("name", name);
    if (!doc.empty()) j.set("doc", doc);
    Json vars = Json::array();
    for (auto& v : variables) vars.push(variableToJson(v));
    j.set("variables", std::move(vars));
    Json gs = Json::array();
    for (auto& g : graphs) gs.push(graphToJson(g));
    j.set("graphs", std::move(gs));
    return j;
}

Script Script::fromJson(const Json& j) {
    if (!j.isObject()) throw JsonError("a script document must be a JSON object");
    if (j["format"].asString() != "flowgraph") throw JsonError("not a FlowGraph script (missing \"format\": \"flowgraph\")");
    Script s;
    s.name = j["name"].asString("Script");
    s.doc = j["doc"].asString();
    for (auto& v : j["variables"].items()) s.variables.push_back(variableFromJson(v));
    for (auto& g : j["graphs"].items()) s.graphs.push_back(graphFromJson(g));
    return s;
}

Script Script::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

void Script::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << toText();
    if (!out) throw std::runtime_error("error while writing '" + path + "'");
}

} // namespace fg
