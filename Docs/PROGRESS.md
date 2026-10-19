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
- flowstudio: the entries box is a *Launch configuration* selector (EditorHost::launchLabel/launchEntrySelected, ScriptEditor::addToLaunchSelector for the Configure... button); Event Graphs lists only the events the script handles and has no + button; event graphs cannot be created from it and are not listed (only their events).
- Event nodes are undeletable in the editor (GraphScene::deleteSelected skips them and reports it; "Remove handler" is gone; event graphs with events cannot be deleted).
- Side docks (My Script / Details / Debugger) have no maximum width any more, so they follow their dock; ScriptEditorWindow::applyInitialSizes sets My Script ~300 px and Details ~360 px the first time the window is shown (KDDockWidgets ignores InitialOption sizes).

## Interpreter speed, round 2 (Core, synchronous runs)
Measured with `FlowGraphBench` (Tests/Bench.cpp; `FlowGraphBench --stable --runs 9 [--debugger] [--scale F] [filter]`), Release /O2, best of
N runs, original Core vs this one, binaries run alternately. Total of the 13 benchmarks: 4997 ms -> 1079 ms (4.6x, geometric mean 4.5x).

| benchmark | before ms | after ms | speedup |
|---|---|---|---|
| For loop `acc = acc + i` (2M) | 590 | 83 | 7.2x |
| For loop `((acc + i) * 3) % 1000003` (1M) | 541 | 81 | 6.7x |
| For loop, float math (1M) | 427 | 54 | 7.9x |
| While loop (1M) | 470 | 57 | 8.2x |
| For loop with Branch (1M) | 632 | 109 | 5.8x |
| Recursive Fib(27), 832k calls | 466 | 122 | 3.8x |
| Array Add + For Each (500k) | 312 | 68 | 4.6x |
| Map Set + Get (150k each) | 191 | 62 | 3.1x |
| String Concat in a loop (300k) | 170 | 116 | 1.5x |
| Empty For loop (3M) | 220 | 45 | 4.9x |
| Array Get Item / Set Item (500k) | 469 | 74 | 6.3x |
| Reflected object: method call + property read (200k) | 141 | 44 | 3.2x |
| C++ -> script function call, 200k `callFunction` | 369 | 163 | 2.3x |

With a Debugger attached (how the editor runs a script, `--debugger`): 5743 ms -> 1386 ms (4.1x).

What changed (all in Core, no API or file format change):
- Intrinsics (`NodeDef::intrinsic`, copied to `CompiledNode::fast`): math, comparisons, logic, Branch, For / ForEach / While, Get Variable and the
  array / map nodes (get, set, add, length, contains, map get/set) run inline in `Task::evalNode` / `Task::execFast` - no NodeCtx, no std::function,
  no copies of the inputs. They mirror the `run` functions in Builtins.cpp exactly (results, error messages, evaluation order); when an input
  cannot be read in place (type conversion, container default literal) the node falls back to `run`.
- `Task::peek` reads an input in place. The compiler classifies every input (`DataSource::Mode`: constant, slot, pure slot, parameter, global /
  local / parameter variable). Without a debugger a Get Variable node feeding an intrinsic is not evaluated at all - the variable is read directly;
  with DebugHooks installed the node is evaluated as before, so pins still show their values.
- Loops keep their continuation on `Frame::conts` and update it in place (no pop / push per iteration); `Frame::resuming` means "the top
  continuation belongs to `pc`". Generic nodes still take it off the stack (`execGeneric`).
- Frames are recycled per Instance (`Instance::spareFrames_`): slot values, stamps (the epoch keeps counting, so stale stamps never match), parameter
  and local cells are reused; arguments are evaluated straight into the callee's cells. Task + control block are one allocation.
- Smaller things: `Value` assignment / `setInt` inlined, `==` and `ValueLess` have int fast paths, `const std::string&` parameters of reflected
  functions use the script's string in place (`StringRefArg`), `registeredTypeOf<T>` caches the C++ type lookup (it took a mutex and hashed a
  type_info on every call that passes an object), 32 bit division when both operands are small and non-negative, cold throw paths moved out of the hot
  functions (std::string temporaries there gave them stack cookies), `Debugger::shouldBreak` returns at once when nothing is armed.

