// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include <FlowGraphGLEditor/Studio.hpp>

#include "Graph/GraphView.hpp"
#include "Icons.hpp"
#include "Language.hpp"
#include "Panels/DebugPanel.hpp"
#include "Panels/DetailsPanel.hpp"
#include "Panels/InfoPanels.hpp"
#include "Panels/LaunchDialog.hpp"
#include "Panels/MyScriptPanel.hpp"

#include <FlowGraph/Vm.hpp>
#include <FlowGraphEditorCore/RunSession.hpp>

#include <GLWidgets/Platform/Platform.hpp>
#include <GLWidgets/Widgets/Action.hpp>
#include <GLWidgets/Widgets/Application.hpp>
#include <GLWidgets/Widgets/Declarative.hpp>
#include <GLWidgets/Widgets/Dialog.hpp>
#include <GLWidgets/Widgets/Docking.hpp>
#include <GLWidgets/Widgets/MenuBar.hpp>
#include <GLWidgets/Widgets/Settings.hpp>
#include <FlowGraphEditorCore/Launches.hpp>
#include <FlowGraph/StandardApp.hpp>
#include <GLWidgets/Widgets/ToolBar.hpp>
#include <GLWidgets/Widgets/ComboBox.hpp>
#include <GLWidgets/Widgets/StatusBar.hpp>
#include <GLWidgets/Widgets/TabView.hpp>
#include <GLWidgets/Widgets/TextEdit.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace fged::gl {

namespace {

namespace ui = glw::ui;
using glw::tr;
using namespace std::chrono_literals;

constexpr std::size_t recentLimit = 10;

// The color schemes and the styles, as the preferences name them ("system": the colors follow the system's).
constexpr std::array<std::pair<std::string_view, glw::ColorScheme>, 5> schemes{{{"dark", glw::ColorScheme::Dark},
                                                                                {"light", glw::ColorScheme::Light},
                                                                                {"dusk", glw::ColorScheme::Dusk},
                                                                                {"sand", glw::ColorScheme::Sand},
                                                                                {"turquoise", glw::ColorScheme::Turquoise}}};
constexpr std::array<std::pair<std::string_view, glw::ThemeStyle>, 5> styles{{{"studio", glw::ThemeStyle::Studio},
                                                                              {"fluid", glw::ThemeStyle::Fluid},
                                                                              {"plain", glw::ThemeStyle::Plain},
                                                                              {"plastic", glw::ThemeStyle::Plastic},
                                                                              {"metal", glw::ThemeStyle::Metal}}};

glw::LocalizedText schemeTitle(glw::ColorScheme scheme) {
    switch (scheme) {
    case glw::ColorScheme::Dark: return tr("Dark");
    case glw::ColorScheme::Light: return tr("Light");
    case glw::ColorScheme::Dusk: return tr("Dusk");
    case glw::ColorScheme::Sand: return tr("Sand");
    case glw::ColorScheme::Turquoise: return tr("Turquoise");
    }
    return {};
}

glw::LocalizedText styleTitle(glw::ThemeStyle style) {
    switch (style) {
    case glw::ThemeStyle::Studio: return tr("Studio");
    case glw::ThemeStyle::Fluid: return tr("Fluid");
    case glw::ThemeStyle::Plain: return tr("Plain");
    case glw::ThemeStyle::Plastic: return tr("Plastic");
    case glw::ThemeStyle::Metal: return tr("Metal");
    }
    return {};
}

std::string fileName(const std::string& path) {
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    const std::u8string name = p.filename().u8string();
    return {name.begin(), name.end()};
}

std::shared_ptr<glw::Action> action(glw::LocalizedText text, std::function<void()> fn, glw::Shortcut shortcut = {}) {
    auto a = std::make_shared<glw::Action>(std::move(text), std::move(fn));
    if (!shortcut.empty())
        a->setShortcut(shortcut);
    return a;
}

glw::Shortcut key(glw::platform::Key k, bool shift = false) {
    using glw::platform::KeyModifiers;
    return {k, shift ? glw::platform::shortcutModifier | KeyModifiers::Shift : glw::platform::shortcutModifier};
}

// What a run reports comes to the editor's thread through the application (Application::post), its buffers emptied by a
// timer of the application's scheduler. Nothing posted runs once it is gone.
class AppThread final : public core::UiThread {
public:
    explicit AppThread(glw::Application& app) : app_(app) {}
    ~AppThread() override { *alive_ = false; }

    void post(std::function<void()> fn) override {
        app_.post([alive = alive_, fn = std::move(fn)] {
            if (*alive)
                fn();
        });
    }
    void startRepeating(int intervalMs, std::function<void()> fn) override {
        if (glw::Scheduler* s = app_.context().scheduler)
            timer_.start(*s, std::chrono::milliseconds(intervalMs), std::move(fn), true);
    }
    void stopRepeating() override { timer_.stop(); }

private:
    glw::Application& app_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    glw::Timer timer_;
};

constexpr const char* graphPrefix = "graph:";      // the id of a graph's panel: graph:<name>

} // namespace

struct Studio::Impl {
    glw::Application& app;
    fg::Runtime& rt;
    StudioOptions options;
    core::Document doc;
    core::Launches launches;                    // the script's launch configurations
    glw::Settings settings;
    AppThread uiThread;
    std::unique_ptr<core::RunSession> run;      // after the thread it posts to: gone before it

