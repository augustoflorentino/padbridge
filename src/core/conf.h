#ifndef PB_CONF_H
#define PB_CONF_H

#include "../map/map.h"

typedef struct pb_conf {
    char profiles_dir[512];
    char state_path[512];
    char hook[512];
    int reconnect_s;
    int log_level;
} pb_conf_t;

int pb_conf_load(const char *path, pb_conf_t *out);
int pb_conf_path(char *buf, size_t len);
int pb_profile_find(const pb_conf_t *conf, const char *id, pb_profile_t **out);
int pb_profile_match(const pb_conf_t *conf, uint16_t vendor, uint16_t product, const char *name,
                     const char *address, pb_profile_t **out);
int pb_profile_each(const pb_conf_t *conf, int (*fn)(void *ctx, const pb_profile_t *profile, const char *origin),
                    void *ctx);
int pb_state_write(const pb_conf_t *conf, const char *controller, const char *status, const char *detail);

#endif
