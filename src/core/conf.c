#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "builtin.h"
#include "conf.h"
#include "out.h"
#include "paths.h"
#include "sys.h"
#include <time.h>

static pb_mutex_t *state_lock;

static char *pb_trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int pb_copy(char *dst, size_t len, const char *src)
{
    int n = snprintf(dst, len, "%s", src);

    return n < 0 || (size_t)n >= len ? PB_ERR_ARG : PB_OK;
}

static int pb_conf_defaults(pb_conf_t *conf)
{
    char dir[480];

    memset(conf, 0, sizeof(*conf));
    conf->reconnect_s = 15;
    conf->log_level = PB_LOG_WARN;
    if (pb_config_dir(dir, sizeof(dir)) == PB_OK)
        snprintf(conf->profiles_dir, sizeof(conf->profiles_dir), "%s/profiles", dir);
    if (pb_runtime_dir(dir, sizeof(dir)) != PB_OK)
        return PB_ERR_NOT_FOUND;
    snprintf(conf->state_path, sizeof(conf->state_path), "%s/state", dir);
    snprintf(conf->pid_path, sizeof(conf->pid_path), "%s/pid", dir);
    return PB_OK;
}

static int pb_conf_set(pb_conf_t *conf, const char *key, const char *value)
{
    if (strcmp(key, "profiles") == 0)
        return pb_copy(conf->profiles_dir, sizeof(conf->profiles_dir), value);
    if (strcmp(key, "hook") == 0)
        return pb_copy(conf->hook, sizeof(conf->hook), value);
    if (strcmp(key, "reconnect") == 0) {
        conf->reconnect_s = atoi(value);
        return conf->reconnect_s > 0 ? PB_OK : PB_ERR_FORMAT;
    }
    if (strcmp(key, "log") == 0) {
        static const char *const levels[] = {"error", "warning", "info", "debug"};
        int i;

        for (i = 0; i < 4; i++)
            if (strcmp(value, levels[i]) == 0) {
                conf->log_level = i;
                return PB_OK;
            }
    }
    return PB_ERR_FORMAT;
}

int pb_conf_path(char *buf, size_t len)
{
    char dir[512];
    int status = pb_config_dir(dir, sizeof(dir));

    if (status != PB_OK)
        return status;
    return snprintf(buf, len, "%s/padbridge.conf", dir) >= (int)len ? PB_ERR_ARG : PB_OK;
}

