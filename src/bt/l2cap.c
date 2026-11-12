#include "bt_internal.h"

#if defined(__linux__) || defined(__FreeBSD__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../core/sys.h"

#define PB_HDR_INPUT 0xa1
#define PB_HDR_OUTPUT 0xa2

#if defined(__linux__)
#define PB_AF_BLUETOOTH 31
#define PB_PROTO_L2CAP 0

struct pb_sockaddr_l2 {
    sa_family_t family;
    unsigned char psm[2];
    unsigned char bdaddr[6];
    unsigned char cid[2];
    unsigned char type;
};
#else
#define PB_AF_BLUETOOTH 36
#define PB_PROTO_L2CAP 132

struct pb_sockaddr_l2 {
    unsigned char len;
    unsigned char family;
    unsigned char psm[2];
    unsigned char bdaddr[6];
    unsigned char cid[2];
    unsigned char type;
};
#endif

typedef struct pb_l2cap {
    pb_source_t base;
    int control;
    int interrupt;
} pb_l2cap_t;

static int pb_from_errno(int err)
{
    switch (err) {
    case EAFNOSUPPORT:
    case EPROTONOSUPPORT:
        return PB_ERR_UNSUPPORTED;
    case EACCES:
    case EPERM:
        return PB_ERR_ACCESS;
    case ETIMEDOUT:
        return PB_ERR_TIMEOUT;
    case EHOSTDOWN:
    case EHOSTUNREACH:
    case ECONNREFUSED:
        return PB_ERR_NOT_FOUND;
    case EBUSY:
    case EALREADY:
        return PB_ERR_BUSY;
    case ECONNRESET:
    case ENOTCONN:
    case EPIPE:
        return PB_ERR_CLOSED;
    default:
        return PB_ERR_IO;
    }
}

static int pb_remaining(uint32_t deadline, int timeout_ms)
{
    uint32_t now = pb_now_ms();

    if (timeout_ms < 0)
        return -1;
    return (int32_t)(deadline - now) > 0 ? (int)(deadline - now) : 0;
}

static int pb_connect_wait(int fd, int timeout_ms)
{
    struct pollfd pfd = {fd, POLLOUT, 0};
    int err = 0;
    socklen_t err_len = sizeof(err);
    int r;

    do
        r = poll(&pfd, 1, timeout_ms);
    while (r < 0 && errno == EINTR);
    if (r < 0)
        return pb_from_errno(errno);
    if (r == 0)
        return PB_ERR_TIMEOUT;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0)
        return pb_from_errno(errno);
    return err == 0 ? PB_OK : pb_from_errno(err);
}

static int pb_l2cap_connect(const unsigned char *bdaddr, unsigned psm, int *out_fd)
{
    struct pb_sockaddr_l2 sa;
    int fd = socket(PB_AF_BLUETOOTH, SOCK_SEQPACKET, PB_PROTO_L2CAP);
    int status = PB_OK;

    if (fd < 0)
        return pb_from_errno(errno);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    memset(&sa, 0, sizeof(sa));
#if defined(__FreeBSD__)
    sa.len = sizeof(sa);
#endif
    sa.family = PB_AF_BLUETOOTH;
    sa.psm[0] = (unsigned char)(psm & 0xff);
    sa.psm[1] = (unsigned char)(psm >> 8);
    memcpy(sa.bdaddr, bdaddr, sizeof(sa.bdaddr));
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        status = errno == EINPROGRESS ? pb_connect_wait(fd, PB_BT_CONNECT_MS) : pb_from_errno(errno);
    if (status != PB_OK) {
        close(fd);
        return status;
    }
    *out_fd = fd;
    return PB_OK;
}

static int pb_recv(int fd, unsigned char *buf, size_t len, size_t *out_len)
{
    ssize_t n;

    do
        n = recv(fd, buf, len, 0);
    while (n < 0 && errno == EINTR);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return PB_ERR_TIMEOUT;
    if (n < 0)
        return pb_from_errno(errno);
    if (n == 0)
        return PB_ERR_CLOSED;
    *out_len = (size_t)n;
    return PB_OK;
}

static int pb_l2cap_poll(pb_l2cap_t *self, struct pollfd *pfds, int timeout_ms)
{
    int r;

    pfds[0].fd = self->control;
    pfds[1].fd = self->interrupt;
    pfds[0].events = pfds[1].events = POLLIN;
    pfds[0].revents = pfds[1].revents = 0;
    r = poll(pfds, 2, timeout_ms);
    if (r < 0)
        return errno == EINTR ? PB_ERR_TIMEOUT : pb_from_errno(errno);
    return r == 0 ? PB_ERR_TIMEOUT : PB_OK;
}

