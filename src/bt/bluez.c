#include "bt_internal.h"

#if defined(PB_HAVE_SDBUS)

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <systemd/sd-bus.h>
#include "../core/out.h"
#include "../core/sys.h"

#define PB_BLUEZ "org.bluez"
#define PB_ADAPTER "org.bluez.Adapter1"
#define PB_DEVICE "org.bluez.Device1"
#define PB_AGENT_PATH "/org/padbridge/agent"
#define PB_HID_UUID "00001124-0000-1000-8000-00805f9b34fb"
#define PB_DISCOVERY_MS 12000
#define PB_PAIR_USEC 30000000ULL
#define PB_TICK_USEC 200000ULL

typedef struct pb_props {
    char address[18];
    char name[128];
    uint32_t device_class;
    int named;
    int paired;
    int connected;
    int powered;
    int hid_uuid;
} pb_props_t;

typedef struct pb_bluez {
    sd_bus *bus;
    sd_bus_slot *agent;
    char adapter[64];
    int powered;
} pb_bluez_t;

typedef int (*pb_object_fn)(void *ctx, const char *path, const char *iface, const pb_props_t *props);

typedef struct pb_pair_wait {
    int done;
    int status;
} pb_pair_wait_t;

static int pb_bus_status(int r, const sd_bus_error *err)
{
    if (err != NULL && sd_bus_error_is_set(err)) {
        pb_log(PB_LOG_INFO, "bluez: %s", err->message ? err->message : err->name);
        if (sd_bus_error_has_names(err, SD_BUS_ERROR_SERVICE_UNKNOWN, SD_BUS_ERROR_NAME_HAS_NO_OWNER,
                                   SD_BUS_ERROR_UNKNOWN_OBJECT, "org.bluez.Error.DoesNotExist"))
            return PB_ERR_NOT_FOUND;
        if (sd_bus_error_has_names(err, SD_BUS_ERROR_ACCESS_DENIED, "org.bluez.Error.AuthenticationRejected",
                                   "org.bluez.Error.AuthenticationFailed"))
            return PB_ERR_ACCESS;
        if (sd_bus_error_has_names(err, SD_BUS_ERROR_NO_REPLY, SD_BUS_ERROR_TIMEOUT,
                                   "org.bluez.Error.AuthenticationTimeout"))
            return PB_ERR_TIMEOUT;
        if (sd_bus_error_has_names(err, "org.bluez.Error.InProgress", "org.bluez.Error.Busy"))
            return PB_ERR_BUSY;
        return PB_ERR_IO;
    }
    if (r == -ENOENT || r == -ECONNREFUSED)
        return PB_ERR_NOT_FOUND;
    if (r == -EACCES || r == -EPERM)
        return PB_ERR_ACCESS;
    if (r == -ETIMEDOUT)
        return PB_ERR_TIMEOUT;
    return r < 0 ? PB_ERR_IO : PB_OK;
}

static int pb_read_text(sd_bus_message *m, char *buf, size_t len)
{
    const char *value;
    int r = sd_bus_message_read(m, "v", "s", &value);

    if (r >= 0)
        snprintf(buf, len, "%s", value);
    return r;
}

static int pb_read_flag(sd_bus_message *m, int *out)
{
    int value;
    int r = sd_bus_message_read(m, "v", "b", &value);

    if (r >= 0)
        *out = value != 0;
    return r;
}

static int pb_read_uuids(sd_bus_message *m, pb_props_t *p)
{
    const char *uuid;
    int r = sd_bus_message_enter_container(m, 'v', "as");

    if (r >= 0)
        r = sd_bus_message_enter_container(m, 'a', "s");
    while (r >= 0 && (r = sd_bus_message_read(m, "s", &uuid)) > 0)
        if (strcasecmp(uuid, PB_HID_UUID) == 0)
            p->hid_uuid = 1;
    if (r >= 0)
        r = sd_bus_message_exit_container(m);
    return r >= 0 ? sd_bus_message_exit_container(m) : r;
}

