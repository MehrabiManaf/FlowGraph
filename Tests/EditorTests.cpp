// Tests of the editor logic that do not need a visible window (run with QT_QPA_PLATFORM=offscreen).
#include "Document.hpp"
#include "Graph/GraphScene.hpp"
#include "Graph/Style.hpp"
#include "Graph/LinkItem.hpp"
#include "Graph/PalettePopup.hpp"
#include "Panels/DetailsPanel.hpp"
#include "Panels/InfoPanels.hpp"
#include "Panels/MyScriptPanel.hpp"
#include "Panels/TypeSpec.hpp"
#include "Studio/RunController.hpp"
#include "Studio/ScriptEditorWindow.hpp"
#include "StandardEditorHost.hpp"
#include <FlowGraphEditor/ScriptEditor.hpp>

#include "Graph/GraphView.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <kddockwidgets/core/DockWidget.h>
#include "Common/CustomViewFactory/DockWidget.hpp"
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QMouseEvent>
#include <QScrollBar>
#include <QClipboard>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QComboBox>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTimer>
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
    CHECK_EQ(plus, 3);
    // a second event graph (made by a file or a host) is not listed: the section holds events only, and there is no
    // way to create event graphs from it
    f.doc.edit("Second event graph", [](fg::Script& sc) { sc.addEventGraph("Other"); });
    QCoreApplication::processEvents();
    CHECK(find("Other") == nullptr);
    CHECK(find("EventGraph") == nullptr);
    for (QTreeWidgetItemIterator it(events); *it; ++it) CHECK((*it)->parent() == nullptr);
    CHECK_EQ(events->topLevelItemCount(), 2);          // Main and Tick
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

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
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
