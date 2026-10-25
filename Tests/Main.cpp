// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// Minimal test runner: TEST(name) { CHECK(cond); }
#include <FlowGraph/Encoding.hpp>
#include <FlowGraph/Runtime.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <filesystem>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <filesystem>
#include <fstream>
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
    CHECK_EQ(j["version"].asInt(), int64_t(1));                     // without structs: version 1, which older FlowGraph versions read
    Script withStruct = s;
    withStruct.structs.push_back({"Point", "", "", {{"X", "int", Value(), ""}}});
    CHECK_EQ(withStruct.toJson()["version"].asInt(), int64_t(Script::kFileVersion));
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
    bool hasAdd = false;
    for (auto& t : fit) {
        hasAdd |= t.defId == "math.add";
        if (t.defId == "flow.foreach") {      // its Array needs an array: an int can only go to Descending (a bool accepts an int)
            std::string pin;
            CHECK(templateAccepts(rt.registry(), t, f, &pin) && pin == "Descending");
        }
    }
    CHECK(hasAdd);
    // every value converts to a string: offered for the primary types; for the others (a class, an enum) only by Print String and
    // the nodes that take any value
    auto offers = [&](const TypeDesc* type, const char* id) {
        PinFilter d;
        d.kind = PinKind::Data; d.dir = PinDir::Out; d.type = type;
        for (auto& t : filterPalette(rt.registry(), all, d))
            if (t.defId == id) return true;
        return false;
    };
    CHECK(offers(rt.registry().tInt(), "fn:String.To Upper") && offers(rt.registry().tInt(), "util.print"));
    for (const char* type : {"Dog", "Weekday"}) {
        const TypeDesc* td = rt.registry().findType(type);
        CHECK(td != nullptr);
        if (!td) continue;
        CHECK(offers(td, "util.print"));
        CHECK(offers(td, "fn:String.To String"));
        CHECK(!offers(td, "fn:String.To Upper"));
    }
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
    std::string path = g_exeDir + "/plugins/FileSystem" + kLibExt;
    if (!std::filesystem::exists(path)) { std::printf("  (skipped: %s not built)\n", path.c_str()); return; }
    TestRuntime rt;
    std::string err;
    CHECK(rt.loadPlugin(path, &err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(rt.registry().findType("File") != nullptr);
    CHECK(rt.registry().findNode("m:File.Is At End") != nullptr);
    CHECK_EQ(rt.registry().findNode("fn:FileSystem.File Name")->source, std::string("FileSystem"));
    rt.registry().addEvent("OnPath", {{"Path", "string"}});      // the application's event (a plugin declares none)
    Script s;
    s.variables.push_back({"Name", "string", Value(""), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:OnPath").id;
    NodeId name = g.addNode("fn:FileSystem.File Name").id;
    NodeId set = g.addNode("var.set").id;
    g.findNode(set)->props = {{"name", "Name"}, {"scope", "global"}};
    link(g, ev, "Then", set, "In");
    link(g, ev, "Path", name, "Path");
    link(g, name, "Name", set, "Value");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK(inst.callEvent("OnPath", {Value("notes/today.txt")}).ok);
    CHECK_EQ(inst.getVariable("Name").toString(), std::string("today.txt"));
}

TEST(plugin_translation_catalog) {
    std::string path = g_exeDir + "/plugins/FileSystem" + kLibExt;
    if (!std::filesystem::exists(path)) { std::printf("  (skipped: %s not built)\n", path.c_str()); return; }
    Runtime rt;                                 // the nodes the plugin's catalog is made with (flow --only)
    standard::registerStandardApp(rt.registry());
    CHECK(rt.loadPlugin(path));
    CHECK_EQ(rt.loadedPluginFiles().size(), rt.loadedPlugins().size());
    // a plugin's catalog: its texts only (no built-in node, no template)
    auto has = [](const std::vector<std::pair<std::string, std::string>>& texts, const std::string& key) {
        for (auto& t : texts)
            if (t.first == key) return true;
        return false;
    };
    const auto texts = translatableTexts(rt.registry(), "FileSystem");
    CHECK(has(texts, "node:fn:FileSystem.Join Path"));
    CHECK(has(texts, "node:m:File.Is At End"));
    CHECK(has(texts, "pin:filesystem.open/Mode"));
    CHECK(!has(texts, "node:flow.branch"));
    CHECK(!has(texts, "template:get-variable"));
    CHECK(has(translatableTexts(rt.registry()), "node:fn:FileSystem.Join Path"));   // the whole registry has them too
    // its Persian catalog is next to the library (the build copies it there), up to date and translated
    const std::string catalog = (std::filesystem::path(rt.loadedPluginFiles().front()).parent_path() / "translations" / "FileSystem_fa.json").string();
    std::ifstream in(catalog, std::ios::binary);
    CHECK(bool(in));
    std::stringstream text;
    text << in.rdbuf();
    std::string lf = text.str();                            // a Windows checkout may have made its line ends CRLF
    lf.erase(std::remove(lf.begin(), lf.end(), '\r'), lf.end());
    CatalogReport report;
    CHECK(updateCatalog(rt.registry(), lf, "fa", &report, "FileSystem") == lf);
    CHECK(report.texts > 0 && report.translated == report.texts);
    Translations t;
    CHECK(t.loadFile(catalog));
    CHECK(t.nodeTitle(rt.registry().findNode("fn:FileSystem.Join Path"), {}, "Join Path") != "Join Path");
    CHECK(t.pin("filesystem.open", "Mode") != "Mode");
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
    CHECK(rt.registry().findType("File") != nullptr);
    size_t libraries = 0;                   // the standard plugins (FileSystem): each once
    for (auto& e : std::filesystem::directory_iterator(dir)) libraries += e.path().extension() == kLibExt;
    CHECK_EQ(rt.loadedPlugins().size(), libraries);
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
    CHECK(rt.registry().findFunction("Test.Legacy Plugin Registered") == nullptr);
    CHECK(rt.registry().findFunction("Test.Mismatch Plugin Registered") == nullptr);
    CHECK(rt.loadedPlugins().empty());
    // A plugin that declares an event is refused: the events are the application's
    const std::string events = g_exeDir + "/testplugins/AbiPlugin_event" + kLibExt;
    CHECK(!rt.loadPlugin(events, &err));
    CHECK(err.find("declares the event 'PluginEvent'") != std::string::npos);
    CHECK(rt.registry().findEvent("PluginEvent") == nullptr);
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

// ---- For Each in descending order, Print Error, time ---------------------------------------------------
// Runs Go -> [node, when it is an exec node] -> Set V0 = out0 -> Set V1 = out1 ...: the values of the given outputs of one node.
static std::vector<Value> nodeOutputs(Runtime& rt, const std::string& def, std::map<std::string, Value> defaults, const std::vector<std::string>& outs) {
    GoGraph t;
    NodeId n = t.node(def, std::move(defaults));
    NodeId prev = t.ev;
    std::string prevPin = "Then";
    if (!rt.registry().findNode(def)->pure) {
        t.link(t.ev, "Then", n, "In");
        prev = n;
    }
    for (size_t i = 0; i < outs.size(); ++i) {
        const std::string v = "V" + std::to_string(i);
        t.s.variables.push_back({v, "any", Value(), "", ""});
        NodeId set = t.var("var.set", v.c_str());
        t.link(n, outs[i], set, "Value");
        t.link(prev, prevPin, set, "In");
        prev = set;
    }
    GoRun r(rt, t.s);
    std::vector<Value> out;
    if (!r.prog->ok()) { std::printf("  %s", dumpDiagnostics(*r.prog).c_str()); return out; }
    std::string log = r.go();
    if (log.rfind("error:", 0) == 0) { std::printf("  %s\n", log.c_str()); return out; }
    for (size_t i = 0; i < outs.size(); ++i) out.push_back(r.inst->getVariable("V" + std::to_string(i)));
    return out;
}

TEST(for_each_runs_in_descending_order) {
    TestRuntime rt;
    {   // a copy without the intrinsic: the run function must do exactly what the interpreter does inline
        NodeDef copy = *rt.registry().findNode("flow.foreach");
        copy.id = "test.foreach";
        copy.intrinsic = Intrinsic::None;
        rt.registry().addNode(std::move(copy));
    }
    // Go -> For Each(A, Descending = D) { Print(Element:Index); Desc = !Desc (a change while it runs is not seen); Break at "stop" }
    for (const char* def : {"flow.foreach", "test.foreach"}) {
        GoGraph t;
        t.s.variables.push_back({"A", "array<string>", Value(), "", ""});
        t.s.variables.push_back({"D", "bool", Value(false), "", ""});
        NodeId fe = t.node(def);
        t.link(t.var("var.get", "A"), "Value", fe, "Array");
        t.link(t.var("var.get", "D"), "Value", fe, "Descending");
        NodeId fmt = t.g->addNode("string.format").id;
        t.g->findNode(fmt)->inputs = {{"E", "string", false, false, Value()}, {"I", "int", false, false, Value()}};
        t.g->findNode(fmt)->defaults["Format"] = Value("{E}{I}");
        t.link(fe, "Element", fmt, "E");
        t.link(fe, "Index", fmt, "I");
        NodeId pr = t.node("util.print");
        t.link(fmt, "Result", pr, "String");
        NodeId notD = t.node("logic.not");
        t.link(t.var("var.get", "D"), "Value", notD, "Value");
        NodeId flip = t.var("var.set", "D");
        t.link(notD, "Result", flip, "Value");
        NodeId isStop = t.node("cmp.eq", {{"B", Value("stop")}});
        t.link(fe, "Element", isStop, "A");
        NodeId br = t.node("flow.branch");
        t.link(isStop, "Result", br, "Condition");
        t.link(t.ev, "Then", fe, "In");
        t.link(fe, "Loop Body", pr, "In");
        t.link(pr, "Then", flip, "In");
        t.link(flip, "Then", br, "In");
        t.link(br, "True", t.node("flow.break"), "In");
        t.link(fe, "Completed", t.say("done"), "In");
        GoRun r(rt, t.s);
        CHECK(r.prog->ok());
        auto setArray = [&](std::vector<const char*> items) {
            Value a = rt.registry().makeArray(rt.registry().tString());
            for (auto* s : items) a.arrayPtr()->items.push_back(Value(s));
            r.inst->setVariable("A", a);
        };
        setArray({"a", "b", "c"});
        r.inst->setVariable("D", Value(false));
        CHECK_EQ(r.go(), std::string("a0,b1,c2,done,"));
        r.inst->setVariable("D", Value(true));
        CHECK_EQ(r.go(), std::string("c2,b1,a0,done,"));              // Index is the position in the array
        r.inst->setVariable("D", Value(true));
        setArray({"a", "stop", "c"});
        CHECK_EQ(r.go(), std::string("c2,stop1,done,"));              // Break goes on with Completed
        setArray({});
        CHECK_EQ(r.go(), std::string("done,"));
    }
    // For Each Entry, keys from the greatest to the smallest
    {
        GoGraph t;
        t.s.variables.push_back({"M", "map<string,int>", Value(), "", ""});
        NodeId fe = t.node("flow.foreachmap", {{"Descending", Value(true)}});
        t.link(t.var("var.get", "M"), "Value", fe, "Map");
        NodeId pr = t.node("util.print");
        t.link(fe, "Key", pr, "String");
        t.link(t.ev, "Then", fe, "In");
        t.link(fe, "Loop Body", pr, "In");
        GoRun r(rt, t.s);
        Value m = rt.registry().makeMap(rt.registry().tString(), rt.registry().tInt());
        for (const char* k : {"b", "d", "a", "c"}) m.mapPtr()->items[Value(k)] = Value(1);
        r.inst->setVariable("M", m);
        CHECK_EQ(r.go(), std::string("d,c,b,a,"));
        r.inst->setVariable("M", rt.registry().makeMap(rt.registry().tString(), rt.registry().tInt()));
        CHECK_EQ(r.go(), std::string(""));
    }
    // Print Error writes an error to the log; the script goes on
    {
        GoGraph t;
        NodeId e = t.node("util.error", {{"String", Value("disk full")}});
        t.link(t.ev, "Then", e, "In");
        t.link(e, "Then", t.say("after"), "In");
        GoRun r(rt, t.s);
        std::vector<std::pair<LogLevel, std::string>> lines;
        r.inst->setLogHandler([&](LogLevel l, const std::string& m) { lines.push_back({l, m}); });
        CHECK(r.inst->callEvent("Go").ok);
        CHECK(lines.size() == 2 && lines[0].first == LogLevel::Error && lines[0].second == "disk full" && lines[1].first == LogLevel::Info);
    }
}

TEST(time_nodes_convert_dates_and_read_the_clocks) {
    TestRuntime rt;
    const std::vector<std::string> fields = {"Year", "Month", "Day", "Hour", "Minute", "Second", "Millisecond", "Weekday", "Day Of Year"};
    auto broken = [&](double ts, bool local) {
        std::string s;
        for (auto& v : nodeOutputs(rt, "time.break", {{"Timestamp", Value(ts)}, {"Local", Value(local)}}, fields)) s += std::to_string(v.toInt()) + " ";
        return s;
    };
    auto made = [&](int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s, int64_t ms, bool local) {
        auto v = nodeOutputs(rt, "time.make", {{"Year", Value(y)}, {"Month", Value(mo)}, {"Day", Value(d)}, {"Hour", Value(h)}, {"Minute", Value(mi)},
                                               {"Second", Value(s)}, {"Millisecond", Value(ms)}, {"Local", Value(local)}}, {"Timestamp"});
        return v.empty() ? -1.0 : v[0].toFloat();
    };
    // UTC: known instants (2024-02-29 was a Thursday, the 60th day of the year; 2000-01-01 a Saturday)
    CHECK_EQ(made(1970, 1, 1, 0, 0, 0, 0, false), 0.0);
    CHECK_EQ(made(2000, 1, 1, 0, 0, 0, 0, false), 946684800.0);
    CHECK_EQ(made(2024, 2, 29, 12, 0, 0, 0, false), 1709208000.0);
    CHECK_EQ(broken(1709208000.25, false), std::string("2024 2 29 12 0 0 250 4 60 "));
    CHECK_EQ(broken(946684800, false), std::string("2000 1 1 0 0 0 0 6 1 "));
    CHECK_EQ(broken(-0.5, false), std::string("1969 12 31 23 59 59 500 3 365 "));     // before 1970
    // out of range values carry over
    CHECK_EQ(made(2026, 13, 1, 0, 0, 0, 0, false), made(2027, 1, 1, 0, 0, 0, 0, false));
    CHECK_EQ(made(2024, 3, 0, 0, 0, 0, 0, false), made(2024, 2, 29, 0, 0, 0, 0, false));
    CHECK_EQ(made(2024, 1, 1, 0, 90, 0, 0, false), made(2024, 1, 1, 1, 30, 0, 0, false));
    // local time: whatever the time zone of the machine, Make and Break agree
    const double localTs = made(2026, 7, 4, 9, 15, 30, 125, true);
    CHECK_EQ(broken(localTs, true).substr(0, 21), std::string("2026 7 4 9 15 30 125 "));
    // the Weekday output is an enum: it prints by name
    auto wd = nodeOutputs(rt, "time.break", {{"Timestamp", Value(1709208000.0)}, {"Local", Value(false)}}, {"Weekday"});
    CHECK(!wd.empty() && rt.registry().toString(wd[0], rt.registry().findType("Weekday")) == "Thursday");

    // format: every code, unknown codes stay as they are
    auto format = [&](double ts, const char* f) {
        auto v = nodeOutputs(rt, "fn:Time.Format Date Time", {{"Timestamp", Value(ts)}, {"Format", Value(f)}, {"Local", Value(false)}}, {"Result"});
        return v.empty() ? std::string("?") : v[0].toString();
    };
    CHECK_EQ(format(1709208000.042, "%F %T.%f"), std::string("2024-02-29 12:00:00.042"));
    CHECK_EQ(format(1709208000, "%a %A %b %B %j %u %y %I %p %z %% %Q"), std::string("Thu Thursday Feb February 060 4 24 12 PM +0000 % %Q"));
    CHECK_EQ(format(1709208000 - 13 * 3600, "%I %p"), std::string("11 PM"));
    // parse: ISO 8601 with and without a zone
    auto parse = [&](const char* text, bool local) {
        auto v = nodeOutputs(rt, "time.parse", {{"Text", Value(text)}, {"Local", Value(local)}}, {"Timestamp", "Valid"});
        return v.size() == 2 && v[1].toBool() ? v[0].toFloat() : -1.0;
    };
    CHECK_EQ(parse("2024-02-29T12:00:00Z", true), 1709208000.0);
    CHECK_EQ(parse("2024-02-29 14:00:00+02:00", true), 1709208000.0);
    CHECK_EQ(parse("2024-02-29T07:30-0430", true), 1709208000.0);
    CHECK_EQ(parse("2024-02-29T12:00:00.250Z", true), 1709208000.25);
    CHECK_EQ(parse("2024-02-29", false), 1709164800.0);
    CHECK_EQ(parse("2026-07-04 09:15:30.125", true), localTs);         // no zone: local time
    for (const char* bad : {"2024-02-30", "2023-02-29", "2024-13-01", "2024-02-29T24:00", "2024-02-29 12:00 tomorrow", "yesterday", ""})
        CHECK_EQ(parse(bad, true), -1.0);

    // the clocks: Now is the system time, Seconds Since Start a steady clock of the instance; both are exec nodes (one reading per run)
    CHECK(!rt.registry().findNode("fn:Time.Now")->pure && !rt.registry().findNode("fn:Time.Seconds Since Start")->pure);
    auto now = nodeOutputs(rt, "fn:Time.Now", {}, {"Timestamp"});
    CHECK(!now.empty() && std::fabs(now[0].toFloat() - static_cast<double>(std::time(nullptr))) < 5.0);
    auto since = nodeOutputs(rt, "fn:Time.Seconds Since Start", {}, {"Seconds"});
    CHECK(!since.empty() && since[0].toFloat() >= 0.0 && since[0].toFloat() < 5.0);
}

// ---- enum literals, date ranges, Delay and links, loops ----------------------------------------------
TEST(enum_literals_show_their_value) {
    TestRuntime rt;
    GoGraph t;
    t.s.variables.push_back({"W", "Weekday", Value(), "", ""});
    Node& lit = t.g->addNode("enum:Weekday");
    lit.props["value"] = "Friday";
    const NodeId litId = lit.id;
    const NodeId plain = t.g->addNode("enum:Weekday").id;                // no value chosen yet: the first one
    NodeId sw = t.node("flow.switch.enum", {}, {{"enum", "Weekday"}});
    NodeId set = t.var("var.set", "W");
    t.link(litId, "Value", set, "Value");
    t.link(t.ev, "Then", set, "In");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    const CompiledGraph& cg = r.prog->graphs[0];
    CHECK_EQ(cg.findNode(litId)->title, std::string("Weekday.Friday"));
    CHECK_EQ(cg.findNode(plain)->title, std::string("Weekday.Monday"));
    CHECK_EQ(cg.findNode(sw)->title, std::string("Switch on Weekday"));
    r.go();
    CHECK_EQ(r.inst->getVariable("W").toInt(), int64_t(5));
    const NodeDef* def = rt.registry().findNode("enum:Weekday");
    CHECK(def && def->settings.size() == 1 && def->settings[0].kind == NodeSetting::Kind::Choice && def->settings[0].choices.size() == 7 &&
          def->settings[0].choices[4] == "Friday");
}

// A catalog translates what editors show of nodes; a translation made for another English text (the node changed since) is not used;
// titles made from names keep the names; updateCatalog adds new texts, drops removed ones and keeps the translations.
TEST(node_translations_catalogs) {
    TestRuntime rt;
    const Registry& reg = rt.registry();
    Translations t;
    std::string error;
    CHECK(t.loadJson(R"({"language": "fa", "texts": {
        "node:flow.branch": ["Branch", "شاخه"],
        "pin:flow.branch/Condition": ["Condition", "شرط"],
        "node:flow.delay": ["Old Delay", "تأخیر"],
        "category:Flow": ["Flow", "جریان"],
        "enum:Weekday.Friday": ["Friday", "جمعه"],
        "template:get-variable": ["Get {Name}", "خواندن {Name}"],
        "pin:flow.sequence/Then {N}": ["Then {N}", "سپس {N}"],
        "node:util.print": ["Print String", ""] }})", &error));
    CHECK_EQ(t.language(), std::string("fa"));
    CHECK_EQ(t.nodeTitle(reg.findNode("flow.branch"), {}, "Branch"), std::string("شاخه"));
    CHECK_EQ(t.pin("flow.branch", "Condition"), std::string("شرط"));
    CHECK_EQ(t.pin("flow.branch", "True"), std::string("True"));                       // no translation: English
    CHECK_EQ(t.nodeTitle(reg.findNode("flow.delay"), {}, "Delay"), std::string("Delay"));   // made for "Old Delay": outdated
    CHECK_EQ(t.nodeTitle(reg.findNode("util.print"), {}, "Print String"), std::string("Print String"));   // empty: none
    CHECK_EQ(t.category("Flow|Switch"), std::string("جریان|Switch"));
    CHECK_EQ(t.enumValue("Weekday", "Friday"), std::string("جمعه"));
    CHECK_EQ(t.nodeTitle(reg.findNode("var.get"), {{"name", "Score"}}, "Get Score"), std::string("خواندن Score"));   // the name stays
    CHECK_EQ(t.nodeTitle(reg.findNode("enum:Weekday"), {{"value", "Friday"}}, "Weekday.Friday"), std::string("Weekday.جمعه"));
    // pins a node numbers from a setting share one text; the cases of a Switch on Enum are its enum's values
    CHECK_EQ(t.pin("flow.sequence", "Then 3"), std::string("سپس 3"));
    CHECK_EQ(t.pin(reg.findNode("flow.switch.enum"), {{"enum", "Weekday"}}, "Friday"), std::string("جمعه"));
    CHECK(!t.loadJson("[1, 2]", &error) && !error.empty());

    // a catalog for a registry: every text once, the translations kept, the outdated reported, the removed dropped
    const auto texts = translatableTexts(reg);
    CHECK(texts.size() > 200);
    std::set<std::string> keys;
    for (auto& [k, en] : texts) CHECK(keys.insert(k).second && !en.empty());
    CHECK(keys.count("node:flow.branch") && keys.count("pin:flow.branch/True") && keys.count("enum:Weekday.Sunday") && keys.count("category:Math"));
    CHECK(keys.count("pin:flow.sequence/Then {N}") && !keys.count("pin:flow.sequence/Then 0"));
    // a pin name more than one node has is one text (pin:*/), not one per node
    CHECK(keys.count("pin:*/In") && keys.count("pin:*/Then") && keys.count("pin:*/Default") && keys.count("pin:*/Loop Body"));
    CHECK(!keys.count("pin:flow.branch/In") && !keys.count("pin:flow.switch.int/Default") && !keys.count("pin:flow.for/Loop Body"));
    Translations shared;
    const std::string sharedCatalog = R"json({"texts": {"pin:*/In": ["In", "ورود"], "pin:flow.branch/In": ["In", "ورودی شاخه"],
        "pin:*/Local#doc": ["Local time (on) or UTC (off)", "وقت محلی"]}})json";
    CHECK(shared.loadJson(sharedCatalog));
    CHECK_EQ(shared.pin("flow.for", "In"), std::string("ورود"));                        // the shared text
    CHECK_EQ(shared.pin("flow.branch", "In"), std::string("ورودی شاخه"));              // a node's own wins
    CHECK_EQ(shared.pinDoc("fn:Time.Make", "Local", "Local time (on) or UTC (off)"), std::string("وقت محلی"));
    CHECK_EQ(shared.pinDoc("fn:Other", "Local", "Something else"), std::string("Something else"));   // another English text
    CatalogReport report;
    const std::string first = updateCatalog(reg, "", "fa", &report);
    CHECK(report.texts == texts.size() && report.added == texts.size() && report.translated == 0);
    std::string edited = first;
    const std::string branch = "\"node:flow.branch\": [\"Branch\", \"\"]";
    CHECK(edited.find(branch) != std::string::npos);
    edited.replace(edited.find(branch), branch.size(), "\"node:flow.branch\": [\"Branch\", \"شاخه\"], \"node:gone\": [\"Gone\", \"x\"]");
    const std::string delay = "\"node:flow.delay\": [\"Delay\", \"\"]";           // a translation of what the node was called before
    CHECK(edited.find(delay) != std::string::npos);
    edited.replace(edited.find(delay), delay.size(), "\"node:flow.delay\": [\"Old Delay\", \"تأخیر\"]");
    const std::string second = updateCatalog(reg, edited, "fa", &report);
    CHECK_EQ(report.translated, size_t(1));
    CHECK_EQ(report.removed, size_t(1));
    CHECK_EQ(report.outdated, size_t(1));
    CHECK(second.find("\"node:flow.branch\": [\"Branch\", \"شاخه\"]") != std::string::npos);
    CHECK(second.find("node:gone") == std::string::npos);
    Translations back;
    CHECK(back.loadJson(second, &error));
    CHECK_EQ(back.nodeTitle(reg.findNode("flow.branch"), {}, "Branch"), std::string("شاخه"));
    CHECK_EQ(back.nodeTitle(reg.findNode("flow.delay"), {}, "Delay"), std::string("Delay"));
}

