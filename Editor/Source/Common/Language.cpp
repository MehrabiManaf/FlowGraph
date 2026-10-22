// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// The language of the editor (ScriptEditor.hpp: availableLanguages, applyLanguage...). The editor's translations are compiled into it
// (:/i18n/flowgraph_<code>.qm, from Editor/Translations/flowgraph_<code>.ts); Qt's own (qtbase_<code>.qm, its dialogs and buttons)
// are taken from next to the program, where the release packages put them, or from the Qt installation.
#include <FlowGraphEditor/ScriptEditor.hpp>

#include "NodeTexts.hpp"
#include "Settings.hpp"

#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTranslator>

#include <set>

namespace fged {

namespace {

QTranslator* g_editor = nullptr;        // the installed translators (null while the language is English)
QTranslator* g_qt = nullptr;
QString g_current = QStringLiteral("en");
fg::Translations g_nodes;               // the texts of the nodes (empty while the language is English)
std::set<std::string> g_pluginsLooked;  // the plugin libraries whose catalog of this language was looked for

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
    g_nodes.clear();
    g_pluginsLooked.clear();
    if (g_current != QLatin1String("en")) {
        g_editor = new QTranslator(&app);
        if (g_editor->load(kPrefix + "flowgraph_" + g_current + ".qm")) app.installTranslator(g_editor);
        g_qt = new QTranslator(&app);
        loadQtTranslation(*g_qt, g_current);
        if (!g_qt->isEmpty()) app.installTranslator(g_qt);
        loadNodeTranslations(kPrefix + "nodes_" + g_current + ".json");        // the built-in nodes (Core/Translations/<code>.json)
    }
    // only the texts change: the panels stay left to right in every language (a mirrored editor looked odd); Qt still writes the
    // letters of a right-to-left text (Persian, Arabic...) in their order inside each label and menu item
    app.setLayoutDirection(Qt::LeftToRight);
}

QString currentLanguage() { return g_current; }

QMenu* createLanguageMenu(const QString& applicationName, QWidget* parent) {
    // System, then every language there is a translation of, each in its own language
    auto* menu = new QMenu(QCoreApplication::translate("fged::Language", "&Language"), parent);
    auto* group = new QActionGroup(menu);
    const QString chosen = savedLanguage(applicationName);
    QStringList codes{QStringLiteral("system")};
    codes << availableLanguages();
    for (const QString& code : codes) {
        QAction* a = menu->addAction(code == QLatin1String("system") ? QCoreApplication::translate("fged::Language", "System") : languageName(code));
        a->setCheckable(true);
        a->setChecked(chosen == code);
        a->setData(code);
        if (code == QLatin1String("system"))
            a->setToolTip(QCoreApplication::translate("fged::Language", "The language of the operating system, when there is a translation of it"));
        group->addAction(a);
        QObject::connect(a, &QAction::triggered, menu, [menu, applicationName, code] {
            if (savedLanguage(applicationName) == code) return;
            saveLanguage(applicationName, code);
            // the texts are taken when the widgets are made: a restart shows the new language everywhere
            QMessageBox::information(menu->parentWidget() ? menu->parentWidget()->window() : nullptr,
                                     QCoreApplication::translate("fged::Language", "Language"),
                                     QCoreApplication::translate("fged::Language", "%1 shows the new language when it is started again.")
                                         .arg(applicationName));
        });
    }
    return menu;
}

bool loadNodeTranslations(const QString& catalogFile, QString* error) {
    QFile f(catalogFile);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(catalogFile);
        return false;
    }
    std::string why;
    const bool ok = g_nodes.loadJson(f.readAll().toStdString(), &why);
    if (!ok && error) *error = QString::fromStdString(why);
    return ok;
}

void loadPluginTranslations(const fg::Runtime& rt) {
    if (g_current == QLatin1String("en")) return;
    const auto& names = rt.loadedPlugins();
    const auto& files = rt.loadedPluginFiles();
    for (size_t i = 0; i < files.size() && i < names.size(); ++i) {
        if (!g_pluginsLooked.insert(files[i]).second) continue;
        // <plugin folder>/translations/<Plugin>_<code>.json (Translations.hpp)
        const QString catalog = QFileInfo(QString::fromStdString(files[i])).dir().filePath(
            QStringLiteral("translations/%1_%2.json").arg(QString::fromStdString(names[i]), g_current));
        QString error;
        if (QFile::exists(catalog) && !loadNodeTranslations(catalog, &error)) qWarning("%s: %s", qPrintable(catalog), qPrintable(error));
    }
}

const fg::Translations& nodeTexts() { return g_nodes; }
fg::Translations& editableNodeTexts() { return g_nodes; }

QString savedLanguage(const QString& applicationName) {
    return editorSettings().value(appSettingsKey(applicationName, QStringLiteral("language")), QStringLiteral("system")).toString();
}

void saveLanguage(const QString& applicationName, const QString& code) {
    editorSettings().setValue(appSettingsKey(applicationName, QStringLiteral("language")), code);
}

void applySavedLanguage(QApplication& app, const QString& applicationName) { applyLanguage(app, savedLanguage(applicationName)); }

} // namespace fged