Rules to keep it correct:
- An intrinsic must stay identical to the `run` of its node; a NodeDef copied from a built-in one keeps the intrinsic (reset it when replacing `run`).
- A pointer from `peek` is only valid until the node that asked for it is done. Pure nodes never change variables or containers, and exec nodes
  read all their inputs before they change anything - keep it that way.
- Cells of a popped frame are cleared (`popFrame`), shared ones are dropped; do not keep `Cell`s of a frame beyond its life.
- Tests: `fast_paths_*` in Tests/Main.cpp pin the behaviour (they also pass on the pre-optimization Core). A differential run of many scripts
  against the old Core (results, errors, log, debugger pins and link events) gave identical output, and the suite is clean under AddressSanitizer.
- Plugins and hosts must be rebuilt with these headers (Registry, NodeCtx, Value and Frame layouts changed).
- Ideas left: pooling Task objects for host calls (~9 allocations per `callFunction`), compiling pure expression trees to flat code, a call path
  for reflected functions without NodeCtx, a smaller Value (16 bytes) with an intrusive reference count.

## Debugger: big containers and watches
- Core (Inspect.hpp): `ValueView` now has `path` (an expression that addresses the value), `total` and `offset`; `inspect` still collects only
  `InspectOptions::maxItems` (64) children. `evaluateExpression(inst, task, frame, "Name[500000..500099]")` evaluates a watch expression:
  `Name`, `.Property`, `[3]`, `["key"]` / `[key]`, `[a..b]` (inclusive, open ends allowed, clamped, at most `maxRange` = 50000 children; arrays by
  index, maps and objects by position). Frame locals / parameters are found before globals. Nothing is modified.
- Editor: containers with unloaded children get a "N more - double-click to load" row (256 per double-click; the context menu loads 1,000 /
  10,000 / up to 50,000, watches a range, adds to watch, copies the value). Opening a container below the collected depth loads its first 64.
  The new Watch area under the variables takes expressions (Enter adds, Delete removes, context menu edits); a new range watch opens at once.
  Expansion, loaded elements and the watch list survive every refresh (each debugger step rebuilds the trees); watches are stored in the
  user's settings per script path (QSettings "FlowGraph" / "FlowGraph Studio"). RunController::evaluate runs the expressions on the worker.