// The string nodes count characters (Unicode code points), not bytes: a character is a string of length 1 in every language.
TEST(string_nodes_count_characters) {
    TestRuntime rt;
    const std::string fa = "سلام دنیا";                  // 9 characters, 17 bytes
    const std::string emoji = "a\xF0\x9F\x98\x80" "b";      // a, 😀 (4 bytes), b: 3 characters
    auto one = [&](const char* def, std::map<std::string, Value> in, const char* out) {
        auto v = nodeOutputs(rt, def, std::move(in), {out});
        return v.empty() ? Value() : v[0];
    };
    CHECK_EQ(one("fn:String.Length", {{"String", Value(fa)}}, "Result").toInt(), int64_t(9));
    CHECK_EQ(one("fn:String.Byte Length", {{"String", Value(fa)}}, "Result").toInt(), int64_t(17));
    CHECK_EQ(one("fn:String.Length", {{"String", Value(emoji)}}, "Result").toInt(), int64_t(3));
    CHECK_EQ(one("fn:String.Length", {{"String", Value("hello")}}, "Result").toInt(), int64_t(5));
    CHECK_EQ(one("fn:String.Substring", {{"String", Value(fa)}, {"Start", Value(int64_t(5))}, {"Length", Value(int64_t(4))}}, "Result").str(),
             std::string("دنیا"));
    CHECK_EQ(one("fn:String.Substring", {{"String", Value(fa)}, {"Start", Value(int64_t(2))}, {"Length", Value(int64_t(-1))}}, "Result").str(),
             std::string("ام دنیا"));
    CHECK_EQ(one("fn:String.Substring", {{"String", Value(fa)}, {"Start", Value(int64_t(9))}, {"Length", Value(int64_t(2))}}, "Result").str(),
             std::string());
    CHECK_EQ(one("fn:String.Substring", {{"String", Value(emoji)}, {"Start", Value(int64_t(1))}, {"Length", Value(int64_t(1))}}, "Result").str(),
             std::string("\xF0\x9F\x98\x80"));
    CHECK_EQ(one("fn:String.Find", {{"String", Value(fa)}, {"Substring", Value("دنیا")}, {"Start", Value(int64_t(0))}}, "Index").toInt(), int64_t(5));
    CHECK_EQ(one("fn:String.Find", {{"String", Value(fa)}, {"Substring", Value("ا")}, {"Start", Value(int64_t(3))}}, "Index").toInt(), int64_t(8));
    CHECK_EQ(one("fn:String.Find", {{"String", Value(fa)}, {"Substring", Value("x")}, {"Start", Value(int64_t(0))}}, "Index").toInt(), int64_t(-1));
    CHECK_EQ(one("fn:String.Find", {{"String", Value(fa)}, {"Substring", Value("")}, {"Start", Value(int64_t(9))}}, "Index").toInt(), int64_t(9));
    CHECK_EQ(one("fn:String.Find", {{"String", Value(fa)}, {"Substring", Value("")}, {"Start", Value(int64_t(10))}}, "Index").toInt(), int64_t(-1));
    CHECK_EQ(one("fn:String.Character At", {{"String", Value(fa)}, {"Index", Value(int64_t(1))}}, "Character").str(), std::string("ل"));
    CHECK_EQ(one("fn:String.Character At", {{"String", Value(fa)}, {"Index", Value(int64_t(9))}}, "Character").str(), std::string());
    CHECK_EQ(one("fn:String.Character At", {{"String", Value(fa)}, {"Index", Value(int64_t(-1))}}, "Character").str(), std::string());
    Value chars = one("fn:String.To Characters", {{"String", Value(fa)}}, "Characters");
    CHECK(chars.arrayPtr() && chars.arrayPtr()->items.size() == 9);
    if (chars.arrayPtr() && chars.arrayPtr()->items.size() == 9) CHECK(chars.arrayPtr()->items[0].str() == "س" && chars.arrayPtr()->items[4].str() == " ");
    // text that is not valid UTF-8 is cut the same way by every node, and no byte is lost: the characters join to the original
    const std::string broken = "\x80" "a\xC3" "b\xE2\x82" "\xA9\xA9";
    Value parts = one("fn:String.To Characters", {{"String", Value(broken)}}, "Characters");
    std::string joined;
    if (auto* a = parts.arrayPtr()) for (auto& p : a->items) joined += p.str();
    CHECK(joined == broken);
    CHECK_EQ(one("fn:String.Length", {{"String", Value(broken)}}, "Result").toInt(),
             parts.arrayPtr() ? static_cast<int64_t>(parts.arrayPtr()->items.size()) : int64_t(-1));
}

// For Each Character goes through every character once, with its index; Break ends it.
TEST(for_each_character_loops_over_the_characters) {
    TestRuntime rt;
    GoGraph t;
    t.s.variables.push_back({"Out", "string", Value(""), "", ""});
    t.s.variables.push_back({"Last", "int", Value(int64_t(-1)), "", ""});
    NodeId fe = t.node("string.foreachchar", {{"String", Value("سلام!")}});
    t.link(t.ev, "Then", fe, "In");
    // Out = Concat(Character, Out): the characters in reverse order; Last = Index
    NodeId cat = t.node("fn:String.Concat");
    t.link(fe, "Character", cat, "A");
    t.link(t.var("var.get", "Out"), "Value", cat, "B");
    NodeId setOut = t.var("var.set", "Out");
    t.link(fe, "Loop Body", setOut, "In");
    t.link(cat, "Result", setOut, "Value");
    NodeId setLast = t.var("var.set", "Last");
    t.link(setOut, "Then", setLast, "In");
    t.link(fe, "Index", setLast, "Value");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) return;
    CHECK_EQ(r.go(), std::string());
    CHECK_EQ(r.inst->getVariable("Out").str(), std::string("!مالس"));
    CHECK_EQ(r.inst->getVariable("Last").toInt(), int64_t(4));
    CHECK_EQ(rt.registry().findNode("string.foreachchar")->breakPin, 0);
}

// An enum's default is its first value (Weekday starts with Monday = 1, so 0 is not one of its values), and a default that is not one
// of the enum's values - a value of the type a variable had before - becomes that first value instead of a bare number.
TEST(enum_defaults_are_values_of_the_enum) {
    TestRuntime rt;
    registerSample(rt);
    const Registry& reg = rt.registry();
    const TypeDesc* weekday = reg.findType("Weekday");
    const TypeDesc* mood = reg.findType("Mood");
    CHECK(weekday && mood);
    if (!weekday || !mood) return;
    CHECK_EQ(reg.defaultValue(weekday).toInt(), int64_t(1));         // Monday
    CHECK_EQ(reg.defaultValue(mood).toInt(), int64_t(0));            // Happy
    Script s;
    s.variables.push_back({"Day", "Weekday", Value(), "", ""});
    s.variables.push_back({"Feeling", "Mood", Value(int64_t(7)), "", ""});     // a Sunday, from when it was a Weekday
    Graph& g = s.addEventGraph();
    Node& set = g.addNode("var.set");
    set.props = {{"name", "Feeling"}, {"scope", "global"}};
    set.defaults["Value"] = Value(int64_t(7));
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    CHECK_EQ(inst.getVariable("Day").toInt(), int64_t(1));
    CHECK_EQ(inst.getVariable("Feeling").toInt(), int64_t(0));
    bool checked = false;
    for (auto& p : buildNodePins(set, reg, &s, &g))
        if (p.name == "Value") { CHECK_EQ(p.def.toInt(), int64_t(0)); checked = true; }      // the editor shows Happy, not 7
    CHECK(checked);
}

