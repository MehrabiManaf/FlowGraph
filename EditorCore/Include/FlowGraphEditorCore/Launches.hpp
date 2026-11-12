// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// How the standard applications' scripts are launched (fg::standard::LaunchFile): their configurations - Main's arguments,
// the working directory, environment variables, plugin folders, slow motion - kept next to the script
// (<script>.flowlaunch), and what a run of one does around the script: the working directory and the variables set while
// it runs, put back after. Without a user interface: an editor shows them and edits them.
#include <FlowGraph/Runtime.hpp>
#include <FlowGraph/StandardApp.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fged::core {

class Launches {
public:
    explicit Launches(fg::Runtime& rt) : rt_(rt) { file_.current(); }
    ~Launches();
    Launches(const Launches&) = delete;
    Launches& operator=(const Launches&) = delete;

    // ---- the script they are of ----

    // A new script, without a file: one default configuration (saved with the script, when it is).
    void scriptCreated();
    // The script of `path` (UTF-8) opened: its launch file read (none: one default configuration), the plugins of its
    // configurations loaded. What went wrong (a launch file that is not one, a plugin that did not load) in `problems`.
    // True when plugins were loaded (the script is to be compiled again: they may have nodes it uses).
    bool scriptOpened(const std::string& path, std::vector<std::string>* problems = nullptr);
    // Saved as `path`: its configurations go with it (saved next to it, when it is another file).
    void scriptSaved(const std::string& path);
    const std::string& scriptPath() const { return script_; }

    // ---- the configurations ----

    const fg::standard::LaunchFile& file() const { return file_; }
    const fg::standard::LaunchConfig& current() { return file_.current(); }
    // The configuration runs use from now on (remembered next to the script).
    void select(const std::string& name);
    // Edited (a dialog's OK): names made unique, saved next to the script, their plugins loaded. True when plugins were.
    bool setFile(fg::standard::LaunchFile file, std::vector<std::string>* problems = nullptr);

    // ---- a run ----

    // The working directory and the variables of `config` in effect, until restore() (or the next apply()).
    void apply(const fg::standard::LaunchConfig& config);
    void restore();
    bool applied() const { return applied_; }

private:
    void save();
    bool loadPlugins(const fg::standard::LaunchConfig& config, std::vector<std::string>* problems);
    std::string folder() const;                 // the script's (the current directory without a file)

    fg::Runtime& rt_;
    fg::standard::LaunchFile file_;
    std::string script_;
    bool applied_ = false;
    std::string savedCwd_;
    std::map<std::string, std::optional<std::string>> savedEnv_;
};

} // namespace fged::core
