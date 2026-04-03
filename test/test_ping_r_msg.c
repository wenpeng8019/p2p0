/*
 * test_ping_r_msg.c - P2P Ping 消息互发测试 (RELAY 模式)
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 *
 * 1. 验证两个 ping 客户端可以通过 RELAY 模式成功建立连接
 * 2. 通过 instrument_req 远程控制 ping 发送消息
 * 3. 通过日志验证对端收到消息
 * 4. 测试非交互模式（管道/重定向场景）
 *
 * ============================================================================
 * 消息互发协议 (RELAY 模式)
 * ============================================================================
 *
 *   [Test]            [Alice]            [Bob]
 *     |                  |                  |
 *     |  (TCP连接建立)    <-- RELAY Server ->|
 *     |                  |                  |
 *     |  instrument_req("alice", "send", "Hello Bob!")
 *     |  ------REQ------>|                  |
 *     |  <----"ok"-------+------ msg ------>|
 *     |                  |  (via RELAY)     |
 *     |                  |                  |
 *     |  instrument_req("bob", "send", "Hi Alice!")
 *     |  --------------------------------------->|
 *     |  <----------"ok"---+<----- msg ----------|
 *     |                    |    (via RELAY)      |
 *     |                  |                  |
 *     |  (验证日志中有 "alice: Hello Bob!" 和 "bob: Hi Alice!")
 *
 * ============================================================================
 */

#define MOD_TAG "TEST_PING_R_MSG"

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

