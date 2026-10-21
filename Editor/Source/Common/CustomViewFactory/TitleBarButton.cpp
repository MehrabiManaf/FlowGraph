#include "TitleBarButton.hpp"
#include "../Theme.hpp"

#include <QPainter>
#include <QPainterPath>

CTitleBarButton::CTitleBarButton(QWidget* pParent, KDDockWidgets::TitleBarButtonType Type)
    : KDDockWidgets::QtWidgets::Button(pParent), m_Type(Type)
{
    setFocusPolicy(Qt::NoFocus);
    setIconSize(QSize(kIconSize, kIconSize));
    setAttribute(Qt::WA_Hover);
    // the icons follow the theme by themselves (themedIcon); the background is painted from the palette
    connect(&fged::ThemeNotifier::instance(), &fged::ThemeNotifier::changed, this, [this] { update(); });
}

QSize CTitleBarButton::sizeHint() const
{
    return QSize(kSize, kSize);
}

void CTitleBarButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool close = m_Type == KDDockWidgets::TitleBarButtonType::Close;
    const bool hot = isEnabled() && underMouse();
    if (hot) {
        QColor back;
        if (close) {
            back = QColor(isDown() ? 0xc4314b : 0xe0485f);
        } else {
            back = palette().color(QPalette::WindowText);
            back.setAlphaF(isDown() ? 0.28 : 0.15);
        }
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        p.fillPath(shape, back);
    }
    // Close under the mouse: a white cross on the red. Float docks a floating window back (KDDockWidgets uses one icon for both):
    // there its arrow points in.
    QIcon icon = this->icon();
    if (close && hot)
        icon = fged::themedIcon(":/Icons/dock-close-hover.svg");
    else if (m_Type == KDDockWidgets::TitleBarButtonType::Float && window()->inherits("KDDockWidgets::QtWidgets::FloatingWindow"))
        icon = fged::themedIcon(":/Icons/dock-dock.svg");
    const QRect target(QPoint((width() - kIconSize) / 2, (height() - kIconSize) / 2), QSize(kIconSize, kIconSize));
    icon.paint(&p, target, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
}
