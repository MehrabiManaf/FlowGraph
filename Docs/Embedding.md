# Embedding FlowGraph

How to run scripts in your application, give them your classes, functions and events, and embed the script editor.

## Building against FlowGraph

```
cmake --install build/Release --prefix C:/FlowGraph --component FlowGraph
```
installs the libraries, their headers, `flow` / `flowstudio` (with the FileSystem plugin in `bin/plugins`) and a CMake package.
A host project then only needs
```cmake
find_package(FlowGraph 0.4 REQUIRED COMPONENTS ScriptEditor)        # ScriptEditor is optional (it needs Qt 6)
target_link_libraries(myapp PRIVATE FlowGraph::Core FlowGraph::ScriptEditor)
```
with `CMAKE_PREFIX_PATH` pointing at the prefix (and at Qt). The editor is installed when it is built as a shared library
(`FG_SHARED`, the default).

## The runtime

```cpp
#include <FlowGraph/Runtime.hpp>

struct Animal { std::string name; virtual std::string speak() const { return "..."; } };

fg::Runtime rt;                                      // registry with all built-in nodes (no plugins are loaded)
rt.registry().registerClass<Animal>("Animal").constructor<>().property("Name", &Animal::name).method("Speak", &Animal::speak);
rt.registry().registerEnum<Mood>("Mood").value("Happy", Mood::Happy).value("Sad", Mood::Sad);
rt.registry().addEvent("Tick", {{"DeltaTime", "float"}});                // your application's events
rt.registry().addFunction("Game.Log", [](const std::string& s) { puts(s.c_str()); }, {"Text"});
rt.loadPluginsFromEnvironment("MYAPP_PLUGIN_PATH");                      // optional

auto loaded = rt.loadScript("level.flow");           // read + check against this application
if (!loaded.ok()) { fprintf(stderr, "%s", loaded.explanation().c_str()); return; }   // "I cannot load this script"
auto program = rt.compile(loaded.script);            // diagnostics in program->diagnostics
fg::Instance instance(program);
instance.callEvent("Tick", {0.016});                 // synchronous: runs until done, or until a Delay / breakpoint
```

* **Events belong to the application.** `registry.addEvent("Tick", {{"DeltaTime", "float"}})` declares a connection point
  between the application and its scripts. Scripts cannot declare, rename, change or delete events: they only *handle* them
  (one `event:Tick` node per event, offered once in the node palette). Think of events as compiled into the application,
  whereas functions are the script's own.
* **Compatibility.** A script refers to the events of the application it was made for. `loadScript` /
  `fg::checkCompatibility` list everything this application lacks: events it never raises, events whose arguments changed
  (the script records the signature of the host events it handles), classes / functions / enums of missing plugins and
  unknown types. Runtime-only applications refuse such a script; the editor opens it anyway so that it can be repaired.
* **Types of C++ arguments and results:** `bool`, the integers (`int64_t`...), `double` / `float`, `std::string` (text),
  `fg::Value` (`any`), `std::vector<T>` (`array<T>`), `std::map<K, V>` (`map<K, V>`), registered classes and enums, `fg::Bytes`
  (`bytes`: data, shared without a copy) and `std::byte` (`byte`, 0 to 255; `uint8_t` stays an int).
* **Structs** are types of the script, not of the registry: each compiled program has its own (`Program::types`). An application
  reads a struct value it gets from a script (a variable, the result of a function) with `Value::isStruct()` and
  `structPtr()`: `fields[i]` is the value of the field named `type->fields[i].name`.
* Classes, enums, methods, properties, constructors, free functions and events become nodes automatically
  (`fn:`, `m:`, `get:`/`set:`, `new:`, `cast:`, `enum:`, `event:` node ids). Inheritance (also multiple) is honoured. Enum
  values print by name and may be given by name in scripts (`"default": "Sad"`).
* **[NodeReference.md](NodeReference.md)** lists every built-in node with its pins, types and defaults. An application writes
  its own, plugins and classes included, with `fg::nodeReferenceMarkdown(registry)`; `flow --reference` does it for the
  standard applications.
* **Plugins** are shared libraries exporting `FG_PLUGIN(reg) { ... }` (see [`Plugins/FileSystem`](../Plugins/FileSystem)):
  `loadPlugin`, `loadPluginsFrom(dir)`, `loadPluginsFromPathList("a;b")`, `loadPluginsFromEnvironment("VAR")`. Build them with
  the same compiler and FlowGraphCore version: the loader refuses a plugin built differently, with the reason, instead of
  crashing later. A plugin adds classes, functions, enums and nodes, but no events: only the application declares them (a
  plugin that calls `addEvent` is refused).
