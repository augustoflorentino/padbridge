#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cli/commands.h"
#include "../core/conf.h"
#include "../core/out.h"
#include "bt_internal.h"

#define PB_SCAN_DEFAULT_S 10
#define PB_PROBE_HEX_MAX 64

static int pb_fail(const char *what, int status)
{
    pb_log(PB_LOG_ERROR, "%s: %s", what, pb_strerror(status));
    return pb_exit_code(status);
}

static const char *pb_yes(int value)
{
    return value ? "yes" : "no";
}

static void pb_device_row(const char *kind, const pb_bt_device_t *d, const char *raw_hid)
{
    static const char *const keys[] = {"address", "name", "class", "paired", "connected", "raw_hid"};
    const char *values[6];
    char cls[16];

    snprintf(cls, sizeof(cls), "0x%06lx", (unsigned long)d->device_class);
    values[0] = d->address;
    values[1] = d->name[0] != '\0' ? d->name : "-";
    values[2] = cls;
    values[3] = pb_yes(d->paired);
    values[4] = pb_yes(d->connected);
    values[5] = raw_hid;
    pb_out_row(kind, 6, keys, values);
}

static int pb_scan_row(void *ctx, const pb_bt_device_t *device)
{
    (void)ctx;
    pb_device_row("device", device, device->raw_hid ? "maybe" : "no");
    return 0;
}

int pb_cmd_scan(int argc, char **argv)
{
    int seconds = PB_SCAN_DEFAULT_S;
    char *end;
    int status;

    if (argc == 3 && strcmp(argv[1], "--seconds") == 0) {
        seconds = (int)strtol(argv[2], &end, 10);
        if (*end != '\0' || seconds <= 0 || seconds > 300)
            return PB_EXIT_USAGE;
    } else if (argc != 1) {
        return PB_EXIT_USAGE;
    }
    status = pb_bt_scan(seconds, pb_scan_row, NULL);
    return status == PB_OK ? PB_EXIT_OK : pb_fail("scan", status);
}

static void pb_report_row(const unsigned char *buf, size_t len)
{
    static const char *const keys[] = {"length", "data"};
    char hex[PB_PROBE_HEX_MAX * 2 + 4];
    char length[16];
    const char *values[2] = {length, hex};
    size_t i;
    size_t shown = len < PB_PROBE_HEX_MAX ? len : PB_PROBE_HEX_MAX;

    snprintf(length, sizeof(length), "%lu", (unsigned long)len);
    for (i = 0; i < shown; i++)
        snprintf(hex + i * 2, 3, "%02x", buf[i]);
    snprintf(hex + shown * 2, 4, "%s", shown < len ? "..." : "");
    pb_out_row("report", 2, keys, values);
}

static void pb_descriptor_row(pb_source_t *src)
{
    static const char *const keys[] = {"length"};
    unsigned char buf[4096];
    char length[16];
    const char *value = length;
    size_t len;

    if (pb_source_descriptor(src, buf, sizeof(buf), &len) == PB_OK)
        snprintf(length, sizeof(length), "%lu", (unsigned long)len);
    else
        value = "none";
    pb_out_row("descriptor", 1, keys, &value);
}

static void pb_profile_row(const pb_source_info_t *info, const char *name, const char *address)
{
    static const char *const keys[] = {"id", "name"};
    const char *values[2] = {"-", "none matches, try learn"};
    pb_profile_t *profile = NULL;
    pb_conf_t conf;

    if (pb_conf_load(NULL, &conf) == PB_OK &&
        pb_profile_match(&conf, info->vendor, info->product, name, address, &profile) == PB_OK) {
        values[0] = pb_profile_meta(profile)->id;
        values[1] = pb_profile_meta(profile)->name;
    }
    pb_out_row("profile", 2, keys, values);
    pb_profile_destroy(profile);
}

static int pb_probe_source(const char *uri, const char *name, const char *address)
{
    unsigned char buf[PB_REPORT_MAX];
    pb_source_t *src;
    size_t len;
    int status = pb_source_open(uri, &src);

    if (status != PB_OK)
        return pb_fail(uri, status);
    status = pb_source_read(src, buf, sizeof(buf), &len, PB_BT_FIRST_REPORT_MS);
    if (status == PB_OK)
        pb_report_row(buf, len);
    else
        pb_log(PB_LOG_WARN, "%s: no input report: %s", uri, pb_strerror(status));
    pb_descriptor_row(src);
    pb_profile_row(pb_source_info(src), name[0] != '\0' ? name : pb_source_info(src)->name, address);
    pb_source_close(src);
    return PB_EXIT_OK;
}

int pb_cmd_probe(int argc, char **argv)
{
    const char *target;
    unsigned char raw[6];
    char uri[32];
    pb_bt_device_t device;
    int status;

    if (argc != 2)
        return PB_EXIT_USAGE;
    target = strncmp(argv[1], "bt:", 3) == 0 ? argv[1] + 3 : argv[1];
    if (pb_bt_address(target, device.address, raw) != PB_OK)
        return pb_probe_source(argv[1], "", NULL);
    status = pb_bt_probe(device.address, &device);
    if (status != PB_OK)
        return pb_fail(device.address, status);
    pb_device_row("device", &device, "yes");
    snprintf(uri, sizeof(uri), "bt:%s", device.address);
    return pb_probe_source(uri, device.name, device.address);
}

int pb_cmd_release(int argc, char **argv)
{
    static const char *const keys[] = {"address"};
    unsigned char raw[6];
    char address[18];
    const char *value = address;
    int status;

    if (argc != 2)
        return PB_EXIT_USAGE;
    if (pb_bt_address(strncmp(argv[1], "bt:", 3) == 0 ? argv[1] + 3 : argv[1], address, raw) != PB_OK)
        return PB_EXIT_USAGE;
    status = pb_bt_release(address);
    if (status != PB_OK)
        return pb_fail(address, status);
    pb_out_row("released", 1, keys, &value);
    return PB_EXIT_OK;
}
