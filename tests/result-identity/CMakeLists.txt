cmake_minimum_required(VERSION 3.21)
project(ResultIdentityRegression LANGUAGES CXX)

# Reuse the plugin's platform check and content hashes for SAMANDARIN_SDK_DIR.
add_subdirectory(../.. plugin EXCLUDE_FROM_ALL)

add_executable(result_identity main.cpp ../../src/availability.cpp)
target_include_directories(result_identity PRIVATE ../../src ../../third_party/everything-sdk-v2/include)
target_include_directories(result_identity SYSTEM PRIVATE "${SAMANDARIN_SDK_DIR}")
target_compile_features(result_identity PRIVATE cxx_std_17)
target_compile_definitions(result_identity PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(result_identity PRIVATE /W4 /utf-8)

target_link_libraries(result_identity PRIVATE comctl32 shell32 ole32)

enable_testing()
add_test(NAME result_identity COMMAND result_identity)
