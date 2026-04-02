/*
 * test_ping_r_reconnect.c - P2P RELAY 模式重连测试集合
 *
 * ============================================================================
 * 测试场景 (RELAY 模式)
 * ============================================================================
 *
 * 1. both_crash:   双端崩溃重连
 *    - 启动 alice/bob，等待连接成功
 *    - SIGKILL 双端（模拟崩溃，不发送 UNREGISTER）
 *    - 重新启动 alice/bob（新 instance_id）
 *    - 验证能否正常重新连接
 *
 * 2. single_crash: 单端崩溃重连 + 双向消息验证
 *    - 启动 alice/bob，等待连接成功，发送消息验证
 *    - 停止 alice（Bob 在线收到 PEER_OFF）
 *    - 重新启动 alice
 *    - 验证重连后双向消息收发正常
 */

#define MOD_TAG "TEST_RECONNECT_R"

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

#define DEFAULT_SERVER_PORT     9436
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define SYNC_TIMEOUT_MS         10000
#define CONNECT_TIMEOUT_MS      30000
#define MESSAGE_TIMEOUT_MS      5000

static const char *g_ping_path = NULL;
static const char *g_server_path = NULL;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static int g_server_port = DEFAULT_SERVER_PORT;
static pid_t g_server_pid = 0;

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

    // 连接成功检测 (RELAY 模式)
    // 日志格式: [STATE] xxx (n) -> yyy (m) 或 [EVENT] State: xxx -> yyy
    if (txt && (strstr(txt, "-> CONNECTED") || 
                strstr(txt, "-> RELAY (") ||
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
    if (rid != 0 && txt && (strstr(txt, ":") || strstr(txt, "send") || strstr(txt, "recv"))) {
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

static int start_server(void) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);

    g_server_pid = fork();
    if (g_server_pid < 0) {
        perror("fork server");
        return -1;
    } else if (g_server_pid == 0) {
        // RELAY 模式: 使用 -r 和 -m 标志
        execl(g_server_path, g_server_path, "-p", port_str, "-r", "-m", NULL);
        perror("exec server");
        _exit(127);
    }

    printf("    Server PID: %d (port=%d, RELAY mode)\n", g_server_pid, g_server_port);
    P_usleep(500 * 1000);
    return 0;
}

static void stop_server(void) {
    if (g_server_pid > 0) {
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
        printf("    Server stopped (exit=%d)\n", WEXITSTATUS(status));
        g_server_pid = 0;
    }
}

static int restart_server(void) {
    printf("    [*] Restarting server...\n");
    stop_server();
    P_usleep(100 * 1000);
    return start_server();
}

static int start_client(client_t *c, const char *target) {
    char server_arg[64];
    snprintf(server_arg, sizeof(server_arg), "%s:%d", g_server_host, g_server_port);

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
        // RELAY 模式: 默认模式（不带 --compact）
        execl(g_ping_path, g_ping_path,
              "-s", server_arg,
              "-n", c->name, "-t", target,
              "--debugger", c->name,
              NULL);
        perror("exec client");
        _exit(127);
    }

    printf("    %s PID: %d\n", c->name, c->pid);
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
        instrument_continue(c->name, c->name);
        P_usleep(100 * 1000);
        printf("    %s synced\n", c->name);
    }
}

// 通过 instrument_req 查询客户端状态
static int get_client_state(client_t *c) {
    char buffer[32] = "";
    ret_t r = instrument_req(c->name, MESSAGE_TIMEOUT_MS, "state", buffer, sizeof(buffer));
    if (r != E_NONE) return -1;
    return atoi(buffer);
}

static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (g_alice.connected && g_bob.connected) {
            // 使用 instrument 查询实际状态
            // P2P_STATE_CONNECTED = 7, P2P_STATE_RELAY = 8
            int alice_state = get_client_state(&g_alice);
            int bob_state = get_client_state(&g_bob);
            if ((alice_state == 7 || alice_state == 8) && 
                (bob_state == 7 || bob_state == 8)) {
                printf("    [CONN] Both ready (alice=%d, bob=%d)\n", 
                       alice_state, bob_state);
                // 额外等待 RELAY 数据通道稳定
                P_usleep(1000 * 1000);
                return 0;
            }
        }
        P_usleep(100 * 1000);
        elapsed += 100;
    }
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
    stop_server();
}

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
// 测试 1: 双端崩溃重连 (RELAY 模式)
//   SIGKILL 双端 → 重启双端 → 验证连接恢复
///////////////////////////////////////////////////////////////////////////////

