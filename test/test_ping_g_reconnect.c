/*
 * test_ping_g_reconnect.c - P2P PUBSUB 模式重连测试集合
 *
 * ============================================================================
 * 测试场景 (PUBSUB 模式)
 * ============================================================================
 *
 * 1. both_crash:   双端崩溃重连
 *    - 启动 Bob(SUB)/Alice(PUB)，等待连接成功
 *    - SIGKILL 双端（模拟崩溃）
 *    - 重新启动 Bob(SUB)/Alice(PUB)（新进程）
 *    - 验证能否正常重新连接
 *
 * 2. pub_crash:    PUB 端崩溃重连 + 双向消息验证
 *    - 启动 Bob(SUB)/Alice(PUB)，等待连接成功，发送消息验证
 *    - 停止 Alice(PUB)（Bob 在线收 NAT FIN）
 *    - Bob(SUB) 自动重连（CLOSED → 新 SUB session → WAIT_OFFER）
 *    - 重新启动 Alice(PUB)（新进程）
 *    - 验证重连后双向消息收发正常
 *
 * 3. sub_crash:    SUB 端崩溃重连 + 双向消息验证
 *    - 启动 Bob(SUB)/Alice(PUB)，等待连接成功，发送消息验证
 *    - 停止 Bob(SUB)（Alice 在线收 NAT FIN）
 *    - Alice(PUB) 自动重连（CLOSED → 新 PUB session → 写 OFFER）
 *    - 重新启动 Bob(SUB)（新进程）
 *    - 验证重连后双向消息收发正常
 *
 * ============================================================================
 * PUBSUB 协议特点
 * ============================================================================
 *
 * - 非对称设计：Alice=PUB（有 target），Bob=SUB（无 target）
 * - 无 server：通过 GitHub Gist 作为信令通道
 * - SUB 端重连：对端断开 → CLOSED → p2p_close + p2p_connect(NULL) → WAIT_OFFER
 * - Gist API 较慢：需要更大超时
 *
 * ============================================================================
 * 环境变量
 * ============================================================================
 *
 *   P2P_TEST_TOKEN  - GitHub Personal Access Token (需要 gist scope)
 *   P2P_TEST_GIST   - GitHub Gist ID
 */

#define MOD_TAG "TEST_RECONNECT_G"

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

#define SYNC_TIMEOUT_MS         15000       // 同步超时（Gist API 较慢）
#define CONNECT_TIMEOUT_MS      90000       // 连接超时（Gist 轮询间隔较大）
#define MESSAGE_TIMEOUT_MS      5000
#define GIST_REGISTER_WAIT_MS   5000        // 等待 SUB 心跳写入 Gist
#define RECONNECT_SETTLE_MS     3000        // 崩溃后等待状态稳定

static const char *g_ping_path = NULL;
static const char *g_token = NULL;
static const char *g_gist_id = NULL;

// 客户端状态
typedef struct {
    const char *name;
    pid_t pid;
    uint16_t rid;
    volatile int waiting;
    volatile int connected;
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
    char txt[512];
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
        strncpy(g_logs[idx].txt, txt, sizeof(g_logs[idx].txt) - 1);
        g_logs[idx].txt[sizeof(g_logs[idx].txt) - 1] = '\0';
    }

    // WAIT 检测
    if (tag == NULL && chn == INSTRUMENT_CTRL && txt) {
        if (strstr(txt, g_alice.name) && g_alice.rid == 0) {
            g_alice.rid = rid;
            g_alice.waiting = 1;
            printf("    [SYNC] Alice WAIT detected (rid=%u)\n", rid);
        } else if (strstr(txt, g_bob.name) && g_bob.rid == 0) {
            g_bob.rid = rid;
            g_bob.waiting = 1;
            printf("    [SYNC] Bob WAIT detected (rid=%u)\n", rid);
        }
    }

    // 连接成功检测
    if (txt && (strstr(txt, "-> CONNECTED") ||
                strstr(txt, "NAT_CONNECTED") ||
                strstr(txt, "bidirectional confirmed"))) {
        if (rid == g_alice.rid && !g_alice.connected) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid && !g_bob.connected) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }

    // 实时显示关键日志
    if (rid != 0 && txt && (strstr(txt, "PUBSUB") || strstr(txt, "OFFER") ||
                strstr(txt, "CONNECTED") || strstr(txt, "CLOSED") ||
                strstr(txt, "reconnect") || strstr(txt, "FIN"))) {
        const char *src = "???";
        if (rid == g_alice.rid) src = "ALICE";
        else if (rid == g_bob.rid) src = "BOB";
        printf("    [LOG] %s: %s\n", src, txt);
    }
}

