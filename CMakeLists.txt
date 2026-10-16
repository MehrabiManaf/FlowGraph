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
    add_subdirectory(ThirdParty/KDDockWidgets EXCLUDE_FROM_ALL)
    add_subdirectory(Editor)
    add_subdirectory(Apps/Studio)
endif()
