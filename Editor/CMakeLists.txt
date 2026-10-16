set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

file(GLOB_RECURSE FGED_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.hpp Include/*.hpp)

qt_add_library(FlowGraphEditor STATIC ${FGED_SOURCES} Resources/Resources.qrc)
add_library(FlowGraph::ScriptEditor ALIAS FlowGraphEditor)
target_include_directories(FlowGraphEditor PUBLIC Include PRIVATE Source)
target_link_libraries(FlowGraphEditor PUBLIC
    FlowGraphCore
    Qt6::Widgets Qt6::Svg Qt6::SvgWidgets
    KDAB::kddockwidgets)

if(FG_BUILD_TESTS)
    qt_add_executable(FlowGraphEditorTests ${CMAKE_SOURCE_DIR}/Tests/EditorTests.cpp ${CMAKE_SOURCE_DIR}/Apps/Studio/StandardEditorHost.cpp)
    target_include_directories(FlowGraphEditorTests PRIVATE Source ${CMAKE_SOURCE_DIR}/Apps/Studio)
    target_link_libraries(FlowGraphEditorTests PRIVATE FlowGraph::ScriptEditor)
    add_test(NAME FlowGraphEditorTests COMMAND FlowGraphEditorTests)
    set_tests_properties(FlowGraphEditorTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