    glw::Window* window = nullptr;
    glw::DockArea* area = nullptr;
    glw::DockTabs* documents = nullptr;         // the graphs: the panels of the dock area's center stack
    MyScriptPanel* myScript = nullptr;
    DetailsPanel* details = nullptr;
    CompileResultsPanel* compileResults = nullptr;
    FindPanel* find = nullptr;
    DebugPanel* debugger = nullptr;
    glw::Label* compileState = nullptr;         // the status bar's: how many errors and warnings
    glw::StatusBar* status = nullptr;
    glw::Label* runState = nullptr;
    glw::ComboBox* launchBox = nullptr;         // the toolbar's: the configuration Run starts
    int speedMs = 0;                            // the Debug menu's execution speed: a delay between two nodes
    glw::TextEdit* log = nullptr;               // the Output Log
    std::map<std::string, GraphView*> views;    // the graphs open, by name (in the center stack, or docked or floated elsewhere)
    std::string currentGraphName;               // the one shown last
    // Views closed while the document tells its listeners (they among them): destroyed once that is over.
    std::vector<std::unique_ptr<glw::Widget>> closedViews;
    void closeGraph(const std::string& name) {
        if (std::unique_ptr<glw::Widget> view = area->removePanel(graphPrefix + name)) {
            closedViews.push_back(std::move(view));
            app.post([this] { closedViews.clear(); });
        }
        views.erase(name);
    }

    std::shared_ptr<glw::Action> newAction, openAction, saveAction, saveAsAction, quitAction, undoAction, redoAction;
    std::shared_ptr<glw::Action> cutAction, copyAction, pasteAction, duplicateAction, selectAllAction, snapAction;
    std::shared_ptr<glw::Action> findAction, findReferencesAction;
    std::shared_ptr<glw::Action> configureAction;
    std::shared_ptr<glw::Action> runAction, pauseAction, stopAction, stepOverAction, stepIntoAction, stepOutAction, breakpointAction;
    bool closing = false;                       // asked whether to save, and it goes: the window may close now

    Impl(glw::Application& a, fg::Runtime& r, StudioOptions o) : app(a), rt(r), options(std::move(o)), doc(r), launches(r), uiThread(a) {}
    // The window's panels and graphs go first, while the document and the run they listen to are there (the application,
    // and its windows, outlive the studio).
    ~Impl() {
        if (window)
            window->ui().setContent(nullptr);
        closedViews.clear();
    }

    // ---- preferences ----

    void loadSettings() {
        const std::string folder = app.platform().preferencesFolder(options.organization, options.settingsName);
        if (folder.empty())
            return;
        settings.setPath(folder + "settings.json");
        settings.load();
        applyTheme(false);
    }

    // The texts in the language chosen (the system's at first); the nodes' too, and the plugins' as they are loaded.
    void applyLanguage() {
        gl::applyLanguage(app, settings.string("language", "system"));
        loadPluginTexts(rt);
        doc.setBeforeCompile([this] { loadPluginTexts(rt); });
    }

    void saveSettings() {
        if (!settings.path().empty())
            settings.save();
    }

    void applyTheme(bool animated) {
        const std::string style = settings.string("style", "studio");
        for (const auto& [name, s] : styles) {
            if (name == style)
                app.setStyle(s, animated);
        }
        const std::string colors = settings.string("colors", "system");
        app.setFollowsSystemColors(colors == "system");
        if (colors == "system" && !app.systemPrefersDark())
            app.setColorScheme(glw::ColorScheme::Dark, animated);      // a system that does not say: dark
        for (const auto& [name, scheme] : schemes) {
            if (name == colors)
                app.setColorScheme(scheme, animated);
        }
    }

    void setTheme(std::string key, std::string value) {
        settings.setString(key, value);
        applyTheme(true);
        saveSettings();
        rebuildMenus();
    }

    std::vector<std::string> recentFiles() const { return settings.strings("recentFiles"); }

    void addRecent(const std::string& path) {
        std::vector<std::string> recent = recentFiles();
        std::erase(recent, path);
        recent.insert(recent.begin(), path);
        if (recent.size() > recentLimit)
            recent.resize(recentLimit);
        settings.setStrings("recentFiles", recent);
        saveSettings();
        rebuildMenus();
    }

    // ---- the window ----

