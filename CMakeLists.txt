cmake_minimum_required(VERSION 3.21)
project(EverythingSearch VERSION 0.5.0 LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "Configure with an x64 MSVC-compatible Windows toolchain.")
endif()

set(SAMANDARIN_SDK_DIR "" CACHE PATH "Open Samandarin 0.16 src/plugins/shared directory")
# Pin the public ABI even when using exported headers without an upstream .git.
set(header_hashes "")
foreach(name IN ITEMS com base gen fs menu vers gui)
    set(header "${SAMANDARIN_SDK_DIR}/spl_${name}.h")
    if(NOT EXISTS "${header}")
        message(FATAL_ERROR "Missing ${header}. Set SAMANDARIN_SDK_DIR to the pinned SDK directory.")
    endif()
    file(READ "${header}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    string(SHA256 hash "${contents}")
    string(APPEND header_hashes "${hash}")
endforeach()
string(SHA256 sdk_hash "${header_hashes}")
if(NOT sdk_hash STREQUAL "0c36ac8c917c4ac4c21b2579e941f2c365590aa1e8d2916689bd880b68fbcec1")
    message(FATAL_ERROR "Use SDK headers from Open Samandarin revision 7382688b9ba1c9471a27b19f36beba65825b2940.")
endif()

add_library(EverythingSdkV2 STATIC third_party/everything-sdk-v2/src/Everything.c)
target_compile_definitions(EverythingSdkV2 PRIVATE UNICODE _UNICODE)
set_target_properties(EverythingSdkV2 PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(EverythingSdkV3 STATIC third_party/everything-sdk-v3/src/Everything3.c)
target_compile_definitions(EverythingSdkV3 PRIVATE EVERYTHING3_LIB UNICODE _UNICODE)
set_target_properties(EverythingSdkV3 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_include_directories(EverythingSdkV3 SYSTEM PUBLIC third_party/everything-sdk-v3/include)

add_library(EverythingSearch MODULE src/modern_search.cpp src/plugin.cpp src/filesystem.cpp src/editor.cpp
    src/prerequisite.cpp src/actions.cpp src/result_listing.cpp src/header_order.cpp
    src/filter_catalog.cpp src/filter_dialog.cpp src/settings.cpp src/settings.rc src/search.cpp src/sdk-notice.rc src/plugin.def)
target_compile_features(EverythingSearch PRIVATE cxx_std_17)
target_compile_definitions(EverythingSearch PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    EVERYTHING_SEARCH_VERSION="${PROJECT_VERSION}")
target_compile_options(EverythingSearch PRIVATE /W4 /utf-8)
target_include_directories(EverythingSearch SYSTEM PRIVATE "${SAMANDARIN_SDK_DIR}"
    third_party/everything-sdk-v2/include third_party/everything-sdk-v2/ipc)
target_link_libraries(EverythingSearch PRIVATE EverythingSdkV2 EverythingSdkV3 comctl32 user32 gdi32 shell32 ole32 uuid dwmapi uxtheme version advapi32)
set_target_properties(EverythingSearch PROPERTIES
    PREFIX "" SUFFIX ".spl"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_program(HHC_EXECUTABLE NAMES hhc HINTS "C:/Program Files (x86)/HTML Help Workshop")
set(CHMCMD_EXECUTABLE "" CACHE FILEPATH "Alternative chmcmd compiler with archive-relative context URLs")
if(HHC_EXECUTABLE OR CHMCMD_EXECUTABLE)
    set(help_source "${CMAKE_CURRENT_SOURCE_DIR}/help")
    set(help_work "${CMAKE_CURRENT_BINARY_DIR}/manual")
    set(help_output "${CMAKE_CURRENT_BINARY_DIR}/help/english/EverythingSearch.chm")
    file(GLOB help_inputs CONFIGURE_DEPENDS "${help_source}/*")
    file(MAKE_DIRECTORY "${help_work}")
    add_custom_command(OUTPUT "${help_output}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${help_source}" "${help_work}"
        COMMAND "${CMAKE_COMMAND}" "-DHHC=${HHC_EXECUTABLE}" "-DCHMCMD=${CHMCMD_EXECUTABLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileHelp.cmake"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/help/english"
        COMMAND "${CMAKE_COMMAND}" -E copy EverythingSearch.chm "${help_output}"
        WORKING_DIRECTORY "${help_work}"
        DEPENDS ${help_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileHelp.cmake"
        VERBATIM)
    add_custom_target(EverythingSearchHelp DEPENDS "${help_output}")
    add_dependencies(EverythingSearch EverythingSearchHelp)
endif()

if(TARGET EverythingSearchHelp)
    install(TARGETS EverythingSearch LIBRARY DESTINATION plugins/EverythingSearch)
    install(FILES "${help_output}" DESTINATION help/english)
    install(FILES docs/INSTALL.txt DESTINATION plugins/EverythingSearch)
    foreach(sdk IN ITEMS v2 v3)
        install(FILES third_party/everything-sdk-${sdk}/LICENSE.txt DESTINATION plugins/EverythingSearch
                RENAME Everything-SDK-${sdk}-LICENSE.txt)
    endforeach()
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "EverythingSearch-${PROJECT_VERSION}-x64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    include(CPack)
endif()
