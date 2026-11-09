# plumb_add_bench(<target> [EXCLUDE_FROM_ALL] <sources>...)
#
# One bench binary from the given bench files, linked with plumb's main.
# The build fails when two cases share a name.
# Debug builds link with LTO, so two different types sharing a name across files fail the link.
function(plumb_add_bench target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "EXCLUDE_FROM_ALL" "" "")
    if(arg_EXCLUDE_FROM_ALL)
        add_executable(${target} EXCLUDE_FROM_ALL ${arg_UNPARSED_ARGUMENTS})
    else()
        add_executable(${target} ${arg_UNPARSED_ARGUMENTS})
    endif()
    target_link_libraries(${target} PRIVATE plumb::main)

    add_custom_command(TARGET ${target} POST_BUILD COMMAND $<TARGET_FILE:${target}> --check)

    target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-flto=auto>)
    target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:-flto=auto> $<$<CONFIG:Debug>:-Werror=odr>)
endfunction()
