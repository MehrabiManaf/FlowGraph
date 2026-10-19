add_library(FlowGraphCore ${FG_LIB_TYPE}
    Source/Json.cpp
    Source/TypeExpr.cpp
    Source/Value.cpp
    Source/Types.cpp
    Source/Registry.cpp
    Source/Script.cpp
    Source/Compiler.cpp
    Source/Vm.cpp
    Source/Executor.cpp
    Source/Compatibility.cpp
    Source/StandardApp.cpp
    Source/Builtins.cpp
    Source/BuiltinsTime.cpp
    Source/BuiltinsSystem.cpp
    Source/Process.cpp
    Source/Inspect.cpp
    Source/Intellisense.cpp
    Source/Search.cpp
    Source/Refactor.cpp
    Source/Reference.cpp
    Source/Runtime.cpp)
add_library(FlowGraph::Core ALIAS FlowGraphCore)
set_target_properties(FlowGraphCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    CXX_VISIBILITY_PRESET hidden            # GCC / Clang: only what FG_CORE_EXPORT marks is exported, as on Windows
    VISIBILITY_INLINES_HIDDEN ON
    EXPORT_NAME Core)
# FG_CORE_EXPORT: <build>/Core/Include/FlowGraph/Export.hpp
generate_export_header(FlowGraphCore
    BASE_NAME FG_CORE
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/Include/FlowGraph/Export.hpp)
if(NOT FG_SHARED)
    target_compile_definitions(FlowGraphCore PUBLIC FG_CORE_STATIC_DEFINE)
endif()
target_include_directories(FlowGraphCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(FlowGraphCore PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(FlowGraphCore PUBLIC Threads::Threads)
if(NOT WIN32)
    target_link_libraries(FlowGraphCore PUBLIC ${CMAKE_DL_LIBS})
endif()
