// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "ScriptEditorWindow.hpp"
#include "../Common/Theme.hpp"
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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
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
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QAbstractItemView>
#include <algorithm>
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
const char* kFilter = QT_TRANSLATE_NOOP("fged::ScriptEditorWindow", "FlowGraph scripts (*.flow);;All files (*)");     // tr(kFilter) where shown
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

// The launch box of the toolbar: about 18 characters wide, whatever the entries are. A longer entry is shortened with "..." in the
// box (cut in the middle of a letter otherwise); its list and its tooltip show the whole title.
constexpr int kLaunchBoxCharacters = 18;
class LaunchBox : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter p(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        p.drawComplexControl(QStyle::CC_ComboBox, opt);
        const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        const int icon = opt.currentIcon.isNull() ? 0 : opt.iconSize.width() + 4;
        opt.currentText = fontMetrics().elidedText(opt.currentText, Qt::ElideRight, field.width() - icon - 2);
        p.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }
};
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

    migrateAppSettings();
    // the theme this application's user chose (View > Theme), before any widget of the editor exists
    if (host_.offersThemeChoice()) applyTheme(*qApp, savedTheme(host_.applicationName()));
    // a new theme: the graphs take the colours again (pins and links keep theirs), and everything is repainted
    connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        for (auto& [view, scene] : views_) {
            scene->sync();
            scene->update();
            view->viewport()->update();
        }
        if (themeGroup_)
            for (QAction* a : themeGroup_->actions()) a->setChecked(Theme(a->data().toInt()) == currentThemeChoice());
    });
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
        statusBar()->showMessage(tr("Paused at '%1'").arg(graph), 5000);
    });
    connect(&run_, &RunController::resumed, this, [this] { clearDebugMarks(); });
    connect(&run_, &RunController::finished, this, [this] { clearDebugMarks(); });
    connect(&run_, &RunController::stateChanged, this, [this](RunController::State s) { if (s == RunController::State::Idle) clearDebugMarks(); });

    doc_.newScript([this](fg::Script& s) { host_.initializeNewScript(s); });
    openGraph(qs(doc_.script().graphs.front().name));
    updateTitle();
    updateEditActions();
    updateCollapseAction();
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
    // the unique name stays English (saved layouts and View find the dock by it); the title is translated
    auto makeDock = [&](const QString& name, const QString& title, QWidget* w, bool scroll) {
        auto* dock = new CDockWidget(name, opts, lopts);
        dock->setTitle(title);
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
    find_ = new FindPanel(&doc_);

    dMy_ = makeDock("My Script", tr("My Script"), myScript_, true);
    dDetails_ = makeDock("Details", tr("Details"), details_, true);
    diagDock_ = makeDock("Compile Results", tr("Compile Results"), diagnostics_, false);
    auto* dLog = makeDock("Output Log", tr("Output Log"), log_, false);
    logDock_ = dLog;
    debugDock_ = makeDock("Debugger", tr("Debugger"), debug_, false);
    findDock_ = makeDock("Find Results", tr("Find Results"), find_, false);

    addDockWidget(dMy_, KDDockWidgets::Location_OnLeft, nullptr, KDDockWidgets::InitialOption(QSize(280, 400)));
    addDockWidget(dDetails_, KDDockWidgets::Location_OnRight, nullptr, KDDockWidgets::InitialOption(QSize(340, 400)));
    addDockWidget(dLog, KDDockWidgets::Location_OnBottom, nullptr, KDDockWidgets::InitialOption(QSize(600, 180)));
    dLog->addDockWidgetAsTab(diagDock_);
    dLog->addDockWidgetAsTab(findDock_);
    diagDock_->setAsCurrentTab();          // as before the Find Results dock existed
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
        statusLabel_->setText(e || w ? tr("%1 error(s), %2 warning(s)").arg(e).arg(w) : tr("OK"));
        const ThemeColors& tc = themeColors();
        setTextColor(statusLabel_, e ? tc.error : w ? tc.warning : tc.success);
    });
    connect(debug_, &DebugPanel::locationActivated, this, [this](const QString& g, int n) { focusNode(g, n); });
    connect(find_, &FindPanel::activated, this, [this](const QString& g, int n) {
        if (n) focusNode(g, n);
        else openGraph(g);                // a comment box: show its graph
    });
    connect(myScript_, &MyScriptPanel::referencesRequested, this, [this](int kind, const QString& owner, const QString& name) {
        fg::RefTarget t;
        t.kind = MyScriptPanel::Kind(kind) == MyScriptPanel::Kind::Function ? fg::RefTarget::Kind::Function : fg::RefTarget::Kind::Variable;
        t.name = ss(name);
        t.graph = ss(owner);
        showReferences(t);
    });
}

