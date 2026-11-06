#include "profile_int.h"

#include <hid-parser/hid-parser.h>
#include <string.h>

#define PB_NONE (-1)

static const int pb_hid_buttons[15] = {
    VPAD_BTN_A, VPAD_BTN_B, PB_NONE, VPAD_BTN_X, VPAD_BTN_Y, PB_NONE, VPAD_BTN_LB, VPAD_BTN_RB,
    PB_NONE, PB_NONE, VPAD_BTN_BACK, VPAD_BTN_START, VPAD_BTN_GUIDE, VPAD_BTN_LSTICK, VPAD_BTN_RSTICK,
};

typedef struct pb_desc_scan {
    const hidp_desc_t *desc;
    unsigned report_id;
    int has_z;
} pb_desc_scan_t;

static int pb_axis_for(uint32_t usage, int has_z)
{
    switch (usage) {
    case 0x00010030u:
        return VPAD_AXIS_LX;
    case 0x00010031u:
        return VPAD_AXIS_LY;
    case 0x00010032u:
        return VPAD_AXIS_RX;
    case 0x00010035u:
        return VPAD_AXIS_RY;
    case 0x00010033u:
        return has_z ? VPAD_AXIS_LT : VPAD_AXIS_RX;
    case 0x00010034u:
        return has_z ? VPAD_AXIS_RT : VPAD_AXIS_RY;
    case 0x000200c5u:
        return VPAD_AXIS_LT;
    case 0x000200c4u:
        return VPAD_AXIS_RT;
    default:
        return PB_NONE;
    }
}

static int pb_target(uint32_t usage, int has_z, unsigned *kind)
{
    uint32_t page = usage >> 16;
    uint32_t id = usage & 0xffffu;

    *kind = PB_FIELD_AXIS;
    if (usage == 0x00010039u) {
        *kind = PB_FIELD_HAT;
        return 0;
    }
    if (page == 0x09) {
        *kind = PB_FIELD_BUTTON;
        return id >= 1 && id <= 15 ? pb_hid_buttons[id - 1] : PB_NONE;
    }
    return pb_axis_for(usage, has_z);
}

static void pb_field_from(const hidp_field_t *h, size_t index, unsigned kind, int target, pb_field_t *f)
{
    memset(f, 0, sizeof(*f));
    f->kind = kind;
    f->target = target < 0 ? 0 : (unsigned)target;
    f->bit = (unsigned)(h->bit_offset + index * h->report_size);
    f->size = kind == PB_FIELD_BUTTON ? 1 : h->report_size;
    f->min = kind == PB_FIELD_BUTTON ? 0 : h->logical_min;
    f->max = kind == PB_FIELD_BUTTON ? 1 : h->logical_max;
    if (h->logical_min < 0)
        f->flags |= PB_FIELD_SIGNED;
    if (kind == PB_FIELD_HAT)
        f->max = f->min + 7;
}

static int pb_usable(const hidp_field_t *h, unsigned report_id)
{
    return h->report_type == HIDP_INPUT && h->report_id == report_id && (h->flags & HIDP_FLAG_VARIABLE)
           && !(h->flags & HIDP_FLAG_CONSTANT) && h->report_size >= 1 && h->report_size <= 32;
}

static size_t pb_scan_report(const pb_desc_scan_t *scan, pb_profile_t *p)
{
    size_t found = 0;
    size_t i;
    size_t k;

    for (i = 0; i < hidp_desc_field_count(scan->desc); i++) {
        const hidp_field_t *h = hidp_desc_field(scan->desc, i);

        if (!pb_usable(h, scan->report_id))
            continue;
        for (k = 0; k < h->report_count; k++) {
            uint32_t usage;
            unsigned kind;
            int target;
            pb_field_t f;

            if (hidp_field_usage(h, k, &usage) != HIDP_OK)
                continue;
            target = pb_target(usage, scan->has_z, &kind);
            if (target == PB_NONE || (kind != PB_FIELD_BUTTON && h->logical_min >= h->logical_max))
                continue;
            found++;
            pb_field_from(h, k, kind, target, &f);
            if (p != NULL && pb_profile_add(p, &f) != PB_OK)
                return found;
        }
    }
    return found;
}

static int pb_has_usage(const hidp_desc_t *d, uint32_t usage)
{
    size_t i;
    size_t k;

    for (i = 0; i < hidp_desc_field_count(d); i++) {
        const hidp_field_t *h = hidp_desc_field(d, i);
        uint32_t u;

        for (k = 0; h->report_type == HIDP_INPUT && k < h->report_count; k++) {
            if (hidp_field_usage(h, k, &u) == HIDP_OK && u == usage)
                return 1;
        }
    }
    return 0;
}

static const hidp_report_t *pb_best_report(pb_desc_scan_t *scan)
{
    const hidp_report_t *best = NULL;
    size_t best_count = 0;
    size_t i;

    for (i = 0; i < hidp_desc_report_count(scan->desc); i++) {
        const hidp_report_t *r = hidp_desc_report(scan->desc, i);
        size_t n;

        if (r->type != HIDP_INPUT || r->byte_len > PB_REPORT_MAX)
            continue;
        scan->report_id = r->id;
        n = pb_scan_report(scan, NULL);
        if (n > best_count) {
            best = r;
            best_count = n;
        }
    }
    return best;
}

static int pb_build(pb_desc_scan_t *scan, pb_profile_t **out)
{
    const hidp_report_t *r = pb_best_report(scan);
    pb_profile_t *p;
    int status;

    if (r == NULL)
        return PB_ERR_UNSUPPORTED;
    if ((status = pb_profile_new(&p)) != PB_OK)
        return status;
    strcpy(p->meta.id, "descriptor");
    strcpy(p->meta.name, "Profile from the HID descriptor");
    p->report_id = r->id;
    p->report_len = r->byte_len;
    scan->report_id = r->id;
    pb_scan_report(scan, p);
    *out = p;
    return PB_OK;
}

int pb_profile_from_descriptor(const unsigned char *desc, size_t len, pb_profile_t **out)
{
    pb_desc_scan_t scan;
    hidp_desc_t *d = NULL;
    size_t error_offset = 0;
    int status;

    if (desc == NULL || out == NULL)
        return PB_ERR_ARG;
    if (hidp_desc_parse(desc, len, NULL, &d, &error_offset) != HIDP_OK)
        return PB_ERR_FORMAT;
    scan.desc = d;
    scan.report_id = 0;
    scan.has_z = pb_has_usage(d, 0x00010032u) || pb_has_usage(d, 0x00010035u);
    status = pb_build(&scan, out);
    hidp_desc_destroy(d);
    return status;
}
