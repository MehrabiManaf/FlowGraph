# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
# The editor of FlowGraph on GLWidgets: FlowGraph Studio's window, its graphs, panels and debugger, drawn by GLWidgets (no
# Qt). The document, its history and running it are EditorCore's, shared with the Qt editor (Editor/).
file(GLOB_RECURSE FGGLED_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.hpp Include/*.hpp)
add_library(FlowGraphGLEditor STATIC ${FGGLED_SOURCES})
target_include_directories(FlowGraphGLEditor
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(FlowGraphGLEditor PUBLIC FlowGraphEditorCore GLWidgets::GLWidgets)

# Its translations, compiled into it: Translations/<code>.json (the texts of the editor and of GLWidgets), and the texts of
# the built-in nodes, Core/Translations/<code>.json (Source/Language.cpp). After texts of the sources changed:
#   python ThirdParty/GLWidgets/Tools/UpdateCatalog.py --language <code> --catalog GLEditor/Translations/<code>.json #          GLEditor/Source ThirdParty/GLWidgets/Library/Source ThirdParty/GLWidgets/Library/Include
file(GLOB FGGLED_CATALOGS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Translations/*.json)
file(GLOB FGGLED_NODE_CATALOGS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Core/Translations/*.json)
set(FGGLED_CATALOG_LIST "")
foreach(catalog ${FGGLED_CATALOGS})
    list(APPEND FGGLED_CATALOG_LIST "editor=${catalog}")
endforeach()
foreach(catalog ${FGGLED_NODE_CATALOGS})
    list(APPEND FGGLED_CATALOG_LIST "nodes=${catalog}")
endforeach()
# the icons of its toolbar and menus: the Qt editor's (Editor/Resources/Icons), compiled in the same way (Source/Icons.cpp)
set(FGGLED_ICONS new open save save-as exit undo redo cut copy paste duplicate delete select-all comment edit-comment
    continue interrupt stop step-over step-into step-out configure breakpoint-add find find-references function rename
    open-function goto add reroute break-links variable-get variable-set watch-add watch-range watch-edit watch-remove
    watch-clear load-more clear recent frame-selection frame-all full-screen snap grid colors style language speed about
    move-up move-down variable local-variable struct dock-float dock-dock dock-close tabbar-hide)
set(FGGLED_ICON_FILES "")
foreach(icon ${FGGLED_ICONS})
    list(APPEND FGGLED_CATALOG_LIST "icon=${CMAKE_SOURCE_DIR}/Editor/Resources/Icons/${icon}.svg")
    list(APPEND FGGLED_ICON_FILES ${CMAKE_SOURCE_DIR}/Editor/Resources/Icons/${icon}.svg)
endforeach()
list(JOIN FGGLED_CATALOG_LIST "|" FGGLED_CATALOG_ARG)        # (a list through a command line)
set(FGGLED_EMBEDDED ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedCatalogs.cpp)
add_custom_command(OUTPUT ${FGGLED_EMBEDDED}
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${FGGLED_EMBEDDED} "-DCATALOGS=${FGGLED_CATALOG_ARG}" -P ${CMAKE_CURRENT_SOURCE_DIR}/EmbedCatalogs.cmake
    DEPENDS ${FGGLED_CATALOGS} ${FGGLED_NODE_CATALOGS} ${FGGLED_ICON_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/EmbedCatalogs.cmake
    COMMENT "Compiling the translations into the GLWidgets editor"
    VERBATIM)
target_sources(FlowGraphGLEditor PRIVATE ${FGGLED_EMBEDDED})

# Its graph edited with the mouse and the keys, in a root without a window (no GPU).
if(FG_BUILD_TESTS)
    add_executable(FlowGraphGLEditorTests Tests/GraphViewTests.cpp)
    target_link_libraries(FlowGraphGLEditorTests PRIVATE FlowGraphGLEditor)
    target_include_directories(FlowGraphGLEditorTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    add_test(NAME FlowGraphGLEditorTests COMMAND FlowGraphGLEditorTests)
endif()
