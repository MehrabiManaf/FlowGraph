// Minimal test runner: TEST(name) { CHECK(cond); }
#include <FlowGraph/Runtime.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <filesystem>
#include <functional>
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
    std::string path = g_exeDir + "/plugins/ExamplePlugin.dll";
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
    CHECK(path.size() > 10 && path.substr(path.size() - 11) == "script.flowlaunch" || path.find("script.flowlaunch") != std::string::npos);
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
