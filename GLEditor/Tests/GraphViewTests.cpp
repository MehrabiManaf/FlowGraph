// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// The graph of the GLWidgets editor, edited with the mouse and the keys: a root of GLWidgets without a window (no GPU), its
// events given to it as a window would.
#include "Graph/GraphView.hpp"
#include "Graph/NodeView.hpp"
#include "Language.hpp"
#include "Panels/DebugPanel.hpp"
#include "Panels/DetailsPanel.hpp"
#include "Panels/InfoPanels.hpp"
#include "Panels/MyScriptPanel.hpp"

#include <FlowGraph/Runtime.hpp>
#include <FlowGraph/StandardApp.hpp>
#include <FlowGraphEditorCore/GraphEdits.hpp>
#include <FlowGraphEditorCore/Launches.hpp>
#include <FlowGraph/Translations.hpp>
#include <GLWidgets/Paint/DrawList.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/GlyphAtlas.hpp>
#include <GLWidgets/Widgets/CollapsiblePanel.hpp>
#include <GLWidgets/Widgets/Layout.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/SearchField.hpp>
#include <GLWidgets/Widgets/SpinBox.hpp>
#include <GLWidgets/Widgets/TableView.hpp>
#include <GLWidgets/Widgets/ToolBar.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <deque>
#include <mutex>
#include <thread>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

// ---- a small test harness ----

struct TestCase {
    const char* name;
    void (*run)();
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}
int failures = 0;
struct Register {
    Register(const char* name, void (*run)()) { registry().push_back({name, run}); }
};
#define TEST(name)                                     \
    void test_##name();                                \
    const Register register_##name(#name, test_##name); \
    void test_##name()
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            ++failures;                                                               \
        }                                                                             \
    } while (false)

// ---- a window that is only a root ----

glw::UiContext& context();

// A popup (a menu, the search of nodes, a dialog): a root of its own, kept by the window's host until the test ends.
struct Popup;

struct Host : glw::UiHost {
    std::string clipboard;
    std::vector<std::unique_ptr<Popup>>* popups = nullptr;      // the window's (a popup's host shares them)
    bool closed = false;
    std::vector<std::unique_ptr<Popup>> own;
    Host() : popups(&own) {}
    glw::UiRoot* openPopup(glw::PopupRequest request) override;
    // The popups open now, in order.
    std::vector<Popup*> open() const;
    glw::platform::WindowState windowState() const override { return glw::platform::WindowState::Normal; }
    void minimizeWindow() override {}
    void toggleMaximizeWindow() override {}
    void closeWindow() override;
    void startTextInput() override {}
    void stopTextInput() override {}
    void setTextInputArea(const glw::Rect&) override {}
    std::string clipboardText() const override { return clipboard; }
    void setClipboardText(const std::string& text) override { clipboard = text; }
};

struct Popup {
    Host host;
    glw::UiRoot root;
    Popup(std::vector<std::unique_ptr<Popup>>* all, glw::PopupRequest request) : root(context(), &host) {
        host.popups = all;
        root.setContent(std::move(request.content));
        root.setSize(root.content()->measure({glw::unbounded, glw::unbounded}));
        root.layout();
    }
};

glw::UiRoot* Host::openPopup(glw::PopupRequest request) {
    popups->push_back(std::make_unique<Popup>(popups, std::move(request)));
    return &popups->back()->root;
}

void Host::closeWindow() {
    if (closed)
        return;
    closed = true;
    for (const auto& p : *popups) {
        if (&p->host == this)
            p->root.windowClosing();
    }
}

std::vector<Popup*> Host::open() const {
    std::vector<Popup*> result;
    for (const auto& p : *popups) {
        if (!p->host.closed)
            result.push_back(p.get());
    }
    return result;
}

glw::UiContext& context() {
    static glw::text::FontLibrary fonts;
    static glw::text::GlyphAtlas atlas(1, 256);
    static glw::Translator translator;
    static glw::Scheduler scheduler([] { return glw::TimePoint{}; });
    static glw::UiContext ui{fonts, atlas, translator, glw::Theme::dark(glw::ThemeStyle::Studio), nullptr, &scheduler, nullptr, nullptr};
    return ui;
}

using glw::Point;
using glw::platform::Key;
using glw::platform::KeyModifiers;
using glw::platform::MouseButton;

// A graph of an event, two Print nodes and a Branch, on a canvas filling a root, at zoom 1, its origin at (50, 150) of the
// root. The tests say points of the canvas: given to the root where they show.
struct Edited {
    fg::Runtime rt;
    std::unique_ptr<fged::core::Document> doc;
    Host host;
    glw::UiRoot root{context(), &host};
    fged::gl::GraphView* view = nullptr;
    fg::NodeId ev = 0, a = 0, b = 0, branch = 0;

    Edited() {
        rt.registry().addEvent("Go");
        doc = std::make_unique<fged::core::Document>(rt);
        fg::Script s;
        fg::Graph& g = s.addEventGraph();
        ev = g.addNode("event:Go", 0, 0).id;
        a = g.addNode("util.print", 300, 0).id;
        b = g.addNode("util.print", 300, 200).id;
        branch = g.addNode("flow.branch", 700, 0).id;
        doc->setScript(s, "Test");
        auto v = std::make_unique<fged::gl::GraphView>(*doc, "EventGraph");
        view = v.get();
        root.setContent(std::move(v));
        root.setSize({1400, 1000});
        root.layout();
        view->setZoom(1);
        view->setOrigin({50, 150});
        root.layout();
    }

    const fg::Graph& graph() const { return *doc->graph("EventGraph"); }
    const fg::Node& node(fg::NodeId id) const { return *graph().findNode(id); }
    // The middle of a pin of a node, on the canvas.
    Point pin(fg::NodeId id, const std::string& name, fg::PinDir dir) const {
        const fged::gl::NodeView& v = *view->nodeView(id);
        return view->itemPosition(v) + *v.pinCenter(name, dir);
    }
    // The middle of the box of an input's value, on the canvas.
    Point value(fg::NodeId id, const std::string& pin) const {
        const fged::gl::NodeView& v = *view->nodeView(id);
        return view->itemPosition(v) + v.valueRect(pin)->center();
    }
    // A point of a node's header.
    Point header(fg::NodeId id) const { return view->itemPosition(*view->nodeView(id)) + Point{12, 8}; }

