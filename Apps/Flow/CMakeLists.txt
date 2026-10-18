add_executable(flow main.cpp)
target_link_libraries(flow PRIVATE FlowGraphCore)

# Docs/NodeReference.md: the reference of everything a script of the standard applications can use (built in, without plugins).
# Not part of the normal build: `cmake --build <dir> --target node_reference` after nodes were added or changed.
add_custom_target(node_reference
    COMMAND flow --no-plugins --reference ${CMAKE_SOURCE_DIR}/Docs/NodeReference.md
    DEPENDS flow
    COMMENT "Writing Docs/NodeReference.md"
    VERBATIM)
