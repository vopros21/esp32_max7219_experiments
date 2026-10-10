idf_component_register(SRCS "main.c" "display.c" "buttons.c" "modes.c" "settings.c" "portal.c"
                    INCLUDE_DIRS ".")

# Generate build_time.h on every build. The second OUTPUT is never created, which
# keeps the command out of date; modes.c is then recompiled with the new time.
set(build_time_h "${CMAKE_CURRENT_BINARY_DIR}/build_time.h")
add_custom_command(
    OUTPUT "${build_time_h}" "${CMAKE_CURRENT_BINARY_DIR}/build_time_always"
    COMMAND ${CMAKE_COMMAND} "-DOUT=${build_time_h}" -P "${CMAKE_CURRENT_SOURCE_DIR}/build_time.cmake"
    VERBATIM)
add_custom_target(build_time DEPENDS "${build_time_h}")
add_dependencies(${COMPONENT_LIB} build_time)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
