/*
 * test_ping_g_msg.c - P2P Ping 消息互发测试 (PUBSUB 模式)
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 *
 * 1. 验证两个 ping 客户端可以通过 PUBSUB 模式成功建立连接并互发消息
 * 2. 通过 instrument_req 远程控制 ping 发送消息
 * 3. 通过日志验证对端收到消息
 *
 * ============================================================================
 * 消息互发协议 (PUBSUB 模式)
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
 *   [Test]            [Alice/PUB]          [Bob/SUB]
 *     |                  |                    |
 *     |  fork bob (SUB)  |                    |
 *     |  ---------------------------------------->|
 *     |  fork alice (PUB, target=bob)         |
 *     |  ------>         |                    |
 *     |           <--- Gist signaling --->    |
 *     |           <---- P2P Connection ---->  |
 *     |                  |                    |
 *     |  instrument_req("alice", "send", "Hello Bob!")
 *     |  ------REQ------>|                    |
 *     |  <----"ok"-------+----- msg -------->|
 *     |                  |    (via P2P)       |
 *     |                  |                    |
 *     |  instrument_req("bob", "send", "Hi Alice!")
 *     |  ------------------------------------------>|
 *     |  <----------"ok"---+<----- msg -------------|
 *     |                    |    (via P2P)           |
 *     |                  |                    |
 *     |  (验证日志中有 "Hello Bob" 和 "Hi Alice")
 *
 * 环境变量：
 *   P2P_TEST_TOKEN  - GitHub Personal Access Token (需要 gist scope)
 *   P2P_TEST_GIST   - GitHub Gist ID
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING_G_MSG"

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
#include <fcntl.h>
#endif

///////////////////////////////////////////////////////////////////////////////
// 配置
///////////////////////////////////////////////////////////////////////////////

#define SYNC_TIMEOUT_MS         15000       // 同步超时 15 秒（Gist API 较慢）
#define CONNECT_TIMEOUT_MS      90000       // 连接超时 90 秒（Gist 轮询间隔大）
#define MESSAGE_TIMEOUT_MS      5000        // 消息发送/接收超时 5 秒
#define GIST_REGISTER_WAIT_MS   5000        // 等待 Bob 在 Gist 上注册心跳

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
} client_t;

static client_t g_alice = { .name = "alice" };
static client_t g_bob   = { .name = "bob" };

// 测试结果
static int g_tests_passed = 0;
static int g_tests_failed = 0;

// instrument 日志收集
#define MAX_LOG_ENTRIES 5000
static struct {
    uint16_t rid;
    uint8_t chn;
    char tag[32];
    char txt[512];
    int64_t ts;
} g_logs[MAX_LOG_ENTRIES];
static volatile int g_log_count = 0;

///////////////////////////////////////////////////////////////////////////////
// Instrument 回调
///////////////////////////////////////////////////////////////////////////////

static void on_instrument(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len) {
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
    if (txt && (strstr(txt, "-> CONNECTED") ||
                strstr(txt, "State: PUNCHING -> CONNECTED"))) {
        if (rid == g_alice.rid && !g_alice.connected) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid && !g_bob.connected) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }
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

// 轮询等待日志出现
static int wait_for_log(uint16_t rid, const char *pattern, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (find_log_from_rid(rid, pattern) >= 0) return 0;
        P_usleep(100 * 1000);
        elapsed += 100;
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
static int start_client(client_t *client, const char *target) {
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
        // 子进程：stdin 重定向到 /dev/null（非交互模式）
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }

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

    printf("    %s PID: %d (target=%s, pubsub)\n",
           client->name, client->pid, target ? target : "none/SUB");
    return 0;
}

// 等待客户端进入 waiting 状态
static int wait_for_waiting(client_t *client, int timeout_ms) {
    int elapsed = 0;
    const int poll_interval = 50;

    while (!client->waiting && elapsed < timeout_ms) {
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    return client->waiting ? 0 : -1;
}

// 发送 CONTINUE 释放客户端
static int sync_client(client_t *client) {
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

// 查询客户端状态
static int query_state(client_t *c) {
    char buffer[32] = "";
    ret_t r = instrument_req(c->name, 1000, "state", buffer, sizeof(buffer));
    if (r != E_NONE) return -999;
    return atoi(buffer);
}

// 等待两个客户端都连接成功（轮询 instrument state）
static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    int poll_interval = 2000;

    printf("    [CONN] Waiting for P2P connection (PUBSUB)...\n");

    while (elapsed < timeout_ms) {
        int alice_st = query_state(&g_alice);
        int bob_st = query_state(&g_bob);

        if (alice_st == 7 && bob_st == 7) {
            g_alice.connected = 1;
            g_bob.connected = 1;
            printf("    [CONN] Both ready (alice=%d, bob=%d)\n", alice_st, bob_st);
            return 0;
        }

        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }

    printf("    [CONN] Timeout! alice=%d, bob=%d\n",
           query_state(&g_alice), query_state(&g_bob));
    return -1;
}

// 通过 instrument_req 让客户端发送消息
static int send_message(client_t *client, const char *message) {
    char buffer[512];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    printf("    [MSG] Requesting %s to send: \"%s\"\n", client->name, message);

    ret_t r = instrument_req(client->name, MESSAGE_TIMEOUT_MS, "send", buffer, sizeof(buffer));
    if (r != E_NONE) {
        printf("    [MSG] instrument_req failed: %d\n", r);
        return -1;
    }

    printf("    [MSG] Response from %s: \"%s\"\n", client->name, buffer);
    return strcmp(buffer, "ok") == 0 ? 0 : -1;
}

// 停止客户端（先 quit 再 SIGTERM）
static void stop_client(client_t *client) {
    if (client->pid > 0) {
        char buffer[32] = "";
        instrument_req(client->name, 1000, "quit", buffer, sizeof(buffer));
        P_usleep(200 * 1000);

        kill(client->pid, SIGTERM);
        int status;
        waitpid(client->pid, &status, 0);
        printf("    %s stopped (exit=%d)\n", client->name, WEXITSTATUS(status));
        client->pid = 0;
        client->rid = 0;
        client->waiting = 0;
        client->connected = 0;
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

// 测试 1: 消息互发 (PUBSUB 模式, PUB=Alice, SUB=Bob)
static void test_message_exchange(void) {
    const char *TEST_NAME = "pubsub_message_exchange";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    // 1. 启动 Bob (SUB 模式, 无 -t, 被动等待 offer)
    printf("[1] Starting Bob (SUB, no target, pubsub)...\n");
    if (start_client(&g_bob, NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }

    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }

    // 2. 同步 Bob → 写入心跳到 Gist
    printf("[2] Syncing Bob...\n");
    if (sync_client(&g_bob) != 0) {
        TEST_FAIL(TEST_NAME, "bob sync failed");
        stop_client(&g_bob);
        return;
    }

    // 3. 等待 Bob 在 Gist 上注册心跳
    printf("[3] Waiting for Bob to register heartbeat (%d ms)...\n", GIST_REGISTER_WAIT_MS);
    P_usleep(GIST_REGISTER_WAIT_MS * 1000);

    // 4. 启动 Alice (PUB 模式, -t bob, 主动发起)
    printf("[4] Starting Alice (PUB, target=bob, pubsub)...\n");
    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        stop_client(&g_bob);
        return;
    }

    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 5. 同步 Alice
    printf("[5] Syncing Alice...\n");
    if (sync_client(&g_alice) != 0) {
        TEST_FAIL(TEST_NAME, "alice sync failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 6. 等待连接成功
    printf("[6] Waiting for P2P connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 7. Alice 发送消息给 Bob
    printf("[7] Alice sends message to Bob...\n");
    if (send_message(&g_alice, "Hello Bob via PUBSUB!") != 0) {
        TEST_FAIL(TEST_NAME, "alice failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 8. Bob 发送消息给 Alice
    printf("[8] Bob sends message to Alice...\n");
    if (send_message(&g_bob, "Hi Alice via PUBSUB!") != 0) {
        TEST_FAIL(TEST_NAME, "bob failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 9. 等待消息送达（可靠层重传 backoff 可能需要数秒）
    printf("[9] Verifying message delivery...\n");
    int bob_ok = wait_for_log(g_bob.rid, "Hello Bob", MESSAGE_TIMEOUT_MS);
    int alice_ok = wait_for_log(g_alice.rid, "Hi Alice", MESSAGE_TIMEOUT_MS);

    printf("    Bob received 'Hello Bob': %s\n", bob_ok == 0 ? "yes" : "no");
    printf("    Alice received 'Hi Alice': %s\n", alice_ok == 0 ? "yes" : "no");

    if (bob_ok < 0 || alice_ok < 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "message not received");
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
    setvbuf(stdout, NULL, _IONBF, 0);

    // 解析命令行参数
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ping_path> [token] [gist_id]\n", argv[0]);
        fprintf(stderr, "\nEnvironment variables (fallback):\n");
        fprintf(stderr, "  P2P_TEST_TOKEN  - GitHub Personal Access Token\n");
        fprintf(stderr, "  P2P_TEST_GIST   - GitHub Gist ID\n");
        return 1;
    }
    g_ping_path = argv[1];

    g_token = (argc > 2) ? argv[2] : getenv("P2P_TEST_TOKEN");
    g_gist_id = (argc > 3) ? argv[3] : getenv("P2P_TEST_GIST");

    if (!g_token || !g_gist_id) {
        fprintf(stderr, "Error: GitHub token and Gist ID are required.\n");
        fprintf(stderr, "Set P2P_TEST_TOKEN and P2P_TEST_GIST environment variables,\n");
        fprintf(stderr, "or pass them as command line arguments.\n");
        return 1;
    }

    printf("=== P2P Ping Message Tests (PUBSUB Mode) ===\n");
    printf("Ping path: %s\n", g_ping_path);
    printf("Gist ID:   %s\n", g_gist_id);
    printf("\n");

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // 初始化 instrument 监听
    instrument_local(0);
    if (instrument_listen(on_instrument, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    printf("[*] Instrument listener started\n");

    // 运行测试（PUBSUB 不需要启动 server）
    printf("\n[*] Running PUBSUB message tests...\n");

    test_message_exchange();

    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();

    // 报告
    printf("\n=== PUBSUB Message Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
