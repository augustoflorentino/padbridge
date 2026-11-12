#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../../src/bt/bt_internal.h"

static int failures;

int pb_hid_open(const char *path, pb_source_t **out)
{
    (void)path;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_replay_open(const char *path, pb_source_t **out)
{
    (void)path;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_address(void)
{
    static const unsigned char want[6] = {0xff, 0xee, 0x0d, 0x0c, 0xbb, 0xaa};
    unsigned char raw[6];
    char norm[18];

    CHECK(pb_bt_address("aa:bb:0c:0D:ee:FF", norm, raw) == PB_OK);
    CHECK(strcmp(norm, "AA:BB:0C:0D:EE:FF") == 0);
    CHECK(memcmp(raw, want, 6) == 0);
    CHECK(pb_bt_address("AA:BB:CC:DD:EE", norm, raw) == PB_ERR_ARG);
    CHECK(pb_bt_address("AA:BB:CC:DD:EE:FF:", norm, raw) == PB_ERR_ARG);
    CHECK(pb_bt_address("AA-BB-CC-DD-EE-FF", norm, raw) == PB_ERR_ARG);
    CHECK(pb_bt_address("AA:BB:CC:DD:EE:FG", norm, raw) == PB_ERR_ARG);
    CHECK(pb_bt_address(NULL, norm, raw) == PB_ERR_ARG);
}

static int test_pair(int fds[2])
{
    return socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0 ? 0 : -1;
}

static void test_frames(void)
{
    static const unsigned char input[] = {0xa1, 0x01, 0x10, 0x20};
    static const unsigned char other[] = {0xa3, 0x09};
    static const unsigned char handshake[] = {0x00};
    unsigned char buf[16];
    int control[2];
    int interrupt[2];
    pb_source_t *src;
    size_t len = 0;

    CHECK(test_pair(control) == 0 && test_pair(interrupt) == 0);
    CHECK(pb_l2cap_from_fds(control[0], interrupt[0], "AA:BB:CC:DD:EE:FF", &src) == PB_OK);
    CHECK(strcmp(pb_source_info(src)->uri, "bt:AA:BB:CC:DD:EE:FF") == 0);
    CHECK(pb_source_info(src)->bus == PB_BUS_BLUETOOTH);
    CHECK(pb_source_read(src, buf, sizeof(buf), &len, 20) == PB_ERR_TIMEOUT);
    CHECK(pb_l2cap_wait_report(src, 10) == PB_ERR_TIMEOUT);
    CHECK(write(control[1], handshake, sizeof(handshake)) == 1);
    CHECK(write(interrupt[1], other, sizeof(other)) == 2);
    CHECK(write(interrupt[1], input, sizeof(input)) == 4);
    CHECK(pb_l2cap_wait_report(src, 100) == PB_OK);
    CHECK(pb_source_read(src, buf, sizeof(buf), &len, 500) == PB_OK);
    CHECK(len == 3 && memcmp(buf, input + 1, 3) == 0);
    CHECK(write(interrupt[1], input, sizeof(input)) == 4);
    CHECK(pb_source_read(src, buf, 2, &len, 500) == PB_ERR_ARG);
    CHECK(pb_source_write(src, (const unsigned char *)"\x01\x80\x40", 3) == PB_OK);
    CHECK(read(interrupt[1], buf, sizeof(buf)) == 4);
    CHECK(memcmp(buf, "\xa2\x01\x80\x40", 4) == 0);
    CHECK(pb_source_descriptor(src, buf, sizeof(buf), &len) == PB_ERR_NOT_FOUND);
    close(control[1]);
    CHECK(pb_source_read(src, buf, sizeof(buf), &len, 500) == PB_ERR_CLOSED);
    close(interrupt[1]);
    CHECK(pb_source_write(src, buf, 1) == PB_ERR_CLOSED);
    pb_source_close(src);
}

static void test_no_radio(void)
{
    pb_source_t *src;

    CHECK(pb_l2cap_open("AA:BB:CC", &src) == PB_ERR_ARG);
    CHECK(pb_bt_connect("nope", 0, &src) == PB_ERR_ARG);
    CHECK(pb_bt_release("nope") == PB_ERR_ARG);
}

int main(void)
{
    test_address();
    test_frames();
    test_no_radio();
    if (failures == 0)
        printf("ok\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
