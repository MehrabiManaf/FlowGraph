// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include <FlowGraphGLEditor/Studio.hpp>

#include "Graph/GraphView.hpp"

#include <GLWidgets/Platform/Platform.hpp>
#include <GLWidgets/Widgets/Action.hpp>
#include <GLWidgets/Widgets/Application.hpp>
#include <GLWidgets/Widgets/Declarative.hpp>
#include <GLWidgets/Widgets/Dialog.hpp>
#include <GLWidgets/Widgets/Docking.hpp>
#include <GLWidgets/Widgets/MenuBar.hpp>
#include <GLWidgets/Widgets/Settings.hpp>
#include <GLWidgets/Widgets/StatusBar.hpp>
#include <GLWidgets/Widgets/TabView.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
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

} // namespace

struct Studio::Impl {
    glw::Application& app;
    fg::Runtime& rt;
    StudioOptions options;
    core::Document doc;
    glw::Settings settings;

    glw::Window* window = nullptr;
    glw::DockArea* area = nullptr;
    glw::TabView* graphs = nullptr;
    glw::Widget* myScript = nullptr;            // the panel's content: made again as the script changes
    glw::StatusBar* status = nullptr;
    glw::Label* runState = nullptr;
    std::vector<std::string> shownGraphs;       // the graphs in the tabs, in their order

    std::shared_ptr<glw::Action> newAction, openAction, saveAction, saveAsAction, quitAction, undoAction, redoAction;
    std::shared_ptr<glw::Action> cutAction, copyAction, pasteAction, duplicateAction, selectAllAction, snapAction;
    bool closing = false;                       // asked whether to save, and it goes: the window may close now

    Impl(glw::Application& a, fg::Runtime& r, StudioOptions o) : app(a), rt(r), options(std::move(o)), doc(r) {}

    // ---- preferences ----

