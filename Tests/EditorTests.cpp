// Tests of the editor logic that do not need a visible window (run with QT_QPA_PLATFORM=offscreen).
#include "Document.hpp"
#include "Graph/GraphScene.hpp"
#include "Graph/LinkItem.hpp"
#include "Panels/MyScriptPanel.hpp"
#include "Studio/RunController.hpp"

#include "Graph/GraphView.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QScrollBar>
#include <QClipboard>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>

#include <cstdio>
#include <functional>
#include <vector>

using namespace fged;

struct TestCase { const char* name; std::function<void()> fn; };
static std::vector<TestCase>& tests() { static std::vector<TestCase> t; return t; }
static int g_failures = 0;
struct Reg { Reg(const char* n, std::function<void()> f) { tests().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { ++g_failures; std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++g_failures; std::printf("  FAILED %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

static bool spinUntil(const std::function<bool()>& done, int timeoutMs = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

static PinRef pinOf(GraphScene& sc, fg::NodeId node, const std::string& pin, fg::PinDir dir) {
    NodeItem* n = sc.nodeItem(node);
    PinRef r;
    if (!n) return r;
    for (auto& p : n->compiled().pins)
        if (p.name == pin && p.dir == dir) {
            r.node = node;
            r.pin = pin;
            r.kind = p.kind;
            r.dir = dir;
            r.type = p.type;
            r.wildcard = p.wildcard;
        }
    return r;
}

static const fg::NodeTemplate* findTemplate(const std::vector<fg::NodeTemplate>& all, const std::string& defId, const std::string& title = "") {
    for (auto& t : all)
        if (t.defId == defId && (title.empty() || t.title == title)) return &t;
    return nullptr;
}

struct Fixture {
    fg::Runtime rt;
    Document doc{rt};
    GraphScene scene{&doc, "EventGraph"};
    Fixture() { scene.sync(); }
    std::vector<fg::NodeTemplate> palette() {
        const fg::Script& s = doc.script();
        fg::NodeBuildCtx ctx{rt.registry(), &s, s.findGraph("EventGraph")};
        return fg::buildPalette(ctx);
    }
    const fg::Graph& graph() { return *doc.graph("EventGraph"); }
};

TEST(new_document_has_main_and_print) {
    Fixture f;
    CHECK(f.doc.program()->ok());
    CHECK_EQ(f.graph().nodes.size(), size_t(2));
    CHECK(f.scene.nodeItem(1) && f.scene.nodeItem(2));
    CHECK_EQ(f.doc.program()->events.count("Main"), size_t(1));
}

TEST(undo_redo_restores_script) {
    Fixture f;
    auto all = f.palette();
    f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(100, 100));
    CHECK_EQ(f.graph().nodes.size(), size_t(3));
    CHECK(f.doc.isDirty());
    f.doc.undo();
    CHECK_EQ(f.graph().nodes.size(), size_t(2));
    f.scene.sync();
    CHECK(!f.scene.nodeItem(3));
    f.doc.redo();
    f.scene.sync();
    CHECK_EQ(f.graph().nodes.size(), size_t(3));
    CHECK(f.scene.nodeItem(3));
}

TEST(drag_from_exec_pin_connects_new_node) {
    Fixture f;
    auto all = f.palette();
    PinRef from = pinOf(f.scene, 2, "Then", fg::PinDir::Out);
    CHECK(from.valid());
    fg::NodeId id = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(500, 0), &from);
    f.scene.sync();
    bool linked = false;
    for (auto& c : f.graph().connections) linked |= c.fromNode == 2 && c.fromPin == "Then" && c.toNode == id && c.toPin == "In";
    CHECK(linked);
}

TEST(connect_data_pins_and_infer_generic_type) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId add = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(0, 200));
    fg::NodeId pr = 2;
    f.scene.sync();
    QString why;
    CHECK(f.scene.connectPins(pinOf(f.scene, add, "Result", fg::PinDir::Out), pinOf(f.scene, pr, "String", fg::PinDir::In), &why));
    f.scene.sync();
    CHECK(f.doc.program()->ok());
    // connecting a float output to input A resolves T = float
    fg::NodeId lerp = f.scene.addNodeFromTemplate(*findTemplate(all, "fn:Math.Lerp"), QPointF(0, 400));
    f.scene.sync();
    CHECK(f.scene.connectPins(pinOf(f.scene, lerp, "Result", fg::PinDir::Out), pinOf(f.scene, add, "A", fg::PinDir::In), &why));
    f.scene.sync();
    const fg::CompiledNode* cn = f.doc.compiledGraph("EventGraph")->findNode(add);
    CHECK(cn && cn->typeArgs.count("T") && cn->typeArgs.at("T") == "float");
}

TEST(incompatible_pins_are_rejected) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId mk = f.scene.addNodeFromTemplate(*findTemplate(all, "array.make"), QPointF(0, 200));
    fg::NodeId br = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(300, 200));
    f.scene.sync();
    QString why;
    CHECK(!f.scene.connectPins(pinOf(f.scene, mk, "Array", fg::PinDir::Out), pinOf(f.scene, br, "Condition", fg::PinDir::In), &why));
    CHECK(!why.isEmpty());
    CHECK(!f.scene.connectPins(pinOf(f.scene, br, "True", fg::PinDir::Out), pinOf(f.scene, mk, "[0]", fg::PinDir::In), &why));
}

