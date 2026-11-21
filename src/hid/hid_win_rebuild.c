#ifdef _WIN32

#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <hidsdi.h>
#include <hid-parser/hid-parser.h>
#include "../core/pb.h"
#include "hid_rebuild.h"

#define PB_PROBE_MAX (PB_REPORT_MAX + 1)
#define PB_ARRAY_PROBES 3u

typedef struct pb_probe {
    PHIDP_PREPARSED_DATA pp;
    HIDP_REPORT_TYPE type;
    unsigned main_tag;
    ULONG len;
    char base[PB_PROBE_MAX];
    char work[PB_PROBE_MAX];
} pb_probe_t;

typedef struct pb_fields {
    pb_rebuild_field_t *items;
    size_t count;
} pb_fields_t;

static int pb_probe_init(pb_probe_t *p, unsigned id)
{
    memset(p->base, 0, sizeof(p->base));
    if (HidP_InitializeReportForID(p->type, (UCHAR)id, p->pp, p->base, p->len) != HIDP_STATUS_SUCCESS)
        return 0;
    memcpy(p->work, p->base, sizeof(p->work));
    return 1;
}

static long pb_probe_low(const pb_probe_t *p)
{
    ULONG byte;
    unsigned bit;

    for (byte = 1; byte < p->len; byte++) {
        unsigned char diff = (unsigned char)(p->base[byte] ^ p->work[byte]);

        for (bit = 0; diff != 0 && bit < 8; bit++)
            if (diff & (1u << bit))
                return (long)((byte - 1) * 8 + bit);
    }
    return -1;
}

static long pb_lowest(long a, long b)
{
    if (a < 0)
        return b;
    if (b < 0)
        return a;
    return a < b ? a : b;
}

static uint32_t pb_probe_read(const pb_probe_t *p, long offset, unsigned bits)
{
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < bits && i < 32; i++) {
        size_t at = (size_t)offset + 8u + i;

        if (at / 8 < p->len && ((unsigned char)p->work[at / 8] >> (at % 8)) & 1u)
            value |= 1u << i;
    }
    return value;
}

static long pb_probe_value_once(pb_probe_t *p, const HIDP_VALUE_CAPS *cap, unsigned id, int ones)
{
    USAGE usage = cap->IsRange ? cap->Range.UsageMin : cap->NotRange.Usage;
    ULONG value = !ones ? 0 : cap->BitSize >= 32 ? 0xffffffffu : (1u << cap->BitSize) - 1u;
    char array[PB_PROBE_MAX];
    USHORT array_len = (USHORT)(((size_t)cap->BitSize * cap->ReportCount + 7u) / 8u);
    NTSTATUS status;

    if (!pb_probe_init(p, id))
        return -1;
    if (!cap->IsRange && cap->ReportCount > 1 && array_len <= sizeof(array)) {
        memset(array, ones ? 0xff : 0x00, array_len);
        status = HidP_SetUsageValueArray(p->type, cap->UsagePage, cap->LinkCollection, usage, array, array_len, p->pp, p->work, p->len);
    } else {
        status = HidP_SetUsageValue(p->type, cap->UsagePage, cap->LinkCollection, usage, value, p->pp, p->work, p->len);
    }
    return status == HIDP_STATUS_SUCCESS ? pb_probe_low(p) : -1;
}

static long pb_probe_button(pb_probe_t *p, const HIDP_BUTTON_CAPS *cap, USAGE usage)
{
    ULONG count = 1;

    if (!pb_probe_init(p, cap->ReportID))
        return -1;
    if (HidP_SetUsages(p->type, cap->UsagePage, cap->LinkCollection, &usage, &count, p->pp, p->work, p->len) != HIDP_STATUS_SUCCESS)
        return -1;
    return pb_probe_low(p);
}

