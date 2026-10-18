// Uses an installed FlowGraph like a host application: registers an event and a function, builds a script, runs it.
// Exit code 0 when the script computed what it should.
#include <FlowGraph/Runtime.hpp>
#if defined(WITH_EDITOR)
#include <FlowGraphEditor/ScriptEditor.hpp>
#endif

#include <cstdio>

int main() {
    fg::Runtime rt;
    int64_t received = 0;
    rt.registry().addEvent("Tick", {{"Frame", "int"}});
    rt.registry().addFunction("Host.Report", [&](int64_t v) { received = v; }, {"Value"});

    // Tick(Frame) -> Report(Frame * 10)
    fg::Script s;
    fg::Graph& g = s.addEventGraph();
    const fg::NodeId ev = g.addNode("event:Tick").id;
    const fg::NodeId mul = g.addNode("math.mul").id;
    g.findNode(mul)->defaults["B"] = fg::Value(10);
    const fg::NodeId report = g.addNode("fn:Host.Report").id;
    g.connections.push_back({ev, "Then", report, "In"});
    g.connections.push_back({ev, "Frame", mul, "A"});
    g.connections.push_back({mul, "Result", report, "Value"});

    auto program = rt.compile(s);
    if (!program->ok()) {
        std::fprintf(stderr, "consumer: the script does not compile\n");
        return 1;
    }
    fg::Instance inst(program);
    fg::RunResult r = inst.callEvent("Tick", {fg::Value(4)});
#if defined(WITH_EDITOR)
    void (*init)() = &fged::initEditor;          // linked from FlowGraphEditor (not called: there is no QApplication here)
    std::printf("editor library linked: %s\n", init ? "yes" : "no");
#endif
    std::printf("Tick(4) reported %lld\n", static_cast<long long>(received));
    return r.ok && received == 40 ? 0 : 1;
}
