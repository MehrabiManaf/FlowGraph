// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "NodeTexts.hpp"

namespace fged::gl {

namespace {
fg::Translations& texts() {
    static fg::Translations t;
    return t;
}
} // namespace

const fg::Translations& nodeTexts() {
    return texts();
}

fg::Translations& editableNodeTexts() {
    return texts();
}

std::string enumValueText(const fg::TypeDesc* type, int64_t value) {
    if (!type)
        return std::to_string(value);
    const std::string name = type->enumName(value);
    return type->hasEnumValue(value) ? nodeTexts().enumValue(type->name, name) : name;
}

} // namespace fged::gl
