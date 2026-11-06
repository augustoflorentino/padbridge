#include "profile_int.h"

#include <string.h>

static const uint8_t pb_hat_dirs[8] = {0x1, 0x9, 0x8, 0xa, 0x2, 0x6, 0x4, 0x5};

static int64_t pb_clamp(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int64_t pb_field_value(const pb_field_t *f, const unsigned char *report)
{
    int64_t v = pb_clamp(pb_bits_read(report, f->bit, f->size, f->flags), f->min, f->max);

    return (f->flags & PB_FIELD_INVERT) ? f->min + f->max - v : v;
}

static int32_t pb_stick(const pb_field_t *f, int64_t v)
{
    int64_t c = (f->flags & PB_FIELD_CENTER) ? pb_clamp(f->center, f->min, f->max) : (f->min + f->max + 1) / 2;

    if (v < c)
        return (int32_t)(-32768 + (v - f->min) * 32768 / (c - f->min));
    if (f->max == c)
        return 0;
    return (int32_t)((v - c) * 32767 / (f->max - c));
}

static void pb_apply_axis(const pb_field_t *f, int64_t v, pb_state_t *state)
{
    if (f->target < VPAD_AXIS_LT)
        state->axes[f->target] = pb_stick(f, v);
    else
        state->axes[f->target] = (int32_t)((v - f->min) * 255 / (f->max - f->min));
}

static void pb_apply_hat(const pb_field_t *f, const unsigned char *report, pb_state_t *state)
{
    int64_t v = pb_bits_read(report, f->bit, f->size, f->flags);
    uint8_t dirs;

    state->buttons &= ~(1u << VPAD_BTN_DPAD_UP | 1u << VPAD_BTN_DPAD_DOWN | 1u << VPAD_BTN_DPAD_LEFT
                        | 1u << VPAD_BTN_DPAD_RIGHT);
    if (v < f->min || v > f->min + 7)
        return;
    dirs = pb_hat_dirs[v - f->min];
    if (dirs & 0x1)
        state->buttons |= 1u << VPAD_BTN_DPAD_UP;
    if (dirs & 0x2)
        state->buttons |= 1u << VPAD_BTN_DPAD_DOWN;
    if (dirs & 0x4)
        state->buttons |= 1u << VPAD_BTN_DPAD_LEFT;
    if (dirs & 0x8)
        state->buttons |= 1u << VPAD_BTN_DPAD_RIGHT;
}

static void pb_apply(const pb_field_t *f, const unsigned char *report, pb_state_t *state)
{
    if (f->kind == PB_FIELD_HAT)
        pb_apply_hat(f, report, state);
    else if (f->kind == PB_FIELD_AXIS)
        pb_apply_axis(f, pb_field_value(f, report), state);
    else if (pb_field_value(f, report))
        state->buttons |= 1u << f->target;
    else
        state->buttons &= ~(1u << f->target);
}

int pb_profile_decode(const pb_profile_t *profile, const unsigned char *report, size_t len,
                      pb_state_t *state)
{
    size_t i;

    if (profile == NULL || report == NULL || state == NULL)
        return PB_ERR_ARG;
    if (profile->report_id != 0 && (len == 0 || report[0] != profile->report_id))
        return PB_ERR_NOT_FOUND;
    if (len < profile->report_len)
        return PB_ERR_FORMAT;
    for (i = 0; i < profile->field_count; i++)
        pb_apply(&profile->fields[i], report, state);
    return PB_OK;
}

int pb_profile_rumble(const pb_profile_t *profile, const uint16_t *motors, unsigned count,
                      unsigned char *buf, size_t len, size_t *out_len)
{
    unsigned m;

    if (profile == NULL || buf == NULL || out_len == NULL || (motors == NULL && count > 0))
        return PB_ERR_ARG;
    if (profile->rumble_len == 0)
        return PB_ERR_UNSUPPORTED;
    if (len < profile->rumble_len)
        return PB_ERR_ARG;
    memcpy(buf, profile->rumble, profile->rumble_len);
    for (m = 0; m < PB_MOTOR_COUNT && m < count; m++) {
        const pb_field_t *f = &profile->motors[m];
        uint32_t full = (uint32_t)((1u << f->size) - 1);

        if (f->kind != 0)
            pb_bits_write(buf, f->bit, f->size, ((uint32_t)motors[m] * full + 32767u) / 65535u);
    }
    *out_len = profile->rumble_len;
    return PB_OK;
}
