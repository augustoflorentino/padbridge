#ifndef PB_LEARN_H
#define PB_LEARN_H

#include "map.h"

#define PB_LEARN_DONE 1

typedef struct pb_learn pb_learn_t;

int pb_learn_create(const unsigned char *desc, size_t desc_len, pb_learn_t **out);
int pb_learn_prompt(pb_learn_t *learn, char *buf, size_t len);
int pb_learn_add(pb_learn_t *learn, const unsigned char *report, size_t len, uint32_t now_ms);
int pb_learn_next(pb_learn_t *learn);
int pb_learn_finish(pb_learn_t *learn, pb_profile_t **out);
void pb_learn_destroy(pb_learn_t *learn);

#endif
