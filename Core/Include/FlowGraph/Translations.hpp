// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Translations of what editors show of the nodes of a registry: titles, pin labels, categories, docs, node settings, enum values.
// Display only: scripts keep the English names (node ids, pin names, enum values), so a script does not depend on a language.
//
// A catalog is a JSON file, one per language (Core/Translations/<code>.json for the built-in nodes; a plugin ships its own):
//   { "language": "fa", "texts": { "node:flow.branch": ["Branch", "شاخه"], "pin:flow.branch/Condition": ["Condition", "شرط"] } }
// Every text is kept with the English text it translates: when that English text changes, the translation is not used any more
// (the English text shows until it is translated again), so an outdated translation is never shown. An empty translation is no
// translation. Keys:
//   node:<id>                the title of a node (generated ones included: fn:Math.Sin, m:Animal.Speak, event:Main...)
//   node:<id>#doc            its documentation
//   pin:<id>/<pin>           the label of a pin of it (pin:<id>/<pin>#doc its documentation); pins a node numbers from a
//                            setting share one: pin:flow.sequence/Then {N}
//   setting:<id>/<key>       the label of a node setting (setting:<id>/<key>#doc)
//   category:<name>          one part of a category path ("Flow|Switch": category:Flow and category:Switch)
//   enum:<Type>.<Value>      the name of an enum value as shown (enum:<Type>.<Value>#doc)
//   template:<name>          titles made from a name: get-variable "Get {Name}", set-variable "Set {Name}", switch-on-enum
//                            "Switch on {Enum}"
// `flow --translations <file>` writes or updates a catalog for the nodes of the standard applications (texts added with an empty
// translation, removed ones dropped, outdated ones reported); updateCatalog does it for any registry.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/NodeDef.hpp>

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fg {

class Registry;

class FG_CORE_EXPORT Translations {
public:
    // Adds the texts of a catalog (a later one wins for the same key). False (and `error`) when it is not a catalog.
    bool loadJson(const std::string& text, std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);
    void clear();
    bool empty() const { return texts_.empty(); }
    const std::string& language() const { return language_; }      // of the last catalog loaded ("" when none)

    // The translation of `key` when there is one for this English text, else `english`.
    std::string text(const std::string& key, const std::string& english) const;

    // What an editor shows. `props` are the node's properties (the variable of a Get, the value of an enum literal...).
    std::string nodeTitle(const NodeDef* def, const std::map<std::string, std::string>& props, const std::string& english) const;
    std::string nodeDoc(const NodeDef& def) const;
    std::string pin(const std::string& defId, const std::string& pin) const;
    // a pin of a node with these properties (the outputs of a Switch on Enum are its enum's values)
    std::string pin(const NodeDef* def, const std::map<std::string, std::string>& props, const std::string& pin) const;
    std::string pinDoc(const std::string& defId, const std::string& pin, const std::string& english) const;
    std::string setting(const std::string& defId, const NodeSetting& s) const;      // its label (the key when it has none)
    std::string settingDoc(const std::string& defId, const NodeSetting& s) const;
    std::string category(const std::string& path) const;                            // every part of "A|B"
    std::string enumValue(const std::string& type, const std::string& value) const;
    std::string enumValueDoc(const std::string& type, const std::string& value, const std::string& english) const;

private:
    std::unordered_map<std::string, std::pair<std::string, std::string>> texts_;   // key -> (English, translation)
    std::string language_;
};

// Every text of `reg` a catalog can translate, with its English text, in a stable order.
FG_CORE_EXPORT std::vector<std::pair<std::string, std::string>> translatableTexts(const Registry& reg);

// The catalog `existing` (JSON text, empty for a new one) brought up to date with `reg`: new texts are added with an empty
// translation, texts the registry does not have any more are dropped, the others keep their translation. `report` says what changed
// (counts of texts, translated, new, outdated - their English text changed - and removed).
struct CatalogReport {
    size_t texts = 0, translated = 0, added = 0, outdated = 0, removed = 0;
    std::vector<std::string> outdatedKeys;
};
FG_CORE_EXPORT std::string updateCatalog(const Registry& reg, const std::string& existing, const std::string& language,
                                         CatalogReport* report = nullptr);

} // namespace fg
