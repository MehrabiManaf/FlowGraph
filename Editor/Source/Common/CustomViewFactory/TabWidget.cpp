#include "TabWidget.hpp"
#include "TabBar.hpp"
#include <QPainter>
#include <QStyle>


CExpandTabButton::CExpandTabButton(QWidget* pParent):
    QWidget(pParent)
{
    m_IsHovered = false;
    m_Polygon.resize(3);
    m_Opacity = 0.0;
    setMouseTracking(false);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

CExpandTabButton::~CExpandTabButton()
{

}

void CExpandTabButton::SetAnimationProgress(const double& Progress)
{
    if(m_Opacity == Progress)
        return;
    m_Opacity = Progress;
    setMouseTracking(Progress > 0.5);
    setAttribute(Qt::WA_TransparentForMouseEvents, Progress < 0.5);
    update();
}

void CExpandTabButton::UpdateHoverState(const QPointF& Point)
{
    auto IsHovered = m_IsHovered;
    m_IsHovered = false;
    if((m_Opacity > 0.95) && !m_Polygon.empty() && m_Polygon.containsPoint(Point, Qt::OddEvenFill))
        m_IsHovered = true;
    if(m_IsHovered != IsHovered)
        update();
}

void CExpandTabButton::paintEvent(QPaintEvent*)
{
    if((m_Opacity < 0.05) || m_Polygon.empty())
        return;
    QPainter Painter(this);
    Painter.setPen(Qt::NoPen);
    auto Color = m_IsHovered? palette().linkVisited().color() : palette().link().color();
    auto BrushColor = QColor::fromRgbF(Color.redF(), Color.greenF(), Color.blueF(), Color.alphaF() * m_Opacity);
    Painter.setBrush(BrushColor);
    Painter.drawPolygon(m_Polygon);
}

void CExpandTabButton::resizeEvent(QResizeEvent* pEvent)
{
    m_Polygon[0] = QPointF(0.0, 0.0);
    m_Polygon[1] = QPointF(width(), 0.0);
    m_Polygon[2] = QPointF(0.0, height());
    QWidget::resizeEvent(pEvent);
}

void CExpandTabButton::mouseMoveEvent(QMouseEvent* pEvent)
{
    UpdateHoverState(pEvent->position());
    QWidget::mouseMoveEvent(pEvent);
}

void CExpandTabButton::mousePressEvent(QMouseEvent* pEvent)
{
    if((m_Opacity > 0.95) && (pEvent->buttons() & Qt::LeftButton) && (m_Polygon.containsPoint(pEvent->position(), Qt::OddEvenFill)))
        emit Clicked();
    QWidget::mousePressEvent(pEvent);
}

void CExpandTabButton::enterEvent(QEnterEvent* pEvent)
{
    UpdateHoverState(pEvent->position());
    QWidget::enterEvent(pEvent);
}

void CExpandTabButton::leaveEvent(QEvent* pEvent)
{
    UpdateHoverState(QPointF(-1.0, -1.0));
    QWidget::leaveEvent(pEvent);
}


// ======================================================================

CTabWidget::CTabWidget(KDDockWidgets::Core::Stack* pController, QWidget* pParent):
    KDDockWidgets::QtWidgets::Stack(pController, pParent)
{
    mp_ExpandTabButton = new CExpandTabButton(this);
    mp_ExpandTabButton->move(0, 0);
    m_ExpandTabButtonSize = 2.0 * static_cast<double>(style()->pixelMetric(QStyle::PM_ButtonIconSize)) / 3.0;
    mp_ExpandTabButton->resize(0, 0);
    mp_ExpandTabButton->SetAnimationProgress(0.0);
    mp_ExpandTabButton->raise();
    connect(mp_ExpandTabButton, SIGNAL(Clicked()), this, SLOT(OnExpandTabsClicked()));
}

CTabWidget::~CTabWidget()
{

}

void CTabWidget::SetAnimationProgress(const double& Progress)
{
    auto DesiredSize = static_cast<int>(Progress * static_cast<double>(m_ExpandTabButtonSize));
    mp_ExpandTabButton->resize(DesiredSize, DesiredSize);
    mp_ExpandTabButton->SetAnimationProgress(Progress);
}

void CTabWidget::init()
{
    KDDockWidgets::QtWidgets::Stack::init();
    mp_ExpandTabButton->raise();
}

void CTabWidget::OnExpandTabsClicked()
{
    auto pTabBar = dynamic_cast<CTabBar*>(tabBar());
    if(pTabBar)
        pTabBar->SetTabBarHidden(false);
}
