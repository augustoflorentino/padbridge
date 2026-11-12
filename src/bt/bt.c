#include <string.h>
#include "../core/out.h"
#include "bt_internal.h"

const char *pb_bt_backend(void)
{
#if defined(PB_HAVE_SDBUS)
    return "bluez";
#elif defined(__linux__) || defined(__FreeBSD__)
    return "l2cap";
#else
    return "none";
#endif
}

int pb_bt_scan(int seconds, pb_bt_device_fn fn, void *ctx)
{
    if (seconds <= 0 || fn == NULL)
        return PB_ERR_ARG;
    return pb_bluez_scan(seconds, fn, ctx);
}

static int pb_bt_open_live(const char *address, pb_source_t **out)
{
    int status = pb_l2cap_open(address, out);

    if (status != PB_OK)
        return status;
    status = pb_l2cap_wait_report(*out, PB_BT_FIRST_REPORT_MS);
    if (status != PB_OK) {
        pb_source_close(*out);
        *out = NULL;
    }
    return status;
}

int pb_bt_probe(const char *address, pb_bt_device_t *out)
{
    unsigned char raw[6];
    pb_source_t *src;
    int status;

    if (out == NULL || pb_bt_address(address, out->address, raw) != PB_OK)
        return PB_ERR_ARG;
    if (pb_bluez_info(out->address, out) != PB_OK) {
        memset(out, 0, sizeof(*out));
        pb_bt_address(address, out->address, raw);
    }
    status = pb_bt_open_live(out->address, &src);
    if (status != PB_OK)
        return status;
    out->raw_hid = 1;
    out->connected = 1;
    pb_source_close(src);
    return PB_OK;
}

int pb_bt_connect(const char *address, int pair, pb_source_t **out)
{
    unsigned char raw[6];
    char norm[18];
    int status;

    if (out == NULL || pb_bt_address(address, norm, raw) != PB_OK)
        return PB_ERR_ARG;
    status = pb_bt_open_live(norm, out);
    if (status == PB_OK || !pair || status == PB_ERR_UNSUPPORTED)
        return status;
    pb_log(PB_LOG_INFO, "%s: direct connection failed (%s), searching", norm, pb_strerror(status));
    status = pb_bluez_pair(norm);
    if (status != PB_OK)
        return status;
    return pb_bt_open_live(norm, out);
}

int pb_bt_release(const char *address)
{
    unsigned char raw[6];
    char norm[18];

    if (pb_bt_address(address, norm, raw) != PB_OK)
        return PB_ERR_ARG;
    return pb_bluez_disconnect(norm);
}
