#pragma once

#include <kddockwidgets/qtwidgets/views/DockWidget.h>

class CDockWidget: public KDDockWidgets::QtWidgets::DockWidget
{
    Q_OBJECT
public:
    explicit CDockWidget(const QString& UniqueName, KDDockWidgets::DockWidgetOptions DWOptions = {},
                        KDDockWidgets::LayoutSaverOptions LSOptions = {},
                        Qt::WindowFlags windowFlags = Qt::Tool);

    ~CDockWidget() override;



public:
    bool IsTabBarHidden() const { return m_IsTabBarHidden; }
    void SetTabBarHidden(bool Hidden) { m_IsTabBarHidden = Hidden; }

protected:
    bool                    m_IsTabBarHidden;
};

