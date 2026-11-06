#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <time.h>
#endif
#include "../../src/hook/hook.h"
#include "../../src/sink/sink.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_text_sink(void)
{
    char buf[512];
    pb_sink_t *sink;
    pb_state_t state;
    vpad_event_t event;
    FILE *stream = tmpfile();
    size_t n;

    CHECK(stream != NULL);
    if (stream == NULL)
        return;
    CHECK(pb_sink_open_text(stream, &sink) == PB_OK);
    pb_state_neutral(&state);
    state.buttons = (1u << VPAD_BTN_A) | (1u << VPAD_BTN_DPAD_LEFT);
    state.axes[VPAD_AXIS_LX] = -32768;
    state.axes[VPAD_AXIS_RT] = 255;
    CHECK(pb_sink_send(sink, &state) == PB_OK);
    CHECK(pb_sink_send(sink, &state) == PB_OK);
    CHECK(pb_sink_next_ff(sink, &event, 0) == PB_ERR_UNSUPPORTED);
    CHECK(pb_sink_wake(sink) == PB_OK);
    pb_sink_close(sink);
    rewind(stream);
    n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    CHECK(strcmp(buf, "state  buttons=a,left  lx=-32768  ly=0  rx=0  ry=0  lt=0  rt=255\n") == 0);
    fclose(stream);
}

static void test_vpad_sink(void)
{
    pb_sink_config_t config = {VPAD_XBOX360, 0, 0, "padbridge test", 0};
    pb_sink_t *sink;
    pb_state_t state;
    vpad_event_t event;
    int status = pb_sink_open_vpad(&config, &sink);

    if (status == PB_ERR_ACCESS || status == PB_ERR_NOT_FOUND || status == PB_ERR_UNSUPPORTED) {
        printf("vpad sink skipped: %s\n", pb_strerror(status));
        return;
    }
    CHECK(status == PB_OK);
    if (status != PB_OK)
        return;
    pb_state_neutral(&state);
    state.buttons = 1u << VPAD_BTN_B;
    state.axes[VPAD_AXIS_LY] = 1000;
    CHECK(pb_sink_send(sink, &state) == PB_OK);
    CHECK(pb_sink_next_ff(sink, &event, 10) == PB_ERR_TIMEOUT);
    CHECK(pb_sink_wake(sink) == PB_OK);
    CHECK(pb_sink_next_ff(sink, &event, -1) == PB_ERR_TIMEOUT);
    state.axes[VPAD_AXIS_LT] = 100000;
    CHECK(pb_sink_send(sink, &state) == PB_ERR_ARG);
    pb_sink_close(sink);
}

#if !defined(_WIN32)
static void pb_sleep_test(void)
{
    struct timespec ts = {0, 10000000L};

    nanosleep(&ts, NULL);
}
#endif

static void test_hook(void)
{
#if !defined(_WIN32)
    char path[] = "pb_hook_out";
    char cmd[256];
    char buf[256];
    FILE *f;
    int i;

    remove(path);
    snprintf(cmd, sizeof(cmd), "printf '%%s %%s %%s %%s' \"$1\" \"$2\" \"$3\" \"$PADBRIDGE_EVENT\" > %s.tmp && mv %s.tmp %s",
             path, path, path);
    CHECK(pb_hook_run(cmd, "connected", "AA:BB:CC:DD:EE:FF", "a b") == PB_OK);
    for (i = 0; i < 200 && (f = fopen(path, "r")) == NULL; i++)
        pb_sleep_test();
    CHECK(f != NULL);
    if (f == NULL)
        return;
    buf[fread(buf, 1, sizeof(buf) - 1, f)] = '\0';
    fclose(f);
    remove(path);
    CHECK(strcmp(buf, "connected AA:BB:CC:DD:EE:FF a b connected") == 0);
#endif
    CHECK(pb_hook_run("", "x", NULL, NULL) == PB_OK);
    CHECK(pb_hook_run(NULL, "x", NULL, NULL) == PB_ERR_ARG);
}

int main(void)
{
    test_text_sink();
    test_vpad_sink();
    test_hook();
    if (failures == 0)
        printf("ok\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
