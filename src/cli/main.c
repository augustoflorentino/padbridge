#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/out.h"
#include "../core/pb.h"
#include "commands.h"

static const pb_command_t commands[] = {
    {"list", "list", "controllers attached to this system", pb_cmd_list},
    {"scan", "scan [--seconds N]", "Bluetooth controllers nearby, raw HID included", pb_cmd_scan},
    {"probe", "probe ADDRESS|URI", "what a controller exposes and which profile fits", pb_cmd_probe},
    {"learn", "learn URI [-o FILE]", "build a profile by pressing what is asked", pb_cmd_learn},
    {"run", "run [URI|ADDRESS|PROFILE]...", "bridge controllers to virtual pads", pb_cmd_run},
    {"monitor", "monitor URI", "print decoded input as text", pb_cmd_monitor},
    {"rumble", "rumble URI STRONG WEAK [MS]", "drive the motors of a controller", pb_cmd_rumble},
    {"record", "record URI FILE", "capture raw reports to pcapng", pb_cmd_record},
    {"replay", "replay FILE [run options]", "feed a capture through the bridge", pb_cmd_replay},
    {"status", "status", "state of the running bridge", pb_cmd_status},
    {"release", "release ADDRESS", "let a controller go, the bridge keeps running", pb_cmd_release},
    {"stop", "stop", "stop the running bridge", pb_cmd_stop},
    {"profiles", "profiles [show ID]", "built-in and user profiles", pb_cmd_profiles},
    {"backends", "backends", "what this system supports", pb_cmd_backends},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void usage(FILE *stream)
{
    size_t i;

    fprintf(stream, "usage: padbridge [--script] [-v|-q] COMMAND [ARGS]\n\ncommands:\n");
    for (i = 0; i < COMMAND_COUNT; i++)
        fprintf(stream, "  %-34s %s\n", commands[i].usage, commands[i].summary);
}

static const pb_command_t *find_command(const char *name)
{
    size_t i;

    for (i = 0; i < COMMAND_COUNT; i++)
        if (strcmp(commands[i].name, name) == 0)
            return &commands[i];
    return NULL;
}

int main(int argc, char **argv)
{
    const pb_command_t *cmd;
    int script = 0;
    int level = PB_LOG_WARN;
    int i = 1;

    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "--script") == 0)
            script = 1;
        else if (strcmp(argv[i], "-v") == 0)
            level++;
        else if (strcmp(argv[i], "-q") == 0)
            level = PB_LOG_ERROR;
        else if (strcmp(argv[i], "--version") == 0) {
            printf("padbridge %s\n", PB_VERSION);
            return PB_EXIT_OK;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return PB_EXIT_OK;
        } else {
            usage(stderr);
            return PB_EXIT_USAGE;
        }
    }
    if (i >= argc) {
        usage(stderr);
        return PB_EXIT_USAGE;
    }
    cmd = find_command(argv[i]);
    if (cmd == NULL) {
        fprintf(stderr, "padbridge: unknown command '%s'\n", argv[i]);
        return PB_EXIT_USAGE;
    }
    pb_out_init(script, level > PB_LOG_DEBUG ? PB_LOG_DEBUG : level);
    return cmd->run(argc - i, argv + i);
}
