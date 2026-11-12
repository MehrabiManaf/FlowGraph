// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The icons of the toolbar and the menus: the Qt editor's (Editor/Resources/Icons/<name>.svg), compiled in
// (GLEditor/CMakeLists.txt names them).
#include <GLWidgets/Widgets/Icon.hpp>

#include <string_view>

namespace fged::gl {

// The icon `name` ("run", "step-over"...); an empty one when it is not compiled in.
glw::Icon studioIcon(std::string_view name);

} // namespace fged::gl
