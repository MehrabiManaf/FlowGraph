# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
# flowstudio: FlowGraph Studio, the editor and debugger of the standard applications, on GLWidgets (no Qt).
add_executable(flowstudio main.cpp)
target_link_libraries(flowstudio PRIVATE FlowGraphGLEditor)
target_compile_definitions(flowstudio PRIVATE FG_VERSION="${PROJECT_VERSION}")     # flowstudio --version
if(WIN32)
    # a window, no console; main() still the entry point
    set_target_properties(flowstudio PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(flowstudio PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
