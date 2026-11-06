#include "profile_int.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define PB_LINE_MAX 512
#define PB_TOKENS 24

typedef struct pb_line {
    char text[PB_LINE_MAX];
    char *tok[PB_TOKENS];
    size_t count;
    const char *rest;
} pb_line_t;

static int pb_split(pb_line_t *l)
{
    char *p = l->text;

    l->count = 0;
    l->rest = "";
    while (*p != '\0') {
        while (isspace((unsigned char)*p))
            *p++ = '\0';
        if (*p == '\0' || *p == '#')
            break;
        if (l->count == PB_TOKENS)
            return 0;
        l->tok[l->count++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p))
            p++;
    }
    return 1;
}

static const char *pb_rest_after(const char *line, size_t skip)
{
    const char *p = line;

    while (skip-- > 0) {
        while (isspace((unsigned char)*p))
            p++;
        while (*p != '\0' && !isspace((unsigned char)*p))
            p++;
    }
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

static int pb_copy_rest(char *dst, size_t len, const char *src)
{
    size_t n = strlen(src);

    while (n > 0 && isspace((unsigned char)src[n - 1]))
        n--;
    if (n == 0 || n >= len)
        return 0;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 1;
}

static int pb_number(const char *s, int64_t min, int64_t max, int64_t *out)
{
    char *end;
    long long v;

    if (s == NULL || *s == '\0')
        return 0;
    v = strtoll(s, &end, 0);
    if (*end != '\0' || v < min || v > max)
        return 0;
    *out = v;
    return 1;
}

static int pb_position(const char *s, unsigned *out)
{
    char *end;
    unsigned long byte = strtoul(s, &end, 10);
    unsigned long bit;

    if (end == s || *end != '.')
        return 0;
    s = end + 1;
    bit = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || bit > 7 || byte >= PB_REPORT_MAX)
        return 0;
    *out = (unsigned)(byte * 8 + bit);
    return 1;
}

static int pb_name_index(const char *const *names, size_t count, const char *name)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0)
            return (int)i;
    }
    return -1;
}

static int pb_options(pb_line_t *l, size_t from, pb_field_t *f)
{
    size_t i = from;

    while (i < l->count) {
        const char *o = l->tok[i++];

        if (strcmp(o, "signed") == 0)
            f->flags |= PB_FIELD_SIGNED;
        else if (strcmp(o, "invert") == 0)
            f->flags |= PB_FIELD_INVERT;
        else if (strcmp(o, "big") == 0)
            f->flags |= PB_FIELD_BIG;
        else if (strcmp(o, "range") == 0 && i + 1 < l->count && pb_number(l->tok[i], INT32_MIN, UINT32_MAX, &f->min)
                 && pb_number(l->tok[i + 1], INT32_MIN, UINT32_MAX, &f->max) && f->min < f->max)
            i += 2;
        else if (strcmp(o, "center") == 0 && i < l->count && pb_number(l->tok[i], INT32_MIN, UINT32_MAX, &f->center)) {
            f->flags |= PB_FIELD_CENTER;
            i++;
        } else
            return 0;
    }
    return 1;
}

static int pb_sized(pb_line_t *l, size_t at, pb_field_t *f)
{
    int64_t size;

    if (at + 1 >= l->count || !pb_position(l->tok[at], &f->bit) || !pb_number(l->tok[at + 1], 1, 32, &size))
        return 0;
    f->size = (unsigned)size;
    f->min = 0;
    f->max = (int64_t)(((uint64_t)1 << f->size) - 1);
    if (!pb_options(l, at + 2, f))
        return 0;
    if ((f->flags & PB_FIELD_SIGNED) && f->min == 0 && f->max == (int64_t)(((uint64_t)1 << f->size) - 1)) {
        f->max = (int64_t)(((uint64_t)1 << (f->size - 1)) - 1);
        f->min = -f->max - 1;
    }
    return 1;
}