static int pb_read_prop(sd_bus_message *m, const char *key, pb_props_t *p)
{
    if (strcmp(key, "Address") == 0)
        return pb_read_text(m, p->address, sizeof(p->address));
    if (strcmp(key, "Name") == 0) {
        p->named = 1;
        return pb_read_text(m, p->name, sizeof(p->name));
    }
    if (strcmp(key, "Alias") == 0 && !p->named)
        return pb_read_text(m, p->name, sizeof(p->name));
    if (strcmp(key, "Class") == 0)
        return sd_bus_message_read(m, "v", "u", &p->device_class);
    if (strcmp(key, "Paired") == 0)
        return pb_read_flag(m, &p->paired);
    if (strcmp(key, "Connected") == 0)
        return pb_read_flag(m, &p->connected);
    if (strcmp(key, "Powered") == 0)
        return pb_read_flag(m, &p->powered);
    if (strcmp(key, "UUIDs") == 0)
        return pb_read_uuids(m, p);
    return sd_bus_message_skip(m, "v");
}

static int pb_read_props(sd_bus_message *m, pb_props_t *p)
{
    const char *key;
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");

    while (r >= 0 && (r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        r = sd_bus_message_read(m, "s", &key);
        if (r >= 0)
            r = pb_read_prop(m, key, p);
        if (r >= 0)
            r = sd_bus_message_exit_container(m);
    }
    return r >= 0 ? sd_bus_message_exit_container(m) : r;
}

static int pb_read_object(sd_bus_message *m, pb_object_fn fn, void *ctx)
{
    const char *path;
    const char *iface;
    pb_props_t props;
    int r = sd_bus_message_read(m, "o", &path);

    if (r >= 0)
        r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
    while (r >= 0 && (r = sd_bus_message_enter_container(m, 'e', "sa{sv}")) > 0) {
        r = sd_bus_message_read(m, "s", &iface);
        if (r >= 0 && (strcmp(iface, PB_ADAPTER) == 0 || strcmp(iface, PB_DEVICE) == 0)) {
            memset(&props, 0, sizeof(props));
            r = pb_read_props(m, &props);
            if (r >= 0)
                r = fn(ctx, path, iface, &props);
        } else if (r >= 0) {
            r = sd_bus_message_skip(m, "a{sv}");
        }
        if (r >= 0)
            r = sd_bus_message_exit_container(m);
    }
    return r >= 0 ? sd_bus_message_exit_container(m) : r;
}

static int pb_objects(sd_bus *bus, pb_object_fn fn, void *ctx)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(bus, PB_BLUEZ, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                               &err, &reply, "");

    if (r >= 0)
        r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0) {
        r = pb_read_object(reply, fn, ctx);
        if (r >= 0)
            r = sd_bus_message_exit_container(reply);
    }
    r = pb_bus_status(r, &err);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r;
}

static int pb_find_adapter(void *ctx, const char *path, const char *iface, const pb_props_t *props)
{
    pb_bluez_t *bz = ctx;

    if (bz->adapter[0] == '\0' && strcmp(iface, PB_ADAPTER) == 0) {
        snprintf(bz->adapter, sizeof(bz->adapter), "%s", path);
        bz->powered = props->powered;
    }
    return 0;
}

static int pb_set_flag(pb_bluez_t *bz, const char *path, const char *iface, const char *name, int value)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_set_property(bz->bus, PB_BLUEZ, path, iface, name, &err, "b", value);

    r = pb_bus_status(r, &err);
    sd_bus_error_free(&err);
    return r;
}

static void pb_bluez_close(pb_bluez_t *bz)
{
    sd_bus_slot_unref(bz->agent);
    sd_bus_flush_close_unref(bz->bus);
}

