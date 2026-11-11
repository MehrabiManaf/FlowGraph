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

struct Host : glw::UiHost {
    std::string clipboard;
    glw::platform::WindowState windowState() const override { return glw::platform::WindowState::Normal; }
    void minimizeWindow() override {}
    void toggleMaximizeWindow() override {}
    void closeWindow() override {}
    void startTextInput() override {}
    void stopTextInput() override {}
    void setTextInputArea(const glw::Rect&) override {}
    std::string clipboardText() const override { return clipboard; }
    void setClipboardText(const std::string& text) override { clipboard = text; }
};

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

// A graph of an event and two Print nodes, on a canvas filling a root, at zoom 1 with its origin at the root's (a point of the
// canvas is a point of the root).
struct Edited {
    fg::Runtime rt;
    std::unique_ptr<fged::core::Document> doc;
    Host host;
    glw::UiRoot root{context(), &host};
    fged::gl::GraphView* view = nullptr;
    fg::NodeId ev = 0, a = 0, b = 0;

    Edited() {
        rt.registry().addEvent("Go");
        doc = std::make_unique<fged::core::Document>(rt);
        fg::Script s;
        fg::Graph& g = s.addEventGraph();
        ev = g.addNode("event:Go", 0, 0).id;
        a = g.addNode("util.print", 300, 0).id;
        b = g.addNode("util.print", 300, 200).id;
        doc->setScript(s, "Test");
        auto v = std::make_unique<fged::gl::GraphView>(*doc, "EventGraph");
        view = v.get();
        root.setContent(std::move(v));
        root.setSize({1200, 800});
        root.layout();
        view->setZoom(1);
        view->setOrigin({0, 0});
        root.layout();
    }

    const fg::Graph& graph() const { return *doc->graph("EventGraph"); }
    const fg::Node& node(fg::NodeId id) const { return *graph().findNode(id); }
    // The middle of a pin of a node, on the canvas.
    Point pin(fg::NodeId id, const std::string& name, fg::PinDir dir) const {
        const fged::gl::NodeView& v = *view->nodeView(id);
        return view->itemPosition(v) + *v.pinCenter(name, dir);
    }
    // A point of a node's header.
    Point header(fg::NodeId id) const { return view->itemPosition(*view->nodeView(id)) + Point{12, 8}; }

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

int main() {
    for (const TestCase& t : registry()) {
        const int before = failures;
        t.run();
        std::printf("%s %s\n", failures == before ? "ok    " : "FAILED", t.name);
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}
