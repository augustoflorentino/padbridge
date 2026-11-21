#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core/pb.h"
#include "core/out.h"
#include "core/sys.h"
#include "record/record.h"
#include "hid_internal.h"
#include "hid_rebuild.h"
#include <hid-parser/hid-parser.h>

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

static const unsigned char numbered_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x85, 0x02, 0x09, 0x01, 0x75, 0x08, 0x95, 0x02,
    0x91, 0x02, 0xc0,
};

static const unsigned char plain_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xc0,
};

static void write_capture(const char *path)
{
    static const unsigned char a[] = {0x01, 0x11};
    static const unsigned char b[] = {0x02, 0xaa, 0xbb};
    static const unsigned char c[] = {0x01, 0x22};
    pb_record_t *rec;
    uint64_t t0 = 5000000000u;

    CHECK(pb_record_open(path, "Test Pad", &rec) == PB_OK);
    CHECK(pb_record_descriptor(rec, numbered_desc, sizeof(numbered_desc)) == PB_OK);
    CHECK(pb_record_packet(rec, PB_DIR_IN, a, sizeof(a), t0) == PB_OK);
    CHECK(pb_record_packet(rec, PB_DIR_OUT, b, sizeof(b), t0 + 10000000u) == PB_OK);
    CHECK(pb_record_packet(rec, PB_DIR_IN, c, sizeof(c), t0 + 80000000u) == PB_OK);
    CHECK(pb_record_packet(rec, 7, c, sizeof(c), t0) == PB_ERR_ARG);
    pb_record_close(rec);
}

static void check_replay_reads(pb_source_t *src)
{
    unsigned char buf[PB_REPORT_MAX];
    size_t len;
    uint64_t start;

    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 200) == PB_OK);
    CHECK(len == 2 && buf[0] == 0x01 && buf[1] == 0x11);
    start = pb_now_ns();
    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 10) == PB_ERR_TIMEOUT && len == 0);
    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 200) == PB_OK);
    CHECK(len == 2 && buf[1] == 0x22);
    CHECK(pb_now_ns() - start >= 60000000u);
    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 200) == PB_ERR_CLOSED);
}

static void test_replay(const char *dir)
{
    char path[512];
    char junk[512];
    unsigned char desc[PB_HID_DESC_MAX];
    size_t len;
    pb_source_t *src;
    FILE *file;

    snprintf(path, sizeof(path), "%s/roundtrip.pcapng", dir);
    snprintf(junk, sizeof(junk), "%s/junk.pcapng", dir);
    write_capture(path);
    CHECK(pb_replay_open(path, &src) == PB_OK);
    CHECK(strcmp(src->info.name, "Test Pad") == 0 && src->info.bus == PB_BUS_REPLAY);
    CHECK(strncmp(src->info.uri, "replay:", 7) == 0);
    CHECK(src->ops->descriptor(src, desc, sizeof(desc), &len) == PB_OK);
    CHECK(len == sizeof(numbered_desc) && memcmp(desc, numbered_desc, len) == 0);
    CHECK(src->ops->write(src, desc, 2) == PB_OK);
    check_replay_reads(src);
    src->ops->close(src);
    CHECK(pb_replay_open("/nonexistent/capture.pcapng", &src) == PB_ERR_NOT_FOUND);
    file = fopen(junk, "wb");
    CHECK(file != NULL && fputs("not a capture", file) >= 0 && fclose(file) == 0);
    CHECK(pb_replay_open(junk, &src) == PB_ERR_FORMAT);
    CHECK(pb_hid_numbered(numbered_desc, sizeof(numbered_desc)) == 1);
    CHECK(pb_hid_numbered(plain_desc, sizeof(plain_desc)) == 0);
}

static const hidp_field_t *find_field(const hidp_desc_t *desc, unsigned type, unsigned id, size_t offset)
{
    size_t i;

    for (i = 0; i < hidp_desc_field_count(desc); i++) {
        const hidp_field_t *f = hidp_desc_field(desc, i);

        if (f->report_type == type && f->report_id == id && f->bit_offset == offset)
            return f;
    }
    return NULL;
}

