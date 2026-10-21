// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-FileCopyrightText: 2019 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>
// SPDX-License-Identifier: GPL-3.0-only
// The placement of the targets and the window flags follow KDDockWidgets' IndicatorWindow
// (src/qtwidgets/views/ClassicIndicatorsWindow.cpp, GPL-2.0-only or GPL-3.0-only).
#include "DropIndicators.hpp"
#include "../Theme.hpp"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/indicators/ClassicDropIndicatorOverlay.h>
#include <kddockwidgets/qtcommon/View.h>

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>

using namespace KDDockWidgets;

namespace {

// the blue of the editor's icons (open-function.svg ...)
const QColor kAccent(0x3f, 0x82, 0xe8);

bool isWayland() { return QGuiApplication::platformName() == QLatin1String("wayland"); }

// Without a compositor the window cannot be see-through: then only the tiles are shown (a mask), like KDDockWidgets does
bool translucent()
{
    return !qEnvironmentVariableIsSet("KDDW_NO_TRANSLUCENCY")
        && !(Config::self().internalFlags() & Config::InternalFlag_DisableTranslucency);
}

QColor withAlpha(QColor c, double a)
{
    c.setAlphaF(a);
    return c;
}

// the panel colour of the tiles and their plate: a little lighter than the window in Dark, the field colour in Light
QColor panelColor(const QPalette& pal)
{
    return fged::darkThemeShown() ? pal.color(QPalette::Window).lighter(128) : pal.color(QPalette::Base);
}

// a soft shadow under a rounded shape: a few wider, fainter copies, a little lower
void paintShadow(QPainter& p, const QRectF& r, double radius)
{
    for (int i = 4; i >= 1; --i) {
        QPainterPath s;
        s.addRoundedRect(r.adjusted(-i, -i + 1.5, i, i + 1.5), radius + i, radius + i);
        p.fillPath(s, QColor(0, 0, 0, 10));
    }
}

bool isOuter(DropLocation l)
{
    return l == DropLocation_OutterLeft || l == DropLocation_OutterRight || l == DropLocation_OutterTop || l == DropLocation_OutterBottom;
}

} // namespace

// ================================================================================================= targets

CDropIndicatorWindow::CDropIndicatorWindow(Core::ClassicDropIndicatorOverlay* pOverlay)
    // on Wayland it cannot be a top-level window (it could not be placed): it is a child of the drop area there
    : QWidget(isWayland() ? QtCommon::View_qt::asQWidget(pOverlay->view()) : nullptr,
              isWayland() ? Qt::Widget : (Qt::Tool | Qt::BypassWindowManagerHint)),
      mp_Overlay(pOverlay)
{
    setWindowFlag(Qt::FramelessWindowHint, true);
    if (Config::self().flags() & Config::Flag_KeepAboveIfNotUtilityWindow)
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void CDropIndicatorWindow::resizeEvent(QResizeEvent* pEvent)
{
    QWidget::resizeEvent(pEvent);
    updatePositions();
}

void CDropIndicatorWindow::updatePositions()
{
    m_Tiles.clear();
    m_Plate = QRect();
    const QRect r = rect();
    auto place = [this](DropLocation Location, const QRect& Tile) {
        if (mp_Overlay->dropIndicatorVisible(Location))
            m_Tiles.insert(Location, Tile);
    };
    const QSize size(kTile, kTile);
    const QPoint c = r.center() - QPoint(kTile / 2, kTile / 2);
    place(DropLocation_OutterLeft, QRect(QPoint(r.left() + kEdgeMargin, c.y()), size));
    place(DropLocation_OutterRight, QRect(QPoint(r.right() - kEdgeMargin - kTile + 1, c.y()), size));
    place(DropLocation_OutterTop, QRect(QPoint(c.x(), r.top() + kEdgeMargin), size));
    place(DropLocation_OutterBottom, QRect(QPoint(c.x(), r.bottom() - kEdgeMargin - kTile + 1), size));
    if (Core::Group* pGroup = mp_Overlay->hoveredGroup()) {
        // the group's geometry is in the drop area's coordinates, and this window covers the drop area
        const QRect hovered = pGroup->view()->geometry();
        const QRect center(r.topLeft() + hovered.center() - QPoint(kTile / 2, kTile / 2), size);
        const int step = kTile + kGap;
        place(DropLocation_Center, center);
        place(DropLocation_Top, center.translated(0, -step));
        place(DropLocation_Bottom, center.translated(0, step));
        place(DropLocation_Left, center.translated(-step, 0));
        place(DropLocation_Right, center.translated(step, 0));
        for (auto it = m_Tiles.cbegin(); it != m_Tiles.cend(); ++it)
            if (!isOuter(it.key()))
                m_Plate |= it.value();
        if (!m_Plate.isNull())
            m_Plate.adjust(-6, -6, 6, 6);
    }
    if (!m_Tiles.contains(m_Hovered))
        m_Hovered = DropLocation_None;
    updateMask();
    update();
}

void CDropIndicatorWindow::updateIndicatorVisibility()
{
    updatePositions();          // which targets exist depends on what is dragged over what
}

void CDropIndicatorWindow::updateMask()
{
    if (translucent()) {
        clearMask();
        return;
    }
    QRegion region(m_Plate);
    for (const QRect& Tile : std::as_const(m_Tiles))
        region |= Tile;
    setMask(region);
}

DropLocation CDropIndicatorWindow::hover(QPoint GlobalPos)
{
    const QPoint pos = mapFromGlobal(GlobalPos);
    DropLocation found = DropLocation_None;
    for (auto it = m_Tiles.cbegin(); it != m_Tiles.cend(); ++it)
        if (it.value().contains(pos))
            found = it.key();
    if (found != m_Hovered) {
        m_Hovered = found;
        update();
    }
    return found;
}

QPoint CDropIndicatorWindow::posForIndicator(DropLocation Location) const
{
    return mapToGlobal(m_Tiles.value(Location).center());
}

void CDropIndicatorWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    if (!m_Plate.isNull()) {
        const QRectF plate = QRectF(m_Plate).adjusted(0.5, 0.5, -0.5, -0.5);
        paintShadow(p, plate, 10);
        QPainterPath shape;
        shape.addRoundedRect(plate, 10, 10);
        p.fillPath(shape, withAlpha(panelColor(pal), 0.82));
        p.setPen(QPen(withAlpha(pal.color(QPalette::WindowText), 0.14), 1));
        p.drawPath(shape);
    }
    for (auto it = m_Tiles.cbegin(); it != m_Tiles.cend(); ++it)
        paintTile(p, it.key(), it.value());
}

