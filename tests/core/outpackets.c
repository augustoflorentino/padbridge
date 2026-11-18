#include <stdio.h>
#include <stdlib.h>
#include <wirefile/wirefile.h>

int main(int argc, char **argv)
{
    static unsigned char data[1 << 20];
    const pcw_block_t *b;
    const unsigned char *flags;
    size_t flags_len, len, i;
    pcw_reader_t *r;
    FILE *f;

    if (argc != 2 || (f = fopen(argv[1], "rb")) == NULL)
        return 1;
    len = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (pcw_reader_create(NULL, NULL, NULL, &r) != PCW_OK || pcw_reader_open_mem(r, data, len) != PCW_OK)
        return 1;
    while (pcw_reader_next(r, &b) == PCW_OK) {
        if (b->kind != PCW_KIND_PACKET || pcw_option_find(b, 2, &flags, &flags_len) != PCW_OK
            || flags_len < 1 || (flags[0] & 3) != 2)
            continue;
        for (i = 0; i < b->data_len; i++)
            printf("%02x%c", b->data[i], i + 1 < b->data_len ? ' ' : '\n');
    }
    pcw_reader_destroy(r);
    return 0;
}
