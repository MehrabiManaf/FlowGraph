// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Runtime: the facade hosts use. Owns the registry (with all built-ins), loads plugins, compiles
// scripts and creates instances.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Builtins.hpp>
#include <FlowGraph/Compatibility.hpp>
#include <FlowGraph/Compiler.hpp>
#include <FlowGraph/Executor.hpp>
#include <FlowGraph/StandardApp.hpp>
#include <FlowGraph/Inspect.hpp>
#include <FlowGraph/Intellisense.hpp>
#include <FlowGraph/Plugin.hpp>
#include <FlowGraph/Refactor.hpp>
#include <FlowGraph/Reference.hpp>
#include <FlowGraph/Search.hpp>
#include <FlowGraph/System.hpp>
#include <FlowGraph/Translations.hpp>
#include <FlowGraph/Vm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace fg {

class FG_CORE_EXPORT Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Registry& registry() { return *reg_; }
    const Registry& registry() const { return *reg_; }

    std::shared_ptr<Program> compile(const Script& s, const CompileOptions& options = {}) const { return fg::compile(s, *reg_, options); }

    // Reads a script file and checks it against this runtime. `error` is set when the file cannot be read or parsed;
    // `report` lists everything the script needs that this application does not provide (events, plugins, types).
    // Applications that only run scripts refuse a script when !ok(); editors may open it anyway.
    struct FG_CORE_EXPORT LoadedScript {
        Script script;
        CompatibilityReport report;
        std::string error;
        bool ok() const { return error.empty() && report.ok(); }
        std::string explanation() const;      // text for a "cannot load this script" message
    };
    LoadedScript loadScript(const std::string& path) const;
    std::shared_ptr<Instance> instantiate(std::shared_ptr<const Program> p) const { return std::make_shared<Instance>(std::move(p)); }

    // Loads a plugin library and lets it register its extensions. Returns false and sets `error` on failure.
    bool loadPlugin(const std::string& path, std::string* error = nullptr);
    // Loads every plugin library (.dll / .so / .dylib) found in a directory; returns how many were loaded.
    int loadPluginsFrom(const std::string& dir, std::vector<std::string>* errors = nullptr);
    // Several directories separated like PATH (';' on Windows, ':' elsewhere).
    int loadPluginsFromPathList(const std::string& list, std::vector<std::string>* errors = nullptr);
    // The directories listed in an environment variable (FLOW_PLUGIN_PATH by default); nothing happens when it is unset.
    int loadPluginsFromEnvironment(const char* variable = "FLOW_PLUGIN_PATH", std::vector<std::string>* errors = nullptr);
    const std::vector<std::string>& loadedPlugins() const { return pluginNames_; }

private:
    // Libraries must outlive the registry (it holds code that lives inside them), so they are declared first.
    struct Libs;
    std::unique_ptr<Libs> libs_;
    std::unique_ptr<Registry> reg_;
    std::vector<std::string> pluginNames_;
    std::vector<std::string> pluginPaths_;       // canonical paths of the libraries that are loaded
};

} // namespace fg