static int pb_parse_button(pb_profile_t *p, pb_line_t *l)
{
    pb_field_t f;
    int target;

    memset(&f, 0, sizeof(f));
    if (l->count < 3 || (target = pb_name_index(pb_button_names, VPAD_BTN_COUNT, l->tok[1])) < 0)
        return 0;
    if (!pb_position(l->tok[2], &f.bit) || !pb_options(l, 3, &f))
        return 0;
    f.kind = PB_FIELD_BUTTON;
    f.target = (unsigned)target;
    f.size = 1;
    f.max = 1;
    return pb_profile_add(p, &f) == PB_OK;
}

static int pb_parse_axis(pb_profile_t *p, pb_line_t *l)
{
    pb_field_t f;
    int target;

    memset(&f, 0, sizeof(f));
    if (l->count < 4 || (target = pb_name_index(pb_axis_names, VPAD_AXIS_COUNT, l->tok[1])) < 0)
        return 0;
    if (!pb_sized(l, 2, &f))
        return 0;
    f.kind = PB_FIELD_AXIS;
    f.target = (unsigned)target;
    return pb_profile_add(p, &f) == PB_OK;
}

static int pb_parse_hat(pb_profile_t *p, pb_line_t *l)
{
    pb_field_t f;

    memset(&f, 0, sizeof(f));
    if (!pb_sized(l, 1, &f))
        return 0;
    if (f.max - f.min == (int64_t)(((uint64_t)1 << f.size) - 1))
        f.max = f.min + 7;
    f.kind = PB_FIELD_HAT;
    return pb_profile_add(p, &f) == PB_OK;
}

static int pb_parse_motor(pb_profile_t *p, pb_line_t *l)
{
    pb_field_t f;
    int target;

    memset(&f, 0, sizeof(f));
    if (l->count != 4 || (target = pb_name_index(pb_motor_names, PB_MOTOR_COUNT, l->tok[1])) < 0)
        return 0;
    if (!pb_sized(l, 2, &f) || f.size > 16)
        return 0;
    f.kind = 1;
    p->motors[target] = f;
    return 1;
}

static int pb_parse_rumble(pb_profile_t *p, pb_line_t *l)
{
    size_t i;
    int64_t v;

    if (l->count < 2 || l->count - 1 > PB_RUMBLE_MAX)
        return 0;
    for (i = 1; i < l->count; i++) {
        if (strlen(l->tok[i]) != 2 || !isxdigit((unsigned char)l->tok[i][0]) || !isxdigit((unsigned char)l->tok[i][1]))
            return 0;
        v = strtol(l->tok[i], NULL, 16);
        p->rumble[i - 1] = (unsigned char)v;
    }
    p->rumble_len = l->count - 1;
    return 1;
}

static int pb_parse_match(pb_profile_t *p, pb_line_t *l, const char *line)
{
    int64_t v;

    if (l->count < 3)
        return 0;
    if (strcmp(l->tok[1], "name") == 0)
        return pb_copy_rest(p->meta.match_name, sizeof(p->meta.match_name), pb_rest_after(line, 2));
    if (strcmp(l->tok[1], "address") == 0)
        return l->count == 3 && pb_copy_rest(p->meta.address, sizeof(p->meta.address), l->tok[2]);
    if (l->count != 3 || !pb_number(l->tok[2], 0, 0xffff, &v))
        return 0;
    if (strcmp(l->tok[1], "vendor") == 0)
        p->meta.vendor = (uint16_t)v;
    else if (strcmp(l->tok[1], "product") == 0)
        p->meta.product = (uint16_t)v;
    else
        return 0;
    return 1;
}

static int pb_parse_output(pb_profile_t *p, pb_line_t *l)
{
    int kind = l->count >= 2 ? pb_name_index(pb_output_names, 3, l->tok[1]) : -1;
    int64_t vendor;
    int64_t product;

    if (kind < 0 || (l->count != 2 && l->count != 4))
        return 0;
    p->meta.out_kind = kind;
    if (l->count == 2)
        return 1;
    if (!pb_number(l->tok[2], 0, 0xffff, &vendor) || !pb_number(l->tok[3], 0, 0xffff, &product))
        return 0;
    p->meta.out_vendor = (uint16_t)vendor;
    p->meta.out_product = (uint16_t)product;
    return 1;
}

