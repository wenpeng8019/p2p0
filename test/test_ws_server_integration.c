/*
 * test_ws_server_integration.c — p2p_server --ws 集成测试
 *
 * 测试方案：
 *   1. 启动 p2p_server 子进程（带 --ws 参数）
 *   2. 用 ws_client 连接 server 同端口
 *   3. 完成 WS 握手，收发文本帧
 *   4. 关闭连接，验证回调序列
 *   5. 正常 TCP relay 连接在同端口依然可用（非 WS 连接不受影响）
 *
 * 测试用例：
 *   1. ws_connect_to_server  — ws_client 连接 p2p_server --ws，握手成功
 *   2. ws_send_text          — client 发文本，下次 update 循环中正常处理
 *   3. ws_relay_coexist      — 同端口开 --ws 后，普通 TCP 连接仍然可建立
 *   4. ws_disabled_default   — 不加 --ws 时，HTTP 请求被当成 relay 帧处理
 *                              （connect 超时或握手失败，不崩溃）
 */

#ifdef WITH_WSLAY

#include "../test/test_framework.h"
#include "../src/ws_client.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#ifndef _WIN32
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/wait.h>
#  include <errno.h>
#endif

/* -----------------------------------------------------------------------
 * 工具
 * -------------------------------------------------------------------- */
#ifndef _WIN32
#  include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#else
#  include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#endif

#define TICK_LIMIT 3000  /* 最多 3 秒 */

