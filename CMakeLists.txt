cmake_minimum_required(VERSION 3.21)
project(EverythingSearch VERSION 0.4.2 LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "Configure with an x64 MSVC-compatible Windows toolchain.")
endif()

set(SAMANDARIN_SDK_DIR "" CACHE PATH "Open Samandarin 0.16 src/plugins/shared directory")
# Pin the public ABI even when using exported headers without an upstream .git.
set(header_hashes "")
foreach(name IN ITEMS com base gen fs menu vers)
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
if(NOT sdk_hash STREQUAL "cdf7ac7347c5e96a23600edca3f24f39e6c7ec7ea55cbc76471157bf3a330b5b")
    message(FATAL_ERROR "Use SDK headers from Open Samandarin revision 7382688b9ba1c9471a27b19f36beba65825b2940.")
endif()

add_library(EverythingSdkV2 STATIC third_party/everything-sdk-v2/src/Everything.c)
target_compile_definitions(EverythingSdkV2 PRIVATE UNICODE _UNICODE)
set_target_properties(EverythingSdkV2 PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(EverythingSearch MODULE src/plugin.cpp src/filesystem.cpp src/editor.cpp
    src/actions.cpp src/result_listing.cpp src/header_order.cpp
    src/settings.cpp src/settings.rc src/search.cpp src/sdk-notice.rc src/plugin.def)
target_compile_features(EverythingSearch PRIVATE cxx_std_17)
target_compile_definitions(EverythingSearch PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    EVERYTHING_SEARCH_VERSION="${PROJECT_VERSION}")
target_compile_options(EverythingSearch PRIVATE /W4 /utf-8)
target_include_directories(EverythingSearch SYSTEM PRIVATE "${SAMANDARIN_SDK_DIR}"
    third_party/everything-sdk-v2/include third_party/everything-sdk-v2/ipc)
target_link_libraries(EverythingSearch PRIVATE EverythingSdkV2 comctl32 user32 gdi32 shell32 ole32 uuid dwmapi uxtheme)
set_target_properties(EverythingSearch PROPERTIES
    PREFIX "" SUFFIX ".spl"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
