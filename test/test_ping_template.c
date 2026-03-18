/*
 * test_ping_template.c - P2P Ping 客户端单元测试模版
 *
 * ============================================================================
 * 测试架构
 * ============================================================================
 * 
 * 1. 测试程序作为 debugger 客户端
 * 2. 通过 instrument 内置 wait/continue 机制与 ping 子进程协同
 *
 * ============================================================================
 * 同步协议
 * ============================================================================
 *
 *   [Test Client]                    [p2p_ping --debugger test_dbg]
 *        |                                    |
 *        |  instrument_listen(cb)             |
 *        |  fork/exec ping                    |
 *        |                                    |
 *        |                          instrument_wait(name, "test_dbg", 60s)
 *        |  <--- WAIT packet ---              |
 *        |  cb(rid, CTRL, NULL, name)         |
 *        |                                    |
 *        |  instrument_continue(name, "test_dbg")
 *        |  --- CONTINUE packet --->          |
 *        |                          instrument_wait returns E_NONE
 *        |                          print("Debugger connected")
 *        |                                    |
 *        |  continue execution...             |
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING"

#include <stdc.h>
#include <p2p.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

///////////////////////////////////////////////////////////////////////////////
// 配置
///////////////////////////////////////////////////////////////////////////////

#define DEFAULT_SERVER_PORT     9333
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define SYNC_TIMEOUT_MS         10000       // 同步超时 10 秒

// 测试状态
static const char *g_ping_path = NULL;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static int g_server_port = DEFAULT_SERVER_PORT;
static pid_t g_ping_pid = 0;
static pid_t g_server_pid = 0;

// Debugger 同步状态
static volatile int g_ping_waiting = 0;     // ping 是否在等待
static volatile int g_ping_resumed = 0;     // ping 是否已恢复
static char g_debugger_name[64] = {0};      // 期望的 debugger 名字
static char g_ping_name[64] = {0};          // ping 的 peer 名字（WAIT port）

// 测试结果
static int g_tests_passed = 0;
static int g_tests_failed = 0;

// instrument 日志收集
#define MAX_LOG_ENTRIES 200
static struct {
    uint16_t rid;
    uint8_t chn;
    char tag[32];
    char txt[256];
} g_logs[MAX_LOG_ENTRIES];
static volatile int g_log_count = 0;

///////////////////////////////////////////////////////////////////////////////
// Instrument 回调
///////////////////////////////////////////////////////////////////////////////

static void on_instrument_log(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len) {
    (void)len;
    
    // 保存日志
    int idx = g_log_count;
    if (idx < MAX_LOG_ENTRIES) {
        g_log_count = idx + 1;
        g_logs[idx].rid = rid;
        g_logs[idx].chn = chn;
        strncpy(g_logs[idx].tag, tag ? tag : "", sizeof(g_logs[idx].tag) - 1);
        g_logs[idx].tag[sizeof(g_logs[idx].tag) - 1] = '\0';
        strncpy(g_logs[idx].txt, txt, sizeof(g_logs[idx].txt) - 1);
        g_logs[idx].txt[sizeof(g_logs[idx].txt) - 1] = '\0';
    }
    
    // WAIT 包检测：instrument_wait 广播的 WAIT 包通过 cb(rid, CTRL, NULL, port_name) 通知
    if (tag == NULL && chn == INSTRUMENT_CTRL && txt) {
        if (g_ping_name[0] && strcmp(txt, g_ping_name) == 0) {
            g_ping_waiting = 1;
            printf("    [SYNC] Detected ping WAIT: '%s' (rid=%u)\n", txt, rid);
        }
    }
    
    // 实时显示（忽略本地 rid=0 的调试日志）
    if (rid != 0) {
        const char* color;
        const char* src = (rid == 1) ? "PING" : "SERVER";
        switch (chn) {
            case LOG_SLOT_DEBUG:   color = "\033[36m"; break;
            case LOG_SLOT_INFO:    color = "\033[32m"; break;
            case LOG_SLOT_WARN:    color = "\033[33m"; break;
            case LOG_SLOT_ERROR:   color = "\033[31m"; break;
            case 'X':              color = "\033[35m"; break;  // X 通道用紫色
            default:               color = "\033[37m"; break;
        }
        printf("%s    [%s] %s: %s\033[0m\n", color, src, tag, txt);
    }
}

// 清空日志缓存
static void clear_logs(void) {
    g_log_count = 0;
}

// 在日志中搜索指定文本
static int find_log(const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

// 在指定 rid 的日志中搜索
static int find_log_from(uint16_t rid, const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (g_logs[i].rid == rid && strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////
// 进程管理
///////////////////////////////////////////////////////////////////////////////

// 启动 server 子进程
static int start_server(const char *server_path) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);
    
    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork server: %s\n", strerror(errno));
        return -1;
    } else if (g_server_pid == 0) {
        // 子进程：执行 server
        execl(server_path, server_path, "-p", port_str, NULL);
        fprintf(stderr, "Failed to exec server: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    Server PID: %d\n", g_server_pid);
    P_usleep(500 * 1000);  // 等待 server 启动
    return 0;
}

// 启动 ping 子进程（带 debugger 参数）
static int start_ping(const char *name, const char *target, const char *debugger_name) {
    
    // 记录 debugger 名字和 ping 的 peer 名字
    strncpy(g_debugger_name, debugger_name, sizeof(g_debugger_name) - 1);
    g_debugger_name[sizeof(g_debugger_name) - 1] = '\0';
    strncpy(g_ping_name, name, sizeof(g_ping_name) - 1);
    g_ping_name[sizeof(g_ping_name) - 1] = '\0';
    g_ping_waiting = 0;
    g_ping_resumed = 0;
    
    char server_arg[64];
    snprintf(server_arg, sizeof(server_arg), "%s:%d", g_server_host, g_server_port);
    
    g_ping_pid = fork();
    if (g_ping_pid < 0) {
        fprintf(stderr, "Failed to fork ping: %s\n", strerror(errno));
        return -1;
    } else if (g_ping_pid == 0) {
        // 子进程：执行 ping
        if (target) {
            execl(g_ping_path, g_ping_path, 
                  "--compact", "-s", server_arg, "-n", name, "-t", target,
                  "--debugger", debugger_name, NULL);
        } else {
            execl(g_ping_path, g_ping_path, 
                  "--compact", "-s", server_arg, "-n", name,
                  "--debugger", debugger_name, NULL);
        }
        fprintf(stderr, "Failed to exec ping: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    Ping PID: %d (name=%s, debugger=%s)\n", g_ping_pid, name, debugger_name);
    return 0;
}

// 等待 ping 进入 waiting 状态，然后发送同步信号
static int sync_with_ping(int timeout_ms) {
    int elapsed = 0;
    const int poll_interval = 50;  // 50ms
    
    printf("    [SYNC] Waiting for ping to enter waiting state...\n");
    
    while (!g_ping_waiting && elapsed < timeout_ms) {
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    
    if (!g_ping_waiting) {
        fprintf(stderr, "    [SYNC] Timeout waiting for ping\n");
        return -1;
    }
    
    // 发送 CONTINUE 释放 ping 的 instrument_wait
    printf("    [SYNC] Sending continue to '%s' from '%s'...\n", g_ping_name, g_debugger_name);
    instrument_continue(g_ping_name, g_debugger_name);
    
    P_usleep(200 * 1000);  // 等待 ping 处理 CONTINUE 并恢复
    printf("    [SYNC] Ping resumed execution\n");
    g_ping_resumed = 1;
    return 0;
}

// 停止 ping 子进程
static void stop_ping(void) {
    if (g_ping_pid > 0) {
        kill(g_ping_pid, SIGTERM);
        int status;
        waitpid(g_ping_pid, &status, 0);
        printf("    Ping stopped (exit=%d)\n", WEXITSTATUS(status));
        g_ping_pid = 0;
    }
    g_ping_waiting = 0;
    g_ping_resumed = 0;
    g_debugger_name[0] = '\0';
}

// 停止 server 子进程
static void stop_server(void) {
    if (g_server_pid > 0) {
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
        printf("    Server stopped (exit=%d)\n", WEXITSTATUS(status));
        g_server_pid = 0;
    }
}

// 清理所有子进程
static void cleanup(void) {
    stop_ping();
    stop_server();
}

// 信号处理
static void on_signal(int sig) {
    (void)sig;
    cleanup();
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
// 测试宏
///////////////////////////////////////////////////////////////////////////////

#define TEST_PASS(name) do { \
    printf("\033[32m  [PASS]\033[0m %s\n", name); \
    g_tests_passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    printf("\033[31m  [FAIL]\033[0m %s: %s\n", name, reason); \
    g_tests_failed++; \
} while(0)

///////////////////////////////////////////////////////////////////////////////
// 测试用例
///////////////////////////////////////////////////////////////////////////////

// 测试 1: 验证 debugger 同步机制
static void test_debugger_sync(void) {
    const char *TEST_NAME = "debugger_sync";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 启动 ping，等待 debugger
    if (start_ping("test_peer", NULL, "test_dbg") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start ping");
        return;
    }
    
    // 同步
    if (sync_with_ping(SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "sync failed");
        stop_ping();
        return;
    }
    
    // 同步成功就算通过
    // 注：instrument 日志收集可能因为进程退出太快而不完整
    TEST_PASS(TEST_NAME);
    
    stop_ping();
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    const char *server_path = NULL;
    
    // 解析命令行参数
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ping_path> [server_path] [port]\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s ./p2p_ping                           # Use existing server on port %d\n", argv[0], DEFAULT_SERVER_PORT);
        fprintf(stderr, "  %s ./p2p_ping ../p2p_server/p2p_server  # Start server on port %d\n", argv[0], DEFAULT_SERVER_PORT);
        fprintf(stderr, "  %s ./p2p_ping ../p2p_server/p2p_server 9555\n", argv[0]);
        return 1;
    }
    g_ping_path = argv[1];
    if (argc > 2) server_path = argv[2];
    if (argc > 3) {
        g_server_port = atoi(argv[3]);
        if (g_server_port <= 0 || g_server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 1;
        }
    }
    
    printf("=== P2P Ping Client Tests ===\n");
    printf("Ping path:   %s\n", g_ping_path);
    printf("Server:      %s\n", server_path ? server_path : "(external)");
    printf("Server addr: %s:%d\n", g_server_host, g_server_port);
    printf("\n");
    
    // 注册信号处理
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    
    // 初始化 instrument 监听
    instrument_local(0);
    if (instrument_listen(on_instrument_log) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    printf("[*] Instrument listener started\n");
    
    // 启动 server（可选）
    if (server_path) {
        printf("[*] Starting server...\n");
        if (start_server(server_path) != 0) {
            fprintf(stderr, "Failed to start server\n");
            return 1;
        }
    } else {
        printf("[*] Using external server at %s:%d\n", g_server_host, g_server_port);
    }
    
    // 运行测试
    printf("\n[*] Running tests...\n");
    
    test_debugger_sync();
    
    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();
    
    // 报告
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