static pb_rebuild_field_t *pb_fields_add(pb_fields_t *fields, const pb_probe_t *p, unsigned id, unsigned collection)
{
    pb_rebuild_field_t *f = &fields->items[fields->count++];

    memset(f, 0, sizeof(*f));
    f->main_tag = p->main_tag;
    f->report_id = id;
    f->collection = collection;
    return f;
}

static void pb_add_value(pb_fields_t *fields, pb_probe_t *p, const HIDP_VALUE_CAPS *cap)
{
    long offset = pb_lowest(pb_probe_value_once(p, cap, cap->ReportID, 1), pb_probe_value_once(p, cap, cap->ReportID, 0));
    pb_rebuild_field_t *f;

    if (offset < 0 || cap->IsAlias)
        return;
    f = pb_fields_add(fields, p, cap->ReportID, cap->LinkCollection);
    f->bit_offset = (size_t)offset;
    f->bit_size = cap->BitSize;
    f->count = cap->ReportCount;
    f->flags = cap->BitField;
    f->usage_page = cap->UsagePage;
    f->is_range = cap->IsRange;
    f->usage_min = cap->IsRange ? cap->Range.UsageMin : cap->NotRange.Usage;
    f->usage_max = cap->IsRange ? cap->Range.UsageMax : cap->NotRange.Usage;
    f->logical_min = cap->LogicalMin;
    f->logical_max = cap->LogicalMax;
    f->physical_min = cap->PhysicalMin;
    f->physical_max = cap->PhysicalMax;
    f->unit = cap->Units;
    f->unit_exp = cap->UnitsExp;
}

static unsigned pb_button_report_count(const HIDP_BUTTON_CAPS *cap)
{
    return (unsigned)(cap->Reserved[0] & 0xffffu);
}

static long pb_probe_array(pb_probe_t *p, const HIDP_BUTTON_CAPS *cap, int32_t *out_first)
{
    long offset = -1;
    unsigned span = (unsigned)(cap->Range.UsageMax - cap->Range.UsageMin) + 1u;
    unsigned k;

    for (k = 0; k < PB_ARRAY_PROBES && k < span; k++)
        offset = pb_lowest(offset, pb_probe_button(p, cap, (USAGE)(cap->Range.UsageMin + k)));
    if (offset < 0)
        return -1;
    pb_probe_button(p, cap, cap->Range.UsageMin);
    *out_first = (int32_t)pb_probe_read(p, offset, 16);
    return offset;
}

static void pb_add_button(pb_fields_t *fields, pb_probe_t *p, const HIDP_BUTTON_CAPS *cap, unsigned fallback_count)
{
    USAGE first = cap->IsRange ? cap->Range.UsageMin : cap->NotRange.Usage;
    USAGE last = cap->IsRange ? cap->Range.UsageMax : cap->NotRange.Usage;
    unsigned usages = (unsigned)(last - first) + 1u;
    int variable = (cap->BitField & 0x02u) != 0;
    int32_t first_index = 0;
    long offset = variable ? pb_probe_button(p, cap, first) : pb_probe_array(p, cap, &first_index);
    pb_rebuild_field_t *f;

    if (offset < 0 || cap->IsAlias)
        return;
    f = pb_fields_add(fields, p, cap->ReportID, cap->LinkCollection);
    f->bit_offset = (size_t)offset;
    f->flags = cap->BitField;
    f->usage_page = cap->UsagePage;
    f->is_range = cap->IsRange;
    f->usage_min = first;
    f->usage_max = last;
    f->count = pb_button_report_count(cap);
    if (!variable && (f->count == 0 || f->count > PB_REPORT_MAX * 8u))
        f->count = fallback_count;
    if (variable) {
        f->bit_size = 1;
        f->count = f->count > usages ? f->count : usages;
        f->logical_max = 1;
        return;
    }
    f->count = f->count > 0 ? f->count : 1;
    f->logical_min = first_index;
    f->logical_max = first_index + (int32_t)usages - 1;
}

