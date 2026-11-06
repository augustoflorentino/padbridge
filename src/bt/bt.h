#ifndef PB_BT_H
#define PB_BT_H

#include "../hid/source.h"

typedef struct pb_bt_device {
    char address[18];
    char name[128];
    uint32_t device_class;
    int paired;
    int connected;
    int raw_hid;
} pb_bt_device_t;

typedef int (*pb_bt_device_fn)(void *ctx, const pb_bt_device_t *device);

const char *pb_bt_backend(void);
int pb_bt_scan(int seconds, pb_bt_device_fn fn, void *ctx);
int pb_bt_probe(const char *address, pb_bt_device_t *out);
int pb_bt_connect(const char *address, int pair, pb_source_t **out);
int pb_bt_release(const char *address);

#endif
