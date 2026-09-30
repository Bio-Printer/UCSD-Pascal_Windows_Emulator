// Minimal Linux stand-in for the few Win32 facilities PSystemEngine uses,
// so the REAL engine (not a mirror) can be compiled and run on Linux for
// testing -- e.g. verify/run_verify.cpp. Not used by the Windows build.
#pragma once
#include <pthread.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
typedef uint32_t DWORD; typedef int BOOL; typedef void* HANDLE; typedef const wchar_t* LPCWSTR;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
typedef pthread_mutex_t CRITICAL_SECTION;
inline void InitializeCriticalSection(CRITICAL_SECTION* c) {
    pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(c, &a); }
inline void DeleteCriticalSection(CRITICAL_SECTION* c) { pthread_mutex_destroy(c); }
inline void EnterCriticalSection(CRITICAL_SECTION* c) { pthread_mutex_lock(c); }
inline void LeaveCriticalSection(CRITICAL_SECTION* c) { pthread_mutex_unlock(c); }
struct ShimEvent { pthread_mutex_t m; pthread_cond_t cv; bool manual, set; };
static pthread_mutex_t g_shimAny = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_shimAnyCv = PTHREAD_COND_INITIALIZER;
inline HANDLE CreateEventW(void*, BOOL manual, BOOL initial, const wchar_t*) {
    ShimEvent* e = new ShimEvent; pthread_mutex_init(&e->m, nullptr); pthread_cond_init(&e->cv, nullptr);
    e->manual = manual != 0; e->set = initial != 0; return e; }
#define CreateEvent CreateEventW
inline BOOL SetEvent(HANDLE h) { ShimEvent* e = (ShimEvent*)h; pthread_mutex_lock(&g_shimAny); e->set = true;
    pthread_cond_broadcast(&g_shimAnyCv); pthread_mutex_unlock(&g_shimAny); return TRUE; }
inline BOOL ResetEvent(HANDLE h) { ShimEvent* e = (ShimEvent*)h; pthread_mutex_lock(&g_shimAny); e->set = false; pthread_mutex_unlock(&g_shimAny); return TRUE; }
inline BOOL CloseHandle(HANDLE h) { delete (ShimEvent*)h; return TRUE; }
inline DWORD WaitForMultipleObjects(DWORD n, const HANDLE* hs, BOOL, DWORD ms) {
    pthread_mutex_lock(&g_shimAny);
    if (ms == 0) {                                   // poll: no waiting at all
        for (DWORD i = 0; i < n; i++) { ShimEvent* e = (ShimEvent*)hs[i];
            if (e->set) { if (!e->manual) e->set = false; pthread_mutex_unlock(&g_shimAny); return WAIT_OBJECT_0 + i; } }
        pthread_mutex_unlock(&g_shimAny); return WAIT_TIMEOUT;
    }
    struct timespec ts; if (ms != INFINITE) { struct timeval tv; gettimeofday(&tv, nullptr);
        long long ns = (long long)tv.tv_usec * 1000 + (long long)ms * 1000000; ts.tv_sec = tv.tv_sec + ns / 1000000000; ts.tv_nsec = ns % 1000000000; }
    for (;;) {
        for (DWORD i = 0; i < n; i++) { ShimEvent* e = (ShimEvent*)hs[i];
            if (e->set) { if (!e->manual) e->set = false; pthread_mutex_unlock(&g_shimAny); return WAIT_OBJECT_0 + i; } }
        if (ms == INFINITE) pthread_cond_wait(&g_shimAnyCv, &g_shimAny);
        else if (pthread_cond_timedwait(&g_shimAnyCv, &g_shimAny, &ts) == ETIMEDOUT) { pthread_mutex_unlock(&g_shimAny); return WAIT_TIMEOUT; }
    }
}
inline DWORD WaitForSingleObject(HANDLE h, DWORD ms) { return WaitForMultipleObjects(1, &h, FALSE, ms); }
inline std::string ShimNarrow(const wchar_t* w) { std::string s; for (; *w; w++) s += (char)(*w < 128 ? *w : '?'); return s; }
inline int _wfopen_s(FILE** f, const wchar_t* path, const wchar_t* mode) {
    *f = fopen(ShimNarrow(path).c_str(), ShimNarrow(mode).c_str()); return *f ? 0 : errno; }
inline BOOL DeleteFileW(const wchar_t* path) { return unlink(ShimNarrow(path).c_str()) == 0; }
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { for (; *a && towupper(*a) == towupper(*b); a++, b++) {} return (int)towupper(*a) - (int)towupper(*b); }
#define _TRUNCATE ((size_t)-1)
#include <sys/stat.h>
#include <cstdarg>
inline BOOL CreateDirectoryW(const wchar_t* path, void*) { return mkdir(ShimNarrow(path).c_str(), 0755) == 0; }
template <size_t N> inline int sprintf_s(char (&buf)[N], const char* fmt, ...) { va_list a; va_start(a, fmt); int r = vsnprintf(buf, N, fmt, a); va_end(a); return r; }
inline int sprintf_s(char* buf, size_t n, const char* fmt, ...) { va_list a; va_start(a, fmt); int r = vsnprintf(buf, n, fmt, a); va_end(a); return r; }