    Point shown(Point canvas) const { return view->fromCanvas(canvas); }
    void press(Point p, KeyModifiers m = KeyModifiers::None, int clicks = 1) {
        root.dispatch(glw::platform::KeyChanged{1, Key::Unknown, m, false, false});
        root.dispatch(glw::platform::MouseMoved{1, shown(p)});
        root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, true, clicks, shown(p)});
    }
    void move(Point p) { root.dispatch(glw::platform::MouseMoved{1, shown(p)}); }
    void release(Point p) {
        root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, false, 1, shown(p)});
        root.layout();
    }
    void drag(Point from, Point to, KeyModifiers m = KeyModifiers::None) {
        press(from, m);
        move({(from.x + to.x) / 2, (from.y + to.y) / 2});
        move(to);
        release(to);
    }
    void click(Point p, KeyModifiers m = KeyModifiers::None) {
        press(p, m);
        release(p);
    }
    void key(Key k, KeyModifiers m = KeyModifiers::None) { root.dispatch(glw::platform::KeyChanged{1, k, m, true, false}); }
    void type(const std::string& text, glw::UiRoot* into = nullptr) { (into ? *into : root).dispatch(glw::platform::TextInput{1, text}); }
    static void key(glw::UiRoot& r, Key k) { r.dispatch(glw::platform::KeyChanged{1, k, KeyModifiers::None, true, false}); }
    std::size_t links() const { return graph().connections.size(); }
};

} // namespace

TEST(nodesAreSelectedAndMovedTogether) {
    Edited e;
    e.click(e.header(e.a));
    CHECK(e.view->selectedNodes() == std::set<fg::NodeId>{e.a});
    // Ctrl adds one; dragged, both move by as much, one step
    e.click(e.header(e.b), glw::platform::shortcutModifier);
    CHECK((e.view->selectedNodes() == std::set<fg::NodeId>{e.a, e.b}));
    e.drag(e.header(e.a), e.header(e.a) + Point{100, 50});
    CHECK(e.node(e.a).x == 400 && e.node(e.a).y == 50);
    CHECK(e.node(e.b).x == 400 && e.node(e.b).y == 250);
    CHECK(e.doc->undoLabel() == "Move");
    e.doc->undo();
    CHECK(e.node(e.a).x == 300 && e.node(e.b).y == 200);
    // a click without moving moves nothing
    e.click(e.header(e.a));
    CHECK(!e.doc->canUndo() || e.doc->undoLabel() != "Move");
    CHECK(e.view->selectedNodes() == std::set<fg::NodeId>{e.a});
}

TEST(aRectangleSelectsWhatItTouches) {
    Edited e;
    e.drag({250, 10}, {700, 400});
    CHECK((e.view->selectedNodes() == std::set<fg::NodeId>{e.a, e.b}));
    // on the empty canvas, a click: nothing selected
    e.click({900, 700});
    CHECK(e.view->selectedNodes().empty());
}

TEST(pinsAreLinkedByDraggingAndUnlinkedByAltAndAClick) {
    Edited e;
    e.drag(e.pin(e.ev, "Then", fg::PinDir::Out), e.pin(e.a, "In", fg::PinDir::In));
    CHECK(e.links() == 1 && e.graph().connections[0].toNode == e.a);
    CHECK(e.doc->undoLabel() == "Connect pins");
    // from an input to an output, too
    e.drag(e.pin(e.b, "In", fg::PinDir::In), e.pin(e.a, "Then", fg::PinDir::Out));
    CHECK(e.links() == 2);
    // what cannot be: told, nothing linked
    std::vector<std::string> told;
    e.view->setOnMessage([&](glw::LocalizedText t) { told.push_back(t.source); });
    e.drag(e.pin(e.a, "In", fg::PinDir::In), e.pin(e.b, "In", fg::PinDir::In));
    CHECK(e.links() == 2 && told.size() == 1 && told[0] == "Connect an output pin to an input pin");
    // let go on the empty canvas: nothing
    e.drag(e.pin(e.a, "Then", fg::PinDir::Out), {900, 700});
    CHECK(e.links() == 2);
    // Alt and a click on a pin: its links gone
    e.click(e.pin(e.a, "In", fg::PinDir::In), KeyModifiers::Alt);
    CHECK(e.links() == 1 && e.doc->undoLabel() == "Break links");
}

TEST(theSelectionIsDeletedCopiedAndPasted) {
    Edited e;
    e.drag(e.pin(e.ev, "Then", fg::PinDir::Out), e.pin(e.a, "In", fg::PinDir::In));
    // the event and a Print selected, Delete: the Print and its link go, the event stays (told)
    std::vector<std::string> told;
    e.view->setOnMessage([&](glw::LocalizedText t) { told.push_back(t.source); });
    e.click(e.header(e.ev));
    e.click(e.header(e.a), glw::platform::shortcutModifier);
    e.key(Key::Delete);
    CHECK(!e.graph().findNode(e.a) && e.graph().findNode(e.ev) && e.links() == 0);
    CHECK(told.size() == 1);
    e.doc->undo();
    CHECK(e.graph().findNode(e.a) && e.links() == 1);
    // copied (the clipboard of the window), pasted where the mouse is: selected
    e.click(e.header(e.b));
    e.view->copySelection();
    CHECK(fged::core::GraphEdits::isClip(e.host.clipboard));
    e.move({600, 500});
    const std::size_t nodes = e.graph().nodes.size();
    e.view->paste();
    CHECK(e.graph().nodes.size() == nodes + 1 && e.view->selectedNodes().size() == 1);
    const fg::NodeId pasted = *e.view->selectedNodes().begin();
    CHECK(e.node(pasted).x == 600 && e.node(pasted).y == 500);
    e.view->duplicateSelection();
    CHECK(e.graph().nodes.size() == nodes + 2);
}

TEST(aNodeMovedAloneSnapsToTheGridOrToItsLink) {
    Edited e;
    e.view->setSnapToGrid(true);
    e.drag(e.header(e.b), e.header(e.b) + Point{33, 47});
    CHECK(e.node(e.b).x == 340 && e.node(e.b).y == 240);
    // Alt: where it is let go
    e.drag(e.header(e.b), e.header(e.b) + Point{13, 7}, KeyModifiers::Alt);
    CHECK(e.node(e.b).x == 353 && e.node(e.b).y == 247);
    // linked: level with the pin it is linked to when near it
    e.drag(e.pin(e.ev, "Then", fg::PinDir::Out), e.pin(e.b, "In", fg::PinDir::In));
    const float y = e.pin(e.ev, "Then", fg::PinDir::Out).y;
    e.drag(e.header(e.b), {e.header(e.b).x + 40, e.header(e.b).y - (e.pin(e.b, "In", fg::PinDir::In).y - y) + 5});
    CHECK(std::abs(e.pin(e.b, "In", fg::PinDir::In).y - y) < 0.01f);
}

