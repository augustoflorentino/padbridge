#include "ff/ff.h"
#include "learn.h"
#include "map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static size_t read_file(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    size_t n;

    if (f == NULL)
        return 0;
    n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

static pb_profile_t *load(const char *path)
{
    static char text[16384];
    pb_profile_t *p = NULL;
    size_t line = 0;
    size_t len = read_file(path, text, sizeof(text));

    CHECK(pb_profile_parse(text, len, &p, &line) == PB_OK);
    return p;
}

static void test_meta(const pb_profile_t *p)
{
    const pb_profile_meta_t *m = pb_profile_meta(p);

    CHECK(strcmp(m->id, "ninja-sword") == 0);
    CHECK(m->out_kind == VPAD_XBOX360);
    CHECK(m->out_vendor == 0x045e && m->out_product == 0x028e);
    CHECK(strcmp(m->out_name, "Microsoft X-Box 360 pad") == 0);
    CHECK(m->motors == 2);
}

static void test_decode(const pb_profile_t *p)
{
    unsigned char rest[10] = {0x01, 0xff, 0xff, 0xff, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00};
    unsigned char moved[10] = {0x01, 0xf7, 0xfe, 0xbe, 0x00, 0xff, 0x80, 0x80, 0xff, 0x00};
    unsigned char other[10] = {0x02};
    pb_state_t s;

    pb_state_neutral(&s);
    CHECK(pb_profile_decode(p, rest, sizeof(rest), &s) == PB_OK);
    CHECK(s.buttons == 0);
    CHECK(s.axes[VPAD_AXIS_LX] == 0 && s.axes[VPAD_AXIS_LT] == 0);
    CHECK(pb_profile_decode(p, moved, sizeof(moved), &s) == PB_OK);
    CHECK(s.buttons == (1u << VPAD_BTN_A | 1u << VPAD_BTN_BACK | 1u << VPAD_BTN_LB | 1u << VPAD_BTN_DPAD_DOWN));
    CHECK(s.axes[VPAD_AXIS_LX] == -32768 && s.axes[VPAD_AXIS_LY] == 32767);
    CHECK(s.axes[VPAD_AXIS_LT] == 255);
    CHECK(pb_profile_decode(p, other, sizeof(other), &s) == PB_ERR_NOT_FOUND);
    CHECK(s.axes[VPAD_AXIS_LT] == 255);
    CHECK(pb_profile_decode(p, rest, 5, &s) == PB_ERR_FORMAT);
}

static void test_rumble(const pb_profile_t *p)
{
    static const unsigned char want[9] = {0x01, 0x80, 0xff, 0, 0, 0, 0, 0, 0};
    uint16_t motors[2] = {0xffff, 0x8000};
    unsigned char buf[16];
    size_t len = 0;

    CHECK(pb_profile_rumble(p, motors, 2, buf, sizeof(buf), &len) == PB_OK);
    CHECK(len == 9 && memcmp(buf, want, 9) == 0);
    CHECK(pb_profile_rumble(p, motors, 2, buf, 4, &len) == PB_ERR_ARG);
}

static void test_roundtrip(const pb_profile_t *p, const char *path)
{
    static char original[16384];
    static char written[16384];
    size_t len = read_file(path, original, sizeof(original));
    FILE *f = tmpfile();
    size_t n;

    CHECK(f != NULL && pb_profile_write(p, f) == PB_OK);
    rewind(f);
    n = fread(written, 1, sizeof(written) - 1, f);
    written[n] = '\0';
    fclose(f);
    CHECK(n == len && strcmp(original, written) == 0);
}

static void test_bad_lines(void)
{
    static const char *const bad[] = {
        "profile x\nreport 1 4\nbutton z 1.0\n",
        "profile x\nreport 1 4\naxis lx 3.0 16\n",
        "profile x\nreport 1 4\nbutton a 1.8\n",
        "profile x\nreport 1 4\nmotor strong 0.0 8\n",
        "report 1 4\n",
    };
    static const size_t lines[] = {3, 0, 3, 0, 0};
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        pb_profile_t *p = NULL;
        size_t line = 99;

        CHECK(pb_profile_parse(bad[i], strlen(bad[i]), &p, &line) == PB_ERR_FORMAT && p == NULL);
        CHECK(line == lines[i]);
    }
}

static void test_descriptor(void)
{
    static const unsigned char desc[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, 0x95, 0x04, 0x81, 0x03,
        0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
        0x75, 0x04, 0x81, 0x03, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08,
        0x95, 0x02, 0x81, 0x02, 0xc0,
    };
    unsigned char report[5] = {0x01, 0x09, 0x02, 0x81, 0x7f};
    pb_profile_t *p = NULL;
    pb_state_t s;

    CHECK(pb_profile_from_descriptor(desc, sizeof(desc), &p) == PB_OK);
    if (p == NULL)
        return;
    pb_state_neutral(&s);
    CHECK(pb_profile_decode(p, report, sizeof(report), &s) == PB_OK);
    CHECK(s.buttons == (1u << VPAD_BTN_A | 1u << VPAD_BTN_X | 1u << VPAD_BTN_DPAD_RIGHT));
    CHECK(s.axes[VPAD_AXIS_LX] == -32768 && s.axes[VPAD_AXIS_LY] == 32767);
    pb_profile_destroy(p);
}

