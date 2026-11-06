#include "pb.h"

void pb_state_neutral(pb_state_t *state)
{
    int i;

    state->buttons = 0;
    for (i = 0; i < VPAD_AXIS_COUNT; i++)
        state->axes[i] = 0;
}

const char *pb_strerror(int status)
{
    static const char *const names[] = {
        "ok", "invalid argument", "out of memory", "not supported on this system", "not found",
        "permission denied", "input/output error", "timed out", "closed", "bad format", "busy",
    };

    if (status < 0 || (size_t)status >= sizeof(names) / sizeof(names[0]))
        return "unknown error";
    return names[status];
}

int pb_exit_code(int status)
{
    switch (status) {
    case PB_OK:
        return PB_EXIT_OK;
    case PB_ERR_ARG:
    case PB_ERR_FORMAT:
        return PB_EXIT_USAGE;
    case PB_ERR_NOT_FOUND:
        return PB_EXIT_NOT_FOUND;
    case PB_ERR_UNSUPPORTED:
        return PB_EXIT_UNSUPPORTED;
    case PB_ERR_IO:
    case PB_ERR_CLOSED:
    case PB_ERR_TIMEOUT:
        return PB_EXIT_IO;
    case PB_ERR_ACCESS:
        return PB_EXIT_ACCESS;
    default:
        return PB_EXIT_FAIL;
    }
}
