add_test(NAME help COMMAND padbridge --help)
add_test(NAME backends COMMAND padbridge backends)

foreach(part core hid bt map)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${part}/CMakeLists.txt")
        add_subdirectory(${part})
    endif()
endforeach()
