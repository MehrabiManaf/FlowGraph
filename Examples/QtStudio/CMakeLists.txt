# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
# flowstudio-qt: FlowGraph Studio on the Qt editor (FlowGraph::ScriptEditor), an example of a host of the Qt editor: the
# editor of the standard applications, as flowstudio is (see README.md). Built with the Qt editor; not installed.
set(CMAKE_AUTOMOC ON)
qt_add_executable(flowstudio-qt WIN32 main.cpp StandardEditorHost.cpp StandardEditorHost.hpp)
target_link_libraries(flowstudio-qt PRIVATE FlowGraph::ScriptEditor)
