# FlowGraph rewrite - progress notes

Goal: rewrite D:\ClaudeProjects\bpscript (Qt-free core, JSON scripts, reflection, debugger) with the
editor look and feel of D:\FGTest\FlowGraph (palette-driven nodes, KDDockWidgets docking, animated fired links).

Decisions: C++20, Qt 6.9 MSVC2022, KDDockWidgets submodule, Qt-free core + separate editor, full feature scope.
Build: Tools\build.bat Release (vcvars + Ninja + Qt prefix).
Old bpscript sources for reference: run "git show HEAD:<file>" inside D:\ClaudeProjects\bpscript
(its working tree has ~47 tracked files deleted; they are restorable with git restore).

## Design (namespace fg)
- Script (plain data, JSON) -> Compiler -> Program (resolved pins, links, slots, diagnostics) -> Instance/Task (VM).
- VM is resumable (explicit frames + continuations): debugger pause/step without threads, latent nodes (Delay), time slicing.
- Generic nodes via type parameters (T/K/V) resolved on connect; pins use type-expression strings ("array<T>").
- Pure nodes evaluated lazily with a per-epoch cache; impure nodes keep outputs in frame slots.
- By-ref params alias caller variable Cells; const params checked at compile time.
- Nodes continue through exec output 0 by default; NodeCtx::next/end/call/breakLoop/wait override.

## Done (Core compiles; 16 tests pass: Toolsuild.bat Debug, then build\Debugin\FlowGraphTests.exe)
Core/Include/FlowGraph: Json, Value, Types, NodeDef, Script, TypeExpr, Registry, Bind, Program, Compiler, Vm,
  Builtins, Plugin, Runtime (.hpp)
Core/Source: Json, TypeExpr, Value, Types, Registry (types, rules, generated nodes), Script (model + JSON),
  Compiler (type inference, wiring, const/by-ref checks), Vm (tasks, debugger), Builtins (script nodes, flow, math,
  strings, containers), Runtime (plugins).
Tests/Main.cpp: mini runner (loops, functions, by-ref, const, reflection, generics, delay, break, debugger...).
Script node ids: event.custom(props name), event.call(event), func.entry/return/call(function), var.get/var.set
  (props name + scope global|local|param), reroute. Generated: fn:/m:/get:/set:/new:/cast:/enum:/event:.

