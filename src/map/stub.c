#include "learn.h"

int pb_profile_parse(const char *text, size_t len, pb_profile_t **out, size_t *out_error_line)
{
    (void)text;
    (void)len;
    (void)out;
    (void)out_error_line;
    return PB_ERR_UNSUPPORTED;
}

int pb_profile_from_descriptor(const unsigned char *desc, size_t len, pb_profile_t **out)
{
    (void)desc;
    (void)len;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_profile_write(const pb_profile_t *profile, FILE *stream)
{
    (void)profile;
    (void)stream;
    return PB_ERR_UNSUPPORTED;
}

const pb_profile_meta_t *pb_profile_meta(const pb_profile_t *profile)
{
    (void)profile;
    return NULL;
}

int pb_profile_decode(const pb_profile_t *profile, const unsigned char *report, size_t len,
                      pb_state_t *state)
{
    (void)profile;
    (void)report;
    (void)len;
    (void)state;
    return PB_ERR_UNSUPPORTED;
}

int pb_profile_rumble(const pb_profile_t *profile, const uint16_t *motors, unsigned count,
                      unsigned char *buf, size_t len, size_t *out_len)
{
    (void)profile;
    (void)motors;
    (void)count;
    (void)buf;
    (void)len;
    (void)out_len;
    return PB_ERR_UNSUPPORTED;
}

void pb_profile_destroy(pb_profile_t *profile)
{
    (void)profile;
}

int pb_learn_create(const unsigned char *desc, size_t desc_len, pb_learn_t **out)
{
    (void)desc;
    (void)desc_len;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_learn_prompt(pb_learn_t *learn, char *buf, size_t len)
{
    (void)learn;
    (void)buf;
    (void)len;
    return PB_ERR_UNSUPPORTED;
}

int pb_learn_add(pb_learn_t *learn, const unsigned char *report, size_t len, uint32_t now_ms)
{
    (void)learn;
    (void)report;
    (void)len;
    (void)now_ms;
    return PB_ERR_UNSUPPORTED;
}

int pb_learn_next(pb_learn_t *learn)
{
    (void)learn;
    return PB_ERR_UNSUPPORTED;
}

int pb_learn_finish(pb_learn_t *learn, pb_profile_t **out)
{
    (void)learn;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

void pb_learn_destroy(pb_learn_t *learn)
{
    (void)learn;
}
