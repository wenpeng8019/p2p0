/*
 * test_ping_w_connect.c - P2P Ping 双客户端连接测试 (WSS 模式)
 *
 * ============================================================================
 * 测试架构
 * ============================================================================
 * 
 *                      +------------+
 *                      |   Server   |
 *                      |    (wss)   |
 *                      +-----+------+
 *                            |
 *              +-------------+-------------+
 *              |                           |
 *        +-----+-----+               +-----+-----+
 *        |   Alice   |  <------->    |    Bob    |
 *        | (waiting) |   P2P conn    | (connect) |
 *        +-----------+               +-----------+
 *
 * 1. 启动 signaling server（WebSocket 模式）
 * 2. 启动 Alice (WSS 模式, --websock --debugger alice, -t bob)
 * 3. 启动 Bob   (WSS 模式, --websock --debugger bob, -t alice)
 * 4. 通过 instrument 日志观察连接流程
 * 5. 验证连接成功（双方都收到 "P2P connection established"）
 *
 * WSS 模式：--websock，使用 WebSocket 信令
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING_W2"

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

#define DEFAULT_SERVER_PORT     9434
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
    
    // WAIT 包检测
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
    
    // 连接状态检测（兼容旧日志和新版状态机日志）
    if (txt && (strstr(txt, "P2P_STATE_CONNECTED") ||
                strstr(txt, "NAT_CONNECTED") ||
                strstr(txt, "-> CONNECTED") ||
                strstr(txt, "State: PUNCHING -> CONNECTED"))) {
        if (rid == g_alice.rid && !g_alice.connected) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid && !g_bob.connected) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }
    
    // 检测连接成功
    if (txt && (strstr(txt, "P2P connection established") ||
                strstr(txt, "Nomination successful") ||
                strstr(txt, "NAT_CONNECTED") ||
                strstr(txt, "-> CONNECTED") ||
                strstr(txt, "State: PUNCHING -> CONNECTED"))) {
        if (rid == g_alice.rid) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// 进程管理
///////////////////////////////////////////////////////////////////////////////

// 启动 server 子进程（WebSocket 模式）
static int start_server(const char *server_path) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);
    
    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork server: %s\n", strerror(errno));
        return -1;
    } else if (g_server_pid == 0) {
        // WebSocket 模式：使用 --ws 参数
        execl(server_path, server_path, "-p", port_str, "--ws", NULL);
        fprintf(stderr, "Failed to exec server: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    Server PID: %d (WebSocket mode)\n", g_server_pid);
    P_usleep(500 * 1000);  // 等待 server 启动
    return 0;
}

// 启动 ping 客户端（WSS 模式：带 --websock）
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
        // 子进程：WSS 模式（--wss）
        if (target) {
            execl(g_ping_path, g_ping_path, 
                  "-s", server_arg, 
                  "--wss",
                  "-n", client->name, "-t", target,
                  "--debugger", client->name, 
                  "--no-stun-test",
                  NULL);
        } else {
            execl(g_ping_path, g_ping_path, 
                  "-s", server_arg, 
                  "--wss",
                  "-n", client->name,
                  "--debugger", client->name, 
                  "--no-stun-test",
                  NULL);
        }
        fprintf(stderr, "Failed to exec %s: %s\n", client->name, strerror(errno));
        _exit(127);
    }
    
    printf("    %s PID: %d (target=%s, wss mode)\n", client->name, client->pid, target ? target : "none");
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

// 发送 CONTINUE 释放客户端
static int sync_client(ping_client_t *client) {
    if (!client->waiting) {
        fprintf(stderr, "    [SYNC] %s not waiting\n", client->name);
        return -1;
    }
    
    printf("    [SYNC] Sending continue to '%s'...\n", client->name);
    instrument_continue(client->name, client->name);
    
    P_usleep(200 * 1000);
    printf("    [SYNC] %s resumed\n", client->name);
    client->resumed = 1;
    return 0;
}

// 等待连接
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
        printf("    %s stopped\n", client->name);
        client->pid = 0;
    }
}

// 停止 server
static void stop_server(void) {
    if (g_server_pid > 0) {
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
        printf("    Server stopped\n");
        g_server_pid = 0;
    }
}

// 清理
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

// 测试 1: 基本 P2P 连接（WSS 模式）
static void test_basic_connection(void) {
    const char *TEST_NAME = "basic_connection_wss";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    g_log_count = 0;
    
    // 1. 启动 Alice
    printf("[1] Starting Alice (target=bob, wss)...\n");
    if (start_ping_client(&g_alice, "bob") < 0) {
        TEST_FAIL(TEST_NAME, "Failed to start Alice");
        return;
    }
    
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) < 0) {
        TEST_FAIL(TEST_NAME, "Alice did not enter waiting state");
        cleanup();
        return;
    }
    
    // 2. 启动 Bob
    printf("[2] Starting Bob (target=alice, wss)...\n");
    if (start_ping_client(&g_bob, "alice") < 0) {
        TEST_FAIL(TEST_NAME, "Failed to start Bob");
        cleanup();
        return;
    }
    
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) < 0) {
        TEST_FAIL(TEST_NAME, "Bob did not enter waiting state");
        cleanup();
        return;
    }
    
    // 3. 释放 Alice
    printf("[3] Releasing Alice...\n");
    sync_client(&g_alice);
    
    // 4. 释放 Bob
    printf("[4] Releasing Bob...\n");
    sync_client(&g_bob);
    
    // 5. 等待连接建立
    printf("[5] Waiting for P2P connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) < 0) {
        TEST_FAIL(TEST_NAME, "Connection timeout");
        cleanup();
        return;
    }
    
    TEST_PASS(TEST_NAME);
    cleanup();
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <p2p_ping_path> <p2p_server_path> [port]\n", argv[0]);
        printf("Example: %s ./p2p_ping ./p2p_server 9434\n", argv[0]);
        return 1;
    }
    
    g_ping_path = argv[1];
    const char *server_path = argv[2];
    
    if (argc >= 4) {
        g_server_port = atoi(argv[3]);
    }
    
    printf("=== P2P Ping WSS Mode Connection Tests ===\n");
    printf("Ping path:   %s\n", g_ping_path);
    printf("Server path: %s\n", server_path);
    printf("Server port: %d\n", g_server_port);
    
    // 设置信号处理
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    
    // 初始化 instrument
    instrument_listen(on_instrument_log, NULL);
    
    // 启动 server
    printf("\n[0] Starting server (WebSocket mode)...\n");
    if (start_server(server_path) < 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    
    // 运行测试
    test_basic_connection();
    
    // 清理
    cleanup();
    
    // 打印结果
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return (g_tests_failed == 0) ? 0 : 1;
}
