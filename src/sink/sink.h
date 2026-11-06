#ifndef PB_SINK_H
#define PB_SINK_H

#include <stdio.h>
#include "../core/pb.h"

typedef struct pb_sink_config {
    int kind;
    uint16_t vendor;
    uint16_t product;
    const char *name;
    int no_ff;
} pb_sink_config_t;

typedef struct pb_sink pb_sink_t;

int pb_sink_open_vpad(const pb_sink_config_t *config, pb_sink_t **out);
int pb_sink_open_text(FILE *stream, pb_sink_t **out);
int pb_sink_send(pb_sink_t *sink, const pb_state_t *state);
int pb_sink_next_ff(pb_sink_t *sink, vpad_event_t *out_event, int timeout_ms);
int pb_sink_wake(pb_sink_t *sink);
void pb_sink_close(pb_sink_t *sink);

#endif
