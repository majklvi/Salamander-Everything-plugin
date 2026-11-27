cmake_minimum_required(VERSION 3.21)
project(SdkMetadataRegression LANGUAGES C CXX)

add_executable(sdk_metadata main.cpp reply_sdk.c)
target_include_directories(sdk_metadata PRIVATE ../../third_party/everything-sdk-v2/include
    ../../third_party/everything-sdk-v2/ipc)
target_compile_features(sdk_metadata PRIVATE cxx_std_17)
target_compile_definitions(sdk_metadata PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(sdk_metadata PRIVATE /utf-8)
target_link_libraries(sdk_metadata PRIVATE user32 shell32)

enable_testing()
add_test(NAME sdk_metadata COMMAND sdk_metadata)
