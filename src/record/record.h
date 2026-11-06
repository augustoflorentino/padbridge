#ifndef PB_RECORD_H
#define PB_RECORD_H

#include "../core/pb.h"

#define PB_DIR_IN 0
#define PB_DIR_OUT 1

typedef struct pb_record pb_record_t;

int pb_record_open(const char *path, const char *device_name, pb_record_t **out);
int pb_record_descriptor(pb_record_t *rec, const unsigned char *desc, size_t len);
int pb_record_packet(pb_record_t *rec, unsigned dir, const unsigned char *buf, size_t len, uint64_t ts_ns);
void pb_record_close(pb_record_t *rec);

#endif
