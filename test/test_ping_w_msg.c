/*
 * test_ping_w_msg.c - P2P Ping 消息互发测试 (WSS 模式)
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 *
 * 1. 验证两个 ping 客户端可以通过 WSS 模式成功建立连接
 * 2. 通过 instrument_req 远程控制 ping 发送消息
 * 3. 通过日志验证对端收到消息
 * 4. 测试非交互模式（管道/重定向场景）
 *
 * ============================================================================
 * 消息互发协议 (WSS 模式)
 * ============================================================================
 *
 *   [Test]            [Alice]            [Bob]
 *     |                  |                  |
 *     |  (WebSocket连接)  <-- WSS Server -->|
 *     |                  |                  |
 *     |  instrument_req("alice", "send", "Hello Bob!")
 *     |  ------REQ------>|                  |
 *     |  <----"ok"-------+------ msg ------>|
 *     |                  |  (via P2P)       |
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

#define MOD_TAG "TEST_PING_W_MSG"

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

#define DEFAULT_SERVER_PORT     9434
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
    
    // 连接状态检测
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
}

///////////////////////////////////////////////////////////////////////////////
// 日志查询辅助
///////////////////////////////////////////////////////////////////////////////

static void clear_logs(void) {
    g_log_count = 0;
}

// 在日志中查找包含指定文本的条目
static int find_log(const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) {
            return i;
        }
    }
    return -1;
}

// 等待日志中出现包含指定文本的条目
static int wait_for_log(const char *pattern, int timeout_ms) {
    int64_t start = P_tick_ms();
    while (P_tick_ms() - start < (int64_t)timeout_ms) {
        if (find_log(pattern) >= 0) {
            return 0;
        }
        P_usleep(50 * 1000);
    }
    return -1;
}

// 打印日志摘要（调试用）
static void print_log_summary(void) {
    printf("    [LOG] Total entries: %d\n", g_log_count);
    for (int i = 0; i < g_log_count && i < 20; i++) {
        printf("    [LOG/%u] %s\n", g_logs[i].rid, g_logs[i].txt);
    }
}

///////////////////////////////////////////////////////////////////////////////
// 客户端管理
///////////////////////////////////////////////////////////////////////////////

// 启动 ping 客户端（WSS 模式）
static int start_ping_client(ping_client_t *client, const char *target, const char *extra_arg) {
    char server_arg[64];
    snprintf(server_arg, sizeof(server_arg), "%s:%d", g_server_host, g_server_port);
    
    client->pid = fork();
    if (client->pid < 0) {
        fprintf(stderr, "Failed to fork %s: %s\n", client->name, strerror(errno));
        return -1;
    } else if (client->pid == 0) {
        // 子进程：运行 p2p_ping
        if (extra_arg) {
            execl(g_ping_path, g_ping_path,
                  "--wss",
                  "--no-stun-test",
                  "-s", server_arg,
                  "-n", client->name,
                  "-t", target,
                  "--debugger", client->name,
                  extra_arg,
                  NULL);
        } else {
            execl(g_ping_path, g_ping_path,
                  "--wss",
                  "--no-stun-test",
                  "-s", server_arg,
                  "-n", client->name,
                  "-t", target,
                  "--debugger", client->name,
                  NULL);
        }
        fprintf(stderr, "Failed to exec p2p_ping: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    %s PID: %d (target=%s, wss mode)\n", client->name, client->pid, target);
    return 0;
}

// 等待客户端进入 WAIT 状态
static int wait_for_waiting(ping_client_t *client, int timeout_ms) {
    int elapsed = 0;
    const int poll_interval = 100;
    
    while (elapsed < timeout_ms) {
        if (client->waiting) {
            return 0;
        }
        P_usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    
    printf("    [SYNC] Timeout waiting for %s WAIT signal\n", client->name);
    return -1;
}

// 发送 CONTINUE 信号
static int sync_client(ping_client_t *client) {
    printf("    [SYNC] Sending continue to '%s'...\n", client->name);
    
    ret_t r = instrument_continue(client->name, client->name);
    if (r != E_NONE) {
        printf("    [SYNC] Failed to send continue: %d\n", r);
        return -1;
    }
    
    client->resumed = 1;
    printf("    [SYNC] %s resumed\n", client->name);
    return 0;
}

// 等待连接建立
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
    
    printf("    [CONN] Timeout! Final state: alice=%d, bob=%d\n", 
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
        printf("    %s stopped\n", client->name);
        client->pid = 0;
    }
}

// 停止 server 子进程
static void stop_server(void) {
    if (g_server_pid > 0) {
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
        printf("    Server stopped\n");
        g_server_pid = 0;
    }
}

// server 路径
static const char *g_server_path = NULL;

// 启动 server 子进程 (WSS 模式)
static int start_server(const char *server_path) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);
    
    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork server: %s\n", strerror(errno));
        return -1;
    } else if (g_server_pid == 0) {
        // 带 --ws 参数启用 WebSocket 支持，以及 -r 和 -m 启用数据中继和 RPC
        execl(server_path, server_path, "--ws", "-p", port_str, "-r", "-m", NULL);
        fprintf(stderr, "Failed to exec server: %s\n", strerror(errno));
        _exit(127);
    }
    
    printf("    Server PID: %d (WebSocket mode with relay and msg support)\n", g_server_pid);
    P_usleep(500 * 1000);
    return 0;
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

// 重置客户端状态
static void reset_clients(void) {
    g_alice.pid = 0;
    g_alice.rid = 0;
    g_alice.waiting = 0;
    g_alice.resumed = 0;
    g_alice.connected = 0;
    g_bob.pid = 0;
    g_bob.rid = 0;
    g_bob.waiting = 0;
    g_bob.resumed = 0;
    g_bob.connected = 0;
}

// 测试: 消息互发 (WSS 模式)
static void test_message_exchange(void) {
    const char *TEST_NAME = "wss_message_exchange";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    reset_clients();
    
    // 1. 启动 Alice (target=bob)
    printf("[1] Starting Alice (target=bob, wss mode)...\n");
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
    printf("[3] Starting Bob (target=alice, wss mode)...\n");
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
    printf("[5] Waiting for P2P connection (WSS)...\n");
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
    if (ping_send_message(&g_alice, "Hello Bob via WSS!") != 0) {
        TEST_FAIL(TEST_NAME, "alice failed to send message");
        stop_client(&g_alice);
        stop_client(&g_bob);
        return;
    }
    
    P_usleep(500 * 1000);
    
    // 7. Bob 发送消息给 Alice
    printf("[7] Bob sends message to Alice...\n");
    if (ping_send_message(&g_bob, "Hi Alice via WSS!") != 0) {
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
    
    if (bob_recv >= 0 && alice_recv >= 0) {
        TEST_PASS(TEST_NAME);
    } else {
        TEST_FAIL(TEST_NAME, "message delivery failed");
    }
    
    stop_client(&g_alice);
    stop_client(&g_bob);
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
    g_server_path = argv[2];
    
    if (argc >= 4) {
        g_server_port = atoi(argv[3]);
    }
    
    printf("=== P2P Ping WSS Mode Message Exchange Tests ===\n");
    printf("Ping path:   %s\n", g_ping_path);
    printf("Server path: %s\n", g_server_path);
    printf("Server port: %d\n", g_server_port);
    
    // 设置信号处理
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    
    // 初始化 instrument
    instrument_listen(on_instrument_log, NULL);
    
    // 启动服务器
    printf("\n[0] Starting server (WebSocket mode)...\n");
    if (start_server(g_server_path) != 0) {
        printf("Failed to start server\n");
        return 1;
    }
    
    // 运行测试
    test_message_exchange();
    
    // 清理
    cleanup();
    
    // 输出测试结果
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return (g_tests_failed > 0) ? 1 : 0;
}
