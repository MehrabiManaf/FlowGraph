// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// The language of the editor (ScriptEditor.hpp: availableLanguages, applyLanguage...). The editor's translations are compiled into it
// (:/i18n/flowgraph_<code>.qm, from Editor/Translations/flowgraph_<code>.ts); Qt's own (qtbase_<code>.qm, its dialogs and buttons)
// are taken from next to the program, where the release packages put them, or from the Qt installation.
#include <FlowGraphEditor/ScriptEditor.hpp>

#include "Settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTranslator>

namespace fged {

namespace {

QTranslator* g_editor = nullptr;        // the installed translators (null while the language is English)
QTranslator* g_qt = nullptr;
QString g_current = QStringLiteral("en");

const QString kPrefix = QStringLiteral(":/i18n/");

bool hasTranslation(const QString& code) { return code == QLatin1String("en") || QFile::exists(kPrefix + "flowgraph_" + code + ".qm"); }

// "system" and region variants to a language there is a translation of: fa-IR -> fa_IR, else fa; nothing fits -> en
QString resolve(const QString& code) {
    QStringList wanted;
    if (code.isEmpty() || code == QLatin1String("system")) wanted = QLocale::system().uiLanguages();
    else wanted << code;
    for (QString w : wanted) {
        w.replace('-', '_');
        if (hasTranslation(w)) return w;
        const QString language = w.section('_', 0, 0);
        if (hasTranslation(language)) return language;
    }
    return QStringLiteral("en");
}

void loadQtTranslation(QTranslator& tr, const QString& code) {
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& dir : {appDir + "/translations", appDir + "/../translations", QLibraryInfo::path(QLibraryInfo::TranslationsPath)})
        if (tr.load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"), dir)) return;
}

} // namespace

QStringList availableLanguages() {
    QStringList out{QStringLiteral("en")};
    static const QRegularExpression name(QStringLiteral("^flowgraph_(.+)\\.qm$"));
    for (const QString& file : QDir(kPrefix).entryList(QDir::Files, QDir::Name))
        if (auto m = name.match(file); m.hasMatch()) out << m.captured(1);
    return out;
}

QString languageName(const QString& code) {
    if (code == QLatin1String("en")) return QStringLiteral("English");
    QString n = QLocale(code).nativeLanguageName();
    if (n.isEmpty()) return code;
    n[0] = n[0].toUpper();          // "deutsch" -> "Deutsch" (no effect on scripts without letter case)
    return n;
}

void applyLanguage(QApplication& app, const QString& code) {
    for (QTranslator** t : {&g_editor, &g_qt})
        if (*t) {
            app.removeTranslator(*t);
            delete *t;
            *t = nullptr;
        }
    g_current = resolve(code);
    if (g_current != QLatin1String("en")) {
        g_editor = new QTranslator(&app);
        if (g_editor->load(kPrefix + "flowgraph_" + g_current + ".qm")) app.installTranslator(g_editor);
        g_qt = new QTranslator(&app);
        loadQtTranslation(*g_qt, g_current);
        if (!g_qt->isEmpty()) app.installTranslator(g_qt);
    }
    // only the texts change: the panels stay left to right in every language (a mirrored editor looked odd); Qt still writes the
    // letters of a right-to-left text (Persian, Arabic...) in their order inside each label and menu item
    app.setLayoutDirection(Qt::LeftToRight);
}

QString currentLanguage() { return g_current; }

QString savedLanguage(const QString& applicationName) {
    return editorSettings().value(appSettingsKey(applicationName, QStringLiteral("language")), QStringLiteral("system")).toString();
}

void saveLanguage(const QString& applicationName, const QString& code) {
    editorSettings().setValue(appSettingsKey(applicationName, QStringLiteral("language")), code);
}

void applySavedLanguage(QApplication& app, const QString& applicationName) { applyLanguage(app, savedLanguage(applicationName)); }

} // namespace fged
