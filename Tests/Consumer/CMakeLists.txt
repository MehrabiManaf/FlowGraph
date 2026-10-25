# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: MIT
# A project that uses an INSTALLED FlowGraph, the way a host application does. CI uses it to check the package:
#   cmake --install <build> --prefix <dir> --component FlowGraph
#   cmake -S Tests/Consumer -B <consumer build> -DCMAKE_PREFIX_PATH=<dir>[;<Qt>]  &&  cmake --build <consumer build>
cmake_minimum_required(VERSION 3.21)
project(FlowGraphConsumer LANGUAGES CXX)

option(WITH_EDITOR "Also use FlowGraph::ScriptEditor" OFF)
if(WITH_EDITOR)
    find_package(FlowGraph 0.5 REQUIRED COMPONENTS ScriptEditor)
else()
    find_package(FlowGraph 0.5 REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE FlowGraph::Core)
if(WITH_EDITOR)
    target_compile_definitions(consumer PRIVATE WITH_EDITOR)
    target_link_libraries(consumer PRIVATE FlowGraph::ScriptEditor)
endif()
