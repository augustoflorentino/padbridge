#include "../cli/commands.h"
#include "../core/conf.h"
#include "../core/out.h"
#include "../core/sys.h"
#include "../hid/source.h"
#include "learn.h"
#include "profile_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PB_STEP_MS 4000u
#define PB_REST_MS 3000u
#define PB_RUMBLE_MS 1000

typedef struct pb_candidate {
    const char *name;
    unsigned char bytes[16];
    size_t len;
    unsigned strong;
    unsigned weak;
} pb_candidate_t;

static const pb_candidate_t pb_candidates[] = {
    {"ninja-sword", {0x01, 0, 0, 0, 0, 0, 0, 0, 0}, 9, 2, 1},
    {"xbox360-usb", {0x00, 0x08, 0x00, 0, 0, 0, 0, 0}, 8, 3, 4},
    {"ds4-usb", {0x05, 0xff, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0}, 11, 5, 4},
    {"switch-like", {0x01, 0, 0, 0, 0, 0, 0, 0}, 8, 1, 2},
    {"generic-2", {0, 0}, 2, 0, 1},
    {"generic-id0", {0x00, 0, 0}, 3, 1, 2},
};

static int pb_exit_for(int status)
{
    return pb_exit_code(status);
}

static int pb_open(const char *uri, pb_source_t **out)
{
    int status = pb_source_open(uri, out);

    if (status != PB_OK)
        pb_log(PB_LOG_ERROR, "%s: %s", uri, pb_strerror(status));
    return status;
}

static int pb_load_conf(pb_conf_t *conf)
{
    memset(conf, 0, sizeof(*conf));
    return pb_conf_load(NULL, conf);
}

static void pb_learn_meta(pb_profile_t *p, const pb_source_info_t *info)
{
    p->meta.vendor = info->vendor;
    p->meta.product = info->product;
    snprintf(p->meta.match_name, sizeof(p->meta.match_name), "%s", info->name);
    if (info->name[0] != '\0')
        snprintf(p->meta.name, sizeof(p->meta.name), "%s", info->name);
}

static int pb_learn_step(pb_source_t *src, pb_learn_t *learn, uint32_t span)
{
    unsigned char report[PB_REPORT_MAX];
    uint32_t start = pb_now_ms();
    size_t len;
    int status;

    while (pb_now_ms() - start < span) {
        status = pb_source_read(src, report, sizeof(report), &len, 100);
        if (status == PB_ERR_TIMEOUT)
            continue;
        if (status != PB_OK)
            return status;
        status = pb_learn_add(learn, report, len, pb_now_ms());
        if (status != PB_OK && status != PB_ERR_BUSY)
            return status;
    }
    return PB_OK;
}

static int pb_learn_steps(pb_source_t *src, pb_learn_t *learn)
{
    static const char *const keys[] = {"step", "action"};
    char text[160];
    char step[16];
    const char *values[2] = {step, text};
    unsigned n = 0;
    int status;

    while (pb_learn_prompt(learn, text, sizeof(text)) == PB_OK) {
        snprintf(step, sizeof(step), "%u", ++n);
        pb_out_row("prompt", 2, keys, values);
        if ((status = pb_learn_step(src, learn, n == 1 ? PB_REST_MS : PB_STEP_MS)) != PB_OK)
            return status;
        pb_learn_next(learn);
    }
    return PB_OK;
}

static int pb_learn_save(pb_profile_t *p, const char *path)
{
    FILE *f = path == NULL ? stdout : fopen(path, "w");
    int status;

    if (f == NULL)
        return PB_ERR_IO;
    status = pb_profile_write(p, f);
    if (f != stdout && fclose(f) != 0)
        status = PB_ERR_IO;
    return status;
}

static int pb_learn_run(pb_source_t *src, const char *out_path)
{
    unsigned char desc[4096];
    size_t desc_len = 0;
    pb_learn_t *learn = NULL;
    pb_profile_t *p = NULL;
    int status;

    if (pb_source_descriptor(src, desc, sizeof(desc), &desc_len) != PB_OK)
        desc_len = 0;
    if ((status = pb_learn_create(desc, desc_len, &learn)) != PB_OK)
        return status;
    status = pb_learn_steps(src, learn);
    if (status == PB_OK)
        status = pb_learn_finish(learn, &p);
    pb_learn_destroy(learn);
    if (status != PB_OK)
        return status;
    pb_learn_meta(p, pb_source_info(src));
    status = pb_learn_save(p, out_path);
    pb_profile_destroy(p);
    return status;
}

int pb_cmd_learn(int argc, char **argv)
{
    const char *out_path = NULL;
    pb_source_t *src;
    int status;

    if (argc == 4 && strcmp(argv[2], "-o") == 0)
        out_path = argv[3];
    else if (argc != 2)
        return PB_EXIT_USAGE;
    if ((status = pb_open(argv[1], &src)) != PB_OK)
        return pb_exit_for(status);
    status = pb_learn_run(src, out_path);
    pb_source_close(src);
    if (status != PB_OK)
        pb_log(PB_LOG_ERROR, "learn: %s", pb_strerror(status));
    return pb_exit_for(status);
}

static int pb_profile_row(void *ctx, const pb_profile_t *profile, const char *origin)
{
    static const char *const keys[] = {"id", "name", "origin"};
    const char *values[3];

    (void)ctx;
    values[0] = profile->meta.id;
    values[1] = profile->meta.name;
    values[2] = origin;
    pb_out_row("profile", 3, keys, values);
    return 0;
}

