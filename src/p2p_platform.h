/*
 * 跨平台兼容层
 *
 * 支持三个目标平台：
 *   - macOS / Linux (POSIX)
 *   - Windows (Win32)
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

/* Packed struct: 直接使用 #pragma pack(push,1) / #pragma pack(pop)，
 * MSVC / GCC / Clang 均支持，无需平台宏。*/

/* ============================================================================
 * 数据类型
 * ============================================================================ */
#ifdef _WIN32

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

    typedef SOCKET p2p_socket_t;
#   define P2P_INVALID_SOCKET  INVALID_SOCKET
#   define P2P_SOCKET_ERROR    SOCKET_ERROR
#   define p2p_close_socket(s)  closesocket(s)
#   define P2P_EAGAIN   WSAEWOULDBLOCK
#   define P2P_EINPROGRESS WSAEWOULDBLOCK
    static inline int p2p_errno(void) { return WSAGetLastError(); }

    typedef unsigned short sa_family_t;
    typedef unsigned short in_port_t;

    /* Windows 不支持 POSIX sig_atomic_t，使用 volatile int 作为替代 */
    typedef volatile int sig_atomic_t;

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

#endif /* _WIN32 */

/* ============================================================================
 * 字节序转换（64-bit: htonll / ntohll）
 * ============================================================================ */
#if defined(_WIN32)
    /* Windows: SDK 在 Windows 8+ 或定义 INCL_EXTRA_HTON_FUNCTIONS 时提供 htonll/ntohll
     * 参考 winsock2.h 条件判断逻辑 */
#   if !defined(INCL_EXTRA_HTON_FUNCTIONS) && \
       (!defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06020000) /* NTDDI_WIN8 */
    /* 旧版 Windows (< Win8) 且未强制启用，需要自己实现 */
    static inline uint64_t htonll(uint64_t x) {
        return (((uint64_t)htonl((uint32_t)(x & 0xFFFFFFFFULL))) << 32) |
            (uint64_t)htonl((uint32_t)(x >> 32));
    }
    static inline uint64_t ntohll(uint64_t x) { return htonll(x); }
#   endif
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    /* Linux/BSD: <endian.h> 提供 htobe64/be64toh */
#   include <endian.h>
#   ifndef htonll
#       define htonll(x) htobe64(x)
#   endif
#   ifndef ntohll
#       define ntohll(x) be64toh(x)
#   endif
#elif defined(__APPLE__)
    /* macOS: 系统头已定义 htonll/ntohll，用 #ifndef 保护以免重定义 */
#   include <libkern/OSByteOrder.h>
#   ifndef htonll
#       define htonll(x) OSSwapHostToBigInt64(x)
#   endif
#   ifndef ntohll
#       define ntohll(x) OSSwapBigToHostInt64(x)
#   endif
#else
  /* 通用 fallback */
  static inline uint64_t htonll(uint64_t x) {
      const uint32_t hi = htonl((uint32_t)(x >> 32));
      const uint32_t lo = htonl((uint32_t)(x & 0xFFFFFFFFULL));
      return ((uint64_t)lo << 32) | hi;
  }
  static inline uint64_t ntohll(uint64_t x) { return htonll(x); }
#endif

/* ============================================================================
 * 系统时间
 * ============================================================================ */

 #ifdef _WIN32
#   include <sys/timeb.h>
    static inline uint64_t p2p_time_ms(void) {
        struct __timeb64 tb;
        _ftime64_s(&tb);
        return (uint64_t)tb.time * 1000 + (uint64_t)tb.millitm;
    }
#elif defined(__APPLE__)
#   include <sys/time.h>
    static inline uint64_t p2p_time_ms(void) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
    }
#else /* Linux / other POSIX */
#   include <time.h>
    static inline uint64_t p2p_time_ms(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
#endif

static inline void p2p_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((unsigned int)ms * 1000);
#endif
}

/* ============================================================================
 * 线程 / 互斥
 * ============================================================================ */
#ifdef P2P_THREADED

#ifdef _WIN32