TEST(aLinkIsSelectedAndGetsAReroute) {
    Edited e;
    e.view->setSnapToGrid(false);
    // a link of data: a Print's string from... none here has one; a link of execution refuses a reroute
    e.drag(e.pin(e.ev, "Then", fg::PinDir::Out), e.pin(e.a, "In", fg::PinDir::In));
    const Point from = e.pin(e.ev, "Then", fg::PinDir::Out), to = e.pin(e.a, "In", fg::PinDir::In);
    const Point middle{(from.x + to.x) / 2, (from.y + to.y) / 2};
    e.click(middle);
    std::vector<std::string> told;
    e.view->setOnMessage([&](glw::LocalizedText t) { told.push_back(t.source); });
    e.key(Key::Delete);
    CHECK(e.links() == 0);
    e.doc->undo();
    e.press(middle, KeyModifiers::None, 2);
    e.release(middle);
    CHECK(e.links() == 1 && told.size() == 1);
}

TEST(valuesAreTypedToggledAndChosenOnTheNode) {
    Edited e;
    // the box of a text: typed in, Enter takes it (one step)
    e.click(e.value(e.a, "String"));
    e.type("hello");
    e.key(Key::Enter);
    e.root.layout();
    CHECK(e.node(e.a).defaults.at("String").str() == "hello");
    CHECK(e.doc->undoLabel() == "Edit value");
    // Escape: not taken
    e.click(e.value(e.a, "String"));
    e.type("other");
    e.key(Key::Escape);
    e.root.layout();
    CHECK(e.node(e.a).defaults.at("String").str() == "hello");
    // another click takes it (the box loses the focus)
    e.click(e.value(e.b, "String"));
    e.type("typed");
    e.click({900, 700});
    e.root.layout();
    CHECK(e.node(e.b).defaults.at("String").str() == "typed");
    // a check box: toggled by a click
    e.click(e.value(e.branch, "Condition"));
    CHECK(e.node(e.branch).defaults.at("Condition").toBool());
    e.click(e.value(e.branch, "Condition"));
    CHECK(!e.node(e.branch).defaults.at("Condition").toBool());
}

// The box of a number takes only what a number may be on its way: an int's digits (a sign before them).
TEST(theBoxOfANumberTakesOnlyANumber) {
    Edited e;
    fg::NodeId loop = 0;
    e.doc->edit("x", [&](fg::Script& s) { loop = s.graphs[0].addNode("flow.for", 300, 500).id; });
    e.root.layout();
    e.click(e.value(loop, "Last"));
    e.key(Key::Backspace);                  // (its text selected: gone)
    for (const char* typed : {"-", "1", "x", "2", ".", "5"})       // one at a time, as typed: the letter and the point refused
        e.type(typed);
    e.key(Key::Enter);
    e.root.layout();
    CHECK(e.node(loop).defaults.at("Last").toInt() == -125);
}

TEST(nodesAreAddedBySearchingThem) {
    Edited e;
    // Space, where the mouse is: every node; typed, the best one taken by Enter
    e.move({500, 500});
    e.click({500, 500});
    e.key(Key::Space);
    CHECK(e.host.open().size() == 1);
    if (e.host.open().empty())
        return;
    glw::UiRoot& search = e.host.open()[0]->root;
    e.type("branch", &search);
    Edited::key(search, Key::Enter);
    e.root.layout();
    CHECK(e.graph().nodes.size() == 5 && e.view->selectedNodes().size() == 1);
    const fg::NodeId made = *e.view->selectedNodes().begin();
    CHECK(e.node(made).def == "flow.branch" && e.doc->undoLabel() == "Add node");
    // a link let go on the empty canvas: those that fit it, the one added linked to it
    e.drag(e.pin(e.ev, "Then", fg::PinDir::Out), {500, 650});
    CHECK(e.host.open().size() == 1);
    if (e.host.open().empty())
        return;
    glw::UiRoot& fits = e.host.open()[0]->root;
    e.type("print string", &fits);
    Edited::key(fits, Key::Enter);
    e.root.layout();
    CHECK(e.graph().nodes.size() == 6 && e.links() == 1);
    const fg::Connection& c = e.graph().connections.at(0);
    CHECK(c.fromNode == e.ev && c.fromPin == "Then" && e.node(c.toNode).def == "util.print");
}

TEST(theSearchFindsNodesByTheirKeywords) {
    Edited e;
    e.move({500, 500});
    e.click({500, 500});
    e.key(Key::Space);
    CHECK(e.host.open().size() == 1);
    if (e.host.open().empty())
        return;
    // "+": Add (its keyword), not a title with + in it
    glw::UiRoot& search = e.host.open()[0]->root;
    e.type("+", &search);
    Edited::key(search, Key::Enter);
    e.root.layout();
    CHECK(e.view->selectedNodes().size() == 1);
    if (!e.view->selectedNodes().empty())
        CHECK(e.node(*e.view->selectedNodes().begin()).def == "math.add");
}

TEST(commentsAreAddedAroundTheSelectionAndNamed) {
    Edited e;
    e.click(e.header(e.a));
    e.click(e.header(e.b), glw::platform::shortcutModifier);
    e.view->addComment({0, 0});
    CHECK(e.graph().comments.size() == 1);
    if (e.graph().comments.empty())
        return;
    const fg::CommentBox c = e.graph().comments[0];
    CHECK(c.x < 300 && c.y < 0 && c.x + c.w > 400 && c.y + c.h > 250);
    // its header clicked twice: a dialog for its text
    e.root.layout();
    const Point header{float(c.x) + 40, float(c.y) + 10};
    e.press(header);
    e.release(header);
    e.press(header, KeyModifiers::None, 2);
    e.release(header);
    CHECK(e.host.open().size() == 1);
    if (e.host.open().empty())
        return;
    glw::UiRoot& dialog = e.host.open()[0]->root;
    dialog.layout();
    e.type(" of Prints", &dialog);
    Edited::key(dialog, Key::Enter);
    CHECK(e.graph().comments[0].text == "Comment of Prints");
}

