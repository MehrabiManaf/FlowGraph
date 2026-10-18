// Minimal test runner: TEST(name) { CHECK(cond); }
#include <FlowGraph/Runtime.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace fg;

// A runtime whose application raises the events the test scripts handle.
struct TestRuntime : Runtime {
    TestRuntime() {
        registry().addEvent("Count");
        registry().addEvent("Go");
        registry().addEvent("OnExample");
        registry().addEvent("Other");
        registry().addEvent("Run");
    }
};


struct TestCase { const char* name; std::function<void()> fn; };
static std::vector<TestCase>& tests() { static std::vector<TestCase> t; return t; }
static int g_failures = 0;
struct Reg { Reg(const char* n, std::function<void()> f) { tests().push_back({n, std::move(f)}); } };

#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { ++g_failures; std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++g_failures; std::printf("  FAILED %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

// ---- helpers ---------------------------------------------------------------------------------------
static void link(Graph& g, NodeId a, const std::string& ap, NodeId b, const std::string& bp) {
    g.connections.push_back({a, ap, b, bp});
}

static std::string dumpDiagnostics(const Program& p) {
    std::string s;
    for (auto& d : p.diagnostics) s += "[" + d.graph + "#" + std::to_string(d.node) + "] " + d.message + "\n";
    return s;
}

static std::shared_ptr<Program> build(Runtime& rt, const Script& s) {
    auto p = rt.compile(s);
    if (!p->ok()) std::printf("%s", dumpDiagnostics(*p).c_str());
    return p;
}

// ---- reflected sample classes -----------------------------------------------------------------------
struct Animal {
    std::string name = "animal";
    int legs = 4;
    virtual ~Animal() = default;
    virtual std::string speak() const { return "..."; }
    void rename(const std::string& n) { name = n; }
};
struct Dog : Animal {
    std::string speak() const override { return "Woof"; }
    int fetched = 0;
    void fetch(int n) { fetched += n; }
};
enum class Mood { Happy, Sad };

static void registerSample(Runtime& rt) {
    Registry& r = rt.registry();
    r.registerEnum<Mood>("Mood").value("Happy", Mood::Happy).value("Sad", Mood::Sad);
    r.registerClass<Animal>("Animal")
        .constructor<>()
        .property("Name", &Animal::name)
        .property("Legs", &Animal::legs)
        .method("Speak", &Animal::speak)
        .method("Rename", &Animal::rename, {"NewName"});
    r.registerClass<Dog>("Dog").base<Animal>().constructor<>().property("Fetched", &Dog::fetched).method("Fetch", &Dog::fetch, {"Count"});
    r.addEvent("Tick", {{"DeltaTime", "float"}});
}

// ---- tests -----------------------------------------------------------------------------------------
TEST(json_roundtrip) {
    Json j = Json::parse(R"({"a":[1,2.5,"x",true,null],"b":{"c":"d\n"}})");
    CHECK_EQ(j["a"][1].asFloat(), 2.5);
    CHECK_EQ(j["b"]["c"].asString(), std::string("d\n"));
    Json k = Json::parse(j.dump());
    CHECK(j == k);
}

TEST(type_parse) {
    TestRuntime rt;
    auto* t = rt.registry().findType("map<string,array<float>>");
    CHECK(t && t->kind == TypeKind::Map);
    CHECK(t && t->elem->kind == TypeKind::Array);
    CHECK(rt.registry().findType("nope") == nullptr);
}

TEST(for_loop_sums_into_variable) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Total", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Run").id;
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults["First"] = Value(1);
    g.findNode(loop)->defaults["Last"] = Value(11);       // Last is exclusive: 1..10
    NodeId get = g.addNode("var.get").id;
    g.findNode(get)->props = {{"name", "Total"}, {"scope", "global"}};
    NodeId add = g.addNode("math.add").id;
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Total"}, {"scope", "global"}};
    link(g, ev, "Then", loop, "In");
    link(g, loop, "Loop Body", set, "In");
    link(g, get, "Value", add, "A");
    link(g, loop, "Index", add, "B");
    link(g, add, "Result", set, "Value");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    auto r = inst.callEvent("Run");
    CHECK(r.ok);
    CHECK_EQ(inst.getVariable("Total").toInt(), int64_t(55));
}

TEST(for_loop_last_index_is_exclusive) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Count", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults["First"] = Value(2);
    g.findNode(loop)->defaults["Last"] = Value(5);
    NodeId getC = g.addNode("var.get").id;
    g.findNode(getC)->props = {{"name", "Count"}, {"scope", "global"}};
    NodeId add = g.addNode("math.add").id;
    g.findNode(add)->defaults["B"] = Value(1);
    NodeId setC = g.addNode("var.set").id;
    g.findNode(setC)->props = {{"name", "Count"}, {"scope", "global"}};
    link(g, ev, "Then", loop, "In");
    link(g, loop, "Loop Body", setC, "In");
    link(g, getC, "Value", add, "A");
    link(g, add, "Result", setC, "Value");
    auto prog = build(rt, s);
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("Count").toInt(), int64_t(3));          // indices 2, 3, 4
    // an empty range runs nothing
    g.findNode(loop)->defaults["Last"] = Value(2);
    auto prog2 = build(rt, s);
    Instance inst2(prog2);
    CHECK(inst2.callEvent("Go").ok);
    CHECK_EQ(inst2.getVariable("Count").toInt(), int64_t(0));
}

TEST(enum_values_variables_and_names) {
    TestRuntime rt;
    registerSample(rt);
    // registered through registerEnum<Mood>: lookups by name, by value and by C++ type
    const TypeDesc* mood = rt.registry().findType("Mood");
    CHECK(mood && mood->isEnum());
    CHECK_EQ(mood->enumName(1), std::string("Sad"));
    CHECK(rt.registry().findTypeByCpp(typeid(Mood)) == mood);
    CHECK_EQ(rt.registry().convert(Value("Sad"), mood).toInt(), int64_t(1));      // names convert to values
    Script s;
    s.variables.push_back({"Current", "Mood", Value("Sad"), "", ""});             // default given by name
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId pr = g.addNode("util.print").id;
    NodeId get = g.addNode("var.get").id;
    g.findNode(get)->props = {{"name", "Current"}, {"scope", "global"}};
    link(g, ev, "Then", pr, "In");
    link(g, get, "Value", pr, "String");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    std::vector<std::string> log;
    inst.setLogHandler([&](LogLevel, const std::string& m) { log.push_back(m); });
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(log.size(), size_t(1));
    CHECK_EQ(log.empty() ? std::string() : log[0], std::string("Sad"));            // printed with its name
    // the generated "enum:Mood" node and its palette entries exist
    CHECK(rt.registry().findNode("enum:Mood") != nullptr);
    fg::NodeBuildCtx ctx{rt.registry(), &s, s.findGraph("EventGraph")};
    int entries = 0;
    for (auto& t : fg::buildPalette(ctx)) entries += t.defId == "enum:Mood";
    CHECK_EQ(entries, 2);
}

// ---- events belong to the application -------------------------------------------------------------------------
TEST(events_come_from_the_application_only) {
    TestRuntime rt;                                   // provides Go, Run, ... but scripts cannot declare events of their own
    Script s;
    Graph& g = s.addEventGraph();
    g.addNode("event.custom").props["name"] = "Mine";   // the old way of declaring an event
    auto rep = checkCompatibility(s, rt.registry());
    CHECK(!rep.ok());
    CHECK(rep.issues[0].kind == CompatibilityIssue::Kind::MissingNode);
    CHECK(!rt.compile(s)->ok());
    CHECK(rt.registry().findNode("event.custom") == nullptr);
    CHECK(rt.registry().findNode("event:Go") != nullptr);
}

TEST(palette_offers_only_events_that_are_not_handled_yet) {
    TestRuntime rt;
    Script s;
    Graph& g = s.addEventGraph();
    g.addNode("event:Go");
    Graph& f = s.addFunction("Fn");
    auto ids = [&](const Graph* graph) {
        std::set<std::string> out;
        for (auto& t : buildPalette(NodeBuildCtx{rt.registry(), &s, graph})) out.insert(t.defId);
        return out;
    };
    auto inEvents = ids(&s.graphs[0]);
    CHECK(!inEvents.count("event:Go"));               // already handled
    CHECK(inEvents.count("event:Run"));               // still available
    CHECK(!ids(&f).count("event:Run"));               // handlers live in event graphs
}

TEST(function_call_with_return) {
    TestRuntime rt;
    Script s;
    Graph& f = s.addFunction("Mul3", {{"X", "float", false, false, Value()}}, {{"Out", "float", false, false, Value()}});
    NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
    NodeId mul = f.addNode("math.mul").id;
    f.findNode(mul)->defaults["B"] = Value(3.0);
    link(f, entry, "X", mul, "A");
    link(f, mul, "Result", ret, "Out");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK_EQ(prog->graphs[0].nodes[size_t(prog->graphs[0].entry)].title, std::string("Mul3"));      // entry node is named after its function
    auto r = inst.callFunction("Mul3", {Value(2.5)});
    CHECK(r.ok);
    CHECK_EQ(r.returns.size(), size_t(1));
    CHECK_EQ(r.returns[0].toFloat(), 7.5);
}

TEST(by_ref_parameter_writes_through) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Counter", "int", Value(5), "", ""});
    Graph& f = s.addFunction("Bump", {{"V", "int", true, false, Value()}});
    NodeId entry = f.nodes[0].id;
    NodeId getp = f.addNode("var.get").id;
    f.findNode(getp)->props = {{"name", "V"}, {"scope", "param"}};
    NodeId add = f.addNode("math.add").id;
    f.findNode(add)->defaults["B"] = Value(10);
    NodeId setp = f.addNode("var.set").id;
    f.findNode(setp)->props = {{"name", "V"}, {"scope", "param"}};
    link(f, getp, "Value", add, "A");
    link(f, add, "Result", setp, "Value");
    f.connections.clear();
    link(f, getp, "Value", add, "A");
    link(f, add, "Result", setp, "Value");
    link(f, entry, "Then", setp, "In");
    NodeId ret = f.nodes[1].id;
    link(f, setp, "Then", ret, "In");

    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId var = g.addNode("var.get").id;
    g.findNode(var)->props = {{"name", "Counter"}, {"scope", "global"}};
    NodeId call = g.addNode("func.call").id;
    g.findNode(call)->props["function"] = "Bump";
    link(g, ev, "Then", call, "In");
    link(g, var, "Value", call, "V");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("Counter").toInt(), int64_t(15));
}

TEST(by_ref_requires_variable) {
    TestRuntime rt;
    Script s;
    s.addFunction("Bump", {{"V", "int", true, false, Value()}});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId call = g.addNode("func.call").id;
    g.findNode(call)->props["function"] = "Bump";
    link(g, ev, "Then", call, "In");
    auto prog = rt.compile(s);
    CHECK(!prog->ok());
}

TEST(const_param_cannot_be_modified) {
    TestRuntime rt;
    Script s;
    Graph& f = s.addFunction("Bad", {{"A", "array<int>", false, true, Value()}});
    NodeId entry = f.nodes[0].id;
    NodeId add = f.addNode("array.add").id;
    link(f, entry, "Then", add, "In");
    NodeId getp = f.addNode("var.get").id;
    f.findNode(getp)->props = {{"name", "A"}, {"scope", "param"}};
    link(f, getp, "Value", add, "Array");
    auto prog = rt.compile(s);
    CHECK(!prog->ok());
}

TEST(reflection_methods_properties_and_inheritance) {
    TestRuntime rt;
    registerSample(rt);
    Script s;
    s.variables.push_back({"Pet", "Dog", Value(), "", ""});
    s.variables.push_back({"Said", "string", Value(""), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId mk = g.addNode("new:Dog").id;
    NodeId setPet = g.addNode("var.set").id;
    g.findNode(setPet)->props = {{"name", "Pet"}, {"scope", "global"}};
    NodeId getPet = g.addNode("var.get").id;
    g.findNode(getPet)->props = {{"name", "Pet"}, {"scope", "global"}};
    NodeId speak = g.addNode("m:Animal.Speak").id;   // inherited method on a Dog
    NodeId setSaid = g.addNode("var.set").id;
    g.findNode(setSaid)->props = {{"name", "Said"}, {"scope", "global"}};
    NodeId fetch = g.addNode("m:Dog.Fetch").id;
    g.findNode(fetch)->defaults["Count"] = Value(3);
    link(g, ev, "Then", mk, "In");
    link(g, mk, "Then", setPet, "In");
    link(g, mk, "Object", setPet, "Value");
    link(g, setPet, "Then", fetch, "In");
    link(g, getPet, "Value", fetch, "Target");
    link(g, getPet, "Value", speak, "Target");
    link(g, speak, "Result", setSaid, "Value");
    link(g, fetch, "Then", setSaid, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    auto r = inst.callEvent("Go");
    CHECK(r.ok);
    CHECK_EQ(inst.getVariable("Said").toString(), std::string("Woof"));     // virtual dispatch through the base method
    auto dog = rt.registry().unwrap<Dog>(inst.getVariable("Pet"));
    CHECK(dog && dog->fetched == 3);
}

TEST(host_event_with_arguments_and_generics) {
    TestRuntime rt;
    registerSample(rt);
    Script s;
    s.variables.push_back({"Acc", "float", Value(0.0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Tick").id;
    NodeId get = g.addNode("var.get").id;
    g.findNode(get)->props = {{"name", "Acc"}, {"scope", "global"}};
    NodeId add = g.addNode("math.add").id;     // T is inferred as float from the connections
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Acc"}, {"scope", "global"}};
    link(g, ev, "Then", set, "In");
    link(g, get, "Value", add, "A");
    link(g, ev, "DeltaTime", add, "B");
    link(g, add, "Result", set, "Value");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    CHECK(prog->graphs[0].nodes[2].typeArgs["T"] == "float");
    Instance inst(prog);
    inst.callEvent("Tick", {Value(0.25)});
    inst.callEvent("Tick", {Value(0.5)});
    CHECK_EQ(inst.getVariable("Acc").toFloat(), 0.75);
}

TEST(delay_suspends_until_tick) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Done", "bool", Value(false), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId delay = g.addNode("flow.delay").id;
    g.findNode(delay)->defaults["Duration"] = Value(1.0);
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Done"}, {"scope", "global"}};
    g.findNode(set)->defaults["Value"] = Value(true);
    link(g, ev, "Then", delay, "In");
    link(g, delay, "Then", set, "In");
    auto prog = build(rt, s);
    Instance inst(prog);
    auto r = inst.callEvent("Go");
    CHECK(r.waiting());
    CHECK(!inst.getVariable("Done").toBool());
    inst.tick(0.5);
    CHECK(!inst.getVariable("Done").toBool());
    inst.tick(0.6);
    CHECK(inst.getVariable("Done").toBool());
    CHECK_EQ(inst.pendingTaskCount(), size_t(0));
}

TEST(break_exits_loop) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"N", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults["Last"] = Value(100);
    NodeId gt = g.addNode("cmp.ge").id;
    g.findNode(gt)->defaults["B"] = Value(4);
    NodeId br = g.addNode("flow.branch").id;
    NodeId brk = g.addNode("flow.break").id;
    NodeId getN = g.addNode("var.get").id;
    g.findNode(getN)->props = {{"name", "N"}, {"scope", "global"}};
    NodeId inc = g.addNode("math.add").id;
    g.findNode(inc)->defaults["B"] = Value(1);
    NodeId setN = g.addNode("var.set").id;
    g.findNode(setN)->props = {{"name", "N"}, {"scope", "global"}};
    link(g, ev, "Then", loop, "In");
    link(g, loop, "Loop Body", setN, "In");
    link(g, getN, "Value", inc, "A");
    link(g, inc, "Result", setN, "Value");
    link(g, setN, "Then", br, "In");
    link(g, getN, "Value", gt, "A");
    link(g, gt, "Result", br, "Condition");
    link(g, br, "True", brk, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("N").toInt(), int64_t(4));
}

TEST(containers_and_foreach) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"List", "array<int>", Value(), "", ""});
    s.variables.push_back({"Sum", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId getL = g.addNode("var.get").id;
    g.findNode(getL)->props = {{"name", "List"}, {"scope", "global"}};
    NodeId a1 = g.addNode("array.add").id;
    g.findNode(a1)->defaults["Item"] = Value(4);
    NodeId a2 = g.addNode("array.add").id;
    g.findNode(a2)->defaults["Item"] = Value(6);
    NodeId each = g.addNode("flow.foreach").id;
    NodeId getS = g.addNode("var.get").id;
    g.findNode(getS)->props = {{"name", "Sum"}, {"scope", "global"}};
    NodeId add = g.addNode("math.add").id;
    NodeId setS = g.addNode("var.set").id;
    g.findNode(setS)->props = {{"name", "Sum"}, {"scope", "global"}};
    link(g, ev, "Then", a1, "In");
    link(g, getL, "Value", a1, "Array");
    link(g, a1, "Then", a2, "In");
    link(g, getL, "Value", a2, "Array");
    link(g, a2, "Then", each, "In");
    link(g, getL, "Value", each, "Array");
    link(g, each, "Loop Body", setS, "In");
    link(g, getS, "Value", add, "A");
    link(g, each, "Element", add, "B");
    link(g, add, "Result", setS, "Value");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("Sum").toInt(), int64_t(10));
    CHECK_EQ(inst.getVariable("List").arrayPtr()->items.size(), size_t(2));
}

TEST(script_json_roundtrip) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"X", "int", Value(7), "Cat", "doc"});
    s.addFunction("F", {{"A", "int", true, false, Value(1)}}, {{"R", "int", false, false, Value()}});
    Graph& g = s.addEventGraph();
    g.addNode("event:Go");
    g.addComment(1, 2).text = "hello";
    Script t = Script::parse(s.toText());
    CHECK_EQ(t.toText(), s.toText());
    CHECK_EQ(t.graphs.size(), size_t(2));
    CHECK(t.graphs[0].inputs[0].byRef);
}

TEST(files_of_a_newer_version_are_refused) {
    auto refused = [](const std::string& text, const char* what) {
        try { Script::parse(text); } catch (const JsonError& e) { return std::string(e.what()).find(what) != std::string::npos; }
        return false;
    };
    Script s;
    s.addEventGraph();
    Json j = s.toJson();
    CHECK_EQ(j["version"].asInt(), int64_t(Script::kFileVersion));
    j.set("version", Script::kFileVersion + 1);
    CHECK(refused(j.dump(), "newer FlowGraph"));
    j.set("version", 0);
    CHECK(refused(j.dump(), "invalid script file version"));
    j.set("version", Script::kFileVersion);
    CHECK_EQ(Script::parse(j.dump()).graphs.size(), size_t(1));

    Json lj = standard::LaunchFile().toJson();
    lj.set("version", 2);
    bool launchRefused = false;
    try { standard::LaunchFile::fromJson(lj); } catch (const JsonError&) { launchRefused = true; }
    CHECK(launchRefused);
}

TEST(runtime_error_is_reported_with_node) {
    TestRuntime rt;
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId pr = g.addNode("util.print").id;
    NodeId div = g.addNode("math.div").id;
    g.findNode(div)->defaults["A"] = Value(1);
    g.findNode(div)->defaults["B"] = Value(0);
    link(g, ev, "Then", pr, "In");
    link(g, div, "Result", pr, "String");
    auto prog = build(rt, s);
    Instance inst(prog);
    inst.setLogHandler([](LogLevel, const std::string&) {});
    auto r = inst.callEvent("Go");
    CHECK(!r.ok);
    CHECK(r.state == TaskState::Failed);
    CHECK_EQ(r.errorNode, div);
}

