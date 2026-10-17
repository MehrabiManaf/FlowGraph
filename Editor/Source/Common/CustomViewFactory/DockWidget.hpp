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
    // Whether the tab of this dock is collapsed while it is alone in its group. Saved with the layout (the user data of the dock).
    void SetTabBarHidden(bool Hidden);
    // Takes the state from the user data of the dock: call after a saved layout was restored.
    void LoadTabBarState();

protected:
    bool                    m_IsTabBarHidden;
};

