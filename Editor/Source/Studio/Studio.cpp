#include <FlowGraphEditor/Studio.hpp>

#include "../Common/CustomViewFactory/CustomViewFactory.hpp"
#include "StudioWindow.hpp"

#include <QStyleFactory>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>

static void initResources() { Q_INIT_RESOURCE(Resources); }

namespace fged {

void applyDarkTheme(QApplication& a) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    const QColor darkBg(45, 45, 45), darkerBg(35, 35, 35), lightBg(53, 53, 53), textColor(240, 240, 240), disabledText(120, 120, 120);
    const QColor highlight(255, 127, 0), highlightedText(Qt::black);
    QPalette p;
    p.setColor(QPalette::Window, darkBg);
    p.setColor(QPalette::WindowText, textColor);
    p.setColor(QPalette::Base, darkerBg);
    p.setColor(QPalette::AlternateBase, lightBg);
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::ButtonText, textColor);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::ToolTipText, textColor);
    p.setColor(QPalette::ToolTipBase, darkBg);
    p.setColor(QPalette::Button, lightBg);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Base, darkBg);
    p.setColor(QPalette::Disabled, QPalette::Window, darkBg);
    p.setColor(QPalette::Disabled, QPalette::Button, darkBg);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, highlight.darker(150));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    p.setColor(QPalette::Link, QColor(42, 130, 218));
    p.setColor(QPalette::LinkVisited, QColor(80, 80, 200));
    p.setColor(QPalette::PlaceholderText, QColor(160, 160, 160));
    p.setColor(QPalette::Light, lightBg.lighter(120));
    p.setColor(QPalette::Midlight, lightBg);
    p.setColor(QPalette::Dark, darkerBg);
    p.setColor(QPalette::Mid, darkBg);
    p.setColor(QPalette::Shadow, Qt::black);
    a.setPalette(p);
}

void initEditor() {
    initResources();
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    auto flags = KDDockWidgets::Config::self().flags();
    flags |= KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;
    flags |= KDDockWidgets::Config::Flag_AllowReorderTabs;
    flags |= KDDockWidgets::Config::Flag_AlwaysShowTabs;
    flags |= KDDockWidgets::Config::Flag_DoubleClickMaximizes;
    flags |= KDDockWidgets::Config::Flag_AutoHideSupport;
    flags |= KDDockWidgets::Config::Flag_CloseOnlyCurrentTab;
    flags |= KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden;
    flags |= KDDockWidgets::Config::Flag_KeepAboveIfNotUtilityWindow;
    KDDockWidgets::Config::self().setFlags(flags);
    KDDockWidgets::Config::self().setViewFactory(new CCustomViewFactory());
}

QWidget* createStudio(fg::Runtime& rt, const QString& openFile) {
    auto* w = new StudioWindow(rt);
    if (!openFile.isEmpty()) w->openFile(openFile);
    w->show();
    return w;
}

void devRun(QWidget* studio, const QString& entry, const QString& breakpoint) {
    auto* w = dynamic_cast<StudioWindow*>(studio);
    if (!w) return;
    if (!breakpoint.isEmpty()) {
        QStringList parts = breakpoint.split(':');
        if (parts.size() == 2) w->document()->debugger().setBreakpoint(parts[0].toStdString(), parts[1].toInt(), true);
    }
    w->runEntry(entry);
}

void devSelect(QWidget* studio, const QString& spec) {
    if (auto* w = dynamic_cast<StudioWindow*>(studio)) w->devSelect(spec);
}

void devProbe(QWidget* studio, const QString& file, bool coordinates) {
    if (auto* w = dynamic_cast<StudioWindow*>(studio)) w->devProbe(file, coordinates);
}

void devSceneShot(QWidget* studio, const QString& file) {
    if (auto* w = dynamic_cast<StudioWindow*>(studio)) w->devSceneShot(file);
}

} // namespace fged