// A tile: a rounded square with a small pane in it whose part where the dock goes is filled (a tab on top for Center).
void CDropIndicatorWindow::paintTile(QPainter& P, DropLocation Location, const QRect& Tile) const
{
    const QPalette pal = palette();
    const bool hot = Location == m_Hovered;
    const QRectF tile = QRectF(Tile).adjusted(0.5, 0.5, -0.5, -0.5);
    if (isOuter(Location) || m_Plate.isNull())
        paintShadow(P, tile, 6);
    QPainterPath shape;
    shape.addRoundedRect(tile, 6, 6);
    P.fillPath(shape, hot ? kAccent : panelColor(pal));
    P.setPen(QPen(hot ? kAccent.darker(115) : withAlpha(pal.color(QPalette::WindowText), 0.18), 1));
    P.drawPath(shape);

    // the pane: 18 x 14 in the middle of the tile
    const QColor line = hot ? QColor(Qt::white) : withAlpha(pal.color(QPalette::WindowText), 0.62);
    const QColor part = hot ? withAlpha(Qt::white, 0.92) : kAccent;
    QRectF pane(tile.center() - QPointF(9, 7), QSizeF(18, 14));
    if (Location == DropLocation_Center) {
        // as a tab: a tab on the pane's top left, the pane filled lightly
        pane.adjust(0, 2, 0, 0);
        const QRectF tab(pane.left() + 1.5, pane.top() - 3, 7, 3.5);
        QPainterPath tabShape;
        tabShape.addRoundedRect(tab, 1, 1);
        P.fillPath(tabShape, part);
        QPainterPath body;
        body.addRoundedRect(pane, 2.5, 2.5);
        P.fillPath(body, withAlpha(part, hot ? 0.45 : 0.35));
    } else {
        // the side where the dock goes: a third of the pane, a narrow strip for the window's edges
        const double strip = isOuter(Location) ? 5 : 7;
        QRectF fill = pane;
        switch (Location) {
        case DropLocation_Left: case DropLocation_OutterLeft: fill.setWidth(strip); break;
        case DropLocation_Right: case DropLocation_OutterRight: fill.setLeft(pane.right() - strip); break;
        case DropLocation_Top: case DropLocation_OutterTop: fill.setHeight(strip * 0.85); break;
        case DropLocation_Bottom: case DropLocation_OutterBottom: fill.setTop(pane.bottom() - strip * 0.85); break;
        default: break;
        }
        QPainterPath clip;
        clip.addRoundedRect(pane, 2.5, 2.5);
        P.save();
        P.setClipPath(clip);
        P.fillRect(fill, part);
        P.restore();
    }
    QPainterPath frame;
    frame.addRoundedRect(pane, 2.5, 2.5);
    P.setPen(QPen(line, 1.4));
    P.setBrush(Qt::NoBrush);
    P.drawPath(frame);
}

// ================================================================================================= preview

CDropPreview::CDropPreview(QWidget* pParent)
    : KDDockWidgets::QtWidgets::RubberBand(pParent), mp_Fade(new QVariantAnimation(this))
{
    if (isWindow())
        setAttribute(Qt::WA_TranslucentBackground);
    mp_Fade->setStartValue(0.0);
    mp_Fade->setEndValue(1.0);
    mp_Fade->setDuration(120);
    mp_Fade->setEasingCurve(QEasingCurve::OutQuad);
    connect(mp_Fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_Opacity = v.toDouble();
        update();
    });
}

void CDropPreview::showEvent(QShowEvent* pEvent)
{
    KDDockWidgets::QtWidgets::RubberBand::showEvent(pEvent);
    clearMask();                // some styles keep only the border of a rubber band (a mask): this one is filled
    m_Opacity = 0.0;
    mp_Fade->stop();
    mp_Fade->start();
}

void CDropPreview::resizeEvent(QResizeEvent* pEvent)
{
    KDDockWidgets::QtWidgets::RubberBand::resizeEvent(pEvent);
    clearMask();
}

void CDropPreview::moveEvent(QMoveEvent* pEvent)
{
    KDDockWidgets::QtWidgets::RubberBand::moveEvent(pEvent);
    clearMask();
}

void CDropPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    QPainterPath shape;
    shape.addRoundedRect(r, 6, 6);
    p.fillPath(shape, withAlpha(kAccent, 0.20 * m_Opacity));
    p.setPen(QPen(withAlpha(kAccent, 0.95 * m_Opacity), 2));
    p.drawPath(shape);
}
