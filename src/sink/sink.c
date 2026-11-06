#include "sink_internal.h"

int pb_sink_send(pb_sink_t *sink, const pb_state_t *state)
{
    if (sink == NULL || state == NULL)
        return PB_ERR_ARG;
    return sink->ops->send(sink, state);
}

int pb_sink_next_ff(pb_sink_t *sink, vpad_event_t *out_event, int timeout_ms)
{
    if (sink == NULL || out_event == NULL || timeout_ms < -1)
        return PB_ERR_ARG;
    return sink->ops->next_ff(sink, out_event, timeout_ms);
}

int pb_sink_wake(pb_sink_t *sink)
{
    if (sink == NULL)
        return PB_ERR_ARG;
    return sink->ops->wake(sink);
}

void pb_sink_close(pb_sink_t *sink)
{
    if (sink != NULL)
        sink->ops->close(sink);
}
