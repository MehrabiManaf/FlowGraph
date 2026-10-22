// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "MessageScripting.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <map>

namespace msgapp {

using namespace fg;

namespace {

Cell toCell(const Value& v) {
    if (v.isNull()) return std::monostate{};
    if (v.isBool()) return v.toBool();
    if (v.isInt()) return v.toInt();
    if (v.isFloat()) return v.toFloat();
    if (v.isString()) return v.str();
    return v.toString();
}

// What the Add Row nodes of a running script write to: Instance::userData() of the instance of a pass.
struct RowSink {
    std::vector<Row> rows;
};

} // namespace

void registerMessageTypes(Registry& reg) {
    reg.beginSource("messages");                                     // everything below is tagged with where it came from

    // The class of the event argument. Scripts only read messages, so there is no constructor and the properties are read only.
    reg.registerClass<Message>("Message", "Messages")
        .doc("One line of the messages file.")
        .toString([](const Message& m) { return m.text; })
        .readonly("Text", [](const Message& m) { return m.text; })
        .readonly("Line", [](const Message& m) { return static_cast<int64_t>(m.line); });

    // The one event of the application: raised once for every message. A script handles it with the event node "NewMessage".
    reg.addEvent(kNewMessageEvent, {{"Message", "Message"}}).doc = "A message of the messages file. The script is run once for every message.";

    // The node that fills the table. It has no data pins of its own: the user adds them (NodeDef::customInputs), like the inputs of a
    // function, in the Inputs list of the Details panel of the node. Every custom input is a column: its name is the name of the column.
    NodeDef d;
    d.id = kAddRowNode;
    d.title = "Add Row";
    d.category = "Table";
    d.style = NodeStyle::Function;
    d.keywords = "table row column cell";
    d.doc = "Adds a row to the table. Every input pin is a column of the table: the name of the pin is the name of the column.";
    d.pins = {execIn(), execOut()};                                  // the fixed pins: the custom ones come after them
    d.customInputs = true;
    d.run = [](NodeCtx& c) {
        const CompiledNode& node = c.node();
        Row row;
        for (size_t i = 0; i < node.dataIn.size(); ++i)
            row.cells.emplace_back(node.pins[static_cast<size_t>(node.dataIn[i])].name, toCell(c.in(i)));
        if (auto* sink = static_cast<RowSink*>(c.instance().userData())) {
            sink->rows.push_back(std::move(row));
        } else {
            // not run by a pass (the editor's own Run button): show the row in the log instead
            std::string text = "row:";
            for (auto& [name, cell] : row.cells) {
                text += " " + name + "=";
                if (auto* s = std::get_if<std::string>(&cell)) text += *s;
                else if (auto* i = std::get_if<int64_t>(&cell)) text += std::to_string(*i);
                else if (auto* f = std::get_if<double>(&cell)) text += std::to_string(*f);
                else if (auto* b = std::get_if<bool>(&cell)) text += *b ? "true" : "false";
            }
            c.log(LogLevel::Info, text);
        }
    };
    reg.addNode(std::move(d));
    reg.endSource();
}

Script makeDefaultScript() {
    Script s;
    s.name = "Table";
    s.doc = "Handles NewMessage and drops every message. Connect nodes to the event; an Add Row node puts a row into the table.";
    Graph& g = s.addEventGraph("EventGraph");
    g.addNode(std::string("event:") + kNewMessageEvent, 0, 0);
    return s;
}

Script makeListScript() {
    Script s;
    s.name = "List";
    s.doc = "Handles NewMessage and adds one row per message: its line number and its text.";
    Graph& g = s.addEventGraph("EventGraph");
    const NodeId ev = g.addNode(std::string("event:") + kNewMessageEvent, 0, 0).id;
    const NodeId row = g.addNode(kAddRowNode, 420, 0).id;
    g.findNode(row)->inputs = {{"Line", "int", false, false, Value()}, {"Text", "string", false, false, Value()}};     // the columns
    const NodeId line = g.addNode("get:Message.Line", 180, 140).id;
    const NodeId text = g.addNode("get:Message.Text", 180, 260).id;
    g.connections.push_back({ev, "Then", row, "In"});
    g.connections.push_back({ev, "Message", line, "Target"});
    g.connections.push_back({ev, "Message", text, "Target"});
    g.connections.push_back({line, "Line", row, "Line"});
    g.connections.push_back({text, "Text", row, "Text"});
    return s;
}

bool handlesNewMessage(const Script& script) {
    for (auto& g : script.graphs)
        for (auto& n : g.nodes)
            if (n.def == std::string("event:") + kNewMessageEvent) return true;
    return false;
}

std::string whyNotUsable(const Script& script, const Registry& reg) {
    const CompatibilityReport report = checkCompatibility(script, reg);
    if (!report.ok())
        return QCoreApplication::translate("msgapp::MessageScripting", "This script needs things this application does not provide:").toStdString() +
               '\n' + report.toText();
    if (!handlesNewMessage(script))
        return QCoreApplication::translate("msgapp::MessageScripting", "The script does not handle the event %1: nothing would run.")
            .arg(kNewMessageEvent).toStdString();
    return {};
}

Script makeCounterScript() {
    Script s;
    s.name = "Counter";
    s.doc = "Counts the messages in the global variable Count (it keeps its value from message to message) and shows it next to the line number.";
    s.variables.push_back({"Count", "int", Value(int64_t(0)), "", ""});
    Graph& g = s.addEventGraph("EventGraph");
    auto variable = [&](const char* def, double x, double y) {
        const NodeId id = g.addNode(def, x, y).id;
        g.findNode(id)->props = {{"name", "Count"}, {"scope", "global"}};
        return id;
    };
    const NodeId ev = g.addNode(std::string("event:") + kNewMessageEvent, 0, 0).id;
    const NodeId set = variable("var.set", 420, 0);
    const NodeId get1 = variable("var.get", 0, 220);
    const NodeId add = g.addNode("math.add", 220, 220).id;
    g.findNode(add)->typeArgs["T"] = "int";
    g.findNode(add)->defaults["B"] = Value(1);
    const NodeId row = g.addNode(kAddRowNode, 700, 0).id;
    g.findNode(row)->inputs = {{"Line", "int", false, false, Value()}, {"Count", "int", false, false, Value()}};
    const NodeId line = g.addNode("get:Message.Line", 420, 320).id;
    const NodeId get2 = variable("var.get", 420, 440);
    g.connections.push_back({ev, "Then", set, "In"});
    g.connections.push_back({get1, "Value", add, "A"});
    g.connections.push_back({add, "Result", set, "Value"});
    g.connections.push_back({set, "Then", row, "In"});
    g.connections.push_back({ev, "Message", line, "Target"});
    g.connections.push_back({line, "Line", row, "Line"});
    g.connections.push_back({get2, "Value", row, "Count"});
    return s;
}

std::vector<std::string> declaredColumns(const Program& program) {
    std::vector<std::string> out;
    for (auto& g : program.graphs)
        for (auto& n : g.nodes)
            if (n.defId == kAddRowNode)
                for (int pi : n.dataIn) {
                    const std::string& name = n.pins[static_cast<size_t>(pi)].name;
                    if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
                }
    return out;
}

// What the callbacks of the executor share with the run (they can outlive it: an editor may still be attached to the executor).
struct ScriptRun::State {
    mutable std::mutex mu;
    RowSink sink;                                      // what Instance::userData() of the executor's instance points to
    std::map<AsyncExecutor::Handle, int> lineOf;       // the message a task was started for
    ScriptRun::Update update;                          // what has not been taken yet
    std::string firstError;
    size_t posted = 0, done = 0;
    bool cancelled = false;
};

ScriptRun::ScriptRun(Runtime& rt, std::shared_ptr<const Program> program) : rt_(rt), state_(std::make_shared<State>()) {
    declared_ = declaredColumns(*program);
    std::weak_ptr<State> weak = state_;
    AsyncExecutor::Callbacks cb;                       // called on the worker thread of the executor
    cb.log = [weak](LogLevel level, const std::string& text) {
        if (auto st = weak.lock()) {
            std::lock_guard lk(st->mu);
            if (st->update.log.size() < kMaxLog) st->update.log.push_back((level == LogLevel::Info ? "" : "(!) ") + text);
        }
    };
    cb.completed = [state = state_](const Executor::Completion& c) {
        std::lock_guard lk(state->mu);
        ++state->done;
        if (state->cancelled) return;
        ++state->update.processed;
        auto it = state->lineOf.find(c.handle);
        const int line = it == state->lineOf.end() ? 0 : it->second;
        if (it != state->lineOf.end()) state->lineOf.erase(it);
        if (c.result.state != TaskState::Finished) {
            ++state->update.failed;
            if (state->firstError.empty()) state->firstError = "line " + std::to_string(line) + ": " + c.result.error;
        }
        // the rows this message added (the sink is only touched by the worker thread and, here, under the lock)
        for (auto& r : state->sink.rows) state->update.rows.push_back(std::move(r));
        state->sink.rows.clear();
    };
    AsyncExecutor::Options options;
    options.serial = true;                             // one task at a time: this is all the application has to say
    executor_ = std::make_shared<AsyncExecutor>(std::move(program), std::move(cb), nullptr, options);
    // before anything runs: the Add Row nodes of this instance write into the sink of this run
    executor_->inspect([this](Executor& ex) { ex.instance().setUserData(&state_->sink); });
}

ScriptRun::~ScriptRun() { cancel(); }

void ScriptRun::post(const MessagePtr& m) {
    // the lock is held while the task is posted, so the completion of a fast task finds its line number
    std::lock_guard lk(state_->mu);
    if (state_->cancelled) return;
    // wrap() hands the C++ object to the script without copying it; the script cannot change it (the properties are read only)
    const auto handle = executor_->postEvent(kNewMessageEvent, {rt_.registry().wrap(std::const_pointer_cast<Message>(m))});
    state_->lineOf[handle] = m->line;
    ++state_->posted;                                  // it starts now when no other message runs, otherwise it waits its turn
}

size_t ScriptRun::pending() const {
    std::lock_guard lk(state_->mu);
    return state_->posted - state_->done;
}

ScriptRun::Update ScriptRun::takeUpdate() {
    std::lock_guard lk(state_->mu);
    Update out = std::move(state_->update);
    state_->update = Update();
    out.firstError = state_->firstError;
    return out;
}

void ScriptRun::cancel() {
    {
        std::lock_guard lk(state_->mu);
        state_->cancelled = true;
    }
    if (executor_) executor_->abortAll();
}

} // namespace msgapp
