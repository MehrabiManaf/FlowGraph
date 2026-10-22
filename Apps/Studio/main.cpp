// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// flowstudio: the editor and debugger of the standard applications (the ones `flow` runs).
//
//   flowstudio [script.flow]
//
// Plugins are loaded like `flow` does: FLOW_PLUGIN_PATH and the plugins folder next to the program.
#include "StandardEditorHost.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("FlowGraph Studio");
    fged::applySavedTheme(app, "FlowGraph Studio");     // the theme chosen in View > Theme (Dark the first time)
    fged::applySavedLanguage(app, "FlowGraph Studio");  // the language chosen in View > Language (the system's the first time)
    fged::initEditor();

    fg::Runtime rt;
    fg::standard::registerStandardApp(rt.registry());
    std::vector<std::string> errors;
    rt.loadPluginsFromEnvironment("FLOW_PLUGIN_PATH", &errors);
    QString pluginDir = QDir(QCoreApplication::applicationDirPath()).filePath("plugins");
    if (QFileInfo::exists(pluginDir)) rt.loadPluginsFrom(pluginDir.toStdString(), &errors);
    for (auto& e : errors) qWarning("plugin: %s", e.c_str());

    flowstudio::StandardEditorHost host(rt);
    fged::ScriptEditor editor(host);
    editor.newScript();
    if (app.arguments().size() > 1) editor.open(app.arguments().at(1));
    editor.show();

    fged::ScriptEditor* ed = &editor;
    if (qEnvironmentVariableIsSet("FG_STUDIO_SELECT")) {
        QTimer::singleShot(600, &app, [ed] { ed->devSelect(qEnvironmentVariable("FG_STUDIO_SELECT")); });
    }
    // Developer aid: FG_STUDIO_PROBE=<prefix> writes <prefix>.coords after 2 s and <prefix>.final after 10 s, then quits.
    if (qEnvironmentVariableIsSet("FG_STUDIO_PROBE")) {
        QString prefix = qEnvironmentVariable("FG_STUDIO_PROBE");
        QTimer::singleShot(2000, &app, [ed, prefix] { ed->devProbe(prefix + ".coords", true); });
        QTimer::singleShot(10000, &app, [ed, prefix, &app] { ed->devProbe(prefix + ".final", false); app.quit(); });
    }
    // Developer aid: FG_STUDIO_RUN=<entry> [FG_STUDIO_BREAK=<graph>:<node id>] starts a run shortly after startup.
    if (qEnvironmentVariableIsSet("FG_STUDIO_RUN")) {
        QTimer::singleShot(800, &app, [ed] { ed->devRun(qEnvironmentVariable("FG_STUDIO_RUN"), qEnvironmentVariable("FG_STUDIO_BREAK")); });
    }
    // Developer aid: FG_STUDIO_SCREENSHOT=<file.png> saves a screenshot of the window and exits.
    if (qEnvironmentVariableIsSet("FG_STUDIO_SCREENSHOT")) {
        QString out = qEnvironmentVariable("FG_STUDIO_SCREENSHOT");
        QTimer::singleShot(2500, &app, [ed, out, &app] {
            ed->window()->grab().save(out);
            if (qEnvironmentVariableIsSet("FG_STUDIO_SCENESHOT")) ed->devSceneShot(qEnvironmentVariable("FG_STUDIO_SCENESHOT"));
            app.quit();
        });
    }
    // Developer aid: FG_STUDIO_SIZE=<width>x<height> sizes the window. FG_STUDIO_RECORD=<folder> saves a picture of the window
    // every FG_STUDIO_RECORD_MS (100) ms from 1 s after the start, FG_STUDIO_RECORD_COUNT (50) of them, then exits: the frames of
    // a recording (the GIF of the README).
    if (qEnvironmentVariableIsSet("FG_STUDIO_SIZE")) {
        const QStringList wh = qEnvironmentVariable("FG_STUDIO_SIZE").split('x');
        if (wh.size() == 2) ed->window()->resize(wh[0].toInt(), wh[1].toInt());
    }
    if (qEnvironmentVariableIsSet("FG_STUDIO_RECORD")) {
        const QString dir = qEnvironmentVariable("FG_STUDIO_RECORD");
        const int every = qEnvironmentVariableIsSet("FG_STUDIO_RECORD_MS") ? qEnvironmentVariableIntValue("FG_STUDIO_RECORD_MS") : 100;
        const int count = qEnvironmentVariableIsSet("FG_STUDIO_RECORD_COUNT") ? qEnvironmentVariableIntValue("FG_STUDIO_RECORD_COUNT") : 50;
        QDir().mkpath(dir);
        auto* timer = new QTimer(&app);
        auto* taken = new int(0);
        QObject::connect(timer, &QTimer::timeout, &app, [=, &app] {
            ed->window()->grab().save(QDir(dir).filePath(QString("frame_%1.png").arg(*taken, 4, 10, QChar('0'))));
            if (++*taken >= count) {
                timer->stop();
                delete taken;
                app.quit();
            }
        });
        QTimer::singleShot(1000, &app, [timer, every] { timer->start(every); });
    }
    return app.exec();
}
