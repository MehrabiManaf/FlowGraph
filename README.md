# FlowGraph

A visual scripting system for C++ applications: scripts are node graphs (events, functions, variables,
flow control, generic math / string / container nodes) that run on a small resumable interpreter and can
call into your own reflected C++ classes. It ships with **FlowGraph Studio**, a Qt editor with docking
panels, an execution debugger and animated links.

* `Core/`   - Qt-free C++20 library: types, reflection, compiler, interpreter, debugger, built-in nodes
* `Editor/` - Qt 6 widgets editor library (graph canvas, panels, run controller) built on KDDockWidgets
* `Apps/Studio/` - the FlowGraph Studio executable
* `Examples/` - `fgrun` command line runner, `fgexamples` script generator, `Scripts/*.flow`
* `Plugins/ExamplePlugin/` - a sample plugin DLL
* `Tests/` - core tests (plain C++) and editor tests (Qt, offscreen)

## Build (Windows, MSVC 2022, Qt 6.9)

```
git submodule update --init
Tools\build.bat Release        # configures with Ninja and builds everything into build\Release\bin
```
`FG_BUILD_EDITOR`, `FG_BUILD_TESTS` and `FG_BUILD_EXAMPLES` switch the parts off. The core library has no Qt dependency.

```
build\Release\bin\FlowGraphStudio.exe Examples\Scripts\hello.flow
build\Release\bin\fgrun.exe Examples\Scripts\fibonacci.flow
build\Release\bin\FlowGraphTests.exe
build\Release\bin\FlowGraphEditorTests.exe
```

## Using the runtime from C++

```cpp
#include <FlowGraph/Runtime.hpp>

struct Animal { std::string name; virtual std::string speak() const { return "..."; } };

fg::Runtime rt;                                    // registry with all built-in nodes
rt.registry().registerClass<Animal>("Animal")
    .constructor<>()
    .property("Name", &Animal::name)
    .method("Speak", &Animal::speak);
rt.registry().addEvent("Tick", {{"DeltaTime", "float"}});
rt.registry().addFunction("Game.Log", [](const std::string& s) { puts(s.c_str()); }, {"Text"});

fg::Script script = fg::Script::load("level.flow");
auto program = rt.compile(script);                 // diagnostics in program->diagnostics
fg::Instance instance(program);
instance.setLogHandler([](fg::LogLevel, const std::string& m) { puts(m.c_str()); });
instance.callEvent("Tick", {0.016});               // runs until done, blocked (Delay) or paused
instance.tick(0.016);                              // advances latent nodes
```

* Classes, enums, methods, properties, constructors, free functions and events become nodes automatically
  (`fn:`, `m:`, `get:`/`set:`, `new:`, `cast:`, `enum:`, `event:` node ids). Inheritance (also multiple) is honoured.
* Nodes are generic where it makes sense (`T` in Add / Select / For Each / array and map nodes); the type is
  inferred from the connections.
* Function parameters can be passed by value, by reference (`List: array<string>&`) or as const; const
  parameters cannot be modified (checked at compile time).
* The interpreter has no threads: a `Task` can be paused by the debugger at any node, suspended by latent
  nodes and time-sliced (`Task::run(maxNodes)`).
* Plugins are shared libraries that export `FG_PLUGIN(reg) { ... }` (see `Plugins/ExamplePlugin`); load them with
  `Runtime::loadPlugin` / `loadPluginsFrom`. Build them with the same compiler and FlowGraphCore version.
* Custom nodes: `registry.addNode(fg::NodeDef{...})` with pins, optional dynamic pins and a `run` callback.

## Script file format

`.flow` files are JSON (`Script::toJson`): variables, and graphs of nodes with connections
(`{"from": [nodeId, "Pin"], "to": [nodeId, "Pin"]}`). Script level node ids: `event.custom`, `event.call`,
`func.entry`, `func.return`, `func.call`, `var.get`, `var.set`, `reroute`.

## FlowGraph Studio

| | |
|-|-|
| Add a node | right-click the canvas, or drag from a pin and release on empty space (the list is filtered to nodes that fit) |
| Connect | drag between pins; Alt+click a pin breaks its links; double-click a data link to add a reroute |
| Variables | drag a variable from *My Script* onto the canvas (Ctrl = Get, Alt = Set, otherwise a menu) |
| Edit | Ctrl+Z / Ctrl+Y, Ctrl+C / X / V, Ctrl+D duplicate, Del delete, `C` add comment box (wraps the selection) |
| Navigate | wheel zooms, middle / right drag pans, Home frames everything |
| Run | F5 runs the entry chosen in the toolbar, F6 pause, Shift+F5 stop |
| Debug | F9 toggles a breakpoint, F10 / F11 / Shift+F11 step over / into / out; hover pins while paused to see values; *Run > Execution speed* slows execution so the animated links are visible |

Editing the script while it runs stops the run. Compile errors and warnings are listed in *Compile Results*.
Developer aids: `FG_STUDIO_SCREENSHOT=file.png` (screenshot and quit), `FG_STUDIO_RUN=Main`, `FG_STUDIO_BREAK=Graph:id`,
`FG_STUDIO_SELECT=node:Graph:id|var:name|graph:name`.

## License

GPLv3 (see `LICENSE`); KDDockWidgets is used under its own license (`ThirdParty/KDDockWidgets`).
