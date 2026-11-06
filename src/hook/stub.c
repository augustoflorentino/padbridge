#include "hook.h"

int pb_hook_run(const char *command, const char *event, const char *controller, const char *detail)
{
    (void)command;
    (void)event;
    (void)controller;
    (void)detail;
    return PB_ERR_UNSUPPORTED;
}
