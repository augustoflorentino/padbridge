#ifndef PB_SINK_INTERNAL_H
#define PB_SINK_INTERNAL_H

#include "sink.h"

typedef struct pb_sink_ops {
    int (*send)(pb_sink_t *sink, const pb_state_t *state);
    int (*next_ff)(pb_sink_t *sink, vpad_event_t *out_event, int timeout_ms);
    int (*wake)(pb_sink_t *sink);
    void (*close)(pb_sink_t *sink);
} pb_sink_ops_t;

struct pb_sink {
    const pb_sink_ops_t *ops;
};

#endif
