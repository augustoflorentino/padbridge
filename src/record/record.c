#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wirefile/wirefile.h>
#include "../core/out.h"
#include "record.h"

#define PB_RECORD_APP "padbridge"
#define PB_RECORD_DESC_NAME "report descriptor"

struct pb_record {
    FILE *file;
    pcw_writer_t *writer;
    uint32_t reports;
    uint32_t descriptor;
    int has_descriptor;
};

static int pb_record_put(void *ctx, const unsigned char *data, size_t data_len)
{
    return fwrite(data, 1, data_len, ctx) == data_len ? 0 : -1;
}

static int pb_record_status(int status)
{
    if (status == PCW_OK)
        return PB_OK;
    if (status == PCW_ERR_NOMEM)
        return PB_ERR_NOMEM;
    if (status == PCW_ERR_ARG)
        return PB_ERR_ARG;
    return PB_ERR_IO;
}

static int pb_record_start(pb_record_t *rec, const char *device_name)
{
    pcw_option_t app = {PCW_OPT_SHB_USERAPPL, PB_RECORD_APP, strlen(PB_RECORD_APP)};
    pcw_option_t name = {PCW_OPT_IF_NAME, device_name, device_name ? strlen(device_name) : 0};
    int status = pcw_writer_create(NULL, NULL, NULL, &rec->writer);

    if (status == PCW_OK)
        status = pcw_writer_open(rec->writer, PCW_FORMAT_PCAPNG, pb_record_put, rec->file);
    if (status == PCW_OK)
        status = pcw_writer_section(rec->writer, &app, 1);
    if (status == PCW_OK)
        status = pcw_writer_add_interface(rec->writer, PCW_LINKTYPE_USER0, 0, PCW_TSRESOL_NANO, &name, device_name && device_name[0] ? 1 : 0, &rec->reports);
    return pb_record_status(status);
}

int pb_record_open(const char *path, const char *device_name, pb_record_t **out)
{
    pb_record_t *rec;
    int status;

    if (path == NULL || out == NULL)
        return PB_ERR_ARG;
    *out = NULL;
    rec = calloc(1, sizeof(*rec));
    if (rec == NULL)
        return PB_ERR_NOMEM;
    rec->file = fopen(path, "wb");
    if (rec->file == NULL) {
        pb_log(PB_LOG_ERROR, "cannot create %s", path);
        free(rec);
        return PB_ERR_IO;
    }
    status = pb_record_start(rec, device_name);
    if (status != PB_OK) {
        pb_record_close(rec);
        return status;
    }
    *out = rec;
    return PB_OK;
}

int pb_record_descriptor(pb_record_t *rec, const unsigned char *desc, size_t len)
{
    pcw_option_t name = {PCW_OPT_IF_NAME, PB_RECORD_DESC_NAME, strlen(PB_RECORD_DESC_NAME)};
    int status;

    if (rec == NULL || desc == NULL || len == 0)
        return PB_ERR_ARG;
    if (!rec->has_descriptor) {
        status = pcw_writer_add_interface(rec->writer, PCW_LINKTYPE_USER0 + 1, 0, PCW_TSRESOL_NANO, &name, 1, &rec->descriptor);
        if (status != PCW_OK)
            return pb_record_status(status);
        rec->has_descriptor = 1;
    }
    return pb_record_status(pcw_writer_packet(rec->writer, rec->descriptor, 0, 0, desc, len, len, NULL, 0));
}

int pb_record_packet(pb_record_t *rec, unsigned dir, const unsigned char *buf, size_t len, uint64_t ts_ns)
{
    uint32_t flags = dir == PB_DIR_OUT ? PCW_EPB_OUTBOUND : PCW_EPB_INBOUND;
    pcw_option_t direction = {PCW_OPT_EPB_FLAGS, &flags, sizeof(flags)};

    if (rec == NULL || (buf == NULL && len > 0) || (dir != PB_DIR_IN && dir != PB_DIR_OUT))
        return PB_ERR_ARG;
    return pb_record_status(pcw_writer_packet(rec->writer, rec->reports, (int64_t)(ts_ns / 1000000000u), (uint32_t)(ts_ns % 1000000000u), buf, len, len, &direction, 1));
}

void pb_record_close(pb_record_t *rec)
{
    if (rec == NULL)
        return;
    pcw_writer_destroy(rec->writer);
    if (rec->file != NULL && fclose(rec->file) != 0)
        pb_log(PB_LOG_ERROR, "capture file was not written completely");
    free(rec);
}
