#ifndef PB_BRIDGE_H
#define PB_BRIDGE_H

#include "conf.h"

typedef struct pb_bridge_options {
    const pb_conf_t *conf;
    const char *profile_id;
    const char *record_path;
    int text_output;
    int no_ff;
    int once;
} pb_bridge_options_t;

int pb_bridge_target_uri(const pb_conf_t *conf, const char *target, char *uri, size_t len);
int pb_bridge_default_targets(const pb_conf_t *conf, char ***out_targets, size_t *out_count);
void pb_bridge_free_targets(char **targets, size_t count);
int pb_bridge_run(const pb_bridge_options_t *options, const char *const *targets, size_t count);

#endif
