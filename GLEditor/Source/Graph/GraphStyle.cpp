// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Graph/GraphStyle.hpp"

#include "NodeTexts.hpp"

#include <FlowGraph/Encoding.hpp>
#include <GLWidgets/Paint/Painter.hpp>

#include <charconv>
#include <cstdio>
#include <cstdlib>

namespace fged::gl {

glw::Color headerColor(fg::NodeStyle style) {
    switch (style) {
    case fg::NodeStyle::Event: return rgb(150, 40, 40);
    case fg::NodeStyle::Function: return rgb(45, 100, 165);
    case fg::NodeStyle::Pure: return rgb(55, 135, 85);
    case fg::NodeStyle::Flow: return rgb(105, 105, 112);
    case fg::NodeStyle::Variable: return rgb(150, 105, 45);
    case fg::NodeStyle::Macro: return rgb(95, 70, 150);
    case fg::NodeStyle::Custom: return rgb(70, 120, 120);
    case fg::NodeStyle::Reroute: return rgb(90, 90, 90);
    }
    return rgb(90, 90, 90);
}

GraphColors GraphColors::of(const glw::Theme& theme) {
    const glw::Palette& p = theme.palette;
    GraphColors c;
    const bool dark = theme.isDark();
    c.body = theme.node.body;
    c.border = theme.node.border;
    c.text = p.text;
    c.mutedText = p.mutedText;
    c.highlight = p.accent;
    c.selection = theme.node.selection;
    c.field = theme.lineEdit.background.normal;
    c.fieldBorder = theme.lineEdit.border.normal;
    c.fieldText = theme.lineEdit.text;
    c.exec = p.text;
    c.lock = dark ? rgb(205, 170, 90) : rgb(150, 110, 30);
    c.typeShade = dark ? 1.0f : 0.62f;
    return c;
}

glw::Color pinColor(const fg::Registry& reg, const fg::CompiledPin& pin, const GraphColors& colors) {
    if (pin.kind == fg::PinKind::Exec)
        return colors.exec;
    if (pin.wildcard)
        return colors.typed(rgb(160, 160, 168));
    return colors.typed(rgb(reg.typeColor(pin.type)));
}

namespace {

// One item of a connector: a circle of radius r, or a diamond of about its weight (`reach`: its tips from its center, in r).
void glyphItem(glw::paint::Painter& p, glw::Point c, float r, bool diamond, float reach, glw::Color line, glw::Color fill) {
    if (!diamond) {
        p.fillCircle(c, r, fill);
        p.strokeCircle(c, r, 1, line);
        return;
    }
    const float d = r * reach;
    const glw::Point points[] = {{c.x, c.y - d}, {c.x + d, c.y}, {c.x, c.y + d}, {c.x - d, c.y}};
    p.fillConvexPolygon(points, fill);
    p.drawPolygon(points, 1, line);
}

} // namespace

void drawPinGlyph(glw::paint::Painter& p, glw::Point c, const PinGlyph& g) {
    const auto fill = [&](glw::Color color) { return g.connected ? color : g.hollow; };
    switch (g.container) {
    case 1:     // an array: [ o o ]
        for (const float side : {-1.0f, 1.0f}) {
            const float x = c.x + side * 6.6f, serif = c.x + side * 4.9f;
            const glw::Point bracket[] = {{serif, c.y - 4.6f}, {x, c.y - 4.6f}, {x, c.y + 4.6f}, {serif, c.y + 4.6f}};
            p.drawPolyline(bracket, 1.2f, g.color);
        }
        for (const float side : {-1.0f, 1.0f})
            glyphItem(p, {c.x + side * 2.9f, c.y}, 1.8f, g.byRef, 1.25f, g.color, fill(g.color));
        break;
    case 2:     // a map: 2 x 2, the keys on the left, the values on the right
        for (int column = 0; column < 2; ++column) {
            const glw::Color color = column == 0 ? g.keyColor : g.color;
            for (const float row : {-1.0f, 1.0f})
                glyphItem(p, {c.x + (column == 0 ? -3.0f : 3.0f), c.y + row * 3.0f}, 1.8f, g.byRef, 1.25f, color, fill(color));
        }
        break;
    default:
        glyphItem(p, c, g.byRef ? 4.5f / 1.35f : 4.0f, g.byRef, 1.35f, g.color, fill(g.color));
        break;
    }
}

void drawExecGlyph(glw::paint::Painter& p, glw::Point c, glw::Color color, bool filled, glw::Color hollow) {
    constexpr float hw = 6, hh = 7.5f;
    const glw::Point points[] = {{c.x - hw, c.y - hh}, {c.x, c.y - hh}, {c.x + hw, c.y}, {c.x, c.y + hh}, {c.x - hw, c.y + hh}};
    p.fillConvexPolygon(points, filled ? color : hollow);
    p.drawPolygon(points, 1.2f, color);         // (joined at every corner: a polyline back to its start left a notch)
}

std::string defaultText(const fg::CompiledPin& pin) {
    if (!pin.type)
        return {};
    switch (pin.type->kind) {
    case fg::TypeKind::Int:
    case fg::TypeKind::Byte: return std::to_string(pin.def.toInt());
    case fg::TypeKind::Float: {
        char buffer[40];
        std::snprintf(buffer, sizeof buffer, "%.10g", pin.def.toFloat());
        return buffer;
    }
    case fg::TypeKind::String: return pin.def.isString() ? pin.def.str() : std::string();
    case fg::TypeKind::Enum: return enumValueText(pin.type, pin.def.toInt());
    case fg::TypeKind::Bytes: return pin.def.isBytes() ? fg::hexEncode(pin.def.bytesView(), true) : std::string();
    default: return {};
    }
}

fg::Value parseValue(const fg::TypeDesc* type, const std::string& text) {
    std::string t = text;
    while (!t.empty() && t.front() == ' ')
        t.erase(t.begin());
    while (!t.empty() && t.back() == ' ')
        t.pop_back();
    const auto integer = [&]() -> int64_t {
        int64_t v = 0;
        const char* begin = t.data() + (!t.empty() && t.front() == '+' ? 1 : 0);
        std::from_chars(begin, t.data() + t.size(), v);
        return v;
    };
    switch (type ? type->kind : fg::TypeKind::Any) {
    case fg::TypeKind::Bool: return fg::Value(!t.empty() && t != "0" && t != "false");
    case fg::TypeKind::Int: return fg::Value(integer());
    case fg::TypeKind::Float: return fg::Value(std::strtod(t.c_str(), nullptr));
    case fg::TypeKind::String: return fg::Value(text);
    case fg::TypeKind::Byte: return fg::Value(static_cast<int64_t>(static_cast<uint8_t>(integer())));
    case fg::TypeKind::Bytes: {
        std::string data;
        fg::hexDecode(t, data);         // what is not hex (an odd digit) gives no bytes
        return fg::Value(fg::Bytes(std::move(data)));
    }
    case fg::TypeKind::Enum:
        for (const fg::EnumValueDesc& e : type->enumValues) {
            if (e.name == t || enumValueText(type, e.value) == t)
                return fg::Value(e.value);
        }
        return fg::Value(integer());
    default: return fg::Value();
    }
}

} // namespace fged::gl
