#ifndef PB_OUT_H
#define PB_OUT_H

#include <stddef.h>

#define PB_LOG_ERROR 0
#define PB_LOG_WARN 1
#define PB_LOG_INFO 2
#define PB_LOG_DEBUG 3

void pb_out_init(int script, int log_level);
int pb_out_script(void);
void pb_out_row(const char *kind, size_t count, const char *const *keys, const char *const *values);
void pb_log(int level, const char *fmt, ...);

#endif
