#include "profile_int.h"

#include <inttypes.h>

static void pb_write_options(const pb_field_t *f, FILE *s)
{
    int64_t full = (int64_t)(((uint64_t)1 << f->size) - 1);
    int64_t smax = (int64_t)(((uint64_t)1 << (f->size - 1)) - 1);
    int natural = (f->flags & PB_FIELD_SIGNED) ? f->min == -smax - 1 && f->max == smax : f->min == 0 && f->max == full;

    if (f->kind == PB_FIELD_HAT)
        natural = f->min == 0 && f->max == 7 && !(f->flags & PB_FIELD_SIGNED);
    if (f->flags & PB_FIELD_SIGNED)
        fputs(" signed", s);
    if (f->flags & PB_FIELD_INVERT)
        fputs(" invert", s);
    if (f->flags & PB_FIELD_BIG)
        fputs(" big", s);
    if (!natural)
        fprintf(s, " range %" PRId64 " %" PRId64, f->min, f->max);
    if (f->flags & PB_FIELD_CENTER)
        fprintf(s, " center %" PRId64, f->center);
}

static void pb_write_field(const pb_field_t *f, FILE *s)
{
    if (f->kind == PB_FIELD_BUTTON) {
        fprintf(s, "button %s %u.%u%s\n", pb_button_names[f->target], f->bit / 8, f->bit % 8,
                (f->flags & PB_FIELD_INVERT) ? " invert" : "");
        return;
    }
    if (f->kind == PB_FIELD_AXIS)
        fprintf(s, "axis %s %u.%u %u", pb_axis_names[f->target], f->bit / 8, f->bit % 8, f->size);
    else
        fprintf(s, "hat %u.%u %u", f->bit / 8, f->bit % 8, f->size);
    pb_write_options(f, s);
    fputc('\n', s);
}

static void pb_write_meta(const pb_profile_meta_t *m, FILE *s)
{
    fprintf(s, "profile %s\n", m->id);
    if (m->name[0] != '\0')
        fprintf(s, "name %s\n", m->name);
    if (m->vendor != 0)
        fprintf(s, "match vendor 0x%04x\n", m->vendor);
    if (m->product != 0)
        fprintf(s, "match product 0x%04x\n", m->product);
    if (m->match_name[0] != '\0')
        fprintf(s, "match name %s\n", m->match_name);
    if (m->address[0] != '\0')
        fprintf(s, "match address %s\n", m->address);
    fprintf(s, "output %s", pb_output_names[m->out_kind]);
    if (m->out_vendor != 0 || m->out_product != 0)
        fprintf(s, " 0x%04x 0x%04x", m->out_vendor, m->out_product);
    fputc('\n', s);
    if (m->out_name[0] != '\0')
        fprintf(s, "output-name %s\n", m->out_name);
}

static void pb_write_rumble(const pb_profile_t *p, FILE *s)
{
    size_t i;
    unsigned m;

    if (p->rumble_len == 0)
        return;
    fputs("rumble", s);
    for (i = 0; i < p->rumble_len; i++)
        fprintf(s, " %02x", p->rumble[i]);
    fputc('\n', s);
    for (m = 0; m < PB_MOTOR_COUNT; m++) {
        const pb_field_t *f = &p->motors[m];

        if (f->kind != 0)
            fprintf(s, "motor %s %u.%u %u\n", pb_motor_names[m], f->bit / 8, f->bit % 8, f->size);
    }
}

int pb_profile_write(const pb_profile_t *profile, FILE *stream)
{
    size_t i;

    if (profile == NULL || stream == NULL)
        return PB_ERR_ARG;
    pb_write_meta(&profile->meta, stream);
    fprintf(stream, "report %u %zu\n", profile->report_id, profile->report_len);
    for (i = 0; i < profile->field_count; i++)
        pb_write_field(&profile->fields[i], stream);
    pb_write_rumble(profile, stream);
    return ferror(stream) ? PB_ERR_IO : PB_OK;
}
