// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// embedded_example: FlowGraph embedded in a Qt application.
//
//   embedded_example [--load script.flow] [--selftest] [--screenshot file.png] [--write-samples dir] [--translations messages_<code>.json]
//
// A main window with dock widgets. The application receives text messages (here: a random one, two per second) into a message store. "New Table"
// adds a table; the script of a table (a .flow file, edited with the embedded FlowGraph script editor) decides which columns the table has and
// what goes into its rows: the event NewMessage is raised for every message, and every Add Row node that runs adds a row. The script keeps its
// variables from message to message; saving it in the editor starts it again over the messages received so far. See README.md for what
// this example shows of FlowGraph.
#include "MainWindow.hpp"
#include "SelfTest.hpp"

#include <FlowGraphEditor/ScriptEditor.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

// This application's own texts in the language the editor uses (fged::currentLanguage): its windows (Translations/
// embedded_example_<code>.ts) and its nodes (Translations/messages_<code>.json), both compiled in. A language without them shows
// this application's texts in English (the editor's are still translated).
static void loadOwnTranslations(QApplication& app) {
    const QString code = fged::currentLanguage();
    if (code == QLatin1String("en")) return;
    auto* own = new QTranslator(&app);
    if (own->load(":/i18n/embedded_example_" + code + ".qm")) app.installTranslator(own);
    fged::loadNodeTranslations(":/i18n/messages_" + code + ".json");      // after applyLanguage: it starts the catalog afresh
}

// --translations FILE: writes or updates the catalog of this application's nodes (messages_<code>.json; texts added untranslated,
// removed ones dropped), like `flow --translations` does for the standard ones.
static int writeTranslations(const fg::Runtime& runtime, const QString& file) {
    QString code = QFileInfo(file).completeBaseName();
    if (code.startsWith("messages_")) code = code.mid(9);
    QFile f(file);
    const std::string existing = f.open(QIODevice::ReadOnly) ? f.readAll().toStdString() : std::string();
    f.close();
    fg::CatalogReport report;
    const std::string text = fg::updateCatalog(runtime.registry(), existing, code.toStdString(), &report, "messages");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(text.data(), qint64(text.size())) != qint64(text.size())) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(file));
        return 1;
    }
    std::printf("%s: %zu texts, %zu translated, %zu new, %zu removed\n", qPrintable(file), report.texts, report.translated, report.added,
                report.removed);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QString(argv[i]) == "--selftest") qputenv("QT_QPA_PLATFORM", "offscreen");     // nothing is shown
    QApplication app(argc, argv);
    app.setApplicationName("FlowGraph embedded example");

    bool selftest = false;
    QString screenshot;                     // developer aid: save a picture of the window a little after the start, then quit
    QString samples;                        // --write-samples dir: writes the sample scripts (list.flow, counter.flow) there and quits
    QString catalog;                        // --translations FILE: see writeTranslations
    QString load;                           // --load FILE: the first table starts with that script (list.flow, counter.flow...)
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--selftest") selftest = true;
        else if (args[i] == "--screenshot" && i + 1 < args.size()) screenshot = args[++i];
        else if (args[i] == "--write-samples" && i + 1 < args.size()) samples = args[++i];
        else if (args[i] == "--translations" && i + 1 < args.size()) catalog = args[++i];
        else if (args[i] == "--load" && i + 1 < args.size()) load = args[++i];
    }

    fged::applySavedTheme(app, msgapp::kApplicationName);     // optional: the theme chosen in the editor (View > Theme), Dark at first
    // optional: the language chosen in View > Language (the system's at first), before any window is made; the self test checks
    // English texts
    if (selftest) fged::applyLanguage(app, "en");
    else fged::applySavedLanguage(app, msgapp::kApplicationName);
    loadOwnTranslations(app);
    fged::initEditor();                     // once, after the QApplication exists: prepares the script editor

    // The runtime knows the types, events and nodes of this application; every table shares it.
    fg::Runtime runtime;
    msgapp::registerMessageTypes(runtime.registry());

    if (!catalog.isEmpty()) return writeTranslations(runtime, catalog);
    if (!samples.isEmpty()) {
        msgapp::makeListScript().save((samples + "/list.flow").toStdString());
        msgapp::makeCounterScript().save((samples + "/counter.flow").toStdString());
        return 0;
    }

    msgapp::MessageStore store;
    if (selftest) {
        // the self test must neither read nor change the settings of the real application (dock layout, recent files)
        const QString dir = QDir::temp().filePath("FlowGraph-embedded-selftest-settings");
        QDir(dir).removeRecursively();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
        return msgapp::runSelfTest(runtime, store) ? 1 : 0;
    }

    msgapp::MainWindow window(runtime, store);
    msgapp::TableDock* first = window.newTable();
    QString reason;
    if (!load.isEmpty() && !first->openScript(load, &reason)) std::fprintf(stderr, "%s\n", qPrintable(reason));
    window.show();
    store.startGenerating();                // a random message every 500 ms (two per second): the tables run their scripts for each of them
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(2500, &app, [&] {
            window.grab().save(screenshot);
            // ... and of the script editor of the first table, with its event node selected
            msgapp::TableDock* table = window.tables().value(0);
            if (!table) { app.quit(); return; }
            table->editScript();
            fged::ScriptEditor* editor = window.editorHub().editor();
            editor->devSelect("node:EventGraph:1");
            QTimer::singleShot(1200, &app, [editor] { editor->devFrameAll(); });     // the whole script, as large as it fits
            QTimer::singleShot(1500, &app, [&, editor] {
                editor->window()->grab().save(screenshot + ".editor.png");
                app.quit();
            });
        });
    }
    return app.exec();
}
