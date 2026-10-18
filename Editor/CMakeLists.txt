set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

file(GLOB_RECURSE FGED_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.hpp Include/*.hpp)

qt_add_library(FlowGraphEditor ${FG_LIB_TYPE} ${FGED_SOURCES} Resources/Resources.qrc)
add_library(FlowGraph::ScriptEditor ALIAS FlowGraphEditor)
set_target_properties(FlowGraphEditor PROPERTIES
    VERSION ${PROJECT_VERSION}
    CXX_VISIBILITY_PRESET hidden            # only what FG_EDITOR_EXPORT marks (ScriptEditor.hpp) is exported
    VISIBILITY_INLINES_HIDDEN ON
    EXPORT_NAME ScriptEditor)
# FG_EDITOR_EXPORT: <build>/Editor/Include/FlowGraphEditor/Export.hpp
generate_export_header(FlowGraphEditor
    BASE_NAME FG_EDITOR
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/Include/FlowGraphEditor/Export.hpp)
if(NOT FG_SHARED)
    target_compile_definitions(FlowGraphEditor PUBLIC FG_EDITOR_STATIC_DEFINE)
endif()
target_include_directories(FlowGraphEditor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Include>
        $<INSTALL_INTERFACE:include>
    PRIVATE Source)
target_link_libraries(FlowGraphEditor PUBLIC
    FlowGraphCore
    Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
# KDDockWidgets is a static library: link it into the editor library only, so a shared FlowGraphEditor owns the one copy.
target_link_libraries(FlowGraphEditor PRIVATE $<BUILD_INTERFACE:KDAB::kddockwidgets>)

if(FG_BUILD_TESTS)
    qt_add_executable(FlowGraphEditorTests ${CMAKE_SOURCE_DIR}/Tests/EditorTests.cpp ${CMAKE_SOURCE_DIR}/Apps/Studio/StandardEditorHost.cpp)
    target_include_directories(FlowGraphEditorTests PRIVATE Source ${CMAKE_SOURCE_DIR}/Apps/Studio)
    if(FG_SHARED)
        # The tests reach into the editor's internals (only ScriptEditor.hpp is exported), so they compile the editor sources
        # themselves instead of using the DLL; for them the editor's API is not imported from it.
        target_sources(FlowGraphEditorTests PRIVATE ${FGED_SOURCES} Resources/Resources.qrc)
        target_include_directories(FlowGraphEditorTests PRIVATE Include ${CMAKE_CURRENT_BINARY_DIR}/Include)
        target_compile_definitions(FlowGraphEditorTests PRIVATE FG_EDITOR_STATIC_DEFINE)
        target_link_libraries(FlowGraphEditorTests PRIVATE FlowGraphCore Qt6::Widgets Qt6::Svg Qt6::SvgWidgets KDAB::kddockwidgets)
    else()
        target_link_libraries(FlowGraphEditorTests PRIVATE FlowGraph::ScriptEditor KDAB::kddockwidgets)
    endif()
    add_test(NAME FlowGraphEditorTests COMMAND FlowGraphEditorTests)
    set_tests_properties(FlowGraphEditorTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
