#include "ff.h"

int pb_ff_create(unsigned motors, pb_ff_t **out)
{
    (void)motors;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_ff_event(pb_ff_t *ff, const vpad_event_t *event, uint32_t now_ms)
{
    (void)ff;
    (void)event;
    (void)now_ms;
    return PB_ERR_UNSUPPORTED;
}

int pb_ff_poll(pb_ff_t *ff, uint32_t now_ms, uint16_t *motors, unsigned count, int *out_changed,
               uint32_t *out_wait_ms)
{
    (void)ff;
    (void)now_ms;
    (void)motors;
    (void)count;
    (void)out_changed;
    (void)out_wait_ms;
    return PB_ERR_UNSUPPORTED;
}

int pb_ff_stop(pb_ff_t *ff)
{
    (void)ff;
    return PB_ERR_UNSUPPORTED;
}

void pb_ff_destroy(pb_ff_t *ff)
{
    (void)ff;
}
