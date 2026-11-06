#ifndef PB_FF_H
#define PB_FF_H

#include "../core/pb.h"

typedef struct pb_ff pb_ff_t;

int pb_ff_create(unsigned motors, pb_ff_t **out);
int pb_ff_event(pb_ff_t *ff, const vpad_event_t *event, uint32_t now_ms);
int pb_ff_poll(pb_ff_t *ff, uint32_t now_ms, uint16_t *motors, unsigned count, int *out_changed,
               uint32_t *out_wait_ms);
int pb_ff_stop(pb_ff_t *ff);
void pb_ff_destroy(pb_ff_t *ff);

#endif
