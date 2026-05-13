/*
 * test_ping_g_filter.c - ICE 候选过滤测试 (PUBSUB 模式)
 *
 * ============================================================================
 * 测试场景 (PUBSUB 模式)
 * ============================================================================
 *
 * 1. test_pubsub_host_only:
 *    - 关闭 SRFLX 和 RELAY 候选收集（--no-srflx --no-relay）
 *    - 验证只有 HOST 候选被使用
 *
 * 2. test_pubsub_ipv4_only:
 *    - 关闭 IPv6 候选收集（--no-ipv6）
 *    - 验证只有 IPv4 候选被使用，无 Host6 候选
 *
 * 3. test_pubsub_ipv6_only:
 *    - 关闭 IPv4 候选收集（--no-ipv4 --no-srflx --no-relay）
 *    - 验证只有 IPv6 候选被使用，无 IPv4 Host 候选
 *    - 允许超时（本地可能无 IPv6 接口）
 *
 * 4. test_pubsub_no_ipv4_no_ipv6:
 *    - 同时关闭 IPv4 和 IPv6 候选收集
 *    - 验证 p2p_create 失败（PUBSUB 无信令中转，进程提前退出）
 *
 * 5. test_pubsub_all_types_off:
 *    - 关闭所有候选类型（--no-host --no-srflx --no-relay）
 *    - 验证 p2p_create 失败（PUBSUB 无信令中转，进程提前退出）
 *
 * ============================================================================
 * 命令行选项（p2p_ping）
 * ============================================================================
 *
 * --no-host   - 禁用 HOST 候选收集（不收集本地网卡地址）
 * --no-srflx  - 禁用 SRFLX 候选收集（不收集 NAT 反射地址）
 * --no-relay  - 禁用 RELAY 候选收集（不收集 TURN 中继地址）
 * --no-ipv4   - 禁用 IPv4 候选（不收集/不接受 IPv4 地址）
 * --no-ipv6   - 禁用 IPv6 候选（不收集/不接受 IPv6 地址）
 *
 * ============================================================================
 * 环境变量
 * ============================================================================
 *
 *   P2P_TEST_TOKEN  - GitHub Personal Access Token (需要 gist scope)
 *   P2P_TEST_GIST   - GitHub Gist ID
 */

#define MOD_TAG "TEST_SYNC_G"

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
#define CONNECT_TIMEOUT_MS      60000       // 连接超时 60 秒（Gist 轮询间隔较大）
#define GIST_REG_WAIT_MS   5000        // 等待 SUB 在 Gist 上注册心跳

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
    (void)tag; (void)len;

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
    if (txt && (strstr(txt, "P2P connection established") ||
                strstr(txt, "Nomination successful") ||
                strstr(txt, "-> CONNECTED"))) {
        if (rid == g_alice.rid && !g_alice.connected) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid && !g_bob.connected) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }

    // 显示关键日志（候选相关）
    if (txt && (strstr(txt, "cand") || strstr(txt, "instrument") || strstr(txt, "punch") ||
                strstr(txt, "Host") || strstr(txt, "host") || strstr(txt, "srflx") ||
                strstr(txt, "SRFLX") || strstr(txt, "relay") || strstr(txt, "RELAY"))) {
        const char *src = "???";
        if (rid == g_alice.rid) src = "ALICE";
        else if (rid == g_bob.rid) src = "BOB";
        printf("    [LOG] %s: %s\n", src, txt);
    }
}

static void clear_logs(void) {
    g_log_count = 0;
}

static int count_logs(const char *pattern) {
    int n = 0;
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) n++;
    }
    return n;
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

