# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
# FlowGraph Studio on GLWidgets, while it grows: flowstudio-next. It becomes flowstudio when it does all the Qt one does.
add_executable(flowstudio-next main.cpp)
target_link_libraries(flowstudio-next PRIVATE FlowGraphGLEditor)
if(WIN32)
    # a window, no console; main() still the entry point
    set_target_properties(flowstudio-next PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(flowstudio-next PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
