// Minimal test runner: TEST(name) { CHECK(cond); }
#include <FlowGraph/Runtime.hpp>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace fg;

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
    Runtime rt;
    auto* t = rt.registry().findType("map<string,array<float>>");
    CHECK(t && t->kind == TypeKind::Map);
    CHECK(t && t->elem->kind == TypeKind::Array);
    CHECK(rt.registry().findType("nope") == nullptr);
}

TEST(for_loop_sums_into_variable) {
    Runtime rt;
    Script s;
    s.variables.push_back({"Total", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom", 0, 0).id;
    g.findNode(ev)->props["name"] = "Run";
    NodeId loop = g.addNode("flow.for").id;
    g.findNode(loop)->defaults["First"] = Value(1);
    g.findNode(loop)->defaults["Last"] = Value(10);
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

TEST(function_call_with_return) {
    Runtime rt;
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
    auto r = inst.callFunction("Mul3", {Value(2.5)});
    CHECK(r.ok);
    CHECK_EQ(r.returns.size(), size_t(1));
    CHECK_EQ(r.returns[0].toFloat(), 7.5);
}

TEST(by_ref_parameter_writes_through) {
    Runtime rt;
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
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    s.addFunction("Bump", {{"V", "int", true, false, Value()}});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
    NodeId call = g.addNode("func.call").id;
    g.findNode(call)->props["function"] = "Bump";
    link(g, ev, "Then", call, "In");
    auto prog = rt.compile(s);
    CHECK(!prog->ok());
}

TEST(const_param_cannot_be_modified) {
    Runtime rt;
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
    Runtime rt;
    registerSample(rt);
    Script s;
    s.variables.push_back({"Pet", "Dog", Value(), "", ""});
    s.variables.push_back({"Said", "string", Value(""), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
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
    Runtime rt;
    Script s;
    s.variables.push_back({"Done", "bool", Value(false), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    s.variables.push_back({"N", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    s.variables.push_back({"List", "array<int>", Value(), "", ""});
    s.variables.push_back({"Sum", "int", Value(0), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    s.variables.push_back({"X", "int", Value(7), "Cat", "doc"});
    s.addFunction("F", {{"A", "int", true, false, Value(1)}}, {{"R", "int", false, false, Value()}});
    Graph& g = s.addEventGraph();
    g.addNode("event.custom").props["name"] = "Go";
    g.addComment(1, 2).text = "hello";
    Script t = Script::parse(s.toText());
    CHECK_EQ(t.toText(), s.toText());
    CHECK_EQ(t.graphs.size(), size_t(2));
    CHECK(t.graphs[0].inputs[0].byRef);
}

TEST(runtime_error_is_reported_with_node) {
    Runtime rt;
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
    Script s;
    s.variables.push_back({"Arr", "array<int>", Value(), "", ""});
    Graph& g = s.addEventGraph();
    NodeId ev = g.addNode("event.custom").id;
    g.findNode(ev)->props["name"] = "Go";
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
    Runtime rt;
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
    for (auto& t : buildPalette(fctx)) CHECK(t.defId != "event:Tick" && t.defId != "event.custom");
    // instantiate applies properties
    Node& n = instantiate(g, vars[0], 5, 6);
    CHECK_EQ(n.def, std::string("var.get"));
    CHECK_EQ(n.prop("name"), std::string("Score"));
}

TEST(inspect_values_and_frames) {
    Runtime rt;
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
    Runtime rt;
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
