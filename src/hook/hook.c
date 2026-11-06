#include <cmdspawn/cmdspawn.h>
#include "hook.h"

static int pb_from_spawn(int status)
{
    switch (status) {
    case SPW_OK:
        return PB_OK;
    case SPW_ERR_NOMEM:
        return PB_ERR_NOMEM;
    case SPW_ERR_NOTFOUND:
        return PB_ERR_NOT_FOUND;
    case SPW_ERR_ACCESS:
        return PB_ERR_ACCESS;
    case SPW_ERR_UNSUPPORTED:
        return PB_ERR_UNSUPPORTED;
    case SPW_ERR_ARG:
    case SPW_ERR_ENCODING:
    case SPW_ERR_RANGE:
        return PB_ERR_ARG;
    default:
        return PB_ERR_IO;
    }
}

static int pb_hook_setup(spw_cmd_t *cmd, const char *command, const char *event, const char *controller,
                         const char *detail)
{
#if defined(_WIN32)
    const char *args[] = {"cmd.exe", "/d", "/c", command};
#else
    const char *args[] = {"/bin/sh", "-c", command, "padbridge-hook", event, controller, detail};
#endif
    int status = spw_cmd_args(cmd, args, sizeof(args) / sizeof(args[0]));

    if (status == SPW_OK)
        status = spw_cmd_env(cmd, "PADBRIDGE_EVENT", event);
    if (status == SPW_OK)
        status = spw_cmd_env(cmd, "PADBRIDGE_CONTROLLER", controller);
    if (status == SPW_OK)
        status = spw_cmd_env(cmd, "PADBRIDGE_DETAIL", detail);
    if (status == SPW_OK)
        status = spw_cmd_stdio(cmd, SPW_STDIN, SPW_NULL);
    return status;
}

int pb_hook_run(const char *command, const char *event, const char *controller, const char *detail)
{
    spw_cmd_t *cmd;
    long pid;
    int status;

    if (command == NULL || event == NULL)
        return PB_ERR_ARG;
    if (command[0] == '\0')
        return PB_OK;
    status = spw_cmd_create(NULL, NULL, NULL, &cmd);
    if (status != SPW_OK)
        return pb_from_spawn(status);
    status = pb_hook_setup(cmd, command, event, controller ? controller : "", detail ? detail : "");
    if (status == SPW_OK)
        status = spw_spawn_detached(cmd, &pid);
    spw_cmd_destroy(cmd);
    return pb_from_spawn(status);
}
