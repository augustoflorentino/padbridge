#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wirefile/wirefile.h>
#include "../core/out.h"
#include "../core/sys.h"
#include "hid_internal.h"

#define PB_REPLAY_REPORTS PCW_LINKTYPE_USER0
#define PB_REPLAY_DESCRIPTOR (PCW_LINKTYPE_USER0 + 1)
#define PB_REPLAY_MAX_FILE (256L * 1024L * 1024L)

typedef struct pb_replay {
    pb_source_t base;
    unsigned char *file;
    size_t file_len;
    pcw_reader_t *reader;
    const pcw_block_t *pending;
    unsigned char desc[PB_HID_DESC_MAX];
    size_t desc_len;
    int started;
    uint64_t wall_start;
    uint64_t capture_start;
} pb_replay_t;

static uint64_t pb_block_ns(const pcw_block_t *block)
{
    return (uint64_t)block->ts_sec * 1000000000u + block->ts_nsec;
}

static int pb_block_inbound(const pcw_block_t *block)
{
    const unsigned char *value;
    size_t value_len;

    if (block->kind != PCW_KIND_PACKET || block->linktype != PB_REPLAY_REPORTS)
        return 0;
    if (pcw_option_find(block, PCW_OPT_EPB_FLAGS, &value, &value_len) != PCW_OK || value_len != 4)
        return 1;
    return (pcw_get_u32(block, value) & 0x3u) != PCW_EPB_OUTBOUND;
}

static int pb_replay_next(pb_replay_t *rp)
{
    int status;

    while ((status = pcw_reader_next(rp->reader, &rp->pending)) != PCW_END) {
        if (status == PCW_OK && pb_block_inbound(rp->pending))
            return PB_OK;
    }
    rp->pending = NULL;
    return PB_ERR_CLOSED;
}

static int pb_replay_wait(pb_replay_t *rp, int timeout_ms)
{
    uint64_t now = pb_now_ns();
    uint64_t at = pb_block_ns(rp->pending);
    uint64_t due = rp->wall_start + (at > rp->capture_start ? at - rp->capture_start : 0);
    uint64_t wait_ms = due > now ? (due - now + 999999u) / 1000000u : 0;

    if (timeout_ms >= 0 && wait_ms > (uint64_t)timeout_ms) {
        pb_sleep_ms((uint32_t)timeout_ms);
        return PB_ERR_TIMEOUT;
    }
    if (wait_ms > 0)
        pb_sleep_ms((uint32_t)wait_ms);
    return PB_OK;
}

static int pb_replay_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms)
{
    pb_replay_t *rp = (pb_replay_t *)src;
    int status;

    *out_len = 0;
    if (rp->pending == NULL && (status = pb_replay_next(rp)) != PB_OK)
        return status;
    if (!rp->started) {
        rp->started = 1;
        rp->wall_start = pb_now_ns();
        rp->capture_start = pb_block_ns(rp->pending);
    }
    status = pb_replay_wait(rp, timeout_ms);
    if (status != PB_OK)
        return status;
    *out_len = rp->pending->data_len < len ? rp->pending->data_len : len;
    memcpy(buf, rp->pending->data, *out_len);
    rp->pending = NULL;
    return PB_OK;
}

static int pb_replay_write(pb_source_t *src, const unsigned char *buf, size_t len)
{
    (void)buf;
    pb_log(PB_LOG_DEBUG, "%s: output report of %zu bytes dropped", src->info.uri, len);
    return PB_OK;
}

static int pb_replay_descriptor(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len)
{
    pb_replay_t *rp = (pb_replay_t *)src;

    *out_len = rp->desc_len;
    if (rp->desc_len == 0)
        return PB_ERR_NOT_FOUND;
    if (len < rp->desc_len)
        return PB_ERR_ARG;
    memcpy(buf, rp->desc, rp->desc_len);
    return PB_OK;
}