- Tests: inspect_* / evaluate_expression_* (Tests/Main.cpp), debugger_panel_pages_big_arrays_and_evaluates_watches (Tests/EditorTests.cpp).
- Details panel, function Inputs / Outputs tables: no header and no widgets in the cells. The column names are in the tooltip of every cell
  ("Type: int ..."); Ref / Const are centered check marks; a ParamDelegate creates the editors only on a double click or F2 (types: editable
  combo with the registry's types, container: a list that opens at once, names: line edit); the Key cell is editable only for maps.
- Pin connectors (NodeItem, drawPinGlyph in Style.cpp): single value = circle; array = `[ o o ]` (bracket frame with two items in a row in the
  element color, its connector is 15 px wide); map = 2 x 2 matrix, left column in the key type color, right column in the value type color;
  by reference: the circles become diamonds (the small diamond on top of reference containers is gone); const keeps the padlock. Items are
  filled when connected, hollow otherwise. Test: pin_connectors_show_the_container_kind (renders the glyphs and samples pixels).
- Built-in nodes and containers (PinDef::access = PinAccess::Read / Modify / Alias, helpers reads() / modifies() / aliases(); the compiler turns
  it into CompiledPin::byRef / isConst only when the pin's resolved type is an array / map): nodes that change a container (Add, Insert, Set
  Item, Remove At, Clear, Set Entry, Remove Entry, Clear) take it by reference; nodes that only read it (Get Item, Length, Contains, Find, Get
  Entry, Contains Key, Keys, Values, For Each, Select and Reroute inputs, comparisons) take a const reference; outputs that hand out a container
  that exists elsewhere (Get Variable, Get Item, Get Entry, For Each element, Select result, Reroute) are references. Visual only: the compiler
  and interpreter use byRef / isConst of a pin for function calls only. Test: builtin_nodes_show_how_they_use_containers.
- Snap to grid (GraphScene::setSnapToGrid / setSnapStep / snapped; View > Snap to Grid and View > Grid Snap Size 10 / 20 / 40, stored in the
  user's settings "editor/snapToGrid", "editor/snapStep"; default on, 20 = the fine grid lines): nodes (reroutes too) snap by their top left
  corner while the user drags them (NodeItem::itemChange, only while the left button is down and not during sync), new nodes snap where they
  are placed, Alt while dragging moves freely. Comments are not snapped. Tests: snap_to_grid_places_and_moves_nodes_on_the_grid,
  view_menu_snap_to_grid_switches_every_graph.

## Robustness, tooling and language round (2026-10-18)
- Files: `.flow` / `.flowlaunch` files of a newer `version` are refused (Script::kFileVersion; migrations go in Script::fromJson).
- Plugins: FG_PLUGIN also exports `fgPluginAbi` (PluginAbi: ABI version FG_PLUGIN_ABI_VERSION, compiler family, Debug/Release flavour,
  hash of shared class sizes); Runtime::loadPlugin refuses a mismatch or a plugin without it, with the reason. A library that failed to
  load is no longer remembered as loaded. Test plugins: Tests/AbiPlugin.cpp -> bin/testplugins.
- Integers: + - * negate abs wrap around through addInt / subInt / mulInt / negInt (Value.hpp) instead of signed overflow (UB).
- Build: CMakePresets.json (windows-*, linux-*, core-only; QT_DIR), build.bat finds VS with vswhere, CI in .github/workflows/ci.yml
  (MSVC, GCC, core under ASan/UBSan). `.gitattributes` keeps .bat files CRLF. vcvars64 itself prints "'vswhere.exe' is not recognized": harmless.
- Nodes: Switch on Int / String / Enum, Do Once, Gate, Flip Flop, For Each Entry, Format (custom inputs), String Trim / Starts With /
  Ends With / Find / Is Empty, Array Sort / Reverse / Slice / Remove Item / Is Empty, Math Pi / Asin / Acos / Atan / Atan2 / Exp / Log.
  VM: NodeCtx::entryPin (Frame::entryPin, set by follow), NodeCtx::nodeState (per instance, cleared by resetVariables),
  NodeDef::settings (Count / Lines) edited generically in Details (Lines apply on focus out).
- Find: Core Search.hpp (findReferences, referenceTargetOf, searchScript); editor Find Results dock (tab of Output Log), Edit > Find in
  Script (Ctrl+F), Find References (Shift+F12, node and My Script context menus). A saved dock layout records its docks
  ("editor/layoutDocks"); a dock newer than the layout is added where it belongs.
- Async: a host function returning std::future / std::shared_future is latent (FunctionDesc::latent / start, PendingCall); its node
  uses NodeCtx::waitThenResume (wait, then re-run with Continuation::native kept) and is never pure.
- Hot reload: Instance / Executor / AsyncExecutor::reload(program) -> ReloadReport; variables kept by name (converted / reset when the
  type changed), node state kept by graph name + node id, waiting / paused tasks aborted (Task::program_ keeps their program alive).
- Speed: interleaved best-of runs vs the previous commit are within this machine's noise (total +1..2% plain, +4..7% with a debugger,
  no benchmark consistently slower).

## Second round (2026-10-18): random, limits, collapse, recent files, reference, exports, install
- Random Int / Float are exec nodes (one number per run, read by every connected pin); the generator is per instance
  (Instance::random, Math.Set Random Seed). New compiler warning: an exec node whose outputs are used but that is not in the execution flow.
- Limits (Instance::Limits: maxContainerSize, maxStringLength, maxTaskSeconds; checked in NodeCtx::out, Array Add / Insert, Map Set - run
  and intrinsics alike - and every 1024 nodes / at the start of each run() for the time) and CompileOptions::allowNode.
- Collapse to Function (Core Refactor.hpp collapseToFunction; Edit menu / node context menu): one exec entry, one exec exit, data in ->
  inputs, data out -> outputs; a Get of a script variable that feeds the selection is COPIED in (an input would be a snapshot and change
  what the script does when the selection sets the variable); refusals leave the script unchanged; the result must compile.
- File > Open Recent (QSettings "editor/recentFiles/<application>", 10 entries). The menu is rebuilt with deleteLater, not clear(): it
  is rebuilt while one of its actions is triggered.
- Node reference: Core Reference.hpp nodeReferenceMarkdown; `flow --reference [file]`, `flow --no-plugins`; Docs/NodeReference.md
  (target node_reference; CI checks it is current).
- Exports: no CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS; generated FlowGraph/Export.hpp and FlowGraphEditor/Export.hpp (FG_CORE_EXPORT,
  FG_EDITOR_EXPORT), hidden visibility on GCC / Clang. Core 7753 -> 527 exported symbols, editor 7311 -> 59. Rules: mark classes with
  out-of-line members, exception classes and polymorphic bases; nested classes and out-of-line friends need their own mark; an exported
  class must not have implicitly copyable members that cannot be copied (Task deletes its copy). The editor tests compile the editor
  sources with FG_EDITOR_STATIC_DEFINE.
- Install: `cmake --install <build> --prefix <dir> --component FlowGraph` (KDDockWidgets' own rules are in another component);
  FlowGraphConfig.cmake (FlowGraph::Core, FlowGraph::ScriptEditor), Tests/Consumer is built against an install in CI.
- Settings per application: the dock layout, its list of known docks and the recent files live in the QSettings group
  apps/<application name>/ (ScriptEditorWindow::appKey), so flowstudio and an embedding application no longer overwrite each other's
  layout. Earlier keys move over once (migrateAppSettings): recent files to their own application, the shared editor/layout to the
  standalone editor. The embedded example's --selftest uses a temporary settings location, like the editor tests.
- Nodes (2026-10-18, third round): For Each / For Each Entry take Descending (read when the loop starts: For Each keeps it in the
  continuation's aux, mirrored by Intrinsic::ForEach; For Each Entry in state()). Print Error (util.error). Time category
  (Core/Source/BuiltinsTime.cpp): Now, Seconds Since Start (Instance::secondsSinceStart), Break / Make / Format / Parse Date Time and the
  Weekday enum; dates are computed with civil-from-days arithmetic, not strftime / mktime (MSVC's strftime ends the process on an
  unknown % code, mktime fails before 1970 on Windows); the local offset comes from localtime_s / localtime_r.
- Fourth round: Now / Seconds Since Start stay exec nodes (made pure and reverted on request). Enum literals: NodeSetting::Kind::Choice ("value"), nodeTitle "Weekday.Friday",
  Switch on Enum titled "Switch on <Enum>". Details check boxes are painted by the delegate (the style's box vanished on rows of the same
  colour). Make Date Time carries over, years limited to +-1000000 (float arithmetic, no int overflow). A node that waits (Delay)
  follows its exec link when the wait ends (Task::waitingNext_ / pendingNextPin_, Task::run), so the debugger's link animation and
  currentNode() are right. Loops: isPlainExecNode / closesEndlessExecLoop / closesPureDataCycle (Compiler.hpp), NodeDef::waits; the
  editor refuses both kinds of links, the compiler warns about endless exec loops (one walk per plain node, O(n)).
- Themes (2026-10-19): fged::Theme Dark / Light / System (ScriptEditor.hpp: applyTheme, applySavedTheme, savedTheme, themedIcon;
  EditorHost::offersThemeChoice, default Standalone). Editor/Source/Common/Theme.*: both palettes (Dark = the old values exactly),
  ThemeColors (error / warning / success, canvas, grid, node body, padlock), ThemeNotifier::changed. Icons go through a QIconEngine
  that renders the SVG for the theme in use (light: the neutral greys #e3e9f2 / #dfe8f7 / #f2f5fa / #9fb0c9 become darker, colours and
  white on coloured shapes stay); its pixmaps are cached per theme generation. On a switch: scenes sync (pin / link colours are taken
  at sync), Details / Compile Results rebuild, the log recolours its warning / error blocks (QTextBlock::userState = level), My Script
  rebuilds its add icon. Saved in QSettings apps/<application>/theme; the window applies it before its widgets exist. System follows
  QStyleHints::colorSchemeChanged.
