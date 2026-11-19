#ifdef __APPLE__

#define _DARWIN_C_SOURCE 1

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include "../core/out.h"
#include "hid_internal.h"

#define PB_MAC_QUEUE 64
#define PB_MAC_PREFIX "DevSrvsID:"

typedef struct pb_mac_report {
    size_t len;
    unsigned char data[PB_REPORT_MAX];
} pb_mac_report_t;

typedef struct pb_mac_hid {
    pb_source_t base;
    IOHIDDeviceRef device;
    CFRunLoopRef loop;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    int started;
    int removed;
    int numbered;
    size_t head;
    size_t count;
    pb_mac_report_t queue[PB_MAC_QUEUE];
    unsigned char input[PB_REPORT_MAX];
    size_t desc_len;
    unsigned char desc[PB_HID_DESC_MAX];
} pb_mac_hid_t;

static void pb_mac_on_report(void *ctx, IOReturn result, void *sender, IOHIDReportType type, uint32_t id, uint8_t *report, CFIndex len)
{
    pb_mac_hid_t *dev = ctx;
    pb_mac_report_t *slot;

    (void)result;
    (void)sender;
    (void)type;
    (void)id;
    pthread_mutex_lock(&dev->lock);
    if (dev->count == PB_MAC_QUEUE) {
        dev->head = (dev->head + 1) % PB_MAC_QUEUE;
        dev->count--;
    }
    slot = &dev->queue[(dev->head + dev->count) % PB_MAC_QUEUE];
    slot->len = (size_t)len < sizeof(slot->data) ? (size_t)len : sizeof(slot->data);
    memcpy(slot->data, report, slot->len);
    dev->count++;
    pthread_cond_signal(&dev->ready);
    pthread_mutex_unlock(&dev->lock);
}

static void pb_mac_on_removal(void *ctx, IOReturn result, void *sender)
{
    pb_mac_hid_t *dev = ctx;

    (void)result;
    (void)sender;
    pthread_mutex_lock(&dev->lock);
    dev->removed = 1;
    pthread_cond_broadcast(&dev->ready);
    pthread_mutex_unlock(&dev->lock);
}

static void *pb_mac_loop(void *arg)
{
    pb_mac_hid_t *dev = arg;

    pthread_mutex_lock(&dev->lock);
    dev->loop = CFRunLoopGetCurrent();
    IOHIDDeviceScheduleWithRunLoop(dev->device, dev->loop, kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(dev->device, dev->input, sizeof(dev->input), pb_mac_on_report, dev);
    IOHIDDeviceRegisterRemovalCallback(dev->device, pb_mac_on_removal, dev);
    dev->started = 1;
    pthread_cond_broadcast(&dev->ready);
    pthread_mutex_unlock(&dev->lock);
    CFRunLoopRun();
    IOHIDDeviceUnscheduleFromRunLoop(dev->device, dev->loop, kCFRunLoopDefaultMode);
    return NULL;
}

static void pb_mac_deadline(struct timespec *at, int timeout_ms)
{
    struct timeval now;

    gettimeofday(&now, NULL);
    at->tv_sec = now.tv_sec + timeout_ms / 1000;
    at->tv_nsec = (long)now.tv_usec * 1000 + (long)(timeout_ms % 1000) * 1000000;
    if (at->tv_nsec >= 1000000000) {
        at->tv_sec++;
        at->tv_nsec -= 1000000000;
    }
}

static int pb_mac_wait(pb_mac_hid_t *dev, int timeout_ms)
{
    struct timespec at;

    pb_mac_deadline(&at, timeout_ms < 0 ? 0 : timeout_ms);
    while (dev->count == 0 && !dev->removed) {
        if (timeout_ms < 0)
            pthread_cond_wait(&dev->ready, &dev->lock);
        else if (pthread_cond_timedwait(&dev->ready, &dev->lock, &at) == ETIMEDOUT)
            return PB_ERR_TIMEOUT;
    }
    return dev->count > 0 ? PB_OK : PB_ERR_CLOSED;
}

static int pb_mac_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms)
{
    pb_mac_hid_t *dev = (pb_mac_hid_t *)src;
    pb_mac_report_t *slot;
    int status;

    *out_len = 0;
    pthread_mutex_lock(&dev->lock);
    status = pb_mac_wait(dev, timeout_ms);
    if (status == PB_OK) {
        slot = &dev->queue[dev->head];
        *out_len = slot->len < len ? slot->len : len;
        memcpy(buf, slot->data, *out_len);
        dev->head = (dev->head + 1) % PB_MAC_QUEUE;
        dev->count--;
    }
    pthread_mutex_unlock(&dev->lock);
    return status;
}

