// Tests of the editor logic that do not need a visible window (run with QT_QPA_PLATFORM=offscreen).
#include "Document.hpp"
#include "Common/Settings.hpp"
#include "Common/Theme.hpp"
#include "Graph/GraphScene.hpp"
#include "Graph/Style.hpp"
#include "Graph/LinkItem.hpp"
#include "Graph/PalettePopup.hpp"
#include "Panels/DetailsPanel.hpp"
#include "Panels/InfoPanels.hpp"
#include "Panels/FitTree.hpp"
#include "Panels/MyScriptPanel.hpp"
#include "Panels/TypeSpec.hpp"
#include "Studio/RunController.hpp"
#include "Studio/ScriptEditorWindow.hpp"
#include "StandardEditorHost.hpp"
#include <FlowGraphEditor/ScriptEditor.hpp>

#include "Graph/GraphView.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QHash>
#include <QStyleHints>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/DockRegistry.h>
#include "Common/CustomViewFactory/DockWidget.hpp"
#include "Common/CollapsiblePanel/CollapsiblePanel.hpp"
#include "Common/CustomViewFactory/TabBar.hpp"
#include <kddockwidgets/core/TabBar.h>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QComboBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QClipboard>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTimer>
#include <QSettings>
#include <QLabel>
#include <QToolBar>
#include <QMenu>
#include <QMimeData>
#include <QDropEvent>
#include <QMessageBox>
#include <stdexcept>
#include <QMenuBar>
#include <QMainWindow>
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

// The application of the editor tests is a standard application: it raises Main and a new script handles it.
struct StandardRuntime : fg::Runtime {
    StandardRuntime() { fg::standard::registerStandardApp(registry()); }
};

struct Fixture {
    StandardRuntime rt;
    Document doc{rt};
    GraphScene scene{&doc, "EventGraph"};
    Fixture() {
        doc.newScript([&](fg::Script& s) { fg::standard::createMainTemplate(s, rt.registry()); });
        scene.sync();
    }
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

// A link that would close an endless execution loop or a cycle of pure data nodes is refused with a reason; a loop that waits
// (a Delay) or has a way out (a Branch) is a loop a script may have.
TEST(editor_refuses_endless_loops_and_data_cycles) {
    Fixture f;
    auto all = f.palette();
    // the default script: Main -> Print (node 2); add Print 3 and Print 4 after it
    fg::NodeId p3 = f.scene.addNodeFromTemplate(*findTemplate(all, "util.print"), QPointF(300, 0));
    fg::NodeId p4 = f.scene.addNodeFromTemplate(*findTemplate(all, "util.print"), QPointF(600, 0));
    f.scene.sync();
    QString why;
    CHECK(f.scene.connectPins(pinOf(f.scene, 2, "Then", fg::PinDir::Out), pinOf(f.scene, p3, "In", fg::PinDir::In), &why));
    CHECK(f.scene.connectPins(pinOf(f.scene, p3, "Then", fg::PinDir::Out), pinOf(f.scene, p4, "In", fg::PinDir::In), &why));
    const size_t links = f.graph().connections.size();
    CHECK(!f.scene.connectPins(pinOf(f.scene, p4, "Then", fg::PinDir::Out), pinOf(f.scene, 2, "In", fg::PinDir::In), &why));
    CHECK(why.contains("endless loop"));
    CHECK_EQ(f.graph().connections.size(), links);
    // with a Delay in it the loop repeats every second: allowed
    fg::NodeId d = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.delay"), QPointF(900, 0));
    f.scene.sync();
    CHECK(f.scene.connectPins(pinOf(f.scene, p4, "Then", fg::PinDir::Out), pinOf(f.scene, d, "In", fg::PinDir::In), &why));
    CHECK(f.scene.connectPins(pinOf(f.scene, d, "Then", fg::PinDir::Out), pinOf(f.scene, 2, "In", fg::PinDir::In), &why));
    CHECK(f.doc.program()->ok());
    // data: Add1 -> Add2 -> Add1 is a value made of itself
    fg::NodeId a1 = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(0, 300));
    fg::NodeId a2 = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(300, 300));
    f.scene.sync();
    CHECK(f.scene.connectPins(pinOf(f.scene, a1, "Result", fg::PinDir::Out), pinOf(f.scene, a2, "A", fg::PinDir::In), &why));
    f.scene.sync();
    why.clear();
    CHECK(!f.scene.connectPins(pinOf(f.scene, a2, "Result", fg::PinDir::Out), pinOf(f.scene, a1, "B", fg::PinDir::In), &why));
    CHECK(why.contains("depend on itself"));
    CHECK(f.doc.program()->ok());
}

// An enum literal shows its value in its title, and the Details panel chooses it.
TEST(enum_literal_value_is_shown_and_chosen) {
    Fixture f;
    const fg::NodeTemplate* friday = nullptr;
    auto all = f.palette();
    for (auto& t : all)
        if (t.title == "Weekday.Friday") friday = &t;
    CHECK(friday != nullptr);
    if (!friday) return;
    fg::NodeId id = f.scene.addNodeFromTemplate(*friday, QPointF(0, 300));
    f.scene.sync();
    CHECK_EQ(f.doc.compiledGraph("EventGraph")->findNode(id)->title, std::string("Weekday.Friday"));
    DetailsPanel details(&f.doc);
    details.show();
    details.showNodes("EventGraph", {id});
    QCoreApplication::processEvents();
    QComboBox* value = nullptr;
    for (auto* c : details.findChildren<QComboBox*>())
        if (c->currentText() == "Friday") value = c;
    CHECK(value != nullptr);
    if (!value) return;
    CHECK_EQ(value->count(), 7);
    const int sunday = value->findText("Sunday");
    value->setCurrentIndex(sunday);
    emit value->activated(sunday);
    QCoreApplication::processEvents();
    CHECK_EQ(f.graph().findNode(id)->prop("value"), std::string("Sunday"));
    CHECK_EQ(f.doc.compiledGraph("EventGraph")->findNode(id)->title, std::string("Weekday.Sunday"));
}

// The Ref / Const check boxes of a parameter table can be seen when they are not checked, on both kinds of rows.
TEST(unchecked_boxes_are_visible_on_every_row) {
    Fixture f;
    f.doc.edit("Function", [](fg::Script& s) {
        s.addFunction("Fn", {{"A", "int", false, false, fg::Value()}, {"B", "int", false, false, fg::Value()}, {"C", "int", true, false, fg::Value()}});
    });
    DetailsPanel details(&f.doc);
    details.resize(500, 600);
    details.show();
    details.showGraph("Fn");
    QCoreApplication::processEvents();
    QTableWidget* table = nullptr;
    for (auto* t : details.findChildren<QTableWidget*>())
        if (t->rowCount() == 3) table = t;
    CHECK(table != nullptr);
    if (!table) return;
    const QImage img = table->viewport()->grab().toImage();
    auto distance = [](QColor a, QColor b) { return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()); };
    int boxes = 0;
    for (int row = 0; row < 2; ++row)                       // A and B: nothing checked; row 0 is a plain row, row 1 an alternate one
        for (int col = 0; col < table->columnCount(); ++col) {
            QTableWidgetItem* it = table->item(row, col);
            if (!it || !(it->flags() & Qt::ItemIsUserCheckable)) continue;
            const QRect r = table->visualRect(table->model()->index(row, col));
            const QColor bg = img.pixelColor(r.left() + 1, r.top() + 1);
            int contrast = 0;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x)
                    if (distance(img.pixelColor(x, y), bg) > 90) ++contrast;
            CHECK(contrast >= 12);                          // the frame of the box (a few dozen pixels)
            ++boxes;
        }
    CHECK(boxes >= 4);                                      // Ref and Const of two rows
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

TEST(event_nodes_cannot_be_deleted) {
    Fixture f;
    QString message;
    QObject::connect(&f.scene, &GraphScene::message, [&](const QString& m) { message = m; });
    f.scene.clearSelection();
    f.scene.nodeItem(1)->setSelected(true);            // the Main event
    f.scene.deleteSelected();
    CHECK(f.scene.nodeItem(1) != nullptr);
    CHECK(f.graph().findNode(1) != nullptr);
    CHECK(!message.isEmpty());
    // a mixed selection deletes everything but the event
    f.scene.clearSelection();
    f.scene.nodeItem(1)->setSelected(true);
    f.scene.nodeItem(2)->setSelected(true);
    f.scene.deleteSelected();
    CHECK(f.graph().findNode(1) != nullptr);
    CHECK(f.graph().findNode(2) == nullptr);
    CHECK(f.doc.program()->events.count("Main") == 1);
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

TEST(run_controller_is_not_flooded_by_a_script_that_follows_millions_of_links) {
    Fixture f;
    f.doc.edit("Long loop", [&](fg::Script& s) {
        auto& g = s.graphs[0];
        g.connections.clear();
        fg::NodeId loop = g.addNode("flow.for").id;
        g.findNode(loop)->defaults["Last"] = fg::Value(int64_t(3000000));
        fg::NodeId br = g.addNode("flow.branch").id;                         // every iteration follows one link into it
        g.connections.push_back({1, "Then", loop, "In"});
        g.connections.push_back({loop, "Loop Body", br, "In"});
    });
    RunController run(&f.doc);
    size_t links = 0;
    QObject::connect(&run, &RunController::linkFired, [&](const QString&, int, int, int) { ++links; });
    bool finished = false, ok = false;
    QObject::connect(&run, &RunController::finished, [&](bool o, const QString&) { finished = true; ok = o; });
    // the GUI thread stays responsive: a timer keeps firing while the script runs
    int ticks = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
    ticker.start(10);
    QElapsedTimer clock;
    clock.start();
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return finished; }, 60000));
    CHECK(ok);
    CHECK(ticks > 3);
    CHECK(links > 0);                                       // the animation still hears about the links ...
    CHECK(links < 20000);                                   // ... but not about every one of the 3 million
    CHECK(clock.elapsed() < 30000);
}

// The debugger panel never loads a whole big container: it shows the first elements, a "more" row loads the next ones and the Watch area
// evaluates expressions for exactly what the user wants to see.
static QTreeWidgetItem* childNamed(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i)
        if (parent->child(i)->text(0) == name) return parent->child(i);
    return nullptr;
}
static int realChildren(QTreeWidgetItem* it) {      // without the "N more" row
    int n = it->childCount();
    if (n > 0 && it->child(n - 1)->text(0) == "...") --n;
    return n;
}
static bool hasMoreRow(QTreeWidgetItem* it) { return it->childCount() > 0 && it->child(it->childCount() - 1)->text(0) == "..."; }

TEST(debugger_panel_pages_big_arrays_and_evaluates_watches) {
    Fixture f;
    f.doc.edit("Big array", [&](fg::Script& s) {
        s.variables.push_back({"IntArray", "array<int>", fg::Value(), "", ""});
        auto& g = s.graphs[0];
        g.connections.clear();
        fg::NodeId loop = g.addNode("flow.for").id;
        g.findNode(loop)->defaults["Last"] = fg::Value(int64_t(5000));
        fg::NodeId get = g.addNode("var.get").id;
        g.findNode(get)->props = {{"name", "IntArray"}, {"scope", "global"}};
        fg::NodeId add = g.addNode("array.add").id;
        fg::NodeId dbl = g.addNode("math.mul").id;
        g.findNode(dbl)->defaults["B"] = fg::Value(int64_t(2));
        g.connections.push_back({1, "Then", loop, "In"});
        g.connections.push_back({loop, "Loop Body", add, "In"});
        g.connections.push_back({get, "Value", add, "Array"});
        g.connections.push_back({loop, "Index", dbl, "A"});
        g.connections.push_back({dbl, "Result", add, "Item"});
    });
    RunController run(&f.doc);
    DebugPanel panel(&f.doc, &run);
    QTreeWidget* vars = nullptr;
    QTreeWidget* watches = nullptr;
    for (auto* t : panel.findChildren<QTreeWidget*>()) {
        if (t->headerItem()->text(0) == "Variable") vars = t;
        if (t->headerItem()->text(0) == "Watch") watches = t;
    }
    CHECK(vars && watches);
    if (!vars || !watches) return;
    auto arrayItem = [&]() -> QTreeWidgetItem* {
        for (int i = 0; i < vars->topLevelItemCount(); ++i)
            if (QTreeWidgetItem* it = childNamed(vars->topLevelItem(i), "IntArray")) return it;
        return nullptr;
    };

    panel.addWatch("IntArray[4990..4999]");               // watches can be added before the script runs
    panel.addWatch("IntArray[2500]");
    panel.addWatch("Missing");
    bool finished = false;
    QObject::connect(&run, &RunController::finished, [&](bool, const QString&) { finished = true; });
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return finished; }));
    QCoreApplication::processEvents();

    // 1. 5000 elements, but only 64 are loaded and a row says so
    QTreeWidgetItem* arr = arrayItem();
    CHECK(arr != nullptr);
    if (!arr) return;
    CHECK_EQ(arr->text(1), QString("array[5000]"));
    CHECK_EQ(realChildren(arr), 64);
    CHECK(hasMoreRow(arr));
    CHECK(arr->child(arr->childCount() - 1)->text(1).startsWith("4,936 more"));
    CHECK_EQ(arr->child(63)->text(1), QString("126"));

    // 2. double-clicking the row loads the next 256
    arr->setExpanded(true);
    emit vars->itemDoubleClicked(arr->child(arr->childCount() - 1), 1);
    CHECK_EQ(realChildren(arr), 64 + 256);
    CHECK_EQ(arr->child(64)->text(0), QString("[64]"));
    CHECK_EQ(arr->child(319)->text(1), QString("638"));
    CHECK(hasMoreRow(arr));
    CHECK(arr->child(arr->childCount() - 1)->text(1).startsWith("4,680 more"));

    // 3. the watches: a range, one element, a mistake
    CHECK_EQ(watches->topLevelItemCount(), 3);
    QTreeWidgetItem* range = watches->topLevelItem(0);
    CHECK_EQ(range->text(0), QString("IntArray[4990..4999]"));
    CHECK_EQ(range->text(1), QString("array[5000]"));
    CHECK_EQ(realChildren(range), 10);
    CHECK(!hasMoreRow(range));
    CHECK_EQ(range->child(0)->text(0), QString("[4990]"));
    CHECK_EQ(range->child(9)->text(1), QString("9998"));
    CHECK(range->isExpanded());
    CHECK_EQ(watches->topLevelItem(1)->text(1), QString("5000"));
    CHECK(watches->topLevelItem(2)->text(1).startsWith("<unknown variable"));

    // 4. a watch of a window in the middle can be paged on as well
    panel.addWatch("IntArray[1000..1009]");
    QTreeWidgetItem* mid = watches->topLevelItem(3);
    CHECK(hasMoreRow(mid));
    emit watches->itemDoubleClicked(mid->child(mid->childCount() - 1), 1);
    CHECK_EQ(realChildren(mid), 10 + 256);
    CHECK_EQ(mid->child(10)->text(0), QString("[1010]"));

    // 5. what the user opened and loaded survives the next refresh (a new run refreshes every tree)
    finished = false;
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return finished; }));
    QCoreApplication::processEvents();
    arr = arrayItem();
    CHECK(arr != nullptr);
    if (!arr) return;
    CHECK(arr->isExpanded());
    CHECK_EQ(realChildren(arr), 64 + 256);
    CHECK_EQ(realChildren(watches->topLevelItem(3)), 10 + 256);

    // 6. removing a watch
    panel.removeWatch("Missing");
    CHECK_EQ(watches->topLevelItemCount(), 3);
    CHECK_EQ(panel.watchExpressions().size(), 3);
}

