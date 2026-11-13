# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: MIT
# What every editor of FlowGraph shares, without a user interface (no Qt, no GLWidgets): the document and its undo history,
# running and debugging its script. A static library, linked into each editor library (which owns its copy).
file(GLOB_RECURSE FGEDCORE_SOURCES CONFIGURE_DEPENDS Source/*.cpp Include/*.hpp)
add_library(FlowGraphEditorCore STATIC ${FGEDCORE_SOURCES})
set_target_properties(FlowGraphEditorCore PROPERTIES POSITION_INDEPENDENT_CODE ON)     # it goes into shared libraries
target_include_directories(FlowGraphEditorCore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
target_link_libraries(FlowGraphEditorCore PUBLIC FlowGraphCore)
