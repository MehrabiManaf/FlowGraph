#pragma once
// The script editor as a library. An application that embeds FlowGraph and wants its users to create, change,
// run and debug scripts creates a ScriptEditor and tells it, through an EditorHost, which runtime to use and what
// "run" means for that application (which events can be started and with which arguments).
//
//   class MyHost : public fged::EditorHost {
//       fg::Runtime& runtime() override { return runtime_; }
//       QString applicationName() const override { return "My Game Editor"; }
//       std::vector<fged::LaunchEntry> launchEntries(const fg::Program&) override { ... }   // optional
//       ...
//   };
//   fged::applyDarkTheme(app);   // optional
//   fged::initEditor();          // once, after QApplication exists
//   MyHost host;
//   fged::ScriptEditor editor(host);
//   editor.open("level1.flow");
//   editor.show();
//
// flowstudio, the editor of the standard applications, is such a host (see Apps/Studio).
#include <FlowGraph/Runtime.hpp>

#include <QApplication>
#include <QObject>
#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

class QMenu;
class QToolBar;

namespace fged {

class ScriptEditor;

enum class ScriptChange { Created, Opened, Saved };

// One item of the "what to run" box of the toolbar.
struct LaunchEntry {
    QString id;                              // unique among the entries (what the combo box remembers)
    QString title;                           // text shown to the user
    QString entry;                           // the event or function that is started
    std::vector<fg::ParamDecl> params;       // its parameters
};

// How a run is started; EditorHost::prepareLaunch fills it in.
struct LaunchSpec {
    QString entry;
    std::vector<fg::Value> args;
    int stepDelayMs = -1;                    // slow motion while debugging; -1 keeps the current setting
};

// Implemented by the application that embeds the editor. Everything but runtime() has a sensible default.
class EditorHost {
public:
    virtual ~EditorHost() = default;

    // The runtime that provides the types, functions and events of the application. It outlives the editor.
    virtual fg::Runtime& runtime() = 0;
    virtual QString applicationName() const { return QStringLiteral("Script Editor"); }

    // What the user can run. Default: every event of the script and every function.
    virtual std::vector<LaunchEntry> launchEntries(const fg::Program& program);
    // Called when the user presses Run: set the arguments (and more) of the run; return false to cancel.
    // Default: asks for a value of every parameter of the entry.
    virtual bool prepareLaunch(const LaunchEntry& entry, LaunchSpec& spec, QWidget* parent);
    // Right before a run is started / after it ended (finished, failed or stopped): set up and restore the
    // environment of the run (environment variables, working directory...).
    virtual void aboutToRun(const LaunchSpec&) {}
    virtual void runFinished() {}

    // Fill a new script: add the event handlers (and whatever else) a new script of this application starts with.
    // The script already has no graphs; call script.addEventGraph() first. Default: one empty event graph.
    virtual void initializeNewScript(fg::Script& script) { script.addEventGraph("EventGraph"); }

    // Called once when the editor window exists: add your own menus and toolbar buttons here.
    virtual void attached(ScriptEditor&) {}
    // Called after a script was created, opened (before it is checked) or saved (possibly under a new name).
    virtual void scriptChanged(ScriptEditor&, ScriptChange) {}
    // The entry that is preselected in the toolbar when the list of entries is rebuilt.
    virtual QString preferredEntry() const { return QStringLiteral("Main"); }
    // Title of the tooltip of the entries box of the toolbar ("Run" / "Launch configuration"; a trailing colon is dropped) and the text below it.
    virtual QString launchLabel() const { return QStringLiteral("Run:"); }
    virtual QString launchToolTip() const { return QStringLiteral("Event or function to run"); }
    // The user picked another entry in the toolbar.
    virtual void launchEntrySelected(const QString& /*id*/) {}
};

class ScriptEditor : public QObject {
    Q_OBJECT
public:
    explicit ScriptEditor(EditorHost& host);
    ~ScriptEditor() override;
    ScriptEditor(const ScriptEditor&) = delete;
    ScriptEditor& operator=(const ScriptEditor&) = delete;

    EditorHost& host() const { return host_; }
    QWidget* window() const;                         // the main window
    void show();

    // Scripts. open() reports a script that needs events / plugins this application does not have (it is opened
    // anyway so that it can be fixed, but it will not compile). Returns false when the file cannot be read.
    bool open(const QString& path);
    void newScript();
    bool save();
    bool saveAs();
    bool maybeSave();                                // asks the user when there are unsaved changes
    QString path() const;                            // empty for a script that was never saved
    bool isModified() const;

    // Customisation.
    QMenu* menu(const QString& title);               // a top level menu (created when it does not exist yet)
    QToolBar* toolBar();
    void addToLaunchSelector(QAction* action);       // a button right behind the entries box (e.g. "configure...")
    void refreshLaunchEntries();                     // launchEntries() would return something else now
    void refreshScript();                            // compile again (plugins or events were added to the runtime)
    void log(int level, const QString& text);        // level: 0 info, 1 warning, 2 error (fg::LogLevel)

    // Developer aids used by the smoke tests: run an entry, select an item, write screen coordinates, render the graph.
    void devRun(const QString& entry, const QString& breakpoint = QString());
    void devSelect(const QString& spec);
    void devProbe(const QString& file, bool coordinates);
    void devSceneShot(const QString& file);

signals:
    void scriptChanged();                            // opened, created or saved

private:
    friend class ScriptEditorWindow;
    EditorHost& host_;
    class ScriptEditorWindow* window_ = nullptr;
};

// Applies the dark palette / Fusion style used by FlowGraph Studio.
void applyDarkTheme(QApplication& app);
// Initialises KDDockWidgets with the editor's view factory. Call once after creating the QApplication.
void initEditor();

} // namespace fged
