#include <stdio.h>
#include <string.h>
#include "../cli/commands.h"
#include "../core/bridge.h"
#include "../core/out.h"
#include "../core/sys.h"
#include "../record/record.h"
#include "hid_internal.h"

#define PB_RECORD_POLL_MS 200

static const char *pb_bus_name(unsigned bus)
{
    if (bus == PB_BUS_USB)
        return "usb";
    if (bus == PB_BUS_BLUETOOTH)
        return "bluetooth";
    if (bus == PB_BUS_REPLAY)
        return "replay";
    return "unknown";
}

static int pb_list_row(void *ctx, const pb_source_info_t *info)
{
    static const char *const keys[] = {"uri", "id", "bus", "name"};
    const char *values[4];
    char id[16];

    (void)ctx;
    snprintf(id, sizeof(id), "%04x:%04x", info->vendor, info->product);
    values[0] = info->uri;
    values[1] = id;
    values[2] = pb_bus_name(info->bus);
    values[3] = info->name;
    pb_out_row("controller", 4, keys, values);
    return PB_OK;
}

int pb_cmd_list(int argc, char **argv)
{
    if (argc > 1) {
        fprintf(stderr, "usage: padbridge %s\n", argv[0]);
        return PB_EXIT_USAGE;
    }
    return pb_exit_code(pb_hid_list(pb_list_row, NULL));
}

static int pb_record_descriptor_of(pb_source_t *src, pb_record_t *rec)
{
    unsigned char desc[PB_HID_DESC_MAX];
    size_t len = 0;
    int r = pb_source_descriptor(src, desc, sizeof(desc), &len);

    if (r == PB_ERR_NOT_FOUND || len == 0) {
        pb_log(PB_LOG_WARN, "%s has no report descriptor; capture will not carry one", src->info.uri);
        return PB_OK;
    }
    return r == PB_OK ? pb_record_descriptor(rec, desc, len) : r;
}

static int pb_record_loop(pb_source_t *src, pb_record_t *rec, unsigned long *out_reports)
{
    unsigned char buf[PB_REPORT_MAX];
    size_t len;
    int r = PB_OK;

    while (!pb_interrupted()) {
        r = pb_source_read(src, buf, sizeof(buf), &len, PB_RECORD_POLL_MS);
        if (r == PB_ERR_TIMEOUT)
            continue;
        if (r != PB_OK)
            break;
        r = pb_record_packet(rec, PB_DIR_IN, buf, len, pb_now_ns());
        if (r != PB_OK)
            break;
        ++*out_reports;
    }
    return r == PB_ERR_CLOSED || r == PB_ERR_TIMEOUT ? PB_OK : r;
}

static void pb_record_summary(const char *file, unsigned long reports)
{
    static const char *const keys[] = {"file", "reports"};
    const char *values[2];
    char count[24];

    snprintf(count, sizeof(count), "%lu", reports);
    values[0] = file;
    values[1] = count;
    pb_out_row("recorded", 2, keys, values);
}

int pb_cmd_record(int argc, char **argv)
{
    pb_source_t *src = NULL;
    pb_record_t *rec = NULL;
    unsigned long reports = 0;
    int r;

    if (argc != 3) {
        fprintf(stderr, "usage: padbridge %s URI FILE\n", argv[0]);
        return PB_EXIT_USAGE;
    }
    r = pb_source_open(argv[1], &src);
    if (r == PB_OK)
        r = pb_record_open(argv[2], src->info.name, &rec);
    if (r == PB_OK)
        r = pb_record_descriptor_of(src, rec);
    if (r == PB_OK) {
        pb_catch_interrupt();
        r = pb_record_loop(src, rec, &reports);
    }
    pb_record_close(rec);
    if (src != NULL)
        pb_source_close(src);
    if (rec != NULL)
        pb_record_summary(argv[2], reports);
    return pb_exit_code(r);
}

static int pb_replay_parse(int argc, char **argv, pb_bridge_options_t *options, const char **config, const char **file)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc)
            options->profile_id = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            options->record_path = argv[++i];
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            *config = argv[++i];
        else if (strcmp(argv[i], "--no-ff") == 0)
            options->no_ff = 1;
        else if (argv[i][0] == '-' || *file != NULL)
            return PB_ERR_ARG;
        else
            *file = argv[i];
    }
    return *file == NULL ? PB_ERR_ARG : PB_OK;
}

int pb_cmd_replay(int argc, char **argv)
{
    pb_bridge_options_t options;
    pb_conf_t conf;
    const char *config = NULL;
    const char *file = NULL;
    const char *targets[1];
    char target[sizeof(((pb_source_info_t *)0)->uri)];
    int r;

    memset(&options, 0, sizeof(options));
    if (pb_replay_parse(argc, argv, &options, &config, &file) != PB_OK) {
        fprintf(stderr, "usage: padbridge %s FILE [--profile ID] [--record FILE] [--no-ff] [--config FILE]\n", argv[0]);
        return PB_EXIT_USAGE;
    }
    r = pb_conf_load(config, &conf);
    if (r != PB_OK)
        return pb_exit_code(r);
    snprintf(target, sizeof(target), "replay:%s", file);
    targets[0] = target;
    options.conf = &conf;
    options.once = 1;
    pb_catch_interrupt();
    r = pb_bridge_run(&options, targets, 1);
    return pb_interrupted() && r == PB_OK ? PB_EXIT_OK : pb_exit_code(r);
}
