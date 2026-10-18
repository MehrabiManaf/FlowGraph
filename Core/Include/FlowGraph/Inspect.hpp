#pragma once
// Value inspection for debuggers and watch windows: turns runtime values into a printable tree.
//
// A tree only holds the first few children of every container (InspectOptions::maxItems), so a million element array is cheap to
// show. Every view that can be addressed carries a `path`, an expression that evaluateExpression() understands; asking for a range of
// that path ("Items[64..319]") returns the next window of children, which is how a debugger pages through big containers.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Vm.hpp>

namespace fg {

struct ValueView {
    std::string name;       // member / element label ("" for the root)
    std::string type;       // type name
    std::string text;       // one line summary
    std::vector<ValueView> children;
    bool truncated = false; // more children exist than were collected
    std::string path;       // expression that addresses this value ("Items[3].Name"); empty when it cannot be addressed
    size_t total = 0;       // number of elements / entries / properties of a container or object
    size_t offset = 0;      // index of the first collected child: the children are the window [offset, offset + children.size())
};

struct InspectOptions {
    int maxDepth = 3;
    size_t maxItems = 64;       // children per container / object
    size_t maxRange = 50000;    // most children a range expression ("a[0..99999]") delivers
    std::string rootPath;       // path of the inspected value; the paths of the children are built from it (empty = no paths)
};

FG_CORE_EXPORT ValueView inspect(const Registry& reg, const Value& v, const TypeDesc* declared = nullptr, const InspectOptions& opt = {});
// Multi line, indented rendering of a view.
FG_CORE_EXPORT std::string toText(const ValueView& v, int indent = 0);

struct VariableView {
    enum class Scope : uint8_t { Global, Local, Param } scope = Scope::Global;
    ValueView value;        // value.name is the variable name
};

FG_CORE_EXPORT std::vector<VariableView> inspectGlobals(const Instance& inst, const InspectOptions& opt = {});
// Parameters and local variables of one frame of a task (frameIndex 0 = outermost frame).
FG_CORE_EXPORT std::vector<VariableView> inspectFrame(const Task& task, size_t frameIndex, const InspectOptions& opt = {});

struct EvalResult {
    bool ok = false;
    std::string error;
    ValueView view;         // view.path is the canonical form of the expression (a range keeps the path of the container)
};
// Evaluates a watch expression:
//   Name                  a variable: the parameters / locals of `task`'s frame `frameIndex` first (task may be null), then the globals
//   Name.Property         a property of an object
//   Name[3]               an array element, or the map entry with key 3
//   Name["key"]           the map entry with key "key" (a bare word is a string key too: Name[key])
//   Name[100..199]        elements 100 to 199 (inclusive, clamped) of an array; entries (by position) of a map; properties of an object
//   Name[100..]  Name[..9]  open ranges
// and any chain of those: Orders[12].Items["apple"].Count. A range can only be the last step. Nothing is modified.
FG_CORE_EXPORT EvalResult evaluateExpression(const Instance& inst, const Task* task, size_t frameIndex, const std::string& expr, const InspectOptions& opt = {});

} // namespace fg
