# Embedded example: message tables

A Qt application that embeds FlowGraph: the runtime, and the script editor as a library.

```
build\Release\bin\embedded_example.exe
build\Release\bin\embedded_example.exe --selftest        (also run by ctest as EmbeddedExample)
```

The application receives text messages into a **message store**: here it makes them up, **a random log-like message every 500 ms (two per second)** (the
*Pause Messages* button stops the source, e.g. while a script is stopped at a breakpoint). The main window shows only how many messages have
been received, and has a dock widget for every **table** the user adds with *New Table*. The toolbar of a table loads, edits and saves the **script** (`.flow`) of that
table:

* the script handles the event **`NewMessage`** (argument `Message` with `Text` and `Line`); it is run for every message as it comes, and
  first for the messages the store already has (it keeps the latest 10 000). **A new table starts with a script that drops every message**:
  it handles the event and does nothing, no `Add Row` runs, the table stays empty. Connect nodes to the event in the editor, or load a
  sample from `Scripts/`: `list.flow` (a row per message: `Line`, `Text`) and `counter.flow` (a global variable `Count` that goes up by one per
  message: the state of the script is kept). `embedded_example --write-samples DIR` writes them again;
* the node **`Add Row`** puts a row into the table every time it runs. Its input pins are the **columns** (the name of a pin is the
  name of the column); add, remove, rename and reorder them in the Details panel of the node;
* **the script keeps its state**: a table runs ONE instance of its script for as long as the script is not replaced, so a global variable
  (a counter, a sum, the last value...) keeps its value from message to message. The run starts again, with fresh variables, over the
  messages received so far, when a table is created, a script is loaded, or the script is saved in the editor;
* the run is on a worker thread; a timer brings the rows into the table (it shows the latest 5 000).

*Load Script* and the editor's *Open* take only a script that fits this application: it must use only what the application provides (a
standalone script that handles `Main` is refused) and handle `NewMessage`. Otherwise nothing is loaded, the table and the editor keep
their script, and a message says why. *Save* in the editor likewise only replaces the script of the table with one that fits and compiles.

*Edit Script* opens the script editor on the script of that table. **There is only one script editor** (the library allows no more):
pressing *Edit Script* again, or on another table, makes the same editor edit the script of that table (the changes of the script it
edited before are asked for first).

The editor is an **embedded** one (`EditorMode::Embedded`), because the application owns the scripts and runs them: no files, no Run, no
launch configurations. *Save* in the editor does not write a file: it hands the script to the table that is being edited, which
replaces its script and runs the new one (from the start, over the messages received so far). (*Save As* writes a copy to a file; *Save Script* in the toolbar of a
table writes the table's script to its `.flow` file, *Load Script* loads one.) The *Debug* menu has *Attach* / *Detach* instead of Run: the
debugger attaches to the executor that runs the script of the table that is being edited, so breakpoints stop the script while the
messages are processed and you can step through it; saving while attached attaches to the new run.

## Where to look

| File | What it shows |
|-|-|
| `Source/MessageScripting.*` | Everything FlowGraph-specific and Qt-free: registering a class, an event and a node with custom pins; `ScriptRun`: a long-lived script on an `fg::AsyncExecutor` with one instance, fed one message at a time (the executor is what the debugger attaches to) |
| `Source/ScriptEditorHub.*` | The application's one `fged::ScriptEditor` and its embedded-mode `fged::EditorHost`: which table it edits, what Save does (`applyScript`), what Attach attaches to (`debugTarget`) |
| `Source/TableDock.*` | A table: its script, running it on a worker thread, showing the result |
| `Source/MainWindow.*`, `Source/MessageStore.*` | The application itself: the message source (random messages) and the window; it knows nothing about FlowGraph except the table docks |
| `Source/SelfTest.cpp` | Drives the application like a user would |

## What each feature uses, and whether it needed a change of FlowGraphCore

| Feature of the example | FlowGraph API | Change in Core? |
|-|-|-|
| The `Message` class (read-only `Text`, `Line`) | `Registry::registerClass`, `.readonly`, `.toString` | no |
| The event `NewMessage(Message)` | `Registry::addEvent` | no |
| The node `Add Row`: what it does when it runs | `Registry::addNode`, `NodeDef::run` | no |
| The row is added when the node runs, to the table of the run that executes it | `NodeCtx::instance().userData()` | no |
| One instance for as long as the script runs, a `NewMessage` task per message, state kept between messages | `AsyncExecutor::postEvent` on one executor (one `Instance`) | no |
| Stopping a run that is going on (the script was replaced) | `AsyncExecutor::abortAll` | no |
| Messages are handed to the script without copying | `Registry::wrap(shared_ptr)` | no |
| Is the script usable here? (missing event, missing node...) | `Runtime::loadScript`, `CompatibilityReport` | no |
| A script that does not compile is reported in the table | `Runtime::compile`, `Program::diagnostics` | no |
| The script runs on a worker thread and a debugger can attach to it | `AsyncExecutor` (`postEvent`, completion callback, `inspect`) | no |
| Columns of an empty table | `Program` / `CompiledNode::pins` | no |
| One script editor for the whole application, switched between the tables | `ScriptEditor::edit` (embedded editor, one per program) | no (editor library) |
| Save in the editor gives the script to the table, which runs it | `EditorHost::mode` = `Embedded`, `EditorHost::applyScript` | no (editor library) |
| Debugging the script while the application runs it: Attach / Detach, breakpoints, stepping | `EditorHost::debugTarget`, `ScriptEditor::attach` | no (editor library; uses the public `AsyncExecutor::inspect`, `Instance::setDebugHooks`, `Debugger`) |
| Add Row has no pins of its own; the user adds columns like the inputs of a function | `NodeDef::customInputs` (the node keeps the pins in `Node::inputs`) | **yes**: new, see below |
| Every node shows its inputs and outputs in the Details panel; the custom ones can be edited | `DetailsPanel` (editor library) | no (editor library) |

The only change of `Core/` for this example is the one marked **yes**: custom pins (27 lines, see below).

### Custom pins: the one change of FlowGraphCore

Until now the pins of a node were fixed by its definition (or by a few properties, like the output count of `Sequence`). Add Row
needs pins that the *user* chooses, with names and types, next to fixed ones. This is a new, optional kind of node:

* `NodeDef::customInputs` / `customOutputs` (two `bool`, default `false`) say that nodes of this definition accept custom data pins;
* `Node::inputs` / `Node::outputs` (`std::vector<ParamDecl>`: name, type, default, like the signature of a function) hold them;
* they are saved in the `.flow` file (`"inputs"` / `"outputs"` of the node, only when there are any), and the compiler appends them
  to the pins of the definition (`collectPinDefs`); `checkCompatibility` reports a custom pin of a type the application does not have.

Nothing else changes. At run time a custom pin is an ordinary data pin of the compiled node, so the interpreter is not touched and
**no existing node is slower or different**: definitions that do not set the flags never look at the new fields (two empty vectors
per script node, and one test of a `bool` per node when a script is compiled). `NodeCtx` reads custom pins by ordinal after the
fixed ones. Files without custom pins are byte for byte what they were.

### The Details panel (editor library)

Selecting a node now shows its **Inputs** and **Outputs**. For every node they are read only: they belong to its definition. For a
definition with custom pins, the fixed pins are shown dimmed and the custom ones below them are edited like the inputs and outputs
of a function (`+` adds a pin, double-click edits the name or the type, the right click menu removes or moves a pin). Renaming a pin
moves its links with it, removing it removes them, and every change is one undoable edit.