static void check_rebuilt(const hidp_desc_t *desc)
{
    const hidp_field_t *f;

    CHECK((f = find_field(desc, HIDP_INPUT, 1, 8)) != NULL && f->report_size == 1 && f->report_count == 8);
    CHECK(f->usage_page == 9 && f->usages_len == 1 && f->usages[0].min == 0x90001u && f->usages[0].max == 0x90008u);
    CHECK((f = find_field(desc, HIDP_INPUT, 1, 16)) != NULL && f->report_size == 4 && f->logical_max == 7);
    CHECK((f->flags & HIDP_FLAG_NULL_STATE) && f->physical_max == 315 && f->unit == 0x14);
    CHECK(hidp_desc_collection(desc, f->collection)->usage == 0x00010001u);
    CHECK((f = find_field(desc, HIDP_INPUT, 1, 20)) != NULL && (f->flags & HIDP_FLAG_CONSTANT) && f->report_size == 4);
    CHECK((f = find_field(desc, HIDP_INPUT, 1, 24)) != NULL && f->report_size == 8 && f->report_count == 2);
    CHECK(f->logical_min == 0 && f->logical_max == 255 && f->usages[0].min == 0x10030u && f->usages[0].max == 0x10031u);
    CHECK((f = find_field(desc, HIDP_OUTPUT, 2, 8)) != NULL && f->usage_page == 0x0f && f->report_count == 2);
    CHECK(hidp_desc_find_report(desc, HIDP_INPUT, 1)->byte_len == 5);
    CHECK(hidp_desc_find_report(desc, HIDP_OUTPUT, 2)->byte_len == 3);
    CHECK(hidp_desc_collection(desc, 0)->usage == 0x00010005u);
}

static void test_rebuild(void)
{
    static const pb_rebuild_collection_t collections[] = {{1, 5, 1, 0}, {1, 1, 0, 0}};
    pb_rebuild_field_t fields[] = {
        {HIDP_MAIN_INPUT, 1, 1, 16, 8, 2, 0x02, 1, 0x30, 0x31, 1, 0, -1, 0, 0, 0, 0},
        {HIDP_MAIN_OUTPUT, 2, 0, 8, 8, 1, 0x02, 0x0f, 0x97, 0x97, 0, 0, 255, 0, 0, 0, 0},
        {HIDP_MAIN_OUTPUT, 2, 0, 0, 8, 1, 0x02, 0x0f, 0x97, 0x97, 0, 0, 255, 0, 0, 0, 0},
        {HIDP_MAIN_INPUT, 1, 0, 0, 1, 8, 0x02, 9, 1, 8, 1, 0, 1, 0, 0, 0, 0},
        {HIDP_MAIN_INPUT, 1, 1, 8, 4, 1, 0x42, 1, 0x39, 0x39, 0, 0, 7, 0, 315, 0x14, 0},
        {HIDP_MAIN_INPUT, 1, 0, 0, 1, 8, 0x02, 9, 1, 8, 1, 0, 1, 0, 0, 0, 0},
    };
    unsigned char out[512];
    size_t len;
    size_t error_offset;
    hidp_desc_t *desc;

    CHECK(pb_rebuild_descriptor(collections, 2, fields, 6, out, sizeof(out), &len) == PB_OK);
    CHECK(hidp_desc_parse(out, len, NULL, &desc, &error_offset) == HIDP_OK);
    check_rebuilt(desc);
    hidp_desc_destroy(desc);
    CHECK(pb_rebuild_descriptor(collections, 2, fields, 6, out, 8, &len) == PB_ERR_FORMAT && len == 0);
}

#ifdef __linux__
#include <poll.h>
#include <linux/uhid.h>

#ifndef PB_TEST_PHYS
#define PB_TEST_PHYS "padbridge/uhid/test"
#endif

typedef struct find_ctx {
    const char *name;
    char uri[288];
} find_ctx_t;

static int find_by_name(void *ctx, const pb_source_info_t *info)
{
    find_ctx_t *find = ctx;

    if (strcmp(info->name, find->name) != 0)
        return PB_OK;
    snprintf(find->uri, sizeof(find->uri), "%s", info->uri);
    return PB_ERR_BUSY;
}

static int uhid_create(const char *name, const unsigned char *desc, size_t len)
{
    struct uhid_event ev;
    int fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);

    if (fd < 0)
        return -1;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE2;
    snprintf((char *)ev.u.create2.name, sizeof(ev.u.create2.name), "%s", name);
    snprintf((char *)ev.u.create2.phys, sizeof(ev.u.create2.phys), "%s", PB_TEST_PHYS);
    ev.u.create2.rd_size = (uint16_t)len;
    ev.u.create2.bus = 0x03;
    ev.u.create2.vendor = 0x1d6b;
    ev.u.create2.product = 0x7a01;
    memcpy(ev.u.create2.rd_data, desc, len);
    CHECK(write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev));
    return fd;
}

static void uhid_input(int fd, const unsigned char *data, size_t len)
{
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_INPUT2;
    ev.u.input2.size = (uint16_t)len;
    memcpy(ev.u.input2.data, data, len);
    CHECK(write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev));
}

