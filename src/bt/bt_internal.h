#ifndef PB_BT_INTERNAL_H
#define PB_BT_INTERNAL_H

#include "bt.h"

#define PB_PSM_CONTROL 17
#define PB_PSM_INTERRUPT 19
#define PB_BT_CONNECT_MS 8000
#define PB_BT_FIRST_REPORT_MS 3000

int pb_bt_address(const char *text, char norm[18], unsigned char raw[6]);
int pb_l2cap_wait_report(pb_source_t *src, int timeout_ms);
int pb_l2cap_from_fds(int control, int interrupt, const char *address, pb_source_t **out);

int pb_bluez_scan(int seconds, pb_bt_device_fn fn, void *ctx);
int pb_bluez_info(const char *address, pb_bt_device_t *out);
int pb_bluez_pair(const char *address);
int pb_bluez_disconnect(const char *address);

#endif