static unsigned pb_array_fallback(const pb_probe_t *p, const HIDP_BUTTON_CAPS *caps, USHORT count, const HIDP_BUTTON_CAPS *cap)
{
    ULONG listed = HidP_MaxUsageListLength(p->type, cap->UsagePage, p->pp);
    unsigned arrays = 0;
    USHORT i;

    for (i = 0; i < count; i++) {
        if (caps[i].UsagePage != cap->UsagePage)
            continue;
        if (caps[i].BitField & 0x02u)
            listed -= listed > 0 ? (ULONG)(caps[i].IsRange ? caps[i].Range.UsageMax - caps[i].Range.UsageMin + 1 : 1) : 0;
        else
            arrays++;
    }
    return arrays == 1 && listed > 0 ? (unsigned)listed : 1u;
}

static void pb_place_split(pb_fields_t *fields, size_t from)
{
    size_t i;
    size_t j;

    for (i = from; i < fields->count; i++) {
        for (j = from; j < fields->count; j++) {
            pb_rebuild_field_t *a = &fields->items[i];
            pb_rebuild_field_t *b = &fields->items[j];
            size_t bits = (size_t)a->bit_size * a->count;

            if (i == j || a->report_id != b->report_id || a->bit_offset != b->bit_offset || a->usage_min != b->usage_min
                || a->count >= b->count || a->bit_offset < bits)
                continue;
            a->bit_offset -= bits;
        }
    }
}

static unsigned pb_bits_needed(int32_t max)
{
    unsigned bits = 1;

    while (bits < 32 && (uint32_t)max >> bits)
        bits++;
    return bits;
}

static size_t pb_next_offset(const pb_fields_t *fields, const pb_rebuild_field_t *f, size_t limit)
{
    size_t next = limit;
    size_t j;

    for (j = 0; j < fields->count; j++) {
        const pb_rebuild_field_t *g = &fields->items[j];

        if (g->main_tag == f->main_tag && g->report_id == f->report_id && g->bit_offset > f->bit_offset && g->bit_offset < next)
            next = g->bit_offset;
    }
    return next;
}

static void pb_size_arrays(pb_fields_t *fields, size_t from, size_t report_bits)
{
    size_t i;

    for (i = from; i < fields->count; i++) {
        pb_rebuild_field_t *f = &fields->items[i];
        size_t next = pb_next_offset(fields, f, report_bits);
        size_t room = next > f->bit_offset ? (next - f->bit_offset) / f->count : 0;
        unsigned needed = pb_bits_needed(f->logical_max);

        if (f->bit_size != 0)
            continue;
        f->bit_size = (needed + 7u) / 8u * 8u;
        if (room >= needed && room < f->bit_size)
            f->bit_size = (unsigned)room;
    }
}

static int pb_collect_type(pb_fields_t *fields, pb_probe_t *p, USHORT buttons, USHORT values)
{
    HIDP_BUTTON_CAPS *button_caps = calloc(buttons + 1u, sizeof(*button_caps));
    HIDP_VALUE_CAPS *value_caps = calloc(values + 1u, sizeof(*value_caps));
    size_t from = fields->count;
    USHORT i;
    int status = button_caps && value_caps ? PB_OK : PB_ERR_NOMEM;

    if (status == PB_OK && buttons > 0 && HidP_GetButtonCaps(p->type, button_caps, &buttons, p->pp) != HIDP_STATUS_SUCCESS)
        buttons = 0;
    if (status == PB_OK && values > 0 && HidP_GetValueCaps(p->type, value_caps, &values, p->pp) != HIDP_STATUS_SUCCESS)
        values = 0;
    for (i = 0; status == PB_OK && i < buttons; i++)
        pb_add_button(fields, p, &button_caps[i], pb_array_fallback(p, button_caps, buttons, &button_caps[i]));
    for (i = 0; status == PB_OK && i < values; i++)
        pb_add_value(fields, p, &value_caps[i]);
    pb_place_split(fields, from);
    if (p->len > 0)
        pb_size_arrays(fields, from, ((size_t)p->len - 1u) * 8u);
    free(button_caps);
    free(value_caps);
    return status;
}

