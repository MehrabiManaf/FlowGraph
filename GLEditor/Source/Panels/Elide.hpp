// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A text in a cell narrower than it: as much of it as fits, then "...".
#include <GLWidgets/Text/TextLayout.hpp>

#include <string>
#include <vector>

namespace fged::gl {

// `text` laid out in `style`, cut where it is wider than `room` (at a character, never inside one) with an ellipsis.
inline glw::text::TextLayout elidedLayout(const std::string& text, const glw::text::TextStyle& style, float room) {
    static constexpr const char* ellipsis = "\u2026";
    glw::text::TextLayout whole(text, style);
    if (whole.size().width <= room)
        return whole;
    std::vector<std::size_t> ends;           // where each character ends (UTF-8)
    for (std::size_t i = 1; i <= text.size(); ++i)
        if (i == text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            ends.push_back(i);
    std::size_t low = 0, high = ends.size();
    while (low < high) {
        const std::size_t mid = (low + high + 1) / 2;
        if (glw::text::TextLayout(text.substr(0, ends[mid - 1]) + ellipsis, style).size().width <= room)
            low = mid;
        else
            high = mid - 1;
    }
    return glw::text::TextLayout((low > 0 ? text.substr(0, ends[low - 1]) : std::string()) + ellipsis, style);
}

} // namespace fged::gl
