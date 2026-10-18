add_executable(FlowGraphTests Main.cpp)
target_link_libraries(FlowGraphTests PRIVATE FlowGraphCore)
add_test(NAME FlowGraphTests COMMAND FlowGraphTests)
add_executable(FlowGraphBench Bench.cpp)
target_link_libraries(FlowGraphBench PRIVATE FlowGraphCore)

# Plugins the loader must refuse (bin/testplugins, so that no application loads them by accident).
foreach(kind LEGACY MISMATCH)
    string(TOLOWER ${kind} lower)
    set(target AbiPlugin_${lower})
    add_library(${target} SHARED AbiPlugin.cpp)
    target_link_libraries(${target} PRIVATE FlowGraphCore)
    target_compile_definitions(${target} PRIVATE FG_TEST_${kind}_PLUGIN)
    set_target_properties(${target} PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/testplugins
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/testplugins)
    add_dependencies(FlowGraphTests ${target})
endforeach()
