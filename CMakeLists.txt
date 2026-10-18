cmake_minimum_required(VERSION 3.21)

project(FlowGraph VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(FG_BUILD_EDITOR   "Build the Qt based editor and FlowGraph Studio" ON)
option(FG_BUILD_TESTS    "Build the unit tests"                            ON)
option(FG_BUILD_EXAMPLES "Build examples and sample plugins"               ON)
option(FG_SHARED         "Build FlowGraphCore and FlowGraphEditor as shared libraries" ON)

if(FG_SHARED)
    set(FG_LIB_TYPE SHARED)
else()
    set(FG_LIB_TYPE STATIC)
endif()
# The libraries export their public API only: FG_CORE_EXPORT / FG_EDITOR_EXPORT (generated FlowGraph/Export.hpp,
# FlowGraphEditor/Export.hpp) mark it, everything else stays inside (hidden visibility on GCC / Clang as well).
include(GenerateExportHeader)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /wd4100 /wd4127 /wd4251 /wd4275)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- core: pure C++20, no Qt ------------------------------------------------
add_subdirectory(Core)

if(FG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# ---- the standard applications ---------------------------------------------
add_subdirectory(Apps/Flow)

if(FG_BUILD_EXAMPLES)
    if(EXISTS ${CMAKE_SOURCE_DIR}/Examples/CMakeLists.txt)
        add_subdirectory(Examples)
    endif()
    if(EXISTS ${CMAKE_SOURCE_DIR}/Plugins/ExamplePlugin/CMakeLists.txt)
        add_subdirectory(Plugins/ExamplePlugin)
    endif()
endif()

# ---- editor: Qt 6 widgets + KDDockWidgets ---------------------------------
if(FG_BUILD_EDITOR AND EXISTS ${CMAKE_SOURCE_DIR}/Editor/CMakeLists.txt)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Svg SvgWidgets Test)
    qt_standard_project_setup()
    set(KDDockWidgets_FRONTENDS "qtwidgets" CACHE STRING "" FORCE)
    set(KDDockWidgets_QT6 ON CACHE BOOL "" FORCE)
    set(KDDockWidgets_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(KDDockWidgets_TESTS OFF CACHE BOOL "" FORCE)
    set(KDDockWidgets_DOCS OFF CACHE BOOL "" FORCE)
    set(KDDockWidgets_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
    set(KDDockWidgets_STATIC ON CACHE BOOL "" FORCE)
    # its own install rules (headers...) go to a component of their own: `cmake --install <build> --component FlowGraph` leaves them out
    set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME KDDockWidgets)
    add_subdirectory(ThirdParty/KDDockWidgets EXCLUDE_FROM_ALL)
    unset(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME)
    add_subdirectory(Editor)
    add_subdirectory(Apps/Studio)
    if(FG_BUILD_EXAMPLES AND EXISTS ${CMAKE_SOURCE_DIR}/Examples/Embedded/CMakeLists.txt)
        add_subdirectory(Examples/Embedded)
    endif()
endif()

# ---- install: cmake --install <build> --prefix <dir> --component FlowGraph ----
# Headers, libraries, the applications and a package configuration, so that a host project finds FlowGraph with
#   find_package(FlowGraph REQUIRED [COMPONENTS ScriptEditor])   ->   FlowGraph::Core, FlowGraph::ScriptEditor
option(FG_INSTALL "Generate the install rules" ON)
if(FG_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(FG_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/FlowGraph)
    set(FG_INSTALL_EDITOR OFF)
    if(TARGET FlowGraphEditor)
        if(FG_SHARED)
            set(FG_INSTALL_EDITOR ON)
        else()
            # a static FlowGraphEditor needs the static KDDockWidgets next to it, which is not installed
            message(STATUS "FlowGraph: the static ScriptEditor is not installed (build with FG_SHARED=ON to install it)")
        endif()
    endif()

    install(TARGETS FlowGraphCore EXPORT FlowGraphTargets COMPONENT FlowGraph
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY Core/Include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT FlowGraph)
    install(FILES ${CMAKE_BINARY_DIR}/Core/Include/FlowGraph/Export.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FlowGraph COMPONENT FlowGraph)
    install(TARGETS flow RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT FlowGraph)
    if(FG_INSTALL_EDITOR)
        install(TARGETS FlowGraphEditor EXPORT FlowGraphTargets COMPONENT FlowGraph
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(DIRECTORY Editor/Include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT FlowGraph)
        install(FILES ${CMAKE_BINARY_DIR}/Editor/Include/FlowGraphEditor/Export.hpp
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FlowGraphEditor COMPONENT FlowGraph)
        install(TARGETS flowstudio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT FlowGraph)
    endif()

    install(EXPORT FlowGraphTargets NAMESPACE FlowGraph:: DESTINATION ${FG_CMAKE_DIR} COMPONENT FlowGraph)
    configure_package_config_file(cmake/FlowGraphConfig.cmake.in ${CMAKE_BINARY_DIR}/FlowGraphConfig.cmake INSTALL_DESTINATION ${FG_CMAKE_DIR})
    # plugins must be built with the same FlowGraph: only the same major.minor version is compatible
    write_basic_package_version_file(${CMAKE_BINARY_DIR}/FlowGraphConfigVersion.cmake COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_BINARY_DIR}/FlowGraphConfig.cmake ${CMAKE_BINARY_DIR}/FlowGraphConfigVersion.cmake
            DESTINATION ${FG_CMAKE_DIR} COMPONENT FlowGraph)
endif()