static int pb_mac_write(pb_source_t *src, const unsigned char *buf, size_t len)
{
    pb_mac_hid_t *dev = (pb_mac_hid_t *)src;
    CFIndex report_id = dev->numbered && len > 0 ? buf[0] : 0;
    IOReturn result;

    if (len == 0)
        return PB_ERR_ARG;
    result = IOHIDDeviceSetReport(dev->device, kIOHIDReportTypeOutput, report_id, buf, (CFIndex)len);
    if (result == kIOReturnSuccess)
        return PB_OK;
    if (result == kIOReturnNotPrivileged || result == kIOReturnNotPermitted)
        return PB_ERR_ACCESS;
    return dev->removed ? PB_ERR_CLOSED : PB_ERR_IO;
}

static int pb_mac_descriptor(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len)
{
    pb_mac_hid_t *dev = (pb_mac_hid_t *)src;

    *out_len = dev->desc_len;
    if (dev->desc_len == 0)
        return PB_ERR_NOT_FOUND;
    if (len < dev->desc_len)
        return PB_ERR_ARG;
    memcpy(buf, dev->desc, dev->desc_len);
    return PB_OK;
}

static void pb_mac_close(pb_source_t *src)
{
    pb_mac_hid_t *dev = (pb_mac_hid_t *)src;

    if (dev->started) {
        CFRunLoopStop(dev->loop);
        CFRunLoopWakeUp(dev->loop);
        pthread_join(dev->thread, NULL);
    }
    if (dev->device != NULL) {
        IOHIDDeviceClose(dev->device, kIOHIDOptionsTypeNone);
        CFRelease(dev->device);
    }
    pthread_cond_destroy(&dev->ready);
    pthread_mutex_destroy(&dev->lock);
    free(dev);
}

static const pb_source_ops_t pb_mac_ops = {pb_mac_read, pb_mac_write, pb_mac_descriptor, pb_mac_close};

static long pb_mac_number(IOHIDDeviceRef device, CFStringRef key)
{
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    long number = 0;

    if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)value, kCFNumberLongType, &number);
    return number;
}

static int pb_mac_string(IOHIDDeviceRef device, CFStringRef key, char *buf, size_t len)
{
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);

    buf[0] = '\0';
    return value != NULL && CFGetTypeID(value) == CFStringGetTypeID()
           && CFStringGetCString((CFStringRef)value, buf, (CFIndex)len, kCFStringEncodingUTF8);
}

static uint64_t pb_mac_entry_id(IOHIDDeviceRef device)
{
    uint64_t id = 0;

    IORegistryEntryGetRegistryEntryID(IOHIDDeviceGetService(device), &id);
    return id;
}

static void pb_mac_info(IOHIDDeviceRef device, pb_source_info_t *info)
{
    char transport[32];
    char path[64];

    info->vendor = (uint16_t)pb_mac_number(device, CFSTR(kIOHIDVendorIDKey));
    info->product = (uint16_t)pb_mac_number(device, CFSTR(kIOHIDProductIDKey));
    pb_mac_string(device, CFSTR(kIOHIDProductKey), info->name, sizeof(info->name));
    pb_mac_string(device, CFSTR(kIOHIDTransportKey), transport, sizeof(transport));
    info->bus = PB_BUS_UNKNOWN;
    if (strcmp(transport, "USB") == 0)
        info->bus = PB_BUS_USB;
    else if (strncmp(transport, "Bluetooth", 9) == 0)
        info->bus = PB_BUS_BLUETOOTH;
    snprintf(path, sizeof(path), PB_MAC_PREFIX "%" PRIu64, pb_mac_entry_id(device));
    pb_hid_set_uri(info, "hid", path);
}

static void pb_mac_load_descriptor(pb_mac_hid_t *dev)
{
    CFTypeRef value = IOHIDDeviceGetProperty(dev->device, CFSTR(kIOHIDReportDescriptorKey));
    CFIndex len;

    if (value == NULL || CFGetTypeID(value) != CFDataGetTypeID())
        return;
    len = CFDataGetLength((CFDataRef)value);
    if (len <= 0 || (size_t)len > sizeof(dev->desc))
        return;
    CFDataGetBytes((CFDataRef)value, CFRangeMake(0, len), dev->desc);
    dev->desc_len = (size_t)len;
    dev->numbered = pb_hid_numbered(dev->desc, dev->desc_len);
}

