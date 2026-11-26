# plumb_add_bench(<target> [EXCLUDE_FROM_ALL] [UNLISTED] <sources>...)
#
# One bench binary from the given bench files, linked with plumb's main.
# The build fails when two cases share a name. Built with debug info in every build.
# Debug builds link with LTO, so two different types sharing a name across files fail the link.
# Listed in <build dir>/plumb_benches.txt, a line of target and binary each, unless UNLISTED or
# EXCLUDE_FROM_ALL.
function(plumb_add_bench target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "EXCLUDE_FROM_ALL;UNLISTED" "" "")
    if(arg_EXCLUDE_FROM_ALL)
        add_executable(${target} EXCLUDE_FROM_ALL ${arg_UNPARSED_ARGUMENTS})
    else()
        add_executable(${target} ${arg_UNPARSED_ARGUMENTS})
    endif()
    target_link_libraries(${target} PRIVATE plumb::main)

    # Debug info in every build, so a profiler maps samples to source lines.
    target_compile_options(${target} PRIVATE -g)

    add_custom_command(TARGET ${target} POST_BUILD COMMAND $<TARGET_FILE:${target}> --check)

    target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-flto=auto>)
    target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:-flto=auto> $<$<CONFIG:Debug>:-Werror=odr>)

    if(NOT arg_EXCLUDE_FROM_ALL AND NOT arg_UNLISTED)
        set_property(GLOBAL APPEND PROPERTY PLUMB_BENCHES "${target}\t$<TARGET_FILE:${target}>")
    endif()
endfunction()

# Writes the bench list once every directory has added its benches.
function(_plumb_write_benches)
    get_property(benches GLOBAL PROPERTY PLUMB_BENCHES)
    list(JOIN benches "\n" lines)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/plumb_benches.txt CONTENT "${lines}\n")
endfunction()

get_property(_plumb_deferred GLOBAL PROPERTY PLUMB_BENCHES_DEFERRED)
if(NOT _plumb_deferred)
    set_property(GLOBAL PROPERTY PLUMB_BENCHES_DEFERRED TRUE)
    cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _plumb_write_benches)
endif()
