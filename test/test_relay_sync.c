/*
 * test_relay_sync.c - RELAY SYNC 协议单元测试
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

#define DEFAULT_SERVER_PORT     9333
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define RECV_TIMEOUT_MS         2000

static int g_server_port = DEFAULT_SERVER_PORT;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static pid_t g_server_pid = 0;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_PASS(name) do { printf("%s:%d   \033[32m[PASS] %s\033[0m\n", __FILE__, __LINE__, name); g_tests_passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("%s:%d   \033[31m[FAIL] %s: %s\033[0m\n", __FILE__, __LINE__, name, reason); g_tests_failed++; } while(0)

typedef struct {
    int received;
    uint8_t features;
    uint8_t candidate_sync_max;
} online_ack_t;

typedef struct {
    int received;
    uint32_t session_id;
    uint8_t online;
} sync0_ack_t;

typedef struct {
    int received;
    uint8_t req_type;
    uint8_t status_code;
    uint32_t session_id;
} status_t;

typedef struct {
    int received;
    int is_confirm;
    uint32_t session_id;
    uint8_t sid;
    uint8_t candidate_count;
    p2p_candidate_t candidates[4];
    int has_fin;
} relay_sync_t;

static sock_t tcp_connect(void) {
    sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == P_INVALID_SOCKET) return P_INVALID_SOCKET;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        P_sock_close(sock);
        return P_INVALID_SOCKET;
    }

    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    return sock;
}

static int tcp_send_all(sock_t sock, const uint8_t *data, int len) {
    int sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, (const char*)(data + sent), len - sent, 0);
        if (n <= 0) return -1;
        sent += (int)n;
    }
    return sent;
}

static int tcp_recv_all(sock_t sock, uint8_t *buf, int len) {
    int received = 0;
    while (received < len) {
        ssize_t n = recv(sock, (char*)(buf + received), len - received, 0);
        if (n <= 0) return -1;
        received += (int)n;
    }
    return received;
}

static int tcp_recv_relay_packet(sock_t sock, uint8_t *buf, int buf_size,
                                 uint8_t *type_out, uint16_t *payload_len_out) {
    if (tcp_recv_all(sock, buf, 3) != 3) return -1;

    *type_out = buf[0];
    *payload_len_out = ntohs(*(uint16_t*)(buf + 1));
    if (*payload_len_out > buf_size - 3) return -2;

    if (*payload_len_out > 0) {
        if (tcp_recv_all(sock, buf + 3, *payload_len_out) != *payload_len_out) return -1;
    }

    return 3 + *payload_len_out;
}

static void fill_candidate(p2p_candidate_t *cand, uint16_t port, uint32_t ipv4_be, uint32_t priority) {
    memset(cand, 0, sizeof(*cand));
    cand->type = 0;
    cand->addr.port = htons(port);
    memcpy(cand->addr.ip, P2P_IPV4_MAPPED_PREFIX, sizeof(P2P_IPV4_MAPPED_PREFIX));
    memcpy(cand->addr.ip + 12, &ipv4_be, 4);
    cand->priority = htonl(priority);
}

static int build_online(uint8_t *buf, int buf_size, const char *peer_id, uint32_t instance_id) {
    const uint16_t payload_len = P2P_RLY_REG_PSZ;
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_REG;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;
    memset(buf + 3, 0, P2P_PEER_ID_MAX);
    if (peer_id) strncpy((char*)(buf + 3), peer_id, P2P_PEER_ID_MAX - 1);
    buf[3 + P2P_PEER_ID_MAX + 0] = (instance_id >> 24) & 0xFF;
    buf[3 + P2P_PEER_ID_MAX + 1] = (instance_id >> 16) & 0xFF;
    buf[3 + P2P_PEER_ID_MAX + 2] = (instance_id >> 8) & 0xFF;
    buf[3 + P2P_PEER_ID_MAX + 3] = instance_id & 0xFF;
    return 3 + payload_len;
}

static int build_sync0(uint8_t *buf, int buf_size, const char *target_peer_id,
                       int candidate_count, p2p_candidate_t *candidates) {
    uint16_t payload_len = P2P_RLY_SYN0_PSZ(candidate_count);
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_SYN0;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;
    memset(buf + 3, 0, P2P_PEER_ID_MAX);
    if (target_peer_id) strncpy((char*)(buf + 3), target_peer_id, P2P_PEER_ID_MAX - 1);
    buf[3 + P2P_PEER_ID_MAX] = (uint8_t)candidate_count;
    if (candidate_count > 0 && candidates) {
        memcpy(buf + 3 + P2P_PEER_ID_MAX + 1, candidates, (size_t)candidate_count * sizeof(p2p_candidate_t));
    }
    return 3 + payload_len;
}

static int build_sync(uint8_t *buf, int buf_size, uint32_t session_id, uint8_t sid,
                      int candidate_count, const p2p_candidate_t *candidates, int add_fin) {
    uint16_t payload_len = P2P_RLY_SYNC_PSZ(candidate_count, add_fin);
    uint8_t *payload;
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_SYNC;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;

    payload = buf + 3;
    nwrite_l(payload, session_id);
    payload[P2P_SESS_ID_SZ] = sid;
    payload[P2P_SESS_ID_SZ + 1] = (uint8_t)candidate_count;
    if (candidate_count > 0 && candidates) {
        memcpy(payload + P2P_SESS_ID_SZ + 2, candidates, (size_t)candidate_count * sizeof(p2p_candidate_t));
    }
    if (add_fin) {
        payload[P2P_SESS_ID_SZ + 2 + candidate_count * (int)sizeof(p2p_candidate_t)] = P2P_RLY_SYNC_FIN_MARKER;
    }
    return 3 + payload_len;
}

static int build_sync_confirm(uint8_t *buf, int buf_size, uint32_t session_id, uint8_t sid) {
    if (buf_size < 3 + (int)P2P_RLY_SYNC_CONFIRM_PSZ) return -1;
    buf[0] = P2P_RLY_SYNC;
    buf[1] = 0;
    buf[2] = (uint8_t)P2P_RLY_SYNC_CONFIRM_PSZ;
    nwrite_l(buf + 3, session_id);
    buf[3 + P2P_SESS_ID_SZ] = sid;
    return 3 + (int)P2P_RLY_SYNC_CONFIRM_PSZ;
}

static int send_online_recv_ack(sock_t sock, const char *peer_id, uint32_t instance_id, online_ack_t *ack) {
    uint8_t pkt[64];
    uint8_t recv_buf[64];
    uint8_t type = 0;
    uint16_t payload_len = 0;
    int pkt_len = build_online(pkt, sizeof(pkt), peer_id, instance_id);
    if (pkt_len < 0) return -1;
    if (tcp_send_all(sock, pkt, pkt_len) != pkt_len) return -1;
    if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
        ack->received = 0;
        return 0;
    }
    if (type == P2P_RLY_REG && payload_len >= P2P_RLY_REG_S2C_PSZ) {
        ack->received = 1;
        ack->features = recv_buf[3];
        ack->candidate_sync_max = recv_buf[4];
        return 1;
    }
    ack->received = 0;
    return 0;
}

static int send_sync0_recv_ack(sock_t sock, const char *target_peer_id,
                               int cand_count, p2p_candidate_t *cands,
                               sync0_ack_t *ack) {
    uint8_t pkt[256];
    int pkt_len = build_sync0(pkt, sizeof(pkt), target_peer_id, cand_count, cands);
    if (pkt_len < 0) return -1;
    if (tcp_send_all(sock, pkt, pkt_len) != pkt_len) return -1;

    for (int i = 0; i < 5; i++) {
        uint8_t recv_buf[256];
        uint8_t type = 0;
        uint16_t payload_len = 0;
        int off;

        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            ack->received = 0;
            return 0;
        }
        if (type != P2P_RLY_SYN0 || payload_len < P2P_RLY_SYN0_S2C_PSZ(0)) continue;

        ack->received = 1;
        off = 3 + P2P_PEER_ID_MAX;
        ack->session_id = nget_l(recv_buf + off);
        ack->online = recv_buf[off + P2P_SESS_ID_SZ] == 0xFF ? 0 : 1;
        return 1;
    }

    ack->received = 0;
    return 0;
}

static int wait_status(sock_t sock, status_t *status) {
    for (int i = 0; i < 5; i++) {
        uint8_t recv_buf[128];
        uint8_t type = 0;
        uint16_t payload_len = 0;
        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            status->received = 0;
            return 0;
        }
        if (type != P2P_RLY_STA || payload_len < P2P_RLY_STA_PSZ(2, 0)) continue;

        status->received = 1;
        status->req_type = recv_buf[3];
        status->status_code = recv_buf[4];
        status->session_id = nget_l(recv_buf + 5);
        return 1;
    }
    status->received = 0;
    return 0;
}

static int wait_sync_packet(sock_t sock, relay_sync_t *sync) {
    for (int i = 0; i < 10; i++) {
        uint8_t recv_buf[512];
        uint8_t type = 0;
        uint16_t payload_len = 0;
        uint8_t *payload;

        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            sync->received = 0;
            return 0;
        }
        if (type != P2P_RLY_SYNC) continue;

        memset(sync, 0, sizeof(*sync));
        sync->received = 1;
        payload = recv_buf + 3;
        sync->session_id = nget_l(payload);
        sync->sid = payload[P2P_SESS_ID_SZ];

        if (payload_len == P2P_RLY_SYNC_CONFIRM_PSZ) {
            sync->is_confirm = 1;
            return 1;
        }

        sync->candidate_count = payload[P2P_SESS_ID_SZ + 1];
        if (sync->candidate_count > 4) sync->candidate_count = 4;
        if (sync->candidate_count > 0) {
            memcpy(sync->candidates, payload + P2P_SESS_ID_SZ + 2,
                   (size_t)sync->candidate_count * sizeof(p2p_candidate_t));
        }
        sync->has_fin = payload_len == P2P_RLY_SYNC_PSZ(payload[P2P_SESS_ID_SZ + 1], true);
        return 1;
    }

    sync->received = 0;
    return 0;
}

static int wait_forwarded_sync_packet(sock_t sock, relay_sync_t *sync) {
    for (int i = 0; i < 10; i++) {
        if (!wait_sync_packet(sock, sync)) return 0;
        if (!sync->is_confirm) return 1;
    }
    sync->received = 0;
    return 0;
}

static int wait_sync_confirm_packet(sock_t sock, relay_sync_t *sync) {
    for (int i = 0; i < 10; i++) {
        if (!wait_sync_packet(sock, sync)) return 0;
        if (sync->is_confirm) return 1;
    }
    sync->received = 0;
    return 0;
}

static int expect_no_sync(sock_t sock, int timeout_ms) {
    uint8_t recv_buf[512];
    uint8_t type = 0;
    uint16_t payload_len = 0;
    P_sock_rcvtimeo(sock, timeout_ms);
    while (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) >= 0) {
        if (type == P2P_RLY_SYNC) {
            P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
            return 0;
        }
    }
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    return 1;
}

static void drain_pending_packets(sock_t sock) {
    uint8_t discard[512];
    uint8_t type = 0;
    uint16_t len = 0;
    P_sock_rcvtimeo(sock, 200);
    for (int i = 0; i < 5; i++) {
        if (tcp_recv_relay_packet(sock, discard, sizeof(discard), &type, &len) < 0) break;
    }
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
}

static int establish_pair(sock_t *sock_a, uint32_t *session_a,
                          sock_t *sock_b, uint32_t *session_b,
                          const char *name_a, const char *name_b) {
    uint32_t inst_a = (uint32_t)P_tick_us() + 1000u;
    uint32_t inst_b = (uint32_t)P_tick_us() + 1001u;
    online_ack_t ack_a;
    online_ack_t ack_b;
    sync0_ack_t sync_a;
    sync0_ack_t sync_b;

    if (send_online_recv_ack(*sock_a, name_a, inst_a, &ack_a) <= 0 || !ack_a.received) return 0;
    if (send_sync0_recv_ack(*sock_a, name_b, 0, NULL, &sync_a) <= 0 || !sync_a.received) return 0;
    *session_a = sync_a.session_id;

    if (send_online_recv_ack(*sock_b, name_b, inst_b, &ack_b) <= 0 || !ack_b.received) return 0;
    if (send_sync0_recv_ack(*sock_b, name_a, 0, NULL, &sync_b) <= 0 || !sync_b.received) return 0;
    *session_b = sync_b.session_id;

    drain_pending_packets(*sock_a);
    drain_pending_packets(*sock_b);
    return 1;
}

static int prime_sync_channel(sock_t sock_alice, uint32_t session_alice,
                              sock_t sock_bob, uint32_t session_bob) {
    p2p_candidate_t cand_a;
    p2p_candidate_t cand_b;
    uint8_t pkt[256];
    relay_sync_t sync_msg;
    int pkt_len;

    fill_candidate(&cand_a, 4101, htonl(0x7F000001u), 11);
    fill_candidate(&cand_b, 4102, htonl(0x7F000001u), 22);

    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 1, 1, &cand_a, 0);
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) return 0;
    pkt_len = build_sync(pkt, sizeof(pkt), session_bob, 1, 1, &cand_b, 0);
    if (tcp_send_all(sock_bob, pkt, pkt_len) != pkt_len) return 0;

    if (!wait_forwarded_sync_packet(sock_bob, &sync_msg) || sync_msg.sid != 1) return 0;
    if (!wait_forwarded_sync_packet(sock_alice, &sync_msg) || sync_msg.sid != 1) return 0;

    pkt_len = build_sync_confirm(pkt, sizeof(pkt), session_bob, 1);
    if (tcp_send_all(sock_bob, pkt, pkt_len) != pkt_len) return 0;
    pkt_len = build_sync_confirm(pkt, sizeof(pkt), session_alice, 1);
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) return 0;

    drain_pending_packets(sock_alice);
    drain_pending_packets(sock_bob);
    return 1;
}

static void test_sync_forwarded_and_confirmed(void) {
    const char *TEST_NAME = "sync_forwarded_and_confirmed";
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    uint32_t session_alice = 0;
    uint32_t session_bob = 0;
    p2p_candidate_t cand;
    uint8_t pkt[256];
    relay_sync_t forwarded;
    relay_sync_t confirm;
    int pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "relay_sync_ok_alice", "relay_sync_ok_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    if (!prime_sync_channel(sock_alice, session_alice, sock_bob, session_bob)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to prime sid=1 sync");
        return;
    }

    fill_candidate(&cand, 3456, htonl(0x7F000001u), 100);
    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 2, 1, &cand, 0);
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to send sync");
        return;
    }

    if (!wait_forwarded_sync_packet(sock_bob, &forwarded)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive forwarded sync");
        return;
    }
    if (forwarded.session_id != session_bob || forwarded.sid != 2 || forwarded.candidate_count != 1
        || memcmp(&forwarded.candidates[0], &cand, sizeof(cand)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "forwarded sync payload mismatch");
        return;
    }

    if (!wait_sync_confirm_packet(sock_alice, &confirm)
        || confirm.session_id != session_alice || confirm.sid != 2) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive sync confirm");
        return;
    }

    pkt_len = build_sync_confirm(pkt, sizeof(pkt), session_bob, 2);
    tcp_send_all(sock_bob, pkt, pkt_len);

    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    TEST_PASS(TEST_NAME);
}

static void test_sync_duplicate_resends_confirm_only(void) {
    const char *TEST_NAME = "sync_duplicate_resends_confirm_only";
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    uint32_t session_alice = 0;
    uint32_t session_bob = 0;
    p2p_candidate_t cand;
    uint8_t pkt[256];
    relay_sync_t forwarded;
    relay_sync_t confirm;
    int pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "relay_sync_dup_alice", "relay_sync_dup_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    if (!prime_sync_channel(sock_alice, session_alice, sock_bob, session_bob)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to prime sid=1 sync");
        return;
    }

    fill_candidate(&cand, 4567, htonl(0x7F000001u), 101);
    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 2, 1, &cand, 0);
    tcp_send_all(sock_alice, pkt, pkt_len);
    if (!wait_forwarded_sync_packet(sock_bob, &forwarded)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive initial sync");
        return;
    }
    if (!wait_sync_confirm_packet(sock_alice, &confirm) || confirm.sid != 2) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice missing initial confirm");
        return;
    }

    tcp_send_all(sock_alice, pkt, pkt_len);
    if (!wait_sync_confirm_packet(sock_alice, &confirm) || confirm.sid != 2) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice missing duplicate confirm");
        return;
    }
    if (!expect_no_sync(sock_bob, 300)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob received duplicate forwarded sync");
        return;
    }

    pkt_len = build_sync_confirm(pkt, sizeof(pkt), session_bob, 2);
    tcp_send_all(sock_bob, pkt, pkt_len);

    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    TEST_PASS(TEST_NAME);
}

static void test_sync_busy_and_release_next(void) {
    const char *TEST_NAME = "sync_busy_and_release_next";
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    uint32_t session_alice = 0;
    uint32_t session_bob = 0;
    p2p_candidate_t cand1;
    p2p_candidate_t cand2;
    uint8_t pkt[256];
    relay_sync_t sync_msg;
    relay_sync_t confirm;
    status_t status;
    int pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "relay_sync_busy_alice", "relay_sync_busy_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    if (!prime_sync_channel(sock_alice, session_alice, sock_bob, session_bob)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to prime sid=1 sync");
        return;
    }

    fill_candidate(&cand1, 5001, htonl(0x7F000001u), 111);
    fill_candidate(&cand2, 5002, htonl(0x7F000001u), 222);

    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 2, 1, &cand1, 0);
    tcp_send_all(sock_alice, pkt, pkt_len);
    if (!wait_forwarded_sync_packet(sock_bob, &sync_msg) || sync_msg.sid != 2) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive sid=2");
        return;
    }
    if (!wait_sync_confirm_packet(sock_alice, &confirm) || confirm.sid != 2) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice missing sid=2 confirm");
        return;
    }

    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 3, 1, &cand2, 0);
    tcp_send_all(sock_alice, pkt, pkt_len);
    if (!expect_no_sync(sock_bob, 300)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "sid=3 forwarded before sid=2 confirm");
        return;
    }

    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 4, 0, NULL, 0);
    tcp_send_all(sock_alice, pkt, pkt_len);
    if (!wait_status(sock_alice, &status) || !status.received
        || status.req_type != P2P_RLY_SYNC || status.status_code != P2P_ERR_BUSY
        || status.session_id != session_alice) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive SYNC BUSY status");
        return;
    }

    pkt_len = build_sync_confirm(pkt, sizeof(pkt), session_bob, 2);
    tcp_send_all(sock_bob, pkt, pkt_len);

    if (!wait_forwarded_sync_packet(sock_bob, &sync_msg) || sync_msg.sid != 3
        || sync_msg.candidate_count != 1 || memcmp(&sync_msg.candidates[0], &cand2, sizeof(cand2)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive released sid=3");
        return;
    }
    if (!wait_sync_confirm_packet(sock_alice, &confirm) || confirm.sid != 3) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive delayed sid=3 confirm");
        return;
    }

    pkt_len = build_sync_confirm(pkt, sizeof(pkt), session_bob, 3);
    tcp_send_all(sock_bob, pkt, pkt_len);

    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    TEST_PASS(TEST_NAME);
}

static void test_sync_sid_zero_protocol_error(void) {
    const char *TEST_NAME = "sync_sid_zero_protocol_error";
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    uint32_t session_alice = 0;
    uint32_t session_bob = 0;
    uint8_t pkt[64];
    status_t status;
    int pkt_len;

    printf("\n--- Test: %s ---\n", TEST_NAME);
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "relay_sync_bad_alice", "relay_sync_bad_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }

    (void)session_bob;
    pkt_len = build_sync(pkt, sizeof(pkt), session_alice, 0, 0, NULL, 0);
    tcp_send_all(sock_alice, pkt, pkt_len);

    if (!wait_status(sock_alice, &status) || !status.received
        || status.req_type != P2P_RLY_SYNC || status.status_code != P2P_ERR_PROTOCOL
        || status.session_id != session_alice) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive SYNC protocol error");
        return;
    }

    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    TEST_PASS(TEST_NAME);
}

int main(int argc, char *argv[]) {
    const char *server_path = NULL;

    if (argc >= 2) server_path = argv[1];
    if (argc > 2) {
        g_server_port = atoi(argv[2]);
        if (g_server_port <= 0 || g_server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 1;
        }
    }

    printf("=== RELAY SYNC Protocol Tests ===\n");
    printf("Server path: %s\n", server_path ? server_path : "(external)");
    printf("Server addr: %s:%d (TCP)\n\n", g_server_host, g_server_port);

    signal(SIGPIPE, SIG_IGN);

    if (server_path) {
        char port_str[16];
        printf("[*] Starting server (RELAY mode)...\n");
        snprintf(port_str, sizeof(port_str), "%d", g_server_port);
        g_server_pid = fork();
        if (g_server_pid < 0) {
            fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
            return 1;
        } else if (g_server_pid == 0) {
            execl(server_path, server_path, "-p", port_str, "--relay", NULL);
            fprintf(stderr, "Failed to exec: %s\n", strerror(errno));
            _exit(127);
        }
        printf("    Server PID: %d\n", g_server_pid);
        P_usleep(500 * 1000);
    }

    printf("\n[*] Running tests...\n");
    test_sync_forwarded_and_confirmed();
    test_sync_duplicate_resends_confirm_only();
    test_sync_busy_and_release_next();
    test_sync_sid_zero_protocol_error();

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
}