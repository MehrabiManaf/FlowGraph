// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
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
//   fged::applySavedTheme(app, "My Game Editor");   // optional: the theme chosen in the editor (View > Theme)
//   fged::initEditor();          // once, after QApplication exists
//   MyHost host;
//   fged::ScriptEditor editor(host);
//   editor.open("level1.flow");
//   editor.show();
//
// flowstudio, the editor of the standard applications, is such a host (see Apps/Studio).
#include <FlowGraph/Runtime.hpp>
#include <FlowGraphEditor/Export.hpp>

#include <QApplication>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QStringList>
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

// What the editor is for in an application.
enum class EditorMode {
    // The user works with script files: File > New / Open / Save / Save As, and Run starts an event or function of the script, chosen in
    // the toolbar (launch configurations...). This is what flowstudio is, and the default.
    Standalone,
    // The application owns the scripts and runs them itself (it raises the events), so the editor has no files and nothing to launch:
    //   - the application hands a script over with ScriptEditor::edit(), or the user opens a file; there is no New. Only a script that fits the
//     application is taken (see EditorHost::acceptsScript): anything else is refused and the editor keeps the script it has;
    //   - Save does not write a file: it gives the edited script back to the application (EditorHost::applyScript), which puts it to use;
    //     Save As still writes a file;
    //   - there is no Run: the debugger attaches to what the application runs instead (Attach / Detach, EditorHost::debugTarget).
    Embedded,
};

// Implemented by the application that embeds the editor. Everything but runtime() has a sensible default.
class FG_EDITOR_EXPORT EditorHost {
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

    // Standalone (default) or embedded, see EditorMode.
    virtual EditorMode mode() const { return EditorMode::Standalone; }
    // Embedded mode: can this script be edited here? Called before a script replaces the one in the editor (edit(), File > Open), after the
    // editor itself has checked that the application provides everything the script uses (events, nodes, types: fg::checkCompatibility). Say
    // what is wrong in `reason` and return false to refuse it, e.g. a script that does not handle the event this application is made for.
    virtual bool acceptsScript(const fg::Script&, QString* reason) { (void)reason; return true; }
    // Embedded mode: the user pressed Save. Take the script (replace the one that runs, run it again...). Return false when it cannot be
    // used: the editor then keeps the changes as unsaved.
    virtual bool applyScript(ScriptEditor&, const fg::Script&) { return false; }
    // Embedded mode: the user pressed Attach. Return the executor that runs the script now, or null when nothing runs (see ScriptEditor::attach).
    virtual std::shared_ptr<fg::AsyncExecutor> debugTarget(ScriptEditor&) { return nullptr; }

    // Called once when the editor window exists: add your own menus and toolbar buttons here.
    virtual void attached(ScriptEditor&) {}
    // Called after a script was created, opened (before it is checked) or saved (possibly under a new name).
    virtual void scriptChanged(ScriptEditor&, ScriptChange) {}
    // The entry that is preselected in the toolbar when the list of entries is rebuilt.
    virtual QString preferredEntry() const { return QStringLiteral("Main"); }
    // Title of the tooltip of the entries box of the toolbar ("Run" / "Launch configuration"; a trailing colon is dropped) and the text below it.
    virtual QString launchLabel() const { return QCoreApplication::translate("fged::EditorHost", "Run:"); }
    virtual QString launchToolTip() const { return QCoreApplication::translate("fged::EditorHost", "Event or function to run"); }
    // The user picked another entry in the toolbar.
    virtual void launchEntrySelected(const QString& /*id*/) {}

    // Does the editor offer View > Theme (Dark / Light / System)? A theme changes the palette of the whole application, so an
    // application that looks after its own appearance says no. When it offers it, the editor applies the choice saved for this
    // application (applicationName()) when it is created and saves every new choice. Default: in Standalone mode.
    virtual bool offersThemeChoice() const { return mode() == EditorMode::Standalone; }
    // Does the editor offer View > Language? The language is the one of the whole application's Qt texts, so an application that
    // chooses it itself says no. The choice is saved per application (applicationName()) and applies at the next start
    // (applySavedLanguage). Default: in Standalone mode.
    virtual bool offersLanguageChoice() const { return mode() == EditorMode::Standalone; }
};

// There is ONE script editor in a program: creating a second one while the first exists throws std::logic_error. An application with
// several things to edit (several tables, objects...) lets the same editor edit them one after the other: edit().
class FG_EDITOR_EXPORT ScriptEditor : public QObject {
    Q_OBJECT
public:
    explicit ScriptEditor(EditorHost& host);
    ~ScriptEditor() override;
    static ScriptEditor* instance();                 // the editor of this program; null when there is none
    ScriptEditor(const ScriptEditor&) = delete;
    ScriptEditor& operator=(const ScriptEditor&) = delete;

    EditorHost& host() const { return host_; }
    QWidget* window() const;                         // the main window
    void show();

    // Scripts. open() reports a script that needs events / plugins this application does not have (it is opened
    // anyway so that it can be fixed, but it will not compile). Returns false when the file cannot be read.
    bool open(const QString& path);
    // Edits a script the application owns (no file): the title is what the window shows. Changes of the script that was edited before are
    // asked for first (Save gives them to the application); false when the user cancelled. An embedded editor takes only a script that fits the
    // application (EditorHost::acceptsScript); otherwise it returns false, says why in its log and keeps the script it has.
    bool edit(const fg::Script& script, const QString& title);
    void newScript();
    bool save();
    bool saveAs();
    bool maybeSave();                                // asks the user when there are unsaved changes
    QString path() const;                            // empty for a script that was never saved
    bool isModified() const;