int pb_conf_load(const char *path, pb_conf_t *out)
{
    char fallback[600];
    char *text, *line, *next, *eq;
    size_t len, number = 0;
    int status = pb_conf_defaults(out);

    if (status != PB_OK)
        return status;
    if (path == NULL) {
        if (pb_conf_path(fallback, sizeof(fallback)) != PB_OK)
            return PB_OK;
        path = fallback;
        if (pb_read_file(path, &text, &len) != PB_OK)
            return PB_OK;
    } else if ((status = pb_read_file(path, &text, &len)) != PB_OK) {
        return status;
    }
    for (line = text; line != NULL && status == PB_OK; line = next) {
        next = strchr(line, '\n');
        if (next != NULL)
            *next++ = '\0';
        number++;
        line = pb_trim(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        eq = strchr(line, '=');
        if (eq == NULL)
            status = PB_ERR_FORMAT;
        else {
            *eq = '\0';
            status = pb_conf_set(out, pb_trim(line), pb_trim(eq + 1));
        }
        if (status != PB_OK)
            pb_log(PB_LOG_ERROR, "%s:%zu: bad setting", path, number);
    }
    free(text);
    return status;
}

#define PB_STOP (-1)
#define PB_MAX_IDS 256

typedef int (*pb_profile_visit)(void *ctx, pb_profile_t *profile, const char *origin, int *taken);

typedef struct pb_walk {
    pb_profile_visit visit;
    void *ctx;
    char ids[PB_MAX_IDS][64];
    size_t id_count;
} pb_walk_t;

static int pb_walk_seen(const pb_walk_t *w, const char *id)
{
    size_t i;

    for (i = 0; i < w->id_count; i++)
        if (strcmp(w->ids[i], id) == 0)
            return 1;
    return 0;
}

static int pb_walk_text(pb_walk_t *w, const char *text, size_t len, const char *origin)
{
    pb_profile_t *profile;
    const pb_profile_meta_t *meta;
    size_t line = 0;
    int taken = 0;
    int r;

    if (pb_profile_parse(text, len, &profile, &line) != PB_OK) {
        pb_log(PB_LOG_WARN, "%s:%zu: profile skipped", origin, line);
        return PB_OK;
    }
    meta = pb_profile_meta(profile);
    if (pb_walk_seen(w, meta->id)) {
        pb_profile_destroy(profile);
        return PB_OK;
    }
    if (w->id_count < PB_MAX_IDS)
        snprintf(w->ids[w->id_count++], sizeof(w->ids[0]), "%s", meta->id);
    r = w->visit(w->ctx, profile, origin, &taken);
    if (!taken)
        pb_profile_destroy(profile);
    return r;
}

static int pb_walk_file(void *ctx, const char *path, const char *name)
{
    char *text;
    size_t len;
    int r;

    (void)name;
    if (pb_read_file(path, &text, &len) != PB_OK)
        return PB_OK;
    r = pb_walk_text(ctx, text, len, path);
    free(text);
    return r;
}

static int pb_profiles_walk(const pb_conf_t *conf, pb_profile_visit visit, void *ctx)
{
    pb_walk_t *w = calloc(1, sizeof(*w));
    const pb_builtin_t *b;
    int r = PB_OK;

    if (w == NULL)
        return PB_ERR_NOMEM;
    w->visit = visit;
    w->ctx = ctx;
    if (conf->profiles_dir[0] != '\0') {
        r = pb_dir_each(conf->profiles_dir, pb_walk_file, w);
        if (r == PB_ERR_NOT_FOUND)
            r = PB_OK;
    }
    for (b = pb_builtin_profiles; r == PB_OK && b->name != NULL; b++)
        r = pb_walk_text(w, b->text, b->len, "builtin");
    free(w);
    return r == PB_STOP ? PB_OK : r;
}

typedef struct pb_each_ctx {
    int (*fn)(void *ctx, const pb_profile_t *profile, const char *origin);
    void *ctx;
} pb_each_ctx_t;

static int pb_each_visit(void *ctx, pb_profile_t *profile, const char *origin, int *taken)
{
    pb_each_ctx_t *e = ctx;

    (void)taken;
    return e->fn(e->ctx, profile, origin);
}

int pb_profile_each(const pb_conf_t *conf, int (*fn)(void *ctx, const pb_profile_t *profile, const char *origin),
                    void *ctx)
{
    pb_each_ctx_t e;

    if (conf == NULL || fn == NULL)
        return PB_ERR_ARG;
    e.fn = fn;
    e.ctx = ctx;
    return pb_profiles_walk(conf, pb_each_visit, &e);
}

typedef struct pb_find_ctx {
    const char *id;
    pb_profile_t *found;
} pb_find_ctx_t;

static int pb_find_visit(void *ctx, pb_profile_t *profile, const char *origin, int *taken)
{
    pb_find_ctx_t *f = ctx;

    (void)origin;
    if (strcmp(pb_profile_meta(profile)->id, f->id) != 0)
        return PB_OK;
    f->found = profile;
    *taken = 1;
    return PB_STOP;
}

int pb_profile_find(const pb_conf_t *conf, const char *id, pb_profile_t **out)
{
    pb_find_ctx_t f = {id, NULL};
    int r;

    if (conf == NULL || id == NULL || out == NULL)
        return PB_ERR_ARG;
    r = pb_profiles_walk(conf, pb_find_visit, &f);
    if (f.found == NULL)
        return r == PB_OK ? PB_ERR_NOT_FOUND : r;
    *out = f.found;
    return PB_OK;
}

typedef struct pb_match_ctx {
    uint16_t vendor;
    uint16_t product;
    const char *name;
    const char *address;
    int best_score;
    pb_profile_t *best;
} pb_match_ctx_t;

static int pb_same_text(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int pb_match_score(const pb_match_ctx_t *m, const pb_profile_meta_t *meta)
{
    int score = 0;

    if (meta->address[0] != '\0') {
        if (m->address == NULL || !pb_same_text(meta->address, m->address))
            return 0;
        score += 8;
    }
    if (meta->vendor != 0 || meta->product != 0) {
        if (meta->vendor != m->vendor || meta->product != m->product)
            return 0;
        score += 4;
    }
    if (meta->match_name[0] != '\0') {
        if (m->name == NULL || strstr(m->name, meta->match_name) == NULL)
            return 0;
        score += 2;
    }
    return score;
}

static int pb_match_visit(void *ctx, pb_profile_t *profile, const char *origin, int *taken)
{
    pb_match_ctx_t *m = ctx;
    int score = pb_match_score(m, pb_profile_meta(profile));

    (void)origin;
    if (score <= m->best_score)
        return PB_OK;
    pb_profile_destroy(m->best);
    m->best = profile;
    m->best_score = score;
    *taken = 1;
    return PB_OK;
}

int pb_profile_match(const pb_conf_t *conf, uint16_t vendor, uint16_t product, const char *name,
                     const char *address, pb_profile_t **out)
{
    pb_match_ctx_t m = {vendor, product, name, address, 0, NULL};
    int r;

    if (conf == NULL || out == NULL)
        return PB_ERR_ARG;
    r = pb_profiles_walk(conf, pb_match_visit, &m);
    if (r != PB_OK) {
        pb_profile_destroy(m.best);
        return r;
    }
    if (m.best == NULL)
        return PB_ERR_NOT_FOUND;
    *out = m.best;
    return PB_OK;
}

int pb_state_claim(const pb_conf_t *conf, long *out_owner)
{
    char dir[512];
    char text[32];
    long owner;
    int n;

    if (conf == NULL || out_owner == NULL)
        return PB_ERR_ARG;
    owner = pb_state_owner(conf);
    *out_owner = owner;
    if (owner > 0 && owner != pb_process_id())
        return PB_ERR_BUSY;
    snprintf(dir, sizeof(dir), "%s", conf->pid_path);
    if (strrchr(dir, '/') != NULL)
        *strrchr(dir, '/') = '\0';
    if (pb_make_dir(dir) != PB_OK)
        return PB_ERR_ACCESS;
    if (state_lock == NULL && pb_mutex_create(&state_lock) != PB_OK)
        return PB_ERR_NOMEM;
    n = snprintf(text, sizeof(text), "%ld\n", pb_process_id());
    remove(conf->state_path);
    return pb_replace_file(conf->pid_path, text, (size_t)n);
}

void pb_state_release(const pb_conf_t *conf)
{
    if (conf == NULL || pb_state_owner(conf) != pb_process_id())
        return;
    remove(conf->pid_path);
    remove(conf->state_path);
}

long pb_state_owner(const pb_conf_t *conf)
{
    char *text;
    size_t len;
    long pid;

    if (conf == NULL || pb_read_file(conf->pid_path, &text, &len) != PB_OK)
        return 0;
    pid = strtol(text, NULL, 10);
    free(text);
    return pb_process_alive(pid) ? pid : 0;
}

static size_t pb_state_keep(const char *text, const char *controller, char *out, size_t cap)
{
    size_t name_len = strlen(controller);
    size_t used = 0;
    const char *line = text;
    const char *end;
    size_t len;

    for (; line != NULL && *line != '\0'; line = end == NULL ? NULL : end + 1) {
        end = strchr(line, '\n');
        len = end == NULL ? strlen(line) : (size_t)(end - line);
        if (len == 0 || (len > name_len && strncmp(line, controller, name_len) == 0
                         && line[name_len] == '\t'))
            continue;
        if (used + len + 1 >= cap)
            break;
        memcpy(out + used, line, len);
        used += len;
        out[used++] = '\n';
    }
    return used;
}

int pb_state_write(const pb_conf_t *conf, const char *controller, const char *status, const char *detail)
{
    static char buf[16384];
    char *text = NULL;
    size_t len = 0;
    int n;
    int r;

    if (conf == NULL || controller == NULL || status == NULL || state_lock == NULL)
        return PB_ERR_ARG;
    pb_mutex_lock(state_lock);
    if (pb_read_file(conf->state_path, &text, &len) != PB_OK)
        text = NULL;
    len = text == NULL ? 0 : pb_state_keep(text, controller, buf, sizeof(buf) - 512);
    free(text);
    n = snprintf(buf + len, sizeof(buf) - len, "%s\t%s\t%lld\t%s\n", controller, status,
                 (long long)time(NULL), detail == NULL ? "" : detail);
    if (n < 0 || (size_t)n >= sizeof(buf) - len)
        n = 0;
    r = pb_replace_file(conf->state_path, buf, len + (size_t)n);
    pb_mutex_unlock(state_lock);
    return r;
}
