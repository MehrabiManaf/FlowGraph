// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// Plugins that Runtime::loadPlugin must refuse (built once for each, see Tests/CMakeLists.txt):
//   FG_TEST_LEGACY_PLUGIN    exports only fgRegisterPlugin, like a plugin built before the ABI check existed
//   FG_TEST_MISMATCH_PLUGIN  reports another plugin ABI version
//   FG_TEST_EVENT_PLUGIN     declares an event (the application's to declare)
// The first two may not register anything: the loader checks the ABI before it calls fgRegisterPlugin.
#include <FlowGraph/Plugin.hpp>

#if defined(FG_TEST_LEGACY_PLUGIN)
FG_PLUGIN_EXPORT void fgRegisterPlugin(::fg::Registry& reg) { reg.addFunction("Test.Legacy Plugin Registered", [] { return 0; }); }
#elif defined(FG_TEST_MISMATCH_PLUGIN)
FG_PLUGIN_EXPORT const ::fg::PluginAbi* fgPluginAbi() {
    static ::fg::PluginAbi abi = [] {
        ::fg::PluginAbi a = ::fg::currentPluginAbi();
        a.version += 1;
        return a;
    }();
    return &abi;
}
FG_PLUGIN_EXPORT void fgRegisterPlugin(::fg::Registry& reg) { reg.addFunction("Test.Mismatch Plugin Registered", [] { return 0; }); }
#elif defined(FG_TEST_EVENT_PLUGIN)
FG_PLUGIN(reg) { reg.addEvent("PluginEvent", {{"Message", "string"}}); }
#endif