// The inputs / outputs tables of a function have no header (the tooltips name the columns) and no widgets in their cells: a combo box
// appears only while the user edits a cell.
TEST(details_parameter_tables_edit_through_a_delegate) {
    Fixture f;
    f.doc.edit("Function", [&](fg::Script& s) {
        s.addFunction("Add", {{"A", "int", false, false, fg::Value()}, {"Items", "array<string>", true, false, fg::Value()}},
                      {{"Sum", "map<string,float>", false, false, fg::Value()}});
    });
    auto settle = [] {      // closed editors are deleted later
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    };
    DetailsPanel details(&f.doc);
    details.showGraph("Add");
    details.resize(420, 600);
    details.show();
    QCoreApplication::processEvents();
    auto tables = [&] { return details.findChildren<QTableWidget*>(); };
    CHECK_EQ(tables().size(), 2);
    if (tables().size() != 2) return;
    QTableWidget* in = tables()[0];
    QTableWidget* out = tables()[1];
    CHECK(in->horizontalHeader()->isHidden());
    CHECK(out->horizontalHeader()->isHidden());
    CHECK_EQ(in->columnCount(), 6);
    CHECK_EQ(out->columnCount(), 4);
    for (int r = 0; r < in->rowCount(); ++r)
        for (int c = 0; c < in->columnCount(); ++c) CHECK(in->cellWidget(r, c) == nullptr);     // plain cells, no widgets
    CHECK(details.findChildren<QComboBox*>().isEmpty());

    // the tooltip of every cell names its column
    CHECK(in->item(0, 0)->toolTip().contains("Name"));
    CHECK(in->item(0, 1)->toolTip().contains("Type") && in->item(0, 1)->toolTip().contains("int"));
    CHECK(in->item(1, 2)->toolTip().contains("Container") && in->item(1, 2)->toolTip().contains("Array"));
    CHECK(in->item(1, 4)->toolTip().contains("Ref") && in->item(1, 4)->toolTip().contains("yes"));
    CHECK(in->item(0, 5)->toolTip().contains("Const") && in->item(0, 5)->toolTip().contains("no"));
    CHECK(out->item(0, 3)->toolTip().contains("Key") && out->item(0, 3)->toolTip().contains("string"));
    CHECK(!(in->item(0, 3)->flags() & Qt::ItemIsEditable));                                    // the key of a non-map is not editable ...
    CHECK(in->item(0, 3)->text().isEmpty());                                                   // ... and empty, as it is for an array
    CHECK(in->item(1, 3)->text().isEmpty());
    CHECK_EQ(out->item(0, 3)->text(), QString("string"));                                      // only a map has a key type

    auto param = [&](bool inputs, size_t i) {
        const fg::Graph* g = f.doc.graph("Add");
        return inputs ? g->inputs[i] : g->outputs[i];
    };

    // editing the Type cell shows an (editable) combo box with the types, and only until the edit ends
    in->setCurrentCell(0, 1);
    in->editItem(in->item(0, 1));
    QCoreApplication::processEvents();
    auto boxes = details.findChildren<QComboBox*>();
    CHECK_EQ(boxes.size(), 1);
    if (boxes.size() != 1) return;
    CHECK(boxes[0]->isEditable());
    CHECK(boxes[0]->findText("float") >= 0);
    CHECK(spinUntil([&] { return boxes[0]->property("popupOpen").toBool(); }, 2000));            // the list of types is open ...
    emit qApp->focusChanged(boxes[0]->lineEdit(), boxes[0]->view());                           // ... and the focus moving into it does not end the edit
    settle();
    CHECK_EQ(details.findChildren<QComboBox*>().size(), 1);
    boxes = details.findChildren<QComboBox*>();
    if (boxes.size() != 1) return;
    boxes[0]->setCurrentText("float");
    boxes[0]->hidePopup();
    emit boxes[0]->lineEdit()->returnPressed();                                                // typed + Return
    QCoreApplication::processEvents();
    CHECK_EQ(param(true, 0).type, std::string("float"));
    settle();
    CHECK(details.findChildren<QComboBox*>().isEmpty());                                        // the editor is gone again

    // a type the registry does not know is refused
    tables()[0]->editItem(tables()[0]->item(0, 1));
    QCoreApplication::processEvents();
    boxes = details.findChildren<QComboBox*>();
    CHECK_EQ(boxes.size(), 1);
    if (boxes.size() != 1) return;
    boxes[0]->setCurrentText("nonsense");
    boxes[0]->hidePopup();
    emit boxes[0]->lineEdit()->returnPressed();
    settle();
    CHECK_EQ(param(true, 0).type, std::string("float"));
    CHECK(details.findChildren<QComboBox*>().isEmpty());

    // the container is chosen from a list; a Map makes the Key cell editable
    in = tables()[0];
    in->editItem(in->item(0, 2));
    QCoreApplication::processEvents();
    boxes = details.findChildren<QComboBox*>();
    CHECK_EQ(boxes.size(), 1);
    if (boxes.size() != 1) return;
    CHECK(!boxes[0]->isEditable());
    CHECK_EQ(boxes[0]->count(), 3);
    boxes[0]->setCurrentIndex(2);
    boxes[0]->hidePopup();                                                                     // a choice closes the list first
    emit boxes[0]->activated(2);
    settle();
    CHECK_EQ(param(true, 0).type, std::string("map<string,float>"));
    in = tables()[0];
    CHECK(in->item(0, 3)->flags() & Qt::ItemIsEditable);
    CHECK_EQ(in->item(0, 3)->text(), QString("string"));                                       // a map starts with string keys

    // the key type is chosen from the (open) list as well
    in->editItem(in->item(0, 3));
    QCoreApplication::processEvents();
    boxes = details.findChildren<QComboBox*>();
    CHECK_EQ(boxes.size(), 1);
    if (boxes.size() != 1) return;
    CHECK(spinUntil([&] { return boxes[0]->property("popupOpen").toBool(); }, 2000));
    boxes[0]->setCurrentText("int");
    boxes[0]->hidePopup();
    emit qApp->focusChanged(boxes[0]->lineEdit(), in);                                         // clicking elsewhere ends (and commits) the edit
    settle();
    CHECK_EQ(param(true, 0).type, std::string("map<int,float>"));
    in = tables()[0];

    // back to a single value: the key cell is empty again, and the key is remembered for the next time it is a map
    in->editItem(in->item(0, 2));
    QCoreApplication::processEvents();
    boxes = details.findChildren<QComboBox*>();
    if (boxes.size() != 1) return;
    boxes[0]->setCurrentIndex(0);
    boxes[0]->hidePopup();                                                                     // a choice closes the list first
    emit boxes[0]->activated(0);
    settle();
    CHECK_EQ(param(true, 0).type, std::string("float"));
    in = tables()[0];
    CHECK(in->item(0, 3)->text().isEmpty());
    in->editItem(in->item(0, 2));
    QCoreApplication::processEvents();
    boxes = details.findChildren<QComboBox*>();
    if (boxes.size() != 1) return;
    boxes[0]->setCurrentIndex(2);
    boxes[0]->hidePopup();                                                                     // a choice closes the list first
    emit boxes[0]->activated(2);
    settle();
    CHECK_EQ(param(true, 0).type, std::string("map<int,float>"));
    in = tables()[0];

    // Ref and Const are check marks in the cell
    in->item(0, 5)->setCheckState(Qt::Checked);
    settle();
    CHECK(param(true, 0).isConst);
    CHECK(in->item(0, 5)->toolTip().contains("yes"));
}

