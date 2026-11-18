#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    char path[64], name[128];
    struct input_id id;
    int i, fd = -1, tries;

    for (tries = 0; tries < 50 && fd < 0; tries++, usleep(100000))
        for (i = 63; i >= 0 && fd < 0; i--) {
            snprintf(path, sizeof(path), "/dev/input/event%d", i);
            fd = open(path, O_RDWR);
            if (fd < 0)
                continue;
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 || ioctl(fd, EVIOCGID, &id) < 0
                || id.vendor != 0x045e || id.product != 0x028e || strstr(name, "X-Box") == NULL
                || id.bustype != BUS_VIRTUAL && id.bustype != BUS_USB) {
                close(fd);
                fd = -1;
            }
        }
    if (fd < 0) {
        fprintf(stderr, "pad not found\n");
        return 1;
    }
    struct ff_effect e;
    memset(&e, 0, sizeof(e));
    e.type = FF_RUMBLE;
    e.id = -1;
    e.u.rumble.strong_magnitude = 0x8000;
    e.u.rumble.weak_magnitude = 0x4000;
    e.replay.length = 300;
    if (ioctl(fd, EVIOCSFF, &e) < 0) {
        perror("EVIOCSFF");
        return 1;
    }
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_FF;
    ev.code = (unsigned short)e.id;
    ev.value = 1;
    if (write(fd, &ev, sizeof(ev)) != sizeof(ev))
        return 1;
    usleep(600000);
    printf("played on %s (%s)\n", path, name);
    return 0;
}