static void test_relay_both_crash(void) {
    const char *TEST_NAME = "relay_both_crash";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    // 重启 server 以清除旧状态
    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }

    // 1. 第一轮连接
    printf("[1] First connection round...\n");
    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS);
    sync_client(&g_alice);

    if (start_client(&g_bob, "alice") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        kill_client(&g_alice);
        return;
    }
    wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS);
    sync_client(&g_bob);

    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "first connection timeout");
        kill_client(&g_alice);
        kill_client(&g_bob);
        return;
    }
    P_usleep(1000 * 1000);

    // 2. 模拟双端崩溃
    printf("[2] Simulating dual crash (SIGKILL)...\n");
    kill_client(&g_alice);
    kill_client(&g_bob);
    P_usleep(2000 * 1000);

    // 3. 重启双端
    printf("[3] Restarting both clients (new instance_id)...\n");
    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart alice");
        return;
    }
    wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS);
    sync_client(&g_alice);

    if (start_client(&g_bob, "alice") != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart bob");
        kill_client(&g_alice);
        return;
    }
    wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS);
    sync_client(&g_bob);

    // 4. 验证重连
    printf("[4] Waiting for reconnection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "reconnection timeout");
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
// 测试 2: 单端崩溃重连 + 双向消息验证 (RELAY 模式)
//   Alice 停止 → Bob 在线收 PEER_OFF → Alice 重启 → 双向消息
///////////////////////////////////////////////////////////////////////////////

static void test_relay_single_crash(void) {
    const char *TEST_NAME = "relay_single_crash";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    // 重启 server 以清除旧状态
    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }

    // 1. 启动 Alice 和 Bob
    printf("[1] Starting Alice and Bob...\n");
    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS);
    sync_client(&g_alice);

    if (start_client(&g_bob, "alice") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        stop_client(&g_alice);
        return;
    }
    wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS);
    sync_client(&g_bob);

    // 2. 等待首次连接
    printf("[2] Waiting for initial connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "initial connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 3. 发送消息验证首次连接正常
    printf("[3] Verifying initial connection with message...\n");
    if (send_message(&g_alice, "First message") != 0) {
        TEST_FAIL(TEST_NAME, "first message failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(300 * 1000);

    // 4. 停止 Alice（Bob 在线收 PEER_OFF）
    printf("[4] Stopping Alice to simulate disconnect...\n");
    uint16_t old_alice_rid = g_alice.rid;
    stop_client(&g_alice);
    P_usleep(1000 * 1000);
    printf("    Bob should have received PEER_OFF\n");

    // 5. 重启 Alice
    printf("[5] Restarting Alice...\n");
    g_bob.connected = 0;  // Bob 需要重新检测连接

    if (start_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart alice");
        stop_client(&g_bob);
        return;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout after restart");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_alice);
    printf("    Alice new rid=%u (old=%u)\n", g_alice.rid, old_alice_rid);

    // 6. 等待重连
    printf("[6] Waiting for reconnection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "reconnection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(500 * 1000);

    // 7. Alice -> Bob
    printf("[7] Verifying Alice->Bob message...\n");
    if (send_message(&g_alice, "After reconnect") != 0) {
        TEST_FAIL(TEST_NAME, "message after reconnect failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(300 * 1000);

    // 8. Bob -> Alice
    printf("[8] Verifying Bob->Alice message...\n");
    if (send_message(&g_bob, "Bob reply after reconnect") != 0) {
        TEST_FAIL(TEST_NAME, "bob message after reconnect failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    P_usleep(300 * 1000);

    // 9. 验证消息送达
    printf("[9] Verifying message delivery...\n");
    int bob_recv = find_log_from_rid(g_bob.rid, "After reconnect");
    int alice_recv = find_log_from_rid(g_alice.rid, "Bob reply");

    printf("    Bob received 'After reconnect': %s\n", bob_recv >= 0 ? "yes" : "no");
    printf("    Alice received 'Bob reply': %s\n", alice_recv >= 0 ? "yes" : "no");

    if (bob_recv < 0 || alice_recv < 0) {
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
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ping_path> <server_path> [port]\n", argv[0]);
        return 1;
    }

    g_ping_path = argv[1];
    g_server_path = argv[2];
    if (argc > 3) {
        g_server_port = atoi(argv[3]);
        if (g_server_port <= 0 || g_server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 1;
        }
    }

    printf("=== P2P RELAY Reconnection Tests ===\n");
    printf("Ping:   %s\n", g_ping_path);
    printf("Server: %s\n", g_server_path);
    printf("Port:   %d (RELAY mode)\n\n", g_server_port);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // 初始化 instrument
    instrument_local(0);
    if (instrument_listen(on_instrument, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }

    // 启动 server
    printf("[*] Starting server (RELAY mode)...\n");
    if (start_server() != 0) {
        return 1;
    }

    // 运行测试
    printf("\n[*] Running RELAY reconnection tests...\n");

    test_relay_both_crash();
    test_relay_single_crash();

    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();

    // 报告
    printf("\n=== RELAY Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
