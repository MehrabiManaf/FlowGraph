# Writing scripts

How to edit, run and debug scripts in `flowstudio` and `flow`, and how the nodes behave. Every built-in node, with its pins and
defaults, is in [NodeReference.md](NodeReference.md).

## flowstudio keys

| | |
|-|-|
| Add a node | right-click the canvas, or drag from a pin and release on empty space (the list is filtered to nodes that fit) |
| Connect | drag between pins; Alt+click a pin breaks its links; double-click a data link to add a reroute |
| Variables | drag a variable from *My Script* onto the canvas (Ctrl = Get, Alt = Set, otherwise a menu) |
| Edit | Ctrl+Z / Ctrl+Y, Ctrl+C / X / V, Ctrl+D duplicate, Del delete, `C` add comment box (wraps the selection) |
| Navigate | wheel zooms, middle / right drag pans, Home frames everything |
| Refactor | *Edit > Collapse to Function* (or right-click a node) moves the selected nodes into a new function and calls it in their place: links that come in become inputs, links that go out become outputs. One undo step |
| Find | Ctrl+F finds nodes by title, comment or value in every graph; Shift+F12 (or right-click > *Find References*) lists the nodes that use the selected variable or function; double-click a result to go there |
| Run | F5 (or F6) runs the launch configuration chosen in the toolbar; the same button pauses the running script and continues a paused one; Shift+F5 stop |
| Debug | F9 toggles a breakpoint, F10 / F11 / Shift+F11 step over / into / out; hover pins while paused to see values; *Run > Execution speed* slows execution so the animated links are visible |
| View | Shift+Alt+Enter (*View > Full Screen*) switches full screen on and off; *View > Theme*: Dark, Light or System; *View > Language*. *File > Open Recent* lists the last 10 scripts |

## Launch configurations

What *Run* does in flowstudio is described by **launch configurations**, stored next to the script as `script.flowlaunch`
(*Run > Launch Configurations...* edits them):

```json
{ "format": "flowlaunch", "version": 1, "selected": "Verbose",
  "configurations": [
    { "name": "Verbose", "args": ["-v", "input.txt"], "cwd": "work", "env": { "MODE": "debug" },
      "pluginPaths": ["plugins"], "stepDelayMs": 0 } ] }
```
`args` become `Args` of `Main`; `env` and `cwd` apply while the script runs (`cwd` and `pluginPaths` are relative to the
script's folder); `stepDelayMs` is the slow motion used while debugging. `flow --launch Verbose script.flow` uses the same file.

## Running scripts with flow

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
* The script is checked before it is run: a script that needs an event or a plugin `flow` does not provide is refused.
* Exit code: the code given to the **Exit** node, `0` when the script ends normally, `1` when it fails at run time,
  `2` when it cannot be loaded or has compile errors. Ctrl+C aborts the script.
* After the script has run, `flow` prints `Execution Time: <n> ms` on **stderr**, so redirecting or piping the script's output
  is not affected.

## How the nodes behave

* **Events** belong to the application: a script handles them (`Main` in `flow` and `flowstudio`), it cannot add its own.
  Functions, variables and their types are the script's own. Function parameters can be passed by value, by reference or as
  const (checked when the script is compiled).
* **For Loop** visits `First <= Index < Last` (Last is excluded).
* **Flow nodes:** Branch, Sequence, For Loop, For Each, For Each Entry (maps, in key order), While, Break, Delay, Select,
  Switch on Int / String / any enum, Do Once, Gate (Enter / Open / Close / Toggle) and Flip Flop. Do Once, Gate and Flip Flop
  remember their state for as long as the script runs. For Each and For Each Entry have a *Descending* input: last element
  (greatest key) first.
* **Delay** waits at the node: the next node runs when the time is up. A loop through a Delay (repeat every second) or a Branch
  (a way out) is fine; the editor refuses a link that would make a value depend on itself, or an execution loop with no way out.
* **Generic nodes** (`T` in Add / Select / For Each / array and map nodes) take their type from what is connected to them.
* **Print String, Print Warning and Print Error** write to the log at their level (the Output Log colours them; `flow` writes
  warnings and errors to stderr). Print Error does not stop the script.
* **Strings** are UTF-8 and there is no char type: a character is a string of length 1, in every language. Length, Substring,
  Find and Character At count characters, not bytes (`Length("سلام")` is 4); Byte Length gives the size in bytes. For Each
  Character goes through a text once: faster than Character At in a loop. **Format** (`"{Name} is {Age}"`) gets an input per
  placeholder in *Details > Inputs*; `{{` and `}}` are literal braces.
* **Bytes** are data, not text: the content of a file, a BLOB, a network message. A `bytes` value is one shared buffer, cheap to
  pass along whatever its size; a `byte` is a number from 0 to 255 that works where an int does (a value out of range wraps around:
  300 becomes 44). *From Text* / *To Text* convert between text and bytes (UTF-8; *To Text* says in *Valid* whether the bytes were
  UTF-8), *To Base64* / *From Base64* and *To Hex* / *From Hex* write and read them as text, *To Array* / *Make Bytes* turn them
  into an `array<byte>` and back for the array nodes. Print String shows a preview (`bytes(5): 48 65 6C 6C 6F`); a bytes default
  is typed as hex.
* **Structs** are record types a script declares (*My Script > Structs*, then the fields in *Details*: name, type, default,
  description). *Make Point* builds one from its fields, *Break Point* gives its fields, *Set Point.X* changes one field in place.
  A struct is a value, like an array: setting a variable or passing it to a function by value makes a copy, while Set Field
  changes the struct it is connected to (a variable, an element of an array...). Fields can be of any type, other structs too;
  a struct holds itself only through an array or a map (`Children: array<Tree>`). Variables, function parameters and containers
  can be of a struct type; the application's events and functions cannot (they do not know the script's structs). Renaming a
  struct or a field updates every type and node that uses it; a running script that is changed keeps its values field by field.
