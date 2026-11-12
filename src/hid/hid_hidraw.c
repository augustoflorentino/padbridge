#if defined(__linux__) || defined(__FreeBSD__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/hidraw.h>
#else
#include <dev/hid/hidraw.h>
#endif
#include "../core/out.h"
#include "hid_internal.h"

typedef struct pb_hidraw {
    pb_source_t base;
    int fd;
    int numbered;
    int read_only;
    size_t desc_len;
    unsigned char desc[PB_HID_DESC_MAX];
} pb_hidraw_t;

static int pb_hidraw_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms)
{
    pb_hidraw_t *dev = (pb_hidraw_t *)src;
    struct pollfd pfd = {dev->fd, POLLIN, 0};
    ssize_t got;
    int ready = poll(&pfd, 1, timeout_ms);

    *out_len = 0;
    if (ready == 0 || (ready < 0 && errno == EINTR))
        return PB_ERR_TIMEOUT;
    if (ready < 0)
        return pb_hid_errno(errno);
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
        return PB_ERR_CLOSED;
    got = read(dev->fd, buf, len);
    if (got < 0)
        return errno == EINTR || errno == EAGAIN ? PB_ERR_TIMEOUT : pb_hid_errno(errno);
    if (got == 0)
        return PB_ERR_CLOSED;
    *out_len = (size_t)got;
    return PB_OK;
}

static int pb_hidraw_write(pb_source_t *src, const unsigned char *buf, size_t len)
{
    pb_hidraw_t *dev = (pb_hidraw_t *)src;
    unsigned char framed[PB_REPORT_MAX + 1];
    const unsigned char *out = buf;
    size_t out_len = len;
    ssize_t put;

    if (dev->read_only)
        return PB_ERR_ACCESS;
    if (!dev->numbered) {
        if (len > PB_REPORT_MAX)
            return PB_ERR_ARG;
        framed[0] = 0;
        memcpy(framed + 1, buf, len);
        out = framed;
        out_len = len + 1;
    }
    put = write(dev->fd, out, out_len);
    if (put < 0)
        return pb_hid_errno(errno);
    return (size_t)put == out_len ? PB_OK : PB_ERR_IO;
}

static int pb_hidraw_descriptor(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len)
{
    pb_hidraw_t *dev = (pb_hidraw_t *)src;

    *out_len = dev->desc_len;
    if (len < dev->desc_len)
        return PB_ERR_ARG;
    memcpy(buf, dev->desc, dev->desc_len);
    return PB_OK;
}

static void pb_hidraw_close(pb_source_t *src)
{
    pb_hidraw_t *dev = (pb_hidraw_t *)src;

    close(dev->fd);
    free(dev);
}

static const pb_source_ops_t pb_hidraw_ops = {pb_hidraw_read, pb_hidraw_write, pb_hidraw_descriptor, pb_hidraw_close};

static int pb_hidraw_query(int fd, pb_source_info_t *info, unsigned char *desc, size_t *desc_len)
{
    struct hidraw_devinfo devinfo;
    struct hidraw_report_descriptor rdesc;
    char name[sizeof(info->name)] = {0};
    int size = 0;

    if (ioctl(fd, HIDIOCGRAWINFO, &devinfo) < 0)
        return pb_hid_errno(errno);
    info->vendor = (uint16_t)devinfo.vendor;
    info->product = (uint16_t)devinfo.product;
    info->bus = pb_hid_bus((unsigned)devinfo.bustype);
    if (ioctl(fd, HIDIOCGRAWNAME(sizeof(name) - 1), name) >= 0)
        pb_hid_copy_name(info, name);
    if (desc == NULL)
        return PB_OK;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 || size < 0 || size > PB_HID_DESC_MAX)
        return PB_ERR_IO;
    memset(&rdesc, 0, sizeof(rdesc));
    rdesc.size = (uint32_t)size;
    if (ioctl(fd, HIDIOCGRDESC, &rdesc) < 0)
        return pb_hid_errno(errno);
    memcpy(desc, rdesc.value, (size_t)size);
    *desc_len = (size_t)size;
    return PB_OK;
}

static int pb_hidraw_open_fd(const char *path, int *out_read_only)
{
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);

    *out_read_only = 0;
    if (fd >= 0 || (errno != EACCES && errno != EPERM))
        return fd;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0) {
        *out_read_only = 1;
        pb_log(PB_LOG_WARN, "%s opened read only: no write permission, output reports will fail", path);
    }
    return fd;
}

int pb_hid_open(const char *path, pb_source_t **out)
{
    pb_hidraw_t *dev;
    int status;

    if (path == NULL || out == NULL || path[0] == '\0')
        return PB_ERR_ARG;
    *out = NULL;
    dev = calloc(1, sizeof(*dev));
    if (dev == NULL)
        return PB_ERR_NOMEM;
    dev->fd = pb_hidraw_open_fd(path, &dev->read_only);
    if (dev->fd < 0) {
        status = pb_hid_errno(errno);
        pb_log(PB_LOG_ERROR, "cannot open %s: %s", path, strerror(errno));
        free(dev);
        return status;
    }
    status = pb_hidraw_query(dev->fd, &dev->base.info, dev->desc, &dev->desc_len);
    if (status != PB_OK) {
        pb_log(PB_LOG_ERROR, "%s is not a hidraw device", path);
        pb_hidraw_close(&dev->base);
        return status;
    }
    dev->base.ops = &pb_hidraw_ops;
    dev->numbered = pb_hid_numbered(dev->desc, dev->desc_len);
    pb_hid_set_uri(&dev->base.info, "hid", path);
    *out = &dev->base;
    return PB_OK;
}

const char *pb_hid_backend(void)
{
    return "hidraw";
}

#else

typedef int pb_hid_hidraw_unused;

#endif
