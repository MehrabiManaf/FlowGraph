#pragma once
#include "../Document.hpp"
#include "RunController.hpp"

#include <FlowGraphEditor/ScriptEditor.hpp>

#include <kddockwidgets/MainWindow.h>

#include <QActionGroup>
#include <map>

class CDockWidget;
class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QToolBar;
class QTabWidget;

namespace fged {

class GraphScene;
class GraphView;
class MyScriptPanel;
class DetailsPanel;
class DiagnosticsPanel;
class LogPanel;
class DebugPanel;

// The FlowGraph Studio main window: docked panels around a tab widget of graph editors.
class ScriptEditorWindow : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT
public:
    ScriptEditorWindow(EditorHost& host, ScriptEditor& facade, QWidget* parent = nullptr);
    ~ScriptEditorWindow() override;

    bool openFile(const QString& path);
    bool editScript(const fg::Script& script, const QString& title);     // an embedded editor: the application's script
    bool attachExecutor(std::shared_ptr<fg::AsyncExecutor> executor);
    void detachExecutor() { run_.detach(); }
    bool isAttached() const { return run_.isAttached(); }
    bool embedded() const { return embedded_; }
    void newScript();
    bool fileSave();
    bool fileSaveAs();
    bool maybeSave();
    QMenu* menuNamed(const QString& title);
    QToolBar* mainToolBar() const { return toolBar_; }
    void addToLaunchSelector(QAction* action);
    void refreshEntries();
    void log(int level, const QString& text);
    Document* document() { return &doc_; }
    RunController& run() { return run_; }
    // Selects `entry` in the toolbar and starts it (as the Run button would, but without the argument dialog).
    void runEntry(const QString& entry);
    // Developer aid: "node:<graph>:<id>", "var:<name>" or "graph:<name>" selects that item and shows its details.
    void devSelect(const QString& spec);
    // Developer aid for real-input smoke tests: writes global screen coordinates / script state to a text file.
    void devProbe(const QString& file, bool coordinates);
    // Developer aid: renders the current graph at 2x into a PNG.
    void devSceneShot(const QString& file);
    // Puts the docks, tabs, floating windows and the window itself back where the user left them the last time
    // (QSettings), and saves them again when the window is closed. Called by ScriptEditor::show().
    void restoreSavedLayout();

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void createDocks();
    void applyInitialSizes();
    void createActions();
    void createMenus();
    void createToolBar();

    GraphView* openGraph(const QString& name);
    GraphView* viewAt(int index) const;
    GraphView* currentView() const;
    GraphScene* currentScene() const;
    void closeTab(int index);
    void syncScenes();
    void focusNode(const QString& graph, int node);
    void onSelectionNodes(GraphScene* scene, const QList<int>& ids);
    void onCurrentTabChanged();
    void clearDebugMarks();

    bool embeddedSave();                // Save of an embedded editor: the script goes back to the application
    // Embedded: may this script replace the one in the editor? (the application provides what it uses, and the host agrees)
    bool acceptable(const fg::Script& script, QString* reason) const;
    void showRejected(const QString& what, const QString& reason);
    void attachToApplication();         // the Attach button
    void fileNew();
    void fileOpen();
    void updateTitle();
    void updateEditActions();
    void updateRunActions();
    void runOrContinue();

    EditorHost& host_;
    const bool embedded_;               // host_.mode() == EditorMode::Embedded
    ScriptEditor& facade_;
    fg::Runtime& rt_;
    std::vector<LaunchEntry> entries_;
    QToolBar* toolBar_ = nullptr;
    bool runPending_ = false;           // EditorHost::runFinished() is still owed for the current run
    Document doc_;
    RunController run_;

    QTabWidget* tabs_ = nullptr;
    std::map<GraphView*, GraphScene*> views_;
    MyScriptPanel* myScript_ = nullptr;
    DetailsPanel* details_ = nullptr;
    DiagnosticsPanel* diagnostics_ = nullptr;
    LogPanel* log_ = nullptr;
    DebugPanel* debug_ = nullptr;
    std::vector<CDockWidget*> docks_;
    CDockWidget* debugDock_ = nullptr;
    CDockWidget* diagDock_ = nullptr;
    CDockWidget* logDock_ = nullptr;
    CDockWidget* dMy_ = nullptr;
    CDockWidget* dDetails_ = nullptr;
    bool sizesApplied_ = false;
    bool persistLayout_ = false;

    bool runShown_ = false;
    QComboBox* entryCombo_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QAction *actNew_, *actOpen_, *actSave_, *actSaveAs_, *actExit_;
    QAction *actUndo_, *actRedo_, *actCut_, *actCopy_, *actPaste_, *actDuplicate_, *actDelete_, *actSelectAll_, *actComment_, *actFrame_;
    QAction *actRun_, *actStop_, *actStepOver_, *actStepInto_, *actStepOut_, *actBreakpoint_;
    QAction *actAttach_ = nullptr, *actDetach_ = nullptr;     // embedded mode: the debugger attaches to what the application runs
    QAction* actAbout_;
    QAction* actSnap_ = nullptr;
    QActionGroup* speedGroup_ = nullptr;
    QActionGroup* snapSizeGroup_ = nullptr;
    bool snapToGrid_ = true;            // View > Snap to Grid, remembered between sessions
    double snapStep_ = 20.0;
    void applySnap();
};

} // namespace fged
