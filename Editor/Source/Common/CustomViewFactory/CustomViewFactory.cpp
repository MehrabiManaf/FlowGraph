#include "CustomViewFactory.hpp"
#include "Frame.hpp"
#include "TabBar.hpp"
#include "TabWidget.hpp"


KDDockWidgets::Core::View* CCustomViewFactory::createGroup(KDDockWidgets::Core::Group* pGroup, KDDockWidgets::Core::View* pParent) const
{
    return new CFrame(pGroup, KDDockWidgets::QtCommon::View_qt::asQWidget(pParent));
}

KDDockWidgets::Core::View* CCustomViewFactory::createTabBar(KDDockWidgets::Core::TabBar* pTabBar, KDDockWidgets::Core::View* pParent) const
{
    return new CTabBar(pTabBar, KDDockWidgets::QtCommon::View_qt::asQWidget(pParent));
}

KDDockWidgets::Core::View* CCustomViewFactory::createStack(KDDockWidgets::Core::Stack* pStack, KDDockWidgets::Core::View* pParent) const
{
    return new CTabWidget(pStack, KDDockWidgets::QtCommon::View_qt::asQWidget(pParent));
}
