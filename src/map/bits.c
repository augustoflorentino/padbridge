#include "profile_int.h"

#include <stdlib.h>
#include <string.h>

const char *const pb_button_names[VPAD_BTN_COUNT] = {
    "a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "lstick", "rstick",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
};
const char *const pb_axis_names[VPAD_AXIS_COUNT] = {"lx", "ly", "rx", "ry", "lt", "rt"};
const char *const pb_motor_names[PB_MOTOR_COUNT] = {"strong", "weak", "left_trigger", "right_trigger"};
const char *const pb_output_names[3] = {"xbox360", "generic", "ds4"};

int pb_profile_new(pb_profile_t **out)
{
    pb_profile_t *p = calloc(1, sizeof(*p));

    if (p == NULL)
        return PB_ERR_NOMEM;
    p->meta.out_kind = VPAD_XBOX360;
    *out = p;
    return PB_OK;
}

int pb_profile_add(pb_profile_t *profile, const pb_field_t *field)
{
    if (profile->field_count == PB_PROFILE_FIELDS)
        return PB_ERR_FORMAT;
    profile->fields[profile->field_count++] = *field;
    return PB_OK;
}

static uint32_t pb_bits_raw(const unsigned char *buf, unsigned bit, unsigned size)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < size; i++)
        v |= (uint32_t)((buf[(bit + i) >> 3] >> ((bit + i) & 7)) & 1u) << i;
    return v;
}

static uint32_t pb_swap(uint32_t v, unsigned size)
{
    uint32_t out = 0;
    unsigned i;

    for (i = 0; i < size / 8; i++)
        out |= ((v >> (8 * i)) & 0xffu) << (size - 8 - 8 * i);
    return out;
}

int64_t pb_bits_read(const unsigned char *buf, unsigned bit, unsigned size, unsigned flags)
{
    uint32_t raw = pb_bits_raw(buf, bit, size);
    int64_t sign = (int64_t)1 << (size - 1);

    if ((flags & PB_FIELD_BIG) && size % 8 == 0)
        raw = pb_swap(raw, size);
    if (flags & PB_FIELD_SIGNED)
        return ((int64_t)raw ^ sign) - sign;
    return (int64_t)raw;
}

void pb_bits_write(unsigned char *buf, unsigned bit, unsigned size, uint32_t value)
{
    unsigned i;

    for (i = 0; i < size; i++) {
        unsigned pos = bit + i;
        unsigned char mask = (unsigned char)(1u << (pos & 7));

        if ((value >> i) & 1u)
            buf[pos >> 3] |= mask;
        else
            buf[pos >> 3] &= (unsigned char)~mask;
    }
}

void pb_profile_destroy(pb_profile_t *profile)
{
    free(profile);
}

const pb_profile_meta_t *pb_profile_meta(const pb_profile_t *profile)
{
    return profile == NULL ? NULL : &profile->meta;
}