static void clear_logs(void) {
    g_log_count = 0;
}

static int find_log_from_rid(uint16_t rid, const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (g_logs[i].rid == rid && strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

// 轮询等待 log 出现（可靠层重传可能需要数秒）
static int wait_for_log(uint16_t rid, const char *pattern, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (find_log_from_rid(rid, pattern) >= 0) return 0;
        P_usleep(100 * 1000);
        elapsed += 100;
    }
    return -1;
}

static void print_log_summary(void) {
    printf("\n--- Log Summary ---\n");
    printf("Total logs: %d\n", g_log_count);
    if (g_alice.rid) {
        int n = 0;
        for (int i = 0; i < g_log_count; i++)
            if (g_logs[i].rid == g_alice.rid) n++;
        printf("Alice (rid=%u): %d logs\n", g_alice.rid, n);
    }
    if (g_bob.rid) {
        int n = 0;
        for (int i = 0; i < g_log_count; i++)
            if (g_logs[i].rid == g_bob.rid) n++;
        printf("Bob (rid=%u): %d logs\n", g_bob.rid, n);
    }
}

///////////////////////////////////////////////////////////////////////////////
// 进程管理
///////////////////////////////////////////////////////////////////////////////

// 启动 ping 客户端（PUBSUB 模式）
// target=NULL → SUB 模式（被动等待 offer）
// target="bob" → PUB 模式（主动发起）
static int start_client(client_t *c, const char *target) {
    c->pid = 0;
    c->rid = 0;
    c->waiting = 0;
    c->connected = 0;

    c->pid = fork();
    if (c->pid < 0) {
        perror("fork client");
        return -1;
    } else if (c->pid == 0) {
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        if (target) {
            execl(g_ping_path, g_ping_path,
                  "--github", g_token, "--gist", g_gist_id,
                  "-n", c->name, "-t", target,
                  "--debugger", c->name,
                  NULL);
        } else {
            execl(g_ping_path, g_ping_path,
                  "--github", g_token, "--gist", g_gist_id,
                  "-n", c->name,
                  "--debugger", c->name,
                  NULL);
        }
        perror("exec client");
        _exit(127);
    }

    printf("    %s PID: %d (target=%s, pubsub)\n", c->name, c->pid,
           target ? target : "none/SUB");
    return 0;
}

static int wait_for_waiting(client_t *c, int timeout_ms) {
    int elapsed = 0;
    while (!c->waiting && elapsed < timeout_ms) {
        P_usleep(50 * 1000);
        elapsed += 50;
    }
    return c->waiting ? 0 : -1;
}

static void sync_client(client_t *c) {
    if (c->waiting) {
        printf("    [SYNC] Sending continue to '%s'...\n", c->name);
        instrument_continue(c->name, c->name);
        P_usleep(200 * 1000);
        printf("    %s synced\n", c->name);
    }
}

static int query_state(client_t *c) {
    char buffer[32] = "";
    ret_t r = instrument_req(c->name, 1000, "state", buffer, sizeof(buffer));
    if (r != E_NONE) return -999;
    return atoi(buffer);
}

static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    int poll_interval = 2000;  // Gist 模式轮询间隔较长，避免 instrument 请求竞争
    while (elapsed < timeout_ms) {
        // 每 poll_interval 主动查询双端状态（不依赖回调检测，避免 instrument 丢包）
        if (g_alice.rid && g_bob.rid) {
            int alice_st = query_state(&g_alice);
            int bob_st = query_state(&g_bob);
            if ((alice_st == P2P_STATE_CONNECTED || alice_st == P2P_STATE_RELAY) &&
                (bob_st == P2P_STATE_CONNECTED || bob_st == P2P_STATE_RELAY)) {
                g_alice.connected = 1;
                g_bob.connected = 1;
                printf("    [CONN] Both ready (alice=%d, bob=%d)\n", alice_st, bob_st);
                P_usleep(500 * 1000);
                return 0;
            }
        }
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    int alice_st = g_alice.rid ? query_state(&g_alice) : -999;
    int bob_st = g_bob.rid ? query_state(&g_bob) : -999;
    printf("    [CONN] Timeout! alice_st=%d, bob_st=%d\n", alice_st, bob_st);
    return -1;
}

// 强制 kill（模拟崩溃，SIGKILL 不给进程清理机会）
static void kill_client(client_t *c) {
    if (c->pid > 0) {
        printf("    Killing %s (pid=%d) - simulating crash\n", c->name, c->pid);
        kill(c->pid, SIGKILL);
        int status;
        waitpid(c->pid, &status, 0);
        c->pid = 0;
        c->rid = 0;
        c->waiting = 0;
        c->connected = 0;
    }
}

// 优雅停止（instrument quit + SIGTERM）
static void stop_client(client_t *c) {
    if (c->pid > 0) {
        char buffer[32] = "";
        instrument_req(c->name, MESSAGE_TIMEOUT_MS, "quit", buffer, sizeof(buffer));
        P_usleep(200 * 1000);
        kill(c->pid, SIGTERM);
        int status;
        waitpid(c->pid, &status, 0);
        printf("    %s stopped (exit=%d)\n", c->name, WEXITSTATUS(status));
        c->pid = 0;
        c->rid = 0;
        c->waiting = 0;
        c->connected = 0;
    }
}

// 通过 instrument_req 让客户端发送消息
static int send_message(client_t *c, const char *message) {
    char buffer[512];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    printf("    [MSG] Requesting %s to send: \"%s\"\n", c->name, message);
    ret_t r = instrument_req(c->name, MESSAGE_TIMEOUT_MS, "send", buffer, sizeof(buffer));
    if (r != E_NONE) {
        printf("    [MSG] instrument_req failed: %d\n", r);
        return -1;
    }
    if (strcmp(buffer, "ok") != 0) return -1;
    return 0;
}

static void cleanup(void) {
    kill_client(&g_alice);
    kill_client(&g_bob);
}

static void on_signal(int sig) {
    (void)sig;
    cleanup();
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
// 辅助：完成一次标准 PUBSUB 连接（Bob SUB → Alice PUB）
///////////////////////////////////////////////////////////////////////////////

static int do_connect(const char *label) {
    // 1. Bob (SUB, 无 target)
    printf("[%s] Starting Bob (SUB, no target)...\n", label);
    if (start_client(&g_bob, NULL) != 0) return -1;
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        printf("    bob waiting timeout\n");
        kill_client(&g_bob);
        return -1;
    }
    sync_client(&g_bob);

    // 2. 等待 Bob 心跳写入 Gist
    printf("[%s] Waiting for Bob heartbeat on Gist (%d ms)...\n", label, GIST_REGISTER_WAIT_MS);
    P_usleep(GIST_REGISTER_WAIT_MS * 1000);

    // 3. Alice (PUB, target=bob)
    printf("[%s] Starting Alice (PUB, target=bob)...\n", label);
    if (start_client(&g_alice, "bob") != 0) {
        kill_client(&g_bob);
        return -1;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        printf("    alice waiting timeout\n");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return -1;
    }
    sync_client(&g_alice);

    // 4. 等待连接
    printf("[%s] Waiting for connection...\n", label);
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) return -1;

    return 0;
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
// 测试 1: 双端崩溃重连 (PUBSUB)
//   SIGKILL 双端 → 重启双端 → 验证连接恢复
///////////////////////////////////////////////////////////////////////////////

static void test_both_crash(void) {
    const char *TEST_NAME = "pubsub_both_crash";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    // 1. 第一轮连接
    printf("[1] First connection round...\n");
    if (do_connect("1") != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "first connection failed");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }
    P_usleep(1000 * 1000);

    // 2. 模拟双端崩溃
    printf("[2] Simulating dual crash (SIGKILL)...\n");
    kill_client(&g_alice);
    kill_client(&g_bob);
    int settle = RECONNECT_SETTLE_MS + GIST_REGISTER_WAIT_MS;
    printf("    Waiting %d ms for Gist state to settle...\n", settle);
    P_usleep(settle * 1000);

    // 3. 重启双端（全新进程）
    printf("[3] Restarting both clients...\n");
    clear_logs();
    if (do_connect("3") != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "reconnection failed");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }

    print_log_summary();
    TEST_PASS(TEST_NAME);

    stop_client(&g_alice);
    stop_client(&g_bob);
}

