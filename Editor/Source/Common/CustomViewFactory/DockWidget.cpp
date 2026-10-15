#include "DockWidget.hpp"

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