TEST(theBoxAndItsNodeFollowWhatIsTypedAndASpaceIsTyped) {
    Edited e;
    const fged::gl::NodeView& node = *e.view->nodeView(e.a);
    const float boxBefore = node.valueRect("String")->width, nodeBefore = node.size().width;
    e.click(e.value(e.a, "String"));
    // the box typed in is as wide as the value's box
    CHECK(e.host.open().empty());
    e.type("a long text, typed with spaces");
    e.root.dispatch(glw::platform::KeyChanged{1, Key::Space, KeyModifiers::None, true, false});
    e.root.layout();
    // a space is the field's: no search of nodes
    CHECK(e.host.open().empty());
    // while it is typed, before it is taken: the box and the node wider
    CHECK(node.valueRect("String")->width > boxBefore + 40);
    CHECK(node.size().width > nodeBefore + 40);
    CHECK(!e.node(e.a).defaults.contains("String"));       // not taken yet
    // Escape: as it was
    e.key(Key::Escape);
    e.root.layout();
    CHECK(node.valueRect("String")->width == boxBefore);
}

TEST(aRunLightsTheLinksItTakesAndANodeClickedTwiceIsOpened) {
    Edited e;
    e.drag(e.pin(e.ev, "Then", fg::PinDir::Out), e.pin(e.a, "In", fg::PinDir::In));
    // the link taken: the node it goes to flashes; painted while lit
    e.view->fireLink(e.ev, 0, e.a);
    CHECK(e.view->nodeView(e.a)->flashing() && !e.view->nodeView(e.b)->flashing());
    glw::paint::DrawList list;
    e.root.paint(list);
    // an output that is not there: nothing
    e.view->fireLink(e.ev, 7, e.b);
    CHECK(!e.view->nodeView(e.b)->flashing());
    // clicked twice: told which
    std::vector<fg::NodeId> opened;
    e.view->setOnNodeActivated([&](fg::NodeId id) { opened.push_back(id); });
    e.press(e.header(e.b));
    e.release(e.header(e.b));
    e.press(e.header(e.b), KeyModifiers::None, 2);
    e.release(e.header(e.b));
    CHECK(opened == std::vector<fg::NodeId>{e.b});
    // while paused: a pin's value with its tooltip (asked of the run)
    e.view->setPinValues([](fg::NodeId, const std::string& pin, std::string& out) {
        out = "42";
        return pin == "String";
    });
    std::string value;
    CHECK(e.view->pinValue(e.a, "String", value) && value == "42");
}

namespace {

// My Script beside a graph, in one root (1400 by 900): the panel 320 wide at the start.
struct Listed {
    fg::Runtime rt;
    std::unique_ptr<fged::core::Document> doc;
    Host host;
    glw::UiRoot root{context(), &host};
    fged::gl::MyScriptPanel* panel = nullptr;
    fged::gl::GraphView* view = nullptr;

    Listed() {
        rt.registry().addEvent("Go");
        doc = std::make_unique<fged::core::Document>(rt);
        fg::Script s;
        s.addEventGraph().addNode("event:Go", 0, 0);
        s.variables.push_back({"Speed", "float", fg::Value(1.5), "", ""});
        s.addFunction("Jump");
        doc->setScript(s, "Test");
        auto row = std::make_unique<glw::Widget>();
        row->setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Horizontal));
        auto p = std::make_unique<fged::gl::MyScriptPanel>(*doc);
        p->setMinimumSize({320, 0});
        p->setMaximumSize({320, glw::unbounded});
        panel = &row->add(std::move(p));
        auto v = std::make_unique<fged::gl::GraphView>(*doc, "EventGraph");
        v->setStretch(1);
        view = &row->add(std::move(v));
        root.setContent(std::move(row));
        root.setSize({1400, 900});
        root.layout();
        view->setZoom(1);
        view->setOrigin({0, 0});
        root.layout();
    }
    Point middleOf(const glw::Widget& w) const { return w.mapToRoot({w.size().width / 2, w.size().height / 2}); }
    void press(Point p, KeyModifiers m = KeyModifiers::None, int clicks = 1) {
        root.dispatch(glw::platform::KeyChanged{1, Key::Unknown, m, false, false});
        root.dispatch(glw::platform::MouseMoved{1, p});
        root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, true, clicks, p});
    }
    void move(Point p) { root.dispatch(glw::platform::MouseMoved{1, p}); }
    void release(Point p) {
        root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, false, 1, p});
        root.layout();
    }
    void click(Point p) {
        press(p);
        release(p);
    }
    // The + buttons of the sections, in order (Functions, Variables, Local Variables, Structs).
    std::vector<glw::Widget*> plusButtons() const {
        std::vector<glw::Widget*> found;
        std::function<void(glw::Widget&)> walk = [&](glw::Widget& w) {
            if (dynamic_cast<glw::ToolButton*>(&w) && w.visibleInTree())
                found.push_back(&w);
            for (const auto& c : w.children())
                walk(*c);
        };
        walk(*panel);
        return found;
    }
};

} // namespace

TEST(myScriptListsSelectsAddsAndRenames) {
    Listed l;
    glw::Widget* speed = l.panel->rowOf(fged::core::Member::Variable, "", "Speed");
    CHECK(speed && l.panel->rowOf(fged::core::Member::Function, "", "Jump"));
    CHECK(!l.panel->rowOf(fged::core::Member::EventGraph, "", "EventGraph"));      // its events, not the graph
    if (!speed)
        return;
    // a click selects it
    l.click(l.middleOf(*speed));
    CHECK(l.panel->selection() && l.panel->selection()->name == "Speed");
    // the + of Functions: a function added, its name typed at once
    const std::vector<glw::Widget*> plus = l.plusButtons();
    CHECK(plus.size() == 3);        // Local Variables hidden: the graph shown is no function
    if (plus.empty())
        return;
    l.click(l.middleOf(*plus[0]));
    CHECK(l.doc->script().findGraph("NewFunction") && l.doc->undoLabel() == "Add function");
    l.root.dispatch(glw::platform::TextInput{1, "Land"});
    l.root.dispatch(glw::platform::KeyChanged{1, Key::Enter, KeyModifiers::None, true, false});
    l.root.layout();
    CHECK(l.doc->script().findGraph("Land") && !l.doc->script().findGraph("NewFunction"));
    CHECK(l.panel->rowOf(fged::core::Member::Function, "", "Land"));
    // a name taken: told, nothing renamed
    std::vector<std::string> told;
    l.panel->setOnMessage([&](glw::LocalizedText t) { told.push_back(t.source); });
    l.click(l.middleOf(*plus[1]));
    l.root.dispatch(glw::platform::TextInput{1, "Speed"});
    l.root.dispatch(glw::platform::KeyChanged{1, Key::Enter, KeyModifiers::None, true, false});
    l.root.layout();
    CHECK(told.size() == 1 && l.doc->script().findVariable("NewVar"));
    // the button beside the search: a menu of what can be added (no local variable: the graph shown is no function)
    glw::Widget* adds = l.panel->addButton();
    CHECK(adds && adds->size().height > 20);
    if (!adds)
        return;
    l.click(l.middleOf(*adds));
    CHECK(l.host.open().size() == 1);
    if (l.host.open().empty())
        return;
    glw::UiRoot& menu = l.host.open()[0]->root;
    for (Key k : {Key::Down, Key::Down, Key::Down, Key::Enter})          // Function, Variable, Struct
        menu.dispatch(glw::platform::KeyChanged{1, k, KeyModifiers::None, true, false});
    l.root.layout();
    CHECK(l.doc->undoLabel() == "Add struct");
}

