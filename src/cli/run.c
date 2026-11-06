#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/bridge.h"
#include "../core/out.h"
#include "../core/sys.h"
#include "commands.h"

typedef struct pb_run_args {
    const char *config;
    pb_bridge_options_t options;
    const char **targets;
    size_t count;
} pb_run_args_t;

static int pb_run_parse(int argc, char **argv, pb_run_args_t *a)
{
    int i;

    a->targets = calloc((size_t)argc, sizeof(*a->targets));
    if (a->targets == NULL)
        return PB_ERR_NOMEM;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc)
            a->options.profile_id = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            a->options.record_path = argv[++i];
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            a->config = argv[++i];
        else if (strcmp(argv[i], "--no-ff") == 0)
            a->options.no_ff = 1;
        else if (argv[i][0] == '-')
            return PB_ERR_ARG;
        else
            a->targets[a->count++] = argv[i];
    }
    return PB_OK;
}

static int pb_run_targets(pb_run_args_t *a, char ***owned, size_t *owned_count)
{
    int r;

    *owned = NULL;
    *owned_count = 0;
    if (a->count > 0)
        return PB_OK;
    r = pb_bridge_default_targets(a->options.conf, owned, owned_count);
    if (r != PB_OK) {
        pb_log(PB_LOG_ERROR, "no controller given and none known: %s", pb_strerror(r));
        return r;
    }
    free(a->targets);
    a->targets = (const char **)*owned;
    a->count = *owned_count;
    return PB_OK;
}

static int pb_bridge_command(int argc, char **argv, int text, int once, int claim)
{
    pb_run_args_t a;
    pb_conf_t conf;
    char **owned = NULL;
    size_t owned_count = 0;
    long owner = 0;
    int r;

    memset(&a, 0, sizeof(a));
    r = pb_run_parse(argc, argv, &a);
    if (r == PB_OK)
        r = pb_conf_load(a.config, &conf);
    a.options.conf = &conf;
    a.options.text_output = text;
    a.options.once = once;
    if (r == PB_OK)
        r = pb_run_targets(&a, &owned, &owned_count);
    if (r == PB_OK && claim && (r = pb_state_claim(&conf, &owner)) == PB_ERR_BUSY)
        pb_log(PB_LOG_ERROR, "a bridge is already running (pid %ld)", owner);
    if (r == PB_OK) {
        pb_catch_interrupt();
        r = pb_bridge_run(&a.options, a.targets, a.count);
        if (claim)
            pb_state_release(&conf);
    }
    if (owned != NULL)
        pb_bridge_free_targets(owned, owned_count);
    else
        free(a.targets);
    if (r == PB_ERR_ARG)
        fprintf(stderr, "usage: padbridge %s [--profile ID] [--record FILE] [--no-ff] [--config FILE] [TARGET]...\n",
                argv[0]);
    return pb_interrupted() && r == PB_OK ? PB_EXIT_OK : pb_exit_code(r);
}

int pb_cmd_run(int argc, char **argv)
{
    return pb_bridge_command(argc, argv, 0, 0, 1);
}

int pb_cmd_monitor(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: padbridge monitor [--profile ID] TARGET\n");
        return PB_EXIT_USAGE;
    }
    return pb_bridge_command(argc, argv, 1, 1, 0);
}
