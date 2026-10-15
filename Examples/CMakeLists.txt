add_executable(fgrun Run.cpp)
target_link_libraries(fgrun PRIVATE FlowGraphCore)

add_executable(fgexamples MakeExamples.cpp)
target_link_libraries(fgexamples PRIVATE FlowGraphCore)
