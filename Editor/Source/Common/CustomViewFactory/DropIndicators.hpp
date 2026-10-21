#pragma once

#include <kddockwidgets/core/views/ClassicIndicatorWindowViewInterface.h>
#include <kddockwidgets/qtwidgets/views/RubberBand.h>

#include <QMap>
#include <QRect>
#include <QVariantAnimation>
#include <QWidget>

namespace KDDockWidgets::Core {
class ClassicDropIndicatorOverlay;
}

// The drop targets shown while a dock is dragged, in the editor's look: flat rounded tiles in the colours of the theme, each with a
// small diagram of where the dock goes (its part in the accent colour); the five over the panel under the mouse sit on one rounded
// plate, the four for the window's edges near those edges. The tile under the mouse turns accent with a white diagram.
// KDDockWidgets keeps the logic (what may be dropped where, the docking itself): this window places the tiles, paints them and says
// which one is under the mouse, like its own IndicatorWindow.
class CDropIndicatorWindow : public QWidget, public KDDockWidgets::Core::ClassicIndicatorWindowViewInterface
{
    Q_OBJECT
public:
    explicit CDropIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* pOverlay);

    KDDockWidgets::DropLocation hover(QPoint GlobalPos) override;
    QPoint posForIndicator(KDDockWidgets::DropLocation Location) const override;
    void updatePositions() override;
    void raise() override { QWidget::raise(); }
    void setVisible(bool Visible) override { QWidget::setVisible(Visible); }
    void resize(QSize Size) override { QWidget::resize(Size); }
    void setGeometry(QRect Rect) override { QWidget::setGeometry(Rect); }
    bool isWindow() const override { return QWidget::isWindow(); }
    void setObjectName(const QString& Name) override { QWidget::setObjectName(Name); }
    void updateIndicatorVisibility() override;

    static constexpr int kTile = 32;       // logical pixels
    static constexpr int kGap = 4;
    static constexpr int kEdgeMargin = 12;

protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;

private:
    void updateMask();
    void paintTile(class QPainter& P, KDDockWidgets::DropLocation Location, const QRect& Tile) const;

    KDDockWidgets::Core::ClassicDropIndicatorOverlay* const mp_Overlay;
    QMap<KDDockWidgets::DropLocation, QRect> m_Tiles;     // the visible ones, in this window's coordinates
    QRect m_Plate;                                         // behind the five tiles of the panel under the mouse
    KDDockWidgets::DropLocation m_Hovered = KDDockWidgets::DropLocation_None;
};

// Where the dragged dock will go: a rounded box with a soft accent fill and an accent border, fading in when it appears.
class CDropPreview : public KDDockWidgets::QtWidgets::RubberBand
{
    Q_OBJECT
public:
    explicit CDropPreview(QWidget* pParent);

protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void showEvent(QShowEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;
    void moveEvent(QMoveEvent* pEvent) override;

private:
    QVariantAnimation* mp_Fade;
    double m_Opacity = 1.0;
};
