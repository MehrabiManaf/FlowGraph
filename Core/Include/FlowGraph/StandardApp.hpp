#pragma once
// The "standard application": the contract shared by `flow` (runs a script) and `flowstudio` (edits and debugs it).
// A standard application raises exactly one event, Main(Args: array<string>), and scripts can end it early with the
// Exit node. Any script that only uses Main runs in both programs.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Json.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>
#include <FlowGraph/Vm.hpp>

#include <map>
#include <string>
#include <vector>

namespace fg::standard {

inline constexpr const char* kMainEvent = "Main";

// A new script of a standard application: one event graph that handles Main and prints a greeting.
FG_CORE_EXPORT void createMainTemplate(Script& script, const Registry& reg);

// Declares the event Main(Args: array<string>) and the "Exit" node (category Application).
FG_CORE_EXPORT void registerStandardApp(Registry& reg);

// array<string> value of the command line arguments of a script.
FG_CORE_EXPORT Value argsValue(const Registry& reg, const std::vector<std::string>& args);

struct RunOutcome {
    bool ok = false;             // the script finished (or exited) without an error
    bool exited = false;         // ... by an Exit node
    int exitCode = 0;            // code of Exit; 1 when the script failed; 0 otherwise
    std::string error;
};
// Runs Main(args) to completion on the calling thread. This is the fast path used by `flow`: no scheduler, no
// extra threads; Delay nodes really sleep. Log lines go to the instance's log handler.
FG_CORE_EXPORT RunOutcome runMain(Instance& instance, const std::vector<std::string>& args);

// How a script is launched by flowstudio (and optionally `flow --launch NAME`). Stored next to the script as
// <script>.flowlaunch (JSON), see LaunchFile.
struct LaunchConfig {
    std::string name = "Default";
    std::vector<std::string> args;                 // arguments passed to Main
    std::string cwd;                               // working directory, relative to the script's folder; empty = unchanged
    std::map<std::string, std::string> env;        // environment variables set while the script runs
    std::vector<std::string> pluginPaths;          // extra plugin directories (relative to the script's folder)
    int stepDelayMs = 0;                           // slow motion while debugging
};

struct FG_CORE_EXPORT LaunchFile {
    std::string selected;                          // name of the active configuration
    std::vector<LaunchConfig> configs;

    LaunchConfig* find(const std::string& name);
    const LaunchConfig* find(const std::string& name) const;
    // The selected configuration (the first one when `selected` is unknown); an empty file gets a default one.
    LaunchConfig& current();
    Json toJson() const;
    static LaunchFile fromJson(const Json& j);     // throws JsonError
    // A missing file is not an error: the result holds one default configuration.
    static LaunchFile load(const std::string& path);
    void save(const std::string& path) const;
    static std::string pathFor(const std::string& scriptPath);   // "dir/foo.flow" -> "dir/foo.flowlaunch"
};

} // namespace fg::standard