    // Debugging a script the application runs (embedded mode). attach() makes the editor the debugger of the script that `executor` runs:
    // breakpoints stop it, the Debugger panel shows its variables and call stack, Pause / Continue / Step control it. Attaching to another
    // executor replaces the previous one; detach() lets everything that was stopped continue. The editor keeps `executor` alive while
    // attached. Edit() of another script detaches. The application attaches the new executor itself when it replaces the one that runs
    // while the editor is attached (isAttached()): do it before the events are posted.
    bool attach(std::shared_ptr<fg::AsyncExecutor> executor);
    void detach();
    bool isAttached() const;

    // Customisation.
    QMenu* menu(const QString& title);               // a top level menu (created when it does not exist yet)
    QToolBar* toolBar();
    void addToLaunchSelector(QAction* action);       // a button right behind the entries box (e.g. "configure...")
    void refreshLaunchEntries();                     // launchEntries() would return something else now
    void refreshScript();                            // compile again (plugins or events were added to the runtime)
    void log(int level, const QString& text);        // level: 0 info, 1 warning, 2 error (fg::LogLevel)

    // Developer aids used by the smoke tests: run an entry, select an item, frame the graph, write screen coordinates, render the
    // graph, write the layout of the docks (as it would be saved).
    void devRun(const QString& entry, const QString& breakpoint = QString());
    void devSelect(const QString& spec);
    void devFrameAll();
    void devProbe(const QString& file, bool coordinates);
    void devSceneShot(const QString& file);
    void devLayout(const QString& file);

signals:
    void scriptChanged();                            // opened, created or saved
    void attachedChanged(bool attached);             // the debugger attached to / detached from an executor

private:
    friend class ScriptEditorWindow;
    EditorHost& host_;
    class ScriptEditorWindow* window_ = nullptr;
};

// The look of the editor - and of the whole application: a theme is a palette for every window of it (Fusion style).
//   Dark    the look FlowGraph Studio always had
//   Light   dark text on light backgrounds; the editor's icons and coloured texts adapt
//   System  Light or Dark like the operating system, and it follows when the system switches
enum class Theme { Dark, Light, System };
FG_EDITOR_EXPORT void applyTheme(QApplication& app, Theme theme);
// = applyTheme(app, Theme::Dark).
FG_EDITOR_EXPORT void applyDarkTheme(QApplication& app);
// The theme the user chose in the editor of the application `applicationName` (View > Theme; it is kept per application), Dark when
// none was chosen. An application whose editor offers the choice (EditorHost::offersThemeChoice) calls applySavedTheme at its start,
// before it shows a window, so that its own windows come up in that theme too.
FG_EDITOR_EXPORT Theme savedTheme(const QString& applicationName);
FG_EDITOR_EXPORT void applySavedTheme(QApplication& app, const QString& applicationName);

// The language of the editor's texts (menus, panels, dialogs; Qt's own dialogs too). Scripts do not depend on it: they keep their
// English node names, so a script made in one language opens unchanged in any other.
// A language is a code ("en", "fa", "de", "pt_BR"...); "system" is the operating system's language. English is built in; another
// language is there when its translation is (Translations/flowgraph_<code>.ts, compiled into the editor): adding a language is adding
// that file. A text without a translation shows in English. Only the texts change: the layout stays left to right in every language,
// right-to-left ones (Persian, Arabic, Hebrew...) included - their texts read from right to left inside each label.
// Languages there are, "en" first. languageName gives a language's name in that language ("English", "فارسی").
FG_EDITOR_EXPORT QStringList availableLanguages();
FG_EDITOR_EXPORT QString languageName(const QString& code);
// Installs the translations of `code` ("system": the system's language when there is a translation of it, else English); the layout
// stays left to right. Call it before the windows are created: the texts are taken when the widgets are made.
FG_EDITOR_EXPORT void applyLanguage(QApplication& app, const QString& code);
// The language chosen in the editor of the application `applicationName` (View > Language, kept per application), "system" when none
// was chosen; applySavedLanguage = applyLanguage(app, savedLanguage(applicationName)), at the start of the application.
FG_EDITOR_EXPORT QString savedLanguage(const QString& applicationName);
FG_EDITOR_EXPORT void saveLanguage(const QString& applicationName, const QString& code);
FG_EDITOR_EXPORT void applySavedLanguage(QApplication& app, const QString& applicationName);
// The language whose translations are installed ("en" when none is).
FG_EDITOR_EXPORT QString currentLanguage();
// A Language menu - System and every language there is, the chosen one checked - for the windows of an application: the editor's
// View > Language is one (EditorHost::offersLanguageChoice). Choosing a language saves it for `applicationName` (savedLanguage) and
// says it shows at the next start. The menu belongs to `parent`.
FG_EDITOR_EXPORT QMenu* createLanguageMenu(const QString& applicationName, QWidget* parent);
// Adds a catalog that translates the texts of nodes (fg::Translations, Translations.hpp): the built-in nodes' is loaded by
// applyLanguage; an application adds the one for its own nodes and classes, after applyLanguage and before the windows are made (a
// file `flow --translations` wrote, or one in the application's resources). False, and `error`, when it cannot be read. A plugin's
// catalog needs no call: the editor loads it for every plugin of the runtime (translations/<Plugin>_<code>.json next to its library).
FG_EDITOR_EXPORT bool loadNodeTranslations(const QString& catalogFile, QString* error = nullptr);
// An icon of the editor's resources (":/Icons/save.svg"...) drawn in the colours of the theme in use: on a light background its
// neutral greys get darker, its colours stay. It follows theme changes by itself. Applications that use the editor's icons for
// their own buttons take them from here.
FG_EDITOR_EXPORT QIcon themedIcon(const QString& resourcePath);
// Initialises KDDockWidgets with the editor's view factory. Call once after creating the QApplication.
FG_EDITOR_EXPORT void initEditor();

} // namespace fged