static int pb_bluez_open(pb_bluez_t *bz, int power)
{
    int status;

    memset(bz, 0, sizeof(*bz));
    status = pb_bus_status(sd_bus_open_system(&bz->bus), NULL);
    if (status == PB_OK)
        status = pb_objects(bz->bus, pb_find_adapter, bz);
    if (status == PB_OK && bz->adapter[0] == '\0') {
        pb_log(PB_LOG_INFO, "bluez: no Bluetooth adapter");
        status = PB_ERR_NOT_FOUND;
    }
    if (status == PB_OK && power && !bz->powered)
        status = pb_set_flag(bz, bz->adapter, PB_ADAPTER, "Powered", 1);
    if (status != PB_OK)
        pb_bluez_close(bz);
    return status;
}

static void pb_device_path(const pb_bluez_t *bz, const char *address, char *buf, size_t len)
{
    char *at;

    snprintf(buf, len, "%s/dev_%s", bz->adapter, address);
    for (at = strstr(buf, "/dev_"); *at != '\0'; at++)
        if (*at == ':')
            *at = '_';
}

static int pb_device_exists(pb_bluez_t *bz, const char *path)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int value;
    int r = sd_bus_get_property_trivial(bz->bus, PB_BLUEZ, path, PB_DEVICE, "Paired", &err, 'b', &value);

    sd_bus_error_free(&err);
    return r >= 0;
}

static int pb_call(pb_bluez_t *bz, const char *path, const char *iface, const char *method, const char *types,
                   const char *arg)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = types[0] == '\0' ? sd_bus_call_method(bz->bus, PB_BLUEZ, path, iface, method, &err, NULL, "")
                             : sd_bus_call_method(bz->bus, PB_BLUEZ, path, iface, method, &err, NULL, types, arg);

    r = pb_bus_status(r, &err);
    sd_bus_error_free(&err);
    return r;
}

static int pb_pump(pb_bluez_t *bz)
{
    int r;

    while ((r = sd_bus_process(bz->bus, NULL)) > 0)
        ;
    if (r >= 0)
        r = sd_bus_wait(bz->bus, PB_TICK_USEC);
    return r == -EINTR ? 0 : r;
}

static int pb_discovery_start(pb_bluez_t *bz)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(bz->bus, PB_BLUEZ, bz->adapter, PB_ADAPTER, "SetDiscoveryFilter", &err, NULL,
                               "a{sv}", 1, "Transport", "s", "bredr");
    int status = pb_bus_status(r, &err);

    sd_bus_error_free(&err);
    if (status != PB_OK)
        return status;
    status = pb_call(bz, bz->adapter, PB_ADAPTER, "StartDiscovery", "", NULL);
    return status == PB_ERR_BUSY ? PB_OK : status;
}

static int pb_discover(pb_bluez_t *bz, int ms, const char *wanted)
{
    uint32_t start = pb_now_ms();
    int status = pb_discovery_start(bz);

    while (status == PB_OK && pb_now_ms() - start < (uint32_t)ms && !pb_interrupted()) {
        if (wanted != NULL && pb_device_exists(bz, wanted))
            break;
        if (pb_pump(bz) < 0)
            status = PB_ERR_IO;
    }
    pb_call(bz, bz->adapter, PB_ADAPTER, "StopDiscovery", "", NULL);
    if (status == PB_OK && pb_interrupted())
        status = PB_ERR_CLOSED;
    return status;
}

static void pb_device_from(const pb_props_t *p, pb_bt_device_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->address, sizeof(out->address), "%s", p->address);
    snprintf(out->name, sizeof(out->name), "%s", p->name);
    out->device_class = p->device_class;
    out->paired = p->paired;
    out->connected = p->connected;
    out->raw_hid = ((p->device_class >> 8) & 0x1f) == 5 && !p->hid_uuid;
}

typedef struct pb_scan_ctx {
    const pb_bluez_t *bz;
    pb_bt_device_fn fn;
    void *ctx;
    int stop;
} pb_scan_ctx_t;

