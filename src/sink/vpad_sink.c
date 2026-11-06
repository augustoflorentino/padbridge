#include <stdlib.h>
#include "sink_internal.h"

typedef struct pb_vpad_sink {
    pb_sink_t base;
    vpad_t *pad;
    int no_ff;
} pb_vpad_sink_t;

static int pb_from_vpad(int status)
{
    switch (status) {
    case VPAD_OK:
        return PB_OK;
    case VPAD_ERR_NOMEM:
        return PB_ERR_NOMEM;
    case VPAD_ERR_UNSUPPORTED:
        return PB_ERR_UNSUPPORTED;
    case VPAD_ERR_NODEVICE:
        return PB_ERR_NOT_FOUND;
    case VPAD_ERR_ACCESS:
        return PB_ERR_ACCESS;
    case VPAD_ERR_IO:
        return PB_ERR_IO;
    case VPAD_ERR_AGAIN:
        return PB_ERR_TIMEOUT;
    default:
        return PB_ERR_ARG;
    }
}

static int pb_vpad_send(pb_sink_t *sink, const pb_state_t *state)
{
    vpad_t *pad = ((pb_vpad_sink_t *)sink)->pad;
    int status = VPAD_OK;
    int i;

    for (i = 0; i < VPAD_BTN_COUNT && status == VPAD_OK; i++)
        status = vpad_set_button(pad, i, (state->buttons >> i) & 1u);
    for (i = 0; i < VPAD_AXIS_COUNT && status == VPAD_OK; i++)
        status = vpad_set_axis(pad, i, state->axes[i]);
    if (status == VPAD_OK)
        status = vpad_send(pad);
    return pb_from_vpad(status);
}

static int pb_vpad_next_ff(pb_sink_t *sink, vpad_event_t *out_event, int timeout_ms)
{
    pb_vpad_sink_t *self = (pb_vpad_sink_t *)sink;
    int status;

    if (self->no_ff)
        return PB_ERR_UNSUPPORTED;
    status = vpad_wait(self->pad, timeout_ms);
    if (status != VPAD_OK && status != VPAD_ERR_AGAIN)
        return pb_from_vpad(status);
    return pb_from_vpad(vpad_next_event(self->pad, out_event, sizeof(*out_event)));
}

static int pb_vpad_wake(pb_sink_t *sink)
{
    return pb_from_vpad(vpad_wake(((pb_vpad_sink_t *)sink)->pad));
}

static void pb_vpad_close(pb_sink_t *sink)
{
    vpad_destroy(((pb_vpad_sink_t *)sink)->pad);
    free(sink);
}

static const pb_sink_ops_t pb_vpad_ops = {pb_vpad_send, pb_vpad_next_ff, pb_vpad_wake, pb_vpad_close};

int pb_sink_open_vpad(const pb_sink_config_t *config, pb_sink_t **out)
{
    pb_vpad_sink_t *self;
    vpad_config_t vc;
    int status;

    if (config == NULL || out == NULL)
        return PB_ERR_ARG;
    self = calloc(1, sizeof(*self));
    if (self == NULL)
        return PB_ERR_NOMEM;
    vpad_config_init(&vc, config->kind);
    if (config->vendor != 0 || config->product != 0) {
        vc.vendor = config->vendor;
        vc.product = config->product;
    }
    if (config->name != NULL && config->name[0] != '\0')
        vc.name = config->name;
    if (config->no_ff)
        vc.flags |= VPAD_NO_FF;
    status = vpad_create(&vc, &self->pad);
    if (status != VPAD_OK) {
        free(self);
        return pb_from_vpad(status);
    }
    self->base.ops = &pb_vpad_ops;
    self->no_ff = config->no_ff;
    *out = &self->base;
    return PB_OK;
}
