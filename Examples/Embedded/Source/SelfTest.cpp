#include "SelfTest.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTableWidget>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <functional>

namespace msgapp {

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool waitFor(const std::function<bool()>& done, int timeoutMs = 10000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}
void pause(int ms) { waitFor([] { return false; }, ms); }

// The table has handled `n` messages (the run started and caught up) and shows what they produced.
bool caughtUp(TableDock* t, int n) { return t->processed() == n && !t->busy(); }

QStringList headers(QTableWidget* t) {
    QStringList out;
    for (int i = 0; i < t->columnCount(); ++i) out << (t->horizontalHeaderItem(i) ? t->horizontalHeaderItem(i)->text() : QString());
    return out;
}
int column(QTableWidget* t, const QString& name) { return headers(t).indexOf(name); }
int number(QTableWidget* t, int row, const QString& col) {
    auto* item = t->item(row, column(t, col));
    return item ? item->data(Qt::DisplayRole).toInt() : -1;
}

// How many script editor windows exist in the whole application.
int editorWindows() {
    int n = 0;
    for (QWidget* w : QApplication::allWidgets())
        if (QByteArray(w->metaObject()->className()) == "fged::ScriptEditorWindow") ++n;
    return n;
}

const char* kSample[] = {
    "2026-10-17 09:58:02 INFO  server started on port 8080",
    "2026-10-17 09:58:41 INFO  user alice logged in",
    "2026-10-17 09:59:15 WARN  disk usage at 81 percent",
    "2026-10-17 10:01:07 INFO  user bob logged in",
    "2026-10-17 10:02:33 ERROR failed to open /var/data/report.csv",
    "2026-10-17 10:02:34 INFO  retrying report.csv (attempt 2)",
    "2026-10-17 10:03:12 INFO  report.csv exported: 1204 rows",
    "2026-10-17 10:05:49 WARN  slow request: GET /api/orders took 2300 ms",
    "2026-10-17 10:07:20 ERROR database connection lost",
    "2026-10-17 10:07:25 INFO  database connection restored",
    "2026-10-17 10:11:03 INFO  user alice logged out",
    "2026-10-17 10:15:40 INFO  backup finished in 94 seconds",
    "2026-10-17 10:16:02 WARN  certificate expires in 12 days",
    "2026-10-17 10:20:18 INFO  user carol logged in",
    "2026-10-17 10:24:51 ERROR out of memory while resizing image 4096x4096",
    "2026-10-17 10:25:00 INFO  user bob logged out",
};

// A script the way a user would draw it: NewMessage -> Add Row with the columns Line, Length and IsError.
QString writeColumnsScript(const QString& file, bool withHandler) {
    using namespace fg;
    Script s = makeListScript();
    s.name = "Self test";
    if (!withHandler) {
        s.graphs.clear();
        s.addEventGraph();
    } else {
        Graph& g = s.graphs[0];
        Node* row = nullptr;
        for (auto& n : g.nodes) if (n.def == kAddRowNode) row = &n;
        const NodeId rowId = row->id;
        row->inputs = {{"Line", "int", false, false, Value()}, {"Length", "int", false, false, Value()}, {"IsError", "bool", false, false, Value()}};
        g.connections.erase(std::remove_if(g.connections.begin(), g.connections.end(), [&](const Connection& c) { return c.toNode == rowId && c.toPin == "Text"; }), g.connections.end());
        NodeId text = 0;
        for (auto& n : g.nodes)
            if (n.def == "get:Message.Text") text = n.id;
        const NodeId length = g.addNode("fn:String.Length", 420, 240).id;
        const NodeId contains = g.addNode("fn:String.Contains", 420, 380).id;
        g.findNode(contains)->defaults["Substring"] = Value("ERROR");
        g.connections.push_back({text, "Text", length, "String"});
        g.connections.push_back({text, "Text", contains, "String"});
        g.connections.push_back({length, "Result", rowId, "Length"});
        g.connections.push_back({contains, "Result", rowId, "IsError"});
    }
    s.save(file.toStdString());
    return file;
}

// A script with a state: the global variable Count goes up by one for every message, and the row shows it (columns Line and Count).
QString writeCounterScript(const QString& file) {
    makeCounterScript().save(file.toStdString());
    return file;
}

} // namespace