// The connector of a data pin tells the container kind: a circle (single value), a bracket with a row of two items (array), a 2 x 2 matrix
// (map: key type left, value type right); diamonds instead of circles for references.
static QImage renderGlyph(const PinGlyph& g, int scale = 8) {
    QImage img(24 * scale, 24 * scale, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    drawPinGlyph(&p, QPointF(12, 12), g);
    return img;
}
// pixel at an offset (in connector units) from the center of the glyph
static QColor glyphAt(const QImage& img, double dx, double dy, int scale = 8) {
    return QColor::fromRgba(img.pixel(int((12 + dx) * scale), int((12 + dy) * scale)));
}
static bool inked(const QImage& img, double dx, double dy) { return glyphAt(img, dx, dy).alpha() > 200; }

TEST(pin_connectors_show_the_container_kind) {
    const QColor value(220, 40, 40), key(40, 90, 230), hollow(30, 30, 30);
    PinGlyph g;
    g.color = value;
    g.keyColor = key;
    g.hollow = hollow;
    g.connected = true;

    // a single value: a circle - and a diamond for a reference
    QImage circle = renderGlyph(g);
    CHECK(inked(circle, 0, 0) && inked(circle, 3.5, 0) && inked(circle, 0, -3.5));
    CHECK(inked(circle, 3.0, 3.0));                                    // the diagonal is inside a circle ...
    CHECK(!inked(circle, 5, 0));
    g.byRef = true;
    QImage diamond = renderGlyph(g);
    CHECK(inked(diamond, 0, 4.2) && inked(diamond, 4.2, 0));          // ... the tips reach further than the circle
    CHECK(!inked(diamond, 3.0, 3.0));                                  // and the corners are cut off
    g.byRef = false;

    // an array: a bracket frame with two items in a row, all in the element color
    g.container = 1;
    QImage array = renderGlyph(g);
    for (double x : {-2.9, 2.9}) CHECK(inked(array, x, 0));            // the items
    CHECK(glyphAt(array, 2.9, 0) == value && glyphAt(array, -2.9, 0) == value);
    CHECK(!inked(array, 0, 0));                                        // ... with room between them
    CHECK(inked(array, -6.6, 0) && inked(array, 6.6, 0));              // the brackets
    CHECK(inked(array, -5.8, -4.6) && inked(array, 5.8, 4.6));         // ... with their serifs
    CHECK(!inked(array, 2.9, -3.2) && !inked(array, 2.9, 3.2));        // the items are in one row, not a grid
    CHECK(!inked(array, 0, -4.6));                                     // no lid on the frame
    CHECK(glyphAt(array, -6.6, 0) == value);
    CHECK(inked(array, 2.9 + 1.7, 0) && inked(array, 2.9, 1.7));       // the items are big: a radius of about 1.8
    g.byRef = true;
    QImage refArray = renderGlyph(g);
    CHECK(glyphAt(refArray, 2.9 + 2.5, 0).alpha() > glyphAt(array, 2.9 + 2.5, 0).alpha() + 40);   // diamond items reach further than circles
    CHECK(inked(refArray, 2.9, 0) && glyphAt(refArray, 2.9, 0) == value);
    g.byRef = false;

    // a map: 2 x 2, the left column in the key color and the right one in the value color
    g.container = 2;
    QImage map = renderGlyph(g);
    for (double y : {-3.0, 3.0}) {
        CHECK(glyphAt(map, -3.0, y) == key);
        CHECK(glyphAt(map, 3.0, y) == value);
    }
    CHECK(!inked(map, 0, 0));                                          // nothing between the columns
    CHECK(!inked(map, -3.0, 0) && !inked(map, 3.0, 0));                // two separate rows
    CHECK(inked(map, -3.0 - 1.7, -3.0) && inked(map, 3.0, 3.0 + 1.7)); // big items
    g.byRef = true;
    QImage refMap = renderGlyph(g);
    CHECK(glyphAt(refMap, -3.0 + 2.5, -3.0).alpha() > glyphAt(map, -3.0 + 2.5, -3.0).alpha() + 40);
    CHECK(glyphAt(refMap, 3.0, 3.0 + 2.5).alpha() > glyphAt(map, 3.0, 3.0 + 2.5).alpha() + 40);
    CHECK(glyphAt(refMap, -3.0, -3.0) == key && glyphAt(refMap, 3.0, 3.0) == value);
    g.byRef = false;

    // not connected: hollow items (the fill is the background, the outline keeps the type color)
    g.connected = false;
    g.container = 0;
    CHECK(glyphAt(renderGlyph(g), 0, 0) == hollow);
    CHECK(inked(renderGlyph(g), 3.9, 0));
    g.container = 1;
    QImage hollowArray = renderGlyph(g);
    CHECK(glyphAt(hollowArray, 2.9, 0) == hollow && glyphAt(hollowArray, -2.9, 0) == hollow);
    CHECK(glyphAt(hollowArray, 2.9, 1.8) == value);                    // the outline of an item
    g.container = 2;
    QImage hollowMap = renderGlyph(g);
    CHECK(glyphAt(hollowMap, -3.0, -3.0) == hollow && glyphAt(hollowMap, 3.0, 3.0) == hollow);
    CHECK(glyphAt(hollowMap, -3.0 - 1.8, -3.0) == key);                // the outline of the key items
    CHECK(glyphAt(hollowMap, 3.0 + 1.8, 3.0) == value);
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

// The Details panel offers the settings a node definition describes (NodeDef::settings): a spin box for a count, a text field with
// one line per entry for a list (applied when it loses the focus).
TEST(details_panel_edits_node_settings) {
    Fixture f;
    fg::NodeId seq = 0, sw = 0, gate = 0;
    f.doc.edit("Add", [&](fg::Script& s) {
        seq = s.graphs[0].addNode("flow.sequence").id;
        sw = s.graphs[0].addNode("flow.switch.string", 0, 200).id;
        gate = s.graphs[0].addNode("flow.gate", 0, 400).id;
    });
    f.scene.sync();
    auto execOuts = [&](fg::NodeId id) { return f.doc.compiledGraph("EventGraph")->findNode(id)->execOut.size(); };
    CHECK_EQ(execOuts(sw), size_t(3));                            // A, B, Default

    DetailsPanel panel(&f.doc);
    panel.show();
    panel.showNodes("EventGraph", {seq});
    QCoreApplication::processEvents();
    auto spins = panel.findChildren<QSpinBox*>();
    CHECK_EQ(spins.size(), 1);
    if (spins.size() == 1) {
        CHECK_EQ(spins[0]->maximum(), 32);
        spins[0]->setValue(4);
        CHECK_EQ(f.graph().findNode(seq)->prop("count"), std::string("4"));
        CHECK_EQ(execOuts(seq), size_t(4));
    }

    panel.showNodes("EventGraph", {sw});
    QCoreApplication::processEvents();
    auto texts = panel.findChildren<QPlainTextEdit*>();
    CHECK_EQ(texts.size(), 1);
    if (texts.size() == 1) {
        CHECK_EQ(texts[0]->toPlainText(), QString("A\nB"));
        texts[0]->setPlainText("Yes\nNo\nMaybe");
        texts[0]->document()->setModified(true);                  // as if typed
        CHECK_EQ(execOuts(sw), size_t(3));                        // nothing applied while the field has the focus
        QFocusEvent out(QEvent::FocusOut);
        QCoreApplication::sendEvent(texts[0], &out);
        CHECK_EQ(f.graph().findNode(sw)->prop("cases"), std::string("Yes\nNo\nMaybe"));
        CHECK_EQ(execOuts(sw), size_t(4));
        CHECK(f.doc.canUndo());
        f.doc.undo();
        CHECK_EQ(execOuts(sw), size_t(3));
    }

    // a node with several exec inputs gets a connector for each of them
    f.scene.sync();
    NodeItem* g = f.scene.nodeItem(gate);
    CHECK(g != nullptr);
    if (g) {
        std::set<std::pair<double, double>> at;
        for (const char* pin : {"Enter", "Open", "Close", "Toggle"}) {
            QPointF p = g->pinScenePos(pin, fg::PinDir::In);
            at.insert({p.x(), p.y()});
        }
        CHECK_EQ(at.size(), size_t(4));
    }
}

// Selecting a call node edits the signature of the function it calls, exactly like selecting that function's entry or return
// node would: it is the same Inputs / Outputs editor, not the read only pin list of an ordinary node.
TEST(selecting_a_call_node_edits_the_called_function) {
    Fixture f;
    f.doc.edit("Add function", [](fg::Script& s) { s.addFunction("Foo"); });
    fg::NodeId call = 0;
    f.doc.edit("Call", [&](fg::Script& s) {
        call = s.graphs[0].addNode("func.call").id;
        s.graphs[0].findNode(call)->props["function"] = "Foo";
    });
    CHECK(f.doc.program()->ok());

    DetailsPanel panel(&f.doc);
    panel.show();
    panel.showNodes("EventGraph", {call});
    QCoreApplication::processEvents();

    // a plain node (built in or not) would show read only "fixedInputs" / "fixedOutputs" tables; the call node shows none of those
    CHECK(panel.findChild<QTableWidget*>("fixedInputs") == nullptr);
    CHECK(panel.findChild<QTableWidget*>("fixedOutputs") == nullptr);
    auto tables = panel.findChildren<QTableWidget*>();
    CHECK_EQ(tables.size(), 2);                 // Foo's Inputs and Outputs editor, same as buildGraphFor("Foo")
    if (tables.size() != 2) return;
    CHECK_EQ(tables[0]->rowCount(), 0);
    CHECK_EQ(tables[1]->rowCount(), 0);

    auto panels = panel.findChildren<CCollapsiblePanel*>();
    CHECK(!panels.isEmpty());
    if (panels.isEmpty()) return;
    emit panels[0]->PlusButtonClicked();        // "+" of the Inputs editor: adds a parameter to Foo itself
    QCoreApplication::processEvents();

    const fg::Graph* foo = f.doc.script().findGraph("Foo");
    CHECK(foo && foo->inputs.size() == size_t(1) && foo->inputs[0].name == "Input");
    CHECK(f.doc.program()->ok());

    // the call node's own compiled pins follow the function's signature
    const fg::CompiledGraph* cg = f.doc.compiledGraph("EventGraph");
    const fg::CompiledNode* cn = cg ? cg->findNode(call) : nullptr;
    CHECK(cn && cn->dataIn.size() == 1 && cn->pins[size_t(cn->dataIn[0])].name == "Input");

    f.doc.undo();
    foo = f.doc.script().findGraph("Foo");                  // undo replaces the script: the old pointer no longer applies
    CHECK(foo && foo->inputs.empty());
}

TEST(save_and_load_roundtrip) {
    Fixture f;
    f.doc.edit("Add var", [](fg::Script& s) { s.variables.push_back({"Score", "int", fg::Value(5), "", ""}); });
    QString path = QDir::temp().filePath("fg_editor_test.flow");
    QString err;
    CHECK(f.doc.save(path, &err));
    CHECK(!f.doc.isDirty());
    StandardRuntime rt2;
    Document d2(rt2);
    CHECK(d2.load(path, &err));
    CHECK_EQ(d2.script().variables.size(), size_t(1));
    CHECK(d2.program()->ok());
    QFile::remove(path);
}


// Simulated mouse interaction on a real view (offscreen).
struct ViewFixture {
    StandardRuntime rt;
    Document doc{rt};
    GraphScene scene{&doc, "EventGraph"};
    GraphView view{&scene};
    ViewFixture() {
        doc.newScript([&](fg::Script& s) { fg::standard::createMainTemplate(s, rt.registry()); });
        scene.sync();
        view.resize(1000, 700);
        view.show();
        view.centerOn(300, 50);
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
    fg::NodeId br = f.scene.addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(560, 0));
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


TEST(pin_editor_follows_its_node_when_the_node_is_dragged) {
    ViewFixture f;
    NodeItem* n = f.scene.nodeItem(2);
    QRectF box = n->editorSceneRect("String");
    CHECK(!box.isNull());
    QPoint inBox = f.toView(box.center());
    f.send(QEvent::MouseMove, inBox, Qt::NoButton, Qt::NoButton);
    f.send(QEvent::MouseButtonPress, inBox, Qt::LeftButton, Qt::LeftButton);      // opens the editor of the pin ...
    CHECK(f.scene.isInplaceEditing());
    QPointF nodeBefore = n->scenePos();
    for (int i = 1; i <= 4; ++i) f.send(QEvent::MouseMove, inBox + QPoint(25 * i, 15 * i), Qt::NoButton, Qt::LeftButton);   // ... and drags
    CHECK(std::abs(n->scenePos().x() - nodeBefore.x()) > 50);                        // the node moved
    QRectF boxAfter = n->editorSceneRect("String");
    CHECK(std::abs(f.scene.inplaceEditorPos().x() - boxAfter.left()) < 1.0);          // and the editor went with it
    CHECK(std::abs(f.scene.inplaceEditorPos().y() - boxAfter.top()) < 1.0);
    f.send(QEvent::MouseButtonRelease, inBox + QPoint(100, 60), Qt::LeftButton, Qt::NoButton);
    CHECK(std::abs(f.scene.inplaceEditorPos().x() - n->editorSceneRect("String").left()) < 1.0);
}

TEST(reroute_nodes_can_be_moved_and_still_start_links) {
    ViewFixture f;
    auto all = fg::buildPalette(fg::NodeBuildCtx{f.rt.registry(), &f.doc.script(), f.doc.script().findGraph("EventGraph")});
    fg::NodeId a = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(260, 150));
    fg::NodeId b = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(560, 170));
    fg::Connection link{a, "Result", b, "A"};
    f.doc.edit("Link", [&](fg::Script& s) { s.graphs[0].connections.push_back(link); });
    f.scene.sync();
    f.scene.insertReroute(link, QPointF(440, 175));
    f.scene.sync();
    QCoreApplication::processEvents();
    fg::NodeId rr = 0;
    for (auto& n : f.doc.graph("EventGraph")->nodes)
        if (n.def == "reroute") rr = n.id;
    CHECK(rr != 0);
    NodeItem* item = f.scene.nodeItem(rr);
    CHECK(item != nullptr && item->isReroute());
    if (!item) return;
    const size_t links = f.doc.graph("EventGraph")->connections.size();

    // pressing on the pill (between the two circles) and dragging moves it - it does not start a link
    QPointF before = item->scenePos();
    QPoint body = f.toView(item->mapToScene(QPointF(18, 9)));
    f.drag(body, body + QPoint(70, 45));
    QCoreApplication::processEvents();
    item = f.scene.nodeItem(rr);
    CHECK(std::abs(item->scenePos().x() - before.x()) > 50);
    CHECK(std::abs(item->scenePos().y() - before.y()) > 30);
    CHECK(!f.scene.isLinkDragging());
    CHECK_EQ(f.doc.graph("EventGraph")->connections.size(), links);
    CHECK(std::abs(f.doc.graph("EventGraph")->findNode(rr)->x - item->scenePos().x()) < 0.5);     // the new position was stored

    // pressing on its output circle still drags a link out of it
    QPoint out = f.pinPoint(rr, "Out", fg::PinDir::Out);
    f.send(QEvent::MouseMove, out, Qt::NoButton, Qt::NoButton);
    f.send(QEvent::MouseButtonPress, out, Qt::LeftButton, Qt::LeftButton);
    CHECK(f.scene.isLinkDragging());
    QPoint target = f.pinPoint(b, "B", fg::PinDir::In);
    f.send(QEvent::MouseMove, (out + target) / 2, Qt::NoButton, Qt::LeftButton);
    f.send(QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
    f.send(QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
    bool linked = false;
    for (auto& c : f.doc.graph("EventGraph")->connections) linked |= c.fromNode == rr && c.fromPin == "Out" && c.toNode == b && c.toPin == "B";
    CHECK(linked);
}

// The same interactions inside the real main window (docking, tabs, refresh on every edit).
struct TestHost : fged::EditorHost {
    fg::Runtime& rt;
    explicit TestHost(fg::Runtime& r) : rt(r) {}
    fg::Runtime& runtime() override { return rt; }
    void initializeNewScript(fg::Script& s) override { fg::standard::createMainTemplate(s, rt.registry()); }
    // runs without asking for the arguments (the default host opens a dialog)
    bool prepareLaunch(const fged::LaunchEntry& entry, fged::LaunchSpec& spec, QWidget*) override {
        spec.entry = entry.entry;
        spec.args.clear();
        for (auto& p : entry.params) spec.args.push_back(rt.registry().defaultValue(rt.registry().findType(p.type)));
        return true;
    }
};

struct StudioFixture {
    StandardRuntime rt;
    TestHost host{rt};
    fged::ScriptEditor editor{host};
    ScriptEditorWindow& win = *static_cast<ScriptEditorWindow*>(editor.window());
    GraphView* view = nullptr;
    StudioFixture() {
        win.show();
        QCoreApplication::processEvents();
        view = win.findChild<GraphView*>();
        QCoreApplication::processEvents();
    }
    GraphScene* scene() { return view->graphScene(); }
    QPoint toView(const QPointF& p) { return view->mapFromScene(p); }
    void send(QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, QPointF(p), QPointF(view->viewport()->mapToGlobal(p)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &e);
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to) {
        send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);      // hover first, like a real mouse
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 5; ++i) send(QEvent::MouseMove, from + (to - from) * i / 5, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }
};

// Edit > Find References lists the nodes that use the variable of the selected node, follows the edits of the script and brings a
// result into view; Find in Script searches the text of the nodes.
TEST(studio_find_references_and_find_in_script) {
    StudioFixture f;
    Document* doc = f.win.document();
    const QString graph = f.scene()->graphName();
    std::vector<fg::NodeId> gets;
    doc->edit("Nodes", [&](fg::Script& s) {
        s.variables.push_back({"Score", "int", fg::Value(int64_t(0)), "", ""});
        fg::Graph* g = s.findGraph(ss(graph));
        for (int i = 0; i < 2; ++i) {
            fg::Node& n = g->addNode("var.get", 100.0 * i, 300);
            n.props = {{"name", "Score"}, {"scope", "global"}};
            gets.push_back(n.id);
        }
        fg::Node& set = g->addNode("var.set", 300, 300);
        set.props = {{"name", "Score"}, {"scope", "global"}};
        g->addNode("util.print", 500, 300).defaults["String"] = fg::Value("Final score");
    });
    QCoreApplication::processEvents();
    f.scene()->selectOnly(gets[0]);
    QCoreApplication::processEvents();

    QAction* refs = nullptr;
    QAction* find = nullptr;
    for (QAction* a : f.win.findChildren<QAction*>()) {
        if (a->text() == "Find &References") refs = a;
        if (a->text() == "&Find in Script...") find = a;
    }
    auto* panel = f.win.findChild<FindPanel*>();
    CHECK(refs && find && panel);
    if (!refs || !find || !panel) return;
    CHECK_EQ(refs->shortcut(), QKeySequence("Shift+F12"));
    refs->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(panel->resultCount(), 3);                            // two Get Score, one Set Score

    // the results follow the script
    doc->edit("More", [&](fg::Script& s) {
        fg::Node& n = s.findGraph(ss(graph))->addNode("var.get", 0, 500);
        n.props = {{"name", "Score"}, {"scope", "global"}};
    });
    QCoreApplication::processEvents();
    CHECK_EQ(panel->resultCount(), 4);

    // activating a result selects that node
    auto* tree = panel->findChild<QTreeWidget*>();
    CHECK(tree != nullptr);
    if (tree) {
        QTreeWidgetItem* second = tree->topLevelItem(1);
        CHECK(second != nullptr);
        if (second) {
            const int id = second->data(0, Qt::UserRole + 1).toInt();
            emit tree->itemActivated(second, 0);
            QCoreApplication::processEvents();
            CHECK(f.scene()->selectedNodeIds() == QList<fg::NodeId>{id});
        }
    }

    // Find in Script: the search field takes the focus; typing searches titles, comments and values
    find->trigger();
    QCoreApplication::processEvents();
    auto* field = panel->findChild<QLineEdit*>();
    CHECK(field != nullptr);
    if (field) {
        for (QChar ch : QString("final")) {                      // typed, so that textEdited fires
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
            QApplication::sendEvent(field, &press);
        }
        CHECK_EQ(field->text(), QString("final"));
        QCoreApplication::processEvents();
        CHECK_EQ(panel->resultCount(), 1);
    }
}

// Edit > Collapse to Function: one undoable step; the call node is selected; a selection that cannot be collapsed changes nothing.
TEST(studio_collapse_to_function) {
    StudioFixture f;
    Document* doc = f.win.document();
    const QString graph = f.scene()->graphName();
    fg::NodeId a = 0, b = 0;
    doc->edit("Nodes", [&](fg::Script& s) {
        fg::Graph* g = s.findGraph(ss(graph));
        fg::NodeId main = 0;
        for (auto& n : g->nodes) if (n.def.rfind("event:", 0) == 0) main = n.id;
        fg::Node& p1 = g->addNode("util.print", 300, 0);
        p1.defaults["String"] = fg::Value("one");
        fg::Node& p2 = g->addNode("util.print", 500, 0);
        p2.defaults["String"] = fg::Value("two");
        a = p1.id;
        b = p2.id;
        g->connections.erase(std::remove_if(g->connections.begin(), g->connections.end(), [&](const fg::Connection& c) { return c.fromNode == main; }),
                             g->connections.end());
        g->connections.push_back({main, "Then", a, "In"});
        g->connections.push_back({a, "Then", b, "In"});
    });
    QCoreApplication::processEvents();
    const std::string before = doc->script().toText();

    QAction* collapseAct = nullptr;
    for (QAction* act : f.win.findChildren<QAction*>())
        if (act->text() == "Collapse to &Function...") collapseAct = act;
    CHECK(collapseAct != nullptr);
    // one node is not collapsed: the command is disabled and does nothing
    f.scene()->selectOnly(a);
    QCoreApplication::processEvents();
    CHECK(collapseAct && !collapseAct->isEnabled());
    CHECK(!f.win.collapseSelection("One"));
    CHECK(doc->script().toText() == before);

    // an event cannot be moved: nothing happens, no undo step
    fg::NodeId mainId = 0;
    for (auto& n : doc->graph(graph)->nodes) if (n.def.rfind("event:", 0) == 0) mainId = n.id;
    f.scene()->clearSelection();
    f.scene()->nodeItem(mainId)->setSelected(true);
    f.scene()->nodeItem(a)->setSelected(true);
    QCoreApplication::processEvents();
    CHECK(collapseAct && collapseAct->isEnabled());
    QTimer::singleShot(0, [] {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(w)) box->accept();
    });
    const QString undoBefore = doc->undoLabel();
    CHECK(!f.win.collapseSelection("Nope"));
    CHECK(doc->script().toText() == before && doc->undoLabel() == undoBefore);

    f.scene()->clearSelection();
    f.scene()->nodeItem(a)->setSelected(true);
    f.scene()->nodeItem(b)->setSelected(true);
    QCoreApplication::processEvents();
    CHECK(f.win.collapseSelection("PrintBoth"));
    QCoreApplication::processEvents();
    CHECK(doc->script().findGraph("PrintBoth") != nullptr);
    CHECK(doc->program()->ok() && doc->program()->diagnostics.empty());       // no exec output with two links (entry -> Return)
    const QList<fg::NodeId> sel = f.scene()->selectedNodeIds();
    CHECK(sel.size() == 1 && doc->graph(graph)->findNode(sel.front())->def == "func.call");
    CHECK_EQ(doc->undoLabel(), QString("Collapse to function"));
    doc->undo();
    QCoreApplication::processEvents();
    CHECK(doc->script().toText() == before);
}

// The search boxes of My Script, Details, Find Results and the node search popup look the same: a magnifier, a clear button, a
// placeholder.
TEST(search_fields_look_the_same) {
    StudioFixture f;
    QList<QLineEdit*> fields;
    PalettePopup popup(f.scene(), PinRef());
    if (QLineEdit* e = popup.findChild<QLineEdit*>()) fields << e;
    else CHECK(false);
    for (QWidget* panel : {static_cast<QWidget*>(f.win.findChild<MyScriptPanel*>()), static_cast<QWidget*>(f.win.findChild<DetailsPanel*>()),
                           static_cast<QWidget*>(f.win.findChild<FindPanel*>())}) {
        CHECK(panel != nullptr);
        if (!panel) return;
        QLineEdit* e = panel->findChild<QLineEdit*>();
        CHECK(e != nullptr);
        if (e) fields << e;
    }
    for (QLineEdit* e : fields) {
        CHECK(e->isClearButtonEnabled());
        CHECK(!e->placeholderText().isEmpty());
        int leading = 0;
        for (QAction* act : e->actions()) leading += !act->icon().isNull();
        CHECK(leading >= 1);                                          // the magnifier (the clear button is not one of these actions)
    }
}

// A collapsible panel opens smoothly even when its contents ask for a new height while it opens (My Script refreshes right after an
// item is added): every height it takes on the way is at least the one before, no jump to the end and back.
TEST(collapsible_panel_does_not_jump_while_it_opens) {
    struct HeightLog : QObject {
        std::vector<int> heights;
        bool eventFilter(QObject* o, QEvent* e) override {
            if (e->type() == QEvent::Resize) heights.push_back(static_cast<QWidget*>(o)->height());
            return false;
        }
    };
    QWidget host;
    auto* lay = new QVBoxLayout(&host);
    auto* panel = new CCollapsiblePanel("Section", &host, false);
    auto* contents = new QWidget;
    contents->setFixedHeight(200);
    panel->AddWidget(contents);
    lay->addWidget(panel);
    lay->addStretch();
    host.resize(300, 400);
    host.show();
    QCoreApplication::processEvents();
    const int collapsed = panel->height();
    HeightLog log;
    panel->installEventFilter(&log);
    bool opened = false;
    QObject::connect(panel, &CCollapsiblePanel::ExpandFinished, &host, [&] { opened = true; });
    panel->SetContentsExpaned(true);
    panel->UpdateContentsHeight();                                   // what a refresh of the contents does
    CHECK(spinUntil([&] { return opened; }));
    CHECK(panel->height() >= collapsed + 200);
    CHECK(log.heights.size() > 2);                                   // it really animated
    for (size_t i = 1; i < log.heights.size(); ++i) CHECK(log.heights[i] >= log.heights[i - 1]);
}

// Adding a function or a variable in My Script (a section's "+" or the Add menu) opens the section it goes to, if it is collapsed,
// and starts renaming the new item there.
TEST(adding_in_my_script_expands_the_section) {
    StudioFixture f;
    auto* panel = f.win.findChild<MyScriptPanel*>();
    CHECK(panel != nullptr);
    if (!panel) return;
    const auto sections = panel->findChildren<CCollapsiblePanel*>();      // Event Graphs, Functions, Variables, Local Variables
    const auto trees = panel->findChildren<QTreeWidget*>();
    CHECK(sections.size() == 4 && trees.size() >= 3);
    if (sections.size() != 4 || trees.size() < 3) return;
    CCollapsiblePanel* functions = sections[1];
    CCollapsiblePanel* variables = sections[2];
    auto editing = [](QTreeWidget* tree, const QString& name) {     // the item is current and its name editor is open
        QTreeWidgetItem* cur = tree->currentItem();
        QLineEdit* editor = tree->findChild<QLineEdit*>();
        return cur && cur->text(0) == name && editor && editor->isVisible();
    };

    // the "+" of a collapsed Variables section
    variables->SetContentsExpaned(false);
    CHECK(spinUntil([&] { return variables->height() < 40; }));
    QToolButton* plus = nullptr;
    for (auto* b : variables->findChildren<QToolButton*>()) if (!plus) plus = b;
    CHECK(plus != nullptr);
    if (!plus) return;
    plus->click();
    CHECK(variables->IsContentsExpanded());
    CHECK(spinUntil([&] { return editing(trees[2], "NewVar"); }));
    CHECK(f.win.document()->script().variableIndex("NewVar") >= 0);

    // the Add menu with a collapsed Functions section
    functions->SetContentsExpaned(false);
    CHECK(spinUntil([&] { return functions->height() < 40; }));
    QToolButton* add = nullptr;
    for (auto* b : panel->findChildren<QToolButton*>()) if (b->menu() && !add) add = b;
    CHECK(add != nullptr);
    if (!add) return;
    emit add->menu()->aboutToShow();                               // fills the menu
    QAction* function = nullptr;
    for (QAction* a : add->menu()->actions()) if (a->text() == "Function") function = a;
    CHECK(function != nullptr);
    if (!function) return;
    bool opened = false;
    QObject::connect(functions, &CCollapsiblePanel::ExpandFinished, panel, [&] { opened = true; });
    function->trigger();
    CHECK(functions->IsContentsExpanded());
    CHECK(spinUntil([&] { return editing(trees[1], "NewFunction"); }));
    CHECK(spinUntil([&] { return opened; }));                      // the animation ran to the end
}

// My Script's add button: the chevron has the size of the launch box's chevron, a small gap after the plus, and the same margin on
// both sides of the button.
TEST(my_script_add_button_chevron_and_margins) {
    StudioFixture f;
    auto* panel = f.win.findChild<MyScriptPanel*>();
    auto* launch = f.win.mainToolBar()->findChild<QComboBox*>();
    CHECK(panel && launch);
    if (!panel || !launch) return;
    QToolButton* add = nullptr;
    for (auto* b : panel->findChildren<QToolButton*>()) if (b->menu() && !add) add = b;
    CHECK(add != nullptr);
    if (!add) return;
    auto lum = [](QRgb c) { return qRed(c) * 3 + qGreen(c) * 6 + qBlue(c); };
    // the pixels of `img` in `area` that stand out (more than a third of the strongest) from what `background` says is behind them
    auto box = [&](const QImage& img, QRect area, const std::function<int(int x, int y)>& strength) {
        int best = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x) best = std::max(best, strength(x, y));
        QRect r;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (best > 0 && strength(x, y) * 3 > best) r |= QRect(x, y, 1, 1);
        return r;
    };
    // the launch box: its arrow cell, compared with the cell's left edge (the box has a vertical gradient)
    const QImage comboImg = launch->grab().toImage();
    QStyleOptionComboBox opt;
    opt.initFrom(launch);
    opt.rect = launch->rect();
    opt.editable = launch->isEditable();
    const QRect cell = launch->style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, launch).adjusted(0, 3, -2, -3);
    const QRect launchArrow = box(comboImg, cell, [&](int x, int y) { return std::abs(lum(comboImg.pixel(x, y)) - lum(comboImg.pixel(cell.left(), y))); });
    // the add button's icon: transparent around the plus and the arrow
    const QImage icon = add->icon().pixmap(add->iconSize()).toImage().convertToFormat(QImage::Format_ARGB32);
    auto alpha = [&](int x, int y) { return qAlpha(icon.pixel(x, y)); };
    const QRect plus = box(icon, QRect(0, 0, 15, icon.height()), alpha);
    const QRect arrow = box(icon, QRect(15, 0, icon.width() - 15, icon.height()), alpha);
    CHECK(!launchArrow.isEmpty() && !plus.isEmpty() && !arrow.isEmpty());
    if (launchArrow.isEmpty() || plus.isEmpty() || arrow.isEmpty()) return;
    CHECK(std::abs(arrow.width() - launchArrow.width()) <= 1);       // the same chevron as the launch box
    CHECK(std::abs(arrow.height() - launchArrow.height()) <= 1);
    const int gap = arrow.left() - plus.right() - 1;
    CHECK(gap >= 3 && gap <= 6);                                     // apart, but close
    // the icon is centred in the button: as much room on the left of the plus as on the right of the arrow
    const int left = (add->width() - icon.width()) / 2 + plus.left();
    const int right = add->width() - ((add->width() - icon.width()) / 2 + arrow.right() + 1);
    CHECK(std::abs(left - right) <= 2);
    CHECK(left >= 4 && left <= 9);
}