void ScriptEditorWindow::createActions() {
    auto act = [this](const QString& text, const QKeySequence& key = QKeySequence()) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        a->setShortcutContext(Qt::WindowShortcut);
        return a;
    };
    actNew_ = act(tr("&New"), QKeySequence::New);
    actOpen_ = act(tr("&Open..."), QKeySequence::Open);
    actSave_ = act(tr("&Save"), QKeySequence::Save);
    actNew_->setIcon(themedIcon(":/Icons/new.svg"));
    actOpen_->setIcon(themedIcon(":/Icons/open.svg"));
    actSave_->setIcon(themedIcon(":/Icons/save.svg"));
    actSaveAs_ = act(tr("Save &As..."), QKeySequence("Ctrl+Shift+S"));
    actExit_ = act(tr("E&xit"));
    actUndo_ = act(tr("&Undo"), QKeySequence::Undo);
    actRedo_ = act(tr("&Redo"), QKeySequence("Ctrl+Y"));
    actCut_ = act(tr("Cu&t") + "\tCtrl+X");
    actCopy_ = act(tr("&Copy") + "\tCtrl+C");
    actPaste_ = act(tr("&Paste") + "\tCtrl+V");
    actDuplicate_ = act(tr("&Duplicate") + "\tCtrl+D");
    actDelete_ = act(tr("De&lete") + "\tDel");
    actSelectAll_ = act(tr("Select &All") + "\tCtrl+A");
    actComment_ = act(tr("Add Co&mment") + "\tC");
    actSaveAs_->setIcon(themedIcon(":/Icons/save-as.svg"));
    actExit_->setIcon(themedIcon(":/Icons/exit.svg"));
    actUndo_->setIcon(themedIcon(":/Icons/undo.svg"));
    actRedo_->setIcon(themedIcon(":/Icons/redo.svg"));
    actCut_->setIcon(themedIcon(":/Icons/cut.svg"));
    actCopy_->setIcon(themedIcon(":/Icons/copy.svg"));
    actPaste_->setIcon(themedIcon(":/Icons/paste.svg"));
    actDuplicate_->setIcon(themedIcon(":/Icons/duplicate.svg"));
    actDelete_->setIcon(themedIcon(":/Icons/delete.svg"));
    actSelectAll_->setIcon(themedIcon(":/Icons/select-all.svg"));
    actComment_->setIcon(themedIcon(":/Icons/comment.svg"));
    actFrame_ = act(tr("&Frame All") + "\tHome");
    {
        QSettings settings = editorSettings();
        snapToGrid_ = settings.value("editor/snapToGrid", true).toBool();
        snapStep_ = settings.value("editor/snapStep", 20.0).toDouble();
        if (snapStep_ != 10.0 && snapStep_ != 20.0 && snapStep_ != 40.0) snapStep_ = 20.0;
    }
    actSnap_ = act(tr("Snap to &Grid"));
    actSnap_->setCheckable(true);
    actSnap_->setChecked(snapToGrid_);
    actSnap_->setToolTip(tr("Nodes you move or place land on the grid. Hold Alt while dragging to move freely."));
    // One button: Run when nothing runs, Pause while the script runs, Continue while it is paused (see updateRunActions)
    actRun_ = new QAction(themedIcon(":/Icons/continue.svg"), tr("Run"), this);
    actRun_->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(Qt::Key_F6)});
    actStop_ = new QAction(themedIcon(":/Icons/stop.svg"), tr("Stop"), this);
    actStop_->setShortcut(QKeySequence("Shift+F5"));
    actStepOver_ = new QAction(themedIcon(":/Icons/step-over.svg"), tr("Step Over"), this);
    actStepOver_->setShortcut(QKeySequence(Qt::Key_F10));
    actStepInto_ = new QAction(themedIcon(":/Icons/step-into.svg"), tr("Step Into"), this);
    actStepInto_->setShortcut(QKeySequence(Qt::Key_F11));
    actStepOut_ = new QAction(themedIcon(":/Icons/step-out.svg"), tr("Step Out"), this);
    actStepOut_->setShortcut(QKeySequence("Shift+F11"));
    actBreakpoint_ = act(tr("Toggle &Breakpoint"), QKeySequence(Qt::Key_F9));
    if (embedded_) {
        // no Run: the application runs the script, the debugger attaches to it
        actAttach_ = new QAction(themedIcon(":/Icons/attach.svg"), tr("&Attach"), this);
        actAttach_->setToolTip(tr("Debug the script that the application is running now: breakpoints stop it, and you can step through it"));
        actDetach_ = new QAction(themedIcon(":/Icons/detach.svg"), tr("&Detach"), this);
        actDetach_->setToolTip(tr("Stop debugging: whatever is stopped continues"));
        actRun_->setText(tr("Continue"));
    }
    actAbout_ = act(tr("&About %1").arg(host_.applicationName()));
    actFind_ = act(tr("&Find in Script..."), QKeySequence::Find);
    actFind_->setIcon(themedIcon(":/Icons/find.svg"));
    actFind_->setToolTip(tr("Find nodes by their title, comment or values in every graph of the script"));
    actFindRefs_ = act(tr("Find &References"), QKeySequence("Shift+F12"));
    actFindRefs_->setIcon(themedIcon(":/Icons/find-references.svg"));
    actFindRefs_->setToolTip(tr("The nodes that use the selected variable or function, or the nodes of the same kind as the selected node"));
    connect(actFind_, &QAction::triggered, this, [this] {
        if (findDock_) { findDock_->open(); findDock_->raise(); }
        find_->focusSearch();
    });
    connect(actFindRefs_, &QAction::triggered, this, [this] { findReferencesOfSelection(); });
    actCollapse_ = act(tr("Collapse to &Function..."));
    actCollapse_->setIcon(themedIcon(":/Icons/function.svg"));
    actCollapse_->setToolTip(tr("Moves the selected nodes into a new function and calls it in their place"));
    connect(actCollapse_, &QAction::triggered, this, [this] { collapseSelection(QString()); });

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
                           "<b>" + host_.applicationName().toHtmlEscaped() + "</b><br>" + tr("Visual scripting editor built on FlowGraph.") + "<br><br>"
                           + tr("Right-click or drag from a pin to add nodes, F5 to run, F9 for breakpoints."));
    });
}

void ScriptEditorWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->setObjectName("File");                // its English name: menuNamed() finds it in any language
    recentMenu_ = new QMenu(tr("Open &Recent"), this);
    recentMenu_->setObjectName("recentFiles");
    if (embedded_) {
        // the application owns the script: no New / Exit; Open takes only scripts that fit the application; Save gives the script back,
        // Save As writes a copy to a file
        file->addAction(actOpen_);
        file->addMenu(recentMenu_);
        file->addActions({actSave_, actSaveAs_});
    } else {
        file->addActions({actNew_, actOpen_});
        file->addMenu(recentMenu_);
        file->addActions({actSave_, actSaveAs_});
        file->addSeparator();
        file->addAction(actExit_);
    }
    rebuildRecentMenu();

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->setObjectName("Edit");                // its English name: menuNamed() finds it in any language
    edit->addActions({actUndo_, actRedo_});
    edit->addSeparator();
    edit->addActions({actCut_, actCopy_, actPaste_, actDuplicate_, actDelete_});
    edit->addSeparator();
    edit->addActions({actSelectAll_, actComment_});
    edit->addSeparator();
    edit->addActions({actFind_, actFindRefs_});
    edit->addSeparator();
    edit->addAction(actCollapse_);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName("View");                // its English name: menuNamed() finds it in any language
    view->addAction(actFrame_);
    view->addSeparator();
    view->addAction(actSnap_);
    QMenu* gridSize = view->addMenu(tr("Grid Snap Si&ze"));
    snapSizeGroup_ = new QActionGroup(this);
    for (double step : {10.0, 20.0, 40.0}) {
        QAction* a = gridSize->addAction(tr("%1 units").arg(int(step)));
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
    if (host_.offersThemeChoice()) {
        view->addSeparator();
        QMenu* themeMenu = view->addMenu(tr("&Theme"));
        themeMenu->setObjectName("themeMenu");
        themeGroup_ = new QActionGroup(this);
        for (Theme t : {Theme::Dark, Theme::Light, Theme::System}) {
            QAction* a = themeMenu->addAction(themeTitle(t));
            a->setCheckable(true);
            a->setChecked(currentThemeChoice() == t);
            a->setData(int(t));
            if (t == Theme::System) a->setToolTip(tr("Light or dark like the operating system, and it follows when the system switches"));
            themeGroup_->addAction(a);
            connect(a, &QAction::triggered, this, [this, t] { chooseTheme(t); });
        }
    }
    if (host_.offersLanguageChoice()) {
        QMenu* languageMenu = createLanguageMenu(host_.applicationName(), this);       // it applies at the next start
        languageMenu->setObjectName("languageMenu");
        view->addMenu(languageMenu);
    }
    view->addSeparator();
    for (auto* dw : docks_) view->addAction(dw->toggleAction());

    if (embedded_) {
        QMenu* debug = menuBar()->addMenu(tr("&Debug"));
        debug->setObjectName("Debug");                // its English name: menuNamed() finds it in any language
        debug->addActions({actAttach_, actDetach_});
        debug->addSeparator();
        debug->addAction(actRun_);
        debug->addSeparator();
        debug->addActions({actStepOver_, actStepInto_, actStepOut_});
        debug->addSeparator();
        debug->addAction(actBreakpoint_);
    } else {
        QMenu* runMenu = menuBar()->addMenu(tr("&Run"));
        runMenu->setObjectName("Run");                // its English name: menuNamed() finds it in any language
        runMenu->addActions({actRun_, actStop_});
        runMenu->addSeparator();
        runMenu->addActions({actStepOver_, actStepInto_, actStepOut_});
        runMenu->addSeparator();
        runMenu->addAction(actBreakpoint_);
        QMenu* speed = runMenu->addMenu(tr("Execution speed"));
        speedGroup_ = new QActionGroup(this);
        for (auto [label, ms] : {std::pair<const char*, int>{QT_TR_NOOP("Full speed"), 0}, {QT_TR_NOOP("Slow (100 ms / node)"), 100}, {QT_TR_NOOP("Slower (300 ms / node)"), 300}, {QT_TR_NOOP("Step by step (1 s / node)"), 1000}}) {
            QAction* a = speed->addAction(tr(label));
            a->setCheckable(true);
            a->setData(ms);
            a->setChecked(ms == 0);
            speedGroup_->addAction(a);
            connect(a, &QAction::triggered, this, [this, ms] { run_.setStepDelay(ms); });
        }
    }

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->setObjectName("Help");                // its English name: menuNamed() finds it in any language
    help->addAction(actAbout_);
}

