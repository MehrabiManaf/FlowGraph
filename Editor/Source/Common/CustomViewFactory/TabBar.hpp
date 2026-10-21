#pragma once

#include <kddockwidgets/qtwidgets/views/TabBar.h>
#include <QProxyStyle>
#include <QPropertyAnimation>


class CTabBarProxyStyle: public QProxyStyle
{
    Q_OBJECT
public:
    CTabBarProxyStyle();
    int styleHint(QStyle::StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
};

// ===============================================================================

class CTabBar: public KDDockWidgets::QtWidgets::TabBar
{
    Q_OBJECT
    Q_PROPERTY(double AnimationProgress READ AnimationProgress WRITE SetAnimationProgress)
public:
    explicit CTabBar(KDDockWidgets::Core::TabBar* pController, QWidget* pParent = nullptr);
    ~CTabBar() override;

private slots:
    void OnCountChanged();
    void OnCurrentDockWidgetChanged(KDDockWidgets::Core::DockWidget* pDockWidget);
public:
    void SetTabBarHidden(bool Hidden);
    // Shows or collapses the tab bar the way the state of its dock says, without animation (after a layout was restored).
    void RefreshTabBarVisibility();
    // Closes the dock of the only tab, like its close button (View lists it to open it again). Done when the event that asked for
    // it is over: closing the last dock of a frame deletes the frame, and this tab bar with it.
    void CloseCurrentTab();
    bool CanCloseCurrentTab() const;      // a single tab whose dock may be closed
private:
    double AnimationProgress() const { return m_AnimationProgress; }
    void SetAnimationProgress(const double& Progress);
    int TabBarFixedHeight() const;
    void SetTabBarHeight(int NewHeight);
protected:
    QSize sizeHint() const override;
    void contextMenuEvent(QContextMenuEvent* pEvent) override;
private:
    int                     m_DesiredTabHeight;
    double                  m_AnimationProgress;
    QPropertyAnimation*     mp_ExpandCollapseAnimation;
};
