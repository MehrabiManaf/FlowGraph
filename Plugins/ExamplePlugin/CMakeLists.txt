# SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
# SPDX-License-Identifier: MIT
add_library(ExamplePlugin SHARED ExamplePlugin.cpp)
target_link_libraries(ExamplePlugin PRIVATE FlowGraphCore)
set_target_properties(ExamplePlugin PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(ExamplePlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin/plugins
        LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin/plugins)
endforeach()

# The translations of its nodes (Translations/ExamplePlugin_<code>.json, written by flow --only ExamplePlugin --translations) go
# next to the library, in plugins/translations, where the editor looks for them.
file(GLOB EXAMPLE_PLUGIN_CATALOGS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Translations/*.json)
set(copied)
foreach(catalog ${EXAMPLE_PLUGIN_CATALOGS})
    get_filename_component(name ${catalog} NAME)
    set(target ${CMAKE_BINARY_DIR}/bin/plugins/translations/${name})
    add_custom_command(OUTPUT ${target}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/plugins/translations
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${catalog} ${target}
        DEPENDS ${catalog} VERBATIM)
    list(APPEND copied ${target})
endforeach()
add_custom_target(ExamplePluginTranslations ALL DEPENDS ${copied})
add_dependencies(ExamplePlugin ExamplePluginTranslations)
