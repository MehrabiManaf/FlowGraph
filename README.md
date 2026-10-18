# FlowGraph

A visual scripting system that is meant to be **embedded in C++ applications**. Scripts are node graphs (events,
functions, variables, flow control, generic math / string / container nodes) that run on a small resumable interpreter
and can call into your own reflected C++ classes.

## The pieces

```
 your application                                           standard applications
 ┌──────────────────────────────┐                           ┌───────────────────────────────┐
 │ runtime only                 │  FlowGraphCore            │ flow         runs a script    │
 │   load / check / run scripts │  (no Qt)                  │ flowstudio   edit, run, debug │
 ├──────────────────────────────┤                           └───────────────────────────────┘
 │ + Executor / AsyncExecutor   │  non-blocking execution       both raise only one event:
 ├──────────────────────────────┤                               Main(Args: array<string>)
 │ + ScriptEditor (library)     │  FlowGraph::ScriptEditor
 └──────────────────────────────┘  (Qt 6 + KDDockWidgets)
```

* **Runtime** (`Core/`): types and reflection, compiler, interpreter, debugger, built-in nodes, plugins. An application
  that only needs to *run* scripts (read from a file, received over a socket...) links only this.
* **Executor / AsyncExecutor** (`Core/`): run scripts without blocking the host (see below).
* **ScriptEditor** (`Editor/`): the editor as a library. An application that lets its users create, change, run and
  debug scripts embeds it and tells it, through an `EditorHost`, what "run" means for that application.
* **flow** and **flowstudio** (`Apps/`): the *standard applications*. Their only event is `Main(Args: array<string>)`
  and scripts may end them with the **Exit** node. `flow script.flow [arguments]` runs a script (like `python script.py`),
  `flowstudio` is the script editor specialised for them.

Other directories: `Examples/` (script generator, `Scripts/*.flow`), `Plugins/ExamplePlugin/` (a sample plugin),
`Tests/` (core tests and editor tests).

## Build (Windows, MSVC 2022, Qt 6.9)

```
git submodule update --init
Tools\build.bat Release        # configures with Ninja and builds everything into build\Release\bin
```
`FG_BUILD_EDITOR`, `FG_BUILD_TESTS` and `FG_BUILD_EXAMPLES` switch parts off. The runtime has no Qt dependency; `flow`
needs only the runtime. (`build.bat` builds Release unless you pass `Debug`: rebuild the configuration you launch.)
`build.bat` finds Visual Studio 2022 (any edition) with `vswhere` and takes Qt from `QT_DIR` (default `C:\Qt\6.9.0\msvc2022_64`).

CMake presets do the same on every platform (`QT_DIR` names the Qt kit; builds go to `build/<preset>`):
```
cmake --preset windows-release      # or linux-release, windows-debug, linux-debug
cmake --build --preset windows-release
ctest --preset windows-release
cmake --preset core-only            # the runtime, flow, tests and examples without Qt
```
CI (`.github/workflows/ci.yml`) builds and tests everything with MSVC on Windows and GCC on Linux, and the runtime alone under
AddressSanitizer / UndefinedBehaviorSanitizer.

### Installing, and using FlowGraph from another CMake project

```
cmake --install build/Release --prefix C:/FlowGraph --component FlowGraph
```
installs the libraries, their headers, `flow` / `flowstudio` and a CMake package. A host project then only needs
```cmake
find_package(FlowGraph 0.2 REQUIRED COMPONENTS ScriptEditor)        # ScriptEditor is optional (it needs Qt 6)
target_link_libraries(myapp PRIVATE FlowGraph::Core FlowGraph::ScriptEditor)
```
with `CMAKE_PREFIX_PATH` pointing at the prefix (and at Qt). `Tests/Consumer` is such a project; CI builds it against an install.
Only the public API is exported from the libraries (`FG_CORE_EXPORT` / `FG_EDITOR_EXPORT`, generated `Export.hpp`); everything
else stays inside them, also on Linux (hidden visibility). The editor is installed when it is built as a shared library (FG_SHARED).