// A press on a function, variable or local variable that is not selected yet keeps the pressed row: the list is not rebuilt under
// the mouse, so the drag onto a graph can start at once. (A press on another function shows its local variables: only that section
// may be rebuilt.)
TEST(my_script_press_keeps_the_row_so_a_drag_can_start) {
    StudioFixture f;
    Document* doc = f.win.document();
    doc->edit("Setup", [](fg::Script& s) {
        s.addFunction("First").locals.push_back({"A", "int", fg::Value(int64_t(0)), "", ""});
        s.addFunction("Second").locals.push_back({"B", "int", fg::Value(int64_t(0)), "", ""});
        s.variables.push_back({"Score", "int", fg::Value(int64_t(0)), "", ""});
        s.variables.push_back({"Lives", "int", fg::Value(int64_t(3)), "", ""});
    });
    QCoreApplication::processEvents();
    auto* panel = f.win.findChild<MyScriptPanel*>();
    CHECK(panel != nullptr);
    if (!panel) return;
    const auto trees = panel->findChildren<QTreeWidget*>();
    QTreeWidget* functions = trees.at(1);
    QTreeWidget* vars = trees.at(2);
    QTreeWidget* locals = trees.at(3);
    constexpr int kMarker = Qt::UserRole + 50;
    // a real press (and release) on the row, like the user's
    auto press = [&](QTreeWidget* tree, QTreeWidgetItem* item) {
        const QPoint p = tree->visualItemRect(item).center();
        item->setData(0, kMarker, true);                             // a rebuilt row would not have it
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(p), QPointF(tree->viewport()->mapToGlobal(p)), Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &down);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();     // the refresh a selection schedules runs here
        QTreeWidgetItem* now = tree->itemAt(p);
        const bool kept = now && now->data(0, kMarker).toBool() && tree->currentItem() == now;
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(p), QPointF(tree->viewport()->mapToGlobal(p)), Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &up);
        QCoreApplication::processEvents();
        return kept;
    };
    auto row = [](QTreeWidget* tree, const QString& name) -> QTreeWidgetItem* {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) if (tree->topLevelItem(i)->text(0) == name) return tree->topLevelItem(i);
        return nullptr;
    };
    CHECK(row(functions, "First") && row(functions, "Second") && row(vars, "Score") && row(vars, "Lives"));
    if (!row(functions, "First") || !row(functions, "Second") || !row(vars, "Score") || !row(vars, "Lives")) return;

    CHECK(press(functions, row(functions, "First")));
    CHECK(row(locals, "A") != nullptr);                              // its local variables are shown
    CHECK(press(functions, row(functions, "Second")));               // another function: still the pressed row
    CHECK(row(locals, "B") != nullptr && row(locals, "A") == nullptr);
    CHECK(press(vars, row(vars, "Score")));
    CHECK(press(vars, row(vars, "Lives")));
    CHECK(press(locals, row(locals, "B")));
}

// My Script never shows an item as selected while the Details panel shows something else: selecting a node (or clicking on the
// empty canvas) clears the list, so a click on the variable shows its details again. A single event node selects its row instead.
TEST(my_script_selection_follows_the_details_panel) {
    StudioFixture f;
    Document* doc = f.win.document();
    const QString graph = f.scene()->graphName();
    fg::NodeId print = 0;
    doc->edit("Setup", [&](fg::Script& s) {
        s.variables.push_back({"Score", "int", fg::Value(int64_t(0)), "", ""});
        print = s.findGraph(ss(graph))->addNode("util.print", 400, 300).id;
    });
    QCoreApplication::processEvents();
    auto* panel = f.win.findChild<MyScriptPanel*>();
    auto* details = f.win.findChild<DetailsPanel*>();
    CHECK(panel && details);
    if (!panel || !details) return;
    const auto trees = panel->findChildren<QTreeWidget*>();
    QTreeWidget* events = trees.at(0);
    QTreeWidget* vars = trees.at(2);
    auto showsScore = [&] {
        for (auto* e : details->findChildren<QLineEdit*>()) if (e->isVisible() && e->text() == "Score") return true;
        return false;
    };
    CHECK_EQ(vars->topLevelItemCount(), 1);
    if (vars->topLevelItemCount() != 1) return;

    vars->setCurrentItem(vars->topLevelItem(0));                     // the variable ...
    QCoreApplication::processEvents();
    CHECK(showsScore());
    f.scene()->selectOnly(print);                                    // ... then a node ...
    QCoreApplication::processEvents();
    CHECK(!showsScore());
    CHECK(vars->selectedItems().isEmpty() && vars->currentItem() == nullptr);
    f.scene()->clearSelection();                                     // ... then the empty canvas
    QCoreApplication::processEvents();
    CHECK(vars->selectedItems().isEmpty());
    vars->setCurrentItem(vars->topLevelItem(0));                     // a click on the variable shows it again
    emit vars->itemClicked(vars->topLevelItem(0), 0);
    QCoreApplication::processEvents();
    CHECK(showsScore());

    // an event: its row stays selected while its node is the selection, and selecting the node selects the row
    QTreeWidgetItem* main = nullptr;
    for (QTreeWidgetItemIterator it(events); *it; ++it)
        if ((*it)->text(0) == "Main") main = *it;                    // the event of the new script
    CHECK(main != nullptr);
    if (!main) return;
    events->setCurrentItem(main);
    QCoreApplication::processEvents();
    CHECK(main->isSelected());
    CHECK(vars->selectedItems().isEmpty());
    CHECK_EQ(f.scene()->selectedNodeIds().size(), qsizetype(1));
    const fg::NodeId mainNode = f.scene()->selectedNodeIds().front();
    f.scene()->selectOnly(print);
    QCoreApplication::processEvents();
    CHECK(!main->isSelected());
    f.scene()->selectOnly(mainNode);
    QCoreApplication::processEvents();
    CHECK(main->isSelected() && events->currentItem() == main);
}

// Every application that embeds the editor keeps its own dock layout and recent files (QSettings group apps/<application>/);
// what earlier versions kept under editor/ moves there once (the shared layout to the standalone editor that wrote it).
struct NamedHost : TestHost {
    QString name;
    fged::EditorMode m;
    NamedHost(fg::Runtime& r, QString n, fged::EditorMode mode) : TestHost(r), name(std::move(n)), m(mode) {}
    QString applicationName() const override { return name; }
    fged::EditorMode mode() const override { return m; }
};

TEST(layout_and_recent_files_are_per_application) {
    QDir(QDir::temp().filePath("FlowGraphEditorTests-apps")).removeRecursively();
    QDir().mkpath(QDir::temp().filePath("FlowGraphEditorTests-apps"));
    const QString script = QDir::temp().filePath("FlowGraphEditorTests-apps/s.flow");
    StandardRuntime rt;
    {
        fg::Script s;
        fg::standard::createMainTemplate(s, rt.registry());
        s.save(script.toStdString());
    }
    // what an earlier version left behind
    editorSettings().setValue("editor/layout", QByteArray("{not a layout}"));
    editorSettings().setValue("editor/layoutDocks", QStringList{"My Script"});
    editorSettings().setValue("editor/recentFiles/Studio A", QStringList{script});
    auto recentOf = [](ScriptEditorWindow& w) {
        QStringList out;
        if (auto* m = w.findChild<QMenu*>("recentFiles"))
            for (QAction* a : m->actions())
                if (a->toolTip().endsWith(".flow")) out << QFileInfo(a->toolTip()).fileName();
        return out;
    };
    {   // an embedded application does not take the old shared layout, and has no recent files of its own yet
        NamedHost host(rt, "Embedded B", fged::EditorMode::Embedded);
        fged::ScriptEditor editor(host);
        auto& win = *static_cast<ScriptEditorWindow*>(editor.window());
        CHECK(recentOf(win).isEmpty());
        CHECK(editorSettings().contains("editor/layout"));
        CHECK(!editorSettings().contains("apps/Embedded B/layout"));
        editor.show();                                              // restores (nothing) and saves its layout when closed
        QCoreApplication::processEvents();
        win.close();
        QCoreApplication::processEvents();
        CHECK(editorSettings().contains("apps/Embedded B/layout"));
    }
    const QByteArray layoutOfB = editorSettings().value("apps/Embedded B/layout").toByteArray();
    {   // the standalone application takes over the old keys
        NamedHost host(rt, "Studio A", fged::EditorMode::Standalone);
        fged::ScriptEditor editor(host);
        auto& win = *static_cast<ScriptEditorWindow*>(editor.window());
        CHECK(!editorSettings().contains("editor/layout") && !editorSettings().contains("editor/layoutDocks"));
        CHECK(!editorSettings().contains("editor/recentFiles/Studio A"));
        CHECK_EQ(editorSettings().value("apps/Studio A/layout").toByteArray(), QByteArray("{not a layout}"));
        CHECK_EQ(recentOf(win), QStringList({"s.flow"}));
        editor.show();                                              // an unreadable layout is ignored: the default one is used
        QCoreApplication::processEvents();
        win.close();
        QCoreApplication::processEvents();
        CHECK(editorSettings().value("apps/Studio A/layout").toByteArray() != QByteArray("{not a layout}"));
    }
    CHECK_EQ(editorSettings().value("apps/Embedded B/layout").toByteArray(), layoutOfB);    // A saved its own layout, not B's
    {   // what one application opened is not in the other's list
        NamedHost host(rt, "Embedded B", fged::EditorMode::Embedded);
        fged::ScriptEditor editor(host);
        CHECK(recentOf(*static_cast<ScriptEditorWindow*>(editor.window())).isEmpty());
    }
    QDir(QDir::temp().filePath("FlowGraphEditorTests-apps")).removeRecursively();
}

// View > Theme: Dark / Light / System, applied at once (palette, graphs, log, compile results, icons), kept per application and
// applied again by the next editor of that application. An embedded editor offers it only when its host says so.
TEST(theme_is_chosen_kept_and_applied_again) {
    auto paleCount = [](const QIcon& icon) {           // drawn pixels that are near white (the "paper" greys of the dark theme)
        const QImage img = icon.pixmap(QSize(32, 32)).toImage().convertToFormat(QImage::Format_ARGB32);
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.alpha() >= 128 && c.lightness() > 215) ++n;
            }
        return n;
    };
    const int darkIcon = paleCount(themedIcon(":/Icons/save.svg"));
    CHECK(darkIcon > 30);
    {
        StudioFixture f;
        auto* menu = f.win.findChild<QMenu*>("themeMenu");
        CHECK(menu != nullptr);
        if (!menu) return;
        QStringList titles;
        for (QAction* a : menu->actions()) titles << a->text();
        CHECK_EQ(titles, QStringList({"Dark", "Light", "System"}));
        CHECK(menu->actions().at(0)->isChecked() && darkThemeShown());

        f.win.log(int(fg::LogLevel::Error), "something failed");
        QLabel styled("a label with a style sheet");
        styled.setStyleSheet("font-weight: bold");
        styled.show();
        QCoreApplication::processEvents();
        CHECK(styled.palette().color(QPalette::Window) == qApp->palette().color(QPalette::Window));      // dark
        f.win.chooseTheme(Theme::Light);
        QCoreApplication::processEvents();
        CHECK(!darkThemeShown() && currentThemeChoice() == Theme::Light);
        CHECK(qApp->palette().color(QPalette::Window).lightness() > 200);
        CHECK(menu->actions().at(1)->isChecked());
        CHECK_EQ(editorSettings().value("apps/Script Editor/theme").toString(), QString("light"));
        // the error that was logged before takes the light theme's red
        auto* log = f.win.findChild<LogPanel*>();
        CHECK(log != nullptr);
        if (log) {
            QTextBlock last = log->document()->lastBlock();
            QTextCursor c(last);
            c.movePosition(QTextCursor::EndOfBlock);
            CHECK(c.charFormat().foreground().color() == themeColors().error);
            CHECK(themeColors().error.lightness() < 128);
        }
        // every widget takes the new palette: My Script's add button (it once had a style sheet and kept the old theme's colours),
        // and a widget that has a style sheet (as a host's widgets may)
        {
            QToolButton* add = nullptr;
            for (auto* b : f.win.findChild<MyScriptPanel*>()->findChildren<QToolButton*>())
                if (b->toolTip() == "Add a function or a variable") add = b;
            CHECK(add != nullptr);
            if (add) CHECK(add->palette().color(QPalette::Button) == qApp->palette().color(QPalette::Button));
            CHECK(styled.palette().color(QPalette::Window) == qApp->palette().color(QPalette::Window));
            // the editor colours its texts through palettes, never with style sheets
            int sheets = 0;
            for (QWidget* w : f.win.findChildren<QWidget*>()) sheets += !w->styleSheet().isEmpty();
            CHECK_EQ(sheets, 0);
            // the compile summary of the status bar ("OK") in the light theme's green, the rest of its palette light
            QLabel* status = nullptr;
            for (QLabel* l : f.win.statusBar()->findChildren<QLabel*>())
                if (l->text() == "OK") status = l;
            CHECK(status != nullptr);
            if (status) {
                CHECK(status->palette().color(QPalette::WindowText) == themeColors().success);
                CHECK(status->palette().color(QPalette::Window) == qApp->palette().color(QPalette::Window));
            }
        }
        // the graph view and the icons: light canvas, darker icon greys
        CHECK(themeColors().canvas.lightness() > 200 && themeColors().nodeBody == QColor(255, 255, 255));
        CHECK(paleCount(themedIcon(":/Icons/save.svg")) < darkIcon / 4);
        // System is light or dark like the operating system
        f.win.chooseTheme(Theme::System);
        CHECK(currentThemeChoice() == Theme::System);
        CHECK_EQ(darkThemeShown(), QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light);
        f.win.chooseTheme(Theme::Light);
    }
    {   // the next editor of the same application comes up in the saved theme
        applyTheme(*qApp, Theme::Dark);
        StudioFixture f;
        CHECK(!darkThemeShown());
        auto* menu = f.win.findChild<QMenu*>("themeMenu");
        CHECK(menu && menu->actions().at(1)->isChecked());
        f.win.chooseTheme(Theme::Dark);
        CHECK(darkThemeShown());
        CHECK_EQ(editorSettings().value("apps/Script Editor/theme").toString(), QString("dark"));
    }
    {   // an embedded editor offers no theme unless its host does
        StandardRuntime rt;
        NamedHost host(rt, "Embedded T", fged::EditorMode::Embedded);
        fged::ScriptEditor editor(host);
        CHECK(editor.window()->findChild<QMenu*>("themeMenu") == nullptr);
    }
    editorSettings().remove("apps/Script Editor/theme");
    applyTheme(*qApp, Theme::Dark);
    CHECK_EQ(paleCount(themedIcon(":/Icons/save.svg")), darkIcon);
}

