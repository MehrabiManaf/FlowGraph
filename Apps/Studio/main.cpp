// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// flowstudio: FlowGraph Studio, the editor and debugger of the standard applications (the ones `flow` runs), on GLWidgets.
//
//   flowstudio [script.flow] [options]
//
// Plugins are loaded like `flow` does: FLOW_PLUGIN_PATH and the plugins folder next to the program. The options are aids
// for tests and for the pictures of the documentation:
//   --run                 runs the script once it is open (what Debug > Run does)
//   --break GRAPH:NODE    a breakpoint on that node (its id) before the run; may be repeated
//   --screenshot FILE     saves a picture of the window as a PNG file after --delay, then exits (1: it could not)
//   --record FOLDER       saves --record-count pictures (frame_0000.png...) every --record-every ms from --delay on,
//                         then exits: the frames of a recording
//   --delay MS            when --screenshot or --record starts (1500 ms)
//   --frames N            draws N frames and exits: a smoke test (1: the frames did not reach the window)
//   --version             prints the version
#include <FlowGraph/Runtime.hpp>
#include <FlowGraph/StandardApp.hpp>
#include <FlowGraph/Vm.hpp>
#include <FlowGraphGLEditor/Studio.hpp>
#include <GLWidgets/Widgets/Animation.hpp>
#include <GLWidgets/Widgets/Application.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef FG_VERSION
#define FG_VERSION "unknown"
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

