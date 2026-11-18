#if !defined(__linux__) && !defined(__FreeBSD__) && !defined(_WIN32) && !defined(__APPLE__)

#include "../core/out.h"
#include "hid_internal.h"

int pb_hid_open(const char *path, pb_source_t **out)
{
    (void)path;
    if (out != NULL)
        *out = NULL;
    pb_log(PB_LOG_ERROR, "no HID backend for this system: padbridge supports hidraw, hid.dll and IOHIDManager");
    return PB_ERR_UNSUPPORTED;
}

int pb_hid_list(pb_source_list_fn fn, void *ctx)
{
    (void)fn;
    (void)ctx;
    pb_log(PB_LOG_ERROR, "no HID backend for this system: cannot list controllers");
    return PB_ERR_UNSUPPORTED;
}

const char *pb_hid_backend(void)
{
    return "none";
}

#else

typedef int pb_hid_none_unused;

#endif
