#include "ScriptEditorWindow.hpp"
#include "../Common/CustomViewFactory/DockWidget.hpp"
#include "../Common/CustomViewFactory/TabBar.hpp"
#include "../Graph/GraphScene.hpp"
#include "../Graph/GraphView.hpp"
#include "../Graph/Style.hpp"
#include "../Panels/DetailsPanel.hpp"
#include "../Panels/InfoPanels.hpp"
#include "../Panels/MyScriptPanel.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QFileDialog>
#include <QTextStream>
#include <iostream>
#include "../Graph/NodeItem.hpp"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <set>
#include "../Common/Settings.hpp"
#include <QSettings>
#include <QToolBar>
#include <QToolButton>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockWidget.h>

namespace fged {

namespace {
const char* kFilter = "FlowGraph scripts (*.flow);;All files (*)";
}

namespace {
// The name of a main window is part of the saved layout, so the first window is always "ScriptEditor0": a number
// is handed out again once its window is gone.
std::set<int> g_windowIds;
QString takeWindowName() {
    int id = 0;
    while (g_windowIds.count(id)) ++id;
    g_windowIds.insert(id);
    return QString("ScriptEditor%1").arg(id);
}
}

ScriptEditorWindow::ScriptEditorWindow(EditorHost& host, ScriptEditor& facade, QWidget* parent)
    : KDDockWidgets::QtWidgets::MainWindow(takeWindowName(), KDDockWidgets::MainWindowOption_HasCentralWidget, parent),
      host_(host), embedded_(host.mode() == EditorMode::Embedded), facade_(facade), rt_(host.runtime()), doc_(host.runtime()), run_(&doc_) {
    setMinimumSize(QSize(900, 520));
    resize(QSize(1500, 900));
    setAffinities(QStringList{windowTitle().isEmpty() ? QString("ScriptEditor") : QString("ScriptEditor")});

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    setPersistentCentralWidget(tabs_);

    createDocks();
    createActions();
    createMenus();
    createToolBar();

    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_);

    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { onCurrentTabChanged(); });
    connect(&doc_, &Document::changed, this, [this] { syncScenes(); updateEditActions(); updateTitle(); });
    connect(&doc_, &Document::scriptReplaced, this, [this] { syncScenes(); });
    connect(&doc_, &Document::compiled, this, [this] { refreshEntries(); });
    connect(&doc_, &Document::titleChanged, this, [this] { updateTitle(); });
    connect(&doc_, &Document::undoStateChanged, this, [this] { updateEditActions(); });

    connect(&run_, &RunController::stateChanged, this, [this] { updateRunActions(); });
    connect(&run_, &RunController::stateChanged, this, [this](RunController::State s) {
        if (s == RunController::State::Running && logDock_ && !runShown_) { logDock_->raise(); runShown_ = true; }
        if (s == RunController::State::Idle) {
            runShown_ = false;
            if (runPending_) { runPending_ = false; host_.runFinished(); }
        }
    });
    connect(&run_, &RunController::logMessage, this, [this](int l, const QString& t) { log(l, t); });
    connect(&run_, &RunController::attachedChanged, this, [this](bool on) {
        updateRunActions();
        emit facade_.attachedChanged(on);
    });
    connect(&run_, &RunController::linkFired, this, [this](const QString& g, int from, int out, int to) {
        for (auto& [v, s] : views_)
            if (s->graphName() == g) s->fireLink(from, out, to);
    });
    connect(&run_, &RunController::paused, this, [this](const QString& graph, int node) {
        if (debugDock_) { debugDock_->open(); debugDock_->raise(); }
        clearDebugMarks();
        if (GraphView* v = openGraph(graph)) {
            v->graphScene()->setDebugNode(node);
            v->centerOnNode(node, true);
        }
        statusBar()->showMessage("Paused at '" + graph + "'", 5000);
    });
    connect(&run_, &RunController::resumed, this, [this] { clearDebugMarks(); });
    connect(&run_, &RunController::finished, this, [this] { clearDebugMarks(); });
    connect(&run_, &RunController::stateChanged, this, [this](RunController::State s) { if (s == RunController::State::Idle) clearDebugMarks(); });

    doc_.newScript([this](fg::Script& s) { host_.initializeNewScript(s); });
    openGraph(qs(doc_.script().graphs.front().name));
    updateTitle();
    updateEditActions();
    updateRunActions();
    refreshEntries();
}

ScriptEditorWindow::~ScriptEditorWindow() {
    g_windowIds.erase(uniqueName().mid(QString("ScriptEditor").size()).toInt());
    run_.stop();
    // the scenes and views are children of the tab widget / owned here
    for (auto& [v, s] : views_) {
        delete v;
        delete s;
    }
    views_.clear();
    for (auto* dw : docks_) delete dw;
}

