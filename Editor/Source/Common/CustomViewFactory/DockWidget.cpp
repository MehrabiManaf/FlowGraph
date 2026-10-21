// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "DockWidget.hpp"
#include <kddockwidgets/core/DockWidget.h>

namespace {
const char* kTabBarHiddenKey = "tabBarHidden";
}

CDockWidget::CDockWidget(const QString& UniqueName, KDDockWidgets::DockWidgetOptions DWOptions,
                     KDDockWidgets::LayoutSaverOptions LSOptions,
                     Qt::WindowFlags windowFlags):
    KDDockWidgets::QtWidgets::DockWidget(UniqueName, DWOptions, LSOptions, windowFlags)
{
    m_IsTabBarHidden = false;
}

CDockWidget::~CDockWidget()
{

}

void CDockWidget::SetTabBarHidden(bool Hidden)
{
    m_IsTabBarHidden = Hidden;
    auto* pController = static_cast<KDDockWidgets::Core::DockWidget*>(controller());
    if(!pController)
        return;
    auto UserData = pController->userData();
    if(UserData.value(kTabBarHiddenKey, false).toBool() == Hidden)
        return;
    UserData[kTabBarHiddenKey] = Hidden;
    pController->setUserData(UserData);
}

void CDockWidget::LoadTabBarState()
{
    auto* pController = static_cast<KDDockWidgets::Core::DockWidget*>(controller());
    m_IsTabBarHidden = pController && pController->userData().value(kTabBarHiddenKey, false).toBool();
}
