#ifndef PB_SYS_H
#define PB_SYS_H

#include <stdint.h>

typedef struct pb_thread pb_thread_t;
typedef struct pb_mutex pb_mutex_t;

int pb_thread_start(pb_thread_t **out, int (*fn)(void *ctx), void *ctx);
int pb_thread_join(pb_thread_t *thread);
int pb_mutex_create(pb_mutex_t **out);
void pb_mutex_lock(pb_mutex_t *mutex);
void pb_mutex_unlock(pb_mutex_t *mutex);
void pb_mutex_destroy(pb_mutex_t *mutex);
uint32_t pb_now_ms(void);
uint64_t pb_now_ns(void);
void pb_sleep_ms(uint32_t ms);
int pb_interrupted(void);
void pb_catch_interrupt(void);

#endif
