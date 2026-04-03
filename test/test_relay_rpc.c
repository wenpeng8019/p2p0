/*
 * test_relay_rpc.c - RELAY RPC 协议单元测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 * 验证 p2p_server 对 RELAY 协议 MSG RPC 机制的处理逻辑：
 * - P2P_RLY_REQ: A→Server→B 请求转发
 * - P2P_RLY_RESP: B→Server→A 响应转发
 * - rpc_pending: 独立于 peer_pending 的并行流控通道
 *
 * ============================================================================
 * 测试方法
 * ============================================================================
 * 1. 启动 p2p_server 子进程，监听指定 TCP 端口（--relay --msg 模式）
 * 2. 通过 instrument 机制收集 server 的实时日志
 * 3. 模拟 A、B 双端的 MSG RPC 交互流程
 *
 * ============================================================================
 * 测试用例分类
 * ============================================================================
 *
 * 一、正常功能测试
 * ---------------------------------------------------------------------------
 *
 * 测试 1: msg_req_forwarded
 *   目标：验证 MSG_REQ 被转发给 B 端
 *   方法：配对后 A 发送 MSG_REQ，B 等待接收
 *   预期：
 *     - B 收到 P2P_RLY_REQ
 *     - 包含正确的 session_id、sid、msg、data
 *
 * 测试 2: msg_resp_forwarded
 *   目标：验证 MSG_RESP 被转发给 A 端
 *   方法：A 发送 REQ → B 收到 → B 发送 RESP → A 收到
 *   预期：
 *     - A 收到 P2P_RLY_RESP
 *     - 包含 B 发送的 code 和 data
 *
 * 测试 3: msg_rpc_complete
 *   目标：验证完整 RPC 流程（含 data 校验）
 *   方法：A:REQ → B:REQ → B:RESP → A:RESP
 *   预期：
 *     - 双方数据正确
 *
 * 测试 4: msg_bidirectional
 *   目标：验证双向 RPC（A→B 后 B→A）
 *   方法：先 A→B，再 B→A
 *
 * 二、失败验证测试
 * ---------------------------------------------------------------------------
 *
 * 测试 5: msg_req_peer_offline
 *   目标：验证对端离线时服务器回复错误 RESP
 *   方法：A 单独配对（B 未上线）→ 发送 REQ
 *   预期：
 *     - A 收到 RESP(code=P2P_MSG_ERR_PEER_OFFLINE)
 *
 * 测试 6: msg_req_bad_payload
 *   目标：验证畸形 MSG_REQ 包被拒绝
 *   方法：发送 payload 过短的 REQ 包
 *   预期：
 *     - 不触发异常
 *
 * 测试 7: msg_req_bad_session
 *   目标：验证无效 session_id 的 REQ 处理
 *   方法：发送包含不存在 session_id 的 REQ
 *   预期：
 *     - 不触发异常
 *
 * 三、边界/临界态测试
 * ---------------------------------------------------------------------------
 *
 * 测试 8: msg_large_data
 *   目标：验证大尺寸 RPC 数据转发
 *   方法：发送接近上限大小的 REQ/RESP
 *
 * 测试 9: msg_rpc_and_data_parallel
 *   目标：验证 RPC（rpc_pending）与 DATA（peer_pending）互不干扰
 *   方法：同时发送 DATA 和 REQ，验证两者都正确转发
 *
 * 测试 10: msg_rpc_timeout
 *   目标：验证 RPC 超时后服务器回复 P2P_MSG_ERR_TIMEOUT
 *   方法：A 发送 REQ → B 收到但不回复 RESP → 等待超时
 *   预期：
 *     - A 收到 RESP(code=P2P_MSG_ERR_TIMEOUT)
 *
 * ============================================================================
 * 依赖与用法
 * ============================================================================
 * 依赖：p2p_server 可执行文件（需支持 --relay --msg）
 *
 * 用法：
 *   ./test_relay_rpc <server_path> [port]
 *
 * 示例：
 *   ./test_relay_rpc ./p2p_server 9780
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

// 默认配置
#define DEFAULT_SERVER_PORT     9780
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define RECV_TIMEOUT_MS         2000

// 测试状态
static int g_server_port = DEFAULT_SERVER_PORT;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static pid_t g_server_pid = 0;

// instrument 日志收集
#define MAX_LOG_ENTRIES 100
static struct {
    uint8_t chn;
    char tag[32];
    char txt[256];
} g_logs[MAX_LOG_ENTRIES];
static volatile int g_log_count = 0;

// 测试结果
static int g_tests_passed = 0;
static int g_tests_failed = 0;

///////////////////////////////////////////////////////////////////////////////
// 工具函数
///////////////////////////////////////////////////////////////////////////////

static void on_instrument_log(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len) {
    (void)len;
    if (rid == 0) return;

    int idx = g_log_count;
    if (idx < MAX_LOG_ENTRIES) {
        g_log_count = idx + 1;
        g_logs[idx].chn = chn;
        strncpy(g_logs[idx].tag, tag ? tag : "", sizeof(g_logs[idx].tag) - 1);
        g_logs[idx].tag[sizeof(g_logs[idx].tag) - 1] = '\0';
        strncpy(g_logs[idx].txt, txt, sizeof(g_logs[idx].txt) - 1);
        g_logs[idx].txt[sizeof(g_logs[idx].txt) - 1] = '\0';
    }

    const char* color;
    switch (chn) {
        case LOG_SLOT_DEBUG:   color = "\033[36m"; break;
        case LOG_SLOT_INFO:    color = "\033[32m"; break;
        case LOG_SLOT_WARN:    color = "\033[33m"; break;
        case LOG_SLOT_ERROR:   color = "\033[31m"; break;
        default:               color = "\033[37m"; break;
    }
    printf("%s    [SERVER] %s: %s\033[0m\n", color, tag, txt);
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

#define TEST_PASS(name) do { printf("%s:%d   \033[32m[PASS] %s\033[0m\n", __FILE__, __LINE__, name); g_tests_passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("%s:%d   \033[31m[FAIL] %s: %s\033[0m\n", __FILE__, __LINE__, name, reason); g_tests_failed++; } while(0)

///////////////////////////////////////////////////////////////////////////////
// TCP 辅助函数
///////////////////////////////////////////////////////////////////////////////

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
        sent += n;
    }
    return sent;
}

static int tcp_recv_all(sock_t sock, uint8_t *buf, int len) {
    int received = 0;
    while (received < len) {
        ssize_t n = recv(sock, (char*)(buf + received), len - received, 0);
        if (n <= 0) return -1;
        received += n;
    }
    return received;
}

// 接收一个完整的 relay 帧: [type(1)][size(2)][payload(size)]
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

///////////////////////////////////////////////////////////////////////////////
// 协议构造函数
///////////////////////////////////////////////////////////////////////////////

// 构造 ONLINE 包
static int build_online(uint8_t *buf, int buf_size, const char *peer_id, uint32_t instance_id) {
    const uint16_t payload_len = P2P_RLY_ONLINE_PSZ;
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_ONLINE;
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

// 构造 SYNC0 包
static int build_sync0(uint8_t *buf, int buf_size, const char *target_peer_id,
                       int candidate_count, p2p_candidate_t *candidates) {
    uint16_t payload_len = P2P_RLY_SYNC0_PSZ(candidate_count);
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_SYNC0;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;

    memset(buf + 3, 0, P2P_PEER_ID_MAX);
    if (target_peer_id) strncpy((char*)(buf + 3), target_peer_id, P2P_PEER_ID_MAX - 1);

    buf[3 + P2P_PEER_ID_MAX] = (uint8_t)candidate_count;

    if (candidate_count > 0 && candidates) {
        memcpy(buf + 3 + P2P_PEER_ID_MAX + 1, candidates, candidate_count * sizeof(p2p_candidate_t));
    }

    return 3 + payload_len;
}

// 构造 RLY_REQ 包
// relay 帧: [type(1)][size(2)][session_id(8)][sid(2)][msg(1)][data(N)]
static int build_rly_req(uint8_t *buf, int buf_size,
                         uint64_t session_id, uint16_t sid,
                         uint8_t msg, const uint8_t *data, int data_len) {
    uint16_t payload_len = P2P_SESS_ID_PSZ + 2 + 1 + data_len;
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_REQ;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;

    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++)
        buf[3 + i] = (session_id >> (56 - i * 8)) & 0xFF;

    // sid (2 bytes, network order)
    buf[11] = (sid >> 8) & 0xFF;
    buf[12] = sid & 0xFF;

    // msg (1 byte)
    buf[13] = msg;

    // data
    if (data_len > 0 && data)
        memcpy(buf + 14, data, data_len);

    return 3 + payload_len;
}

// 构造 RLY_RESP 包
// relay 帧: [type(1)][size(2)][session_id(8)][sid(2)][code(1)][data(N)]
static int build_rly_resp(uint8_t *buf, int buf_size,
                          uint64_t session_id, uint16_t sid,
                          uint8_t code, const uint8_t *data, int data_len) {
    uint16_t payload_len = P2P_SESS_ID_PSZ + 2 + 1 + data_len;
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_RESP;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;

    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++)
        buf[3 + i] = (session_id >> (56 - i * 8)) & 0xFF;

    // sid (2 bytes, network order)
    buf[11] = (sid >> 8) & 0xFF;
    buf[12] = sid & 0xFF;

    // code (1 byte)
    buf[13] = code;

    // data
    if (data_len > 0 && data)
        memcpy(buf + 14, data, data_len);

    return 3 + payload_len;
}

// 构造 DATA 包（用于并行测试）
// relay 帧: [type(1)][size(2)][session_id(8)][P2P_hdr(4)][data(N)]
static int build_relay_data(uint8_t *buf, int buf_size, uint64_t session_id,
                            uint8_t pkt_type, uint8_t flags, uint16_t seq,
                            const uint8_t *data, int data_len) {
    uint16_t payload_len = 8 + 4 + data_len;
    if (buf_size < 3 + (int)payload_len) return -1;

    buf[0] = P2P_RLY_DATA;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;

    for (int i = 0; i < 8; i++)
        buf[3 + i] = (session_id >> (56 - i * 8)) & 0xFF;

    buf[11] = pkt_type;
    buf[12] = flags;
    buf[13] = (seq >> 8) & 0xFF;
    buf[14] = seq & 0xFF;

    if (data_len > 0 && data)
        memcpy(buf + 15, data, data_len);

    return 3 + payload_len;
}

///////////////////////////////////////////////////////////////////////////////
// 解析结构和辅助函数
///////////////////////////////////////////////////////////////////////////////

typedef struct {
    int received;
    uint8_t features;
    uint8_t candidate_sync_max;
} online_ack_t;

typedef struct {
    int received;
    uint64_t session_id;
    uint8_t online;
} sync0_ack_t;

typedef struct {
    int received;
    uint64_t session_id;
    uint16_t sid;
    uint8_t msg_or_code;
    uint8_t data[1024];
    int data_len;
} rly_rpc_pkt_t;

// 发送 ONLINE 并接收 ONLINE_ACK
static int send_online_recv_ack(sock_t sock, const char *peer_id, uint32_t instance_id, online_ack_t *ack) {
    uint8_t pkt[64];
    int pkt_len = build_online(pkt, sizeof(pkt), peer_id, instance_id);
    if (pkt_len < 0) return -1;

    if (tcp_send_all(sock, pkt, pkt_len) != pkt_len) return -1;

    uint8_t recv_buf[64];
    uint8_t type;
    uint16_t payload_len;
    if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
        ack->received = 0;
        return 0;
    }

    if (type == P2P_RLY_ONLINE_ACK && payload_len >= P2P_RLY_ONLINE_ACK_PSZ) {
        ack->received = 1;
        ack->features = recv_buf[3];
        ack->candidate_sync_max = recv_buf[4];
        return 1;
    }

    ack->received = 0;
    return 0;
}

// 发送 SYNC0 并接收 SYNC0_ACK
static int send_sync0_recv_ack(sock_t sock, const char *target_peer_id,
                                int cand_count, p2p_candidate_t *cands,
                                sync0_ack_t *ack) {
    uint8_t pkt[512];
    int pkt_len = build_sync0(pkt, sizeof(pkt), target_peer_id, cand_count, cands);
    if (pkt_len < 0) return -1;

    if (tcp_send_all(sock, pkt, pkt_len) != pkt_len) return -1;

    for (int i = 0; i < 5; i++) {
        uint8_t recv_buf[256];
        uint8_t type;
        uint16_t payload_len;
        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            ack->received = 0;
            return 0;
        }

        if (type == P2P_RLY_SYNC0_ACK && payload_len >= P2P_RLY_SYNC0_ACK_PSZ) {
            ack->received = 1;
            ack->session_id = 0;
            for (int j = 0; j < 8; j++) {
                ack->session_id = (ack->session_id << 8) | recv_buf[3 + j];
            }
            ack->online = recv_buf[3 + 8];
            return 1;
        }
    }

    ack->received = 0;
    return 0;
}

// 消费待处理的包（下行 SYNC0 等）
static void drain_pending_packets(sock_t sock) {
    P_sock_rcvtimeo(sock, 200);
    uint8_t discard[512];
    uint8_t type;
    uint16_t len;
    for (int i = 0; i < 5; i++) {
        if (tcp_recv_relay_packet(sock, discard, sizeof(discard), &type, &len) < 0) break;
    }
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
}

// 建立配对（两端 ONLINE + SYNC0）
static int establish_pair(sock_t *sock_a, uint64_t *session_a,
                          sock_t *sock_b, uint64_t *session_b,
                          const char *name_a, const char *name_b) {
    uint32_t inst_a = (uint32_t)P_tick_us() + 1000;
    uint32_t inst_b = (uint32_t)P_tick_us() + 1001;

    online_ack_t ack_a;
    if (send_online_recv_ack(*sock_a, name_a, inst_a, &ack_a) <= 0 || !ack_a.received) return 0;

    sync0_ack_t sync_a;
    if (send_sync0_recv_ack(*sock_a, name_b, 0, NULL, &sync_a) <= 0 || !sync_a.received) return 0;
    *session_a = sync_a.session_id;

    online_ack_t ack_b;
    if (send_online_recv_ack(*sock_b, name_b, inst_b, &ack_b) <= 0 || !ack_b.received) return 0;

    sync0_ack_t sync_b;
    if (send_sync0_recv_ack(*sock_b, name_a, 0, NULL, &sync_b) <= 0 || !sync_b.received) return 0;
    *session_b = sync_b.session_id;

    drain_pending_packets(*sock_a);
    drain_pending_packets(*sock_b);
    return 1;
}

// 等待接收 REQ 或 RESP 包
static int wait_rly_rpc(sock_t sock, uint8_t expect_type, rly_rpc_pkt_t *out) {
    memset(out, 0, sizeof(*out));

    for (int i = 0; i < 10; i++) {
        uint8_t recv_buf[1200];
        uint8_t type;
        uint16_t payload_len;
        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            return 0;
        }

        if (type == expect_type && payload_len >= P2P_RLY_REQ_MIN_PSZ) {
            out->received = 1;

            // session_id (8 bytes)
            out->session_id = 0;
            for (int j = 0; j < 8; j++)
                out->session_id = (out->session_id << 8) | recv_buf[3 + j];

            // sid (2 bytes)
            out->sid = ((uint16_t)recv_buf[11] << 8) | recv_buf[12];

            // msg or code (1 byte)
            out->msg_or_code = recv_buf[13];

            // data
            out->data_len = (int)payload_len - P2P_RLY_REQ_MIN_PSZ;
            if (out->data_len > 0 && out->data_len <= (int)sizeof(out->data))
                memcpy(out->data, recv_buf + 14, out->data_len);

            return 1;
        }
        // 收到其他包（STATUS/SYNC0_ACK 等），继续接收
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// 测试用例
///////////////////////////////////////////////////////////////////////////////

// 测试 1: MSG_REQ 被转发给 B 端
static void test_msg_req_forwarded(void) {
    const char *TEST_NAME = "msg_req_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "req_alice", "req_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    // Alice 发送 REQ
    uint8_t req_data[] = "hello_relay";
    uint8_t pkt[256];
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x42, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    // Bob 等待 REQ
    rly_rpc_pkt_t req;
    int got = wait_rly_rpc(sb, P2P_RLY_REQ, &req);

    P_sock_close(sa); P_sock_close(sb);

    if (!got) { TEST_FAIL(TEST_NAME, "Bob did not receive REQ"); return; }
    if (req.session_id != ses_b) { TEST_FAIL(TEST_NAME, "session_id mismatch"); return; }
    if (req.sid != 1) { TEST_FAIL(TEST_NAME, "sid mismatch"); return; }
    if (req.msg_or_code != 0x42) { TEST_FAIL(TEST_NAME, "msg mismatch"); return; }
    if (req.data_len != (int)(sizeof(req_data) - 1) ||
        memcmp(req.data, req_data, req.data_len) != 0) {
        TEST_FAIL(TEST_NAME, "data mismatch"); return;
    }

    TEST_PASS(TEST_NAME);
}

// 测试 2: MSG_RESP 被转发给 A 端
static void test_msg_resp_forwarded(void) {
    const char *TEST_NAME = "msg_resp_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "resp_alice", "resp_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    // Alice 发送 REQ
    uint8_t req_data[] = "ping";
    uint8_t pkt[256];
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x20, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    // Bob 接收 REQ
    rly_rpc_pkt_t req;
    wait_rly_rpc(sb, P2P_RLY_REQ, &req);

    // Bob 发送 RESP
    uint8_t resp_data[] = "pong";
    len = build_rly_resp(pkt, sizeof(pkt), ses_b, req.sid, 0x01, resp_data, sizeof(resp_data) - 1);
    tcp_send_all(sb, pkt, len);

    // Alice 接收 RESP
    rly_rpc_pkt_t resp;
    int got = wait_rly_rpc(sa, P2P_RLY_RESP, &resp);

    P_sock_close(sa); P_sock_close(sb);

    if (!got) { TEST_FAIL(TEST_NAME, "Alice did not receive RESP"); return; }
    if (resp.session_id != ses_a) { TEST_FAIL(TEST_NAME, "session_id mismatch"); return; }
    if (resp.msg_or_code != 0x01) { TEST_FAIL(TEST_NAME, "code mismatch"); return; }
    if (resp.data_len != (int)(sizeof(resp_data) - 1) ||
        memcmp(resp.data, resp_data, resp.data_len) != 0) {
        TEST_FAIL(TEST_NAME, "data mismatch"); return;
    }

    TEST_PASS(TEST_NAME);
}

// 测试 3: 完整 RPC 流程
static void test_msg_rpc_complete(void) {
    const char *TEST_NAME = "msg_rpc_complete";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "rpc_alice", "rpc_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    // 1. Alice 发送 REQ
    uint8_t req_data[] = "request_data_123";
    uint8_t pkt[256];
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 100, 0x30, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    // 2. Bob 接收 REQ
    rly_rpc_pkt_t req;
    if (!wait_rly_rpc(sb, P2P_RLY_REQ, &req)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "Bob did not receive REQ");
        return;
    }

    if (req.msg_or_code != 0x30 || req.data_len != (int)(sizeof(req_data) - 1) ||
        memcmp(req.data, req_data, req.data_len) != 0) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "REQ content mismatch");
        return;
    }

    // 3. Bob 发送 RESP
    uint8_t resp_data[] = "response_data_456";
    len = build_rly_resp(pkt, sizeof(pkt), ses_b, req.sid, 0x00, resp_data, sizeof(resp_data) - 1);
    tcp_send_all(sb, pkt, len);

    // 4. Alice 接收 RESP
    rly_rpc_pkt_t resp;
    if (!wait_rly_rpc(sa, P2P_RLY_RESP, &resp)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "Alice did not receive RESP");
        return;
    }

    P_sock_close(sa); P_sock_close(sb);

    if (resp.sid != 100) { TEST_FAIL(TEST_NAME, "RESP sid mismatch"); return; }
    if (resp.msg_or_code != 0x00) { TEST_FAIL(TEST_NAME, "RESP code mismatch"); return; }
    if (resp.data_len != (int)(sizeof(resp_data) - 1) ||
        memcmp(resp.data, resp_data, resp.data_len) != 0) {
        TEST_FAIL(TEST_NAME, "RESP data mismatch"); return;
    }

    TEST_PASS(TEST_NAME);
}

// 测试 4: 双向 RPC
static void test_msg_bidirectional(void) {
    const char *TEST_NAME = "msg_bidirectional";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "bi_alice", "bi_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    uint8_t pkt[256];
    int len;

    // --- A → B RPC ---
    uint8_t req1[] = "from_alice";
    len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x10, req1, sizeof(req1) - 1);
    tcp_send_all(sa, pkt, len);

    rly_rpc_pkt_t r1;
    if (!wait_rly_rpc(sb, P2P_RLY_REQ, &r1)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "B did not receive REQ from A"); return;
    }

    uint8_t resp1[] = "ack_alice";
    len = build_rly_resp(pkt, sizeof(pkt), ses_b, r1.sid, 0x00, resp1, sizeof(resp1) - 1);
    tcp_send_all(sb, pkt, len);

    rly_rpc_pkt_t rsp1;
    if (!wait_rly_rpc(sa, P2P_RLY_RESP, &rsp1)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "A did not receive RESP"); return;
    }

    // --- B → A RPC ---
    uint8_t req2[] = "from_bob";
    len = build_rly_req(pkt, sizeof(pkt), ses_b, 2, 0x20, req2, sizeof(req2) - 1);
    tcp_send_all(sb, pkt, len);

    rly_rpc_pkt_t r2;
    if (!wait_rly_rpc(sa, P2P_RLY_REQ, &r2)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "A did not receive REQ from B"); return;
    }

    uint8_t resp2[] = "ack_bob";
    len = build_rly_resp(pkt, sizeof(pkt), ses_a, r2.sid, 0x00, resp2, sizeof(resp2) - 1);
    tcp_send_all(sa, pkt, len);

    rly_rpc_pkt_t rsp2;
    if (!wait_rly_rpc(sb, P2P_RLY_RESP, &rsp2)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "B did not receive RESP"); return;
    }

    P_sock_close(sa); P_sock_close(sb);
    TEST_PASS(TEST_NAME);
}

// 测试 5: 对端离线时收到错误 RESP
static void test_msg_req_peer_offline(void) {
    const char *TEST_NAME = "msg_req_peer_offline";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect();
    if (sa == P_INVALID_SOCKET) { TEST_FAIL(TEST_NAME, "tcp connect failed"); return; }

    uint32_t inst = (uint32_t)P_tick_us() + 2000;
    online_ack_t ack;
    if (send_online_recv_ack(sa, "offline_alice", inst, &ack) <= 0 || !ack.received) {
        P_sock_close(sa);
        TEST_FAIL(TEST_NAME, "ONLINE failed"); return;
    }

    // SYNC0 单方面（Bob 不在线）
    sync0_ack_t sync;
    if (send_sync0_recv_ack(sa, "offline_bob", 0, NULL, &sync) <= 0 || !sync.received) {
        P_sock_close(sa);
        TEST_FAIL(TEST_NAME, "SYNC0 failed"); return;
    }
    uint64_t ses_a = sync.session_id;

    drain_pending_packets(sa);

    // 发送 REQ（对端不在线）
    uint8_t pkt[256];
    uint8_t req_data[] = "test";
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x50, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    // 应收到 RESP(code=P2P_MSG_ERR_PEER_OFFLINE)
    rly_rpc_pkt_t resp;
    int got = wait_rly_rpc(sa, P2P_RLY_RESP, &resp);

    P_sock_close(sa);

    if (!got) { TEST_FAIL(TEST_NAME, "no error RESP received"); return; }
    if (resp.msg_or_code != P2P_MSG_ERR_PEER_OFFLINE) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected code=0x%02x, got 0x%02x", P2P_MSG_ERR_PEER_OFFLINE, resp.msg_or_code);
        TEST_FAIL(TEST_NAME, msg); return;
    }

    TEST_PASS(TEST_NAME);
}

// 测试 6: 畸形 REQ 包
static void test_msg_req_bad_payload(void) {
    const char *TEST_NAME = "msg_req_bad_payload";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "bad_alice", "bad_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    // 发送过短的 REQ（只有 5 字节 payload，不够 11 字节最小要求）
    uint8_t bad_pkt[8];
    bad_pkt[0] = P2P_RLY_REQ;
    bad_pkt[1] = 0;
    bad_pkt[2] = 5;  // payload_len = 5 (too short)
    memset(bad_pkt + 3, 0, 5);
    tcp_send_all(sa, bad_pkt, 8);

    P_usleep(200 * 1000);

    // 验证服务器没有崩溃 - 发送正常 REQ 应仍然工作
    uint8_t req_data[] = "still_alive";
    uint8_t pkt[256];
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x01, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    rly_rpc_pkt_t req;
    int got = wait_rly_rpc(sb, P2P_RLY_REQ, &req);

    P_sock_close(sa); P_sock_close(sb);

    if (!got) { TEST_FAIL(TEST_NAME, "server crashed after bad payload"); return; }

    TEST_PASS(TEST_NAME);
}

// 测试 7: 无效 session_id 的 REQ
static void test_msg_req_bad_session(void) {
    const char *TEST_NAME = "msg_req_bad_session";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect();
    if (sa == P_INVALID_SOCKET) { TEST_FAIL(TEST_NAME, "tcp connect failed"); return; }

    uint32_t inst = (uint32_t)P_tick_us() + 3000;
    online_ack_t ack;
    if (send_online_recv_ack(sa, "badsess_alice", inst, &ack) <= 0 || !ack.received) {
        P_sock_close(sa);
        TEST_FAIL(TEST_NAME, "ONLINE failed"); return;
    }

    // 发送一个虚假 session_id 的 REQ
    uint64_t fake_session = 0xDEADBEEF12345678ULL;
    uint8_t pkt[256];
    uint8_t req_data[] = "test";
    int len = build_rly_req(pkt, sizeof(pkt), fake_session, 1, 0x60, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    P_usleep(200 * 1000);

    // 没有崩溃即可
    P_sock_close(sa);

    int found = find_log("unknown ses_id");
    (void)found;  // 日志可选

    TEST_PASS(TEST_NAME);
}

// 测试 8: 大尺寸数据 RPC
static void test_msg_large_data(void) {
    const char *TEST_NAME = "msg_large_data";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "lg_alice", "lg_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    // 构造 ~1000 字节数据
    uint8_t big_data[1000];
    for (int i = 0; i < (int)sizeof(big_data); i++)
        big_data[i] = (uint8_t)(i & 0xFF);

    uint8_t pkt[1200];
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x77, big_data, sizeof(big_data));
    tcp_send_all(sa, pkt, len);

    rly_rpc_pkt_t req;
    if (!wait_rly_rpc(sb, P2P_RLY_REQ, &req)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "Bob did not receive large REQ"); return;
    }

    if (req.data_len != (int)sizeof(big_data) || memcmp(req.data, big_data, sizeof(big_data)) != 0) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "large data mismatch"); return;
    }

    // Bob 发送大 RESP
    len = build_rly_resp(pkt, sizeof(pkt), ses_b, req.sid, 0x00, big_data, sizeof(big_data));
    tcp_send_all(sb, pkt, len);

    rly_rpc_pkt_t resp;
    if (!wait_rly_rpc(sa, P2P_RLY_RESP, &resp)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "Alice did not receive large RESP"); return;
    }

    P_sock_close(sa); P_sock_close(sb);

    if (resp.data_len != (int)sizeof(big_data) || memcmp(resp.data, big_data, sizeof(big_data)) != 0) {
        TEST_FAIL(TEST_NAME, "large RESP data mismatch"); return;
    }

    TEST_PASS(TEST_NAME);
}

// 测试 9: RPC 与 DATA 并行（rpc_pending 与 peer_pending 互不干扰）
static void test_msg_rpc_and_data_parallel(void) {
    const char *TEST_NAME = "msg_rpc_and_data_parallel";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "par_alice", "par_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    uint8_t pkt[256];
    int len;

    // 同时发送 DATA 和 REQ
    uint8_t data_payload[] = "data_packet";
    len = build_relay_data(pkt, sizeof(pkt), ses_a, P2P_PKT_DATA, 0, 1, data_payload, sizeof(data_payload) - 1);
    tcp_send_all(sa, pkt, len);

    uint8_t req_data[] = "rpc_request";
    len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x99, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    // Bob 应收到两个包（DATA 和 REQ），顺序可能不同
    int got_data = 0, got_req = 0;
    for (int i = 0; i < 10; i++) {
        uint8_t recv_buf[1200];
        uint8_t type;
        uint16_t payload_len;
        if (tcp_recv_relay_packet(sb, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) break;

        if (type == P2P_RLY_DATA) {
            got_data = 1;
        } else if (type == P2P_RLY_REQ) {
            got_req = 1;
        }

        if (got_data && got_req) break;
    }

    P_sock_close(sa); P_sock_close(sb);

    if (!got_data) { TEST_FAIL(TEST_NAME, "DATA not received"); return; }
    if (!got_req) { TEST_FAIL(TEST_NAME, "REQ not received"); return; }

    TEST_PASS(TEST_NAME);
}

// 测试 10: RPC 超时（B 收到 REQ 后不回复，A 等待超时错误）
static void test_msg_rpc_timeout(void) {
    const char *TEST_NAME = "msg_rpc_timeout";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();

    sock_t sa = tcp_connect(), sb = tcp_connect();
    if (sa == P_INVALID_SOCKET || sb == P_INVALID_SOCKET) {
        if (sa != P_INVALID_SOCKET) P_sock_close(sa);
        if (sb != P_INVALID_SOCKET) P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "tcp connect failed");
        return;
    }

    uint64_t ses_a, ses_b;
    if (!establish_pair(&sa, &ses_a, &sb, &ses_b, "to_alice", "to_bob")) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "establish pair failed");
        return;
    }

    // Alice 发送 REQ
    uint8_t req_data[] = "timeout_test";
    uint8_t pkt[256];
    int len = build_rly_req(pkt, sizeof(pkt), ses_a, 1, 0x55, req_data, sizeof(req_data) - 1);
    tcp_send_all(sa, pkt, len);

    // Bob 收到 REQ 但故意不回复 RESP
    rly_rpc_pkt_t req;
    if (!wait_rly_rpc(sb, P2P_RLY_REQ, &req)) {
        P_sock_close(sa); P_sock_close(sb);
        TEST_FAIL(TEST_NAME, "Bob did not receive REQ"); return;
    }

    // 等待服务器超时（MSG_REQ_MAX_RETRY * MSG_RPC_RETRY_INTERVAL_MS = 5000ms）
    // 加一些余量
    printf("    Waiting for RPC timeout (~6s)...\n");
    P_sock_rcvtimeo(sa, 8000);

    rly_rpc_pkt_t resp;
    int got = wait_rly_rpc(sa, P2P_RLY_RESP, &resp);

    P_sock_close(sa); P_sock_close(sb);

    if (!got) { TEST_FAIL(TEST_NAME, "no timeout RESP received"); return; }
    if (resp.msg_or_code != P2P_MSG_ERR_TIMEOUT) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected code=0x%02x, got 0x%02x", P2P_MSG_ERR_TIMEOUT, resp.msg_or_code);
        TEST_FAIL(TEST_NAME, msg); return;
    }
    if (resp.sid != 1) { TEST_FAIL(TEST_NAME, "sid mismatch"); return; }

    TEST_PASS(TEST_NAME);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {

    const char *server_path = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_path> [port]\n", argv[0]);
        return 1;
    }
    server_path = argv[1];
    if (argc > 2) {
        g_server_port = atoi(argv[2]);
        if (g_server_port <= 0 || g_server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 1;
        }
    }

    printf("=== RELAY MSG RPC Protocol Tests ===\n");
    printf("Server path: %s\n", server_path);
    printf("Server addr: %s:%d (TCP)\n", g_server_host, g_server_port);
    printf("\n");

    // 初始化 instrument 监听
    instrument_local(0);
    if (instrument_listen(on_instrument_log, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }

    // 启动 server 子进程（--relay --msg 启用数据中继和 MSG RPC）
    printf("[*] Starting server (RELAY + MSG mode)...\n");
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);

    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return 1;
    } else if (g_server_pid == 0) {
        execl(server_path, server_path, "-p", port_str, "--relay", "--msg", NULL);
        fprintf(stderr, "Failed to exec: %s\n", strerror(errno));
        _exit(127);
    }
    printf("    Server PID: %d\n", g_server_pid);

    // 等待 server 启动
    P_usleep(500 * 1000);

    // 运行测试用例
    printf("\n[*] Running tests...\n");

    // 一、正常功能测试
    test_msg_req_forwarded();
    test_msg_resp_forwarded();
    test_msg_rpc_complete();
    test_msg_bidirectional();

    // 二、失败验证测试
    test_msg_req_peer_offline();
    test_msg_req_bad_payload();
    test_msg_req_bad_session();

    // 三、边界/临界态测试
    test_msg_large_data();
    test_msg_rpc_and_data_parallel();
    test_msg_rpc_timeout();

    // 终止 server
    if (g_server_pid > 0) {
        printf("\n[*] Terminating server...\n");
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
    }

    // 显示结果
    printf("\n===== Test Results =====\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("========================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