// ------------------------------------------------------------------ construction
void ScriptEditorWindow::createDocks() {
    KDDockWidgets::DockWidgetOptions opts = KDDockWidgets::DockWidgetOption_None;
    KDDockWidgets::LayoutSaverOptions lopts = KDDockWidgets::LayoutSaverOption::None;
    auto makeDock = [&](const QString& title, QWidget* w, bool scroll) {
        auto* dock = new CDockWidget(title, opts, lopts);
        dock->setAffinities(affinities());
        if (scroll) {
            auto* sa = new QScrollArea(this);
            sa->setWidgetResizable(true);
            sa->setWidget(w);
            dock->setWidget(sa);
        } else {
            dock->setWidget(w);
        }
        docks_.push_back(dock);
        return dock;
    };

    myScript_ = new MyScriptPanel(&doc_);
    details_ = new DetailsPanel(&doc_);
    diagnostics_ = new DiagnosticsPanel(&doc_);
    log_ = new LogPanel();
    debug_ = new DebugPanel(&doc_, &run_);

    dMy_ = makeDock("My Script", myScript_, true);
    dDetails_ = makeDock("Details", details_, true);
    diagDock_ = makeDock("Compile Results", diagnostics_, false);
    auto* dLog = makeDock("Output Log", log_, false);
    logDock_ = dLog;
    debugDock_ = makeDock("Debugger", debug_, false);

    addDockWidget(dMy_, KDDockWidgets::Location_OnLeft, nullptr, KDDockWidgets::InitialOption(QSize(280, 400)));
    addDockWidget(dDetails_, KDDockWidgets::Location_OnRight, nullptr, KDDockWidgets::InitialOption(QSize(340, 400)));
    addDockWidget(dLog, KDDockWidgets::Location_OnBottom, nullptr, KDDockWidgets::InitialOption(QSize(600, 180)));
    dLog->addDockWidgetAsTab(diagDock_);
    dDetails_->addDockWidgetAsTab(debugDock_);       // the debugger shares the right-hand column with the details
    dDetails_->setAsCurrentTab();


    connect(myScript_, &MyScriptPanel::openGraph, this, [this](const QString& n) { openGraph(n); });
    connect(myScript_, &MyScriptPanel::eventActivated, this, [this](const QString& g, int n) { focusNode(g, n); });
    connect(myScript_, &MyScriptPanel::eventSelected, this, [this](const QString& g, int n) {
        if (GraphView* v = openGraph(g)) v->graphScene()->selectOnly(n);        // shows the event details
    });
    connect(myScript_, &MyScriptPanel::selectionChanged, this, [this](int kind, const QString& owner, const QString& name) {
        using K = MyScriptPanel::Kind;
        switch (K(kind)) {
        case K::Variable: details_->showVariable("", name); break;
        case K::Local: details_->showVariable(owner, name); break;
        case K::Function: details_->showGraph(name); break;
        default: details_->showNothing(); break;
        }
    });
    connect(diagnostics_, &DiagnosticsPanel::activated, this, [this](const QString& g, int n) { focusNode(g, n); });
    connect(diagnostics_, &DiagnosticsPanel::summaryChanged, this, [this](int e, int w) {
        statusLabel_->setText(e || w ? QString("%1 error(s), %2 warning(s)").arg(e).arg(w) : QString("OK"));
        statusLabel_->setStyleSheet(e ? "color:#ff7b7b" : w ? "color:#e6be3c" : "color:#7bd88f");
    });
    connect(debug_, &DebugPanel::locationActivated, this, [this](const QString& g, int n) { focusNode(g, n); });
}