static int pb_scan_object(void *ctx, const char *path, const char *iface, const pb_props_t *props)
{
    pb_scan_ctx_t *scan = ctx;
    size_t n = strlen(scan->bz->adapter);
    pb_bt_device_t device;

    if (scan->stop || strcmp(iface, PB_DEVICE) != 0 || strncmp(path, scan->bz->adapter, n) != 0)
        return 0;
    if (((props->device_class >> 8) & 0x1f) != 5 && !props->hid_uuid)
        return 0;
    pb_device_from(props, &device);
    scan->stop = scan->fn(scan->ctx, &device) != 0;
    return 0;
}

int pb_bluez_scan(int seconds, pb_bt_device_fn fn, void *ctx)
{
    pb_bluez_t bz;
    pb_scan_ctx_t scan = {&bz, fn, ctx, 0};
    int status = pb_bluez_open(&bz, 1);

    if (status != PB_OK)
        return status;
    status = pb_discover(&bz, seconds * 1000, NULL);
    if (status == PB_OK)
        status = pb_objects(bz.bus, pb_scan_object, &scan);
    pb_bluez_close(&bz);
    return status;
}

typedef struct pb_info_ctx {
    const char *address;
    pb_bt_device_t *out;
    int found;
} pb_info_ctx_t;

static int pb_info_object(void *ctx, const char *path, const char *iface, const pb_props_t *props)
{
    pb_info_ctx_t *info = ctx;

    (void)path;
    if (info->found || strcmp(iface, PB_DEVICE) != 0 || strcasecmp(props->address, info->address) != 0)
        return 0;
    pb_device_from(props, info->out);
    info->found = 1;
    return 0;
}

int pb_bluez_info(const char *address, pb_bt_device_t *out)
{
    pb_bluez_t bz;
    pb_info_ctx_t info = {address, out, 0};
    int status = pb_bluez_open(&bz, 0);

    if (status != PB_OK)
        return status;
    status = pb_objects(bz.bus, pb_info_object, &info);
    pb_bluez_close(&bz);
    if (status == PB_OK && !info.found)
        status = PB_ERR_NOT_FOUND;
    return status;
}

static int pb_agent_method(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
    const char *member = sd_bus_message_get_member(m);

    (void)userdata;
    (void)err;
    if (strcmp(member, "RequestPinCode") == 0)
        return sd_bus_reply_method_return(m, "s", "0000");
    if (strcmp(member, "RequestPasskey") == 0)
        return sd_bus_reply_method_return(m, "u", (uint32_t)0);
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable pb_agent_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", pb_agent_method, 0),
    SD_BUS_METHOD("RequestPinCode", "o", "s", pb_agent_method, 0),
    SD_BUS_METHOD("DisplayPinCode", "os", "", pb_agent_method, 0),
    SD_BUS_METHOD("RequestPasskey", "o", "u", pb_agent_method, 0),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", pb_agent_method, 0),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", pb_agent_method, 0),
    SD_BUS_METHOD("RequestAuthorization", "o", "", pb_agent_method, 0),
    SD_BUS_METHOD("AuthorizeService", "os", "", pb_agent_method, 0),
    SD_BUS_METHOD("Cancel", "", "", pb_agent_method, 0),
    SD_BUS_VTABLE_END,
};

static int pb_agent_register(pb_bluez_t *bz)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_add_object_vtable(bz->bus, &bz->agent, PB_AGENT_PATH, "org.bluez.Agent1", pb_agent_vtable, NULL);
    int status;

    if (r >= 0)
        r = sd_bus_call_method(bz->bus, PB_BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", &err,
                               NULL, "os", PB_AGENT_PATH, "NoInputNoOutput");
    status = pb_bus_status(r, &err);
    sd_bus_error_free(&err);
    if (status == PB_OK)
        pb_call(bz, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", "o", PB_AGENT_PATH);
    return status;
}

