#include <ctype.h>
#include "bt_internal.h"

static int pb_hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

int pb_bt_address(const char *text, char norm[18], unsigned char raw[6])
{
    int i;

    if (text == NULL)
        return PB_ERR_ARG;
    for (i = 0; i < 6; i++) {
        const char *at = text + i * 3;
        int hi = pb_hex_digit((unsigned char)at[0]);
        int lo = hi < 0 ? -1 : pb_hex_digit((unsigned char)at[1]);

        if (lo < 0 || at[2] != (i == 5 ? '\0' : ':'))
            return PB_ERR_ARG;
        raw[5 - i] = (unsigned char)(hi << 4 | lo);
        norm[i * 3] = (char)toupper((unsigned char)at[0]);
        norm[i * 3 + 1] = (char)toupper((unsigned char)at[1]);
        norm[i * 3 + 2] = i == 5 ? '\0' : ':';
    }
    return PB_OK;
}
