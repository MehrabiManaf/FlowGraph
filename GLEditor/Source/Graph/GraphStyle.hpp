// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// How graphs look: the colors of nodes' headers and of pins, the shapes of pins' connectors. The same as the Qt editor's.
#include <FlowGraph/Program.hpp>
#include <FlowGraph/Registry.hpp>
#include <GLWidgets/Core/Color.hpp>
#include <GLWidgets/Core/Geometry.hpp>
#include <GLWidgets/Widgets/Theme.hpp>

#include <cstdint>
#include <string>

namespace glw::paint {
class Painter;
}

namespace fged::gl {

inline glw::Color rgb(std::uint32_t c, float alpha = 1) {
    return {((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f, alpha};
}
inline glw::Color rgb(int r, int g, int b, float alpha = 1) { return {r / 255.0f, g / 255.0f, b / 255.0f, alpha}; }

glw::Color headerColor(fg::NodeStyle style);

// The colors of what graphs show, from the theme (its nodes' style: Theme::node; the boxes of values: its fields').
struct GraphColors {
    glw::Color body;            // a node's, a reroute's; the hollow of a pin without a link
    glw::Color border;
    glw::Color text;
    glw::Color mutedText;
    glw::Color highlight;       // a pin under the mouse, a node's border under it
    glw::Color selection;       // around a node selected
    glw::Color field;           // the box of an input's value
    glw::Color fieldBorder;
    glw::Color fieldText;
    glw::Color exec;            // execution pins and links
    glw::Color error{0.9f, 0.24f, 0.24f, 1};
    glw::Color warning{0.9f, 0.75f, 0.24f, 1};
    glw::Color lock;            // the padlock of a const pin
    glw::Color debug{1, 0.67f, 0, 1};       // the node a paused run is at
    glw::Color breakpoint{0.86f, 0.16f, 0.16f, 1};
    glw::Color bubble{0.9f, 0.78f, 0.35f, 0.92f};   // a node's comment
    glw::Color bubbleText{0.12f, 0.12f, 0.12f, 1};

    static GraphColors of(const glw::Theme& theme);
};

glw::Color pinColor(const fg::Registry& reg, const fg::CompiledPin& pin, const GraphColors& colors);

// The connector of a data pin around `center` (12 x 12; an array's 15 x 12): a single value a circle, an array a bracket
// frame with two items in a row [ o o ] (the element type's color), a map 2 x 2 items (the key type's color on the left,
// the value type's on the right). A reference has diamonds for circles. Items are filled when the pin has a link.
constexpr float arrayConnectorWidth = 15;
struct PinGlyph {
    int container = 0;          // 0: a single value, 1: an array, 2: a map
    bool byRef = false;
    bool connected = false;
    glw::Color color;
    glw::Color keyColor;
    glw::Color hollow;
};
void drawPinGlyph(glw::paint::Painter& p, glw::Point center, const PinGlyph& glyph);
// An execution pin's connector around `center`: an arrow 12 x 15, filled when it has a link.
void drawExecGlyph(glw::paint::Painter& p, glw::Point center, glw::Color color, bool filled, glw::Color hollow);

// The text of an input's value, as its box shows it (empty for a bool: a check box shows it).
std::string defaultText(const fg::CompiledPin& pin);
// What is typed into the box of a value of `type`: a value of that type (a number that is no number: 0; bytes as hex,
// "48 65 6C"; an enum by its name, shown or English, or its number).
fg::Value parseValue(const fg::TypeDesc* type, const std::string& text);

} // namespace fged::gl