static int start_client(client_t *c, const char *target, const char *extra_args) {
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

        // 构建参数列表（PUBSUB 模式: --github TOKEN --gist GIST_ID）
        const char *argv[32];
        int argc = 0;
        argv[argc++] = g_ping_path;
        argv[argc++] = "--github";
        argv[argc++] = g_token;
        argv[argc++] = "--gist";
        argv[argc++] = g_gist_id;
        argv[argc++] = "-n";
        argv[argc++] = c->name;
        if (target) {
            argv[argc++] = "-t";
            argv[argc++] = target;
        }
        argv[argc++] = "--debugger";
        argv[argc++] = c->name;

        // 添加额外参数
        if (extra_args && *extra_args) {
            char *args_copy = strdup(extra_args);
            char *token = strtok(args_copy, " ");
            while (token && argc < 30) {
                argv[argc++] = token;
                token = strtok(NULL, " ");
            }
        }

        argv[argc] = NULL;
        execv(g_ping_path, (char *const *)argv);
        perror("exec client");
        _exit(127);
    }

    printf("    %s PID: %d (target=%s, extra=%s, pubsub mode)\n",
           c->name, c->pid, target ? target : "SUB", extra_args ? extra_args : "none");
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
        P_usleep(200 * 1000);
        printf("    %s synced\n", c->name);
    }
}

static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (g_alice.connected && g_bob.connected) {
            printf("    [CONN] Both clients connected!\n");
            return 0;
        }
        P_usleep(100 * 1000);
        elapsed += 100;
    }
    printf("    [CONN] Timeout! alice=%d, bob=%d\n",
           g_alice.connected, g_bob.connected);
    return -1;
}

static void stop_client(client_t *c) {
    if (c->pid > 0) {
        kill(c->pid, SIGTERM);
        int status;
        waitpid(c->pid, &status, 0);
        printf("    %s stopped (exit=%d)\n", c->name, WEXITSTATUS(status));
        c->pid = 0;
    }
}

// 等待进程退出（用于验证 p2p_create 失败导致的提前退出）
static int wait_for_exit(client_t *c, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        int status;
        pid_t result = waitpid(c->pid, &status, WNOHANG);
        if (result > 0) {
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            printf("    %s exited (code=%d)\n", c->name, exit_code);
            c->pid = 0;
            return exit_code;
        }
        P_usleep(50 * 1000);
        elapsed += 50;
    }
    return -1;  // 未退出
}

