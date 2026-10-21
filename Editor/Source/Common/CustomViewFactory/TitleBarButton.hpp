// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/qtwidgets/views/TitleBar.h>

// The buttons of the docks (close, float / dock, maximize / restore, minimize, auto-hide) on the tab bars and on the title bars of
// floating windows, in the editor's look: only the icon, a soft rounded background under the mouse (a stronger one while pressed),
// and red with a white cross for Close. The icons are the editor's SVGs (CCustomViewFactory::iconForButtonType), sharp at any scale
// and in the colours of the theme in use.
//
// Derived from KDDockWidgets' own Button, not just a QToolButton: the title bar finds its buttons as that class when it goes, to
// delete a button whose click is deleting the title bar later instead of at once (closing a floating window from its close button).
class CTitleBarButton : public KDDockWidgets::QtWidgets::Button
{
    Q_OBJECT
public:
    CTitleBarButton(QWidget* pParent, KDDockWidgets::TitleBarButtonType Type);
    KDDockWidgets::TitleBarButtonType Type() const { return m_Type; }

    static constexpr int kSize = 20;       // logical pixels, square
    static constexpr int kIconSize = 14;

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* pEvent) override;

private:
    KDDockWidgets::TitleBarButtonType m_Type;
};
