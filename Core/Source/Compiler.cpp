#include <FlowGraph/Compiler.hpp>
#include <FlowGraph/Compatibility.hpp>

#include <algorithm>
#include <functional>
#include <set>

namespace fg {

namespace {

using Severity = Diagnostic::Severity;

std::string fallbackOf(const TypeParam& tp) {
    if (!tp.allowed.empty() && std::find(tp.allowed.begin(), tp.allowed.end(), tp.fallback) == tp.allowed.end()) return tp.allowed.front();
    return tp.fallback;
}

// Everything the compiler knows about one node while it is being analysed.
struct Work {
    const Node* node = nullptr;
    const NodeDef* def = nullptr;
    std::map<std::string, std::string> args;   // resolved type parameters
    std::set<std::string> inferred;            // the ones that were not given by the user
    std::vector<PinDef> raw;
    std::vector<TypeExpr> exprs;
    std::vector<CompiledPin> pins;
};

void resolvePins(const Registry& reg, Work& w) {
    w.pins.clear();
    w.exprs.clear();
    if (!w.def) return;
    int ord[2][2] = {{0, 0}, {0, 0}};
    for (auto& pd : w.raw) {
        CompiledPin cp;
        cp.name = pd.name;
        cp.kind = pd.kind;
        cp.dir = pd.dir;
        cp.byRef = pd.byRef;
        cp.isConst = pd.isConst;
        cp.hidden = pd.hidden;
        cp.advanced = pd.advanced;
        cp.doc = pd.doc;
        cp.ordinal = ord[static_cast<int>(pd.kind)][static_cast<int>(pd.dir)]++;
        if (pd.kind == PinKind::Exec) {
            w.exprs.emplace_back();
            w.pins.push_back(std::move(cp));
            continue;
        }
        TypeExpr e = TypeExpr::parse(pd.type);
        std::map<std::string, std::string> eff = w.args;
        for (auto& tp : w.def->typeParams) {
            if (e.mentions(tp.name) && !w.args.count(tp.name)) {
                cp.wildcard = true;
                eff[tp.name] = fallbackOf(tp);
            }
        }
        cp.type = reg.resolveType(e.substitute(eff));
        if (!cp.type) cp.type = reg.tAny();
        if (cp.dir == PinDir::In) {
            Value d = pd.def;
            auto it = w.node->defaults.find(pd.name);
            if (it != w.node->defaults.end()) d = it->second;
            try {
                cp.def = d.isNull() ? reg.defaultValue(cp.type) : reg.convert(d, cp.type);
            } catch (const ScriptError&) {
                cp.def = reg.defaultValue(cp.type);
            }
        }
        w.exprs.push_back(std::move(e));
        w.pins.push_back(std::move(cp));
    }
}

int pinIndex(const std::vector<CompiledPin>& pins, const std::string& name, PinDir dir) {
    for (size_t i = 0; i < pins.size(); ++i)
        if (pins[i].dir == dir && pins[i].name == name) return static_cast<int>(i);
    return -1;
}

// Binds type parameters mentioned by `pat` using the concrete type `actual`. Returns true on change.
bool unify(Work& w, const TypeExpr& pat, const TypeDesc* actual, bool allowPromote) {
    if (!actual || actual->kind == TypeKind::Any || actual->kind == TypeKind::Void) return false;
    if (pat.isPlain()) {
        const TypeParam* tp = w.def->typeParam(pat.name);
        if (!tp) return false;
        if (!tp->allowed.empty() && std::find(tp->allowed.begin(), tp->allowed.end(), actual->name) == tp->allowed.end()) return false;
        auto it = w.args.find(pat.name);
        if (it == w.args.end()) {
            w.args[pat.name] = actual->name;
            w.inferred.insert(pat.name);
            return true;
        }
        if (allowPromote && w.inferred.count(pat.name) && it->second == "int" && actual->kind == TypeKind::Float &&
            (tp->allowed.empty() || std::find(tp->allowed.begin(), tp->allowed.end(), "float") != tp->allowed.end())) {
            it->second = "float";
            return true;
        }
        return false;
    }
    bool changed = false;
    if (pat.isArray() && actual->kind == TypeKind::Array) changed |= unify(w, pat.args[0], actual->elem, false);
    else if (pat.isMap() && actual->kind == TypeKind::Map) {
        changed |= unify(w, pat.args[0], actual->keyType, false);
        changed |= unify(w, pat.args[1], actual->elem, false);
    }
    return changed;
}

bool mentionsParams(const Work& w, const TypeExpr& e) {
    for (auto& tp : w.def->typeParams)
        if (e.mentions(tp.name)) return true;
    return false;
}

struct GraphCompiler {
    const Script& script;
    const Registry& reg;
    Program& prog;

