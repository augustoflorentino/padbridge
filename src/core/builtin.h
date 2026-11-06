#ifndef PB_BUILTIN_H
#define PB_BUILTIN_H

#include <stddef.h>

typedef struct pb_builtin {
    const char *name;
    const char *text;
    size_t len;
} pb_builtin_t;

extern const pb_builtin_t pb_builtin_profiles[];

#endif
