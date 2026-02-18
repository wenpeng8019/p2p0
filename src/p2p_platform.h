/*
 * p2p_platform.h — 跨平台兼容层
 *
 * 支持三个目标平台：
 *   - macOS / Linux (POSIX)
 *   - Windows (Winsock2 / Win32)
 *
 * 提供统一封装：
 *   - Socket API (socket / bind / connect / send / recv / close)
 *   - 非阻塞设置 (p2p_set_nonblock)
 *   - 时间戳 (p2p_time_ms)
 *   - 线程 / 互斥 (p2p_mutex_t, p2p_thread_t)
 *   - Sleep (p2p_sleep_ms)
 *   - ANSI 颜色宏（Windows 终端默认关闭）
 */

#ifndef P2P_PLATFORM_H
#define P2P_PLATFORM_H

/* ============================================================================
 * 平台检测
 * ============================================================================ */
#if defined(_WIN32) || defined(_WIN64)
#   define P2P_OS_WINDOWS
#elif defined(__APPLE__)
#   define P2P_OS_MACOS
#   define P2P_OS_POSIX
#elif defined(__linux__)
#   define P2P_OS_LINUX
#   define P2P_OS_POSIX
#else
#   define P2P_OS_POSIX   /* 其他 POSIX 系统（BSD 等）*/
#endif

/* ============================================================================
 * Socket / 网络头文件
 * ============================================================================ */
#ifdef P2P_OS_WINDOWS

#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <windows.h>
#   pragma comment(lib, "ws2_32.lib")

    /* Winsock 使用 SOCKET 类型，POSIX 用 int；统一用 p2p_socket_t */
    typedef SOCKET p2p_socket_t;
#   define P2P_INVALID_SOCKET  INVALID_SOCKET
#   define P2P_SOCKET_ERROR    SOCKET_ERROR

    /* Winsock 关闭 socket */
#   define p2p_close_socket(s)  closesocket(s)

    /* errno 等价 */
#   define P2P_EAGAIN   WSAEWOULDBLOCK
#   define P2P_EINPROGRESS WSAEWOULDBLOCK
    static inline int p2p_errno(void) { return WSAGetLastError(); }

#else /* POSIX */

#   include <sys/types.h>
#   include <sys/socket.h>
#   include <sys/select.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <errno.h>

    typedef int p2p_socket_t;
#   define P2P_INVALID_SOCKET  (-1)
#   define P2P_SOCKET_ERROR    (-1)
#   define p2p_close_socket(s) close(s)
#   define P2P_EAGAIN          EAGAIN
#   define P2P_EINPROGRESS     EINPROGRESS
    static inline int p2p_errno(void) { return errno; }

#endif /* P2P_OS_WINDOWS */

/* ============================================================================
 * 非阻塞模式设置
 * ============================================================================ */
#include <stdint.h>

static inline int p2p_set_nonblock(p2p_socket_t sock) {
#ifdef P2P_OS_WINDOWS
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
#endif
}

/* ============================================================================
 * 高精度时间戳（毫秒）
 * ============================================================================ */
#ifdef P2P_OS_WINDOWS
#   include <windows.h>
    static inline uint64_t p2p_time_ms(void) {
        return (uint64_t)GetTickCount64();
    }
#elif defined(P2P_OS_MACOS)
#   include <sys/time.h>
    static inline uint64_t p2p_time_ms(void) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
    }
