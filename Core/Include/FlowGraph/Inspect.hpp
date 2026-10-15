#pragma once
// Value inspection for debuggers and watch windows: turns runtime values into a printable tree.
#include <FlowGraph/Vm.hpp>

namespace fg {

struct ValueView {
    std::string name;       // member / element label ("" for the root)
    std::string type;       // type name
    std::string text;       // one line summary
    std::vector<ValueView> children;
    bool truncated = false; // more children exist than were collected
};

struct InspectOptions {
    int maxDepth = 3;
    size_t maxItems = 64;   // children per container / object
};

ValueView inspect(const Registry& reg, const Value& v, const TypeDesc* declared = nullptr, const InspectOptions& opt = {});
// Multi line, indented rendering of a view.
std::string toText(const ValueView& v, int indent = 0);

struct VariableView {
    enum class Scope : uint8_t { Global, Local, Param } scope = Scope::Global;
    ValueView value;        // value.name is the variable name
};

std::vector<VariableView> inspectGlobals(const Instance& inst, const InspectOptions& opt = {});
// Parameters and local variables of one frame of a task (frameIndex 0 = outermost frame).
std::vector<VariableView> inspectFrame(const Task& task, size_t frameIndex, const InspectOptions& opt = {});

} // namespace fg
