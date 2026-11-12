// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// flowstudio-next: FlowGraph Studio on GLWidgets, the editor and debugger of the standard applications (the ones `flow`
// runs), while it grows to take the place of flowstudio.
//
//   flowstudio-next [script.flow] [--frames N] [--run]
//
// Plugins are loaded like `flow` does: FLOW_PLUGIN_PATH and the plugins folder next to the program. --frames N draws N
// frames and exits: a smoke test. --run runs the script once it is open (what Debug > Run does).
#include <FlowGraph/Runtime.hpp>
#include <FlowGraph/StandardApp.hpp>
#include <FlowGraphGLEditor/Studio.hpp>
#include <GLWidgets/Widgets/Application.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

// The arguments, in UTF-8 (on Windows, from the wide command line: argv is in the code page, not every path fits it).
std::vector<std::string> arguments(int argc, char** argv) {
#ifdef _WIN32
    std::vector<std::string> result;
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 0; i < count; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(static_cast<std::size_t>(std::max(0, size - 1)), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
            result.push_back(std::move(arg));
        }
        LocalFree(wide);
        return result;
    }
#endif
    return {argv, argv + argc};
}

std::filesystem::path pathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = arguments(argc, argv);
    std::string script;
    int frames = -1;
    bool runAtStart = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--run")
            runAtStart = true;
        else if (args[i] == "--frames" && i + 1 < args.size())
            frames = std::max(1, std::atoi(args[++i].c_str()));
        else
            script = args[i];
    }
    try {
        glw::Application app;

        fg::Runtime rt;
        fg::standard::registerStandardApp(rt.registry());
        std::vector<std::string> errors;
        rt.loadPluginsFromEnvironment("FLOW_PLUGIN_PATH", &errors);
        const std::filesystem::path plugins = std::filesystem::absolute(pathOf(args.empty() ? std::string() : args[0])).parent_path() / "plugins";
        if (std::filesystem::exists(plugins)) {
            const std::u8string folder = plugins.u8string();
            rt.loadPluginsFrom(std::string(folder.begin(), folder.end()), &errors);
        }
        for (const std::string& e : errors)
            std::fprintf(stderr, "flowstudio-next: plugin: %s\n", e.c_str());

        fged::gl::Studio studio(app, rt,
                                {.applicationName = "FlowGraph Studio",
                                 .settingsName = "FlowGraph Studio Next",
                                 .newScript = [&rt](fg::Script& s) { fg::standard::createMainTemplate(s, rt.registry()); },
                                 // Run: Main, with no arguments (launch configurations come later)
                                 .runEntry = fg::standard::kMainEvent,
                                 .runArguments = [&rt] { return std::vector<fg::Value>{fg::standard::argsValue(rt.registry(), {})}; },
                                 .launchConfigurations = true});
        if (!script.empty())
            studio.open(script);
        if (runAtStart)
            studio.runScript();
        return app.run(frames);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "flowstudio-next: %s\n", e.what());
        return 1;
    }
}
