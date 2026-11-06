#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../bt/bt.h"
#include "../ff/ff.h"
#include "../hid/source.h"
#include "../hook/hook.h"
#include "../map/map.h"
#include "../record/record.h"
#include "../sink/sink.h"
#include "bridge.h"
#include "out.h"
#include "sys.h"

#define PB_READ_TIMEOUT_MS 200
#define PB_FF_IDLE_MS 100

typedef struct pb_shared {
    const pb_bridge_options_t *options;
    pb_record_t *record;
    pb_mutex_t *record_lock;
} pb_shared_t;

typedef struct pb_link {
    pb_shared_t *shared;
    char uri[288];
    char profile_id[64];
    pb_profile_t *profile;
    pb_sink_t *sink;
    pb_ff_t *ff;
    pb_source_t *source;
    pb_mutex_t *source_lock;
    pb_thread_t *worker;
    pb_thread_t *ff_thread;
    volatile int done;
    int fatal;
    int status;
} pb_link_t;

static void pb_link_record(pb_link_t *link, unsigned dir, const unsigned char *buf, size_t len)
{
    pb_shared_t *s = link->shared;

    if (s->record == NULL)
        return;
    pb_mutex_lock(s->record_lock);
    pb_record_packet(s->record, dir, buf, len, pb_now_ns());
    pb_mutex_unlock(s->record_lock);
}

static void pb_link_event(pb_link_t *link, const char *status, const char *detail)
{
    const pb_conf_t *conf = link->shared->options->conf;

    pb_state_write(conf, link->uri, status, detail);
    pb_log(PB_LOG_INFO, "%s: %s%s%s", link->uri, status, detail[0] ? ": " : "", detail);
    if (conf->hook[0] != '\0' && pb_hook_run(conf->hook, status, link->uri, detail) != PB_OK)
        pb_log(PB_LOG_WARN, "%s: hook failed", link->uri);
}

static int pb_link_open_source(pb_link_t *link, pb_source_t **out)
{
    if (strncmp(link->uri, "bt:", 3) == 0)
        return pb_bt_connect(link->uri + 3, 1, out);
    return pb_source_open(link->uri, out);
}

static int pb_link_profile(pb_link_t *link, pb_source_t *src)
{
    const pb_source_info_t *info = pb_source_info(src);
    const pb_bridge_options_t *o = link->shared->options;
    const char *address = strncmp(link->uri, "bt:", 3) == 0 ? link->uri + 3 : NULL;
    unsigned char desc[4096];
    size_t len;

    if (link->profile != NULL)
        return PB_OK;
    if (link->profile_id[0] != '\0')
        return pb_profile_find(o->conf, link->profile_id, &link->profile);
    if (o->profile_id != NULL)
        return pb_profile_find(o->conf, o->profile_id, &link->profile);
    if (pb_profile_match(o->conf, info->vendor, info->product, info->name, address, &link->profile)
        == PB_OK)
        return PB_OK;
    if (pb_source_descriptor(src, desc, sizeof(desc), &len) != PB_OK)
        return PB_ERR_NOT_FOUND;
    return pb_profile_from_descriptor(desc, len, &link->profile);
}

static int pb_link_sink(pb_link_t *link)
{
    const pb_profile_meta_t *meta = pb_profile_meta(link->profile);
    const pb_bridge_options_t *o = link->shared->options;
    pb_sink_config_t config;
    int r;

    if (link->sink != NULL)
        return PB_OK;
    if (o->text_output)
        return pb_sink_open_text(stdout, &link->sink);
    config.kind = meta->out_kind;
    config.vendor = meta->out_vendor;
    config.product = meta->out_product;
    config.name = meta->out_name[0] ? meta->out_name : NULL;
    config.no_ff = o->no_ff || meta->motors == 0;
    r = pb_sink_open_vpad(&config, &link->sink);
    if (r == PB_OK && !config.no_ff)
        r = pb_ff_create(meta->motors, &link->ff);
    return r;
}

static void pb_link_rumble(pb_link_t *link, const uint16_t *motors, unsigned count)
{
    unsigned char buf[PB_REPORT_MAX];
    size_t len;

    if (pb_profile_rumble(link->profile, motors, count, buf, sizeof(buf), &len) != PB_OK)
        return;
    pb_mutex_lock(link->source_lock);
    if (link->source != NULL && pb_source_write(link->source, buf, len) == PB_OK)
        pb_link_record(link, PB_DIR_OUT, buf, len);
    pb_mutex_unlock(link->source_lock);
}

