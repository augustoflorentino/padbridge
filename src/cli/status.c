#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/conf.h"
#include "../core/out.h"
#include "../core/paths.h"
#include "../core/sys.h"
#include "commands.h"

#define PB_STOP_WAIT_MS 5000

static int pb_load_conf(int argc, char **argv, pb_conf_t *conf)
{
    if (argc == 3 && strcmp(argv[1], "--config") == 0)
        return pb_conf_load(argv[2], conf);
    if (argc != 1)
        return PB_ERR_ARG;
    return pb_conf_load(NULL, conf);
}

static void pb_print_state_line(char *line)
{
    static const char *const keys[] = {"name", "status", "since", "detail"};
    const char *values[4] = {"", "", "", ""};
    char *tab;
    int i;

    values[0] = line;
    for (i = 1; i < 4 && (tab = strchr(line, '\t')) != NULL; i++) {
        *tab = '\0';
        line = tab + 1;
        values[i] = line;
    }
    pb_out_row("controller", 4, keys, values);
}

int pb_cmd_status(int argc, char **argv)
{
    static const char *const keys[] = {"running", "pid"};
    pb_conf_t conf;
    char pid_text[24];
    const char *values[2];
    char *text, *line, *next;
    size_t len;
    long owner;
    int r = pb_load_conf(argc, argv, &conf);

    if (r != PB_OK)
        return pb_exit_code(r);
    owner = pb_state_owner(&conf);
    snprintf(pid_text, sizeof(pid_text), "%ld", owner);
    values[0] = owner ? "yes" : "no";
    values[1] = pid_text;
    pb_out_row("bridge", 2, keys, values);
    if (owner == 0 || pb_read_file(conf.state_path, &text, &len) != PB_OK)
        return owner ? PB_EXIT_OK : PB_EXIT_NOT_FOUND;
    for (line = text; line != NULL && *line != '\0'; line = next) {
        next = strchr(line, '\n');
        if (next != NULL)
            *next++ = '\0';
        if (*line != '\0')
            pb_print_state_line(line);
    }
    free(text);
    return PB_EXIT_OK;
}

int pb_cmd_stop(int argc, char **argv)
{
    pb_conf_t conf;
    uint32_t start;
    long owner;
    int r = pb_load_conf(argc, argv, &conf);

    if (r != PB_OK)
        return pb_exit_code(r);
    owner = pb_state_owner(&conf);
    if (owner == 0) {
        pb_log(PB_LOG_WARN, "no bridge running");
        return PB_EXIT_NOT_FOUND;
    }
    r = pb_process_signal_stop(owner);
    if (r != PB_OK)
        return pb_exit_code(r);
    start = pb_now_ms();
    while (pb_process_alive(owner) && pb_now_ms() - start < PB_STOP_WAIT_MS)
        pb_sleep_ms(100);
    if (!pb_process_alive(owner))
        return PB_EXIT_OK;
    pb_log(PB_LOG_ERROR, "bridge %ld did not stop", owner);
    return PB_EXIT_FAIL;
}
