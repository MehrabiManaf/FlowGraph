#pragma once
#include "../Document.hpp"
#include "RunController.hpp"

#include <kddockwidgets/MainWindow.h>

#include <QActionGroup>
#include <map>

class CDockWidget;
class QAction;
class QComboBox;
class QLabel;
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
class StudioWindow : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT
public:
    explicit StudioWindow(fg::Runtime& rt, QWidget* parent = nullptr);
    ~StudioWindow() override;

    bool openFile(const QString& path);
    Document* document() { return &doc_; }
    // Selects `entry` in the toolbar and starts it (as the Run button would, but without the argument dialog).
    void runEntry(const QString& entry);
    // Developer aid: "node:<graph>:<id>", "var:<name>" or "graph:<name>" selects that item and shows its details.
    void devSelect(const QString& spec);
    // Developer aid for real-input smoke tests: writes global screen coordinates / script state to a text file.
    void devProbe(const QString& file, bool coordinates);

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

    bool maybeSave();
    void fileNew();
    void fileOpen();
    bool fileSave();
    bool fileSaveAs();
    void updateTitle();
    void updateEditActions();
    void updateRunActions();
    void refreshEntries();
    void runOrContinue();
    void log(int level, const QString& text);

    fg::Runtime& rt_;
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