void ScriptEditorWindow::createToolBar() {
    auto* tb = addToolBar(tr("Main"));
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
    entryCombo_ = new LaunchBox(tb);
    entryCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);     // not as wide as the longest entry
    entryCombo_->setMinimumContentsLength(kLaunchBoxCharacters);
    // The label that used to stand in front of the box is the title of its tooltip; the entry chosen follows it in full (the box
    // may show it shortened).
    QString launchTitle = host_.launchLabel().trimmed();
    if (launchTitle.endsWith(':')) launchTitle.chop(1);
    entryCombo_->setProperty("baseToolTip", launchTitle.isEmpty() ? host_.launchToolTip().toHtmlEscaped()
                                                                  : "<b>" + launchTitle.toHtmlEscaped() + "</b><br>" + host_.launchToolTip().toHtmlEscaped());
    entryCombo_->setToolTip(entryCombo_->property("baseToolTip").toString());
    connect(entryCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString base = entryCombo_->property("baseToolTip").toString();
        const QString current = entryCombo_->currentText();
        entryCombo_->setToolTip(current.isEmpty() ? base : base + "<br><br>" + current.toHtmlEscaped());
    });
    tb->addWidget(entryCombo_);
    connect(entryCombo_, qOverload<int>(&QComboBox::activated), this, [this](int) { host_.launchEntrySelected(entryCombo_->currentData().toString()); });
    tb->addActions({actRun_, actStop_});
    tb->addSeparator();
    tb->addActions({actStepOver_, actStepInto_, actStepOut_});
}

