cmake_minimum_required(VERSION 3.14)
project(libparsegraph C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source files
file(GLOB_RECURSE LIB_SOURCES
    src/util/*.c
    src/core/*.c
    src/storage/*.c
    src/index/*.c
    src/query/*.c
    src/exec/*.c
    src/memo/*.c
    src/stream/*.c
)

# Include directories
include_directories(include src)

# Static library
add_library(parsegraph STATIC ${LIB_SOURCES})
target_include_directories(parsegraph PUBLIC include PRIVATE src)
target_link_libraries(parsegraph m)

# Test executable
file(GLOB TEST_SOURCES tests/*.c)
add_executable(test_parsegraph ${TEST_SOURCES})
target_link_libraries(test_parsegraph parsegraph m)
target_include_directories(test_parsegraph PRIVATE include src)

# Fuzz target (clang only)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_executable(fuzz_parsegraph src/fuzz/fuzz_target.c)
    target_link_libraries(fuzz_parsegraph parsegraph m)
    target_compile_options(fuzz_parsegraph PRIVATE
        -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
    target_link_options(fuzz_parsegraph PRIVATE
        -fsanitize=fuzzer,address)
endif()

# ASAN variant
add_executable(test_parsegraph_asan ${TEST_SOURCES})
target_link_libraries(test_parsegraph_asan parsegraph m)
target_include_directories(test_parsegraph_asan PRIVATE include src)
target_compile_options(test_parsegraph_asan PRIVATE
    -fsanitize=address -fno-omit-frame-pointer -g -O1)
target_link_options(test_parsegraph_asan PRIVATE
    -fsanitize=address)
