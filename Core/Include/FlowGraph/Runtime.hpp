#pragma once
// Runtime: the facade hosts use. Owns the registry (with all built-ins), loads plugins, compiles
// scripts and creates instances.
#include <FlowGraph/Builtins.hpp>
#include <FlowGraph/Compiler.hpp>
#include <FlowGraph/Plugin.hpp>
#include <FlowGraph/Vm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace fg {

class Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Registry& registry() { return *reg_; }
    const Registry& registry() const { return *reg_; }

    std::shared_ptr<Program> compile(const Script& s) const { return fg::compile(s, *reg_); }
    std::shared_ptr<Instance> instantiate(std::shared_ptr<const Program> p) const { return std::make_shared<Instance>(std::move(p)); }

    // Loads a plugin library and lets it register its extensions. Returns false and sets `error` on failure.
    bool loadPlugin(const std::string& path, std::string* error = nullptr);
    // Loads every plugin library (.dll / .so / .dylib) found in a directory; returns how many were loaded.
    int loadPluginsFrom(const std::string& dir, std::vector<std::string>* errors = nullptr);
    const std::vector<std::string>& loadedPlugins() const { return pluginNames_; }

private:
    // Libraries must outlive the registry (it holds code that lives inside them), so they are declared first.
    struct Libs;
    std::unique_ptr<Libs> libs_;
    std::unique_ptr<Registry> reg_;
    std::vector<std::string> pluginNames_;
};

} // namespace fg
