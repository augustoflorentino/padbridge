#include "bt.h"

const char *pb_bt_backend(void)
{
    return "none";
}

int pb_bt_scan(int seconds, pb_bt_device_fn fn, void *ctx)
{
    (void)seconds;
    (void)fn;
    (void)ctx;
    return PB_ERR_UNSUPPORTED;
}

int pb_bt_probe(const char *address, pb_bt_device_t *out)
{
    (void)address;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_bt_connect(const char *address, int pair, pb_source_t **out)
{
    (void)address;
    (void)pair;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_bt_release(const char *address)
{
    (void)address;
    return PB_ERR_UNSUPPORTED;
}

int pb_l2cap_open(const char *address, pb_source_t **out)
{
    (void)address;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}