TEST(aVariableOrAFunctionDraggedOntoTheGraphMakesANode) {
    Listed l;
    const std::size_t nodes = l.doc->graph("EventGraph")->nodes.size();
    // a variable with Ctrl: a Get where it is let go
    glw::Widget* speed = l.panel->rowOf(fged::core::Member::Variable, "", "Speed");
    if (!speed)
        return;
    const Point from = l.middleOf(*speed);
    l.press(from);
    l.move({from.x + 20, from.y});
    l.move({700, 400});
    CHECK(l.root.dragging());
    l.root.dispatch(glw::platform::KeyChanged{1, Key::Control, glw::platform::shortcutModifier, true, false});
    l.release({700, 400});
    const fg::Graph& g = *l.doc->graph("EventGraph");
    CHECK(g.nodes.size() == nodes + 1);
    if (g.nodes.size() == nodes + 1) {
        const fg::Node& made = g.nodes.back();
        CHECK(made.def == "var.get" && made.props.at("name") == "Speed");
        const Point canvas = l.view->toCanvas(l.view->mapFromRoot({700, 400}));
        CHECK(std::abs(made.x - canvas.x) < 1 && std::abs(made.y - canvas.y) < 1);
    }
    // a function: a call of it
    glw::Widget* jump = l.panel->rowOf(fged::core::Member::Function, "", "Jump");
    if (!jump)
        return;
    const Point f = l.middleOf(*jump);
    l.root.dispatch(glw::platform::KeyChanged{1, Key::Control, KeyModifiers::None, false, false});
    l.press(f);
    l.move({f.x + 20, f.y});
    l.move({800, 500});
    l.release({800, 500});
    CHECK(l.doc->graph("EventGraph")->nodes.size() == nodes + 2 && l.doc->graph("EventGraph")->nodes.back().def == "func.call");
}

TEST(myScriptsTablesRenameByF2AndOpenByADoubleClick) {
    Listed l;
    glw::Widget* speed = l.panel->rowOf(fged::core::Member::Variable, "", "Speed");
    CHECK(speed != nullptr);
    if (!speed)
        return;
    // F2 on the member chosen: its name typed in its cell
    l.click(l.middleOf(*speed));
    l.root.dispatch(glw::platform::KeyChanged{1, Key::F2, KeyModifiers::None, true, false});
    l.root.layout();
    l.root.dispatch(glw::platform::TextInput{1, "Velocity"});
    l.root.dispatch(glw::platform::KeyChanged{1, Key::Enter, KeyModifiers::None, true, false});
    l.root.layout();
    CHECK(l.doc->script().findVariable("Velocity") && !l.doc->script().findVariable("Speed"));
    CHECK(l.panel->selection() && l.panel->selection()->name == "Velocity");
    // a function clicked twice: its graph opened
    std::vector<std::string> opened;
    l.panel->setOnOpenGraph([&](const std::string& g) { opened.push_back(g); });
    glw::Widget* jump = l.panel->rowOf(fged::core::Member::Function, "", "Jump");
    if (!jump)
        return;
    const Point at = l.middleOf(*jump);
    l.press(at);
    l.release(at);
    l.press(at, KeyModifiers::None, 2);
    l.release(at);
    CHECK(opened == std::vector<std::string>{"Jump"});
    CHECK(l.panel->selection() && l.panel->selection()->name == "Jump");
}

namespace {

// The Details panel alone in a root (500 by 900), and a script with a bit of everything.
struct Detailed {
    fg::Runtime rt;
    std::unique_ptr<fged::core::Document> doc;
    Host host;
    glw::UiRoot root{context(), &host};
    fged::gl::DetailsPanel* panel = nullptr;
    fg::NodeId format = 0, sequence = 0;
    std::vector<std::string> messages;

    Detailed() {
        rt.registry().addEvent("Go");
        doc = std::make_unique<fged::core::Document>(rt);
        fg::Script s;
        fg::Graph& g = s.addEventGraph();
        g.addNode("event:Go", 0, 0);
        format = g.addNode("string.format", 200, 0).id;
        sequence = g.addNode("flow.sequence", 400, 0).id;
        s.variables.push_back({"Speed", "float", fg::Value(1.5), "", ""});
        s.addFunction("Jump", {{"Height", "float", false, false, fg::Value()}});
        s.structs.push_back({"Point", "", "", {{"X", "float", fg::Value(), ""}, {"Y", "float", fg::Value(), ""}}});
        doc->setScript(s, "Test");
        auto p = std::make_unique<fged::gl::DetailsPanel>(*doc);
        panel = p.get();
        panel->setOnMessage([this](glw::LocalizedText t) { messages.push_back(t.source); });
        root.setContent(std::move(p));
        root.setSize({500, 900});
        root.layout();
    }
    Point middleOf(const glw::Widget& w) const { return w.mapToRoot({w.size().width / 2, w.size().height / 2}); }
    // What the panel posted for after the event (it made again), done.
    void settle() {
        context().scheduler->tick();
        root.layout();
    }
    void click(Point p) {
        root.dispatch(glw::platform::MouseMoved{1, p});
        root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, true, 1, p});
        root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, false, 1, p});
        settle();
    }
    // `text` typed into a field (all it had replaced), then Enter.
    void type(const std::string& id, const std::string& text) {
        root.layout();
        glw::Widget* f = panel->field(id);
        CHECK(f != nullptr);
        if (!f)
            return;
        root.setFocus(f);
        static_cast<glw::LineEdit*>(f)->selectAll();
        root.dispatch(glw::platform::TextInput{1, text});
        root.dispatch(glw::platform::KeyChanged{1, Key::Enter, KeyModifiers::None, true, false});
        settle();
    }
    // The + buttons that show, in order.
    std::vector<glw::Widget*> plusButtons() const {
        std::vector<glw::Widget*> found;
        std::function<void(glw::Widget&)> walk = [&](glw::Widget& w) {
            if (dynamic_cast<glw::ToolButton*>(&w) && w.visibleInTree())
                found.push_back(&w);
            for (const auto& c : w.children())
                walk(*c);
        };
        walk(*panel);
        return found;
    }
};

} // namespace

