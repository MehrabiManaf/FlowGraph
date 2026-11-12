// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The language of the editor. Its texts (and GLWidgets') are translated by the catalogs of GLEditor/Translations, the texts
// of the nodes by those of Core/Translations and of the plugins (<plugin folder>/translations/<Plugin>_<code>.json): all of
// them compiled into the editor but the plugins'. Only the texts change: the layout stays left to right in every language
// (a mirrored editor looked odd; the graphs do not mirror). Names in scripts (nodes, pins, events) stay English.
#include <FlowGraph/Runtime.hpp>

#include <string>
#include <vector>

namespace glw {
class Application;
}

namespace fged::gl {

// A catalog compiled into the editor (GLEditor/EmbedCatalogs.cmake): its text in parts, null-terminated.
struct EmbeddedCatalog {
    const char* kind;               // "editor", "nodes", "icon"
    const char* language;           // fa, pt_BR... (an icon's: its name)
    const char* const* parts;

    std::string text() const;
};
const std::vector<EmbeddedCatalog>& embeddedCatalogs();

// The languages there is a translation of, and "en".
std::vector<std::string> availableLanguages();
// A language's name in that language (the Language menu): Deutsch, فارسی...
std::string languageName(const std::string& code);
// What a choice is ("system", or a language) as a language there is a translation of: fa-IR -> fa, zh-Hans-CN -> zh_CN;
// none fits: en.
std::string resolveLanguage(const std::string& choice);
// The texts of the editor in that choice's language, now; the nodes' too (and the plugins' loaded, loadPluginTexts).
// Returns the language.
std::string applyLanguage(glw::Application& app, const std::string& choice);
std::string currentLanguage();
// The catalogs of the plugins `rt` loaded since the last call, in the language in effect.
void loadPluginTexts(const fg::Runtime& rt);

} // namespace fged::gl