static void pb_agent_unregister(pb_bluez_t *bz)
{
    pb_call(bz, "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent", "o", PB_AGENT_PATH);
}

static int pb_pair_done(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    pb_pair_wait_t *wait = userdata;
    const sd_bus_error *err = sd_bus_message_get_error(m);

    (void)ret_error;
    wait->done = 1;
    wait->status = PB_OK;
    if (err != NULL && !sd_bus_error_has_name(err, "org.bluez.Error.AlreadyExists"))
        wait->status = pb_bus_status(-EIO, err);
    return 1;
}

static int pb_pair_call(pb_bluez_t *bz, const char *path)
{
    pb_pair_wait_t wait = {0, PB_ERR_TIMEOUT};
    sd_bus_message *m = NULL;
    sd_bus_slot *slot = NULL;
    int r = sd_bus_message_new_method_call(bz->bus, &m, PB_BLUEZ, path, PB_DEVICE, "Pair");

    if (r >= 0)
        r = sd_bus_call_async(bz->bus, &slot, m, pb_pair_done, &wait, PB_PAIR_USEC);
    sd_bus_message_unref(m);
    while (r >= 0 && !wait.done && !pb_interrupted())
        r = pb_pump(bz);
    sd_bus_slot_unref(slot);
    if (r < 0)
        return pb_bus_status(r, NULL);
    return wait.done ? wait.status : PB_ERR_CLOSED;
}

static int pb_pair_steps(pb_bluez_t *bz, const char *path)
{
    int status = pb_set_flag(bz, bz->adapter, PB_ADAPTER, "Pairable", 1);

    if (status == PB_OK)
        status = pb_agent_register(bz);
    if (status != PB_OK)
        return status;
    if (pb_device_exists(bz, path))
        pb_call(bz, bz->adapter, PB_ADAPTER, "RemoveDevice", "o", path);
    status = pb_discover(bz, PB_DISCOVERY_MS, path);
    if (status == PB_OK && !pb_device_exists(bz, path))
        status = PB_ERR_NOT_FOUND;
    if (status == PB_OK)
        pb_set_flag(bz, path, PB_DEVICE, "Trusted", 1);
    if (status == PB_OK)
        status = pb_pair_call(bz, path);
    pb_agent_unregister(bz);
    return status;
}

int pb_bluez_pair(const char *address)
{
    pb_bluez_t bz;
    char path[96];
    int status = pb_bluez_open(&bz, 1);

    if (status != PB_OK)
        return status;
    pb_device_path(&bz, address, path, sizeof(path));
    status = pb_pair_steps(&bz, path);
    pb_bluez_close(&bz);
    return status;
}

int pb_bluez_disconnect(const char *address)
{
    pb_bluez_t bz;
    char path[96];
    int status = pb_bluez_open(&bz, 0);

    if (status != PB_OK)
        return status;
    pb_device_path(&bz, address, path, sizeof(path));
    status = pb_device_exists(&bz, path) ? pb_call(&bz, path, PB_DEVICE, "Disconnect", "", NULL) : PB_ERR_NOT_FOUND;
    pb_bluez_close(&bz);
    return status;
}

#else

int pb_bluez_scan(int seconds, pb_bt_device_fn fn, void *ctx)
{
    (void)seconds;
    (void)fn;
    (void)ctx;
    return PB_ERR_UNSUPPORTED;
}

int pb_bluez_info(const char *address, pb_bt_device_t *out)
{
    (void)address;
    (void)out;
    return PB_ERR_UNSUPPORTED;
}

int pb_bluez_pair(const char *address)
{
    (void)address;
    return PB_ERR_UNSUPPORTED;
}

int pb_bluez_disconnect(const char *address)
{
    (void)address;
    return PB_ERR_UNSUPPORTED;
}

#endif