TEST(detailsEditAVariable) {
    Detailed d;
    d.panel->showVariable("", "Speed");
    d.root.layout();
    CHECK(d.panel->mode() == fged::gl::DetailsPanel::Mode::Variable);
    CHECK(d.panel->field("name") && d.panel->field("type") && d.panel->field("default") && !d.panel->field("key"));
    d.type("category", "Movement");
    d.type("default", "2.5");
    CHECK(d.doc->script().findVariable("Speed")->category == "Movement");
    CHECK(d.doc->script().findVariable("Speed")->def.toFloat() == 2.5);
    d.type("name", "Velocity");
    CHECK(d.doc->script().findVariable("Velocity") && d.panel->mode() == fged::gl::DetailsPanel::Mode::Variable);
    CHECK(d.panel->field("name") && static_cast<glw::LineEdit*>(d.panel->field("name"))->text() == "Velocity");
    // a name taken: told, not renamed
    d.doc->edit("v", [](fg::Script& s) { s.variables.push_back({"Other", "int", fg::Value(int64_t(0)), "", ""}); });
    d.settle();
    d.type("name", "Other");
    CHECK(d.doc->script().findVariable("Velocity") && !d.messages.empty());
    // the search (the panel's first child): only the rows with the words
    d.root.setFocus(nullptr);
    static_cast<glw::SearchField&>(*d.panel->children()[0]).setText("categ");
    d.panel->rebuild();
    d.root.layout();
    glw::Widget* category = d.panel->field("category");
    glw::Widget* name = d.panel->field("name");
    CHECK(category && category->visible() && name && !name->visible());
    // deleted elsewhere: nothing shown
    d.doc->edit("x", [](fg::Script& s) { s.variables.clear(); });
    d.settle();
    CHECK(d.panel->mode() == fged::gl::DetailsPanel::Mode::None);
}

TEST(detailsEditAFunctionsParameters) {
    Detailed d;
    d.panel->setClosedSections({"function/outputs"});
    d.panel->showGraph("Jump");
    d.root.layout();
    CHECK(d.panel->field("inputs") && d.panel->field("outputs"));
    const std::vector<glw::Widget*> plus = d.plusButtons();
    CHECK(plus.size() == 2);
    if (plus.size() != 2)
        return;
    // the + of Outputs, closed: an output added, the section opened to show it, animated
    const auto outputs = [&d]() -> glw::CollapsiblePanel* {
        for (glw::Widget* w = d.panel->field("outputs"); w; w = w->parent())
            if (auto* p = dynamic_cast<glw::CollapsiblePanel*>(w))
                return p;
        return nullptr;
    };
    CHECK(outputs() && !outputs()->expanded());
    d.click(d.middleOf(*plus[1]));
    const fg::Graph* jump = d.doc->script().findGraph("Jump");
    CHECK(jump->outputs.size() == 1 && jump->outputs[0].name == "Output");
    CHECK(outputs() && outputs()->expanded() && outputs()->openness() < 1);
    d.type("description", "Jumps");
    CHECK(d.doc->script().findGraph("Jump")->doc == "Jumps");
}

TEST(detailsEditAStructAndItsFields) {
    Detailed d;
    d.panel->showStruct("Point");
    d.root.layout();
    CHECK(d.panel->field("fields") && d.panel->field("default"));
    d.type("default", "3");
    CHECK(d.doc->script().findStruct("Point")->fields[0].def.toFloat() == 3);
    const std::vector<glw::Widget*> plus = d.plusButtons();
    CHECK(plus.size() == 1);
    if (plus.empty())
        return;
    d.click(d.middleOf(*plus[0]));
    CHECK(d.doc->script().findStruct("Point")->fields.size() == 3);
    d.type("fieldDescription", "the new one");
    CHECK(d.doc->script().findStruct("Point")->fields[2].doc == "the new one");
}

TEST(detailsEditANode) {
    Detailed d;
    d.panel->showNodes("EventGraph", {d.format});
    d.root.layout();
    CHECK(d.panel->field("fixedInputs") && d.panel->field("nodeInputs") && d.panel->field("comment"));
    // its own inputs: + adds one
    const std::vector<glw::Widget*> plus = d.plusButtons();
    CHECK(plus.size() == 1);
    if (!plus.empty())
        d.click(d.middleOf(*plus[0]));
    const fg::Node* n = d.doc->script().findGraph("EventGraph")->findNode(d.format);
    CHECK(n->inputs.size() == 1 && n->inputs[0].name == "Input");
    d.type("comment", "says hello");
    CHECK(d.doc->script().findGraph("EventGraph")->findNode(d.format)->comment == "says hello");
    // a node with a setting: a spin box
    d.panel->showNodes("EventGraph", {d.sequence});
    d.root.layout();
    auto* spin = dynamic_cast<glw::SpinBox*>(d.panel->field("setting:count"));
    CHECK(spin && spin->value() == 2);
}

TEST(propertyEditsKeepLinksAndValues) {
    fg::Runtime rt;
    fged::core::Document doc(rt);
    fg::Script s;
    s.addFunction("F", {{"A", "int", false, false, fg::Value()}, {"B", "int", false, false, fg::Value()}});
    fg::Graph& g = s.addEventGraph();
    fg::Node& call = g.addNode("func.call", 0, 0);
    call.props["function"] = "F";
    call.defaults["A"] = fg::Value(int64_t(7));
    const fg::NodeId callId = call.id;
    s.structs.push_back({"P", "", "", {{"X", "int", fg::Value(), ""}, {"Y", "float", fg::Value(), ""}}});
    doc.setScript(s, "T");
    fged::core::PropertyEdits edits(doc);
    // A and B swap their names: the value typed into the call's A follows it (B now)
    std::vector<fg::ParamDecl> params = doc.script().findGraph("F")->inputs;
    std::swap(params[0].name, params[1].name);
    CHECK(edits.setParams("F", true, params, "swap") == fged::core::ListProblem::None);
    const fg::Node* c = doc.script().findGraph("EventGraph")->findNode(callId);
    CHECK(c && c->defaults.count("B") && c->defaults.at("B").toInt() == 7 && !c->defaults.count("A"));
    // two of a name: refused
    params[1].name = params[0].name;
    CHECK(edits.setParams("F", true, params, "dup") == fged::core::ListProblem::DuplicateName);
    // fields renamed into each other's names, and retyped
    std::vector<fg::ParamDecl> fields{{"Y", "int", false, false, fg::Value()}, {"X", "string", false, false, fg::Value()}};
    CHECK(edits.setFields("P", fields, "f") == fged::core::ListProblem::None);
    const fg::StructDecl* p = doc.script().findStruct("P");
    CHECK(p->fields[0].name == "Y" && p->fields[0].type == "int" && p->fields[1].name == "X" && p->fields[1].type == "string");
    edits.moveField("P", 0, false, "m");
    CHECK(doc.script().findStruct("P")->fields[0].name == "X");
    CHECK(fged::core::TypeParts::parse("map<string,array<int>>").container == fged::core::TypeParts::Container::Map);
    CHECK(fged::core::TypeParts::parse("array<float>").str() == "array<float>");
    // undone in one step each
    doc.undo();
    CHECK(doc.script().findStruct("P")->fields[0].name == "Y");
}

