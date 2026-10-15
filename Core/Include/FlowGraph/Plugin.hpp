#pragma once
// Plugin ABI. A plugin is a shared library exporting `fgRegisterPlugin`; it adds classes, functions,
// events and nodes to the registry. Build it against the same FlowGraphCore version and compiler.
//
//   FG_PLUGIN(reg) {
//       reg.addFunction("My.Hello", [] { return std::string("hi"); }).makePure();
//   }
#include <FlowGraph/Registry.hpp>

#if defined(_WIN32)
#define FG_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define FG_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define FG_PLUGIN(regName) FG_PLUGIN_EXPORT void fgRegisterPlugin(::fg::Registry& regName)

namespace fg {
using PluginRegisterFn = void (*)(Registry&);
}