static void pb_replay_close(pb_source_t *src)
{
    pb_replay_t *rp = (pb_replay_t *)src;

    pcw_reader_destroy(rp->reader);
    free(rp->file);
    free(rp);
}

static const pb_source_ops_t pb_replay_ops = {pb_replay_read, pb_replay_write, pb_replay_descriptor, pb_replay_close};

static int pb_replay_load(pb_replay_t *rp, const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;

    if (file == NULL)
        return PB_ERR_NOT_FOUND;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || size > PB_REPLAY_MAX_FILE || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return PB_ERR_IO;
    }
    rp->file = malloc(size > 0 ? (size_t)size : 1);
    if (rp->file == NULL) {
        fclose(file);
        return PB_ERR_NOMEM;
    }
    rp->file_len = fread(rp->file, 1, (size_t)size, file);
    fclose(file);
    return rp->file_len == (size_t)size ? PB_OK : PB_ERR_IO;
}

static void pb_replay_scan_block(pb_replay_t *rp, const pcw_block_t *block)
{
    const unsigned char *value;
    size_t value_len;

    if (block->kind == PCW_KIND_INTERFACE && block->linktype == PB_REPLAY_REPORTS && rp->base.info.name[0] == '\0'
        && pcw_option_find(block, PCW_OPT_IF_NAME, &value, &value_len) == PCW_OK) {
        snprintf(rp->base.info.name, sizeof(rp->base.info.name), "%.*s", (int)value_len, (const char *)value);
    }
    if (block->kind == PCW_KIND_PACKET && block->linktype == PB_REPLAY_DESCRIPTOR && rp->desc_len == 0 && block->data_len <= sizeof(rp->desc)) {
        memcpy(rp->desc, block->data, block->data_len);
        rp->desc_len = block->data_len;
    }
}

static int pb_replay_open_reader(pb_replay_t *rp, pcw_reader_t **out)
{
    if (pcw_reader_create(NULL, NULL, NULL, out) != PCW_OK)
        return PB_ERR_NOMEM;
    return pcw_reader_open_mem(*out, rp->file, rp->file_len) == PCW_OK ? PB_OK : PB_ERR_FORMAT;
}

static int pb_replay_scan(pb_replay_t *rp)
{
    pcw_reader_t *reader = NULL;
    const pcw_block_t *block;
    size_t inbound = 0;
    int read_status;
    int status = pb_replay_open_reader(rp, &reader);

    while (status == PB_OK && (read_status = pcw_reader_next(reader, &block)) != PCW_END) {
        if (read_status != PCW_OK)
            continue;
        pb_replay_scan_block(rp, block);
        inbound += (size_t)pb_block_inbound(block);
    }
    if (status == PB_OK && pcw_reader_error(reader) != PCW_OK)
        status = PB_ERR_FORMAT;
    pcw_reader_destroy(reader);
    if (status == PB_OK && inbound == 0)
        status = PB_ERR_FORMAT;
    return status;
}

int pb_replay_open(const char *path, pb_source_t **out)
{
    pb_replay_t *rp;
    int status;

    if (path == NULL || out == NULL || path[0] == '\0')
        return PB_ERR_ARG;
    *out = NULL;
    rp = calloc(1, sizeof(*rp));
    if (rp == NULL)
        return PB_ERR_NOMEM;
    rp->base.ops = &pb_replay_ops;
    rp->base.info.bus = PB_BUS_REPLAY;
    pb_hid_set_uri(&rp->base.info, "replay", path);
    status = pb_replay_load(rp, path);
    if (status == PB_OK)
        status = pb_replay_scan(rp);
    if (status == PB_OK)
        status = pb_replay_open_reader(rp, &rp->reader);
    if (status != PB_OK) {
        pb_log(PB_LOG_ERROR, "%s: %s", path, status == PB_ERR_FORMAT ? "not a capture with input reports on link type USER0" : pb_strerror(status));
        pb_replay_close(&rp->base);
        return status;
    }
    *out = &rp->base;
    return PB_OK;
}
