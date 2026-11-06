#include "source.h"

int pb_hid_open(const char *path, pb_source_t **out)
{
    (void)path;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_hid_list(pb_source_list_fn fn, void *ctx)
{
    (void)fn;
    (void)ctx;
    return PB_ERR_UNSUPPORTED;
}

const char *pb_hid_backend(void)
{
    return "none";
}

int pb_replay_open(const char *path, pb_source_t **out)
{
    (void)path;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}
