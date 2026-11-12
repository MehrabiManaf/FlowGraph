// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraphEditorCore/Launches.hpp>

#include <cstdlib>
#include <filesystem>

namespace fged::core {

namespace {

std::filesystem::path pathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return {s.begin(), s.end()};
}

std::optional<std::string> variable(const std::string& name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name.c_str()) != 0 || !value)
        return std::nullopt;
    std::string v = value;
    std::free(value);
    return v;
#else
    const char* value = std::getenv(name.c_str());
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

void setVariable(const std::string& name, const std::optional<std::string>& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value ? value->c_str() : "");      // "": removed
#else
    if (value)
        setenv(name.c_str(), value->c_str(), 1);
    else
        unsetenv(name.c_str());
#endif
}

} // namespace

Launches::~Launches() {
    restore();
}

std::string Launches::folder() const {
    std::error_code ec;
    return script_.empty() ? utf8Of(std::filesystem::current_path(ec)) : utf8Of(pathOf(script_).parent_path());
}

void Launches::scriptCreated() {
    script_.clear();
    file_ = {};
    file_.current();
}

bool Launches::scriptOpened(const std::string& path, std::vector<std::string>* problems) {
    script_ = path;
    try {
        file_ = fg::standard::LaunchFile::load(fg::standard::LaunchFile::pathFor(path));
    } catch (const std::exception& e) {
        if (problems)
            problems->push_back(std::string("the launch file of the script: ") + e.what());
        file_ = {};
    }
    file_.current();
    bool loaded = false;
    for (const fg::standard::LaunchConfig& c : file_.configs)
        loaded = loadPlugins(c, problems) || loaded;
    return loaded;
}

void Launches::scriptSaved(const std::string& path) {
    if (path == script_)
        return;
    script_ = path;
    save();                     // a new name: its configurations next to it
}

void Launches::select(const std::string& name) {
    if (file_.selected == name || !file_.find(name))
        return;
    file_.selected = name;
    save();
}

bool Launches::setFile(fg::standard::LaunchFile file, std::vector<std::string>* problems) {
    if (file.configs.empty())
        file.current();
    // names unique: a later one of a name taken gets a number
    for (std::size_t i = 0; i < file.configs.size(); ++i) {
        if (file.configs[i].name.empty())
            file.configs[i].name = "Default";
        for (std::size_t j = 0; j < i; ++j)
            if (file.configs[i].name == file.configs[j].name)
                file.configs[i].name += " (" + std::to_string(i + 1) + ")";
    }
    if (!file.find(file.selected))
        file.selected = file.configs.front().name;
    file_ = std::move(file);
    save();
    bool loaded = false;
    for (const fg::standard::LaunchConfig& c : file_.configs)
        loaded = loadPlugins(c, problems) || loaded;
    return loaded;
}

void Launches::save() {
    if (script_.empty())
        return;                 // saved with the script
    try {
        file_.save(fg::standard::LaunchFile::pathFor(script_));
    } catch (...) {
        // (a folder that cannot be written: the configurations live as long as the script is open)
    }
}

bool Launches::loadPlugins(const fg::standard::LaunchConfig& config, std::vector<std::string>* problems) {
    bool loaded = false;
    for (const std::string& p : config.pluginPaths) {
        const std::filesystem::path dir = pathOf(folder()) / pathOf(p);
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec))
            continue;
        const std::size_t before = rt_.loadedPlugins().size();
        std::vector<std::string> errors;
        rt_.loadPluginsFrom(utf8Of(dir), &errors);
        loaded = loaded || rt_.loadedPlugins().size() != before;
        if (problems)
            problems->insert(problems->end(), errors.begin(), errors.end());
    }
    return loaded;
}

void Launches::apply(const fg::standard::LaunchConfig& config) {
    restore();
    applied_ = true;
    for (const auto& [name, value] : config.env) {
        savedEnv_[name] = variable(name);
        setVariable(name, value);
    }
    std::error_code ec;
    savedCwd_ = utf8Of(std::filesystem::current_path(ec));
    if (!config.cwd.empty())
        std::filesystem::current_path(pathOf(folder()) / pathOf(config.cwd), ec);
}

void Launches::restore() {
    if (!applied_)
        return;
    applied_ = false;
    for (const auto& [name, value] : savedEnv_)
        setVariable(name, value);
    savedEnv_.clear();
    std::error_code ec;
    if (!savedCwd_.empty())
        std::filesystem::current_path(pathOf(savedCwd_), ec);
    savedCwd_.clear();
}

} // namespace fged::core