void ScriptEditorWindow::createActions() {
    auto act = [this](const QString& text, const QKeySequence& key = QKeySequence()) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        a->setShortcutContext(Qt::WindowShortcut);
        return a;
    };
    actNew_ = act("&New", QKeySequence::New);
    actOpen_ = act("&Open...", QKeySequence::Open);
    actSave_ = act("&Save", QKeySequence::Save);
    actNew_->setIcon(QIcon(":/Icons/new.svg"));
    actOpen_->setIcon(QIcon(":/Icons/open.svg"));
    actSave_->setIcon(QIcon(":/Icons/save.svg"));
    actSaveAs_ = act("Save &As...", QKeySequence("Ctrl+Shift+S"));
    actExit_ = act("E&xit");
    actUndo_ = act("&Undo", QKeySequence::Undo);
    actRedo_ = act("&Redo", QKeySequence("Ctrl+Y"));
    actCut_ = act("Cu&t\tCtrl+X");
    actCopy_ = act("&Copy\tCtrl+C");
    actPaste_ = act("&Paste\tCtrl+V");
    actDuplicate_ = act("&Duplicate\tCtrl+D");
    actDelete_ = act("De&lete\tDel");
    actSelectAll_ = act("Select &All\tCtrl+A");
    actComment_ = act("Add Co&mment\tC");
    actSaveAs_->setIcon(QIcon(":/Icons/save-as.svg"));
    actExit_->setIcon(QIcon(":/Icons/exit.svg"));
    actUndo_->setIcon(QIcon(":/Icons/undo.svg"));
    actRedo_->setIcon(QIcon(":/Icons/redo.svg"));
    actCut_->setIcon(QIcon(":/Icons/cut.svg"));
    actCopy_->setIcon(QIcon(":/Icons/copy.svg"));
    actPaste_->setIcon(QIcon(":/Icons/paste.svg"));
    actDuplicate_->setIcon(QIcon(":/Icons/duplicate.svg"));
    actDelete_->setIcon(QIcon(":/Icons/delete.svg"));
    actSelectAll_->setIcon(QIcon(":/Icons/select-all.svg"));
    actComment_->setIcon(QIcon(":/Icons/comment.svg"));
    actFrame_ = act("&Frame All\tHome");
    {
        QSettings settings = editorSettings();
        snapToGrid_ = settings.value("editor/snapToGrid", true).toBool();
        snapStep_ = settings.value("editor/snapStep", 20.0).toDouble();
        if (snapStep_ != 10.0 && snapStep_ != 20.0 && snapStep_ != 40.0) snapStep_ = 20.0;
    }
    actSnap_ = act("Snap to &Grid");
    actSnap_->setCheckable(true);
    actSnap_->setChecked(snapToGrid_);
    actSnap_->setToolTip("Nodes you move or place land on the grid. Hold Alt while dragging to move freely.");
    // One button: Run when nothing runs, Pause while the script runs, Continue while it is paused (see updateRunActions)
    actRun_ = new QAction(QIcon(":/Icons/continue.svg"), "Run", this);
    actRun_->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(Qt::Key_F6)});
    actStop_ = new QAction(QIcon(":/Icons/stop.svg"), "Stop", this);
    actStop_->setShortcut(QKeySequence("Shift+F5"));
    actStepOver_ = new QAction(QIcon(":/Icons/step-over.svg"), "Step Over", this);
    actStepOver_->setShortcut(QKeySequence(Qt::Key_F10));
    actStepInto_ = new QAction(QIcon(":/Icons/step-into.svg"), "Step Into", this);
    actStepInto_->setShortcut(QKeySequence(Qt::Key_F11));
    actStepOut_ = new QAction(QIcon(":/Icons/step-out.svg"), "Step Out", this);
    actStepOut_->setShortcut(QKeySequence("Shift+F11"));
    actBreakpoint_ = act("Toggle &Breakpoint", QKeySequence(Qt::Key_F9));
    if (embedded_) {
        // no Run: the application runs the script, the debugger attaches to it
        actAttach_ = new QAction(QIcon(":/Icons/attach.svg"), "&Attach", this);
        actAttach_->setToolTip("Debug the script that the application is running now: breakpoints stop it, and you can step through it");
        actDetach_ = new QAction(QIcon(":/Icons/detach.svg"), "&Detach", this);
        actDetach_->setToolTip("Stop debugging: whatever is stopped continues");
        actRun_->setText("Continue");
    }
    actAbout_ = act("&About " + host_.applicationName());

    connect(actNew_, &QAction::triggered, this, [this] { fileNew(); });
    connect(actOpen_, &QAction::triggered, this, [this] { fileOpen(); });
    connect(actSave_, &QAction::triggered, this, [this] { fileSave(); });
    connect(actSaveAs_, &QAction::triggered, this, [this] { fileSaveAs(); });
    connect(actExit_, &QAction::triggered, this, [this] { close(); });
    connect(actUndo_, &QAction::triggered, this, [this] { doc_.undo(); });
    connect(actRedo_, &QAction::triggered, this, [this] { doc_.redo(); });
    connect(actCut_, &QAction::triggered, this, [this] { if (auto* s = currentScene()) { s->copySelected(); s->deleteSelected(); } });
    connect(actCopy_, &QAction::triggered, this, [this] { if (auto* s = currentScene()) s->copySelected(); });
    connect(actPaste_, &QAction::triggered, this, [this] {
        if (auto* v = currentView()) v->graphScene()->paste(v->mapToScene(v->viewport()->rect().center()));
    });
    connect(actDuplicate_, &QAction::triggered, this, [this] { if (auto* s = currentScene()) s->duplicateSelected(); });
    connect(actDelete_, &QAction::triggered, this, [this] { if (auto* s = currentScene()) s->deleteSelected(); });
    connect(actSelectAll_, &QAction::triggered, this, [this] {
        if (auto* s = currentScene()) { QPainterPath p; p.addRect(s->contentRect()); s->setSelectionArea(p); }
    });
    connect(actComment_, &QAction::triggered, this, [this] {
        if (auto* v = currentView()) v->graphScene()->addComment(v->mapToScene(v->viewport()->rect().center()));
    });
    connect(actFrame_, &QAction::triggered, this, [this] { if (auto* v = currentView()) v->frameAll(); });
    connect(actSnap_, &QAction::toggled, this, [this](bool on) {
        snapToGrid_ = on;
        editorSettings().setValue("editor/snapToGrid", on);
        applySnap();
    });
    connect(actRun_, &QAction::triggered, this, [this] {
        const auto st = run_.state();
        if (st == RunController::State::Running || st == RunController::State::Waiting) run_.pause();
        else runOrContinue();
    });
    connect(actStop_, &QAction::triggered, this, [this] { run_.stop(); });
    if (embedded_) {
        connect(actAttach_, &QAction::triggered, this, [this] { attachToApplication(); });
        connect(actDetach_, &QAction::triggered, this, [this] { run_.detach(); });
    }
    connect(actStepOver_, &QAction::triggered, this, [this] { run_.stepOver(); });
    connect(actStepInto_, &QAction::triggered, this, [this] { run_.stepInto(); });
    connect(actStepOut_, &QAction::triggered, this, [this] { run_.stepOut(); });
    connect(actBreakpoint_, &QAction::triggered, this, [this] {
        if (auto* s = currentScene()) for (auto id : s->selectedNodeIds()) s->toggleBreakpoint(id);
    });
    connect(actAbout_, &QAction::triggered, this, [this] {
        QMessageBox::about(this, host_.applicationName(),
                           "<b>" + host_.applicationName().toHtmlEscaped() + "</b><br>Visual scripting editor built on FlowGraph.<br><br>"
                           "Right-click or drag from a pin to add nodes, F5 to run, F9 for breakpoints.");
    });
}