// File > Open Recent lists what was opened and saved, newest first, and opens it again; a file that is gone is dropped.
TEST(studio_open_recent_files) {
    const QString dir = QDir::temp().filePath("FlowGraphEditorTests-recent");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString a = QDir(dir).filePath("a.flow"), b = QDir(dir).filePath("b.flow");
    StudioFixture f;
    auto* recent = f.win.findChild<QMenu*>("recentFiles");
    CHECK(recent != nullptr);
    if (!recent) return;
    auto entries = [&] {
        QStringList out;
        for (QAction* act : recent->actions())
            if (!act->isSeparator() && !act->toolTip().isEmpty() && act->toolTip().endsWith(".flow")) out << QFileInfo(act->toolTip()).fileName();
        return out;
    };
    CHECK(entries().isEmpty() && !recent->isEnabled());
    CHECK(f.win.document()->save(a, nullptr));                      // the new script, under two names
    CHECK(f.win.document()->save(b, nullptr));
    CHECK(f.win.openFile(a));
    CHECK(f.win.openFile(b));
    CHECK_EQ(entries(), QStringList({"b.flow", "a.flow"}));
    CHECK(f.win.openFile(a));                                       // opened again: back on top, not twice
    CHECK_EQ(entries(), QStringList({"a.flow", "b.flow"}));
    // choosing an entry opens it
    for (QAction* act : recent->actions())
        if (act->toolTip().endsWith("b.flow")) act->trigger();
    QCoreApplication::processEvents();
    CHECK(QFileInfo(f.win.document()->path()).fileName() == "b.flow");
    // a file that is gone: a message (closed at once here) and the entry is dropped
    QFile::remove(a);
    QTimer::singleShot(0, [] {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(w)) box->accept();
    });
    for (QAction* act : recent->actions())
        if (act->toolTip().endsWith("a.flow")) act->trigger();
    QCoreApplication::processEvents();
    CHECK_EQ(entries(), QStringList({"b.flow"}));
    QDir(dir).removeRecursively();
}

TEST(studio_window_node_move_sticks) {
    StudioFixture f;
    CHECK(f.view != nullptr);
    if (!f.view) return;
    NodeItem* n = f.scene()->nodeItem(2);
    CHECK(n != nullptr);
    if (!n) return;
    QPointF before = n->scenePos();
    QPoint header = f.toView(n->mapToScene(QPointF(20, 8)));
    f.drag(header, header + QPoint(90, 60));
    QCoreApplication::processEvents();
    QPointF after = f.scene()->nodeItem(2)->scenePos();
    CHECK(std::abs(after.x() - before.x()) > 30);
    CHECK(std::abs(f.win.document()->graph(f.scene()->graphName())->findNode(2)->x - after.x()) < 0.5);
}

TEST(studio_window_pin_drag_creates_link) {
    StudioFixture f;
    CHECK(f.view != nullptr);
    if (!f.view) return;
    GraphScene* sc = f.scene();
    auto all = fg::buildPalette(fg::NodeBuildCtx{f.rt.registry(), &f.win.document()->script(), f.win.document()->script().findGraph(ss(sc->graphName()))});
    fg::NodeId br = sc->addNodeFromTemplate(*findTemplate(all, "flow.branch"), QPointF(600, 0));
    QCoreApplication::processEvents();
    f.view->centerOn(400, 50);
    QCoreApplication::processEvents();
    QPoint from = f.toView(sc->nodeItem(2)->pinScenePos("Then", fg::PinDir::Out));
    QPoint to = f.toView(sc->nodeItem(br)->pinScenePos("In", fg::PinDir::In));
    f.drag(from, to);
    bool linked = false;
    for (auto& c : f.win.document()->graph(sc->graphName())->connections) linked |= c.fromNode == 2 && c.toNode == br && c.toPin == "In";
    CHECK(linked);
}

TEST(event_list_shows_only_the_events_the_script_handles) {
    Fixture f;
    f.rt.registry().addEvent("Tick", {{"DeltaTime", "float"}});
    f.doc.refresh();
    MyScriptPanel panel(&f.doc);
    QCoreApplication::processEvents();
    auto trees = panel.findChildren<QTreeWidget*>();
    CHECK(!trees.isEmpty());
    QTreeWidget* events = trees.front();               // the first section is "Event Graphs"
    auto find = [&](const QString& name) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(events); *it; ++it)
            if ((*it)->text(0) == name) return *it;
        return nullptr;
    };
    QTreeWidgetItem* main = find("Main");
    CHECK(main != nullptr);
    CHECK(find("Tick") == nullptr);                    // the application raises Tick, but this script does not handle it
    if (main) CHECK(!(main->flags() & Qt::ItemIsEditable));      // events cannot be renamed
    // handling Tick (a node of the event appears in a graph) lists it
    f.doc.edit("Handle Tick", [](fg::Script& s) { s.graphs[0].addNode("event:Tick", 0, 300); });
    QCoreApplication::processEvents();
    CHECK(find("Tick") != nullptr);
    if (auto* tick = find("Tick")) CHECK(tick->text(1).contains("DeltaTime"));
    // the section has no "+" button (functions, variables and local variables have one)
    int plus = 0;
    for (auto* b : panel.findChildren<QToolButton*>()) plus += b->text() == "+";
    CHECK_EQ(plus, 4);                                 // those three and the "add" button next to the search box
    // a second event graph (made by a file or a host) is not listed: the section holds events only, and there is no
    // way to create event graphs from it
    f.doc.edit("Second event graph", [](fg::Script& sc) { sc.addEventGraph("Other"); });
    QCoreApplication::processEvents();
    CHECK(find("Other") == nullptr);
    CHECK(find("EventGraph") == nullptr);
    for (QTreeWidgetItemIterator it(events); *it; ++it) CHECK((*it)->parent() == nullptr);
    CHECK_EQ(events->topLevelItemCount(), 2);          // Main and Tick
}

TEST(my_script_search_filters_and_the_add_button_offers_what_can_be_added) {
    Fixture f;
    f.doc.edit("Add", [](fg::Script& s) {
        s.addFunction("Compute");
        s.addFunction("Other");
        s.variables.push_back({"Counter", "int", fg::Value(int64_t(0)), "", ""});
        s.variables.push_back({"Name", "string", fg::Value(std::string()), "", ""});
        s.findGraph("Compute")->locals.push_back({"countdown", "int", fg::Value(int64_t(0)), "", ""});
    });
    MyScriptPanel panel(&f.doc);
    panel.show();
    QCoreApplication::processEvents();
    auto trees = panel.findChildren<QTreeWidget*>();
    CHECK_EQ(trees.size(), 4);
    if (trees.size() != 4) return;
    auto visibleRows = [](QTreeWidget* t) {
        int n = 0;
        for (QTreeWidgetItemIterator it(t); *it; ++it) n += !(*it)->isHidden();
        return n;
    };
    auto* search = panel.findChild<QLineEdit*>();
    CHECK(search != nullptr);
    if (!search) return;
    CHECK_EQ(visibleRows(trees[1]), 2);                    // functions
    CHECK_EQ(visibleRows(trees[2]), 2);                    // variables
    // select a function: its local variables are listed
    trees[1]->setCurrentItem(trees[1]->topLevelItem(0));
    for (int i = 0; i < trees[1]->topLevelItemCount(); ++i)
        if (trees[1]->topLevelItem(i)->text(0) == "Compute") trees[1]->setCurrentItem(trees[1]->topLevelItem(i));
    QCoreApplication::processEvents();
    CHECK_EQ(visibleRows(trees[3]), 1);
    // the search matches every section, ignoring the case
    search->setText("CO");
    QCoreApplication::processEvents();
    CHECK_EQ(visibleRows(trees[1]), 1);                    // Compute
    CHECK_EQ(visibleRows(trees[2]), 1);                    // Counter
    CHECK_EQ(visibleRows(trees[3]), 1);                    // countdown
    CHECK(!trees[0]->isVisibleTo(&panel) || visibleRows(trees[0]) == 0);     // Main does not match: its section disappears
    search->setText("zzz");
    QCoreApplication::processEvents();
    for (auto* t : trees) CHECK(!t->isVisibleTo(&panel));
    search->clear();
    QCoreApplication::processEvents();
    for (auto* t : trees) CHECK(t->isVisibleTo(&panel));
    // the add button: function and variable always, local variable only while a function is selected
    QToolButton* add = nullptr;
    for (auto* b : panel.findChildren<QToolButton*>()) if (b->menu()) add = b;
    CHECK(add != nullptr);
    if (!add) return;
    auto entries = [&] {
        emit add->menu()->aboutToShow();
        QStringList out;
        for (auto* a : add->menu()->actions()) out << a->text();
        return out;
    };
    auto withLocal = entries();
    CHECK_EQ(withLocal.size(), 3);
    CHECK(withLocal.value(0) == "Function" && withLocal.value(1) == "Variable" && withLocal.value(2).startsWith("Local Variable"));
    trees[0]->setCurrentItem(trees[0]->topLevelItem(0));      // an event is selected: no function any more
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK_EQ(entries().size(), 2);
    // adding clears the search so that the new item is visible
    search->setText("Counter");
    add->menu()->actions().at(1)->trigger();
    QCoreApplication::processEvents();
    CHECK(search->text().isEmpty());
    CHECK_EQ(f.doc.script().variables.size(), size_t(3));
}

TEST(details_search_shows_only_the_matching_properties) {
    Fixture f;
    f.doc.edit("Add", [](fg::Script& sc) { sc.variables.push_back({"Counter", "int", fg::Value(int64_t(0)), "", ""}); });
    DetailsPanel panel(&f.doc);
    panel.show();
    panel.showVariable("", "Counter");
    QCoreApplication::processEvents();
    auto* search = panel.findChild<QLineEdit*>();
    CHECK(search != nullptr);
    if (!search) return;
    auto visibleLabels = [&] {
        QStringList out;
        for (auto* l : panel.findChildren<QLabel*>()) if (l->isVisibleTo(&panel) && !l->text().isEmpty()) out << l->text();
        return out;
    };
    auto hasLabel = [&](const QString& name) { return visibleLabels().contains(name); };
    CHECK(hasLabel("Name") && hasLabel("Type") && hasLabel("Category"));
    search->setText("categ");                         // ignores the case, matches the name of a property
    QCoreApplication::processEvents();
    CHECK(hasLabel("Category") && !hasLabel("Name") && !hasLabel("Type"));
    search->setText("Counter");                       // matches a value: the Name field holds it
    QCoreApplication::processEvents();
    CHECK(hasLabel("Name") && !hasLabel("Category"));
    search->setText("zzz");
    QCoreApplication::processEvents();
    CHECK(!hasLabel("Name") && !hasLabel("Category"));
    bool noMatch = false;
    for (auto* l : panel.findChildren<QLabel*>()) noMatch |= l->isVisibleTo(&panel) && l->text().contains("Nothing matches");
    CHECK(noMatch);
    // the filter stays when another item is shown
    panel.showNothing();
    panel.showVariable("", "Counter");
    QCoreApplication::processEvents();
    CHECK(!hasLabel("Name"));
    search->clear();
    QCoreApplication::processEvents();
    CHECK(hasLabel("Name") && hasLabel("Category"));
}

TEST(output_log_context_menu_clears_the_log) {
    LogPanel log;
    log.append(0, "first");
    log.append(2, "second");
    CHECK(!log.document()->isEmpty());
    std::unique_ptr<QMenu> menu(log.buildContextMenu());
    QAction* clear = nullptr;
    for (auto* a : menu->actions()) if (a->text() == "Clear") clear = a;
    CHECK(clear != nullptr);
    if (!clear) return;
    CHECK(clear->isEnabled());
    clear->trigger();
    CHECK(log.document()->isEmpty());
    std::unique_ptr<QMenu> again(log.buildContextMenu());
    for (auto* a : again->actions()) if (a->text() == "Clear") CHECK(!a->isEnabled());    // nothing to clear
}

// ------------------------------------------------------------------ drag and drop of variables and functions onto a graph
namespace {
// Counts the nodes of a definition in the graph, optionally with a property.
int countNodes(const fg::Graph& g, const std::string& def, const std::string& prop = "", const std::string& value = "") {
    int n = 0;
    for (auto& node : g.nodes)
        if (node.def == def && (prop.empty() || node.prop(prop) == value)) ++n;
    return n;
}
// What the user does: drops `mime` on the view (the keys held are `modifiers`).
void dropOn(GraphView& view, const QMimeData& mime, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    // the system always announces a drag before the drop
    QDragEnterEvent enter(QPoint(200, 150), Qt::CopyAction, &mime, Qt::LeftButton, modifiers);
    QApplication::sendEvent(view.viewport(), &enter);
    QDropEvent drop(QPointF(200, 150), Qt::CopyAction, &mime, Qt::LeftButton, modifiers);
    QApplication::sendEvent(view.viewport(), &drop);
}
QMenu* shownMenu() { return qobject_cast<QMenu*>(QApplication::activePopupWidget()); }
void key(QWidget* w, Qt::Key k) {
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
}
} // namespace

TEST(dropping_a_variable_on_the_graph_asks_whether_to_get_or_set_it) {
    Fixture f;
    f.doc.edit("Add var", [](fg::Script& s) { s.variables.push_back({"Score", "int", fg::Value(5), "", ""}); });
    GraphView view(&f.scene);
    view.resize(600, 400);
    view.show();
    QMimeData mime;
    mime.setData(GraphView::variableMime(), "global|Score");
    auto variableNodes = [&] { return countNodes(f.graph(), "var.get") + countNodes(f.graph(), "var.set"); };

    // No key held: a menu with Get and Set appears, and the choice decides the node. The menu runs its own event loop, so the "user" is a timer.
    QStringList texts;
    int nodesWhileAsking = -1;
    QTimer::singleShot(100, [&] {
        QMenu* menu = shownMenu();
        if (!menu) return;
        for (auto* a : menu->actions()) texts << a->text();
        nodesWhileAsking = variableNodes();
        key(menu, Qt::Key_Down);
        key(menu, Qt::Key_Down);
        key(menu, Qt::Key_Return);                       // "Set Score"
    });
    dropOn(view, mime);
    CHECK(shownMenu() == nullptr);                       // not inside the drop: the menu comes when the drop is over
    CHECK(spinUntil([&] { return countNodes(f.graph(), "var.set", "name", "Score") == 1; }));
    CHECK_EQ(texts, QStringList({"Get Score", "Set Score"}));
    CHECK_EQ(nodesWhileAsking, 0);                       // nothing is made before the choice
    CHECK_EQ(countNodes(f.graph(), "var.get"), 0);
    CHECK(f.doc.program()->ok());

    // Escape: no node
    bool asked = false;
    QTimer::singleShot(100, [&] {
        if (QMenu* menu = shownMenu()) {
            asked = true;
            key(menu, Qt::Key_Escape);
        }
    });
    dropOn(view, mime);
    CHECK(spinUntil([&] { return asked; }));
    spinUntil([] { return shownMenu() == nullptr; }, 1000);
    CHECK_EQ(variableNodes(), 1);

    // Ctrl = Get and Alt = Set at once, without a menu
    dropOn(view, mime, Qt::ControlModifier);
    CHECK_EQ(countNodes(f.graph(), "var.get", "name", "Score"), 1);
    dropOn(view, mime, Qt::AltModifier);
    CHECK_EQ(countNodes(f.graph(), "var.set", "name", "Score"), 2);
    spinUntil([] { return false; }, 100);
    CHECK(shownMenu() == nullptr);
}

TEST(dropping_a_function_on_the_graph_makes_a_node_that_calls_it) {
    Fixture f;
    f.doc.edit("Add function", [](fg::Script& s) { s.addFunction("Helper"); });
    GraphView view(&f.scene);
    view.resize(600, 400);
    view.show();
    QMimeData mime;
    mime.setData(GraphView::functionMime(), "Helper");
    dropOn(view, mime);
    CHECK_EQ(countNodes(f.graph(), "func.call", "function", "Helper"), 1);
    CHECK(f.doc.program()->ok());
    CHECK(shownMenu() == nullptr);                      // no question: a function has only one thing to do
    // a name that is not a function of the script makes nothing
    QMimeData unknown;
    unknown.setData(GraphView::functionMime(), "Nope");
    dropOn(view, unknown);
    CHECK_EQ(countNodes(f.graph(), "func.call"), 1);
}

TEST(functions_of_my_script_can_be_dragged) {
    Fixture f;
    f.doc.edit("Add function", [](fg::Script& s) { s.addFunction("Helper"); });
    MyScriptPanel panel(&f.doc);
    panel.show();
    QCoreApplication::processEvents();
    bool found = false;
    for (auto* tree : panel.findChildren<FitTree*>())
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->text(0) == "Helper") {
                found = true;
                CHECK(tree->dragEnabled());
                CHECK_EQ(tree->topLevelItem(i)->data(0, Qt::UserRole + 2).toString(), QString("Helper"));     // what the drag carries
            }
    CHECK(found);
}

