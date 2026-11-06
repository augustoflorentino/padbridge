#include <string.h>
#include "source.h"

static int pb_has_scheme(const char *uri, const char *scheme, const char **rest)
{
    size_t n = strlen(scheme);

    if (strncmp(uri, scheme, n) != 0 || uri[n] != ':')
        return 0;
    *rest = uri + n + 1;
    return 1;
}

int pb_source_open(const char *uri, pb_source_t **out)
{
    const char *rest;

    if (uri == NULL || out == NULL)
        return PB_ERR_ARG;
    if (pb_has_scheme(uri, "hid", &rest))
        return pb_hid_open(rest, out);
    if (pb_has_scheme(uri, "bt", &rest))
        return pb_l2cap_open(rest, out);
    if (pb_has_scheme(uri, "replay", &rest))
        return pb_replay_open(rest, out);
    return PB_ERR_ARG;
}

int pb_source_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms)
{
    if (src == NULL || buf == NULL || out_len == NULL)
        return PB_ERR_ARG;
    return src->ops->read(src, buf, len, out_len, timeout_ms);
}

int pb_source_write(pb_source_t *src, const unsigned char *buf, size_t len)
{
    if (src == NULL || buf == NULL || len == 0)
        return PB_ERR_ARG;
    if (src->ops->write == NULL)
        return PB_ERR_UNSUPPORTED;
    return src->ops->write(src, buf, len);
}

int pb_source_descriptor(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len)
{
    if (src == NULL || buf == NULL || out_len == NULL)
        return PB_ERR_ARG;
    if (src->ops->descriptor == NULL)
        return PB_ERR_NOT_FOUND;
    return src->ops->descriptor(src, buf, len, out_len);
}

const pb_source_info_t *pb_source_info(const pb_source_t *src)
{
    return src == NULL ? NULL : &src->info;
}

void pb_source_close(pb_source_t *src)
{
    if (src != NULL)
        src->ops->close(src);
}
