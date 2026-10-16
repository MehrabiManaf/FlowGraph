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

## flowstudio and launch configurations

`flowstudio [script.flow]` edits and debugs a standard script. What *Run* does is described by **launch configurations**,
stored next to the script as `script.flowlaunch` (JSON; *Run > Launch Configurations...* edits them):

```json
{ "format": "flowlaunch", "version": 1, "selected": "Verbose",
  "configurations": [
    { "name": "Verbose", "args": ["-v", "input.txt"], "cwd": "work", "env": { "MODE": "debug" },
      "pluginPaths": ["plugins"], "stepDelayMs": 0 } ] }
```
Every configuration shows up in the toolbar as `Main - <name>`. `args` become `Args` of `Main`; `env` and `cwd` apply
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

* **Compatibility.** A script refers to the events of the application it was made for. `loadScript` /
  `fg::checkCompatibility` list everything this application lacks: events it never raises, events whose arguments changed
  (the script records the signature of the host events it handles), classes / functions / enums of missing plugins and
  unknown types. Runtime-only applications refuse such a script; the editor opens it anyway so that it can be repaired.
* Classes, enums, methods, properties, constructors, free functions and events become nodes automatically
  (`fn:`, `m:`, `get:`/`set:`, `new:`, `cast:`, `enum:`, `event:` node ids). Inheritance (also multiple) is honoured. Enum
  values print by name and may be given by name in scripts (`"default": "Sad"`).
* The For Loop node visits `First <= Index < Last` (Last is excluded).
* Nodes are generic where it makes sense (`T` in Add / Select / For Each / array and map nodes); the type is inferred from
  the connections. Function parameters can be passed by value, by reference or as const (checked at compile time).
* Plugins are shared libraries exporting `FG_PLUGIN(reg) { ... }` (see `Plugins/ExamplePlugin`): `loadPlugin`,
  `loadPluginsFrom(dir)`, `loadPluginsFromPathList("a;b")`, `loadPluginsFromEnvironment("VAR")`. Build them with the same
  compiler and FlowGraphCore version. A plugin that is reachable through several directories is loaded once.
* Custom nodes: `registry.addNode(fg::NodeDef{...})` with pins, optional dynamic pins and a `run` callback.

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

## FlowGraph Studio keys

| | |
|-|-|
| Add a node | right-click the canvas, or drag from a pin and release on empty space (the list is filtered to nodes that fit) |
| Connect | drag between pins; Alt+click a pin breaks its links; double-click a data link to add a reroute |
| Variables | drag a variable from *My Script* onto the canvas (Ctrl = Get, Alt = Set, otherwise a menu) |
| Edit | Ctrl+Z / Ctrl+Y, Ctrl+C / X / V, Ctrl+D duplicate, Del delete, `C` add comment box (wraps the selection) |
| Navigate | wheel zooms, middle / right drag pans, Home frames everything |
| Run | F5 runs the entry chosen in the toolbar, F6 pause, Shift+F5 stop |
| Debug | F9 toggles a breakpoint, F10 / F11 / Shift+F11 step over / into / out; hover pins while paused to see values; *Run > Execution speed* slows execution so the animated links are visible |

## Script file format

`.flow` files are JSON (`Script::toJson`): variables, and graphs of nodes with connections
(`{"from": [nodeId, "Pin"], "to": [nodeId, "Pin"]}`). Script level node ids: `event.custom`, `event.call`, `func.entry`,
`func.return`, `func.call`, `var.get`, `var.set`, `reroute`. Nodes of host events (`event:Name`) carry `props.sig`, the
signature of the event when the script was made.

Developer aids of flowstudio: `FG_STUDIO_SCREENSHOT=file.png` (screenshot and quit), `FG_STUDIO_RUN=Main`,
`FG_STUDIO_BREAK=Graph:id`, `FG_STUDIO_SELECT=node:Graph:id|var:name|graph:name`, `FG_STUDIO_SCENESHOT=file.png`.

## License

GPLv3 (see `LICENSE`); KDDockWidgets is used under its own license (`ThirdParty/KDDockWidgets`).
