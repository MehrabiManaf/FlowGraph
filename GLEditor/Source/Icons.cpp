// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Icons.hpp"

#include "Language.hpp"

namespace fged::gl {

glw::Icon studioIcon(std::string_view name) {
    for (const EmbeddedCatalog& c : embeddedCatalogs())
        if (std::string_view(c.kind) == "icon" && name == c.language)
            return glw::Icon::fromSvg(c.text());
    return {};
}

} // namespace fged::gl
