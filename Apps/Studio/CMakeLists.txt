set(CMAKE_AUTOMOC ON)
qt_add_executable(FlowGraphStudio WIN32 main.cpp)
target_link_libraries(FlowGraphStudio PRIVATE FlowGraph::Editor)
