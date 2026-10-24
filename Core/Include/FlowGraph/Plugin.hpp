// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Plugin ABI. A plugin is a shared library exporting `fgRegisterPlugin`; it adds classes, functions,
// enums and nodes to the registry. It declares no events: the application raises its events, so only the
// application declares them (Runtime::loadPlugin refuses a plugin that does). Build it against the same
// FlowGraphCore version and compiler.
//
//   FG_PLUGIN(reg) {
//       reg.addFunction("My.Hello", [] { return std::string("hi"); }).makePure();
//   }
//
// FG_PLUGIN also exports `fgPluginAbi`, a description of how the plugin was built (ABI version, compiler, standard
// library flavour and the sizes of the classes the host and the plugin share). Runtime::loadPlugin compares it with its
// own and refuses a plugin that does not match - a mismatched plugin would otherwise crash the host in unpredictable ways.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Vm.hpp>

#include <cstdint>
#include <functional>
#include <string>

#if defined(_WIN32)
#define FG_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define FG_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Bump when the meaning of something plugins rely on changes without changing a class size (see PluginAbi::layout).
#define FG_PLUGIN_ABI_VERSION 2          // 2: the byte and bytes types (Value::Kind, TypeKind)

namespace fg {

using PluginRegisterFn = void (*)(Registry&);

struct PluginAbi {
    uint32_t size = sizeof(PluginAbi);   // grows when fields are added; compared first
    uint32_t version = 0;                // FG_PLUGIN_ABI_VERSION
    uint32_t compiler = 0;               // 1 = MSVC ABI (cl, clang-cl), 2 = Itanium ABI (GCC, Clang)
    uint32_t flavour = 0;                // standard library / runtime variant (debug CRT, iterator debugging, libstdc++ ABI)
    uint32_t layout = 0;                 // hash of the sizes of the classes the host and the plugin share
};
using PluginAbiFn = const PluginAbi* (*)();

// What this translation unit was built with. Inline, so the host (inside FlowGraphCore) and every plugin compute it
// from their own compiler settings and their own copy of the headers.
inline PluginAbi currentPluginAbi() {
    PluginAbi a;
    a.version = FG_PLUGIN_ABI_VERSION;
#if defined(_MSC_VER)
    a.compiler = 1;
#if defined(_DEBUG)
    a.flavour |= 1u;
#endif
#if defined(_ITERATOR_DEBUG_LEVEL)
    a.flavour |= static_cast<uint32_t>(_ITERATOR_DEBUG_LEVEL) << 1;
#endif
#else
    a.compiler = 2;
#if defined(_GLIBCXX_DEBUG)
    a.flavour |= 1u;
#endif
#if defined(_GLIBCXX_USE_CXX11_ABI)
    a.flavour |= static_cast<uint32_t>(_GLIBCXX_USE_CXX11_ABI) << 1;
#endif
#endif
    const size_t sizes[] = {sizeof(Value), sizeof(TypeDesc), sizeof(FunctionDesc), sizeof(PropertyDesc), sizeof(EventDesc),
                            sizeof(PinDef), sizeof(NodeDef), sizeof(Registry), sizeof(NodeCtx), sizeof(Frame), sizeof(CompiledNode),
                            sizeof(std::string), sizeof(std::function<void()>), sizeof(std::shared_ptr<void>)};
    uint32_t h = 2166136261u;            // FNV-1a over the sizes
    for (size_t s : sizes) {
        h ^= static_cast<uint32_t>(s);
        h *= 16777619u;
    }
    a.layout = h;
    return a;
}

// Why a plugin built as `plugin` cannot be loaded by a host built as `host` (empty when they match).
FG_CORE_EXPORT std::string pluginAbiMismatch(const PluginAbi& plugin, const PluginAbi& host);

} // namespace fg

#define FG_PLUGIN(regName)                                                         \
    FG_PLUGIN_EXPORT const ::fg::PluginAbi* fgPluginAbi() {                        \
        static const ::fg::PluginAbi abi = ::fg::currentPluginAbi();               \
        return &abi;                                                               \
    }                                                                              \
    FG_PLUGIN_EXPORT void fgRegisterPlugin(::fg::Registry& regName)
