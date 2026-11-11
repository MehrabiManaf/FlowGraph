// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
// The graph of the GLWidgets editor, edited with the mouse and the keys: a root of GLWidgets without a window (no GPU), its
// events given to it as a window would.
#include "Graph/GraphView.hpp"
#include "Graph/NodeView.hpp"

#include <FlowGraph/Runtime.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/GlyphAtlas.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <cstdio>
#include <functional>
#include <memory>
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
