#include <stdio.h>
#include <stdlib.h>
#include "record/record.h"

static const unsigned char idle[] = {0x01, 0xff, 0xff, 0xff, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00};
static const unsigned char press_a[] = {0x01, 0xf7, 0xff, 0xff, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00};
static const unsigned char left_rt[] = {0x01, 0xff, 0xff, 0xf7, 0x00, 0x80, 0x80, 0x80, 0x00, 0xff};

int main(int argc, char **argv)
{
    const unsigned char *reports[] = {idle, press_a, idle, left_rt, idle};
    pb_record_t *rec;
    uint64_t t = 1000000000u;
    size_t i;

    size_t idle_count = argc == 3 ? (size_t)atoi(argv[2]) : 0;

    if (argc < 2 || pb_record_open(argv[1], "Ninja Sword", &rec) != PB_OK)
        return 1;
    for (i = 0; i < sizeof(reports) / sizeof(reports[0]); i++, t += 20000000u)
        if (pb_record_packet(rec, PB_DIR_IN, reports[i], sizeof(idle), t) != PB_OK)
            return 1;
    for (i = 0; i < idle_count; i++, t += 20000000u)
        if (pb_record_packet(rec, PB_DIR_IN, idle, sizeof(idle), t) != PB_OK)
            return 1;
    pb_record_close(rec);
    return 0;
}