```
build\Release\bin\flow.exe Examples\Scripts\hello.flow a b c
build\Release\bin\flowstudio.exe Examples\Scripts\hello.flow
build\Release\bin\FlowGraphTests.exe
build\Release\bin\FlowGraphEditorTests.exe
```

## flow

```
flow [options] script.flow [arguments...]
  --check          load and compile the script, but do not run it
  --plugins DIR    load the plugins in DIR as well (repeatable)
  --no-plugins     skip FLOW_PLUGIN_PATH and the plugins folder
  --reference [F]  write the Markdown reference of every node, event, class and enum (plugins included) and exit
  --launch NAME    use configuration NAME of script.flowlaunch (arguments, environment, working directory, plugins)
```

* Plugins are loaded from the directories in the **`FLOW_PLUGIN_PATH`** environment variable (separated like `PATH`) and
  from the `plugins` folder next to the program.
* The script is checked before it is run. A script that needs an event or a plugin `flow` does not provide is refused
  (`flow: cannot load 'x.flow': ... this application does not provide the event 'OnHit'`).
* Exit code: the code given to the **Exit** node, `0` when the script ends normally, `1` when it fails at run time,
  `2` when it cannot be loaded or has compile errors. Ctrl+C aborts the script.
* `flow` runs the script directly on the calling thread: no scheduler, no extra threads, no step limit. Delay nodes
  really sleep.
* After the script has run, `flow` prints `Execution Time: <n> ms` on **stderr** (wall clock time of the `Main` event, delays
  included; loading and compiling are not counted), so redirecting or piping the script's output is not affected.

## flowstudio and launch configurations

`flowstudio [script.flow]` edits and debugs a standard script. What *Run* does is described by **launch configurations**,
stored next to the script as `script.flowlaunch` (JSON; *Run > Launch Configurations...* edits them):