static int pb_parse_report(pb_profile_t *p, pb_line_t *l)
{
    int64_t id;
    int64_t len;

    if (l->count != 3 || !pb_number(l->tok[1], 0, 255, &id) || !pb_number(l->tok[2], 1, PB_REPORT_MAX, &len))
        return 0;
    p->report_id = (unsigned)id;
    p->report_len = (size_t)len;
    return 1;
}

static int pb_parse_line(pb_profile_t *p, pb_line_t *l, const char *line)
{
    const char *k = l->tok[0];

    if (strcmp(k, "profile") == 0)
        return l->count == 2 && pb_copy_rest(p->meta.id, sizeof(p->meta.id), l->tok[1]);
    if (strcmp(k, "name") == 0)
        return pb_copy_rest(p->meta.name, sizeof(p->meta.name), pb_rest_after(line, 1));
    if (strcmp(k, "output-name") == 0)
        return pb_copy_rest(p->meta.out_name, sizeof(p->meta.out_name), pb_rest_after(line, 1));
    if (strcmp(k, "match") == 0)
        return pb_parse_match(p, l, line);
    if (strcmp(k, "output") == 0)
        return pb_parse_output(p, l);
    if (strcmp(k, "report") == 0)
        return pb_parse_report(p, l);
    if (strcmp(k, "button") == 0)
        return pb_parse_button(p, l);
    if (strcmp(k, "axis") == 0)
        return pb_parse_axis(p, l);
    if (strcmp(k, "hat") == 0)
        return pb_parse_hat(p, l);
    if (strcmp(k, "rumble") == 0)
        return pb_parse_rumble(p, l);
    if (strcmp(k, "motor") == 0)
        return pb_parse_motor(p, l);
    return 0;
}

static int pb_fits(const pb_field_t *f, size_t len)
{
    return len == 0 || f->bit + f->size <= len * 8;
}

static size_t pb_check(pb_profile_t *p)
{
    size_t i;
    unsigned m;

    if (p->meta.id[0] == '\0' || p->report_len == 0)
        return 1;
    for (i = 0; i < p->field_count; i++) {
        if (!pb_fits(&p->fields[i], p->report_len))
            return 1;
    }
    p->meta.motors = 0;
    for (m = 0; m < PB_MOTOR_COUNT; m++) {
        if (p->motors[m].kind == 0)
            continue;
        if (p->rumble_len == 0 || !pb_fits(&p->motors[m], p->rumble_len))
            return 1;
        p->meta.motors = m + 1;
    }
    return 0;
}

int pb_profile_parse(const char *text, size_t len, pb_profile_t **out, size_t *out_error_line)
{
    static pb_line_t l;
    pb_profile_t *p = NULL;
    size_t pos = 0;
    size_t line_no = 0;
    int status;

    if (text == NULL || out == NULL)
        return PB_ERR_ARG;
    if ((status = pb_profile_new(&p)) != PB_OK)
        return status;
    while (pos < len) {
        size_t end = pos;

        while (end < len && text[end] != '\n')
            end++;
        line_no++;
        if (end - pos >= PB_LINE_MAX)
            goto fail;
        memcpy(l.text, text + pos, end - pos);
        l.text[end - pos] = '\0';
        pos = end + 1;
        {
            char copy[PB_LINE_MAX];

            memcpy(copy, l.text, sizeof(copy));
            if (!pb_split(&l))
                goto fail;
            if (l.count > 0 && !pb_parse_line(p, &l, copy))
                goto fail;
        }
    }
    line_no = 0;
    if (pb_check(p) != 0)
        goto fail;
    *out = p;
    return PB_OK;
fail:
    if (out_error_line != NULL)
        *out_error_line = line_no;
    pb_profile_destroy(p);
    return PB_ERR_FORMAT;
}