int runSelfTest(fg::Runtime& rt, MessageStore& store) {
    const QString dir = QDir::temp().filePath("embedded-example-selftest");
    QDir().mkpath(dir);

    std::printf("the messages\n");
    {
        RandomMessages a(7), b(7);
        bool same = true, levels[3] = {false, false, false};
        for (int i = 0; i < 300; ++i) {
            const QString x = a.next(), y = b.next();
            same &= x.mid(20) == y.mid(20);                       // the time stamp is the time of the day
            levels[0] |= x.contains(" INFO "); levels[1] |= x.contains(" WARN "); levels[2] |= x.contains(" ERROR");
        }
        check(same, "the same seed makes the same messages");
        check(levels[0] && levels[1] && levels[2], "they are INFO, WARN and ERROR lines");
        MessageStore other;
        std::vector<int> lines;
        QObject::connect(&other, &MessageStore::messageAdded, [&](MessagePtr m) { lines.push_back(m->line); });
        other.startGenerating(10);
        check(waitFor([&] { return lines.size() >= 4; }), "the store makes a message at the interval it is given");
        other.stopGenerating();
        check(lines.size() >= 4 && lines[0] == 1 && lines[1] == 2 && !other.generating(), "numbered 1, 2, 3...; and it stops");
    }
    for (const char* line : kSample) store.addMessage(line);
    check(store.total() == 16 && store.messages().front()->line == 1 && store.messages().back()->line == 16, "the sample messages are in the store");
    const int messages = 16;

    MainWindow window(rt, store);
    ScriptEditorHub& hub = window.editorHub();
    std::printf("a new table runs the default script: it drops every message\n");
    TableDock* t1 = window.newTable();
    check(waitFor([&] { return caughtUp(t1, messages); }), "the script ran for every message");
    check(t1->table()->columnCount() == 0 && t1->table()->rowCount() == 0, "no Add Row ran: the table has no columns and no rows");
    check(!hub.created() && editorWindows() == 0, "no script editor exists until one is asked for");

    std::printf("a script that lists the messages\n");
    const QString list = dir + "/list.flow";
    makeListScript().save(list.toStdString());
    check(t1->openScript(list), "the script is loaded");
    check(waitFor([&] { return caughtUp(t1, messages); }), "it runs over the messages that were received");
    check(headers(t1->table()) == QStringList({"Line", "Text"}), "the columns are the custom inputs of the Add Row node: Line, Text");
    check(t1->table()->rowCount() == messages, "one row per message");
    check(t1->table()->item(0, 1) && t1->table()->item(0, 1)->text().startsWith("2026-10-17 09:58:02 INFO"), "the Text column holds the message");
    check(t1->table()->item(0, 0) && t1->table()->item(0, 0)->data(Qt::DisplayRole).typeId() == QMetaType::LongLong, "numbers stay numbers");
    store.addMessage("2026-10-17 10:30:00 INFO  user dave logged in");
    check(waitFor([&] { return caughtUp(t1, messages + 1); }), "a new message: the script ran for it");
    check(t1->table()->rowCount() == messages + 1 && number(t1->table(), messages, "Line") == messages + 1, "and the table has one more row");

    std::printf("a script with more columns\n");
    const QString columns = writeColumnsScript(dir + "/columns.flow", true);
    check(t1->openScript(columns), "the script is loaded");
    check(waitFor([&] { return caughtUp(t1, messages + 1) && headers(t1->table()).size() == 3; }), "it runs over the messages that were received");
    check(headers(t1->table()) == QStringList({"Line", "Length", "IsError"}), "the columns follow the custom inputs of the node: Line, Length, IsError");
    int errors = 0, lengthOk = 0;
    for (int r = 0; r < t1->table()->rowCount(); ++r) {
        errors += t1->table()->item(r, 2)->text() == "true";
        lengthOk += number(t1->table(), r, "Length") == int(store.messages()[size_t(r)]->text.size());
    }
    check(errors == 3, "the IsError column is true for the three ERROR lines");
    check(lengthOk == messages + 1, "the Length column is what the script computed (String.Length of the text)");

    std::printf("the script keeps its state from message to message\n");
    const QString counter = writeCounterScript(dir + "/counter.flow");
    check(t1->openScript(counter), "a script with a variable that counts the messages");
    check(waitFor([&] { return caughtUp(t1, messages + 1); }), "it runs over the messages that were received");
    bool sequential = t1->table()->rowCount() == messages + 1;
    for (int r = 0; sequential && r < t1->table()->rowCount(); ++r) sequential = number(t1->table(), r, "Count") == r + 1;
    check(sequential, "Count is 1, 2, 3... down the table: the variable is not reset for every message");
    for (int i = 0; i < 3; ++i) store.addMessage("2026-10-17 10:31:00 INFO  one more");
    check(waitFor([&] { return caughtUp(t1, messages + 4); }), "three messages arrive while the script runs");
    check(number(t1->table(), messages + 3, "Count") == messages + 4 && number(t1->table(), messages + 3, "Line") == messages + 4, "and the count goes on where it was");
    const int total = messages + 4;

    std::printf("there is one script editor, however often Edit Script is pressed\n");
    t1->editScript();
    check(hub.created() && hub.current() == t1, "Edit Script creates the editor and it edits the script of the table");
    check(hub.editor()->window()->windowTitle().contains("Table 1"), "its window says which table it edits");
    fged::ScriptEditor* theEditor = hub.editor();
    t1->editScript();
    t1->editScript();
    check(editorWindows() == 1 && hub.editor() == theEditor && fged::ScriptEditor::instance() == theEditor, "pressing it again does not make another editor");

    std::printf("saving in the editor starts the script again\n");
    auto before = t1->executor();
    check(hub.save(t1), "Save in the editor");
    check(t1->executor() != before && waitFor([&] { return caughtUp(t1, total); }), "the table runs its script again, over everything received");
    check(number(t1->table(), 0, "Count") == 1 && number(t1->table(), total - 1, "Count") == total, "with fresh variables: Count starts at 1 again");

    std::printf("a second table uses the same editor\n");
    TableDock* t2 = window.newTable();
    check(waitFor([&] { return caughtUp(t2, total); }), "the second table runs the default script");
    t2->editScript();
    check(editorWindows() == 1 && hub.editor() == theEditor, "still one editor");
    check(hub.current() == t2 && hub.editor()->window()->windowTitle().contains("Table 2"), "it now edits the script of the second table");
    check(t2->table()->columnCount() == 0, "which is still the default script (it drops the messages)");
    // the user changes the script in the editor (here: by opening a file in it) and saves: the SCRIPT OF THE TABLE changes
    check(hub.editor()->open(columns), "the script in the editor is another one");
    auto firstTable = t1->executor();
    check(hub.save(t2), "Save in the editor");
    check(waitFor([&] { return caughtUp(t2, total); }) && headers(t2->table()) == QStringList({"Line", "Length", "IsError"}), "the second table runs the script it got");
    check(t1->executor() == firstTable && number(t1->table(), total - 1, "Count") == total, "the first table is not touched");
    store.addMessage("2026-10-17 10:32:00 ERROR something broke");
    check(waitFor([&] { return caughtUp(t1, total + 1) && caughtUp(t2, total + 1); }), "a new message reaches both tables");
    check(number(t1->table(), total, "Count") == total + 1 && t2->table()->item(total, 2)->text() == "true", "each with its own script");

    std::printf("the debugger attaches to what the application runs\n");
    check(t2->executor() != nullptr, "the table keeps the executor of its run");
    check(!hub.editor()->isAttached() && hub.editor()->attach(t2->executor()), "the editor attaches to it");
    before = t2->executor();
    check(hub.save(t2), "the table starts its script again while the editor is attached");
    check(t2->executor() != before && waitFor([&] { return caughtUp(t2, total + 1); }) && hub.editor()->isAttached(), "the editor is attached to the new executor too");
    t1->editScript();
    check(hub.current() == t1 && !hub.editor()->isAttached(), "editing another table ends the session");
    check(t1->saveScript(), "Save Script of a table writes its script to its file");
    check(editorWindows() == 1, "there is still one editor");

    std::printf("a script that does not fit is not loaded\n");
    const QStringList columnsBefore = headers(t1->table());
    const int rowsBefore = t1->table()->rowCount();
    const QString pathBefore = t1->scriptPath();
    QString reason;
    check(!t1->openScript(writeColumnsScript(dir + "/nohandler.flow", false), &reason), "a script without a NewMessage handler is refused");
    check(reason.contains("NewMessage"), "and the reason says so");
    // a script made for a standalone application: it handles Main, which this application never raises
    {
        fg::Script standalone;
        standalone.name = "Standalone";
        fg::Graph& g = standalone.addEventGraph();
        const fg::NodeId ev = g.addNode("event:Main").id;
        const fg::NodeId print = g.addNode("util.print").id;
        g.connections.push_back({ev, "Then", print, "In"});
        standalone.save((dir + "/standalone.flow").toStdString());
    }
    reason.clear();
    check(!t1->openScript(dir + "/standalone.flow", &reason), "a script made for a standalone application is refused");
    check(reason.contains("Main"), "and the reason names the event that is missing");
    check(headers(t1->table()) == columnsBefore && t1->table()->rowCount() == rowsBefore && t1->scriptPath() == pathBefore,
          "the table, its script and its rows are as they were");
    t1->editScript();
    check(hub.current() == t1 && hub.editor()->window()->windowTitle().contains("Table 1"), "Edit Script shows the script the table has");
    check(!hub.editor()->open(dir + "/standalone.flow") && !hub.editor()->open(dir + "/nohandler.flow"), "the editor refuses these files too (File > Open)");
    before = t1->executor();
    check(hub.save(t1), "the editor still has the script of the table");
    check(t1->executor() != before && waitFor([&] { return caughtUp(t1, total + 1); }) && headers(t1->table()) == columnsBefore, "and saving it changes nothing");

    std::printf("the messages start again\n");
    store.clear();
    for (const char* line : {kSample[0], kSample[1]}) store.addMessage(line);
    check(waitFor([&] { return caughtUp(t1, 2) && caughtUp(t2, 2); }), "after the store was cleared the scripts start again");
    check(t1->table()->rowCount() == 2 && number(t1->table(), 1, "Count") == 2, "the tables show the new messages only");
    check(window.tables().size() == 2, "the window has two tables");

    std::printf("the messages are handled one at a time\n");
    // a script that waits 0.15 s for every message (a Delay node) before it adds its row
    const QString slow = dir + "/slow.flow";
    {
        fg::Script sc;
        sc.name = "Slow";
        fg::Graph& g = sc.addEventGraph();
        const fg::NodeId ev = g.addNode(std::string("event:") + kNewMessageEvent).id;
        const fg::NodeId delay = g.addNode("flow.delay").id;
        g.findNode(delay)->defaults["Duration"] = fg::Value(0.15);
        const fg::NodeId row = g.addNode(kAddRowNode).id;
        g.findNode(row)->inputs = {{"Line", "int", false, false, fg::Value()}};
        const fg::NodeId line = g.addNode("get:Message.Line").id;
        g.connections.push_back({ev, "Then", delay, "In"});
        g.connections.push_back({delay, "Then", row, "In"});
        g.connections.push_back({ev, "Message", line, "Target"});
        g.connections.push_back({line, "Line", row, "Line"});
        sc.save(slow.toStdString());
    }
    store.clear();
    check(t1->openScript(slow), "a script that waits 0.15 s for every message");
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < 3; ++i) store.addMessage("2026-10-17 11:00:00 INFO  message");
    check(waitFor([&] { return caughtUp(t1, 3); }), "three messages arrive at once and are all handled");
    check(clock.elapsed() >= 400, "one after the other: three handlers of 0.15 s took more than 0.4 s, not 0.15 s (no two run together)");
    check(t1->table()->rowCount() == 3 && number(t1->table(), 0, "Line") == 1 && number(t1->table(), 1, "Line") == 2 && number(t1->table(), 2, "Line") == 3,
          "and the rows are in the order of the messages");
    clock.restart();
    for (int i = 0; i < 2000; ++i) store.addMessage("2026-10-17 11:00:01 INFO  burst");
    check(waitFor([&] { return caughtUp(t2, 2003); }, 30000), "2000 messages that arrive at once are all handled, one at a time");
    std::printf("      (%d ms)\n", int(clock.elapsed()));

    std::printf("%s\n", g_failures ? "SOME CHECKS FAILED" : "all checks passed");
    return g_failures;
}

} // namespace msgapp
