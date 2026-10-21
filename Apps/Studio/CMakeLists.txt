# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
set(CMAKE_AUTOMOC ON)
qt_add_executable(flowstudio WIN32 main.cpp StandardEditorHost.cpp StandardEditorHost.hpp)
target_link_libraries(flowstudio PRIVATE FlowGraph::ScriptEditor)
