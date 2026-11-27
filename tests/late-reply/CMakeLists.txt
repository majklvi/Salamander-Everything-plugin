cmake_minimum_required(VERSION 3.21)
project(LateReplyHostTest LANGUAGES C CXX)

get_filename_component(plugin_root "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory("${plugin_root}" plugin)

# Only this test build routes SDK completions through the gate. The unchanged
# filesystem receiver remains the last step for every delivered snapshot.
set_property(SOURCE "${plugin_root}/src/filesystem.cpp" TARGET_DIRECTORY EverythingSearch
    APPEND PROPERTY COMPILE_DEFINITIONS DeliverSearchReply=ApplySearchReplyInHost)
set_property(SOURCE "${plugin_root}/src/search.cpp" TARGET_DIRECTORY EverythingSearch
    APPEND PROPERTY COMPILE_DEFINITIONS
    Everything_IsDBLoaded=TestIsDBLoaded Everything_GetLastError=TestGetLastError)
target_sources(EverythingSearch PRIVATE reply_gate.cpp index_state.cpp)
target_include_directories(EverythingSearch PRIVATE "${plugin_root}/src")
# Use names that are actually in the default index of the machine under test.
set(HELD_NAME "samandarinm3-falcon-old.txt" CACHE STRING "Exact basename for the held reply")
set(CURRENT_NAME "samandarinm3-linden-new.txt" CACHE STRING "Exact basename for the newer reply")
set(RELEASE_NAME "samandarinm3-cedar-other.txt" CACHE STRING "Exact basename that releases the held reply")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/reply_gate.cpp" TARGET_DIRECTORY EverythingSearch
    APPEND PROPERTY COMPILE_DEFINITIONS
    "HELD_NAME=L\"${HELD_NAME}\"" "CURRENT_NAME=L\"${CURRENT_NAME}\"" "RELEASE_NAME=L\"${RELEASE_NAME}\"")
set_target_properties(EverythingSearch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
