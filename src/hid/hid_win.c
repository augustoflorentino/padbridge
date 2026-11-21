#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include "../core/out.h"
#include "hid_internal.h"
#include "hid_rebuild.h"

#define PB_WIN_WRITE_MS 1000
#define PB_WIN_BT_GUID "{00001124-0000-1000-8000-00805f9b34fb}"

typedef struct pb_win_hid {
    pb_source_t base;
    HANDLE handle;
    OVERLAPPED read_ov;
    OVERLAPPED write_ov;
    int reading;
    int numbered;
    int read_only;
    size_t input_len;
    size_t output_len;
    unsigned char input[PB_REPORT_MAX + 1];
    size_t desc_len;
    unsigned char desc[PB_HID_DESC_MAX];
} pb_win_hid_t;

static int pb_win_error(DWORD err)
{
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return PB_ERR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
        return PB_ERR_ACCESS;
    case ERROR_SHARING_VIOLATION:
        return PB_ERR_BUSY;
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_OPERATION_ABORTED:
    case ERROR_BAD_COMMAND:
        return PB_ERR_CLOSED;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return PB_ERR_NOMEM;
    default:
        return PB_ERR_IO;
    }
}

static int pb_win_wait(pb_win_hid_t *dev, OVERLAPPED *ov, int timeout_ms, DWORD *out_count)
{
    DWORD waited = WaitForSingleObject(ov->hEvent, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);

    if (waited == WAIT_TIMEOUT)
        return PB_ERR_TIMEOUT;
    if (waited != WAIT_OBJECT_0)
        return PB_ERR_IO;
    if (!GetOverlappedResult(dev->handle, ov, out_count, FALSE))
        return pb_win_error(GetLastError());
    return PB_OK;
}

static int pb_win_start_read(pb_win_hid_t *dev)
{
    ResetEvent(dev->read_ov.hEvent);
    if (!ReadFile(dev->handle, dev->input, (DWORD)dev->input_len, NULL, &dev->read_ov) && GetLastError() != ERROR_IO_PENDING)
        return pb_win_error(GetLastError());
    dev->reading = 1;
    return PB_OK;
}

static int pb_win_read(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len, int timeout_ms)
{
    pb_win_hid_t *dev = (pb_win_hid_t *)src;
    const unsigned char *report = dev->input;
    DWORD got = 0;
    size_t size;
    int status = dev->reading ? PB_OK : pb_win_start_read(dev);

    *out_len = 0;
    if (status == PB_OK)
        status = pb_win_wait(dev, &dev->read_ov, timeout_ms, &got);
    if (status == PB_ERR_TIMEOUT)
        return status;
    dev->reading = 0;
    if (status != PB_OK)
        return status;
    size = got;
    if (!dev->numbered && size > 0) {
        report++;
        size--;
    }
    *out_len = size < len ? size : len;
    memcpy(buf, report, *out_len);
    return PB_OK;
}

static int pb_win_write(pb_source_t *src, const unsigned char *buf, size_t len)
{
    pb_win_hid_t *dev = (pb_win_hid_t *)src;
    unsigned char framed[PB_REPORT_MAX + 1] = {0};
    size_t offset = dev->numbered ? 0 : 1;
    DWORD put = 0;
    int status;

    if (dev->read_only)
        return PB_ERR_ACCESS;
    if (dev->output_len == 0)
        return PB_ERR_UNSUPPORTED;
    if (len + offset > dev->output_len)
        return PB_ERR_ARG;
    memcpy(framed + offset, buf, len);
    ResetEvent(dev->write_ov.hEvent);
    if (!WriteFile(dev->handle, framed, (DWORD)dev->output_len, NULL, &dev->write_ov) && GetLastError() != ERROR_IO_PENDING)
        return pb_win_error(GetLastError());
    status = pb_win_wait(dev, &dev->write_ov, PB_WIN_WRITE_MS, &put);
    if (status == PB_ERR_TIMEOUT)
        CancelIo(dev->handle);
    return status;
}

