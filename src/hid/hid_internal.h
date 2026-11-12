#ifndef PB_HID_INTERNAL_H
#define PB_HID_INTERNAL_H

#include "source.h"

#define PB_HID_DESC_MAX 4096

int pb_hid_numbered(const unsigned char *desc, size_t len);
int pb_hid_errno(int err);
void pb_hid_set_uri(pb_source_info_t *info, const char *scheme, const char *path);
void pb_hid_copy_name(pb_source_info_t *info, const char *name);
unsigned pb_hid_bus(unsigned bustype);

#endif