// Scripts must not be able to end the host process: each of these used to kill it (hardware trap, stack overflow, std::terminate).
TEST(int64_min_divided_by_minus_one_does_not_trap) {
    for (const char* op : {"math.div", "math.mod"}) {
        TestRuntime rt;
        Script s;
        s.variables.push_back({"R", "int", Value(7), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = g.addNode("event:Go").id;
        NodeId n = g.addNode(op).id;
        g.findNode(n)->defaults["A"] = Value(std::numeric_limits<int64_t>::min());
        g.findNode(n)->defaults["B"] = Value(-1);
        NodeId set = g.addNode("var.set").id;
        g.findNode(set)->props = {{"name", "R"}, {"scope", "global"}};
        link(g, ev, "Then", set, "In");
        link(g, n, "Result", set, "Value");
        auto prog = build(rt, s);
        Instance inst(prog);
        CHECK(inst.callEvent("Go").ok);
        CHECK_EQ(inst.getVariable("R").toInt(), std::string(op) == "math.div" ? std::numeric_limits<int64_t>::min() : int64_t(0));
    }
    CHECK_EQ(divideInt(std::numeric_limits<int64_t>::min(), -1), std::numeric_limits<int64_t>::min());
    CHECK_EQ(divideInt(7, -1), int64_t(-7));
    CHECK_EQ(moduloInt(std::numeric_limits<int64_t>::min(), -1), int64_t(0));
    CHECK_EQ(moduloInt(-7, 3), int64_t(-1));
}

TEST(integer_overflow_wraps_around) {
    const int64_t mn = std::numeric_limits<int64_t>::min(), mx = std::numeric_limits<int64_t>::max();
    struct Case { const char* op; int64_t a, b, expected; };
    const Case cases[] = {{"math.add", mx, 1, mn}, {"math.sub", mn, 1, mx}, {"math.mul", mx, 2, -2}, {"math.mul", mn, -1, mn},
                          {"math.neg", mn, 0, mn}, {"math.abs", mn, 0, mn}};
    // Every case runs twice: inline (the interpreter's intrinsic) and through the node's `run` (a copy of the node without the intrinsic).
    for (bool viaRun : {false, true}) {
        for (const Case& c : cases) {
            TestRuntime rt;
            std::string op = c.op;
            if (viaRun) {
                NodeDef copy = *rt.registry().findNode(c.op);
                copy.id = std::string("test.") + c.op;
                copy.intrinsic = Intrinsic::None;
                rt.registry().addNode(std::move(copy));
                op = std::string("test.") + c.op;
            }
            Script s;
            s.variables.push_back({"R", "int", Value(7), "", ""});
            Graph& g = s.addEventGraph();
            NodeId ev = g.addNode("event:Go").id;
            Node& n = g.addNode(op);
            const bool unary = std::string(c.op) == "math.neg" || std::string(c.op) == "math.abs";
            if (unary) n.defaults["Value"] = Value(c.a);
            else { n.defaults["A"] = Value(c.a); n.defaults["B"] = Value(c.b); }
            NodeId nid = n.id;
            NodeId set = g.addNode("var.set").id;
            g.findNode(set)->props = {{"name", "R"}, {"scope", "global"}};
            link(g, ev, "Then", set, "In");
            link(g, nid, "Result", set, "Value");
            auto prog = build(rt, s);
            Instance inst(prog);
            CHECK(inst.callEvent("Go").ok);
            CHECK_EQ(inst.getVariable("R").toInt(), c.expected);
        }
    }
    CHECK_EQ(addInt(mx, 1), mn);
    CHECK_EQ(subInt(mn, 1), mx);
    CHECK_EQ(negInt(mn), mn);
}

TEST(deeply_nested_containers_are_a_script_error) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Cur", "any", Value(), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults["Last"] = Value(100000);
    NodeId get = g.addNode("var.get").id;
    g.findNode(get)->props = {{"name", "Cur"}, {"scope", "global"}};
    NodeId mk = g.addNode("array.make").id;
    g.findNode(mk)->props["count"] = "1";
    g.findNode(mk)->typeArgs["T"] = "any";
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Cur"}, {"scope", "global"}};
    link(g, ev, "Then", loop, "In");
    link(g, loop, "Loop Body", set, "In");
    link(g, get, "Value", mk, "[0]");
    link(g, mk, "Array", set, "Value");
    auto prog = build(rt, s);
    Instance inst(prog);
    auto r = inst.callEvent("Go");
    CHECK(!r.ok);
    CHECK(r.error.find("nested too deeply") != std::string::npos);
    CHECK_EQ(r.errorNode, set);     // storing into the variable copies the container
    // the data built so far is still a valid, droppable value
    Value v = inst.getVariable("Cur");
    int depth = 0;
    for (const Value* p = &v; p->isArray() && !p->arrayPtr()->items.empty(); p = &p->arrayPtr()->items[0]) ++depth;
    CHECK(depth > 100 && depth <= Value::kMaxContainerDepth + 1);
}

TEST(unknown_native_exception_is_a_script_error) {
    TestRuntime rt;
    rt.registry().addFunction("Test.ThrowsInt", []() -> int { throw 42; }).makePure();
    Script s;
    s.variables.push_back({"X", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId t = g.addNode("fn:Test.ThrowsInt").id;
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "X"}, {"scope", "global"}};
    link(g, ev, "Then", set, "In");
    link(g, t, "Result", set, "Value");
    auto prog = build(rt, s);
    Instance inst(prog);
    auto r = inst.callEvent("Go");
    CHECK(!r.ok);
    CHECK(r.state == TaskState::Failed);
    CHECK(r.error.find("unknown exception") != std::string::npos);
}

TEST(debugger_breakpoint_and_step) {
    TestRuntime rt;
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId p1 = g.addNode("util.print").id;
    NodeId p2 = g.addNode("util.print").id;
    link(g, ev, "Then", p1, "In");
    link(g, p1, "Then", p2, "In");
    auto prog = build(rt, s);
    Instance inst(prog);
    std::vector<std::string> log;
    inst.setLogHandler([&](LogLevel, const std::string& m) { log.push_back(m); });
    Debugger dbg;
    std::vector<NodeId> fired;
    dbg.linkFired = [&](const std::string&, NodeId, int, NodeId to) { fired.push_back(to); };
    inst.setDebugHooks(&dbg);
    dbg.setBreakpoint(g.name, p2, true);
    auto r = inst.callEvent("Go");
    CHECK(r.paused());
    CHECK_EQ(r.task->currentNode(), p2);
    CHECK_EQ(log.size(), size_t(1));
    dbg.setBreakpoint(g.name, p2, false);
    auto r2 = r.task->run();
    CHECK(r2.ok);
    CHECK_EQ(log.size(), size_t(2));
    CHECK_EQ(fired.size(), size_t(2));
    // stepping: pause at the very first node, then step into the next one
    log.clear();
    dbg.pauseNext();
    auto r3 = inst.callEvent("Go");
    CHECK(r3.paused());
    CHECK_EQ(r3.task->currentNode(), ev);
    dbg.command(*r3.task, Debugger::Command::StepInto);
    auto r4 = r3.task->run();
    CHECK(r4.paused());
    CHECK_EQ(r4.task->currentNode(), p1);
}

TEST(infinite_loop_is_stopped) {
    TestRuntime rt;
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId w = g.addNode("flow.while").id;
    g.findNode(w)->defaults["Condition"] = Value(true);
    link(g, ev, "Then", w, "In");
    auto prog = build(rt, s);
    Instance inst(prog);
    inst.setLogHandler([](LogLevel, const std::string&) {});
    inst.setStepLimit(10000);
    auto r = inst.callEvent("Go");
    CHECK(!r.ok);
}

TEST(type_mismatch_is_a_compile_error) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Arr", "array<int>", Value(), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId get = g.addNode("var.get").id;
    g.findNode(get)->props = {{"name", "Arr"}, {"scope", "global"}};
    NodeId pr = g.addNode("math.add").id;
    link(g, get, "Value", pr, "A");
    auto prog = rt.compile(s);
    CHECK(!prog->ok());
    (void)ev;
}

static std::string g_exeDir;
// File name extension of shared libraries (plugins) on this platform.
#if defined(_WIN32)
static const std::string kLibExt = ".dll";
#elif defined(__APPLE__)
static const std::string kLibExt = ".dylib";
#else
static const std::string kLibExt = ".so";
#endif

