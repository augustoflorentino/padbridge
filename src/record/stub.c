#include "record.h"

int pb_record_open(const char *path, const char *device_name, pb_record_t **out)
{
    (void)path;
    (void)device_name;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_record_descriptor(pb_record_t *rec, const unsigned char *desc, size_t len)
{
    (void)rec;
    (void)desc;
    (void)len;
    return PB_ERR_UNSUPPORTED;
}

int pb_record_packet(pb_record_t *rec, unsigned dir, const unsigned char *buf, size_t len, uint64_t ts_ns)
{
    (void)rec;
    (void)dir;
    (void)buf;
    (void)len;
    (void)ts_ns;
    return PB_ERR_UNSUPPORTED;
}

void pb_record_close(pb_record_t *rec)
{
    (void)rec;
}