TEST(compileResultsListTheErrorsAndShowWhere) {
    fg::Runtime rt;
    fged::core::Document doc(rt);
    fg::Script s;
    fg::Graph& g = s.addEventGraph();
    const fg::NodeId unknown = g.addNode("fn:No.SuchThing", 0, 0).id;
    doc.setScript(s, "T");
    Host host;
    glw::UiRoot root{context(), &host};
    auto p = std::make_unique<fged::gl::CompileResultsPanel>(doc);
    fged::gl::CompileResultsPanel& panel = *p;
    std::vector<std::pair<std::string, fg::NodeId>> shown;
    panel.setOnActivated([&](const std::string& graph, fg::NodeId node) { shown.emplace_back(graph, node); });
    root.setContent(std::move(p));
    root.setSize({800, 300});
    root.layout();
    CHECK(panel.errors() >= 1 && panel.rowCount() >= 1);
    // the row clicked twice: its node
    if (glw::Widget* cell = panel.table().cellWidget(0, 0)) {
        const Point at = cell->mapToRoot({cell->size().width / 2, cell->size().height / 2});
        for (int clicks : {1, 2}) {
            root.dispatch(glw::platform::MouseMoved{1, at});
            root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, true, clicks, at});
            root.dispatch(glw::platform::MouseButtonChanged{1, MouseButton::Left, false, clicks, at});
        }
    }
    CHECK(shown.size() == 1 && shown[0].first == "EventGraph" && shown[0].second == unknown);
    // fixed: none
    doc.edit("x", [](fg::Script& sc) { sc.graphs[0].nodes.clear(); });
    CHECK(panel.errors() == 0 && panel.rowCount() == 0);
}

TEST(findResultsSearchAndFindReferences) {
    fg::Runtime rt;
    fged::core::Document doc(rt);
    fg::Script s;
    s.variables.push_back({"Speed", "float", fg::Value(1.5), "", ""});
    fg::Graph& g = s.addEventGraph();
    fg::Node& get = g.addNode("var.get", 0, 0);
    get.props["name"] = "Speed";
    get.props["scope"] = "global";
    fg::Node& print = g.addNode("util.print", 200, 0);
    print.defaults["String"] = fg::Value(std::string("Hello there"));
    doc.setScript(s, "T");
    Host host;
    glw::UiRoot root{context(), &host};
    auto p = std::make_unique<fged::gl::FindPanel>(doc);
    fged::gl::FindPanel& panel = *p;
    root.setContent(std::move(p));
    root.setSize({800, 300});
    root.layout();
    CHECK(panel.resultCount() == 0);
    panel.search("there");
    CHECK(panel.resultCount() == 1);
    panel.findReferences({fg::RefTarget::Kind::Variable, "Speed", {}});
    CHECK(panel.resultCount() == 1);
    // they follow the edits
    doc.edit("x", [](fg::Script& sc) { sc.graphs[0].nodes.erase(sc.graphs[0].nodes.begin()); });
    CHECK(panel.resultCount() == 0);
}

namespace {

// The editor's thread of a test: what the run posts, done when pumped.
struct PumpedThread : fged::core::UiThread {
    std::mutex mu;
    std::deque<std::function<void()>> queue;
    std::function<void()> repeated;
    void post(std::function<void()> fn) override {
        std::lock_guard lk(mu);
        queue.push_back(std::move(fn));
    }
    void startRepeating(int, std::function<void()> fn) override { repeated = std::move(fn); }
    void stopRepeating() override { repeated = nullptr; }
    void pump() {
        std::deque<std::function<void()>> now;
        {
            std::lock_guard lk(mu);
            now.swap(queue);
        }
        for (auto& fn : now)
            fn();
        if (repeated)
            repeated();
    }
};

} // namespace