void ScriptEditorWindow::createMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    if (embedded_) {
        // the application owns the script: no New / Exit; Open takes only scripts that fit the application; Save gives the script back,
        // Save As writes a copy to a file
        file->addActions({actOpen_, actSave_, actSaveAs_});
    } else {
        file->addActions({actNew_, actOpen_, actSave_, actSaveAs_});
        file->addSeparator();
        file->addAction(actExit_);
    }

    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addActions({actUndo_, actRedo_});
    edit->addSeparator();
    edit->addActions({actCut_, actCopy_, actPaste_, actDuplicate_, actDelete_});
    edit->addSeparator();
    edit->addActions({actSelectAll_, actComment_});

    QMenu* view = menuBar()->addMenu("&View");
    view->addAction(actFrame_);
    view->addSeparator();
    view->addAction(actSnap_);
    QMenu* gridSize = view->addMenu("Grid Snap Si&ze");
    snapSizeGroup_ = new QActionGroup(this);
    for (double step : {10.0, 20.0, 40.0}) {
        QAction* a = gridSize->addAction(QString::number(int(step)) + " units");
        a->setCheckable(true);
        a->setChecked(step == snapStep_);
        a->setData(step);
        snapSizeGroup_->addAction(a);
        connect(a, &QAction::triggered, this, [this, step] {
            snapStep_ = step;
            editorSettings().setValue("editor/snapStep", step);
            applySnap();
        });
    }
    view->addSeparator();
    for (auto* dw : docks_) view->addAction(dw->toggleAction());

    if (embedded_) {
        QMenu* debug = menuBar()->addMenu("&Debug");
        debug->addActions({actAttach_, actDetach_});
        debug->addSeparator();
        debug->addAction(actRun_);
        debug->addSeparator();
        debug->addActions({actStepOver_, actStepInto_, actStepOut_});
        debug->addSeparator();
        debug->addAction(actBreakpoint_);
    } else {
        QMenu* runMenu = menuBar()->addMenu("&Run");
        runMenu->addActions({actRun_, actStop_});
        runMenu->addSeparator();
        runMenu->addActions({actStepOver_, actStepInto_, actStepOut_});
        runMenu->addSeparator();
        runMenu->addAction(actBreakpoint_);
        QMenu* speed = runMenu->addMenu("Execution speed");
        speedGroup_ = new QActionGroup(this);
        for (auto [label, ms] : {std::pair<const char*, int>{"Full speed", 0}, {"Slow (100 ms / node)", 100}, {"Slower (300 ms / node)", 300}, {"Step by step (1 s / node)", 1000}}) {
            QAction* a = speed->addAction(label);
            a->setCheckable(true);
            a->setData(ms);
            a->setChecked(ms == 0);
            speedGroup_->addAction(a);
            connect(a, &QAction::triggered, this, [this, ms] { run_.setStepDelay(ms); });
        }
    }

    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction(actAbout_);
}

void ScriptEditorWindow::createToolBar() {
    auto* tb = addToolBar("Main");
    toolBar_ = tb;
    tb->setObjectName("MainToolBar");
    tb->setIconSize(QSize(22, 22));
    if (embedded_) {
        tb->addActions({actOpen_, actSave_});
        tb->addSeparator();
        tb->addActions({actAttach_, actDetach_});
        tb->addSeparator();
        tb->addAction(actRun_);
        tb->addSeparator();
        tb->addActions({actStepOver_, actStepInto_, actStepOut_});
        entryCombo_ = new QComboBox(this);          // the application chooses what runs: the box is not shown, but the editor keeps its list of entries
        entryCombo_->hide();
        return;
    }
    tb->addActions({actNew_, actOpen_, actSave_});
    tb->addSeparator();
    entryCombo_ = new QComboBox(tb);
    entryCombo_->setMinimumWidth(200);
    // The label that used to stand in front of the box is the title of its tooltip.
    QString launchTitle = host_.launchLabel().trimmed();
    if (launchTitle.endsWith(':')) launchTitle.chop(1);
    entryCombo_->setToolTip(launchTitle.isEmpty() ? host_.launchToolTip() : "<b>" + launchTitle.toHtmlEscaped() + "</b><br>" + host_.launchToolTip().toHtmlEscaped());
    tb->addWidget(entryCombo_);
    connect(entryCombo_, qOverload<int>(&QComboBox::activated), this, [this](int) { host_.launchEntrySelected(entryCombo_->currentData().toString()); });
    tb->addActions({actRun_, actStop_});
    tb->addSeparator();
    tb->addActions({actStepOver_, actStepInto_, actStepOut_});
}

QMenu* ScriptEditorWindow::menuNamed(const QString& title) {
    QString wanted = QString(title).remove('&');
    for (QAction* a : menuBar()->actions())
        if (a->menu() && QString(a->menu()->title()).remove('&') == wanted) return a->menu();
    return menuBar()->addMenu(title);
}

void ScriptEditorWindow::addToLaunchSelector(QAction* action) {
    toolBar_->insertAction(actRun_, action);        // between the entries box and the Run button
}

// ------------------------------------------------------------------ graph tabs
GraphView* ScriptEditorWindow::viewAt(int index) const { return index < 0 ? nullptr : qobject_cast<GraphView*>(tabs_->widget(index)); }
GraphView* ScriptEditorWindow::currentView() const { return viewAt(tabs_->currentIndex()); }
GraphScene* ScriptEditorWindow::currentScene() const {
    auto* v = currentView();
    return v ? v->graphScene() : nullptr;
}

void ScriptEditorWindow::applySnap() {
    for (auto& [v, s] : views_) {
        s->setSnapToGrid(snapToGrid_);
        s->setSnapStep(snapStep_);
    }
}