* **Time:** a timestamp is seconds since 1970-01-01 00:00 UTC, as a float. *Now* and *Seconds Since Start* read the clock once
  per run. *Break Date Time* / *Make Date Time* convert to and from year, month, day... in local time or UTC (out of range values
  carry over: month 13 is January); *Format Date Time* writes `%Y-%m-%d %H:%M:%S` style text, *Parse Date Time* reads ISO 8601
  (`2026-10-18T14:30:05.250+02:00`).
* **Random Int / Random Float** draw one number per run that every connected pin reads; *Set Random Seed* makes a run repeatable.
* **An enum literal** (*Weekday.Friday* in the palette) shows its value in its title; *Details > Value* chooses another one.
* **System nodes** (in `flow` and `flowstudio`, and in applications that add them): *Run Command* runs a program to its end
  (Exit Code, Output, Error Output, an optional Timeout); *For Each Output Line* runs one and its loop body for each line as it
  comes (Break ends the program); *Operating System* says Windows / Linux / macOS / Other. *Use Shell* runs a command line with
  cmd.exe or /bin/sh. The script waits, the application does not; Stop, a Break or the end of the script kill the program and
  everything it started.

## The FileSystem plugin

It comes with `flow` and `flowstudio` (category *File System*):

* **File:** Open File (Read, Write or Append) gives a `File` to read a line at a time (Read Line, Read Text, Is At End) or to
  write a piece at a time (Write Line, Write Text); each write is in the file at once. Close it when it is done. Files that are
  not text (images, archives, binary formats) are read with Read Bytes (Count bytes, or all the rest) and written with Write
  Bytes, as `bytes`; a text read at the start of a file skips its UTF-8 byte order mark, a bytes read does not.
* **Files and Folders:** Copy, Move, Delete, Create Folder, List Folder, For Each File (patterns like `*.txt;*.md`, Recursive).
* **Paths** and **Information** (pure): Join Path, Parent Folder, File Name, Extension, Change Extension, Absolute Path,
  Current / Temp / Home Folder; Exists, Is File, Is Folder, File Size, Last Modified.

The nodes that work on the disk go on with **Then**, or with **Failed** and the reason in **Error** when it cannot be done (a
missing file, writing a file opened for Read, a closed file, a full disk): that is not a script error. Big work (a copy, a big
read or write) runs in the background: the script waits, the application does not, and Stop stops the work. A file that is too
big to read at once goes to Failed; read it a line at a time with Read Line. Paths are UTF-8, relative to the working directory
(the `cwd` of a launch configuration); files are read and written as UTF-8 text.