    void build() {
        auto dock = std::make_unique<glw::DockArea>();
        area = dock.get();

        // the graphs in the middle: dock tabs of the area's center stack (closed, moved, floated as any panel), the stack kept
        // with its room when none is open
        documents = &area->setCenterStack();
        glw::Widget* center = area->center();
        area->setOnPanelShown([this](glw::DockTabs&, const std::string& id) {
            if (id.rfind(graphPrefix, 0) != 0)
                return;
            currentGraphName = id.substr(std::string_view(graphPrefix).size());
            if (myScript)
                myScript->setCurrentGraph(currentGraphName);
        });
        // a graph's tab closed: its view goes (the graph stays in the script: My Script opens it again)
        area->setOnPanelClosed([this](glw::DockPanel panel) {
            if (panel.id.rfind(graphPrefix, 0) != 0)
                return;
            const std::string name = panel.id.substr(std::string_view(graphPrefix).size());
            views.erase(name);
            if (currentGraphName == name) {
                currentGraphName.clear();
                if (myScript)
                    myScript->setCurrentGraph({});
            }
        });

        auto scriptPanel = std::make_unique<MyScriptPanel>(doc);
        myScript = scriptPanel.get();
        myScript->setOnOpenGraph([this](const std::string& graph) { showGraph(graph); });
        myScript->setOnEventChosen([this](const std::string& graph, fg::NodeId node, bool show) {
            if (GraphView* g = showGraph(graph)) {
                g->select({node});
                if (show)
                    g->showNode(node);
            }
        });
        myScript->setOnMessage([this](glw::LocalizedText text) { status->showMessage(std::move(text), 4s); });
        area->addPanel({.id = "myScript", .title = tr("My Script"), .content = std::move(scriptPanel), .closable = false}, center,
                       glw::DockSide::Start, 0.2f);
        // what is chosen in My Script, or selected in a graph: its properties
        auto detailsPanel = std::make_unique<DetailsPanel>(doc);
        details = detailsPanel.get();
        {
            const std::vector<std::string> closed = settings.strings("detailsClosed");
            details->setClosedSections({closed.begin(), closed.end()});
        }
        details->setOnSectionsChanged([this](const std::set<std::string>& closed) {
            settings.setStrings("detailsClosed", {closed.begin(), closed.end()});
            saveSettings();
        });
        details->setOnMessage([this](glw::LocalizedText text) { status->showMessage(std::move(text), 4s); });
        myScript->setOnSelectionChanged([this] {
            const std::optional<MyScriptPanel::Selection>& chosen = myScript->selection();
            if (!chosen) {
                if (details->mode() != DetailsPanel::Mode::Nodes)
                    details->showNothing();
                return;
            }
            switch (chosen->member) {
            case core::Member::Variable: details->showVariable({}, chosen->name); break;
            case core::Member::Local: details->showVariable(chosen->owner, chosen->name); break;
            case core::Member::Function:
            case core::Member::EventGraph: details->showGraph(chosen->name); break;
            case core::Member::Struct: details->showStruct(chosen->name); break;
            }
        });
        area->addPanel({.id = "details", .title = tr("Details"), .content = std::move(detailsPanel), .closable = false}, center,
                       glw::DockSide::End, 0.24f);
        // what a run prints
        auto output = std::make_unique<glw::TextEdit>();
        log = output.get();
        log->setReadOnly(true);
        log->setLineNumbers(false);
        area->addPanel({.id = "output", .title = tr("Output Log"), .content = std::move(output), .closable = false}, center,
                       glw::DockSide::Bottom, 0.24f);
        // what the compiler says, what was found: beside it
        auto results = std::make_unique<CompileResultsPanel>(doc);
        compileResults = results.get();
        compileResults->setOnActivated([this](const std::string& graph, fg::NodeId node) { focusNode(graph, node); });
        compileResults->setOnSummaryChanged([this](int errors, int warnings) { showCompileState(errors, warnings); });
        auto findPanel = std::make_unique<FindPanel>(doc);
        find = findPanel.get();
        find->setOnActivated([this](const std::string& graph, fg::NodeId node) { focusNode(graph, node); });
        find->setOnDeclaration([this](fg::ScriptRef::Declaration declaration, const std::string& graph, const std::string& name) {
            showDeclaration(declaration, graph, name);
        });
        myScript->setOnFindReferences([this](core::Member member, const std::string& owner, const std::string& name) {
            fg::RefTarget target;
            switch (member) {
            case core::Member::EventGraph: target = {fg::RefTarget::Kind::Definition, "event:" + name, {}}; break;
            case core::Member::Function: target = {fg::RefTarget::Kind::Function, name, {}}; break;
            case core::Member::Struct: target = {fg::RefTarget::Kind::Struct, name, {}}; break;
            case core::Member::Variable:
            case core::Member::Local: target = {fg::RefTarget::Kind::Variable, name, owner}; break;
            }
            showReferences(target);
        });
        // the debugger: the call stack, the variables and the watches of the run (the session made for it now)
        run = std::make_unique<core::RunSession>(doc, uiThread);
        auto debugPanel = std::make_unique<DebugPanel>(doc, *run);
        debugger = debugPanel.get();
        debugger->setWatchStore([this](const std::string& path) { return settings.strings("watches:" + path); },
                                [this](const std::string& path, const std::vector<std::string>& watches) {
                                    settings.setStrings("watches:" + path, watches);
                                    saveSettings();
                                });
        debugger->setOnLocation([this](const std::string& graph, fg::NodeId node) { focusNode(graph, node); });
        // what the compiler says, what was found: beside the Output Log; the debugger beside the Details. A layout saved
        // before them does not know them: they are put there after it is restored (it would put them in its first stack)
        std::vector<std::pair<glw::DockPanel, const char*>> added;
        added.emplace_back(glw::DockPanel{.id = "compileResults", .title = tr("Compile Results"), .content = std::move(results), .closable = false}, "output");
        added.emplace_back(glw::DockPanel{.id = "findResults", .title = tr("Find Results"), .content = std::move(findPanel), .closable = false}, "output");
        added.emplace_back(glw::DockPanel{.id = "debugger", .title = tr("Debugger"), .content = std::move(debugPanel), .closable = false}, "details");
        const std::string layout = settings.string("layout");
        const std::vector<std::string> known = glw::DockArea::panelsIn(layout);
        std::vector<std::pair<glw::DockPanel, const char*>> later;
        for (auto& [panel, beside] : added) {
            if (layout.empty() || std::find(known.begin(), known.end(), panel.id) != known.end())
                area->addPanel(std::move(panel), area->tabsOf(beside));
            else
                later.emplace_back(std::move(panel), beside);
        }
        if (!layout.empty())
            area->restoreLayout(layout);
        for (auto& [panel, beside] : later)
            area->addPanel(std::move(panel), area->tabsOf(beside));
        showPanel("output");
        showPanel("details");

        const glw::Size size{static_cast<float>(settings.number("windowWidth", 1280)), static_cast<float>(settings.number("windowHeight", 760))};
        window = &app.openWindow({.title = tr("FlowGraph Studio"), .size = size}, std::move(dock));
        status = &window->setStatusBar();
        status->setText(tr("Ready"));
        compileState = &status->addPermanent(std::make_unique<glw::Label>(glw::Label::Props{.fontSize = 12}));
        showCompileState(compileResults->errors(), compileResults->warnings());
        runState = &status->addPermanent(std::make_unique<glw::Label>(glw::Label::Props{.text = tr("Idle"), .fontSize = 12, .muted = true}));

        makeRun();
        makeActions();
        makeToolBar();
        rebuildMenus();

        // closing the window, quitting: what was not saved first
        window->setOnCloseRequested([this] { return closeRequested([this] { window->close(); }); });
        app.setOnQuitRequested([this] { return closeRequested([this] { app.quit(); }); });
        app.setOnQuitting([this] {
            settings.setString("layout", area->saveLayout());
            saveSettings();
        });

        doc.titleChanged.connect([this] { showTitle(); });
        doc.undoStateChanged.connect([this] { showUndoState(); });
        doc.changed.connect([this](bool structural) {
            if (structural)
                showScript();
        });
        doc.scriptReplaced.connect([this] { showScript(); });
        // another script: views of its own (not those of the graphs of the same name the other had)
        doc.scriptOpened.connect([this] { dropGraphs(); });
    }

