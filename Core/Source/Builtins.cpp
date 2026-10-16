#include <FlowGraph/Builtins.hpp>
#include <FlowGraph/Script.hpp>
#include <FlowGraph/Vm.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace fg {

namespace {

// ------------------------------------------------------------------ helpers
NodeDef flowNode(std::string id, std::string title, std::string category) {
    NodeDef d;
    d.id = std::move(id);
    d.title = std::move(title);
    d.category = std::move(category);
    d.style = NodeStyle::Flow;
    return d;
}

NodeDef pureNode(std::string id, std::string title, std::string category) {
    NodeDef d;
    d.id = std::move(id);
    d.title = std::move(title);
    d.category = std::move(category);
    d.pure = true;
    d.style = NodeStyle::Pure;
    return d;
}

NodeDef execNode(std::string id, std::string title, std::string category) {
    NodeDef d;
    d.id = std::move(id);
    d.title = std::move(title);
    d.category = std::move(category);
    d.pins.push_back(execIn());
    d.pins.push_back(execOut());
    return d;
}

ArrayData& arrayArg(NodeCtx& c, size_t i) {
    auto* a = c.in(i).arrayPtr();
    if (!a) c.error("array is null");
    return *a;
}
MapData& mapArg(NodeCtx& c, size_t i) {
    auto* m = c.in(i).mapPtr();
    if (!m) c.error("map is null");
    return *m;
}
int64_t checkIndex(NodeCtx& c, int64_t idx, size_t size, bool allowEnd = false) {
    if (idx < 0 || static_cast<size_t>(idx) > size || (!allowEnd && static_cast<size_t>(idx) == size))
        c.error("index " + std::to_string(idx) + " is out of range (size " + std::to_string(size) + ")");
    return idx;
}

std::mt19937_64& rng() {
    static thread_local std::mt19937_64 g{std::random_device{}()};
    return g;
}

// type of the variable a variable node refers to
std::string varType(const Node& n, const NodeBuildCtx& c) {
    const std::string name = n.prop("name"), scope = n.prop("scope");
    if (c.graph) {
        if (scope.empty() || scope == "local")
            if (auto* l = c.graph->findLocal(name)) return l->type;
        if (scope.empty() || scope == "param")
            for (auto& p : c.graph->inputs)
                if (p.name == name) return p.type;
    }
    if (c.script && (scope.empty() || scope == "global"))
        if (auto* v = c.script->findVariable(name)) return v->type;
    return "any";
}

void signaturePins(const Graph& g, std::vector<PinDef>& pins) {
    for (auto& p : g.inputs) {
        PinDef d = dataIn(p.name, p.type, p.def);
        d.byRef = p.byRef;
        d.isConst = p.isConst;
        pins.push_back(std::move(d));
    }
    for (auto& p : g.outputs) pins.push_back(dataOut(p.name, p.type));
}

// ------------------------------------------------------------------ script level nodes
void registerScriptNodes(Registry& r) {
    {
        NodeDef d;
        d.id = "func.entry";
        d.title = "Function Entry";
        d.category = "Functions";
        d.style = NodeStyle::Event;
        d.op = NodeOp::FunctionEntry;
        d.functionGraphOnly = true;
        d.userCreatable = false;
        d.pins.push_back(execOut());
        d.dynamicPins = [](const Node&, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            if (!c.graph) return;
            for (auto& p : c.graph->inputs) {
                PinDef pd = dataOut(p.name, p.type);
                pd.byRef = p.byRef;
                pd.isConst = p.isConst;
                pins.push_back(std::move(pd));
            }
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d;
        d.id = "func.return";
        d.title = "Return";
        d.category = "Functions";
        d.style = NodeStyle::Flow;
        d.op = NodeOp::Return;
        d.functionGraphOnly = true;
        d.pins.push_back(execIn());
        d.dynamicPins = [](const Node&, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            if (!c.graph) return;
            for (auto& p : c.graph->outputs) pins.push_back(dataIn(p.name, p.type, p.def));
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d;
        d.id = "func.call";
        d.title = "Call Function";
        d.category = "Functions";
        d.op = NodeOp::CallFunction;
        d.pins.push_back(execIn());
        d.pins.push_back(execOut());
        d.dynamicPins = [](const Node& n, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            if (!c.script) return;
            if (auto* g = c.script->findGraph(n.prop("function")))
                if (g->kind == GraphKind::Function) signaturePins(*g, pins);
        };
        d.expand = [](const NodeBuildCtx& c, std::vector<NodeTemplate>& out) {
            if (!c.script) return;
            for (auto& g : c.script->graphs) {
                if (g.kind != GraphKind::Function) continue;
                NodeTemplate t;
                t.defId = "func.call";
                t.props["function"] = g.name;
                t.title = g.name;
                t.category = "Functions";
                t.doc = g.doc;
                t.pins.push_back(execIn());
                t.pins.push_back(execOut());
                signaturePins(g, t.pins);
                out.push_back(std::move(t));
            }
        };
        r.addNode(std::move(d));
    }
    for (bool isSet : {false, true}) {
        NodeDef d;
        d.id = isSet ? "var.set" : "var.get";
        d.title = isSet ? "Set Variable" : "Get Variable";
        d.category = "Variables";
        d.style = NodeStyle::Variable;
        d.op = isSet ? NodeOp::SetVariable : NodeOp::GetVariable;
        d.pure = !isSet;
        if (isSet) {
            d.pins.push_back(execIn());
            d.pins.push_back(execOut());
        }
        d.dynamicPins = [isSet](const Node& n, const NodeBuildCtx& c, std::vector<PinDef>& pins) {
            std::string t = varType(n, c);
            bool constParam = false;       // reading a const parameter: mark the pin so editors can show it
            if (c.graph && (n.prop("scope").empty() || n.prop("scope") == "param") && !c.graph->findLocal(n.prop("name")))
                for (auto& p : c.graph->inputs)
                    if (p.name == n.prop("name") && p.isConst) constParam = true;
            PinDef d = isSet ? dataIn("Value", t) : aliases(dataOut("Value", t));      // a container variable is read without a copy
            d.isConst = constParam;
            pins.push_back(std::move(d));
        };
        d.expand = [isSet](const NodeBuildCtx& c, std::vector<NodeTemplate>& out) {
            auto add = [&](const std::string& name, const std::string& type, const std::string& scope, const std::string& cat) {
                NodeTemplate t;
                t.defId = isSet ? "var.set" : "var.get";
                t.props["name"] = name;
                t.props["scope"] = scope;
                t.title = (isSet ? "Set " : "Get ") + name;
                t.category = cat;
                t.keywords = "variable " + name;
                t.style = NodeStyle::Variable;
                if (isSet) { t.pins.push_back(execIn()); t.pins.push_back(execOut()); t.pins.push_back(dataIn("Value", type)); }
                else t.pins.push_back(dataOut("Value", type));
                out.push_back(std::move(t));
            };
            if (!c.script) return;
            for (auto& v : c.script->variables) add(v.name, v.type, "global", "Variables");
            if (c.graph) {
                for (auto& v : c.graph->locals) add(v.name, v.type, "local", "Local Variables");
                for (auto& p : c.graph->inputs)
                    if (!(isSet && p.isConst)) add(p.name, p.type, "param", "Parameters");
            }
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = pureNode("reroute", "Reroute", "Utilities");
        d.style = NodeStyle::Reroute;
        d.op = NodeOp::Reroute;
        d.userCreatable = false;
        d.aliasesFirstInput = true;
        d.typeParams.push_back({"T", {}, "any"});
        d.pins.push_back(reads(dataIn("In", "T")));
        d.pins.push_back(aliases(dataOut("Out", "T")));
        r.addNode(std::move(d));
    }
}

// ------------------------------------------------------------------ flow control
void registerFlow(Registry& r) {
    {
        NodeDef d = flowNode("flow.branch", "Branch", "Flow");
        d.intrinsic = Intrinsic::Branch;
        d.keywords = "if else condition";
        d.pins = {execIn(), dataIn("Condition", "bool", Value(false)), execOut("True"), execOut("False")};
        d.run = [](NodeCtx& c) { c.next(c.inBool(0) ? 0 : 1); };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = flowNode("flow.sequence", "Sequence", "Flow");
        d.keywords = "then order";
        d.pins = {execIn()};
        d.dynamicPins = [](const Node& n, const NodeBuildCtx&, std::vector<PinDef>& pins) {
            int count = 2;
            try { count = std::clamp(std::stoi(n.prop("count", "2")), 1, 32); } catch (...) {}
            for (int i = 0; i < count; ++i) pins.push_back(execOut("Then " + std::to_string(i)));
        };
        d.run = [](NodeCtx& c) {
            int64_t i = c.resumed() ? c.state() + 1 : 0;
            if (static_cast<size_t>(i) < c.node().execOut.size()) {
                c.state() = i;
                c.call(static_cast<size_t>(i));
            } else c.end();
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = flowNode("flow.for", "For Loop", "Flow");
        d.intrinsic = Intrinsic::For;
        d.keywords = "loop repeat for";
        d.breakPin = 0;                                    // Break continues with "Completed"
        d.doc = "Runs the loop body for First <= Index < Last (Last itself is not visited).";
        d.pins = {execIn(), dataIn("First", "int", Value(0)), dataIn("Last", "int", Value(10)), execOut("Completed"), execOut("Loop Body"),
                  dataOut("Index", "int")};
        d.pins[1].doc = "First index (included)";
        d.pins[2].doc = "Last index (excluded): the loop runs while Index < Last";
        d.run = [](NodeCtx& c) {
            int64_t i = c.resumed() ? c.state() + 1 : c.inInt(0);
            if (i < c.inInt(1)) {
                c.state() = i;
                c.out(0, Value(i));
                c.call(1);
            } else c.next(0);
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = flowNode("flow.foreach", "For Each", "Flow");
        d.intrinsic = Intrinsic::ForEach;
        d.keywords = "loop array iterate";
        d.breakPin = 0;
        d.typeParams.push_back({"T", {}, "int"});
        d.pins = {execIn(), reads(dataIn("Array", "array<T>")), execOut("Completed"), execOut("Loop Body"), aliases(dataOut("Element", "T")), dataOut("Index", "int")};
        d.run = [](NodeCtx& c) {
            int64_t i = c.resumed() ? c.state() + 1 : 0;
            auto* a = c.in(0).arrayPtr();
            if (a && static_cast<size_t>(i) < a->items.size()) {
                c.state() = i;
                c.out(0, a->items[static_cast<size_t>(i)]);
                c.out(1, Value(i));
                c.call(1);
            } else c.next(0);
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = flowNode("flow.while", "While Loop", "Flow");
        d.intrinsic = Intrinsic::While;
        d.keywords = "loop while";
        d.breakPin = 0;
        d.pins = {execIn(), dataIn("Condition", "bool", Value(false)), execOut("Completed"), execOut("Loop Body")};
        d.run = [](NodeCtx& c) {
            if (c.inBool(0)) c.call(1);
            else c.next(0);
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = flowNode("flow.break", "Break", "Flow");
        d.keywords = "stop exit loop";
        d.pins = {execIn()};
        d.run = [](NodeCtx& c) { c.breakLoop(); };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = execNode("flow.delay", "Delay", "Flow");
        d.style = NodeStyle::Flow;
        d.keywords = "wait sleep latent";
        d.pins.push_back(dataIn("Duration", "float", Value(1.0)));
        d.run = [](NodeCtx& c) { c.wait(c.inFloat(0)); };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = pureNode("flow.select", "Select", "Flow");
        d.keywords = "ternary choose if";
        d.typeParams.push_back({"T", {}, "int"});
        d.pins = {dataIn("Condition", "bool", Value(false)), reads(dataIn("A", "T")), reads(dataIn("B", "T")), aliases(dataOut("Result", "T"))};
        d.run = [](NodeCtx& c) { c.out(0, c.inBool(0) ? c.in(1) : c.in(2)); };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = execNode("util.print", "Print String", "Utilities");
        d.keywords = "log debug output";
        d.pins.push_back(dataIn("String", "string", Value("Hello")));
        d.run = [](NodeCtx& c) { c.log(LogLevel::Info, c.inString(0)); };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = execNode("util.warn", "Print Warning", "Utilities");
        d.pins.push_back(dataIn("String", "string", Value("Warning")));
        d.run = [](NodeCtx& c) { c.log(LogLevel::Warning, c.inString(0)); };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = pureNode("util.isvalid", "Is Valid", "Utilities");
        d.pins = {dataIn("Object", "any"), dataOut("Valid", "bool")};
        d.run = [](NodeCtx& c) { c.out(0, Value(c.in(0).objectPtr() != nullptr)); };
        r.addNode(std::move(d));
    }
}

// ------------------------------------------------------------------ math / logic
void registerMath(Registry& r) {
    auto numeric = [](NodeDef& d) { d.typeParams.push_back({"T", {"int", "float"}, "int"}); };
    auto binary = [&](const char* id, Intrinsic fast, const char* title, const char* kw, auto opInt, auto opFloat) {
        NodeDef d = pureNode(id, title, "Math");
        d.intrinsic = fast;
        d.keywords = kw;
        numeric(d);
        d.pins = {dataIn("A", "T", Value(0)), dataIn("B", "T", Value(0)), dataOut("Result", "T")};
        d.run = [opInt, opFloat](NodeCtx& c) {
            const Value &a = c.in(0), &b = c.in(1);
            if (a.isInt() && b.isInt()) c.out(0, Value(opInt(c, a.toInt(), b.toInt())));
            else c.out(0, Value(opFloat(a.toFloat(), b.toFloat())));
        };
        r.addNode(std::move(d));
    };
    binary("math.add", Intrinsic::Add, "Add", "+ plus sum", [](NodeCtx&, int64_t a, int64_t b) { return a + b; }, [](double a, double b) { return a + b; });
    binary("math.sub", Intrinsic::Sub, "Subtract", "- minus", [](NodeCtx&, int64_t a, int64_t b) { return a - b; }, [](double a, double b) { return a - b; });
    binary("math.mul", Intrinsic::Mul, "Multiply", "* times", [](NodeCtx&, int64_t a, int64_t b) { return a * b; }, [](double a, double b) { return a * b; });
    binary("math.div", Intrinsic::Div, "Divide", "/ over",
           [](NodeCtx& c, int64_t a, int64_t b) -> int64_t { if (b == 0) c.error("division by zero"); return divideInt(a, b); },
           [](double a, double b) { return a / b; });
    binary("math.mod", Intrinsic::Mod, "Modulo", "% remainder",
           [](NodeCtx& c, int64_t a, int64_t b) -> int64_t { if (b == 0) c.error("division by zero"); return moduloInt(a, b); },
           [](double a, double b) { return std::fmod(a, b); });
    binary("math.min", Intrinsic::Min, "Min", "smaller", [](NodeCtx&, int64_t a, int64_t b) { return std::min(a, b); }, [](double a, double b) { return std::min(a, b); });
    binary("math.max", Intrinsic::Max, "Max", "larger", [](NodeCtx&, int64_t a, int64_t b) { return std::max(a, b); }, [](double a, double b) { return std::max(a, b); });
    {
        NodeDef d = pureNode("math.abs", "Abs", "Math");
        numeric(d);
        d.pins = {dataIn("Value", "T", Value(0)), dataOut("Result", "T")};
        d.run = [](NodeCtx& c) {
            const Value& a = c.in(0);
            c.out(0, a.isInt() ? Value(a.toInt() < 0 ? -a.toInt() : a.toInt()) : Value(std::fabs(a.toFloat())));
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = pureNode("math.neg", "Negate", "Math");
        numeric(d);
        d.pins = {dataIn("Value", "T", Value(0)), dataOut("Result", "T")};
        d.run = [](NodeCtx& c) {
            const Value& a = c.in(0);
            c.out(0, a.isInt() ? Value(-a.toInt()) : Value(-a.toFloat()));
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = pureNode("math.clamp", "Clamp", "Math");
        numeric(d);
        d.pins = {dataIn("Value", "T", Value(0)), dataIn("Min", "T", Value(0)), dataIn("Max", "T", Value(1)), dataOut("Result", "T")};
        d.run = [](NodeCtx& c) {
            const Value &v = c.in(0), &lo = c.in(1), &hi = c.in(2);
            if (v.isInt() && lo.isInt() && hi.isInt()) c.out(0, Value(std::clamp(v.toInt(), lo.toInt(), hi.toInt())));
            else c.out(0, Value(std::clamp(v.toFloat(), lo.toFloat(), hi.toFloat())));
        };
        r.addNode(std::move(d));
    }

    // comparisons
    auto compare = [&](const char* id, Intrinsic fast, const char* title, const char* kw, bool ordered, auto fn) {
        NodeDef d = pureNode(id, title, "Math|Comparison");
        d.intrinsic = fast;
        d.keywords = kw;
        if (ordered) d.typeParams.push_back({"T", {"int", "float", "string"}, "int"});
        else d.typeParams.push_back({"T", {}, "int"});
        d.pins = {reads(dataIn("A", "T")), reads(dataIn("B", "T")), dataOut("Result", "bool")};
        d.run = [fn](NodeCtx& c) { c.out(0, Value(fn(c.in(0), c.in(1)))); };
        r.addNode(std::move(d));
    };
    ValueLess less;
    compare("cmp.eq", Intrinsic::Eq, "Equal", "==", false, [](const Value& a, const Value& b) { return a == b; });
    compare("cmp.ne", Intrinsic::Ne, "Not Equal", "!=", false, [](const Value& a, const Value& b) { return a != b; });
    compare("cmp.lt", Intrinsic::Lt, "Less", "<", true, [less](const Value& a, const Value& b) { return less(a, b); });
    compare("cmp.le", Intrinsic::Le, "Less Or Equal", "<=", true, [less](const Value& a, const Value& b) { return !less(b, a); });
    compare("cmp.gt", Intrinsic::Gt, "Greater", ">", true, [less](const Value& a, const Value& b) { return less(b, a); });
    compare("cmp.ge", Intrinsic::Ge, "Greater Or Equal", ">=", true, [less](const Value& a, const Value& b) { return !less(a, b); });

    // logic
    auto logic = [&](const char* id, Intrinsic fast, const char* title, const char* kw, auto fn) {
        NodeDef d = pureNode(id, title, "Math|Logic");
        d.intrinsic = fast;
        d.keywords = kw;
        d.pins = {dataIn("A", "bool", Value(false)), dataIn("B", "bool", Value(false)), dataOut("Result", "bool")};
        d.run = [fn](NodeCtx& c) { c.out(0, Value(fn(c.inBool(0), c.inBool(1)))); };
        r.addNode(std::move(d));
    };
    logic("logic.and", Intrinsic::And, "And", "&&", [](bool a, bool b) { return a && b; });
    logic("logic.or", Intrinsic::Or, "Or", "||", [](bool a, bool b) { return a || b; });
    logic("logic.xor", Intrinsic::Xor, "Xor", "^", [](bool a, bool b) { return a != b; });
    {
        NodeDef d = pureNode("logic.not", "Not", "Math|Logic");
        d.intrinsic = Intrinsic::Not;
        d.keywords = "!";
        d.pins = {dataIn("Value", "bool", Value(false)), dataOut("Result", "bool")};
        d.run = [](NodeCtx& c) { c.out(0, Value(!c.inBool(0))); };
        r.addNode(std::move(d));
    }

    // float functions via reflection
    r.addFunction("Math.Sin", [](double x) { return std::sin(x); }, {"X"}).makePure();
    r.addFunction("Math.Cos", [](double x) { return std::cos(x); }, {"X"}).makePure();
    r.addFunction("Math.Tan", [](double x) { return std::tan(x); }, {"X"}).makePure();
    r.addFunction("Math.Sqrt", [](double x) { return std::sqrt(x); }, {"X"}).makePure();
    r.addFunction("Math.Floor", [](double x) { return std::floor(x); }, {"X"}).makePure();
    r.addFunction("Math.Ceil", [](double x) { return std::ceil(x); }, {"X"}).makePure();
    r.addFunction("Math.Round", [](double x) { return std::round(x); }, {"X"}).makePure();
    r.addFunction("Math.Pow", [](double b, double e) { return std::pow(b, e); }, {"Base", "Exponent"}).makePure();
    r.addFunction("Math.Lerp", [](double a, double b, double t) { return a + (b - a) * t; }, {"A", "B", "Alpha"}).makePure().withDefault(1, Value(1.0));
    r.addFunction("Math.Random Float", []() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }).makePure().withKeywords("rand");
    r.addFunction("Math.Random Int", [](int64_t lo, int64_t hi) {
        if (hi < lo) std::swap(lo, hi);
        return std::uniform_int_distribution<int64_t>(lo, hi)(rng());
    }, {"Min", "Max"}).makePure().withDefault(1, Value(100)).withKeywords("rand");
    r.addFunction("Math.Float To Int", [](double x) { return static_cast<int64_t>(x); }, {"Value"}).makePure().withKeywords("truncate");
}

// ------------------------------------------------------------------ strings
void registerStrings(Registry& r) {
    r.addFunction("String.Concat", [](const std::string& a, const std::string& b) { return a + b; }, {"A", "B"}).makePure().withKeywords("append join +");
    r.addFunction("String.Length", [](const std::string& s) { return static_cast<int64_t>(s.size()); }, {"String"}).makePure();
    r.addFunction("String.To Upper", [](std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }, {"String"}).makePure();
    r.addFunction("String.To Lower", [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }, {"String"}).makePure();
    r.addFunction("String.Contains", [](const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }, {"String", "Substring"}).makePure();
    r.addFunction("String.Substring", [](const std::string& s, int64_t start, int64_t len) {
        if (start < 0) start = 0;
        if (static_cast<size_t>(start) >= s.size()) return std::string();
        return s.substr(static_cast<size_t>(start), len < 0 ? std::string::npos : static_cast<size_t>(len));
    }, {"String", "Start", "Length"}).makePure().withDefault(2, Value(-1));
    r.addFunction("String.Replace", [](std::string s, const std::string& from, const std::string& to) {
        if (from.empty()) return s;
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
        return s;
    }, {"String", "From", "To"}).makePure();
    r.addFunction("String.Split", [](const std::string& s, const std::string& sep) {
        std::vector<std::string> out;
        if (sep.empty()) { out.push_back(s); return out; }
        size_t p = 0, q;
        while ((q = s.find(sep, p)) != std::string::npos) { out.push_back(s.substr(p, q - p)); p = q + sep.size(); }
        out.push_back(s.substr(p));
        return out;
    }, {"String", "Separator"}).makePure();
    r.addFunction("String.Join", [](const std::vector<std::string>& v, const std::string& sep) {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) { if (i) out += sep; out += v[i]; }
        return out;
    }, {"Strings", "Separator"}).makePure();
    r.addFunction("String.To String", [](const Value& v) { return v.toString(); }, {"Value"}).makePure().withKeywords("convert format");
    r.addFunction("String.Parse Int", [](const std::string& s) { try { return static_cast<int64_t>(std::stoll(s)); } catch (...) { return int64_t(0); } }, {"String"}).makePure();
    r.addFunction("String.Parse Float", [](const std::string& s) { try { return std::stod(s); } catch (...) { return 0.0; } }, {"String"}).makePure();
}

// ------------------------------------------------------------------ containers
// The first data input of an Array / Map node is the container: a node that modifies it changes the one it is connected to (containers
// are references inside a graph), every other node only reads it. Neither copies it.
void markContainerAccess(NodeDef& d, bool mutating) {
    for (auto& p : d.pins)
        if (p.kind == PinKind::Data && p.dir == PinDir::In) {
            p.access = mutating ? PinAccess::Modify : PinAccess::Read;
            return;
        }
}

void registerContainers(Registry& r) {
    auto arrayNode = [&](const char* id, Intrinsic fast, const char* title, bool pure, bool mutates, std::vector<PinDef> pins, std::function<void(NodeCtx&)> run) {
        NodeDef d = pure ? pureNode(id, title, "Containers|Array") : execNode(id, title, "Containers|Array");
        d.intrinsic = fast;
        d.typeParams.push_back({"T", {}, "int"});
        for (auto& p : pins) d.pins.push_back(std::move(p));
        d.mutatesFirstInput = mutates;
        markContainerAccess(d, mutates);
        d.keywords = "array list vector";
        d.run = std::move(run);
        r.addNode(std::move(d));
    };
    arrayNode("array.add", Intrinsic::ArrayAdd, "Add", false, true, {dataIn("Array", "array<T>"), dataIn("Item", "T")},
              [](NodeCtx& c) { arrayArg(c, 0).items.push_back(c.in(1).deepCopy()); });
    arrayNode("array.insert", Intrinsic::None, "Insert", false, true, {dataIn("Array", "array<T>"), dataIn("Index", "int", Value(0)), dataIn("Item", "T")}, [](NodeCtx& c) {
        auto& a = arrayArg(c, 0);
        int64_t i = checkIndex(c, c.inInt(1), a.items.size(), true);
        a.items.insert(a.items.begin() + i, c.in(2).deepCopy());
    });
    arrayNode("array.set", Intrinsic::ArraySet, "Set Item", false, true, {dataIn("Array", "array<T>"), dataIn("Index", "int", Value(0)), dataIn("Item", "T")}, [](NodeCtx& c) {
        auto& a = arrayArg(c, 0);
        a.items[static_cast<size_t>(checkIndex(c, c.inInt(1), a.items.size()))] = c.in(2).deepCopy();
    });
    arrayNode("array.remove", Intrinsic::None, "Remove At", false, true, {dataIn("Array", "array<T>"), dataIn("Index", "int", Value(0))}, [](NodeCtx& c) {
        auto& a = arrayArg(c, 0);
        a.items.erase(a.items.begin() + checkIndex(c, c.inInt(1), a.items.size()));
    });
    arrayNode("array.clear", Intrinsic::None, "Clear", false, true, {dataIn("Array", "array<T>")}, [](NodeCtx& c) { arrayArg(c, 0).items.clear(); });
    arrayNode("array.get", Intrinsic::ArrayGet, "Get Item", true, false, {dataIn("Array", "array<T>"), dataIn("Index", "int", Value(0)), aliases(dataOut("Item", "T"))}, [](NodeCtx& c) {
        auto& a = arrayArg(c, 0);
        c.out(0, a.items[static_cast<size_t>(checkIndex(c, c.inInt(1), a.items.size()))]);
    });
    arrayNode("array.length", Intrinsic::ArrayLength, "Length", true, false, {dataIn("Array", "array<T>"), dataOut("Length", "int")},
              [](NodeCtx& c) { c.out(0, Value(static_cast<int64_t>(arrayArg(c, 0).items.size()))); });
    arrayNode("array.contains", Intrinsic::None, "Contains", true, false, {dataIn("Array", "array<T>"), reads(dataIn("Item", "T")), dataOut("Contains", "bool")}, [](NodeCtx& c) {
        auto& a = arrayArg(c, 0);
        c.out(0, Value(std::find(a.items.begin(), a.items.end(), c.in(1)) != a.items.end()));
    });
    arrayNode("array.find", Intrinsic::None, "Find", true, false, {dataIn("Array", "array<T>"), reads(dataIn("Item", "T")), dataOut("Index", "int")}, [](NodeCtx& c) {
        auto& a = arrayArg(c, 0);
        auto it = std::find(a.items.begin(), a.items.end(), c.in(1));
        c.out(0, Value(it == a.items.end() ? int64_t(-1) : static_cast<int64_t>(it - a.items.begin())));
    });
    {
        NodeDef d = pureNode("array.make", "Make Array", "Containers|Array");
        d.keywords = "array literal";
        d.typeParams.push_back({"T", {}, "int"});
        d.dynamicPins = [](const Node& n, const NodeBuildCtx&, std::vector<PinDef>& pins) {
            int count = 2;
            try { count = std::clamp(std::stoi(n.prop("count", "2")), 0, 64); } catch (...) {}
            for (int i = 0; i < count; ++i) pins.push_back(dataIn("[" + std::to_string(i) + "]", "T"));
            pins.push_back(dataOut("Array", "array<T>"));
        };
        d.run = [](NodeCtx& c) {
            auto a = std::make_shared<ArrayData>();
            auto it = c.node().typeArgs.find("T");
            a->elem = c.registry().typeOrAny(it == c.node().typeArgs.end() ? "any" : it->second);
            for (size_t i = 0; i < c.inputCount(); ++i) a->items.push_back(c.in(i).deepCopy());
            c.out(0, Value(std::move(a)));
        };
        r.addNode(std::move(d));
    }

    auto mapNode = [&](const char* id, Intrinsic fast, const char* title, bool pure, bool mutates, std::vector<PinDef> pins, std::function<void(NodeCtx&)> run) {
        NodeDef d = pure ? pureNode(id, title, "Containers|Map") : execNode(id, title, "Containers|Map");
        d.intrinsic = fast;
        d.typeParams.push_back({"K", {}, "string"});
        d.typeParams.push_back({"V", {}, "int"});
        for (auto& p : pins) d.pins.push_back(std::move(p));
        d.mutatesFirstInput = mutates;
        markContainerAccess(d, mutates);
        d.keywords = "map dictionary table";
        d.run = std::move(run);
        r.addNode(std::move(d));
    };
    mapNode("map.set", Intrinsic::MapSet, "Set Entry", false, true, {dataIn("Map", "map<K,V>"), dataIn("Key", "K"), dataIn("Value", "V")},
            [](NodeCtx& c) { mapArg(c, 0).items[c.in(1)] = c.in(2).deepCopy(); });
    mapNode("map.remove", Intrinsic::None, "Remove Entry", false, true, {dataIn("Map", "map<K,V>"), dataIn("Key", "K")}, [](NodeCtx& c) { mapArg(c, 0).items.erase(c.in(1)); });
    mapNode("map.clear", Intrinsic::None, "Clear", false, true, {dataIn("Map", "map<K,V>")}, [](NodeCtx& c) { mapArg(c, 0).items.clear(); });
    mapNode("map.get", Intrinsic::MapGet, "Get Entry", true, false, {dataIn("Map", "map<K,V>"), reads(dataIn("Key", "K")), aliases(dataOut("Value", "V")), dataOut("Found", "bool")}, [](NodeCtx& c) {
        auto& m = mapArg(c, 0);
        auto it = m.items.find(c.in(1));
        c.out(0, it == m.items.end() ? c.registry().defaultValue(m.val) : it->second);
        c.out(1, Value(it != m.items.end()));
    });
    mapNode("map.contains", Intrinsic::MapContains, "Contains Key", true, false, {dataIn("Map", "map<K,V>"), reads(dataIn("Key", "K")), dataOut("Contains", "bool")},
            [](NodeCtx& c) { c.out(0, Value(mapArg(c, 0).items.count(c.in(1)) > 0)); });
    mapNode("map.length", Intrinsic::MapLength, "Length", true, false, {dataIn("Map", "map<K,V>"), dataOut("Length", "int")},
            [](NodeCtx& c) { c.out(0, Value(static_cast<int64_t>(mapArg(c, 0).items.size()))); });
    mapNode("map.keys", Intrinsic::None, "Keys", true, false, {dataIn("Map", "map<K,V>"), dataOut("Keys", "array<K>")}, [](NodeCtx& c) {
        auto& m = mapArg(c, 0);
        auto a = std::make_shared<ArrayData>();
        a->elem = m.key;
        for (auto& [k, v] : m.items) a->items.push_back(k);
        c.out(0, Value(std::move(a)));
    });
    mapNode("map.values", Intrinsic::None, "Values", true, false, {dataIn("Map", "map<K,V>"), dataOut("Values", "array<V>")}, [](NodeCtx& c) {
        auto& m = mapArg(c, 0);
        auto a = std::make_shared<ArrayData>();
        a->elem = m.val;
        for (auto& [k, v] : m.items) a->items.push_back(v);
        c.out(0, Value(std::move(a)));
    });
}

} // namespace

void registerBuiltins(Registry& reg) {
    reg.beginSource("core");
    registerScriptNodes(reg);
    registerFlow(reg);
    registerMath(reg);
    registerStrings(reg);
    registerContainers(reg);
    reg.endSource();
}

} // namespace fg
