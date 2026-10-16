#pragma once
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Program.hpp>

#include <chrono>

#include <QApplication>
#include <QPointF>
#include <QColor>
#include <QPainter>
#include <QPalette>
#include <QString>

namespace fged {

inline QString qs(const std::string& s) { return QString::fromStdString(s); }
inline std::string ss(const QString& s) { return s.toStdString(); }

inline QColor rgb(uint32_t c) { return QColor((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff); }

inline QColor pinColor(const fg::Registry& reg, const fg::CompiledPin& p) {
    if (p.kind == fg::PinKind::Exec) return qApp->palette().windowText().color();
    if (p.wildcard) return QColor(160, 160, 168);
    return rgb(reg.typeColor(p.type));
}

inline QColor headerColor(fg::NodeStyle s) {
    switch (s) {
    case fg::NodeStyle::Event: return QColor(150, 40, 40);
    case fg::NodeStyle::Function: return QColor(45, 100, 165);
    case fg::NodeStyle::Pure: return QColor(55, 135, 85);
    case fg::NodeStyle::Flow: return QColor(105, 105, 112);
    case fg::NodeStyle::Variable: return QColor(150, 105, 45);
    case fg::NodeStyle::Macro: return QColor(95, 70, 150);
    case fg::NodeStyle::Custom: return QColor(70, 120, 120);
    case fg::NodeStyle::Reroute: return QColor(90, 90, 90);
    }
    return QColor(90, 90, 90);
}

inline qint64 nowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The connector of a data pin, drawn around `center` (12 x 12, an array 16 x 12 - see kArrayConnector):
//   a single value is a circle,
//   an array is a bracket frame with two items in a row   [ o o ]   (the items have the color of the element type),
//   a map is a matrix of 2 x 2 items: the left column has the color of the key type, the right one that of the value type.
// A reference has diamonds instead of circles. Items are filled when the pin is connected and hollow otherwise.
inline constexpr double kArrayConnectorWidth = 15.0;

struct PinGlyph {
    int container = 0;          // 0 = single value, 1 = array, 2 = map
    bool byRef = false;
    bool connected = false;
    QColor color;               // the value (element) type
    QColor keyColor;            // maps: the key type
    QColor hollow;              // fill of the items of a pin that is not connected
};
void drawPinGlyph(QPainter* p, const QPointF& center, const PinGlyph& g);

// Display text of the default value of a pin.
QString defaultText(const fg::CompiledPin& p);
// Parses editor text into a value of the pin's type.
fg::Value parseDefault(const fg::CompiledPin& p, const QString& text);

} // namespace fged
