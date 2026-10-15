add_library(FlowGraphCore STATIC
    Source/Json.cpp
    Source/TypeExpr.cpp
    Source/Value.cpp
    Source/Types.cpp
    Source/Registry.cpp
    Source/Script.cpp
    Source/Compiler.cpp
    Source/Vm.cpp
    Source/Builtins.cpp
    Source/Inspect.cpp
    Source/Intellisense.cpp
    Source/Runtime.cpp)
target_include_directories(FlowGraphCore PUBLIC Include)
target_compile_features(FlowGraphCore PUBLIC cxx_std_20)
if(NOT WIN32)
    target_link_libraries(FlowGraphCore PUBLIC ${CMAKE_DL_LIBS})
endif()