    void makeActions() {
        using glw::platform::Key;
        newAction = action(tr("&New"), [this] { whenSaved([this] { newScript(); }); }, key(Key::N));
        openAction = action(tr("&Open..."), [this] { whenSaved([this] { chooseFile(); }); }, key(Key::O));
        saveAction = action(tr("&Save"), [this] { save({}); }, key(Key::S));
        saveAsAction = action(tr("Save &As..."), [this] { saveAs({}); }, key(Key::S, true));
        quitAction = action(tr("&Quit"), [this] { window->requestClose(); }, key(Key::Q));
        undoAction = action(tr("&Undo"), [this] { doc.undo(); }, key(Key::Z));
        redoAction = action(tr("&Redo"), [this] { doc.redo(); }, key(Key::Y));
        // on the graph shown
        const auto onGraph = [this](void (GraphView::*command)()) {
            return [this, command] {
                if (GraphView* g = currentGraph())
                    (g->*command)();
            };
        };
        cutAction = action(tr("Cu&t"), onGraph(&GraphView::cutSelection), key(Key::X));
        copyAction = action(tr("&Copy"), onGraph(&GraphView::copySelection), key(Key::C));
        pasteAction = action(tr("&Paste"), onGraph(&GraphView::paste), key(Key::V));
        duplicateAction = action(tr("D&uplicate"), onGraph(&GraphView::duplicateSelection), key(Key::D));
        selectAllAction = action(tr("Select &All"), onGraph(&GraphView::selectAll), key(Key::A));
        snapAction = action(tr("&Snap to Grid"), [this] {
            settings.setBoolean("snapToGrid", snapAction->checked());
            saveSettings();
            for (GraphView* g : graphViews())
                g->setSnapToGrid(snapAction->checked());
        });
        snapAction->setCheckable(true);
        findAction = action(tr("&Find in Script..."), [this] {
            showPanel("findResults");
            find->focusSearch();
        }, key(Key::F));
        findReferencesAction = action(tr("Find &References"), [this] { findReferencesOfSelection(); },
                                      {Key::F12, glw::platform::KeyModifiers::Shift});
        snapAction->setChecked(settings.boolean("snapToGrid", false));

        // running and debugging
        runAction = action(tr("&Run"), [this] {
            if (run->state() == core::RunState::Paused)
                run->resume();
            else if (!run->isActive())
                startRun();
        }, {Key::F5});
        pauseAction = action(tr("&Pause"), [this] { run->pause(); }, {Key::F6});
        stopAction = action(tr("S&top"), [this] { run->stop(); }, {Key::F5, glw::platform::KeyModifiers::Shift});
        stepOverAction = action(tr("Step &Over"), [this] { run->stepOver(); }, {Key::F10});
        stepIntoAction = action(tr("Step &Into"), [this] { run->stepInto(); }, {Key::F11});
        stepOutAction = action(tr("Step O&ut"), [this] { run->stepOut(); }, {Key::F11, glw::platform::KeyModifiers::Shift});
        configureAction = action(tr("Launch &Configurations..."), [this] { editLaunches(); });
        configureAction->setToolTip(tr("Edit the launch configurations of this script"));
        for (auto [a, icon] : {std::pair{newAction, "new"}, {openAction, "open"}, {saveAction, "save"}, {runAction, "continue"},
                               {pauseAction, "interrupt"}, {stopAction, "stop"}, {stepOverAction, "step-over"},
                               {stepIntoAction, "step-into"}, {stepOutAction, "step-out"}, {configureAction, "configure"}})
            a->setIcon(studioIcon(icon));
        breakpointAction = action(tr("Toggle &Breakpoint"), [this] {
            if (GraphView* g = currentGraph())
                g->toggleBreakpoints();
        }, {Key::F9});
        showRunState(core::RunState::Idle);
        showUndoState();
    }

    void rebuildMenus() {
        if (!window)
            return;
        std::vector<glw::MenuItem> recent;
        for (const std::string& path : recentFiles())
            recent.push_back({.text = glw::LocalizedText(path), .onTriggered = [this, path] { whenSaved([this, path] { open(path); }); }});
        if (!recent.empty()) {
            recent.push_back(glw::MenuItem::separatorItem());
            recent.push_back({.text = tr("Clear the List"), .onTriggered = [this] {
                                  settings.setStrings("recentFiles", {});
                                  saveSettings();
                                  rebuildMenus();
                              }});
        }

        std::vector<glw::MenuItem> colors{{.text = tr("As the System"),
                                           .onTriggered = [this] { setTheme("colors", "system"); },
                                           .checked = settings.string("colors", "system") == "system"},
                                          glw::MenuItem::separatorItem()};
        for (const auto& [name, scheme] : schemes)
            colors.push_back({.text = schemeTitle(scheme),
                              .onTriggered = [this, n = std::string(name)] { setTheme("colors", n); },
                              .checked = settings.string("colors", "system") == name});
        std::vector<glw::MenuItem> looks;
        for (const auto& [name, style] : styles)
            looks.push_back({.text = styleTitle(style),
                             .onTriggered = [this, n = std::string(name)] { setTheme("style", n); },
                             .checked = settings.string("style", "studio") == name});

        std::vector<glw::MenuItem> menus{
            {.text = tr("&File"),
             .items = {{.action = newAction},
                       {.action = openAction},
                       {.text = tr("Open &Recent"), .enabled = !recent.empty(), .items = std::move(recent)},
                       glw::MenuItem::separatorItem(),
                       {.action = saveAction},
                       {.action = saveAsAction},
                       glw::MenuItem::separatorItem(),
                       {.action = quitAction}}},
            {.text = tr("&Edit"),
             .items = {{.action = undoAction},
                       {.action = redoAction},
                       glw::MenuItem::separatorItem(),
                       {.action = cutAction},
                       {.action = copyAction},
                       {.action = pasteAction},
                       {.action = duplicateAction},
                       {.text = tr("&Delete"), .onTriggered = [this] {
                            if (GraphView* g = currentGraph())
                                g->deleteSelection();
                        }, .shortcut = tr("Del")},
                       glw::MenuItem::separatorItem(),
                       {.action = selectAllAction},
                       glw::MenuItem::separatorItem(),
                       {.action = findAction},
                       {.action = findReferencesAction}}},
            {.text = tr("&View"),
             .items = {{.text = tr("&Frame Selection"), .onTriggered = [this] {
                            if (GraphView* g = currentGraph())
                                g->frameSelection();
                        }, .shortcut = tr("F")},
                       {.action = snapAction},
                       glw::MenuItem::separatorItem(),
                       {.text = tr("&Colors"), .items = std::move(colors)},
                       {.text = tr("&Style"), .items = std::move(looks)},
                       {.text = tr("&Language"), .items = languageItems()}}},
            {.text = tr("&Debug"),
             .items = {{.action = runAction},
                       {.action = pauseAction},
                       {.action = stopAction},
                       glw::MenuItem::separatorItem(),
                       {.action = stepOverAction},
                       {.action = stepIntoAction},
                       {.action = stepOutAction},
                       glw::MenuItem::separatorItem(),
                       {.action = breakpointAction},
                       glw::MenuItem::separatorItem(),
                       {.text = tr("Execution &Speed"), .items = speedItems()},
                       {.enabled = options.launchConfigurations, .action = configureAction}}},
            {.text = tr("&Help"), .items = {{.text = tr("&About FlowGraph Studio"), .onTriggered = [this] { about(); }}}},
        };
        if (!menuBar)
            menuBar = &window->setMenuBar(std::make_unique<glw::MenuBar>());
        menuBar->setMenus(std::move(menus));
    }
    glw::MenuBar* menuBar = nullptr;

