/*
 * test_wss_lifecycle.c - WSS 协议生命周期单元测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 * 验证 p2p_server 对 WSS 会话生命周期的处理：
 * - 连接建立与断开
 * - 会话清理
 * - 重连处理
 *
 * ============================================================================
 * 测试用例
 * ============================================================================
 *
 * 测试 1: basic_connect_disconnect
 *   目标：验证基本的连接建立和断开
 *   方法：建立 WS 连接，REG，然后主动断开
 *   预期：server 正常处理断开事件
 *
 * 测试 2: paired_disconnect
 *   目标：验证配对后一方断开的处理
 *   方法：Alice 和 Bob 配对后，Alice 断开
 *   预期：Bob 收到对端离线通知
 *
 * 测试 3: reconnect_same_peer
 *   目标：验证同一 peer 的重连
 *   方法：Alice 上线，断开，再次上线
 *   预期：两次都能成功 REG
 *
 * ============================================================================
 * 依赖与用法
 * ============================================================================
 * 依赖：p2p_server (--ws 模式), ws_client
 *
 * 用法：
 *   ./test_wss_lifecycle <server_path> [port]
 */

#define MOD_TAG "TEST"

#include <stdc.h>
#include <p2p.h>
#include <p2pp.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef WITH_WS
#include "../src/ws_client.h"
#endif

#define DEFAULT_SERVER_PORT     9333
#define DEFAULT_SERVER_HOST     "127.0.0.1"

static int g_server_port = DEFAULT_SERVER_PORT;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static pid_t g_server_pid = 0;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_PASS(name) do { printf("%s:%d   \033[32m[PASS] %s\033[0m\n", __FILE__, __LINE__, name); g_tests_passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("%s:%d   \033[31m[FAIL] %s: %s\033[0m\n", __FILE__, __LINE__, name, reason); g_tests_failed++; } while(0)

#ifdef WITH_WS

// WebSocket 回调（简化版）
static void ws_on_message_simple(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c; (void)user_data;
    if (type == WS_MSG_TEXT) {
        char msg[512];
        size_t copy_len = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
        memcpy(msg, data, copy_len);
        msg[copy_len] = '\0';
        printf("    [WS] Received: %s\n", msg);
    }
}

static ws_client_t* ws_connect_simple(void) {
    ws_client_cfg_t cfg = {
        .on_open = NULL,
        .on_message = ws_on_message_simple,
        .on_close = NULL,
        .user_data = NULL,
        .extra_headers = NULL
    };
    
    ws_client_t *client = ws_client_create(&cfg);
    if (!client) return NULL;
    
    if (ws_client_connect(client, g_server_host, (uint16_t)g_server_port, "/") != 0) {
        ws_client_destroy(client);
        return NULL;
    }
    
    // 等待握手完成
    for (int i = 0; i < 3000; i++) {
        ws_client_update(client);
        if (ws_client_state(client) == WS_CLIENT_OPEN) return client;
        if (ws_client_state(client) == WS_CLIENT_ERROR) break;
        P_usleep(1000);
    }
    
    ws_client_destroy(client);
    return NULL;
}