// A host names a menu in English ("&Run"): the menus have their English name as object name, so it is found in any language.
QMenu* ScriptEditorWindow::menuNamed(const QString& title) {
    QString wanted = QString(title).remove('&');
    for (QAction* a : menuBar()->actions())
        if (a->menu() && (a->menu()->objectName() == wanted || QString(a->menu()->title()).remove('&') == wanted)) return a->menu();
    QMenu* m = menuBar()->addMenu(title);
    m->setObjectName(wanted);
    return m;
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
    connect(scene, &GraphScene::collapseRequested, this, [this] { collapseSelection(QString()); });
    connect(scene, &GraphScene::referencesRequested, this, [this, scene](int id) {
        const fg::Graph* g = doc_.graph(scene->graphName());
        if (const fg::Node* n = g ? g->findNode(id) : nullptr) showReferences(fg::referenceTargetOf(doc_.script(), ss(scene->graphName()), *n));
    });
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

void ScriptEditorWindow::showReferences(const fg::RefTarget& target) {
    find_->findReferences(target);
    if (findDock_) { findDock_->open(); findDock_->raise(); }
    statusBar()->showMessage(tr("%1 reference(s) of %2").arg(find_->resultCount()).arg(qs(target.describe())), 4000);
}

void ScriptEditorWindow::findReferencesOfSelection() {
    // the panel the user works in decides: an item of My Script, otherwise the node selected in the current graph
    QWidget* focus = QApplication::focusWidget();
    QString owner, name;
    const MyScriptPanel::Kind kind = myScript_->selected(&owner, &name);
    if (kind != MyScriptPanel::Kind::None && focus && myScript_->isAncestorOf(focus)) {
        fg::RefTarget t;
        t.kind = kind == MyScriptPanel::Kind::Function ? fg::RefTarget::Kind::Function : fg::RefTarget::Kind::Variable;
        t.name = ss(name);
        t.graph = ss(owner);
        showReferences(t);
        return;
    }
    GraphScene* s = currentScene();
    const QList<fg::NodeId> ids = s ? s->selectedNodeIds() : QList<fg::NodeId>();
    const fg::Graph* g = s ? doc_.graph(s->graphName()) : nullptr;
    const fg::Node* n = g && ids.size() == 1 ? g->findNode(ids.front()) : nullptr;
    if (!n) {
        statusBar()->showMessage(tr("Select a node, or a variable or function in My Script, to find its references"), 4000);
        return;
    }
    showReferences(fg::referenceTargetOf(doc_.script(), ss(s->graphName()), *n));
}

void ScriptEditorWindow::chooseTheme(Theme theme) {
    applyTheme(*qApp, theme);                    // ThemeNotifier::changed: the graphs, panels and icons follow
    saveTheme(host_.applicationName(), theme);
    statusBar()->showMessage(tr("%1 theme").arg(themeTitle(theme)), 3000);
}

void ScriptEditorWindow::updateCollapseAction() {
    if (!actCollapse_) return;
    GraphScene* s = currentScene();
    actCollapse_->setEnabled(s && s->canCollapseSelection());
}

bool ScriptEditorWindow::collapseSelection(const QString& givenName) {
    GraphScene* s = currentScene();
    const QList<fg::NodeId> sel = s ? s->selectedNodeIds() : QList<fg::NodeId>();
    if (!s || sel.size() < GraphScene::kMinCollapseNodes) {
        statusBar()->showMessage(tr("Select at least two nodes to collapse them into a function"), 4000);
        return false;
    }
    const std::string graph = ss(s->graphName());
    std::vector<fg::NodeId> ids(sel.begin(), sel.end());
    // tried on a copy first: a selection that cannot be collapsed leaves no undo step behind
    fg::Script trial = doc_.script();
    fg::CollapseResult probe = fg::collapseToFunction(trial, rt_.registry(), graph, ids);
    if (!probe.ok) {
        QMessageBox::information(this, tr("Collapse to Function"), tr("These nodes cannot be collapsed into a function:") + "\n" + qs(probe.error));
        return false;
    }
    QString name = givenName;
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Collapse to Function"), tr("Name of the new function:"), QLineEdit::Normal,
                                     qs(doc_.script().uniqueGraphName("NewFunction")), &ok).trimmed();
        if (!ok || name.isEmpty()) return false;
    }
    fg::CollapseResult result;
    doc_.edit(tr("Collapse to function"), [&](fg::Script& script) { result = fg::collapseToFunction(script, rt_.registry(), graph, ids, ss(name)); });
    if (!result.ok) {                                    // cannot happen after the trial, but never keep a half change
        doc_.undo();
        return false;
    }
    for (auto& w : result.warnings) log(int(fg::LogLevel::Warning), tr("Collapse to Function: %1").arg(qs(w)));
    statusBar()->showMessage(tr("Collapsed %1 node(s) into %2").arg(ids.size()).arg(qs(result.function)), 4000);
    if (GraphView* v = openGraph(s->graphName())) v->graphScene()->selectOnly(result.callNode);
    return true;
}

void ScriptEditorWindow::onSelectionNodes(GraphScene* scene, const QList<int>& ids) {
    if (scene != currentScene()) return;
    details_->showNodes(scene->graphName(), ids);
    myScript_->showNodeSelection(scene->graphName(), ids);     // the list does not keep an item selected that Details no longer shows
    updateCollapseAction();
}

void ScriptEditorWindow::onCurrentTabChanged() {
    GraphScene* s = currentScene();
    if (!s) return;
    myScript_->setCurrentGraph(s->graphName());
    QList<int> ids;
    for (auto id : s->selectedNodeIds()) ids.push_back(id);
    if (!ids.isEmpty()) {
        details_->showNodes(s->graphName(), ids);
        myScript_->showNodeSelection(s->graphName(), ids);
    }
    updateCollapseAction();
}

void ScriptEditorWindow::clearDebugMarks() {
    for (auto& [v, s] : views_) s->setDebugNode(0);
}

