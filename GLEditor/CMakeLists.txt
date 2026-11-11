# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
# The editor of FlowGraph on GLWidgets: FlowGraph Studio's window, its graphs, panels and debugger, drawn by GLWidgets (no
# Qt). The document, its history and running it are EditorCore's, shared with the Qt editor it is to take the place of.
file(GLOB_RECURSE FGGLED_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.hpp Include/*.hpp)
add_library(FlowGraphGLEditor STATIC ${FGGLED_SOURCES})
target_include_directories(FlowGraphGLEditor
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(FlowGraphGLEditor PUBLIC FlowGraphEditorCore GLWidgets::GLWidgets)

# Its graph edited with the mouse and the keys, in a root without a window (no GPU).
if(FG_BUILD_TESTS)
    add_executable(FlowGraphGLEditorTests Tests/GraphViewTests.cpp)
    target_link_libraries(FlowGraphGLEditorTests PRIVATE FlowGraphGLEditor)
    target_include_directories(FlowGraphGLEditorTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    add_test(NAME FlowGraphGLEditorTests COMMAND FlowGraphGLEditorTests)
endif()