```json
{ "format": "flowlaunch", "version": 1, "selected": "Verbose",
  "configurations": [
    { "name": "Verbose", "args": ["-v", "input.txt"], "cwd": "work", "env": { "MODE": "debug" },
      "pluginPaths": ["plugins"], "stepDelayMs": 0 } ] }
```
The toolbar of flowstudio has a **Launch configuration** selector (and a *Configure...* button) instead of an event box: Main is the only event, so the choice is how to launch it. `args` become `Args` of `Main`; `env` and `cwd` apply
while the script runs (`cwd` and `pluginPaths` are relative to the script's folder); `stepDelayMs` is the slow motion
used while debugging. `flow --launch Verbose script.flow` uses the same file.

## Using the runtime from C++

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
  (one `event:Tick` node per event, offered once in the node palette; *My Script > Event Graphs* lists the events the script
  handles). Event nodes cannot be deleted in the editor (neither can an event graph that contains them). Think of events as
  compiled into the application, whereas functions are the script's own.
* **Compatibility.** A script refers to the events of the application it was made for. `loadScript` /
  `fg::checkCompatibility` list everything this application lacks: events it never raises, events whose arguments changed
  (the script records the signature of the host events it handles), classes / functions / enums of missing plugins and
  unknown types. Runtime-only applications refuse such a script; the editor opens it anyway so that it can be repaired.
* Classes, enums, methods, properties, constructors, free functions and events become nodes automatically
  (`fn:`, `m:`, `get:`/`set:`, `new:`, `cast:`, `enum:`, `event:` node ids). Inheritance (also multiple) is honoured. Enum
  values print by name and may be given by name in scripts (`"default": "Sad"`).
* **[Docs/NodeReference.md](Docs/NodeReference.md)** lists every built-in node with its pins, types and defaults (regenerate it with
  `cmake --build build/Release --target node_reference`). An application writes its own, plugins and classes included, with
  `fg::nodeReferenceMarkdown(registry)`; `flow --reference` does it for the standard applications.
* The For Loop node visits `First <= Index < Last` (Last is excluded).
* Flow nodes: Branch, Sequence, For Loop, For Each, For Each Entry (maps, in key order), While, Break, Delay, Select,
  Switch on Int / String / any enum (*Switch on Mood*), Do Once, Gate (Enter / Open / Close / Toggle) and Flip Flop. Do Once,
  Gate and Flip Flop remember their state for the life of the instance (`NodeCtx::nodeState`; `Instance::resetVariables` forgets it).
  For Each and For Each Entry have a *Descending* input: last element (greatest key) first; Index stays the element's position.
* Print String, Print Warning and Print Error write to the log at their level (the Output Log colours them; `flow` writes
  warnings and errors to stderr). Print Error does not stop the script.
* Time: a timestamp is seconds since 1970-01-01 00:00 UTC as a float (milliseconds included). *Now* and *Seconds Since Start*
  (a steady clock of the instance, for measuring) are exec nodes, one reading per run. *Break Date Time* / *Make Date Time*
  convert to and from year, month, day, hour, minute, second, millisecond (plus the Weekday enum and the day of the year), in
  local time or UTC; out of range values carry over (month 13 is January). *Format Date Time* writes `%Y-%m-%d %H:%M:%S`
  style text (`%f` milliseconds, `%A` weekday, `%B` month, `%z` offset...; unknown codes are kept as they are), *Parse Date
  Time* reads ISO 8601 (`2026-10-18T14:30:05.250+02:00`; no zone = local time) and says whether the text was valid.
* Random Int / Random Float are exec nodes: each run draws one number that every connected pin reads. The numbers come from the
  instance (`Instance::random()`); *Set Random Seed* makes a run repeatable on any thread. The compiler warns about an exec node
  whose results are used but that is not connected to the execution flow (its outputs would keep their defaults).
* Format (`"{Name} is {Age}"`): add an input per placeholder in *Details > Inputs*; `{{` and `}}` are literal braces.
* Settings of a node (pin counts, the cases of a Switch on String) are described by `NodeDef::settings` and edited in *Details*;
  a custom node gets the same editors by declaring them. A node with several exec inputs asks `NodeCtx::entryPin()` which one fired.
* Nodes are generic where it makes sense (`T` in Add / Select / For Each / array and map nodes); the type is inferred from
  the connections. Function parameters can be passed by value, by reference or as const (checked at compile time).
* Plugins are shared libraries exporting `FG_PLUGIN(reg) { ... }` (see `Plugins/ExamplePlugin`): `loadPlugin`,
  `loadPluginsFrom(dir)`, `loadPluginsFromPathList("a;b")`, `loadPluginsFromEnvironment("VAR")`. Build them with the same
  compiler and FlowGraphCore version: `FG_PLUGIN` also exports how the plugin was built (plugin ABI version, compiler family,
  Debug / Release runtime, sizes of the shared classes) and the loader refuses a plugin that does not match, with the reason,
  instead of crashing later. A plugin that is reachable through several directories is loaded once.
* Custom nodes: `registry.addNode(fg::NodeDef{...})` with pins, optional dynamic pins and a `run` callback.
* **Asynchronous functions.** A function that returns a `std::future<T>` (or `std::shared_future<T>`) becomes a node that waits:
  ```cpp
  rt.registry().addFunction("Net.Get", [](const std::string& url) {
      return std::async(std::launch::async, [url] { return httpGet(url); });     // copy what the work needs
  }, {"Url"});
  ```
  The task waits like a Delay (the thread does not: `callEvent` returns with the task waiting, `tick` / `Executor::pump` / the
  `AsyncExecutor` poll it every 10 ms) and goes on with the result; an exception of the work is a script error at the node. Such a
  node is never pure. Called from C++ (`FunctionDesc::invoke`) the function waits for its result. A custom node gets the same
  behaviour with `NodeCtx::waitThenResume(poll)`, which runs the node again when the wait is over.

### Scripts you do not trust

A script that comes from somewhere else (a user, the network) can be kept in bounds; all of this is off by default:

```cpp
fg::CompileOptions opts;                                       // which nodes a script may use at all
opts.allowNode = [](const fg::NodeDef& d) { return d.source != "FilesPlugin"; };
auto program = rt.compile(script, opts);                       // a refused node is a compile error

fg::Instance::Limits lim;
lim.maxContainerSize = 100'000;                                // elements of one array / map
lim.maxStringLength = 1 << 20;                                 // bytes of one string
lim.maxTaskSeconds = 0.5;                                      // time one task may spend executing nodes
instance.setLimits(lim);
instance.setStepLimit(10'000'000);                             // nodes per run (default 50 million)
instance.setMaxCallDepth(64);                                  // nested function calls (default 256)
```
A script that goes beyond a limit fails with a script error at the node that did it; the host goes on.

### Hot reload

A host that keeps a script running (a game level, a server) can swap in a changed script without losing its state:

```cpp
auto report = instance.reload(rt.compile(changedScript));   // or executor.reload(...) / asyncExecutor.reload(...)
if (!report.ok()) log(report.error);                          // compile errors, another registry...: nothing changed
```
Variables that still exist keep their values (converted when their type changed, `report.reset` lists the ones that could not be),
new ones get their defaults, and Do Once / Gate / Flip Flop nodes that are still there keep their state. Tasks of the old program
that wait or are paused are aborted (`report.abortedTasks`); the executors report them to their completion handlers before
`reload` returns. Call it between events, not from inside a running script.

### The standard application

```cpp
fg::standard::registerStandardApp(rt.registry());    // event Main(Args: array<string>) and the Exit node
auto outcome = fg::standard::runMain(instance, {"a", "b"});   // outcome.exitCode, outcome.error
```

### Running without blocking

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

**Serial mode.** By default every posted event is a task and all of them run together, a slice at a time. A host that wants the events
handled *one after the other* (messages, requests, commands: the script keeps state, and a breakpoint should stop the whole sequence)
only says so:

```cpp
fg::AsyncExecutor::Options o; o.serial = true;           // or: executor.setSerial(true)
fg::AsyncExecutor async(program, cb, nullptr, o);
async.postEvent("NewMessage", {msg1}); async.postEvent("NewMessage", {msg2}); ...   // queued, in this order
```

There is one task at a time; the next event starts when the previous task has ended. A task that waits (`Delay`) or is stopped by a
debugger holds the others up, so a script is followed from its first node to its last for one event before the next begins. Off by
default: nothing changes for hosts that do not ask for it.

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
fged::applyDarkTheme(app); fged::initEditor();       // after QApplication
MyHost host; fged::ScriptEditor editor(host);
editor.open("level.flow"); editor.show();
```
By default every event and function of the script can be run and the editor asks for the arguments. Scripts run on a
worker thread (`AsyncExecutor`): the editor never blocks, even when a script computes for a long time or a native
function blocks; editing the script stops the run. `Apps/Studio` (`flowstudio`) is a complete host.

**There is one `ScriptEditor` per program**: creating a second one while the first exists throws `std::logic_error`
(`ScriptEditor::instance()` is the one that exists). An application with several things to edit lets the same editor edit them one
after the other, `editor.edit(script, "title")`.

The editor has two modes, chosen by `EditorHost::mode()`:

* `EditorMode::Standalone` (the default; flowstudio): the user works with files (New, Open, Save, Save As) and *Run* starts an
  event or function of the script, chosen in the toolbar (launch configurations).
* `EditorMode::Embedded`: the application owns the scripts and runs them itself (it raises the events), so the editor has no files and
  nothing to launch. The application hands a script over with `ScriptEditor::edit()` (there is no *New*, but *Open* works). **Only a
  script that fits the application is taken**: the editor checks that the application provides everything the script uses (events,
  nodes, types: a standalone script that handles `Main` is refused by an application that never raises it) and asks
  `EditorHost::acceptsScript()` (e.g. "does it handle the event this application is made for?"). A script that does not fit is refused,
  the reason goes to the log, and the editor keeps the script it has. *Save* does not write a file, it gives the
  edited script back (`EditorHost::applyScript`: replace the running script, run it again...; return `false` to keep the changes
  unsaved); *Save As* writes a copy. There is no *Run*: the debugger **attaches** to what the application runs. *Attach* asks
  `EditorHost::debugTarget()` for the `fg::AsyncExecutor` that runs the script (or the application calls
  `ScriptEditor::attach(executor)`); breakpoints stop its tasks, the Debugger panel shows them, Pause / Continue / Step control
  them, *Detach* lets everything continue. When the application replaces the executor while the editor is attached it attaches the new
  one before posting the first event.

## FlowGraph Studio keys

| | |
|-|-|
| Add a node | right-click the canvas, or drag from a pin and release on empty space (the list is filtered to nodes that fit) |
| Connect | drag between pins; Alt+click a pin breaks its links; double-click a data link to add a reroute |
| Variables | drag a variable from *My Script* onto the canvas (Ctrl = Get, Alt = Set, otherwise a menu) |
| Edit | Ctrl+Z / Ctrl+Y, Ctrl+C / X / V, Ctrl+D duplicate, Del delete, `C` add comment box (wraps the selection) |
| Navigate | wheel zooms, middle / right drag pans, Home frames everything |
| Refactor | *Edit > Collapse to Function* (or right-click a node) moves the selected nodes (two or more) into a new function and calls it in their place: links that come in become inputs (a *Get* of a variable is copied in, so it keeps reading the live value), links that go out become outputs; the execution must enter at one pin and leave towards one place. One undo step |
| Files | *File > Open Recent* lists the last 10 scripts opened or saved. Recent files and the dock layout are kept per application (an application that embeds the editor does not share them with flowstudio) |
| Find | Ctrl+F finds nodes by title, comment or value in every graph; Shift+F12 (or right-click > *Find References*) lists the nodes that use the selected variable or function, or the nodes of the selected kind; double-click a result to go there |
| Run | F5 (or F6) runs the entry chosen in the toolbar; the same button pauses the running script and continues a paused one; Shift+F5 stop |
| Debug | F9 toggles a breakpoint, F10 / F11 / Shift+F11 step over / into / out; hover pins while paused to see values; *Run > Execution speed* slows execution so the animated links are visible |

## Script file format

`.flow` files are JSON (`Script::toJson`) with `"format": "flowgraph"` and `"version"` (`Script::kFileVersion`; a file of a newer
version is refused rather than read partially and overwritten): variables, and graphs of nodes with connections
(`{"from": [nodeId, "Pin"], "to": [nodeId, "Pin"]}`). Script level node ids: `func.entry`, `func.return`, `func.call`, `var.get`,
`var.set`, `reroute`; events are `event:<Name>`. Nodes of host events (`event:Name`) carry `props.sig`, the
signature of the event when the script was made. A node of a definition with `customInputs` / `customOutputs` (`NodeDef`) can carry
the data pins the user added: `"inputs"` / `"outputs"` arrays of `{"name", "type", "default"}`, like the signature of a function.

Developer aids of flowstudio: `FG_STUDIO_SCREENSHOT=file.png` (screenshot and quit), `FG_STUDIO_RUN=Main`,
`FG_STUDIO_BREAK=Graph:id`, `FG_STUDIO_SELECT=node:Graph:id|var:name|graph:name`, `FG_STUDIO_SCENESHOT=file.png`.

## License

GPLv3 (see `LICENSE`); KDDockWidgets is used under its own license (`ThirdParty/KDDockWidgets`).
