#ifdef __FreeBSD__

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <dev/hid/hidraw.h>
#include "../core/out.h"
#include "hid_internal.h"

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
        nodes[count] = strdup(entry->d_name);
        if (nodes[count] == NULL)
            break;
        count++;
    }
    qsort(nodes, count, sizeof(*nodes), pb_compare_nodes);
    *out_nodes = nodes;
    return count;
}

static int pb_report_node(const char *node, pb_source_list_fn fn, void *ctx)
{
    pb_source_info_t info;
    struct hidraw_devinfo devinfo;
    char path[64];
    char name[sizeof(info.name)] = {0};
    int fd;

    snprintf(path, sizeof(path), "/dev/%s", node);
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        pb_log(PB_LOG_INFO, "%s skipped: cannot open it to read its identity", path);
        return PB_OK;
    }
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, HIDIOCGRAWINFO, &devinfo) == 0) {
        info.vendor = (uint16_t)devinfo.vendor;
        info.product = (uint16_t)devinfo.product;
        info.bus = pb_hid_bus((unsigned)devinfo.bustype);
    }
    if (ioctl(fd, HIDIOCGRAWNAME(sizeof(name) - 1), name) >= 0)
        pb_hid_copy_name(&info, name);
    close(fd);
    pb_hid_set_uri(&info, "hid", path);
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
    dir = opendir("/dev");
    if (dir == NULL)
        return PB_ERR_IO;
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

typedef int pb_hid_list_bsd_unused;

#endif
