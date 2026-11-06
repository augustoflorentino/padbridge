#ifndef PB_MAP_H
#define PB_MAP_H

#include <stdio.h>
#include "../core/pb.h"

typedef struct pb_profile_meta {
    char id[64];
    char name[128];
    uint16_t vendor;
    uint16_t product;
    char match_name[128];
    char address[18];
    int out_kind;
    uint16_t out_vendor;
    uint16_t out_product;
    char out_name[128];
    unsigned motors;
} pb_profile_meta_t;

typedef struct pb_profile pb_profile_t;

int pb_profile_parse(const char *text, size_t len, pb_profile_t **out, size_t *out_error_line);
int pb_profile_from_descriptor(const unsigned char *desc, size_t len, pb_profile_t **out);
int pb_profile_write(const pb_profile_t *profile, FILE *stream);
const pb_profile_meta_t *pb_profile_meta(const pb_profile_t *profile);
int pb_profile_decode(const pb_profile_t *profile, const unsigned char *report, size_t len,
                      pb_state_t *state);
int pb_profile_rumble(const pb_profile_t *profile, const uint16_t *motors, unsigned count,
                      unsigned char *buf, size_t len, size_t *out_len);
void pb_profile_destroy(pb_profile_t *profile);

#endif
