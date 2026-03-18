/*
 * test_ping2_template.c - P2P Ping 双客户端连接测试模板
 *
 * ============================================================================
 * 测试架构
 * ============================================================================
 * 
 *                      +------------+
 *                      |   Server   |
 *                      | (compact)  |
 *                      +-----+------+
 *                            |
 *              +-------------+-------------+
 *              |                           |
 *        +-----+-----+               +-----+-----+
 *        |   Alice   |  <------->    |    Bob    |
 *        | (waiting) |   P2P conn    | (connect) |
 *        +-----------+               +-----------+
 *
 * 1. 启动 compact signaling server
 * 2. 启动 Alice (--debugger alice, -t bob)
 * 3. 启动 Bob   (--debugger bob, -t alice)
 * 4. 通过 instrument 日志观察连接流程
 * 5. 验证连接成功（双方都收到 "P2P connection established"）
 *
 * 注意：COMPACT 模式要求双方都指定 target
 *
 * ============================================================================
 * 同步协议
 * ============================================================================
 *
 *   [Test]            [Alice]            [Bob]
 *     |                  |                  |
 *     |  fork alice      |                  |
 *     |  ------>         |                  |
 *     |           instrument_wait("alice", "alice", 60s)
 *     |  <-- WAIT ---    |                  |
 *     |  fork bob        |                  |
 *     |  ---------------------------------->|
 *     |                  | instrument_wait("bob", "bob", 60s)
 *     |  <-- WAIT ---------------------------|
 *     |                  |                  |
 *     |  instrument_continue("alice", "alice")
 *     |  --- CONTINUE -->|                  |
 *     |           "Debugger connected"      |
 *     |                  |                  |
 *     |  instrument_continue("bob", "bob")  |
 *     |  --- CONTINUE ---------------------->|
 *     |                  |   "Debugger connected"
 *     |                  |                  |
 *     |           <---- P2P Connection ---->|
 *     |                  |                  |
 *     |    logs...  "P2P connection established"
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING2"

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

#define DEFAULT_SERVER_PORT     9334
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define SYNC_TIMEOUT_MS         10000       // 同步超时 10 秒
#define CONNECT_TIMEOUT_MS      30000       // 连接超时 30 秒

// 测试状态
static const char *g_ping_path = NULL;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static int g_server_port = DEFAULT_SERVER_PORT;
static pid_t g_server_pid = 0;

// 客户端状态
typedef struct {
    const char *name;           // "alice" 或 "bob"
    pid_t pid;                  // 进程 PID
    uint16_t rid;               // instrument rid (通过 waiting 消息获取)
    volatile int waiting;       // 是否在等待 debugger
    volatile int resumed;       // 是否已恢复执行
    volatile int connected;     // 是否已连接成功
} ping_client_t;

static ping_client_t g_alice = { .name = "alice" };
static ping_client_t g_bob   = { .name = "bob" };

// 测试结果
static int g_tests_passed = 0;
static int g_tests_failed = 0;

// instrument 日志收集
#define MAX_LOG_ENTRIES 500
static struct {
    uint16_t rid;
    uint8_t chn;
    char tag[32];
    char txt[256];
    int64_t ts;
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
        g_logs[idx].ts = P_tick_ms();
        strncpy(g_logs[idx].tag, tag ? tag : "", sizeof(g_logs[idx].tag) - 1);
        g_logs[idx].tag[sizeof(g_logs[idx].tag) - 1] = '\0';
        strncpy(g_logs[idx].txt, txt, sizeof(g_logs[idx].txt) - 1);
        g_logs[idx].txt[sizeof(g_logs[idx].txt) - 1] = '\0';
    }
    
    // WAIT 包检测：instrument_wait 广播的 WAIT 包通过 cb(rid, CTRL, NULL, port_name) 通知
    if (tag == NULL && chn == INSTRUMENT_CTRL && txt) {
        if (strcmp(txt, g_alice.name) == 0 && g_alice.rid == 0) {
            g_alice.rid = rid;
            g_alice.waiting = 1;
            printf("    [SYNC] Alice WAIT detected (rid=%u)\n", rid);
        } else if (strcmp(txt, g_bob.name) == 0 && g_bob.rid == 0) {
            g_bob.rid = rid;
            g_bob.waiting = 1;
            printf("    [SYNC] Bob WAIT detected (rid=%u)\n", rid);
        }
    }
    
    // 检测连接成功
    // COMPACT 模式: "NAT_CONNECTED"
    // ICE 模式: "P2P connection established" 或 "Nomination successful"
    if (txt && (strstr(txt, "P2P connection established") || 
                strstr(txt, "Nomination successful") ||
                strstr(txt, "NAT_CONNECTED"))) {
        if (rid == g_alice.rid) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }
    
    // 实时显示日志（忽略 rid=0，只在调试时启用）
    #if 0
    if (rid != 0) {
        const char* color;
        const char* src = "???";
        if (rid == g_alice.rid) src = "ALICE";
        else if (rid == g_bob.rid) src = "BOB";
        else {
            // 未知 rid，可能是尚未识别的客户端
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", rid);
            src = buf;
        }
        
        switch (chn) {
            case LOG_SLOT_DEBUG:   color = "\033[36m"; break;
            case LOG_SLOT_INFO:    color = "\033[32m"; break;
            case LOG_SLOT_WARN:    color = "\033[33m"; break;
            case LOG_SLOT_ERROR:   color = "\033[31m"; break;
            case 'X':              color = "\033[35m"; break;
            default:               color = "\033[37m"; break;
        }
        printf("%s    [%s] %s: %s\033[0m\n", color, src, tag, txt);
    }
    #endif
}

// 清空日志缓存
static void clear_logs(void) {
    g_log_count = 0;
}

// 在指定 rid 的日志中搜索
static int find_log_from_rid(uint16_t rid, const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (g_logs[i].rid == rid && strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

// 打印日志摘要
static void print_log_summary(void) {
    printf("\n--- Log Summary ---\n");
    printf("Total logs: %d\n", g_log_count);
    
    if (g_alice.rid) {
        int alice_logs = 0;
        for (int i = 0; i < g_log_count; i++) {
            if (g_logs[i].rid == g_alice.rid) alice_logs++;
        }
        printf("Alice (rid=%u): %d logs\n", g_alice.rid, alice_logs);
    }
    
    if (g_bob.rid) {
        int bob_logs = 0;
        for (int i = 0; i < g_log_count; i++) {
            if (g_logs[i].rid == g_bob.rid) bob_logs++;
        }
        printf("Bob (rid=%u): %d logs\n", g_bob.rid, bob_logs);
    }
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
        execl(server_path, server_path, "-p", port_str, NULL);
        fprintf(stderr, "Failed to exec server: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    Server PID: %d\n", g_server_pid);
    P_usleep(500 * 1000);  // 等待 server 启动
    return 0;
}

// 启动 ping 客户端
static int start_ping_client(ping_client_t *client, const char *target) {
    char server_arg[64];
    snprintf(server_arg, sizeof(server_arg), "%s:%d", g_server_host, g_server_port);
    
    // 重置状态
    client->pid = 0;
    client->rid = 0;
    client->waiting = 0;
    client->resumed = 0;
    client->connected = 0;
    
    client->pid = fork();
    if (client->pid < 0) {
        fprintf(stderr, "Failed to fork %s: %s\n", client->name, strerror(errno));
        return -1;
    } else if (client->pid == 0) {
        // 子进程
        if (target) {
            execl(g_ping_path, g_ping_path, 
                  "--compact", "-s", server_arg, 
                  "-n", client->name, "-t", target,
                  "--debugger", client->name, 
                  NULL);
        } else {
            execl(g_ping_path, g_ping_path, 
                  "--compact", "-s", server_arg, 
                  "-n", client->name,
                  "--debugger", client->name, 
                  NULL);
        }
        fprintf(stderr, "Failed to exec %s: %s\n", client->name, strerror(errno));
        _exit(127);
    }
    
    printf("    %s PID: %d (target=%s)\n", client->name, client->pid, target ? target : "none");
    return 0;
}

// 等待客户端进入 waiting 状态
static int wait_for_waiting(ping_client_t *client, int timeout_ms) {
    int elapsed = 0;
    const int poll_interval = 50;
    
    while (!client->waiting && elapsed < timeout_ms) {
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    return client->waiting ? 0 : -1;
}

// 发送 CONTINUE 释放客户端的 instrument_wait
static int sync_client(ping_client_t *client) {
    if (!client->waiting) {
        fprintf(stderr, "    [SYNC] %s not waiting\n", client->name);
        return -1;
    }
    
    printf("    [SYNC] Sending continue to '%s' from '%s'...\n", client->name, client->name);
    instrument_continue(client->name, client->name);
    
    P_usleep(200 * 1000);  // 等待客户端处理 CONTINUE 并恢复
    printf("    [SYNC] %s resumed\n", client->name);
    client->resumed = 1;
    return 0;
}

// 等待两个客户端都连接成功
static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    const int poll_interval = 100;
    
    printf("    [CONN] Waiting for P2P connection...\n");
    
    while (elapsed < timeout_ms) {
        if (g_alice.connected && g_bob.connected) {
            printf("    [CONN] Both clients connected!\n");
            return 0;
        }
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    
    printf("    [CONN] Timeout! alice=%d, bob=%d\n", 
           g_alice.connected, g_bob.connected);
    return -1;
}

// 停止客户端
static void stop_client(ping_client_t *client) {
    if (client->pid > 0) {
        kill(client->pid, SIGTERM);
        int status;
        waitpid(client->pid, &status, 0);
        printf("    %s stopped (exit=%d)\n", client->name, WEXITSTATUS(status));
        client->pid = 0;
    }
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

// 清理所有进程
static void cleanup(void) {
    stop_client(&g_alice);
    stop_client(&g_bob);
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

// 测试 1: 基本 P2P 连接（Alice <-> Bob）
// 注：COMPACT 模式要求双方都指定 target
static void test_basic_connection(void) {
    const char *TEST_NAME = "basic_connection";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 1. 启动 Alice (target=bob)
    printf("[1] Starting Alice (target=bob)...\n");
    if (start_ping_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    
    // 等待 Alice 进入 waiting 状态
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        return;
    }
    
    // 2. 同步 Alice（让它停止发送 waiting 消息）
    printf("[2] Syncing Alice first (to stop waiting messages)...\n");
    if (sync_client(&g_alice) != 0) {
        TEST_FAIL(TEST_NAME, "alice sync failed");
        stop_client(&g_alice);
        return;
    }
    
    // 等待一下让序号稳定
    P_usleep(200 * 1000);
    
    // 3. 启动 Bob (target=alice)
    printf("[3] Starting Bob (target=alice)...\n");
    if (start_ping_client(&g_bob, "alice") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        stop_client(&g_alice);
        return;
    }
    
    // 等待 Bob 进入 waiting 状态
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 4. 同步 Bob
    printf("[4] Syncing Bob...\n");
    if (sync_client(&g_bob) != 0) {
        TEST_FAIL(TEST_NAME, "bob sync failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 5. 等待连接成功
    printf("[5] Waiting for P2P connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        // 打印日志分析
        print_log_summary();
        
        // 检查是否有候选交换
        int alice_cand = find_log_from_rid(g_alice.rid, "remote");
        int bob_cand = find_log_from_rid(g_bob.rid, "remote");
        printf("Alice received remote cand: %s\n", alice_cand >= 0 ? "yes" : "no");
        printf("Bob received remote cand: %s\n", bob_cand >= 0 ? "yes" : "no");
        
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // wait_for_connection 已通过回调确认双方连接成功，无需再从日志搜索
    // （UDP 组播高负载下丢包可能导致日志条目缺失，但回调中实时检测已足够可靠）
    
    print_log_summary();
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    const char *server_path = NULL;
    
    // 解析命令行参数
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ping_path> <server_path> [port]\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server        # Use port %d\n", argv[0], DEFAULT_SERVER_PORT);
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server 9555   # Use custom port\n", argv[0]);
        return 1;
    }
    g_ping_path = argv[1];
    server_path = argv[2];
    if (argc > 3) {
        g_server_port = atoi(argv[3]);
        if (g_server_port <= 0 || g_server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 1;
        }
    }
    
    printf("=== P2P Ping Pair Tests ===\n");
    printf("Ping path:   %s\n", g_ping_path);
    printf("Server path: %s\n", server_path);
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
    
    // 启动 server
    printf("[*] Starting server...\n");
    if (start_server(server_path) != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    
    // 运行测试
    printf("\n[*] Running tests...\n");
    
    test_basic_connection();
    
    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();
    
    // 报告
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
