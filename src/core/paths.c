#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "pb.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define PB_SEP "\\"
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define PB_SEP "/"
#endif

static int pb_join(char *buf, size_t len, const char *base, const char *tail)
{
    int n;

    if (base == NULL || base[0] == '\0')
        return PB_ERR_NOT_FOUND;
    n = snprintf(buf, len, "%s%s%s", base, PB_SEP, tail);
    return n < 0 || (size_t)n >= len ? PB_ERR_ARG : PB_OK;
}

int pb_config_dir(char *buf, size_t len)
{
#if defined(_WIN32)
    return pb_join(buf, len, getenv("APPDATA"), "padbridge");
#elif defined(__APPLE__)
    char base[512];

    if (pb_join(base, sizeof(base), getenv("HOME"), "Library/Application Support") != PB_OK)
        return PB_ERR_NOT_FOUND;
    return pb_join(buf, len, base, "padbridge");
#else
    char base[512];
    const char *xdg = getenv("XDG_CONFIG_HOME");

    if (xdg != NULL && xdg[0] == '/')
        return pb_join(buf, len, xdg, "padbridge");
    if (pb_join(base, sizeof(base), getenv("HOME"), ".config") != PB_OK)
        return PB_ERR_NOT_FOUND;
    return pb_join(buf, len, base, "padbridge");
#endif
}

int pb_runtime_dir(char *buf, size_t len)
{
#if defined(_WIN32)
    return pb_join(buf, len, getenv("LOCALAPPDATA"), "padbridge");
#else
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    char base[512];

    if (xdg != NULL && xdg[0] == '/')
        return pb_join(buf, len, xdg, "padbridge");
    snprintf(base, sizeof(base), "/tmp/padbridge-%ld", (long)getuid());
    return snprintf(buf, len, "%s", base) >= (int)len ? PB_ERR_ARG : PB_OK;
#endif
}

int pb_make_dir(const char *path)
{
    char tmp[1024];
    size_t i;
    int n = snprintf(tmp, sizeof(tmp), "%s", path);

    if (n < 0 || (size_t)n >= sizeof(tmp))
        return PB_ERR_ARG;
    for (i = 1; i <= (size_t)n; i++) {
        char c = tmp[i];

        if (c != '/' && c != '\\' && c != '\0')
            continue;
        tmp[i] = '\0';
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST)
#else
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
#endif
            return errno == EACCES ? PB_ERR_ACCESS : PB_ERR_IO;
        tmp[i] = c;
    }
    return PB_OK;
}

int pb_dir_each(const char *dir, pb_dir_fn fn, void *ctx)
{
    char path[1024];
    int status = PB_OK;
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE h;

    if (pb_join(path, sizeof(path), dir, "*") != PB_OK)
        return PB_ERR_ARG;
    h = FindFirstFileA(path, &data);
    if (h == INVALID_HANDLE_VALUE)
        return PB_ERR_NOT_FOUND;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == '.')
            continue;
        if (pb_join(path, sizeof(path), dir, data.cFileName) == PB_OK)
            status = fn(ctx, path, data.cFileName);
    } while (status == PB_OK && FindNextFileA(h, &data));
    FindClose(h);
#else
    struct dirent *e;
    struct stat st;
    DIR *d = opendir(dir);

    if (d == NULL)
        return errno == EACCES ? PB_ERR_ACCESS : PB_ERR_NOT_FOUND;
    while (status == PB_OK && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || pb_join(path, sizeof(path), dir, e->d_name) != PB_OK)
            continue;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
            status = fn(ctx, path, e->d_name);
    }
    closedir(d);
#endif
    return status;
}

int pb_read_file(const char *path, char **out_text, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t n;

    if (f == NULL)
        return errno == EACCES ? PB_ERR_ACCESS : PB_ERR_NOT_FOUND;
    do {
        if (len + 4096 + 1 > cap) {
            char *grown = realloc(text, cap = (cap ? cap * 2 : 8192));

            if (grown == NULL) {
                free(text);
                fclose(f);
                return PB_ERR_NOMEM;
            }
            text = grown;
        }
        n = fread(text + len, 1, cap - len - 1, f);
        len += n;
    } while (n > 0 && len < (1u << 20));
    fclose(f);
    text[len] = '\0';
    *out_text = text;
    *out_len = len;
    return PB_OK;
}

int pb_replace_file(const char *path, const char *text, size_t len)
{
    char tmp[1100];
    FILE *f;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (f == NULL)
        return errno == EACCES ? PB_ERR_ACCESS : PB_ERR_IO;
    if (fwrite(text, 1, len, f) != len || fclose(f) != 0) {
        remove(tmp);
        return PB_ERR_IO;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
        return PB_ERR_IO;
#else
    if (rename(tmp, path) != 0)
        return PB_ERR_IO;
#endif
    return PB_OK;
}

long pb_process_id(void)
{
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

int pb_process_alive(long pid)
{
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    DWORD r;

    if (h == NULL)
        return 0;
    r = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return r == WAIT_TIMEOUT;
#else
    return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
#endif
}

int pb_process_signal_stop(long pid)
{
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    BOOL ok;

    if (h == NULL)
        return PB_ERR_NOT_FOUND;
    ok = TerminateProcess(h, PB_EXIT_INTERRUPTED);
    CloseHandle(h);
    return ok ? PB_OK : PB_ERR_ACCESS;
#else
    if (pid <= 0)
        return PB_ERR_ARG;
    if (kill((pid_t)pid, SIGTERM) == 0)
        return PB_OK;
    return errno == EPERM ? PB_ERR_ACCESS : PB_ERR_NOT_FOUND;
#endif
}
