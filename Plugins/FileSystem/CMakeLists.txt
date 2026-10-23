# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: MIT
# The FileSystem plugin: built into bin/plugins, where flow and flowstudio load it, and installed (and packaged) the same way.
add_library(FileSystem MODULE FileSystem.cpp)
target_link_libraries(FileSystem PRIVATE FlowGraphCore ${CMAKE_DL_LIBS})
set_target_properties(FileSystem PROPERTIES
    PREFIX ""
    SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}        # .dylib on macOS, like the other plugins (a module would be .so)
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(FileSystem PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin/plugins
        LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin/plugins)
endforeach()
if(UNIX AND NOT APPLE)
    # installed in bin/plugins: the FlowGraph libraries are two folders up, in lib
    set_target_properties(FileSystem PROPERTIES INSTALL_RPATH "$ORIGIN/../../lib")
endif()

# The translations of its nodes (Translations/FileSystem_<code>.json, written by flow --only FileSystem --translations) go next to
# the library, in plugins/translations, where the editor looks for them.
file(GLOB FILESYSTEM_CATALOGS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Translations/*.json)
set(copied)
foreach(catalog ${FILESYSTEM_CATALOGS})
    get_filename_component(name ${catalog} NAME)
    set(target ${CMAKE_BINARY_DIR}/bin/plugins/translations/${name})
    add_custom_command(OUTPUT ${target}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/plugins/translations
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${catalog} ${target}
        DEPENDS ${catalog} VERBATIM)
    list(APPEND copied ${target})
endforeach()
add_custom_target(FileSystemTranslations ALL DEPENDS ${copied})
add_dependencies(FileSystem FileSystemTranslations)
if(TARGET FlowGraphTests)
    add_dependencies(FlowGraphTests FileSystem)         # its tests load it from bin/plugins
endif()

if(FG_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS FileSystem LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins COMPONENT FlowGraph)
    if(FILESYSTEM_CATALOGS)
        install(FILES ${FILESYSTEM_CATALOGS} DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins/translations COMPONENT FlowGraph)
    endif()
endif()