#define DEFAULT_SERVER_PORT     9435        // RELAY 模式使用不同端口
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define SYNC_TIMEOUT_MS         10000       // 同步超时 10 秒
#define CONNECT_TIMEOUT_MS      10000       // 连接超时 10 秒（调试用）
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
    volatile int relay;         // 是否走了 RELAY 路径
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
    if (tag == NULL && chn == INSTRUMENT_CTRL && txt) {
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
    
    // 检测连接成功（RELAY 模式特有的状态）
    // 日志格式: [STATE] xxx (n) -> yyy (m) 或 [EVENT] State: xxx -> yyy
    if (txt && (strstr(txt, "-> CONNECTED") || 
                strstr(txt, "-> RELAY (") ||
                strstr(txt, "P2P connection established") || 
                strstr(txt, "bidirectional confirmed"))) {
        if (rid == g_alice.rid && !g_alice.connected) {
            g_alice.connected = 1;
            printf("    [CONN] Alice connected!\n");
        } else if (rid == g_bob.rid && !g_bob.connected) {
            g_bob.connected = 1;
            printf("    [CONN] Bob connected!\n");
        }
    }
    
    // 检测 RELAY 状态（走中继路径）
    if (txt && strstr(txt, "-> RELAY (")) {
        if (rid == g_alice.rid) {
            g_alice.relay = 1;
            printf("    [CONN] Alice using RELAY path!\n");
        } else if (rid == g_bob.rid) {
            g_bob.relay = 1;
            printf("    [CONN] Bob using RELAY path!\n");
        }
    }
    
    // 调试：显示 STATE 相关的日志
    if (txt && strstr(txt, "STATE")) {
        const char *src = (rid == g_alice.rid) ? "ALICE" : 
                          (rid == g_bob.rid) ? "BOB" : "???";
        printf("    [DBG] %s STATE: %s\n", src, txt);
    }
    
    // 实时显示关键日志
    #if 0  // 暂时关闭冗长的日志
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

static int wait_for_log(const char *pattern, int timeout_ms) {
    int waited = 0;
    while (waited < timeout_ms) {
        if (find_log(pattern) >= 0) return 0;
        P_usleep(100 * 1000);
        waited += 100;
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

// 启动 ping 客户端 (RELAY 模式)
static int start_ping_client(ping_client_t *client, const char *target, const char *extra_args) {
    char server_arg[64];
    snprintf(server_arg, sizeof(server_arg), "%s:%d", g_server_host, g_server_port);
    
    // 重置状态
    client->pid = 0;
    client->rid = 0;
    client->waiting = 0;
    client->resumed = 0;
    client->connected = 0;
    client->relay = 0;
    
    client->pid = fork();
    if (client->pid < 0) {
        fprintf(stderr, "Failed to fork %s: %s\n", client->name, strerror(errno));
        return -1;
    } else if (client->pid == 0) {
        // 子进程：重定向 stdin 到 /dev/null 以测试非交互模式
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        
        // 构建参数列表，RELAY 模式（默认，不带 --compact）
        const char *argv[32];
        int argc = 0;
        argv[argc++] = g_ping_path;
        argv[argc++] = "-s";
        argv[argc++] = server_arg;
        argv[argc++] = "-n";
        argv[argc++] = client->name;
        if (target) {
            argv[argc++] = "-t";
            argv[argc++] = target;
        }
        argv[argc++] = "--debugger";
        argv[argc++] = client->name;
        
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
        fprintf(stderr, "Failed to exec %s: %s\n", client->name, strerror(errno));
        _exit(127);
    }
    
    printf("    %s PID: %d (target=%s, relay mode, non-interactive)\n", 
           client->name, client->pid, target ? target : "none");
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

// 前向声明：查询客户端状态
static int ping_get_state(ping_client_t *client);

// 等待两个客户端都连接成功
static int wait_for_connection(int timeout_ms) {
    int elapsed = 0;
    const int poll_interval = 500;  // 每 500ms 查询一次状态
    
    printf("    [CONN] Waiting for P2P connection (RELAY mode)...\n");
    
    while (elapsed < timeout_ms) {
        // 直接使用 instrument 查询实际状态
        // P2P_STATE_CONNECTED = 7, P2P_STATE_RELAY = 8
        int alice_state = ping_get_state(&g_alice);
        int bob_state = ping_get_state(&g_bob);
        
        printf("    [CONN] State check: alice=%d, bob=%d (need 7 or 8)\n", 
               alice_state, bob_state);
        
        if ((alice_state == 7 || alice_state == 8) && 
            (bob_state == 7 || bob_state == 8)) {
            printf("    [CONN] Both clients ready!\n");
            // 额外等待数据通道稳定
            P_usleep(500 * 1000);
            return 0;
        }
        
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    
    printf("    [CONN] Timeout! Final state: alice=%d, bob=%d\n", 
           ping_get_state(&g_alice), ping_get_state(&g_bob));
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
        ping_quit(client);
        P_usleep(200 * 1000);
        
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

// server 路径
static const char *g_server_path = NULL;

// 启动 server 子进程 (RELAY 模式必须带 -r)
static int start_server(const char *server_path) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);
    
    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork server: %s\n", strerror(errno));
        return -1;
    } else if (g_server_pid == 0) {
        // 带 -r 参数启用 RELAY 支持，-m 启用消息中继
        execl(server_path, server_path, "-p", port_str, "-r", "-m", NULL);
        fprintf(stderr, "Failed to exec server: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    Server PID: %d (relay mode)\n", g_server_pid);
    P_usleep(500 * 1000);
    return 0;
}

// 重启 server
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

// 重置客户端状态
static void reset_clients(void) {
    g_alice.pid = 0;
    g_alice.rid = 0;
    g_alice.waiting = 0;
    g_alice.resumed = 0;
    g_alice.connected = 0;
    g_alice.relay = 0;
    g_bob.pid = 0;
    g_bob.rid = 0;
    g_bob.waiting = 0;
    g_bob.resumed = 0;
    g_bob.connected = 0;
    g_bob.relay = 0;
}

// 测试 1: 消息互发 (RELAY 模式)
static void test_message_exchange(void) {
    const char *TEST_NAME = "relay_message_exchange";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    reset_clients();
    
    // 1. 启动 Alice (target=bob)
    printf("[1] Starting Alice (target=bob, relay mode)...\n");
    if (start_ping_client(&g_alice, "bob", NULL) != 0) {
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
    if (sync_client(&g_alice) != 0) {
        TEST_FAIL(TEST_NAME, "alice sync failed");
        stop_client(&g_alice);
        return;
    }
    
    P_usleep(200 * 1000);
    
    // 3. 启动 Bob (target=alice)
    printf("[3] Starting Bob (target=alice, relay mode)...\n");
    if (start_ping_client(&g_bob, "alice", NULL) != 0) {
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
    
    // 4. 同步 Bob
    printf("[4] Syncing Bob...\n");
    if (sync_client(&g_bob) != 0) {
        TEST_FAIL(TEST_NAME, "bob sync failed");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 5. 等待连接成功
    printf("[5] Waiting for P2P connection (RELAY)...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(500 * 1000);
    
    // 6. Alice 发送消息给 Bob
    printf("[6] Alice sends message to Bob...\n");
    if (ping_send_message(&g_alice, "Hello Bob via RELAY!") != 0) {
        TEST_FAIL(TEST_NAME, "alice failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(500 * 1000);
    
    // 7. Bob 发送消息给 Alice
    printf("[7] Bob sends message to Alice...\n");
    if (ping_send_message(&g_bob, "Hi Alice via RELAY!") != 0) {
        TEST_FAIL(TEST_NAME, "bob failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    // 8. 验证消息接收
    printf("[8] Verifying message delivery...\n");

    int bob_recv = (wait_for_log("Hello Bob", 3000) == 0) ? find_log("Hello Bob") : -1;
    int alice_recv = (wait_for_log("Hi Alice", 3000) == 0) ? find_log("Hi Alice") : -1;
    
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
    P_usleep(500 * 1000);
}

// 测试 2: 非交互模式验证
static void test_non_interactive_mode(void) {
    const char *TEST_NAME = "relay_non_interactive_mode";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }
    
    reset_clients();
    
    // 1. 启动 Alice
    printf("[1] Starting Alice (non-interactive, relay)...\n");
    if (start_ping_client(&g_alice, "bob", NULL) != 0) {
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
    printf("[3] Starting Bob (non-interactive, relay)...\n");
    if (start_ping_client(&g_bob, "alice", NULL) != 0) {
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
    
    // 5. 查询状态
    printf("[5] Querying client states...\n");
    int alice_state = ping_get_state(&g_alice);
    int bob_state = ping_get_state(&g_bob);
    
    printf("    Alice state: %d\n", alice_state);
    printf("    Bob state: %d\n", bob_state);
    
    if (alice_state < 0 || bob_state < 0) {
        TEST_FAIL(TEST_NAME, "failed to query state");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
    P_usleep(500 * 1000);
}

// 测试 3: instrument 日志收集验证
static void test_log_collection(void) {
    const char *TEST_NAME = "relay_log_collection";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    reset_clients();
    
    // 启动并同步客户端
    printf("[1] Starting clients (relay mode)...\n");
    start_ping_client(&g_alice, "bob", NULL);
    wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS);
    sync_client(&g_alice);
    
    start_ping_client(&g_bob, "alice", NULL);
    wait_for_waiting(&g_bob, SYNC_TIMEOUT_MS);
    sync_client(&g_bob);
    
    // 等待连接
    printf("[2] Waiting for connection...\n");
    wait_for_connection(CONNECT_TIMEOUT_MS);
    P_usleep(1000 * 1000);
    
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
    
    if (total_logs < 10) {
        TEST_FAIL(TEST_NAME, "too few logs collected");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    if (alice_logs == 0 || bob_logs == 0) {
        TEST_FAIL(TEST_NAME, "missing logs from one client");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
    P_usleep(500 * 1000);
}

// 测试 4: 数据中继功能 (RELAY 模式特有)
static void test_data_relay(void) {
    const char *TEST_NAME = "data_relay";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    printf("    Testing data relay through RELAY server\n");
    clear_logs();
    
    if (restart_server() != 0) {
        TEST_FAIL(TEST_NAME, "failed to restart server");
        return;
    }
    
    reset_clients();
    
    // 1. 启动 Alice（使用 --no-host --no-srflx 禁用直连，强制走 RELAY）
    printf("[1] Starting Alice (no-host, no-srflx, relay mode)...\n");
    if (start_ping_client(&g_alice, "bob", "--no-host --no-srflx") != 0) {
        TEST_FAIL(TEST_NAME, "failed to start alice");
        return;
    }
    
    if (wait_for_waiting(&g_alice, SYNC_TIMEOUT_MS) != 0) {
        TEST_FAIL(TEST_NAME, "alice waiting timeout");
        stop_client(&g_alice);
        return;
    }
    
    // 2. 启动 Bob
    printf("[2] Starting Bob (no-host, no-srflx, relay mode)...\n");
    if (start_ping_client(&g_bob, "alice", "--no-host --no-srflx") != 0) {
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
    
    // 3. 同步双方
    printf("[3] Syncing Alice and Bob...\n");
    sync_client(&g_alice);
    sync_client(&g_bob);
    
    // 4. 等待连接（应该通过 RELAY 连接）
    printf("[4] Waiting for RELAY connection...\n");
    if (wait_for_connection(CONNECT_TIMEOUT_MS) != 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "relay connection timeout");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(500 * 1000);
    
    // 5. Alice 发送消息
    printf("[5] Alice sends message via RELAY...\n");
    if (ping_send_message(&g_alice, "Data via RELAY path!") != 0) {
        TEST_FAIL(TEST_NAME, "alice failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(1000 * 1000);
    
    // 6. Bob 发送消息
    printf("[6] Bob sends message via RELAY...\n");
    if (ping_send_message(&g_bob, "Reply via RELAY!") != 0) {
        TEST_FAIL(TEST_NAME, "bob failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(1000 * 1000);
    
    // 7. 验证 RELAY 路径使用
    printf("[7] Verifying RELAY path usage...\n");
    printf("    Alice relay: %s\n", g_alice.relay ? "yes" : "no");
    printf("    Bob relay: %s\n", g_bob.relay ? "yes" : "no");
    
    int signaling_log = find_log("SIGNALING path") >= 0;
    printf("    SIGNALING path log: %s\n", signaling_log ? "yes" : "no");
    
    // 8. 验证消息接收
    printf("[8] Verifying message delivery via RELAY...\n");
    
    int bob_recv = find_log_from_rid(g_bob.rid, "Data via RELAY");
    int alice_recv = find_log_from_rid(g_alice.rid, "Reply via RELAY");
    
    printf("    Bob received message: %s\n", bob_recv >= 0 ? "yes" : "no");
    printf("    Alice received message: %s\n", alice_recv >= 0 ? "yes" : "no");
    
    if (bob_recv < 0 || alice_recv < 0) {
        print_log_summary();
        TEST_FAIL(TEST_NAME, "message not received via relay");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    print_log_summary();
    TEST_PASS(TEST_NAME);
    
    stop_client(&g_alice);
    stop_client(&g_bob);
    P_usleep(500 * 1000);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    // 解析命令行参数
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ping_path> <server_path> [port] [test_name]\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server              # Run all tests on port %d\n", argv[0], DEFAULT_SERVER_PORT);
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server 9555         # Use custom port\n", argv[0]);
        fprintf(stderr, "  %s ./p2p_ping ./p2p_server 9555 msg     # Only run tests matching 'msg'\n", argv[0]);
        return 1;
    }
    g_ping_path = argv[1];
    g_server_path = argv[2];
    const char *test_filter = NULL;
    if (argc > 3) {
        int port = atoi(argv[3]);
        if (port > 0 && port <= 65535) {
            g_server_port = port;
        } else {
            test_filter = argv[3];
        }
    }
    if (argc > 4) {
        test_filter = argv[4];
    }
    
    printf("=== P2P Ping Message Tests (RELAY Mode) ===\n");
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
    
    // 启动 server (RELAY 模式)
    printf("[*] Starting server (relay mode)...\n");
    if (start_server(g_server_path) != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    
    // 运行测试
    printf("\n[*] Running tests...%s\n", test_filter ? test_filter : " (all)");

#define RUN_IF(name, fn) do { \
    if (!test_filter || strstr(#name, test_filter)) { fn(); } \
    else { printf("  [SKIP] %s\n", #name); } \
} while(0)

    RUN_IF(relay_message_exchange,    test_message_exchange);
    RUN_IF(relay_non_interactive_mode, test_non_interactive_mode);
    RUN_IF(relay_log_collection,      test_log_collection);
    RUN_IF(data_relay,                test_data_relay);

#undef RUN_IF
    
    // 清理
    printf("\n[*] Cleaning up...\n");
    cleanup();
    
    // 报告
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