Core extras done: Inspect (ValueView tree, globals/frame variables), Intellisense (buildPalette, filterPalette by
  dragged pin, searchPalette, instantiate), Examples/ (fgrun runner, fgexamples generator -> Examples/Scripts/*.flow),
  Plugins/ExamplePlugin (Counter class, Example.Greet, OnExample event; built to bin/plugins). 19 tests pass.
  Note: plugins link FlowGraphCore statically (same version/compiler as the host required).

Editor done (Editor/Source, Apps/Studio): Document (undo via script snapshots), GraphScene/NodeItem/LinkItem/CommentItem/
  GraphView/PalettePopup (reconcile with the compiled graph; link drag, palette, copy/paste, comments, reroute, variable
  drops), panels (MyScript, Details, Compile Results, Output Log, Debugger), RunController (timer sliced run, breakpoints,
  stepping, slow motion, link animation), StudioWindow (KDDockWidgets, tabs per graph). FlowGraphEditorTests (offscreen) cover
  scene / document / run controller logic. README.md documents the use.

## Ideas / not done
- Details panel: per pin default editing for non-primitive types, node alignment tools, minimap, search in graph.
- Variable watch editing while paused, hot reload of a running script, save layout of the docks (KDDW LayoutSaver).
- Plugins link FlowGraphCore statically (works, same version/compiler needed); a shared core would be cleaner.

## Status (latest)
- Core + editor build and pass tests (Debug verified after the last fixes; Release verified before them).
- Fixed: pin hit-testing (odd-even shape) and node positions reverting after a move (scene read stale compiled positions).
- Editor tests include simulated mouse drags (ViewFixture in Tests/EditorTests.cpp). Untested by hand: context menus,
  palette popup, rubber-band select, panning/zoom.
- Gotchas: the bash tool turns backslash-n inside string literals into real newlines (use %c,10 or the Edit tool);
  Qt's `slots` macro clashes with member names; QTest::mouseMove sends no buttons (use QMouseEvent + sendEvent).
- Tools/real-input-smoke.ps1 drives the real Studio exe with OS mouse input (SetCursorPos/mouse_event) using the
  FG_STUDIO_PROBE hook: usage `real-input-smoke.ps1 -Exe <Studio.exe> -Script <x.flow> -Prefix <tmp path>`. Coordinates from the
  probe are logical pixels: multiply by the device pixel ratio (it is printed); points beyond the screen edge are clamped.
- build.bat defaults to Release: after editor fixes rebuild Release too, or the Studio you launch is stale.
- UI round 2: Backspace/Delete/Ctrl+C in a pin editor no longer reach the graph (view hands keys to the focused proxy widget); Details dock
  on the right (tabbed with Debugger); Local Variables only while a function graph/function item is active; container kind is its own
  column (TypeSpec: Single/Array/Map + key) in variable and parameter editors; pins: array = dot grid, map = circle+square, const = padlock,
  by-ref = diamond (also on containers). Dev hooks: FG_STUDIO_SCENESHOT renders the current graph at 2x.

## Architecture round (apps, executors, compatibility)
- Core: Executor (cooperative pump) + AsyncExecutor (worker thread: post events, pause/step/resume/abort, inspect(), callbacks on the
  worker thread); Debugger is thread safe; NodeCtx::exit / RunResult::exitCode; Instance::advanceWaits / nextWakeIn;
  Compatibility (checkCompatibility, stampHostEvents - host event nodes record props.sig; compile reports changed signatures);
  StandardApp (event Main(Args: array<string>), Exit node std.exit, runMain fast path, LaunchFile/LaunchConfig = <script>.flowlaunch);
  Runtime::loadScript / loadPluginsFromPathList / loadPluginsFromEnvironment (FLOW_PLUGIN_PATH), duplicate plugin loads are skipped.
- Apps: `flow` (Apps/Flow, runtime only: --check/--plugins/--launch, exit codes 0/1/2, Ctrl+C) and `flowstudio` (Apps/Studio:
  StandardEditorHost = EditorHost with launch configurations). fgrun / FlowGraphStudio no longer exist (stale exes may linger in build dirs).
- Editor library renamed ScriptEditor (target FlowGraph::ScriptEditor, public header FlowGraphEditor/ScriptEditor.hpp): EditorHost
  (runtime, launchEntries, prepareLaunch, aboutToRun/runFinished, attached, scriptChanged, preferredEntry) + ScriptEditor facade.
  RunController now runs scripts on an AsyncExecutor (GUI never blocks); the debug panel reads state through AsyncExecutor::inspect.
- Interpreter speed: InputBuf only constructs the inputs a node needs (3M loop iterations 1.9 s -> 1.05 s in Release).
- Ideas: profile the VM further (NodeCtx / Continuation per node), save the dock layout, per-script plugin requirements in the file.
- Events are application-owned: custom events (event.custom / event.call, Script::customEvents, renameEvent, NodeOp::CallEvent) were
  removed from core, editor and tests. Scripts only handle `event:<Name>` nodes; the palette offers each event once; My Script lists all
  events of the app (dimmed = not handled, double click adds the handler); Details shows an event read-only; copy/paste never
  duplicates a handler. Hosts decide what a new script contains (EditorHost::initializeNewScript, standard::createMainTemplate).
- flowstudio: the entries box is a *Launch configuration* selector (EditorHost::launchLabel/launchEntrySelected, ScriptEditor::addToLaunchSelector for the Configure... button); Event Graphs lists only the events the script handles and has no + button (context menu: New event graph).
- Event nodes are undeletable in the editor (GraphScene::deleteSelected skips them and reports it; "Remove handler" is gone; event graphs with events cannot be deleted).
