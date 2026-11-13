# Translations of FlowGraph Studio

`<code>.json`: the texts of flowstudio (the editor on GLWidgets) and of GLWidgets in one language (GLWidgets' catalog format,
Localization.hpp), compiled into the editor with the texts of the built-in nodes (`Core/Translations/<code>.json`). The editor
shows the language chosen in View > Language (System: the operating system's) at once. Only the texts change: the layout
stays left to right in every language.

After texts of the sources changed, for each language:

    python ThirdParty/GLWidgets/Tools/UpdateCatalog.py --language <code> --catalog GLEditor/Translations/<code>.json \
           GLEditor/Source ThirdParty/GLWidgets/Library/Source ThirdParty/GLWidgets/Library/Include

New texts come in untranslated (the English shows). A new language is a new file here.

Most texts came from the Qt editor's translations (`Editor/Translations/flowgraph_<code>.ts`). Persian (`fa`) is reviewed
by the author; the other languages are machine translations, to be reviewed by native speakers.