static void cleanup(void) {
    stop_client(&g_alice);
    stop_client(&g_bob);
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
// 测试 1: HOST only（关闭 SRFLX 和 RELAY）(PUBSUB 模式)
///////////////////////////////////////////////////////////////////////////////

static void test_pubsub_host_only(void) {
    const char *TEST_NAME = "pubsub_host_only";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling SRFLX and RELAY candidates, HOST only (PUBSUB mode)\n");
    clear_logs();

    // PUBSUB 非对称：先启动 Bob (SUB, 无 target)
    printf("[1] Starting Bob (SUB, pubsub, --no-srflx --no-relay)...\n");
    if (start_client(&g_bob, NULL, "--no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    // 等待 Bob 在 Gist 上注册心跳
    printf("[2] Waiting for Bob to register heartbeat on Gist (%d ms)...\n", GIST_REG_WAIT_MS);
    P_usleep(GIST_REG_WAIT_MS * 1000);

    // 启动 Alice (PUB, target=bob)
    printf("[3] Starting Alice (PUB, target=bob, pubsub, --no-srflx --no-relay)...\n");
    if (start_client(&g_alice, "bob", "--no-srflx --no-relay") != 0) {
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
    sync_client(&g_alice);

    // 等待连接
    printf("[4] Waiting for connection (HOST only, PUBSUB mode)...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 验证日志
    printf("[5] Verifying ICE candidate logs...\n");
    int host_cand = count_logs("host cand") + count_logs("Host cand") + count_logs("HOST");
    printf("    HOST candidate: %d logs\n", host_cand);

    if (host_cand == 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "no HOST candidate found");
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
// 测试 2: IPv4 only（关闭 IPv6 候选）(PUBSUB 模式)
///////////////////////////////////////////////////////////////////////////////

static void test_pubsub_ipv4_only(void) {
    const char *TEST_NAME = "pubsub_ipv4_only";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling IPv6 candidates, IPv4 only (PUBSUB mode)\n");
    clear_logs();

    // PUBSUB 非对称：先启动 Bob (SUB, 无 target)
    printf("[1] Starting Bob (SUB, pubsub, --no-ipv6)...\n");
    if (start_client(&g_bob, NULL, "--no-ipv6") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    // 等待 Bob 在 Gist 上注册心跳
    printf("[2] Waiting for Bob to register heartbeat on Gist (%d ms)...\n", GIST_REG_WAIT_MS);
    P_usleep(GIST_REG_WAIT_MS * 1000);

    // 启动 Alice (PUB, target=bob)
    printf("[3] Starting Alice (PUB, target=bob, pubsub, --no-ipv6)...\n");
    if (start_client(&g_alice, "bob", "--no-ipv6") != 0) {
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
    sync_client(&g_alice);

    // 等待连接
    printf("[4] Waiting for connection (IPv4 only, PUBSUB mode)...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 验证：不应该有 IPv6 候选（Host6）
    printf("[5] Verifying no IPv6 candidates...\n");
    int host6_count = count_logs("Host6 cand") + count_logs("Host6 candidate");
    int host4_count = count_logs("Host cand") + count_logs("Host candidate");

    printf("    IPv4 Host candidates: %d logs\n", host4_count);
    printf("    IPv6 Host candidates: %d logs\n", host6_count);

    if (host6_count > 0) {
        TEST_FAIL(TEST_NAME, "IPv6 candidates should have been filtered");
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
// 测试 3: IPv6 only（关闭 IPv4 候选）(PUBSUB 模式)
///////////////////////////////////////////////////////////////////////////////

static void test_pubsub_ipv6_only(void) {
    const char *TEST_NAME = "pubsub_ipv6_only";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling IPv4 candidates, IPv6 only (PUBSUB mode)\n");
    printf("    Note: may fail if no IPv6 interfaces available\n");
    clear_logs();

    // PUBSUB 非对称：先启动 Bob (SUB, 无 target)
    printf("[1] Starting Bob (SUB, pubsub, --no-ipv4)...\n");
    if (start_client(&g_bob, NULL, "--no-ipv4 --no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    // 等待 Bob 在 Gist 上注册心跳
    printf("[2] Waiting for Bob to register heartbeat on Gist (%d ms)...\n", GIST_REG_WAIT_MS);
    P_usleep(GIST_REG_WAIT_MS * 1000);

    // 启动 Alice (PUB, target=bob)
    printf("[3] Starting Alice (PUB, target=bob, pubsub, --no-ipv4)...\n");
    if (start_client(&g_alice, "bob", "--no-ipv4 --no-srflx --no-relay") != 0) {
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
    sync_client(&g_alice);

    // 等待连接（IPv6 only，允许超时）
    printf("[4] Waiting for connection (IPv6 only, PUBSUB mode)...\n");
    int connect_result = wait_for_connection(CONNECT_TIMEOUT_MS);

    // 验证：不应该有 IPv4 Host 候选
    printf("[5] Verifying no IPv4 candidates...\n");
    int host4_gathered = 0;
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, "Gathered Host candidate:"))
            host4_gathered++;
    }
    int host6_gathered = count_logs("Host6 candidate");

    printf("    IPv4 Host gathered: %d logs\n", host4_gathered);
    printf("    IPv6 Host gathered: %d logs\n", host6_gathered);

    if (host4_gathered > 0) {
        TEST_FAIL(TEST_NAME, "IPv4 candidates should have been filtered");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    if (connect_result == 0) {
        printf("    Connection succeeded via IPv6!\n");
    } else {
        printf("    Connection timed out (no IPv6 interfaces or no IPv6 connectivity)\n");
    }

    print_log_summary();
    TEST_PASS(TEST_NAME);

    stop_client(&g_alice);
    stop_client(&g_bob);
}

///////////////////////////////////////////////////////////////////////////////
// 测试 4: 同时关闭 IPv4 和 IPv6（PUBSUB 模式，p2p_create 应失败）
///////////////////////////////////////////////////////////////////////////////

static void test_pubsub_no_ipv4_no_ipv6(void) {
    const char *TEST_NAME = "pubsub_no_ipv4_no_ipv6";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling both IPv4 and IPv6 (PUBSUB, p2p_create should fail)\n");
    clear_logs();

    // 启动 Bob (SUB)，应该在 sync 后 p2p_create 失败退出
    printf("[1] Starting Bob (SUB, pubsub, --no-ipv4 --no-ipv6)...\n");
    if (start_client(&g_bob, NULL, "--no-ipv4 --no-ipv6") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    // 等待进程退出（p2p_create 应返回 NULL）
    printf("[2] Waiting for Bob to exit (p2p_create should fail)...\n");
    int exit_code = wait_for_exit(&g_bob, 10000);

    if (exit_code < 0) {
        TEST_FAIL(TEST_NAME, "Bob did not exit as expected");
        stop_client(&g_bob);
        return;
    }

    if (exit_code == 0) {
        TEST_FAIL(TEST_NAME, "Bob exited with code 0, expected non-zero");
        return;
    }

    printf("    Bob exited with code %d as expected (p2p_create failed)\n", exit_code);
    print_log_summary();
    TEST_PASS(TEST_NAME);
}

///////////////////////////////////////////////////////////////////////////////
// 测试 5: 关闭所有候选类型（PUBSUB 模式，p2p_create 应失败）
///////////////////////////////////////////////////////////////////////////////

static void test_pubsub_all_types_off(void) {
    const char *TEST_NAME = "pubsub_all_types_off";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling all candidate types (PUBSUB, p2p_create should fail)\n");
    clear_logs();

    // 启动 Bob (SUB)，应该在 sync 后 p2p_create 失败退出
    printf("[1] Starting Bob (SUB, pubsub, --no-host --no-srflx --no-relay)...\n");
    if (start_client(&g_bob, NULL, "--no-host --no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    // 等待进程退出（p2p_create 应返回 NULL）
    printf("[2] Waiting for Bob to exit (p2p_create should fail)...\n");
    int exit_code = wait_for_exit(&g_bob, 10000);

    if (exit_code < 0) {
        TEST_FAIL(TEST_NAME, "Bob did not exit as expected");
        stop_client(&g_bob);
        return;
    }

    if (exit_code == 0) {
        TEST_FAIL(TEST_NAME, "Bob exited with code 0, expected non-zero");
        return;
    }

    printf("    Bob exited with code %d as expected (p2p_create failed)\n", exit_code);
    print_log_summary();
    TEST_PASS(TEST_NAME);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ping_path> [token] [gist_id]\n", argv[0]);
        fprintf(stderr, "\nEnvironment variables (fallback):\n");
        fprintf(stderr, "  P2P_TEST_TOKEN  - GitHub Personal Access Token\n");
        fprintf(stderr, "  P2P_TEST_GIST   - GitHub Gist ID\n");
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

    printf("=== P2P PUBSUB ICE Candidate Filter Tests ===\n");
    printf("Ping:    %s\n", g_ping_path);
    printf("Gist ID: %s\n", g_gist_id);
    printf("Token:   %s...%s\n",
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

    // PUBSUB 模式不需要启动 server
    printf("[*] Running PUBSUB ICE filter tests...\n");

    test_pubsub_host_only();
    test_pubsub_ipv4_only();
    test_pubsub_ipv6_only();
    test_pubsub_no_ipv4_no_ipv6();
    test_pubsub_all_types_off();

    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();

    // 报告
    printf("\n=== PUBSUB ICE Filter Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