static int pb_l2cap_take(pb_l2cap_t *self, struct pollfd *pfds, unsigned char *buf, size_t len,
                         size_t *out_len)
{
    unsigned char frame[PB_REPORT_MAX + 1];
    size_t n;
    int status;

    if (pfds[0].revents != 0) {
        status = pb_recv(self->control, frame, sizeof(frame), &n);
        if (status != PB_OK && status != PB_ERR_TIMEOUT)
            return status;
    }
    if (pfds[1].revents == 0)
        return PB_ERR_TIMEOUT;
    status = pb_recv(self->interrupt, frame, sizeof(frame), &n);
    if (status != PB_OK)
        return status;
    if (n < 2 || frame[0] != PB_HDR_INPUT)
        return PB_ERR_TIMEOUT;
    if (n - 1 > len)
        return PB_ERR_ARG;
    memcpy(buf, frame + 1, n - 1);
    *out_len = n - 1;
    return PB_OK;
}

static int pb_l2cap_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms)
{
    pb_l2cap_t *self = (pb_l2cap_t *)src;
    uint32_t deadline = pb_now_ms() + (uint32_t)(timeout_ms < 0 ? 0 : timeout_ms);
    struct pollfd pfds[2];
    int status;

    do {
        status = pb_l2cap_poll(self, pfds, pb_remaining(deadline, timeout_ms));
        if (status == PB_OK)
            status = pb_l2cap_take(self, pfds, buf, len, out_len);
        if (status != PB_ERR_TIMEOUT)
            return status;
    } while (pb_remaining(deadline, timeout_ms) != 0);
    return PB_ERR_TIMEOUT;
}

static int pb_l2cap_write(pb_source_t *src, const unsigned char *buf, size_t len)
{
    pb_l2cap_t *self = (pb_l2cap_t *)src;
    unsigned char frame[PB_REPORT_MAX + 1];
    struct pollfd pfd = {self->interrupt, POLLOUT, 0};
    ssize_t n;

    if (len > PB_REPORT_MAX)
        return PB_ERR_ARG;
    frame[0] = PB_HDR_OUTPUT;
    memcpy(frame + 1, buf, len);
    for (;;) {
        n = send(self->interrupt, frame, len + 1, MSG_NOSIGNAL);
        if (n == (ssize_t)(len + 1))
            return PB_OK;
        if (n >= 0)
            return PB_ERR_IO;
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return pb_from_errno(errno);
        if (poll(&pfd, 1, 200) <= 0)
            return PB_ERR_TIMEOUT;
    }
}

static void pb_l2cap_close(pb_source_t *src)
{
    pb_l2cap_t *self = (pb_l2cap_t *)src;

    close(self->interrupt);
    close(self->control);
    free(self);
}

static const pb_source_ops_t pb_l2cap_ops = {pb_l2cap_read, pb_l2cap_write, NULL, pb_l2cap_close};

int pb_l2cap_from_fds(int control, int interrupt, const char *address, pb_source_t **out)
{
    pb_l2cap_t *self = calloc(1, sizeof(*self));

    if (self == NULL)
        return PB_ERR_NOMEM;
    self->base.ops = &pb_l2cap_ops;
    self->control = control;
    self->interrupt = interrupt;
    snprintf(self->base.info.uri, sizeof(self->base.info.uri), "bt:%s", address);
    self->base.info.bus = PB_BUS_BLUETOOTH;
    *out = &self->base;
    return PB_OK;
}

int pb_l2cap_wait_report(pb_source_t *src, int timeout_ms)
{
    pb_l2cap_t *self = (pb_l2cap_t *)src;
    struct pollfd pfd = {self->interrupt, POLLIN, 0};
    int r;

    do
        r = poll(&pfd, 1, timeout_ms);
    while (r < 0 && errno == EINTR);
    if (r < 0)
        return pb_from_errno(errno);
    if (r == 0)
        return PB_ERR_TIMEOUT;
    return (pfd.revents & POLLIN) ? PB_OK : PB_ERR_CLOSED;
}

int pb_l2cap_open(const char *address, pb_source_t **out)
{
    unsigned char raw[6];
    char norm[18];
    int control;
    int interrupt;
    int status;

    if (out == NULL || pb_bt_address(address, norm, raw) != PB_OK)
        return PB_ERR_ARG;
    status = pb_l2cap_connect(raw, PB_PSM_CONTROL, &control);
    if (status != PB_OK)
        return status;
    status = pb_l2cap_connect(raw, PB_PSM_INTERRUPT, &interrupt);
    if (status != PB_OK) {
        close(control);
        return status;
    }
    status = pb_l2cap_from_fds(control, interrupt, norm, out);
    if (status != PB_OK) {
        close(interrupt);
        close(control);
    }
    return status;
}

#else

int pb_l2cap_wait_report(pb_source_t *src, int timeout_ms)
{
    (void)src;
    (void)timeout_ms;
    return PB_ERR_UNSUPPORTED;
}

int pb_l2cap_from_fds(int control, int interrupt, const char *address, pb_source_t **out)
{
    (void)control;
    (void)interrupt;
    (void)address;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_l2cap_open(const char *address, pb_source_t **out)
{
    (void)address;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

#endif
