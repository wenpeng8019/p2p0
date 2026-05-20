/*
 * test_wss_pkt.c - WSS 协议 PKT 消息单元测试
 *
 * 覆盖 P2P_WSS_BIN_PKT 的基础转发路径：
 * - DATA 转发
 * - ACK 转发
 * - CRYPTO 转发
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

typedef struct {
    int received;
    uint32_t session_id;
    uint8_t inner_type;
    uint8_t flags;
    uint16_t seq;
    uint16_t ack_seq;
    uint32_t sack;
    uint8_t data[1024];
    size_t data_len;
} wss_pkt_t;

static int can_bind_server_port(uint16_t port) {
    sock_t udp_fd = P_INVALID_SOCKET;
    sock_t tcp_fd = P_INVALID_SOCKET;
    struct sockaddr_in addr;
    int ok = 0;

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (udp_fd == P_INVALID_SOCKET || tcp_fd == P_INVALID_SOCKET) goto done;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) goto done;
    if (bind(tcp_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) goto done;

    ok = 1;

done:
    if (udp_fd != P_INVALID_SOCKET) P_sock_close(udp_fd);
    if (tcp_fd != P_INVALID_SOCKET) P_sock_close(tcp_fd);
    return ok;
}

static uint16_t choose_server_port(void) {
    uint32_t seed = (uint32_t)P_tick_us();
    uint16_t start = (uint16_t)(20000u + (seed % 20000u));

    for (uint32_t i = 0; i < 20000u; i++) {
        uint16_t port = (uint16_t)(20000u + ((start - 20000u + i) % 20000u));
        if (can_bind_server_port(port)) return port;
    }

    return (uint16_t)DEFAULT_SERVER_PORT;
}

static volatile int g_alice_reg_ok = 0;
static volatile int g_bob_reg_ok = 0;
static volatile int g_alice_syn0_ok = 0;
static volatile int g_bob_syn0_ok = 0;
static uint32_t g_alice_session_id = 0;
static uint32_t g_bob_session_id = 0;

static volatile int g_alice_status_count = 0;
static uint32_t g_alice_status_session_id = 0;
static char g_alice_status_type[16];
static uint8_t g_alice_status_code = 0xFF;

static volatile int g_alice_pkt_count = 0;
static volatile int g_bob_pkt_count = 0;
static wss_pkt_t g_alice_pkt;
static wss_pkt_t g_bob_pkt;

static void reset_state(void) {
    g_alice_reg_ok = 0;
    g_bob_reg_ok = 0;
    g_alice_syn0_ok = 0;
    g_bob_syn0_ok = 0;
    g_alice_session_id = 0;
    g_bob_session_id = 0;
    g_alice_status_count = 0;
    g_alice_status_session_id = 0;
    g_alice_status_type[0] = '\0';
    g_alice_status_code = 0xFF;
    g_alice_pkt_count = 0;
    g_bob_pkt_count = 0;
    memset(&g_alice_pkt, 0, sizeof(g_alice_pkt));
    memset(&g_bob_pkt, 0, sizeof(g_bob_pkt));
}

static void parse_text_message(const uint8_t *data, size_t len,
                               volatile int *reg_ok, volatile int *syn0_ok,
                               uint32_t *session_id,
                               volatile int *status_count,
                               uint32_t *status_session_id,
                               char *status_type, uint8_t *status_code) {
    char msg[512];
    size_t copy_len = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
    unsigned long sid_hex = 0;
    unsigned status_hex = 0;
    char peer[128] = {0};
    char state[32] = {0};
    char req_type[16] = {0};

    memcpy(msg, data, copy_len);
    msg[copy_len] = '\0';

    if (reg_ok && strstr(msg, "REG OK")) {
        *reg_ok = 1;
    }
    if (syn0_ok && session_id && sscanf(msg, "SYN0 %127s %lx %31s", peer, &sid_hex, state) == 3) {
        *session_id = (uint32_t)sid_hex;
        *syn0_ok = 1;
    }
    if (status_count && status_session_id && status_type && status_code &&
        sscanf(msg, "STA %lx %15s %x", &sid_hex, req_type, &status_hex) == 3) {
        *status_session_id = (uint32_t)sid_hex;
        strncpy(status_type, req_type, 15);
        status_type[15] = '\0';
        *status_code = (uint8_t)status_hex;
        (*status_count)++;
    }
}

static void parse_binary_pkt(const uint8_t *data, size_t len, wss_pkt_t *pkt, volatile int *pkt_count) {
    if (len < P2P_WSS_BIN_PKT_MIN_SZ || data[0] != P2P_WSS_BIN_PKT) {
        return;
    }

    memset(pkt, 0, sizeof(*pkt));
    pkt->received = 1;
    pkt->session_id = nget_l((uint8_t*)data + 1);
    pkt->inner_type = data[1 + P2P_SESS_ID_SZ];
    pkt->flags = data[1 + P2P_SESS_ID_SZ + 1];
    pkt->seq = nget_s((uint8_t*)data + 1 + P2P_SESS_ID_SZ + 2);

    if (pkt->inner_type == P2P_PKT_ACK && len >= P2P_WSS_BIN_PKT_MIN_SZ + 6) {
        pkt->ack_seq = nget_s((uint8_t*)data + P2P_WSS_BIN_PKT_MIN_SZ);
        pkt->sack = nget_l((uint8_t*)data + P2P_WSS_BIN_PKT_MIN_SZ + 2);
    } else {
        pkt->data_len = len - P2P_WSS_BIN_PKT_MIN_SZ;
        if (pkt->data_len > sizeof(pkt->data)) pkt->data_len = sizeof(pkt->data);
        if (pkt->data_len > 0) memcpy(pkt->data, data + P2P_WSS_BIN_PKT_MIN_SZ, pkt->data_len);
    }

    (*pkt_count)++;
}

static void ws_on_message_alice(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c;
    (void)user_data;

    if (type == WS_MSG_TEXT) {
        parse_text_message(data, len,
                           &g_alice_reg_ok, &g_alice_syn0_ok, &g_alice_session_id,
                           &g_alice_status_count, &g_alice_status_session_id,
                           g_alice_status_type, &g_alice_status_code);
    } else if (type == WS_MSG_BINARY) {
        parse_binary_pkt(data, len, &g_alice_pkt, &g_alice_pkt_count);
    }
}

static void ws_on_message_bob(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c;
    (void)user_data;

    if (type == WS_MSG_TEXT) {
        parse_text_message(data, len,
                           &g_bob_reg_ok, &g_bob_syn0_ok, &g_bob_session_id,
                           NULL, NULL, NULL, NULL);
    } else if (type == WS_MSG_BINARY) {
        parse_binary_pkt(data, len, &g_bob_pkt, &g_bob_pkt_count);
    }
}

static ws_client_t* ws_connect_with_handler(ws_client_on_message_cb on_message) {
    ws_client_cfg_t cfg = {
        .on_open = NULL,
        .on_message = on_message,
        .on_close = NULL,
        .user_data = NULL,
        .extra_headers = NULL,
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

static void close_clients(ws_client_t *alice, ws_client_t *bob) {
    if (alice) ws_client_close(alice, 1000);
    if (bob) ws_client_close(bob, 1000);
    for (int i = 0; i < 100; i++) {
        if (alice) ws_client_update(alice);
        if (bob) ws_client_update(bob);
        P_usleep(1000);
    }
    if (alice) ws_client_destroy(alice);
    if (bob) ws_client_destroy(bob);
}

static int establish_pair(ws_client_t **alice_out, ws_client_t **bob_out,
                          const char *alice_id, const char *bob_id) {
    char cmd[128];
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1;
    ws_client_t *alice = ws_connect_with_handler(ws_on_message_alice);
    ws_client_t *bob = ws_connect_with_handler(ws_on_message_bob);

    if (!alice || !bob) {
        close_clients(alice, bob);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "REG %s %u\n", alice_id, inst_alice);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "REG %s %u\n", bob_id, inst_bob);
    ws_client_send_text(bob, cmd);

    int reg_ok = 0;
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_reg_ok && g_bob_reg_ok) {
            reg_ok = 1;
            break;
        }
        P_usleep(10000);
    }
    if (!reg_ok) {
        close_clients(alice, bob);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "SYN0 %s\n", bob_id);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "SYN0 %s\n", alice_id);
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
        close_clients(alice, bob);
        return 0;
    }

    *alice_out = alice;
    *bob_out = bob;
    return 1;
}

static size_t build_pkt_data(uint8_t *buf, size_t buf_sz, uint32_t session_id,
                             uint8_t pkt_type, uint8_t flags, uint16_t seq,
                             const uint8_t *data, size_t data_len) {
    size_t total = P2P_WSS_BIN_PKT_MIN_SZ + data_len;
    if (buf_sz < total) return 0;
    buf[0] = P2P_WSS_BIN_PKT;
    nwrite_l(buf + 1, session_id);
    buf[1 + P2P_SESS_ID_SZ] = pkt_type;
    buf[1 + P2P_SESS_ID_SZ + 1] = flags;
    nwrite_s(buf + 1 + P2P_SESS_ID_SZ + 2, seq);
    if (data_len > 0) memcpy(buf + P2P_WSS_BIN_PKT_MIN_SZ, data, data_len);
    return total;
}

static size_t build_pkt_ack(uint8_t *buf, size_t buf_sz, uint32_t session_id,
                            uint16_t ack_seq, uint32_t sack) {
    size_t total = P2P_WSS_BIN_PKT_MIN_SZ + 6;
    if (buf_sz < total) return 0;
    buf[0] = P2P_WSS_BIN_PKT;
    nwrite_l(buf + 1, session_id);
    buf[1 + P2P_SESS_ID_SZ] = P2P_PKT_ACK;
    buf[1 + P2P_SESS_ID_SZ + 1] = 0;
    nwrite_s(buf + 1 + P2P_SESS_ID_SZ + 2, 0);
    nwrite_s(buf + P2P_WSS_BIN_PKT_MIN_SZ, ack_seq);
    nwrite_l(buf + P2P_WSS_BIN_PKT_MIN_SZ + 2, sack);
    return total;
}

static int wait_for_pkt_and_status(ws_client_t *alice, ws_client_t *bob) {
    for (int i = 0; i < 200; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_bob_pkt_count > 0 && g_alice_status_count > 0) return 1;
        P_usleep(10000);
    }
    return 0;
}

static void test_wss_pkt_data_forwarded(void) {
    const char *TEST_NAME = "wss_pkt_data_forwarded";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;
    uint8_t pkt[128];
    const uint8_t payload[] = "Hello, Bob over WSS PKT";
    size_t pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();

    if (!establish_pair(&alice, &bob, "wss_pkt_data_alice", "wss_pkt_data_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }

    g_alice_status_count = 0;
    pkt_len = build_pkt_data(pkt, sizeof(pkt), g_alice_session_id, P2P_PKT_DATA, 0, 1, payload, sizeof(payload));
    ws_client_send_binary(alice, pkt, (uint16_t)pkt_len);

    if (!wait_for_pkt_and_status(alice, bob)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting pkt/status");
        return;
    }

    if (!g_bob_pkt.received || g_bob_pkt.inner_type != P2P_PKT_DATA || g_bob_pkt.session_id != g_bob_session_id) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received wrong DATA packet metadata");
        return;
    }
    if (g_bob_pkt.seq != 1 || g_bob_pkt.data_len != sizeof(payload) || memcmp(g_bob_pkt.data, payload, sizeof(payload)) != 0) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received wrong DATA payload");
        return;
    }
    if (strcmp(g_alice_status_type, "PKT") != 0 || g_alice_status_code != P2P_CODE_READY || g_alice_status_session_id != g_alice_session_id) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive PKT READY status");
        return;
    }

    close_clients(alice, bob);
    TEST_PASS(TEST_NAME);
}

static void test_wss_pkt_ack_forwarded(void) {
    const char *TEST_NAME = "wss_pkt_ack_forwarded";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;
    uint8_t pkt[64];
    size_t pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();

    if (!establish_pair(&alice, &bob, "wss_pkt_ack_alice", "wss_pkt_ack_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }

    g_alice_status_count = 0;
    pkt_len = build_pkt_ack(pkt, sizeof(pkt), g_alice_session_id, 42, 0x01020304u);
    ws_client_send_binary(alice, pkt, (uint16_t)pkt_len);

    if (!wait_for_pkt_and_status(alice, bob)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting ack/status");
        return;
    }

    if (!g_bob_pkt.received || g_bob_pkt.inner_type != P2P_PKT_ACK || g_bob_pkt.session_id != g_bob_session_id) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received wrong ACK packet metadata");
        return;
    }
    if (g_bob_pkt.ack_seq != 42 || g_bob_pkt.sack != 0x01020304u) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received wrong ACK payload");
        return;
    }
    if (strcmp(g_alice_status_type, "PKT") != 0 || g_alice_status_code != P2P_CODE_READY) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive ACK READY status");
        return;
    }

    close_clients(alice, bob);
    TEST_PASS(TEST_NAME);
}

static void test_wss_pkt_crypto_forwarded(void) {
    const char *TEST_NAME = "wss_pkt_crypto_forwarded";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;
    uint8_t pkt[64];
    const uint8_t payload[] = {0x16, 0xfe, 0xfd, 0x00, 0x01, 0x02, 0x03, 0x04};
    size_t pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();

    if (!establish_pair(&alice, &bob, "wss_pkt_crypto_alice", "wss_pkt_crypto_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }

    g_alice_status_count = 0;
    pkt_len = build_pkt_data(pkt, sizeof(pkt), g_alice_session_id, P2P_PKT_CRYPTO, 0, 0, payload, sizeof(payload));
    ws_client_send_binary(alice, pkt, (uint16_t)pkt_len);

    if (!wait_for_pkt_and_status(alice, bob)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting crypto/status");
        return;
    }

    if (!g_bob_pkt.received || g_bob_pkt.inner_type != P2P_PKT_CRYPTO || g_bob_pkt.session_id != g_bob_session_id) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received wrong CRYPTO packet metadata");
        return;
    }
    if (g_bob_pkt.data_len != sizeof(payload) || memcmp(g_bob_pkt.data, payload, sizeof(payload)) != 0) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received wrong CRYPTO payload");
        return;
    }
    if (strcmp(g_alice_status_type, "PKT") != 0 || g_alice_status_code != P2P_CODE_READY) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive CRYPTO READY status");
        return;
    }

    close_clients(alice, bob);
    TEST_PASS(TEST_NAME);
}

#endif

int main(int argc, char *argv[]) {
#ifndef WITH_WS
    printf("WSS tests require WITH_WS build flag\n");
    return 0;
#else
    const char *server_path = NULL;

    if (argc >= 2) server_path = argv[1];
    if (argc > 2) g_server_port = atoi(argv[2]);
    else if (server_path) g_server_port = choose_server_port();

    printf("=== WSS PKT Tests ===\n");
    printf("Server: %s\n", server_path ? server_path : "(external)");
    printf("Address: ws://%s:%d\n\n", g_server_host, g_server_port);

    signal(SIGPIPE, SIG_IGN);

    if (server_path) {
        char port_str[16];
        printf("[*] Starting server (WSS mode)...\n");
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
    test_wss_pkt_data_forwarded();
    test_wss_pkt_ack_forwarded();
    test_wss_pkt_crypto_forwarded();

    if (g_server_pid > 0) {
        int status;
        printf("\n[*] Terminating server...\n");
        kill(g_server_pid, SIGTERM);
        waitpid(g_server_pid, &status, 0);
    }

    printf("\n===== Test Results =====\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("========================\n");

    return g_tests_failed > 0 ? 1 : 0;
#endif
}