GraphView* ScriptEditorWindow::openGraph(const QString& name) {
    for (auto& [v, s] : views_)
        if (s->graphName() == name) {
            tabs_->setCurrentWidget(v);
            return v;
        }
    if (!doc_.graph(name)) return nullptr;
    auto* scene = new GraphScene(&doc_, name);
    auto* view = new GraphView(scene);
    views_[view] = scene;
    scene->setSnapToGrid(snapToGrid_);
    scene->setSnapStep(snapStep_);
    scene->pinValueProvider = [this, scene](const PinRef& ref, QString& out) {
        return run_.pinText(scene->graphName(), ref.node, ref.pin, out);
    };
    connect(scene, &GraphScene::selectionNodesChanged, this, [this, scene](const QList<int>& ids) { onSelectionNodes(scene, ids); });
    connect(scene, &GraphScene::message, this, [this](const QString& m) { statusBar()->showMessage(m, 4000); });
    connect(scene, &GraphScene::nodeDoubleClicked, this, [this, scene](int id) {
        const fg::Graph* g = doc_.graph(scene->graphName());
        const fg::Node* n = g ? g->findNode(id) : nullptr;
        if (!n) return;
        if (n->def == "func.call") openGraph(qs(n->prop("function")));
    });
    scene->sync();
    bool isFn = doc_.graph(name)->kind == fg::GraphKind::Function;
    int idx = tabs_->addTab(view, (isFn ? QString::fromUtf8("ƒ ") : QString()) + name);
    tabs_->setCurrentIndex(idx);
    QTimer::singleShot(0, view, [view] { view->frameAll(); });
    return view;
}

void ScriptEditorWindow::closeTab(int index) {
    GraphView* v = viewAt(index);
    if (!v) return;
    GraphScene* s = views_[v];
    tabs_->removeTab(index);
    views_.erase(v);
    delete v;
    delete s;
}

void ScriptEditorWindow::syncScenes() {
    // detect a renamed graph: exactly one open scene lost its graph and exactly one graph is not open
    std::vector<GraphView*> missing;
    std::set<QString> open;
    for (auto& [v, s] : views_) {
        if (doc_.graph(s->graphName())) open.insert(s->graphName());
        else missing.push_back(v);
    }
    std::vector<QString> unopened;
    for (auto& g : doc_.script().graphs)
        if (!open.count(qs(g.name))) unopened.push_back(qs(g.name));
    if (missing.size() == 1 && unopened.size() == 1 && views_[missing[0]]) {
        GraphScene* s = views_[missing[0]];
        QString nn = unopened[0];
        const fg::Graph* ng = doc_.graph(nn);
        if (ng) {
            s->setGraphName(nn);
            int idx = tabs_->indexOf(missing[0]);
            tabs_->setTabText(idx, (ng->kind == fg::GraphKind::Function ? QString::fromUtf8("ƒ ") : QString()) + nn);
            missing.clear();
        }
    }
    for (GraphView* v : missing) closeTab(tabs_->indexOf(v));
    for (auto& [v, s] : views_) s->sync();
    if (tabs_->count() == 0 && !doc_.script().graphs.empty()) {
        const fg::Graph* first = &doc_.script().graphs.front();
        for (auto& g : doc_.script().graphs)
            if (g.kind == fg::GraphKind::Event) { first = &g; break; }
        openGraph(qs(first->name));
    }
    if (auto* s = currentScene()) myScript_->setCurrentGraph(s->graphName());
}

void ScriptEditorWindow::focusNode(const QString& graph, int node) {
    GraphView* v = openGraph(graph);
    if (!v) return;
    v->graphScene()->selectOnly(node);
    v->centerOnNode(node, true);
    v->setFocus();
}

void ScriptEditorWindow::onSelectionNodes(GraphScene* scene, const QList<int>& ids) {
    if (scene != currentScene()) return;
    details_->showNodes(scene->graphName(), ids);
}

void ScriptEditorWindow::onCurrentTabChanged() {
    GraphScene* s = currentScene();
    if (!s) return;
    myScript_->setCurrentGraph(s->graphName());
    QList<int> ids;
    for (auto id : s->selectedNodeIds()) ids.push_back(id);
    if (!ids.isEmpty()) details_->showNodes(s->graphName(), ids);
}

void ScriptEditorWindow::clearDebugMarks() {
    for (auto& [v, s] : views_) s->setDebugNode(0);
}