TEST(theDebuggerShowsTheStackVariablesAndWatchesOfAPausedRun) {
    fg::Runtime rt;
    rt.registry().addEvent("Go");
    fged::core::Document doc(rt);
    fg::Script s;
    s.variables.push_back({"Speed", "float", fg::Value(1.5), "", ""});
    fg::Graph& g = s.addEventGraph();
    const fg::NodeId go = g.addNode("event:Go", 0, 0).id;
    fg::Node& print = g.addNode("util.print", 200, 0);
    print.defaults["String"] = fg::Value(std::string("hello"));
    const fg::NodeId printId = print.id;
    g.connections.push_back({go, "Then", printId, "In"});
    doc.setScript(s, "T");
    doc.debugger().toggleBreakpoint("EventGraph", printId);
    PumpedThread ui;
    {
        fged::core::RunSession run(doc, ui);
        Host host;
        glw::UiRoot root{context(), &host};
        auto p = std::make_unique<fged::gl::DebugPanel>(doc, run);
        fged::gl::DebugPanel& panel = *p;
        std::vector<std::string> saved;
        panel.setWatchStore([](const std::string&) { return std::vector<std::string>{}; },
                            [&](const std::string&, const std::vector<std::string>& w) { saved = w; });
        root.setContent(std::move(p));
        root.setSize({400, 700});
        root.layout();
        // before a run: nothing, a watch not available
        CHECK(panel.stackDepth() == 0 && !panel.valueOf("Speed"));
        panel.addWatch("Speed");
        CHECK(panel.watches() == std::vector<std::string>{"Speed"});
        // paused at the breakpoint: the stack, the variable, the watch
        CHECK(run.start("Go", {}));
        for (int i = 0; i < 500 && run.state() != fged::core::RunState::Paused; ++i) {
            ui.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(run.state() == fged::core::RunState::Paused);
        root.layout();
        CHECK(panel.stackDepth() == 1);
        const std::optional<std::string> speed = panel.valueOf("Speed");
        CHECK(speed && speed->find("1.5") != std::string::npos);
        const std::optional<std::string> watched = panel.valueOf("Speed", true);
        CHECK(watched && watched->find("1.5") != std::string::npos);
        // a watch that is no expression: its error
        panel.addWatch("Nope");
        const std::optional<std::string> nope = panel.valueOf("Nope", true);
        CHECK(nope && !nope->empty() && nope->front() == '<');
        panel.removeWatch("Nope");
        CHECK(panel.watches() == std::vector<std::string>{"Speed"} && !panel.valueOf("Nope", true));
        run.stop();
        for (int i = 0; i < 50; ++i)
            ui.pump();
        root.setContent(nullptr);           // (the panel before the run it listens to)
    }
}

TEST(theTranslationsAreCompiledIn) {
    // the editor's catalogs, each language named in itself; the nodes' too
    const std::vector<std::string> languages = fged::gl::availableLanguages();
    CHECK(languages.size() >= 10 && languages.front() == "en");
    CHECK(fged::gl::languageName("de") == "Deutsch" && fged::gl::languageName("en") == "English");
    CHECK(fged::gl::resolveLanguage("fa-IR") == "fa" && fged::gl::resolveLanguage("zh-Hans-CN") == "zh_CN" && fged::gl::resolveLanguage("pt") == "pt_BR");
    CHECK(fged::gl::resolveLanguage("xx") == "en");
    // every catalog whole (they are compiled in in pieces: a piece cut inside a character broke them)
    fg::Runtime rt;
    const fg::NodeDef* def = rt.registry().findNode("flow.foreach");
    std::size_t nodes = 0;
    for (const fged::gl::EmbeddedCatalog& c : fged::gl::embeddedCatalogs()) {
        std::string error;
        if (std::string_view(c.kind) == "icon")
            continue;
        if (std::string_view(c.kind) == "editor") {
            glw::Translator t;
            const bool loaded = t.loadCatalog(c.text(), &error);
            if (!loaded)
                std::printf("  the editor's catalog of %s: %s\n", c.language, error.c_str());
            CHECK(loaded);
            continue;
        }
        fg::Translations t;
        const bool loaded = t.loadJson(c.text(), &error);
        if (!loaded)
            std::printf("  the nodes' catalog of %s: %s\n", c.language, error.c_str());
        CHECK(loaded && def && t.nodeDoc(*def) != def->doc);       // (a title may stay as it is: For Each in Japanese)
        ++nodes;
    }
    CHECK(nodes + 1 == languages.size());
}

TEST(launchConfigurationsAreKeptNextToTheScriptAndAppliedToARun) {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "fg_launches_test";
    fs::remove_all(folder);
    fs::create_directories(folder / "work");
    const std::string script = (folder / "a.flow").string();
    fg::Runtime rt;
    fged::core::Launches launches(rt);
    // a script without a launch file: one default configuration
    std::vector<std::string> problems;
    CHECK(!launches.scriptOpened(script, &problems) && problems.empty());
    CHECK(launches.file().configs.size() == 1);
    // edited: names made unique, saved next to the script
    fg::standard::LaunchFile file;
    file.configs.push_back({.name = "Fast", .args = {"one", "two"}, .cwd = "work", .env = {{"FG_LAUNCH_TEST", "yes"}}});
    file.configs.push_back({.name = "Fast"});
    file.selected = "Fast";
    launches.setFile(file);
    CHECK(launches.file().configs[1].name != "Fast" && fs::exists(folder / "a.flowlaunch"));
    launches.select(launches.file().configs[1].name);
    // read again, as another editor would
    fged::core::Launches again(rt);
    again.scriptOpened(script);
    CHECK(again.file().configs.size() == 2 && again.file().selected == launches.file().configs[1].name);
    // a run: its working directory and its variables, then as they were
    const fs::path before = fs::current_path();
    launches.apply(launches.file().configs[0]);
    CHECK(fs::equivalent(fs::current_path(), folder / "work"));
    const auto variable = [](const char* name) -> std::optional<std::string> {
#ifdef _WIN32
        char* value = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&value, &size, name) != 0 || !value)
            return std::nullopt;
        std::string v = value;
        std::free(value);
        return v;
#else
        const char* value = std::getenv(name);
        return value ? std::optional<std::string>(value) : std::nullopt;
#endif
    };
    CHECK(variable("FG_LAUNCH_TEST") == "yes");
    launches.restore();
    CHECK(fs::equivalent(fs::current_path(), before) && !variable("FG_LAUNCH_TEST"));
    // saved as another file: the configurations go with it
    launches.scriptSaved((folder / "b.flow").string());
    CHECK(fs::exists(folder / "b.flowlaunch"));
    fs::remove_all(folder);
}

TEST(nodesAreCollapsedIntoAFunctionOrNothingIsDone) {
    fg::Runtime rt;
    rt.registry().addEvent("Go");
    fged::core::Document doc(rt);
    fg::Script s;
    fg::Graph& g = s.addEventGraph();
    const fg::NodeId go = g.addNode("event:Go", 0, 0).id;
    const fg::NodeId a = g.addNode("util.print", 200, 0).id;
    const fg::NodeId b = g.addNode("util.print", 400, 0).id;
    g.connections.push_back({go, "Then", a, "In"});
    g.connections.push_back({a, "Then", b, "In"});
    doc.setScript(s, "T");
    fged::core::GraphEdits edits(doc, "EventGraph");
    // an event cannot be moved: nothing done, no step to undo
    const fg::CollapseResult refused = edits.collapse({go, a}, "Greet", "Collapse");
    CHECK(!refused.ok && !refused.error.empty() && !doc.canUndo());
    // two prints: a function of them, a call where they were, one step
    const fg::CollapseResult done = edits.collapse({a, b}, "Greet", "Collapse");
    CHECK(done.ok && done.function == "Greet" && doc.script().findGraph("Greet") && done.callNode != 0);
    const fg::Graph* event = doc.script().findGraph("EventGraph");
    CHECK(event && !event->findNode(a) && !event->findNode(b) && event->findNode(done.callNode));
    CHECK(doc.undoLabel() == "Collapse");
    doc.undo();
    CHECK(!doc.script().findGraph("Greet") && doc.script().findGraph("EventGraph")->findNode(a));
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);       // what a test printed, before one that fails hard
    for (const TestCase& t : registry()) {
        const int before = failures;
        t.run();
        std::printf("%s %s\n", failures == before ? "ok    " : "FAILED", t.name);
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}
