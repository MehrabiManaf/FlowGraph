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
    void newScript();
    bool fileSave();
    bool fileSaveAs();
    bool maybeSave();
    QMenu* menuNamed(const QString& title);
    QToolBar* mainToolBar() const { return toolBar_; }
    void refreshEntries();
    void log(int level, const QString& text);
    Document* document() { return &doc_; }
    // Selects `entry` in the toolbar and starts it (as the Run button would, but without the argument dialog).
    void runEntry(const QString& entry);
    // Developer aid: "node:<graph>:<id>", "var:<name>" or "graph:<name>" selects that item and shows its details.
    void devSelect(const QString& spec);
    // Developer aid for real-input smoke tests: writes global screen coordinates / script state to a text file.
    void devProbe(const QString& file, bool coordinates);
    // Developer aid: renders the current graph at 2x into a PNG.
    void devSceneShot(const QString& file);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createDocks();
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

    void fileNew();
    void fileOpen();
    void updateTitle();
    void updateEditActions();
    void updateRunActions();
    void runOrContinue();

    EditorHost& host_;
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

    bool runShown_ = false;
    QComboBox* entryCombo_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QAction *actNew_, *actOpen_, *actSave_, *actSaveAs_, *actExit_;
    QAction *actUndo_, *actRedo_, *actCut_, *actCopy_, *actPaste_, *actDuplicate_, *actDelete_, *actSelectAll_, *actComment_, *actFrame_;
    QAction *actRun_, *actStop_, *actPause_, *actStepOver_, *actStepInto_, *actStepOut_, *actBreakpoint_;
    QAction* actAbout_;
    QActionGroup* speedGroup_ = nullptr;
};

} // namespace fged