    void diag(Severity s, const std::string& graph, NodeId node, const std::string& msg) {
        Diagnostic d;
        d.severity = s;
        d.graph = graph;
        d.node = node;
        d.message = msg;
        if (s == Severity::Error) prog.hasErrors = true;
        prog.diagnostics.push_back(std::move(d));
    }

    const TypeDesc* typeFor(const std::string& expr, const std::string& graph, NodeId node, const std::string& what) {
        const TypeDesc* t = reg.findType(expr);
        if (!t) {
            diag(Severity::Error, graph, node, "Unknown type '" + expr + "' for " + what);
            return reg.tAny();
        }
        return t;
    }

    Value convertDefault(const Value& def, const TypeDesc* t) {
        try {
            return def.isNull() ? reg.defaultValue(t) : reg.convert(def, t);
        } catch (const ScriptError&) {
            return reg.defaultValue(t);
        }
    }

    // ------------------------------------------------------------------ pass A: structure
    void declare() {
        prog.registry = &reg;
        prog.name = script.name;
        std::set<std::string> seen;
        for (auto& v : script.variables) {
            CompiledVariable cv;
            cv.decl = v;
            cv.type = typeFor(v.type, "", 0, "variable '" + v.name + "'");
            cv.def = convertDefault(v.def, cv.type);
            if (!seen.insert(v.name).second) diag(Severity::Error, "", 0, "Duplicate variable '" + v.name + "'");
            prog.variables.push_back(std::move(cv));
        }
        std::set<std::string> graphNames;
        for (auto& g : script.graphs) {
            if (!graphNames.insert(g.name).second) diag(Severity::Error, g.name, 0, "Duplicate graph name '" + g.name + "'");
            CompiledGraph cg;
            cg.name = g.name;
            cg.kind = g.kind;
            for (auto& p : g.inputs) {
                cg.inputs.push_back({p, typeFor(p.type, g.name, 0, "parameter '" + p.name + "'")});
                if (p.byRef) cg.hasRefParams = true;
            }
            for (auto& p : g.outputs) cg.outputs.push_back({p, typeFor(p.type, g.name, 0, "output '" + p.name + "'")});
            for (auto& v : g.locals) {
                CompiledVariable cv;
                cv.decl = v;
                cv.type = typeFor(v.type, g.name, 0, "local variable '" + v.name + "'");
                cv.def = convertDefault(v.def, cv.type);
                cg.locals.push_back(std::move(cv));
            }
            if (g.kind == GraphKind::Function) prog.functions[g.name] = static_cast<int>(prog.graphs.size());
            prog.graphs.push_back(std::move(cg));
        }
    }

