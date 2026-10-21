// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "CustomViewFactory.hpp"
#include "Frame.hpp"
#include "TabBar.hpp"
#include "TabWidget.hpp"
#include "TitleBarButton.hpp"
#include "DropIndicators.hpp"
#include <FlowGraphEditor/ScriptEditor.hpp>


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

QIcon CCustomViewFactory::iconForButtonType(KDDockWidgets::TitleBarButtonType Type, qreal) const
{
    using T = KDDockWidgets::TitleBarButtonType;
    const char* Name = nullptr;
    switch(Type)
    {
    case T::Close: Name = "dock-close"; break;
    case T::Float: Name = "dock-float"; break;
    case T::Minimize: Name = "dock-minimize"; break;
    case T::Maximize: Name = "dock-maximize"; break;
    case T::Normal: Name = "dock-restore"; break;          // restores a maximized window
    case T::AutoHide: Name = "dock-pin"; break;            // pinned: a click hides the dock in the side bar
    case T::UnautoHide: Name = "dock-unpin"; break;        // in the side bar: a click pins it again
    case T::AllTitleBarButtonTypes: break;
    }
    if(!Name)
        return {};
    // SVG through the theme: sharp at any scale, in the colours of the theme in use (no cache: the icon follows a switch itself)
    return fged::themedIcon(QStringLiteral(":/Icons/%1.svg").arg(QLatin1String(Name)));
}

QAbstractButton* CCustomViewFactory::createTitleBarButton(QWidget* pParent, KDDockWidgets::TitleBarButtonType Type) const
{
    if(!pParent)
        return nullptr;
    auto* pButton = new CTitleBarButton(pParent, Type);
    pButton->setIcon(iconForButtonType(Type, pParent->devicePixelRatioF()));
    return pButton;
}

KDDockWidgets::Core::ClassicIndicatorWindowViewInterface* CCustomViewFactory::createClassicIndicatorWindow(
    KDDockWidgets::Core::ClassicDropIndicatorOverlay* pOverlay, KDDockWidgets::Core::View*) const
{
    return new CDropIndicatorWindow(pOverlay);     // a window of its own, over the window being dragged (not parented to the drop area)
}

KDDockWidgets::Core::View* CCustomViewFactory::createRubberBand(KDDockWidgets::Core::View* pParent) const
{
    return new CDropPreview(pParent ? KDDockWidgets::QtCommon::View_qt::asQWidget(pParent) : nullptr);
}
