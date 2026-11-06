#include "conf.h"

int pb_conf_load(const char *path, pb_conf_t *out)
{
    (void)path;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_conf_path(char *buf, size_t len)
{
    (void)buf;
    (void)len;
    return PB_ERR_UNSUPPORTED;
}

int pb_profile_find(const pb_conf_t *conf, const char *id, pb_profile_t **out)
{
    (void)conf;
    (void)id;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_profile_match(const pb_conf_t *conf, uint16_t vendor, uint16_t product, const char *name,
                     const char *address, pb_profile_t **out)
{
    (void)conf;
    (void)vendor;
    (void)product;
    (void)name;
    (void)address;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_profile_each(const pb_conf_t *conf, int (*fn)(void *ctx, const pb_profile_t *profile, const char *origin),
                    void *ctx)
{
    (void)conf;
    (void)fn;
    (void)ctx;
    return PB_ERR_UNSUPPORTED;
}

int pb_state_write(const pb_conf_t *conf, const char *controller, const char *status, const char *detail)
{
    (void)conf;
    (void)controller;
    (void)status;
    (void)detail;
    return PB_ERR_UNSUPPORTED;
}