TEST(connecting_replaces_existing_link) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId a = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(300, 0));
    fg::NodeId b = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(300, 200));
    f.scene.sync();
    QString why;
    CHECK(f.scene.connectPins(pinOf(f.scene, 2, "Then", fg::PinDir::Out), pinOf(f.scene, a, "In", fg::PinDir::In), &why));
    CHECK(f.scene.connectPins(pinOf(f.scene, 2, "Then", fg::PinDir::Out), pinOf(f.scene, b, "In", fg::PinDir::In), &why));
    int fromThen = 0;
    for (auto& c : f.graph().connections) fromThen += c.fromNode == 2 && c.fromPin == "Then";
    CHECK_EQ(fromThen, 1);
}

TEST(delete_selected_removes_links_and_is_undoable) {
    Fixture f;
    f.scene.selectOnly(2);
    f.scene.deleteSelected();
    CHECK_EQ(f.graph().nodes.size(), size_t(1));
    CHECK(f.graph().connections.empty());
    f.scene.sync();
    CHECK(!f.scene.nodeItem(2));
    f.doc.undo();
    f.scene.sync();
    CHECK_EQ(f.graph().connections.size(), size_t(1));
    CHECK(f.scene.nodeItem(2));
}

TEST(pin_default_edit_updates_program) {
    Fixture f;
    f.scene.setPinDefault(2, "String", fg::Value("changed"));
    f.scene.sync();
    const fg::CompiledNode* cn = f.doc.compiledGraph("EventGraph")->findNode(2);
    CHECK(cn);
    CHECK_EQ(cn->pins[size_t(cn->dataIn[0])].def.toString(), std::string("changed"));
}

TEST(copy_paste_duplicates_nodes_and_links) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId add = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(0, 200));
    f.scene.sync();
    QString why;
    f.scene.connectPins(pinOf(f.scene, add, "Result", fg::PinDir::Out), pinOf(f.scene, 2, "String", fg::PinDir::In), &why);
    f.scene.sync();
    f.scene.clearSelection();
    f.scene.nodeItem(add)->setSelected(true);
    f.scene.nodeItem(2)->setSelected(true);
    f.scene.copySelected();
    CHECK(f.scene.canPaste());
    size_t before = f.graph().nodes.size();
    f.scene.paste(QPointF(800, 800));
    CHECK_EQ(f.graph().nodes.size(), before + 2);
    int links = 0;
    for (auto& c : f.graph().connections) links += c.fromPin == "Result";
    CHECK_EQ(links, 2);
}

TEST(reroute_splits_a_link) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId add = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(0, 200));
    f.scene.sync();
    QString why;
    f.scene.connectPins(pinOf(f.scene, add, "Result", fg::PinDir::Out), pinOf(f.scene, 2, "String", fg::PinDir::In), &why);
    fg::Connection link{add, "Result", 2, "String"};
    f.scene.insertReroute(link, QPointF(200, 250));
    f.scene.sync();
    CHECK(f.doc.program()->ok());
    bool direct = false;
    for (auto& c : f.graph().connections) direct |= c == link;
    CHECK(!direct);
    int reroutes = 0;
    for (auto& n : f.graph().nodes) reroutes += n.def == "reroute";
    CHECK_EQ(reroutes, 1);
}