    // ---- what the document shows ----

    void showTitle() {
        std::string name = doc.path().empty() ? (doc.title().empty() ? std::string() : doc.title()) : fileName(doc.path());
        glw::LocalizedText title = name.empty() ? tr("Untitled") : glw::LocalizedText(name);
        const std::string dirty = doc.isDirty() ? "*" : "";
        window->setTitle(tr("{name}{dirty} - {application}")
                             .arg("name", app.translator().resolve(title))
                             .arg("dirty", dirty)
                             .arg("application", options.applicationName));
    }

    // System, then every language there is a translation of, each in its own; chosen, it shows at once
    std::vector<glw::MenuItem> languageItems() {
        const std::string chosen = settings.string("language", "system");
        std::vector<glw::MenuItem> items{{.text = tr("System"), .onTriggered = [this] { chooseLanguage("system"); }, .checked = chosen == "system"},
                                         glw::MenuItem::separatorItem()};
        for (const std::string& code : availableLanguages())
            items.push_back({.text = glw::LocalizedText(languageName(code)), .onTriggered = [this, code] { chooseLanguage(code); }, .checked = chosen == code});
        return items;
    }

    void chooseLanguage(const std::string& code) {
        if (settings.string("language", "system") == code)
            return;
        settings.setString("language", code);
        saveSettings();
        // at once: what GLWidgets shows of the texts follows by itself; what was made of them (the nodes' titles and pins,
        // the panels' rows, the window's title) is made again
        applyLanguage();
        for (GraphView* g : graphViews())
            g->refresh();
        details->rebuild();
        compileResults->refresh();
        find->refresh();
        debugger->refresh();
        showTitle();
        rebuildMenus();
    }

    // Full speed, or a delay between two nodes: slow motion, to watch the links (it applies at once, to a run too)
    std::vector<glw::MenuItem> speedItems() {
        std::vector<glw::MenuItem> items;
        for (auto [label, ms] : {std::pair{tr("Full Speed"), 0}, {tr("Slow (100 ms / node)"), 100}, {tr("Slower (300 ms / node)"), 300},
                                 {tr("Step by Step (1 s / node)"), 1000}})
            items.push_back({.text = label, .onTriggered = [this, ms] {
                                 speedMs = ms;
                                 run->setStepDelay(ms);
                                 rebuildMenus();
                             }, .checked = speedMs == ms});
        return items;
    }

    void showUndoState() {
        undoAction->setEnabled(doc.canUndo());
        redoAction->setEnabled(doc.canRedo());
        undoAction->setText(doc.canUndo() ? tr("&Undo {what}").arg("what", doc.undoLabel()) : tr("&Undo"));
        redoAction->setText(doc.canRedo() ? tr("&Redo {what}").arg("what", doc.redoLabel()) : tr("&Redo"));
    }

    // The graphs in tabs, what the script has in My Script: until the canvas and the panels are made, what there is.
    // A graph's view: told what to say in the status bar, snapping as the View menu says.
    std::unique_ptr<GraphView> makeGraphView(const std::string& name) {
        auto view = std::make_unique<GraphView>(doc, name);
        view->setSnapToGrid(snapAction && snapAction->checked());
        view->setOnMessage([this](glw::LocalizedText text) { status->showMessage(std::move(text), 4s); });
        // while a run is paused: the values of pins, with their tooltips
        GraphView* g = view.get();
        view->setPinValues([this, g](fg::NodeId node, const std::string& pin, std::string& out) {
            return run && run->state() == core::RunState::Paused && run->pinText(g->graphName(), node, pin, out);
        });
        // nodes selected: their properties (what My Script had chosen let go)
        view->setOnSelectionChanged([this, g] {
            if (!details)
                return;
            const std::set<fg::NodeId>& nodes = g->selectedNodes();
            if (!nodes.empty()) {
                if (myScript)
                    myScript->clearSelection();
                details->showNodes(g->graphName(), {nodes.begin(), nodes.end()});
            } else if (details->mode() == DetailsPanel::Mode::Nodes) {
                details->showNothing();
            }
        });
        view->setOnFindReferences([this, g](fg::NodeId id) {
            const fg::Graph* graph = doc.graph(g->graphName());
            if (const fg::Node* node = graph ? graph->findNode(id) : nullptr)
                showReferences(fg::referenceTargetOf(doc.script(), g->graphName(), *node));
        });
        // a call of a function clicked twice: the function's graph
        view->setOnNodeActivated([this, g](fg::NodeId id) {
            const NodeView* node = g->nodeView(id);
            if (!node)
                return;
            const fg::CompiledNode& cn = node->compiled();
            const auto function = cn.props.find("function");
            if (cn.defId == "func.call" && function != cn.props.end())
                showGraph(function->second);
        });
        return view;
    }