// Changing a variable's type clears its default and the values typed into its Set nodes (they were values of the old type); a local
// variable of the same name, and other variables, keep theirs.
TEST(retyping_a_variable_clears_the_values_of_its_old_type) {
    Script s;
    s.variables.push_back({"Day", "Weekday", Value(int64_t(7)), "", ""});
    s.variables.push_back({"Other", "Weekday", Value(int64_t(3)), "", ""});
    Graph& events = s.addEventGraph();
    // addNode / addFunction may move the nodes and graphs: no reference is kept past the next one, only ids and names
    NodeId dayId = 0, otherId = 0;
    {
        Node& setDay = events.addNode("var.set");
        setDay.props = {{"name", "Day"}, {"scope", "global"}};
        setDay.defaults["Value"] = Value(int64_t(7));
        dayId = setDay.id;
    }
    {
        Node& setOther = events.addNode("var.set");
        setOther.props = {{"name", "Other"}, {"scope", "global"}};
        setOther.defaults["Value"] = Value(int64_t(3));
        otherId = setOther.id;
    }
    const std::string eventGraph = events.name;
    Graph& fn = s.addFunction("F");
    fn.locals.push_back({"Day", "Weekday", Value(int64_t(5)), "", ""});        // shadows the global one in F
    Node& setLocal = fn.addNode("var.set");
    setLocal.props = {{"name", "Day"}, {"scope", "local"}};
    setLocal.defaults["Value"] = Value(int64_t(5));
    const NodeId localId = setLocal.id;

    s.retypeVariable("Day", "Mood");
    CHECK_EQ(s.findVariable("Day")->type, std::string("Mood"));
    CHECK(s.findVariable("Day")->def.isNull());
    CHECK(!s.findGraph(eventGraph)->findNode(dayId)->defaults.count("Value"));
    CHECK(s.findGraph(eventGraph)->findNode(otherId)->defaults.count("Value"));
    CHECK_EQ(s.findVariable("Other")->def.toInt(), int64_t(3));
    CHECK(s.findGraph("F")->findNode(localId)->defaults.count("Value"));     // the local one is another variable
    CHECK_EQ(s.findGraph("F")->findLocal("Day")->def.toInt(), int64_t(5));

    s.retypeLocal("F", "Day", "int");
    CHECK_EQ(s.findGraph("F")->findLocal("Day")->type, std::string("int"));
    CHECK(s.findGraph("F")->findLocal("Day")->def.isNull());
    CHECK(!s.findGraph("F")->findNode(localId)->defaults.count("Value"));
}

TEST(make_date_time_carries_over_and_has_a_range) {
    TestRuntime rt;
    auto made = [&](int64_t y, int64_t mo, int64_t d, int64_t h) {
        return nodeOutputs(rt, "time.make", {{"Year", Value(y)}, {"Month", Value(mo)}, {"Day", Value(d)}, {"Hour", Value(h)}, {"Local", Value(false)}},
                           {"Timestamp"});
    };
    auto a = made(2024, 1, 1, 0), b = made(2024, 1, 1, 10000);
    CHECK(a.size() == 1 && b.size() == 1 && b[0].toFloat() - a[0].toFloat() == 10000.0 * 3600);     // 416 days and 16 hours later
    // beyond a million years: a script error, not an overflow
    GoGraph t;
    t.s.variables.push_back({"T", "float", Value(0.0), "", ""});
    NodeId mk = t.node("time.make", {{"Hour", Value(std::numeric_limits<int64_t>::max())}});
    NodeId set = t.var("var.set", "T");
    t.link(mk, "Timestamp", set, "Value");
    t.link(t.ev, "Then", set, "In");
    GoRun r(rt, t.s);
    RunResult res = r.inst->callEvent("Go");
    CHECK(!res.ok && res.error.find("out of range") != std::string::npos);
    t.g->findNode(mk)->defaults = {{"Year", Value(int64_t(5000000))}};
    GoRun r2(rt, t.s);
    CHECK(r2.inst->callEvent("Go").error.find("out of range") != std::string::npos);
}

TEST(a_delay_follows_its_link_when_the_wait_is_over) {
    TestRuntime rt;
    struct Links : DebugHooks {
        std::vector<std::pair<int, int>> fired;          // node indices
        void onExecLink(Task&, const CompiledGraph&, int from, int, int to) override { fired.push_back({from, to}); }
    } hooks;
    GoGraph t;
    NodeId d = t.node("flow.delay", {{"Duration", Value(0.5)}});
    NodeId p = t.say("after");
    t.link(t.ev, "Then", d, "In");
    t.link(d, "Then", p, "In");
    GoRun r(rt, t.s);
    r.inst->setDebugHooks(&hooks);
    const CompiledGraph& cg = r.prog->graphs[0];
    const int di = cg.nodeIndex.at(d), pi = cg.nodeIndex.at(p);
    RunResult res = r.inst->callEvent("Go");
    CHECK(res.waiting());
    CHECK(hooks.fired.size() == 1 && hooks.fired[0].second == di);          // only the link into the Delay
    CHECK_EQ(res.task->currentNode(), d);                                  // the task waits AT the Delay
    CHECK(r.log.empty());
    r.inst->tick(0.2);
    CHECK(hooks.fired.size() == 1 && r.log.empty());
    r.inst->tick(0.4);
    CHECK(hooks.fired.size() == 2 && hooks.fired[1] == std::make_pair(di, pi));   // now the Delay's link, then the Print
    CHECK(r.log.size() == 1 && res.task->state() == TaskState::Finished);
}