static int pb_win_descriptor(pb_source_t *src, unsigned char *buf, size_t len, size_t *out_len)
{
    pb_win_hid_t *dev = (pb_win_hid_t *)src;

    *out_len = dev->desc_len;
    if (dev->desc_len == 0)
        return PB_ERR_NOT_FOUND;
    if (len < dev->desc_len)
        return PB_ERR_ARG;
    memcpy(buf, dev->desc, dev->desc_len);
    return PB_OK;
}

static void pb_win_close(pb_source_t *src)
{
    pb_win_hid_t *dev = (pb_win_hid_t *)src;

    if (dev->handle != INVALID_HANDLE_VALUE) {
        CancelIo(dev->handle);
        CloseHandle(dev->handle);
    }
    if (dev->read_ov.hEvent != NULL)
        CloseHandle(dev->read_ov.hEvent);
    if (dev->write_ov.hEvent != NULL)
        CloseHandle(dev->write_ov.hEvent);
    free(dev);
}

static const pb_source_ops_t pb_win_ops = {pb_win_read, pb_win_write, pb_win_descriptor, pb_win_close};

static int pb_win_caps_numbered(PHIDP_PREPARSED_DATA pp, HIDP_REPORT_TYPE type, USHORT buttons, USHORT values)
{
    HIDP_BUTTON_CAPS button_caps[64];
    HIDP_VALUE_CAPS value_caps[64];
    USHORT count;
    USHORT i;

    count = buttons < 64 ? buttons : 64;
    if (count > 0 && HidP_GetButtonCaps(type, button_caps, &count, pp) == HIDP_STATUS_SUCCESS) {
        for (i = 0; i < count; i++)
            if (button_caps[i].ReportID != 0)
                return 1;
    }
    count = values < 64 ? values : 64;
    if (count > 0 && HidP_GetValueCaps(type, value_caps, &count, pp) == HIDP_STATUS_SUCCESS) {
        for (i = 0; i < count; i++)
            if (value_caps[i].ReportID != 0)
                return 1;
    }
    return 0;
}

static int pb_win_caps(pb_win_hid_t *dev)
{
    PHIDP_PREPARSED_DATA pp;
    HIDP_CAPS caps;

    if (!HidD_GetPreparsedData(dev->handle, &pp))
        return PB_ERR_IO;
    if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) {
        HidD_FreePreparsedData(pp);
        return PB_ERR_IO;
    }
    dev->input_len = caps.InputReportByteLength;
    dev->output_len = caps.OutputReportByteLength;
    dev->numbered = pb_win_caps_numbered(pp, HidP_Input, caps.NumberInputButtonCaps, caps.NumberInputValueCaps)
                    || pb_win_caps_numbered(pp, HidP_Output, caps.NumberOutputButtonCaps, caps.NumberOutputValueCaps);
    if (pb_win_rebuild(pp, dev->desc, sizeof(dev->desc), &dev->desc_len) != PB_OK)
        pb_log(PB_LOG_INFO, "report descriptor could not be rebuilt from the preparsed data");
    HidD_FreePreparsedData(pp);
    if (dev->input_len > sizeof(dev->input) || dev->output_len > PB_REPORT_MAX + 1)
        return PB_ERR_FORMAT;
    return PB_OK;
}

static void pb_win_info(HANDLE handle, const char *path, pb_source_info_t *info)
{
    HIDD_ATTRIBUTES attrs;
    WCHAR wide[sizeof(info->name)];

    attrs.Size = sizeof(attrs);
    if (HidD_GetAttributes(handle, &attrs)) {
        info->vendor = attrs.VendorID;
        info->product = attrs.ProductID;
    }
    if (HidD_GetProductString(handle, wide, sizeof(wide)))
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, info->name, (int)sizeof(info->name), NULL, NULL);
    info->bus = _strnicmp(path, "\\\\?\\hid#", 8) == 0 ? PB_BUS_USB : PB_BUS_UNKNOWN;
    if (strstr(path, PB_WIN_BT_GUID) != NULL || strstr(path, "{00001124-0000-1000-8000-00805F9B34FB}") != NULL)
        info->bus = PB_BUS_BLUETOOTH;
    pb_hid_set_uri(info, "hid", path);
}

