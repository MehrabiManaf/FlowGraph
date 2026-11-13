// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The icons of the toolbar and the menus: the Qt editor's (Editor/Resources/Icons/<name>.svg), compiled in
// (GLEditor/CMakeLists.txt names them).
#include <GLWidgets/Widgets/Icon.hpp>
#include <GLWidgets/Widgets/Menu.hpp>

#include <string_view>
#include <vector>

namespace fged::gl {

// The icon `name` ("run", "step-over"...); an empty one when it is not compiled in.
glw::Icon studioIcon(std::string_view name);
// The items of a menu (and of its submenus) their icons, by what they say (their English): the Qt editor's for the same
// command. An item with an action, or an icon already, is left as it is.
void giveIcons(std::vector<glw::MenuItem>& items);

} // namespace fged::gl