static void uhid_expect_output(int fd, const unsigned char *data, size_t len)
{
    struct uhid_event ev;
    struct pollfd pfd = {fd, POLLIN, 0};
    int tries;

    for (tries = 0; tries < 20; tries++) {
        CHECK(poll(&pfd, 1, 1000) == 1);
        CHECK(read(fd, &ev, sizeof(ev)) > 0);
        if (ev.type != UHID_OUTPUT)
            continue;
        CHECK(ev.u.output.size == len && memcmp(ev.u.output.data, data, len) == 0);
        return;
    }
    CHECK(!"no UHID_OUTPUT");
}

static pb_source_t *open_fake(const char *name)
{
    find_ctx_t find = {name, {0}};
    pb_source_t *src = NULL;
    int status = PB_ERR_NOT_FOUND;
    int tries;

    for (tries = 0; tries < 100 && find.uri[0] == '\0'; tries++) {
        CHECK(pb_hid_list(find_by_name, &find) == (find.uri[0] ? PB_ERR_BUSY : PB_OK));
        if (find.uri[0] == '\0')
            pb_sleep_ms(20);
    }
    CHECK(find.uri[0] != '\0');
    for (tries = 0; tries < 100 && (status = pb_hid_open(find.uri + 4, &src)) != PB_OK; tries++)
        pb_sleep_ms(20);
    if (status == PB_ERR_ACCESS) {
        fprintf(stderr, "skip: no udev rule grants access to %s\n", find.uri);
        exit(77);
    }
    CHECK(src != NULL);
    CHECK(strcmp(src->info.uri, find.uri) == 0);
    return src;
}

static void check_fake_info(pb_source_t *src, const unsigned char *desc, size_t desc_len)
{
    unsigned char got[PB_HID_DESC_MAX];
    size_t len;

    CHECK(src->info.vendor == 0x1d6b && src->info.product == 0x7a01 && src->info.bus == PB_BUS_USB);
    CHECK(src->ops->descriptor(src, got, sizeof(got), &len) == PB_OK);
    CHECK(len == desc_len && memcmp(got, desc, len) == 0);
}

static void check_fake_io(int fd, pb_source_t *src, const unsigned char *report, size_t report_len)
{
    unsigned char buf[PB_REPORT_MAX];
    size_t len;

    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 50) == PB_ERR_TIMEOUT);
    uhid_input(fd, report, report_len);
    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 1000) == PB_OK);
    CHECK(len == report_len && memcmp(buf, report, len) == 0);
}

static void test_numbered(void)
{
    static const unsigned char in[] = {0x01, 0x5a};
    static const unsigned char out[] = {0x02, 0x10, 0x20};
    int fd = uhid_create("padbridge test numbered", numbered_desc, sizeof(numbered_desc));
    pb_source_t *src = open_fake("padbridge test numbered");
    struct uhid_event ev = {.type = UHID_DESTROY};
    unsigned char buf[PB_REPORT_MAX];
    size_t len;

    check_fake_info(src, numbered_desc, sizeof(numbered_desc));
    check_fake_io(fd, src, in, sizeof(in));
    CHECK(src->ops->write(src, out, sizeof(out)) == PB_OK);
    uhid_expect_output(fd, out, sizeof(out));
    CHECK(write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev));
    CHECK(src->ops->read(src, buf, sizeof(buf), &len, 1000) == PB_ERR_CLOSED);
    src->ops->close(src);
    close(fd);
}

static void test_plain(void)
{
    static const unsigned char in[] = {0x81};
    int fd = uhid_create("padbridge test plain", plain_desc, sizeof(plain_desc));
    pb_source_t *src = open_fake("padbridge test plain");

    check_fake_info(src, plain_desc, sizeof(plain_desc));
    check_fake_io(fd, src, in, sizeof(in));
    src->ops->close(src);
    close(fd);
}

static int test_uhid(void)
{
    if (access("/dev/uhid", R_OK | W_OK) != 0) {
        fprintf(stderr, "skip: /dev/uhid not accessible\n");
        return 77;
    }
    CHECK(pb_hid_open("/dev/nonexistent-hidraw", &(pb_source_t *){NULL}) == PB_ERR_NOT_FOUND);
    test_numbered();
    test_plain();
    return 0;
}
#else
static int test_uhid(void)
{
    return 77;
}
#endif

int main(int argc, char **argv)
{
    pb_out_init(0, PB_LOG_ERROR - 1);
    if (argc == 3 && strcmp(argv[1], "replay") == 0) {
        test_replay(argv[2]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "rebuild") == 0) {
        test_rebuild();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "uhid") == 0)
        return test_uhid();
    return 2;
}
