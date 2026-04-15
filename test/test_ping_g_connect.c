/*
 * test_ping_g_connect.c - P2P Ping 双客户端连接测试 (PUBSUB 模式)
 *
 * ============================================================================
 * 测试架构
 * ============================================================================
 * 
 *                    +----------------+
 *                    |  GitHub Gist   |
 *                    |  (signaling)   |
 *                    +-------+--------+
 *                            |
 *              +-------------+-------------+
 *              |                           |
 *        +-----+-----+               +-----+-----+
 *        |   Alice   |  <------->    |    Bob    |
 *        |   (PUB)   |   P2P conn    |   (SUB)   |
 *        +-----------+               +-----------+
 *
 * 1. 无需启动 server（使用 GitHub Gist 作为信令通道）
 * 2. 启动 Bob   (PUBSUB SUB 模式, --debugger bob, 无 -t，等待 offer)
 * 3. 启动 Alice (PUBSUB PUB 模式, --debugger alice, -t bob)
 * 4. 通过 instrument 日志观察连接流程
 * 5. 验证连接成功（双方都收到 "P2P connection established"）
 *
 * PUBSUB 协议：非对称设计
 *   PUB (Alice): 指定 -t bob，主动发起，写 offer 到 Bob 的 Gist 文件
 *   SUB (Bob):   不指定 -t，被动等待，轮询自己的 Gist 文件检测 offer
 *
 * 环境变量：
 *   P2P_TEST_TOKEN  - GitHub Personal Access Token (需要 gist scope)
 *   P2P_TEST_GIST   - GitHub Gist ID
 *
 * ============================================================================
 * 同步协议
 * ============================================================================
 *
 *   [Test]            [Bob/SUB]          [Alice/PUB]
 *     |
 *     |  fork bob (SUB, no target)
 *     |  ------>         |
 *     |           instrument_wait("bob", "bob", 60s)
 *     |  <-- WAIT ---    |
 *     |  continue bob    |
 *     |  --- CONTINUE -->|
 *     |           "Debugger connected"
 *     |           (heartbeat written, waiting for offer)
 *     |
 *     |  (wait for Bob to register heartbeat on Gist)
 *     |
 *     |  fork alice (PUB, target=bob)     |
 *     |  --------------------------------->|
 *     |                  | instrument_wait("alice", "alice", 60s)
 *     |  <-- WAIT ---------------------------|
 *     |  continue alice  |                  |
 *     |  --- CONTINUE ---------------------->|
 *     |                  |   "Debugger connected"
 *     |                  |   (reads SUB heartbeat, writes offer to Bob's file)
 *     |                  |
 *     |           <---- P2P Connection ---->|
 *     |                  |
 *     |    logs...  "P2P connection established"
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING_P2"

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

#define SYNC_TIMEOUT_MS         15000       // 同步超时 15 秒（Gist API 较慢）
#define CONNECT_TIMEOUT_MS      60000       // 连接超时 60 秒（Gist 轮询间隔较大）
#define GIST_REGISTER_WAIT_MS   5000        // 等待 Alice 在 Gist 上注册

// 测试状态
static const char *g_ping_path = NULL;
static const char *g_token = NULL;
static const char *g_gist_id = NULL;

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
    
    // 实时显示日志（调试时启用）
    #if 0
    if (rid != 0) {
        const char* color;
        const char* src = "???";
        if (rid == g_alice.rid) src = "ALICE";
        else if (rid == g_bob.rid) src = "BOB";
        else {
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

// 启动 ping 客户端（PUBSUB 模式）
static int start_ping_client(ping_client_t *client, const char *target) {
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
        // 子进程：PUBSUB 模式（--github TOKEN --gist GIST_ID）
        if (target) {
            execl(g_ping_path, g_ping_path, 
                  "--github", g_token, "--gist", g_gist_id,
                  "-n", client->name, "-t", target,
                  "--debugger", client->name, 
                  NULL);
        } else {
            execl(g_ping_path, g_ping_path, 
                  "--github", g_token, "--gist", g_gist_id,
                  "-n", client->name,
                  "--debugger", client->name, 
                  NULL);
        }
        fprintf(stderr, "Failed to exec %s: %s\n", client->name, strerror(errno));
        _exit(127);
    }
    
    printf("    %s PID: %d (target=%s, pubsub mode)\n", client->name, client->pid, target ? target : "none");
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
    
    P_usleep(200 * 1000);
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

// 清理所有进程
static void cleanup(void) {
    stop_client(&g_alice);
    stop_client(&g_bob);
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

// 测试 1: 基本 P2P 连接（Alice PUB <-> Bob SUB，PUBSUB 模式，同一 Gist）
static void test_basic_connection(void) {
    const char *TEST_NAME = "basic_connection_pubsub";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 1. 启动 Bob (SUB 模式, 无 -t，被动等待 offer)
    printf("[1] Starting Bob (SUB, no target, pubsub)...\n");
    if (start_ping_client(&g_bob, NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    
    // 等待 Bob 进入 waiting 状态
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }
    
    // 2. 同步 Bob
    printf("[2] Syncing Bob...\n");
    if (sync_client(&g_bob) != 0) {
        TEST_FAIL(TEST_NAME, "bob sync failed");
        stop_client(&g_bob);
        return;
    }
    
    // 3. 等待 Bob 在 Gist 上写入心跳（SUB 注册）
    printf("[3] Waiting for Bob to register heartbeat on Gist (%d ms)...\n", GIST_REGISTER_WAIT_MS);
    P_usleep(GIST_REGISTER_WAIT_MS * 1000);
    
    // 4. 启动 Alice (PUB 模式, -t bob，主动发起)
    printf("[4] Starting Alice (PUB, target=bob, pubsub)...\n");
    if (start_ping_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        stop_client(&g_bob);
        return;
    }
    
    // 等待 Alice 进入 waiting 状态
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 5. 同步 Alice
    printf("[5] Syncing Alice...\n");
    if (sync_client(&g_alice) != 0) {
        TEST_FAIL(TEST_NAME, "alicece sync failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 6. 等待连接成功
    printf("[6] Waiting for P2P connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        
        int alice_cand = find_log_from_rid(g_alice.rid, "remote");
        int bob_cand = find_log_from_rid(g_bob.rid, "remote");
        printf("Alice received remote cand: %s\n", alice_cand >= 0 ? "yes" : "no");
        printf("Bob received remote cand: %s\n", bob_cand >= 0 ? "yes" : "no");
        
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    print_log_summary();
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    // 解析命令行参数
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ping_path> [token] [gist_id]\n", argv[0]);
        fprintf(stderr, "\nEnvironment variables (fallback):\n");
        fprintf(stderr, "  P2P_TEST_TOKEN  - GitHub Personal Access Token\n");
        fprintf(stderr, "  P2P_TEST_GIST   - GitHub Gist ID\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  P2P_TEST_TOKEN=ghp_xxx P2P_TEST_GIST=abc123 %s ./p2p_ping\n", argv[0]);
        fprintf(stderr, "  %s ./p2p_ping ghp_xxx abc123\n", argv[0]);
        return 1;
    }
    g_ping_path = argv[1];
    
    // Token: 命令行参数 > 环境变量
    g_token = (argc > 2) ? argv[2] : getenv("P2P_TEST_TOKEN");
    g_gist_id = (argc > 3) ? argv[3] : getenv("P2P_TEST_GIST");
    
    if (!g_token || !g_gist_id) {
        fprintf(stderr, "Error: GitHub token and Gist ID are required.\n");
        fprintf(stderr, "Set P2P_TEST_TOKEN and P2P_TEST_GIST environment variables,\n");
        fprintf(stderr, "or pass them as command line arguments.\n");
        return 1;
    }
    
    printf("=== P2P Ping Pair Tests (PUBSUB Mode) ===\n");
    printf("Ping path: %s\n", g_ping_path);
    printf("Gist ID:   %s\n", g_gist_id);
    printf("Token:     %s...%s\n", 
           (strlen(g_token) > 8) ? "ghp_" : g_token,
           (strlen(g_token) > 8) ? g_token + strlen(g_token) - 4 : "");
    printf("\n");
    
    // 注册信号处理
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    
    // 初始化 instrument 监听
    instrument_local(0);
    if (instrument_listen(on_instrument_log, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    printf("[*] Instrument listener started\n");
    
    // 运行测试（PUBSUB 不需要启动 server）
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