// ------------------------------------------------------------------ files
bool ScriptEditorWindow::maybeSave() {
    if (!doc_.isDirty()) return true;
    auto r = QMessageBox::question(this, host_.applicationName(), tr("Save changes to '%1'?").arg(doc_.displayName().remove('*')),
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
    if (!embedded_ && !maybeSave()) return;      // embedded: a script that does not fit is refused before anything is asked
    QString path = QFileDialog::getOpenFileName(this, tr("Open script"), QString(), tr(kFilter), nullptr, QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) openPath(path, !embedded_);
}

void ScriptEditorWindow::openPath(const QString& path, bool alreadyAsked) {
    if (embedded_) {
        // a script that does not fit is refused before anything is asked or changed
        fg::Script script;
        QString reason;
        try {
            script = fg::Script::load(path.toStdString());
        } catch (const std::exception& e) {
            QMessageBox::critical(this, tr("Open script"), tr("Cannot open '%1':").arg(path) + "\n" + e.what());
            return;
        }
        if (!acceptable(script, &reason)) {
            showRejected(path, reason);
            return;
        }
        if (!alreadyAsked && !maybeSave()) return;
        openFile(path);
        return;
    }
    if (!alreadyAsked && !maybeSave()) return;
    openFile(path);
}

// ------------------------------------------------------------------ recent files
// ------------------------------------------------------------------ settings of this application
// Several applications embed the editor and share its settings store; what belongs to one of them (its dock layout, its recent
// files) is kept in a group of its own, apps/<application name>/.
QString ScriptEditorWindow::appKey(const QString& name) const { return appSettingsKey(host_.applicationName(), name); }

// Earlier versions kept the dock layout in editor/layout for every application, and the recent files in editor/recentFiles/<app>.
// They move to the application's group once: the recent files to their own application, the layout to the standalone editor
// (flowstudio wrote it); an embedded application starts with its own default layout.
void ScriptEditorWindow::migrateAppSettings() {
    QSettings s = editorSettings();
    const QString oldRecent = "editor/recentFiles/" + host_.applicationName();
    if (s.contains(oldRecent)) {
        if (!s.contains(appKey("recentFiles"))) s.setValue(appKey("recentFiles"), s.value(oldRecent));
        s.remove(oldRecent);
    }
    if (!embedded_ && s.contains("editor/layout")) {
        if (!s.contains(appKey("layout"))) {
            s.setValue(appKey("layout"), s.value("editor/layout"));
            if (s.contains("editor/layoutDocks")) s.setValue(appKey("layoutDocks"), s.value("editor/layoutDocks"));
        }
        s.remove("editor/layout");
        s.remove("editor/layoutDocks");
    }
}

QString ScriptEditorWindow::recentKey() const { return appKey("recentFiles"); }

QStringList ScriptEditorWindow::recentFiles() const { return editorSettings().value(recentKey()).toStringList(); }

void ScriptEditorWindow::addRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    const QString abs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList list = recentFiles();
    list.removeIf([&](const QString& p) { return QFileInfo(p) == QFileInfo(abs); });     // the same file (case, separators...)
    list.prepend(abs);
    while (list.size() > kMaxRecentFiles) list.removeLast();
    editorSettings().setValue(recentKey(), list);
    rebuildRecentMenu();
}

void ScriptEditorWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    // not clear(): the menu is rebuilt while one of its actions is being triggered (opening a recent file adds it again)
    for (QAction* old : recentMenu_->actions()) {
        recentMenu_->removeAction(old);
        old->deleteLater();
    }
    const QStringList list = recentFiles();
    for (int i = 0; i < list.size(); ++i) {
        const QString path = list[i];
        QFileInfo fi(path);
        QAction* a = recentMenu_->addAction(QString("&%1  %2   %3").arg(i + 1 < 10 ? QString::number(i + 1) : QString("1&0"))
                                                 .arg(fi.fileName(), QDir::toNativeSeparators(fi.absolutePath())));
        a->setToolTip(QDir::toNativeSeparators(path));
        a->setStatusTip(QDir::toNativeSeparators(path));
        connect(a, &QAction::triggered, this, [this, path] { openRecent(path); });
    }
    if (!list.isEmpty()) {
        recentMenu_->addSeparator();
        recentMenu_->addAction(themedIcon(":/Icons/clear.svg"), tr("&Clear Recent Files"), this, [this] {
            editorSettings().remove(recentKey());
            rebuildRecentMenu();
        });
    }
    recentMenu_->setEnabled(!list.isEmpty());
}

void ScriptEditorWindow::openRecent(const QString& path) {
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Open Recent"), tr("'%1' does not exist any more. It is removed from the list.").arg(QDir::toNativeSeparators(path)));
        QStringList list = recentFiles();
        list.removeAll(path);
        editorSettings().setValue(recentKey(), list);
        rebuildRecentMenu();
        return;
    }
    openPath(path, false);
}

