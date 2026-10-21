// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// Plugins that Runtime::loadPlugin must refuse (built twice, see Tests/CMakeLists.txt):
//   FG_TEST_LEGACY_PLUGIN    exports only fgRegisterPlugin, like a plugin built before the ABI check existed
//   FG_TEST_MISMATCH_PLUGIN  reports another plugin ABI version
// Neither may register anything: the loader checks the ABI before it calls fgRegisterPlugin.
#include <FlowGraph/Plugin.hpp>

#if defined(FG_TEST_LEGACY_PLUGIN)
FG_PLUGIN_EXPORT void fgRegisterPlugin(::fg::Registry& reg) { reg.addEvent("LegacyPluginRegistered"); }
#elif defined(FG_TEST_MISMATCH_PLUGIN)
FG_PLUGIN_EXPORT const ::fg::PluginAbi* fgPluginAbi() {
    static ::fg::PluginAbi abi = [] {
        ::fg::PluginAbi a = ::fg::currentPluginAbi();
        a.version += 1;
        return a;
    }();
    return &abi;
}
FG_PLUGIN_EXPORT void fgRegisterPlugin(::fg::Registry& reg) { reg.addEvent("MismatchPluginRegistered"); }
#endif
