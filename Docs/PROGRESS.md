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

## Done (headers written, NOT yet compiled)
Core/Include/FlowGraph: Json, Value, Types, NodeDef, Script, TypeExpr, Registry, Bind, Program, Vm (.hpp)
Core/Source: Json.cpp, TypeExpr.cpp (check includes <cstdio>, <cctype>)

## TODO (in order)
1. Core/Source: Value.cpp, Types.cpp (castPtr, isA...), Registry.cpp (types, convert, canConnect, node generation,
   events), Script.cpp (model + JSON io), Compiler.cpp (+Compiler.hpp: compile(), buildNodePins()), Vm.cpp,
   Builtins.cpp (flow, math generics, string, containers, variables, print, delay), Runtime.hpp/.cpp (facade, plugins),
   Inspect, Intellisense (palette query), Plugin.hpp.
2. Core/CMakeLists.txt, Tests/ (mini test runner), Examples/, Plugins/ExamplePlugin.
3. Editor (Qt): port FlowGraph visuals (FunctionCallView, LinkView, ScriptableScene) onto the Program model;
   panels: MyScript (variables/functions/events), Details, palette menu, debug toolbar, log, diagnostics;
   undo/redo (snapshot), copy/paste, comment boxes, reroute. Apps/Studio main with the dark palette from FGTest.
4. README and docs.