static int pb_profile_file(const char *path, pb_profile_t **out)
{
    static char text[64 * 1024];
    FILE *f = fopen(path, "r");
    size_t len;
    size_t line = 0;
    int status;

    if (f == NULL)
        return PB_ERR_NOT_FOUND;
    len = fread(text, 1, sizeof(text), f);
    fclose(f);
    status = pb_profile_parse(text, len, out, &line);
    if (status != PB_OK)
        pb_log(PB_LOG_ERROR, "%s:%zu: bad profile line", path, line);
    return status;
}

static int pb_profile_show(const pb_conf_t *conf, const char *id)
{
    pb_profile_t *p = NULL;
    int status;

    if (strchr(id, '/') != NULL)
        status = pb_profile_file(id, &p);
    else
        status = pb_profile_find(conf, id, &p);
    if (status != PB_OK)
        return status;
    status = pb_profile_write(p, stdout);
    pb_profile_destroy(p);
    return status;
}

int pb_cmd_profiles(int argc, char **argv)
{
    pb_conf_t conf;
    int status;

    if (argc != 1 && !(argc == 3 && strcmp(argv[1], "show") == 0))
        return PB_EXIT_USAGE;
    if ((status = pb_load_conf(&conf)) != PB_OK && !(argc == 3 && strchr(argv[2], '/') != NULL))
        return pb_exit_for(status);
    if (argc == 3)
        status = pb_profile_show(&conf, argv[2]);
    else
        status = pb_profile_each(&conf, pb_profile_row, NULL);
    if (status != PB_OK)
        pb_log(PB_LOG_ERROR, "profiles: %s", pb_strerror(status));
    return pb_exit_for(status);
}

static int pb_parse_level(const char *s, uint16_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);

    if (end == s || *end != '\0' || v > 65535)
        return 0;
    *out = (uint16_t)v;
    return 1;
}

static int pb_send_hold(pb_source_t *src, const unsigned char *on, const unsigned char *off, size_t len,
                        uint32_t ms)
{
    int status = pb_source_write(src, on, len);

    if (status != PB_OK)
        return status;
    pb_sleep_ms(ms);
    return pb_source_write(src, off, len);
}

static int pb_rumble_profile(pb_source_t *src, const pb_profile_t *p, const uint16_t *motors, uint32_t ms)
{
    static const uint16_t zero[PB_MOTOR_COUNT];
    unsigned char on[PB_RUMBLE_MAX];
    unsigned char off[PB_RUMBLE_MAX];
    size_t len;
    int status;

    if ((status = pb_profile_rumble(p, motors, PB_MOTOR_COUNT, on, sizeof(on), &len)) != PB_OK)
        return status;
    pb_profile_rumble(p, zero, PB_MOTOR_COUNT, off, sizeof(off), &len);
    return pb_send_hold(src, on, off, len, ms);
}

static void pb_candidate_row(const pb_candidate_t *c, const unsigned char *bytes, int status)
{
    static const char *const keys[] = {"format", "packet", "status"};
    char hex[3 * 16 + 1];
    const char *values[3];
    size_t i;

    for (i = 0; i < c->len; i++)
        snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
    values[0] = c->name;
    values[1] = hex;
    values[2] = status == PB_OK ? "sent" : pb_strerror(status);
    pb_out_row("candidate", 3, keys, values);
}

static int pb_rumble_candidates(pb_source_t *src, const uint16_t *motors, uint32_t ms)
{
    size_t i;
    int sent = 0;

    for (i = 0; i < sizeof(pb_candidates) / sizeof(pb_candidates[0]); i++) {
        const pb_candidate_t *c = &pb_candidates[i];
        unsigned char on[16];
        int status;

        memcpy(on, c->bytes, c->len);
        on[c->strong] = (unsigned char)(motors[PB_MOTOR_STRONG] >> 8);
        on[c->weak] = (unsigned char)(motors[PB_MOTOR_WEAK] >> 8);
        status = pb_send_hold(src, on, c->bytes, c->len, ms);
        pb_candidate_row(c, on, status);
        sent |= status == PB_OK;
        pb_sleep_ms(ms / 2);
    }
    return sent ? PB_OK : PB_ERR_UNSUPPORTED;
}

static int pb_rumble_run(pb_source_t *src, const uint16_t *motors, uint32_t ms)
{
    const pb_source_info_t *info = pb_source_info(src);
    const char *address = strncmp(info->uri, "bt:", 3) == 0 ? info->uri + 3 : NULL;
    pb_profile_t *p = NULL;
    pb_conf_t conf;
    int status;

    if (pb_load_conf(&conf) == PB_OK
        && pb_profile_match(&conf, info->vendor, info->product, info->name, address, &p) == PB_OK) {
        status = pb_rumble_profile(src, p, motors, ms);
        pb_profile_destroy(p);
        if (status != PB_ERR_UNSUPPORTED)
            return status;
    }
    pb_log(PB_LOG_WARN, "no rumble format known for this controller, trying candidates");
    return pb_rumble_candidates(src, motors, ms);
}

int pb_cmd_rumble(int argc, char **argv)
{
    uint16_t motors[PB_MOTOR_COUNT] = {0};
    uint16_t ms = PB_RUMBLE_MS;
    pb_source_t *src;
    int status;

    if (argc < 4 || argc > 5 || !pb_parse_level(argv[2], &motors[PB_MOTOR_STRONG])
        || !pb_parse_level(argv[3], &motors[PB_MOTOR_WEAK]) || (argc == 5 && !pb_parse_level(argv[4], &ms)))
        return PB_EXIT_USAGE;
    if ((status = pb_open(argv[1], &src)) != PB_OK)
        return pb_exit_for(status);
    status = pb_rumble_run(src, motors, ms);
    pb_source_close(src);
    if (status != PB_OK)
        pb_log(PB_LOG_ERROR, "rumble: %s", pb_strerror(status));
    return pb_exit_for(status);
}
