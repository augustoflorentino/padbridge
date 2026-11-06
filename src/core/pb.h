#ifndef PB_H
#define PB_H

#include <stddef.h>
#include <stdint.h>
#include <vpad/vpad.h>

#define PB_OK 0
#define PB_ERR_ARG 1
#define PB_ERR_NOMEM 2
#define PB_ERR_UNSUPPORTED 3
#define PB_ERR_NOT_FOUND 4
#define PB_ERR_ACCESS 5
#define PB_ERR_IO 6
#define PB_ERR_TIMEOUT 7
#define PB_ERR_CLOSED 8
#define PB_ERR_FORMAT 9
#define PB_ERR_BUSY 10

#define PB_EXIT_OK 0
#define PB_EXIT_FAIL 1
#define PB_EXIT_USAGE 2
#define PB_EXIT_NOT_FOUND 3
#define PB_EXIT_UNSUPPORTED 4
#define PB_EXIT_IO 5
#define PB_EXIT_ACCESS 6
#define PB_EXIT_INTERRUPTED 130

#define PB_MOTOR_STRONG 0
#define PB_MOTOR_WEAK 1
#define PB_MOTOR_LEFT_TRIGGER 2
#define PB_MOTOR_RIGHT_TRIGGER 3
#define PB_MOTOR_COUNT 4

#define PB_REPORT_MAX 1024

typedef struct pb_state {
    uint32_t buttons;
    int32_t axes[VPAD_AXIS_COUNT];
} pb_state_t;

void pb_state_neutral(pb_state_t *state);
const char *pb_strerror(int status);
int pb_exit_code(int status);

#endif