TEST(details_shows_the_pins_of_every_node_and_edits_only_the_custom_ones) {
    StandardRuntime rt;
    {
        // a node of the application: fixed exec pins, and data inputs the user adds
        fg::NodeDef d;
        d.id = "test.row";
        d.title = "Row";
        d.category = "Test";
        d.pins = {fg::execIn(), fg::execOut()};
        d.customInputs = true;
        d.run = [](fg::NodeCtx&) {};
        rt.registry().addNode(std::move(d));
    }
    Document doc(rt);
    doc.newScript([&](fg::Script& s) { fg::standard::createMainTemplate(s, rt.registry()); });
    fg::NodeId row = 0, concat = 0;
    doc.edit("Nodes", [&](fg::Script& s) {
        fg::Graph& g = s.graphs[0];
        row = g.addNode("test.row", 400, 200).id;
        g.findNode(row)->inputs = {{"A", "string", false, false, fg::Value()}, {"B", "int", false, false, fg::Value()}};
        concat = g.addNode("fn:String.Concat", 100, 200).id;
        g.connections.push_back({concat, "Result", row, "A"});
    });
    CHECK(doc.program()->ok());

    DetailsPanel panel(&doc);
    panel.show();
    auto table = [&](const char* name) { return panel.findChild<QTableWidget*>(name); };
    auto show = [&](fg::NodeId id) {
        panel.showNodes("EventGraph", {id});
        QCoreApplication::processEvents();
    };
    auto customNode = [&]() { return doc.script().graphs[0].findNode(row); };
    auto linkedTo = [&](const char* pin) {
        for (auto& c : doc.script().graphs[0].connections)
            if (c.toNode == row && c.toPin == pin) return true;
        return false;
    };

    // a built-in node: its inputs and outputs are listed and nothing can be changed (they belong to the definition of the node)
    show(concat);
    QTableWidget* in = table("fixedInputs");
    QTableWidget* out = table("fixedOutputs");
    CHECK(in && in->rowCount() == 2 && out && out->rowCount() == 1);
    if (in && out) {
        CHECK_EQ(in->item(0, 0)->text(), QString("A"));
        CHECK_EQ(in->item(0, 1)->text(), QString("string"));
        CHECK_EQ(out->item(0, 0)->text(), QString("Result"));
        CHECK(!(in->item(0, 0)->flags() & Qt::ItemIsEditable) && !(in->item(0, 1)->flags() & Qt::ItemIsEditable));
    }
    CHECK(table("customInputs") == nullptr && table("customOutputs") == nullptr);

    // a node that allows custom pins: the fixed pins stay read only, the custom ones can be edited
    show(row);
    in = table("fixedInputs");
    CHECK(in && in->rowCount() == 1 && in->item(0, 1)->text() == "exec");      // the "In" pin of the definition
    QTableWidget* custom = table("customInputs");
    CHECK(custom && custom->rowCount() == 2);
    CHECK(table("customOutputs") == nullptr);                                  // this node has fixed outputs only
    if (!custom) return;
    CHECK_EQ(custom->item(0, 0)->text(), QString("A"));

    custom->item(0, 0)->setText("Name");                                        // rename: the link follows the pin
    QCoreApplication::processEvents();
    CHECK_EQ(customNode()->inputs[0].name, std::string("Name"));
    CHECK(linkedTo("Name") && !linkedTo("A"));
    CHECK(doc.program()->ok());

    for (auto* p : panel.findChildren<CCollapsiblePanel*>()) emit p->PlusButtonClicked();       // "+" of the Inputs panel
    QCoreApplication::processEvents();
    CHECK_EQ(customNode()->inputs.size(), size_t(3));
    CHECK_EQ(customNode()->inputs[2].name, std::string("Input"));
    CHECK(doc.program()->ok());

    custom = table("customInputs");
    custom->item(1, 1)->setText("float");                                       // the type of a column
    QCoreApplication::processEvents();
    CHECK_EQ(customNode()->inputs[1].type, std::string("float"));

    doc.undo();                                                                 // each change is one undoable edit
    doc.undo();
    doc.undo();
    QCoreApplication::processEvents();
    CHECK_EQ(customNode()->inputs.size(), size_t(2));
    CHECK_EQ(customNode()->inputs[0].name, std::string("A"));
    CHECK(linkedTo("A"));
}

// ------------------------------------------------------------------ the editor in an application that owns the scripts
namespace {
// An application that embeds the editor: it owns the scripts, runs them itself and takes the script back when the user saves.
struct EmbeddedHost : fged::EditorHost {
    fg::Runtime& rt;
    fg::Script applied;
    int applyCount = 0;
    bool accept = true;
    std::shared_ptr<fg::AsyncExecutor> target;
    std::string requiredEvent;                      // the event this application is made for: a script must handle it
    explicit EmbeddedHost(fg::Runtime& r) : rt(r) {}
    fg::Runtime& runtime() override { return rt; }
    QString applicationName() const override { return "Embedded test"; }
    fged::EditorMode mode() const override { return fged::EditorMode::Embedded; }
    bool acceptsScript(const fg::Script& s, QString* reason) override {
        if (requiredEvent.empty()) return true;
        for (auto& g : s.graphs)
            for (auto& n : g.nodes)
                if (n.def == "event:" + requiredEvent) return true;
        if (reason) *reason = QString("The script does not handle the event %1.").arg(QString::fromStdString(requiredEvent));
        return false;
    }
    bool applyScript(fged::ScriptEditor&, const fg::Script& s) override {
        ++applyCount;
        if (accept) applied = s;
        return accept;
    }
    std::shared_ptr<fg::AsyncExecutor> debugTarget(fged::ScriptEditor&) override { return target; }
};

fg::Script mainScript(fg::Runtime& rt) {
    fg::Script s;
    fg::standard::createMainTemplate(s, rt.registry());
    return s;
}

QAction* actionNamed(QWidget* w, const QString& text) {
    for (auto* a : w->findChildren<QAction*>())
        if (QString(a->text()).remove('&') == text) return a;
    return nullptr;
}

QStringList menuTexts(QMenu* m) {
    QStringList out;
    for (auto* a : m->actions())
        if (!a->isSeparator()) out << QString(a->text()).remove('&');
    return out;
}

QMenu* menuTitled(QMainWindow* w, const QString& title) {
    for (auto* a : w->menuBar()->actions())
        if (a->menu() && QString(a->menu()->title()).remove('&') == title) return a->menu();
    return nullptr;
}
} // namespace

