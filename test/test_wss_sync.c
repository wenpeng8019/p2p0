/*
 * test_wss_sync.c - WSS SYNC 协议单元测试
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
    uint32_t session_id;
    uint8_t sid;
    uint8_t is_confirm;
    uint8_t is_busy;
    char payload[256];
} wss_sync_event_t;

typedef struct {
    uint32_t session_id;
    char type[16];
    uint8_t code;
} wss_status_event_t;

static volatile int g_alice_reg_ok = 0;
static volatile int g_bob_reg_ok = 0;
static volatile int g_alice_syn0_ok = 0;
static volatile int g_bob_syn0_ok = 0;
static uint32_t g_alice_session_id = 0;
static uint32_t g_bob_session_id = 0;

static wss_sync_event_t g_alice_sync_events[16];
static wss_sync_event_t g_bob_sync_events[16];
static volatile int g_alice_sync_count = 0;
static volatile int g_bob_sync_count = 0;

static wss_status_event_t g_alice_status_events[16];
static volatile int g_alice_status_count = 0;

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

static void reset_state(void) {
    g_alice_reg_ok = 0;
    g_bob_reg_ok = 0;
    g_alice_syn0_ok = 0;
    g_bob_syn0_ok = 0;
    g_alice_session_id = 0;
    g_bob_session_id = 0;
    g_alice_sync_count = 0;
    g_bob_sync_count = 0;
    g_alice_status_count = 0;
    memset(g_alice_sync_events, 0, sizeof(g_alice_sync_events));
    memset(g_bob_sync_events, 0, sizeof(g_bob_sync_events));
    memset(g_alice_status_events, 0, sizeof(g_alice_status_events));
}

static int parse_sync_event(const uint8_t *data, size_t len, wss_sync_event_t *event) {
    static const size_t kSyncPrefixLen = sizeof(P2P_WSS_RSP_SYNC) - 1u;
    char msg[512];
    char *headline;
    char *payload;
    char *saveptr = NULL;
    char *token;
    unsigned long sess = 0;
    unsigned long sid = 0;

    if (len < kSyncPrefixLen || memcmp(data, P2P_WSS_RSP_SYNC, kSyncPrefixLen) != 0) return 0;

    memset(event, 0, sizeof(*event));
    if (len >= sizeof(msg)) len = sizeof(msg) - 1;
    memcpy(msg, data, len);
    msg[len] = '\0';

    headline = msg + kSyncPrefixLen;
    payload = strchr(headline, '\n');
    if (payload) {
        *payload = '\0';
        payload++;
        strncpy(event->payload, payload, sizeof(event->payload) - 1);
        event->payload[sizeof(event->payload) - 1] = '\0';
    }

    token = strtok_r(headline, " ", &saveptr);
    if (!token) return 0;
    sess = strtoul(token, NULL, 16);
    token = strtok_r(NULL, " ", &saveptr);
    if (!token) return 0;
    sid = strtoul(token, NULL, 16);

    event->session_id = (uint32_t)sess;
    event->sid = (uint8_t)sid;

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
        if (strcmp(token, "confirm") == 0) event->is_confirm = 1;
        else if (strcmp(token, "busy") == 0) event->is_busy = 1;
    }
    return 1;
}

static void parse_text_message(const uint8_t *data, size_t len,
                               volatile int *reg_ok, volatile int *syn0_ok,
                               uint32_t *session_id,
                               wss_sync_event_t *sync_events, volatile int *sync_count,
                               wss_status_event_t *status_events, volatile int *status_count) {
    char msg[512];
    size_t copy_len = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
    unsigned long sid_hex = 0;
    unsigned status_hex = 0;
    char peer[128] = {0};
    char state[32] = {0};
    char req_type[16] = {0};
    wss_sync_event_t event;

    memcpy(msg, data, copy_len);
    msg[copy_len] = '\0';

    if (reg_ok && strstr(msg, "REG OK")) {
        *reg_ok = 1;
        return;
    }
    if (syn0_ok && session_id && sscanf(msg, "SYN0 %127s %lx %31s", peer, &sid_hex, state) == 3) {
        *session_id = (uint32_t)sid_hex;
        *syn0_ok = 1;
        return;
    }
    if (status_events && status_count && sscanf(msg, "STA %lx %15s %x", &sid_hex, req_type, &status_hex) == 3) {
        int idx = *status_count;
        if (idx < 16) {
            status_events[idx].session_id = (uint32_t)sid_hex;
            strncpy(status_events[idx].type, req_type, sizeof(status_events[idx].type) - 1);
            status_events[idx].type[sizeof(status_events[idx].type) - 1] = '\0';
            status_events[idx].code = (uint8_t)status_hex;
            *status_count = idx + 1;
        }
        return;
    }
    if (sync_events && sync_count && parse_sync_event(data, len, &event)) {
        int idx = *sync_count;
        if (idx < 16) {
            sync_events[idx] = event;
            *sync_count = idx + 1;
        }
    }
}

static void ws_on_message_alice(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c;
    (void)user_data;
    if (type == WS_MSG_TEXT) {
        parse_text_message(data, len,
                           &g_alice_reg_ok, &g_alice_syn0_ok, &g_alice_session_id,
                           g_alice_sync_events, &g_alice_sync_count,
                           g_alice_status_events, &g_alice_status_count);
    }
}

static void ws_on_message_bob(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *user_data) {
    (void)c;
    (void)user_data;
    if (type == WS_MSG_TEXT) {
        parse_text_message(data, len,
                           &g_bob_reg_ok, &g_bob_syn0_ok, &g_bob_session_id,
                           g_bob_sync_events, &g_bob_sync_count,
                           NULL, NULL);
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

    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_reg_ok && g_bob_reg_ok) break;
        P_usleep(10000);
    }
    if (!g_alice_reg_ok || !g_bob_reg_ok) {
        close_clients(alice, bob);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "SYN0 %s\n", bob_id);
    ws_client_send_text(alice, cmd);
    snprintf(cmd, sizeof(cmd), "SYN0 %s\n", alice_id);
    ws_client_send_text(bob, cmd);

    for (int i = 0; i < 100; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_syn0_ok && g_bob_syn0_ok && g_alice_session_id && g_bob_session_id) break;
        P_usleep(10000);
    }
    if (!g_alice_syn0_ok || !g_bob_syn0_ok || !g_alice_session_id || !g_bob_session_id) {
        close_clients(alice, bob);
        return 0;
    }

    *alice_out = alice;
    *bob_out = bob;
    return 1;
}

static int wait_for_counts(ws_client_t *alice, ws_client_t *bob,
                           int alice_sync_count, int bob_sync_count, int alice_status_count) {
    for (int i = 0; i < 200; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_alice_sync_count >= alice_sync_count
            && g_bob_sync_count >= bob_sync_count
            && g_alice_status_count >= alice_status_count) return 1;
        P_usleep(10000);
    }
    return 0;
}

static int expect_bob_sync_unchanged(ws_client_t *alice, ws_client_t *bob, int count) {
    for (int i = 0; i < 40; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        if (g_bob_sync_count != count) return 0;
        P_usleep(10000);
    }
    return 1;
}

static void send_sync_text(ws_client_t *client, uint32_t session_id, uint8_t sid, const char *payload);
static void send_sync_confirm_text(ws_client_t *client, uint32_t session_id, uint8_t sid);

static int wait_for_forwarded_sid(ws_client_t *alice, ws_client_t *bob,
                                  volatile int *count, wss_sync_event_t *events, uint8_t sid) {
    for (int i = 0; i < 200; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        for (int j = 0; j < *count; j++) {
            if (!events[j].is_confirm && !events[j].is_busy && events[j].sid == sid) return 1;
        }
        P_usleep(10000);
    }
    return 0;
}

static void clear_runtime_events(void) {
    g_alice_sync_count = 0;
    g_bob_sync_count = 0;
    g_alice_status_count = 0;
    memset(g_alice_sync_events, 0, sizeof(g_alice_sync_events));
    memset(g_bob_sync_events, 0, sizeof(g_bob_sync_events));
    memset(g_alice_status_events, 0, sizeof(g_alice_status_events));
}

static void dump_sync_events(const char *name, wss_sync_event_t *events, int count) {
    printf("    [%s sync] count=%d\n", name, count);
    for (int i = 0; i < count; i++) {
        printf("      #%d sid=%u sess=%08X confirm=%u busy=%u payload='%s'\n",
               i, events[i].sid, events[i].session_id,
               events[i].is_confirm, events[i].is_busy, events[i].payload);
    }
}

static void dump_status_events(const char *name, wss_status_event_t *events, int count) {
    printf("    [%s status] count=%d\n", name, count);
    for (int i = 0; i < count; i++) {
        printf("      #%d sess=%08X type=%s code=%02X\n",
               i, events[i].session_id, events[i].type, events[i].code);
    }
}

static int prime_sync_channel(ws_client_t *alice, ws_client_t *bob) {
    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }

    send_sync_text(alice, g_alice_session_id, 1, "prime-a\n");
    send_sync_text(bob, g_bob_session_id, 1, "prime-b\n");

    if (!wait_for_forwarded_sid(alice, bob, &g_bob_sync_count, g_bob_sync_events, 1)) return 0;
    if (!wait_for_forwarded_sid(alice, bob, &g_alice_sync_count, g_alice_sync_events, 1)) {
        send_sync_text(alice, g_alice_session_id, 1, "prime-a\n");
        if (!wait_for_forwarded_sid(alice, bob, &g_alice_sync_count, g_alice_sync_events, 1)) return 0;
    }

    // 这里发的是客户端对已转发 sid=1 的 ACK，不是在等待服务端 confirm 响应。
    send_sync_confirm_text(alice, g_alice_session_id, 1);
    send_sync_confirm_text(bob, g_bob_session_id, 1);

    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }
    clear_runtime_events();
    return 1;
}

static void send_sync_text(ws_client_t *client, uint32_t session_id, uint8_t sid, const char *payload) {
    char cmd[512];
    int n = snprintf(cmd, sizeof(cmd), P2P_WSS_CMD_SYNC_FMT, session_id, sid);
    if (n < 0) return;
    if (payload) {
        strncat(cmd, payload, sizeof(cmd) - strlen(cmd) - 1);
    }
    ws_client_send_text(client, cmd);
}

static void send_sync_confirm_text(ws_client_t *client, uint32_t session_id, uint8_t sid) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), P2P_WSS_RSP_SYNC_CONFIRM_FMT, session_id, sid);
    ws_client_send_text(client, cmd);
}

static void test_sync_forwarded_and_confirmed(void) {
    const char *TEST_NAME = "sync_sid1_bidirectional_forwarded";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;
    int alice_idx = -1;
    int bob_idx = -1;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();
    if (!establish_pair(&alice, &bob, "wss_sync_ok_alice", "wss_sync_ok_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }

    for (int i = 0; i < 50; i++) {
        ws_client_update(alice);
        ws_client_update(bob);
        P_usleep(10000);
    }

    send_sync_text(alice, g_alice_session_id, 1, "prime-a\n");
    send_sync_text(bob, g_bob_session_id, 1, "prime-b\n");

    if (!wait_for_forwarded_sid(alice, bob, &g_bob_sync_count, g_bob_sync_events, 1)) {
        dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
        dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive sid=1");
        return;
    }
    if (!wait_for_forwarded_sid(alice, bob, &g_alice_sync_count, g_alice_sync_events, 1)) {
        send_sync_text(alice, g_alice_session_id, 1, "prime-a\n");
        if (!wait_for_forwarded_sid(alice, bob, &g_alice_sync_count, g_alice_sync_events, 1)) {
            dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
            dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
            close_clients(alice, bob);
            TEST_FAIL(TEST_NAME, "Alice did not receive sid=1");
            return;
        }
    }

    for (int i = 0; i < g_alice_sync_count; i++) {
        if (!g_alice_sync_events[i].is_confirm && !g_alice_sync_events[i].is_busy && g_alice_sync_events[i].sid == 1) {
            alice_idx = i;
            break;
        }
    }
    for (int i = 0; i < g_bob_sync_count; i++) {
        if (!g_bob_sync_events[i].is_confirm && !g_bob_sync_events[i].is_busy && g_bob_sync_events[i].sid == 1) {
            bob_idx = i;
            break;
        }
    }

    if (alice_idx < 0 || bob_idx < 0) {
        dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
        dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "forwarded sid=1 event missing");
        return;
    }
    if (g_alice_sync_events[alice_idx].session_id != g_alice_session_id
        || strcmp(g_alice_sync_events[alice_idx].payload, "prime-b\n") != 0) {
        dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
        dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Alice sid=1 payload mismatch");
        return;
    }
    if (g_bob_sync_events[bob_idx].session_id != g_bob_session_id
        || strcmp(g_bob_sync_events[bob_idx].payload, "prime-a\n") != 0) {
        dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
        dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob sid=1 payload mismatch");
        return;
    }

    send_sync_confirm_text(alice, g_alice_session_id, 1);
    send_sync_confirm_text(bob, g_bob_session_id, 1);
    close_clients(alice, bob);
    TEST_PASS(TEST_NAME);
}

static void test_sync_duplicate_resends_confirm_only(void) {
    const char *TEST_NAME = "sync_duplicate_resends_confirm_only";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();
    if (!establish_pair(&alice, &bob, "wss_sync_dup_alice", "wss_sync_dup_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    if (!prime_sync_channel(alice, bob)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "failed to prime sid=1 sync");
        return;
    }

    send_sync_text(alice, g_alice_session_id, 2, "candidate-dup\n");
    if (!wait_for_counts(alice, bob, 1, 1, 0)) {
        dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
        dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
        dump_status_events("alice", g_alice_status_events, g_alice_status_count);
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting first sync");
        return;
    }

    send_sync_text(alice, g_alice_session_id, 2, "candidate-dup\n");
    if (!wait_for_counts(alice, bob, 2, 1, 0)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting duplicate confirm");
        return;
    }
    if (!g_alice_sync_events[1].is_confirm || g_alice_sync_events[1].sid != 2) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "duplicate confirm mismatch");
        return;
    }
    if (!expect_bob_sync_unchanged(alice, bob, 1)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "Bob received duplicate forwarded sync");
        return;
    }

    send_sync_confirm_text(bob, g_bob_session_id, 2);
    close_clients(alice, bob);
    TEST_PASS(TEST_NAME);
}

static void test_sync_busy_and_release_next(void) {
    const char *TEST_NAME = "sync_busy_and_release_next";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();
    if (!establish_pair(&alice, &bob, "wss_sync_busy_alice", "wss_sync_busy_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    if (!prime_sync_channel(alice, bob)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "failed to prime sid=1 sync");
        return;
    }

    send_sync_text(alice, g_alice_session_id, 2, "candidate-1\n");
    if (!wait_for_counts(alice, bob, 1, 1, 0)) {
        dump_sync_events("alice", g_alice_sync_events, g_alice_sync_count);
        dump_sync_events("bob", g_bob_sync_events, g_bob_sync_count);
        dump_status_events("alice", g_alice_status_events, g_alice_status_count);
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting sid=1");
        return;
    }

    send_sync_text(alice, g_alice_session_id, 3, "candidate-2\n");
    if (!expect_bob_sync_unchanged(alice, bob, 1)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "sid=3 forwarded before sid=2 confirm");
        return;
    }

    send_sync_text(alice, g_alice_session_id, 4, "candidate-3\n");
    if (!wait_for_counts(alice, bob, 2, 1, 0)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting sid=4 busy response");
        return;
    }
    if (!g_alice_sync_events[1].is_busy || g_alice_sync_events[1].sid != 4
        || g_alice_sync_events[1].session_id != g_alice_session_id) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "sid=4 busy response mismatch");
        return;
    }

    send_sync_confirm_text(bob, g_bob_session_id, 2);
    if (!wait_for_counts(alice, bob, 3, 2, 0)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting delayed sid=3 release");
        return;
    }
    if (!g_alice_sync_events[2].is_confirm || g_alice_sync_events[2].sid != 3) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "sid=3 delayed confirm mismatch");
        return;
    }
    if (g_bob_sync_events[1].is_confirm || g_bob_sync_events[1].is_busy
        || g_bob_sync_events[1].sid != 3 || strcmp(g_bob_sync_events[1].payload, "candidate-2\n") != 0) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "sid=3 forwarded payload mismatch");
        return;
    }

    send_sync_confirm_text(bob, g_bob_session_id, 3);
    close_clients(alice, bob);
    TEST_PASS(TEST_NAME);
}

static void test_sync_sid_zero_invalid(void) {
    const char *TEST_NAME = "sync_sid_zero_invalid";
    ws_client_t *alice = NULL;
    ws_client_t *bob = NULL;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    reset_state();
    if (!establish_pair(&alice, &bob, "wss_sync_bad_alice", "wss_sync_bad_bob")) {
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }

    send_sync_text(alice, g_alice_session_id, 0, "bad-sid\n");
    if (!wait_for_counts(alice, bob, 0, 0, 1)) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "timeout waiting invalid status");
        return;
    }
    if (strcmp(g_alice_status_events[0].type, "SYNC") != 0
        || g_alice_status_events[0].code != P2P_ERR_INVALID
        || g_alice_status_events[0].session_id != g_alice_session_id) {
        close_clients(alice, bob);
        TEST_FAIL(TEST_NAME, "invalid status mismatch");
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

    printf("=== WSS SYNC Protocol Tests ===\n");
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
    test_sync_forwarded_and_confirmed();
    test_sync_duplicate_resends_confirm_only();
    test_sync_busy_and_release_next();
    test_sync_sid_zero_invalid();

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
