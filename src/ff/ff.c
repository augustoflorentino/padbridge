#include "ff.h"

#include <padff/padff.h>
#include <stdlib.h>
#include <string.h>

#define PB_FF_SLOTS 16
#define PB_FF_INTERVAL_MS 10

struct pb_ff {
    pff_mixer_t *mixer;
    unsigned motors;
    uint16_t last[PB_MOTOR_COUNT];
    union {
        uint64_t align;
        unsigned char mem[PFF_MIXER_SIZE(PB_FF_SLOTS)];
    } store;
};

static void pb_ff_config(unsigned motors, pff_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->size = sizeof(*c);
    c->mix = PFF_MIX_MAX;
    c->motor_max = 65535;
    c->interval_ms = PB_FF_INTERVAL_MS;
    c->motors = motors;
    c->trigger_max = 65535;
}

int pb_ff_create(unsigned motors, pb_ff_t **out)
{
    pff_config_t config;
    pb_ff_t *ff;

    if (out == NULL || motors == 0 || motors > PB_MOTOR_COUNT)
        return PB_ERR_ARG;
    ff = calloc(1, sizeof(*ff));
    if (ff == NULL)
        return PB_ERR_NOMEM;
    pb_ff_config(motors, &config);
    if (pff_mixer_init(ff->store.mem, sizeof(ff->store.mem), PB_FF_SLOTS, &config, &ff->mixer) != PFF_OK) {
        free(ff);
        return PB_ERR_ARG;
    }
    ff->motors = motors;
    *out = ff;
    return PB_OK;
}

static int pb_ff_status(int status)
{
    if (status == PFF_OK)
        return PB_OK;
    return status == PFF_ERR_SLOT ? PB_ERR_NOT_FOUND : PB_ERR_ARG;
}

static int pb_ff_upload(pb_ff_t *ff, const vpad_event_t *event, uint32_t now_ms)
{
    pff_effect_t effect;
    size_t size = event->effect.size;

    memset(&effect, 0, sizeof(effect));
    if (size < 24 || size > sizeof(effect))
        size = size < 24 ? 0 : sizeof(effect);
    if (size == 0)
        return PB_ERR_ARG;
    memcpy(&effect, &event->effect, size < sizeof(event->effect) ? size : sizeof(event->effect));
    effect.size = 24;
    return pb_ff_status(pff_mixer_upload(ff->mixer, event->slot, &effect, now_ms));
}

int pb_ff_event(pb_ff_t *ff, const vpad_event_t *event, uint32_t now_ms)
{
    if (ff == NULL || event == NULL)
        return PB_ERR_ARG;
    switch (event->kind) {
    case VPAD_EV_UPLOAD:
        return pb_ff_upload(ff, event, now_ms);
    case VPAD_EV_ERASE:
        return pb_ff_status(pff_mixer_erase(ff->mixer, event->slot));
    case VPAD_EV_PLAY:
        return pb_ff_status(pff_mixer_play(ff->mixer, event->slot, event->value, now_ms));
    case VPAD_EV_GAIN:
        return pb_ff_status(pff_mixer_set_gain(ff->mixer, (uint16_t)(event->value > 65535 ? 65535 : event->value)));
    default:
        return PB_OK;
    }
}

int pb_ff_poll(pb_ff_t *ff, uint32_t now_ms, uint16_t *motors, unsigned count, int *out_changed,
               uint32_t *out_wait_ms)
{
    uint16_t now[PFF_MOTORS];
    int send = 0;
    unsigned i;

    if (ff == NULL || motors == NULL || out_changed == NULL || out_wait_ms == NULL)
        return PB_ERR_ARG;
    if (pff_mixer_poll(ff->mixer, now_ms, now, PFF_MOTORS, &send) != PFF_OK
        || pff_mixer_next(ff->mixer, now_ms, out_wait_ms) != PFF_OK)
        return PB_ERR_ARG;
    for (i = 0; i < count; i++)
        motors[i] = i < ff->motors ? now[i] : 0;
    *out_changed = send;
    return PB_OK;
}

int pb_ff_stop(pb_ff_t *ff)
{
    if (ff == NULL)
        return PB_ERR_ARG;
    return pb_ff_status(pff_mixer_stop_all(ff->mixer));
}

void pb_ff_destroy(pb_ff_t *ff)
{
    free(ff);
}