static int pb_ff_main(void *arg)
{
    pb_link_t *link = arg;
    unsigned count = pb_profile_meta(link->profile)->motors;
    uint16_t motors[PB_MOTOR_COUNT];
    uint32_t wait = PB_FF_IDLE_MS;
    vpad_event_t ev;
    int changed;
    int r;

    if (count > PB_MOTOR_COUNT)
        count = PB_MOTOR_COUNT;
    while (!link->done) {
        r = pb_sink_next_ff(link->sink, &ev, (int)wait);
        if (r == PB_OK)
            pb_ff_event(link->ff, &ev, pb_now_ms());
        else if (r != PB_ERR_TIMEOUT)
            pb_sleep_ms(PB_FF_IDLE_MS);
        changed = 0;
        if (pb_ff_poll(link->ff, pb_now_ms(), motors, count, &changed, &wait) != PB_OK
            || wait > PB_FF_IDLE_MS)
            wait = PB_FF_IDLE_MS;
        if (changed)
            pb_link_rumble(link, motors, count);
    }
    return PB_OK;
}

static int pb_link_attach(pb_link_t *link, pb_source_t *src)
{
    int r = pb_link_profile(link, src);

    if (r != PB_OK) {
        pb_log(PB_LOG_ERROR, "%s: no profile fits (%s)", link->uri, pb_strerror(r));
        return r;
    }
    r = pb_link_sink(link);
    if (r != PB_OK) {
        pb_log(PB_LOG_ERROR, "%s: virtual pad: %s", link->uri, pb_strerror(r));
        return r;
    }
    if (link->ff != NULL && link->ff_thread == NULL)
        r = pb_thread_start(&link->ff_thread, pb_ff_main, link);
    return r;
}

static int pb_link_pump(pb_link_t *link, pb_source_t *src)
{
    unsigned char buf[PB_REPORT_MAX];
    pb_state_t state;
    size_t len;
    int r;

    pb_state_neutral(&state);
    while (!pb_interrupted()) {
        r = pb_source_read(src, buf, sizeof(buf), &len, PB_READ_TIMEOUT_MS);
        if (r == PB_ERR_TIMEOUT)
            continue;
        if (r != PB_OK)
            return r;
        pb_link_record(link, PB_DIR_IN, buf, len);
        if (pb_profile_decode(link->profile, buf, len, &state) == PB_OK
            && pb_sink_send(link->sink, &state) != PB_OK)
            pb_log(PB_LOG_WARN, "%s: virtual pad rejected the state", link->uri);
    }
    return PB_OK;
}

static void pb_link_wait(int seconds)
{
    uint32_t end = pb_now_ms() + (uint32_t)seconds * 1000u;

    while (!pb_interrupted() && (int32_t)(end - pb_now_ms()) > 0)
        pb_sleep_ms(PB_READ_TIMEOUT_MS);
}

static int pb_link_session(pb_link_t *link, pb_source_t *src)
{
    pb_state_t neutral;
    int r = pb_link_attach(link, src);

    if (r != PB_OK) {
        link->fatal = 1;
        pb_source_close(src);
        return r;
    }
    pb_mutex_lock(link->source_lock);
    link->source = src;
    pb_mutex_unlock(link->source_lock);
    pb_link_event(link, "connected", pb_profile_meta(link->profile)->id);
    r = pb_link_pump(link, src);
    pb_mutex_lock(link->source_lock);
    link->source = NULL;
    pb_mutex_unlock(link->source_lock);
    pb_source_close(src);
    pb_state_neutral(&neutral);
    pb_sink_send(link->sink, &neutral);
    pb_link_event(link, "disconnected", r == PB_OK || r == PB_ERR_CLOSED ? "" : pb_strerror(r));
    return r == PB_ERR_CLOSED ? PB_OK : r;
}

static int pb_open_fatal(const pb_link_t *link, int r)
{
    if (r == PB_ERR_ARG || r == PB_ERR_UNSUPPORTED || r == PB_ERR_ACCESS || r == PB_ERR_NOMEM)
        return 1;
    return r == PB_ERR_NOT_FOUND && strncmp(link->uri, "bt:", 3) != 0;
}

