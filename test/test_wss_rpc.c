/*
 * test_wss_rpc.c - WSS 协议 RPC 消息单元测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 * 验证 p2p_server 对 WSS 协议 RPC 消息的处理：
 * - REQ/RSP 请求-响应机制
 * - 超时重传
 * - 对端离线处理
 *
 * ============================================================================
 * 测试用例
 * ============================================================================
 *
 * 测试 1: msg_req_rsp_basic
 *   目标：验证基本的 REQ/RSP 流程
 *   方法：Alice 向 Bob 发送 REQ，Bob 回复 RSP
 *   预期：Alice 收到 RSP 响应
 *
 * 测试 2: msg_req_timeout
 *   目标：验证 REQ 超时处理
 *   方法：Alice 发 REQ，Bob 不回复
 *   预期：Alice 收到超时错误
 *
 * 测试 3: msg_req_peer_offline
 *   目标：验证对端离线时的处理
 *   方法：Alice 发 REQ 给已断线的 Bob
 *   预期：Alice 收到对端离线错误
 *
 * 测试 4: msg_multiple_requests
 *   目标：验证多个连续请求
 *   方法：Alice 逐条发送多个 REQ（每条等待 RSP）
 *   预期：按序收到所有 RSP
 *
 * ============================================================================
 * 依赖与用法
 * ============================================================================
 * 依赖：p2p_server (--ws --msg 模式), ws_client
 *
 * 用法：
 *   ./test_wss_rpc <server_path> [port]
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

// 注意：RPC 测试需要服务器支持 --msg 功能
// 这里实现基本的占位符测试，确保连接和命令发送工作正常

static void ws_on_message_simple(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c; (void)user_data;
    if (type == WS_MSG_TEXT) {
        char msg[512];
        size_t copy_len = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
        memcpy(msg, data, copy_len);
        msg[copy_len] = '\0';
        printf("    [WS] Received: %s\n", msg);
    } else if (type == WS_MSG_BINARY) {
        printf("    [WS] Received binary: %zu bytes\n", len);
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

// --- 全局缓冲区用于同步消息 ---
static volatile int g_alice_got_rsp = 0;
static char g_alice_rsp[512];
static volatile int g_bob_got_req = 0;
static char g_bob_req[512];
static volatile int g_alice_reg_ok = 0;
static volatile int g_bob_reg_ok = 0;
static volatile int g_alice_syn0_ok = 0;
static volatile int g_bob_syn0_ok = 0;
static uint32_t g_alice_session_id = 0;
static uint32_t g_bob_session_id = 0;
static uint16_t g_bob_req_sid = 0;

#define MAX_TRACKED_RPC 32
static volatile int g_alice_rsp_count = 0;
static uint16_t g_alice_rsp_sid = 0;
static uint8_t g_alice_rsp_code = 0;
static uint16_t g_alice_rsp_sid_list[MAX_TRACKED_RPC];
static uint8_t g_alice_rsp_code_list[MAX_TRACKED_RPC];
static volatile int g_bob_req_count = 0;
static uint16_t g_bob_req_sid_list[MAX_TRACKED_RPC];
static volatile int g_bob_auto_rsp = 0;
static const char *g_bob_auto_rsp_payload = "auto_rsp";

static void reset_rpc_state(void) {
    g_alice_got_rsp = 0;
    g_bob_got_req = 0;
    g_alice_rsp[0] = 0;
    g_bob_req[0] = 0;
    g_alice_reg_ok = 0;
    g_bob_reg_ok = 0;
    g_alice_syn0_ok = 0;
    g_bob_syn0_ok = 0;
    g_alice_session_id = 0;
    g_bob_session_id = 0;
    g_bob_req_sid = 0;
    g_alice_rsp_count = 0;
    g_alice_rsp_sid = 0;
    g_alice_rsp_code = 0;
    g_bob_req_count = 0;
    g_bob_auto_rsp = 0;
    memset(g_alice_rsp_sid_list, 0, sizeof(g_alice_rsp_sid_list));
    memset(g_alice_rsp_code_list, 0, sizeof(g_alice_rsp_code_list));
    memset(g_bob_req_sid_list, 0, sizeof(g_bob_req_sid_list));
}

static void ws_on_message_alice(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c; (void)user_data;
    if (type == WS_MSG_TEXT && len < sizeof(g_alice_rsp)) {
        memcpy((void*)g_alice_rsp, data, len);
        g_alice_rsp[len] = '\0';
        if (strstr(g_alice_rsp, "REG OK")) g_alice_reg_ok = 1;
        if (strstr(g_alice_rsp, "SYN0 ")) {
            char peer[128] = {0};
            char state[32] = {0};
            unsigned long sid = 0;
            if (sscanf(g_alice_rsp, "SYN0 %127s %lx %31s", peer, &sid, state) == 3) {
                g_alice_session_id = (uint32_t)sid;
                g_alice_syn0_ok = 1;
            }
        }
        printf("    [Alice] Received: %s\n", g_alice_rsp);
    } else if (type == WS_MSG_BINARY && len >= P2P_WSS_BIN_RPC_MIN_SZ && data[0] == P2P_WSS_BIN_RSP) {
        uint8_t *ptr = (uint8_t*)data + 1 + P2P_SESS_ID_SZ;
        uint16_t sid = nget_s(ptr);
        uint8_t code = data[P2P_WSS_BIN_RPC_MIN_SZ - 1];
        size_t data_len = len - P2P_WSS_BIN_RPC_MIN_SZ;
        size_t copy_len = data_len < sizeof(g_alice_rsp) - 1 ? data_len : sizeof(g_alice_rsp) - 1;
        if (copy_len > 0) memcpy((void*)g_alice_rsp, data + P2P_WSS_BIN_RPC_MIN_SZ, copy_len);
        g_alice_rsp[copy_len] = '\0';
        g_alice_got_rsp = 1;
        g_alice_rsp_sid = sid;
        g_alice_rsp_code = code;
        if (g_alice_rsp_count < MAX_TRACKED_RPC) {
            g_alice_rsp_sid_list[g_alice_rsp_count] = sid;
            g_alice_rsp_code_list[g_alice_rsp_count] = code;
        }
        g_alice_rsp_count++;
        printf("    [Alice] Received BIN RSP sid=%u code=%u len=%zu data=%s\n",
               sid, code, data_len, g_alice_rsp);
    }
}

static void ws_on_message_bob(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c; (void)user_data;
    if (type == WS_MSG_TEXT && len < sizeof(g_bob_req)) {
        memcpy((void*)g_bob_req, data, len);
        g_bob_req[len] = '\0';
        if (strstr(g_bob_req, "REG OK")) g_bob_reg_ok = 1;
        if (strstr(g_bob_req, "SYN0 ")) {
            char peer[128] = {0};
            char state[32] = {0};
            unsigned long sid = 0;
            if (sscanf(g_bob_req, "SYN0 %127s %lx %31s", peer, &sid, state) == 3) {
                g_bob_session_id = (uint32_t)sid;
                g_bob_syn0_ok = 1;
            }
        }
        printf("    [Bob] Received: %s\n", g_bob_req);
    } else if (type == WS_MSG_BINARY && len >= P2P_WSS_BIN_RPC_MIN_SZ && data[0] == P2P_WSS_BIN_REQ) {
        uint8_t *ptr = (uint8_t*)data + 1 + P2P_SESS_ID_SZ;
        g_bob_req_sid = nget_s(ptr);
        size_t data_len = len - P2P_WSS_BIN_RPC_MIN_SZ;
        size_t copy_len = data_len < sizeof(g_bob_req) - 1 ? data_len : sizeof(g_bob_req) - 1;
        if (copy_len > 0) memcpy((void*)g_bob_req, data + P2P_WSS_BIN_RPC_MIN_SZ, copy_len);
        g_bob_req[copy_len] = '\0';
        g_bob_got_req = 1;
        if (g_bob_req_count < MAX_TRACKED_RPC) {
            g_bob_req_sid_list[g_bob_req_count] = g_bob_req_sid;
        }
        g_bob_req_count++;
        printf("    [Bob] Received BIN REQ sid=%u len=%zu data=%s\n", g_bob_req_sid, data_len, g_bob_req);

        if (g_bob_auto_rsp) {
            uint8_t rsp_bin[128] = {0};
            size_t rsp_data_len = strlen(g_bob_auto_rsp_payload);
            rsp_bin[0] = P2P_WSS_BIN_RSP;
            nwrite_l(rsp_bin + 1, g_bob_session_id);
            nwrite_s(rsp_bin + 1 + P2P_SESS_ID_SZ, g_bob_req_sid);
            rsp_bin[1 + P2P_SESS_ID_SZ + 2] = 0;
            memcpy(rsp_bin + P2P_WSS_BIN_RPC_MIN_SZ, g_bob_auto_rsp_payload, rsp_data_len);
            ws_client_send_binary(c, rsp_bin, (uint16_t)(P2P_WSS_BIN_RPC_MIN_SZ + rsp_data_len));
        }
    }
}

static ws_client_t* ws_connect_alice(void) {
    ws_client_cfg_t cfg = {
        .on_open = NULL,
        .on_message = ws_on_message_alice,
        .on_close = NULL,
        .user_data = NULL,
        .extra_headers = NULL
    };
    printf("[ALICE] ws_connect_alice: host=%s port=%d\n", g_server_host, g_server_port);
    ws_client_t *client = ws_client_create(&cfg);
    if (!client) {
        printf("[ALICE] ws_client_create failed\n");
        return NULL;
    }
    int conn_ret = ws_client_connect(client, g_server_host, (uint16_t)g_server_port, "/");
    if (conn_ret != 0) {
        printf("[ALICE] ws_client_connect failed, ret=%d\n", conn_ret);
        ws_client_destroy(client);
        return NULL;
    }
    for (int i = 0; i < 3000; i++) {
        ws_client_update(client);
        int state = ws_client_state(client);
        if (state == WS_CLIENT_OPEN) {
            printf("[ALICE] ws_client_state=OPEN after %d ms\n", i);
            return client;
        }
        if (state == WS_CLIENT_ERROR) {
            printf("[ALICE] ws_client_state=ERROR at %d ms\n", i);
            break;
        }
        P_usleep(1000);
    }
    printf("[ALICE] ws_connect_alice handshake timeout or error\n");
    ws_client_destroy(client);
    return NULL;
}

static ws_client_t* ws_connect_bob(void) {
    ws_client_cfg_t cfg = {
        .on_open = NULL,
        .on_message = ws_on_message_bob,
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
    for (int i = 0; i < 3000; i++) {
        ws_client_update(client);
        if (ws_client_state(client) == WS_CLIENT_OPEN) return client;
        if (ws_client_state(client) == WS_CLIENT_ERROR) break;
        P_usleep(1000);
    }
    ws_client_destroy(client);
    return NULL;
}

static void test_msg_req_rsp_basic(void) {
    const char *TEST_NAME = "msg_req_rsp_basic";
    printf("\n--- Test: %s ---\n", TEST_NAME);

    reset_rpc_state();

    ws_client_t *alice = ws_connect_alice();
    ws_client_t *bob = ws_connect_bob();
    if (!alice || !bob) {
        if (alice) ws_client_destroy(alice);
        if (bob) ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }

    char cmd[128];
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1;

    // REG
    snprintf(cmd, sizeof(cmd), "REG alice_rpc %u\n", inst_alice);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "REG bob_rpc %u\n", inst_bob);
    ws_client_send_text(bob, cmd);
    int reg_ok = 0;
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_reg_ok && g_bob_reg_ok) { reg_ok = 1; break; }
        P_usleep(10000);
    }
    if (!reg_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "REG failed");
        return;
    }

    // SYN0 配对
    snprintf(cmd, sizeof(cmd), "SYN0 bob_rpc\n");
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "SYN0 alice_rpc\n");
    ws_client_send_text(bob, cmd);
    int syn0_ok = 0;
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_syn0_ok && g_bob_syn0_ok && g_alice_session_id != 0 && g_bob_session_id != 0) {
            syn0_ok = 1;
            break;
        }
        P_usleep(10000);
    }
    if (!syn0_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "SYN0 failed");
        return;
    }

    // Alice 发送二进制 REQ
    uint8_t req_bin[64] = {0};
    const char *req_data = "hello_from_alice";
    size_t req_data_len = strlen(req_data);
    req_bin[0] = P2P_WSS_BIN_REQ;
    nwrite_l(req_bin + 1, g_alice_session_id);
    nwrite_s(req_bin + 1 + P2P_SESS_ID_SZ, 42);
    req_bin[1 + P2P_SESS_ID_SZ + 2] = 1;
    memcpy(req_bin + P2P_WSS_BIN_RPC_MIN_SZ, req_data, req_data_len);
    ws_client_send_binary(alice, req_bin, (uint16_t)(P2P_WSS_BIN_RPC_MIN_SZ + req_data_len));

    // Bob 等待收到 REQ 并自动回复 RSP
    int wait = 0;
    while (!g_bob_got_req && wait++ < 200) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }
    if (!g_bob_got_req) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive REQ");
        return;
    }
    // Bob 回复二进制 RSP
    uint8_t rsp_bin[64] = {0};
    const char *rsp_data = "world_from_bob";
    size_t rsp_data_len = strlen(rsp_data);
    rsp_bin[0] = P2P_WSS_BIN_RSP;
    nwrite_l(rsp_bin + 1, g_bob_session_id);
    nwrite_s(rsp_bin + 1 + P2P_SESS_ID_SZ, g_bob_req_sid ? g_bob_req_sid : 42);
    rsp_bin[1 + P2P_SESS_ID_SZ + 2] = 0;
    memcpy(rsp_bin + P2P_WSS_BIN_RPC_MIN_SZ, rsp_data, rsp_data_len);
    ws_client_send_binary(bob, rsp_bin, (uint16_t)(P2P_WSS_BIN_RPC_MIN_SZ + rsp_data_len));

    // Alice 等待收到 RSP
    wait = 0;
    while (!g_alice_got_rsp && wait++ < 200) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }
    ws_client_close(alice, 1000);
    ws_client_close(bob, 1000);
    ws_client_destroy(alice);
    ws_client_destroy(bob);
    if (!g_alice_got_rsp) {
        TEST_FAIL(TEST_NAME, "Alice did not receive RSP");
        return;
    }
    if (!strstr(g_alice_rsp, "world_from_bob")) {
        TEST_FAIL(TEST_NAME, "Alice got wrong RSP content");
        return;
    }
    TEST_PASS(TEST_NAME);
}

static void test_msg_req_timeout(void) {
    const char *TEST_NAME = "msg_req_timeout";
    printf("\n--- Test: %s ---\n", TEST_NAME);

    reset_rpc_state();

    ws_client_t *alice = ws_connect_alice();
    ws_client_t *bob = ws_connect_bob();
    if (!alice || !bob) {
        if (alice) ws_client_destroy(alice);
        if (bob) ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }

    char cmd[128];
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1;

    snprintf(cmd, sizeof(cmd), "REG alice_timeout %u\n", inst_alice);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "REG bob_timeout %u\n", inst_bob);
    ws_client_send_text(bob, cmd);

    int reg_ok = 0;
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_reg_ok && g_bob_reg_ok) { reg_ok = 1; break; }
        P_usleep(10000);
    }
    if (!reg_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "REG failed");
        return;
    }

    snprintf(cmd, sizeof(cmd), "SYN0 bob_timeout\n");
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "SYN0 alice_timeout\n");
    ws_client_send_text(bob, cmd);

    int syn0_ok = 0;
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_syn0_ok && g_bob_syn0_ok && g_alice_session_id != 0 && g_bob_session_id != 0) {
            syn0_ok = 1;
            break;
        }
        P_usleep(10000);
    }
    if (!syn0_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "SYN0 failed");
        return;
    }

    uint16_t req_sid = 100;
    uint8_t req_bin[64] = {0};
    const char *req_data = "timeout_probe";
    size_t req_data_len = strlen(req_data);
    req_bin[0] = P2P_WSS_BIN_REQ;
    nwrite_l(req_bin + 1, g_alice_session_id);
    nwrite_s(req_bin + 1 + P2P_SESS_ID_SZ, req_sid);
    req_bin[1 + P2P_SESS_ID_SZ + 2] = 1;
    memcpy(req_bin + P2P_WSS_BIN_RPC_MIN_SZ, req_data, req_data_len);
    ws_client_send_binary(alice, req_bin, (uint16_t)(P2P_WSS_BIN_RPC_MIN_SZ + req_data_len));

    int bob_got_req = 0;
    for (int i = 0; i < 200; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_bob_req_count > 0) { bob_got_req = 1; break; }
        P_usleep(10000);
    }
    if (!bob_got_req) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive REQ");
        return;
    }

    int got_timeout_rsp = 0;
    for (int i = 0; i < 900; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_rsp_count > 0) {
            got_timeout_rsp = 1;
            break;
        }
        P_usleep(10000);
    }

    ws_client_close(alice, 1000);
    ws_client_close(bob, 1000);
    ws_client_destroy(alice);
    ws_client_destroy(bob);

    if (!got_timeout_rsp) {
        TEST_FAIL(TEST_NAME, "Alice did not receive timeout RSP");
        return;
    }
    if (g_alice_rsp_sid != req_sid) {
        char reason[128];
        snprintf(reason, sizeof(reason), "wrong sid: expected=%u got=%u", req_sid, g_alice_rsp_sid);
        TEST_FAIL(TEST_NAME, reason);
        return;
    }
    if (g_alice_rsp_code != P2P_RPC_ERR_TIMEOUT) {
        char reason[128];
        snprintf(reason, sizeof(reason), "wrong code: expected=0x%02X got=0x%02X", P2P_RPC_ERR_TIMEOUT, g_alice_rsp_code);
        TEST_FAIL(TEST_NAME, reason);
        return;
    }

    TEST_PASS(TEST_NAME);
}

static void test_msg_req_peer_offline(void) {
    const char *TEST_NAME = "msg_req_peer_offline";
    printf("\n--- Test: %s ---\n", TEST_NAME);

    reset_rpc_state();

    ws_client_t *alice = ws_connect_alice();
    ws_client_t *bob = ws_connect_bob();
    if (!alice || !bob) {
        if (alice) ws_client_destroy(alice);
        if (bob) ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }

    char cmd[128];
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1;

    snprintf(cmd, sizeof(cmd), "REG alice_offline_test %u\n", inst_alice);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "REG bob_offline_test %u\n", inst_bob);
    ws_client_send_text(bob, cmd);

    int reg_ok = 0;
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_reg_ok && g_bob_reg_ok) { reg_ok = 1; break; }
        P_usleep(10000);
    }
    if (!reg_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "REG failed");
        return;
    }

    snprintf(cmd, sizeof(cmd), "SYN0 bob_offline_test\n");
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "SYN0 alice_offline_test\n");
    ws_client_send_text(bob, cmd);

    int syn0_ok = 0;
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_syn0_ok && g_bob_syn0_ok && g_alice_session_id != 0 && g_bob_session_id != 0) {
            syn0_ok = 1;
            break;
        }
        P_usleep(10000);
    }
    if (!syn0_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "SYN0 failed");
        return;
    }

    ws_client_close(bob, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(1000);
    }
    ws_client_destroy(bob);

    uint16_t req_sid = 77;
    uint8_t req_bin[64] = {0};
    const char *req_data = "peer_offline_probe";
    size_t req_data_len = strlen(req_data);
    req_bin[0] = P2P_WSS_BIN_REQ;
    nwrite_l(req_bin + 1, g_alice_session_id);
    nwrite_s(req_bin + 1 + P2P_SESS_ID_SZ, req_sid);
    req_bin[1 + P2P_SESS_ID_SZ + 2] = 1;
    memcpy(req_bin + P2P_WSS_BIN_RPC_MIN_SZ, req_data, req_data_len);
    ws_client_send_binary(alice, req_bin, (uint16_t)(P2P_WSS_BIN_RPC_MIN_SZ + req_data_len));

    int got_offline_rsp = 0;
    for (int i = 0; i < 300; i++) {
        ws_client_update(alice);
        if (g_alice_rsp_count > 0) {
            got_offline_rsp = 1;
            break;
        }
        P_usleep(10000);
    }

    ws_client_close(alice, 1000);
    ws_client_destroy(alice);

    if (!got_offline_rsp) {
        TEST_FAIL(TEST_NAME, "Alice did not receive peer-offline RSP");
        return;
    }
    if (g_alice_rsp_sid != req_sid) {
        char reason[128];
        snprintf(reason, sizeof(reason), "wrong sid: expected=%u got=%u", req_sid, g_alice_rsp_sid);
        TEST_FAIL(TEST_NAME, reason);
        return;
    }
    if (g_alice_rsp_code != P2P_RPC_ERR_PEER_OFF) {
        char reason[128];
        snprintf(reason, sizeof(reason), "wrong code: expected=0x%02X got=0x%02X", P2P_RPC_ERR_PEER_OFF, g_alice_rsp_code);
        TEST_FAIL(TEST_NAME, reason);
        return;
    }

    TEST_PASS(TEST_NAME);
}

static void test_msg_multiple_requests(void) {
    const char *TEST_NAME = "msg_multiple_requests";
    printf("\n--- Test: %s ---\n", TEST_NAME);

    reset_rpc_state();

    ws_client_t *alice = ws_connect_alice();
    ws_client_t *bob = ws_connect_bob();

    if (!alice || !bob) {
        if (alice) ws_client_destroy(alice);
        if (bob) ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    char cmd[128];
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1;
    
    // REG
    snprintf(cmd, sizeof(cmd), "REG alice_multi %u\n", inst_alice);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "REG bob_multi %u\n", inst_bob);
    ws_client_send_text(bob, cmd);

    int reg_ok = 0;
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_reg_ok && g_bob_reg_ok) { reg_ok = 1; break; }
        P_usleep(10000);
    }
    if (!reg_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "REG failed");
        return;
    }
    
    // SYN0 配对
    snprintf(cmd, sizeof(cmd), "SYN0 bob_multi\n");
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "SYN0 alice_multi\n");
    ws_client_send_text(bob, cmd);

    int syn0_ok = 0;
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_syn0_ok && g_bob_syn0_ok && g_alice_session_id != 0 && g_bob_session_id != 0) {
            syn0_ok = 1;
            break;
        }
        P_usleep(10000);
    }
    if (!syn0_ok) {
        ws_client_close(alice, 1000);
        ws_client_close(bob, 1000);
        ws_client_destroy(alice);
        ws_client_destroy(bob);
        TEST_FAIL(TEST_NAME, "SYN0 failed");
        return;
    }

    g_bob_auto_rsp = 1;
    g_bob_auto_rsp_payload = "multi_rsp";

    const uint16_t req_sid_list[] = {200, 201, 202};
    const int req_cnt = (int)(sizeof(req_sid_list) / sizeof(req_sid_list[0]));
    for (int i = 0; i < req_cnt; i++) {
        char req_data[32];
        uint8_t req_bin[64] = {0};
        int n = snprintf(req_data, sizeof(req_data), "multi_req_%d", i + 1);
        req_bin[0] = P2P_WSS_BIN_REQ;
        nwrite_l(req_bin + 1, g_alice_session_id);
        nwrite_s(req_bin + 1 + P2P_SESS_ID_SZ, req_sid_list[i]);
        req_bin[1 + P2P_SESS_ID_SZ + 2] = 1;
        memcpy(req_bin + P2P_WSS_BIN_RPC_MIN_SZ, req_data, (size_t)n);
        ws_client_send_binary(alice, req_bin, (uint16_t)(P2P_WSS_BIN_RPC_MIN_SZ + n));

        int one_done = 0;
        for (int j = 0; j < 200; j++) {
            ws_client_update(alice);
            ws_client_update(bob);
            if (g_bob_req_count >= (i + 1) && g_alice_rsp_count >= (i + 1)) {
                one_done = 1;
                break;
            }
            P_usleep(10000);
        }
        if (!one_done) {
            ws_client_close(alice, 1000);
            ws_client_close(bob, 1000);
            ws_client_destroy(alice);
            ws_client_destroy(bob);
            TEST_FAIL(TEST_NAME, "one REQ/RSP round did not complete");
            return;
        }
        if (g_bob_req_sid_list[i] != req_sid_list[i]) {
            char reason[128];
            snprintf(reason, sizeof(reason), "REQ order mismatch at %d: expected sid=%u got=%u", i, req_sid_list[i], g_bob_req_sid_list[i]);
            ws_client_close(alice, 1000);
            ws_client_close(bob, 1000);
            ws_client_destroy(alice);
            ws_client_destroy(bob);
            TEST_FAIL(TEST_NAME, reason);
            return;
        }
        if (g_alice_rsp_sid_list[i] != req_sid_list[i]) {
            char reason[128];
            snprintf(reason, sizeof(reason), "RSP order mismatch at %d: expected sid=%u got=%u", i, req_sid_list[i], g_alice_rsp_sid_list[i]);
            ws_client_close(alice, 1000);
            ws_client_close(bob, 1000);
            ws_client_destroy(alice);
            ws_client_destroy(bob);
            TEST_FAIL(TEST_NAME, reason);
            return;
        }
        if (g_alice_rsp_code_list[i] != 0) {
            char reason[128];
            snprintf(reason, sizeof(reason), "RSP code mismatch at %d: expected=0 got=%u", i, g_alice_rsp_code_list[i]);
            ws_client_close(alice, 1000);
            ws_client_close(bob, 1000);
            ws_client_destroy(alice);
            ws_client_destroy(bob);
            TEST_FAIL(TEST_NAME, reason);
            return;
        }
    }

    // 清理
    ws_client_close(alice, 1000);
    ws_client_close(bob, 1000);
    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(1000);
    }
    ws_client_destroy(alice);
    ws_client_destroy(bob);

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
    
    printf("=== WSS RPC/Message Tests ===\n");
    printf("Server: %s\n", server_path ? server_path : "(external)");
    printf("Address: ws://%s:%d\n\n", g_server_host, g_server_port);
    
    // 忽略 SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    // 启动 server
    if (server_path) {
        printf("[*] Starting server (WSS mode with MSG support)...\n");
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", g_server_port);

        g_server_pid = fork();
        if (g_server_pid == 0) {
            execl(server_path, server_path, "--cn", "-p", port_str, "--ws", "--msg", NULL);
            _exit(127);
        }
        printf("    Server PID: %d\n", g_server_pid);
        P_usleep(500 * 1000);
    }
    
    printf("\n[*] Running tests...\n");
    
    test_msg_req_rsp_basic();
    test_msg_req_timeout();
    test_msg_req_peer_offline();
    test_msg_multiple_requests();
    
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