#else /* Linux 优先用 clock_gettime(MONOTONIC) */
#   include <time.h>
    static inline uint64_t p2p_time_ms(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
#endif

/* ============================================================================
 * Sleep（毫秒）
 * ============================================================================ */
static inline void p2p_sleep_ms(int ms) {
#ifdef P2P_OS_WINDOWS
    Sleep((DWORD)ms);
#else
    usleep((unsigned int)ms * 1000);
#endif
}

/* ============================================================================
 * 线程 / 互斥  (仅在 P2P_THREADED 时使用)
 * ============================================================================ */
#ifdef P2P_THREADED

#ifdef P2P_OS_WINDOWS

#   include <windows.h>
    typedef HANDLE          p2p_thread_t;
    typedef CRITICAL_SECTION p2p_mutex_t;

    static inline int p2p_mutex_init(p2p_mutex_t *m) {
        InitializeCriticalSection(m); return 0;
    }
    static inline void p2p_mutex_destroy(p2p_mutex_t *m) {
        DeleteCriticalSection(m);
    }
    static inline void p2p_mutex_lock(p2p_mutex_t *m) {
        EnterCriticalSection(m);
    }
    static inline void p2p_mutex_unlock(p2p_mutex_t *m) {
        LeaveCriticalSection(m);
    }

    typedef DWORD (WINAPI *p2p_thread_func_t)(LPVOID);

    static inline int p2p_thread_create(p2p_thread_t *t,
                                        DWORD (WINAPI *fn)(LPVOID), void *arg) {
        *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
        return (*t == NULL) ? -1 : 0;
    }
    static inline void p2p_thread_join(p2p_thread_t t) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }

#else /* POSIX pthreads */

#   include <pthread.h>
    typedef pthread_t       p2p_thread_t;
    typedef pthread_mutex_t p2p_mutex_t;

    static inline int  p2p_mutex_init(p2p_mutex_t *m)    { return pthread_mutex_init(m, NULL); }
    static inline void p2p_mutex_destroy(p2p_mutex_t *m)  { pthread_mutex_destroy(m); }
    static inline void p2p_mutex_lock(p2p_mutex_t *m)     { pthread_mutex_lock(m); }
    static inline void p2p_mutex_unlock(p2p_mutex_t *m)   { pthread_mutex_unlock(m); }

    static inline int p2p_thread_create(p2p_thread_t *t,
                                        void *(*fn)(void *), void *arg) {
        return pthread_create(t, NULL, fn, arg);
    }
    static inline void p2p_thread_join(p2p_thread_t t) {
        pthread_join(t, NULL);
    }

#endif /* P2P_OS_WINDOWS / POSIX */
#endif /* P2P_THREADED */

/* ============================================================================
 * ANSI 颜色（Windows 旧终端不支持，默认关闭；可通过 P2P_FORCE_COLOR 强制开启）
 * ============================================================================ */
#if defined(P2P_OS_WINDOWS) && !defined(P2P_FORCE_COLOR)
#   define P2P_COLOR_RESET   ""
#   define P2P_COLOR_RED     ""
#   define P2P_COLOR_YELLOW  ""
#   define P2P_COLOR_GREEN   ""
#   define P2P_COLOR_CYAN    ""
#   define P2P_COLOR_GRAY    ""
#else
#   define P2P_COLOR_RESET   "\033[0m"
#   define P2P_COLOR_RED     "\033[31m"
#   define P2P_COLOR_YELLOW  "\033[33m"
#   define P2P_COLOR_GREEN   "\033[32m"
#   define P2P_COLOR_CYAN    "\033[36m"
#   define P2P_COLOR_GRAY    "\033[90m"
#endif

/* ============================================================================
 * Winsock 初始化助手（在 main() 或库初始化时调用一次）
 * ============================================================================ */
#ifdef P2P_OS_WINDOWS
static inline int p2p_net_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
}
static inline void p2p_net_cleanup(void) {
    WSACleanup();
}
#else
static inline int  p2p_net_init(void)    { return 0; }
static inline void p2p_net_cleanup(void) {}
#endif

/* ============================================================================
 * 兼容宏：将代码中已有的 POSIX 调用映射到平台 API
 * ============================================================================
 *
 * 这些宏让现有 .c 文件不加修改也能在 Windows 下编译
 * （除了 fcntl / O_NONBLOCK，已由 p2p_set_nonblock 替代）
 */
#ifdef P2P_OS_WINDOWS
#   define close(s)          closesocket(s)
    /* ssize_t 不存在于 MSVC */
#   if defined(_MSC_VER) && !defined(ssize_t)
        typedef intptr_t ssize_t;
#   endif
    /* snprintf 在 MSVC < 2015 有 bug，_snprintf_s 替代；但 VS2015+ 已修复 */
#endif

#endif /* P2P_PLATFORM_H */