TEST(palette_search_and_pin_filter) {
    TestRuntime rt;
    registerSample(rt);
    Script s;
    s.variables.push_back({"Score", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeBuildCtx ctx{rt.registry(), &s, &g};
    auto all = buildPalette(ctx);
    CHECK(!all.empty());
    auto hits = searchPalette(all, "branch");
    CHECK(!hits.empty() && hits[0].title == "Branch");
    auto vars = searchPalette(all, "get score");
    CHECK(!vars.empty() && vars[0].props["name"] == "Score");
    // dragging an int output: Add must be offered (generic), Branch (bool condition accepts int) too, Print String as well
    PinFilter f;
    f.kind = PinKind::Data; f.dir = PinDir::Out; f.type = rt.registry().tInt();
    auto fit = filterPalette(rt.registry(), all, f);
    bool hasAdd = false, hasForeach = false;
    for (auto& t : fit) { hasAdd |= t.defId == "math.add"; hasForeach |= t.defId == "flow.foreach"; }
    CHECK(hasAdd);
    CHECK(!hasForeach);                       // needs an array
    // dragging an exec output: only nodes with an exec input
    PinFilter e; e.kind = PinKind::Exec; e.dir = PinDir::Out;
    for (auto& t : filterPalette(rt.registry(), all, e)) CHECK(t.defId != "math.add");
    // events are not creatable inside a function graph
    Graph& fn = s.addFunction("F");
    NodeBuildCtx fctx{rt.registry(), &s, &fn};
    for (auto& t : buildPalette(fctx)) CHECK(t.defId != "event:Tick");
    // instantiate applies properties
    Node& n = instantiate(g, vars[0], 5, 6);
    CHECK_EQ(n.def, std::string("var.get"));
    CHECK_EQ(n.prop("name"), std::string("Score"));
}

TEST(inspect_values_and_frames) {
    TestRuntime rt;
    registerSample(rt);
    auto dog = std::make_shared<Dog>();
    dog->name = "Rex";
    Value v = rt.registry().wrap(dog);
    auto view = inspect(rt.registry(), v);
    CHECK_EQ(view.type, std::string("Dog"));
    bool foundName = false;
    for (auto& c : view.children) if (c.name == "Name") { foundName = true; CHECK_EQ(c.text, std::string("\"Rex\"")); }
    CHECK(foundName);
    auto arr = rt.registry().makeArray(rt.registry().tInt());
    arr.arrayPtr()->items = {Value(1), Value(2)};
    auto av = inspect(rt.registry(), arr);
    CHECK_EQ(av.text, std::string("array[2]"));
    CHECK_EQ(av.children.size(), size_t(2));
    auto ev = inspect(rt.registry(), Value(1), rt.registry().findType("Mood"));
    CHECK_EQ(ev.text, std::string("Sad"));

    Script s;
    s.variables.push_back({"G", "int", Value(3), "", ""});
    Graph& f = s.addFunction("F", {{"P", "int", false, false, Value()}});
    f.locals.push_back({"L", "string", Value("x"), "", ""});
    f.connections.clear();
    NodeId entry = f.nodes[0].id;
    NodeId d = f.addNode("flow.delay").id;
    link(f, entry, "Then", d, "In");
    auto prog = build(rt, s);
    Instance inst(prog);
    auto g = inspectGlobals(inst);
    CHECK_EQ(g.size(), size_t(1));
    CHECK_EQ(g[0].value.text, std::string("3"));
    auto r = inst.callFunction("F", {Value(9)});
    CHECK(r.waiting());
    auto fv = inspectFrame(*r.task, 0);
    CHECK_EQ(fv.size(), size_t(2));
    CHECK_EQ(fv[0].value.name, std::string("P"));
    CHECK_EQ(fv[0].value.text, std::string("9"));
    CHECK_EQ(fv[1].value.text, std::string("\"x\""));
}

TEST(plugin_loading) {
    std::string path = g_exeDir + "/plugins/ExamplePlugin" + kLibExt;
    if (!std::filesystem::exists(path)) { std::printf("  (skipped: %s not built)\n", path.c_str()); return; }
    TestRuntime rt;
    std::string err;
    CHECK(rt.loadPlugin(path, &err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(rt.registry().findType("Counter") != nullptr);
    CHECK(rt.registry().findNode("m:Counter.Increment") != nullptr);
    CHECK_EQ(rt.registry().findNode("fn:Example.Greet")->source, std::string("ExamplePlugin"));
    Script s;
    s.variables.push_back({"Msg", "string", Value(""), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:OnExample").id;
    NodeId greet = g.addNode("fn:Example.Greet").id;
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Msg"}, {"scope", "global"}};
    link(g, ev, "Then", set, "In");
    link(g, ev, "Message", greet, "Name");
    link(g, greet, "Result", set, "Value");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("OnExample", {Value("plugin")}).ok);
    CHECK_EQ(inst.getVariable("Msg").toString(), std::string("Hello, plugin!"));
}

// ---- Exit, Executor, AsyncExecutor -------------------------------------------------------------------
static void registerTestExit(Runtime& rt) {
    NodeDef d;
    d.id = "test.exit";
    d.title = "Exit";
    d.pins = {execIn(), dataIn("Code", "int", Value(0))};
    d.run = [](NodeCtx& c) { c.exit(static_cast<int>(c.inInt(0))); };
    rt.registry().addNode(std::move(d));
}

// event "Go": [Delay seconds] -> Print "<text>"   (delay < 0 = no delay)
static Script printScript(double delay, const char* text) {
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId pr = g.addNode("util.print").id;
    g.findNode(pr)->defaults["String"] = Value(text);
    if (delay >= 0) {
        NodeId d = g.addNode("flow.delay").id;
        g.findNode(d)->defaults["Duration"] = Value(delay);
        link(g, ev, "Then", d, "In");
        link(g, d, "Then", pr, "In");
    } else link(g, ev, "Then", pr, "In");
    return s;
}

TEST(exit_node_ends_the_task_with_a_code) {
    TestRuntime rt;
    registerTestExit(rt);
    Script s = printScript(-1, "before");
    Graph& g = s.graphs[0];
    NodeId ex = g.addNode("test.exit").id;
    g.findNode(ex)->defaults["Code"] = Value(7);
    NodeId after = g.addNode("util.print").id;
    g.findNode(after)->defaults["String"] = Value("after");
    link(g, 2, "Then", ex, "In");           // Print "before" -> Exit(7) -> Print "after" (never reached)
    link(g, ex, "Then", after, "In");
    // Exit has no exec output pin: the line above is dropped by the compiler warning only when pins exist, so build it by hand
    g.connections.pop_back();
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    std::vector<std::string> log;
    inst.setLogHandler([&](LogLevel, const std::string& m) { log.push_back(m); });
    auto r = inst.callEvent("Go");
    CHECK(r.ok);
    CHECK(r.exited);
    CHECK_EQ(r.exitCode, 7);
    CHECK_EQ(log.size(), size_t(1));
}

TEST(executor_pump_advances_delays_without_blocking) {
    TestRuntime rt;
    auto prog = build(rt, printScript(0.2, "late"));
    Executor ex(prog);
    std::vector<std::string> log;
    ex.instance().setLogHandler([&](LogLevel, const std::string& m) { log.push_back(m); });
    int completions = 0;
    ex.setCompletionHandler([&](const Executor::Completion& c) { ++completions; CHECK(c.result.ok); CHECK_EQ(c.name, std::string("Go")); });
    std::string err;
    CHECK(ex.postEvent("Go", {}, &err) != 0);
    auto st = ex.pump(0.0);
    CHECK_EQ(st.waiting, size_t(1));
    CHECK(st.nextWake > 0.15 && st.nextWake <= 0.2);
    CHECK(log.empty());
    CHECK(!ex.idle());
    st = ex.pump(0.1);
    CHECK(log.empty());
    st = ex.pump(0.15);                     // total 0.25 s of script time, no real waiting
    CHECK_EQ(log.size(), size_t(1));
    CHECK_EQ(completions, 1);
    CHECK(ex.idle());
    CHECK(ex.postEvent("Nope", {}, &err) == 0 && !err.empty());
}

TEST(executor_runs_several_tasks_fairly) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"A", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Count").id;
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults["Last"] = Value(5000);
    link(g, ev, "Then", loop, "In");
    auto prog = build(rt, s);
    Executor ex(prog);
    int done = 0;
    ex.setCompletionHandler([&](const Executor::Completion&) { ++done; });
    ex.postEvent("Count");
    ex.postEvent("Count");
    int pumps = 0;
    while (!ex.idle() && pumps < 100) { ex.pump(0, 300); ++pumps; }      // small slices: both tasks advance together
    CHECK_EQ(done, 2);
    CHECK(pumps > 2);
}

TEST(serial_executor_runs_one_task_at_a_time_in_the_order_they_were_posted) {
    TestRuntime rt;
    auto prog = build(rt, printScript(0.1, "x"));                  // Go: wait 0.1 s, then print
    auto run = [&](bool serial, std::vector<Executor::Handle>& order, Executor::Handle* third, size_t* doneAt15) {
        Executor ex(prog);
        ex.setSerial(serial);
        ex.setCompletionHandler([&](const Executor::Completion& c) { order.push_back(c.handle); });
        const auto h1 = ex.postEvent("Go");
        ex.postEvent("Go");
        const auto h3 = ex.postEvent("Go");
        (void)h1;
        for (int i = 0; i < 15; ++i) ex.pump(0.01);                  // 0.15 s of script time
        *doneAt15 = order.size();
        if (third) *third = h3;
        if (serial) CHECK(ex.task(h3) && ex.task(h3)->state() == TaskState::Ready);       // queued: it has not even started
        for (int i = 0; i < 40; ++i) ex.pump(0.01);
        CHECK(ex.idle());
    };
    std::vector<Executor::Handle> together, serial;
    size_t togetherAt = 0, serialAt = 0;
    run(false, together, nullptr, &togetherAt);
    Executor::Handle third = 0;
    run(true, serial, &third, &serialAt);
    CHECK_EQ(togetherAt, size_t(3));                                // without the mode all three waited together: done after 0.1 s
    CHECK_EQ(serialAt, size_t(1));                                  // serial: the second one started when the first ended (0.1 s) and ends at 0.2 s
    CHECK_EQ(serial.size(), size_t(3));
    CHECK(serial[0] < serial[1] && serial[1] < serial[2] && serial[2] == third);       // in the order they were posted
}

TEST(serial_executor_is_held_up_by_a_task_that_a_breakpoint_stopped) {
    TestRuntime rt;
    auto prog = build(rt, printScript(-1, "x"));                     // Go: event (1) -> print (2)
    std::atomic<int> paused{0};
    std::mutex mu;
    std::vector<Executor::Handle> completed;
    AsyncExecutor::Callbacks cb;
    cb.taskState = [&](AsyncExecutor::Handle, TaskState st) { if (st == TaskState::Paused) ++paused; };
    cb.completed = [&](const Executor::Completion& c) { std::lock_guard lk(mu); completed.push_back(c.handle); };
    AsyncExecutor::Options options;
    options.serial = true;                                          // all an application has to say
    AsyncExecutor ax(prog, cb, nullptr, options);
    ax.debugger().setBreakpoint("EventGraph", 2, true);
    const auto h1 = ax.postEvent("Go");
    const auto h2 = ax.postEvent("Go");
    const auto h3 = ax.postEvent("Go");
    for (int i = 0; i < 300 && paused.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(paused.load(), 1);                                     // only the first task started: it stands at the breakpoint
    TaskState s2 = TaskState::Finished;
    CHECK(ax.inspect([&](Executor& ex) { if (auto t = ex.task(h2)) s2 = t->state(); }));
    CHECK(s2 == TaskState::Ready);                                  // the second one waits for it
    ax.resume(h1);                                                  // the first runs to its end ...
    for (int i = 0; i < 300 && paused.load() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK_EQ(paused.load(), 2);                                     // ... and the next one is at the breakpoint
    ax.debugger().setBreakpoint("EventGraph", 2, false);
    ax.resume(h2);
    CHECK(ax.waitIdle(3000));
    std::lock_guard lk(mu);
    CHECK_EQ(completed.size(), size_t(3));
    CHECK(completed.size() == 3 && completed[0] == h1 && completed[1] == h2 && completed[2] == h3);
}

TEST(async_executor_runs_on_its_own_thread) {
    TestRuntime rt;
    auto prog = build(rt, printScript(0.1, "async"));
    std::mutex mu;
    std::vector<std::string> log;
    std::thread::id logThread;
    std::atomic<int> completed{0};
    bool okResult = false;
    AsyncExecutor::Callbacks cb;
    cb.log = [&](LogLevel, const std::string& m) { std::lock_guard lk(mu); log.push_back(m); logThread = std::this_thread::get_id(); };
    cb.completed = [&](const Executor::Completion& c) { okResult = c.result.ok; ++completed; };
    AsyncExecutor ax(prog, cb);
    auto t0 = std::chrono::steady_clock::now();
    auto h = ax.postEvent("Go");
    CHECK(h != 0);
    auto postTime = std::chrono::steady_clock::now() - t0;
    CHECK(std::chrono::duration<double>(postTime).count() < 0.05);       // posting never blocks on the script
    CHECK(ax.waitIdle(3000));
    CHECK_EQ(completed.load(), 1);
    CHECK(okResult);
    {
        std::lock_guard lk(mu);
        CHECK_EQ(log.size(), size_t(1));
        CHECK(logThread != std::this_thread::get_id());
    }
    // unknown events are reported, not thrown
    std::string failure;
    AsyncExecutor::Callbacks cb2;
    cb2.completed = [&](const Executor::Completion& c) { failure = c.result.error; };
    AsyncExecutor ax2(prog, cb2);
    ax2.postEvent("Missing");
    CHECK(ax2.waitIdle(3000));
    CHECK(!failure.empty());
}

TEST(async_executor_pause_inspect_resume_and_stop) {
    TestRuntime rt;
    Script s = printScript(-1, "one");
    Graph& g = s.graphs[0];
    NodeId second = g.addNode("util.print").id;
    g.findNode(second)->defaults["String"] = Value("two");
    link(g, 2, "Then", second, "In");
    auto prog = build(rt, s);
    std::atomic<int> paused{0}, completed{0};
    std::mutex mu;
    std::vector<std::string> log;
    AsyncExecutor::Callbacks cb;
    cb.taskState = [&](AsyncExecutor::Handle, TaskState st) { if (st == TaskState::Paused) ++paused; };
    cb.completed = [&](const Executor::Completion&) { ++completed; };
    cb.log = [&](LogLevel, const std::string& m) { std::lock_guard lk(mu); log.push_back(m); };
    AsyncExecutor ax(prog, cb);
    ax.debugger().setBreakpoint("EventGraph", second, true);
    auto h = ax.postEvent("Go");
    for (int i = 0; i < 300 && paused.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK_EQ(paused.load(), 1);
    NodeId at = 0;
    CHECK(ax.inspect([&](Executor& ex) { if (auto t = ex.task(h)) at = t->currentNode(); }));
    CHECK_EQ(at, second);
    {
        std::lock_guard lk(mu);
        CHECK_EQ(log.size(), size_t(1));       // "one" was printed, "two" is held back by the breakpoint
    }
    ax.debugger().setBreakpoint("EventGraph", second, false);
    ax.resume(h);
    CHECK(ax.waitIdle(3000));
    CHECK_EQ(completed.load(), 1);
    {
        std::lock_guard lk(mu);
        CHECK_EQ(log.size(), size_t(2));
    }
    // stopping interrupts a script that waits for a long time
    AsyncExecutor::Callbacks cb2;
    TaskState endState = TaskState::Ready;
    cb2.completed = [&](const Executor::Completion& c) { endState = c.result.state; };
    auto prog2 = build(rt, printScript(60.0, "never"));
    {
        AsyncExecutor slow(prog2, cb2);
        slow.postEvent("Go");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto t0 = std::chrono::steady_clock::now();
        slow.stop();
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 2.0);
    }
    CHECK(endState == TaskState::Aborted);
}

// ---- compatibility, standard application, launch files ---------------------------------------------------------
static Script scriptWithHostEvent(const char* event) {
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode(std::string("event:") + event).id;
    NodeId pr = g.addNode("util.print").id;
    link(g, ev, "Then", pr, "In");
    return s;
}

TEST(compatibility_reports_what_the_application_lacks) {
    TestRuntime rt;                                              // knows neither Tick nor any plugin
    Script s = scriptWithHostEvent("Tick");
    s.variables.push_back({"Hero", "Ghost", Value(), "", ""});
    s.graphs[0].addNode("fn:Nope.Missing");
    auto rep = checkCompatibility(s, rt.registry());
    CHECK(!rep.ok());
    CHECK_EQ(rep.missingEvents().size(), size_t(1));
    CHECK_EQ(rep.missingEvents()[0], std::string("Tick"));
    bool node = false, type = false;
    for (auto& i : rep.issues) {
        node |= i.kind == CompatibilityIssue::Kind::MissingNode;
        type |= i.kind == CompatibilityIssue::Kind::UnknownType;
    }
    CHECK(node);
    CHECK(type);
    CHECK(rep.toText().find("Tick") != std::string::npos);
    // an application that provides the event accepts the script (the other problems stay)
    Runtime other;
    other.registry().addEvent("Tick", {{"DeltaTime", "float"}});
    CHECK(checkCompatibility(scriptWithHostEvent("Tick"), other.registry()).ok());
    // compiling reports the missing event too
    auto prog = rt.compile(scriptWithHostEvent("Tick"));
    CHECK(!prog->ok());
    bool mentions = false;
    for (auto& d : prog->diagnostics) mentions |= d.message.find("does not provide the event 'Tick'") != std::string::npos;
    CHECK(mentions);
}

TEST(changed_event_signature_is_detected) {
    Runtime a;
    a.registry().addEvent("Hit", {{"Damage", "int"}});
    Script s = scriptWithHostEvent("Hit");
    stampHostEvents(s, a.registry());
    CHECK_EQ(s.graphs[0].nodes[0].prop("sig"), std::string("(Damage: int)"));
    CHECK(checkCompatibility(s, a.registry()).ok());
    CHECK(a.compile(s)->ok());
    // the signature survives saving
    Script loaded = Script::parse(s.toText());
    CHECK_EQ(loaded.graphs[0].nodes[0].prop("sig"), std::string("(Damage: int)"));
    // another application raises Hit with different arguments
    Runtime b;
    b.registry().addEvent("Hit", {{"Damage", "float"}, {"Source", "string"}});
    auto rep = checkCompatibility(loaded, b.registry());
    CHECK(!rep.ok());
    CHECK(rep.issues[0].kind == CompatibilityIssue::Kind::EventSignatureChanged);
    CHECK(!b.compile(loaded)->ok());
    // scripts without a recorded signature are accepted as before
    CHECK(checkCompatibility(scriptWithHostEvent("Hit"), b.registry()).ok());
}

TEST(standard_app_runs_main_with_arguments_and_exit_code) {
    TestRuntime rt;
    standard::registerStandardApp(rt.registry());
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Main").id;
    NodeId each = g.addNode("flow.foreach").id;
    NodeId pr = g.addNode("util.print").id;
    NodeId ex = g.addNode("std.exit").id;
    g.findNode(ex)->defaults["Exit Code"] = Value(3);
    link(g, ev, "Then", each, "In");
    link(g, ev, "Args", each, "Array");
    link(g, each, "Loop Body", pr, "In");
    link(g, each, "Element", pr, "String");
    link(g, each, "Completed", ex, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    std::vector<std::string> log;
    inst.setLogHandler([&](LogLevel, const std::string& m) { log.push_back(m); });
    auto out = standard::runMain(inst, {"alpha", "beta"});
    CHECK(out.ok);
    CHECK(out.exited);
    CHECK_EQ(out.exitCode, 3);
    CHECK_EQ(log.size(), size_t(2));
    CHECK_EQ(log.empty() ? std::string() : log[0], std::string("alpha"));
    // a script without Main cannot be run as a standard application
    Script empty;
    empty.addEventGraph().addNode("event:Other");
    Instance inst2(build(rt, empty));
    auto bad = standard::runMain(inst2, {});
    CHECK(!bad.ok);
    CHECK_EQ(bad.exitCode, 1);
}

TEST(standard_app_runtime_error_gives_exit_code_one) {
    TestRuntime rt;
    standard::registerStandardApp(rt.registry());
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Main").id;
    NodeId div = g.addNode("math.div").id;
    g.findNode(div)->defaults["B"] = Value(0);
    NodeId pr = g.addNode("util.print").id;
    link(g, ev, "Then", pr, "In");
    link(g, div, "Result", pr, "String");
    Instance inst(build(rt, s));
    inst.setLogHandler([](LogLevel, const std::string&) {});
    auto out = standard::runMain(inst, {});
    CHECK(!out.ok);
    CHECK_EQ(out.exitCode, 1);
    CHECK(!out.error.empty());
}

TEST(standard_app_main_really_sleeps_for_delays) {
    TestRuntime rt;
    standard::registerStandardApp(rt.registry());
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Main").id;
    NodeId d = g.addNode("flow.delay").id;
    g.findNode(d)->defaults["Duration"] = Value(0.12);
    NodeId pr = g.addNode("util.print").id;
    link(g, ev, "Then", d, "In");
    link(g, d, "Then", pr, "In");
    Instance inst(build(rt, s));
    int lines = 0;
    inst.setLogHandler([&](LogLevel, const std::string&) { ++lines; });
    auto t0 = std::chrono::steady_clock::now();
    auto out = standard::runMain(inst, {});
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(out.ok);
    CHECK_EQ(lines, 1);
    CHECK(took >= 0.1 && took < 1.0);
}

TEST(launch_file_roundtrip_and_defaults) {
    std::string path = standard::LaunchFile::pathFor("some/dir/script.flow");
    CHECK(path.find("script.flowlaunch") != std::string::npos);
    auto missing = standard::LaunchFile::load("definitely/not/there.flowlaunch");
    CHECK_EQ(missing.configs.size(), size_t(1));          // a missing file yields one default configuration
    CHECK_EQ(missing.current().name, std::string("Default"));
    standard::LaunchFile f;
    standard::LaunchConfig c;
    c.name = "Verbose";
    c.args = {"-v", "input.txt"};
    c.cwd = "work";
    c.env["MODE"] = "debug";
    c.pluginPaths = {"plugins"};
    c.stepDelayMs = 250;
    f.configs.push_back(c);
    f.configs.push_back({});
    f.selected = "Verbose";
    std::string tmp = (std::filesystem::temp_directory_path() / "fg_test.flowlaunch").string();
    f.save(tmp);
    auto g = standard::LaunchFile::load(tmp);
    std::filesystem::remove(tmp);
    CHECK_EQ(g.configs.size(), size_t(2));
    CHECK_EQ(g.current().name, std::string("Verbose"));
    CHECK_EQ(g.current().args.size(), size_t(2));
    CHECK_EQ(g.current().env["MODE"], std::string("debug"));
    CHECK_EQ(g.current().stepDelayMs, 250);
    CHECK_EQ(g.current().pluginPaths[0], std::string("plugins"));
}

TEST(runtime_load_script_reports_problems) {
    TestRuntime rt;
    auto missing = rt.loadScript("no/such/file.flow");
    CHECK(!missing.ok());
    CHECK(!missing.error.empty());
    std::string path = (std::filesystem::temp_directory_path() / "fg_load_test.flow").string();
    scriptWithHostEvent("Tick").save(path);
    auto lacking = rt.loadScript(path);
    CHECK(lacking.error.empty());
    CHECK(!lacking.ok());
    CHECK(lacking.explanation().find("Tick") != std::string::npos);
    rt.registry().addEvent("Tick", {});
    CHECK(rt.loadScript(path).ok());
    std::filesystem::remove(path);
}

TEST(plugin_path_lists_load_each_plugin_once) {
    std::string dir = g_exeDir + "/plugins";
    if (!std::filesystem::exists(dir)) { std::printf("  (skipped: no plugins directory)\n"); return; }
    TestRuntime rt;
#if defined(_WIN32)
    std::string list = dir + ";" + dir + ";" + "does/not/exist";
#else
    std::string list = dir + ":" + dir + ":" + "does/not/exist";
#endif
    std::vector<std::string> errors;
    rt.loadPluginsFromPathList(list, &errors);
    CHECK(errors.empty());
    CHECK(rt.registry().findType("Counter") != nullptr);
    CHECK_EQ(rt.loadedPlugins().size(), size_t(1));
}

TEST(plugins_built_for_another_abi_are_refused) {
    const std::string legacy = g_exeDir + "/testplugins/AbiPlugin_legacy" + kLibExt, mismatch = g_exeDir + "/testplugins/AbiPlugin_mismatch" + kLibExt;
    if (!std::filesystem::exists(legacy) || !std::filesystem::exists(mismatch)) { std::printf("  (skipped: test plugins not built)\n"); return; }
    CHECK(pluginAbiMismatch(currentPluginAbi(), currentPluginAbi()).empty());

    TestRuntime rt;
    std::string err;
    CHECK(!rt.loadPlugin(legacy, &err));
    CHECK(err.find("older FlowGraph") != std::string::npos);
    CHECK(!rt.loadPlugin(mismatch, &err));
    CHECK(err.find("plugin ABI version") != std::string::npos);
    // Their registration functions never ran.
    CHECK(rt.registry().findEvent("LegacyPluginRegistered") == nullptr);
    CHECK(rt.registry().findEvent("MismatchPluginRegistered") == nullptr);
    CHECK(rt.loadedPlugins().empty());
    // A failed library is not remembered as loaded: trying again reports the reason again.
    err.clear();
    CHECK(!rt.loadPlugin(legacy, &err));
    CHECK(!err.empty());

    PluginAbi other = currentPluginAbi();
    other.flavour ^= 1u;
    CHECK(pluginAbiMismatch(other, currentPluginAbi()).find("Debug and Release") != std::string::npos);
    other = currentPluginAbi();
    other.layout ^= 1u;
    CHECK(pluginAbiMismatch(other, currentPluginAbi()).find("other FlowGraph headers") != std::string::npos);
}

// ---- interpreter fast paths ---------------------------------------------------------------------------
// The interpreter evaluates math, comparisons, branches, loops, variables and the array / map nodes inline. These tests pin down
// that the results, the error messages and the debugger's view are exactly what the node implementations define.
static NodeId fpNode(Graph& g, const char* def) { return g.addNode(def).id; }
static NodeId fpNode(Graph& g, const char* def, const char* pin, Value v) {
    Node& n = g.addNode(def);
    n.defaults[pin] = std::move(v);
    return n.id;
}
static NodeId fpVar(Graph& g, const char* kind, const char* name, const char* scope = "global") {
    Node& n = g.addNode(kind);
    n.props = {{"name", name}, {"scope", scope}};
    return n.id;
}
static NodeId fpCall(Graph& g, const char* fn) {
    Node& n = g.addNode("func.call");
    n.props["function"] = fn;
    return n.id;
}
static void fpDefault(Graph& g, NodeId n, const char* pin, Value v) { g.findNode(n)->defaults[pin] = std::move(v); }
static NodeId fpLoop(Graph& g, NodeId prev, const char* prevPin, int64_t first, int64_t last) {
    NodeId l = fpNode(g, "flow.for");
    fpDefault(g, l, "First", Value(first));
    fpDefault(g, l, "Last", Value(last));
    link(g, prev, prevPin, l, "In");
    return l;
}
// var = var + src.pin, executed after execFrom.execPin
static NodeId fpAccumulate(Graph& g, const char* var, NodeId src, const char* pin, NodeId execFrom, const char* execPin) {
    NodeId get = fpVar(g, "var.get", var), sum = fpNode(g, "math.add"), set = fpVar(g, "var.set", var);
    link(g, get, "Value", sum, "A");
    link(g, src, pin, sum, "B");
    link(g, sum, "Result", set, "Value");
    link(g, execFrom, execPin, set, "In");
    return set;
}

TEST(fast_paths_nested_loops_and_break) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"acc", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId outer = fpLoop(g, ev, "Then", 0, 5);
    NodeId inner = fpLoop(g, outer, "Loop Body", 0, 10);
    NodeId eq = fpNode(g, "cmp.eq", "B", Value(3)), br = fpNode(g, "flow.branch"), brk = fpNode(g, "flow.break");
    link(g, inner, "Index", eq, "A");
    link(g, eq, "Result", br, "Condition");
    link(g, inner, "Loop Body", br, "In");
    link(g, br, "True", brk, "In");
    NodeId mul = fpNode(g, "math.mul", "B", Value(10)), idx = fpNode(g, "math.add");
    link(g, outer, "Index", mul, "A");
    link(g, mul, "Result", idx, "A");
    link(g, inner, "Index", idx, "B");
    fpAccumulate(g, "acc", idx, "Result", br, "False");
    // after the outer loop: acc = acc * 2
    NodeId done = fpVar(g, "var.set", "acc"), get = fpVar(g, "var.get", "acc"), dbl = fpNode(g, "math.mul", "B", Value(2));
    link(g, get, "Value", dbl, "A");
    link(g, dbl, "Result", done, "Value");
    link(g, outer, "Completed", done, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("acc").toInt(), int64_t(630));      // 2 * sum over i < 5 of (30 i + 3)
}

TEST(fast_paths_while_and_for_each_with_break) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"i", "int", Value(0), "", ""});
    s.variables.push_back({"acc", "int", Value(0), "", ""});
    s.variables.push_back({"arr", "array<int>", Value(), "", ""});
    s.variables.push_back({"seen", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    // while (i < 100) { i = i + 1; if (i >= 7) break; acc += i } ; then acc += 1000
    NodeId wh = fpNode(g, "flow.while");
    link(g, ev, "Then", wh, "In");
    NodeId geti = fpVar(g, "var.get", "i"), lt = fpNode(g, "cmp.lt", "B", Value(100));
    link(g, geti, "Value", lt, "A");
    link(g, lt, "Result", wh, "Condition");
    NodeId geti2 = fpVar(g, "var.get", "i"), inc = fpNode(g, "math.add", "B", Value(1)), seti = fpVar(g, "var.set", "i");
    link(g, geti2, "Value", inc, "A");
    link(g, inc, "Result", seti, "Value");
    link(g, wh, "Loop Body", seti, "In");
    NodeId geti3 = fpVar(g, "var.get", "i"), ge = fpNode(g, "cmp.ge", "B", Value(7)), br = fpNode(g, "flow.branch"), brk = fpNode(g, "flow.break");
    link(g, geti3, "Value", ge, "A");
    link(g, ge, "Result", br, "Condition");
    link(g, seti, "Then", br, "In");
    link(g, br, "True", brk, "In");
    NodeId geti4 = fpVar(g, "var.get", "i");
    fpAccumulate(g, "acc", geti4, "Value", br, "False");
    NodeId thousand = fpNode(g, "math.add", "A", Value(1000));
    NodeId after = fpAccumulate(g, "acc", thousand, "Result", wh, "Completed");
    // arr = squares of 0..5; for each element: if (e > 10) break; seen += e
    NodeId fill = fpLoop(g, after, "Then", 0, 6);
    NodeId garr = fpVar(g, "var.get", "arr"), push = fpNode(g, "array.add"), sq = fpNode(g, "math.mul");
    link(g, garr, "Value", push, "Array");
    link(g, fill, "Index", sq, "A");
    link(g, fill, "Index", sq, "B");
    link(g, sq, "Result", push, "Item");
    link(g, fill, "Loop Body", push, "In");
    NodeId each = fpNode(g, "flow.foreach"), garr2 = fpVar(g, "var.get", "arr");
    link(g, fill, "Completed", each, "In");
    link(g, garr2, "Value", each, "Array");
    NodeId gt = fpNode(g, "cmp.gt", "B", Value(10)), br2 = fpNode(g, "flow.branch"), brk2 = fpNode(g, "flow.break");
    link(g, each, "Element", gt, "A");
    link(g, gt, "Result", br2, "Condition");
    link(g, each, "Loop Body", br2, "In");
    link(g, br2, "True", brk2, "In");
    fpAccumulate(g, "seen", each, "Element", br2, "False");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("i").toInt(), int64_t(7));
    CHECK_EQ(inst.getVariable("acc").toInt(), int64_t(21 + 1000));  // i = 1..6 were added, Break continues with Completed
    CHECK_EQ(inst.getVariable("arr").toString(), std::string("[0, 1, 4, 9, 16, 25]"));
    CHECK_EQ(inst.getVariable("seen").toInt(), int64_t(14));       // 0 + 1 + 4 + 9, stopped at 16
}

TEST(fast_paths_loop_bound_is_read_every_iteration) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"limit", "int", Value(3), "", ""});
    s.variables.push_back({"acc", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId l = fpNode(g, "flow.for");
    fpDefault(g, l, "First", Value(0));
    NodeId lim = fpVar(g, "var.get", "limit");
    link(g, lim, "Value", l, "Last");
    link(g, ev, "Then", l, "In");
    // body: while limit < 8 { limit += 1 }; acc += Index   (the loop keeps growing until limit reaches 8)
    NodeId getl = fpVar(g, "var.get", "limit"), lt = fpNode(g, "cmp.lt", "B", Value(8)), br = fpNode(g, "flow.branch");
    link(g, getl, "Value", lt, "A");
    link(g, lt, "Result", br, "Condition");
    link(g, l, "Loop Body", br, "In");
    NodeId getl2 = fpVar(g, "var.get", "limit"), inc = fpNode(g, "math.add", "B", Value(1)), setl = fpVar(g, "var.set", "limit");
    link(g, getl2, "Value", inc, "A");
    link(g, inc, "Result", setl, "Value");
    link(g, br, "True", setl, "In");
    NodeId a = fpAccumulate(g, "acc", l, "Index", setl, "Then");
    link(g, br, "False", a, "In");
    auto prog = build(rt, s);
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("limit").toInt(), int64_t(8));
    CHECK_EQ(inst.getVariable("acc").toInt(), int64_t(28));        // Index 0..7
}

TEST(fast_paths_sequence_and_delay_resume_loops) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"acc", "int", Value(0), "", ""});
    s.variables.push_back({"slow", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId seq = fpNode(g, "flow.sequence");
    g.findNode(seq)->props["count"] = "4";
    link(g, ev, "Then", seq, "In");
    for (int k = 0; k < 3; ++k) {
        NodeId l = fpLoop(g, seq, ("Then " + std::to_string(k)).c_str(), 0, 3 + k);
        fpAccumulate(g, "acc", l, "Index", l, "Loop Body");
    }
    // a loop whose body waits: the loop continues after each tick
    NodeId l = fpLoop(g, seq, "Then 3", 0, 3);
    NodeId d = fpNode(g, "flow.delay", "Duration", Value(0.0));
    link(g, l, "Loop Body", d, "In");
    fpAccumulate(g, "slow", l, "Index", d, "Then");
    auto prog = build(rt, s);
    Instance inst(prog);
    auto r = inst.callEvent("Go");
    CHECK(r.waiting());
    for (int k = 0; k < 10 && inst.pendingTaskCount() > 0; ++k) inst.tick(0.5);
    CHECK_EQ(inst.pendingTaskCount(), size_t(0));
    CHECK_EQ(inst.getVariable("acc").toInt(), int64_t(3 + 6 + 10));
    CHECK_EQ(inst.getVariable("slow").toInt(), int64_t(0 + 1 + 2));
}

TEST(fast_paths_numbers_and_conversions) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"f", "float", Value(0.0), "", ""});
    s.variables.push_back({"i", "int", Value(0), "", ""});
    s.variables.push_back({"b", "bool", Value(false), "", ""});
    s.variables.push_back({"t", "string", Value(""), "", ""});
    s.variables.push_back({"sa", "string", Value("apple"), "", ""});
    s.variables.push_back({"sb", "string", Value("banana"), "", ""});
    s.variables.push_back({"fl", "float", Value(2.5), "", ""});
    s.variables.push_back({"n", "int", Value(9), "", ""});
    s.variables.push_back({"mix", "float", Value(0.0), "", ""});
    s.variables.push_back({"root", "float", Value(0.0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    // f = 7 / 2 + 0.5 * 3  (connected to a float Add, the Divide becomes a float division: 3.5 + 1.5)
    NodeId d = fpNode(g, "math.div", "A", Value(7)), half = fpNode(g, "math.mul", "A", Value(0.5)), fsum = fpNode(g, "math.add");
    fpDefault(g, d, "B", Value(2));
    fpDefault(g, half, "B", Value(3));
    link(g, d, "Result", fsum, "A");
    link(g, half, "Result", fsum, "B");
    NodeId setf = fpVar(g, "var.set", "f");
    link(g, fsum, "Result", setf, "Value");
    link(g, ev, "Then", setf, "In");
    // i = 7 mod 4 + min(3, 9)
    NodeId m = fpNode(g, "math.mod", "A", Value(7)), mn = fpNode(g, "math.min", "A", Value(3)), isum = fpNode(g, "math.add");
    fpDefault(g, m, "B", Value(4));
    fpDefault(g, mn, "B", Value(9));
    link(g, m, "Result", isum, "A");
    link(g, mn, "Result", isum, "B");
    NodeId seti = fpVar(g, "var.set", "i");
    link(g, isum, "Result", seti, "Value");
    link(g, setf, "Then", seti, "In");
    // b = (f > i) xor (i == 6.0)
    NodeId getf = fpVar(g, "var.get", "f"), geti = fpVar(g, "var.get", "i");
    NodeId gt = fpNode(g, "cmp.gt"), eq = fpNode(g, "cmp.eq", "B", Value(6.0)), xr = fpNode(g, "logic.xor");
    link(g, getf, "Value", gt, "A");
    link(g, geti, "Value", gt, "B");
    link(g, geti, "Value", eq, "A");
    link(g, gt, "Result", xr, "A");
    link(g, eq, "Result", xr, "B");
    NodeId setb = fpVar(g, "var.set", "b");
    link(g, xr, "Result", setb, "Value");
    link(g, seti, "Then", setb, "In");
    // t = sa < sb ? "yes" : "no"  (strings compare as strings)
    NodeId lt = fpNode(g, "cmp.lt"), sel = fpNode(g, "flow.select"), gsa = fpVar(g, "var.get", "sa"), gsb = fpVar(g, "var.get", "sb");
    link(g, gsa, "Value", lt, "A");
    link(g, gsb, "Value", lt, "B");
    fpDefault(g, sel, "A", Value("yes"));
    fpDefault(g, sel, "B", Value("no"));
    link(g, lt, "Result", sel, "Condition");
    NodeId sett = fpVar(g, "var.set", "t");
    link(g, sel, "Result", sett, "Value");
    link(g, setb, "Then", sett, "In");
    // a float variable plus an int variable (the int input is converted), and Sqrt of an int
    NodeId gfl = fpVar(g, "var.get", "fl"), gn = fpVar(g, "var.get", "n"), sum = fpNode(g, "math.add");
    link(g, gfl, "Value", sum, "A");
    link(g, gn, "Value", sum, "B");
    NodeId setm = fpVar(g, "var.set", "mix");
    link(g, sum, "Result", setm, "Value");
    link(g, sett, "Then", setm, "In");
    NodeId gn2 = fpVar(g, "var.get", "n"), sq = fpNode(g, "fn:Math.Sqrt");
    link(g, gn2, "Value", sq, "X");
    NodeId setr = fpVar(g, "var.set", "root");
    link(g, sq, "Result", setr, "Value");
    link(g, setm, "Then", setr, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("f").toFloat(), 5.0);
    CHECK_EQ(inst.getVariable("i").toInt(), int64_t(6));
    CHECK_EQ(inst.getVariable("b").toBool(), true);               // (5.0 > 6) is false, (6 == 6.0) is true
    CHECK_EQ(inst.getVariable("t").toString(), std::string("yes"));
    CHECK_EQ(inst.getVariable("mix").toFloat(), 11.5);
    CHECK_EQ(inst.getVariable("root").toFloat(), 3.0);
}

TEST(fast_paths_integer_division_and_modulo) {
    TestRuntime rt;
    Script s;
    {
        Graph& f = s.addFunction("Calc", {{"A", "int", false, false, Value()}, {"B", "int", false, false, Value()}},
                                 {{"Q", "int", false, false, Value()}, {"R", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
        NodeId dv = fpNode(f, "math.div"), md = fpNode(f, "math.mod");
        link(f, entry, "A", dv, "A");
        link(f, entry, "B", dv, "B");
        link(f, entry, "A", md, "A");
        link(f, entry, "B", md, "B");
        link(f, dv, "Result", ret, "Q");
        link(f, md, "Result", ret, "R");
        link(f, entry, "Then", ret, "In");
    }
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    const std::vector<std::pair<int64_t, int64_t>> cases = {
        {7, 2}, {-7, 2}, {7, -2}, {-7, -2}, {0, 5}, {5, 5}, {1, 7}, {4294967295LL, 2}, {4294967296LL, 3}, {4294967295LL, 4294967295LL},
        {(1LL << 40), 7}, {(1LL << 40) + 5, (1LL << 33)}, {123456789012345LL, 1000003}, {-(1LL << 40), 9}, {9, 4294967296LL}};
    for (auto [a, b] : cases) {
        auto r = inst.callFunction("Calc", {Value(a), Value(b)});
        CHECK(r.ok);
        CHECK_EQ(r.returns.size(), size_t(2));
        if (r.returns.size() == 2) {
            CHECK_EQ(r.returns[0].toInt(), a / b);
            CHECK_EQ(r.returns[1].toInt(), a % b);
        }
    }
}

TEST(fast_paths_errors_name_the_failing_node) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"i", "int", Value(5), "", ""});
    s.variables.push_back({"arr", "array<int>", Value(), "", ""});
    s.variables.push_back({"r", "int", Value(0), "", ""});
    {
        Graph& f = s.addFunction("Boom", {{"D", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
        NodeId dv = fpNode(f, "math.div", "A", Value(1));
        link(f, entry, "D", dv, "B");
        link(f, dv, "Result", ret, "Result");
        link(f, entry, "Then", ret, "In");
    }
    {
        Graph& f = s.addFunction("Id", {{"V", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
        link(f, entry, "V", ret, "Result");
        link(f, entry, "Then", ret, "In");
    }
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    // 1. a call whose argument fails (1 / (2 - 2)): the error belongs to the caller
    NodeId dv = fpNode(g, "math.div", "A", Value(1)), zero = fpNode(g, "math.sub", "A", Value(2));
    fpDefault(g, zero, "B", Value(2));
    link(g, zero, "Result", dv, "B");
    NodeId id = fpCall(g, "Id");
    link(g, dv, "Result", id, "V");
    link(g, ev, "Then", id, "In");
    // 2. an error inside a function
    NodeId boom = fpCall(g, "Boom");
    fpDefault(g, boom, "D", Value(0));
    link(g, id, "Then", boom, "In");
    // 3. modulo by zero
    NodeId md = fpNode(g, "math.mod", "A", Value(10)), zero2 = fpNode(g, "math.sub", "A", Value(3));
    fpDefault(g, zero2, "B", Value(3));
    link(g, zero2, "Result", md, "B");
    NodeId set = fpVar(g, "var.set", "i");
    link(g, md, "Result", set, "Value");
    link(g, boom, "Then", set, "In");
    // 4. an array index out of range
    NodeId garr = fpVar(g, "var.get", "arr"), push = fpNode(g, "array.add", "Item", Value(41));
    link(g, garr, "Value", push, "Array");
    link(g, set, "Then", push, "In");
    NodeId garr2 = fpVar(g, "var.get", "arr"), bad = fpNode(g, "array.get", "Index", Value(1));
    link(g, garr2, "Value", bad, "Array");
    NodeId setr = fpVar(g, "var.set", "r");
    link(g, bad, "Item", setr, "Value");
    link(g, push, "Then", setr, "In");

    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    inst.setLogHandler([](LogLevel, const std::string&) {});
    auto run = [&]() {
        auto r = inst.callEvent("Go");
        CHECK(!r.ok);
        CHECK_EQ(r.state, TaskState::Failed);
        return r;
    };
    auto r = run();
    CHECK_EQ(r.error, std::string("division by zero"));
    CHECK_EQ(r.errorGraph, g.name);
    CHECK_EQ(r.errorNode, dv);
    r = run();                                           // frames of the failed run are recycled
    CHECK_EQ(r.errorNode, dv);
    // let the argument evaluate, then each following error in turn
    fpDefault(g, zero, "B", Value(1));
    prog = build(rt, s);
    Instance inst2(prog);
    inst2.setLogHandler([](LogLevel, const std::string&) {});
    r = inst2.callEvent("Go");
    CHECK_EQ(r.error, std::string("division by zero"));
    CHECK_EQ(r.errorGraph, std::string("Boom"));
    fpDefault(g, boom, "D", Value(1));
    prog = build(rt, s);
    Instance inst3(prog);
    inst3.setLogHandler([](LogLevel, const std::string&) {});
    r = inst3.callEvent("Go");
    CHECK_EQ(r.error, std::string("division by zero"));
    CHECK_EQ(r.errorGraph, g.name);
    CHECK_EQ(r.errorNode, md);
    fpDefault(g, zero2, "B", Value(7));
    prog = build(rt, s);
    Instance inst4(prog);
    inst4.setLogHandler([](LogLevel, const std::string&) {});
    r = inst4.callEvent("Go");
    CHECK_EQ(r.error, std::string("index 1 is out of range (size 1)"));
    CHECK_EQ(r.errorNode, bad);
    CHECK_EQ(inst4.getVariable("arr").toString(), std::string("[41]"));
}

TEST(fast_paths_arrays_and_maps) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"arr", "array<int>", Value(), "", ""});
    s.variables.push_back({"copy", "array<int>", Value(), "", ""});
    s.variables.push_back({"m", "map<string,int>", Value(), "", ""});
    s.variables.push_back({"x", "int", Value(0), "", ""});
    s.variables.push_back({"a", "int", Value(0), "", ""});
    s.variables.push_back({"b", "int", Value(-1), "", ""});
    s.variables.push_back({"found", "bool", Value(true), "", ""});
    s.variables.push_back({"has", "bool", Value(false), "", ""});
    s.variables.push_back({"mlen", "int", Value(0), "", ""});
    s.variables.push_back({"empty", "int", Value(-1), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId prev = ev;
    for (int k = 0; k < 3; ++k) {                         // arr = [0, 10, 20]
        NodeId ga = fpVar(g, "var.get", "arr"), p = fpNode(g, "array.add", "Item", Value(k * 10));
        link(g, ga, "Value", p, "Array");
        link(g, prev, "Then", p, "In");
        prev = p;
    }
    NodeId ga = fpVar(g, "var.get", "arr"), st = fpNode(g, "array.set", "Index", Value(1));       // arr[1] = 99
    fpDefault(g, st, "Item", Value(99));
    link(g, ga, "Value", st, "Array");
    link(g, prev, "Then", st, "In");
    NodeId ga2 = fpVar(g, "var.get", "arr"), setCopy = fpVar(g, "var.set", "copy");               // copy = arr (a copy)
    link(g, ga2, "Value", setCopy, "Value");
    link(g, st, "Then", setCopy, "In");
    NodeId ga3 = fpVar(g, "var.get", "arr"), push = fpNode(g, "array.add", "Item", Value(7));      // arr.add(7): copy is unchanged
    link(g, ga3, "Value", push, "Array");
    link(g, setCopy, "Then", push, "In");
    NodeId ga4 = fpVar(g, "var.get", "arr"), get = fpNode(g, "array.get", "Index", Value(1)), len = fpNode(g, "array.length"), sum = fpNode(g, "math.add");
    link(g, ga4, "Value", get, "Array");
    link(g, ga4, "Value", len, "Array");
    link(g, get, "Item", sum, "A");
    link(g, len, "Length", sum, "B");
    NodeId setx = fpVar(g, "var.set", "x");                                                        // x = arr[1] + arr.length = 99 + 4
    link(g, sum, "Result", setx, "Value");
    link(g, push, "Then", setx, "In");
    // maps
    NodeId m1 = fpVar(g, "var.get", "m"), s1 = fpNode(g, "map.set", "Key", Value("x"));
    fpDefault(g, s1, "Value", Value(7));
    link(g, m1, "Value", s1, "Map");
    link(g, setx, "Then", s1, "In");
    NodeId m2 = fpVar(g, "var.get", "m"), s2 = fpNode(g, "map.set", "Key", Value("y"));
    fpDefault(g, s2, "Value", Value(8));
    link(g, m2, "Value", s2, "Map");
    link(g, s1, "Then", s2, "In");
    NodeId m3 = fpVar(g, "var.get", "m"), g1 = fpNode(g, "map.get", "Key", Value("x")), g2 = fpNode(g, "map.get", "Key", Value("zzz"));
    link(g, m3, "Value", g1, "Map");
    link(g, m3, "Value", g2, "Map");
    NodeId seta = fpVar(g, "var.set", "a"), setb = fpVar(g, "var.set", "b"), setf = fpVar(g, "var.set", "found");
    link(g, g1, "Value", seta, "Value");
    link(g, g2, "Value", setb, "Value");                  // missing key: the default of the value type
    link(g, g2, "Found", setf, "Value");
    link(g, s2, "Then", seta, "In");
    link(g, seta, "Then", setb, "In");
    link(g, setb, "Then", setf, "In");
    NodeId m4 = fpVar(g, "var.get", "m"), has = fpNode(g, "map.contains", "Key", Value("y")), mlen = fpNode(g, "map.length");
    link(g, m4, "Value", has, "Map");
    link(g, m4, "Value", mlen, "Map");
    NodeId seth = fpVar(g, "var.set", "has"), setl = fpVar(g, "var.set", "mlen");
    link(g, has, "Contains", seth, "Value");
    link(g, mlen, "Length", setl, "Value");
    link(g, setf, "Then", seth, "In");
    link(g, seth, "Then", setl, "In");
    // an input that is not connected uses the (empty) default container
    NodeId elen = fpNode(g, "array.length"), sete = fpVar(g, "var.set", "empty");
    link(g, elen, "Length", sete, "Value");
    link(g, setl, "Then", sete, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("arr").toString(), std::string("[0, 99, 20, 7]"));
    CHECK_EQ(inst.getVariable("copy").toString(), std::string("[0, 99, 20]"));
    CHECK_EQ(inst.getVariable("x").toInt(), int64_t(103));
    CHECK_EQ(inst.getVariable("m").toString(), std::string("{\"x\": 7, \"y\": 8}"));
    CHECK_EQ(inst.getVariable("a").toInt(), int64_t(7));
    CHECK_EQ(inst.getVariable("b").toInt(), int64_t(0));
    CHECK_EQ(inst.getVariable("found").toBool(), false);
    CHECK_EQ(inst.getVariable("has").toBool(), true);
    CHECK_EQ(inst.getVariable("mlen").toInt(), int64_t(2));
    CHECK_EQ(inst.getVariable("empty").toInt(), int64_t(0));
}

TEST(fast_paths_calls_recycle_frames) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"r", "int", Value(0), "", ""});
    s.variables.push_back({"counter", "int", Value(0), "", ""});
    s.variables.push_back({"a", "array<int>", Value(), "", ""});
    s.variables.push_back({"len", "int", Value(0), "", ""});
    {   // Fact(N) = N < 2 ? 1 : N * Fact(N - 1)
        Graph& f = s.addFunction("Fact", {{"N", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, retRec = f.nodes[1].id;
        NodeId lt = fpNode(f, "cmp.lt", "B", Value(2)), br = fpNode(f, "flow.branch"), retBase = fpNode(f, "func.return");
        fpDefault(f, retBase, "Result", Value(1));
        NodeId sub = fpNode(f, "math.sub", "B", Value(1)), c = fpCall(f, "Fact"), mul = fpNode(f, "math.mul");
        link(f, entry, "Then", br, "In");
        link(f, entry, "N", lt, "A");
        link(f, lt, "Result", br, "Condition");
        link(f, br, "True", retBase, "In");
        link(f, br, "False", c, "In");
        link(f, entry, "N", sub, "A");
        link(f, sub, "Result", c, "N");
        link(f, entry, "N", mul, "A");
        link(f, c, "Result", mul, "B");
        link(f, mul, "Result", retRec, "Result");
        link(f, c, "Then", retRec, "In");
    }
    {   // Bump(ref Count, const Step) -> Twice: Count += Step; the local doubles from 5 every call
        Graph& f = s.addFunction("Bump", {{"Count", "int", true, false, Value()}, {"Step", "int", false, true, Value()}}, {{"Twice", "int", false, false, Value()}});
        f.locals.push_back({"tmp", "int", Value(5), "", ""});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
        NodeId gc = fpVar(f, "var.get", "Count", "param"), gs = fpVar(f, "var.get", "Step", "param"), sum = fpNode(f, "math.add");
        link(f, gc, "Value", sum, "A");
        link(f, gs, "Value", sum, "B");
        NodeId setc = fpVar(f, "var.set", "Count", "param");
        link(f, sum, "Result", setc, "Value");
        link(f, entry, "Then", setc, "In");
        NodeId gt = fpVar(f, "var.get", "tmp", "local"), dbl = fpNode(f, "math.mul", "B", Value(2)), sett = fpVar(f, "var.set", "tmp", "local");
        link(f, gt, "Value", dbl, "A");
        link(f, dbl, "Result", sett, "Value");
        link(f, setc, "Then", sett, "In");
        NodeId gt2 = fpVar(f, "var.get", "tmp", "local");
        link(f, gt2, "Value", ret, "Twice");
        link(f, sett, "Then", ret, "In");
    }
    {   // Grow(A: array) -> Len: the array is passed by value
        Graph& f = s.addFunction("Grow", {{"A", "array<int>", false, false, Value()}}, {{"Len", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
        NodeId ga = fpVar(f, "var.get", "A", "param"), p = fpNode(f, "array.add", "Item", Value(5)), ga2 = fpVar(f, "var.get", "A", "param"), ln = fpNode(f, "array.length");
        link(f, ga, "Value", p, "Array");
        link(f, entry, "Then", p, "In");
        link(f, ga2, "Value", ln, "Array");
        link(f, ln, "Length", ret, "Len");
        link(f, p, "Then", ret, "In");
    }
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId c1 = fpCall(g, "Fact");
    fpDefault(g, c1, "N", Value(10));
    link(g, ev, "Then", c1, "In");
    NodeId setr = fpVar(g, "var.set", "r");
    link(g, c1, "Result", setr, "Value");
    link(g, c1, "Then", setr, "In");
    NodeId prev = setr;
    for (int k = 0; k < 3; ++k) {
        NodeId b = fpCall(g, "Bump");
        NodeId gv = fpVar(g, "var.get", "counter");
        link(g, gv, "Value", b, "Count");
        fpDefault(g, b, "Step", Value(k + 1));
        link(g, prev, "Then", b, "In");
        prev = b;
    }
    NodeId setc = fpVar(g, "var.set", "counter");
    link(g, prev, "Twice", setc, "Value");
    link(g, prev, "Then", setc, "In");
    NodeId ga = fpVar(g, "var.get", "a"), grow = fpCall(g, "Grow"), grow2 = fpCall(g, "Grow");
    link(g, ga, "Value", grow, "A");
    link(g, ga, "Value", grow2, "A");
    link(g, setc, "Then", grow, "In");
    link(g, grow, "Then", grow2, "In");
    NodeId setl = fpVar(g, "var.set", "len");
    link(g, grow2, "Len", setl, "Value");
    link(g, grow2, "Then", setl, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    for (int round = 0; round < 3; ++round) {             // the second and third run use recycled frames
        inst.resetVariables();
        CHECK(inst.callEvent("Go").ok);
        CHECK_EQ(inst.getVariable("r").toInt(), int64_t(3628800));
        CHECK_EQ(inst.getVariable("counter").toInt(), int64_t(10));    // 0 + 1 + 2 + 3 = 6 went through the reference, then Twice = 10
        CHECK_EQ(inst.getVariable("a").toString(), std::string("[]"));
        CHECK_EQ(inst.getVariable("len").toInt(), int64_t(1));
    }
    // host calls, many times
    int64_t acc = 0;
    for (int64_t i = 1; i <= 200; ++i) {
        auto r = inst.callFunction("Fact", {Value(i % 12)});
        CHECK(r.ok);
        int64_t want = 1;
        for (int64_t k = 2; k <= i % 12; ++k) want *= k;
        CHECK_EQ(r.returns.at(0).toInt(), want);
        acc += want;
    }
    CHECK(acc > 0);
}

TEST(fast_paths_strings_are_used_in_place) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"t", "string", Value(""), "", ""});
    s.variables.push_back({"u", "string", Value(""), "", ""});
    s.variables.push_back({"len", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId l = fpLoop(g, ev, "Then", 0, 4);
    NodeId gt = fpVar(g, "var.get", "t"), cat = fpNode(g, "fn:String.Concat"), cat2 = fpNode(g, "fn:String.Concat", "B", Value("-")), set = fpVar(g, "var.set", "t");
    link(g, gt, "Value", cat, "A");
    link(g, l, "Index", cat, "B");                         // int -> string conversion on the way in
    link(g, cat, "Result", cat2, "A");
    link(g, cat2, "Result", set, "Value");
    link(g, l, "Loop Body", set, "In");
    NodeId gt2 = fpVar(g, "var.get", "t"), ln = fpNode(g, "fn:String.Length"), setl = fpVar(g, "var.set", "len");
    link(g, gt2, "Value", ln, "String");
    link(g, ln, "Result", setl, "Value");
    link(g, l, "Completed", setl, "In");
    NodeId up = fpNode(g, "fn:String.To Upper", "String", Value("hello")), cat3 = fpNode(g, "fn:String.Concat");
    link(g, up, "Result", cat3, "A");
    fpDefault(g, cat3, "B", Value(42));                    // default literal converted at compile time
    NodeId setu = fpVar(g, "var.set", "u");
    link(g, cat3, "Result", setu, "Value");
    link(g, setl, "Then", setu, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("t").toString(), std::string("0-1-2-3-"));
    CHECK_EQ(inst.getVariable("len").toInt(), int64_t(8));
    CHECK_EQ(inst.getVariable("u").toString(), std::string("HELLO42"));
}

TEST(fast_paths_debugger_sees_the_values_of_pure_nodes) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"acc", "int", Value(0), "", ""});
    s.variables.push_back({"mark", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId l = fpLoop(g, ev, "Then", 0, 3);
    NodeId get = fpVar(g, "var.get", "acc"), sum = fpNode(g, "math.add"), set = fpVar(g, "var.set", "acc"), tail = fpVar(g, "var.set", "mark");
    link(g, get, "Value", sum, "A");
    link(g, l, "Index", sum, "B");
    link(g, sum, "Result", set, "Value");
    link(g, l, "Loop Body", set, "In");
    link(g, l, "Index", tail, "Value");
    link(g, set, "Then", tail, "In");
    auto prog = build(rt, s);
    Instance inst(prog);
    Debugger dbg;
    std::vector<std::string> links;
    dbg.linkFired = [&](const std::string&, NodeId from, int out, NodeId to) { links.push_back(std::to_string(from) + "." + std::to_string(out) + "->" + std::to_string(to)); };
    inst.setDebugHooks(&dbg);
    // without a breakpoint the debugger stays out of the way
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("acc").toInt(), int64_t(3));
    CHECK_EQ(links.size(), size_t(1 + 3 + 3));                  // Go -> loop, loop -> body (3x), Set -> Set mark (3x)
    // stop after the Set node of the third iteration: the pure nodes it evaluated show their values
    inst.resetVariables();
    dbg.setBreakpoint(g.name, tail, true);
    auto r = inst.callEvent("Go");
    CHECK(r.paused());
    r = r.task->run();                                    // second stop (Index = 1)
    CHECK(r.paused());
    r = r.task->run();                                    // third stop (Index = 2); acc was 1 before the Set node ran
    CHECK(r.paused());
    CHECK_EQ(r.task->currentNode(), tail);
    Value v;
    CHECK(r.task->pinValue(0, get, "Value", v));
    CHECK_EQ(v.toInt(), int64_t(1));                       // the variable as the Set node read it
    CHECK(r.task->pinValue(0, sum, "Result", v));
    CHECK_EQ(v.toInt(), int64_t(3));
    CHECK(r.task->pinValue(0, sum, "B", v));
    CHECK_EQ(v.toInt(), int64_t(2));
    CHECK(r.task->pinValue(0, l, "Index", v));
    CHECK_EQ(v.toInt(), int64_t(2));
    dbg.setBreakpoint(g.name, tail, false);
    r = r.task->run();
    CHECK(r.ok);
    CHECK_EQ(inst.getVariable("acc").toInt(), int64_t(3));
    // breakpoints and steps still work after a run in which nothing was armed
    dbg.clearBreakpoints();
    dbg.setBreakpoint(g.name, set, true);
    inst.resetVariables();
    r = inst.callEvent("Go");
    CHECK(r.paused());
    CHECK_EQ(r.task->currentNode(), set);
    r.task->abort();
}

TEST(fast_paths_step_limit_and_stack_overflow_are_reported) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"i", "int", Value(0), "", ""});
    s.variables.push_back({"r", "int", Value(0), "", ""});
    {
        Graph& f = s.addFunction("Down", {{"N", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
        NodeId c = fpCall(f, "Down"), add1 = fpNode(f, "math.add", "B", Value(1));
        link(f, entry, "N", add1, "A");
        link(f, add1, "Result", c, "N");
        link(f, entry, "Then", c, "In");
        link(f, c, "Result", ret, "Result");
        link(f, c, "Then", ret, "In");
    }
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId wh = fpNode(g, "flow.while", "Condition", Value(true));
    link(g, ev, "Then", wh, "In");
    NodeId get = fpVar(g, "var.get", "i"), inc = fpNode(g, "math.add", "B", Value(1)), set = fpVar(g, "var.set", "i");
    link(g, get, "Value", inc, "A");
    link(g, inc, "Result", set, "Value");
    link(g, wh, "Loop Body", set, "In");
    Graph& g2 = s.addEventGraph("Second");
    NodeId ev2 = fpNode(g2, "event:Run");
    NodeId c = fpCall(g2, "Down");
    link(g2, ev2, "Then", c, "In");
    NodeId setr = fpVar(g2, "var.set", "r");
    link(g2, c, "Result", setr, "Value");
    link(g2, c, "Then", setr, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    inst.setLogHandler([](LogLevel, const std::string&) {});
    inst.setStepLimit(10000);
    auto r = inst.callEvent("Go");
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("Step limit exceeded (infinite loop?)"));
    CHECK_EQ(r.errorNode, set);
    auto r2 = inst.callEvent("Run");
    CHECK(!r2.ok);
    CHECK_EQ(r2.error, std::string("call stack overflow (recursion too deep?)"));
    CHECK_EQ(r2.errorGraph, std::string("Down"));
}

// ---- inspection: paging and watch expressions ------------------------------------------------------------
// A script that fills an array (and a map) with `count` entries; Main raises "Go".
static std::shared_ptr<Program> bigContainers(TestRuntime& rt, int64_t count) {
    Script s;
    s.variables.push_back({"Big", "array<int>", Value(), "", ""});
    s.variables.push_back({"Names", "map<string,int>", Value(), "", ""});
    s.variables.push_back({"Nested", "array<array<int>>", Value(), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId l = fpLoop(g, ev, "Then", 0, count);
    NodeId garr = fpVar(g, "var.get", "Big"), push = fpNode(g, "array.add"), sq = fpNode(g, "math.mul", "B", Value(2));
    link(g, garr, "Value", push, "Array");
    link(g, l, "Index", sq, "A");
    link(g, sq, "Result", push, "Item");
    link(g, l, "Loop Body", push, "In");
    NodeId gm = fpVar(g, "var.get", "Names"), set = fpNode(g, "map.set");
    NodeId key = fpNode(g, "fn:String.Concat", "A", Value("k"));
    link(g, l, "Index", key, "B");
    link(g, gm, "Value", set, "Map");
    link(g, key, "Result", set, "Key");
    link(g, l, "Index", set, "Value");
    link(g, push, "Then", set, "In");
    return build(rt, s);
}

TEST(inspect_collects_a_window_and_addresses_every_value) {
    TestRuntime rt;
    auto prog = bigContainers(rt, 1000);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    auto globals = inspectGlobals(inst);
    const ValueView& big = globals[0].value;
    CHECK_EQ(big.name, std::string("Big"));
    CHECK_EQ(big.total, size_t(1000));
    CHECK_EQ(big.children.size(), size_t(64));                  // only the first 64 are collected ...
    CHECK(big.truncated);
    CHECK_EQ(big.path, std::string("Big"));                     // ... and every value knows how to be addressed
    CHECK_EQ(big.children[3].path, std::string("Big[3]"));
    CHECK_EQ(big.children[3].text, std::string("6"));
    const ValueView& names = globals[1].value;
    CHECK_EQ(names.total, size_t(1000));
    CHECK_EQ(names.children.size(), size_t(64));
    CHECK_EQ(names.children[0].path, std::string("Names[\"k0\"]"));
    // a container below the depth limit knows how many children it has, but has none yet
    InspectOptions shallow;
    shallow.maxDepth = 0;
    auto flat = inspectGlobals(inst, shallow);
    CHECK_EQ(flat[0].value.children.size(), size_t(0));
    CHECK_EQ(flat[0].value.total, size_t(1000));
}

TEST(evaluate_expression_pages_through_big_containers) {
    TestRuntime rt;
    auto prog = bigContainers(rt, 1000);
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    auto eval = [&](const std::string& e) { return evaluateExpression(inst, nullptr, 0, e); };

    auto r = eval("Big[64..127]");                               // the next window (inclusive)
    CHECK(r.ok);
    CHECK_EQ(r.view.children.size(), size_t(64));
    CHECK_EQ(r.view.offset, size_t(64));
    CHECK_EQ(r.view.total, size_t(1000));
    CHECK_EQ(r.view.path, std::string("Big"));
    CHECK_EQ(r.view.children.front().name, std::string("[64]"));
    CHECK_EQ(r.view.children.front().text, std::string("128"));
    CHECK_EQ(r.view.children.back().path, std::string("Big[127]"));
    CHECK(r.view.truncated);

    r = eval("Big[990..5000]");                                  // clamped to the end
    CHECK(r.ok);
    CHECK_EQ(r.view.children.size(), size_t(10));
    CHECK(!r.view.truncated);
    r = eval("Big[ ..2 ]");
    CHECK_EQ(r.view.children.size(), size_t(3));
    r = eval("Big[998..]");
    CHECK_EQ(r.view.children.size(), size_t(2));
    CHECK_EQ(r.view.children.back().text, std::string("1998"));
    r = eval("Big[2000..2100]");                                 // beyond the end: nothing, not an error
    CHECK(r.ok);
    CHECK_EQ(r.view.children.size(), size_t(0));

    InspectOptions small;
    small.maxRange = 10;                                         // a request is limited, the rest stays "more"
    r = evaluateExpression(inst, nullptr, 0, "Big[0..999]", small);
    CHECK_EQ(r.view.children.size(), size_t(10));
    CHECK(r.view.truncated);

    r = eval("Big[500]");
    CHECK(r.ok);
    CHECK_EQ(r.view.text, std::string("1000"));
    CHECK_EQ(r.view.path, std::string("Big[500]"));
    r = eval("Big[1000]");
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("index 1000 is out of range (size 1000)"));
    r = eval("Names[\"k7\"]");
    CHECK(r.ok);
    CHECK_EQ(r.view.text, std::string("7"));
    r = eval("Names[k8]");                                       // a bare word is a string key
    CHECK_EQ(r.view.text, std::string("8"));
    r = eval("Names[\"nope\"]");
    CHECK(!r.ok);
    r = eval("Names[10..12]");                                   // entries by position (the keys are ordered as text)
    CHECK(r.ok);
    CHECK_EQ(r.view.children.size(), size_t(3));
    CHECK_EQ(r.view.children[0].name, std::string("\"k107\""));
    CHECK_EQ(r.view.children[0].path, std::string("Names[\"k107\"]"));
    CHECK_EQ(r.view.total, size_t(1000));
}

TEST(evaluate_expression_reports_mistakes) {
    TestRuntime rt;
    auto prog = bigContainers(rt, 10);
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    auto err = [&](const std::string& e) {
        auto r = evaluateExpression(inst, nullptr, 0, e);
        CHECK(!r.ok);
        return r.error;
    };
    CHECK_EQ(err("Missing"), std::string("unknown variable 'Missing'"));
    CHECK_EQ(err(""), std::string("expected a variable name"));
    CHECK_EQ(err("Big["), std::string("missing ']'"));
    CHECK_EQ(err("Big[]"), std::string("empty []"));
    CHECK_EQ(err("Big[1..2][0]"), std::string("a range can only be the last step"));
    CHECK_EQ(err("Big[x..2]"), std::string("'x' is not a number"));
    CHECK_EQ(err("Big.Length"), std::string("'Big' has no properties"));
    CHECK_EQ(err("Big[k]"), std::string("'Big' is an array: use a number as index"));
    CHECK_EQ(err("Big[2][1]"), std::string("'Big[2]' is not an array, map or object"));
    CHECK_EQ(err("Big[2..3].X"), std::string("a range can only be the last step"));
    auto ok = evaluateExpression(inst, nullptr, 0, "  Big [ 3 ]  ");       // blanks are fine
    CHECK(ok.ok);
    CHECK_EQ(ok.view.text, std::string("6"));
}

TEST(evaluate_expression_finds_locals_and_nested_values) {
    TestRuntime rt;
    registerSample(rt);
    Script s;
    s.variables.push_back({"G", "int", Value(3), "", ""});
    s.variables.push_back({"Pets", "array<Dog>", Value(), "", ""});
    Graph& f = s.addFunction("F", {{"P", "array<int>", false, false, Value()}});
    f.locals.push_back({"G", "string", Value("local wins"), "", ""});      // hides the global G inside F
    f.connections.clear();
    NodeId entry = f.nodes[0].id;
    NodeId d = f.addNode("flow.delay").id;
    link(f, entry, "Then", d, "In");
    Graph& g = s.addEventGraph();
    NodeId ev = fpNode(g, "event:Go");
    NodeId mk = fpNode(g, "new:Dog"), garr = fpVar(g, "var.get", "Pets"), push = fpNode(g, "array.add");
    link(g, ev, "Then", mk, "In");
    link(g, garr, "Value", push, "Array");
    link(g, mk, "Object", push, "Item");
    link(g, mk, "Then", push, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    auto r = evaluateExpression(inst, nullptr, 0, "Pets[0].Fetched");
    CHECK(r.ok);
    CHECK_EQ(r.view.text, std::string("0"));
    CHECK_EQ(r.view.path, std::string("Pets[0].Fetched"));
    r = evaluateExpression(inst, nullptr, 0, "Pets[0].Name");
    CHECK(r.ok);
    CHECK_EQ(r.view.text, std::string("\"animal\""));
    r = evaluateExpression(inst, nullptr, 0, "Pets[0].Wings");
    CHECK(!r.ok);
    r = evaluateExpression(inst, nullptr, 0, "Pets[0][0..2]");                   // the properties of an object, by position
    CHECK(r.ok);
    CHECK_EQ(r.view.children.size(), size_t(3));
    CHECK_EQ(r.view.children[0].path.rfind("Pets[0].", 0), size_t(0));
    // inside a call: the frame's own names come first
    auto call = inst.callFunction("F", {Value()});
    CHECK(call.waiting());
    r = evaluateExpression(inst, nullptr, 0, "G");
    CHECK_EQ(r.view.text, std::string("3"));
    r = evaluateExpression(inst, call.task.get(), 0, "G");
    CHECK_EQ(r.view.text, std::string("\"local wins\""));
    r = evaluateExpression(inst, call.task.get(), 0, "P");
    CHECK(r.ok);
    CHECK_EQ(r.view.text, std::string("array[0]"));
    r = evaluateExpression(inst, call.task.get(), 0, "Pets");                     // globals stay visible
    CHECK(r.ok);
    CHECK_EQ(r.view.total, size_t(1));
}

// Containers are references inside a graph: a built-in node that works with an array / map does not copy it. The pins say so (by reference
// when the node modifies the container, const reference when it only reads it, reference on outputs that hand out an existing container).
TEST(builtin_nodes_show_how_they_use_containers) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Ints", "array<int>", Value(), "", ""});
    s.variables.push_back({"Nested", "array<array<int>>", Value(), "", ""});
    s.variables.push_back({"Lists", "map<string,array<int>>", Value(), "", ""});
    s.variables.push_back({"Num", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    fpNode(g, "event:Go");
    struct Case { const char* node; const char* pin; PinDir dir; bool byRef; bool isConst; };
    std::vector<Case> cases;
    std::vector<NodeId> ids;
    auto add = [&](const char* node, std::vector<Case> pins) {
        NodeId id = fpNode(g, node);
        for (auto& c : pins) { cases.push_back(c); ids.push_back(id); }
        return id;
    };
    const PinDir in = PinDir::In, out = PinDir::Out;
    // nodes that modify their container: by reference
    for (const char* n : {"array.add", "array.insert", "array.set", "array.remove", "array.clear"}) add(n, {{n, "Array", in, true, false}});
    for (const char* n : {"map.set", "map.remove", "map.clear"}) add(n, {{n, "Map", in, true, false}});
    // nodes that only read it: const reference
    for (const char* n : {"array.get", "array.length", "array.contains", "array.find", "flow.foreach"}) add(n, {{n, "Array", in, true, true}});
    for (const char* n : {"map.get", "map.contains", "map.length", "map.keys", "map.values"}) add(n, {{n, "Map", in, true, true}});
    // new containers and plain values are not references
    add("array.make", {{"array.make", "Array", out, false, false}, {"array.make", "[0]", in, false, false}});
    add("map.keys", {{"map.keys", "Keys", out, false, false}});
    add("array.add", {{"array.add", "Item", in, false, false}, {"array.add", "Array", in, true, false}});

    // pins of type T are references only when T turns out to be a container
    NodeId nested = fpVar(g, "var.get", "Nested"), ints = fpVar(g, "var.get", "Ints"), num = fpVar(g, "var.get", "Num"), lists = fpVar(g, "var.get", "Lists");
    ids.push_back(nested); cases.push_back({"var.get Nested", "Value", out, true, false});
    ids.push_back(ints); cases.push_back({"var.get Ints", "Value", out, true, false});
    ids.push_back(num); cases.push_back({"var.get Num", "Value", out, false, false});
    NodeId getOuter = fpNode(g, "array.get"), getInner = fpNode(g, "array.get");
    link(g, nested, "Value", getOuter, "Array");
    link(g, ints, "Value", getInner, "Array");
    ids.push_back(getOuter); cases.push_back({"array.get of arrays", "Item", out, true, false});
    ids.push_back(getInner); cases.push_back({"array.get of ints", "Item", out, false, false});
    NodeId each = fpNode(g, "flow.foreach"), eachInts = fpNode(g, "flow.foreach");
    link(g, nested, "Value", each, "Array");
    link(g, ints, "Value", eachInts, "Array");
    ids.push_back(each); cases.push_back({"foreach of arrays", "Element", out, true, false});
    ids.push_back(eachInts); cases.push_back({"foreach of ints", "Element", out, false, false});
    NodeId mapGet = fpNode(g, "map.get");
    link(g, lists, "Value", mapGet, "Map");
    ids.push_back(mapGet); cases.push_back({"map.get of arrays", "Value", out, true, false});
    ids.push_back(mapGet); cases.push_back({"map.get of arrays", "Key", in, false, false});
    ids.push_back(mapGet); cases.push_back({"map.get of arrays", "Found", out, false, false});
    NodeId sel = fpNode(g, "flow.select"), selInts = fpNode(g, "flow.select");
    link(g, nested, "Value", sel, "A");
    link(g, num, "Value", selInts, "A");
    ids.push_back(sel); cases.push_back({"select of arrays", "A", in, true, true});
    ids.push_back(sel); cases.push_back({"select of arrays", "Result", out, true, false});
    ids.push_back(selInts); cases.push_back({"select of ints", "A", in, false, false});
    ids.push_back(selInts); cases.push_back({"select of ints", "Result", out, false, false});
    NodeId eq = fpNode(g, "cmp.eq"), eqNum = fpNode(g, "cmp.eq");
    link(g, ints, "Value", eq, "A");
    link(g, num, "Value", eqNum, "A");
    ids.push_back(eq); cases.push_back({"cmp.eq of arrays", "A", in, true, true});
    ids.push_back(eq); cases.push_back({"cmp.eq of arrays", "Result", out, false, false});
    ids.push_back(eqNum); cases.push_back({"cmp.eq of ints", "A", in, false, false});
    NodeId addNested = fpNode(g, "array.add");
    link(g, nested, "Value", addNested, "Array");
    ids.push_back(addNested); cases.push_back({"array.add to arrays", "Array", in, true, false});
    ids.push_back(addNested); cases.push_back({"array.add to arrays", "Item", in, false, false});   // the item is copied into the array
    NodeId set = fpVar(g, "var.set", "Ints");
    ids.push_back(set); cases.push_back({"var.set", "Value", in, false, false});                  // assignment copies

    auto prog = build(rt, s);
    CHECK(prog->ok());
    const CompiledGraph& cg = prog->graphs[0];
    CHECK_EQ(ids.size(), cases.size());
    for (size_t i = 0; i < ids.size() && i < cases.size(); ++i) {
        const CompiledNode* n = cg.findNode(ids[i]);
        const CompiledPin* pin = n ? n->findPin(cases[i].pin, cases[i].dir, PinKind::Data) : nullptr;
        if (!pin) {
            if (std::string(cases[i].pin).empty()) continue;
            CHECK(pin != nullptr);
            continue;
        }
        if (pin->byRef != cases[i].byRef || pin->isConst != cases[i].isConst)
            std::printf("  %s.%s: byRef=%d const=%d, expected byRef=%d const=%d%c", cases[i].node, cases[i].pin, int(pin->byRef), int(pin->isConst), int(cases[i].byRef), int(cases[i].isConst), 10);
        CHECK_EQ(pin->byRef, cases[i].byRef);
        CHECK_EQ(pin->isConst, cases[i].isConst);
    }
}

// Scripts come from files: a chain of pure nodes of any length must compile (no recursion along it) and run into the evaluation depth limit.
TEST(very_long_chain_of_pure_nodes_is_a_script_error) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"R", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "R"}, {"scope", "global"}};
    link(g, ev, "Then", set, "In");
    NodeId prev = g.addNode("math.add").id;
    link(g, prev, "Result", set, "Value");
    for (int i = 0; i < 200000; ++i) {
        NodeId n = g.addNode("math.add").id;
        link(g, n, "Result", prev, "A");
        prev = n;
    }
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    auto r = inst.callEvent("Go");
    CHECK(!r.ok);
    CHECK(r.error.find("nested too deeply") != std::string::npos);
}

TEST(nodes_can_have_custom_pins_next_to_the_fixed_ones) {
    TestRuntime rt;
    {
        NodeDef d;
        d.id = "test.sum";
        d.title = "Sum";
        d.category = "Test";
        d.style = NodeStyle::Function;
        d.customInputs = true;                           // the user may add data inputs and outputs to nodes of this kind
        d.customOutputs = true;
        d.pins = {execIn(), execOut(), dataIn("Base", "int", Value(100))};
        // the fixed input comes first (ordinal 0), then the custom ones; the only data output is the first custom output
        d.run = [](NodeCtx& c) {
            int64_t sum = 0;
            for (size_t i = 0; i < c.inputCount(); ++i) sum += c.inInt(i);
            c.out(0, Value(sum));
        };
        rt.registry().addNode(std::move(d));
    }
    Script s;
    s.variables.push_back({"Result", "int", Value(), "", ""});
    Graph& g = s.addEventGraph();
    const NodeId ev = g.addNode("event:Go").id;
    const NodeId sum = g.addNode("test.sum").id;
    g.findNode(sum)->inputs = {{"A", "int", false, false, Value(5)}, {"B", "int", false, false, Value()}};
    g.findNode(sum)->outputs = {{"Total", "int", false, false, Value()}};
    g.findNode(sum)->defaults["B"] = Value(7);           // the literal of an unconnected custom input works like any other
    Node& set = g.addNode("var.set");
    set.props = {{"name", "Result"}, {"scope", "global"}};
    const NodeId setId = set.id;
    link(g, ev, "Then", sum, "In");
    link(g, sum, "Then", setId, "In");
    link(g, sum, "Total", setId, "Value");

    auto prog = build(rt, s);
    CHECK(prog->ok());
    const CompiledNode* cn = prog->graphs[0].findNode(sum);
    CHECK(cn && cn->dataIn.size() == 3 && cn->pins[size_t(cn->dataIn[0])].name == "Base" && cn->pins[size_t(cn->dataIn[2])].name == "B");
    Instance inst(prog);
    CHECK(inst.callEvent("Go").ok);
    CHECK_EQ(inst.getVariable("Result").toInt(), int64_t(112));          // 100 + 5 + 7

    // the pins are part of the script file; a node without custom pins keeps the old file format
    const Json json = s.toJson();         // const: avoids an ambiguous operator[](size_t)/operator[](const char*) overload pick
    CHECK(!json["graphs"][size_t(0)]["nodes"][size_t(0)].has("inputs"));
    Script back = Script::parse(s.toText());
    const Node* n = back.graphs[0].findNode(sum);
    CHECK(n && n->inputs.size() == 2 && n->inputs[0].name == "A" && n->inputs[0].def.toInt() == 5 && n->outputs.size() == 1);
    Instance again(build(rt, back));
    CHECK(again.callEvent("Go").ok);
    CHECK_EQ(again.getVariable("Result").toInt(), int64_t(112));

    // a definition that does not ask for custom pins ignores them
    g.findNode(ev)->inputs = {{"X", "int", false, false, Value()}};
    CHECK(build(rt, s)->ok());
    // a pin of a type this application does not have is reported before the script is run
    CHECK(checkCompatibility(s, rt.registry()).ok());
    g.findNode(sum)->inputs[0].type = "Nope";
    CHECK(!checkCompatibility(s, rt.registry()).ok());
}

// ---- flow, string and array nodes ----------------------------------------------------------------------
// An event graph for "Go" built node by node; Print String nodes ("say") show which way the execution went.
struct GoGraph {
    Script s;
    Graph* g = nullptr;
    NodeId ev = 0;
    GoGraph() { g = &s.addEventGraph(); ev = g->addNode("event:Go").id; }
    NodeId node(const std::string& def, std::map<std::string, Value> defaults = {}, std::map<std::string, std::string> props = {}) {
        Node& n = g->addNode(def);
        n.defaults = std::move(defaults);
        n.props = std::move(props);
        return n.id;
    }
    NodeId say(const char* text) { return node("util.print", {{"String", Value(text)}}); }
    NodeId var(const char* kind, const char* name) { return node(kind, {}, {{"name", name}, {"scope", "global"}}); }
    void link(NodeId a, const std::string& ap, NodeId b, const std::string& bp) { g->connections.push_back({a, ap, b, bp}); }
};
struct GoRun {
    std::shared_ptr<Program> prog;
    std::unique_ptr<Instance> inst;
    std::vector<std::string> log;
    GoRun(Runtime& rt, const Script& s) : prog(build(rt, s)) {
        inst = std::make_unique<Instance>(prog);
        inst->setLogHandler([this](LogLevel, const std::string& m) { log.push_back(m); });
    }
    std::string go() {                    // runs Go once, returns what it printed ("a,b,")
        log.clear();
        RunResult r = inst->callEvent("Go");
        if (!r.ok) return "error: " + r.error;
        std::string out;
        for (auto& l : log) out += l + ",";
        return out;
    }
};

TEST(switch_nodes_choose_the_output_of_the_selection) {
    TestRuntime rt;
    registerSample(rt);
    GoGraph t;
    t.s.variables.push_back({"I", "int", Value(0), "", ""});
    t.s.variables.push_back({"S", "string", Value(""), "", ""});
    t.s.variables.push_back({"M", "Mood", Value(0), "", ""});
    NodeId si = t.node("flow.switch.int", {}, {{"count", "3"}});
    NodeId ss = t.node("flow.switch.string", {}, {{"cases", "Red\nGreen\n\nRed\nDefault"}});   // empty, repeated and "Default" lines are dropped
    NodeId se = t.node("flow.switch.enum", {}, {{"enum", "Mood"}});
    t.link(t.var("var.get", "I"), "Value", si, "Selection");
    t.link(t.var("var.get", "S"), "Value", ss, "Selection");
    t.link(t.var("var.get", "M"), "Value", se, "Selection");
    NodeId seq = t.node("flow.sequence", {}, {{"count", "3"}});
    t.link(t.ev, "Then", seq, "In");
    t.link(seq, "Then 0", si, "In");
    t.link(seq, "Then 1", ss, "In");
    t.link(seq, "Then 2", se, "In");
    for (const char* p : {"0", "1", "2", "Default"}) t.link(si, p, t.say((std::string("i") + p).c_str()), "In");
    for (const char* p : {"Red", "Green", "Default"}) t.link(ss, p, t.say((std::string("s") + p).c_str()), "In");
    for (const char* p : {"Happy", "Sad"}) t.link(se, p, t.say((std::string("m") + p).c_str()), "In");

    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    const CompiledNode* cs = r.prog->graphs[0].findNode(ss);
    CHECK(cs && cs->execOut.size() == 3);                          // Red, Green, Default
    CHECK_EQ(r.go(), std::string("i0,sDefault,mHappy,"));
    r.inst->setVariable("I", Value(2));
    r.inst->setVariable("S", Value("Green"));
    r.inst->setVariable("M", Value(1));
    CHECK_EQ(r.go(), std::string("i2,sGreen,mSad,"));
    r.inst->setVariable("I", Value(3));
    r.inst->setVariable("S", Value("red"));                        // case sensitive
    CHECK_EQ(r.go(), std::string("iDefault,sDefault,mSad,"));
    r.inst->setVariable("I", Value(-1));
    r.inst->setVariable("S", Value("Default"));
    r.inst->setVariable("M", Value(7));                            // not a Mood
    std::string out = r.go();
    CHECK(out.find("7 is not a value of Mood") != std::string::npos);

    // the palette offers one Switch per enum of the application
    NodeBuildCtx ctx{rt.registry(), &t.s, t.g};
    bool offered = false;
    for (auto& tp : buildPalette(ctx)) offered |= tp.defId == "flow.switch.enum" && tp.title == "Switch on Mood" && tp.props.at("enum") == "Mood";
    CHECK(offered);
    // the settings are described for editors
    CHECK_EQ(rt.registry().findNode("flow.switch.string")->settings.at(0).kind, NodeSetting::Kind::Lines);
    CHECK_EQ(rt.registry().findNode("flow.sequence")->settings.at(0).max, 32);
}

TEST(do_once_gate_and_flip_flop_remember_their_state) {
    TestRuntime rt;
    GoGraph t;
    t.s.variables.push_back({"Step", "int", Value(0), "", ""});
    NodeId sw = t.node("flow.switch.int", {}, {{"count", "6"}});
    t.link(t.var("var.get", "Step"), "Value", sw, "Selection");
    NodeId seq = t.node("flow.sequence", {}, {{"count", "3"}});
    t.link(t.ev, "Then", seq, "In");
    t.link(seq, "Then 0", sw, "In");
    // Step 0: Do Once, 1: Reset Do Once, 2: Gate Enter, 3: Gate Close, 4: Gate Open, 5: Gate Toggle
    NodeId once = t.node("flow.doonce");
    t.link(sw, "0", once, "In");
    t.link(sw, "1", once, "Reset");
    t.link(once, "Completed", t.say("once"), "In");
    NodeId gate = t.node("flow.gate");
    t.link(sw, "2", gate, "Enter");
    t.link(sw, "3", gate, "Close");
    t.link(sw, "4", gate, "Open");
    t.link(sw, "5", gate, "Toggle");
    t.link(gate, "Exit", t.say("gate"), "In");
    // every event also goes through the Flip Flop
    NodeId ff = t.node("flow.flipflop");
    t.link(seq, "Then 1", ff, "In");
    t.link(ff, "A", t.say("A"), "In");
    t.link(ff, "B", t.say("B"), "In");

    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    auto step = [&](int s) { r.inst->setVariable("Step", Value(s)); return r.go(); };
    CHECK_EQ(step(0), std::string("once,A,"));
    CHECK_EQ(step(0), std::string("B,"));                         // already done; the Flip Flop alternates
    CHECK_EQ(step(1), std::string("A,"));                         // Reset lets it through again
    CHECK_EQ(step(0), std::string("once,B,"));
    CHECK_EQ(step(2), std::string("gate,A,"));                    // open at first
    CHECK_EQ(step(3), std::string("B,"));
    CHECK_EQ(step(2), std::string("A,"));                         // closed
    CHECK_EQ(step(5), std::string("B,"));
    CHECK_EQ(step(2), std::string("gate,A,"));                    // toggled open
    CHECK_EQ(step(5), std::string("B,"));
    CHECK_EQ(step(4), std::string("A,"));
    CHECK_EQ(step(2), std::string("gate,B,"));
    // resetVariables forgets the state of the nodes too
    r.inst->resetVariables();
    CHECK_EQ(step(0), std::string("once,A,"));

    // Start Closed: the Do Once waits for a Reset, the Gate for an Open
    GoGraph c;
    NodeId once2 = c.node("flow.doonce", {{"Start Closed", Value(true)}});
    NodeId gate2 = c.node("flow.gate", {{"Start Closed", Value(true)}});
    NodeId seq2 = c.node("flow.sequence");
    c.link(c.ev, "Then", seq2, "In");
    c.link(seq2, "Then 0", once2, "In");
    c.link(seq2, "Then 1", gate2, "Enter");
    c.link(once2, "Completed", c.say("once"), "In");
    c.link(gate2, "Exit", c.say("gate"), "In");
    GoRun rc(rt, c.s);
    CHECK_EQ(rc.go(), std::string(""));
}

TEST(for_each_entry_walks_a_map_in_key_order) {
    TestRuntime rt;
    GoGraph t;
    t.s.variables.push_back({"M", "map<string,int>", Value(), "", ""});
    t.s.variables.push_back({"Keys", "string", Value(""), "", ""});
    t.s.variables.push_back({"Sum", "int", Value(0), "", ""});
    NodeId fe = t.node("flow.foreachmap");
    t.link(t.var("var.get", "M"), "Value", fe, "Map");
    t.link(t.ev, "Then", fe, "In");
    // Keys = Keys + Key; Sum = Sum + Value; break at key "c"
    NodeId concat = t.node("fn:String.Concat");
    t.link(t.var("var.get", "Keys"), "Value", concat, "A");
    t.link(fe, "Key", concat, "B");
    NodeId setKeys = t.var("var.set", "Keys");
    t.link(concat, "Result", setKeys, "Value");
    t.link(fe, "Loop Body", setKeys, "In");
    NodeId add = t.node("math.add");
    t.link(t.var("var.get", "Sum"), "Value", add, "A");
    t.link(fe, "Value", add, "B");
    NodeId setSum = t.var("var.set", "Sum");
    t.link(add, "Result", setSum, "Value");
    t.link(setKeys, "Then", setSum, "In");
    NodeId eq = t.node("cmp.eq", {{"B", Value("c")}});
    t.link(fe, "Key", eq, "A");
    NodeId br = t.node("flow.branch");
    t.link(eq, "Result", br, "Condition");
    t.link(setSum, "Then", br, "In");
    t.link(br, "True", t.node("flow.break"), "In");
    t.link(fe, "Completed", t.say("done"), "In");

    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    Value m = rt.registry().makeMap(rt.registry().tString(), rt.registry().tInt());
    for (auto [k, v] : std::vector<std::pair<const char*, int>>{{"d", 1000}, {"b", 10}, {"a", 1}, {"c", 100}}) m.mapPtr()->items[Value(k)] = Value(v);
    r.inst->setVariable("M", m);
    CHECK_EQ(r.go(), std::string("done,"));                       // Break goes on with Completed
    CHECK_EQ(r.inst->getVariable("Keys").toString(), std::string("abc"));
    CHECK_EQ(r.inst->getVariable("Sum").toInt(), int64_t(111));
}

TEST(format_string_and_array_nodes) {
    TestRuntime rt;
    registerSample(rt);
    // Format: {Name} from custom inputs, {{ }} escapes, unknown placeholders stay, enums print by name
    {
        GoGraph t;
        t.s.variables.push_back({"R", "string", Value(""), "", ""});
        Node& f = t.g->addNode("string.format");
        f.inputs = {{"Name", "string", false, false, Value("Ann")}, {"Age", "int", false, false, Value(41)}, {"Mood", "Mood", false, false, Value(1)}};
        f.defaults["Format"] = Value("{Name} is {Age} and {Mood} {{literal}} {Nope}");
        NodeId fid = f.id;
        NodeId set = t.var("var.set", "R");
        t.link(t.ev, "Then", set, "In");
        t.link(fid, "Result", set, "Value");
        GoRun r(rt, t.s);
        CHECK(r.prog->ok());
        CHECK_EQ(r.go(), std::string(""));
        CHECK_EQ(r.inst->getVariable("R").toString(), std::string("Ann is 41 and Sad {literal} {Nope}"));
    }
    // strings: pure functions are easiest to check one by one through a variable
    auto stringResult = [&](const char* fn, std::map<std::string, Value> defaults, const char* type) {
        GoGraph t;
        t.s.variables.push_back({"R", type, Value(), "", ""});
        NodeId n = t.node(fn, std::move(defaults));
        NodeId set = t.var("var.set", "R");
        t.link(t.ev, "Then", set, "In");
        t.link(n, rt.registry().findFunction(std::string(fn).substr(3))->retName, set, "Value");
        GoRun r(rt, t.s);
        r.go();
        return r.inst->getVariable("R");
    };
    CHECK_EQ(stringResult("fn:String.Trim", {{"String", Value("  a b \t\n")}}, "string").toString(), std::string("a b"));
    CHECK(stringResult("fn:String.Starts With", {{"String", Value("hello")}, {"Prefix", Value("he")}}, "bool").toBool());
    CHECK(!stringResult("fn:String.Starts With", {{"String", Value("h")}, {"Prefix", Value("he")}}, "bool").toBool());
    CHECK(stringResult("fn:String.Ends With", {{"String", Value("hello")}, {"Suffix", Value("llo")}}, "bool").toBool());
    CHECK_EQ(stringResult("fn:String.Find", {{"String", Value("abcabc")}, {"Substring", Value("c")}, {"Start", Value(3)}}, "int").toInt(), int64_t(5));
    CHECK_EQ(stringResult("fn:String.Find", {{"String", Value("abc")}, {"Substring", Value("x")}}, "int").toInt(), int64_t(-1));
    CHECK(stringResult("fn:String.Is Empty", {{"String", Value("")}}, "bool").toBool());
    CHECK(std::fabs(stringResult("fn:Math.Pi", {}, "float").toFloat() - 3.14159265358979) < 1e-12);
    CHECK(std::fabs(stringResult("fn:Math.Atan2", {{"Y", Value(1.0)}, {"X", Value(1.0)}}, "float").toFloat() - 0.785398163397448) < 1e-12);

    // arrays: A = [3, 1, 2, 1]; Remove Item 1; Sort; Reverse; S = Slice(A, 1, 5); E = Is Empty(S)
    {
        GoGraph t;
        t.s.variables.push_back({"A", "array<int>", Value(), "", ""});
        t.s.variables.push_back({"S", "array<int>", Value(), "", ""});
        t.s.variables.push_back({"E", "bool", Value(true), "", ""});
        t.s.variables.push_back({"Removed", "bool", Value(false), "", ""});
        NodeId rm = t.node("array.removeitem", {{"Item", Value(1)}});
        NodeId sort = t.node("array.sort"), rev = t.node("array.reverse");
        NodeId slice = t.node("array.slice", {{"Start", Value(1)}, {"Length", Value(5)}});
        NodeId empty = t.node("array.isempty");
        for (NodeId n : {rm, sort, rev}) t.link(t.var("var.get", "A"), "Value", n, "Array");
        t.link(t.var("var.get", "A"), "Value", slice, "Array");
        t.link(slice, "Result", empty, "Array");
        NodeId setRemoved = t.var("var.set", "Removed"), setS = t.var("var.set", "S"), setE = t.var("var.set", "E");
        t.link(rm, "Removed", setRemoved, "Value");
        t.link(slice, "Result", setS, "Value");
        t.link(empty, "Empty", setE, "Value");
        t.link(t.ev, "Then", rm, "In");
        t.link(rm, "Then", setRemoved, "In");
        t.link(setRemoved, "Then", sort, "In");
        t.link(sort, "Then", rev, "In");
        t.link(rev, "Then", setS, "In");
        t.link(setS, "Then", setE, "In");
        GoRun r(rt, t.s);
        CHECK(r.prog->ok());
        Value a = rt.registry().makeArray(rt.registry().tInt());
        for (int v : {3, 1, 2, 1}) a.arrayPtr()->items.push_back(Value(v));
        r.inst->setVariable("A", a);
        CHECK_EQ(r.go(), std::string(""));
        auto ints = [](const Value& v) {
            std::string s;
            for (auto& x : v.arrayPtr()->items) s += std::to_string(x.toInt()) + " ";
            return s;
        };
        CHECK(r.inst->getVariable("Removed").toBool());
        CHECK_EQ(ints(r.inst->getVariable("A")), std::string("3 2 1 "));   // first 1 removed, sorted, reversed
        CHECK_EQ(ints(r.inst->getVariable("S")), std::string("2 1 "));     // clamped to the end
        CHECK(!r.inst->getVariable("E").toBool());
    }
}

TEST(find_references_and_search_the_script) {
    TestRuntime rt;
    Script s;
    s.variables.push_back({"Score", "int", Value(0), "", ""});
    Graph& f = s.addFunction("Bump", {{"Amount", "int", false, false, Value(1)}});
    f.locals.push_back({"Score", "int", Value(0), "", ""});                       // hides the variable of the script in Bump
    NodeId localGet = f.addNode("var.get").id;
    f.findNode(localGet)->props = {{"name", "Score"}};                            // no scope: resolves to the local
    NodeId explicitGlobal = f.addNode("var.set").id;
    f.findNode(explicitGlobal)->props = {{"name", "Score"}, {"scope", "global"}};
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Score"}, {"scope", "global"}};
    NodeId get = g.addNode("var.get").id;
    g.findNode(get)->props = {{"name", "Score"}};
    NodeId call1 = g.addNode("func.call").id, call2 = g.addNode("func.call").id;
    g.findNode(call1)->props["function"] = "Bump";
    g.findNode(call2)->props["function"] = "Bump";
    NodeId sin = g.addNode("fn:Math.Sin").id;
    g.findNode(sin)->comment = "the wave";
    NodeId pr = g.addNode("util.print").id;
    g.findNode(pr)->defaults["String"] = Value("Game Over");
    g.addComment(0, 0).text = "Scoring rules";
    auto prog = rt.compile(s);

    auto ids = [](const std::vector<ScriptRef>& refs) {
        std::string out;
        for (auto& r : refs) out += r.graph + "#" + std::to_string(r.node) + (r.detail.empty() ? "" : ":" + r.detail) + " ";
        return out;
    };
    RefTarget global{RefTarget::Kind::Variable, "Score", ""};
    CHECK_EQ(ids(findReferences(s, prog.get(), global)),
             "Bump#" + std::to_string(explicitGlobal) + ":Set EventGraph#" + std::to_string(set) + ":Set EventGraph#" + std::to_string(get) + ":Get ");
    RefTarget local{RefTarget::Kind::Variable, "Score", "Bump"};
    CHECK_EQ(ids(findReferences(s, prog.get(), local)), "Bump#" + std::to_string(localGet) + ":Get ");
    CHECK_EQ(findReferences(s, prog.get(), {RefTarget::Kind::Function, "Bump", ""}).size(), size_t(2));
    CHECK_EQ(findReferences(s, prog.get(), {RefTarget::Kind::Definition, "fn:Math.Sin", ""}).size(), size_t(1));

    // the target of a node: its variable (in the right scope), its function, or its definition
    RefTarget t = referenceTargetOf(s, "Bump", *f.findNode(localGet));
    CHECK(t.kind == RefTarget::Kind::Variable && t.name == "Score" && t.graph == "Bump");
    t = referenceTargetOf(s, "Bump", *f.findNode(explicitGlobal));
    CHECK(t.kind == RefTarget::Kind::Variable && t.graph.empty());
    t = referenceTargetOf(s, "EventGraph", *g.findNode(call1));
    CHECK(t.kind == RefTarget::Kind::Function && t.name == "Bump");
    t = referenceTargetOf(s, "EventGraph", *g.findNode(ev));
    CHECK(t.kind == RefTarget::Kind::Definition && t.name == "event:Go");
    CHECK_EQ(t.describe(), std::string("event:Go nodes"));
    CHECK_EQ((RefTarget{RefTarget::Kind::Definition, "fn:Math.Sin", ""}.describe()), std::string("Math.Sin nodes"));

    // search: titles, comments, literals and comment boxes, case insensitive
    auto found = searchScript(s, prog.get(), "game over");
    CHECK(found.size() == 1 && found[0].node == pr && found[0].detail == "String = Game Over");
    found = searchScript(s, prog.get(), "WAVE");
    CHECK(found.size() == 1 && found[0].node == sin && found[0].detail.rfind("comment", 0) == 0);
    found = searchScript(s, prog.get(), "scoring");
    CHECK(found.size() == 1 && found[0].node == 0 && found[0].title == "Scoring rules");
    found = searchScript(s, prog.get(), "sin");
    CHECK(found.size() == 1 && found[0].node == sin);
    CHECK(searchScript(s, prog.get(), "").empty());
}

// ---- asynchronous host functions ------------------------------------------------------------------------
// Test.Later(X) returns a future the test fulfils by hand; Test.Thread(X) computes X * 10 on a thread of its own.
struct Promises {
    std::mutex mu;
    std::vector<std::promise<int64_t>> list;
    std::future<int64_t> add() {
        std::lock_guard<std::mutex> lock(mu);
        list.emplace_back();
        return list.back().get_future();
    }
};

// Go: R = R + Test.<fn>(X); Print "done"
static Script asyncScript(const char* fn, int loops = 0) {
    GoGraph t;
    t.s.variables.push_back({"R", "int", Value(0), "", ""});
    NodeId call = t.node(fn, {{"X", Value(4)}});
    NodeId add = t.node("math.add");
    t.link(t.var("var.get", "R"), "Value", add, "A");
    t.link(call, "Result", add, "B");
    NodeId set = t.var("var.set", "R");
    t.link(add, "Result", set, "Value");
    t.link(call, "Then", set, "In");
    NodeId done = t.say("done");
    if (loops > 0) {
        NodeId loop = t.node("flow.for", {{"First", Value(0)}, {"Last", Value(loops)}});
        t.link(t.ev, "Then", loop, "In");
        t.link(loop, "Loop Body", call, "In");
        t.link(loop, "Completed", done, "In");
        t.link(loop, "Index", call, "X");
    } else {
        t.link(t.ev, "Then", call, "In");
        t.link(set, "Then", done, "In");
    }
    return t.s;
}

TEST(asynchronous_host_functions_wait_without_blocking) {
    TestRuntime rt;
    auto promises = std::make_shared<Promises>();
    rt.registry().addFunction("Test.Later", [promises](int64_t) { return promises->add(); }, {"X"}).makePure();   // makePure is ignored
    rt.registry().addFunction("Test.Ready", [](int64_t x) {
        std::promise<int64_t> p;
        p.set_value(x + 1);
        return p.get_future();
    }, {"X"});
    rt.registry().addFunction("Test.Fails", [](int64_t) {
        return std::async(std::launch::async, []() -> int64_t { throw std::runtime_error("the server said no"); });
    }, {"X"});
    rt.registry().addFunction("Test.Thread", [](int64_t x) {
        return std::async(std::launch::async, [x] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return x * 10;
        }).share();
    }, {"X"});

    const FunctionDesc* later = rt.registry().findFunction("Test.Later");
    CHECK(later && later->latent && later->ret == rt.registry().tInt());
    const NodeDef* nd = rt.registry().findNode("fn:Test.Later");
    CHECK(nd && !nd->pure && nd->keywords.find("async") != std::string::npos);

    // the task waits until the future is ready; the thread that runs it is never blocked
    {
        GoRun r(rt, asyncScript("fn:Test.Later"));
        CHECK(r.prog->ok());
        RunResult res = r.inst->callEvent("Go");
        CHECK(res.waiting());
        r.inst->tick(0.1);
        CHECK(res.task->isWaiting());
        CHECK_EQ(promises->list.size(), size_t(1));
        promises->list[0].set_value(38);
        r.inst->tick(0.01);
        CHECK(res.task->state() == TaskState::Finished);
        CHECK_EQ(r.inst->getVariable("R").toInt(), int64_t(38));
        CHECK_EQ(r.log.size(), size_t(1));
    }
    // a future that is ready at once: no wait at all
    {
        GoRun r(rt, asyncScript("fn:Test.Ready"));
        CHECK(r.inst->callEvent("Go").finished());
        CHECK_EQ(r.inst->getVariable("R").toInt(), int64_t(5));
    }
    // what the asynchronous work throws is a script error at the node
    {
        Script s = asyncScript("fn:Test.Fails");
        GoRun r(rt, s);
        RunResult res = r.inst->callEvent("Go");
        for (int i = 0; i < 500 && res.task && !res.task->done(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            r.inst->tick(0.002);
        }
        RunResult fin = res.task->result();
        CHECK(fin.state == TaskState::Failed);
        CHECK(fin.error.find("the server said no") != std::string::npos);
        CHECK_EQ(fin.errorNode, s.graphs[0].nodes[1].id);        // the Test.Fails node
    }
    // in a loop, on real threads, through standard::runMain's kind of loop (sleep until the next wake, tick)
    {
        GoRun r(rt, asyncScript("fn:Test.Thread", 4));
        RunResult res = r.inst->callEvent("Go");
        for (int i = 0; i < 1000 && res.task && !res.task->done(); ++i) {
            double wake = r.inst->nextWakeIn();
            CHECK(wake >= 0 && wake <= 0.011);                      // a waiting call is polled every 10 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            r.inst->tick(0.002);
        }
        CHECK(res.task->state() == TaskState::Finished);
        CHECK_EQ(r.inst->getVariable("R").toInt(), int64_t(60));     // (0 + 1 + 2 + 3) * 10
        CHECK_EQ(r.log.size(), size_t(1));
    }
    // AsyncExecutor: the worker polls the call like a Delay
    {
        auto prog = build(rt, asyncScript("fn:Test.Thread", 3));
        std::atomic<int> ok{0};
        AsyncExecutor::Callbacks cb;
        cb.completed = [&](const Executor::Completion& c) { if (c.result.state == TaskState::Finished) ++ok; };
        AsyncExecutor ex(prog, cb);
        ex.postEvent("Go");
        CHECK(ex.waitIdle(5000));
        CHECK_EQ(ok.load(), 1);
        Value r;
        ex.inspect([&](Executor& e) { r = e.instance().getVariable("R"); });
        CHECK_EQ(r.toInt(), int64_t(30));
    }
    // called directly (not from a script), the function waits for its result
    {
        Value args[1] = {Value(6)};
        CHECK_EQ(rt.registry().findFunction("Test.Thread")->invoke(args).toInt(), int64_t(60));
    }
    // aborting a waiting task drops the call
    {
        GoRun r(rt, asyncScript("fn:Test.Later"));
        RunResult res = r.inst->callEvent("Go");
        CHECK(res.waiting());
        r.inst->abortAll();
        CHECK(res.task->state() == TaskState::Aborted);
        promises->list.back().set_value(1);                         // nobody waits for it any more
        r.inst->tick(0.01);
        CHECK_EQ(r.inst->getVariable("R").toInt(), int64_t(0));
    }
}

// ---- collapse to function -------------------------------------------------------------------------------
TEST(collapse_to_function_keeps_what_the_script_does) {
    TestRuntime rt;
    // Go -> Set Total = Total + Step -> Print(To String(Total + Step)) -> Set Double = (Total + Step) * 2 -> Print "end"
    GoGraph t;
    t.s.variables.push_back({"Total", "int", Value(10), "", ""});
    t.s.variables.push_back({"Step", "int", Value(3), "", ""});
    t.s.variables.push_back({"Double", "int", Value(0), "", ""});
    NodeId getTotal = t.var("var.get", "Total");
    NodeId getStep = t.var("var.get", "Step");
    NodeId add = t.node("math.add");
    t.link(getTotal, "Value", add, "A");
    t.link(getStep, "Value", add, "B");
    NodeId setTotal = t.var("var.set", "Total");
    t.link(add, "Result", setTotal, "Value");
    NodeId str = t.node("fn:String.To String");
    t.link(add, "Result", str, "Value");
    NodeId pr = t.node("util.print");
    t.link(str, "Result", pr, "String");
    NodeId mul = t.node("math.mul", {{"B", Value(2)}});
    t.link(add, "Result", mul, "A");
    NodeId setDouble = t.var("var.set", "Double");
    t.link(mul, "Result", setDouble, "Value");
    NodeId end = t.say("end");
    t.link(t.ev, "Then", setTotal, "In");
    t.link(setTotal, "Then", pr, "In");
    t.link(pr, "Then", setDouble, "In");
    t.link(setDouble, "Then", end, "In");

    auto runIt = [&](const Script& s) {
        GoRun r(rt, s);
        std::string out = r.prog->ok() ? r.go() : "does not compile: " + dumpDiagnostics(*r.prog);
        return out + " Total=" + std::to_string(r.inst->getVariable("Total").toInt()) + " Double=" + std::to_string(r.inst->getVariable("Double").toInt());
    };
    // Add is pure: every node that reads it computes it again, so Print sees the new Total (13 + 3) and so does Set Double
    const std::string expected = runIt(t.s);
    CHECK_EQ(expected, std::string("16,end, Total=13 Double=32"));

    // move Step, Add, Set Total, To String and Print into "AddStep": Add's result goes out, the flow enters at Set Total and leaves
    // towards Set Double. Get Total feeds the selection: it is copied in (an input would be a copy of Total taken at the call).
    Script s = t.s;
    CollapseResult cr = collapseToFunction(s, rt.registry(), "EventGraph", {getStep, add, setTotal, str, pr}, "AddStep");
    CHECK(cr.ok);
    if (!cr.ok) std::printf("  %s\n", cr.error.c_str());
    CHECK(cr.warnings.empty());
    CHECK_EQ(cr.function, std::string("AddStep"));
    const Graph* fnGraph = s.findGraph("AddStep");
    CHECK(fnGraph && fnGraph->kind == GraphKind::Function);
    if (fnGraph) {
        CHECK(fnGraph->inputs.empty());
        CHECK(fnGraph->outputs.size() == 1 && fnGraph->outputs[0].name == "Result" && fnGraph->outputs[0].type == "int");
        CHECK_EQ(fnGraph->nodes.size(), size_t(8));                       // entry, return, the five nodes and the copy of Get Total
    }
    const Graph& eg = *s.findGraph("EventGraph");
    CHECK(eg.findNode(add) == nullptr && eg.findNode(cr.callNode) != nullptr);
    CHECK(eg.findNode(getTotal) == nullptr);                              // it fed nothing else
    CHECK_EQ(eg.nodes.size(), size_t(5));                                 // event, call, Mul, Set Double, Print end
    CHECK_EQ(runIt(s), expected);
    CHECK_EQ(Script::parse(s.toText()).toText(), s.toText());
    // no exec output has two links, and the result compiles without a diagnostic of any kind
    auto execOutsLinkedOnce = [&](const Script& sc) {
        auto prog = rt.compile(sc);
        for (auto& g : sc.graphs) {
            std::map<std::pair<NodeId, std::string>, int> links;
            for (auto& c : g.connections) {
                const CompiledNode* cn = prog->findGraph(g.name)->findNode(c.fromNode);
                const CompiledPin* p = cn ? cn->findPin(c.fromPin, PinDir::Out, PinKind::Exec) : nullptr;
                if (p && ++links[{c.fromNode, c.fromPin}] > 1) return false;
            }
        }
        return prog->diagnostics.empty();
    };
    CHECK(execOutsLinkedOnce(s));
    // the default script of the standard applications: an event and one Print String; collapse the Print
    {
        GoGraph one;
        NodeId p = one.say("hello");
        one.link(one.ev, "Then", p, "In");
        CollapseResult r = collapseToFunction(one.s, rt.registry(), "EventGraph", {p}, "Hello");
        CHECK(r.ok);
        const Graph* hg = one.s.findGraph("Hello");
        CHECK(hg && hg->connections.size() == 1);                        // entry -> Print, nothing else (no entry -> Return)
        CHECK(execOutsLinkedOnce(one.s));
        GoRun run(rt, one.s);
        CHECK_EQ(run.go(), std::string("hello,"));
    }

    // a value computed outside that reads what the selection changes becomes an input: the function sees the value of the call
    {
        GoGraph w;
        w.s.variables.push_back({"N", "int", Value(1), "", ""});
        NodeId plus = w.node("math.add", {{"B", Value(100)}});
        w.link(w.var("var.get", "N"), "Value", plus, "A");
        NodeId setN = w.var("var.set", "N");
        w.link(plus, "Result", setN, "Value");
        NodeId toStr = w.node("fn:String.To String");
        w.link(plus, "Result", toStr, "Value");
        NodeId p = w.node("util.print");
        w.link(toStr, "Result", p, "String");
        w.link(w.ev, "Then", setN, "In");
        w.link(setN, "Then", p, "In");
        CollapseResult r = collapseToFunction(w.s, rt.registry(), "EventGraph", {setN, toStr, p});
        CHECK(r.ok && r.warnings.size() == 1 && r.warnings[0].find("reads 'N'") != std::string::npos);
    }
    CHECK_EQ(Script::parse(s.toText()).toText(), s.toText());

    // what cannot be collapsed: the script stays as it is
    auto refused = [&](std::vector<NodeId> ids, const char* why) {
        Script copy = t.s;
        CollapseResult r = collapseToFunction(copy, rt.registry(), "EventGraph", ids);
        bool ok = !r.ok && r.error.find(why) != std::string::npos && copy.toText() == t.s.toText();
        if (!ok) std::printf("  got: %s\n", r.error.c_str());
        return ok;
    };
    CHECK(refused({t.ev, setTotal}, "the event 'Go' cannot be moved"));
    CHECK(refused({add, str}, "no node with execution pins"));
    CHECK(refused({setTotal, setDouble}, "enters the selection at more than one place"));
    CHECK(refused({}, "select the nodes"));

    // leaving towards two places
    {
        GoGraph b;
        NodeId br = b.node("flow.branch");
        NodeId yes = b.say("yes"), no = b.say("no");
        b.link(b.ev, "Then", br, "In");
        b.link(br, "True", yes, "In");
        b.link(br, "False", no, "In");
        Script copy = b.s;
        CollapseResult r = collapseToFunction(copy, rt.registry(), "EventGraph", {br});
        CHECK(!r.ok && r.error.find("more than one place") != std::string::npos);
        // with one way out and one that ends inside it is done, with a warning
        copy.graphs[0].connections.pop_back();
        r = collapseToFunction(copy, rt.registry(), "EventGraph", {br});
        CHECK(r.ok && r.warnings.size() == 1);
    }
    // local variables of a function stay where they are
    {
        Script f;
        Graph& fg = f.addFunction("F");
        fg.locals.push_back({"L", "int", Value(0), "", ""});
        Node& set = fg.addNode("var.set");
        set.props = {{"name", "L"}, {"scope", "local"}};
        CollapseResult r = collapseToFunction(f, rt.registry(), "F", {set.id});
        CHECK(!r.ok && r.error.find("local variable or parameter") != std::string::npos);
    }
}

// ---- node reference ------------------------------------------------------------------------------------
TEST(node_reference_describes_what_the_registry_offers) {
    TestRuntime rt;
    registerSample(rt);
    const std::string md = nodeReferenceMarkdown(rt.registry());
    auto has = [&](const std::string& s) {
        bool ok = md.find(s) != std::string::npos;
        if (!ok) std::printf("  missing: %s\n", s.c_str());
        return ok;
    };
    CHECK(has("# FlowGraph node reference"));
    CHECK(has("## Flow\n"));
    CHECK(has("### Branch\n\n`flow.branch` · exec\n"));
    CHECK(has("| in | Condition | `bool` | false |"));
    CHECK(has("### Switch on String"));
    CHECK(has("Settings (Details panel): **Cases** (one per line) - One case per line"));
    CHECK(has("Type parameters: `T` one of `int`, `float` (from what is connected)"));    // Add
    CHECK(has("| in | Array | `array<T>` (changed in place) |"));                      // Array Add
    CHECK(has("### Tick\n"));                                                           // an event, with its argument
    CHECK(has("| DeltaTime | `float` |"));
    CHECK(has("### Dog\n\nDerives from Animal."));
    CHECK(has("| Value | Number |\n|-|-|\n| Happy | 0 |\n| Sad | 1 |"));
    CHECK(has("[Containers / Array](#containers--array)"));
    CHECK(md.find("Function Entry") == std::string::npos);                              // not in the palette
    ReferenceOptions noGen;
    noGen.includeGenerated = false;
    CHECK(nodeReferenceMarkdown(rt.registry(), noGen).find("m:Animal.Speak") == std::string::npos);
    CHECK(md.find("m:Animal.Speak") != std::string::npos);
}

// ---- random numbers ------------------------------------------------------------------------------------
TEST(random_nodes_draw_one_number_per_run_and_can_be_seeded) {
    TestRuntime rt;
    CHECK(!rt.registry().findNode("fn:Math.Random Int")->pure);
    // Go -> Set Random Seed(Seed) -> Random Int -> Print(To String(R)) -> Set X = R: Print and Set read the SAME number
    GoGraph t;
    t.s.variables.push_back({"X", "int", Value(0), "", ""});
    t.s.variables.push_back({"Seed", "int", Value(7), "", ""});
    NodeId seed = t.node("fn:Math.Set Random Seed");
    t.link(t.var("var.get", "Seed"), "Value", seed, "Seed");
    NodeId rnd = t.node("fn:Math.Random Int", {{"Min", Value(1)}, {"Max", Value(1000000000)}});
    NodeId str = t.node("fn:String.To String");
    t.link(rnd, "Result", str, "Value");
    NodeId pr = t.node("util.print");
    t.link(str, "Result", pr, "String");
    NodeId set = t.var("var.set", "X");
    t.link(rnd, "Result", set, "Value");
    t.link(t.ev, "Then", seed, "In");
    t.link(seed, "Then", rnd, "In");
    t.link(rnd, "Then", pr, "In");
    t.link(pr, "Then", set, "In");
    GoRun a(rt, t.s), b(rt, t.s);
    CHECK(a.prog->ok() && a.prog->diagnostics.empty());
    const std::string printed = a.go();
    CHECK_EQ(printed, std::to_string(a.inst->getVariable("X").toInt()) + ",");
    CHECK_EQ(b.go(), printed);                                       // the same seed gives the same numbers
    a.inst->setVariable("Seed", Value(8));
    CHECK(a.go() != printed);

    // an exec node that is used like a pure one is never run: the compiler says so
    GoGraph w;
    w.s.variables.push_back({"X", "int", Value(0), "", ""});
    NodeId lonely = w.node("fn:Math.Random Int");
    NodeId set2 = w.var("var.set", "X");
    w.link(w.ev, "Then", set2, "In");
    w.link(lonely, "Result", set2, "Value");
    auto prog = rt.compile(w.s);
    CHECK(prog->ok());
    bool warned = false;
    for (auto& d : prog->diagnostics)
        warned |= d.severity == Diagnostic::Severity::Warning && d.node == lonely && d.message.find("not connected to the execution flow") != std::string::npos;
    CHECK(warned);
}

// ---- limits for scripts the application does not trust -------------------------------------------------
TEST(limits_stop_scripts_that_grow_too_big_or_run_too_long) {
    TestRuntime rt;
    {   // a copy of Array Add without the intrinsic: the run function must enforce the limit exactly like the interpreter
        NodeDef copy = *rt.registry().findNode("array.add");
        copy.id = "test.arrayadd";
        copy.intrinsic = Intrinsic::None;
        rt.registry().addNode(std::move(copy));
        NodeDef mcopy = *rt.registry().findNode("map.set");
        mcopy.id = "test.mapset";
        mcopy.intrinsic = Intrinsic::None;
        rt.registry().addNode(std::move(mcopy));
    }
    // For 0..100: Add(A, Index) / Set Entry(M, Index, Index)
    auto growing = [&](const char* def, const char* type) {
        GoGraph t;
        t.s.variables.push_back({"C", type, Value(), "", ""});
        NodeId loop = t.node("flow.for", {{"First", Value(0)}, {"Last", Value(100)}});
        NodeId add = t.node(def);
        t.link(t.var("var.get", "C"), "Value", add, std::string(def).find("map") != std::string::npos ? "Map" : "Array");
        t.link(loop, "Index", add, std::string(def).find("map") != std::string::npos ? "Key" : "Item");
        if (std::string(def).find("map") != std::string::npos) t.link(loop, "Index", add, "Value");
        t.link(t.ev, "Then", loop, "In");
        t.link(loop, "Loop Body", add, "In");
        return std::make_pair(t.s, add);
    };
    for (const char* def : {"array.add", "test.arrayadd", "map.set", "test.mapset"}) {
        const bool isMap = std::string(def).find("map") != std::string::npos;
        auto [s, add] = growing(def, isMap ? "map<int,int>" : "array<int>");
        GoRun r(rt, s);
        CHECK(r.prog->ok());
        CHECK_EQ(r.go(), std::string(""));                            // no limit: 100 elements
        Instance::Limits lim;
        lim.maxContainerSize = 50;
        r.inst->setLimits(lim);
        r.inst->resetVariables();
        RunResult res = r.inst->callEvent("Go");
        CHECK(res.state == TaskState::Failed);
        CHECK(res.error.find("would have 51 elements; this application allows 50") != std::string::npos);
        CHECK_EQ(res.errorNode, add);
        const Value c = r.inst->getVariable("C");
        CHECK_EQ(isMap ? c.mapPtr()->items.size() : c.arrayPtr()->items.size(), size_t(50));
    }
    // strings: S = Concat(S, S) doubles until it is too long; results of nodes are checked
    {
        GoGraph t;
        t.s.variables.push_back({"S", "string", Value("ab"), "", ""});
        NodeId loop = t.node("flow.for", {{"First", Value(0)}, {"Last", Value(30)}});
        NodeId cat = t.node("fn:String.Concat");
        t.link(t.var("var.get", "S"), "Value", cat, "A");
        t.link(t.var("var.get", "S"), "Value", cat, "B");
        NodeId set = t.var("var.set", "S");
        t.link(cat, "Result", set, "Value");
        t.link(t.ev, "Then", loop, "In");
        t.link(loop, "Loop Body", set, "In");
        GoRun r(rt, t.s);
        Instance::Limits lim;
        lim.maxStringLength = 1000;
        r.inst->setLimits(lim);
        RunResult res = r.inst->callEvent("Go");
        CHECK(res.state == TaskState::Failed && res.error.find("a string of 1024 bytes; this application allows 1000") != std::string::npos);
        CHECK_EQ(res.errorNode, cat);
        CHECK_EQ(r.inst->getVariable("S").str().size(), size_t(512));
    }
    // time: an endless While loop without a step limit ends when its time is up
    {
        GoGraph t;
        NodeId wl = t.node("flow.while", {{"Condition", Value(true)}});
        t.link(t.ev, "Then", wl, "In");
        GoRun r(rt, t.s);
        r.inst->setStepLimit(0);
        Instance::Limits lim;
        lim.maxTaskSeconds = 0.05;
        r.inst->setLimits(lim);
        const auto t0 = std::chrono::steady_clock::now();
        RunResult res = r.inst->callEvent("Go");
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(res.state == TaskState::Failed && res.error.find("Time limit") != std::string::npos);
        CHECK(took >= 0.05 && took < 2.0);
        // the time of the slices adds up, even when each slice is shorter than the interval of the clock checks
        auto task = r.inst->startEvent("Go", {});
        int slices = 0;
        while (task && !task->done() && slices < 100000) { task->run(500); ++slices; }
        CHECK(task && task->state() == TaskState::Failed && slices > 1);
    }
    // nodes the application does not allow are compile errors
    {
        GoGraph t;
        t.link(t.ev, "Then", t.say("hi"), "In");
        CompileOptions opts;
        opts.allowNode = [](const NodeDef& d) { return d.id != "util.print"; };
        auto prog = rt.compile(t.s, opts);
        CHECK(!prog->ok());
        CHECK(!prog->diagnostics.empty() && prog->diagnostics[0].message == "'Print String' is not allowed in this application");
        CHECK(rt.compile(t.s)->ok());
    }
}

// ---- hot reload ----------------------------------------------------------------------------------------
TEST(hot_reload_keeps_the_state_of_the_script) {
    TestRuntime rt;
    // version 1: Go -> Count = Count + 1 -> Do Once -> Print "first"
    GoGraph t;
    t.s.variables.push_back({"Count", "int", Value(0), "", ""});
    t.s.variables.push_back({"Name", "string", Value("a"), "", ""});
    t.s.variables.push_back({"Old", "int", Value(0), "", ""});
    t.s.variables.push_back({"Mood", "string", Value("x"), "", ""});
    NodeId add = t.node("math.add", {{"B", Value(1)}});
    t.link(t.var("var.get", "Count"), "Value", add, "A");
    NodeId set = t.var("var.set", "Count");
    t.link(add, "Result", set, "Value");
    t.link(t.ev, "Then", set, "In");
    NodeId once = t.node("flow.doonce");
    t.link(set, "Then", once, "In");
    t.link(once, "Completed", t.say("first"), "In");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    CHECK_EQ(r.go(), std::string("first,"));
    r.go();
    r.go();
    r.inst->setVariable("Name", Value("kept"));
    CHECK_EQ(r.inst->getVariable("Count").toInt(), int64_t(3));

    // version 2: Count is a float now and goes up by 10, Old is gone, New is new, Mood changed to a type its value cannot become
    Script v2 = t.s;
    v2.variables[0].type = "float";
    v2.variables[0].def = Value(0.0);
    v2.variables.erase(v2.variables.begin() + 2);
    v2.variables[2].type = "array<int>";
    v2.variables[2].def = Value();
    v2.variables.push_back({"New", "int", Value(42), "", ""});
    v2.graphs[0].findNode(add)->defaults["B"] = Value(10.0);
    auto p2 = build(rt, v2);
    CHECK(p2->ok());
    ReloadReport rep = r.inst->reload(p2);
    CHECK(rep.ok());
    CHECK(rep.kept == std::vector<std::string>{"Name"});
    CHECK(rep.converted == std::vector<std::string>{"Count"});
    CHECK(rep.reset == std::vector<std::string>{"Mood"});
    CHECK(rep.added == std::vector<std::string>{"New"});
    CHECK(rep.removed == std::vector<std::string>{"Old"});
    CHECK(rep.toText().find("converted: Count") != std::string::npos);
    CHECK(&r.inst->program() == p2.get());
    CHECK_EQ(r.inst->getVariable("Name").toString(), std::string("kept"));
    CHECK_EQ(r.inst->getVariable("New").toInt(), int64_t(42));
    CHECK_EQ(r.go(), std::string(""));                             // the Do Once still remembers that it fired
    CHECK(r.inst->getVariable("Count").isFloat());
    CHECK_EQ(r.inst->getVariable("Count").toFloat(), 13.0);

    // a program with errors is refused and nothing changes
    Script broken = v2;
    broken.graphs[0].findNode(set)->props["name"] = "Nope";
    ReloadReport bad = r.inst->reload(rt.compile(broken));
    CHECK(!bad.ok() && bad.toText().find("compile errors") != std::string::npos);
    CHECK(&r.inst->program() == p2.get());

    // tasks of the old program that wait are aborted, and can still be inspected
    GoGraph d;
    d.s.variables.push_back({"Count", "float", Value(0.0), "", ""});
    NodeId delay = d.node("flow.delay", {{"Duration", Value(5.0)}});
    d.link(d.ev, "Then", delay, "In");
    d.link(delay, "Then", d.say("late"), "In");
    auto pd = build(rt, d.s);
    ReloadReport toDelay = r.inst->reload(pd);
    CHECK(toDelay.ok() && toDelay.abortedTasks == 0);
    CHECK_EQ(r.inst->getVariable("Count").toFloat(), 13.0);
    RunResult waiting = r.inst->callEvent("Go");
    CHECK(waiting.waiting());
    ReloadReport back = r.inst->reload(p2);
    CHECK(back.ok() && back.abortedTasks == 1);
    CHECK(waiting.task->state() == TaskState::Aborted);
    CHECK_EQ(waiting.task->callStack().size(), size_t(1));          // its frames still point into the old program, which it keeps
    r.inst->tick(10);
    CHECK_EQ(r.log.size(), size_t(0));

    // Executor and AsyncExecutor: the aborted tasks are reported, the events posted afterwards run the new program
    {
        Executor ex(pd);
        std::vector<TaskState> done;
        ex.setCompletionHandler([&](const Executor::Completion& c) { done.push_back(c.result.state); });
        ex.postEvent("Go");
        ex.pump(0);
        CHECK(ex.reload(p2).abortedTasks == 1);
        CHECK(done.size() == 1 && done[0] == TaskState::Aborted);
        ex.postEvent("Go");
        ex.pump(0);
        CHECK(done.size() == 2 && done[1] == TaskState::Finished);
        CHECK_EQ(ex.instance().getVariable("Count").toFloat(), 10.0);
    }
    {
        AsyncExecutor ax(pd);
        ax.postEvent("Go");
        ReloadReport ar = ax.reload(p2);
        CHECK(ar.ok());
        ax.postEvent("Go");
        CHECK(ax.waitIdle(5000));
        Value c;
        ax.inspect([&](Executor& e) { c = e.instance().getVariable("Count"); });
        CHECK_EQ(c.toFloat(), 10.0);
    }
}

int main(int argc, char** argv) {
    g_exeDir = std::filesystem::path(argv[0]).parent_path().string();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (auto& t : tests()) {
        int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("  EXCEPTION: %s\n", e.what());
        }
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
