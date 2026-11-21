#include <stdlib.h>
#include <string.h>
#include <hid-parser/hid-parser.h>
#include "../core/pb.h"
#include "hid_rebuild.h"

#define PB_REBUILD_DEPTH 32
#define PB_REBUILD_CONSTANT 0x01u

typedef struct pb_emit {
    unsigned char *out;
    size_t cap;
    size_t len;
    int status;
    int64_t globals[16];
    const pb_rebuild_collection_t *collections;
    size_t collection_count;
    unsigned open[PB_REBUILD_DEPTH];
    size_t depth;
} pb_emit_t;

static void pb_emit_item(pb_emit_t *e, unsigned type, unsigned tag, int64_t value)
{
    size_t written = 0;

    if (e->status != PB_OK)
        return;
    if (hidp_item_write(e->out + e->len, e->cap - e->len, type, tag, value, &written) != HIDP_OK) {
        e->status = PB_ERR_FORMAT;
        return;
    }
    e->len += written;
}

static void pb_emit_global(pb_emit_t *e, unsigned tag, int64_t value)
{
    if (e->globals[tag] == value)
        return;
    e->globals[tag] = value;
    pb_emit_item(e, HIDP_ITEM_GLOBAL, tag, value);
}

static size_t pb_collection_path(const pb_emit_t *e, unsigned collection, unsigned *path)
{
    unsigned reversed[PB_REBUILD_DEPTH];
    size_t count = 0;
    size_t i;

    if (collection >= e->collection_count)
        collection = 0;
    reversed[count++] = collection;
    while (collection != 0 && count < PB_REBUILD_DEPTH) {
        collection = e->collections[collection].parent < e->collection_count ? e->collections[collection].parent : 0;
        reversed[count++] = collection;
    }
    if (reversed[count - 1] != 0 && count < PB_REBUILD_DEPTH)
        reversed[count++] = 0;
    for (i = 0; i < count; i++)
        path[i] = reversed[count - 1 - i];
    return count;
}

static void pb_emit_open(pb_emit_t *e, unsigned collection)
{
    const pb_rebuild_collection_t *c = &e->collections[collection];

    pb_emit_global(e, HIDP_GLOBAL_USAGE_PAGE, c->usage_page);
    pb_emit_item(e, HIDP_ITEM_LOCAL, HIDP_LOCAL_USAGE, c->usage);
    pb_emit_item(e, HIDP_ITEM_MAIN, HIDP_MAIN_COLLECTION, c->type);
    e->open[e->depth++] = collection;
}

static void pb_emit_enter(pb_emit_t *e, unsigned collection)
{
    unsigned path[PB_REBUILD_DEPTH];
    size_t count = pb_collection_path(e, collection, path);
    size_t common = 0;

    while (common < e->depth && common < count && e->open[common] == path[common])
        common++;
    while (e->depth > common) {
        pb_emit_item(e, HIDP_ITEM_MAIN, HIDP_MAIN_END_COLLECTION, 0);
        e->depth--;
    }
    while (e->depth < count)
        pb_emit_open(e, path[e->depth]);
}

static void pb_emit_padding(pb_emit_t *e, const pb_rebuild_field_t *f, size_t bits)
{
    if (bits == 0)
        return;
    pb_emit_global(e, HIDP_GLOBAL_REPORT_ID, f->report_id);
    pb_emit_global(e, HIDP_GLOBAL_REPORT_SIZE, (int64_t)bits);
    pb_emit_global(e, HIDP_GLOBAL_REPORT_COUNT, 1);
    pb_emit_item(e, HIDP_ITEM_MAIN, f->main_tag, PB_REBUILD_CONSTANT);
}

static int32_t pb_logical_max(const pb_rebuild_field_t *f)
{
    if (f->logical_max >= f->logical_min || f->bit_size >= 32)
        return f->logical_max;
    return (int32_t)((uint32_t)f->logical_max & ((1u << f->bit_size) - 1u));
}

