# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)

project(FlowGraph VERSION 0.6.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The Qt editor (the ScriptEditor library, its examples and tests): ON, OFF, or AUTO - built when Qt 6 is found
set(FG_BUILD_EDITOR AUTO CACHE STRING "Build the Qt editor library (ScriptEditor) and its examples: ON, OFF or AUTO (when Qt 6 is found)")
set_property(CACHE FG_BUILD_EDITOR PROPERTY STRINGS AUTO ON OFF)
option(FG_BUILD_GLEDITOR "Build FlowGraph Studio (flowstudio) on GLWidgets (CMake 3.24 or newer)" ON)
option(FG_BUILD_TESTS    "Build the unit tests"                            ON)
option(FG_BUILD_EXAMPLES "Build the examples"                              ON)
option(FG_SHARED         "Build FlowGraphCore and FlowGraphEditor as shared libraries" ON)
option(FG_BUILD_PLUGINS  "Build the standard plugins (FileSystem)"         ON)
option(FG_INSTALL        "Generate the install rules"                      ON)

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
    # GLWidgets' Props and menu items are made with designated initializers that leave out what keeps its default
    # ({.text = ...}): GCC warns of every member left out, even with a default of its own (as GLWidgets' own build says)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-missing-field-initializers)
    endif()
endif()

# ---- core: pure C++20, no Qt ------------------------------------------------
add_subdirectory(Core)
# the editors' shared part, without a user interface (Qt-free: built in every configuration, tested with Core)
add_subdirectory(EditorCore)

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
endif()

# ---- the standard plugins: in bin/plugins, loaded by flow and flowstudio, and in the release packages ----
if(FG_BUILD_PLUGINS)
    add_subdirectory(Plugins/FileSystem)
endif()

# ---- FlowGraph Studio: the editor on GLWidgets (its own widgets and GPU drawing on SDL3), no Qt ----------------------
if(FG_BUILD_GLEDITOR)
    if(CMAKE_VERSION VERSION_LESS 3.24)
        message(WARNING "FlowGraph: FlowGraph Studio needs CMake 3.24 or newer (this is ${CMAKE_VERSION}): not built")
    elseif(NOT EXISTS ${CMAKE_SOURCE_DIR}/ThirdParty/GLWidgets/CMakeLists.txt)
        message(WARNING "FlowGraph: ThirdParty/GLWidgets is missing (git submodule update --init ThirdParty/GLWidgets): "
                        "FlowGraph Studio is not built")
    else()
        # linked into the editor: no library of its own to ship; none of its examples, tests or benchmarks
        set(GLW_SHARED OFF CACHE BOOL "" FORCE)
        set(GLW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLW_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
        add_subdirectory(ThirdParty/GLWidgets EXCLUDE_FROM_ALL)
        add_subdirectory(GLEditor)
        add_subdirectory(Apps/Studio)
    endif()
endif()

# ---- the Qt editor: Qt 6 widgets + KDDockWidgets, the ScriptEditor library for Qt applications ----------------------
set(FG_QT_EDITOR OFF)
if(EXISTS ${CMAKE_SOURCE_DIR}/Editor/CMakeLists.txt)
    if(FG_BUILD_EDITOR STREQUAL "AUTO")
        find_package(Qt6 QUIET COMPONENTS Widgets Svg SvgWidgets Test LinguistTools)
        if(Qt6_FOUND)
            set(FG_QT_EDITOR ON)
        else()
            message(STATUS "FlowGraph: Qt 6 not found: the Qt editor (ScriptEditor) and its examples are not built "
                           "(FG_BUILD_EDITOR=ON requires them, CMAKE_PREFIX_PATH or QT_DIR says where Qt is)")
        endif()
    elseif(FG_BUILD_EDITOR)
        find_package(Qt6 REQUIRED COMPONENTS Widgets Svg SvgWidgets Test LinguistTools)
        set(FG_QT_EDITOR ON)
    endif()
endif()
if(FG_QT_EDITOR)
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
    # its examples: FlowGraph Studio on the Qt editor (flowstudio-qt), and a Qt application that embeds it
    if(FG_BUILD_EXAMPLES)
        add_subdirectory(Examples/QtStudio)
        if(EXISTS ${CMAKE_SOURCE_DIR}/Examples/Embedded/CMakeLists.txt)
            add_subdirectory(Examples/Embedded)
        endif()
    endif()
endif()

# ---- install: cmake --install <build> --prefix <dir> --component FlowGraph ----
# Headers, libraries, the applications and a package configuration, so that a host project finds FlowGraph with
#   find_package(FlowGraph REQUIRED [COMPONENTS ScriptEditor])   ->   FlowGraph::Core, FlowGraph::ScriptEditor
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
    # the MSVC runtime next to the programs: they run where the Visual C++ Redistributable is not installed
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT FlowGraph)
        include(InstallRequiredSystemLibraries)
    endif()
    if(TARGET flowstudio)
        install(TARGETS flowstudio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT FlowGraph)
    endif()
    if(FG_INSTALL_EDITOR)
        install(TARGETS FlowGraphEditor EXPORT FlowGraphTargets COMPONENT FlowGraph
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(DIRECTORY Editor/Include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT FlowGraph)
        install(FILES ${CMAKE_BINARY_DIR}/Editor/Include/FlowGraphEditor/Export.hpp
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FlowGraphEditor COMPONENT FlowGraph)
    endif()

    # the licenses (LICENSE says which part has which); KDDockWidgets' with the Qt editor, which links it in
    install(FILES LICENSE LICENSES/MIT.txt LICENSES/GPL-3.0.txt LICENSES/LGPL-3.0.txt DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT FlowGraph)
    # ... and with flowstudio, those of GLWidgets and of the libraries it links in (its font among them)
    if(TARGET flowstudio)
        set(FG_GLW_LICENSES ${CMAKE_SOURCE_DIR}/ThirdParty/GLWidgets/LICENSE=GLWidgets.txt
            ${CMAKE_SOURCE_DIR}/ThirdParty/GLWidgets/Library/Fonts/Vazirmatn-OFL.txt=Vazirmatn-OFL.txt)
        include(FetchContent)
        foreach(dependency SDL3:LICENSE.txt freetype:LICENSE.TXT freetype:docs/FTL.TXT harfbuzz:COPYING SheenBidi:LICENSE
                           libunibreak:LICENCE thorvg:LICENSE)
            string(REPLACE ":" ";" parts ${dependency})
            list(GET parts 0 name)
            list(GET parts 1 file)
            FetchContent_GetProperties(${name} SOURCE_DIR source)
            if(source AND EXISTS ${source}/${file})
                get_filename_component(leaf ${file} NAME)
                list(APPEND FG_GLW_LICENSES "${source}/${file}=${name}-${leaf}")
            endif()
        endforeach()
        foreach(entry ${FG_GLW_LICENSES})
            string(REPLACE "=" ";" parts ${entry})
            list(GET parts 0 from)
            list(GET parts 1 as)
            install(FILES ${from} DESTINATION ${CMAKE_INSTALL_DOCDIR}/GLWidgets RENAME ${as} COMPONENT FlowGraph)
        endforeach()
    endif()
    if(FG_INSTALL_EDITOR)
        install(FILES ThirdParty/KDDockWidgets/LICENSE.txt ThirdParty/KDDockWidgets/LICENSES/GPL-2.0-only.txt
                      ThirdParty/KDDockWidgets/LICENSES/GPL-3.0-only.txt
                DESTINATION ${CMAKE_INSTALL_DOCDIR}/KDDockWidgets COMPONENT FlowGraph)
    endif()

    install(EXPORT FlowGraphTargets NAMESPACE FlowGraph:: DESTINATION ${FG_CMAKE_DIR} COMPONENT FlowGraph)
    configure_package_config_file(cmake/FlowGraphConfig.cmake.in ${CMAKE_BINARY_DIR}/FlowGraphConfig.cmake INSTALL_DESTINATION ${FG_CMAKE_DIR})
    # plugins must be built with the same FlowGraph: only the same major.minor version is compatible
    write_basic_package_version_file(${CMAKE_BINARY_DIR}/FlowGraphConfigVersion.cmake COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_BINARY_DIR}/FlowGraphConfig.cmake ${CMAKE_BINARY_DIR}/FlowGraphConfigVersion.cmake
            DESTINATION ${FG_CMAKE_DIR} COMPONENT FlowGraph)
endif()
