cmake_minimum_required(VERSION 3.25)

project(plumb CXX)

# Tests and global settings only when built on its own, never when a consumer adds it.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Feeds .vscode/c_cpp_properties.json.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    endif()

    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    endif()
endif()

# The headers.
add_library(plumb INTERFACE)
add_library(plumb::plumb ALIAS plumb)
target_include_directories(plumb INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(plumb INTERFACE cxx_std_26)

# The bench binary's main, compiled into the consumer's executable.
add_library(plumb_main INTERFACE)
add_library(plumb::main ALIAS plumb_main)
target_sources(plumb_main INTERFACE ${PROJECT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(plumb_main INTERFACE plumb::plumb)

if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

include(CTest)                                  # BUILD_TESTING, default ON

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    # Every test_*.cpp under tests/. CONFIGURE_DEPENDS re-globs at build
    # time, so a new file needs no edit here.
    file(GLOB_RECURSE PLUMB_TEST_SRCS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/test_*.cpp)
    add_executable(plumb_tests ${PLUMB_TEST_SRCS})
    target_compile_options(plumb_tests PRIVATE -Wall -Wextra)
    target_link_libraries(plumb_tests PRIVATE plumb::plumb GTest::gtest GTest::gtest_main)
    add_test(NAME plumb_tests COMMAND plumb_tests)
endif()