TEST(a_program_has_one_script_editor) {
    StandardRuntime rt;
    flowstudio::StandardEditorHost host(rt);
    CHECK(fged::ScriptEditor::instance() == nullptr);
    {
        fged::ScriptEditor first(host);
        CHECK(fged::ScriptEditor::instance() == &first);
        bool threw = false;
        try {
            fged::ScriptEditor second(host);
        } catch (const std::logic_error&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(fged::ScriptEditor::instance() == &first);          // the first one is not harmed
    }
    CHECK(fged::ScriptEditor::instance() == nullptr);
    fged::ScriptEditor again(host);                               // once it is gone another one can be made
    CHECK(fged::ScriptEditor::instance() == &again);
}

TEST(standalone_editor_keeps_its_files_and_run_commands) {
    StandardRuntime rt;
    flowstudio::StandardEditorHost host(rt);
    fged::ScriptEditor editor(host);
    auto* win = static_cast<ScriptEditorWindow*>(editor.window());
    CHECK(!win->embedded());
    QMenu* file = menuTitled(win, "File");
    CHECK(file && menuTexts(file) == QStringList({"New", "Open...", "Open Recent", "Save", "Save As...", "Exit"}));
    CHECK(menuTitled(win, "Run") != nullptr && menuTitled(win, "Debug") == nullptr);
    CHECK(actionNamed(win, "Attach") == nullptr);
}

TEST(embedded_editor_edits_the_script_of_the_application_and_save_gives_it_back) {
    StandardRuntime rt;
    EmbeddedHost host(rt);
    fged::ScriptEditor editor(host);
    auto* win = static_cast<ScriptEditorWindow*>(editor.window());
    CHECK(win->embedded());
    // no New / Exit, no Run: Open (of scripts that fit), Save, Save As and the debugger's Attach / Detach instead
    QMenu* file = menuTitled(win, "File");
    CHECK(file && menuTexts(file) == QStringList({"Open...", "Open Recent", "Save", "Save As..."}));
    CHECK(menuTitled(win, "Run") == nullptr);
    QMenu* debug = menuTitled(win, "Debug");
    CHECK(debug && menuTexts(debug).contains("Attach") && menuTexts(debug).contains("Detach"));
    CHECK(actionNamed(win, "Attach") && actionNamed(win, "Attach")->isEnabled());
    CHECK(actionNamed(win, "Detach") && !actionNamed(win, "Detach")->isEnabled());
    CHECK(actionNamed(win, "Stop") && !actionNamed(win, "Stop")->isEnabled());

    // the application hands a script over
    CHECK(editor.edit(mainScript(rt), "Table 1"));
    CHECK(win->windowTitle().startsWith("Table 1"));
    CHECK(!win->document()->isDirty() && win->document()->path().isEmpty());
    CHECK(win->document()->program()->ok());

    // Save does not ask for a file: it gives the edited script to the application
    win->document()->edit("Change", [](fg::Script& s) { s.graphs[0].nodes[1].defaults["String"] = fg::Value("changed"); });
    CHECK(win->document()->isDirty());
    actionNamed(win, "Save")->trigger();
    CHECK_EQ(host.applyCount, 1);
    CHECK(!win->document()->isDirty());
    CHECK(host.applied.graphs[0].nodes[1].defaults.at("String").toString() == "changed");

    // an application that cannot use the script keeps the changes unsaved
    host.accept = false;
    win->document()->edit("Change again", [](fg::Script& s) { s.graphs[0].nodes[1].defaults["String"] = fg::Value("again"); });
    actionNamed(win, "Save")->trigger();
    CHECK_EQ(host.applyCount, 2);
    CHECK(win->document()->isDirty());
    host.accept = true;
    actionNamed(win, "Save")->trigger();
    CHECK(!win->document()->isDirty());
    CHECK(host.applied.graphs[0].nodes[1].defaults.at("String").toString() == "again");

    // another script: nothing of the first one is left
    CHECK(editor.edit(mainScript(rt), "Table 2"));
    CHECK(win->windowTitle().startsWith("Table 2"));
    CHECK(win->document()->script().graphs[0].nodes[1].defaults.count("String") == 0);
    CHECK(!win->document()->canUndo());
}

TEST(embedded_editor_takes_only_scripts_that_fit_the_application) {
    StandardRuntime rt;                              // the application raises one event: Main
    EmbeddedHost host(rt);
    host.requiredEvent = "Main";                     // and a script has to handle it
    fged::ScriptEditor editor(host);
    auto* win = static_cast<ScriptEditorWindow*>(editor.window());
    CHECK(editor.edit(mainScript(rt), "Good"));
    auto stillGood = [&] {
        return win->windowTitle().startsWith("Good") && win->document()->script().graphs.size() == 1 &&
               win->document()->script().graphs[0].nodes.size() == 2 && win->document()->program()->ok();
    };
    CHECK(stillGood());

    // a script made for another application: it uses an event this one never raises. Nothing is loaded.
    fg::Script foreign;
    foreign.name = "Foreign";
    foreign.addEventGraph().addNode("event:Explode");
    CHECK(!editor.edit(foreign, "Foreign"));
    CHECK(stillGood());
    // a script the runtime understands but that does not handle the event the application is made for
    fg::Script empty;
    empty.name = "Empty";
    empty.addEventGraph();
    CHECK(!editor.edit(empty, "Empty"));
    CHECK(stillGood());

    // File > Open has the same rule: the files of scripts that do not fit are refused and the editor is unchanged
    const QString dir = QDir::temp().filePath("embedded-editor-accept");
    QDir().mkpath(dir);
    foreign.save((dir + "/foreign.flow").toStdString());
    empty.save((dir + "/empty.flow").toStdString());
    mainScript(rt).save((dir + "/good.flow").toStdString());
    CHECK(!editor.open(dir + "/foreign.flow"));
    CHECK(!editor.open(dir + "/empty.flow"));
    CHECK(stillGood() && win->document()->path().isEmpty());
    // a script that fits is taken, by edit() and by Open
    CHECK(editor.open(dir + "/good.flow"));
    CHECK(win->document()->path() == dir + "/good.flow" && win->document()->program()->ok());
    CHECK(editor.edit(mainScript(rt), "Again"));
    CHECK(win->windowTitle().startsWith("Again"));
    for (auto* b : win->findChildren<QMessageBox*>()) b->close();       // the notices about the refused scripts
}

TEST(embedded_debugger_attaches_to_the_executor_of_the_application) {
    StandardRuntime rt;
    EmbeddedHost host(rt);
    fged::ScriptEditor editor(host);
    auto* win = static_cast<ScriptEditorWindow*>(editor.window());
    RunController& run = win->run();
    fg::Script script = mainScript(rt);
    CHECK(editor.edit(script, "Table 1"));
    auto executor = std::make_shared<fg::AsyncExecutor>(rt.compile(script));      // the application runs the script
    host.target = executor;
    bool attachedSignal = false;
    QObject::connect(&editor, &fged::ScriptEditor::attachedChanged, [&](bool on) { attachedSignal = on; });

    // Attach asks the application for its executor
    CHECK(!editor.isAttached());
    actionNamed(win, "Attach")->trigger();
    CHECK(editor.isAttached() && attachedSignal);
    CHECK(!actionNamed(win, "Attach")->isEnabled() && actionNamed(win, "Detach")->isEnabled());
    CHECK(actionNamed(win, "Pause") && actionNamed(win, "Pause")->isEnabled());                  // the one button of Run is Pause while attached

    // a breakpoint stops the tasks the application starts; two events stop two tasks
    win->document()->debugger().setBreakpoint("EventGraph", 2, true);
    executor->postEvent("Main");
    executor->postEvent("Main");
    CHECK(spinUntil([&] { return run.state() == RunController::State::Paused; }));
    RunController::DebugSnapshot snap;
    CHECK(run.snapshot(snap) && !snap.stack.empty() && snap.stack.front().graph == "EventGraph");
    CHECK(actionNamed(win, "Step Over")->isEnabled());
    run.stepInto();                                    // runs the node of the first task; the second task is stopped at the same place
    CHECK(spinUntil([&] { return run.state() == RunController::State::Paused; }));
    run.resume();
    CHECK(spinUntil([&] { return run.state() == RunController::State::Running; }));
    CHECK(executor->waitIdle(5000));                   // both tasks finished

    // Pause stops the next task that runs
    run.pause();
    executor->postEvent("Main");
    CHECK(spinUntil([&] { return run.state() == RunController::State::Paused; }));

    // Detach lets what is stopped continue, and the application's script no longer stops
    editor.detach();
    CHECK(!editor.isAttached() && !attachedSignal);
    CHECK(run.state() == RunController::State::Idle);
    CHECK(executor->waitIdle(5000));
    executor->postEvent("Main");                       // the breakpoint is still there, but nobody listens
    CHECK(executor->waitIdle(5000));

    // a structural edit does not end the session (the application keeps running the script it has); another script does
    actionNamed(win, "Attach")->trigger();
    CHECK(editor.isAttached());
    win->document()->edit("Change", [](fg::Script& s) { s.graphs[0].nodes[1].defaults["String"] = fg::Value("x"); });
    CHECK(editor.isAttached());
    actionNamed(win, "Save")->trigger();
    CHECK(editor.edit(mainScript(rt), "Table 2"));
    CHECK(!editor.isAttached());
    executor.reset();
}

TEST(attached_debugger_steps_through_one_task_and_goes_on_with_the_next_when_it_ends) {
    StandardRuntime rt;
    EmbeddedHost host(rt);
    fged::ScriptEditor editor(host);
    auto* win = static_cast<ScriptEditorWindow*>(editor.window());
    RunController& run = win->run();
    // Main -> Print (node 2) -> Print (node 3): a script of three nodes
    fg::Script script = mainScript(rt);
    {
        fg::Graph& g = script.graphs[0];
        const fg::NodeId second = g.addNode("util.print", 600, 0).id;
        g.connections.push_back({2, "Then", second, "In"});
    }
    CHECK(editor.edit(script, "T"));
    auto executor = std::make_shared<fg::AsyncExecutor>(rt.compile(script));
    host.target = executor;
    int stops = 0;
    int lastNode = 0;
    QObject::connect(&run, &RunController::paused, [&](const QString&, int node) { ++stops; lastNode = node; });
    CHECK(win->attachExecutor(executor));
    win->document()->debugger().setBreakpoint("EventGraph", 2, true);
    // three messages: three tasks, all stopped at the breakpoint
    executor->postEvent("Main");
    executor->postEvent("Main");
    executor->postEvent("Main");
    CHECK(spinUntil([&] { return run.state() == RunController::State::Paused; }));
    spinUntil([] { return false; }, 150);                // the other two reach their breakpoint too
    CHECK_EQ(lastNode, 2);
    const int stopsBefore = stops;

    // Step Over: the SAME task goes on to the next node; the other tasks stay where they are
    run.stepOver();
    CHECK(spinUntil([&] { return stops > stopsBefore; }));
    CHECK_EQ(lastNode, 3);
    CHECK(run.state() == RunController::State::Paused);
    // Step Into at the last node: the task ends, and the debugger goes on with the next task, stopped at the breakpoint
    const int stops2 = stops;
    run.stepInto();
    CHECK(spinUntil([&] { return stops > stops2; }));
    CHECK_EQ(lastNode, 2);
    // the same for the third task, then nothing is stopped
    run.stepOver();
    CHECK(spinUntil([&] { return lastNode == 3 && run.state() == RunController::State::Paused; }));
    const int stops3 = stops;
    run.stepOver();
    CHECK(spinUntil([&] { return stops > stops3; }));
    CHECK_EQ(lastNode, 2);
    // Continue runs the task to its end
    run.resume();
    CHECK(executor->waitIdle(5000));
    CHECK(spinUntil([&] { return run.state() == RunController::State::Running; }));
    // a task that is started later stops at the breakpoint
    executor->postEvent("Main");
    CHECK(spinUntil([&] { return run.state() == RunController::State::Paused; }));
    editor.detach();
    CHECK(executor->waitIdle(5000));
}

TEST(copying_nodes_never_duplicates_an_event_handler) {
    Fixture f;
    f.scene.clearSelection();
    f.scene.nodeItem(1)->setSelected(true);            // the Main handler
    f.scene.nodeItem(2)->setSelected(true);
    f.scene.copySelected();
    f.scene.paste(QPointF(0, 400));
    int handlers = 0;
    for (auto& n : f.graph().nodes) handlers += n.def == "event:Main";
    CHECK_EQ(handlers, 1);
    CHECK(f.doc.program()->ok());
}

TEST(adding_a_parameter_keeps_the_function_details_visible) {
    StudioFixture f;
    f.win.document()->edit("Add function", [](fg::Script& s) { s.addFunction("NewFunction"); });
    QCoreApplication::processEvents();
    auto* panel = f.win.findChild<MyScriptPanel*>();
    auto* details = f.win.findChild<DetailsPanel*>();
    CHECK(panel && details);
    if (!panel || !details) return;
    // select the function in the list like the user does
    QTreeWidget* functions = panel->findChildren<QTreeWidget*>().at(1);
    CHECK_EQ(functions->topLevelItemCount(), 1);
    functions->setCurrentItem(functions->topLevelItem(0));
    QCoreApplication::processEvents();
    auto addButtons = [&] {                              // the "+" buttons in the headers of Inputs and Outputs
        std::vector<QToolButton*> out;
        for (auto* b : details->findChildren<QToolButton*>()) if (b->text() == "+") out.push_back(b);
        return out;
    };
    CHECK_EQ(addButtons().size(), size_t(2));          // inputs and outputs
    for (int round = 0; round < 3; ++round) {
        auto buttons = addButtons();
        CHECK(buttons.size() == 2);
        if (buttons.size() != 2) return;
        buttons[round % 2]->click();                   // Add an input, then an output, then an input
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        CHECK_EQ(details->findChildren<QTableWidget*>().size(), 2);      // still the function editor, not an empty panel
        CHECK_EQ(addButtons().size(), size_t(2));
    }
    const fg::Graph* g = f.win.document()->graph("NewFunction");
    CHECK(g && g->inputs.size() == 2 && g->outputs.size() == 1);
    // the signature shown next to the function's name in the list follows its inputs, like it follows its name
    CHECK_EQ(functions->topLevelItem(0)->text(1), QString("(Input, Input1)"));
}

TEST(renaming_a_function_in_the_list_keeps_the_details_in_sync) {
    StudioFixture f;
    f.win.document()->edit("Add function", [](fg::Script& s) { s.addFunction("NewFunction"); });
    QCoreApplication::processEvents();
    auto* panel = f.win.findChild<MyScriptPanel*>();
    auto* details = f.win.findChild<DetailsPanel*>();
    if (!panel || !details) { CHECK(false); return; }
    QTreeWidget* functions = panel->findChildren<QTreeWidget*>().at(1);
    functions->setCurrentItem(functions->topLevelItem(0));
    QCoreApplication::processEvents();
    functions->topLevelItem(0)->setText(0, "Compute");           // the user types a name in the list
    QCoreApplication::processEvents();
    CHECK(f.win.document()->graph("Compute") != nullptr);
    QToolButton* add = nullptr;
    for (auto* b : details->findChildren<QToolButton*>()) if (b->text() == "+" && !add) add = b;
    CHECK(add != nullptr);
    if (!add) return;
    add->click();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const fg::Graph* g = f.win.document()->graph("Compute");
    CHECK(g && g->inputs.size() == 1);                           // the parameter went to the renamed function
    CHECK_EQ(details->findChildren<QTableWidget*>().size(), 2);
    // renaming a variable in the list is followed by its details as well
    f.win.document()->edit("Add variable", [](fg::Script& s) { s.variables.push_back({"V", "int", fg::Value(0), "", ""}); });
    QCoreApplication::processEvents();
    QTreeWidget* vars = panel->findChildren<QTreeWidget*>().at(2);
    vars->setCurrentItem(vars->topLevelItem(0));
    QCoreApplication::processEvents();
    vars->topLevelItem(0)->setText(0, "Score");
    QCoreApplication::processEvents();
    bool nameShown = false;
    for (auto* e : details->findChildren<QLineEdit*>()) nameShown |= e->text() == "Score";
    CHECK(nameShown);
}

TEST(launch_label_is_the_tooltip_of_the_entries_box) {
    StudioFixture f;
    auto* combo = f.win.mainToolBar()->findChild<QComboBox*>();
    CHECK(combo != nullptr);
    if (!combo) return;
    CHECK(combo->toolTip().contains("Run") && combo->toolTip().contains("Event or function to run"));
    for (auto* l : f.win.mainToolBar()->findChildren<QLabel*>()) CHECK(!l->text().contains("Run"));
}

// The tab bar of the group in which `dock` is the only tab.
static CTabBar* singleTabBarOf(const char* dock) {
    for (auto* w : QApplication::allWidgets()) {
        auto* t = dynamic_cast<CTabBar*>(w);
        auto* c = t ? static_cast<KDDockWidgets::Core::TabBar*>(t->controller()) : nullptr;
        if (c && t->count() == 1 && c->currentDockWidget() && c->currentDockWidget()->uniqueName() == dock) return t;
    }
    return nullptr;
}

// "Close Tab" of the menu of a single tab closes its dock, like its close button: unticked in View, and it opens again from there.
TEST(close_tab_closes_the_dock_of_a_single_tab) {
    StudioFixture f;
    CTabBar* tab = singleTabBarOf("My Script");
    auto* dock = KDDockWidgets::DockRegistry::self()->dockByName("My Script");
    CHECK(tab && dock);
    if (!tab || !dock) return;
    CHECK(tab->CanCloseCurrentTab());
    CHECK(dock->isOpen() && dock->toggleAction()->isChecked());
    tab->CloseCurrentTab();                                          // what the menu's "Close Tab" does
    CHECK(spinUntil([&] { return !dock->isOpen(); }));
    CHECK(!dock->toggleAction()->isChecked());
    dock->open();                                                    // View > My Script
    CHECK(spinUntil([&] { return dock->isOpen(); }));
    CHECK(f.win.findChild<MyScriptPanel*>() != nullptr);
}

// The launch box stays narrow (about 18 characters) when an entry has a long title; its list shows the whole title, and so does
// its tooltip.
TEST(launch_box_is_narrow_and_its_list_shows_long_entries) {
    StudioFixture f;
    auto* combo = f.win.mainToolBar()->findChild<QComboBox*>();
    CHECK(combo != nullptr);
    if (!combo) return;
    const int width = combo->width();
    // about 18 characters and the arrow (the test platform's characters are twice as wide as a desktop font's)
    CHECK(width > 0 && width <= 18 * combo->fontMetrics().horizontalAdvance(QLatin1Char('x')) + 64);
    const QString longName = "ComputeTheFinalScoreOfTheLevelWithAllBonusesIncluded";
    f.win.document()->edit("Add function", [&](fg::Script& s) { s.addFunction(ss(longName)); });
    CHECK(spinUntil([&] { return combo->findText(longName, Qt::MatchContains) >= 0; }));
    QCoreApplication::processEvents();
    CHECK_EQ(combo->width(), width);                                 // not as wide as the long entry
    CHECK(combo->view()->minimumWidth() >= combo->fontMetrics().horizontalAdvance(longName));
    combo->setCurrentIndex(combo->findText(longName, Qt::MatchContains));
    CHECK(combo->toolTip().contains(longName));
    CHECK(combo->toolTip().contains("Event or function to run"));    // the label it had stays its title
}

TEST(dock_layout_is_restored_at_the_next_start) {
    int detailsWidth = 0;
    {
        StandardRuntime rt;
        TestHost host(rt);
        fged::ScriptEditor editor(host);
        auto& win = *static_cast<ScriptEditorWindow*>(editor.window());
        editor.show();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        auto* details = KDDockWidgets::DockRegistry::self()->dockByName("Details");
        auto* log = KDDockWidgets::DockRegistry::self()->dockByName("Output Log");
        CHECK(details && log);
        if (!details || !log) return;
        details->resizeInLayout(150, 0, 0, 0);
        singleTabBarOf("My Script")->SetTabBarHidden(true);       // collapse the tab of the only dock of its group
        QCoreApplication::processEvents();
        log->close();
        QCoreApplication::processEvents();
        win.resize(1234, 777);
        QCoreApplication::processEvents();
        detailsWidth = details->sizeInLayout().width();
        CHECK(win.close());
    }
    {
        StandardRuntime rt;
        TestHost host(rt);
        fged::ScriptEditor editor(host);
        auto& win = *static_cast<ScriptEditorWindow*>(editor.window());
        editor.show();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        auto* details = KDDockWidgets::DockRegistry::self()->dockByName("Details");
        auto* log = KDDockWidgets::DockRegistry::self()->dockByName("Output Log");
        CHECK(details && log);
        if (!details || !log) return;
        CHECK(!log->isOpen());
        CTabBar* collapsed = singleTabBarOf("My Script");
        CHECK(collapsed != nullptr);
        if (collapsed) CHECK(collapsed->property("AnimationProgress").toDouble() == 0.0);
        auto* myDock = dynamic_cast<CDockWidget*>(KDDockWidgets::DockRegistry::self()->dockByName("My Script")->view());
        CHECK(myDock && myDock->IsTabBarHidden());
        // a group with two tabs keeps showing them
        for (auto* w : QApplication::allWidgets())
            if (auto* t = dynamic_cast<CTabBar*>(w)) if (t->isVisible() && t->count() > 1) CHECK(t->property("AnimationProgress").toDouble() == 1.0);
        CHECK(details->isOpen());
        int restored = details->sizeInLayout().width();
        CHECK(std::abs(restored - detailsWidth) < 25);
        CHECK(std::abs(win.width() - 1234) < 20 && std::abs(win.height() - 777) < 20);
        CHECK(win.close());
    }
}

TEST(side_panels_follow_their_docks) {
    StudioFixture f;
    f.win.resize(QSize(1500, 900));
    f.win.show();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    auto* details = f.win.findChild<DetailsPanel*>();
    auto* my = f.win.findChild<MyScriptPanel*>();
    CHECK(details && my);
    if (!details || !my) return;
    // nothing limits the width of the panels: they take whatever their dock gets
    CHECK(details->parentWidget() && details->parentWidget()->maximumWidth() > 5000);
    CHECK(my->parentWidget() && my->parentWidget()->maximumWidth() > 5000);
    // at the start they have a sensible width
    CHECK(details->width() > 250 && details->width() < 600);
    CHECK(my->width() > 200 && my->width() < 500);
    // widen the details dock: the panel fills it
    auto docks = f.win.findChildren<CDockWidget*>();
    CDockWidget* detailsDock = nullptr;
    for (auto* d : docks) if (d->isAncestorOf(details)) detailsDock = d;
    CHECK(detailsDock != nullptr);
    if (!detailsDock) return;
    auto* c = static_cast<KDDockWidgets::Core::DockWidget*>(detailsDock->controller());
    int before = details->width();
    c->resizeInLayout(300, 0, 0, 0);
    QCoreApplication::processEvents();
    CHECK(details->width() > before + 200);
}

TEST(backspace_in_pin_editor_does_not_delete_the_node) {
    StudioFixture f;
    CHECK(f.view != nullptr);
    if (!f.view) return;
    GraphScene* sc = f.scene();
    NodeItem* n = sc->nodeItem(2);
    sc->selectOnly(2);
    sc->startInplaceEdit(n, "String", n->scenePos() + QPointF(10, 40), 80, "Hello", f.rt.registry().tString());
    QCoreApplication::processEvents();
    size_t nodesBefore = f.win.document()->graph(sc->graphName())->nodes.size();
    for (int i = 0; i < 3; ++i) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        QApplication::sendEvent(f.view, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Backspace, Qt::NoModifier);
        QApplication::sendEvent(f.view, &release);
        QCoreApplication::processEvents();
    }
    CHECK_EQ(f.win.document()->graph(sc->graphName())->nodes.size(), nodesBefore);
    CHECK(sc->nodeItem(2) != nullptr);
}

TEST(typing_in_a_pin_editor_resizes_the_node_live) {
    StudioFixture f;
    CHECK(f.view != nullptr);
    if (!f.view) return;
    GraphScene* sc = f.scene();
    NodeItem* n = sc->nodeItem(2);
    sc->selectOnly(2);
    double widthBefore = n->boundingRect().width();
    double outBefore = n->pinScenePos("Then", fg::PinDir::Out).x();
    sc->startInplaceEdit(n, "String", n->scenePos() + QPointF(10, 40), 80, "Hi", f.rt.registry().tString());
    QCoreApplication::processEvents();
    for (int i = 0; i < 30; ++i) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QApplication::sendEvent(f.view, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier, "x");
        QApplication::sendEvent(f.view, &release);
    }
    QCoreApplication::processEvents();
    // nothing was committed yet, but the node already grew and the output pins moved with it
    CHECK(n->boundingRect().width() > widthBefore + 40);
    CHECK(n->pinScenePos("Then", fg::PinDir::Out).x() > outBefore + 40);
    CHECK(!f.win.document()->isDirty());
    // Escape restores the original layout
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(f.view, &esc);
    QCoreApplication::processEvents();
    CHECK(std::abs(n->boundingRect().width() - widthBefore) < 1.0);
    CHECK(std::abs(n->pinScenePos("Then", fg::PinDir::Out).x() - outBefore) < 1.0);
}

// The panel's sizeHint() must track AnimationProgress, not just whether it is checked: the enclosing layout reads
// sizeHint() to place the row below it, and if that jumps to its final value before the real height has animated
// there too, the row below moves into place early and visibly overlaps the still-animating panel.
TEST(collapsible_panel_size_hint_follows_the_animation) {
    CCollapsiblePanel panel("Section", nullptr, false);
    QWidget& base = panel;         // sizeHint() is a private override: call it through the public QWidget interface
    auto* content = new QWidget;
    content->setFixedHeight(100);
    panel.AddWidget(content);
    panel.show();
    QCoreApplication::processEvents();
    CHECK_EQ(base.sizeHint().height(), panel.height());
    for (double progress : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        panel.setProperty("AnimationProgress", progress);
        CHECK_EQ(base.sizeHint().height(), panel.height());
    }
}

TEST(palette_search_keeps_the_category_tree) {
    Fixture f;
    PalettePopup popup(&f.scene, PinRef());
    auto* search = popup.findChild<QLineEdit*>();
    auto* tree = popup.findChild<QTreeWidget*>();
    CHECK(search && tree);
    if (!search || !tree) return;
    search->setText("add");
    CHECK(tree->topLevelItemCount() > 0);
    bool onlyFoldersAtTop = true, foundLeafInFolder = false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree->topLevelItem(i);
        onlyFoldersAtTop &= !top->data(0, Qt::UserRole).isValid() && top->isExpanded();
        std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
            for (int c = 0; c < it->childCount(); ++c) {
                QTreeWidgetItem* ch = it->child(c);
                if (ch->data(0, Qt::UserRole).isValid() && ch->text(0).contains("Add", Qt::CaseInsensitive)) foundLeafInFolder = true;
                walk(ch);
            }
        };
        walk(top);
    }
    CHECK(onlyFoldersAtTop);             // matches stay below their category folders instead of forming a flat list
    CHECK(foundLeafInFolder);
    CHECK(tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole).isValid());       // the best match is selected
    search->setText("");
    CHECK(tree->topLevelItemCount() > 0);
}

TEST(type_spec_splits_containers) {
    TypeSpec a = TypeSpec::parse("array<float>");
    CHECK(a.container == TypeSpec::Container::Array && a.value == "float");
    TypeSpec m = TypeSpec::parse("map<string,array<int>>");
    CHECK(m.container == TypeSpec::Container::Map && m.key == "string" && m.value == "array<int>");
    CHECK_EQ(m.str(), QString("map<string,array<int>>"));
    TypeSpec s = TypeSpec::parse("Actor");
    CHECK(s.container == TypeSpec::Container::Single && s.str() == "Actor");
}

