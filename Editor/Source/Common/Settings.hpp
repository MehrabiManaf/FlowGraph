#pragma once
#include <QSettings>

namespace fged {

// The settings of the editor. Native format unless the application chose another default format
// (QSettings::setDefaultFormat), which the tests do to stay away from the user's real settings.
inline QSettings editorSettings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("FlowGraph"), QStringLiteral("FlowGraph Studio"));
}

} // namespace fged
