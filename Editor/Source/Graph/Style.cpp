// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Style.hpp"
#include "../Common/NodeTexts.hpp"

namespace fged {

namespace {
// One item of a connector: a circle of radius r, or a diamond of about the same weight (`reach`: how far its tips are from the center,
// in units of r).
void glyphItem(QPainter* p, const QPointF& c, double r, bool diamond, double reach = 1.35) {
    if (!diamond) {
        p->drawEllipse(c, r, r);
        return;
    }
    const double d = r * reach;
    const QPointF pts[4] = {{c.x(), c.y() - d}, {c.x() + d, c.y()}, {c.x(), c.y() + d}, {c.x() - d, c.y()}};
    p->drawPolygon(pts, 4);
}
} // namespace

void drawPinGlyph(QPainter* p, const QPointF& c, const PinGlyph& g) {
    p->save();
    auto fill = [&](const QColor& col) { return g.connected ? col : g.hollow; };
    switch (g.container) {
    case 1: {                                               // array: [ o o ]
        p->setPen(QPen(g.color, 1.2));
        p->setBrush(Qt::NoBrush);
        for (int side : {-1, 1}) {
            const double x = c.x() + side * 6.6, serif = c.x() + side * 4.9;
            p->drawLine(QPointF(x, c.y() - 4.6), QPointF(x, c.y() + 4.6));
            p->drawLine(QPointF(x, c.y() - 4.6), QPointF(serif, c.y() - 4.6));
            p->drawLine(QPointF(x, c.y() + 4.6), QPointF(serif, c.y() + 4.6));
        }
        p->setPen(QPen(g.color, 0.9));
        p->setBrush(fill(g.color));
        for (int side : {-1, 1}) glyphItem(p, QPointF(c.x() + side * 2.9, c.y()), 1.8, g.byRef, 1.25);
        break;
    }
    case 2: {                                               // map: 2 x 2, keys on the left, values on the right
        for (int col = 0; col < 2; ++col) {
            const QColor& color = col == 0 ? g.keyColor : g.color;
            p->setPen(QPen(color, 0.9));
            p->setBrush(fill(color));
            for (int row = -1; row <= 1; row += 2) glyphItem(p, QPointF(c.x() + (col == 0 ? -3.0 : 3.0), c.y() + row * 3.0), 1.8, g.byRef, 1.25);
        }
        break;
    }
    default:
        p->setPen(g.color);
        p->setBrush(fill(g.color));
        glyphItem(p, c, g.byRef ? 4.5 / 1.35 : 4.0, g.byRef);
        break;
    }
    p->restore();
}

QString defaultText(const fg::CompiledPin& p) {
    if (!p.type) return {};
    switch (p.type->kind) {
    case fg::TypeKind::Bool: return p.def.toBool() ? "X" : "";
    case fg::TypeKind::Int: return QString::number(p.def.toInt());
    case fg::TypeKind::Float: return QString::number(p.def.toFloat(), 'g', 10);
    case fg::TypeKind::String: return p.def.isString() ? qs(p.def.str()) : QString();
    case fg::TypeKind::Enum: return enumValueText(p.type, p.def.toInt());       // its name in the language of the editor
    default: return {};
    }
}

fg::Value parseDefault(const fg::CompiledPin& p, const QString& text) {
    switch (p.type ? p.type->kind : fg::TypeKind::Any) {
    case fg::TypeKind::Bool: return fg::Value(!text.isEmpty());
    case fg::TypeKind::Int: return fg::Value(static_cast<int64_t>(text.toLongLong()));
    case fg::TypeKind::Float: return fg::Value(text.toDouble());
    case fg::TypeKind::String: return fg::Value(ss(text));
    case fg::TypeKind::Enum:
        for (auto& e : p.type->enumValues)
            if (qs(e.name) == text) return fg::Value(e.value);
        return fg::Value(static_cast<int64_t>(text.toLongLong()));
    default: return fg::Value();
    }
}

} // namespace fged