static int pb_worker_main(void *arg)
{
    pb_link_t *link = arg;
    const pb_bridge_options_t *o = link->shared->options;
    pb_source_t *src;
    int r;

    while (!pb_interrupted()) {
        r = pb_link_open_source(link, &src);
        if (r == PB_OK)
            r = pb_link_session(link, src);
        else if (pb_open_fatal(link, r)) {
            pb_log(PB_LOG_ERROR, "%s: %s", link->uri, pb_strerror(r));
            link->fatal = 1;
        }
        if (o->once || link->fatal) {
            link->status = r;
            break;
        }
        if (r != PB_OK)
            pb_link_event(link, "waiting", pb_strerror(r));
        pb_link_wait(o->conf->reconnect_s);
    }
    link->done = 1;
    if (link->sink != NULL)
        pb_sink_wake(link->sink);
    return PB_OK;
}

static int pb_is_address(const char *s)
{
    int i;

    for (i = 0; i < 17; i++) {
        char c = s[i];

        if (i % 3 == 2 ? c != ':' : !((c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f')))
            return 0;
    }
    return s[17] == '\0';
}

static int pb_has_scheme(const char *s)
{
    return strncmp(s, "hid:", 4) == 0 || strncmp(s, "bt:", 3) == 0 || strncmp(s, "replay:", 7) == 0;
}

typedef struct pb_hid_pick {
    uint16_t vendor;
    uint16_t product;
    char uri[288];
} pb_hid_pick_t;

static int pb_pick_hid(void *ctx, const pb_source_info_t *info)
{
    pb_hid_pick_t *p = ctx;

    if (info->vendor != p->vendor || info->product != p->product)
        return PB_OK;
    snprintf(p->uri, sizeof(p->uri), "%s", info->uri);
    return PB_ERR_BUSY;
}

static int pb_profile_uri(const pb_profile_meta_t *meta, char *uri, size_t len)
{
    pb_hid_pick_t pick;

    if (meta->address[0] != '\0')
        return snprintf(uri, len, "bt:%s", meta->address) >= (int)len ? PB_ERR_ARG : PB_OK;
    if (meta->vendor == 0 && meta->product == 0)
        return PB_ERR_NOT_FOUND;
    memset(&pick, 0, sizeof(pick));
    pick.vendor = meta->vendor;
    pick.product = meta->product;
    pb_hid_list(pb_pick_hid, &pick);
    if (pick.uri[0] == '\0')
        return PB_ERR_NOT_FOUND;
    return snprintf(uri, len, "%s", pick.uri) >= (int)len ? PB_ERR_ARG : PB_OK;
}

int pb_bridge_target_uri(const pb_conf_t *conf, const char *target, char *uri, size_t len)
{
    pb_profile_t *profile;
    int r;

    if (target == NULL || uri == NULL)
        return PB_ERR_ARG;
    if (pb_has_scheme(target))
        r = snprintf(uri, len, "%s", target) >= (int)len ? PB_ERR_ARG : PB_OK;
    else if (pb_is_address(target))
        r = snprintf(uri, len, "bt:%s", target) >= (int)len ? PB_ERR_ARG : PB_OK;
    else if (strchr(target, '/') != NULL || strchr(target, '\\') != NULL)
        r = snprintf(uri, len, "hid:%s", target) >= (int)len ? PB_ERR_ARG : PB_OK;
    else if ((r = pb_profile_find(conf, target, &profile)) == PB_OK) {
        r = pb_profile_uri(pb_profile_meta(profile), uri, len);
        pb_profile_destroy(profile);
    }
    return r;
}

typedef struct pb_target_list {
    const pb_conf_t *conf;
    char **items;
    size_t count;
    size_t cap;
    int status;
} pb_target_list_t;

static void pb_targets_add(pb_target_list_t *l, const char *uri)
{
    size_t i;
    char **grown;

    for (i = 0; i < l->count; i++)
        if (strcmp(l->items[i], uri) == 0)
            return;
    if (l->count == l->cap) {
        grown = realloc(l->items, (l->cap ? l->cap * 2 : 8) * sizeof(*grown));
        if (grown == NULL) {
            l->status = PB_ERR_NOMEM;
            return;
        }
        l->items = grown;
        l->cap = l->cap ? l->cap * 2 : 8;
    }
    l->items[l->count] = malloc(strlen(uri) + 1);
    if (l->items[l->count] == NULL) {
        l->status = PB_ERR_NOMEM;
        return;
    }
    strcpy(l->items[l->count++], uri);
}

static int pb_targets_profile(void *ctx, const pb_profile_t *profile, const char *origin)
{
    const pb_profile_meta_t *meta = pb_profile_meta(profile);
    char uri[300];

    (void)origin;
    if (meta->address[0] != '\0' && snprintf(uri, sizeof(uri), "bt:%s", meta->address) < (int)sizeof(uri))
        pb_targets_add(ctx, uri);
    return ((pb_target_list_t *)ctx)->status;
}

static int pb_targets_hid(void *ctx, const pb_source_info_t *info)
{
    pb_target_list_t *l = ctx;
    pb_profile_t *profile;

    if (pb_profile_match(l->conf, info->vendor, info->product, info->name, NULL, &profile) != PB_OK)
        return PB_OK;
    pb_profile_destroy(profile);
    pb_targets_add(l, info->uri);
    return l->status;
}

int pb_bridge_default_targets(const pb_conf_t *conf, char ***out_targets, size_t *out_count)
{
    pb_target_list_t l;

    memset(&l, 0, sizeof(l));
    l.conf = conf;
    pb_profile_each(conf, pb_targets_profile, &l);
    pb_hid_list(pb_targets_hid, &l);
    if (l.status != PB_OK) {
        pb_bridge_free_targets(l.items, l.count);
        return l.status;
    }
    *out_targets = l.items;
    *out_count = l.count;
    return l.count ? PB_OK : PB_ERR_NOT_FOUND;
}

void pb_bridge_free_targets(char **targets, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        free(targets[i]);
    free(targets);
}

static int pb_link_start(pb_link_t *link, pb_shared_t *shared, const char *target)
{
    const pb_conf_t *conf = shared->options->conf;
    int r;

    link->shared = shared;
    if (!pb_has_scheme(target) && !pb_is_address(target) && strchr(target, '/') == NULL
        && strchr(target, '\\') == NULL)
        snprintf(link->profile_id, sizeof(link->profile_id), "%s", target);
    r = pb_bridge_target_uri(conf, target, link->uri, sizeof(link->uri));
    if (r != PB_OK) {
        pb_log(PB_LOG_ERROR, "%s: %s", target, pb_strerror(r));
        return r;
    }
    r = pb_mutex_create(&link->source_lock);
    if (r == PB_OK)
        r = pb_thread_start(&link->worker, pb_worker_main, link);
    return r;
}

static int pb_link_finish(pb_link_t *link)
{
    if (link->worker != NULL)
        pb_thread_join(link->worker);
    link->done = 1;
    if (link->ff_thread != NULL) {
        pb_sink_wake(link->sink);
        pb_thread_join(link->ff_thread);
    }
    pb_ff_destroy(link->ff);
    pb_sink_close(link->sink);
    pb_profile_destroy(link->profile);
    pb_mutex_destroy(link->source_lock);
    return link->status;
}

static int pb_shared_open(pb_shared_t *shared, const pb_bridge_options_t *options)
{
    int r;

    memset(shared, 0, sizeof(*shared));
    shared->options = options;
    if (options->record_path == NULL)
        return PB_OK;
    r = pb_mutex_create(&shared->record_lock);
    if (r == PB_OK)
        r = pb_record_open(options->record_path, "padbridge", &shared->record);
    return r;
}

static void pb_shared_close(pb_shared_t *shared)
{
    pb_record_close(shared->record);
    pb_mutex_destroy(shared->record_lock);
}

int pb_bridge_run(const pb_bridge_options_t *options, const char *const *targets, size_t count)
{
    pb_shared_t shared;
    pb_link_t *links;
    size_t i, started = 0;
    int opened;
    int status;
    int r;

    if (options == NULL || options->conf == NULL || targets == NULL || count == 0)
        return PB_ERR_ARG;
    links = calloc(count, sizeof(*links));
    if (links == NULL)
        return PB_ERR_NOMEM;
    status = pb_shared_open(&shared, options);
    opened = status == PB_OK;
    for (i = 0; opened && i < count; i++) {
        r = pb_link_start(&links[i], &shared, targets[i]);
        if (r == PB_OK)
            started++;
        else if (status == PB_OK)
            status = r;
    }
    if (started == 0 && status == PB_OK)
        status = PB_ERR_NOT_FOUND;
    for (i = 0; i < count; i++) {
        r = pb_link_finish(&links[i]);
        if (status == PB_OK)
            status = r;
    }
    pb_shared_close(&shared);
    free(links);
    return status;
}
