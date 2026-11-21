#ifndef PB_HID_REBUILD_H
#define PB_HID_REBUILD_H

#include <stddef.h>
#include <stdint.h>

typedef struct pb_rebuild_collection {
    uint16_t usage_page;
    uint16_t usage;
    unsigned type;
    unsigned parent;
} pb_rebuild_collection_t;

typedef struct pb_rebuild_field {
    unsigned main_tag;
    unsigned report_id;
    unsigned collection;
    size_t bit_offset;
    unsigned bit_size;
    unsigned count;
    unsigned flags;
    uint16_t usage_page;
    uint16_t usage_min;
    uint16_t usage_max;
    int is_range;
    int32_t logical_min;
    int32_t logical_max;
    int32_t physical_min;
    int32_t physical_max;
    uint32_t unit;
    uint32_t unit_exp;
} pb_rebuild_field_t;

int pb_rebuild_descriptor(const pb_rebuild_collection_t *collections, size_t collection_count,
                          pb_rebuild_field_t *fields, size_t field_count,
                          unsigned char *out, size_t out_cap, size_t *out_len);

int pb_win_rebuild(void *preparsed, unsigned char *out, size_t out_cap, size_t *out_len);

#endif