TEST(loops_without_a_way_out_and_data_cycles_are_found) {
    TestRuntime rt;
    // Go -> P1 -> P2 -> P3 (-> P1?)   and a Branch / a Delay variant
    GoGraph t;
    NodeId p1 = t.say("1"), p2 = t.say("2"), p3 = t.say("3");
    t.link(t.ev, "Then", p1, "In");
    t.link(p1, "Then", p2, "In");
    t.link(p2, "Then", p3, "In");
    auto cg = [&](const Script& s) { return rt.compile(s); };
    auto prog = cg(t.s);
    const CompiledGraph& g = prog->graphs[0];
    auto idx = [&](NodeId id) { return g.nodeIndex.at(id); };
    CHECK(isPlainExecNode(*g.findNode(p1)));
    CHECK(closesEndlessExecLoop(g, idx(p3), idx(p1)));                     // P3 -> P1 would loop for ever
    CHECK(!closesEndlessExecLoop(g, idx(p3), idx(t.ev)));
    CHECK(!closesEndlessExecLoop(g, idx(p1), idx(p3)));                    // P3 does not lead back to P1
    auto warned = [&](const Script& s) {
        const auto compiled = cg(s);            // kept: a range-for over cg(s)->diagnostics would outlive the temporary program
        for (auto& d : compiled->diagnostics)
            if (d.severity == Diagnostic::Severity::Warning && d.message.find("no way out") != std::string::npos) return true;
        return false;
    };
    CHECK(!warned(t.s));
    Script loop = t.s;
    loop.graphs[0].connections.push_back({p3, "Then", p1, "In"});
    CHECK(warned(loop));
    // a Branch in the loop is a way out, a Delay makes it wait: both are fine
    for (const char* def : {"flow.branch", "flow.delay"}) {
        GoGraph w;
        NodeId a = w.say("a"), x = w.node(def), b = w.say("b");
        w.link(w.ev, "Then", a, "In");
        w.link(a, "Then", x, "In");
        w.link(x, std::string(def) == "flow.branch" ? "True" : "Then", b, "In");
        auto pw = cg(w.s);
        CHECK(!closesEndlessExecLoop(pw->graphs[0], pw->graphs[0].nodeIndex.at(b), pw->graphs[0].nodeIndex.at(a)));
        w.link(b, "Then", a, "In");
        CHECK(!warned(w.s));
    }
    // data: Add1 -> Add2 -> Add1 would be a value made of itself; an exec node in the way breaks the cycle
    GoGraph d;
    d.s.variables.push_back({"X", "int", Value(0), "", ""});
    NodeId add1 = d.node("math.add"), add2 = d.node("math.add");
    d.link(add1, "Result", add2, "A");
    NodeId set = d.var("var.set", "X");
    d.link(add2, "Result", set, "Value");
    auto pd = cg(d.s);
    const CompiledGraph& dg = pd->graphs[0];
    CHECK(closesPureDataCycle(dg, dg.nodeIndex.at(add2), dg.nodeIndex.at(add1)));
    CHECK(!closesPureDataCycle(dg, dg.nodeIndex.at(add1), dg.nodeIndex.at(add2)));       // already that way round: no cycle
    CHECK(!closesPureDataCycle(dg, dg.nodeIndex.at(set), dg.nodeIndex.at(add1)));        // an exec node's output is a stored value
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
    // For 0..100: Add(A, Index) / Add(M, Index, Index)
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

// ---- System nodes (opt-in): Run Command, For Each Output Line, Operating System -------------------------------
bool processAlive(long pid);         // ProcessProbe.cpp

namespace {
struct SystemRuntime : TestRuntime {
    SystemRuntime() { registerSystemNodes(registry()); }
};
std::string helperPath() {           // Tests/ProcessHelper.cpp
#if defined(_WIN32)
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    return std::filesystem::absolute(g_exeDir + "/FlowGraphProcessHelper" + ext).string();
}
std::string quotedHelper() {         // for a shell command line
#if defined(_WIN32)
    return "\"" + helperPath() + "\"";
#else
    return "'" + helperPath() + "'";
#endif
}
// Runs Go to its end, ticking with the real time (at most `seconds`); `each` is called after every tick.
RunResult goToEnd(GoRun& r, double seconds = 20, const std::function<void()>& each = {}) {
    RunResult res = r.inst->callEvent("Go");
    const auto start = std::chrono::steady_clock::now();
    while (res.task && !res.task->done() && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        r.inst->tick(0.002);
        if (each) each();
    }
    return res.task ? res.task->result() : res;
}
bool diesSoon(long pid) {
    for (int i = 0; i < 200; ++i) {
        if (!processAlive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
long readPid(const std::filesystem::path& file) {
    FILE* f = std::fopen(file.string().c_str(), "r");
    if (!f) return 0;
    long pid = 0;
    if (std::fscanf(f, "%ld", &pid) != 1) pid = 0;
    std::fclose(f);
    return pid;
}

// Go -> Run Command(defaults, Arguments = Args) -> V0..V3 = Exit Code, Output, Error Output, Timed Out. `error`: the script error.
struct RunCommand {
    Value code, out, err, timedOut;
    std::string error;
    bool waitedAtFirst = false;
    double seconds = 0;
    std::vector<std::string> log;
};
RunCommand runCommand(Runtime& rt, std::map<std::string, Value> defaults, const std::vector<std::string>& args) {
    GoGraph t;
    t.s.variables.push_back({"Args", "array<string>", Value(), "", ""});
    const char* outs[] = {"Exit Code", "Output", "Error Output", "Timed Out"};
    NodeId run = t.node("system.run", std::move(defaults));
    t.link(t.ev, "Then", run, "In");
    t.link(t.var("var.get", "Args"), "Value", run, "Arguments");
    NodeId prev = run;
    for (int i = 0; i < 4; ++i) {
        const std::string v = "V" + std::to_string(i);
        t.s.variables.push_back({v, "any", Value(), "", ""});
        NodeId set = t.var("var.set", v.c_str());
        t.link(run, outs[i], set, "Value");
        t.link(prev, "Then", set, "In");
        prev = set;
    }
    GoRun r(rt, t.s);
    RunCommand rc;
    if (!r.prog->ok()) { rc.error = dumpDiagnostics(*r.prog); return rc; }
    r.inst->setVariable("Args", standard::argsValue(rt.registry(), args));
    const auto start = std::chrono::steady_clock::now();
    RunResult first = r.inst->callEvent("Go");
    rc.waitedAtFirst = first.waiting();
    RunResult res = first;
    while (res.task && !res.task->done() && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        r.inst->tick(0.002);
        res = res.task->result();
    }
    rc.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    rc.log = r.log;
    if (!res.ok) { rc.error = res.error; return rc; }
    rc.code = r.inst->getVariable("V0");
    rc.out = r.inst->getVariable("V1");
    rc.err = r.inst->getVariable("V2");
    rc.timedOut = r.inst->getVariable("V3");
    return rc;
}
} // namespace

TEST(system_nodes_are_opt_in) {
    TestRuntime bare;
    CHECK(!bare.registry().findNode("system.run"));
    CHECK(!bare.registry().findNode("system.lines"));
    CHECK(!bare.registry().findNode("fn:System.Operating System"));
    CHECK(!bare.registry().findType("OperatingSystem"));
    TestRuntime standardApp;                   // flow and flowstudio register them with the standard application
    standard::registerStandardApp(standardApp.registry());
    for (const char* id : {"system.run", "system.lines", "fn:System.Operating System"}) {
        const NodeDef* d = standardApp.registry().findNode(id);
        CHECK(d && d->source == "system" && d->category == "System" && d->waits == (std::string(id) != "fn:System.Operating System"));
    }
    CHECK_EQ(standardApp.registry().source(), std::string("core"));      // what is registered next is not tagged "system"
    CHECK_EQ(standardApp.registry().findNode("system.lines")->breakPin, 0);
    // and an application that registers them can still keep a script from using them
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Go").id;
    NodeId run = g.addNode("system.run").id;
    link(g, ev, "Then", run, "In");
    CompileOptions opts;
    opts.allowNode = [](const NodeDef& d) { return d.source != "system"; };
    auto p = compile(s, standardApp.registry(), opts);
    CHECK(!p->ok());
}

TEST(operating_system_names_the_platform) {
    SystemRuntime rt;
    auto v = nodeOutputs(rt, "fn:System.Operating System", {}, {"System"});
    CHECK_EQ(v.size(), size_t(1));
#if defined(_WIN32)
    const int64_t expected = 0;
#elif defined(__APPLE__)
    const int64_t expected = 2;
#elif defined(__linux__)
    const int64_t expected = 1;
#else
    const int64_t expected = 3;
#endif
    if (!v.empty()) CHECK_EQ(v[0].toInt(), expected);
    CHECK(rt.registry().findNode("fn:System.Operating System")->pure);
}

TEST(run_command_hands_out_output_error_output_and_exit_code) {
    SystemRuntime rt;
    const Value helper(helperPath());
    // arguments go to the program as they are: spaces, quotes, empty ones, trailing backslashes
    auto a = runCommand(rt, {{"Program", helper}}, {"args", "a b", "say \"hi\"", "", "back\\slash\\", "h\xC3\xA9"});
    CHECK_EQ(a.error, std::string());
    CHECK_EQ(a.code.toInt(), int64_t(0));
    CHECK_EQ(a.out.str(), std::string("[a b]\n[say \"hi\"]\n[]\n[back\\slash\\]\n[h\xC3\xA9]\n"));      // \n line breaks everywhere
    CHECK_EQ(a.err.str(), std::string());
    CHECK(!a.timedOut.toBool());
    CHECK(a.waitedAtFirst);                    // the task waits for the program; the call returned at once
    auto e = runCommand(rt, {{"Program", helper}}, {"exit", "3"});
    CHECK_EQ(e.code.toInt(), int64_t(3));
    auto err = runCommand(rt, {{"Program", helper}}, {"err", "oops"});
    CHECK_EQ(err.out.str(), std::string("out\n"));
    CHECK_EQ(err.err.str(), std::string("oops\n"));
    auto u = runCommand(rt, {{"Program", helper}}, {"utf8"});
    CHECK_EQ(u.out.str(), std::string("h\xC3\xA9llo \xE2\x9C\x93\n"));
    // the working directory
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    auto w = runCommand(rt, {{"Program", helper}, {"Working Directory", Value(dir.string())}}, {"cwd"});
    std::string printed = w.out.isString() ? w.out.str() : std::string();
    while (!printed.empty() && (printed.back() == '\n' || printed.back() == '\r')) printed.pop_back();
    std::error_code ec;
    CHECK(!printed.empty() && std::filesystem::equivalent(printed, dir, ec));
    // more than 16 MiB: cut, with a warning
    auto big = runCommand(rt, {{"Program", helper}}, {"big", std::to_string((16 << 20) + 100000)});
    CHECK_EQ(big.out.isString() ? big.out.str().size() : size_t(0), size_t(16 << 20));
    CHECK(!big.log.empty() && big.log.back().find("16 MiB") != std::string::npos);
}

TEST(run_command_times_out_fails_to_start_and_uses_the_shell) {
    SystemRuntime rt;
    const Value helper(helperPath());
    auto t = runCommand(rt, {{"Program", helper}, {"Timeout", Value(0.3)}}, {"sleep", "10"});
    CHECK(t.timedOut.toBool());
    CHECK_EQ(t.code.toInt(), int64_t(-1));
    CHECK(t.seconds < 5);
    auto fast = runCommand(rt, {{"Program", helper}, {"Timeout", Value(10.0)}}, {"sleep", "0.05"});
    CHECK(!fast.timedOut.toBool() && fast.out.str() == "slept\n");
    // a program that cannot be started is a script error
    auto missing = runCommand(rt, {{"Program", Value("flowgraph-no-such-program")}}, {});
    CHECK(missing.error.find("cannot run 'flowgraph-no-such-program'") != std::string::npos);
    auto noDir = runCommand(rt, {{"Program", helper}, {"Working Directory", Value("/flowgraph/no/such/dir")}}, {"cwd"});
    CHECK(noDir.error.find("does not exist") != std::string::npos);
    auto empty = runCommand(rt, {}, {});
    CHECK(empty.error.find("no program given") != std::string::npos);
    // the shell: its redirections work, the arguments are appended quoted
    auto sh = runCommand(rt, {{"Program", Value(quotedHelper() + " err boom 2>&1")}, {"Use Shell", Value(true)}}, {});
    CHECK_EQ(sh.error, std::string());
    CHECK(sh.out.isString() && sh.out.str().find("boom") != std::string::npos && sh.out.str().find("out") != std::string::npos);
    CHECK_EQ(sh.err.isString() ? sh.err.str() : std::string("?"), std::string());
    auto shArgs = runCommand(rt, {{"Program", Value(quotedHelper())}, {"Use Shell", Value(true)}}, {"args", "x y"});
    CHECK_EQ(shArgs.out.isString() ? shArgs.out.str() : std::string(), std::string("[x y]\n"));
    auto shExit = runCommand(rt, {{"Program", Value("exit 4")}, {"Use Shell", Value(true)}}, {});
    CHECK_EQ(shExit.code.toInt(), int64_t(4));
}

TEST(for_each_output_line_streams_the_lines_as_they_come) {
    SystemRuntime rt;
    // Go -> For Each Output Line { Print(Line) or ErrLine = Line } Completed -> Code = Exit Code
    GoGraph t;
    t.s.variables.push_back({"Args", "array<string>", Value(), "", ""});
    t.s.variables.push_back({"Code", "int", Value(99), "", ""});
    t.s.variables.push_back({"ErrLine", "string", Value(""), "", ""});
    NodeId fe = t.node("system.lines", {{"Program", Value(helperPath())}});
    t.link(t.ev, "Then", fe, "In");
    t.link(t.var("var.get", "Args"), "Value", fe, "Arguments");
    NodeId br = t.node("flow.branch");
    t.link(fe, "Loop Body", br, "In");
    t.link(fe, "From Error Output", br, "Condition");
    NodeId pr = t.node("util.print");
    t.link(br, "False", pr, "In");
    t.link(fe, "Line", pr, "String");
    NodeId setErr = t.var("var.set", "ErrLine");
    t.link(br, "True", setErr, "In");
    t.link(fe, "Line", setErr, "Value");
    NodeId setCode = t.var("var.set", "Code");
    t.link(fe, "Completed", setCode, "In");
    t.link(fe, "Exit Code", setCode, "Value");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) return;
    r.inst->setVariable("Args", standard::argsValue(rt.registry(), {"lines", "4", "150"}));
    bool sawPartial = false;            // a line was handled while the program still ran
    RunResult res = goToEnd(r, 20, [&] { if (r.log.size() == 1 && r.inst->pendingTaskCount() == 1) sawPartial = true; });
    CHECK(res.ok);
    CHECK(sawPartial);
    CHECK_EQ(r.log.size(), size_t(4));
    if (r.log.size() == 4) CHECK(r.log[0] == "line 1" && r.log[3] == "line 4");       // without "\r" on Windows
    CHECK_EQ(r.inst->getVariable("Code").toInt(), int64_t(0));
    // the error output is marked; the exit code comes when the program has ended
    r.log.clear();
    r.inst->setVariable("Args", standard::argsValue(rt.registry(), {"err", "boom"}));
    res = goToEnd(r);
    CHECK(res.ok);
    CHECK_EQ(r.log.size(), size_t(1));
    if (!r.log.empty()) CHECK_EQ(r.log[0], std::string("out"));
    CHECK_EQ(r.inst->getVariable("ErrLine").toString(), std::string("boom"));
    r.log.clear();
    r.inst->setVariable("Args", standard::argsValue(rt.registry(), {"exit", "7"}));
    res = goToEnd(r);
    CHECK(res.ok && r.log.empty());
    CHECK_EQ(r.inst->getVariable("Code").toInt(), int64_t(7));
}

TEST(a_break_an_abort_and_a_reload_kill_the_program_of_for_each_output_line) {
    SystemRuntime rt;
    const auto pidFile = std::filesystem::temp_directory_path() / ("flowgraph_test_forever_" + std::to_string(std::time(nullptr)) + ".pid");
    // Go -> For Each Output Line(forever) { N = N + 1; Branch(N >= 3) -> Break } Completed -> Code = Exit Code
    for (bool shell : {false, true}) {         // with the shell the program is the shell's child: it dies too
        GoGraph t;
        t.s.variables.push_back({"N", "int", Value(0), "", ""});
        t.s.variables.push_back({"Code", "int", Value(99), "", ""});
        t.s.variables.push_back({"Args", "array<string>", Value(), "", ""});
        NodeId fe = t.node("system.lines", {{"Program", Value(shell ? quotedHelper() : helperPath())}, {"Use Shell", Value(shell)}});
        t.link(t.ev, "Then", fe, "In");
        t.link(t.var("var.get", "Args"), "Value", fe, "Arguments");
        NodeId getN = t.var("var.get", "N");
        NodeId inc = t.node("math.add", {{"B", Value(1)}});
        t.link(getN, "Value", inc, "A");
        NodeId setN = t.var("var.set", "N");
        t.link(fe, "Loop Body", setN, "In");
        t.link(inc, "Result", setN, "Value");
        NodeId ge = t.node("cmp.ge", {{"B", Value(3)}});
        t.link(getN, "Value", ge, "A");
        NodeId br = t.node("flow.branch");
        t.link(setN, "Then", br, "In");
        t.link(ge, "Result", br, "Condition");
        NodeId brk = t.node("flow.break");
        t.link(br, "True", brk, "In");
        NodeId setCode = t.var("var.set", "Code");
        t.link(fe, "Completed", setCode, "In");
        t.link(fe, "Exit Code", setCode, "Value");
        GoRun r(rt, t.s);
        CHECK(r.prog->ok());
        if (!r.prog->ok()) return;
        r.inst->setVariable("Args", standard::argsValue(rt.registry(), {"forever", pidFile.string()}));

        std::filesystem::remove(pidFile);
        RunResult res = goToEnd(r);
        CHECK(res.ok);
        CHECK_EQ(r.inst->getVariable("N").toInt(), int64_t(3));
        CHECK_EQ(r.inst->getVariable("Code").toInt(), int64_t(-1));      // ended by the Break, not by the program
        long pid = readPid(pidFile);
        CHECK(pid > 0 && diesSoon(pid));

        // aborted while it waits for a line, with the task still held (a RunResult, a debugger): the program dies now
        r.inst->setVariable("N", Value(-1000000));
        std::filesystem::remove(pidFile);
        RunResult held = r.inst->callEvent("Go");
        for (int i = 0; i < 500 && readPid(pidFile) == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            r.inst->tick(0.01);
        }
        pid = readPid(pidFile);
        CHECK(pid > 0 && processAlive(pid));
        CHECK(held.task && !held.task->done());
        r.inst->abortAll();
        CHECK(pid > 0 && diesSoon(pid));

        // a hot reload aborts the waiting task: its program dies too
        std::filesystem::remove(pidFile);
        held = r.inst->callEvent("Go");
        for (int i = 0; i < 500 && readPid(pidFile) == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            r.inst->tick(0.01);
        }
        pid = readPid(pidFile);
        CHECK(pid > 0);
        CHECK(r.inst->reload(r.prog).abortedTasks == 1);
        CHECK(pid > 0 && diesSoon(pid));
    }
    std::filesystem::remove(pidFile);
}

// ---- the FileSystem plugin (Plugins/FileSystem) ----------------------------------------------------------------
namespace {
struct FileSystemRuntime : TestRuntime {
    bool loaded = false;
    FileSystemRuntime() {
        const std::string path = g_exeDir + "/plugins/FileSystem" + kLibExt;
        if (!std::filesystem::exists(path)) { std::printf("  (skipped: %s not built)\n", path.c_str()); return; }
        std::string err;
        loaded = loadPlugin(path, &err);
        if (!err.empty()) std::printf("  %s\n", err.c_str());
        CHECK(loaded);
    }
};
std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string u8text(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
// A folder of its own for a test, removed at its end.
struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(const char* name)
        : path(std::filesystem::temp_directory_path() / (std::string("flowgraph_test_") + name + "_" + std::to_string(std::time(nullptr)))) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::string operator[](const std::string& rel) const { return u8text((path / u8path(rel)).make_preferred()); }
};
std::string fileText(const std::string& path) {
    std::ifstream in(u8path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
void writeRaw(const std::string& path, const std::string& text) { std::ofstream(u8path(path), std::ios::binary) << text; }
std::vector<std::string> strings(const Value& v) {
    std::vector<std::string> out;
    if (auto* a = v.arrayPtr())
        for (auto& item : a->items) out.push_back(item.toString());
    return out;
}
Value fnCall(Runtime& rt, const std::string& id, std::vector<Value> args) {
    const FunctionDesc* fd = rt.registry().findFunction(id);
    CHECK(fd != nullptr);
    return fd ? fd->invoke(args.data()) : Value();
}

// Go -> the node (defaults) -> Then: V0.. = its outputs, then prints "then"; Failed: prints "failed". `out` has the outputs and
// Error; `way` is "then" or "failed"; `error` a script error.
struct FsRun {
    std::string way, error;
    std::map<std::string, Value> out;
};
FsRun fsNode(Runtime& rt, const char* id, std::map<std::string, Value> defaults, std::vector<std::pair<std::string, std::string>> outputs = {}) {
    GoGraph t;
    NodeId n = t.node(id, std::move(defaults));
    t.link(t.ev, "Then", n, "In");
    outputs.emplace_back("Error", "string");
    NodeId prev = n;
    for (size_t i = 0; i < outputs.size(); ++i) {
        const std::string v = "V" + std::to_string(i);
        t.s.variables.push_back({v, outputs[i].second, Value(), "", ""});
        NodeId set = t.var("var.set", v.c_str());
        t.link(n, outputs[i].first, set, "Value");
        t.link(prev, "Then", set, "In");
        prev = set;
    }
    t.link(prev, "Then", t.say("then"), "In");
    t.s.variables.push_back({"FailError", "string", Value(""), "", ""});
    NodeId setError = t.var("var.set", "FailError");
    t.link(n, "Failed", setError, "In");
    t.link(n, "Error", setError, "Value");
    t.link(setError, "Then", t.say("failed"), "In");
    GoRun r(rt, t.s);
    FsRun fr;
    if (!r.prog->ok()) { fr.error = dumpDiagnostics(*r.prog); return fr; }
    RunResult res = goToEnd(r);
    if (!res.ok) { fr.error = res.error; return fr; }
    for (auto& l : r.log) fr.way += l;
    for (size_t i = 0; i < outputs.size(); ++i) fr.out[outputs[i].first] = r.inst->getVariable("V" + std::to_string(i));
    if (fr.way == "failed") fr.out["Error"] = r.inst->getVariable("FailError");
    return fr;
}
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
} // namespace

TEST(filesystem_paths_and_questions) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    namespace fs = std::filesystem;
    CHECK_EQ(rt.registry().findNode("filesystem.open")->source, std::string("FileSystem"));
    {   // dragging a File: its own nodes and Print String, not every string node
        Script s;
        Graph& g = s.addEventGraph();
        NodeBuildCtx ctx{rt.registry(), &s, &g};
        PinFilter f;
        f.kind = PinKind::Data; f.dir = PinDir::Out; f.type = rt.registry().findType("File");
        std::set<std::string> ids;
        for (auto& t : filterPalette(rt.registry(), buildPalette(ctx), f)) ids.insert(t.defId);
        CHECK(ids.count("filesystem.file.writeline") && ids.count("m:File.Is At End") && ids.count("util.print"));
        CHECK(!ids.count("fn:String.To Upper") && !ids.count("fn:FileSystem.Join Path"));
    }
    auto text = [&](const char* id, std::vector<Value> args) { return fnCall(rt, std::string("FileSystem.") + id, std::move(args)).toString(); };
    CHECK_EQ(text("Join Path", {Value("notes"), Value("today.txt")}), (fs::path("notes") / "today.txt").string());
    CHECK_EQ(text("Join Path", {Value(""), Value("today.txt")}), std::string("today.txt"));
    CHECK_EQ(text("Parent Folder", {Value("notes/today.txt")}), std::string("notes"));
    CHECK_EQ(text("Parent Folder", {Value("today.txt")}), std::string(""));
    CHECK_EQ(text("File Name", {Value("notes/today.txt")}), std::string("today.txt"));
    CHECK_EQ(text("Extension", {Value("notes/today.txt")}), std::string(".txt"));
    CHECK_EQ(text("Extension", {Value("notes/README")}), std::string(""));
    CHECK_EQ(text("Change Extension", {Value("notes/today.txt"), Value(".md")}), std::string("notes/today.md"));
    CHECK_EQ(text("Change Extension", {Value("notes/today.txt"), Value("")}), std::string("notes/today"));
    CHECK_EQ(text("Absolute Path", {Value("x/../y")}), (fs::current_path() / "y").string());
    CHECK_EQ(text("Current Folder", {}), fs::current_path().string());
    CHECK_EQ(text("Temp Folder", {}), fs::temp_directory_path().string());
    CHECK(!text("Home Folder", {}).empty());

    TempFolder dir("paths");
    const std::string file = dir["h\xC3\xA9 \xD9\x81.txt"];         // a name with letters of other languages (UTF-8)
    writeRaw(file, "hello");
    auto ask = [&](const char* id, const std::string& path) { return fnCall(rt, std::string("FileSystem.") + id, {Value(path)}); };
    CHECK(ask("Exists", file).toBool() && ask("Is File", file).toBool() && !ask("Is Folder", file).toBool());
    CHECK(ask("Exists", u8text(dir.path)).toBool() && ask("Is Folder", u8text(dir.path)).toBool());
    CHECK_EQ(ask("File Size", file).toInt(), int64_t(5));
    const double changed = ask("Last Modified", file).toFloat();
    CHECK(std::abs(changed - double(std::time(nullptr))) < 600);
    const std::string missing = dir["missing.txt"];
    CHECK(!ask("Exists", missing).toBool() && !ask("Is File", missing).toBool() && !ask("Exists", "").toBool());
    CHECK_EQ(ask("File Size", missing).toInt(), int64_t(-1));
    CHECK_EQ(ask("File Size", u8text(dir.path)).toInt(), int64_t(-1));      // a folder has no size
    CHECK_EQ(ask("Last Modified", missing).toFloat(), -1.0);
}

TEST(filesystem_copies_moves_deletes_and_lists) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    namespace fs = std::filesystem;
    TempFolder dir("files");
    auto run = [&](const char* id, std::map<std::string, Value> defaults) { return fsNode(rt, id, std::move(defaults)).way; };
    CHECK_EQ(run("filesystem.mkdir", {{"Path", Value(dir["a/b/c"])}}), std::string("then"));
    CHECK(fs::is_directory(u8path(dir["a/b/c"])));
    CHECK_EQ(run("filesystem.mkdir", {{"Path", Value(dir["a/b/c"])}}), std::string("then"));       // already there
    writeRaw(dir["a/x.txt"], "x");
    writeRaw(dir["a/b/y.md"], "y");
    writeRaw(dir["a/b/c/z.txt"], "z");
    CHECK_EQ(run("filesystem.mkdir", {{"Path", Value(dir["a/x.txt"])}}), std::string("failed"));    // a file is there

    // copy: a folder with all it holds, a file; Overwrite
    CHECK_EQ(run("filesystem.copy", {{"From", Value(dir["a"])}, {"To", Value(dir["copy"])}}), std::string("then"));
    CHECK_EQ(fileText(dir["copy/b/c/z.txt"]), std::string("z"));
    CHECK_EQ(run("filesystem.copy", {{"From", Value(dir["a"])}, {"To", Value(dir["copy"])}}), std::string("failed"));
    CHECK_EQ(run("filesystem.copy", {{"From", Value(dir["a"])}, {"To", Value(dir["copy"])}, {"Overwrite", Value(true)}}), std::string("then"));
    CHECK_EQ(run("filesystem.copy", {{"From", Value(dir["a/x.txt"])}, {"To", Value(dir["x2.txt"])}}), std::string("then"));
    CHECK_EQ(fileText(dir["x2.txt"]), std::string("x"));
    CHECK_EQ(run("filesystem.copy", {{"From", Value(dir["nothing"])}, {"To", Value(dir["x3.txt"])}}), std::string("failed"));

    // move and rename
    CHECK_EQ(run("filesystem.move", {{"From", Value(dir["x2.txt"])}, {"To", Value(dir["moved.txt"])}}), std::string("then"));
    CHECK(!fs::exists(u8path(dir["x2.txt"])) && fileText(dir["moved.txt"]) == "x");
    CHECK_EQ(run("filesystem.move", {{"From", Value(dir["copy/x.txt"])}, {"To", Value(dir["moved.txt"])}}), std::string("failed"));
    CHECK_EQ(run("filesystem.move", {{"From", Value(dir["copy/x.txt"])}, {"To", Value(dir["moved.txt"])}, {"Overwrite", Value(true)}}),
             std::string("then"));
    CHECK_EQ(run("filesystem.move", {{"From", Value(dir["x2.txt"])}, {"To", Value(dir["x4.txt"])}}), std::string("failed"));

    // list: folders and files apart, in name order; patterns; recursive
    const std::vector<std::pair<std::string, std::string>> lists = {{"Files", "array<string>"}, {"Folders", "array<string>"}};
    FsRun l = fsNode(rt, "filesystem.list", {{"Folder", Value(dir["a"])}}, lists);
    CHECK_EQ(l.way, std::string("then"));
    CHECK(strings(l.out["Files"]) == std::vector<std::string>({dir["a/x.txt"]}));
    CHECK(strings(l.out["Folders"]) == std::vector<std::string>({dir["a/b"]}));
    l = fsNode(rt, "filesystem.list", {{"Folder", Value(dir["a"])}, {"Pattern", Value("*.txt")}, {"Recursive", Value(true)}}, lists);
    CHECK(strings(l.out["Files"]) == std::vector<std::string>({dir["a/b/c/z.txt"], dir["a/x.txt"]}));
    CHECK(strings(l.out["Folders"]).empty());
    l = fsNode(rt, "filesystem.list", {{"Folder", Value(dir["a"])}, {"Pattern", Value("*.md;?.TXT")}, {"Recursive", Value(true)}}, lists);
#if defined(_WIN32)
    CHECK_EQ(strings(l.out["Files"]).size(), size_t(3));           // letter case does not count on Windows
#else
    CHECK(strings(l.out["Files"]) == std::vector<std::string>({dir["a/b/y.md"]}));
#endif
    l = fsNode(rt, "filesystem.list", {{"Folder", Value(dir["nothing"])}}, lists);
    CHECK(l.way == "failed" && has(l.out["Error"].toString(), "does not exist"));

    // delete: a file, an empty folder; a full one only with Recursive
    CHECK_EQ(run("filesystem.delete", {{"Path", Value(dir["moved.txt"])}}), std::string("then"));
    CHECK(!fs::exists(u8path(dir["moved.txt"])));
    CHECK_EQ(run("filesystem.delete", {{"Path", Value(dir["moved.txt"])}}), std::string("failed"));
    CHECK_EQ(run("filesystem.delete", {{"Path", Value(dir["copy"])}}), std::string("failed"));
    CHECK(fs::exists(u8path(dir["copy/b/y.md"])));
    CHECK_EQ(run("filesystem.delete", {{"Path", Value(dir["copy"])}, {"Recursive", Value(true)}}), std::string("then"));
    CHECK(!fs::exists(u8path(dir["copy"])));
    CHECK_EQ(run("filesystem.mkdir", {{"Path", Value(dir["empty"])}}), std::string("then"));
    CHECK_EQ(run("filesystem.delete", {{"Path", Value(dir["empty"])}}), std::string("then"));
}

TEST(filesystem_for_each_file_runs_the_body_for_each_entry) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("foreach");
    std::filesystem::create_directories(u8path(dir["b"]));
    writeRaw(dir["a.txt"], "");
    writeRaw(dir["b/inner.txt"], "");
    writeRaw(dir["c.md"], "");
    // Go -> For Each File(Folder) { Print(File Name(Path) [+ "/" when Is Folder]) [-> Break] } Completed: "done", Failed: "failed"
    auto loop = [&](const std::string& folder, bool recursive, bool breakAtOnce) {
        GoGraph t;
        NodeId fe = t.node("filesystem.foreach", {{"Folder", Value(folder)}, {"Recursive", Value(recursive)}, {"Pattern", Value("*")}});
        t.link(t.ev, "Then", fe, "In");
        NodeId name = t.node("fn:FileSystem.File Name");
        t.link(fe, "Path", name, "Path");
        NodeId pr = t.node("util.print");
        t.link(fe, "Loop Body", pr, "In");
        t.link(name, "Name", pr, "String");
        if (breakAtOnce) t.link(pr, "Then", t.node("flow.break"), "In");
        t.link(fe, "Completed", t.say("done"), "In");
        t.link(fe, "Failed", t.say("failed"), "In");
        GoRun r(rt, t.s);
        CHECK(r.prog->ok());
        if (!r.prog->ok()) return dumpDiagnostics(*r.prog);
        RunResult res = goToEnd(r);
        if (!res.ok) return "error: " + res.error;
        std::string out;
        for (auto& l : r.log) out += l + ",";
        return out;
    };
    CHECK_EQ(loop(u8text(dir.path), false, false), std::string("b,a.txt,c.md,done,"));     // folders first
    CHECK_EQ(loop(u8text(dir.path), true, false), std::string("b,a.txt,inner.txt,c.md,done,"));
    CHECK_EQ(loop(u8text(dir.path), true, true), std::string("b,done,"));                  // Break goes on with Completed
    CHECK_EQ(loop(dir["nothing"], false, false), std::string("failed,"));
    CHECK_EQ(loop(dir["a.txt"], false, false), std::string("failed,"));                    // not a folder
}

TEST(filesystem_a_stopped_copy_leaves_no_half_copied_file) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("stopped");
    const std::string big = dir["big.txt"], copy = dir["copy.txt"];
    writeRaw(big, std::string(48 << 20, 'x'));
    GoGraph t;
    NodeId c = t.node("filesystem.copy", {{"From", Value(big)}, {"To", Value(copy)}});
    t.link(t.ev, "Then", c, "In");
    t.link(c, "Then", t.say("then"), "In");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) return;
    RunResult held = r.inst->callEvent("Go");
    CHECK(held.waiting());                      // the work is on its own thread: the event comes back at once
    r.inst->abortAll();
    // the work stops at its next piece and removes what it copied: the copy is either all there or not at all
    std::error_code ec;
    bool settled = false;
    for (int i = 0; i < 500 && !settled; ++i) {
        const auto size = std::filesystem::file_size(u8path(copy), ec);
        settled = ec || size == (48u << 20);
        if (!settled) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(settled);
    CHECK(r.log.empty());                       // the script did not go on
}

TEST(filesystem_small_operations_in_a_loop_are_quick) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    standard::registerStandardApp(rt.registry());
    TempFolder dir("quick");
    // Main -> For 0..200 { Copy(notes.txt, copy.txt, Overwrite) }: the way flow runs it (runMain sleeps until the next wake)
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event:Main").id;
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults = {{"First", Value(0)}, {"Last", Value(200)}};
    writeRaw(dir["notes.txt"], "x");
    NodeId w = g.addNode("filesystem.copy").id;
    g.findNode(w)->defaults = {{"From", Value(dir["notes.txt"])}, {"To", Value(dir["copy.txt"])}, {"Overwrite", Value(true)}};
    link(g, ev, "Then", loop, "In");
    link(g, loop, "Loop Body", w, "In");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    if (!prog->ok()) return;
    Instance inst(prog);
    const auto start = std::chrono::steady_clock::now();
    auto out = standard::runMain(inst, {});
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(out.ok);
    // quick work goes on at once, not at the next wake of runMain (a sleep of 1 ms takes 15 ms with the default timer of Windows)
    CHECK(seconds < 2.0);
}