    // ------------------------------------------------------------------ pass B: one graph
    void compileGraph(size_t gi) {
        const Graph& g = script.graphs[gi];
        CompiledGraph& cg = prog.graphs[gi];
        NodeBuildCtx bctx{reg, &script, &g};

        std::vector<Work> work(g.nodes.size());
        for (size_t i = 0; i < g.nodes.size(); ++i) {
            const Node& n = g.nodes[i];
            Work& w = work[i];
            w.node = &n;
            w.def = reg.findNode(n.def);
            w.args = n.typeArgs;
            if (!w.def) {
                if (n.def.rfind("event:", 0) == 0) diag(Severity::Error, g.name, n.id, "This application does not provide the event '" + n.def.substr(6) + "'");
                else diag(Severity::Error, g.name, n.id, "Unknown node type '" + n.def + "' (missing plugin?)");
            } else {
                w.raw = collectPinDefs(n, *w.def, reg, &script, &g);
                if (w.def->eventGraphOnly && g.kind != GraphKind::Event)
                    diag(Severity::Error, g.name, n.id, "'" + w.def->title + "' can only be used in an event graph");
                if (w.def->functionGraphOnly && g.kind != GraphKind::Function)
                    diag(Severity::Error, g.name, n.id, "'" + w.def->title + "' can only be used in a function graph");
            }
            resolvePins(reg, w);
            cg.nodeIndex[n.id] = static_cast<int>(i);
        }
        (void)bctx;

        // ---- locate pins of every connection (skipping broken ones)
        struct Link { int from, fpin, to, tpin; };
        std::vector<Link> links;
        for (auto& c : g.connections) {
            auto fi = cg.nodeIndex.find(c.fromNode);
            auto ti = cg.nodeIndex.find(c.toNode);
            if (fi == cg.nodeIndex.end() || ti == cg.nodeIndex.end()) {
                diag(Severity::Error, g.name, fi == cg.nodeIndex.end() ? c.toNode : c.fromNode, "Connection refers to a node that does not exist");
                continue;
            }
            Work& fw = work[static_cast<size_t>(fi->second)];
            Work& tw = work[static_cast<size_t>(ti->second)];
            if (!fw.def || !tw.def) continue;
            int fp = pinIndex(fw.pins, c.fromPin, PinDir::Out);
            int tp = pinIndex(tw.pins, c.toPin, PinDir::In);
            if (fp < 0) { diag(Severity::Error, g.name, c.fromNode, "Missing output pin '" + c.fromPin + "'"); continue; }
            if (tp < 0) { diag(Severity::Error, g.name, c.toNode, "Missing input pin '" + c.toPin + "'"); continue; }
            if (fw.pins[static_cast<size_t>(fp)].kind != tw.pins[static_cast<size_t>(tp)].kind) {
                diag(Severity::Error, g.name, c.toNode, "Cannot connect an execution pin to a data pin ('" + c.fromPin + "' -> '" + c.toPin + "')");
                continue;
            }
            links.push_back({fi->second, fp, ti->second, tp});
        }

        // ---- infer type parameters from connections
        for (int pass = 0; pass < 16; ++pass) {
            std::set<int> dirty;
            for (auto& l : links) {
                Work& fw = work[static_cast<size_t>(l.from)];
                Work& tw = work[static_cast<size_t>(l.to)];
                const CompiledPin& fp = fw.pins[static_cast<size_t>(l.fpin)];
                const CompiledPin& tp = tw.pins[static_cast<size_t>(l.tpin)];
                if (fp.kind != PinKind::Data) continue;
                const TypeExpr& fe = fw.exprs[static_cast<size_t>(l.fpin)];
                const TypeExpr& te = tw.exprs[static_cast<size_t>(l.tpin)];
                if (!fp.wildcard && tw.def && mentionsParams(tw, te) && unify(tw, te, fp.type, true)) dirty.insert(l.to);
                if (!tp.wildcard && fw.def && mentionsParams(fw, fe) && unify(fw, fe, tp.type, false)) dirty.insert(l.from);
            }
            if (dirty.empty()) break;
            for (int i : dirty) resolvePins(reg, work[static_cast<size_t>(i)]);
        }

        // ---- build compiled nodes
        int slot = 0;
        cg.nodes.resize(g.nodes.size());
        for (size_t i = 0; i < g.nodes.size(); ++i) {
            const Node& n = g.nodes[i];
            Work& w = work[i];
            CompiledNode& cn = cg.nodes[i];
            cn.id = n.id;
            cn.index = static_cast<int>(i);
            cn.defId = n.def;
            cn.def = w.def;
            cn.x = n.x;
            cn.y = n.y;
            cn.comment = n.comment;
            cn.props = n.props;
            cn.typeArgs = w.args;
            cn.title = nodeTitle(n, w.def);
            if (w.def && w.def->op == NodeOp::FunctionEntry) cn.title = g.name;       // the entry node is named after its function
            if (w.def) {
                cn.op = w.def->op;
                cn.fast = cn.op == NodeOp::GetVariable ? Intrinsic::GetVar : w.def->intrinsic;
                cn.pure = w.def->pure;
                for (auto& tp : w.def->typeParams) {
                    auto it = w.args.find(tp.name);
                    if (it == w.args.end() || tp.allowed.empty()) continue;
                    if (std::find(tp.allowed.begin(), tp.allowed.end(), it->second) == tp.allowed.end())
                        diag(Severity::Error, g.name, n.id, "'" + w.def->title + "' does not support the type '" + it->second + "'");
                }
            }
            cn.pins = std::move(w.pins);
            for (size_t p = 0; p < cn.pins.size(); ++p) {
                const CompiledPin& cp = cn.pins[p];
                auto& list = cp.kind == PinKind::Exec ? (cp.dir == PinDir::In ? cn.execIn : cn.execOut)
                                                       : (cp.dir == PinDir::In ? cn.dataIn : cn.dataOut);
                list.push_back(static_cast<int>(p));
            }
            cn.inputs.assign(cn.dataIn.size(), DataSource{});
            cn.execTarget.assign(cn.execOut.size(), -1);
            cn.execTargetPin.assign(cn.execOut.size(), -1);
            cn.outBase = slot;
            for (int p : cn.dataOut) {
                cg.slotTypes.push_back(cn.pins[static_cast<size_t>(p)].type);
                ++slot;
            }
        }
        cg.slotCount = slot;
        for (const TypeDesc* t : cg.slotTypes) {
            cg.slotInit.push_back(t ? reg.defaultValue(t) : Value());
        }

        // ---- wire
        for (auto& l : links) {
            CompiledNode& from = cg.nodes[static_cast<size_t>(l.from)];
            CompiledNode& to = cg.nodes[static_cast<size_t>(l.to)];
            CompiledPin& fp = from.pins[static_cast<size_t>(l.fpin)];
            CompiledPin& tp = to.pins[static_cast<size_t>(l.tpin)];
            if (fp.kind == PinKind::Exec) {
                int& target = from.execTarget[static_cast<size_t>(fp.ordinal)];
                if (target >= 0) diag(Severity::Warning, g.name, from.id, "Execution output '" + fp.name + "' is connected more than once; only the last link is used");
                target = l.to;
                from.execTargetPin[static_cast<size_t>(fp.ordinal)] = l.tpin;
                fp.connected = tp.connected = true;
                continue;
            }
            if (!reg.canConnect(fp.type, tp.type)) {
                diag(Severity::Error, g.name, to.id, "Cannot connect '" + fp.type->name + "' to '" + tp.type->name + "' (" + from.title + "." + fp.name + " -> " + to.title + "." + tp.name + ")");
                continue;
            }
            DataSource& ds = to.inputs[static_cast<size_t>(tp.ordinal)];
            if (tp.connected) diag(Severity::Warning, g.name, to.id, "Input '" + tp.name + "' is connected more than once; only the last link is used");
            ds.node = l.from;
            ds.pin = l.fpin;
            ds.pure = from.pure;
            if (from.op == NodeOp::FunctionEntry) {
                ds.kind = DataSource::Kind::Param;
                ds.slot = fp.ordinal;
            } else {
                ds.kind = DataSource::Kind::Slot;
                ds.slot = from.outBase + fp.ordinal;
            }
            ds.from = fp.type;
            ds.to = tp.type;
            ds.convert = fp.type != tp.type && tp.type->kind != TypeKind::Any;
            fp.connected = tp.connected = true;
        }

        resolveReferences(gi);

        // ---- how the interpreter reads every input
        for (auto& cn : cg.nodes) {
            for (size_t i = 0; i < cn.inputs.size(); ++i) {
                DataSource& ds = cn.inputs[i];
                if (ds.kind == DataSource::Kind::Default) {
                    const Value& def = cn.pins[static_cast<size_t>(cn.dataIn[i])].def;
                    if (!def.isContainer()) {
                        ds.mode = DataSource::Mode::Const;
                        ds.constant = def;
                    }
                } else if (!ds.convert) {
                    ds.mode = ds.kind == DataSource::Kind::Param ? DataSource::Mode::Param
                              : ds.pure                          ? DataSource::Mode::PureSlot
                                                                 : DataSource::Mode::Slot;
                    if (ds.kind == DataSource::Kind::Slot) {
                        const CompiledNode& src = cg.nodes[static_cast<size_t>(ds.node)];
                        if (src.op == NodeOp::GetVariable && src.def && src.resolved >= 0) {
                            ds.varIndex = src.resolved;
                            ds.mode = src.scope == VarScope::Global  ? DataSource::Mode::VarGlobal
                                      : src.scope == VarScope::Local ? DataSource::Mode::VarLocal
                                                                     : DataSource::Mode::VarParam;
                        }
                    }
                }
            }
        }

        validate(gi);
    }

