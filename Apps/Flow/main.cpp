// flow: runs a FlowGraph script, like `python script.py`.
//
//   flow [options] script.flow [arguments...]
//
// The arguments after the script are passed to its Main event (Args: array<string>). Plugins are loaded from the
// directories listed in the FLOW_PLUGIN_PATH environment variable and from the `plugins` folder next to the program.
// Exit code: the code given to the Exit node, 0 when the script ends normally, 1 when it fails at run time and 2
// when it cannot be loaded (unreadable file, missing events / plugins, compile errors).
#include <FlowGraph/Runtime.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#define fg_chdir _chdir
#define fg_putenv(k, v) _putenv_s(k, v)
#else
#include <unistd.h>
#define fg_chdir chdir
#define fg_putenv(k, v) setenv(k, v, 1)
#endif

using namespace fg;
namespace fs = std::filesystem;

namespace {

fg::Instance* g_running = nullptr;
void onInterrupt(int) {
    if (g_running) g_running->requestAbort();       // an atomic flag: safe from a signal handler
}

void usage() {
    std::puts(
        "usage: flow [options] script.flow [arguments...]\n"
        "\n"
        "options:\n"
        "  --check               load and compile the script, but do not run it\n"
        "  --plugins DIR         load the plugins in DIR as well (may be repeated)\n"
        "  --launch NAME         use the configuration NAME of script.flowlaunch (arguments, environment,\n"
        "                        working directory, plugin directories); command line arguments are appended\n"
        "  --version             print the version\n"
        "  --help                show this text\n"
        "  --                    end of options (the next word is the script)\n"
        "\n"
        "Plugins are also loaded from the directories in the FLOW_PLUGIN_PATH environment variable and from the\n"
        "'plugins' folder next to this program.");
}

int fail(const std::string& msg, int code = 2) {
    std::fprintf(stderr, "flow: %s\n", msg.c_str());
    return code;
}

} // namespace

int main(int argc, char** argv) {
    std::string script, launchName;
    std::vector<std::string> scriptArgs, extraPlugins;
    bool checkOnly = false, launch = false;
    int i = 1;
    for (; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--") { ++i; break; }
        if (a.size() < 2 || a[0] != '-') break;
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (a == "--version") { std::puts("flow 0.2"); return 0; }
        if (a == "--check") checkOnly = true;
        else if (a == "--plugins" && i + 1 < argc) extraPlugins.push_back(argv[++i]);
        else if (a == "--launch" && i + 1 < argc) { launch = true; launchName = argv[++i]; }
        else return fail("unknown option '" + a + "' (see flow --help)");
    }
    if (i >= argc) {
        usage();
        return 2;
    }
    script = argv[i++];
    for (; i < argc; ++i) scriptArgs.push_back(argv[i]);

    Runtime rt;
    standard::registerStandardApp(rt.registry());

    // launch configuration
    standard::LaunchConfig cfg;
    fs::path scriptDir = fs::absolute(fs::path(script)).parent_path();
    if (launch) {
        std::string lf = standard::LaunchFile::pathFor(script);
        if (!fs::exists(lf)) return fail("there is no launch file '" + lf + "'");
        try {
            auto file = standard::LaunchFile::load(lf);
            const standard::LaunchConfig* found = file.find(launchName);
            if (!found) return fail("'" + lf + "' has no configuration named '" + launchName + "'");
            cfg = *found;
        } catch (const std::exception& e) {
            return fail("cannot read '" + lf + "': " + e.what());
        }
        for (auto& p : cfg.pluginPaths) extraPlugins.push_back((scriptDir / p).string());
    }

    // plugins
    std::vector<std::string> errors;
    rt.loadPluginsFromEnvironment("FLOW_PLUGIN_PATH", &errors);
    fs::path self = fs::absolute(fs::path(argv[0]));
    if (fs::exists(self.parent_path() / "plugins")) rt.loadPluginsFrom((self.parent_path() / "plugins").string(), &errors);
    for (auto& d : extraPlugins) {
        if (!fs::exists(d)) { errors.push_back("plugin directory '" + d + "' does not exist"); continue; }
        rt.loadPluginsFrom(d, &errors);
    }
    for (auto& e : errors) std::fprintf(stderr, "flow: warning: %s\n", e.c_str());

    // load: readable, and made for this application
    auto loaded = rt.loadScript(script);
    if (!loaded.ok()) return fail("cannot load '" + script + "': " + loaded.explanation());
    auto program = rt.compile(loaded.script);
    bool errorsFound = false;
    for (auto& d : program->diagnostics) {
        bool err = d.severity == Diagnostic::Severity::Error;
        errorsFound |= err;
        std::fprintf(stderr, "%s: %s: [%s #%d] %s\n", script.c_str(), err ? "error" : "warning", d.graph.c_str(), d.node, d.message.c_str());
    }
    if (errorsFound) return 2;
    if (!program->events.count(standard::kMainEvent)) return fail("'" + script + "' has no Main event, so it cannot be run");
    if (checkOnly) {
        std::printf("%s: ok\n", script.c_str());
        return 0;
    }

    // environment of the run
    for (auto& [k, v] : cfg.env) fg_putenv(k.c_str(), v.c_str());
    if (!cfg.cwd.empty() && fg_chdir((scriptDir / cfg.cwd).string().c_str()) != 0) return fail("cannot change to the working directory '" + cfg.cwd + "'");
    std::vector<std::string> args = cfg.args;
    args.insert(args.end(), scriptArgs.begin(), scriptArgs.end());

    Instance inst(program);
    inst.setStepLimit(0);                       // no limit: a long running script is not a runaway loop
    g_running = &inst;
    std::signal(SIGINT, onInterrupt);
    auto outcome = standard::runMain(inst, args);
    g_running = nullptr;
    return outcome.exitCode;
}
