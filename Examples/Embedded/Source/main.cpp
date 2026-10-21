// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// embedded_example: FlowGraph embedded in a Qt application.
//
//   embedded_example [--selftest] [--screenshot file.png] [--write-samples dir]
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
#include <QSettings>
#include <QTimer>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QString(argv[i]) == "--selftest") qputenv("QT_QPA_PLATFORM", "offscreen");     // nothing is shown
    QApplication app(argc, argv);
    app.setApplicationName("FlowGraph embedded example");
    fged::applySavedTheme(app, "Message Tables");       // optional: the theme chosen in the editor (View > Theme), Dark at first
    fged::initEditor();                     // once, after the QApplication exists: prepares the script editor

    // The runtime knows the types, events and nodes of this application; every table shares it.
    fg::Runtime runtime;
    msgapp::registerMessageTypes(runtime.registry());

    bool selftest = false;
    QString screenshot;                     // developer aid: save a picture of the window a little after the start, then quit
    QString samples;                        // --write-samples dir: writes the sample scripts (list.flow, counter.flow) there and quits
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--selftest") selftest = true;
        else if (args[i] == "--screenshot" && i + 1 < args.size()) screenshot = args[++i];
        else if (args[i] == "--write-samples" && i + 1 < args.size()) samples = args[++i];
    }
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
    window.newTable();
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
            QTimer::singleShot(1500, &app, [&, editor] {
                editor->window()->grab().save(screenshot + ".editor.png");
                app.quit();
            });
        });
    }
    return app.exec();
}
