// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <kddockwidgets/qtwidgets/ViewFactory.h>


class CCustomViewFactory: public KDDockWidgets::QtWidgets::ViewFactory
{
    Q_OBJECT
public:
    KDDockWidgets::Core::View* createGroup(KDDockWidgets::Core::Group* pGroup, KDDockWidgets::Core::View* pParent) const override;
    KDDockWidgets::Core::View* createTabBar(KDDockWidgets::Core::TabBar* pTabBar, KDDockWidgets::Core::View* pParent) const override;
    KDDockWidgets::Core::View* createStack(KDDockWidgets::Core::Stack* pStack, KDDockWidgets::Core::View* pParent) const override;
    // The buttons of the docks and their icons: the editor's (CTitleBarButton, Icons/dock-*.svg) instead of KDDockWidgets' bitmaps.
    QIcon iconForButtonType(KDDockWidgets::TitleBarButtonType Type, qreal Dpr) const override;
    QAbstractButton* createTitleBarButton(QWidget* pParent, KDDockWidgets::TitleBarButtonType Type) const override;
    // The drop targets and the preview of where a dragged dock goes (CDropIndicatorWindow, CDropPreview).
    KDDockWidgets::Core::ClassicIndicatorWindowViewInterface* createClassicIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* pOverlay,
                                                                                          KDDockWidgets::Core::View* pParent) const override;
    KDDockWidgets::Core::View* createRubberBand(KDDockWidgets::Core::View* pParent) const override;
    // we can customize createDockWidget for persistentcentralwidget and ... but we dont need it for now
};
