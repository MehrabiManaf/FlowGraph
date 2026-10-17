#include "TabBar.hpp"
#include "TabWidget.hpp"
#include "DockWidget.hpp"
#include <QStyleFactory>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/TabBar.h>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>



CTabBarProxyStyle::CTabBarProxyStyle(): QProxyStyle(QStyleFactory::create(qApp->style()->name()))
{
    setParent(qApp);
}

int CTabBarProxyStyle::styleHint(QStyle::StyleHint hint, const QStyleOption *option,
                  const QWidget *widget,
                  QStyleHintReturn *returnData) const
{
    if (hint == QStyle::SH_Widget_Animation_Duration) {
        // QTabBar has a bug which causes the paint event to dereference a tab which was already
        // removed. Because, after the tab being removed, the d->pressedIndex is only reset
        // after the animation ends. So disable the animation. Crash can be repro by enabling
        // movable tabs, and detaching a tab quickly from a floating window containing two dock
        // widgets. Reproduced on Windows
        return 0;
    }
    return baseStyle()->styleHint(hint, option, widget, returnData);
}

static CTabBarProxyStyle* proxyStyle()
{
    static auto* proxy = new CTabBarProxyStyle();
    return proxy;
}

CTabBar::CTabBar(KDDockWidgets::Core::TabBar* pController, QWidget* pParent):
    KDDockWidgets::QtWidgets::TabBar(pController, pParent)
{
    setStyle(proxyStyle());
    m_DesiredTabHeight = -1;
    m_AnimationProgress = 1.0;
    mp_ExpandCollapseAnimation = new QPropertyAnimation(this, "AnimationProgress", this);
    mp_ExpandCollapseAnimation->setStartValue(0.0);
    mp_ExpandCollapseAnimation->setEndValue(1.0);
    mp_ExpandCollapseAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    mp_ExpandCollapseAnimation->setDuration(333);

    connect(this, SIGNAL(countChanged()), this, SLOT(OnCountChanged()));
    connect(this, SIGNAL(currentDockWidgetChanged(KDDockWidgets::Core::DockWidget*)), this,
            SLOT(OnCurrentDockWidgetChanged(KDDockWidgets::Core::DockWidget*)));
}

CTabBar::~CTabBar()
{

}
void CTabBar::OnCountChanged()
{
    if((count() > 1) && (m_AnimationProgress < 1.0))
    {
        auto pController = static_cast<KDDockWidgets::Core::TabBar*>(controller());
        if(pController)
        {
            for(int Iter = 0; Iter < count(); ++Iter)
            {
                auto pDockWidget = dynamic_cast<CDockWidget*>(pController->dockWidgetAt(Iter)->view());
                if(pDockWidget)
                    pDockWidget->SetTabBarHidden(false);
            }
        }
        mp_ExpandCollapseAnimation->stop();
        SetAnimationProgress(1.0);        
    }
}

void CTabBar::OnCurrentDockWidgetChanged(KDDockWidgets::Core::DockWidget* pKDDockWidget)
{
    if((!pKDDockWidget) || (count() > 1))
        return;
    auto pDockWidget = dynamic_cast<CDockWidget*>(pKDDockWidget->view());
    if(!pDockWidget)
        return;
    SetAnimationProgress(pDockWidget->IsTabBarHidden()? 0.0: 1.0);
}

void CTabBar::SetTabBarHidden(bool Hidden)
{
    if(Hidden)
        mp_ExpandCollapseAnimation->setDirection(QPropertyAnimation::Backward);
    else
        mp_ExpandCollapseAnimation->setDirection(QPropertyAnimation::Forward);
    auto pController = static_cast<KDDockWidgets::Core::TabBar*>(controller());
    if((count() == 1) && pController)
    {
        auto pDockWidget = dynamic_cast<CDockWidget*>(pController->currentDockWidget()->view());
        if(pDockWidget)
            pDockWidget->SetTabBarHidden(Hidden);
    }
    mp_ExpandCollapseAnimation->start();
}

void CTabBar::RefreshTabBarVisibility()
{
    double Progress = 1.0;
    auto pController = static_cast<KDDockWidgets::Core::TabBar*>(controller());
    if((count() == 1) && pController && pController->currentDockWidget())
    {
        auto pDockWidget = dynamic_cast<CDockWidget*>(pController->currentDockWidget()->view());
        if(pDockWidget && pDockWidget->IsTabBarHidden())
            Progress = 0.0;
    }
    mp_ExpandCollapseAnimation->stop();
    SetAnimationProgress(Progress);
}

int CTabBar::TabBarFixedHeight() const
{
    return KDDockWidgets::QtWidgets::TabBar::sizeHint().height();
}

void CTabBar::SetTabBarHeight(int NewHeight)
{
    if(m_DesiredTabHeight == NewHeight)
        return;
    m_DesiredTabHeight = NewHeight;
    updateGeometry();
}

void CTabBar::SetAnimationProgress(const double& Progress)
{
    if((Progress < 0.0) || (Progress > 1.0) || (Progress == m_AnimationProgress))
        return;
    m_AnimationProgress = Progress;
    // update height
    int AnimatedHeight = static_cast<int>(m_AnimationProgress * static_cast<double>(TabBarFixedHeight()));
    SetTabBarHeight(AnimatedHeight);
    // update tabwidget
    auto pTabWidget = dynamic_cast<CTabWidget*>(tabWidget());
    if(pTabWidget)
        pTabWidget->SetAnimationProgress(1.0 - Progress);
}

QSize CTabBar::sizeHint() const
{
    QSize TabBarSize = KDDockWidgets::QtWidgets::TabBar::sizeHint();
    if((m_DesiredTabHeight >= 0) && (m_DesiredTabHeight <= TabBarSize.height()))
        TabBarSize.setHeight(m_DesiredTabHeight);
    return TabBarSize;
}

void CTabBar::contextMenuEvent(QContextMenuEvent* pEvent)
{
    if(count() != 1)
        return;
    if (tabAt(pEvent->pos()) != 0)
        return;

    QMenu Menu(this);

    QAction* pHideTabBarAction = Menu.addAction("Hide TabBar");
    QAction* pCloseTabAction = Menu.addAction("Close Tab");

    QAction* pSelectedAction = Menu.exec(pEvent->globalPos());

    if (pSelectedAction == pHideTabBarAction)
    {
        // Collapse
        SetTabBarHidden(true);

    }
    else if(pSelectedAction == pCloseTabAction)
    {
        // Close Tab
    }

}
