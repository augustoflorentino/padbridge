#include "learn.h"
#include "profile_int.h"

#include <report-guess/report-guess.h>
#include <hid-parser/hid-parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PB_LEARN_SAMPLES 8192
#define PB_LEARN_GAP_MS 8

typedef struct pb_learn_step {
    unsigned kind;
    unsigned target;
    const char *text;
} pb_learn_step_t;

static const pb_learn_step_t pb_steps[] = {
    {0, 0, "leave the controller untouched"},
    {PB_FIELD_BUTTON, VPAD_BTN_A, "press and release A a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_B, "press and release B a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_X, "press and release X a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_Y, "press and release Y a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_LB, "press and release the left bumper a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_RB, "press and release the right bumper a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_BACK, "press and release Back/Select a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_START, "press and release Start a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_LSTICK, "click the left stick a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_RSTICK, "click the right stick a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_DPAD_UP, "press and release d-pad up a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_DPAD_DOWN, "press and release d-pad down a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_DPAD_RIGHT, "press and release d-pad right a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_DPAD_LEFT, "press and release d-pad left a few times"},
    {PB_FIELD_AXIS, VPAD_AXIS_LX, "move the left stick fully left and right"},
    {PB_FIELD_AXIS, VPAD_AXIS_LY, "move the left stick fully up and down"},
    {PB_FIELD_AXIS, VPAD_AXIS_RX, "move the right stick fully left and right"},
    {PB_FIELD_AXIS, VPAD_AXIS_RY, "move the right stick fully up and down"},
    {PB_FIELD_AXIS, VPAD_AXIS_LT, "squeeze the left trigger fully a few times"},
    {PB_FIELD_AXIS, VPAD_AXIS_RT, "squeeze the right trigger fully a few times"},
    {PB_FIELD_BUTTON, VPAD_BTN_GUIDE, "press the guide/home button a few times, or wait if there is none"},
};

#define PB_STEP_COUNT (sizeof(pb_steps) / sizeof(pb_steps[0]))

struct pb_learn {
    infr_session_t *session;
    int report_ids;
    size_t step;
    unsigned char last[PB_REPORT_MAX];
    size_t last_len;
    uint32_t last_ms;
};

static int pb_desc_has_ids(const unsigned char *desc, size_t len)
{
    hidp_desc_t *d = NULL;
    size_t error_offset = 0;
    size_t i;
    int ids = 0;

    if (desc == NULL || len == 0 || hidp_desc_parse(desc, len, NULL, &d, &error_offset) != HIDP_OK)
        return -1;
    for (i = 0; i < hidp_desc_report_count(d); i++)
        ids |= hidp_desc_report(d, i)->id != 0;
    hidp_desc_destroy(d);
    return ids;
}

int pb_learn_create(const unsigned char *desc, size_t desc_len, pb_learn_t **out)
{
    pb_learn_t *l;

    if (out == NULL)
        return PB_ERR_ARG;
    l = calloc(1, sizeof(*l));
    if (l == NULL)
        return PB_ERR_NOMEM;
    l->report_ids = pb_desc_has_ids(desc, desc_len);
    *out = l;
    return PB_OK;
}

int pb_learn_prompt(pb_learn_t *learn, char *buf, size_t len)
{
    if (learn == NULL || buf == NULL || len == 0)
        return PB_ERR_ARG;
    if (learn->step >= PB_STEP_COUNT)
        return PB_LEARN_DONE;
    snprintf(buf, len, "%s", pb_steps[learn->step].text);
    return PB_OK;
}

static int pb_learn_open(pb_learn_t *l, const unsigned char *report, size_t len)
{
    infr_options_t options;

    memset(&options, 0, sizeof(options));
    if (l->report_ids < 0)
        l->report_ids = len > 1 && report[0] != 0 && report[0] < 0x40;
    options.flags = l->report_ids ? INFR_REPORT_IDS : 0;
    if (infr_create(l->report_ids ? PB_REPORT_MAX : len, PB_LEARN_SAMPLES, &options, &l->session) != INFR_OK)
        return PB_ERR_NOMEM;
    return PB_OK;
}

static int pb_learn_repeat(const pb_learn_t *l, const unsigned char *report, size_t len, uint32_t now_ms)
{
    return len == l->last_len && now_ms - l->last_ms < PB_LEARN_GAP_MS && memcmp(report, l->last, len) == 0;
}

int pb_learn_add(pb_learn_t *learn, const unsigned char *report, size_t len, uint32_t now_ms)
{
    int status;

    if (learn == NULL || report == NULL || len == 0 || len > PB_REPORT_MAX)
        return PB_ERR_ARG;
    if (learn->step >= PB_STEP_COUNT)
        return PB_LEARN_DONE;
    if (learn->session == NULL && (status = pb_learn_open(learn, report, len)) != PB_OK)
        return status;
    if (pb_learn_repeat(learn, report, len, now_ms))
        return PB_OK;
    memcpy(learn->last, report, len);
    learn->last_len = len;
    learn->last_ms = now_ms;
    status = infr_add(learn->session, (int)learn->step, report, len);
    if (status == INFR_ERR_FULL)
        return PB_ERR_BUSY;
    return status == INFR_OK || status == INFR_ERR_ARG ? PB_OK : PB_ERR_FORMAT;
}

int pb_learn_next(pb_learn_t *learn)
{
    if (learn == NULL)
        return PB_ERR_ARG;
    if (learn->step < PB_STEP_COUNT)
        learn->step++;
    return learn->step >= PB_STEP_COUNT ? PB_LEARN_DONE : PB_OK;
}

static unsigned pb_best_id(const infr_model_t *m)
{
    size_t votes[256] = {0};
    unsigned best = 0;
    size_t i;

    for (i = 0; i < infr_field_count(m); i++) {
        const infr_field_t *f = infr_field(m, i);

        if (f->label > 0 && f->report_id < 256)
            votes[f->report_id]++;
    }
    for (i = 0; i < 256; i++) {
        if (votes[i] > votes[best])
            best = (unsigned)i;
    }
    return best;
}

static size_t pb_model_len(const infr_model_t *m, unsigned id)
{
    size_t i;

    for (i = 0; i < infr_model_report_count(m); i++) {
        unsigned rid;
        size_t len;

        if (infr_model_report_info(m, i, &rid, &len) == INFR_OK && rid == id)
            return len;
    }
    return 0;
}

static unsigned pb_flags_of(const infr_field_t *f)
{
    unsigned flags = 0;

    if (f->flags & INFR_SIGNED)
        flags |= PB_FIELD_SIGNED;
    if ((f->flags & INFR_BIG_ENDIAN) && f->bit_size % 8 == 0 && f->bit_size > 8)
        flags |= PB_FIELD_BIG;
    return flags;
}

static int pb_learn_field(const infr_field_t *in, pb_field_t *f)
{
    const pb_learn_step_t *step;

    if (in->label <= 0 || (size_t)in->label >= PB_STEP_COUNT || in->bit_size == 0 || in->bit_size > 32)
        return 0;
    step = &pb_steps[in->label];
    memset(f, 0, sizeof(*f));
    f->bit = (unsigned)in->bit_offset;
    f->size = in->bit_size;
    f->flags = pb_flags_of(in);
    f->target = step->target;
    f->min = in->logical_min;
    f->max = in->logical_max;
    if (in->kind == INFR_HAT) {
        f->kind = PB_FIELD_HAT;
        f->max = f->min + 7;
        return 1;
    }
    if (in->kind == INFR_BUTTON && step->kind == PB_FIELD_BUTTON) {
        f->kind = PB_FIELD_BUTTON;
        f->size = 1;
        f->min = 0;
        f->max = 1;
        f->flags = (in->flags & INFR_ACTIVE_LOW) ? PB_FIELD_INVERT : 0;
        return in->bit_size == 1;
    }
    if (in->kind != INFR_AXIS || step->kind != PB_FIELD_AXIS || f->min >= f->max)
        return 0;
    f->kind = PB_FIELD_AXIS;
    if (step->target < VPAD_AXIS_LT && in->rest > f->min && in->rest < f->max) {
        f->flags |= PB_FIELD_CENTER;
        f->center = in->rest;
    }
    if (step->target >= VPAD_AXIS_LT && in->rest == f->max)
        f->flags |= PB_FIELD_INVERT;
    return 1;
}

static int pb_learn_build(const infr_model_t *m, pb_profile_t **out)
{
    unsigned id = pb_best_id(m);
    int hat = 0;
    pb_profile_t *p;
    size_t i;
    int status;

    if ((status = pb_profile_new(&p)) != PB_OK)
        return status;
    strcpy(p->meta.id, "learned");
    strcpy(p->meta.name, "Learned profile");
    p->report_id = id;
    p->report_len = pb_model_len(m, id);
    for (i = 0; i < infr_field_count(m); i++) {
        const infr_field_t *in = infr_field(m, i);
        pb_field_t f;

        if (in->report_id != id || !pb_learn_field(in, &f) || (f.kind == PB_FIELD_HAT && hat++))
            continue;
        pb_profile_add(p, &f);
    }
    if (p->field_count == 0 || p->report_len == 0) {
        pb_profile_destroy(p);
        return PB_ERR_NOT_FOUND;
    }
    *out = p;
    return PB_OK;
}

int pb_learn_finish(pb_learn_t *learn, pb_profile_t **out)
{
    infr_model_t *model = NULL;
    int status;

    if (learn == NULL || out == NULL)
        return PB_ERR_ARG;
    if (learn->session == NULL)
        return PB_ERR_NOT_FOUND;
    status = infr_analyze(learn->session, &model);
    if (status == INFR_ERR_SAMPLES)
        return PB_ERR_NOT_FOUND;
    if (status != INFR_OK)
        return status == INFR_ERR_NOMEM ? PB_ERR_NOMEM : PB_ERR_FORMAT;
    status = pb_learn_build(model, out);
    infr_model_destroy(model);
    return status;
}

void pb_learn_destroy(pb_learn_t *learn)
{
    if (learn == NULL)
        return;
    infr_destroy(learn->session);
    free(learn);
}