TEST(comment_boxes_are_created_and_synced) {
    Fixture f;
    f.scene.clearSelection();
    f.scene.nodeItem(2)->setSelected(true);
    f.scene.addComment(QPointF(0, 0));
    CHECK_EQ(f.graph().comments.size(), size_t(1));
    CHECK_EQ(f.graph().comments[0].text, std::string("Comment"));
}

TEST(variable_nodes_from_drop) {
    Fixture f;
    f.doc.edit("Add var", [](fg::Script& s) { s.variables.push_back({"Score", "int", fg::Value(5), "", ""}); });
    fg::NodeId id = f.scene.addVariableNode("Score", "global", false, QPointF(0, 300));
    f.scene.sync();
    const fg::CompiledNode* cn = f.doc.compiledGraph("EventGraph")->findNode(id);
    CHECK(cn && cn->op == fg::NodeOp::GetVariable);
    CHECK(f.doc.program()->ok());
}

TEST(layout_edit_does_not_recompile) {
    Fixture f;
    auto before = f.doc.program();
    f.doc.edit("Move", [](fg::Script& s) { s.graphs[0].nodes[0].x = 77; }, EditKind::Layout);
    CHECK(f.doc.program() == before);
    CHECK(f.doc.isDirty());
}

TEST(run_controller_runs_and_logs) {
    Fixture f;
    RunController run(&f.doc);
    std::vector<QString> log;
    QObject::connect(&run, &RunController::logMessage, [&](int, const QString& t) { log.push_back(t); });
    bool finished = false, ok = false;
    QObject::connect(&run, &RunController::finished, [&](bool o, const QString&) { finished = true; ok = o; });
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return finished; }));
    CHECK(ok);
    bool hello = false;
    for (auto& l : log) hello |= l == "Hello";
    CHECK(hello);
    CHECK(run.state() == RunController::State::Idle);
}

TEST(run_controller_breakpoint_pause_step_resume) {
    Fixture f;
    f.doc.debugger().setBreakpoint("EventGraph", 2, true);
    RunController run(&f.doc);
    run.setStepDelay(0);
    int pausedNode = 0;
    bool finished = false;
    QObject::connect(&run, &RunController::paused, [&](const QString&, int n) { pausedNode = n; });
    QObject::connect(&run, &RunController::finished, [&](bool, const QString&) { finished = true; });
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return run.state() == RunController::State::Paused; }));
    CHECK_EQ(pausedNode, 2);
    run.stepInto();
    // after the last node the run finishes
    CHECK(spinUntil([&] { return finished || run.state() == RunController::State::Paused; }));
    f.doc.debugger().setBreakpoint("EventGraph", 2, false);
}

TEST(run_controller_waits_on_delay) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId d = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.delay"), QPointF(300, 0));
    f.doc.edit("Delay", [&](fg::Script& s) {
        auto& g = s.graphs[0];
        g.findNode(d)->defaults["Duration"] = fg::Value(0.15);
        g.connections.clear();
        g.connections.push_back({1, "Then", d, "In"});
        g.connections.push_back({d, "Then", 2, "In"});
    });
    RunController run(&f.doc);
    bool finished = false, ok = false;
    bool sawWaiting = false;
    QObject::connect(&run, &RunController::stateChanged, [&](RunController::State s) { sawWaiting |= s == RunController::State::Waiting; });
    QObject::connect(&run, &RunController::finished, [&](bool o, const QString&) { finished = true; ok = o; });
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return finished; }));
    CHECK(ok);
    CHECK(sawWaiting);
}

