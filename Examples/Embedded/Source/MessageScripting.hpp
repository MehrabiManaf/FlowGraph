#pragma once
// The scripting side of the embedded example, without any Qt: what the application tells FlowGraph (a class, an event and a node) and how it
// runs a script over all the messages. The Qt side (windows, tables, the editor) is in TableDock / MainWindow.
//
//   class Message          Text, Line                           what the script sees of one line of the messages file
//   event NewMessage       (Message: Message)                   raised once for every message
//   node  Add Row          pins: whatever the user named        every time it runs, one row is added to the table
#include <FlowGraph/Runtime.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace msgapp {

struct Message {
    std::string text;
    int line = 0;                                  // 1-based line number in the file
};
using MessagePtr = std::shared_ptr<const Message>;

inline constexpr const char* kNewMessageEvent = "NewMessage";
inline constexpr const char* kAddRowNode = "table.addrow";

// One cell of the table. Numbers stay numbers (the table sorts them as numbers).
using Cell = std::variant<std::monostate, int64_t, double, bool, std::string>;
struct Row {
    std::vector<std::pair<std::string, Cell>> cells;           // by column name, in the order of the pins
};

// Tells the runtime about Message, NewMessage and Add Row. Call once, before scripts are loaded.
void registerMessageTypes(fg::Registry& reg);

// What a new table starts with: it handles NewMessage and does nothing with it. Every message is dropped, no Add Row node runs, the table
// stays empty: the user decides what goes into it.
fg::Script makeDefaultScript();
// Two scripts to try (the self test and `--write-samples` use them):
//   list     NewMessage -> Add Row with the columns Line and Text: a row per message
//   counter  a global variable Count goes up by one per message, and the row shows Line and Count: the state of a script is kept
fg::Script makeListScript();
fg::Script makeCounterScript();

// Does the script handle the event NewMessage? Without a handler nothing would ever run.
bool handlesNewMessage(const fg::Script& script);
// Can a table use this script? Empty when it can; otherwise what is wrong: it needs events, nodes or types this application does not have
// (a script made for another application, e.g. a standalone one that handles Main), or it does not handle NewMessage. A script is only
// put into a table or the editor when this is empty.
std::string whyNotUsable(const fg::Script& script, const fg::Registry& reg);

// The columns the Add Row nodes of a compiled script declare, in the order of the nodes and their pins (so an empty table has headers).
std::vector<std::string> declaredColumns(const fg::Program& program);

inline constexpr size_t kMaxLog = 200;

// A running script. ONE instance lives as long as the run: the event NewMessage is raised for the messages one after the other as they come
// (post()), and the variables of the script keep their values from message to message. The messages are handled SERIALLY: the executor is
// created with AsyncExecutor::Options::serial, which is all it takes. There is one task at a time, the next message starts when the handler of
// the previous one has ended (the others wait in the executor's queue), so a script is followed from the first node to the last for one
// message before the next message begins, and a breakpoint stops the whole sequence. A run starts with the messages the store already has
// and goes on with the new ones; another script (or the same one saved again) is another run, with fresh variables.
// Every Add Row node that runs puts a row into the run; takeUpdate() collects what happened since it was last called.
//
// The script runs on an fg::AsyncExecutor (a worker thread of its own), so the application never waits for it and a script editor can attach
// its debugger to it: executor() is what EditorHost::debugTarget() hands to the editor. Create the run, give the executor to whoever wants it
// (the editor attaches before anything runs), then post the messages. Does not touch any window; every function may be called from any thread.
class ScriptRun {
public:
    ScriptRun(fg::Runtime& rt, std::shared_ptr<const fg::Program> program);
    ~ScriptRun();
    ScriptRun(const ScriptRun&) = delete;
    ScriptRun& operator=(const ScriptRun&) = delete;

    std::shared_ptr<fg::AsyncExecutor> executor() const { return executor_; }
    void post(const MessagePtr& message);          // raises NewMessage for it (after the ones posted before)
    size_t pending() const;                        // messages posted whose handler has not finished: the one that runs and the ones that wait
    const std::vector<std::string>& declared() const { return declared_; }     // the columns the script declares (Add Row pins)

    struct Update {
        std::vector<Row> rows;                     // what Add Row nodes added
        int processed = 0;                         // messages whose handler finished
        int failed = 0;                            // ... and how many of those ended with an error
        std::string firstError;                    // "line 7: division by zero" (the first of the whole run)
        std::vector<std::string> log;              // Print String output (at most kMaxLog lines per call)
        bool empty() const { return rows.empty() && !processed && log.empty(); }
    };
    Update takeUpdate();                           // what happened since the last call (empty when nothing)
    void cancel();                                 // aborts what runs; nothing is reported any more

private:
    struct State;                                  // shared with the callbacks of the executor, which can outlive this object
    fg::Runtime& rt_;
    std::vector<std::string> declared_;
    std::shared_ptr<State> state_;
    std::shared_ptr<fg::AsyncExecutor> executor_;
};

} // namespace msgapp