static int pb_collect_fields(PHIDP_PREPARSED_DATA pp, const HIDP_CAPS *caps, pb_fields_t *fields)
{
    pb_probe_t probe;
    int status;

    probe.pp = pp;
    probe.type = HidP_Input;
    probe.main_tag = HIDP_MAIN_INPUT;
    probe.len = caps->InputReportByteLength;
    status = pb_collect_type(fields, &probe, caps->NumberInputButtonCaps, caps->NumberInputValueCaps);
    probe.type = HidP_Output;
    probe.main_tag = HIDP_MAIN_OUTPUT;
    probe.len = caps->OutputReportByteLength;
    if (status == PB_OK)
        status = pb_collect_type(fields, &probe, caps->NumberOutputButtonCaps, caps->NumberOutputValueCaps);
    probe.type = HidP_Feature;
    probe.main_tag = HIDP_MAIN_FEATURE;
    probe.len = caps->FeatureReportByteLength;
    if (status == PB_OK)
        status = pb_collect_type(fields, &probe, caps->NumberFeatureButtonCaps, caps->NumberFeatureValueCaps);
    return status;
}

static pb_rebuild_collection_t *pb_collect_collections(PHIDP_PREPARSED_DATA pp, const HIDP_CAPS *caps, size_t *out_count)
{
    ULONG count = caps->NumberLinkCollectionNodes;
    HIDP_LINK_COLLECTION_NODE *nodes = calloc(count + 1u, sizeof(*nodes));
    pb_rebuild_collection_t *collections = calloc(count + 1u, sizeof(*collections));
    ULONG i;

    *out_count = 0;
    if (nodes == NULL || collections == NULL || count == 0 || HidP_GetLinkCollectionNodes(nodes, &count, pp) != HIDP_STATUS_SUCCESS) {
        free(nodes);
        free(collections);
        return NULL;
    }
    for (i = 0; i < count; i++) {
        collections[i].usage_page = nodes[i].LinkUsagePage;
        collections[i].usage = nodes[i].LinkUsage;
        collections[i].type = nodes[i].CollectionType;
        collections[i].parent = nodes[i].Parent;
    }
    free(nodes);
    *out_count = count;
    return collections;
}

static size_t pb_caps_total(const HIDP_CAPS *caps)
{
    return (size_t)caps->NumberInputButtonCaps + caps->NumberInputValueCaps + caps->NumberOutputButtonCaps
           + caps->NumberOutputValueCaps + caps->NumberFeatureButtonCaps + caps->NumberFeatureValueCaps;
}

int pb_win_rebuild(void *preparsed, unsigned char *out, size_t out_cap, size_t *out_len)
{
    PHIDP_PREPARSED_DATA pp = preparsed;
    HIDP_CAPS caps;
    pb_rebuild_collection_t *collections;
    pb_fields_t fields = {NULL, 0};
    size_t collection_count;
    int status;

    *out_len = 0;
    if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS)
        return PB_ERR_IO;
    collections = pb_collect_collections(pp, &caps, &collection_count);
    fields.items = calloc(pb_caps_total(&caps) + 1u, sizeof(*fields.items));
    status = collections != NULL && fields.items != NULL ? PB_OK : PB_ERR_NOMEM;
    if (status == PB_OK)
        status = pb_collect_fields(pp, &caps, &fields);
    if (status == PB_OK)
        status = pb_rebuild_descriptor(collections, collection_count, fields.items, fields.count, out, out_cap, out_len);
    free(collections);
    free(fields.items);
    return status;
}

#else

typedef int pb_hid_win_rebuild_unused;

#endif