///////////////////////////////////////////////////////////////////////////////
// 测试 2: PUB 端崩溃重连 + 双向消息验证 (PUBSUB)
//   Alice(PUB) 停止 → Bob(SUB) 检测到 CLOSED → 自动重建 SUB session
//   → 重启 Alice(PUB) → 验证重连 + 双向消息
///////////////////////////////////////////////////////////////////////////////

static void test_pub_crash(void) {
    const char *TEST_NAME = "pubsub_pub_crash";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    // 1. 第一轮连接
    printf("[1] Starting Bob (SUB, no target)...\n");
    if (start_client(&g_bob, NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        kill_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    printf("    Waiting for Bob heartbeat on Gist (%d ms)...\n", GIST_REGISTER_WAIT_MS);
    P_usleep(GIST_REGISTER_WAIT_MS * 1000);

    printf("[2] Starting Alice (PUB, target=bob)...\n");
    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        kill_client(&g_bob);
        return;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }
    sync_client(&g_alice);

    printf("[3] Waiting for initial connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "initial connection timeout");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 2. 发送消息验证首次连接正常
    printf("[4] Verifying initial connection with message...\n");
    if (send_message(&g_alice, "First message") != 0) {
        TEST_FAIL(TEST_NAME, "first message failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 3. 停止 Alice（PUB）— Bob 收到 NAT FIN → CLOSED → 自动重建 SUB session
    printf("[5] Stopping Alice (PUB) to simulate disconnect...\n");
    uint16_t old_alice_rid = g_alice.rid;
    stop_client(&g_alice);
    printf("    Bob should receive FIN and auto-reconnect as SUB\n");

    // 等待 Bob 检测到 CLOSED 并重回 WAIT_OFFER + 写入新心跳到 Gist
    printf("[6] Waiting for Bob to re-register heartbeat (%d ms)...\n",
           RECONNECT_SETTLE_MS + GIST_REGISTER_WAIT_MS);
    {
        int total_wait = RECONNECT_SETTLE_MS + GIST_REGISTER_WAIT_MS;
        int elapsed = 0;
        while (elapsed < total_wait) {
            P_usleep(2000 * 1000);
            elapsed += 2000;
            int bob_st = query_state(&g_bob);
            printf("    [DIAG] Bob state after %ds: %d\n", elapsed / 1000, bob_st);
            // 如果 Bob 已进入 SIGNALING 或更高状态，提前结束等待
            if (bob_st == P2P_STATE_SIGNALING || bob_st == P2P_STATE_PUNCHING) break;
        }
    }

    // 4. 重启 Alice（PUB，新进程）
    printf("[7] Restarting Alice (PUB, target=bob)...\n");
    g_bob.connected = 0;  // Bob 需要重新检测连接

    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart alice");
        stop_client(&g_bob);
        return;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout after restart");
        kill_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_alice);
    printf("    Alice new rid=%u (old=%u)\n", g_alice.rid, old_alice_rid);

    // 5. 等待重连
    printf("[8] Waiting for reconnection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "reconnection timeout");
        kill_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 6. Alice -> Bob
    printf("[9] Verifying Alice->Bob message...\n");
    if (send_message(&g_alice, "After reconnect") != 0) {
        TEST_FAIL(TEST_NAME, "message after reconnect failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 7. Bob -> Alice
    printf("[10] Verifying Bob->Alice message...\n");
    if (send_message(&g_bob, "Bob reply after reconnect") != 0) {
        TEST_FAIL(TEST_NAME, "bob message after reconnect failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 8. 等待消息送达（可靠层重传 backoff 可能需要数秒）
    printf("[11] Verifying message delivery...\n");
    int bob_ok = wait_for_log(g_bob.rid, "After reconnect", MESSAGE_TIMEOUT_MS);
    int alice_ok = wait_for_log(g_alice.rid, "Bob reply", MESSAGE_TIMEOUT_MS);

    printf("    Bob received 'After reconnect': %s\n", bob_ok == 0 ? "yes" : "no");
    printf("    Alice received 'Bob reply': %s\n", alice_ok == 0 ? "yes" : "no");

    if (bob_ok < 0 || alice_ok < 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "message not received after reconnect");
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
// 测试 3: SUB 端崩溃重连 + 双向消息验证 (PUBSUB)
//   Bob(SUB) 停止 → Alice(PUB) 检测到 CLOSED → 自动重建 PUB session → 写 OFFER
//   → 重启 Bob(SUB) → 验证重连 + 双向消息
///////////////////////////////////////////////////////////////////////////////

static void test_sub_crash(void) {
    const char *TEST_NAME = "pubsub_sub_crash";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    // 1. 第一轮连接
    printf("[1] Starting Bob (SUB, no target)...\n");
    if (start_client(&g_bob, NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        kill_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    printf("    Waiting for Bob heartbeat on Gist (%d ms)...\n", GIST_REGISTER_WAIT_MS);
    P_usleep(GIST_REGISTER_WAIT_MS * 1000);

    printf("[2] Starting Alice (PUB, target=bob)...\n");
    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        kill_client(&g_bob);
        return;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }
    sync_client(&g_alice);

    printf("[3] Waiting for initial connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "initial connection timeout");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 2. 发送消息验证首次连接
    printf("[4] Verifying initial connection with message...\n");
    if (send_message(&g_alice, "First message") != 0) {
        TEST_FAIL(TEST_NAME, "first message failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 3. 停止 Bob（SUB）— Alice 收到 NAT FIN → CLOSED → 自动重建 PUB session → 写 OFFER
    printf("[5] Stopping Bob (SUB) to simulate disconnect...\n");
    uint16_t old_bob_rid = g_bob.rid;
    stop_client(&g_bob);
    printf("    Alice should receive FIN and auto-reconnect as PUB\n");

    // 等待 Alice 检测到 CLOSED 并重建 PUB session
    printf("[6] Waiting for Alice to settle (%d ms)...\n", RECONNECT_SETTLE_MS);
    P_usleep(RECONNECT_SETTLE_MS * 1000);

    // 4. 重启 Bob（SUB，新进程）
    printf("[7] Restarting Bob (SUB, no target)...\n");
    g_alice.connected = 0;  // Alice 需要重新检测连接

    if (start_client(&g_bob, NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart bob");
        stop_client(&g_alice);
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout after restart");
        kill_client(&g_bob);
        stop_client(&g_alice);
        return;
    }
    sync_client(&g_bob);
    printf("    Bob new rid=%u (old=%u)\n", g_bob.rid, old_bob_rid);

    // 5. 等待重连
    printf("[8] Waiting for reconnection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "reconnection timeout");
        kill_client(&g_bob);
        stop_client(&g_alice);
        return;
    }
    P_usleep(500 * 1000);

    // 6. Alice -> Bob
    printf("[9] Verifying Alice->Bob message...\n");
    if (send_message(&g_alice, "After sub reconnect") != 0) {
        TEST_FAIL(TEST_NAME, "message after reconnect failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 7. Bob -> Alice
    printf("[10] Verifying Bob->Alice message...\n");
    if (send_message(&g_bob, "Bob reply after sub reconnect") != 0) {
        TEST_FAIL(TEST_NAME, "bob message after reconnect failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 8. 等待消息送达（可靠层重传 backoff 可能需要数秒）
    printf("[11] Verifying message delivery...\n");
    int bob_ok = wait_for_log(g_bob.rid, "After sub reconnect", MESSAGE_TIMEOUT_MS);
    int alice_ok = wait_for_log(g_alice.rid, "Bob reply after sub reconnect", MESSAGE_TIMEOUT_MS);

    printf("    Bob received 'After sub reconnect': %s\n", bob_ok == 0 ? "yes" : "no");
    printf("    Alice received 'Bob reply after sub reconnect': %s\n", alice_ok == 0 ? "yes" : "no");

    if (bob_ok < 0 || alice_ok < 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "message not received after reconnect");
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

    printf("=== P2P PUBSUB Reconnection Tests ===\n");
    printf("Ping:   %s\n", g_ping_path);
    printf("Gist:   %s\n", g_gist_id);
    printf("Token:  %s...%s\n",
           (strlen(g_token) > 8) ? "ghp_" : g_token,
           (strlen(g_token) > 8) ? g_token + strlen(g_token) - 4 : "");
    printf("\n");

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // 初始化 instrument
    instrument_local(0);
    if (instrument_listen(on_instrument, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    printf("[*] Instrument listener started\n");

    // 运行测试（PUBSUB 不需要启动 server）
    printf("\n[*] Running PUBSUB reconnection tests...\n");

    test_both_crash();
    test_pub_crash();
    test_sub_crash();

    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();

    // 报告
    printf("\n=== PUBSUB Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