static HANDLE pb_win_open_handle(const char *path, DWORD access)
{
    return CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
}

static int pb_win_open_device(pb_win_hid_t *dev, const char *path)
{
    dev->handle = pb_win_open_handle(path, GENERIC_READ | GENERIC_WRITE);
    if (dev->handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
        dev->handle = pb_win_open_handle(path, GENERIC_READ);
        dev->read_only = dev->handle != INVALID_HANDLE_VALUE;
        if (dev->read_only)
            pb_log(PB_LOG_WARN, "%s opened read only: output reports will fail", path);
    }
    if (dev->handle == INVALID_HANDLE_VALUE)
        return pb_win_error(GetLastError());
    dev->read_ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    dev->write_ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (dev->read_ov.hEvent == NULL || dev->write_ov.hEvent == NULL)
        return PB_ERR_NOMEM;
    return pb_win_caps(dev);
}

int pb_hid_open(const char *path, pb_source_t **out)
{
    pb_win_hid_t *dev;
    int status;

    if (path == NULL || out == NULL || path[0] == '\0')
        return PB_ERR_ARG;
    *out = NULL;
    dev = calloc(1, sizeof(*dev));
    if (dev == NULL)
        return PB_ERR_NOMEM;
    dev->handle = INVALID_HANDLE_VALUE;
    status = pb_win_open_device(dev, path);
    if (status != PB_OK) {
        pb_log(PB_LOG_ERROR, "cannot open %s: %s", path, pb_strerror(status));
        pb_win_close(&dev->base);
        return status;
    }
    dev->base.ops = &pb_win_ops;
    pb_win_info(dev->handle, path, &dev->base.info);
    *out = &dev->base;
    return PB_OK;
}

static int pb_win_list_one(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *iface, pb_source_list_fn fn, void *ctx)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail;
    pb_source_info_t info;
    DWORD size = 0;
    HANDLE handle;
    int status = PB_OK;

    SetupDiGetDeviceInterfaceDetailA(set, iface, NULL, 0, &size, NULL);
    detail = size > 0 ? malloc(size) : NULL;
    if (detail == NULL)
        return PB_ERR_NOMEM;
    detail->cbSize = sizeof(*detail);
    memset(&info, 0, sizeof(info));
    if (SetupDiGetDeviceInterfaceDetailA(set, iface, detail, size, NULL, NULL)
        && strlen(detail->DevicePath) + 4 < sizeof(info.uri)
        && (handle = pb_win_open_handle(detail->DevicePath, 0)) != INVALID_HANDLE_VALUE) {
        pb_win_info(handle, detail->DevicePath, &info);
        CloseHandle(handle);
        status = fn(ctx, &info);
    }
    free(detail);
    return status;
}

int pb_hid_list(pb_source_list_fn fn, void *ctx)
{
    SP_DEVICE_INTERFACE_DATA iface;
    HDEVINFO set;
    GUID guid;
    DWORD index;
    int status = PB_OK;

    if (fn == NULL)
        return PB_ERR_ARG;
    HidD_GetHidGuid(&guid);
    set = SetupDiGetClassDevsA(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return pb_win_error(GetLastError());
    iface.cbSize = sizeof(iface);
    for (index = 0; status == PB_OK && SetupDiEnumDeviceInterfaces(set, NULL, &guid, index, &iface); index++)
        status = pb_win_list_one(set, &iface, fn, ctx);
    SetupDiDestroyDeviceInfoList(set);
    return status;
}

const char *pb_hid_backend(void)
{
    return "hid.dll";
}

#else

typedef int pb_hid_win_unused;

#endif