// ---- the editor of the standard applications --------------------------------------------------------------
TEST(standard_host_offers_main_per_launch_configuration) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "fg_standard_host";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    fg::Runtime rt;
    fg::standard::registerStandardApp(rt.registry());
    fg::Script script;
    {
        fg::Graph& g = script.addEventGraph();
        fg::NodeId ev = g.addNode("event:Main").id;
        fg::NodeId pr = g.addNode("util.print").id;
        g.connections.push_back({ev, "Then", pr, "In"});
        fg::stampHostEvents(script, rt.registry());
    }
    std::string path = (dir / "tool.flow").string();
    script.save(path);
    fg::standard::LaunchFile lf;
    fg::standard::LaunchConfig a;
    a.name = "A";
    a.args = {"x", "y z"};
    a.env["FG_HOST_TEST"] = "1";
    a.cwd = "sub";
    a.stepDelayMs = 40;
    fg::standard::LaunchConfig b;
    b.name = "B";
    lf.configs = {a, b};
    lf.selected = "B";
    lf.save(fg::standard::LaunchFile::pathFor(path));

    flowstudio::StandardEditorHost host(rt);
    fged::ScriptEditor editor(host);
    CHECK(editor.open(QString::fromStdString(path)));           // loads tool.flowlaunch through scriptChanged
    CHECK_EQ(host.preferredEntry(), QString("B"));
    auto program = rt.compile(script);
    auto entries = host.launchEntries(*program);
    CHECK_EQ(entries.size(), size_t(2));
    CHECK_EQ(entries[0].id, QString("A"));
    CHECK_EQ(entries[0].entry, QString("Main"));
    CHECK(entries[0].title.startsWith("A"));                       // the selector shows the configuration, not the event

    fged::LaunchSpec spec;
    CHECK(host.prepareLaunch(entries[0], spec, nullptr));
    CHECK_EQ(spec.entry, QString("Main"));
    CHECK_EQ(spec.args.size(), size_t(1));
    CHECK(spec.args[0].isArray() && spec.args[0].arrayPtr()->items.size() == 2);
    CHECK_EQ(spec.args[0].arrayPtr()->items[1].toString(), std::string("y z"));
    CHECK_EQ(spec.stepDelayMs, 40);
    CHECK_EQ(host.launchFile().selected, std::string("A"));        // the choice is remembered

    QString before = QDir::currentPath();
    qunsetenv("FG_HOST_TEST");
    host.aboutToRun(spec);
    CHECK_EQ(qgetenv("FG_HOST_TEST"), QByteArray("1"));
    CHECK(QDir::currentPath().endsWith("sub"));
    host.runFinished();
    CHECK(!qEnvironmentVariableIsSet("FG_HOST_TEST"));
    CHECK_EQ(QDir::currentPath(), before);
    // the remembered selection was written next to the script
    CHECK_EQ(fg::standard::LaunchFile::load(fg::standard::LaunchFile::pathFor(path)).selected, std::string("A"));
    fs::remove_all(dir);
}

TEST(editor_reports_scripts_that_need_missing_events) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "fg_incompatible";
    fs::create_directories(dir);
    fg::Runtime other;                                            // an application with its own event
    other.registry().addEvent("OnHit", {{"Damage", "int"}});
    fg::Script s;
    fg::Graph& g = s.addEventGraph();
    g.addNode("event:OnHit");
    fg::stampHostEvents(s, other.registry());
    std::string path = (dir / "game.flow").string();
    s.save(path);

    fg::Runtime rt;                                               // standard applications know Main only
    fg::standard::registerStandardApp(rt.registry());
    // a runtime-only application refuses it ...
    auto loaded = rt.loadScript(path);
    CHECK(!loaded.ok());
    CHECK(loaded.explanation().find("OnHit") != std::string::npos);
    // ... the editor opens it anyway so that it can be repaired, and says why it cannot run
    TestHost host(rt);
    fged::ScriptEditor editor(host);
    CHECK(editor.open(QString::fromStdString(path)));
    CHECK(!static_cast<ScriptEditorWindow*>(editor.window())->document()->program()->ok());
    fs::remove_all(dir);
}

// Snap to grid: nodes the user moves (and nodes that are placed) land on the grid; Alt moves freely.
static bool onGrid(double v, double step) { return std::abs(v / step - std::round(v / step)) < 1e-6; }

TEST(snap_to_grid_places_and_moves_nodes_on_the_grid) {
    ViewFixture f;
    NodeItem* n = f.scene.nodeItem(2);
    QPoint header = f.toView(n->mapToScene(QPointF(20, 8)));
    auto stored = [&] { return f.doc.graph("EventGraph")->findNode(2); };

    f.scene.setSnapToGrid(true);
    f.scene.setSnapStep(20);
    f.drag(header, header + QPoint(93, 47));                 // not a multiple of the step
    QCoreApplication::processEvents();
    n = f.scene.nodeItem(2);
    CHECK(onGrid(n->scenePos().x(), 20) && onGrid(n->scenePos().y(), 20));
    CHECK(std::abs(n->scenePos().x() - 300.0) > 60);                          // it did move
    CHECK(std::abs(stored()->x - n->scenePos().x()) < 0.5 && std::abs(stored()->y - n->scenePos().y()) < 0.5);   // and the grid position is what is saved

    // a larger step
    f.scene.setSnapStep(40);
    header = f.toView(n->mapToScene(QPointF(20, 8)));
    f.drag(header, header + QPoint(37, 33));
    QCoreApplication::processEvents();
    n = f.scene.nodeItem(2);
    CHECK(onGrid(n->scenePos().x(), 40) && onGrid(n->scenePos().y(), 40));

    // without snapping the node follows the mouse exactly
    f.scene.setSnapToGrid(false);
    QPointF free0 = n->scenePos();
    header = f.toView(n->mapToScene(QPointF(20, 8)));
    f.drag(header, header + QPoint(13, 7));
    QCoreApplication::processEvents();
    n = f.scene.nodeItem(2);
    CHECK(!onGrid(n->scenePos().x(), 20) || !onGrid(n->scenePos().y(), 20));
    CHECK(std::abs((n->scenePos().x() - free0.x()) - 13) < 1.5);

    // Alt switches snapping off while dragging
    f.scene.setSnapToGrid(true);
    f.scene.setSnapStep(20);
    header = f.toView(n->mapToScene(QPointF(20, 8)));
    auto sendAlt = [&](QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, QPointF(pos), QPointF(f.view.viewport()->mapToGlobal(pos)), button, buttons, Qt::AltModifier);
        QApplication::sendEvent(f.view.viewport(), &e);
        QCoreApplication::processEvents();
    };
    QPointF beforeAlt = n->scenePos();
    sendAlt(QEvent::MouseButtonPress, header, Qt::LeftButton, Qt::LeftButton);
    sendAlt(QEvent::MouseMove, header + QPoint(8, 5), Qt::NoButton, Qt::LeftButton);
    sendAlt(QEvent::MouseMove, header + QPoint(17, 11), Qt::NoButton, Qt::LeftButton);
    sendAlt(QEvent::MouseButtonRelease, header + QPoint(17, 11), Qt::LeftButton, Qt::NoButton);
    n = f.scene.nodeItem(2);
    CHECK(std::abs((n->scenePos().x() - beforeAlt.x()) - 17) < 1.5);
    CHECK(!onGrid(n->scenePos().x(), 20));

    // new nodes are placed on the grid too
    auto all = fg::buildPalette(fg::NodeBuildCtx{f.rt.registry(), &f.doc.script(), f.doc.script().findGraph("EventGraph")});
    fg::NodeId placed = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(263, 157));
    CHECK(std::abs(f.doc.graph("EventGraph")->findNode(placed)->x - 260) < 0.01 && std::abs(f.doc.graph("EventGraph")->findNode(placed)->y - 160) < 0.01);
    f.scene.setSnapToGrid(false);
    fg::NodeId free = f.scene.addNodeFromTemplate(*findTemplate(all, "math.add"), QPointF(263, 157));
    CHECK(std::abs(f.doc.graph("EventGraph")->findNode(free)->x - 263) < 0.01);
}

TEST(view_menu_snap_to_grid_switches_every_graph) {
    StudioFixture f;
    CHECK(f.view != nullptr);
    if (!f.view) return;
    QAction* snap = nullptr;
    QAction* forty = nullptr;
    QAction* twenty = nullptr;
    for (auto* a : f.win.findChildren<QAction*>()) {
        if (a->text() == "Snap to &Grid") snap = a;
        if (a->text() == "40 units") forty = a;
        if (a->text() == "20 units") twenty = a;
    }
    CHECK(snap && forty && twenty);
    if (!snap || !forty || !twenty) return;
    CHECK(snap->isCheckable());
    const bool initial = snap->isChecked();                              // the user's setting: put it back at the end
    const bool wasForty = forty->isChecked();
    CHECK_EQ(f.scene()->snapToGrid(), initial);
    snap->setChecked(true);
    CHECK(f.scene()->snapToGrid());
    forty->trigger();
    CHECK_EQ(f.scene()->snapStep(), 40.0);
    // a graph that is opened later follows the setting as well
    f.win.document()->edit("Function", [&](fg::Script& s) { s.addFunction("Other"); });
    emit f.win.findChild<MyScriptPanel*>()->openGraph("Other");           // what double-clicking the function does
    GraphView* other = nullptr;
    for (auto* v : f.win.findChildren<GraphView*>())
        if (v->graphScene()->graphName() == "Other") other = v;
    CHECK(other != nullptr);
    if (other) {
        CHECK(other->graphScene()->snapToGrid());
        CHECK_EQ(other->graphScene()->snapStep(), 40.0);
    }
    snap->setChecked(false);
    CHECK(!f.scene()->snapToGrid());
    if (other) CHECK(!other->graphScene()->snapToGrid());
    (wasForty ? forty : twenty)->trigger();
    snap->setChecked(initial);
}

// Opening another script starts the debugger panel afresh: the variables of the previous run and the watches belong to the old script.
TEST(debugger_panel_forgets_the_previous_script_when_another_is_opened) {
    Fixture f;
    const QString fileA = QDir::temp().filePath("fg_debugger_reset_a.flow");
    QString error;
    CHECK(f.doc.save(fileA, &error));                          // a file to open later (no watches stored for it)
    f.doc.newScript([&](fg::Script& s) { fg::standard::createMainTemplate(s, f.rt.registry()); });
    f.doc.edit("Variable", [&](fg::Script& s) { s.variables.push_back({"Total", "int", fg::Value(7), "", ""}); });

    RunController run(&f.doc);
    DebugPanel panel(&f.doc, &run);
    QTreeWidget* vars = nullptr;
    QTreeWidget* watches = nullptr;
    for (auto* t : panel.findChildren<QTreeWidget*>()) {
        if (t->headerItem()->text(0) == "Variable") vars = t;
        if (t->headerItem()->text(0) == "Watch") watches = t;
    }
    CHECK(vars && watches);
    if (!vars || !watches) return;
    auto hasVariable = [&](const QString& name) {
        for (int i = 0; i < vars->topLevelItemCount(); ++i)
            for (int c = 0; c < vars->topLevelItem(i)->childCount(); ++c)
                if (vars->topLevelItem(i)->child(c)->text(0) == name) return true;
        return false;
    };
    bool finished = false;
    QObject::connect(&run, &RunController::finished, [&](bool, const QString&) { finished = true; });
    panel.addWatch("Total");                                   // an unsaved script: the watch lives in the window only
    CHECK(run.start("Main", {}));
    CHECK(spinUntil([&] { return finished; }));
    QCoreApplication::processEvents();
    CHECK(hasVariable("Total"));
    CHECK_EQ(watches->topLevelItemCount(), 1);
    CHECK_EQ(watches->topLevelItem(0)->text(1), QString("7"));

    // opening a file: neither the variables of the run nor the watch are carried over
    CHECK(f.doc.load(fileA, &error));
    QCoreApplication::processEvents();
    CHECK(!hasVariable("Total"));
    CHECK_EQ(vars->topLevelItemCount(), 0);                    // nothing ran in this script
    CHECK_EQ(watches->topLevelItemCount(), 0);
    CHECK(panel.watchExpressions().isEmpty());
    CHECK(run.state() == RunController::State::Idle);

    // the same for a new script, and a run that is still active is stopped
    panel.addWatch("Other");                                   // a script with a path: stored for that file ...
    CHECK_EQ(watches->topLevelItemCount(), 1);
    f.doc.newScript([&](fg::Script& s) { fg::standard::createMainTemplate(s, f.rt.registry()); });
    QCoreApplication::processEvents();
    CHECK_EQ(watches->topLevelItemCount(), 0);                 // ... not for the new one
    CHECK_EQ(vars->topLevelItemCount(), 0);
    f.doc.load(fileA);                                         // and it is back with its own file
    QCoreApplication::processEvents();
    CHECK_EQ(watches->topLevelItemCount(), 1);
    CHECK_EQ(watches->topLevelItem(0)->text(0), QString("Other"));
    panel.removeWatch("Other");                                // leave nothing in the user's settings
    QFile::remove(fileA);
}

// Run is the same action as Continue and has its icon; Stop has its own (a red square) - the attach / detach icons are for attaching a debugger.
TEST(run_and_stop_have_their_own_icons) {
    StudioFixture f;
    QAction* run = nullptr;
    QAction* stop = nullptr;
    for (auto* a : f.win.findChildren<QAction*>()) {
        if (a->text() == "Run") run = a;
        if (a->text() == "Stop") stop = a;
    }
    CHECK(run && stop);
    if (!run || !stop) return;
    auto image = [](const QIcon& icon) { return icon.pixmap(32, 32).toImage(); };
    CHECK(!image(run->icon()).isNull() && !image(stop->icon()).isNull());
    CHECK(image(run->icon()) == image(QIcon(":/Icons/continue.svg")));
    CHECK(image(stop->icon()) == image(QIcon(":/Icons/stop.svg")));
    CHECK(image(stop->icon()) != image(QIcon(":/Icons/detach.svg")));
    CHECK(image(run->icon()) != image(QIcon(":/Icons/attach.svg")));
    // a red square: the middle of the icon is red, its corners are not
    QImage stopImage = image(stop->icon());
    QColor middle = stopImage.pixelColor(16, 16);
    CHECK(middle.red() > 200 && middle.green() < 130 && middle.blue() < 150);
    CHECK(stopImage.pixelColor(2, 2).alpha() < 20);
}

// One button: Run when nothing runs, Pause while the script runs, Continue while it is paused (there is no separate Pause action).
TEST(one_button_runs_pauses_and_continues) {
    StudioFixture f;
    QAction* run = nullptr;
    QAction* stop = nullptr;
    int pauseActions = 0;
    for (auto* a : f.win.findChildren<QAction*>()) {
        if (a->text() == "Run") run = a;
        if (a->text() == "Stop") stop = a;
        if (a->text() == "Pause") ++pauseActions;
    }
    CHECK(run && stop);
    if (!run || !stop) return;
    CHECK_EQ(pauseActions, 0);
    auto image = [](const QIcon& icon) { return icon.pixmap(32, 32).toImage(); };
    Document* doc = f.win.document();

    // a script that waits: Main -> Delay(30 s) -> Print
    doc->edit("Delay", [&](fg::Script& s) {
        auto& g = s.graphs[0];
        fg::NodeId d = g.addNode("flow.delay").id;
        g.findNode(d)->defaults["Duration"] = fg::Value(30.0);
        g.connections.clear();
        g.connections.push_back({1, "Then", d, "In"});
        g.connections.push_back({d, "Then", 2, "In"});
    });
    CHECK(run->isEnabled() && !stop->isEnabled());
    CHECK(image(run->icon()) == image(QIcon(":/Icons/continue.svg")));
    run->trigger();                                                    // Run
    CHECK(spinUntil([&] { return run->text() == "Pause"; }));
    CHECK(run->isEnabled() && stop->isEnabled());
    CHECK(image(run->icon()) == image(QIcon(":/Icons/interrupt.svg")));
    run->trigger();                                                    // Pause (the script waits; it keeps being "running")
    QCoreApplication::processEvents();
    CHECK(run->isEnabled());
    stop->trigger();
    CHECK(spinUntil([&] { return run->text() == "Run"; }));
    CHECK(image(run->icon()) == image(QIcon(":/Icons/continue.svg")));
    CHECK(!stop->isEnabled());

    // a breakpoint: the same button continues
    doc->edit("No delay", [&](fg::Script& s) {
        auto& g = s.graphs[0];
        g.connections.clear();
        g.connections.push_back({1, "Then", 2, "In"});
    });
    doc->debugger().setBreakpoint("EventGraph", 2, true);
    run->trigger();
    CHECK(spinUntil([&] { return run->text() == "Continue"; }));
    CHECK(run->isEnabled());
    CHECK(image(run->icon()) == image(QIcon(":/Icons/continue.svg")));
    doc->debugger().setBreakpoint("EventGraph", 2, false);
    run->trigger();                                                    // Continue
    CHECK(spinUntil([&] { return run->text() == "Run"; }));
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // the tests must neither read nor change the settings of the real application
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::temp().filePath("FlowGraphEditorTests-settings"));
    QDir(QDir::temp().filePath("FlowGraphEditorTests-settings")).removeRecursively();
    fged::applyDarkTheme(app);
    fged::initEditor();
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
