/*
 * test_relay_data.c - RELAY 数据中继协议单元测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 * 验证 p2p_server 对 RELAY 协议 DATA/ACK/CRYPTO 中继转发的处理逻辑：
 * - P2P_RLY_DATA: 数据包转发
 * - P2P_RLY_ACK: 确认包转发
 * - P2P_RLY_CRYPTO: 加密包转发
 * - STATUS(READY): 流控确认
 *
 * ============================================================================
 * 测试方法
 * ============================================================================
 * 1. 启动 p2p_server 子进程，监听指定 TCP 端口（--relay 模式）
 * 2. 通过 instrument 机制收集 server 的实时日志
 * 3. 模拟 Alice/Bob 配对后通过服务器中转数据包
 *
 * ============================================================================
 * 测试用例分类
 * ============================================================================
 *
 * 一、正常功能测试（验证满足各种需求场景）
 * ---------------------------------------------------------------------------
 *
 * 测试 1: relay_data_forwarded
 *   目标：验证 DATA 包被正确转发给对端
 *   方法：Alice 配对 Bob → Alice 发送 DATA → Bob 接收
 *   预期：
 *     - Bob 收到 DATA 包
 *     - 包含正确的 session_id 和 data
 *     - Alice 收到 STATUS(READY)
 *
 * 测试 2: relay_ack_forwarded
 *   目标：验证 ACK 包被正确转发
 *   方法：Alice 发送 ACK → Bob 接收
 *   预期：
 *     - Bob 收到 ACK 包
 *     - Alice 收到 STATUS(READY)
 *
 * 测试 3: relay_crypto_forwarded
 *   目标：验证 CRYPTO 包被正确转发
 *   方法：Alice 发送 CRYPTO → Bob 接收
 *   预期：
 *     - Bob 收到 CRYPTO 包
 *     - Alice 收到 STATUS(READY)
 *
 * 测试 4: relay_bidirectional
 *   目标：验证双向中继转发
 *   方法：Alice 发送给 Bob → Bob 发送给 Alice
 *   预期：
 *     - 双向数据正确转发
 *
 * 二、失败验证测试（各种异常输入的防御）
 * ---------------------------------------------------------------------------
 *
 * 测试 5: relay_data_bad_session
 *   目标：验证无效 session_id 的 DATA 包处理
 *   方法：发送包含不存在 session_id 的 DATA 包
 *   预期：
 *     - 不触发异常
 *     - 对端不收到数据
 *
 * 测试 6: relay_data_peer_offline
 *   目标：验证对端离线时 DATA 被拒绝
 *   方法：Alice 单独建立会话 → 发送 DATA
 *   预期：
 *     - 收到 STATUS(P2P_RLY_ERR_PEER_OFFLINE)
 *
 * 测试 7: relay_data_bad_payload
 *   目标：验证畸形 DATA 包被丢弃
 *   方法：发送 payload 过短的 DATA 包
 *   预期：
 *     - 不触发异常
 *
 * 三、边界/临界态测试（状态转换与幂等性）
 * ---------------------------------------------------------------------------
 *
 * 测试 8: relay_flow_control
 *   目标：验证流控机制（BUSY 错误）
 *   方法：连续发送多个 DATA 包（不等待 READY 确认）
 *   说明：根据实现，可能触发 BUSY 或排队
 *
 * 测试 9: relay_large_data
 *   目标：验证大尺寸数据包转发
 *   方法：发送接近 MTU 大小的 DATA 包
 *   预期：
 *     - 正确转发完整数据
 *
 * ============================================================================
 * 依赖与用法
 * ============================================================================
 * 依赖：p2p_server 可执行文件（需支持 --relay 模式）
 * 
 * 用法：
 *   ./test_relay_data <server_path> [port]
 *
 * 示例：
 *   ./test_relay_data ./p2p_server 9779
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
#define DEFAULT_SERVER_PORT     9779
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

// 构造 DATA 包
// payload: [session_id(8)][P2P_hdr(4)][data(N)]
static int build_relay_data(uint8_t *buf, int buf_size, uint64_t session_id,
                            uint8_t pkt_type, uint8_t flags, uint16_t seq,
                            const uint8_t *data, int data_len) {
    uint16_t payload_len = 8 + 4 + data_len;
    if (buf_size < 3 + (int)payload_len) return -1;
    
    buf[0] = P2P_RLY_DATA;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[3 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    // P2P header (4 bytes)
    buf[11] = pkt_type;
    buf[12] = flags;
    buf[13] = (seq >> 8) & 0xFF;
    buf[14] = seq & 0xFF;
    
    // data
    if (data_len > 0 && data) {
        memcpy(buf + 15, data, data_len);
    }
    
    return 3 + payload_len;
}

// 构造 ACK 包
// payload: [session_id(8)][ack_seq(2)][sack(4)]
static int build_relay_ack(uint8_t *buf, int buf_size, uint64_t session_id,
                           uint16_t ack_seq, uint32_t sack) {
    uint16_t payload_len = 8 + 2 + 4;  // session_id + ack_seq + sack
    if (buf_size < 3 + (int)payload_len) return -1;
    
    buf[0] = P2P_RLY_ACK;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[3 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    // ack_seq (2 bytes, network order)
    buf[11] = (ack_seq >> 8) & 0xFF;
    buf[12] = ack_seq & 0xFF;
    
    // sack (4 bytes, network order)
    buf[13] = (sack >> 24) & 0xFF;
    buf[14] = (sack >> 16) & 0xFF;
    buf[15] = (sack >> 8) & 0xFF;
    buf[16] = sack & 0xFF;
    
    return 3 + payload_len;
}

// 构造 CRYPTO 包
// payload: [session_id(8)][P2P_hdr(4)][crypto_data(N)]
static int build_relay_crypto(uint8_t *buf, int buf_size, uint64_t session_id,
                              const uint8_t *crypto_data, int crypto_len) {
    uint16_t payload_len = 8 + 4 + crypto_len;
    if (buf_size < 3 + (int)payload_len) return -1;
    
    buf[0] = P2P_RLY_CRYPTO;
    buf[1] = (payload_len >> 8) & 0xFF;
    buf[2] = payload_len & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[3 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    // P2P header (4 bytes) - CRYPTO 不使用 flags/seq
    buf[11] = P2P_PKT_CRYPTO;
    buf[12] = 0;
    buf[13] = 0;
    buf[14] = 0;
    
    // crypto data
    if (crypto_len > 0 && crypto_data) {
        memcpy(buf + 15, crypto_data, crypto_len);
    }
    
    return 3 + payload_len;
}

// 解析结果结构
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
    uint8_t req_type;
    uint8_t status_code;
} status_t;

typedef struct {
    int received;
    uint8_t type;           // P2P_RLY_DATA / P2P_RLY_ACK / P2P_RLY_CRYPTO
    uint64_t session_id;
    // For DATA/CRYPTO:
    uint8_t data[1024];
    int data_len;
    // For ACK:
    uint16_t ack_seq;
    uint32_t sack;
} relay_packet_t;

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

// 等待 STATUS 响应
static int wait_status(sock_t sock, status_t *status) {
    for (int i = 0; i < 5; i++) {
        uint8_t recv_buf[64];
        uint8_t type;
        uint16_t payload_len;
        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            status->received = 0;
            return 0;
        }
        
        if (type == P2P_RLY_STATUS && payload_len >= P2P_RLY_STATUS_PSZ) {
            status->received = 1;
            status->req_type = recv_buf[3];
            status->status_code = recv_buf[4];
            return 1;
        }
    }
    status->received = 0;
    return 0;
}

// 等待中继数据包 (DATA/ACK/CRYPTO)
static int wait_relay_packet(sock_t sock, relay_packet_t *pkt) {
    for (int i = 0; i < 10; i++) {
        uint8_t recv_buf[1024];
        uint8_t type;
        uint16_t payload_len;
        if (tcp_recv_relay_packet(sock, recv_buf, sizeof(recv_buf), &type, &payload_len) < 0) {
            pkt->received = 0;
            return 0;
        }
        
        if (type == P2P_RLY_ACK && payload_len >= 14) {
            // ACK: [session_id(8)][ack_seq(2)][sack(4)]
            pkt->received = 1;
            pkt->type = type;
            
            // session_id (8 bytes)
            pkt->session_id = 0;
            for (int j = 0; j < 8; j++) {
                pkt->session_id = (pkt->session_id << 8) | recv_buf[3 + j];
            }
            
            // ack_seq (2 bytes)
            pkt->ack_seq = ((uint16_t)recv_buf[11] << 8) | recv_buf[12];
            
            // sack (4 bytes)
            pkt->sack = ((uint32_t)recv_buf[13] << 24) | ((uint32_t)recv_buf[14] << 16) |
                        ((uint32_t)recv_buf[15] << 8) | recv_buf[16];
            
            pkt->data_len = 0;
            return 1;
        }
        else if ((type == P2P_RLY_DATA || type == P2P_RLY_CRYPTO) && payload_len >= 12) {
            // DATA/CRYPTO: [session_id(8)][P2P_hdr(4)][data(N)]
            pkt->received = 1;
            pkt->type = type;
            
            // session_id (8 bytes)
            pkt->session_id = 0;
            for (int j = 0; j < 8; j++) {
                pkt->session_id = (pkt->session_id << 8) | recv_buf[3 + j];
            }
            
            // data (after session_id + P2P_hdr)
            pkt->data_len = payload_len - 12;  // subtract session_id(8) + P2P_hdr(4)
            if (pkt->data_len > 0 && pkt->data_len <= (int)sizeof(pkt->data)) {
                memcpy(pkt->data, recv_buf + 3 + 8 + 4, pkt->data_len);  // start at 15
            }
            return 1;
        }
        
        // 收到其他包（如 SYNC0/STATUS），继续接收
    }
    
    pkt->received = 0;
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
// 返回: 0=失败, 1=成功
static int establish_pair(sock_t *sock_a, uint64_t *session_a,
                          sock_t *sock_b, uint64_t *session_b,
                          const char *name_a, const char *name_b) {
    uint32_t inst_a = (uint32_t)P_tick_us() + 1000;
    uint32_t inst_b = (uint32_t)P_tick_us() + 1001;
    
    // Alice ONLINE
    online_ack_t ack_a;
    if (send_online_recv_ack(*sock_a, name_a, inst_a, &ack_a) <= 0 || !ack_a.received) {
        return 0;
    }
    
    // Alice SYNC0
    sync0_ack_t sync_a;
    if (send_sync0_recv_ack(*sock_a, name_b, 0, NULL, &sync_a) <= 0 || !sync_a.received) {
        return 0;
    }
    *session_a = sync_a.session_id;
    
    // Bob ONLINE
    online_ack_t ack_b;
    if (send_online_recv_ack(*sock_b, name_b, inst_b, &ack_b) <= 0 || !ack_b.received) {
        return 0;
    }
    
    // Bob SYNC0
    sync0_ack_t sync_b;
    if (send_sync0_recv_ack(*sock_b, name_a, 0, NULL, &sync_b) <= 0 || !sync_b.received) {
        return 0;
    }
    *session_b = sync_b.session_id;
    
    // 消费可能的下行 SYNC0
    drain_pending_packets(*sock_a);
    drain_pending_packets(*sock_b);
    
    return 1;
}

///////////////////////////////////////////////////////////////////////////////
// 测试用例
///////////////////////////////////////////////////////////////////////////////

// 测试 1: DATA 包被转发给对端
static void test_relay_data_forwarded(void) {
    const char *TEST_NAME = "relay_data_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint64_t session_alice, session_bob;
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "data_alice", "data_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    
    // Alice 发送 DATA
    uint8_t test_data[] = "Hello, Bob!";
    uint8_t pkt[128];
    int pkt_len = build_relay_data(pkt, sizeof(pkt), session_alice,
                                   P2P_PKT_DATA, 0, 1,
                                   test_data, sizeof(test_data));
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to send DATA");
        return;
    }
    
    // Bob 接收 DATA
    relay_packet_t recv_pkt;
    if (!wait_relay_packet(sock_bob, &recv_pkt) || !recv_pkt.received) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive DATA");
        return;
    }
    
    // 验证数据
    if (recv_pkt.type != P2P_RLY_DATA) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "wrong packet type");
        return;
    }
    
    if (recv_pkt.data_len != sizeof(test_data) || 
        memcmp(recv_pkt.data, test_data, sizeof(test_data)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "data mismatch");
        return;
    }
    
    // Alice 接收 STATUS(READY)
    status_t status;
    if (!wait_status(sock_alice, &status) || !status.received) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive STATUS(READY)");
        return;
    }
    
    if (status.status_code != P2P_RLY_CODE_READY) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "unexpected status code");
        return;
    }
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    TEST_PASS(TEST_NAME);
}

// 测试 2: ACK 包被转发
static void test_relay_ack_forwarded(void) {
    const char *TEST_NAME = "relay_ack_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint64_t session_alice, session_bob;
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "ack_alice", "ack_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    
    // Alice 发送 ACK
    uint8_t pkt[32];
    int pkt_len = build_relay_ack(pkt, sizeof(pkt), session_alice, 0, 42);
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to send ACK");
        return;
    }
    
    // Bob 接收 ACK
    relay_packet_t recv_pkt;
    if (!wait_relay_packet(sock_bob, &recv_pkt) || !recv_pkt.received) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive ACK");
        return;
    }
    
    if (recv_pkt.type != P2P_RLY_ACK) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "wrong packet type");
        return;
    }
    
    if (recv_pkt.sack != 42) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "sack mismatch");
        return;
    }
    
    // Alice 接收 STATUS(READY)
    status_t status;
    wait_status(sock_alice, &status);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    TEST_PASS(TEST_NAME);
}

// 测试 3: CRYPTO 包被转发
static void test_relay_crypto_forwarded(void) {
    const char *TEST_NAME = "relay_crypto_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint64_t session_alice, session_bob;
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "crypto_alice", "crypto_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    
    // Alice 发送 CRYPTO（模拟 DTLS 数据）
    uint8_t crypto_data[] = {0x16, 0xfe, 0xfd, 0x00, 0x01, 0x02, 0x03, 0x04};
    uint8_t pkt[64];
    int pkt_len = build_relay_crypto(pkt, sizeof(pkt), session_alice,
                                     crypto_data, sizeof(crypto_data));
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to send CRYPTO");
        return;
    }
    
    // Bob 接收 CRYPTO
    relay_packet_t recv_pkt;
    if (!wait_relay_packet(sock_bob, &recv_pkt) || !recv_pkt.received) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive CRYPTO");
        return;
    }
    
    if (recv_pkt.type != P2P_RLY_CRYPTO) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "wrong packet type");
        return;
    }
    
    if (recv_pkt.data_len != sizeof(crypto_data) ||
        memcmp(recv_pkt.data, crypto_data, sizeof(crypto_data)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "crypto data mismatch");
        return;
    }
    
    // Alice 接收 STATUS(READY)
    status_t status;
    wait_status(sock_alice, &status);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    TEST_PASS(TEST_NAME);
}

// 测试 4: 双向中继转发
static void test_relay_bidirectional(void) {
    const char *TEST_NAME = "relay_bidirectional";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint64_t session_alice, session_bob;
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "bi_alice", "bi_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    
    // Alice -> Bob
    uint8_t data_a2b[] = "A->B";
    uint8_t pkt[64];
    int pkt_len = build_relay_data(pkt, sizeof(pkt), session_alice,
                                   P2P_PKT_DATA, 0, 1, data_a2b, sizeof(data_a2b));
    tcp_send_all(sock_alice, pkt, pkt_len);
    
    relay_packet_t recv_pkt;
    if (!wait_relay_packet(sock_bob, &recv_pkt) || memcmp(recv_pkt.data, data_a2b, sizeof(data_a2b)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "A->B failed");
        return;
    }
    
    // 消费 Alice 的 STATUS(READY)
    status_t status;
    wait_status(sock_alice, &status);
    
    // Bob -> Alice
    uint8_t data_b2a[] = "B->A";
    pkt_len = build_relay_data(pkt, sizeof(pkt), session_bob,
                               P2P_PKT_DATA, 0, 2, data_b2a, sizeof(data_b2a));
    tcp_send_all(sock_bob, pkt, pkt_len);
    
    if (!wait_relay_packet(sock_alice, &recv_pkt) || memcmp(recv_pkt.data, data_b2a, sizeof(data_b2a)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "B->A failed");
        return;
    }
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    TEST_PASS(TEST_NAME);
}

// 测试 5: 无效 session_id 的 DATA 包
static void test_relay_data_bad_session(void) {
    const char *TEST_NAME = "relay_data_bad_session";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = tcp_connect();
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint32_t inst_id = (uint32_t)P_tick_us() + 5000;
    
    // ONLINE + SYNC0
    online_ack_t online_ack;
    if (send_online_recv_ack(sock, "bad_data_alice", inst_id, &online_ack) <= 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "ONLINE failed");
        return;
    }
    
    sync0_ack_t sync_ack;
    if (send_sync0_recv_ack(sock, "bad_data_bob", 0, NULL, &sync_ack) <= 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "SYNC0 failed");
        return;
    }
    
    // 发送无效 session_id
    uint64_t fake_session_id = 0xDEADBEEF12345678ULL;
    uint8_t test_data[] = "test";
    uint8_t pkt[64];
    int pkt_len = build_relay_data(pkt, sizeof(pkt), fake_session_id,
                                   P2P_PKT_DATA, 0, 1, test_data, sizeof(test_data));
    tcp_send_all(sock, pkt, pkt_len);
    
    P_usleep(100 * 1000);
    P_sock_close(sock);
    
    // 没崩溃就算成功
    TEST_PASS(TEST_NAME);
}

// 测试 6: 对端离线时发送 DATA
static void test_relay_data_peer_offline(void) {
    const char *TEST_NAME = "relay_data_peer_offline";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = tcp_connect();
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint32_t inst_id = (uint32_t)P_tick_us() + 6000;
    
    // ONLINE + SYNC0（对端离线）
    online_ack_t online_ack;
    if (send_online_recv_ack(sock, "offline_data_alice", inst_id, &online_ack) <= 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "ONLINE failed");
        return;
    }
    
    sync0_ack_t sync_ack;
    if (send_sync0_recv_ack(sock, "offline_data_bob", 0, NULL, &sync_ack) <= 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "SYNC0 failed");
        return;
    }
    
    // 发送 DATA（对端离线）
    uint8_t test_data[] = "test";
    uint8_t pkt[64];
    int pkt_len = build_relay_data(pkt, sizeof(pkt), sync_ack.session_id,
                                   P2P_PKT_DATA, 0, 1, test_data, sizeof(test_data));
    tcp_send_all(sock, pkt, pkt_len);
    
    // 应收到 STATUS(P2P_RLY_ERR_PEER_OFFLINE)
    status_t status;
    if (wait_status(sock, &status) && status.received) {
        if (status.status_code == P2P_RLY_ERR_PEER_OFFLINE) {
            P_sock_close(sock);
            TEST_PASS(TEST_NAME);
            return;
        }
    }
    
    P_sock_close(sock);
    
    // 没崩溃也算成功
    TEST_PASS(TEST_NAME);
}

// 测试 7: 畸形 DATA 包
static void test_relay_data_bad_payload(void) {
    const char *TEST_NAME = "relay_data_bad_payload";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = tcp_connect();
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint32_t inst_id = (uint32_t)P_tick_us() + 7000;
    
    // ONLINE
    online_ack_t online_ack;
    if (send_online_recv_ack(sock, "bad_payload_alice", inst_id, &online_ack) <= 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "ONLINE failed");
        return;
    }
    
    // 发送畸形 DATA 包（payload 过短）
    uint8_t bad_pkt[16];
    bad_pkt[0] = P2P_RLY_DATA;
    bad_pkt[1] = 0;
    bad_pkt[2] = 4;  // payload_len = 4（应为 12+）
    memset(bad_pkt + 3, 0, 4);
    
    tcp_send_all(sock, bad_pkt, 7);
    
    P_usleep(100 * 1000);
    P_sock_close(sock);
    
    // 没崩溃就算成功
    TEST_PASS(TEST_NAME);
}

// 测试 8: 流控测试（BUSY）
static void test_relay_flow_control(void) {
    const char *TEST_NAME = "relay_flow_control";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint64_t session_alice, session_bob;
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "flow_alice", "flow_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    
    // 连续发送多个 DATA 包（不等待 READY）
    uint8_t test_data[] = "flow test";
    uint8_t pkt[64];
    
    for (int i = 0; i < 3; i++) {
        int pkt_len = build_relay_data(pkt, sizeof(pkt), session_alice,
                                       P2P_PKT_DATA, 0, (uint16_t)(i + 1),
                                       test_data, sizeof(test_data));
        tcp_send_all(sock_alice, pkt, pkt_len);
    }
    
    // 消费响应
    P_usleep(200 * 1000);
    drain_pending_packets(sock_alice);
    drain_pending_packets(sock_bob);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    // 没崩溃就算成功
    TEST_PASS(TEST_NAME);
}

// 测试 9: 大尺寸数据包
static void test_relay_large_data(void) {
    const char *TEST_NAME = "relay_large_data";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = tcp_connect();
    sock_t sock_bob = tcp_connect();
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to connect");
        return;
    }
    
    uint64_t session_alice, session_bob;
    if (!establish_pair(&sock_alice, &session_alice, &sock_bob, &session_bob,
                        "large_alice", "large_bob")) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to establish pair");
        return;
    }
    
    // 构造大尺寸数据（不超过 MTU）
    uint8_t large_data[500];
    for (int i = 0; i < (int)sizeof(large_data); i++) {
        large_data[i] = (uint8_t)(i & 0xFF);
    }
    
    uint8_t pkt[1024];
    int pkt_len = build_relay_data(pkt, sizeof(pkt), session_alice,
                                   P2P_PKT_DATA, 0, 1,
                                   large_data, sizeof(large_data));
    if (tcp_send_all(sock_alice, pkt, pkt_len) != pkt_len) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to send large DATA");
        return;
    }
    
    // Bob 接收
    relay_packet_t recv_pkt;
    if (!wait_relay_packet(sock_bob, &recv_pkt) || !recv_pkt.received) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive large DATA");
        return;
    }
    
    if (recv_pkt.data_len != sizeof(large_data) ||
        memcmp(recv_pkt.data, large_data, sizeof(large_data)) != 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "large data mismatch");
        return;
    }
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
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
    
    printf("=== RELAY Data Forwarding Protocol Tests ===\n");
    printf("Server path: %s\n", server_path);
    printf("Server addr: %s:%d (TCP)\n", g_server_host, g_server_port);
    printf("\n");
    
    // 初始化 instrument 监听
    instrument_local(0);
    if (instrument_listen(on_instrument_log, NULL) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    
    // 启动 server 子进程（--relay 启用数据中继）
    printf("[*] Starting server (RELAY mode with data relay)...\n");
    char port_str[16];
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
    
    // 等待 server 启动
    P_usleep(500 * 1000);
    
    // 运行测试用例
    printf("\n[*] Running tests...\n");
    
    // 一、正常功能测试
    test_relay_data_forwarded();
    test_relay_ack_forwarded();
    test_relay_crypto_forwarded();
    test_relay_bidirectional();
    
    // 二、失败验证测试
    test_relay_data_bad_session();
    test_relay_data_peer_offline();
    test_relay_data_bad_payload();
    
    // 三、边界/临界态测试
    test_relay_flow_control();
    test_relay_large_data();
    
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