    void loadSettings() {
        const std::string folder = app.platform().preferencesFolder(options.organization, options.settingsName);
        if (folder.empty())
            return;
        settings.setPath(folder + "settings.json");
        settings.load();
        applyTheme(false);
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

        auto tabs = std::make_unique<glw::TabView>(glw::TabView::Props{.closable = false});
        graphs = tabs.get();
        area->setCenter(std::move(tabs));
        glw::Widget* center = area->center();

        auto scriptPanel = std::make_unique<glw::Widget>();
        myScript = scriptPanel.get();
        area->addPanel({.id = "myScript", .title = tr("My Script"), .content = ui::ScrollArea({}, std::move(scriptPanel)), .closable = false},
                       center, glw::DockSide::Start, 0.2f);
        area->addPanel({.id = "details",
                        .title = tr("Details"),
                        .content = ui::Label({.text = tr("Select a node, a variable or a function to see its details."), .muted = true, .wrap = true})
                                       .alignment(glw::Alignment::Stretch, glw::Alignment::Start)
                                       .padding(glw::Insets::all(12)),
                        .closable = false},
                       center, glw::DockSide::End, 0.24f);
        area->addPanel({.id = "output",
                        .title = tr("Output Log"),
                        .content = ui::Label({.text = tr("What a run prints shows here."), .muted = true})
                                       .alignment(glw::Alignment::Stretch, glw::Alignment::Start)
                                       .padding(glw::Insets::all(10)),
                        .closable = false},
                       center, glw::DockSide::Bottom, 0.24f);
        const std::string layout = settings.string("layout");
        if (!layout.empty())
            area->restoreLayout(layout);

        const glw::Size size{static_cast<float>(settings.number("windowWidth", 1280)), static_cast<float>(settings.number("windowHeight", 760))};
        window = &app.openWindow({.title = tr("FlowGraph Studio"), .size = size}, std::move(dock));
        status = &window->setStatusBar();
        status->setText(tr("Ready"));
        runState = &status->addPermanent(std::make_unique<glw::Label>(glw::Label::Props{.text = tr("Idle"), .fontSize = 12, .muted = true}));

        makeActions();
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
        snapAction->setChecked(settings.boolean("snapToGrid", false));
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
                       {.action = selectAllAction}}},
            {.text = tr("&View"),
             .items = {{.text = tr("&Frame Selection"), .onTriggered = [this] {
                            if (GraphView* g = currentGraph())
                                g->frameSelection();
                        }, .shortcut = tr("F")},
                       {.action = snapAction},
                       glw::MenuItem::separatorItem(),
                       {.text = tr("&Colors"), .items = std::move(colors)},
                       {.text = tr("&Style"), .items = std::move(looks)}}},
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
        return view;
    }

    GraphView* currentGraph() const {
        const std::optional<std::size_t> i = graphs->current();
        return i ? dynamic_cast<GraphView*>(graphs->content(*i)) : nullptr;
    }

    std::vector<GraphView*> graphViews() const {
        std::vector<GraphView*> views;
        for (std::size_t i = 0; i < graphs->count(); ++i) {
            if (auto* g = dynamic_cast<GraphView*>(graphs->content(i)))
                views.push_back(g);
        }
        return views;
    }

    void dropGraphs() {
        while (graphs->count() > 0)
            graphs->removeTab(graphs->count() - 1);
        shownGraphs.clear();
    }

    // A tab for each graph, its view kept while the graph is there (where it was zoomed and panned).
    void showGraphs() {
        const fg::Script& script = doc.script();
        std::vector<std::string> names;
        for (const fg::Graph& g : script.graphs)
            names.push_back(g.name);
        if (names == shownGraphs)
            return;
        const std::optional<std::size_t> current = graphs->current();
        const std::string shown = current && *current < shownGraphs.size() ? shownGraphs[*current] : std::string();
        std::map<std::string, std::unique_ptr<glw::Widget>> kept;
        while (graphs->count() > 0) {
            const std::size_t last = graphs->count() - 1;
            kept[shownGraphs[last]] = graphs->removeTab(last);
            shownGraphs.pop_back();
        }
        for (const fg::Graph& g : script.graphs) {
            std::unique_ptr<glw::Widget> view = std::move(kept[g.name]);
            if (!view)
                view = makeGraphView(g.name);
            // a function's tab says so, as the Qt editor's
            graphs->addTab(glw::LocalizedText(g.kind == fg::GraphKind::Function ? "\u0192 " + g.name : g.name), std::move(view));
            shownGraphs.push_back(g.name);
        }
        const auto it = std::find(shownGraphs.begin(), shownGraphs.end(), shown);
        if (it != shownGraphs.end())
            graphs->setCurrent(std::size_t(it - shownGraphs.begin()));
        else if (!shownGraphs.empty())
            graphs->setCurrent(0);
    }

    // The graphs in tabs, what the script has in My Script (its panel comes later).
    void showScript() {
        const fg::Script& script = doc.script();
        showGraphs();

        while (!myScript->children().empty())
            myScript->removeChild(*myScript->children().back());
        myScript->setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 4.0f));
        myScript->setPadding(glw::Insets::all(10));
        const auto section = [this](glw::LocalizedText title) {
            myScript->add(std::make_unique<glw::Label>(glw::Label::Props{.text = std::move(title), .muted = true}));
        };
        const auto entry = [this](const std::string& name) {
            myScript->add(std::make_unique<glw::Label>(glw::Label::Props{.text = glw::LocalizedText(name)})).setMargin({10, 0, 0, 0});
        };
        section(tr("Graphs"));
        for (const fg::Graph& g : script.graphs) {
            if (g.kind == fg::GraphKind::Event)
                entry(g.name);
        }
        section(tr("Functions"));
        for (const fg::Graph& g : script.graphs) {
            if (g.kind == fg::GraphKind::Function)
                entry(g.name);
        }
        section(tr("Variables"));
        for (const fg::VariableDecl& v : script.variables)
            entry(v.name);
        section(tr("Structs"));
        for (const fg::StructDecl& s : script.structs)
            entry(s.name);
    }

    // ---- files ----

    void newScript() {
        doc.newScript(options.newScript);
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

} // namespace fged::gl
