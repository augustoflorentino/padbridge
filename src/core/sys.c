#include <signal.h>
#include <stdlib.h>
#include "pb.h"
#include "sys.h"

#ifdef _WIN32
#include <windows.h>

struct pb_thread {
    HANDLE handle;
    int (*fn)(void *ctx);
    void *ctx;
    int result;
};

struct pb_mutex {
    CRITICAL_SECTION cs;
};

static DWORD WINAPI pb_thread_main(LPVOID arg)
{
    pb_thread_t *t = arg;

    t->result = t->fn(t->ctx);
    return 0;
}

int pb_thread_start(pb_thread_t **out, int (*fn)(void *ctx), void *ctx)
{
    pb_thread_t *t = calloc(1, sizeof(*t));

    if (t == NULL)
        return PB_ERR_NOMEM;
    t->fn = fn;
    t->ctx = ctx;
    t->handle = CreateThread(NULL, 0, pb_thread_main, t, 0, NULL);
    if (t->handle == NULL) {
        free(t);
        return PB_ERR_IO;
    }
    *out = t;
    return PB_OK;
}

int pb_thread_join(pb_thread_t *thread)
{
    int result;

    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    result = thread->result;
    free(thread);
    return result;
}

int pb_mutex_create(pb_mutex_t **out)
{
    pb_mutex_t *m = malloc(sizeof(*m));

    if (m == NULL)
        return PB_ERR_NOMEM;
    InitializeCriticalSection(&m->cs);
    *out = m;
    return PB_OK;
}

void pb_mutex_lock(pb_mutex_t *mutex)
{
    EnterCriticalSection(&mutex->cs);
}

void pb_mutex_unlock(pb_mutex_t *mutex)
{
    LeaveCriticalSection(&mutex->cs);
}

void pb_mutex_destroy(pb_mutex_t *mutex)
{
    if (mutex == NULL)
        return;
    DeleteCriticalSection(&mutex->cs);
    free(mutex);
}

uint64_t pb_now_ns(void)
{
    LARGE_INTEGER freq, now;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000000u
           + (uint64_t)(now.QuadPart % freq.QuadPart) * 1000000000u / (uint64_t)freq.QuadPart;
}

void pb_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}
#else
#include <pthread.h>
#include <time.h>

struct pb_thread {
    pthread_t id;
    int (*fn)(void *ctx);
    void *ctx;
    int result;
};

struct pb_mutex {
    pthread_mutex_t m;
};

static void *pb_thread_main(void *arg)
{
    pb_thread_t *t = arg;

    t->result = t->fn(t->ctx);
    return NULL;
}

int pb_thread_start(pb_thread_t **out, int (*fn)(void *ctx), void *ctx)
{
    pb_thread_t *t = calloc(1, sizeof(*t));

    if (t == NULL)
        return PB_ERR_NOMEM;
    t->fn = fn;
    t->ctx = ctx;
    if (pthread_create(&t->id, NULL, pb_thread_main, t) != 0) {
        free(t);
        return PB_ERR_IO;
    }
    *out = t;
    return PB_OK;
}

int pb_thread_join(pb_thread_t *thread)
{
    int result;

    pthread_join(thread->id, NULL);
    result = thread->result;
    free(thread);
    return result;
}

int pb_mutex_create(pb_mutex_t **out)
{
    pb_mutex_t *m = malloc(sizeof(*m));

    if (m == NULL)
        return PB_ERR_NOMEM;
    pthread_mutex_init(&m->m, NULL);
    *out = m;
    return PB_OK;
}

void pb_mutex_lock(pb_mutex_t *mutex)
{
    pthread_mutex_lock(&mutex->m);
}

void pb_mutex_unlock(pb_mutex_t *mutex)
{
    pthread_mutex_unlock(&mutex->m);
}

void pb_mutex_destroy(pb_mutex_t *mutex)
{
    if (mutex == NULL)
        return;
    pthread_mutex_destroy(&mutex->m);
    free(mutex);
}

uint64_t pb_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void pb_sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

static volatile sig_atomic_t pb_stop_flag;

static void pb_on_signal(int sig)
{
    (void)sig;
    pb_stop_flag = 1;
}

uint32_t pb_now_ms(void)
{
    return (uint32_t)(pb_now_ns() / 1000000u);
}

int pb_interrupted(void)
{
    return pb_stop_flag != 0;
}

void pb_catch_interrupt(void)
{
    signal(SIGINT, pb_on_signal);
    signal(SIGTERM, pb_on_signal);
}