namespace {
// Go -> Open File(path, mode) -> Then: the chain `steps` makes (it gets the Open File node and links its own nodes after it,
// returning the last exec node); Failed: prints "failed: <Error>". Returns what was printed, or "error: <script error>".
std::string withFile(Runtime& rt, const std::string& path, int mode, const std::function<NodeId(GoGraph&, NodeId open)>& steps,
                     std::unique_ptr<GoRun>* keep = nullptr) {
    GoGraph t;
    NodeId open = t.node("filesystem.open", {{"Path", Value(path)}, {"Mode", Value(int64_t(mode))}});
    t.link(t.ev, "Then", open, "In");
    NodeId failed = t.node("fn:String.Concat", {{"A", Value("failed: ")}});
    t.link(open, "Error", failed, "B");
    NodeId sayFailed = t.node("util.print");
    t.link(open, "Failed", sayFailed, "In");
    t.link(failed, "Result", sayFailed, "String");
    steps(t, open);
    auto r = std::make_unique<GoRun>(rt, t.s);
    if (!r->prog->ok()) return dumpDiagnostics(*r->prog);
    RunResult res = goToEnd(*r);
    std::string out;
    for (auto& l : r->log) out += l + ",";
    if (!res.ok) out += "error: " + res.error;
    if (keep) *keep = std::move(r);
    return out;
}
// a method of the file: In from `after` (its exec output `pin`), Target = the file of Open File
// a node of the file (Read Line -> filesystem.file.readline): In from `after` (its exec output `pin`), Target = the file of Open File
NodeId method(GoGraph& t, NodeId open, NodeId after, const char* pin, const char* name, std::map<std::string, Value> defaults = {}) {
    std::string id = "filesystem.file.";
    for (const char* c = name; *c; ++c)
        if (*c != ' ') id += char(std::tolower(static_cast<unsigned char>(*c)));
    NodeId m = t.node(id, std::move(defaults));
    t.link(open, "File", m, "Target");
    t.link(after, pin, m, "In");
    return m;
}
enum { kRead = 0, kWrite = 1, kAppend = 2 };
} // namespace

TEST(filesystem_file_writes_and_reads_a_piece_at_a_time) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("file");
    const std::string notes = dir["notes.txt"];
    // Write: it starts empty; Write Line, Write Text, Close
    auto write = [&](int mode, const char* line, const char* text) {
        return withFile(rt, notes, mode, [&](GoGraph& t, NodeId open) {
            NodeId a = method(t, open, open, "Then", "Write Line", {{"Text", Value(line)}});
            NodeId b = method(t, open, a, "Then", "Write Text", {{"Text", Value(text)}});
            NodeId c = method(t, open, b, "Then", "Close");
            NodeId again = method(t, open, c, "Then", "Close");           // closing a closed file does nothing
            t.link(again, "Then", t.say("closed"), "In");
            return again;
        });
    };
    writeRaw(notes, "old");
    CHECK_EQ(write(kWrite, "one", "two"), std::string("closed,"));
    CHECK_EQ(fileText(notes), std::string("one\ntwo"));
    CHECK_EQ(write(kAppend, "", "three"), std::string("closed,"));
    CHECK_EQ(fileText(notes), std::string("one\ntwo\nthree"));
    CHECK_EQ(write(kAppend, "x", "y"), std::string("closed,"));            // Append makes a file that is not there yet
    const std::string fresh = dir["fresh.txt"];
    CHECK_EQ(withFile(rt, fresh, kAppend, [&](GoGraph& t, NodeId open) { return method(t, open, open, "Then", "Write Text", {{"Text", Value("new")}}); }),
             std::string(""));
    CHECK_EQ(fileText(fresh), std::string("new"));
    const std::string named = dir["\xD9\x81\xD8\xA7\xDB\x8C\xD9\x84.txt"];        // a name with letters of another language (UTF-8)
    CHECK_EQ(withFile(rt, named, kWrite, [&](GoGraph& t, NodeId open) { return method(t, open, open, "Then", "Write Text", {{"Text", Value("h\xC3\xA9")}}); }),
             std::string(""));
    CHECK(std::filesystem::exists(u8path(named)) && fileText(named) == "h\xC3\xA9");

    // Read: While not Is At End { Print(Read Line) }, then "end"; without the byte order mark and the \r of the lines
    writeRaw(notes, "\xEF\xBB\xBF" "alpha\r\nbeta\n\ngamma");
    auto lines = withFile(rt, notes, kRead, [&](GoGraph& t, NodeId open) {
        NodeId loop = t.node("flow.while");
        t.link(open, "Then", loop, "In");
        NodeId atEnd = t.node("m:File.Is At End");
        t.link(open, "File", atEnd, "Target");
        NodeId notEnd = t.node("logic.not");
        t.link(atEnd, "At End", notEnd, "Value");
        t.link(notEnd, "Result", loop, "Condition");
        NodeId read = method(t, open, loop, "Loop Body", "Read Line");
        NodeId pr = t.node("util.print");
        t.link(read, "Then", pr, "In");
        t.link(read, "Line", pr, "String");
        t.link(loop, "Completed", t.say("end"), "In");
        return loop;
    });
    CHECK_EQ(lines, std::string("alpha,beta,,gamma,end,"));
    // Read Text: the rest, from where reading is; at the end Read Line gives ""
    auto rest = withFile(rt, notes, kRead, [&](GoGraph& t, NodeId open) {
        NodeId line = method(t, open, open, "Then", "Read Line");
        NodeId p1 = t.node("util.print");
        t.link(line, "Then", p1, "In");
        t.link(line, "Line", p1, "String");
        NodeId text = method(t, open, p1, "Then", "Read Text");
        NodeId p2 = t.node("util.print");
        t.link(text, "Then", p2, "In");
        t.link(text, "Text", p2, "String");
        NodeId after = method(t, open, p2, "Then", "Read Line");
        NodeId p3 = t.node("fn:String.Concat", {{"A", Value("[")}, {"B", Value("]")}});
        NodeId p3b = t.node("fn:String.Concat");
        t.link(p3, "Result", p3b, "A");
        t.link(after, "Line", p3b, "B");
        NodeId p4 = t.node("util.print");
        t.link(after, "Then", p4, "In");
        t.link(p3b, "Result", p4, "String");
        return p4;
    });
    CHECK_EQ(rest, std::string("alpha,beta\n\ngamma,[],"));
}

