set(PB_SRC ${PROJECT_SOURCE_DIR}/src)

add_executable(test_bt test_bt.c ${PB_SRC}/sink/sink.c ${PB_SRC}/sink/vpad_sink.c ${PB_SRC}/sink/text_sink.c
    ${PB_SRC}/hook/hook.c ${PB_SRC}/core/out.c ${PB_SRC}/core/pb.c)
target_link_libraries(test_bt PRIVATE cmdspawn::cmdspawn vpad::vpad)
add_test(NAME bt_sink_hook COMMAND test_bt)

if(CMAKE_SYSTEM_NAME MATCHES "Linux|FreeBSD")
    add_executable(test_l2cap test_l2cap.c ${PB_SRC}/bt/addr.c ${PB_SRC}/bt/l2cap.c ${PB_SRC}/bt/bt.c
        ${PB_SRC}/bt/bluez.c ${PB_SRC}/hid/source.c ${PB_SRC}/core/out.c ${PB_SRC}/core/pb.c ${PB_SRC}/core/sys.c)
    target_link_libraries(test_l2cap PRIVATE vpad::vpad Threads::Threads)
    if(SYSTEMD_FOUND)
        target_link_libraries(test_l2cap PRIVATE PkgConfig::SYSTEMD)
        target_compile_definitions(test_l2cap PRIVATE PB_HAVE_SDBUS=1)
    endif()
    add_test(NAME bt_l2cap COMMAND test_l2cap)
endif()

if(UNIX)
    foreach(t test_bt test_l2cap)
        if(TARGET ${t})
            target_compile_definitions(${t} PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
    endforeach()
endif()