// ------------------------------------------------------------------ files
bool ScriptEditorWindow::maybeSave() {
    if (!doc_.isDirty()) return true;
    auto r = QMessageBox::question(this, host_.applicationName(), "Save changes to '" + doc_.displayName().remove('*') + "'?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return fileSave();
    return true;
}

void ScriptEditorWindow::fileNew() {
    if (embedded_) return;                              // the application hands the scripts over
    if (!maybeSave()) return;
    newScript();
}

void ScriptEditorWindow::newScript() {
    run_.stop();
    while (tabs_->count()) closeTab(0);
    doc_.newScript([this](fg::Script& s) { host_.initializeNewScript(s); });
    host_.scriptChanged(facade_, ScriptChange::Created);
    emit facade_.scriptChanged();
}

void ScriptEditorWindow::fileOpen() {
    if (embedded_) {
        // a script that does not fit is refused before anything is asked or changed
        QString path = QFileDialog::getOpenFileName(this, "Open script", QString(), kFilter, nullptr, QFileDialog::DontUseNativeDialog);
        if (path.isEmpty()) return;
        fg::Script script;
        QString reason;
        try {
            script = fg::Script::load(path.toStdString());
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Open script", "Cannot open '" + path + "':\n" + e.what());
            return;
        }
        if (!acceptable(script, &reason)) {
            showRejected(path, reason);
            return;
        }
        if (!maybeSave()) return;
        openFile(path);
        return;
    }
    if (!maybeSave()) return;
    QString path = QFileDialog::getOpenFileName(this, "Open script", QString(), kFilter, nullptr, QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) openFile(path);
}

bool ScriptEditorWindow::openFile(const QString& path) {
    if (embedded_) {
        // an embedded editor takes only a script that fits the application; otherwise the script it has stays as it is
        fg::Script script;
        try {
            script = fg::Script::load(path.toStdString());
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Open script", "Cannot open '" + path + "':\n" + e.what());
            return false;
        }
        QString reason;
        if (!acceptable(script, &reason)) {
            showRejected(path, reason);
            return false;
        }
    }
    run_.stop();
    QString err;
    while (tabs_->count()) closeTab(0);
    if (!doc_.load(path, &err)) {
        QMessageBox::critical(this, "Open script", "Cannot open '" + path + "':\n" + err);
        doc_.newScript([this](fg::Script& s) { host_.initializeNewScript(s); });
        host_.scriptChanged(facade_, ScriptChange::Created);
        emit facade_.scriptChanged();
        return false;
    }
    log(int(fg::LogLevel::Info), "Opened " + path);
    host_.scriptChanged(facade_, ScriptChange::Opened);   // a host may load plugins / launch files for this script
    auto report = fg::checkCompatibility(doc_.script(), rt_.registry());
    if (!report.ok()) {
        // the editor still opens it so that it can be repaired, but it cannot run here
        QString text = qs(report.toText());
        log(int(fg::LogLevel::Warning), "This script was made for another application:\n" + text.trimmed());
        statusBar()->showMessage("This script needs things " + host_.applicationName() + " does not provide - see the Output Log", 8000);
        auto* box = new QMessageBox(QMessageBox::Warning, host_.applicationName(), "This script cannot run in " + host_.applicationName() + ":\n\n" + text, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();                                    // non modal for the program flow
    }
    emit facade_.scriptChanged();
    return true;
}

// An embedded editor edits the scripts the application hands over, one after the other.
bool ScriptEditorWindow::editScript(const fg::Script& script, const QString& title) {
    if (embedded_) {
        QString reason;
        if (!acceptable(script, &reason)) {
            log(int(fg::LogLevel::Warning), "'" + title + "' was not taken: " + reason.trimmed());
            statusBar()->showMessage("The script '" + title + "' does not fit " + host_.applicationName() + ": see the Output Log", 8000);
            return false;
        }
    }
    if (!maybeSave()) return false;
    if (!embedded_) run_.stop();
    while (tabs_->count()) closeTab(0);
    doc_.setScript(script, title);                         // another script: an attached debugger lets go of the executor of the old one
    auto report = fg::checkCompatibility(doc_.script(), rt_.registry());
    if (!report.ok()) {
        const QString text = qs(report.toText());
        log(int(fg::LogLevel::Warning), "This script needs things " + host_.applicationName() + " does not provide:\n" + text.trimmed());
        statusBar()->showMessage("This script needs things " + host_.applicationName() + " does not provide - see the Output Log", 8000);
    }
    emit facade_.scriptChanged();
    return true;
}

bool ScriptEditorWindow::attachExecutor(std::shared_ptr<fg::AsyncExecutor> executor) {
    const bool ok = run_.attach(std::move(executor));
    if (ok) {
        log(int(fg::LogLevel::Info), "--- attached ---");
        if (logDock_) logDock_->raise();
    }
    return ok;
}

void ScriptEditorWindow::attachToApplication() {
    std::shared_ptr<fg::AsyncExecutor> target = host_.debugTarget(facade_);
    if (!target) {
        statusBar()->showMessage("Nothing is running in " + host_.applicationName() + " to attach to", 5000);
        return;
    }
    attachExecutor(std::move(target));
}

bool ScriptEditorWindow::acceptable(const fg::Script& script, QString* reason) const {
    const auto report = fg::checkCompatibility(script, rt_.registry());
    if (!report.ok()) {
        if (reason) *reason = "This script needs things " + host_.applicationName() + " does not provide:\n" + qs(report.toText());
        return false;
    }
    QString why;
    if (!host_.acceptsScript(script, &why)) {
        if (reason) *reason = why.isEmpty() ? "This script does not fit " + host_.applicationName() + "." : why;
        return false;
    }
    return true;
}

void ScriptEditorWindow::showRejected(const QString& what, const QString& reason) {
    log(int(fg::LogLevel::Warning), "'" + what + "' was not opened: " + reason.trimmed());
    statusBar()->showMessage("That script does not fit " + host_.applicationName() + " - it was not opened", 8000);
    auto* box = new QMessageBox(QMessageBox::Warning, host_.applicationName(),
                                "This script cannot be opened here:\n\n" + reason.trimmed() + "\n\nThe script that was open is unchanged.", QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();                                        // not modal for the flow of the program
}

// Save of an embedded editor: no file. The script goes to the application, which decides what to do with it.
bool ScriptEditorWindow::embeddedSave() {
    if (!host_.applyScript(facade_, doc_.script())) {
        statusBar()->showMessage(host_.applicationName() + " did not take the script: the changes are still unsaved", 6000);
        return false;
    }
    doc_.markSaved();
    statusBar()->showMessage("The script was handed over to " + host_.applicationName(), 3000);
    emit facade_.scriptChanged();
    return true;
}

bool ScriptEditorWindow::fileSave() {
    if (embedded_) return embeddedSave();
    if (doc_.path().isEmpty()) return fileSaveAs();
    QString err;
    if (!doc_.save(QString(), &err)) {
        QMessageBox::critical(this, "Save script", err);
        return false;
    }
    statusBar()->showMessage("Saved " + doc_.path(), 3000);
    host_.scriptChanged(facade_, ScriptChange::Saved);
    emit facade_.scriptChanged();
    return true;
}

bool ScriptEditorWindow::fileSaveAs() {
    if (embedded_) {
        // a copy in a file: the script stays the application's (unsaved changes stay unsaved)
        QString sel = kFilter;
        QString file = QFileDialog::getSaveFileName(this, "Save a copy of the script", doc_.displayName().remove('*'), kFilter, &sel, QFileDialog::DontUseNativeDialog);
        if (file.isEmpty()) return false;
        if (!file.contains('.')) file += ".flow";
        QString err;
        if (!doc_.exportTo(file, &err)) {
            QMessageBox::critical(this, "Save script", err);
            return false;
        }
        statusBar()->showMessage("Saved " + file, 3000);
        return true;
    }
    QString sel = kFilter;
    QString path = QFileDialog::getSaveFileName(this, "Save script", doc_.path(), kFilter, &sel, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return false;
    if (!path.contains('.')) path += ".flow";
    QString err;
    if (!doc_.save(path, &err)) {
        QMessageBox::critical(this, "Save script", err);
        return false;
    }
    statusBar()->showMessage("Saved " + path, 3000);
    host_.scriptChanged(facade_, ScriptChange::Saved);
    emit facade_.scriptChanged();
    return true;
}

// The side panels have no maximum width (they follow their dock when the user widens it). KDDockWidgets gives the
// new docks a share of the window instead of the size we ask for, so the first time the window is shown the
// side docks are brought to a sensible width.
void ScriptEditorWindow::showEvent(QShowEvent* e) {
    KDDockWidgets::QtWidgets::MainWindow::showEvent(e);
    if (sizesApplied_) return;
    sizesApplied_ = true;
    QTimer::singleShot(0, this, [this] { applyInitialSizes(); });
}

void ScriptEditorWindow::applyInitialSizes() {
    auto resize = [](CDockWidget* dock, int wanted, bool growToTheLeft) {
        auto* c = static_cast<KDDockWidgets::Core::DockWidget*>(dock->controller());
        if (!c) return;
        int delta = wanted - c->sizeInLayout().width();
        if (delta == 0) return;
        if (growToTheLeft) c->resizeInLayout(delta, 0, 0, 0);
        else c->resizeInLayout(0, 0, delta, 0);
    };
    resize(dMy_, 300, false);
    resize(dDetails_, 360, true);
}

void ScriptEditorWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) { e->ignore(); return; }
    run_.stop();
    if (persistLayout_) {
        // Before the window closes: afterwards the floating windows and the docks are gone.
        editorSettings().setValue("editor/layout", KDDockWidgets::LayoutSaver().serializeLayout());
    }
    KDDockWidgets::QtWidgets::MainWindow::closeEvent(e);
}

void ScriptEditorWindow::restoreSavedLayout() {
    persistLayout_ = true;
    const QByteArray saved = editorSettings().value("editor/layout").toByteArray();
    if (saved.isEmpty()) return;
    // The saved sizes replace the initial ones. Set before restoring: the saved layout shows the window, and the first
    // showEvent would otherwise schedule applyInitialSizes().
    sizesApplied_ = true;
    if (!KDDockWidgets::LayoutSaver().restoreLayout(saved)) {
        sizesApplied_ = false;
        return;
    }
    // which single tabs the user had collapsed
    for (auto* dock : docks_) dock->LoadTabBarState();
    for (auto* tabBar : QApplication::allWidgets())
        if (auto* ct = dynamic_cast<CTabBar*>(tabBar)) ct->RefreshTabBarVisibility();
}

// ------------------------------------------------------------------ state
void ScriptEditorWindow::updateTitle() { setWindowTitle(doc_.displayName() + " - " + host_.applicationName()); }

void ScriptEditorWindow::updateEditActions() {
    actUndo_->setEnabled(doc_.canUndo());
    actRedo_->setEnabled(doc_.canRedo());
    actUndo_->setText(doc_.canUndo() ? "&Undo " + doc_.undoLabel() : "&Undo");
    actRedo_->setText(doc_.canRedo() ? "&Redo " + doc_.redoLabel() : "&Redo");
}

void ScriptEditorWindow::updateRunActions() {
    using S = RunController::State;
    S s = run_.state();
    const bool busy = s == S::Running || s == S::Waiting;       // the script runs: the button pauses it
    if (embedded_) {
        // the application runs the script: attach / detach, and pause / continue / step while attached
        const bool attached = run_.isAttached();
        actAttach_->setEnabled(!attached);
        actDetach_->setEnabled(attached);
        actRun_->setEnabled(attached);
        actRun_->setText(s == S::Paused ? "Continue" : "Pause");
        actRun_->setIcon(QIcon(s == S::Paused ? ":/Icons/continue.svg" : ":/Icons/interrupt.svg"));
        actStop_->setEnabled(false);
        actStepOver_->setEnabled(s == S::Paused);
        actStepInto_->setEnabled(s == S::Paused);
        actStepOut_->setEnabled(s == S::Paused);
        return;
    }
    actRun_->setText(busy ? "Pause" : s == S::Paused ? "Continue" : "Run");
    actRun_->setIcon(QIcon(busy ? ":/Icons/interrupt.svg" : ":/Icons/continue.svg"));
    actStop_->setEnabled(s != S::Idle);
    actStepOver_->setEnabled(s == S::Paused);
    actStepInto_->setEnabled(s == S::Paused);
    actStepOut_->setEnabled(s == S::Paused);
    entryCombo_->setEnabled(s == S::Idle);
}

void ScriptEditorWindow::refreshEntries() {
    QString keep = entryCombo_->currentData().toString();
    entries_.clear();
    auto program = doc_.program();
    if (program) entries_ = host_.launchEntries(*program);
    entryCombo_->blockSignals(true);
    entryCombo_->clear();
    for (auto& e : entries_) entryCombo_->addItem(e.title, e.id);
    int idx = entryCombo_->findData(keep);
    if (idx < 0) idx = entryCombo_->findData(host_.preferredEntry());
    if (idx >= 0) entryCombo_->setCurrentIndex(idx);
    entryCombo_->blockSignals(false);
}

void ScriptEditorWindow::runOrContinue() {
    if (embedded_) {                                    // the application starts the script: all there is to do is to continue
        if (run_.state() == RunController::State::Paused) run_.resume();
        return;
    }
    if (run_.state() == RunController::State::Paused) { run_.resume(); return; }
    if (run_.state() != RunController::State::Idle) return;
    QString id = entryCombo_->currentData().toString();
    for (auto& e : entries_) {
        if (e.id != id) continue;
        LaunchSpec spec;
        spec.entry = e.entry;
        if (!host_.prepareLaunch(e, spec, this)) return;
        if (spec.stepDelayMs >= 0) run_.setStepDelay(spec.stepDelayMs);
        host_.aboutToRun(spec);
        runPending_ = true;
        if (!run_.start(spec.entry, spec.args) && runPending_) { runPending_ = false; host_.runFinished(); }
        return;
    }
    log(int(fg::LogLevel::Warning), "Nothing to run: add an event to a graph (this application offers: " + QString::number(entries_.size()) + " entries).");
}

void ScriptEditorWindow::runEntry(const QString& entry) {
    int idx = entryCombo_->findData(entry);
    if (idx >= 0) entryCombo_->setCurrentIndex(idx);
    runOrContinue();
}

void ScriptEditorWindow::devSelect(const QString& spec) {
    QStringList p = spec.split(':');
    if (p.size() == 3 && p[0] == "node") focusNode(p[1], p[2].toInt());
    else if (p.size() == 2 && p[0] == "var") details_->showVariable("", p[1]);
    else if (p.size() == 2 && p[0] == "graph") { openGraph(p[1]); details_->showGraph(p[1]); }
}

void ScriptEditorWindow::devProbe(const QString& file, bool coordinates) {
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    GraphView* v = currentView();
    if (!v) { out << "noview\n"; return; }
    GraphScene* sc = v->graphScene();
    auto global = [&](const QPointF& scenePos) { return v->viewport()->mapToGlobal(v->mapFromScene(scenePos)); };
    const fg::Graph* g = doc_.graph(sc->graphName());
    if (coordinates) {
        if (NodeItem* n2 = sc->nodeItem(2)) {
            QPoint h = global(n2->mapToScene(QPointF(20, 8)));
            out << "header " << h.x() << " " << h.y() << "\n";
        }
        if (NodeItem* n3 = sc->nodeItem(3), *n2 = sc->nodeItem(1); n3 && n2) {
            QPoint a = global(n2->pinScenePos("Then", fg::PinDir::Out));
            QPoint b = global(n3->pinScenePos("In", fg::PinDir::In));
            out << "scale " << v->transform().m11() << " scenePinFrom " << n3->pinScenePos("Completed", fg::PinDir::Out).x() << " " << n3->pinScenePos("Completed", fg::PinDir::Out).y() << " dpr " << v->devicePixelRatioF() << Qt::endl;
            out << "from " << a.x() << " " << a.y() << "\n";
            out << "to " << b.x() << " " << b.y() << "\n";
        }
    }
    if (g) {
        if (auto* n = g->findNode(2)) out << "node2 " << n->x << " " << n->y << "\n";
        if (NodeItem* n2 = sc->nodeItem(2)) out << "item2 " << n2->pos().x() << " " << n2->pos().y() << "\n";
        int completed = 0;
        for (auto& c : g->connections) completed += c.fromNode == 1 && c.toNode == 3;
        out << "completedLinks " << completed << "\n";
    }
}

void ScriptEditorWindow::devSceneShot(const QString& file) {
    GraphScene* sc = currentScene();
    if (!sc) return;
    QRectF r = sc->contentRect().adjusted(-40, -40, 40, 40);
    QImage img(int(r.width() * 2), int(r.height() * 2), QImage::Format_ARGB32);
    img.fill(QColor(35, 35, 35));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    sc->render(&p, QRectF(0, 0, img.width(), img.height()), r);
    p.end();
    img.save(file);
}

void ScriptEditorWindow::log(int level, const QString& text) { log_->append(level, text); }

} // namespace fged