static IOHIDDeviceRef pb_mac_find(const char *path)
{
    io_service_t service;
    IOHIDDeviceRef device;
    char *end;
    uint64_t id;

    if (strncmp(path, PB_MAC_PREFIX, strlen(PB_MAC_PREFIX)) != 0)
        return NULL;
    id = strtoull(path + strlen(PB_MAC_PREFIX), &end, 10);
    if (*end != '\0')
        return NULL;
    service = IOServiceGetMatchingService(MACH_PORT_NULL, IORegistryEntryIDMatching(id));
    if (service == MACH_PORT_NULL)
        return NULL;
    device = IOHIDDeviceCreate(kCFAllocatorDefault, service);
    IOObjectRelease(service);
    return device;
}

static int pb_mac_open_device(pb_mac_hid_t *dev, const char *path)
{
    IOReturn result;

    dev->device = pb_mac_find(path);
    if (dev->device == NULL)
        return PB_ERR_NOT_FOUND;
    result = IOHIDDeviceOpen(dev->device, kIOHIDOptionsTypeNone);
    if (result == kIOReturnNotPrivileged || result == kIOReturnNotPermitted)
        return PB_ERR_ACCESS;
    if (result == kIOReturnExclusiveAccess)
        return PB_ERR_BUSY;
    if (result != kIOReturnSuccess)
        return PB_ERR_IO;
    pb_mac_load_descriptor(dev);
    pb_mac_info(dev->device, &dev->base.info);
    if (pthread_create(&dev->thread, NULL, pb_mac_loop, dev) != 0)
        return PB_ERR_NOMEM;
    pthread_mutex_lock(&dev->lock);
    while (!dev->started)
        pthread_cond_wait(&dev->ready, &dev->lock);
    pthread_mutex_unlock(&dev->lock);
    return PB_OK;
}

int pb_hid_open(const char *path, pb_source_t **out)
{
    pb_mac_hid_t *dev;
    int status;

    if (path == NULL || out == NULL || path[0] == '\0')
        return PB_ERR_ARG;
    *out = NULL;
    dev = calloc(1, sizeof(*dev));
    if (dev == NULL)
        return PB_ERR_NOMEM;
    pthread_mutex_init(&dev->lock, NULL);
    pthread_cond_init(&dev->ready, NULL);
    status = pb_mac_open_device(dev, path);
    if (status != PB_OK) {
        pb_log(PB_LOG_ERROR, "cannot open %s: %s", path, pb_strerror(status));
        pb_mac_close(&dev->base);
        return status;
    }
    dev->base.ops = &pb_mac_ops;
    *out = &dev->base;
    return PB_OK;
}

static int pb_mac_list_set(CFSetRef set, pb_source_list_fn fn, void *ctx)
{
    CFIndex count = CFSetGetCount(set);
    const void **devices = calloc(count > 0 ? (size_t)count : 1, sizeof(*devices));
    pb_source_info_t info;
    CFIndex i;
    int status = PB_OK;

    if (devices == NULL)
        return PB_ERR_NOMEM;
    CFSetGetValues(set, devices);
    for (i = 0; i < count && status == PB_OK; i++) {
        memset(&info, 0, sizeof(info));
        pb_mac_info((IOHIDDeviceRef)devices[i], &info);
        status = fn(ctx, &info);
    }
    free(devices);
    return status;
}

int pb_hid_list(pb_source_list_fn fn, void *ctx)
{
    IOHIDManagerRef manager;
    CFSetRef set;
    int status = PB_OK;

    if (fn == NULL)
        return PB_ERR_ARG;
    manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (manager == NULL)
        return PB_ERR_NOMEM;
    IOHIDManagerSetDeviceMatching(manager, NULL);
    set = IOHIDManagerCopyDevices(manager);
    if (set != NULL) {
        status = pb_mac_list_set(set, fn, ctx);
        CFRelease(set);
    }
    CFRelease(manager);
    return status;
}

const char *pb_hid_backend(void)
{
    return "IOHIDManager";
}

#else

typedef int pb_hid_mac_unused;

#endif
