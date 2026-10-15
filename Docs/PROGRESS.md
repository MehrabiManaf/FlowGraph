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