TEST(filesystem_file_reads_and_writes_bytes) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("binary");
    const std::string path = dir["data.bin"];
    std::string all;
    for (int c = 0; c < 256; ++c) all += static_cast<char>(c);
    const std::string mark = "\xEF\xBB\xBF";
    // Write Bytes: as they are, also a byte order mark at the start
    CHECK_EQ(withFile(rt, path, kWrite, [&](GoGraph& t, NodeId open) {
        NodeId a = method(t, open, open, "Then", "Write Bytes", {{"Bytes", Value(Bytes(mark + "text"))}});
        return method(t, open, a, "Then", "Write Bytes", {{"Bytes", Value(Bytes(all))}});
    }), std::string(""));
    CHECK_EQ(fileText(path), mark + "text" + all);

    // Read Bytes: Count bytes, or all the rest; the mark is data here. Then Read Text goes on from there.
    auto readParts = [&](GoGraph& t, NodeId open) {
        NodeId a = method(t, open, open, "Then", "Read Bytes", {{"Count", Value(3)}});
        NodeId b = method(t, open, a, "Then", "Read Bytes", {{"Count", Value(4)}});
        NodeId c = method(t, open, b, "Then", "Read Bytes");             // Count -1: the rest
        NodeId d = method(t, open, c, "Then", "Read Bytes", {{"Count", Value(10)}});      // at the end: none
        t.s.variables.push_back({"A", "bytes", Value(), "", ""});
        t.s.variables.push_back({"B", "bytes", Value(), "", ""});
        t.s.variables.push_back({"C", "bytes", Value(), "", ""});
        t.s.variables.push_back({"D", "bytes", Value(), "", ""});
        NodeId prev = d;
        const std::pair<NodeId, const char*> outs[] = {{a, "A"}, {b, "B"}, {c, "C"}, {d, "D"}};
        for (auto& [node, var] : outs) {
            NodeId set = t.var("var.set", var);
            t.link(prev, "Then", set, "In");
            t.link(node, "Bytes", set, "Value");
            prev = set;
        }
        return prev;
    };
    std::unique_ptr<GoRun> run;
    CHECK_EQ(withFile(rt, path, kRead, readParts, &run), std::string(""));
    if (run) {
        CHECK(run->inst->getVariable("A") == Value(Bytes(mark)));
        CHECK(run->inst->getVariable("B") == Value(Bytes(std::string("text"))));
        CHECK(run->inst->getVariable("C") == Value(Bytes(all)));
        CHECK(run->inst->getVariable("D") == Value(Bytes()));
    }
    run.reset();
    // a text read at the start still skips the mark
    auto text = withFile(rt, path, kRead, [&](GoGraph& t, NodeId open) {
        NodeId line = method(t, open, open, "Then", "Read Text");
        NodeId len = t.node("fn:String.Byte Length");
        t.link(line, "Text", len, "String");
        NodeId p = t.node("util.print");
        t.link(line, "Then", p, "In");
        t.link(len, "Result", p, "String");
        return p;
    });
    CHECK_EQ(text, std::to_string(4 + 256) + ",");

    // big bytes go through the background, both ways
    const std::string big(3 << 20, '\x5A');
    CHECK_EQ(withFile(rt, path, kWrite, [&](GoGraph& t, NodeId open) {
        return method(t, open, open, "Then", "Write Bytes", {{"Bytes", Value(Bytes(big + all))}});
    }), std::string(""));
    CHECK_EQ(fileText(path).size(), big.size() + 256);
    CHECK_EQ(withFile(rt, path, kRead, [&](GoGraph& t, NodeId open) {
        NodeId r = method(t, open, open, "Then", "Read Bytes");
        t.s.variables.push_back({"All", "bytes", Value(), "", ""});
        NodeId set = t.var("var.set", "All");
        t.link(r, "Then", set, "In");
        t.link(r, "Bytes", set, "Value");
        return set;
    }, &run), std::string(""));
    if (run) CHECK(run->inst->getVariable("All") == Value(Bytes(big + all)));
    run.reset();

    // the other way round is Failed, as for text
    auto wrong = withFile(rt, path, kWrite, [&](GoGraph& t, NodeId open) {
        NodeId r = method(t, open, open, "Then", "Read Bytes", {{"Count", Value(1)}});
        NodeId why = t.node("fn:String.Concat", {{"A", Value("why: ")}});
        t.link(r, "Error", why, "B");
        NodeId p = t.node("util.print");
        t.link(r, "Failed", p, "In");
        t.link(why, "Result", p, "String");
        return r;
    });
    CHECK(has(wrong, "why: cannot read") && has(wrong, "it is open for writing"));
}

TEST(filesystem_file_failures_and_mistakes) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("filefail");
    const std::string notes = dir["notes.txt"];
    writeRaw(notes, "text");
    auto nothing = [](GoGraph&, NodeId open) { return open; };
    // what cannot be opened goes to Failed, with the reason
    std::string out = withFile(rt, dir["missing.txt"], kRead, nothing);
    CHECK(has(out, "failed: cannot open") && has(out, "it does not exist"));
    out = withFile(rt, dir["nowhere/notes.txt"], kWrite, nothing);
    CHECK(has(out, "failed: ") && has(out, "does not exist"));
    out = withFile(rt, u8text(dir.path), kRead, nothing);
    CHECK(has(out, "it is a folder"));
    // what cannot be done with an open file goes to Failed of that node, with the reason: "then," or "why: <Error>,"
    auto outcome = [&](GoGraph& t, NodeId node) {
        NodeId why = t.node("fn:String.Concat", {{"A", Value("why: ")}});
        t.link(node, "Error", why, "B");
        NodeId say = t.node("util.print");
        t.link(node, "Failed", say, "In");
        t.link(why, "Result", say, "String");
        t.link(node, "Then", t.say("then"), "In");
        return node;
    };
    out = withFile(rt, notes, kRead, [&](GoGraph& t, NodeId open) { return outcome(t, method(t, open, open, "Then", "Write Line", {{"Text", Value("x")}})); });
    CHECK(has(out, "why: cannot write") && has(out, "it is open for reading") && !has(out, "error"));
    out = withFile(rt, notes, kWrite, [&](GoGraph& t, NodeId open) { return outcome(t, method(t, open, open, "Then", "Read Line")); });
    CHECK(has(out, "why: cannot read") && has(out, "it is open for writing"));
    out = withFile(rt, notes, kWrite, [&](GoGraph& t, NodeId open) { return outcome(t, method(t, open, open, "Then", "Read Text")); });
    CHECK(has(out, "it is open for writing"));
    out = withFile(rt, notes, kRead, [&](GoGraph& t, NodeId open) {
        return outcome(t, method(t, open, method(t, open, open, "Then", "Close"), "Then", "Read Line"));
    });
    CHECK(has(out, "why: ") && has(out, "the file is closed"));
    out = withFile(rt, notes, kWrite, [&](GoGraph& t, NodeId open) {
        return outcome(t, method(t, open, method(t, open, open, "Then", "Close"), "Then", "Write Text", {{"Text", Value("x")}}));
    });
    CHECK(has(out, "the file is closed"));
    out = withFile(rt, notes, kWrite, [&](GoGraph& t, NodeId open) {   // closing twice is fine
        return outcome(t, method(t, open, method(t, open, open, "Then", "Close"), "Then", "Close"));
    });
    CHECK_EQ(out, std::string("then,"));
    // nothing connected to Target
    out = withFile(rt, notes, kRead, [&](GoGraph& t, NodeId open) {
        NodeId read = t.node("filesystem.file.readline");
        t.link(open, "Then", read, "In");
        return outcome(t, read);
    });
    CHECK(has(out, "why: no file"));
    CHECK_EQ(fileText(notes), std::string(""));                              // Write emptied it
}

TEST(filesystem_file_writes_are_in_the_file_before_it_is_closed) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("fileclose");
    const std::string notes = dir["notes.txt"];
    // no Close: the instance keeps the output of Open File (as flowstudio keeps the last run), the file stays open
    std::unique_ptr<GoRun> run;
    CHECK_EQ(withFile(rt, notes, kWrite, [&](GoGraph& t, NodeId open) { return method(t, open, open, "Then", "Write Text", {{"Text", Value("kept")}}); }, &run),
             std::string(""));
    CHECK_EQ(fileText(notes), std::string("kept"));                         // written all the same
    run.reset();                                                             // nothing refers to it any more: closed
    std::error_code ec;
    CHECK(std::filesystem::remove(u8path(notes), ec) && !ec);
}

TEST(filesystem_file_reads_a_big_text_in_the_background) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("bigread");
    const std::string big = dir["big.txt"];
    const size_t size = size_t(128) << 20;
    writeRaw(big, std::string(size, 'x'));
    // Go: F = Open File(big) -> T = Read Text(F) -> "read";  Other: Read Line(F) -> "line" or "why: <Error>"
    GoGraph t;
    t.s.variables.push_back({"F", "File", Value(), "", ""});
    t.s.variables.push_back({"T", "string", Value(""), "", ""});
    NodeId open = t.node("filesystem.open", {{"Path", Value(big)}});
    t.link(t.ev, "Then", open, "In");
    NodeId setF = t.var("var.set", "F");
    t.link(open, "Then", setF, "In");
    t.link(open, "File", setF, "Value");
    NodeId read = t.node("filesystem.file.readtext");
    t.link(setF, "Then", read, "In");
    t.link(t.var("var.get", "F"), "Value", read, "Target");
    NodeId setT = t.var("var.set", "T");
    t.link(read, "Then", setT, "In");
    t.link(read, "Text", setT, "Value");
    t.link(setT, "Then", t.say("read"), "In");
    NodeId other = t.g->addNode("event:Other").id;
    NodeId line = t.node("filesystem.file.readline");
    t.link(other, "Then", line, "In");
    t.link(t.var("var.get", "F"), "Value", line, "Target");
    t.link(line, "Then", t.say("line"), "In");
    NodeId why = t.node("fn:String.Concat", {{"A", Value("why: ")}});
    t.link(line, "Error", why, "B");
    NodeId sayWhy = t.node("util.print");
    t.link(line, "Failed", sayWhy, "In");
    t.link(why, "Result", sayWhy, "String");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) return;

    // read in the background: the event comes back at once; meanwhile the file is busy for the other nodes
    RunResult go = r.inst->callEvent("Go");
    CHECK(go.waiting());
    for (int i = 0; i < 5000 && r.inst->getVariable("F").isNull(); ++i) {     // Open File may wait too (a virus scanner...)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        r.inst->tick(0.001);
    }
    r.log.clear();
    CHECK(r.inst->callEvent("Other").ok);
    CHECK(r.log.size() == 1 && has(r.log[0], "why: ") && has(r.log[0], "Read Text is reading it"));
    for (auto& l : r.log)
        if (!has(l, "Read Text is reading it")) std::printf("  %s\n", l.c_str());
    for (int i = 0; i < 5000 && go.task && !go.task->done(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        r.inst->tick(0.002);
    }
    CHECK(go.task && go.task->done() && go.task->result().ok);
    CHECK_EQ(r.inst->getVariable("T").toString().size(), size);
    CHECK(!r.log.empty() && r.log.back() == "read");

    // Stop while it reads: the reading stops, and the file is not busy any more
    go = r.inst->callEvent("Go");
    CHECK(go.waiting());
    r.inst->abortAll();
    bool free = false;
    for (int i = 0; i < 500 && !free; ++i) {
        r.log.clear();
        r.inst->callEvent("Other");
        free = r.log.size() == 1 && r.log[0] == "line";
        if (!free) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(free);
}

TEST(filesystem_file_writes_a_big_text_in_the_background) {
    FileSystemRuntime rt;
    if (!rt.loaded) return;
    TempFolder dir("bigwrite");
    const std::string out = dir["out.txt"];
    const size_t size = size_t(128) << 20;
    // Go: F = Open File(out, Write) -> Write Text(F, Big) -> "written";  Other: Write Line(F, "x") -> "line" or "why: <Error>"
    GoGraph t;
    t.s.variables.push_back({"F", "File", Value(), "", ""});
    t.s.variables.push_back({"Big", "string", Value(""), "", ""});
    NodeId open = t.node("filesystem.open", {{"Path", Value(out)}, {"Mode", Value(int64_t(1))}});
    t.link(t.ev, "Then", open, "In");
    NodeId setF = t.var("var.set", "F");
    t.link(open, "Then", setF, "In");
    t.link(open, "File", setF, "Value");
    NodeId write = t.node("filesystem.file.writetext");
    t.link(setF, "Then", write, "In");
    t.link(t.var("var.get", "F"), "Value", write, "Target");
    t.link(t.var("var.get", "Big"), "Value", write, "Text");
    t.link(write, "Then", t.say("written"), "In");
    NodeId other = t.g->addNode("event:Other").id;
    NodeId line = t.node("filesystem.file.writeline", {{"Text", Value("x")}});
    t.link(other, "Then", line, "In");
    t.link(t.var("var.get", "F"), "Value", line, "Target");
    t.link(line, "Then", t.say("line"), "In");
    NodeId why = t.node("fn:String.Concat", {{"A", Value("why: ")}});
    t.link(line, "Error", why, "B");
    NodeId sayWhy = t.node("util.print");
    t.link(line, "Failed", sayWhy, "In");
    t.link(why, "Result", sayWhy, "String");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) return;
    r.inst->setVariable("Big", Value(std::string(size, 'y')));

    RunResult go = r.inst->callEvent("Go");
    CHECK(go.waiting());                        // written in the background
    for (int i = 0; i < 5000 && r.inst->getVariable("F").isNull(); ++i) {     // Open File may wait too
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        r.inst->tick(0.001);
    }
    r.log.clear();
    CHECK(r.inst->callEvent("Other").ok);
    CHECK(r.log.size() == 1 && has(r.log[0], "Write Text is writing it"));
    for (int i = 0; i < 10000 && go.task && !go.task->done(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        r.inst->tick(0.002);
    }
    CHECK(go.task && go.task->done() && go.task->result().ok);
    r.log.clear();
    CHECK(r.inst->callEvent("Other").ok);       // not busy any more: a small line is written at once
    CHECK(r.log.size() == 1 && r.log[0] == "line");
    std::error_code ec;
    CHECK_EQ(std::filesystem::file_size(u8path(out), ec), uintmax_t(size + 2));
}

// ---- byte and bytes -----------------------------------------------------------------------------------------------
TEST(bytes_values_are_shared_and_compare_by_content) {
    TestRuntime rt;
    const Registry& reg = rt.registry();
    const Value a(Bytes(std::string("Hello")));
    const Value b = a;                                       // a copy shares the buffer
    CHECK(a.isBytes() && a.kind() == Value::Kind::Bytes);
    CHECK(a.identity() == b.identity());
    CHECK(a.deepCopy().identity() == a.identity());          // immutable: nothing to copy
    CHECK(a == Value(Bytes(std::string("Hello"))));          // by content
    CHECK(a != Value(Bytes(std::string("Hellp"))));
    CHECK(a != Value(std::string("Hello")));                 // text is not bytes
    CHECK(ValueLess()(Value(Bytes(std::string("A"))), Value(Bytes(std::string("B")))));
    CHECK(ValueLess()(Value(std::string("zz")), a));         // strings come before bytes
    CHECK_EQ(a.toString(), std::string("bytes(5): 48 65 6C 6C 6F"));
    CHECK_EQ(Value(Bytes()).toString(), std::string("bytes(0)"));
    CHECK_EQ(Value(Bytes(std::string(20, 'A'))).toString(),
             std::string("bytes(20): 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 ..."));
    CHECK(a.toBool() && !Value(Bytes()).toBool());
    CHECK_EQ(a.bytes().size(), size_t(5));
    CHECK(Value(std::string("x")).bytes().empty());          // not bytes: empty
    const uint8_t raw[] = {0, 255, 10};
    CHECK_EQ(Bytes(raw, 3).size(), size_t(3));
    CHECK_EQ(Bytes(std::vector<uint8_t>{1, 2})[1], uint8_t(2));

    // the types and their rules; editors list them in this order
    CHECK(reg.findType("byte") == reg.tByte() && reg.findType("bytes") == reg.tBytes());
    std::vector<std::string> first;
    for (const TypeDesc* t : reg.types())
        if (first.size() < 7) first.push_back(t->name);
    CHECK(first == std::vector<std::string>({"bool", "byte", "int", "float", "string", "bytes", "any"}));
    CHECK(reg.findType("array<byte>") != nullptr);
    CHECK(reg.canConnect(reg.tByte(), reg.tInt()) && reg.canConnect(reg.tInt(), reg.tByte()));
    CHECK(reg.canConnect(reg.tByte(), reg.tFloat()));
    CHECK(reg.canConnect(reg.tBytes(), reg.tString()));      // to print it
    CHECK(!reg.canConnect(reg.tString(), reg.tBytes()));     // text becomes bytes with From Text
    CHECK(!reg.canConnect(reg.tInt(), reg.tBytes()));
    CHECK(!reg.canConnect(reg.arrayOf(reg.tByte()), reg.tBytes()));
    CHECK(reg.defaultValue(reg.tBytes()).isBytes() && reg.defaultValue(reg.tBytes()).bytes().empty());
    CHECK_EQ(reg.defaultValue(reg.tByte()).toInt(), int64_t(0));
    CHECK(reg.convert(Value(), reg.tBytes()).isBytes());
    bool refused = false;
    try {
        reg.convert(Value(std::string("text")), reg.tBytes());
    } catch (const ScriptError&) {
        refused = true;
    }
    CHECK(refused);

    // a byte wraps around, like int overflow
    CHECK_EQ(reg.convert(Value(300), reg.tByte()).toInt(), int64_t(44));
    CHECK_EQ(reg.convert(Value(-1), reg.tByte()).toInt(), int64_t(255));
    CHECK_EQ(reg.convert(Value(256), reg.tByte()).toInt(), int64_t(0));
    CHECK_EQ(reg.convert(Value(200), reg.tByte()).toInt(), int64_t(200));
    CHECK_EQ(reg.convert(Value(65.9), reg.tByte()).toInt(), int64_t(65));

    // in a .flow file: Base64
    const Json j = valueToJson(a);
    CHECK(j.find("$bytes") && j.find("$bytes")->asString() == "SGVsbG8=");
    CHECK(jsonToValue(j) == a);
    CHECK(jsonToValue(valueToJson(Value(Bytes()))) == Value(Bytes()));
    bool bad = false;
    try {
        Json broken = Json::object();
        broken.set("$bytes", "not base64!");
        jsonToValue(broken);
    } catch (const JsonError&) {
        bad = true;
    }
    CHECK(bad);
}

TEST(utf8_base64_and_hex_conversions) {
    const std::string R = "\xEF\xBF\xBD";                    // U+FFFD
    bool valid = false;
    CHECK(bytesToText("h\xC3\xA9llo \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", &valid) == "h\xC3\xA9llo \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85" && valid);
    CHECK(bytesToText("\xF0\x9F\x98\x80", &valid) == "\xF0\x9F\x98\x80" && valid);      // 4 byte sequence
    CHECK(bytesToText("a\xC3(b", &valid) == "a" + R + "(b" && !valid);                   // a lead without its continuation
    CHECK_EQ(bytesToText("\xC0\xAF"), R + R);                                            // overlong: never UTF-8
    CHECK_EQ(bytesToText("\xED\xA0\x80"), R + R + R);                                    // a surrogate
    CHECK_EQ(bytesToText("x\xE2\x82"), "x" + R);                                         // cut at the end: one
    CHECK_EQ(bytesToText("\xF4\x90\x80\x80"), R + R + R + R);                            // beyond U+10FFFF
    CHECK_EQ(bytesToText(std::string_view("a\0b", 3)), std::string("a\0b", 3));          // a zero byte is text too

    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(base64Encode(plain[i]), std::string(coded[i]));
        std::string back;
        CHECK(base64Decode(coded[i], back) && back == plain[i]);
    }
    std::string out;
    CHECK(base64Decode("Zm9vYg", out) && out == "foob");                                 // no padding
    CHECK(base64Decode(" Zm9v\nYmFy ", out) && out == "foobar");                         // white space
    CHECK(base64Decode("-_8=", out) && out == "\xFB\xFF");                               // URL alphabet
    CHECK(!base64Decode("Zm9v!", out) && out.empty());
    CHECK(!base64Decode("Z", out));                                                      // a lone digit
    CHECK(!base64Decode("Zg==Zg", out));                                                 // a digit after the padding
    std::string all;
    for (int c = 0; c < 256; ++c) all += static_cast<char>(c);
    CHECK(base64Decode(base64Encode(all), out) && out == all);

    CHECK_EQ(hexEncode("Hello"), std::string("48656C6C6F"));
    CHECK_EQ(hexEncode("Hello", true), std::string("48 65 6C 6C 6F"));
    CHECK_EQ(hexEncode(""), std::string(""));
    CHECK(hexDecode("48656c6C6F", out) && out == "Hello");
    CHECK(hexDecode("48 65:6C-6C\n6F", out) && out == "Hello");
    CHECK(!hexDecode("486", out) && out.empty());                                        // odd
    CHECK(!hexDecode("4 8", out));                                                       // a separator inside a pair
    CHECK(!hexDecode("4G", out));
    CHECK(hexDecode(hexEncode(all), out) && out == all);
}

