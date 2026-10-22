// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The texts of nodes - titles, pin labels, categories, docs, node settings, enum values - in the language of the editor
// (fg::Translations: Core/Translations/<code>.json compiled in, plugins add theirs with loadNodeTranslations). Display only: what is
// stored in scripts (node ids, pin names, enum values) stays English. Every place that shows a node text takes it from here.
#include <FlowGraph/Program.hpp>
#include <FlowGraph/Runtime.hpp>
#include <FlowGraph/Translations.hpp>
#include <FlowGraph/Types.hpp>

#include <QString>

namespace fged {

const fg::Translations& nodeTexts();
fg::Translations& editableNodeTexts();          // Language.cpp loads into it
// Loads the catalog of the editor's language of every plugin of `rt` not looked at yet (translations/<Plugin>_<code>.json next to
// its library, Translations.hpp). The document does it before it compiles, so that the nodes of a plugin loaded later show translated.
void loadPluginTranslations(const fg::Runtime& rt);

inline QString nodeTitle(const fg::CompiledNode& cn) {
    return QString::fromStdString(nodeTexts().nodeTitle(cn.def, cn.props, cn.title));
}
inline QString nodeDoc(const fg::NodeDef* def) { return def ? QString::fromStdString(nodeTexts().nodeDoc(*def)) : QString(); }
inline QString pinLabel(const std::string& defId, const std::string& pin) { return QString::fromStdString(nodeTexts().pin(defId, pin)); }
// A pin of that node: the numbered ones ("Then 2") and the cases of a Switch on Enum (its enum's values) included.
inline QString pinLabel(const fg::CompiledNode& cn, const std::string& pin) {
    return QString::fromStdString(nodeTexts().pin(cn.def, cn.props, pin));
}
inline QString pinDoc(const std::string& defId, const std::string& pin, const std::string& english) {
    return QString::fromStdString(nodeTexts().pinDoc(defId, pin, english));
}
inline QString categoryText(const std::string& path) { return QString::fromStdString(nodeTexts().category(path)); }
// The name of an enum value as shown (its number when the enum has no such value).
inline QString enumValueText(const fg::TypeDesc* type, int64_t value) {
    if (!type) return QString::number(value);
    const std::string name = type->enumName(value);
    return type->hasEnumValue(value) ? QString::fromStdString(nodeTexts().enumValue(type->name, name)) : QString::fromStdString(name);
}
inline QString enumValueText(const std::string& type, const std::string& value) {
    return QString::fromStdString(nodeTexts().enumValue(type, value));
}

} // namespace fged