static int hex_report(const char *hex, unsigned char *out, size_t *len)
{
    size_t n = 0;
    unsigned v;

    while (hex[0] != '\0' && hex[0] != '\n' && sscanf(hex, "%2x", &v) == 1) {
        out[n++] = (unsigned char)v;
        hex += 2;
    }
    *len = n;
    return n > 0;
}

static void learn_feed(pb_learn_t *l, const char *path)
{
    static char line[4096];
    FILE *f = fopen(path, "r");
    int step = 0;
    uint32_t now = 0;

    CHECK(f != NULL);
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        unsigned char report[1024];
        size_t len;
        int label;
        char *hex;

        if (line[0] == '#' || (label = (int)strtol(line, &hex, 10)) < step || !hex_report(hex + 1, report, &len))
            continue;
        while (step < label && pb_learn_next(l) == PB_OK)
            step++;
        now += 10;
        CHECK(pb_learn_add(l, report, len, now) == PB_OK);
    }
    if (f != NULL)
        fclose(f);
}

static void compare_states(const pb_profile_t *a, const pb_profile_t *b, const char *path)
{
    static char line[4096];
    FILE *f = fopen(path, "r");
    size_t mismatches = 0;
    unsigned i;

    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        unsigned char report[1024];
        size_t len;
        pb_state_t sa;
        pb_state_t sb;

        if (line[0] == '#' || !hex_report(strchr(line, ' ') + 1, report, &len))
            continue;
        pb_state_neutral(&sa);
        pb_state_neutral(&sb);
        pb_profile_decode(a, report, len, &sa);
        pb_profile_decode(b, report, len, &sb);
        mismatches += sa.buttons != sb.buttons;
        for (i = 0; i < VPAD_AXIS_COUNT; i++)
            mismatches += abs(sa.axes[i] - sb.axes[i]) > (i < VPAD_AXIS_LT ? 1024 : 8);
    }
    if (f != NULL)
        fclose(f);
    CHECK(mismatches == 0);
}

static void test_learn(const pb_profile_t *ninja, const char *samples)
{
    pb_learn_t *l = NULL;
    pb_profile_t *p = NULL;
    char prompt[160];

    CHECK(pb_learn_create(NULL, 0, &l) == PB_OK);
    CHECK(pb_learn_prompt(l, prompt, sizeof(prompt)) == PB_OK);
    learn_feed(l, samples);
    CHECK(pb_learn_finish(l, &p) == PB_OK);
    pb_learn_destroy(l);
    if (p == NULL)
        return;
    compare_states(ninja, p, samples);
    pb_profile_destroy(p);
}

static void test_ff(void)
{
    pb_ff_t *ff = NULL;
    vpad_event_t ev;
    uint16_t motors[2] = {0, 0};
    uint32_t wait = 0;
    int changed = 0;

    CHECK(pb_ff_create(2, &ff) == PB_OK);
    memset(&ev, 0, sizeof(ev));
    ev.kind = VPAD_EV_UPLOAD;
    ev.effect.size = sizeof(ev.effect);
    ev.effect.type = VPAD_RUMBLE;
    ev.effect.strong = 0xffff;
    ev.effect.weak = 0x4000;
    ev.effect.length_ms = 100;
    CHECK(pb_ff_event(ff, &ev, 0) == PB_OK);
    ev.kind = VPAD_EV_PLAY;
    ev.value = 1;
    CHECK(pb_ff_event(ff, &ev, 0) == PB_OK);
    CHECK(pb_ff_poll(ff, 10, motors, 2, &changed, &wait) == PB_OK);
    CHECK(changed && motors[0] == 0xffff && motors[1] == 0x4000);
    CHECK(pb_ff_poll(ff, 200, motors, 2, &changed, &wait) == PB_OK);
    CHECK(changed && motors[0] == 0 && motors[1] == 0);
    CHECK(pb_ff_stop(ff) == PB_OK);
    pb_ff_destroy(ff);
}

int main(int argc, char **argv)
{
    pb_profile_t *p;

    if (argc != 3)
        return 2;
    p = load(argv[1]);
    if (p == NULL)
        return 1;
    test_meta(p);
    test_decode(p);
    test_rumble(p);
    test_roundtrip(p, argv[1]);
    test_bad_lines();
    test_descriptor();
    test_learn(p, argv[2]);
    test_ff();
    pb_profile_destroy(p);
    if (failures == 0)
        puts("ok");
    return failures != 0;
}
