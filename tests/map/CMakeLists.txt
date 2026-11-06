set(PB_MAP_DIR ${PROJECT_SOURCE_DIR}/src/map)
add_executable(map_test map_test.c ${PB_MAP_DIR}/bits.c ${PB_MAP_DIR}/parse.c ${PB_MAP_DIR}/write.c
    ${PB_MAP_DIR}/decode.c ${PB_MAP_DIR}/descriptor.c ${PB_MAP_DIR}/learn.c
    ${PROJECT_SOURCE_DIR}/src/ff/ff.c ${PROJECT_SOURCE_DIR}/src/core/pb.c)
target_include_directories(map_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${PB_MAP_DIR})
target_link_libraries(map_test PRIVATE hid-parser::hid-parser padff::padff vpad::vpad report-guess::report-guess)
add_test(NAME map COMMAND map_test ${PROJECT_SOURCE_DIR}/profiles/ninja-sword
    ${CMAKE_CURRENT_SOURCE_DIR}/ninja-labeled.samples)
add_test(NAME map_profiles_show COMMAND padbridge profiles show ninja-sword)
add_test(NAME map_profiles_bad COMMAND padbridge profiles show nonexistent)
set_tests_properties(map_profiles_bad PROPERTIES WILL_FAIL TRUE)
add_test(NAME map_rumble_usage COMMAND padbridge rumble x)
set_tests_properties(map_rumble_usage PROPERTIES WILL_FAIL TRUE)
