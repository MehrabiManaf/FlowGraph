set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

file(GLOB_RECURSE FGED_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.hpp Include/*.hpp)

qt_add_library(FlowGraphEditor ${FG_LIB_TYPE} ${FGED_SOURCES} Resources/Resources.qrc)
add_library(FlowGraph::ScriptEditor ALIAS FlowGraphEditor)
target_include_directories(FlowGraphEditor PUBLIC Include PRIVATE Source)
target_link_libraries(FlowGraphEditor PUBLIC
    FlowGraphCore
    Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
# KDDockWidgets is a static library: link it into the editor library only, so a shared FlowGraphEditor owns the one copy.
target_link_libraries(FlowGraphEditor PRIVATE KDAB::kddockwidgets)

if(FG_BUILD_TESTS)
    qt_add_executable(FlowGraphEditorTests ${CMAKE_SOURCE_DIR}/Tests/EditorTests.cpp ${CMAKE_SOURCE_DIR}/Apps/Studio/StandardEditorHost.cpp)
    target_include_directories(FlowGraphEditorTests PRIVATE Source ${CMAKE_SOURCE_DIR}/Apps/Studio)
    if(FG_SHARED)
        # The tests reach into the editor's internals (Q_OBJECT classes of the editor are not exported individually),
        # so they compile the editor sources themselves instead of using the DLL.
        target_sources(FlowGraphEditorTests PRIVATE ${FGED_SOURCES} Resources/Resources.qrc)
        target_include_directories(FlowGraphEditorTests PRIVATE Include)
        target_link_libraries(FlowGraphEditorTests PRIVATE FlowGraphCore Qt6::Widgets Qt6::Svg Qt6::SvgWidgets KDAB::kddockwidgets)
    else()
        target_link_libraries(FlowGraphEditorTests PRIVATE FlowGraph::ScriptEditor KDAB::kddockwidgets)
    endif()
    add_test(NAME FlowGraphEditorTests COMMAND FlowGraphEditorTests)
    set_tests_properties(FlowGraphEditorTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