static void pb_emit_field(pb_emit_t *e, const pb_rebuild_field_t *f)
{
    pb_emit_global(e, HIDP_GLOBAL_REPORT_ID, f->report_id);
    pb_emit_global(e, HIDP_GLOBAL_USAGE_PAGE, f->usage_page);
    pb_emit_global(e, HIDP_GLOBAL_LOGICAL_MIN, f->logical_min);
    pb_emit_global(e, HIDP_GLOBAL_LOGICAL_MAX, pb_logical_max(f));
    pb_emit_global(e, HIDP_GLOBAL_PHYSICAL_MIN, f->physical_min);
    pb_emit_global(e, HIDP_GLOBAL_PHYSICAL_MAX, f->physical_max);
    pb_emit_global(e, HIDP_GLOBAL_UNIT_EXPONENT, f->unit_exp);
    pb_emit_global(e, HIDP_GLOBAL_UNIT, f->unit);
    pb_emit_global(e, HIDP_GLOBAL_REPORT_SIZE, f->bit_size);
    pb_emit_global(e, HIDP_GLOBAL_REPORT_COUNT, f->count);
    if (f->is_range) {
        pb_emit_item(e, HIDP_ITEM_LOCAL, HIDP_LOCAL_USAGE_MIN, f->usage_min);
        pb_emit_item(e, HIDP_ITEM_LOCAL, HIDP_LOCAL_USAGE_MAX, f->usage_max);
    } else {
        pb_emit_item(e, HIDP_ITEM_LOCAL, HIDP_LOCAL_USAGE, f->usage_min);
    }
    pb_emit_item(e, HIDP_ITEM_MAIN, f->main_tag, f->flags);
}

static int pb_field_order(const void *a, const void *b)
{
    const pb_rebuild_field_t *x = a;
    const pb_rebuild_field_t *y = b;

    if (x->main_tag != y->main_tag)
        return x->main_tag < y->main_tag ? -1 : 1;
    if (x->report_id != y->report_id)
        return x->report_id < y->report_id ? -1 : 1;
    if (x->bit_offset != y->bit_offset)
        return x->bit_offset < y->bit_offset ? -1 : 1;
    return 0;
}

static int pb_same_report(const pb_rebuild_field_t *a, const pb_rebuild_field_t *b)
{
    return a->main_tag == b->main_tag && a->report_id == b->report_id;
}

static void pb_emit_fields(pb_emit_t *e, const pb_rebuild_field_t *fields, size_t count)
{
    size_t end = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        const pb_rebuild_field_t *f = &fields[i];

        if (i > 0 && !pb_same_report(f, &fields[i - 1])) {
            pb_emit_padding(e, &fields[i - 1], (8u - end % 8u) % 8u);
            end = 0;
        }
        if (f->bit_offset < end || f->bit_size == 0 || f->count == 0)
            continue;
        pb_emit_enter(e, f->collection);
        pb_emit_padding(e, f, f->bit_offset - end);
        pb_emit_field(e, f);
        end = f->bit_offset + (size_t)f->bit_size * f->count;
    }
    if (count > 0)
        pb_emit_padding(e, &fields[count - 1], (8u - end % 8u) % 8u);
}

static int pb_continues(const pb_rebuild_field_t *a, const pb_rebuild_field_t *b)
{
    return pb_same_report(a, b) && !a->is_range && !b->is_range && a->collection == b->collection
           && a->bit_offset + (size_t)a->bit_size * a->count == b->bit_offset && a->bit_size == b->bit_size
           && a->flags == b->flags && a->usage_page == b->usage_page && a->usage_min == b->usage_min
           && a->logical_min == b->logical_min && a->logical_max == b->logical_max
           && a->physical_min == b->physical_min && a->physical_max == b->physical_max
           && a->unit == b->unit && a->unit_exp == b->unit_exp;
}

static size_t pb_merge_runs(pb_rebuild_field_t *fields, size_t count)
{
    size_t kept = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (kept > 0 && pb_continues(&fields[kept - 1], &fields[i]))
            fields[kept - 1].count += fields[i].count;
        else
            fields[kept++] = fields[i];
    }
    return kept;
}

int pb_rebuild_descriptor(const pb_rebuild_collection_t *collections, size_t collection_count,
                          pb_rebuild_field_t *fields, size_t field_count,
                          unsigned char *out, size_t out_cap, size_t *out_len)
{
    pb_emit_t e;

    if (collections == NULL || collection_count == 0 || (fields == NULL && field_count > 0) || out == NULL || out_len == NULL)
        return PB_ERR_ARG;
    memset(&e, 0, sizeof(e));
    e.out = out;
    e.cap = out_cap;
    e.collections = collections;
    e.collection_count = collection_count;
    qsort(fields, field_count, sizeof(*fields), pb_field_order);
    field_count = pb_merge_runs(fields, field_count);
    pb_emit_open(&e, 0);
    pb_emit_fields(&e, fields, field_count);
    while (e.depth > 0) {
        pb_emit_item(&e, HIDP_ITEM_MAIN, HIDP_MAIN_END_COLLECTION, 0);
        e.depth--;
    }
    *out_len = e.status == PB_OK ? e.len : 0;
    return e.status;
}
