// FlowGraphBench: how fast does the interpreter run scripts?
//   FlowGraphBench [--scale F] [--runs N] [--stable] [--debugger] [filter]
// --stable pins the process to one core at high priority (Windows), which makes the figures repeatable.
// --debugger runs with a Debugger attached (no breakpoints), the way an editor runs a script.
// Every benchmark builds a script through the C++ API, compiles it and runs its Main event (the way `flow` does:
// fg::standard::runMain, no debugger, no executor). The best of N runs is reported.
#include <FlowGraph/Runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace fg;

namespace {

bool g_debugger = false;

// A reflected class: what plugins expose to scripts.
struct BenchCounter {
    int64_t total = 0;
    void add(int64_t n) { total += n; }
};

void link(Graph& g, NodeId a, const std::string& ap, NodeId b, const std::string& bp) { g.connections.push_back({a, ap, b, bp}); }
NodeId add(Graph& g, const char* def) { return g.addNode(def).id; }
NodeId add(Graph& g, const char* def, const char* pin, Value v) {
    Node& n = g.addNode(def);
    n.defaults[pin] = std::move(v);
    return n.id;
}
NodeId var(Graph& g, const char* kind, const char* name) {
    Node& n = g.addNode(kind);
    n.props = {{"name", name}, {"scope", "global"}};
    return n.id;
}

struct Bench {
    std::string name;
    std::string what;
    double iterations;                          // loop iterations / calls, for the per-iteration figure
    std::function<void(Script&)> build;
    // Optional: drives the instance from C++ instead of running the Main event. Returns false (and sets `error`) on a wrong result.
    std::function<bool(Instance&, std::string& error)> drive;
};

// For (0, n) { body }: returns {loop, bodyTail}; the caller chains nodes after `tail` ("Then").
struct Loop {
    NodeId loop, tail;
    std::string tailPin;
};
Loop forLoop(Graph& g, NodeId ev, int64_t n) {
    NodeId loop = add(g, "flow.for");
    g.findNode(loop)->defaults["First"] = Value(int64_t(0));
    g.findNode(loop)->defaults["Last"] = Value(n);
    link(g, ev, "Then", loop, "In");
    return {loop, loop, "Loop Body"};
}

// var = var + src.pin, executed after execFrom.execPin
NodeId accumulate(Graph& g, const char* name, NodeId src, const char* pin, NodeId execFrom, const char* execPin) {
    NodeId get = var(g, "var.get", name), sum = add(g, "math.add"), set = var(g, "var.set", name);
    link(g, get, "Value", sum, "A");
    link(g, src, pin, sum, "B");
    link(g, sum, "Result", set, "Value");
    link(g, execFrom, execPin, set, "In");
    return set;
}

std::vector<Bench> makeBenchmarks(double scale) {
    auto N = [scale](double n) { return static_cast<int64_t>(n * scale); };
    std::vector<Bench> b;

    // acc = acc + i
    b.push_back({"loop_sum", "For loop: acc = acc + i", double(N(2e6)), [=](Script& s) {
        s.variables.push_back({"acc", "int", Value(int64_t(0)), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(2e6));
        NodeId get = var(g, "var.get", "acc"), sum = add(g, "math.add"), set = var(g, "var.set", "acc");
        link(g, get, "Value", sum, "A");
        link(g, l.loop, "Index", sum, "B");
        link(g, sum, "Result", set, "Value");
        link(g, l.loop, "Loop Body", set, "In");
    }});

    // acc = ((acc + i) * 3) % 1000003 : a longer pure expression
    b.push_back({"loop_expr", "For loop: acc = ((acc + i) * 3) % 1000003", double(N(1e6)), [=](Script& s) {
        s.variables.push_back({"acc", "int", Value(int64_t(0)), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(1e6));
        NodeId get = var(g, "var.get", "acc"), sum = add(g, "math.add"), mul = add(g, "math.mul", "B", Value(int64_t(3)));
        NodeId mod = add(g, "math.mod", "B", Value(int64_t(1000003))), set = var(g, "var.set", "acc");
        link(g, get, "Value", sum, "A");
        link(g, l.loop, "Index", sum, "B");
        link(g, sum, "Result", mul, "A");
        link(g, mul, "Result", mod, "A");
        link(g, mod, "Result", set, "Value");
        link(g, l.loop, "Loop Body", set, "In");
    }});

    // floating point: x = x * 0.999999 + 0.5
    b.push_back({"loop_float", "For loop: x = x * 0.999999 + 0.5", double(N(1e6)), [=](Script& s) {
        s.variables.push_back({"x", "float", Value(0.0), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(1e6));
        NodeId get = var(g, "var.get", "x"), mul = add(g, "math.mul", "B", Value(0.999999)), sum = add(g, "math.add", "B", Value(0.5));
        NodeId set = var(g, "var.set", "x");
        link(g, get, "Value", mul, "A");
        link(g, mul, "Result", sum, "A");
        link(g, sum, "Result", set, "Value");
        link(g, l.loop, "Loop Body", set, "In");
    }});

    // while (i < n) i = i + 1
    b.push_back({"while_count", "While loop: i = i + 1 while i < n", double(N(1e6)), [=](Script& s) {
        s.variables.push_back({"i", "int", Value(int64_t(0)), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        NodeId wh = add(g, "flow.while");
        NodeId get = var(g, "var.get", "i"), lt = add(g, "cmp.lt", "B", Value(N(1e6)));
        NodeId get2 = var(g, "var.get", "i"), inc = add(g, "math.add", "B", Value(int64_t(1))), set = var(g, "var.set", "i");
        link(g, ev, "Then", wh, "In");
        link(g, get, "Value", lt, "A");
        link(g, lt, "Result", wh, "Condition");
        link(g, get2, "Value", inc, "A");
        link(g, inc, "Result", set, "Value");
        link(g, wh, "Loop Body", set, "In");
    }});

    // if (i % 2 == 0) acc += 1 else acc -= 1
    b.push_back({"loop_branch", "For loop with Branch (i % 2 == 0)", double(N(1e6)), [=](Script& s) {
        s.variables.push_back({"acc", "int", Value(int64_t(0)), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(1e6));
        NodeId mod = add(g, "math.mod", "B", Value(int64_t(2))), eq = add(g, "cmp.eq", "B", Value(int64_t(0))), br = add(g, "flow.branch");
        NodeId g1 = var(g, "var.get", "acc"), inc = add(g, "math.add", "B", Value(int64_t(1))), s1 = var(g, "var.set", "acc");
        NodeId g2 = var(g, "var.get", "acc"), dec = add(g, "math.sub", "B", Value(int64_t(1))), s2 = var(g, "var.set", "acc");
        link(g, l.loop, "Index", mod, "A");
        link(g, mod, "Result", eq, "A");
        link(g, eq, "Result", br, "Condition");
        link(g, l.loop, "Loop Body", br, "In");
        link(g, g1, "Value", inc, "A");
        link(g, inc, "Result", s1, "Value");
        link(g, br, "True", s1, "In");
        link(g, g2, "Value", dec, "A");
        link(g, dec, "Result", s2, "Value");
        link(g, br, "False", s2, "In");
    }});

    // recursive function calls
    b.push_back({"fib_call", "Recursive Fib(N) function, N = 27 (~830k calls)", 832039.0 * scale, [=](Script& s) {
        Graph& f = s.addFunction("Fib", {{"N", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
        f.connections.clear();
        NodeId entry = f.nodes[0].id, retRec = f.nodes[1].id;
        NodeId lt = add(f, "cmp.lt", "B", Value(int64_t(2)));
        NodeId br = add(f, "flow.branch");
        NodeId retBase = add(f, "func.return");
        NodeId sub1 = add(f, "math.sub", "B", Value(int64_t(1)));
        NodeId sub2 = add(f, "math.sub", "B", Value(int64_t(2)));
        NodeId a = add(f, "func.call"), c = add(f, "func.call");
        f.findNode(a)->props["function"] = "Fib";
        f.findNode(c)->props["function"] = "Fib";
        NodeId sum = add(f, "math.add");
        link(f, entry, "Then", br, "In");
        link(f, entry, "N", lt, "A");
        link(f, lt, "Result", br, "Condition");
        link(f, br, "True", retBase, "In");
        link(f, entry, "N", retBase, "Result");
        link(f, br, "False", a, "In");
        link(f, entry, "N", sub1, "A");
        link(f, entry, "N", sub2, "A");
        link(f, sub1, "Result", a, "N");
        link(f, a, "Then", c, "In");
        link(f, sub2, "Result", c, "N");
        link(f, c, "Then", retRec, "In");
        link(f, a, "Result", sum, "A");
        link(f, c, "Result", sum, "B");
        link(f, sum, "Result", retRec, "Result");
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        NodeId call = add(g, "func.call", "N", Value(int64_t(scale >= 1.0 ? 27 : 22)));
        g.findNode(call)->props["function"] = "Fib";
        link(g, ev, "Then", call, "In");
    }});

    // arrays: fill, then sum with For Each
    b.push_back({"array_fill_sum", "Array: Add N items, then For Each sum", double(N(5e5)) * 2, [=](Script& s) {
        s.variables.push_back({"arr", "array<int>", Value(), "", ""});
        s.variables.push_back({"sum", "int", Value(int64_t(0)), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(5e5));
        NodeId arr = var(g, "var.get", "arr"), push = add(g, "array.add");
        link(g, arr, "Value", push, "Array");
        link(g, l.loop, "Index", push, "Item");
        link(g, l.loop, "Loop Body", push, "In");
        NodeId each = add(g, "flow.foreach");
        NodeId arr2 = var(g, "var.get", "arr"), gs = var(g, "var.get", "sum"), add2 = add(g, "math.add"), set = var(g, "var.set", "sum");
        link(g, l.loop, "Completed", each, "In");
        link(g, arr2, "Value", each, "Array");
        link(g, gs, "Value", add2, "A");
        link(g, each, "Element", add2, "B");
        link(g, add2, "Result", set, "Value");
        link(g, each, "Loop Body", set, "In");
    }});

    // maps
    b.push_back({"map_fill_read", "Map: Set N entries, then Get them", double(N(1.5e5)) * 2, [=](Script& s) {
        s.variables.push_back({"m", "map<int,int>", Value(), "", ""});
        s.variables.push_back({"sum", "int", Value(int64_t(0)), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(1.5e5));
        NodeId m = var(g, "var.get", "m"), ms = add(g, "map.set");
        link(g, m, "Value", ms, "Map");
        link(g, l.loop, "Index", ms, "Key");
        link(g, l.loop, "Index", ms, "Value");
        link(g, l.loop, "Loop Body", ms, "In");
        NodeId l2 = add(g, "flow.for");
        g.findNode(l2)->defaults["Last"] = Value(N(1.5e5));
        link(g, l.loop, "Completed", l2, "In");
        NodeId m2 = var(g, "var.get", "m"), mg = add(g, "map.get");
        NodeId gs = var(g, "var.get", "sum"), add2 = add(g, "math.add"), set = var(g, "var.set", "sum");
        link(g, m2, "Value", mg, "Map");
        link(g, l2, "Index", mg, "Key");
        link(g, gs, "Value", add2, "A");
        link(g, mg, "Value", add2, "B");
        link(g, add2, "Result", set, "Value");
        link(g, l2, "Loop Body", set, "In");
    }});

    // strings
    b.push_back({"string_concat", "For loop: s = Concat(\"item \", i)", double(N(3e5)), [=](Script& s) {
        s.variables.push_back({"s", "string", Value(""), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        Loop l = forLoop(g, ev, N(3e5));
        NodeId cat = add(g, "fn:String.Concat", "A", Value("item ")), set = var(g, "var.set", "s");
        link(g, l.loop, "Index", cat, "B");
        link(g, cat, "Result", set, "Value");
        link(g, l.loop, "Loop Body", set, "In");
    }});

    // exec chain without data: long Sequence of Branch-free nodes (flow overhead only)
    b.push_back({"loop_empty", "Empty For loop (loop node overhead only)", double(N(3e6)), [=](Script& s) {
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        forLoop(g, ev, N(3e6));
    }});
    // array reads and writes through nodes: arr[i % 1000] = arr[(i + 1) % 1000] + 1
    b.push_back({"array_rw", "Array: Get Item / Set Item in a For loop", double(N(5e5)), [=](Script& s) {
        s.variables.push_back({"arr", "array<int>", Value(), "", ""});
        Graph& g = s.addEventGraph();
        NodeId ev = add(g, "event:Main");
        NodeId fill = add(g, "flow.for");
        g.findNode(fill)->defaults["Last"] = Value(int64_t(1000));
        link(g, ev, "Then", fill, "In");
        NodeId arr0 = var(g, "var.get", "arr"), push = add(g, "array.add");
        link(g, arr0, "Value", push, "Array");
        link(g, fill, "Index", push, "Item");
        link(g, fill, "Loop Body", push, "In");
        NodeId loop = add(g, "flow.for");
        g.findNode(loop)->defaults["Last"] = Value(N(5e5));
        link(g, fill, "Completed", loop, "In");
        NodeId i1 = add(g, "math.add", "B", Value(int64_t(1))), m1 = add(g, "math.mod", "B", Value(int64_t(1000))), m0 = add(g, "math.mod", "B", Value(int64_t(1000)));
        NodeId a1 = var(g, "var.get", "arr"), a2 = var(g, "var.get", "arr");
        NodeId get = add(g, "array.get"), inc = add(g, "math.add", "B", Value(int64_t(1))), set = add(g, "array.set");
        link(g, loop, "Index", i1, "A");
        link(g, i1, "Result", m1, "A");
        link(g, loop, "Index", m0, "A");
        link(g, a1, "Value", get, "Array");
        link(g, m1, "Result", get, "Index");
        link(g, get, "Item", inc, "A");
        link(g, a2, "Value", set, "Array");
        link(g, m0, "Result", set, "Index");
        link(g, inc, "Result", set, "Item");
        link(g, loop, "Loop Body", set, "In");
    }});

    // methods and properties of a registered C++ class: total += i; acc += total
    {
        Bench ob{"object_calls", "Reflected object: method call + property read per iteration", double(N(2e5)), [=](Script& s) {
            s.variables.push_back({"c", "BenchCounter", Value(), "", ""});
            s.variables.push_back({"acc", "int", Value(int64_t(0)), "", ""});
            Graph& g = s.addEventGraph();
            NodeId ev = add(g, "event:Main");
            NodeId mk = add(g, "new:BenchCounter"), setc = var(g, "var.set", "c");
            link(g, ev, "Then", mk, "In");
            link(g, mk, "Then", setc, "In");
            link(g, mk, "Object", setc, "Value");
            Loop l = forLoop(g, setc, N(2e5));
            NodeId getc = var(g, "var.get", "c"), addm = add(g, "m:BenchCounter.Add"), getc2 = var(g, "var.get", "c"), total = add(g, "get:BenchCounter.Total");
            link(g, getc, "Value", addm, "Target");
            link(g, l.loop, "Index", addm, "N");
            link(g, l.loop, "Loop Body", addm, "In");
            link(g, getc2, "Value", total, "Target");
            accumulate(g, "acc", total, "Total", addm, "Then");
        }, nullptr};
        ob.drive = [=](Instance& inst, std::string& error) {
            auto out = standard::runMain(inst, {});
            if (!out.ok) { error = out.error; return false; }
            int64_t want = 0, total = 0;
            for (int64_t i = 0; i < N(2e5); ++i) { total += i; want += total; }
            if (inst.getVariable("acc").toInt() != want) { error = "wrong sum"; return false; }
            return true;
        };
        b.push_back(std::move(ob));
    }

    // calling a script function from C++ over and over (what a host does for callbacks)
    {
        Bench hb{"host_call", "C++ -> script function call (AddOne), 200k calls", double(N(2e5)), [=](Script& s) {
            Graph& f = s.addFunction("AddOne", {{"X", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
            f.connections.clear();
            NodeId entry = f.nodes[0].id, ret = f.nodes[1].id;
            NodeId sum = add(f, "math.add", "B", Value(int64_t(1)));
            link(f, entry, "Then", ret, "In");
            link(f, entry, "X", sum, "A");
            link(f, sum, "Result", ret, "Result");
            s.addEventGraph();
        }, nullptr};
        hb.drive = [=](Instance& inst, std::string& error) {
            int64_t acc = 0;
            for (int64_t i = 0; i < N(2e5); ++i) {
                RunResult r = inst.callFunction("AddOne", {Value(i)});
                if (!r.ok || r.returns.size() != 1) { error = "call failed: " + r.error; return false; }
                acc += r.returns[0].toInt();
            }
            int64_t n = N(2e5);
            if (acc != n * (n - 1) / 2 + n) { error = "wrong sum"; return false; }
            return true;
        };
        b.push_back(std::move(hb));
    }
    return b;
}

double runOnce(Runtime& rt, const Bench& bench, const Script& script, bool& ok, std::string& error) {
    auto program = rt.compile(script);
    if (!program->ok()) {
        ok = false;
        for (auto& d : program->diagnostics) error += d.message + "; ";
        return 0;
    }
    Instance inst(program);
    inst.setLogHandler([](LogLevel, const std::string&) {});
    inst.setStepLimit(0);
    Debugger debugger;
    if (g_debugger) inst.setDebugHooks(&debugger);
    auto t0 = std::chrono::steady_clock::now();
    if (bench.drive) {
        error.clear();
        ok = bench.drive(inst, error);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    auto out = standard::runMain(inst, {});
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ok = out.ok;
    error = out.error;
    return sec;
}

} // namespace

int main(int argc, char** argv) {
    double scale = 1.0;
    int runs = 5;
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--debugger")) g_debugger = true;
        else if (!std::strcmp(argv[i], "--stable")) {
#ifdef _WIN32
            SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
            SetProcessAffinityMask(GetCurrentProcess(), 1u << 2);
#endif
        }
        else filter = argv[i];
    }
    Runtime rt;
    standard::registerStandardApp(rt.registry());
    rt.registry().registerClass<BenchCounter>("BenchCounter").constructor<>().property("Total", &BenchCounter::total).method("Add", &BenchCounter::add, {"N"});
    std::printf("%-16s %-52s %10s %10s %10s\n", "benchmark", "what", "best ms", "median ms", "ns/iter");
    double total = 0;
    for (auto& bench : makeBenchmarks(scale)) {
        if (!filter.empty() && bench.name.find(filter) == std::string::npos) continue;
        Script s;
        bench.build(s);
        stampHostEvents(s, rt.registry());
        std::vector<double> times;
        bool ok = true;
        std::string error;
        for (int r = 0; r < runs && ok; ++r) times.push_back(runOnce(rt, bench, s, ok, error));
        if (!ok) {
            std::printf("%-16s FAILED: %s\n", bench.name.c_str(), error.c_str());
            continue;
        }
        std::sort(times.begin(), times.end());
        double best = times.front(), median = times[times.size() / 2];
        total += best;
        std::printf("%-16s %-52s %10.1f %10.1f %10.0f\n", bench.name.c_str(), bench.what.c_str(), best * 1e3, median * 1e3, best * 1e9 / bench.iterations);
    }
    std::printf("%-16s %-52s %10.1f\n", "total", "", total * 1e3);
    return 0;
}
