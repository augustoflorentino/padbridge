#include <stdlib.h>
#include <string.h>
#include "../core/out.h"
#include "sink_internal.h"

typedef struct pb_text_sink {
    pb_sink_t base;
    FILE *stream;
    pb_state_t last;
    int sent;
} pb_text_sink_t;

static const char *const pb_button_names[VPAD_BTN_COUNT] = {
    "a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "ls", "rs", "up", "down", "left", "right",
};

static const char *const pb_axis_names[VPAD_AXIS_COUNT] = {"lx", "ly", "rx", "ry", "lt", "rt"};

static void pb_text_buttons(FILE *stream, uint32_t buttons)
{
    int first = 1;
    int i;

    for (i = 0; i < VPAD_BTN_COUNT; i++) {
        if (!((buttons >> i) & 1u))
            continue;
        fprintf(stream, "%s%s", first ? "" : ",", pb_button_names[i]);
        first = 0;
    }
    if (first)
        fputc('-', stream);
}

static int pb_text_send(pb_sink_t *sink, const pb_state_t *state)
{
    pb_text_sink_t *self = (pb_text_sink_t *)sink;
    const char *sep = pb_out_script() ? "\t" : "  ";
    int i;

    if (self->sent && memcmp(&self->last, state, sizeof(*state)) == 0)
        return PB_OK;
    fprintf(self->stream, "state%sbuttons=", sep);
    pb_text_buttons(self->stream, state->buttons);
    for (i = 0; i < VPAD_AXIS_COUNT; i++)
        fprintf(self->stream, "%s%s=%ld", sep, pb_axis_names[i], (long)state->axes[i]);
    fputc('\n', self->stream);
    self->last = *state;
    self->sent = 1;
    return fflush(self->stream) == 0 ? PB_OK : PB_ERR_IO;
}

static int pb_text_next_ff(pb_sink_t *sink, vpad_event_t *out_event, int timeout_ms)
{
    (void)sink;
    (void)out_event;
    (void)timeout_ms;
    return PB_ERR_UNSUPPORTED;
}

static int pb_text_wake(pb_sink_t *sink)
{
    (void)sink;
    return PB_OK;
}

static void pb_text_close(pb_sink_t *sink)
{
    free(sink);
}

static const pb_sink_ops_t pb_text_ops = {pb_text_send, pb_text_next_ff, pb_text_wake, pb_text_close};

int pb_sink_open_text(FILE *stream, pb_sink_t **out)
{
    pb_text_sink_t *self;

    if (stream == NULL || out == NULL)
        return PB_ERR_ARG;
    self = calloc(1, sizeof(*self));
    if (self == NULL)
        return PB_ERR_NOMEM;
    self->base.ops = &pb_text_ops;
    self->stream = stream;
    *out = &self->base;
    return PB_OK;
}