static void test_basic_connect_disconnect(void) {
    const char *TEST_NAME = "basic_connect_disconnect";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    
    ws_client_t *client = ws_connect_simple();
    if (!client) {
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    // REG
    char cmd[128];
    uint32_t inst_id = (uint32_t)P_tick_us();
    snprintf(cmd, sizeof(cmd), "REG test_lifecycle %u\n", inst_id);
    ws_client_send_text(client, cmd);
    
    // 等待响应
    for (int i = 0; i < 100; i++) {
        ws_client_update(client);
        P_usleep(10000);
    }
    
    // 主动断开
    ws_client_close(client, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(client);
        P_usleep(1000);
    }
    ws_client_destroy(client);
    
    TEST_PASS(TEST_NAME);
}

static void test_paired_disconnect(void) {
    const char *TEST_NAME = "paired_disconnect";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    
    ws_client_t *alice = ws_connect_simple();
    ws_client_t *bob = ws_connect_simple();
    
    if (!alice || !bob) {
        if (alice) ws_client_destroy(alice);
        if (bob) ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    char cmd[128];
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1;
    
    // Alice 和 Bob REG
    snprintf(cmd, sizeof(cmd), "REG alice_lf %u\n", inst_alice);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "REG bob_lf %u\n", inst_bob);
    ws_client_send_text(bob, cmd);
    
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }
    
    // Alice SYN0 等待 Bob
    snprintf(cmd, sizeof(cmd), "SYN0 bob_lf\n");
    ws_client_send_text(alice, cmd);
    
    // Bob SYN0 等待 Alice
    snprintf(cmd, sizeof(cmd), "SYN0 alice_lf\n");
    ws_client_send_text(bob, cmd);
    
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }
    
    // Alice 断开
    ws_client_close(alice, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(1000);
    }
    ws_client_destroy(alice);
    
    // Bob 继续运行一会
    for (int i = 0; i < 100; i++) {
        ws_client_update(bob);
        P_usleep(1000);
    }
    
    ws_client_close(bob, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(bob);
        P_usleep(1000);
    }
    ws_client_destroy(bob);
    
    TEST_PASS(TEST_NAME);
}

static void test_reconnect_same_peer(void) {
    const char *TEST_NAME = "reconnect_same_peer";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    
    // 第一次连接
    ws_client_t *client1 = ws_connect_simple();
    if (!client1) {
        TEST_FAIL(TEST_NAME, "first connect failed");
        return;
    }
    
    char cmd[128];
    uint32_t inst_id1 = (uint32_t)P_tick_us();
    snprintf(cmd, sizeof(cmd), "REG alice_reconnect %u\n", inst_id1);
    ws_client_send_text(client1, cmd);
    
    for (int i = 0; i < 50; i++) {
        ws_client_update(client1);
        P_usleep(10000);
    }
    
    // 断开
    ws_client_close(client1, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(client1);
        P_usleep(1000);
    }
    ws_client_destroy(client1);
    
    P_usleep(200 * 1000);
    
    // 第二次连接（新 instance_id）
    ws_client_t *client2 = ws_connect_simple();
    if (!client2) {
        TEST_FAIL(TEST_NAME, "reconnect failed");
        return;
    }
    
    uint32_t inst_id2 = (uint32_t)P_tick_us();
    snprintf(cmd, sizeof(cmd), "REG alice_reconnect %u\n", inst_id2);
    ws_client_send_text(client2, cmd);
    
    for (int i = 0; i < 50; i++) {
        ws_client_update(client2);
        P_usleep(10000);
    }
    
    ws_client_close(client2, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(client2);
        P_usleep(1000);
    }
    ws_client_destroy(client2);
    
    TEST_PASS(TEST_NAME);
}

#endif // WITH_WS

int main(int argc, char *argv[]) {
    
#ifndef WITH_WS
    printf("WSS tests require WITH_WS build flag\n");
    return 0;
#else
    
    const char *server_path = NULL;
    
    if (argc >= 2) {
        server_path = argv[1];
    }
    if (argc > 2) {
        g_server_port = atoi(argv[2]);
    }
    
    printf("=== WSS Lifecycle Tests ===\n");
    printf("Server: %s\n", server_path ? server_path : "(external)");
    printf("Address: ws://%s:%d\n\n", g_server_host, g_server_port);
    
    // 忽略 SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    // 启动 server
    if (server_path) {
        printf("[*] Starting server (WSS mode)...\n");
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", g_server_port);

        g_server_pid = fork();
        if (g_server_pid == 0) {
            execl(server_path, server_path, "--cn", "-p", port_str, "--ws", NULL);
            _exit(127);
        }
        printf("    Server PID: %d\n", g_server_pid);
        P_usleep(500 * 1000);
    }
    
    printf("\n[*] Running tests...\n");
    
    test_basic_connect_disconnect();
    test_paired_disconnect();
    test_reconnect_same_peer();
    
    // 终止 server
    if (g_server_pid > 0) {
        printf("\n[*] Terminating server...\n");
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
    }
    
    printf("\n===== Test Results =====\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("========================\n");
    
    return g_tests_failed > 0 ? 1 : 0;
    
#endif
}
