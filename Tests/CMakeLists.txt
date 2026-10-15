add_executable(FlowGraphTests Main.cpp)
target_link_libraries(FlowGraphTests PRIVATE FlowGraphCore)
add_test(NAME FlowGraphTests COMMAND FlowGraphTests)
