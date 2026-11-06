#ifndef PB_PROFILE_INT_H
#define PB_PROFILE_INT_H

#include "map.h"

#define PB_FIELD_BUTTON 1u
#define PB_FIELD_AXIS 2u
#define PB_FIELD_HAT 3u

#define PB_FIELD_SIGNED 0x1u
#define PB_FIELD_INVERT 0x2u
#define PB_FIELD_BIG 0x4u
#define PB_FIELD_CENTER 0x8u

#define PB_PROFILE_FIELDS 64
#define PB_RUMBLE_MAX 64

typedef struct pb_field {
    unsigned kind;
    unsigned target;
    unsigned bit;
    unsigned size;
    unsigned flags;
    int64_t min;
    int64_t max;
    int64_t center;
} pb_field_t;

struct pb_profile {
    pb_profile_meta_t meta;
    unsigned report_id;
    size_t report_len;
    pb_field_t fields[PB_PROFILE_FIELDS];
    size_t field_count;
    unsigned char rumble[PB_RUMBLE_MAX];
    size_t rumble_len;
    pb_field_t motors[PB_MOTOR_COUNT];
};

extern const char *const pb_button_names[VPAD_BTN_COUNT];
extern const char *const pb_axis_names[VPAD_AXIS_COUNT];
extern const char *const pb_motor_names[PB_MOTOR_COUNT];
extern const char *const pb_output_names[3];

int pb_profile_new(pb_profile_t **out);
int pb_profile_add(pb_profile_t *profile, const pb_field_t *field);
int64_t pb_bits_read(const unsigned char *buf, unsigned bit, unsigned size, unsigned flags);
void pb_bits_write(unsigned char *buf, unsigned bit, unsigned size, uint32_t value);

#endif
