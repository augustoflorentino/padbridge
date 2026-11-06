#ifndef PB_PATHS_H
#define PB_PATHS_H

#include <stddef.h>

typedef int (*pb_dir_fn)(void *ctx, const char *path, const char *name);

int pb_config_dir(char *buf, size_t len);
int pb_runtime_dir(char *buf, size_t len);
int pb_make_dir(const char *path);
int pb_dir_each(const char *dir, pb_dir_fn fn, void *ctx);
int pb_read_file(const char *path, char **out_text, size_t *out_len);
int pb_replace_file(const char *path, const char *text, size_t len);
long pb_process_id(void);
int pb_process_alive(long pid);
int pb_process_signal_stop(long pid);

#endif
