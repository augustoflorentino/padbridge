cmake_minimum_required(VERSION 3.16)
project(padbridge VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/prefix")

foreach(lib bitspan hid-parser wirefile padff cmdspawn vpad report-guess)
    find_package(${lib} REQUIRED CONFIG)
endforeach()

file(GLOB PB_SOURCES CONFIGURE_DEPENDS src/*/*.c)
add_executable(padbridge ${PB_SOURCES})
target_compile_definitions(padbridge PRIVATE PB_VERSION="${PROJECT_VERSION}")
target_link_libraries(padbridge PRIVATE hid-parser::hid-parser wirefile::wirefile padff::padff cmdspawn::cmdspawn
    vpad::vpad report-guess::report-guess bitspan::bitspan)

if(UNIX)
    target_compile_definitions(padbridge PRIVATE _POSIX_C_SOURCE=200809L)
endif()

find_package(Threads REQUIRED)
target_link_libraries(padbridge PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)
    endif()
    if(SYSTEMD_FOUND)
        target_link_libraries(padbridge PRIVATE PkgConfig::SYSTEMD)
        target_compile_definitions(padbridge PRIVATE PB_HAVE_SDBUS=1)
    endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(padbridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(PADBRIDGE_BUILD_TESTS "Build tests" OFF)
if(PADBRIDGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS padbridge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/padbridge/profiles)
