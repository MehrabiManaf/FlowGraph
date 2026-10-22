# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: GPL-3.0-only
# The embedded example: a Qt application that embeds the FlowGraph runtime and the script editor (see README.md).
# Needs the editor, so it is only built when the editor is.
if(NOT TARGET FlowGraphEditor)
    return()
endif()

set(CMAKE_AUTOMOC ON)
qt_add_executable(embedded_example
    Source/main.cpp
    Source/MainWindow.cpp Source/MainWindow.hpp
    Source/MessageScripting.cpp Source/MessageScripting.hpp
    Source/MessageStore.cpp Source/MessageStore.hpp
    Source/ScriptEditorHub.cpp Source/ScriptEditorHub.hpp
    Source/SelfTest.cpp Source/SelfTest.hpp
    Source/TableDock.cpp Source/TableDock.hpp)
target_link_libraries(embedded_example PRIVATE FlowGraph::ScriptEditor)

# Its translations, compiled in (see Source/main.cpp): Translations/embedded_example_<code>.ts - the texts of its windows, like
# the editor's (cmake --build <build> --target embedded_example_lupdate adds new texts of the sources) - and
# Translations/messages_<code>.json - the texts of its nodes (embedded_example --translations <file> writes and updates one).
file(GLOB EMBEDDED_TS_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Translations/embedded_example_*.ts)
qt_add_lupdate(TS_FILES ${EMBEDDED_TS_FILES} SOURCE_TARGETS embedded_example
    LUPDATE_TARGET embedded_example_lupdate NO_GLOBAL_TARGET OPTIONS -no-obsolete -locations none)
qt_add_lrelease(TS_FILES ${EMBEDDED_TS_FILES} QM_FILES_OUTPUT_VARIABLE EMBEDDED_QM_FILES LRELEASE_TARGET embedded_example_lrelease NO_GLOBAL_TARGET)
qt_add_resources(embedded_example embedded_example_translations PREFIX /i18n BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${EMBEDDED_QM_FILES})
file(GLOB EMBEDDED_NODE_CATALOGS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Translations/messages_*.json)
if(EMBEDDED_NODE_CATALOGS)
    qt_add_resources(embedded_example embedded_example_node_translations PREFIX /i18n BASE ${CMAKE_CURRENT_SOURCE_DIR}/Translations
        FILES ${EMBEDDED_NODE_CATALOGS})
endif()
if(FG_BUILD_TESTS)
    add_test(NAME EmbeddedExample COMMAND embedded_example --selftest)
    set_tests_properties(EmbeddedExample PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