namespace {
// Go -> V0.. = the outputs of a pure node with these input defaults; the values, or "error: ..." in the first.
std::vector<Value> pureOutputs(Runtime& rt, const std::string& def, std::map<std::string, Value> defaults,
                               const std::vector<std::pair<std::string, std::string>>& outputs, std::string* error = nullptr) {
    GoGraph t;
    NodeId n = t.node(def, std::move(defaults));
    NodeId prev = t.ev;
    for (size_t i = 0; i < outputs.size(); ++i) {
        const std::string v = "V" + std::to_string(i);
        t.s.variables.push_back({v, outputs[i].second, Value(), "", ""});
        NodeId set = t.var("var.set", v.c_str());
        t.link(n, outputs[i].first, set, "Value");
        t.link(prev, "Then", set, "In");
        prev = set;
    }
    GoRun r(rt, t.s);
    std::vector<Value> out;
    if (!r.prog->ok()) {
        if (error) *error = dumpDiagnostics(*r.prog);
        return out;
    }
    RunResult res = r.inst->callEvent("Go");
    if (!res.ok) {
        if (error) *error = res.error;
        return out;
    }
    for (size_t i = 0; i < outputs.size(); ++i) out.push_back(r.inst->getVariable("V" + std::to_string(i)));
    return out;
}
Value bytesOf(const std::string& s) { return Value(Bytes(s)); }
} // namespace

TEST(bytes_nodes) {
    TestRuntime rt;
    auto one = [&](const std::string& def, std::map<std::string, Value> in, const char* out, const char* type) {
        auto v = pureOutputs(rt, def, std::move(in), {{out, type}});
        return v.empty() ? Value(std::string("<failed>")) : v[0];
    };
    const Value hello = bytesOf("Hello");
    CHECK_EQ(one("fn:Bytes.Length", {{"Bytes", hello}}, "Length", "int").toInt(), int64_t(5));
    CHECK_EQ(one("fn:Bytes.Byte At", {{"Bytes", hello}, {"Index", Value(1)}}, "Byte", "byte").toInt(), int64_t('e'));
    std::string error;
    CHECK(pureOutputs(rt, "fn:Bytes.Byte At", {{"Bytes", hello}, {"Index", Value(5)}}, {{"Byte", "byte"}}, &error).empty());
    CHECK(has(error, "index 5 is out of range (size 5)"));
    CHECK(one("fn:Bytes.Slice", {{"Bytes", hello}, {"Start", Value(1)}, {"Length", Value(3)}}, "Result", "bytes") == bytesOf("ell"));
    CHECK(one("fn:Bytes.Slice", {{"Bytes", hello}, {"Start", Value(3)}}, "Result", "bytes") == bytesOf("lo"));          // to the end
    CHECK(one("fn:Bytes.Slice", {{"Bytes", hello}, {"Start", Value(9)}}, "Result", "bytes") == bytesOf(""));
    CHECK(one("fn:Bytes.Slice", {{"Bytes", hello}, {"Start", Value(-2)}, {"Length", Value(2)}}, "Result", "bytes") == bytesOf("He"));
    CHECK(one("fn:Bytes.Concat", {{"A", hello}, {"B", bytesOf("!")}}, "Result", "bytes") == bytesOf("Hello!"));
    CHECK_EQ(one("fn:Bytes.Find", {{"Bytes", hello}, {"What", bytesOf("l")}, {"Start", Value(3)}}, "Index", "int").toInt(), int64_t(3));
    CHECK_EQ(one("fn:Bytes.Find", {{"Bytes", hello}, {"What", bytesOf("z")}, {"Start", Value(0)}}, "Index", "int").toInt(), int64_t(-1));
    CHECK(one("fn:Bytes.Starts With", {{"Bytes", hello}, {"Prefix", bytesOf("He")}}, "Result", "bool").toBool());
    CHECK(!one("fn:Bytes.Ends With", {{"Bytes", hello}, {"Suffix", bytesOf("He")}}, "Result", "bool").toBool());
    CHECK(one("fn:Bytes.Ends With", {{"Bytes", hello}, {"Suffix", bytesOf("lo")}}, "Result", "bool").toBool());

    // text: UTF-8 both ways, and what is not UTF-8
    CHECK(one("fn:Bytes.From Text", {{"Text", Value("h\xC3\xA9")}}, "Bytes", "bytes") == bytesOf("h\xC3\xA9"));
    auto text = pureOutputs(rt, "bytes.totext", {{"Bytes", bytesOf("h\xC3\xA9")}}, {{"Text", "string"}, {"Valid", "bool"}});
    CHECK(text.size() == 2 && text[0].str() == "h\xC3\xA9" && text[1].toBool());
    text = pureOutputs(rt, "bytes.totext", {{"Bytes", bytesOf("a\xFF")}}, {{"Text", "string"}, {"Valid", "bool"}});
    CHECK(text.size() == 2 && text[0].str() == "a\xEF\xBF\xBD" && !text[1].toBool());

    // Base64 and hex
    CHECK_EQ(one("fn:Bytes.To Base64", {{"Bytes", hello}}, "Text", "string").str(), std::string("SGVsbG8="));
    auto dec = pureOutputs(rt, "bytes.frombase64", {{"Text", Value("SGVsbG8=")}}, {{"Bytes", "bytes"}, {"Valid", "bool"}});
    CHECK(dec.size() == 2 && dec[0] == hello && dec[1].toBool());
    dec = pureOutputs(rt, "bytes.frombase64", {{"Text", Value("??")}}, {{"Bytes", "bytes"}, {"Valid", "bool"}});
    CHECK(dec.size() == 2 && dec[0] == bytesOf("") && !dec[1].toBool());
    CHECK_EQ(one("fn:Bytes.To Hex", {{"Bytes", hello}, {"Spaced", Value(true)}}, "Text", "string").str(), std::string("48 65 6C 6C 6F"));
    dec = pureOutputs(rt, "bytes.fromhex", {{"Text", Value("48656c6c6f")}}, {{"Bytes", "bytes"}, {"Valid", "bool"}});
    CHECK(dec.size() == 2 && dec[0] == hello && dec[1].toBool());

    // array<byte> and back
    const Value arr = one("fn:Bytes.To Array", {{"Bytes", bytesOf("A\xFF")}}, "Array", "array<byte>");
    CHECK(arr.arrayPtr() && arr.arrayPtr()->items.size() == 2 && arr.arrayPtr()->elem == rt.registry().tByte());
    if (arr.arrayPtr() && arr.arrayPtr()->items.size() == 2)
        CHECK(arr.arrayPtr()->items[0].toInt() == 65 && arr.arrayPtr()->items[1].toInt() == 255);
    CHECK(one("fn:Bytes.Make Bytes", {{"Array", arr}}, "Bytes", "bytes") == bytesOf("A\xFF"));

    // bytes are compared by Equal, printed as a preview
    CHECK(one("cmp.eq", {{"A", hello}, {"B", bytesOf("Hello")}}, "Result", "bool").toBool());
    GoGraph t;
    NodeId pr = t.node("util.print", {});
    t.link(t.ev, "Then", pr, "In");
    NodeId from = t.node("fn:Bytes.From Text", {{"Text", Value("Hi")}});
    t.link(from, "Bytes", pr, "String");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    CHECK_EQ(r.go(), std::string("bytes(2): 48 69,"));
}

TEST(bytes_and_byte_from_cpp_and_in_limits) {
    TestRuntime rt;
    Registry& reg = rt.registry();
    // fg::Bytes is bytes, std::byte is byte (uint8_t stays an int)
    reg.addFunction("Test.Reverse", [](const Bytes& b) {
        std::string s(b.view());
        std::reverse(s.begin(), s.end());
        return Bytes(std::move(s));
    }, {"Bytes"}).makePure();
    reg.addFunction("Test.First", [](const Bytes& b) { return b.empty() ? std::byte{0} : static_cast<std::byte>(b[0]); }, {"Bytes"}).makePure();
    reg.addFunction("Test.Small", [](uint8_t x) { return x; }, {"X"}).makePure();
    const FunctionDesc* rev = reg.findFunction("Test.Reverse");
    CHECK(rev && rev->params[0].type == reg.tBytes() && rev->ret == reg.tBytes());
    CHECK(reg.findFunction("Test.First")->ret == reg.tByte());
    CHECK(reg.findFunction("Test.Small")->ret == reg.tInt());
    Value arg = bytesOf("abc");
    CHECK(rev->invoke(&arg) == bytesOf("cba"));

    // Limits: a string length limit is a limit for bytes too
    auto out = pureOutputs(rt, "fn:Bytes.Concat", {{"A", bytesOf(std::string(600, 'x'))}, {"B", bytesOf(std::string(600, 'y'))}},
                           {{"Result", "bytes"}});
    CHECK(out.size() == 1 && out[0].bytes().size() == 1200);
    GoGraph t;
    t.s.variables.push_back({"B", "bytes", Value(), "", ""});
    NodeId cat = t.node("fn:Bytes.Concat", {{"A", bytesOf(std::string(600, 'x'))}, {"B", bytesOf(std::string(600, 'y'))}});
    NodeId set = t.var("var.set", "B");
    t.link(t.ev, "Then", set, "In");
    t.link(cat, "Result", set, "Value");
    GoRun r(rt, t.s);
    Instance::Limits lim;
    lim.maxStringLength = 1000;
    r.inst->setLimits(lim);
    RunResult res = r.inst->callEvent("Go");
    CHECK(!res.ok && has(res.error, "bytes of 1200 bytes"));

    // the debugger: a byte variable is a byte, bytes show their preview
    Script s;
    s.variables.push_back({"One", "byte", Value(7), "", ""});
    s.variables.push_back({"Data", "bytes", bytesOf("Hi"), "", ""});
    s.addEventGraph().addNode("event:Go");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    auto globals = inspectGlobals(inst);
    CHECK_EQ(globals.size(), size_t(2));
    if (globals.size() == 2) {
        CHECK(globals[0].value.type == "byte" && globals[0].value.text == "7");
        CHECK(globals[1].value.type == "bytes" && globals[1].value.text == "bytes(2): 48 69");
    }
    // a script with both round-trips through its file
    Script back = Script::parse(s.toText());
    CHECK(back.variables.size() == 2 && back.variables[1].def == bytesOf("Hi") && back.variables[0].type == "byte");
}

// Add of a map adds the key or changes its value (titled like the array nodes; the ids stay those of Set Entry / Get Entry).
TEST(map_add_adds_a_key_or_changes_its_value) {
    TestRuntime rt;
    const NodeDef* add = rt.registry().findNode("map.set");
    const NodeDef* get = rt.registry().findNode("map.get");
    CHECK(add && add->title == "Add" && add->keywords.find("insert") != std::string::npos && add->keywords.find("set entry") != std::string::npos);
    CHECK(get && get->title == "Get Item" && get->keywords.find("get entry") != std::string::npos);
    GoGraph t;
    t.s.variables.push_back({"M", "map<string,int>", Value(), "", ""});
    NodeId prev = t.ev;
    const char* prevPin = "Then";
    for (auto [key, value] : std::vector<std::pair<const char*, int>>{{"a", 1}, {"a", 2}, {"b", 3}}) {
        NodeId n = t.node("map.set", {{"Key", Value(key)}, {"Value", Value(value)}});
        t.link(prev, prevPin, n, "In");
        t.link(t.var("var.get", "M"), "Value", n, "Map");
        prev = n;
    }
    NodeId len = t.node("map.length");
    t.link(t.var("var.get", "M"), "Value", len, "Map");
    NodeId item = t.node("map.get", {{"Key", Value("a")}});
    t.link(t.var("var.get", "M"), "Value", item, "Map");
    NodeId p1 = t.node("util.print");
    t.link(prev, prevPin, p1, "In");
    t.link(len, "Length", p1, "String");
    NodeId p2 = t.node("util.print");
    t.link(p1, "Then", p2, "In");
    t.link(item, "Value", p2, "String");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    CHECK_EQ(r.go(), std::string("2,2,"));
}

// ---- structs ------------------------------------------------------------------------------------------------------
namespace {
StructDecl pointStruct() { return {"Point", "A position", "", {{"X", "int", Value(1), "across"}, {"Y", "int", Value(), "down"}}}; }
NodeId makePoint(GoGraph& t, std::map<std::string, Value> fields = {}) { return t.node("struct.make", std::move(fields), {{"struct", "Point"}}); }
NodeId breakPoint(GoGraph& t) { return t.node("struct.break", {}, {{"struct", "Point"}}); }
NodeId setField(GoGraph& t, const char* field, Value v) { return t.node("struct.set", {{"Value", v}}, {{"struct", "Point"}, {"field", field}}); }
std::vector<std::string> structErrors(const Registry& reg, const Script& s) {
    std::vector<std::string> errors;
    TypeScope::forScript(reg, s, &errors);
    return errors;
}
bool anyHas(const std::vector<std::string>& v, const std::string& part) {
    for (auto& s : v)
        if (s.find(part) != std::string::npos) return true;
    return false;
}
} // namespace

