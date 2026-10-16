#include <FlowGraphEditor/Studio.hpp>

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("FlowGraph Studio");
    fged::applyDarkTheme(app);
    fged::initEditor();

    fg::Runtime rt;
    // plugins: <exe dir>/plugins/*.dll
    QString pluginDir = QDir(QCoreApplication::applicationDirPath()).filePath("plugins");
    if (QFileInfo::exists(pluginDir)) {
        std::vector<std::string> errors;
        rt.loadPluginsFrom(pluginDir.toStdString(), &errors);
        for (auto& e : errors) qWarning("plugin: %s", e.c_str());
    }
    QString file = app.arguments().size() > 1 ? app.arguments().at(1) : QString();
    auto* w = fged::createStudio(rt, file);
    if (qEnvironmentVariableIsSet("FG_STUDIO_SELECT")) {
        QTimer::singleShot(600, &app, [w] { fged::devSelect(w, qEnvironmentVariable("FG_STUDIO_SELECT")); });
    }
    // Developer aid: FG_STUDIO_PROBE=<prefix> writes <prefix>.coords after 2 s and <prefix>.final after 10 s, then quits.
    if (qEnvironmentVariableIsSet("FG_STUDIO_PROBE")) {
        QString prefix = qEnvironmentVariable("FG_STUDIO_PROBE");
        QTimer::singleShot(2000, &app, [w, prefix] { fged::devProbe(w, prefix + ".coords", true); });
        QTimer::singleShot(10000, &app, [w, prefix, &app] { fged::devProbe(w, prefix + ".final", false); app.quit(); });
    }
    // Developer aid: FG_STUDIO_RUN=<entry> [FG_STUDIO_BREAK=<graph>:<node id>] starts a run shortly after startup.
    if (qEnvironmentVariableIsSet("FG_STUDIO_RUN")) {
        QTimer::singleShot(800, &app, [w] {
            fged::devRun(w, qEnvironmentVariable("FG_STUDIO_RUN"), qEnvironmentVariable("FG_STUDIO_BREAK"));
        });
    }
    // Developer aid: FG_STUDIO_SCREENSHOT=<file.png> saves a screenshot of the window and exits.
    if (qEnvironmentVariableIsSet("FG_STUDIO_SCREENSHOT")) {
        QString out = qEnvironmentVariable("FG_STUDIO_SCREENSHOT");
        QTimer::singleShot(2500, &app, [w, out, &app] {
            w->grab().save(out);
            if (qEnvironmentVariableIsSet("FG_STUDIO_SCENESHOT")) fged::devSceneShot(w, qEnvironmentVariable("FG_STUDIO_SCENESHOT"));
            app.quit();
        });
    }
    int rc = app.exec();
    delete w;
    return rc;
}