    // The tab of `graph` shown (opened, in the center stack, when it is not open); its view. None: no such graph.
    GraphView* showGraph(const std::string& graph) {
        const fg::Graph* g = doc.script().findGraph(graph);
        if (!g)
            return nullptr;
        const std::string id = graphPrefix + graph;
        if (!views.contains(graph)) {
            std::unique_ptr<GraphView> view = makeGraphView(graph);
            views[graph] = view.get();
            // a function's tab says so, as the Qt editor's
            area->addPanel({.id = id, .title = glw::LocalizedText(g->kind == fg::GraphKind::Function ? "\u0192 " + graph : graph), .content = std::move(view)},
                           documents, glw::DockSide::Center);
        }
        if (glw::DockTabs* tabs = area->tabsOf(id))
            tabs->setActive(*tabs->indexOf(id));
        currentGraphName = graph;
        if (myScript)
            myScript->setCurrentGraph(graph);
        return views[graph];
    }

    GraphView* graphView(const std::string& graph) const {
        const auto it = views.find(graph);
        return it == views.end() ? nullptr : it->second;
    }

    // ---- running ----

    void makeRun() {
        if (!run)
            run = std::make_unique<core::RunSession>(doc, uiThread);
        run->stateChanged.connect([this](core::RunState state) {
            showRunState(state);
            if (state == core::RunState::Idle)
                launches.restore();         // the working directory and the variables of the run, put back
        });
        run->paused.connect([this](const std::string& graph, int node) {
            // the graph it stopped in shown, the node in view
            for (GraphView* g : graphViews())
                g->setDebugNode(0);
            if (GraphView* g = showGraph(graph)) {
                g->setDebugNode(fg::NodeId(node));
                g->showNode(fg::NodeId(node));
            }
            showPanel("debugger");
            status->showMessage(tr("Paused at '{graph}'").arg("graph", graph), 5s);
        });
        run->resumed.connect([this] {
            for (GraphView* g : graphViews())
                g->setDebugNode(0);
        });
        run->finished.connect([this](bool ok, const std::string& message) {
            for (GraphView* g : graphViews())
                g->setDebugNode(0);
            if (!message.empty())
                log->append(message);
            status->showMessage(ok ? tr("The run ended") : tr("The run failed: see the Output Log"), 4s);
        });
        run->logMessage.connect([this](int level, const std::string& text) {
            switch (fg::LogLevel(level)) {
            case fg::LogLevel::Warning: log->append(app.translator().resolve(tr("Warning: {text}").arg("text", text))); break;
            case fg::LogLevel::Error: log->append(app.translator().resolve(tr("Error: {text}").arg("text", text))); break;
            default: log->append(text); break;
            }
        });
        run->notice.connect([this](core::RunNotice notice) {
            switch (notice) {
            case core::RunNotice::CompileErrors: status->showMessage(tr("The script has errors: it cannot run"), 5s); break;
            case core::RunNotice::StoppedByEdit: status->showMessage(tr("The script was changed: the run was stopped"), 5s); break;
            }
        });
        run->linkFired.connect([this](const std::string& graph, int from, int out, int to) {
            if (GraphView* g = graphView(graph))
                g->fireLink(fg::NodeId(from), out, fg::NodeId(to));
        });
    }

    void startRun() {
        if (options.runEntry.empty()) {
            status->showMessage(tr("Nothing to run: the application has no event to start"), 4s);
            return;
        }
        std::vector<fg::Value> args = options.runArguments ? options.runArguments() : std::vector<fg::Value>{};
        int delay = speedMs;
        if (options.launchConfigurations) {
            // the configuration chosen: Main's arguments, its slow motion (unless the menu's speed says otherwise), its
            // working directory and variables while it runs
            const fg::standard::LaunchConfig& config = launches.current();
            args = {fg::standard::argsValue(rt.registry(), config.args)};
            if (delay == 0)
                delay = config.stepDelayMs;
            launches.apply(config);
        }
        run->setStepDelay(delay);
        log->setText("");
        if (!run->start(options.runEntry, args))
            launches.restore();
    }

    // ---- launch configurations ----

    void makeToolBar() {
        auto bar = std::make_unique<glw::ToolBar>();
        bar->setButtonSizes(34.0f, 22.0f);         // as the Qt editor's (its icons 22)
        bar->addAction(newAction);
        bar->addAction(openAction);
        bar->addAction(saveAction);
        bar->addSeparator();
        if (options.launchConfigurations) {
            auto box = std::make_unique<glw::ComboBox>();
            box->setToolTip(glw::ToolTip{.title = tr("Run:"), .text = tr("The launch configuration Run starts")});
            box->setOnChanged([this](std::size_t i) {
                if (i < launches.file().configs.size())
                    launches.select(launches.file().configs[i].name);
            });
            launchBox = &bar->addWidget(std::move(box));
            bar->addAction(configureAction);
        }
        bar->addAction(runAction);
        bar->addAction(pauseAction);
        bar->addAction(stopAction);
        bar->addSeparator();
        bar->addAction(stepOverAction);
        bar->addAction(stepIntoAction);
        bar->addAction(stepOutAction);
        window->setToolBar(std::move(bar));
        showLaunches();
    }

