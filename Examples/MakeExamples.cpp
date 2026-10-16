// fgexamples: builds the sample scripts through the C++ API and saves them as .flow files.
//   fgexamples <output directory>
// Doubles as a tour of the Script API.
#include <FlowGraph/Runtime.hpp>

#include <cstdio>
#include <filesystem>

using namespace fg;

namespace {

void link(Graph& g, NodeId a, const std::string& ap, NodeId b, const std::string& bp) { g.connections.push_back({a, ap, b, bp}); }

NodeId add(Graph& g, const char* def, double x, double y) { return g.addNode(def, x, y).id; }

// Events belong to the application: a script only handles them. Main is the event of the standard application.
NodeId event(Graph& g, const char* name, double x = 0, double y = 0) {
    return g.addNode(std::string("event:") + name, x, y).id;
}

fg::Runtime* g_runtime = nullptr;
void saveScript(Script& s, const std::string& path) {
    stampHostEvents(s, g_runtime->registry());       // remember the signature of the events the script handles
    s.save(path);
}

NodeId var(Graph& g, const char* kind, const char* name, const char* scope, double x, double y) {
    Node& n = g.addNode(kind, x, y);
    n.props = {{"name", name}, {"scope", scope}};
    return n.id;
}

NodeId print(Graph& g, double x, double y, const char* text = nullptr) {
    Node& n = g.addNode("util.print", x, y);
    if (text) n.defaults["String"] = Value(text);
    return n.id;
}

NodeId concat(Graph& g, const char* a, double x, double y) {
    Node& n = g.addNode("fn:String.Concat", x, y);
    n.defaults["A"] = Value(a);
    return n.id;
}

NodeId call(Graph& g, const char* fn, double x, double y) {
    Node& n = g.addNode("func.call", x, y);
    n.props["function"] = fn;
    return n.id;
}

void hello(const std::string& dir) {
    Script s;
    s.name = "Hello";
    s.doc = "Prints a greeting and counts to five.";
    Graph& g = s.addEventGraph();
    NodeId ev = event(g, "Main");
    NodeId hi = print(g, 250, 0, "Hello, FlowGraph!");
    NodeId loop = add(g, "flow.for", 500, 0);
    g.findNode(loop)->defaults["First"] = Value(1);
    g.findNode(loop)->defaults["Last"] = Value(6);        // Last is exclusive: counts 1..5
    NodeId msg = concat(g, "Count: ", 760, 100);
    NodeId pr = print(g, 980, 40);
    link(g, ev, "Then", hi, "In");
    link(g, hi, "Then", loop, "In");
    link(g, loop, "Loop Body", pr, "In");
    link(g, loop, "Index", msg, "B");
    link(g, msg, "Result", pr, "String");
    g.addComment(480, -40, 700, 260).text = "Loop";
    saveScript(s, dir + "/hello.flow");
}

void fibonacci(const std::string& dir) {
    Script s;
    s.name = "Fibonacci";
    s.doc = "A recursive function.";
    Graph& f = s.addFunction("Fib", {{"N", "int", false, false, Value()}}, {{"Result", "int", false, false, Value()}});
    f.doc = "Returns the N-th Fibonacci number.";
    f.connections.clear();
    NodeId entry = f.nodes[0].id, retRec = f.nodes[1].id;
    f.findNode(retRec)->x = 1100;
    NodeId lt = add(f, "cmp.lt", 250, 120);
    f.findNode(lt)->defaults["B"] = Value(2);
    NodeId br = add(f, "flow.branch", 450, 0);
    NodeId retBase = add(f, "func.return", 700, -120);
    NodeId sub1 = add(f, "math.sub", 450, 260);
    f.findNode(sub1)->defaults["B"] = Value(1);
    NodeId sub2 = add(f, "math.sub", 450, 400);
    f.findNode(sub2)->defaults["B"] = Value(2);
    NodeId a = call(f, "Fib", 700, 120);
    NodeId b = call(f, "Fib", 900, 120);
    NodeId sum = add(f, "math.add", 900, 330);
    link(f, entry, "Then", br, "In");
    link(f, entry, "N", lt, "A");
    link(f, lt, "Result", br, "Condition");
    link(f, br, "True", retBase, "In");
    link(f, entry, "N", retBase, "Result");
    link(f, br, "False", a, "In");
    link(f, entry, "N", sub1, "A");
    link(f, entry, "N", sub2, "A");
    link(f, sub1, "Result", a, "N");
    link(f, a, "Then", b, "In");
    link(f, sub2, "Result", b, "N");
    link(f, b, "Then", retRec, "In");
    link(f, a, "Result", sum, "A");
    link(f, b, "Result", sum, "B");
    link(f, sum, "Result", retRec, "Result");

    Graph& g = s.addEventGraph();
    NodeId ev = event(g, "Main");
    NodeId c = call(g, "Fib", 250, 0);
    g.findNode(c)->defaults["N"] = Value(15);
    NodeId msg = concat(g, "Fib(15) = ", 500, 80);
    NodeId pr = print(g, 720, 0);
    link(g, ev, "Then", c, "In");
    link(g, c, "Then", pr, "In");
    link(g, c, "Result", msg, "B");
    link(g, msg, "Result", pr, "String");
    saveScript(s, dir + "/fibonacci.flow");
}

void inventory(const std::string& dir) {
    Script s;
    s.name = "Inventory";
    s.doc = "Arrays, maps, by-reference parameters and loops.";
    s.variables.push_back({"Items", "array<string>", Value(), "Inventory", "Distinct item names"});
    s.variables.push_back({"Stock", "map<string,int>", Value(), "Inventory", "Item -> amount"});

    // AddUnique(List: array<string>&, Name: string) - adds Name unless it is already there.
    Graph& f = s.addFunction("AddUnique", {{"List", "array<string>", true, false, Value()}, {"Name", "string", false, true, Value()}});
    f.connections.clear();
    NodeId entry = f.nodes[0].id;
    f.nodes.pop_back();                       // drop the default return node; the chain simply ends
    NodeId list = var(f, "var.get", "List", "param", 250, 160);
    NodeId has = add(f, "array.contains", 450, 160);
    NodeId br = add(f, "flow.branch", 700, 0);
    NodeId push = add(f, "array.add", 950, 60);
    link(f, entry, "Then", br, "In");
    link(f, list, "Value", has, "Array");
    link(f, entry, "Name", has, "Item");
    link(f, has, "Contains", br, "Condition");
    link(f, br, "False", push, "In");
    link(f, list, "Value", push, "Array");
    link(f, entry, "Name", push, "Item");

    Graph& g = s.addEventGraph();
    NodeId ev = event(g, "Main");
    NodeId items = var(g, "var.get", "Items", "global", 250, 300);
    NodeId stock = var(g, "var.get", "Stock", "global", 250, 460);
    const char* names[] = {"apple", "pear", "apple", "fig"};
    NodeId prev = ev;
    std::string prevPin = "Then";
    double x = 250;
    for (const char* n : names) {
        NodeId c = call(g, "AddUnique", x, 0);
        g.findNode(c)->defaults["Name"] = Value(n);
        link(g, prev, prevPin, c, "In");
        link(g, items, "Value", c, "List");
        prev = c;
        prevPin = "Then";
        x += 230;
    }
    // Stock["apple"] = 7
    NodeId setStock = add(g, "map.set", x, 0);
    g.findNode(setStock)->defaults["Key"] = Value("apple");
    g.findNode(setStock)->defaults["Value"] = Value(7);
    link(g, prev, prevPin, setStock, "In");
    link(g, stock, "Value", setStock, "Map");
    NodeId each = add(g, "flow.foreach", x + 230, 0);
    link(g, setStock, "Then", each, "In");
    link(g, items, "Value", each, "Array");
    NodeId get = add(g, "map.get", x + 230, 300);
    link(g, stock, "Value", get, "Map");
    link(g, each, "Element", get, "Key");
    NodeId label = add(g, "fn:String.Concat", x + 500, 220);
    g.findNode(label)->defaults["B"] = Value(" = ");
    NodeId label2 = add(g, "fn:String.Concat", x + 740, 220);
    NodeId pr = print(g, x + 980, 0);
    link(g, each, "Element", label, "A");
    link(g, get, "Value", label2, "B");
    link(g, label, "Result", label2, "A");
    link(g, label2, "Result", pr, "String");
    link(g, each, "Loop Body", pr, "In");
    saveScript(s, dir + "/inventory.flow");
}

} // namespace

int main(int argc, char** argv) {
    fg::Runtime runtime;
    fg::standard::registerStandardApp(runtime.registry());
    g_runtime = &runtime;
    std::string dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(dir);
    hello(dir);
    fibonacci(dir);
    inventory(dir);
    std::printf("wrote hello.flow, fibonacci.flow, inventory.flow to %s\n", dir.c_str());
    return 0;
}