* **Custom nodes:** `registry.addNode(fg::NodeDef{...})` with pins, optional dynamic pins and a `run` callback. Settings of a
  node (pin counts, the cases of a Switch on String) are described by `NodeDef::settings` and edited in *Details*. A node with
  several exec inputs asks `NodeCtx::entryPin()` which one fired; a node that waits sets `NodeDef::waits`.
* **Asynchronous functions.** A function that returns a `std::future<T>` (or `std::shared_future<T>`) becomes a node that waits:
  ```cpp
  rt.registry().addFunction("Net.Get", [](const std::string& url) {
      return std::async(std::launch::async, [url] { return httpGet(url); });     // copy what the work needs
  }, {"Url"});
  ```
  The task waits like a Delay (the thread does not: `callEvent` returns with the task waiting, `tick` / `Executor::pump` / the
  `AsyncExecutor` poll it every 10 ms) and goes on with the result; an exception of the work is a script error at the node. A
  custom node gets the same behaviour with `NodeCtx::waitThenResume(poll)`, which runs the node again when the wait is over.

## Scripts you do not trust

A script that comes from somewhere else (a user, the network) can be kept in bounds; all of this is off by default:

```cpp
fg::CompileOptions opts;                                       // which nodes a script may use at all
opts.allowNode = [](const fg::NodeDef& d) { return d.source != "FileSystem"; };
auto program = rt.compile(script, opts);                       // a refused node is a compile error

fg::Instance::Limits lim;
lim.maxContainerSize = 100'000;                                // elements of one array / map
lim.maxStringLength = 1 << 20;                                 // bytes of one string (or of one bytes value)
lim.maxTaskSeconds = 0.5;                                      // time one task may spend executing nodes
instance.setLimits(lim);
instance.setStepLimit(10'000'000);                             // nodes per run (default 50 million)
instance.setMaxCallDepth(64);                                  // nested function calls (default 256)
```
A script that goes beyond a limit fails with a script error at the node that did it; the host goes on. A runtime has no node
that reaches the operating system unless the application registers the System nodes (`fg::registerSystemNodes`, also done by
`registerStandardApp`) or loads the FileSystem plugin; `allowNode` can refuse them per script (`d.source != "system"`,
`d.source != "FileSystem"`).

## Hot reload

A host that keeps a script running (a game level, a server) can swap in a changed script without losing its state:

```cpp
auto report = instance.reload(rt.compile(changedScript));   // or executor.reload(...) / asyncExecutor.reload(...)
if (!report.ok()) log(report.error);                          // compile errors, another registry...: nothing changed
```
Variables that still exist keep their values (converted when their type changed, `report.reset` lists the ones that could not be),
new ones get their defaults, and Do Once / Gate / Flip Flop nodes that are still there keep their state. Tasks of the old program
that wait or are paused are aborted (`report.abortedTasks`); the executors report them to their completion handlers before
`reload` returns. Call it between events, not from inside a running script.

## The standard application

`flow` and `flowstudio` are built on it; an application can be one too:

```cpp
fg::standard::registerStandardApp(rt.registry());    // event Main(Args: array<string>), the Exit node and the System nodes
auto outcome = fg::standard::runMain(instance, {"a", "b"});   // outcome.exitCode, outcome.error
```

## Running without blocking

`Instance::callEvent` runs on the calling thread until the script ends or waits. For hosts that must stay responsive:

```cpp
fg::Executor ex(program);                 // cooperative: you call pump() from your own loop / frame / timer
ex.postEvent("Tick", {0.016});
ex.pump(dtSeconds);                       // advances Delay nodes, runs every ready task for a bounded number of nodes

fg::AsyncExecutor::Callbacks cb;          // or: the same scheduler on a thread of its own
cb.log       = [](fg::LogLevel l, const std::string& m) { ... };   // callbacks run on the worker thread
cb.completed = [](const fg::Executor::Completion& c) { ... };
fg::AsyncExecutor async(program, cb);
auto h = async.postEvent("Tick", {0.016});         // returns immediately, from any thread
async.pause(h); async.step(h, fg::Debugger::Command::StepOver); async.resume(h);
async.inspect([&](fg::Executor& e) { /* look at tasks and variables while the script is paused */ });
async.stop();                                      // aborts everything and joins
```
Breakpoints, stepping, the `Debugger` callbacks and `Task::run(maxNodes)` time slicing are available in both.

**Serial mode.** By default every posted event is a task and all of them run together, a slice at a time. A host that wants the
events handled *one after the other* (messages, requests, commands: the script keeps state, and a breakpoint should stop the whole
sequence) only says so:

```cpp
fg::AsyncExecutor::Options o; o.serial = true;           // or: executor.setSerial(true)
fg::AsyncExecutor async(program, cb, nullptr, o);
async.postEvent("NewMessage", {msg1}); async.postEvent("NewMessage", {msg2}); ...   // queued, in this order
```
There is one task at a time; the next event starts when the previous task has ended. A task that waits (`Delay`) or is stopped by a
debugger holds the others up.

## Embedding the script editor

```cpp
class MyHost : public fged::EditorHost {
    fg::Runtime& runtime() override { return rt_; }
    QString applicationName() const override { return "My Game Editor"; }
    std::vector<fged::LaunchEntry> launchEntries(const fg::Program&) override;   // optional: what can be run
    bool prepareLaunch(const fged::LaunchEntry&, fged::LaunchSpec&, QWidget*) override;   // optional: arguments
    void attached(fged::ScriptEditor& e) override { e.menu("&Game")->addAction(...); }    // optional: own menus
    ...
};
fged::applySavedTheme(app, "My Game Editor"); fged::initEditor();   // after QApplication (the theme chosen in View > Theme)
MyHost host; fged::ScriptEditor editor(host);
editor.open("level.flow"); editor.show();
```
By default every event and function of the script can be run and the editor asks for the arguments. Scripts run on a
worker thread (`AsyncExecutor`): the editor never blocks, even when a script computes for a long time or a native
function blocks; editing the script stops the run. `Apps/Studio` (`flowstudio`) and [`Examples/Embedded`](../Examples/Embedded)
are complete hosts.

**There is one `ScriptEditor` per program**: creating a second one while the first exists throws `std::logic_error`
(`ScriptEditor::instance()` is the one that exists). An application with several things to edit lets the same editor edit them one
after the other, `editor.edit(script, "title")`.

The editor has two modes, chosen by `EditorHost::mode()`:

* `EditorMode::Standalone` (the default; flowstudio): the user works with files (New, Open, Save, Save As) and *Run* starts an
  event or function of the script, chosen in the toolbar (launch configurations).
* `EditorMode::Embedded`: the application owns the scripts and runs them itself (it raises the events), so the editor has no files and
  nothing to launch. The application hands a script over with `ScriptEditor::edit()`. **Only a script that fits the application is
  taken**: the editor checks that the application provides everything the script uses and asks `EditorHost::acceptsScript()` (e.g.
  "does it handle the event this application is made for?"); a script that does not fit is refused, with the reason in the log.
  *Save* gives the edited script back (`EditorHost::applyScript`: replace the running script, run it again...; return `false` to
  keep the changes unsaved); *Save As* writes a copy. There is no *Run*: the debugger **attaches** to what the application runs.
  *Attach* asks `EditorHost::debugTarget()` for the `fg::AsyncExecutor` that runs the script (or the application calls
  `ScriptEditor::attach(executor)`); breakpoints stop its tasks, Pause / Continue / Step control them, *Detach* lets everything
  continue.

The editor's theme (*View > Theme*: Dark, Light or System) is offered by an embedding application with
`EditorHost::offersThemeChoice`; `fged::themedIcon(":/Icons/...")` gives the editor's icons in the colours of the theme. Recent
files and the dock layout are kept per application.

## Languages in your application

The editor's texts and the texts of the built-in nodes are translated (see [Languages](../README.md#languages)); the language is
applied with `fged::applyLanguage` / `fged::applySavedLanguage`. Scripts keep the English node names, so a script made in one
language opens unchanged in any other.

* **The nodes your application registers** are translated with a catalog of its own: `fged::loadNodeTranslations(file)` after
  `applyLanguage`. A catalog is JSON, one text per line with its English text next to it (`"node:flow.branch": ["Branch",
  "شاخه"]`).
* **A plugin** ships catalogs of its own nodes next to its library, `plugins/translations/<Plugin>_<code>.json`; the editor loads
  the one of its language for every plugin the runtime loads. `flow --plugins <dir> --only <Plugin> --translations
  <Plugin>_<code>.json` writes or updates one with the plugin's texts only (`Plugins/FileSystem/Translations` is an example).

## Script file format

`.flow` files are JSON (`Script::toJson`) with `"format": "flowgraph"` and `"version"` (`Script::kFileVersion`; a file of a newer
version is refused rather than read partially and overwritten): variables, and graphs of nodes with connections
(`{"from": [nodeId, "Pin"], "to": [nodeId, "Pin"]}`). Script level node ids: `func.entry`, `func.return`, `func.call`, `var.get`,
`var.set`, `reroute`; events are `event:<Name>`. Nodes of host events carry `props.sig`, the signature of the event when the
script was made. A node of a definition with `customInputs` / `customOutputs` (`NodeDef`) can carry the data pins the user added:
`"inputs"` / `"outputs"` arrays of `{"name", "type", "default"}`, like the signature of a function.