    // ------------------------------------------------------------------ references
    void resolveReferences(size_t gi) {
        const Graph& g = script.graphs[gi];
        CompiledGraph& cg = prog.graphs[gi];
        bool haveEntry = false;
        for (auto& cn : cg.nodes) {
            if (!cn.def) continue;
            switch (cn.op) {
            case NodeOp::FunctionEntry:
                if (g.kind == GraphKind::Function) {
                    if (haveEntry) diag(Severity::Error, g.name, cn.id, "A function graph can only have one entry node");
                    haveEntry = true;
                    cg.entry = cn.index;
                }
                break;
            case NodeOp::Event: {
                // events are provided by the application (event:Name); a script only handles them
                std::string name = cn.defId.rfind("event:", 0) == 0 ? cn.defId.substr(6) : std::string();
                if (name.empty()) {
                    diag(Severity::Error, g.name, cn.id, "Event has no name");
                    break;
                }
                std::string why;
                if (const EventDesc* ev = reg.findEvent(name))
                    if (!hostEventSignatureMatches(*script.findGraph(g.name)->findNode(cn.id), *ev, reg, &why))
                        diag(Severity::Error, g.name, cn.id, "The event '" + name + "' changed: " + why);
                if (prog.events.count(name)) diag(Severity::Error, g.name, cn.id, "Event '" + name + "' is already handled elsewhere");
                else prog.events[name] = {static_cast<int>(gi), cn.index};
                break;
            }
            case NodeOp::GetVariable:
            case NodeOp::SetVariable: {
                std::string name = cn.prop("name");
                std::string scope = cn.prop("scope");
                int idx = -1;
                auto tryLocal = [&] {
                    for (size_t i = 0; i < g.locals.size(); ++i)
                        if (g.locals[i].name == name) { idx = static_cast<int>(i); cn.scope = VarScope::Local; return true; }
                    return false;
                };
                auto tryParam = [&] {
                    for (size_t i = 0; i < g.inputs.size(); ++i)
                        if (g.inputs[i].name == name) { idx = static_cast<int>(i); cn.scope = VarScope::Param; return true; }
                    return false;
                };
                auto tryGlobal = [&] {
                    int i = script.variableIndex(name);
                    if (i >= 0) { idx = i; cn.scope = VarScope::Global; return true; }
                    return false;
                };
                if (scope == "local") tryLocal();
                else if (scope == "param") tryParam();
                else if (scope == "global") tryGlobal();
                else if (!tryLocal() && !tryParam()) tryGlobal();
                if (idx < 0) diag(Severity::Error, g.name, cn.id, "Unknown variable '" + name + "'");
                cn.resolved = idx;
                if (cn.op == NodeOp::SetVariable && cn.scope == VarScope::Param && idx >= 0 && g.inputs[static_cast<size_t>(idx)].isConst)
                    diag(Severity::Error, g.name, cn.id, "Cannot modify the const parameter '" + name + "'");
                break;
            }
            case NodeOp::CallFunction: {
                int fi = prog.graphIndex(cn.prop("function"));
                if (fi < 0 || prog.graphs[static_cast<size_t>(fi)].kind != GraphKind::Function)
                    diag(Severity::Error, g.name, cn.id, "Unknown function '" + cn.prop("function") + "'");
                cn.resolved = fi;
                break;
            }
            default: break;
            }
        }
        if (g.kind == GraphKind::Function && !haveEntry) diag(Severity::Error, g.name, 0, "Function graph has no entry node");
    }

