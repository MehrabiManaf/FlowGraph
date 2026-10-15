// fgrun: command line runner for FlowGraph scripts.
//   fgrun script.flow [--call Name] [--arg value]... [--plugins dir] [--check] [--time seconds]
// Runs the event or function `Name` (default "Main") and keeps ticking latent nodes (Delay) until
// every task is done or the simulated time limit is reached.
#include <FlowGraph/Runtime.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace fg;

static Value guessValue(const std::string& s) {
    if (s == "true") return Value(true);
    if (s == "false") return Value(false);
    char* end = nullptr;
    long long i = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() && *end == '\0') return Value(static_cast<int64_t>(i));
    double d = std::strtod(s.c_str(), &end);
    if (end != s.c_str() && *end == '\0') return Value(d);
    return Value(s);
}

int main(int argc, char** argv) {
    std::string file, call = "Main", pluginDir;
    std::vector<Value> args;
    bool checkOnly = false;
    double timeLimit = 60.0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--call") call = next();
        else if (a == "--arg") args.push_back(guessValue(next()));
        else if (a == "--plugins") pluginDir = next();
        else if (a == "--check") checkOnly = true;
        else if (a == "--time") timeLimit = std::atof(next().c_str());
        else if (file.empty()) file = a;
        else { std::fprintf(stderr, "unexpected argument '%s'\n", a.c_str()); return 2; }
    }
    if (file.empty()) {
        std::fprintf(stderr, "usage: fgrun script.flow [--call Name] [--arg value]... [--plugins dir] [--check] [--time seconds]\n");
        return 2;
    }

    Runtime rt;
    if (pluginDir.empty()) pluginDir = (std::filesystem::path(argv[0]).parent_path() / "plugins").string();
    std::vector<std::string> errors;
    if (std::filesystem::exists(pluginDir)) rt.loadPluginsFrom(pluginDir, &errors);
    for (auto& e : errors) std::fprintf(stderr, "plugin: %s\n", e.c_str());

    Script script;
    try {
        script = Script::load(file);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", file.c_str(), e.what());
        return 1;
    }
    auto prog = rt.compile(script);
    for (auto& d : prog->diagnostics)
        std::fprintf(stderr, "%s: [%s #%d] %s\n", d.severity == Diagnostic::Severity::Error ? "error" : "warning", d.graph.c_str(), d.node, d.message.c_str());
    if (!prog->ok()) return 1;
    if (checkOnly) {
        std::printf("%s: ok\n", file.c_str());
        return 0;
    }

    Instance inst(prog);
    RunResult r = prog->functions.count(call) ? inst.callFunction(call, args) : inst.callEvent(call, args);
    double t = 0;
    while (r.task && !r.task->done() && t < timeLimit) {
        inst.tick(0.05);
        t += 0.05;
    }
    if (r.task && !r.task->done()) { std::fprintf(stderr, "stopped: time limit reached while waiting\n"); return 1; }
    if (r.task && r.task->state() == TaskState::Failed) return 1;
    if (!r.task && !r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
    for (auto& v : r.returns) std::printf("=> %s\n", v.toString().c_str());
    return 0;
}