// A program with a window and no console (Windows): what it prints goes to the console it was started from, if any.
// (Not when its output goes elsewhere already: into a file, a pipe.)
void attachConsole() {
#ifdef _WIN32
    const auto missing = [](DWORD which) {
        const HANDLE handle = GetStdHandle(which);
        return handle == nullptr || handle == INVALID_HANDLE_VALUE;
    };
    const bool out = missing(STD_OUTPUT_HANDLE), err = missing(STD_ERROR_HANDLE);
    if ((out || err) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        if (out)
            freopen_s(&stream, "CONOUT$", "w", stdout);
        if (err)
            freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif
}

// Why it cannot start, where the user sees it: a message box on Windows (no console there), the error output elsewhere.
void fail(const std::string& message, bool quiet) {
    std::fprintf(stderr, "flowstudio: %s\n", message.c_str());
#ifdef _WIN32
    if (!quiet) {
        const std::string text = "FlowGraph Studio cannot start:\n\n" + message;
        std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
        MessageBoxW(nullptr, wide.c_str(), L"FlowGraph Studio", MB_OK | MB_ICONERROR);
    }
#else
    (void)quiet;
#endif
}

// --screenshot, --record: pictures of the window, then the application ends.
class Pictures {
public:
    Pictures(glw::Application& app, glw::Window& window) : app_(app), window_(window) {}

    void screenshot(std::string file, std::chrono::milliseconds delay) {
        start(delay, [this, file = std::move(file)] {
            window_.saveImage(file, [this](const std::string& error) {
                if (!error.empty())
                    std::fprintf(stderr, "flowstudio: the screenshot was not saved: %s\n", error.c_str());
                app_.quit(error.empty() ? 0 : 1);
            });
        });
    }

    void record(std::filesystem::path folder, std::chrono::milliseconds delay, std::chrono::milliseconds every, int count) {
        std::error_code ignored;
        std::filesystem::create_directories(folder, ignored);
        folder_ = std::move(folder);
        every_ = every;
        count_ = count;
        start(delay, [this] { next(); });
    }

private:
    void start(std::chrono::milliseconds delay, std::function<void()> fire) {
        if (glw::Scheduler* scheduler = app_.context().scheduler)
            timer_.start(*scheduler, delay, std::move(fire));
    }

    // a picture; once it is saved, the next one `every_` later
    void next() {
        char name[32];
        std::snprintf(name, sizeof name, "frame_%04d.png", taken_);
        const std::u8string path = (folder_ / name).u8string();
        window_.saveImage(std::string(path.begin(), path.end()), [this](const std::string& error) {
            if (!error.empty()) {
                std::fprintf(stderr, "flowstudio: a picture was not saved: %s\n", error.c_str());
                app_.quit(1);
            } else if (++taken_ >= count_) {
                app_.quit(0);
            } else {
                start(every_, [this] { next(); });
            }
        });
    }

    glw::Application& app_;
    glw::Window& window_;
    glw::Timer timer_;
    std::filesystem::path folder_;
    std::chrono::milliseconds every_{100};
    int count_ = 0;
    int taken_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = arguments(argc, argv);
    std::string script, screenshot, record;
    std::vector<std::pair<std::string, fg::NodeId>> breakpoints;
    int frames = -1, recordCount = 50;
    std::chrono::milliseconds delay{1500}, recordEvery{100};
    bool runAtStart = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool more = i + 1 < args.size();
        if (a == "--version") {
            attachConsole();
            std::printf("flowstudio %s\n", FG_VERSION);
            return 0;
        } else if (a == "--run") {
            runAtStart = true;
        } else if (a == "--frames" && more) {
            frames = std::max(1, std::atoi(args[++i].c_str()));
        } else if (a == "--screenshot" && more) {
            screenshot = args[++i];
        } else if (a == "--record" && more) {
            record = args[++i];
        } else if (a == "--record-every" && more) {
            recordEvery = std::chrono::milliseconds(std::max(1, std::atoi(args[++i].c_str())));
        } else if (a == "--record-count" && more) {
            recordCount = std::max(1, std::atoi(args[++i].c_str()));
        } else if (a == "--delay" && more) {
            delay = std::chrono::milliseconds(std::max(0, std::atoi(args[++i].c_str())));
        } else if (a == "--break" && more) {
            const std::string& at = args[++i];
            const std::size_t colon = at.rfind(':');
            if (colon == std::string::npos || colon == 0) {
                attachConsole();
                std::fprintf(stderr, "flowstudio: --break %s: GRAPH:NODE expected\n", at.c_str());
                return 2;
            }
            breakpoints.emplace_back(at.substr(0, colon), static_cast<fg::NodeId>(std::atoi(at.c_str() + colon + 1)));
        } else if (a.starts_with("--")) {
            attachConsole();
            std::fprintf(stderr, "flowstudio: unknown option %s (see Apps/Studio/main.cpp)\n", a.c_str());
            return 2;
        } else {
            script = a;
        }
    }
    // a test or a picture: what fails is told on the error output, not in a message box
    const bool unattended = frames >= 0 || !screenshot.empty() || !record.empty();
    if (unattended)
        attachConsole();
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
            std::fprintf(stderr, "flowstudio: plugin: %s\n", e.c_str());

        fged::gl::Studio studio(app, rt,
                                {.applicationName = "FlowGraph Studio",
                                 .settingsName = "FlowGraph Studio",
                                 .newScript = [&rt](fg::Script& s) { fg::standard::createMainTemplate(s, rt.registry()); },
                                 // Run: Main, with the arguments of the launch configuration chosen (none: no arguments)
                                 .runEntry = fg::standard::kMainEvent,
                                 .runArguments = [&rt] { return std::vector<fg::Value>{fg::standard::argsValue(rt.registry(), {})}; },
                                 .launchConfigurations = true});
        if (!script.empty() && !studio.open(script) && unattended) {
            std::fprintf(stderr, "flowstudio: %s cannot be opened\n", script.c_str());
            return 1;
        }
        for (const auto& [graph, node] : breakpoints)
            studio.document().debugger().setBreakpoint(graph, node, true);
        if (runAtStart)
            studio.runScript();

        Pictures pictures(app, studio.window());
        if (!screenshot.empty())
            pictures.screenshot(screenshot, delay);
        else if (!record.empty())
            pictures.record(pathOf(record), delay, recordEvery, recordCount);
        return app.run(frames);
    } catch (const std::exception& e) {
        fail(e.what(), unattended);
        return 1;
    }
}
