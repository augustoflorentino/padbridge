#include "../bt/bt.h"
#include "../core/out.h"
#include "../hid/source.h"
#include "commands.h"

int pb_cmd_backends(int argc, char **argv)
{
    static const char *const keys[] = {"backend"};
    const char *value;

    (void)argv;
    if (argc != 1)
        return PB_EXIT_USAGE;
    value = pb_hid_backend();
    pb_out_row("hid", 1, keys, &value);
    value = pb_bt_backend();
    pb_out_row("bluetooth", 1, keys, &value);
    value = vpad_backend();
    pb_out_row("virtual", 1, keys, &value);
    return PB_EXIT_OK;
}
