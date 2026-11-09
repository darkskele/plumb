# Unit tests, every test_*.cpp here. CONFIGURE_DEPENDS re-globs at build time, so a new file needs no edit.
file(GLOB_RECURSE PLUMB_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(plumb_tests ${PLUMB_TEST_SRCS})
target_compile_options(plumb_tests PRIVATE -Wall -Wextra)
target_link_libraries(plumb_tests PRIVATE plumb::plumb GTest::gtest GTest::gtest_main)
add_test(NAME plumb_tests COMMAND plumb_tests)

# Builds a target as a test. Passes when the build output matches the regex, so a build that fails for another reason fails the test.
# One at a time, since builds in one build directory share its ninja log.
function(plumb_build_test name target regex)
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target})
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}" RESOURCE_LOCK plumb_build_dir)
endfunction()

# Cases in two files, each with its own fixture named Push, list together.
plumb_add_bench(plumb_selftest selftest/bench_a.cpp selftest/bench_b.cpp)
add_test(NAME plumb_selftest_lists_both COMMAND plumb_selftest --list)
set_tests_properties(plumb_selftest_lists_both PROPERTIES PASS_REGULAR_EXPRESSION "^selftest_a\nselftest_b\n$")

# One name in two files stops the binary before anything runs.
add_executable(plumb_selftest_dup selftest_dup/bench_a.cpp selftest_dup/bench_b.cpp)
target_link_libraries(plumb_selftest_dup PRIVATE plumb::main)
add_test(NAME plumb_selftest_dup_stops
    COMMAND sh -c "if out=$(\"$0\" --list 2>&1); then exit 1; fi; echo \"$out\"; echo \"$out\" | grep -q 'duplicate case name dup_case'"
        $<TARGET_FILE:plumb_selftest_dup>)

# The same binary through plumb_add_bench fails its build.
plumb_add_bench(plumb_selftest_dup_checked EXCLUDE_FROM_ALL selftest_dup/bench_a.cpp selftest_dup/bench_b.cpp)
plumb_build_test(plumb_add_bench_stops_duplicate plumb_selftest_dup_checked "duplicate case name dup_case")

# Two different types named Push outside an anonymous namespace fail the link. Only debug builds link with LTO.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    plumb_add_bench(plumb_selftest_odr EXCLUDE_FROM_ALL selftest_odr/bench_a.cpp selftest_odr/bench_b.cpp)
    plumb_build_test(plumb_add_bench_stops_odr plumb_selftest_odr "One Definition Rule")
endif()

# Each compile_fail/*.cpp must fail to compile with the message on its first line, after "// expect: ".
file(GLOB PLUMB_COMPILE_FAIL CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/*.cpp)
foreach(src ${PLUMB_COMPILE_FAIL})
    get_filename_component(stem ${src} NAME_WE)
    file(STRINGS ${src} expect LIMIT_COUNT 1 REGEX "^// expect: ")
    string(REPLACE "// expect: " "" expect "${expect}")
    add_library(compile_fail_${stem} OBJECT EXCLUDE_FROM_ALL ${src})
    target_link_libraries(compile_fail_${stem} PRIVATE plumb::plumb)
    plumb_build_test(compile_fail_${stem} compile_fail_${stem} "${expect}")
endforeach()
