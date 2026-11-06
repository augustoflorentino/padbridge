set(PB_SRC ${PROJECT_SOURCE_DIR}/src)
add_executable(test_bt test_bt.c ${PB_SRC}/sink/sink.c ${PB_SRC}/sink/vpad_sink.c ${PB_SRC}/sink/text_sink.c
    ${PB_SRC}/hook/hook.c ${PB_SRC}/core/out.c ${PB_SRC}/core/pb.c)
target_link_libraries(test_bt PRIVATE cmdspawn::cmdspawn vpad::vpad)
if(UNIX)
    target_compile_definitions(test_bt PRIVATE _POSIX_C_SOURCE=200809L)
endif()
add_test(NAME bt COMMAND test_bt)
