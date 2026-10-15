// A sample plugin: reflects a small C++ class, a free function and a host event.
#include <FlowGraph/Plugin.hpp>

namespace {

class Counter {
public:
    int value = 0;
    int step = 1;
    void increment() { value += step; }
    void add(int amount) { value += amount; }
    void reset() { value = 0; }
    bool isZero() const { return value == 0; }
};

} // namespace

FG_PLUGIN(reg) {
    reg.registerClass<Counter>("Counter", "Example")
        .doc("A counter owned by the host.")
        .constructor<>()
        .property("Value", &Counter::value)
        .property("Step", &Counter::step)
        .method("Increment", &Counter::increment)
        .method("Add", &Counter::add, {"Amount"})
        .method("Reset", &Counter::reset)
        .method("Is Zero", &Counter::isZero);

    reg.addFunction("Example.Greet", [](const std::string& who) { return "Hello, " + who + "!"; }, {"Name"})
        .makePure()
        .withDefault(0, fg::Value("world"))
        .withDoc("Builds a greeting.");

    reg.addEvent("OnExample", {{"Message", "string"}});
}
