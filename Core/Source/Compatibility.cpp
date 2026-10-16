#include <FlowGraph/Compatibility.hpp>

#include <map>
#include <set>

namespace fg {

namespace {

bool isHostEventDef(const std::string& id) { return id.rfind("event:", 0) == 0; }

std::string describeKind(const std::string& defId) {
    auto prefix = [&](const char* p) { return defId.rfind(p, 0) == 0; };
    if (prefix("m:")) return "method";
    if (prefix("fn:")) return "function";
    if (prefix("get:") || prefix("set:")) return "property";
    if (prefix("new:") || prefix("cast:")) return "class";
    if (prefix("enum:")) return "enum";
    return "node";
}

std::string displayName(const std::string& defId) {
    auto colon = defId.find(':');
    return colon == std::string::npos ? defId : defId.substr(colon + 1);
}

} // namespace

std::string describeParams(const std::vector<ParamDecl>& params) {
    std::string s = "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) s += ", ";
        s += params[i].name + ": " + params[i].type;
    }
    return s + ")";
}

std::string describeParams(const EventDesc& ev) {
    std::string s = "(";
    for (size_t i = 0; i < ev.params.size(); ++i) {
        if (i) s += ", ";
        s += ev.params[i].name + ": " + (ev.params[i].type ? ev.params[i].type->name : "any");
    }
    return s + ")";
}

bool hostEventSignatureMatches(const Node& node, const EventDesc& event, const Registry&, std::string* why) {
    auto it = node.props.find("sig");
    if (it == node.props.end()) return true;                      // no signature recorded (script of an older version)
    std::string now = describeParams(event);
    if (it->second == now) return true;
    if (why) *why = "the script was made for " + event.name + it->second + " but this application provides " + event.name + now;
    return false;
}

void stampHostEvents(Script& script, const Registry& reg) {
    for (auto& g : script.graphs)
        for (auto& n : g.nodes) {
            if (!isHostEventDef(n.def) || n.props.count("sig")) continue;
            if (const EventDesc* ev = reg.findEvent(n.def.substr(6))) n.props["sig"] = describeParams(*ev);
        }
}

CompatibilityReport checkCompatibility(const Script& script, const Registry& reg) {
    CompatibilityReport rep;
    auto add = [&](CompatibilityIssue::Kind k, const std::string& graph, NodeId node, const std::string& subject, const std::string& msg) {
        rep.issues.push_back({k, graph, node, subject, msg});
    };
    auto checkType = [&](const std::string& graph, const std::string& what, const std::string& type) {
        if (!reg.findType(type)) add(CompatibilityIssue::Kind::UnknownType, graph, 0, type, "the type '" + type + "' of " + what + " is not registered");
    };
    for (auto& v : script.variables) checkType("", "variable '" + v.name + "'", v.type);
    for (auto& g : script.graphs) {
        for (auto& p : g.inputs) checkType(g.name, "parameter '" + p.name + "'", p.type);
        for (auto& p : g.outputs) checkType(g.name, "output '" + p.name + "'", p.type);
        for (auto& v : g.locals) checkType(g.name, "local variable '" + v.name + "'", v.type);
        for (auto& n : g.nodes) {
            const NodeDef* def = reg.findNode(n.def);
            if (!def) {
                if (isHostEventDef(n.def)) {
                    std::string name = n.def.substr(6);
                    add(CompatibilityIssue::Kind::MissingEvent, g.name, n.id, name, "this application does not provide the event '" + name + "'");
                } else {
                    add(CompatibilityIssue::Kind::MissingNode, g.name, n.id, n.def,
                        "the " + describeKind(n.def) + " '" + displayName(n.def) + "' is not available (missing plugin?)");
                }
                continue;
            }
            if (isHostEventDef(n.def)) {
                if (const EventDesc* ev = reg.findEvent(n.def.substr(6))) {
                    std::string why;
                    if (!hostEventSignatureMatches(n, *ev, reg, &why))
                        add(CompatibilityIssue::Kind::EventSignatureChanged, g.name, n.id, ev->name, "the event '" + ev->name + "' changed: " + why);
                }
            }
        }
    }
    return rep;
}

std::string CompatibilityReport::toText() const {
    // group identical messages
    std::map<std::string, int> counts;
    std::vector<std::string> order;
    for (auto& i : issues) {
        if (counts[i.message]++ == 0) order.push_back(i.message);
    }
    std::string out;
    for (auto& m : order) {
        out += "  - " + m;
        if (counts[m] > 1) out += " (" + std::to_string(counts[m]) + " times)";
        out += "\n";
    }
    return out;
}

std::vector<std::string> CompatibilityReport::missingEvents() const {
    std::set<std::string> names;
    for (auto& i : issues)
        if (i.kind == CompatibilityIssue::Kind::MissingEvent) names.insert(i.subject);
    return {names.begin(), names.end()};
}

} // namespace fg
