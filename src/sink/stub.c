#include "sink.h"

int pb_sink_open_vpad(const pb_sink_config_t *config, pb_sink_t **out)
{
    (void)config;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_sink_open_text(FILE *stream, pb_sink_t **out)
{
    (void)stream;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_sink_send(pb_sink_t *sink, const pb_state_t *state)
{
    (void)sink;
    (void)state;
    return PB_ERR_UNSUPPORTED;
}

int pb_sink_next_ff(pb_sink_t *sink, vpad_event_t *out_event, int timeout_ms)
{
    (void)sink;
    (void)out_event;
    (void)timeout_ms;
    return PB_ERR_UNSUPPORTED;
}

int pb_sink_wake(pb_sink_t *sink)
{
    (void)sink;
    return PB_ERR_UNSUPPORTED;
}

void pb_sink_close(pb_sink_t *sink)
{
    (void)sink;
}
