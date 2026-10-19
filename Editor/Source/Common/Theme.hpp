#pragma once
// The editor's themes (fged::Theme: Dark, Light, System). A theme is a palette for the whole application plus the colours the
// editor paints with beyond it (ThemeColors): their meaning stays the same, their shade follows the background. The editor's
// icons are drawn in the colours of the theme in use (themedIcon). applyTheme (ScriptEditor.hpp) switches, ThemeNotifier tells.
#include <FlowGraphEditor/ScriptEditor.hpp>

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QWidget>

namespace fged {

struct ThemeColors {
    bool dark = true;
    QColor error, warning, success;         // text of errors, warnings and "compiled" in logs, lists, labels and the status bar
    QColor canvas, gridMinor, gridMajor;    // the graph view
    QColor nodeBody;                        // nodes, reroutes, the hollow of pins that are not connected
    QColor constLock;                       // the padlock of const pins
    QString css(const QColor& c) const { return c.name(QColor::HexRgb); }    // for style sheets and HTML
};

// The colours of the theme in use (System is resolved to Dark or Light).
const ThemeColors& themeColors();
// What the user chose (System stays System) and what is shown.
Theme currentThemeChoice();
bool darkThemeShown();

// Gives a label (any widget that draws text) its own text colour through its palette - not with a style sheet, which would freeze
// the rest of the widget's palette at the theme of the moment. The other roles keep following the application's palette.
inline void setTextColor(QWidget* w, const QColor& c) {
    QPalette p = w->palette();
    p.setColor(QPalette::WindowText, c);
    p.setColor(QPalette::Text, c);
    w->setPalette(p);
}

// The choice of an application (QSettings apps/<application>/theme); savedTheme / applySavedTheme read it (ScriptEditor.hpp).
void saveTheme(const QString& applicationName, Theme theme);

QString themeKey(Theme t);                                  // "dark", "light", "system" (settings)
Theme themeFromKey(const QString& key);                     // unknown -> Dark
QString themeTitle(Theme t);                                // "Dark", "Light", "System" (menus)

// Widgets that paint with theme colours, or keep them, listen to changed() (emitted after the palette was set).
class ThemeNotifier : public QObject {
    Q_OBJECT
public:
    static ThemeNotifier& instance();
signals:
    void changed();
};

// themedIcon (an icon of the editor's resources in the colours of the theme in use) is declared in ScriptEditor.hpp: hosts use it too.

} // namespace fged
