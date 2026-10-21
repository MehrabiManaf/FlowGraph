// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// flowstudio's EditorHost: the editor of the *standard applications* (see StandardApp.hpp). The only thing a
// standard application can run is Main(Args: array<string>), so "Run" starts Main with the arguments of the
// selected launch configuration stored in <script>.flowlaunch.
#include <FlowGraphEditor/ScriptEditor.hpp>

#include <map>
#include <optional>

namespace flowstudio {

class StandardEditorHost : public fged::EditorHost {
public:
    explicit StandardEditorHost(fg::Runtime& rt) : rt_(rt) {}

    fg::Runtime& runtime() override { return rt_; }
    QString applicationName() const override { return QStringLiteral("FlowGraph Studio"); }

    std::vector<fged::LaunchEntry> launchEntries(const fg::Program& program) override;
    bool prepareLaunch(const fged::LaunchEntry& entry, fged::LaunchSpec& spec, QWidget* parent) override;
    void aboutToRun(const fged::LaunchSpec& spec) override;
    void runFinished() override;
    void initializeNewScript(fg::Script& script) override { fg::standard::createMainTemplate(script, rt_.registry()); }
    void attached(fged::ScriptEditor& editor) override;
    void scriptChanged(fged::ScriptEditor& editor, fged::ScriptChange why) override;
    QString preferredEntry() const override { return QString::fromStdString(launch_.selected); }
    QString launchLabel() const override { return QStringLiteral("Launch configuration:"); }
    QString launchToolTip() const override { return QStringLiteral("How the script is launched: arguments, environment, working directory"); }
    void launchEntrySelected(const QString& id) override;

    fg::standard::LaunchFile& launchFile() { return launch_; }

private:
    void editConfigurations();
    void saveLaunchFile();
    QString scriptDir() const;
    void loadPluginPaths(const fg::standard::LaunchConfig& cfg);

    fg::Runtime& rt_;
    fged::ScriptEditor* editor_ = nullptr;
    fg::standard::LaunchFile launch_;
    QString scriptPath_;
    fg::standard::LaunchConfig active_;                      // the configuration of the run in progress
    std::map<std::string, std::optional<std::string>> savedEnv_;
    QString savedCwd_;
    bool envApplied_ = false;
};

} // namespace flowstudio
