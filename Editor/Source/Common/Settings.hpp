#pragma once
#include <QSettings>

namespace fged {

// The settings of the editor. Native format unless the application chose another default format
// (QSettings::setDefaultFormat), which the tests do to stay away from the user's real settings.
inline QSettings editorSettings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("FlowGraph"), QStringLiteral("FlowGraph Studio"));
}

// The key of a setting that belongs to one application (apps/<application name>/<name>): the dock layout, the recent files, the theme.
inline QString appSettingsKey(QString application, const QString& name) {
    application.replace('/', '_').replace('\\', '_');       // a separator would make groups of the name
    return QStringLiteral("apps/") + application + QLatin1Char('/') + name;
}

} // namespace fged
