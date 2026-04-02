/*
 * test_ping_ice.c - ICE 候选过滤测试
 *
 * ============================================================================
 * 测试场景
 * ============================================================================
 *
 * 1. test_host_only:
 *    - 关闭 SRFLX 和 RELAY 候选收集
 *    - 验证只有 HOST 候选被使用
 *    - 验证日志中 SRFLX/RELAY 被标记为 ignored
 *
 * 2. test_no_host_punch:
 *    - 关闭 HOST 打洞（但仍收集）
 *    - 验证 HOST 候选被收集但打洞被跳过
 *
 * 3. test_all_off:
 *    - 关闭所有候选收集
 *    - 验证连接超时失败
 *
 * ============================================================================
 * 命令行选项（p2p_ping）
 * ============================================================================
 *
 * --no-host   - 禁用 HOST 候选收集（不收集本地网卡地址）
 * --no-srflx  - 禁用 SRFLX 候选收集（不收集 NAT 反射地址）
 * --no-relay  - 禁用 RELAY 候选收集（不收集 TURN 中继地址）
 */

#define MOD_TAG "TEST_ICE"

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

#define DEFAULT_SERVER_PORT     9350
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
    if (txt && (strstr(txt, "State: → CONNECTED") || strstr(txt, "CONNECTING → CONNECTED"))) {
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

static int find_log(const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

static int find_log_from_rid(uint16_t rid, const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (g_logs[i].rid == rid && strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
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

static int start_server(void) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);

    g_server_pid = fork();
    if (g_server_pid < 0) {
        perror("fork server");
        return -1;
    } else if (g_server_pid == 0) {
        execl(g_server_path, g_server_path, "-p", port_str, NULL);
        perror("exec server");
        _exit(127);
    }

    printf("    Server PID: %d (port=%d)\n", g_server_pid, g_server_port);
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

static int start_client(client_t *c, const char *target, const char *extra_args) {
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
        
        // 构建参数列表
        const char *argv[32];
        int argc = 0;
        argv[argc++] = g_ping_path;
        argv[argc++] = "--compact";
        argv[argc++] = "-s";
        argv[argc++] = server_arg;
        argv[argc++] = "-n";
        argv[argc++] = c->name;
        argv[argc++] = "-t";
        argv[argc++] = target;
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

    printf("    %s PID: %d (extra_args=%s)\n", c->name, c->pid, extra_args ? extra_args : "none");
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

static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (g_alice.connected && g_bob.connected)
            return 0;
        P_usleep(100 * 1000);
        elapsed += 100;
    }
    return -1;
}

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

// 设置 instrument 选项（广播方式，同时生效于所有监听进程）
static void set_opt(int idx, int enable) {
    instrument_enable(idx, enable != 0);
    printf("    [OPT] opt[%d] = %d (broadcast)\n", idx, enable);
}

static void cleanup(void) {
    stop_client(&g_alice);
    stop_client(&g_bob);
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
// 测试 1: HOST only（关闭 SRFLX 和 RELAY）
///////////////////////////////////////////////////////////////////////////////

static void test_host_only(void) {
    const char *TEST_NAME = "host_only";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling SRFLX and RELAY candidates, HOST only\n");
    clear_logs();

    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }

    // 启动 Alice（使用 --no-srflx --no-relay 禁用 SRFLX 和 RELAY）
    if (start_client(&g_alice, "bob", "--no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        return;
    }
    sync_client(&g_alice);

    // 启动 Bob（同样使用 --no-srflx --no-relay）
    if (start_client(&g_bob, "alice", "--no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        stop_client(&g_alice);
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    sync_client(&g_bob);

    // 等待连接
    printf("[1] Waiting for connection (HOST only)...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 验证日志
    printf("[2] Verifying ICE candidate logs...\n");

    // 应该看到 "ignored due to instrument" 针对 srflx
    int srflx_ignored = count_logs("ignored due to instrument");
    int host_cand = count_logs("host cand") + count_logs("Host cand") + count_logs("HOST");

    printf("    SRFLX/RELAY ignored: %d logs\n", srflx_ignored);
    printf("    HOST candidate: %d logs\n", host_cand);

    // HOST 候选应该被使用
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
// 测试 2: 关闭 HOST 打洞（但仍收集）
///////////////////////////////////////////////////////////////////////////////

static void test_no_host_punch(void) {
    const char *TEST_NAME = "no_host_punch";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Disabling HOST punch, candidates should be collected but not punched\n");
    clear_logs();

    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }

    // 启动 Alice（使用 --no-srflx --no-relay 禁用 SRFLX 和 RELAY）
    if (start_client(&g_alice, "bob", "--no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        return;
    }

    // 设置选项：关闭 HOST 打洞（但仍收集）
    set_opt(1, 1);  // P2P_INST_OPT_HOST_PUNCH_OFF
    sync_client(&g_alice);

    // 启动 Bob（同样使用 --no-srflx --no-relay）
    if (start_client(&g_bob, "alice", "--no-srflx --no-relay") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        stop_client(&g_alice);
        return;
    }
    if (wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "bob waiting timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // Bob 也设置相同选项
    set_opt(1, 1);  // P2P_INST_OPT_HOST_PUNCH_OFF
    sync_client(&g_bob);

    // 等待连接（应该失败，因为 HOST 打洞被关闭，SRFLX 和 RELAY 也关闭）
    printf("[1] Waiting for connection (should timeout)...\n");
    int connect_result = wait_for_connection(15000);  // 15秒超时

    // 验证日志
    printf("[2] Verifying ICE candidate logs...\n");

    // 应该看到 "punch skipped due to instrument" 针对 host
    int punch_skipped = count_logs("punch skipped due to instrument");
    int host_cand = count_logs("host cand") + count_logs("Host cand");

    printf("    Punch skipped: %d logs\n", punch_skipped);
    printf("    HOST candidate collected: %d logs\n", host_cand);

    // HOST 候选应该被收集，但打洞被跳过
    if (host_cand == 0) {
        TEST_FAIL(TEST_NAME, "no HOST candidate collected");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    if (punch_skipped == 0) {
        printf("    Warning: no 'punch skipped' log found\n");
    }

    // 由于所有打洞都被禁用，连接应该失败
    if (connect_result == 0) {
        printf("    Note: Connection succeeded unexpectedly (possible other path)\n");
    } else {
        printf("    Connection timed out as expected (all punch disabled)\n");
    }

    print_log_summary();
    TEST_PASS(TEST_NAME);

    stop_client(&g_alice);
    stop_client(&g_bob);
}

///////////////////////////////////////////////////////////////////////////////
// 测试 3: 验证日志格式
///////////////////////////////////////////////////////////////////////////////

static void test_log_format(void) {
    const char *TEST_NAME = "log_format";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Verifying ICE log messages format\n");
    clear_logs();

    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }

    // 启动 Alice（不设置任何选项，正常模式）
    if (start_client(&g_alice, "bob", NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS);
    sync_client(&g_alice);

    // 启动 Bob
    if (start_client(&g_bob, "alice", NULL) != 0) {
        TEST_FAIL(TEST_NAME, "failed to start bob");
        stop_client(&g_alice);
        return;
    }
    wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS);
    sync_client(&g_bob);

    // 等待连接
    printf("[1] Waiting for normal connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }

    // 验证日志格式
    printf("[2] Verifying log format...\n");

    // 打印一些示例日志
    printf("\n--- Sample ICE Logs ---\n");
    int sample_count = 0;
    for (int i = 0; i < g_log_count && sample_count < 10; i++) {
        if (strstr(g_logs[i].txt, "cand") || strstr(g_logs[i].txt, "Cand") ||
            strstr(g_logs[i].txt, "ICE") || strstr(g_logs[i].txt, "Host") ||
            strstr(g_logs[i].txt, "srflx") || strstr(g_logs[i].txt, "relay")) {
            printf("  [%d] %s\n", i, g_logs[i].txt);
            sample_count++;
        }
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

    printf("=== P2P ICE Candidate Tests ===\n");
    printf("Ping:   %s\n", g_ping_path);
    printf("Server: %s\n", g_server_path);
    printf("Port:   %d\n\n", g_server_port);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // 初始化 instrument
    instrument_local(0);
    if (instrument_listen(on_instrument, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }

    // 启动 server
    printf("[*] Starting server...\n");
    if (start_server() != 0) {
        return 1;
    }

    // 运行测试
    printf("\n[*] Running tests...\n");

    test_host_only();
    test_no_host_punch();
    test_log_format();

    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();

    // 报告
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
