// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Language.hpp"

#include "NodeTexts.hpp"

#include <GLWidgets/Widgets/Application.hpp>
#include <GLWidgets/Widgets/Localization.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fged::gl {

using glw::tr;

namespace {

std::string g_current = "en";
std::set<std::string> g_pluginsLooked;      // the plugin libraries whose catalog of this language was looked for

bool hasTranslation(const std::string& code) {
    if (code == "en")
        return true;
    const auto& all = embeddedCatalogs();
    return std::any_of(all.begin(), all.end(), [&](const EmbeddedCatalog& c) { return std::string_view(c.kind) == "editor" && code == c.language; });
}

// The languages the user's system asks for, the first first: fa-IR, en-US...
std::vector<std::string> systemLanguages() {
    std::vector<std::string> out;
#ifdef _WIN32
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
        std::wstring buffer(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) {
            for (const wchar_t* p = buffer.c_str(); *p; p += std::wcslen(p) + 1) {
                std::string name;
                for (const wchar_t* c = p; *c; ++c)
                    name += char(*c);           // language tags are ASCII
                out.push_back(name);
            }
        }
    }
#endif
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* value = std::getenv(variable); value && *value) {
            std::string v = value;
            v = v.substr(0, v.find('.'));       // fa_IR.UTF-8
            if (!v.empty() && v != "C" && v != "POSIX")
                out.push_back(v);
        }
    return out;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

std::string EmbeddedCatalog::text() const {
    std::string all;
    for (const char* const* p = parts; *p; ++p)
        all += *p;
    return all;
}

std::vector<std::string> availableLanguages() {
    std::vector<std::string> codes{"en"};
    for (const EmbeddedCatalog& c : embeddedCatalogs())
        if (std::string_view(c.kind) == "editor")
            codes.emplace_back(c.language);
    return codes;
}

std::string languageName(const std::string& code) {
    // what a catalog says "English" is in its language: its name (the menu shows each language in its own)
    static const glw::LocalizedText english = tr("English");
    if (code == "en")
        return english.source;
    for (const EmbeddedCatalog& c : embeddedCatalogs())
        if (std::string_view(c.kind) == "editor" && code == c.language) {
            glw::Translator t;
            t.loadCatalog(c.text());
            t.setLanguage(code);
            return t.resolve(english);
        }
    return code;
}

std::string resolveLanguage(const std::string& choice) {
    std::vector<std::string> wanted;
    if (choice.empty() || choice == "system")
        wanted = systemLanguages();
    else
        wanted.push_back(choice);
    for (std::string w : wanted) {
        std::replace(w.begin(), w.end(), '-', '_');
        if (hasTranslation(w))
            return w;
        // zh_Hans_CN: zh_CN; fa_IR: fa
        const std::string language = w.substr(0, w.find('_'));
        const std::string region = w.rfind('_') != std::string::npos ? w.substr(w.rfind('_') + 1) : std::string();
        if (!region.empty() && hasTranslation(language + "_" + region))
            return language + "_" + region;
        if (hasTranslation(language))
            return language;
        for (const std::string& code : availableLanguages())      // pt: pt_BR
            if (code.rfind(language + "_", 0) == 0)
                return code;
    }
    return "en";
}

std::string applyLanguage(glw::Application& app, const std::string& choice) {
    g_current = resolveLanguage(choice);
    g_pluginsLooked.clear();
    editableNodeTexts().clear();
    for (const EmbeddedCatalog& c : embeddedCatalogs()) {
        if (g_current != c.language)
            continue;
        if (std::string_view(c.kind) == "editor")
            app.translator().loadCatalog(c.text());
        else
            editableNodeTexts().loadJson(c.text());
    }
    app.setLanguage(g_current);
    // only the texts change: the panels stay left to right in every language
    app.setLayoutDirection(glw::LayoutDirection::LeftToRight);
    return g_current;
}

std::string currentLanguage() {
    return g_current;
}

void loadPluginTexts(const fg::Runtime& rt) {
    if (g_current == "en")
        return;
    const auto& names = rt.loadedPlugins();
    const auto& files = rt.loadedPluginFiles();
    for (std::size_t i = 0; i < files.size() && i < names.size(); ++i) {
        if (!g_pluginsLooked.insert(files[i]).second)
            continue;
        // <plugin folder>/translations/<Plugin>_<code>.json (Translations.hpp)
        const std::filesystem::path catalog =
            std::filesystem::u8path(files[i]).parent_path() / "translations" / std::filesystem::u8path(names[i] + "_" + g_current + ".json");
        std::error_code error;
        if (std::filesystem::exists(catalog, error))
            editableNodeTexts().loadJson(readFile(catalog));
    }
}

} // namespace fged::gl
