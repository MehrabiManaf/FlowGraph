// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// FlowGraph Studio on GLWidgets: the window of a script, its menus, its graphs in the middle and its panels docked around
// them, the status bar; opening and saving the script, the user's preferences (the theme, the files opened lately, where
// the panels are). One script at a time, as the Qt editor.
//
//   glw::Application app;
//   fged::gl::Studio studio(app, runtime, {.newScript = [&](fg::Script& s) { fg::standard::createMainTemplate(s, reg); }});
//   studio.open(path);
//   return app.run();
#include <FlowGraphEditorCore/Document.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace glw {
class Application;
}

namespace fged::gl {

struct StudioOptions {
    std::string applicationName = "FlowGraph Studio";       // in the window's title, the dialogs
    // Where its preferences are kept: the system's folder for them (Platform::preferencesFolder), of this application.
    std::string organization = "FlowGraph";
    std::string settingsName = "FlowGraph Studio";
    // What a new script starts as (a template of the application); none: an empty event graph.
    std::function<void(fg::Script&)> newScript;
    // What Run starts: an event of the application (the standard applications' Main), with these arguments.
    std::string runEntry;
    std::function<std::vector<fg::Value>()> runArguments;
};

class Studio {
public:
    // Opens its window, with a new script.
    Studio(glw::Application& app, fg::Runtime& rt, StudioOptions options = {});
    ~Studio();
    Studio(const Studio&) = delete;
    Studio& operator=(const Studio&) = delete;

    core::Document& document();
    // A new script, at once (what was not saved is lost: the menu asks first).
    void newScript();
    // The script of `path` (UTF-8), at once; false, and a message in the window, when it cannot be read.
    bool open(const std::string& path);
    // What the Debug menu's Run does: the application's event started (the run's state in the status bar, what it prints in
    // the Output Log).
    void runScript();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fged::gl
