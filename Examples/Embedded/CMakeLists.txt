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
if(FG_BUILD_TESTS)
    add_test(NAME EmbeddedExample COMMAND embedded_example --selftest)
    set_tests_properties(EmbeddedExample PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
