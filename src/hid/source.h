#ifndef PB_SOURCE_H
#define PB_SOURCE_H

#include "../core/pb.h"

#define PB_BUS_UNKNOWN 0
#define PB_BUS_USB 1
#define PB_BUS_BLUETOOTH 2
#define PB_BUS_REPLAY 3

typedef struct pb_source_info {
    char uri[288];
    char name[128];
    uint16_t vendor;
    uint16_t product;
    unsigned bus;
} pb_source_info_t;

typedef struct pb_source pb_source_t;

typedef struct pb_source_ops {
    int (*read)(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms);
    int (*write)(pb_source_t *src, const unsigned char *buf, size_t len);
    int (*descriptor)(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len);
    void (*close)(pb_source_t *src);
} pb_source_ops_t;

struct pb_source {
    const pb_source_ops_t *ops;
    pb_source_info_t info;
};

typedef int (*pb_source_list_fn)(void *ctx, const pb_source_info_t *info);

int pb_source_open(const char *uri, pb_source_t **out);
int pb_source_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms);
int pb_source_write(pb_source_t *src, const unsigned char *buf, size_t len);
int pb_source_descriptor(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len);
const pb_source_info_t *pb_source_info(const pb_source_t *src);
void pb_source_close(pb_source_t *src);

int pb_hid_open(const char *path, pb_source_t **out);
int pb_hid_list(pb_source_list_fn fn, void *ctx);
const char *pb_hid_backend(void);
int pb_replay_open(const char *path, pb_source_t **out);
int pb_l2cap_open(const char *address, pb_source_t **out);

#endif