    // The configurations in the toolbar's box: each name, with Main's arguments.
    void showLaunches() {
        if (!launchBox)
            return;
        const fg::standard::LaunchFile& file = launches.file();
        std::vector<glw::LocalizedText> items;
        std::size_t current = 0;
        for (std::size_t i = 0; i < file.configs.size(); ++i) {
            std::string args;
            for (const std::string& a : file.configs[i].args)
                args += (args.empty() ? "" : " ") + a;
            items.emplace_back(file.configs[i].name + (args.empty() ? std::string() : "  (" + args + ")"));
            if (file.configs[i].name == file.selected)
                current = i;
        }
        launchBox->setItems(std::move(items));
        launchBox->setCurrent(current);
    }

    void editLaunches() {
        showLaunchConfigurations(*area, launches.file(), [this](fg::standard::LaunchFile file) {
            std::vector<std::string> problems;
            if (launches.setFile(std::move(file), &problems))
                doc.refresh();
            for (const std::string& p : problems)
                log->append(app.translator().resolve(tr("Warning: {text}").arg("text", p)));
            showLaunches();
            if (doc.path().empty())
                status->showMessage(tr("The launch configurations are saved with the script"), 4s);
        });
    }

    void showRunState(core::RunState state) {
        const bool active = state != core::RunState::Idle, paused = state == core::RunState::Paused;
        if (runAction) {
            runAction->setText(paused ? tr("&Continue") : tr("&Run"));
            runAction->setEnabled(!active || paused);
            pauseAction->setEnabled(active && !paused);
            stopAction->setEnabled(active);
            stepOverAction->setEnabled(paused);
            stepIntoAction->setEnabled(paused);
            stepOutAction->setEnabled(paused);
        }
        switch (state) {
        case core::RunState::Idle: runState->setText(tr("Idle")); break;
        case core::RunState::Running: runState->setText(tr("Running")); break;
        case core::RunState::Waiting: runState->setText(tr("Waiting")); break;
        case core::RunState::Paused: runState->setText(tr("Paused")); break;
        }
    }

    GraphView* currentGraph() const { return graphView(currentGraphName); }

    // The tab of the panel `id` shown.
    void showPanel(const std::string& id) {
        if (glw::DockTabs* tabs = area->tabsOf(id))
            if (const std::optional<std::size_t> index = tabs->indexOf(id))
                tabs->setActive(*index);
    }

    // A node in view, selected, in its graph (a comment's graph: shown).
    void focusNode(const std::string& graph, fg::NodeId node) {
        GraphView* g = showGraph(graph);
        if (!g || node == 0)
            return;
        g->select({node});
        g->showNode(node);
    }

    // A declaration that uses a struct, found: in the Details panel (a local's, a parameter's function shown).
    void showDeclaration(fg::ScriptRef::Declaration declaration, const std::string& graph, const std::string& name) {
        using D = fg::ScriptRef::Declaration;
        switch (declaration) {
        case D::Variable: details->showVariable({}, name); break;
        case D::Local:
            showGraph(graph);
            details->showVariable(graph, name);
            break;
        case D::Input:
        case D::Output:
            showGraph(graph);
            details->showGraph(graph);
            break;
        case D::Field: details->showStruct(name.substr(0, name.find('.'))); break;
        case D::None: return;
        }
        showPanel("details");
    }

    void showReferences(const fg::RefTarget& target) {
        find->findReferences(target);
        showPanel("findResults");
        status->showMessage(tr("{count} reference(s) of {what}").arg("count", find->resultCount()).arg("what", target.describe()), 4s);
    }

    // Shift+F12: what My Script has selected, when it has the focus; otherwise the node selected in the graph shown.
    void findReferencesOfSelection() {
        if (const std::optional<MyScriptPanel::Selection>& chosen = myScript->selection(); chosen && myScript->hasFocusInside()) {
            switch (chosen->member) {
            case core::Member::Function: showReferences({fg::RefTarget::Kind::Function, chosen->name, {}}); return;
            case core::Member::Struct: showReferences({fg::RefTarget::Kind::Struct, chosen->name, {}}); return;
            case core::Member::Variable:
            case core::Member::Local: showReferences({fg::RefTarget::Kind::Variable, chosen->name, chosen->owner}); return;
            case core::Member::EventGraph: break;
            }
        }
        GraphView* g = currentGraph();
        const fg::Graph* graph = g ? doc.graph(g->graphName()) : nullptr;
        const fg::Node* node = graph && g->selectedNodes().size() == 1 ? graph->findNode(*g->selectedNodes().begin()) : nullptr;
        if (!node) {
            status->showMessage(tr("Select a node, or a variable, function or struct in My Script, to find its references"), 4s);
            return;
        }
        showReferences(fg::referenceTargetOf(doc.script(), g->graphName(), *node));
    }

    void showCompileState(int errors, int warnings) {
        if (!compileState)
            return;
        compileState->setText(errors || warnings ? tr("{errors} error(s), {warnings} warning(s)").arg("errors", errors).arg("warnings", warnings) : tr("OK"));
        compileState->setColor(errors ? glw::Color{0.92f, 0.36f, 0.36f, 1} : warnings ? glw::Color{0.95f, 0.70f, 0.30f, 1} : glw::Color{0.35f, 0.80f, 0.50f, 1});
    }

    std::vector<GraphView*> graphViews() const {
        std::vector<GraphView*> all;
        for (const auto& [name, view] : views)
            all.push_back(view);
        return all;
    }

    // Every graph's tab closed (another script).
    void dropGraphs() {
        for (const auto& [name, view] : std::map<std::string, GraphView*>(views))
            closeGraph(name);
        currentGraphName.clear();
    }

    // The graphs open follow the script: a graph gone, its tab closed; a script just opened, its event graphs open.
    void showScript() {
        const fg::Script& script = doc.script();
        for (const auto& [name, view] : std::map<std::string, GraphView*>(views)) {
            if (!script.findGraph(name))
                closeGraph(name);
        }
        if (views.empty()) {
            for (const fg::Graph& g : script.graphs)
                if (g.kind == fg::GraphKind::Event)
                    showGraph(g.name);
        }
        if (!views.contains(currentGraphName))
            currentGraphName = views.empty() ? std::string() : views.begin()->first;
        myScript->setCurrentGraph(currentGraphName);
    }