    // The node a data input is ultimately fed by, looking through reroutes and aliasing nodes.
    const CompiledNode* originOf(const CompiledGraph& cg, const CompiledNode& n, size_t ordinal) {
        const CompiledNode* cur = &n;
        size_t ord = ordinal;
        for (int guard = 0; guard < 64; ++guard) {
            if (ord >= cur->inputs.size()) return nullptr;
            const DataSource& ds = cur->inputs[ord];
            if (ds.kind == DataSource::Kind::Default) return nullptr;
            const CompiledNode& src = cg.nodes[static_cast<size_t>(ds.node)];
            if (src.op == NodeOp::Reroute || (src.def && src.def->aliasesFirstInput)) {
                cur = &src;
                ord = 0;
                continue;
            }
            return &src;
        }
        return nullptr;
    }

    void validate(size_t gi) {
        const Graph& g = script.graphs[gi];
        CompiledGraph& cg = prog.graphs[gi];
        auto isConstParam = [&](const CompiledNode& v) {
            return v.op == NodeOp::GetVariable && v.scope == VarScope::Param && v.resolved >= 0 && g.inputs[static_cast<size_t>(v.resolved)].isConst;
        };
        for (auto& cn : cg.nodes) {
            if (!cn.def) continue;
            // by-reference arguments must be variables
            if (cn.op == NodeOp::CallFunction && cn.resolved >= 0) {
                for (size_t i = 0; i < cn.dataIn.size(); ++i) {
                    const CompiledPin& p = cn.pins[static_cast<size_t>(cn.dataIn[i])];
                    if (!p.byRef) continue;
                    const CompiledNode* src = originOf(cg, cn, i);
                    bool isVar = src && src->op == NodeOp::GetVariable;
                    if (!isVar && !p.isConst) {
                        diag(Severity::Error, g.name, cn.id, "Argument for the by-reference parameter '" + p.name + "' must be a variable");
                    } else if (isVar && !p.isConst && isConstParam(*src)) {
                        diag(Severity::Error, g.name, cn.id, "Cannot pass the const parameter '" + src->prop("name") + "' to the non-const reference '" + p.name + "'");
                    }
                }
            }
            // nodes that modify their first input may not be applied to const parameters
            if (cn.def->mutatesFirstInput && !cn.inputs.empty()) {
                const CompiledNode* src = originOf(cg, cn, 0);
                if (src && isConstParam(*src))
                    diag(Severity::Error, g.name, cn.id, "'" + cn.title + "' modifies the const parameter '" + src->prop("name") + "'");
            }
        }
        // cycles through pure nodes can never be evaluated
        enum { White, Gray, Black };
        std::vector<int> color(cg.nodes.size(), White);
        std::function<bool(int)> visit = [&](int ni) {
            color[static_cast<size_t>(ni)] = Gray;
            for (auto& ds : cg.nodes[static_cast<size_t>(ni)].inputs) {
                if (ds.kind == DataSource::Kind::Default || !ds.pure) continue;
                int c = color[static_cast<size_t>(ds.node)];
                if (c == Gray) return true;
                if (c == White && visit(ds.node)) return true;
            }
            color[static_cast<size_t>(ni)] = Black;
            return false;
        };
        for (auto& cn : cg.nodes) {
            if (color[static_cast<size_t>(cn.index)] != White) continue;
            if (visit(cn.index)) {
                diag(Severity::Error, g.name, cn.id, "Data connections form a cycle");
                break;
            }
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------- public API
std::string nodeTitle(const Node& n, const NodeDef* def) {
    if (def) {
        if (def->op == NodeOp::CallFunction) return n.prop("function").empty() ? def->title : n.prop("function");
        if (def->op == NodeOp::GetVariable) return "Get " + n.prop("name");
        if (def->op == NodeOp::SetVariable) return "Set " + n.prop("name");
        return def->title;
    }
    return n.def;
}

std::vector<PinDef> collectPinDefs(const Node& node, const NodeDef& def, const Registry& reg, const Script* script, const Graph* graph) {
    std::vector<PinDef> pins = def.pins;
    if (def.dynamicPins) {
        NodeBuildCtx ctx{reg, script, graph};
        def.dynamicPins(node, ctx, pins);
    }
    return pins;
}

std::vector<CompiledPin> buildNodePins(const Node& node, const Registry& reg, const Script* script, const Graph* graph) {
    Work w;
    w.node = &node;
    w.def = reg.findNode(node.def);
    if (!w.def) return {};
    w.args = node.typeArgs;
    w.raw = collectPinDefs(node, *w.def, reg, script, graph);
    resolvePins(reg, w);
    return std::move(w.pins);
}

std::shared_ptr<Program> compile(const Script& script, const Registry& reg) {
    auto prog = std::make_shared<Program>();
    GraphCompiler gc{script, reg, *prog};
    gc.declare();
    for (size_t i = 0; i < script.graphs.size(); ++i) gc.compileGraph(i);
    return prog;
}

} // namespace fg
