// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The launch configurations of a script edited, in a dialog: the configurations listed on the start side (Add, Duplicate,
// Remove: one is always left), the one chosen in a form - its name, Main's arguments (one per line), the working directory,
// the environment (NAME=value per line), the plugin folders (one per line) and the delay between two nodes.
#include <FlowGraph/StandardApp.hpp>

#include <functional>

namespace glw {
class Widget;
}

namespace fged::gl {

// The dialog over `from`'s window, `file` in it; `accepted` with what it became when OK is pressed (nothing on Cancel).
void showLaunchConfigurations(glw::Widget& from, const fg::standard::LaunchFile& file,
                              std::function<void(fg::standard::LaunchFile)> accepted);

} // namespace fged::gl