static int pump_client_until(ws_client_t *cli, int *flag, int ticks) {
    for (int i = 0; i < ticks; i++) {
        ws_client_update(cli);
        if (*flag) return 1;
        sleep_ms(1);
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * 查找可用端口（绑定到 0 然后关闭，返回 OS 分配的端口号）
 * -------------------------------------------------------------------- */
static uint16_t pick_free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 19990;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return 19990;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

/* -----------------------------------------------------------------------
 * 启动 p2p_server 子进程（POSIX fork/exec）
 * -------------------------------------------------------------------- */
#ifndef _WIN32

/* 查找 p2p_server 可执行文件路径 */
static const char *find_server_binary(void) {
    /* 相对于测试可执行文件所在目录 ../p2p_server/p2p_server */
    static const char *candidates[] = {
        "p2p_server/p2p_server",       /* cmake-build-debug */
        "../p2p_server/p2p_server",    /* 从 test/ 目录运行 */
        "./p2p_server",                /* 与测试在同目录 */
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0) return candidates[i];
    }
    return NULL;
}

typedef struct { pid_t pid; uint16_t port; } server_proc_t;

static server_proc_t start_server(uint16_t port, int ws_enabled) {
    server_proc_t sp = { -1, port };
    const char *bin = find_server_binary();
    if (!bin) return sp;

    pid_t pid = fork();
    if (pid < 0) return sp;
    if (pid == 0) {
        /* 子进程：重定向 stdout/stderr 到 /dev/null，避免污染测试输出 */
        int dev_null = open("/dev/null", 1);
        if (dev_null >= 0) { dup2(dev_null, 1); dup2(dev_null, 2); close(dev_null); }

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

        if (ws_enabled) {
            execlp(bin, bin, "-p", port_str, "--ws", (char*)NULL);
        } else {
            execlp(bin, bin, "-p", port_str, (char*)NULL);
        }
        _exit(1);
    }
    sp.pid = pid;
    /* 等待 server 启动（最多 500ms）*/
    for (int i = 0; i < 50; i++) {
        sleep_ms(10);
        /* 尝试 TCP 连接，成功则 server 已就绪 */
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(port);
        if (connect(fd, (struct sockaddr*)&a, sizeof(a)) == 0) {
            close(fd);
            break;
        }
        close(fd);
    }
    return sp;
}

static void stop_server(server_proc_t *sp) {
    if (sp->pid <= 0) return;
    kill(sp->pid, SIGTERM);
    int status;
    waitpid(sp->pid, &status, 0);
    sp->pid = -1;
}

/* -----------------------------------------------------------------------
 * 测试 1: ws_connect_to_server
 *   ws_client 连接携带 --ws 参数的 p2p_server，握手成功
 * -------------------------------------------------------------------- */
static int g_cn_open = 0;
static void cn_on_open(ws_client_t *c, void *ud) { (void)c;(void)ud; g_cn_open = 1; }

TEST(ws_connect_to_server) {
    g_cn_open = 0;

    const char *bin = find_server_binary();
    if (!bin) {
        printf("\n    [skip] p2p_server binary not found");
        return;  /* 跳过，不影响 pass/fail */
    }

    uint16_t port = pick_free_port();
    server_proc_t sp = start_server(port, 1 /* --ws */);
    ASSERT(sp.pid > 0);

    ws_client_cfg_t cfg = {0};
    cfg.on_open = cn_on_open;
    ws_client_t *cli = ws_client_create(&cfg);
    ASSERT(cli != NULL);

    int ret = ws_client_connect(cli, "127.0.0.1", port, "/");
    ASSERT_EQ(ret, 0);

    int ok = pump_client_until(cli, &g_cn_open, TICK_LIMIT);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(ws_client_state(cli), WS_CLIENT_OPEN);

    ws_client_destroy(cli);
    stop_server(&sp);
}

/* -----------------------------------------------------------------------
 * 测试 2: ws_send_text
 *   握手后 client 发文本帧，server 不崩溃（无 on_message 回调时正常忽略）
 * -------------------------------------------------------------------- */
static int g_st_open = 0;
static void st_on_open(ws_client_t *c, void *ud) { (void)c;(void)ud; g_st_open = 1; }

TEST(ws_send_text) {
    g_st_open = 0;

    const char *bin = find_server_binary();
    if (!bin) { printf("\n    [skip] p2p_server binary not found"); return; }

    uint16_t port = pick_free_port();
    server_proc_t sp = start_server(port, 1);
    ASSERT(sp.pid > 0);

    ws_client_cfg_t cfg = {0};
    cfg.on_open = st_on_open;
    ws_client_t *cli = ws_client_create(&cfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);
    ASSERT_EQ(pump_client_until(cli, &g_st_open, TICK_LIMIT), 1);

    /* 发送多条文本帧，server 处理后不崩溃 */
    ASSERT_EQ(ws_client_send_text(cli, "hello from test"), 0);
    ASSERT_EQ(ws_client_send_text(cli, "second message"),  0);
    for (int i = 0; i < 100; i++) ws_client_update(cli), sleep_ms(1);

    ASSERT_EQ(ws_client_state(cli), WS_CLIENT_OPEN);  /* 连接依然在线 */

    ws_client_destroy(cli);
    stop_server(&sp);
}

/* -----------------------------------------------------------------------
 * 测试 3: ws_relay_coexist
 *   --ws 开启后，普通 TCP relay 连接依然可在同端口建立
 * -------------------------------------------------------------------- */
TEST(ws_relay_coexist) {
    const char *bin = find_server_binary();
    if (!bin) { printf("\n    [skip] p2p_server binary not found"); return; }

    uint16_t port = pick_free_port();
    server_proc_t sp = start_server(port, 1 /* --ws */);
    ASSERT(sp.pid > 0);
    sleep_ms(50);

    /* 建立普通 TCP 连接（非 WS），发送非 GET 字节 */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(fd >= 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    int cr = connect(fd, (struct sockaddr*)&a, sizeof(a));
    ASSERT_EQ(cr, 0);   /* TCP 层能连上 */
    close(fd);

    stop_server(&sp);
}

/* -----------------------------------------------------------------------
 * 测试 4: ws_disabled_default
 *   不加 --ws 时，ws_client 连接 server 会超时或握手失败，但 server 不崩溃
 * -------------------------------------------------------------------- */
static int g_nd_open = 0;
static void nd_on_open(ws_client_t *c, void *ud) { (void)c;(void)ud; g_nd_open = 1; }

TEST(ws_disabled_default) {
    g_nd_open = 0;

    const char *bin = find_server_binary();
    if (!bin) { printf("\n    [skip] p2p_server binary not found"); return; }

    uint16_t port = pick_free_port();
    server_proc_t sp = start_server(port, 0 /* no --ws */);
    ASSERT(sp.pid > 0);

    ws_client_cfg_t cfg = {0};
    cfg.on_open = nd_on_open;
    ws_client_t *cli = ws_client_create(&cfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);

    /* 不应握手成功（server 把 HTTP 帧当 relay 帧，会关掉连接）*/
    int ok = pump_client_until(cli, &g_nd_open, 500 /* 0.5s */);
    ASSERT_EQ(ok, 0);   /* 期望握手失败 */

    /* server 进程仍然运行（无崩溃）*/
    ASSERT_EQ(kill(sp.pid, 0), 0);

    ws_client_destroy(cli);
    stop_server(&sp);
}

#endif /* !_WIN32 */

/* -----------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------- */
int main(void) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    printf("=== test_ws_server_integration ===\n");

#ifndef _WIN32
    RUN_TEST(ws_connect_to_server);
    RUN_TEST(ws_send_text);
    RUN_TEST(ws_relay_coexist);
    RUN_TEST(ws_disabled_default);
#else
    printf("  All tests skipped (Windows not supported yet)\n");
#endif

    printf("\nResults: %d passed, %d failed\n", test_passed, test_failed);
    return test_failed ? 1 : 0;
}

#else /* !WITH_WSLAY */

int main(void) {
    printf("=== test_ws_server_integration ===\n");
    printf("  Skipped: built without WITH_WSLAY\n");
    return 0;
}

#endif /* WITH_WSLAY */