    // ---- files ----

    void newScript() {
        doc.newScript(options.newScript);
        launches.scriptCreated();
        showLaunches();
        status->showMessage(tr("A new script"), 3s);
    }

    bool open(const std::string& given) {
        // by its whole path: what the document and the files opened lately keep (a command line's may be relative)
        std::error_code ec;
        const std::filesystem::path whole = std::filesystem::absolute(std::filesystem::path(std::u8string(given.begin(), given.end())), ec);
        const std::u8string text = (ec ? std::filesystem::path(std::u8string(given.begin(), given.end())) : whole.lexically_normal()).u8string();
        const std::string path(text.begin(), text.end());
        std::string error;
        if (!doc.load(path, &error)) {
            glw::showMessage(*area, tr("The script was not opened"), glw::LocalizedText(error), {tr("Close")});
            return false;
        }
        // its launch configurations, and the plugins they name (the script compiled again when they bring nodes)
        std::vector<std::string> problems;
        if (launches.scriptOpened(path, &problems))
            doc.refresh();
        for (const std::string& p : problems)
            log->append(app.translator().resolve(tr("Warning: {text}").arg("text", p)));
        showLaunches();
        addRecent(path);
        status->showMessage(tr("Opened {file}").arg("file", path), 3s);
        return true;
    }

    std::vector<glw::platform::FileFilter> filters() const {
        return {{app.translator().resolve(tr("FlowGraph scripts")), "flow"}};
    }

    std::string folderOfScript() const {
        if (doc.path().empty())
            return {};
        const std::filesystem::path p(std::u8string(doc.path().begin(), doc.path().end()));
        const std::u8string folder = p.parent_path().u8string();
        return {folder.begin(), folder.end()};
    }

    void chooseFile() {
        app.showFileDialog({.kind = glw::platform::FileDialogKind::Open,
                            .title = app.translator().resolve(tr("Open a Script")),
                            .location = folderOfScript(),
                            .filters = filters()},
                           [this](glw::platform::FileDialogResult result) {
                               if (result.failed())
                                   glw::showMessage(*area, tr("No file dialog"), glw::LocalizedText(result.error), {tr("Close")});
                               else if (!result.files.empty())
                                   open(result.files.front());
                           },
                           window);
    }

    // Saved, then `then`: into its file, or one chosen (Save As) when it has none.
    void save(std::function<void()> then) {
        if (doc.path().empty()) {
            saveAs(std::move(then));
            return;
        }
        saveTo(doc.path(), std::move(then));
    }

    void saveAs(std::function<void()> then) {
        std::string proposed = doc.path();
        if (proposed.empty())
            proposed = (doc.title().empty() ? doc.script().name : doc.title()) + ".flow";
        app.showFileDialog({.kind = glw::platform::FileDialogKind::Save,
                            .title = app.translator().resolve(tr("Save the Script")),
                            .location = proposed,
                            .filters = filters()},
                           [this, then = std::move(then)](glw::platform::FileDialogResult result) {
                               if (result.failed()) {
                                   glw::showMessage(*area, tr("No file dialog"), glw::LocalizedText(result.error), {tr("Close")});
                                   return;
                               }
                               if (result.files.empty())
                                   return;
                               std::string path = result.files.front();
                               if (std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
                                   path += ".flow";
                               saveTo(path, then);
                           },
                           window);
    }

    void saveTo(const std::string& path, const std::function<void()>& then) {
        std::string error;
        if (!doc.save(path, &error)) {
            glw::showMessage(*area, tr("The script was not saved"), glw::LocalizedText(error), {tr("Close")});
            return;
        }
        launches.scriptSaved(path);
        addRecent(path);
        status->showMessage(tr("Saved {file}").arg("file", path), 3s);
        if (then)
            then();
    }

    // `then` once what was not saved is saved, or the user said to leave it; nothing when the user cancels.
    void whenSaved(std::function<void()> then) {
        if (!doc.isDirty()) {
            then();
            return;
        }
        const std::string name = doc.path().empty() ? app.translator().resolve(tr("Untitled")) : fileName(doc.path());
        glw::showMessage(*area, tr("Save the changes?"),
                         tr("The changes to {name} are lost if they are not saved.").arg("name", name),
                         {tr("Save"), tr("Don't Save"), tr("Cancel")},
                         [this, then = std::move(then)](std::optional<std::size_t> button) {
                             if (button == 0u)
                                 save(then);
                             else if (button == 1u)
                                 then();
                         });
    }

    // Whether the window may close now; when not, it closes (by `close`) once what was not saved is saved or left.
    bool closeRequested(std::function<void()> close) {
        if (closing || !doc.isDirty())
            return true;
        whenSaved([this, close = std::move(close)] {
            closing = true;
            close();
        });
        return false;
    }

    void about() {
        glw::showMessage(*area, tr("About {application}").arg("application", options.applicationName),
                         tr("The editor and debugger of FlowGraph's standard applications, drawn by GLWidgets. A preview: it "
                            "takes the place of the Qt editor once it does all that one does."),
                         {tr("Close")});
    }
};

Studio::Studio(glw::Application& app, fg::Runtime& rt, StudioOptions options) : impl_(std::make_unique<Impl>(app, rt, std::move(options))) {
    impl_->loadSettings();
    impl_->applyLanguage();
    impl_->build();
    impl_->newScript();
    impl_->showTitle();
}

Studio::~Studio() = default;

core::Document& Studio::document() {
    return impl_->doc;
}

void Studio::newScript() {
    impl_->newScript();
}

bool Studio::open(const std::string& path) {
    return impl_->open(path);
}

void Studio::runScript() {
    if (!impl_->run->isActive())
        impl_->startRun();
}

} // namespace fged::gl
