# Contributing to FlowGraph

Thank you for helping! Bug reports, ideas, translations and code are all welcome.

## Reporting a bug or asking for something

Open an [issue](https://github.com/MehrabiManaf/FlowGraph/issues/new/choose). For a bug, the most useful things are the steps
that show it and, when you can, the `.flow` script it happens with (scripts are small JSON files: attach it, or paste it).

## Translations

The editor and the nodes are translated into 9 languages. Persian is reviewed; the others are machine translations waiting for
a native speaker. A correction of a single word helps.

* **The editor's texts** (menus, panels, messages): `Editor/Translations/flowgraph_<code>.ts`. Open it with
  [Qt Linguist](https://doc.qt.io/qt-6/linguist-translators.html) (it comes with Qt), or edit the XML by hand. Texts that are
  not reviewed yet are marked *unfinished*; mark one finished when it is right.
* **The nodes' texts** (titles, pins, descriptions, categories): `Core/Translations/<code>.json`. Every line is
  `"key": ["English", "translation"]`: change the second text only.
* **A new language** is a new `.ts` file and a new `.json` file (see *Languages* in the README).

You can also open a *Translation* issue with the wrong text and the right one, and it will be changed for you.

## Code

Build and test (Windows with MSVC 2022 and Qt 6.9; Linux with GCC and Qt 6.9 - see *Build* in the README):

```
cmake --preset windows-release            # or linux-release
cmake --build --preset windows-release
ctest --preset windows-release
```

`FlowGraphEditorTests <part of a name>` runs only the editor tests whose name contains it.

* **Follow the code around you**: its naming, its comments (they say why, in full sentences), its idioms. C++20.
* **Add a test** for what you fix or add: `Tests/Main.cpp` for the runtime, `Tests/EditorTests.cpp` for the editor.
* **Texts the editor shows** go through `tr()` (the editor tests check that every text can be translated). After you add or
  change one, `cmake --build <build> --target flowgraph_lupdate` puts it into the translation files.
* **Nodes**: a new or changed node needs its texts in the node catalogs. `flow --no-plugins --translations
  Core/Translations/<code>.json` adds them (untranslated) for every language; `Docs/NodeReference.md` is regenerated with
  `cmake --build <build> --target node_reference`.
* CI builds and tests on Windows and Linux, runs the runtime under AddressSanitizer / UndefinedBehaviorSanitizer, and checks
  that the translation files are up to date and complete.

## License of contributions

Each part of FlowGraph has its license (see `LICENSE` and the SPDX line of each file): the runtime, `flow` and the plugins are
MIT, the editor and `flowstudio` are GPL-3.0-only. A contribution to a part is under the license of that part.
