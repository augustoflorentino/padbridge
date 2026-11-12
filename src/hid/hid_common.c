#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <hid-parser/hid-parser.h>
#include "hid_internal.h"

#define PB_BUSTYPE_USB 0x03u
#define PB_BUSTYPE_BLUETOOTH 0x05u

static int pb_find_report_id(void *ctx, const hidp_item_t *item)
{
    int *found = ctx;

    if (item->type == HIDP_ITEM_GLOBAL && item->tag == HIDP_GLOBAL_REPORT_ID) {
        *found = 1;
        return 1;
    }
    return HIDP_OK;
}

int pb_hid_numbered(const unsigned char *desc, size_t len)
{
    int found = 0;
    size_t error_offset;

    hidp_tokenize(desc, len, pb_find_report_id, &found, &error_offset);
    return found;
}

int pb_hid_errno(int err)
{
    switch (err) {
    case EACCES:
    case EPERM:
        return PB_ERR_ACCESS;
    case ENOENT:
    case ENXIO:
        return PB_ERR_NOT_FOUND;
    case ENODEV:
    case EPIPE:
        return PB_ERR_CLOSED;
    case EBUSY:
        return PB_ERR_BUSY;
    case ENOMEM:
        return PB_ERR_NOMEM;
    default:
        return PB_ERR_IO;
    }
}

void pb_hid_set_uri(pb_source_info_t *info, const char *scheme, const char *path)
{
    snprintf(info->uri, sizeof(info->uri), "%s:%s", scheme, path);
}

void pb_hid_copy_name(pb_source_info_t *info, const char *name)
{
    snprintf(info->name, sizeof(info->name), "%s", name);
}

unsigned pb_hid_bus(unsigned bustype)
{
    if (bustype == PB_BUSTYPE_USB)
        return PB_BUS_USB;
    if (bustype == PB_BUSTYPE_BLUETOOTH)
        return PB_BUS_BLUETOOTH;
    return PB_BUS_UNKNOWN;
}
