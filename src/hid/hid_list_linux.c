#ifdef __linux__

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/out.h"
#include "hid_internal.h"

#define PB_SYS_HIDRAW "/sys/class/hidraw"

static void pb_uevent_line(pb_source_info_t *info, const char *line)
{
    unsigned bus;
    unsigned vendor;
    unsigned product;

    if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &vendor, &product) == 3) {
        info->bus = pb_hid_bus(bus);
        info->vendor = (uint16_t)vendor;
        info->product = (uint16_t)product;
    } else if (strncmp(line, "HID_NAME=", 9) == 0) {
        pb_hid_copy_name(info, line + 9);
    }
}

static int pb_read_uevent(const char *node, pb_source_info_t *info)
{
    char path[512];
    char line[256];
    FILE *file;

    snprintf(path, sizeof(path), PB_SYS_HIDRAW "/%s/device/uevent", node);
    file = fopen(path, "r");
    if (file == NULL)
        return PB_ERR_NOT_FOUND;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        pb_uevent_line(info, line);
    }
    fclose(file);
    return PB_OK;
}

static int pb_node_number(const char *name)
{
    return atoi(name + strlen("hidraw"));
}

static int pb_compare_nodes(const void *a, const void *b)
{
    return pb_node_number(*(char *const *)a) - pb_node_number(*(char *const *)b);
}

static size_t pb_collect_nodes(DIR *dir, char ***out_nodes)
{
    struct dirent *entry;
    char **nodes = NULL;
    char **grown;
    size_t count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0)
            continue;
        grown = realloc(nodes, (count + 1) * sizeof(*nodes));
        if (grown == NULL)
            break;
        nodes = grown;
        nodes[count] = malloc(strlen(entry->d_name) + 1);
        if (nodes[count] == NULL)
            break;
        strcpy(nodes[count++], entry->d_name);
    }
    qsort(nodes, count, sizeof(*nodes), pb_compare_nodes);
    *out_nodes = nodes;
    return count;
}

static int pb_report_node(const char *node, pb_source_list_fn fn, void *ctx)
{
    pb_source_info_t info;
    char dev_path[64];

    memset(&info, 0, sizeof(info));
    if (pb_read_uevent(node, &info) != PB_OK)
        return PB_OK;
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", node);
    pb_hid_set_uri(&info, "hid", dev_path);
    return fn(ctx, &info);
}

int pb_hid_list(pb_source_list_fn fn, void *ctx)
{
    DIR *dir;
    char **nodes;
    size_t count;
    size_t i;
    int status = PB_OK;

    if (fn == NULL)
        return PB_ERR_ARG;
    dir = opendir(PB_SYS_HIDRAW);
    if (dir == NULL) {
        pb_log(PB_LOG_INFO, "no %s: the kernel has no hidraw devices", PB_SYS_HIDRAW);
        return PB_OK;
    }
    count = pb_collect_nodes(dir, &nodes);
    closedir(dir);
    for (i = 0; i < count; i++) {
        if (status == PB_OK)
            status = pb_report_node(nodes[i], fn, ctx);
        free(nodes[i]);
    }
    free(nodes);
    return status;
}

#else

typedef int pb_hid_list_linux_unused;

#endif
