#include <stdarg.h>
#include <stdio.h>
#include "out.h"

static int out_script;
static int out_level = PB_LOG_WARN;

void pb_out_init(int script, int log_level)
{
    out_script = script;
    out_level = log_level;
}

int pb_out_script(void)
{
    return out_script;
}

void pb_out_row(const char *kind, size_t count, const char *const *keys, const char *const *values)
{
    size_t i;

    fputs(kind, stdout);
    for (i = 0; i < count; i++)
        printf("%s%s=%s", out_script ? "\t" : "  ", keys[i], values[i]);
    putchar('\n');
    fflush(stdout);
}

void pb_log(int level, const char *fmt, ...)
{
    static const char *const names[] = {"error", "warning", "info", "debug"};
    va_list args;

    if (level > out_level)
        return;
    fprintf(stderr, "padbridge: %s: ", names[level]);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}