TEST(structs_are_types_of_their_script) {
    TestRuntime rt;
    const Registry& reg = rt.registry();
    Script s;
    s.structs.push_back(pointStruct());
    s.structs.push_back({"Line", "", "", {{"From", "Point", Value(), ""}, {"To", "Point", Value(), ""}, {"Tags", "array<string>", Value(), ""}}});
    s.structs.push_back({"Tree", "", "", {{"Name", "string", Value(""), ""}, {"Children", "array<Tree>", Value(), ""}}});    // through an array: fine
    CHECK(structErrors(reg, s).empty());
    auto scope = TypeScope::forScript(reg, s);
    const TypeDesc* point = scope->findType("Point");
    CHECK(point && point->isStruct() && point->fields.size() == 2 && point->fields[0].type == reg.tInt());
    CHECK(scope->findType("int") == reg.tInt());                                   // the registry's types too
    CHECK(!reg.findType("Point"));                                                 // the registry does not know it
    const TypeDesc* points = scope->findType("array<Point>");
    CHECK(points && points->kind == TypeKind::Array && points->elem == point);
    CHECK(scope->findType("array<int>") == reg.arrayOf(reg.tInt()));              // a registry type stays the registry's
    CHECK(scope->findType("map<string,Line>") && scope->findType("map<string,Line>")->elem == scope->findType("Line"));
    CHECK_EQ(scope->structs().size(), size_t(3));
    // another script has its own Point
    auto other = TypeScope::forScript(reg, s);
    CHECK(other->findType("Point") != point);

    // the default: each field with its default; nested structs too
    Value p = reg.defaultValue(point);
    CHECK(p.isStruct() && p.toString() == "Point{X: 1, Y: 0}");
    Value line = reg.defaultValue(scope->findType("Line"));
    CHECK_EQ(line.toString(), std::string("Line{From: Point{X: 1, Y: 0}, To: Point{X: 1, Y: 0}, Tags: []}"));
    CHECK(p == reg.defaultValue(point) && p != line);
    Value copy = p.deepCopy();                                                     // a value: a copy is another struct
    CHECK(copy.identity() != p.identity() && copy == p);
    copy.structPtr()->fields[0] = Value(5);
    CHECK(p.structPtr()->fields[0].toInt() == 1);
    // a struct keeps its types alive
    Value kept;
    {
        auto gone = TypeScope::forScript(reg, s);
        kept = reg.defaultValue(gone->findType("Point"));
    }
    CHECK_EQ(kept.toString(), std::string("Point{X: 1, Y: 0}"));

    // what is not allowed
    Script bad;
    bad.structs.push_back({"Self", "", "", {{"Me", "Self", Value(), ""}}});
    bad.structs.push_back({"A", "", "", {{"B", "B", Value(), ""}}});
    bad.structs.push_back({"B", "", "", {{"A", "A", Value(), ""}}});
    bad.structs.push_back({"Odd", "", "", {{"X", "Nope", Value(), ""}, {"X", "int", Value(), ""}, {"N", "int", Value("seven"), ""}}});
    bad.structs.push_back({"Odd", "", "", {}});
    bad.structs.push_back({"int", "", "", {}});
    const auto errors = structErrors(reg, bad);
    CHECK(anyHas(errors, "The struct 'Self' contains itself"));
    CHECK(anyHas(errors, "The struct 'A' contains itself"));
    CHECK(anyHas(errors, "Unknown type 'Nope' of the field 'Odd.X'"));
    CHECK(anyHas(errors, "Duplicate field 'X'"));
    CHECK(anyHas(errors, "The default of the field 'Odd.N'"));
    CHECK(anyHas(errors, "Duplicate struct 'Odd'"));
    CHECK(anyHas(errors, "has the name of a type of this application"));
    auto badScope = TypeScope::forScript(reg, bad);
    CHECK(reg.defaultValue(badScope->findType("Self")).isStruct());                // made all the same, and not infinite
    // and they are compile errors
    GoGraph t;
    t.s.structs = bad.structs;
    auto prog = build(rt, t.s);
    CHECK(!prog->ok() && has(dumpDiagnostics(*prog), "contains itself"));
}

TEST(structs_are_copied_like_arrays_and_changed_in_place_by_set_field) {
    TestRuntime rt;
    GoGraph t;
    t.s.structs.push_back(pointStruct());
    t.s.variables.push_back({"A", "Point", Value(), "", ""});
    t.s.variables.push_back({"B", "Point", Value(), "", ""});
    t.s.variables.push_back({"List", "array<Point>", Value(), "", ""});
    // A = Make Point(3, 4); B = A; A.X = 9; List.Add(A); List[0].Y = 7 (through Get Item: in place)
    NodeId setA = t.var("var.set", "A");
    t.link(t.ev, "Then", setA, "In");
    t.link(makePoint(t, {{"X", Value(3)}, {"Y", Value(4)}}), "Struct", setA, "Value");
    NodeId setB = t.var("var.set", "B");
    t.link(setA, "Then", setB, "In");
    t.link(t.var("var.get", "A"), "Value", setB, "Value");
    NodeId ax = setField(t, "X", Value(9));
    t.link(setB, "Then", ax, "In");
    t.link(t.var("var.get", "A"), "Value", ax, "Struct");
    NodeId add = t.node("array.add");
    t.link(ax, "Then", add, "In");
    t.link(t.var("var.get", "List"), "Value", add, "Array");
    t.link(t.var("var.get", "A"), "Value", add, "Item");
    NodeId item = t.node("array.get", {{"Index", Value(0)}});
    t.link(t.var("var.get", "List"), "Value", item, "Array");
    NodeId y = setField(t, "Y", Value(7));
    t.link(add, "Then", y, "In");
    t.link(item, "Item", y, "Struct");
    // print A, B and List; Break the first of the list
    NodeId pa = t.node("util.print");
    t.link(y, "Then", pa, "In");
    t.link(t.var("var.get", "A"), "Value", pa, "String");
    NodeId pb = t.node("util.print");
    t.link(pa, "Then", pb, "In");
    t.link(t.var("var.get", "B"), "Value", pb, "String");
    NodeId pl = t.node("util.print");
    t.link(pb, "Then", pl, "In");
    t.link(t.var("var.get", "List"), "Value", pl, "String");
    NodeId br = breakPoint(t);
    t.link(item, "Item", br, "Struct");
    NodeId px = t.node("util.print");
    t.link(pl, "Then", px, "In");
    t.link(br, "Y", px, "String");
    // Equal compares the fields
    NodeId eq = t.node("cmp.eq");
    t.link(t.var("var.get", "A"), "Value", eq, "A");
    t.link(makePoint(t, {{"X", Value(9)}, {"Y", Value(4)}}), "Struct", eq, "B");
    NodeId pe = t.node("util.print");
    t.link(px, "Then", pe, "In");
    t.link(eq, "Result", pe, "String");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) { std::printf("%s", dumpDiagnostics(*r.prog).c_str()); return; }
    CHECK_EQ(r.go(), std::string("Point{X: 9, Y: 4},Point{X: 3, Y: 4},[Point{X: 9, Y: 7}],7,true,"));
    // the variable starts with the defaults of the fields; a default in the file is a map of fields
    CHECK_EQ(r.prog->variables[0].def.toString(), std::string("Point{X: 1, Y: 0}"));
    t.s.variables[1].def = jsonToValue(Json::parse(R"({"$struct": "Point", "fields": {"Y": 5, "Gone": 1}})"));
    GoRun r2(rt, t.s);
    CHECK_EQ(r2.prog->variables[1].def.toString(), std::string("Point{X: 1, Y: 5}"));
}

TEST(structs_in_functions_and_files) {
    TestRuntime rt;
    GoGraph t;
    t.s.structs.push_back(pointStruct());
    // Twice(P: Point) -> Point: X and Y doubled; Set Field on its parameter does not change the caller's struct (a copy)
    Graph& f = t.s.addFunction("Twice", {{"P", "Point", false, false, Value()}}, {{"Out", "Point", false, false, Value()}});
    f.connections.clear();
    NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
    NodeId fb = f.addNode("struct.break").id;
    f.findNode(fb)->props["struct"] = "Point";
    NodeId fx = f.addNode("math.mul").id, fy = f.addNode("math.mul").id;
    f.findNode(fx)->defaults["B"] = Value(2);
    f.findNode(fy)->defaults["B"] = Value(2);
    NodeId fm = f.addNode("struct.make").id;
    f.findNode(fm)->props["struct"] = "Point";
    NodeId fs = f.addNode("struct.set").id;
    f.findNode(fs)->props = {{"struct", "Point"}, {"field", "X"}};
    f.findNode(fs)->defaults["Value"] = Value(-100);
    auto fl = [&](NodeId a, const char* ap, NodeId b, const char* bp) { f.connections.push_back({a, ap, b, bp}); };
    fl(entry, "P", fb, "Struct");
    fl(fb, "X", fx, "A");
    fl(fb, "Y", fy, "A");
    fl(fx, "Result", fm, "X");
    fl(fy, "Result", fm, "Y");
    fl(entry, "Then", fs, "In");
    fl(entry, "P", fs, "Struct");
    fl(fs, "Then", ret, "In");
    fl(fm, "Struct", ret, "Out");
    t.s.variables.push_back({"P", "Point", Value(), "", ""});
    NodeId call = t.node("func.call", {}, {{"function", "Twice"}});
    t.link(t.ev, "Then", call, "In");
    t.link(t.var("var.get", "P"), "Value", call, "P");
    NodeId p1 = t.node("util.print");
    t.link(call, "Then", p1, "In");
    t.link(call, "Out", p1, "String");
    NodeId p2 = t.node("util.print");
    t.link(p1, "Then", p2, "In");
    t.link(t.var("var.get", "P"), "Value", p2, "String");
    GoRun r(rt, t.s);
    CHECK(r.prog->ok());
    if (!r.prog->ok()) { std::printf("%s", dumpDiagnostics(*r.prog).c_str()); return; }
    // the pure nodes run when Return asks for Out, after Set Field: the function sees its own change, the caller's P does not
    CHECK_EQ(r.go(), std::string("Point{X: -200, Y: 0},Point{X: 1, Y: 0},"));

    // in a file: version 2, the structs and their fields kept
    const std::string text = t.s.toText();
    CHECK(has(text, "\"version\": 2") && has(text, "\"structs\""));
    Script back = Script::parse(text);
    CHECK(back.structs.size() == 1 && back.structs[0].fields.size() == 2 && back.structs[0].fields[0].def == Value(1));
    CHECK(back.structs[0].doc == "A position" && back.structs[0].fields[1].doc == "down");
    CHECK(build(rt, back)->ok());
    // a value of a struct type, in a file: written with its fields
    CHECK(has(valueToJson(r.prog->variables[0].def).dump(), "\"$struct\""));

    // what the compiler refuses
    GoGraph bad;
    bad.s.structs.push_back(pointStruct());
    NodeId unknown = bad.node("struct.make", {}, {{"struct", "Nope"}});
    NodeId wrongField = bad.node("struct.set", {}, {{"struct", "Point"}, {"field", "Z"}});
    (void)unknown;
    (void)wrongField;
    auto prog = build(rt, bad.s);
    CHECK(!prog->ok());
    CHECK(has(dumpDiagnostics(*prog), "Unknown struct 'Nope'") && has(dumpDiagnostics(*prog), "has no field 'Z'"));
}

TEST(structs_survive_a_reload_field_by_field) {
    TestRuntime rt;
    Script s;
    s.structs.push_back(pointStruct());
    s.variables.push_back({"P", "Point", Value(), "", ""});
    s.variables.push_back({"List", "array<Point>", Value(), "", ""});
    s.addEventGraph().addNode("event:Go");
    auto prog = build(rt, s);
    CHECK(prog->ok());
    Instance inst(prog);
    const Registry& reg = rt.registry();
    Value p = reg.defaultValue(prog->types->findType("Point"));
    p.structPtr()->fields = {Value(7), Value(8)};
    inst.setVariable("P", p);
    auto list = std::make_shared<ArrayData>();
    list->elem = prog->types->findType("Point");
    list->items.push_back(p.deepCopy());
    inst.setVariable("List", Value(list));
    // the struct changes: Y goes, X becomes a float, Z comes
    Script s2 = s;
    s2.structs[0].fields = {{"X", "float", Value(), ""}, {"Z", "string", Value("new"), ""}};
    auto prog2 = build(rt, s2);
    CHECK(prog2->ok());
    auto rep = inst.reload(prog2);
    CHECK(rep.ok());
    CHECK_EQ(inst.getVariable("P").toString(), std::string("Point{X: 7.0, Z: \"new\"}"));
    CHECK_EQ(inst.getVariable("List").toString(), std::string("[Point{X: 7.0, Z: \"new\"}]"));
    CHECK(inst.getVariable("P").structPtr()->type.get() == prog2->types->findType("Point"));

    // the debugger shows the fields; a watch reaches one
    auto globals = inspectGlobals(inst);
    CHECK(globals.size() == 2 && globals[0].value.type == "Point" && globals[0].value.children.size() == 2);
    if (globals.size() == 2 && globals[0].value.children.size() == 2)
        CHECK(globals[0].value.children[0].name == "X" && globals[0].value.children[0].text == "7.0" && globals[0].value.children[0].path == "P.X");
    auto watch = evaluateExpression(inst, nullptr, 0, "List[0].Z");
    CHECK(watch.ok && watch.view.text == "\"new\"");
    CHECK(has(evaluateExpression(inst, nullptr, 0, "P.Nope").error, "has no field 'Nope'"));
}

TEST(structs_are_renamed_everywhere_and_offered_in_the_palette) {
    TestRuntime rt;
    GoGraph t;
    t.s.structs.push_back(pointStruct());
    t.s.structs.push_back({"Line", "", "", {{"From", "Point", Value(), ""}, {"All", "array<Point>", Value(), ""}}});
    t.s.variables.push_back({"P", "map<string,Point>", Value(), "", ""});
    NodeId mk = makePoint(t, {{"X", Value(5)}});
    NodeId br = breakPoint(t);
    NodeId st = setField(t, "X", Value(1));
    t.link(mk, "Struct", br, "Struct");
    NodeId pr = t.node("util.print");
    t.link(br, "X", pr, "String");
    t.s.renameStruct("Point", "Spot");
    t.s.renameField("Spot", "X", "Across");
    CHECK(t.s.findStruct("Spot") && !t.s.findStruct("Point"));
    CHECK_EQ(t.s.structs[1].fields[0].type, std::string("Spot"));
    CHECK_EQ(t.s.structs[1].fields[1].type, std::string("array<Spot>"));
    CHECK_EQ(t.s.variables[0].type, std::string("map<string,Spot>"));
    CHECK(t.g->findNode(mk)->prop("struct") == "Spot" && t.g->findNode(mk)->defaults.count("Across"));
    CHECK(t.g->findNode(st)->prop("field") == "Across");
    bool linked = false;
    for (auto& c : t.g->connections) linked |= c.fromNode == br && c.fromPin == "Across";
    CHECK(linked);
    CHECK(build(rt, t.s)->ok());

    // the palette: Make, Break and a Set for each field, for each struct of the script
    NodeBuildCtx ctx{rt.registry(), &t.s, t.g};
    auto all = buildPalette(ctx);
    std::set<std::string> titles;
    for (auto& tp : all)
        if (tp.defId.rfind("struct.", 0) == 0) titles.insert(tp.title);
    CHECK(titles == std::set<std::string>({"Make Spot", "Break Spot", "Set Spot.Across", "Set Spot.Y", "Make Line", "Break Line",
                                           "Set Line.From", "Set Line.All"}));
    // dragging a Spot: its Break and Set nodes fit, a Line's do not
    auto prog = build(rt, t.s);
    PinFilter f;
    f.kind = PinKind::Data; f.dir = PinDir::Out; f.type = prog->types->findType("Spot");
    std::set<std::string> fits;
    for (auto& tp : filterPalette(rt.registry(), all, f, prog->types.get()))
        if (tp.defId.rfind("struct.", 0) == 0) fits.insert(tp.title);
    CHECK(fits.count("Break Spot") && fits.count("Set Spot.Y") && fits.count("Make Line") && !fits.count("Break Line"));

    // the application does not need to know the structs; a type it does not know in a field is reported
    CHECK(checkCompatibility(t.s, rt.registry()).ok());
    t.s.structs[1].fields.push_back({"Gone", "MissingClass", Value(), ""});
    CHECK(!checkCompatibility(t.s, rt.registry()).ok());
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
