/*
 * test_ping_msg.c - P2P Ping 消息互发测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 *
 * 1. 验证两个 ping 客户端可以成功建立连接
 * 2. 通过 instrument_req 远程控制 ping 发送消息
 * 3. 通过日志验证对端收到消息
 * 4. 测试非交互模式（管道/重定向场景）
 *
 * ============================================================================
 * 消息互发协议
 * ============================================================================
 *
 *   [Test]            [Alice]            [Bob]
 *     |                  |                  |
 *     |  (连接建立)       <---- P2P ---->    |
 *     |                  |                  |
 *     |  instrument_req("alice", "send", "Hello Bob!")
 *     |  ------REQ------>|                  |
 *     |  <----"ok"-------+------ msg ------>|
 *     |                  |     (via P2P)    |
 *     |                  |                  |
 *     |  instrument_req("bob", "send", "Hi Alice!")
 *     |  --------------------------------------->|
 *     |  <----------"ok"---+<----- msg ----------|
 *     |                    |    (via P2P)        |
 *     |                  |                  |
 *     |  (验证日志中有 "alice: Hello Bob!" 和 "bob: Hi Alice!")
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING_MSG"

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

#define DEFAULT_SERVER_PORT     9335
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define SYNC_TIMEOUT_MS         10000       // 同步超时 10 秒
#define CONNECT_TIMEOUT_MS      30000       // 连接超时 30 秒
#define MESSAGE_TIMEOUT_MS      5000        // 消息发送/接收超时 5 秒

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
    // 网络 WAIT 包格式: "port_name" (如 "alice")
    // 本地回调格式: "waiting for xxx"
    if (tag == NULL && chn == INSTRUMENT_CTRL && txt) {
        // 检查是否包含名字
        char alice_pattern[64], bob_pattern[64];
        snprintf(alice_pattern, sizeof(alice_pattern), "%s", g_alice.name);
        snprintf(bob_pattern, sizeof(bob_pattern), "%s", g_bob.name);
        
        // 匹配 "alice" 或 "waiting for alice" 等
        if ((strcmp(txt, g_alice.name) == 0 || strstr(txt, g_alice.name)) && g_alice.rid == 0) {
            g_alice.rid = rid;
            g_alice.waiting = 1;
            printf("    [SYNC] Alice WAIT detected (rid=%u)\n", rid);
        } else if ((strcmp(txt, g_bob.name) == 0 || strstr(txt, g_bob.name)) && g_bob.rid == 0) {
            g_bob.rid = rid;
            g_bob.waiting = 1;
            printf("    [SYNC] Bob WAIT detected (rid=%u)\n", rid);
        }
    }
    
    // 检测连接成功
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
    
    // 实时显示关键日志
    #if 1
    if (rid != 0 && txt && (strstr(txt, ":") || strstr(txt, "send") || strstr(txt, "recv"))) {
        const char *src = "???";
        if (rid == g_alice.rid) src = "ALICE";
        else if (rid == g_bob.rid) src = "BOB";
        printf("    [LOG] %s: %s\n", src, txt);
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

// 在所有日志中搜索（不限 rid）
static int find_log(const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) return i;
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
        // 子进程：重定向 stdin/stdout 到 /dev/null 以测试非交互模式
        // 这会导致 P_term_init() 返回 false，ping 不会启动 TUI
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            // stdout 保持打开，便于观察日志
            close(null_fd);
        }
        
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
    
    printf("    %s PID: %d (target=%s, non-interactive)\n", client->name, client->pid, target ? target : "none");
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

// 通过 instrument_req 让客户端发送消息
static int ping_send_message(ping_client_t *client, const char *message) {
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
    
    if (strcmp(buffer, "ok") == 0) {
        return 0;
    }
    return -1;
}

// 通过 instrument_req 查询客户端状态
static int ping_get_state(ping_client_t *client) {
    char buffer[32] = "";
    
    ret_t r = instrument_req(client->name, MESSAGE_TIMEOUT_MS, "state", buffer, sizeof(buffer));
    if (r != E_NONE) {
        return -1;
    }
    
    return atoi(buffer);
}

// 通过 instrument_req 让客户端退出
static int ping_quit(ping_client_t *client) {
    char buffer[32] = "";
    
    printf("    [MSG] Requesting %s to quit\n", client->name);
    ret_t r = instrument_req(client->name, MESSAGE_TIMEOUT_MS, "quit", buffer, sizeof(buffer));
    if (r != E_NONE) {
        return -1;
    }
    return 0;
}

// 停止客户端
static void stop_client(ping_client_t *client) {
    if (client->pid > 0) {
        // 先尝试优雅退出
        ping_quit(client);
        P_usleep(200 * 1000);
        
        // 再发送 SIGTERM
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

// server 路径（需要在 main 前保存）
static const char *g_server_path = NULL;

// 重启 server（用于测试间隔离）
static int restart_server(void) {
    printf("    [*] Restarting server...\n");
    stop_server();
    P_usleep(100 * 1000);
    return start_server(g_server_path);
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

// 测试 1: 消息互发
static void test_message_exchange(void) {
    const char *TEST_NAME = "message_exchange";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 1. 启动 Alice (target=bob)
    printf("[1] Starting Alice (target=bob, non-interactive)...\n");
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
    
    // 2. 同步 Alice
    printf("[2] Syncing Alice...\n");
    if (sync_client(&g_alice) != 0) {
        TEST_FAIL(TEST_NAME, "alice sync failed");
        stop_client(&g_alice);
        return;
    }
    
    P_usleep(200 * 1000);
    
    // 3. 启动 Bob (target=alice)
    printf("[3] Starting Bob (target=alice, non-interactive)...\n");
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
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 等待连接稳定
    P_usleep(500 * 1000);
    
    // 6. Alice 发送消息给 Bob
    printf("[6] Alice sends message to Bob...\n");
    if (ping_send_message(&g_alice, "Hello Bob!") != 0) {
        TEST_FAIL(TEST_NAME, "alice failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 等待消息传递
    P_usleep(500 * 1000);
    
    // 7. Bob 发送消息给 Alice
    printf("[7] Bob sends message to Alice...\n");
    if (ping_send_message(&g_bob, "Hi Alice!") != 0) {
        TEST_FAIL(TEST_NAME, "bob failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 等待消息传递
    P_usleep(500 * 1000);
    
    // 8. 验证消息接收（通过日志）
    printf("[8] Verifying message delivery...\n");
    
    // Bob 应该收到 "alice: Hello Bob!"
    int bob_recv = find_log_from_rid(g_bob.rid, "Hello Bob");
    // Alice 应该收到 "bob: Hi Alice!"
    int alice_recv = find_log_from_rid(g_alice.rid, "Hi Alice");
    
    printf("    Bob received 'Hello Bob': %s\n", bob_recv >= 0 ? "yes" : "no");
    printf("    Alice received 'Hi Alice': %s\n", alice_recv >= 0 ? "yes" : "no");
    
    if (bob_recv < 0 || alice_recv < 0) {
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

// 测试 2: 非交互模式验证（stdin 重定向）
static void test_non_interactive_mode(void) {
    const char *TEST_NAME = "non_interactive_mode";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 重启 server 以清除旧会话状态
    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }
    
    // 重置客户端状态
    g_alice.pid = 0;
    g_alice.rid = 0;
    g_alice.waiting = 0;
    g_alice.connected = 0;
    g_bob.pid = 0;
    g_bob.rid = 0;
    g_bob.waiting = 0;
    g_bob.connected = 0;
    
    // 1. 启动 Alice（stdin 已重定向到 /dev/null）
    printf("[1] Starting Alice (non-interactive)...\n");
    if (start_ping_client(&g_alice, "bob") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        return;
    }
    
    // 2. 同步 Alice
    printf("[2] Syncing Alice...\n");
    sync_client(&g_alice);
    P_usleep(200 * 1000);
    
    // 3. 启动 Bob
    printf("[3] Starting Bob (non-interactive)...\n");
    if (start_ping_client(&g_bob, "alice") != 0) {
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
    
    // 4. 等待连接
    printf("[4] Waiting for connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(300 * 1000);
    
    // 5. 查询状态 - 验证客户端可以正常响应
    printf("[5] Querying client states...\n");
    int alice_state = ping_get_state(&g_alice);
    int bob_state = ping_get_state(&g_bob);
    
    printf("    Alice state: %d\n", alice_state);
    printf("    Bob state: %d\n", bob_state);
    
    // 状态应该 >= 0（有效状态），说明非交互模式下进程正常运行
    if (alice_state < 0 || bob_state < 0) {
        TEST_FAIL(TEST_NAME, "failed to query state");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
}

// 测试 3: instrument 日志收集验证
static void test_log_collection(void) {
    const char *TEST_NAME = "log_collection";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 重置客户端状态
    g_alice.rid = 0;
    g_alice.waiting = 0;
    g_bob.rid = 0;
    g_bob.waiting = 0;
    
    // 启动并同步客户端
    printf("[1] Starting clients...\n");
    start_ping_client(&g_alice, "bob");
    wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS);
    sync_client(&g_alice);
    
    start_ping_client(&g_bob, "alice");
    wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS);
    sync_client(&g_bob);
    
    // 等待连接
    printf("[2] Waiting for connection...\n");
    wait_for_connection(CONNECT_TIMEOUT_MS);
    P_usleep(1000 * 1000);  // 等待更多日志
    
    // 验证日志收集
    printf("[3] Verifying log collection...\n");
    
    int total_logs = g_log_count;
    int alice_logs = 0, bob_logs = 0;
    
    for (int i = 0; i < g_log_count; i++) {
        if (g_logs[i].rid == g_alice.rid) alice_logs++;
        else if (g_logs[i].rid == g_bob.rid) bob_logs++;
    }
    
    printf("    Total logs: %d\n", total_logs);
    printf("    Alice logs: %d\n", alice_logs);
    printf("    Bob logs: %d\n", bob_logs);
    
    // 应该收集到一定数量的日志
    if (total_logs < 10) {
        TEST_FAIL(TEST_NAME, "too few logs collected");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 两个客户端都应该有日志
    if (alice_logs == 0 || bob_logs == 0) {
        TEST_FAIL(TEST_NAME, "missing logs from one client");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    // 解析命令行参数
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ping_path> <server_path> [port]\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server        # Use port %d\n", argv[0], DEFAULT_SERVER_PORT);
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server 9555   # Use custom port\n", argv[0]);
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
    
    printf("=== P2P Ping Message Tests ===\n");
    printf("Ping path:   %s\n", g_ping_path);
    printf("Server path: %s\n", g_server_path);
    printf("Server addr: %s:%d\n", g_server_host, g_server_port);
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
    
    // 启动 server
    printf("[*] Starting server...\n");
    if (start_server(g_server_path) != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    
    // 运行测试
    printf("\n[*] Running tests...\n");
    
    test_message_exchange();
    test_non_interactive_mode();
    test_log_collection();
    
    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();
    
    // 报告
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