TEST(editing_stops_a_running_script) {
    Fixture f;
    auto all = f.palette();
    fg::NodeId d = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.delay"), QPointF(300, 0));
    f.doc.edit("Delay", [&](fg::Script& s) {
        auto& g = s.graphs[0];
        g.findNode(d)->defaults["Duration"] = fg::Value(30.0);
        g.connections.clear();
        g.connections.push_back({1, "Then", d, "In"});
    });
    RunController run(&f.doc);
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return run.state() == RunController::State::Waiting; }));
    f.scene.addComment(QPointF(0, 0));      // layout edit: keeps running? (comments are structural-free)
    f.doc.edit("Change", [](fg::Script& s) { s.graphs[0].nodes[0].x += 1; });
    CHECK(run.state() == RunController::State::Idle);
}

TEST(rename_function_updates_calls) {
    Fixture f;
    f.doc.edit("Add function", [](fg::Script& s) { s.addFunction("Foo"); });
    f.doc.edit("Call", [](fg::Script& s) { s.graphs[0].addNode("func.call").props["function"] = "Foo"; });
    CHECK(f.doc.program()->ok());
    f.doc.edit("Rename", [](fg::Script& s) { s.renameFunction("Foo", "Bar"); });
    CHECK(f.doc.program()->ok());
    CHECK_EQ(f.graph().nodes.back().prop("function"), std::string("Bar"));
}

TEST(save_and_load_roundtrip) {
    Fixture f;
    f.doc.edit("Add var", [](fg::Script& s) { s.variables.push_back({"Score", "int", fg::Value(5), "", ""}); });
    QString path = QDir::temp().filePath("fg_editor_test.flow");
    QString err;
    CHECK(f.doc.save(path, &err));
    CHECK(!f.doc.isDirty());
    fg::Runtime rt2;
    Document d2(rt2);
    CHECK(d2.load(path, &err));
    CHECK_EQ(d2.script().variables.size(), size_t(1));
    CHECK(d2.program()->ok());
    QFile::remove(path);
}


// Simulated mouse interaction on a real view (offscreen).
struct ViewFixture {
    fg::Runtime rt;
    Document doc{rt};
    GraphScene scene{&doc, "EventGraph"};
    GraphView view{&scene};
    ViewFixture() {
        scene.sync();
        view.resize(1000, 700);
        view.show();
        view.centerOn(150, 50);
        QCoreApplication::processEvents();
    }
    QPoint toView(const QPointF& scenePos) { return view.mapFromScene(scenePos); }
    QPoint pinPoint(fg::NodeId id, const std::string& pin, fg::PinDir dir) { return toView(scene.nodeItem(id)->pinScenePos(pin, dir)); }
    void send(QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, QPointF(p), QPointF(view.viewport()->mapToGlobal(p)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &e);
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to) {
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }
};

TEST(mouse_drag_between_pins_creates_link) {
    ViewFixture f;
    auto all = fg::buildPalette(fg::NodeBuildCtx{f.rt.registry(), &f.doc.script(), f.doc.script().findGraph("EventGraph")});
    fg::NodeId br = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(500, 0));
    f.scene.sync();
    QCoreApplication::processEvents();
    QPoint a = f.pinPoint(2, "Then", fg::PinDir::Out);
    f.drag(a, f.pinPoint(br, "In", fg::PinDir::In));
    bool linked = false;
    for (auto& c : f.doc.graph("EventGraph")->connections) linked |= c.fromNode == 2 && c.toNode == br && c.toPin == "In";
    CHECK(linked);
}

TEST(mouse_drag_node_keeps_new_position) {
    ViewFixture f;
    NodeItem* n = f.scene.nodeItem(2);
    QPointF before = n->scenePos();
    QPoint header = f.toView(n->mapToScene(QPointF(20, 8)));
    f.drag(header, header + QPoint(120, 80));
    QCoreApplication::processEvents();
    f.scene.sync();
    QPointF after = f.scene.nodeItem(2)->scenePos();
    CHECK(std::abs(after.x() - before.x()) > 50);
    CHECK(std::abs(f.doc.graph("EventGraph")->findNode(2)->x - after.x()) < 0.5);
    // a later structural edit must not move it back
    f.scene.setPinDefault(2, "String", fg::Value("x"));
    f.scene.sync();
    CHECK(std::abs(f.scene.nodeItem(2)->scenePos().x() - after.x()) < 0.5);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    for (auto& t : tests()) {
        int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("  EXCEPTION: %s\n", e.what());
        }
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
