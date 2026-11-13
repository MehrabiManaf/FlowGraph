# flowstudio-qt: FlowGraph Studio on the Qt editor

`flowstudio` (Apps/Studio) is built on GLWidgets. This example is the same studio built on the **Qt editor library**,
`FlowGraph::ScriptEditor` (Editor/): a Qt application whose whole window is `fged::ScriptEditor`, the editor and debugger of
the *standard applications* (the scripts `flow` runs, with their only event `Main(Args: array<string>)`).

```
build\Release\bin\flowstudio-qt.exe [script.flow]
```

It shows what a host of the Qt editor provides: `StandardEditorHost` is its `fged::EditorHost`, which says what *Run* starts
(the launch configurations stored next to the script as `script.flowlaunch`) and loads the plugins as `flow` does
(`FLOW_PLUGIN_PATH` and the `plugins` folder next to the program). For an application that embeds the editor next to its own
windows, see [Embedded](../Embedded).

It is built with the Qt editor (`FG_BUILD_EDITOR`, on when Qt 6 is found) and is not installed or packaged. Like the Qt editor
it is GPL-3.0-only (see [LICENSE](LICENSE)): the editor is built on KDDockWidgets.

Developer aids (environment variables): `FG_STUDIO_SCREENSHOT=file.png` (a screenshot, then quit; `FG_STUDIO_FRAME=1` frames
the graph first, `FG_STUDIO_SCENESHOT=file.png` also saves the whole graph), `FG_STUDIO_SIZE=1480x800`, `FG_STUDIO_RUN=Main`,
`FG_STUDIO_BREAK=Graph:id`, `FG_STUDIO_SELECT=node:Graph:id|var:name|graph:name`, `FG_STUDIO_PROBE=prefix`,
`FG_STUDIO_LAYOUT=file`, `FG_STUDIO_RECORD=folder` (a picture of the window every `FG_STUDIO_RECORD_MS` ms,
`FG_STUDIO_RECORD_COUNT` of them, then quit).
