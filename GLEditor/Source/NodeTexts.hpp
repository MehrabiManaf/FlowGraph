// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The texts of nodes - titles, pin labels, docs, enum values - in the language of the editor (fg::Translations: the
// catalogs of Core/Translations, and of the plugins). Display only: what scripts store (node ids, pin names, enum values)
// stays English. Every place of the editor that shows a node text takes it from here.
#include <FlowGraph/Program.hpp>
#include <FlowGraph/Translations.hpp>
#include <FlowGraph/Types.hpp>

#include <string>

namespace fged::gl {

const fg::Translations& nodeTexts();
fg::Translations& editableNodeTexts();

inline std::string nodeTitle(const fg::CompiledNode& cn) { return nodeTexts().nodeTitle(cn.def, cn.props, cn.title); }
inline std::string nodeDoc(const fg::NodeDef* def) { return def ? nodeTexts().nodeDoc(*def) : std::string(); }
// A pin of that node: the numbered ones ("Then 2") and the cases of a Switch on Enum (its enum's values) included.
inline std::string pinLabel(const fg::CompiledNode& cn, const std::string& pin) { return nodeTexts().pin(cn.def, cn.props, pin); }
inline std::string pinDoc(const std::string& defId, const std::string& pin, const std::string& english) {
    return nodeTexts().pinDoc(defId, pin, english);
}
// The name of an enum value as shown (its number when the enum has no such value).
std::string enumValueText(const fg::TypeDesc* type, int64_t value);

} // namespace fged::gl
