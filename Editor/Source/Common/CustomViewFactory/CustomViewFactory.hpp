#pragma once

#include <kddockwidgets/qtwidgets/ViewFactory.h>


class CCustomViewFactory: public KDDockWidgets::QtWidgets::ViewFactory
{
    Q_OBJECT
public:
    KDDockWidgets::Core::View* createGroup(KDDockWidgets::Core::Group* pGroup, KDDockWidgets::Core::View* pParent) const override;
    KDDockWidgets::Core::View* createTabBar(KDDockWidgets::Core::TabBar* pTabBar, KDDockWidgets::Core::View* pParent) const override;
    KDDockWidgets::Core::View* createStack(KDDockWidgets::Core::Stack* pStack, KDDockWidgets::Core::View* pParent) const override;
    // we can customize createDockWidget for persistentcentralwidget and ... but we dont need it for now
};