bool ScriptEditorWindow::openFile(const QString& path) {
    if (embedded_) {
        // an embedded editor takes only a script that fits the application; otherwise the script it has stays as it is
        fg::Script script;
        try {
            script = fg::Script::load(path.toStdString());
        } catch (const std::exception& e) {
            QMessageBox::critical(this, tr("Open script"), tr("Cannot open '%1':").arg(path) + "\n" + e.what());
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
        QMessageBox::critical(this, tr("Open script"), tr("Cannot open '%1':").arg(path) + "\n" + err);
        doc_.newScript([this](fg::Script& s) { host_.initializeNewScript(s); });
        host_.scriptChanged(facade_, ScriptChange::Created);
        emit facade_.scriptChanged();
        return false;
    }
    log(int(fg::LogLevel::Info), tr("Opened %1").arg(path));
    addRecentFile(path);
    host_.scriptChanged(facade_, ScriptChange::Opened);   // a host may load plugins / launch files for this script
    auto report = fg::checkCompatibility(doc_.script(), rt_.registry());
    if (!report.ok()) {
        // the editor still opens it so that it can be repaired, but it cannot run here
        QString text = qs(report.toText());
        log(int(fg::LogLevel::Warning), tr("This script was made for another application:") + "\n" + text.trimmed());
        statusBar()->showMessage(tr("This script needs things %1 does not provide - see the Output Log").arg(host_.applicationName()), 8000);
        auto* box = new QMessageBox(QMessageBox::Warning, host_.applicationName(), tr("This script cannot run in %1:").arg(host_.applicationName()) + "\n\n" + text, QMessageBox::Ok, this);
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
            log(int(fg::LogLevel::Warning), tr("'%1' was not taken: %2").arg(title, reason.trimmed()));
            statusBar()->showMessage(tr("The script '%1' does not fit %2: see the Output Log").arg(title, host_.applicationName()), 8000);
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
        log(int(fg::LogLevel::Warning), tr("This script needs things %1 does not provide:").arg(host_.applicationName()) + "\n" + text.trimmed());
        statusBar()->showMessage(tr("This script needs things %1 does not provide - see the Output Log").arg(host_.applicationName()), 8000);
    }
    emit facade_.scriptChanged();
    return true;
}

bool ScriptEditorWindow::attachExecutor(std::shared_ptr<fg::AsyncExecutor> executor) {
    const bool ok = run_.attach(std::move(executor));
    if (ok) {
        log(int(fg::LogLevel::Info), tr("--- attached ---"));
        if (logDock_) logDock_->raise();
    }
    return ok;
}

void ScriptEditorWindow::attachToApplication() {
    std::shared_ptr<fg::AsyncExecutor> target = host_.debugTarget(facade_);
    if (!target) {
        statusBar()->showMessage(tr("Nothing is running in %1 to attach to").arg(host_.applicationName()), 5000);
        return;
    }
    attachExecutor(std::move(target));
}

bool ScriptEditorWindow::acceptable(const fg::Script& script, QString* reason) const {
    const auto report = fg::checkCompatibility(script, rt_.registry());
    if (!report.ok()) {
        if (reason) *reason = tr("This script needs things %1 does not provide:").arg(host_.applicationName()) + "\n" + qs(report.toText());
        return false;
    }
    QString why;
    if (!host_.acceptsScript(script, &why)) {
        if (reason) *reason = why.isEmpty() ? tr("This script does not fit %1.").arg(host_.applicationName()) : why;
        return false;
    }
    return true;
}

void ScriptEditorWindow::showRejected(const QString& what, const QString& reason) {
    log(int(fg::LogLevel::Warning), tr("'%1' was not opened: %2").arg(what, reason.trimmed()));
    statusBar()->showMessage(tr("That script does not fit %1 - it was not opened").arg(host_.applicationName()), 8000);
    auto* box = new QMessageBox(QMessageBox::Warning, host_.applicationName(),
                                tr("This script cannot be opened here:") + "\n\n" + reason.trimmed() + "\n\n" + tr("The script that was open is unchanged."), QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();                                        // not modal for the flow of the program
}

// Save of an embedded editor: no file. The script goes to the application, which decides what to do with it.
bool ScriptEditorWindow::embeddedSave() {
    if (!host_.applyScript(facade_, doc_.script())) {
        statusBar()->showMessage(tr("%1 did not take the script: the changes are still unsaved").arg(host_.applicationName()), 6000);
        return false;
    }
    doc_.markSaved();
    statusBar()->showMessage(tr("The script was handed over to %1").arg(host_.applicationName()), 3000);
    emit facade_.scriptChanged();
    return true;
}

bool ScriptEditorWindow::fileSave() {
    if (embedded_) return embeddedSave();
    if (doc_.path().isEmpty()) return fileSaveAs();
    QString err;
    if (!doc_.save(QString(), &err)) {
        QMessageBox::critical(this, tr("Save script"), err);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(doc_.path()), 3000);
    addRecentFile(doc_.path());
    host_.scriptChanged(facade_, ScriptChange::Saved);
    emit facade_.scriptChanged();
    return true;
}

bool ScriptEditorWindow::fileSaveAs() {
    if (embedded_) {
        // a copy in a file: the script stays the application's (unsaved changes stay unsaved)
        QString sel = kFilter;
        QString file = QFileDialog::getSaveFileName(this, tr("Save a copy of the script"), doc_.displayName().remove('*'), tr(kFilter), &sel, QFileDialog::DontUseNativeDialog);
        if (file.isEmpty()) return false;
        if (!file.contains('.')) file += ".flow";
        QString err;
        if (!doc_.exportTo(file, &err)) {
            QMessageBox::critical(this, tr("Save script"), err);
            return false;
        }
        statusBar()->showMessage(tr("Saved %1").arg(file), 3000);
        return true;
    }
    QString sel = kFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("Save script"), doc_.path(), tr(kFilter), &sel, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return false;
    if (!path.contains('.')) path += ".flow";
    QString err;
    if (!doc_.save(path, &err)) {
        QMessageBox::critical(this, tr("Save script"), err);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
    addRecentFile(path);
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
        editorSettings().setValue(appKey("layout"), KDDockWidgets::LayoutSaver().serializeLayout());
        QStringList names;                 // the docks this layout knows (see restoreSavedLayout)
        for (auto* dock : docks_) names << dock->uniqueName();
        editorSettings().setValue(appKey("layoutDocks"), names);
    }
    KDDockWidgets::QtWidgets::MainWindow::closeEvent(e);
}

void ScriptEditorWindow::restoreSavedLayout() {
    persistLayout_ = true;
    const QByteArray saved = editorSettings().value(appKey("layout")).toByteArray();
    if (saved.isEmpty()) return;
    // The saved sizes replace the initial ones. Set before restoring: the saved layout shows the window, and the first
    // showEvent would otherwise schedule applyInitialSizes().
    sizesApplied_ = true;
    if (!KDDockWidgets::LayoutSaver().restoreLayout(saved)) {
        sizesApplied_ = false;
        return;
    }
    // A dock that is newer than the saved layout would stay closed: put it where it goes by default. (A layout saved before this list
    // was kept knows the docks the editor had then.) Docks the layout knows keep whatever the user did with them, closed or not.
    QStringList known = editorSettings().value(appKey("layoutDocks")).toStringList();
    if (known.isEmpty()) known = {"My Script", "Details", "Compile Results", "Output Log", "Debugger"};
    if (findDock_ && !known.contains(findDock_->uniqueName()) && logDock_) {
        logDock_->addDockWidgetAsTab(findDock_);
        logDock_->setAsCurrentTab();
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
    actUndo_->setText(doc_.canUndo() ? tr("&Undo %1").arg(doc_.undoLabel()) : tr("&Undo"));
    actRedo_->setText(doc_.canRedo() ? tr("&Redo %1").arg(doc_.redoLabel()) : tr("&Redo"));
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
        actRun_->setText(s == S::Paused ? tr("Continue") : tr("Pause"));
        actRun_->setIcon(themedIcon(s == S::Paused ? ":/Icons/continue.svg" : ":/Icons/interrupt.svg"));
        actStop_->setEnabled(false);
        actStepOver_->setEnabled(s == S::Paused);
        actStepInto_->setEnabled(s == S::Paused);
        actStepOut_->setEnabled(s == S::Paused);
        return;
    }
    actRun_->setText(busy ? tr("Pause") : s == S::Paused ? tr("Continue") : tr("Run"));
    actRun_->setIcon(themedIcon(busy ? ":/Icons/interrupt.svg" : ":/Icons/continue.svg"));
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
    int longest = 0;
    for (auto& e : entries_) {
        entryCombo_->addItem(e.title, e.id);
        longest = std::max(longest, entryCombo_->fontMetrics().horizontalAdvance(e.title));
    }
    // the box is narrower than a long entry: its list is as wide as the longest one (plus the room of a scroll bar)
    entryCombo_->view()->setMinimumWidth(longest + 2 * entryCombo_->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    int idx = entryCombo_->findData(keep);
    if (idx < 0) idx = entryCombo_->findData(host_.preferredEntry());
    if (idx >= 0) entryCombo_->setCurrentIndex(idx);
    entryCombo_->blockSignals(false);
    const QString base = entryCombo_->property("baseToolTip").toString();      // signals were blocked: the tooltip follows here
    if (!base.isEmpty()) entryCombo_->setToolTip(entryCombo_->currentText().isEmpty() ? base : base + "<br><br>" + entryCombo_->currentText().toHtmlEscaped());
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
    log(int(fg::LogLevel::Warning), tr("Nothing to run: add an event to a graph (this application offers: %1 entries).").arg(entries_.size()));
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
    else if (p.size() == 2 && p[0] == "theme") applyTheme(*qApp, themeFromKey(p[1]));     // a switch while running; not saved
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
    img.fill(themeColors().canvas);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    sc->render(&p, QRectF(0, 0, img.width(), img.height()), r);
    p.end();
    img.save(file);
}

void ScriptEditorWindow::log(int level, const QString& text) { log_->append(level, text); }

} // namespace fged
