// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Icons.hpp"

#include "Language.hpp"

#include <unordered_map>

namespace fged::gl {

glw::Icon studioIcon(std::string_view name) {
    for (const EmbeddedCatalog& c : embeddedCatalogs())
        if (std::string_view(c.kind) == "icon" && name == c.language)
            return glw::Icon::fromSvg(c.text());
    return {};
}

void giveIcons(std::vector<glw::MenuItem>& items) {
    static const std::unordered_map<std::string_view, std::string_view> icons{
        {"&Delete", "delete"},
        {"Delete", "delete"},
        {"Delete Comment", "delete"},
        {"Delete Link", "delete"},
        {"Remove", "delete"},
        {"Cut", "cut"},
        {"Copy", "copy"},
        {"Copy Value", "copy"},
        {"Paste", "paste"},
        {"Duplicate", "duplicate"},
        {"Rename", "rename"},
        {"Open", "open-function"},
        {"Go to the Handler", "goto"},
        {"Find References", "find-references"},
        {"Toggle Breakpoint", "breakpoint-add"},
        {"Collapse to Function...", "function"},
        {"Edit Node Comment...", "edit-comment"},
        {"Edit Comment Text...", "edit-comment"},
        {"Add Comment", "comment"},
        {"Add Comment Around", "comment"},
        {"Add Node...", "add"},
        {"Add Reroute Node", "reroute"},
        {"Break All Links", "break-links"},
        {"Get {name}", "variable-get"},
        {"Set {name}", "variable-set"},
        {"Add to Watch", "watch-add"},
        {"Watch a Range...", "watch-range"},
        {"Edit Watch", "watch-edit"},
        {"Remove Watch", "watch-remove"},
        {"Remove All Watches", "watch-clear"},
        {"Load the next {count}", "load-more"},
        {"Load the next {count} (of {left} left)", "load-more"},
        {"Load all ({left} left)", "load-more"},
        {"Clear", "clear"},
        {"Clear the List", "clear"},
        {"Select All", "select-all"},
        {"Float", "dock-float"},
        {"Dock", "dock-dock"},
        {"Hide the tab bar", "tabbar-hide"},
        {"Close", "dock-close"},
        {"Open &Recent", "recent"},
        {"&Frame Selection", "frame-selection"},
        {"Frame", "frame-selection"},
        {"&Frame All", "frame-all"},
        {"Grid Snap Si&ze", "grid"},
        {"&Colors", "colors"},
        {"&Style", "style"},
        {"&Language", "language"},
        {"Execution &Speed", "speed"},
        {"&About FlowGraph Studio", "about"},
        {"Move Up", "move-up"},
        {"Move Down", "move-down"},
        {"Function", "function"},
        {"Variable", "variable"},
        {"Local Variable of {function}", "local-variable"},
        {"Struct", "struct"},
    };
    for (glw::MenuItem& item : items) {
        if (!item.items.empty())
            giveIcons(item.items);
        if (item.action || !item.icon.empty())
            continue;
        if (const auto it = icons.find(item.text.source); it != icons.end())
            item.icon = studioIcon(it->second);
    }
}

} // namespace fged::gl
