// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Runtime.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fg {

struct Runtime::Libs {
#if defined(_WIN32)
    std::vector<HMODULE> handles;
    ~Libs() { for (auto it = handles.rbegin(); it != handles.rend(); ++it) FreeLibrary(*it); }
#else
    std::vector<void*> handles;
    ~Libs() { for (auto it = handles.rbegin(); it != handles.rend(); ++it) dlclose(*it); }
#endif
};

Runtime::Runtime() : libs_(std::make_unique<Libs>()), reg_(std::make_unique<Registry>()) { registerBuiltins(*reg_); }

Runtime::~Runtime() {
    reg_.reset();     // before the libraries are unloaded
    libs_.reset();
}

bool Runtime::loadPlugin(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    std::filesystem::path p(path);
    std::string name = p.stem().string();
    // The same library may be reachable through several directories of the plugin path. It is remembered once it has
    // been loaded: a library that failed is tried again (and fails with its reason again) instead of being skipped.
    std::error_code canonError;
    std::string canon = std::filesystem::weakly_canonical(p, canonError).string();
    if (!canonError && std::find(pluginPaths_.begin(), pluginPaths_.end(), canon) != pluginPaths_.end()) return true;
#if defined(_WIN32)
    HMODULE h = LoadLibraryW(p.wstring().c_str());
    if (!h) return fail("cannot load '" + path + "' (error " + std::to_string(GetLastError()) + ")");
    auto unload = [h] { FreeLibrary(h); };
    auto symbol = [h](const char* s) { return reinterpret_cast<void*>(GetProcAddress(h, s)); };
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) return fail(std::string("cannot load '") + path + "': " + dlerror());
    auto unload = [h] { dlclose(h); };
    auto symbol = [h](const char* s) { return dlsym(h, s); };
#endif
    auto fn = reinterpret_cast<PluginRegisterFn>(symbol("fgRegisterPlugin"));
    if (!fn) { unload(); return fail("'" + path + "' does not export fgRegisterPlugin"); }
    // Check how the plugin was built before any of its code touches the registry.
    auto abiFn = reinterpret_cast<PluginAbiFn>(symbol("fgPluginAbi"));
    if (!abiFn) {
        unload();
        return fail("plugin '" + name + "' was built for an older FlowGraph (it has no ABI information): rebuild it with this version");
    }
    std::string mismatch = pluginAbiMismatch(*abiFn(), currentPluginAbi());
    if (!mismatch.empty()) { unload(); return fail("plugin '" + name + "' cannot be loaded: " + mismatch); }
    libs_->handles.push_back(h);
    reg_->beginSource(name);
    try {
        fn(*reg_);
    } catch (const std::exception& e) {
        reg_->endSource();
        return fail("plugin '" + name + "' failed to register: " + e.what());
    }
    reg_->endSource();
    pluginNames_.push_back(name);
    if (!canonError) pluginPaths_.push_back(canon);
    return true;
}

std::string pluginAbiMismatch(const PluginAbi& plugin, const PluginAbi& host) {
    // `size` first: the other fields of a smaller (older) description may not exist.
    if (plugin.size != host.size || plugin.version != host.version)
        return "it was built for plugin ABI version " + std::to_string(plugin.size == host.size ? plugin.version : 0) + ", this application uses version " +
               std::to_string(host.version) + " (rebuild the plugin with this FlowGraph)";
    if (plugin.compiler != host.compiler) return "it was built with a compiler of a different ABI family than this application";
    if (plugin.flavour != host.flavour)
        return "it was built with another standard library variant than this application (Debug and Release builds cannot be mixed)";
    if (plugin.layout != host.layout) return "it was built against other FlowGraph headers than this application (rebuild the plugin with this FlowGraph)";
    return {};
}

int Runtime::loadPluginsFrom(const std::string& dir, std::vector<std::string>* errors) {
    namespace fs = std::filesystem;
    int n = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext != ".dll" && ext != ".so" && ext != ".dylib") continue;
        std::string err;
        if (loadPlugin(e.path().string(), &err)) ++n;
        else if (errors) errors->push_back(err);
    }
    return n;
}

int Runtime::loadPluginsFromPathList(const std::string& list, std::vector<std::string>* errors) {
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    int n = 0;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(sep, start);
        if (end == std::string::npos) end = list.size();
        std::string dir = list.substr(start, end - start);
        if (!dir.empty() && std::filesystem::exists(dir)) n += loadPluginsFrom(dir, errors);
        start = end + 1;
    }
    return n;
}

int Runtime::loadPluginsFromEnvironment(const char* variable, std::vector<std::string>* errors) {
    const char* v = std::getenv(variable);
    return v ? loadPluginsFromPathList(v, errors) : 0;
}

std::string Runtime::LoadedScript::explanation() const {
    if (!error.empty()) return error;
    return "this script cannot be loaded by this application:\n" + report.toText();
}

Runtime::LoadedScript Runtime::loadScript(const std::string& path) const {
    LoadedScript out;
    try {
        out.script = Script::load(path);
    } catch (const std::exception& e) {
        out.error = std::string("cannot read '") + path + "': " + e.what();
        return out;
    }
    out.report = checkCompatibility(out.script, *reg_);
    return out;
}

} // namespace fg