#   include <windows.h>
    typedef HANDLE           p2p_thread_t;
    typedef CRITICAL_SECTION p2p_mutex_t;

    static inline int p2p_mutex_init(p2p_mutex_t *m) {
        InitializeCriticalSection(m); return 0;
    }
    static inline void p2p_mutex_destroy(p2p_mutex_t *m) { DeleteCriticalSection(m); }
    static inline void p2p_mutex_lock(p2p_mutex_t *m)    { EnterCriticalSection(m); }
    static inline void p2p_mutex_unlock(p2p_mutex_t *m)  { LeaveCriticalSection(m); }

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

    static inline int  p2p_mutex_init(p2p_mutex_t *m)   { return pthread_mutex_init(m, NULL); }
    static inline void p2p_mutex_destroy(p2p_mutex_t *m) { pthread_mutex_destroy(m); }
    static inline void p2p_mutex_lock(p2p_mutex_t *m)    { pthread_mutex_lock(m); }
    static inline void p2p_mutex_unlock(p2p_mutex_t *m)  { pthread_mutex_unlock(m); }

    static inline int p2p_thread_create(p2p_thread_t *t,
                                        void *(*fn)(void *), void *arg) {
        return pthread_create(t, NULL, fn, arg);
    }
    static inline void p2p_thread_join(p2p_thread_t t) { pthread_join(t, NULL); }

#endif /* _WIN32 / POSIX */
#endif /* P2P_THREADED */

/* ============================================================================
 * 网络相关工具函数
 * ============================================================================ */

// 网络初始化（Windows 平台的特性，其他平台无操作适配）
#ifdef _WIN32
static inline int p2p_net_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
}
static inline void p2p_net_cleanup(void) { WSACleanup(); }
#else
static inline int  p2p_net_init(void)    { return 0; }
static inline void p2p_net_cleanup(void) {}
#endif

// 关闭套接字的跨平台适配
#ifdef _WIN32
#   define close(s) closesocket(s)
#   if defined(_MSC_VER) && !defined(ssize_t)
        typedef intptr_t ssize_t;
#   endif
#endif

// 非阻塞模式设置
static inline int p2p_set_nonblock(p2p_socket_t sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
#endif
}

/* ============================================================================
 * 终端操作（跨平台）
 * ============================================================================ */

/* 终端检测：检查文件流是否是终端设备 */
#ifdef _WIN32
#   include <io.h>  /* _isatty, _fileno */
#   define p2p_isatty(f) _isatty(_fileno(f))
#else
    /* POSIX: isatty / fileno 由 unistd.h 提供（已在前面包含） */
#   define p2p_isatty(f) isatty(fileno(f))
#endif

/* 终端尺寸：获取终端行数和列数 */
#ifndef _WIN32
#   include <sys/ioctl.h>  /* ioctl, TIOCGWINSZ */
#endif

static inline int p2p_get_terminal_rows(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (rows > 4) return rows;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 4)
        return (int)ws.ws_row;
#endif
    return 24;  /* 默认值 */
}

static inline int p2p_get_terminal_cols(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (cols > 10) return cols;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 10)
        return (int)ws.ws_col;
#endif
    return 80;  /* 默认值 */
}

/*
 * ============================================================================
 * 注意：以下终端操作不适合跨平台统一封装（应由应用层按需实现）
 * ============================================================================
 *
 * 1. 终端模式控制（raw 模式 / 行缓冲 / 回显设置）：
 *    【Windows】GetConsoleMode() + SetConsoleMode()
 *               - 使用 DWORD 标志位组合（ENABLE_LINE_INPUT, ENABLE_ECHO_INPUT 等）
 *               - 需区分输入/输出句柄，支持 VT 模式（ENABLE_VIRTUAL_TERMINAL_PROCESSING）
 *    【POSIX】 tcgetattr() + tcsetattr()
 *               - 使用 termios 结构体，包含 c_iflag / c_lflag / c_cc 等多个字段
 *               - 典型 raw 模式设置：t.c_lflag &= ~(ICANON | ECHO)
 *    → 两者语义和 API 完全不同，强行统一会失去灵活性
 *
 * 2. 非阻塞键盘输入检测与读取：
 *    【Windows 真实控制台】_kbhit() + _getch()           （需 <conio.h>）
 *    【Windows ConPTY/管道】PeekNamedPipe() + ReadFile()  （VS Code 等模拟终端）
 *    【POSIX】read(STDIN_FILENO, ...) 配合 fcntl(O_NONBLOCK) + termios raw 模式
 *    → 实现方式差异巨大，依赖终端模式预配置，不适合通用封装
 *
 * 3. 终端专有 API（高度应用相关）：
 *    - POSIX: 窗口大小变化信号 SIGWINCH
 *    - Windows: Console Buffer / Screen Buffer 操作
 *    - 光标控制、滚动区域设置（ANSI 转义序列）
 */

// ANSI 颜色（Windows 旧终端默认关闭；可通过 P2P_FORCE_COLOR 强制开启）
#if defined(_WIN32) && !defined(P2P_FORCE_COLOR)
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

#endif /* P2P_PLATFORM_H */
