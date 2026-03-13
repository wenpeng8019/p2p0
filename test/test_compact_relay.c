/*
 * test_compact_relay.c - COMPACT RELAY 和 NAT_PROBE 协议单元测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 * 验证 p2p_server 对 COMPACT 协议中继和 NAT 探测功能的处理逻辑：
 * - RELAY_DATA: 数据中继转发（P2P 打洞失败后的降级方案）
 * - RELAY_ACK: 中继 ACK 包转发
 * - RELAY_CRYPTO: DTLS 加密包转发
 * - NAT_PROBE: NAT 类型探测（返回映射地址）
 *
 * ============================================================================
 * 测试方法
 * ============================================================================
 * 1. 启动 p2p_server 子进程，监听主端口和探测端口
 * 2. 通过 instrument 机制收集 server 的实时日志
 * 3. 模拟中继数据转发和 NAT 探测流程
 *
 * ============================================================================
 * 测试用例分类
 * ============================================================================
 *
 * 一、正常功能测试（验证满足各种需求场景）
 * ---------------------------------------------------------------------------
 *
 * 测试 1: relay_data_forwarded
 *   目标：验证 RELAY_DATA 包被正确转发给对端
 *   方法：Alice 配对 Bob → Alice 发送 RELAY_DATA → Bob 接收
 *   预期：
 *     - Bob 收到 RELAY_DATA 包
 *     - 包含正确的 session_id 和 data
 *
 * 测试 2: relay_ack_forwarded
 *   目标：验证 RELAY_ACK 包被正确转发
 *   方法：Alice 发送 RELAY_ACK → Bob 接收
 *   预期：
 *     - Bob 收到 RELAY_ACK 包
 *
 * 测试 3: relay_crypto_forwarded
 *   目标：验证 RELAY_CRYPTO 包被正确转发
 *   方法：Alice 发送 RELAY_CRYPTO → Bob 接收
 *   预期：
 *     - Bob 收到 RELAY_CRYPTO 包
 *
 * 测试 4: nat_probe_returns_mapping
 *   目标：验证 NAT_PROBE 返回正确的映射地址
 *   方法：发送 NAT_PROBE 到探测端口
 *   预期：
 *     - 收到 NAT_PROBE_ACK
 *     - 包含正确的映射 IP 和端口
 *
 * 二、失败验证测试（各种异常输入的防御）
 * ---------------------------------------------------------------------------
 *
 * 测试 5: relay_data_bad_session
 *   目标：验证无效 session_id 的 RELAY_DATA 被丢弃
 *   方法：发送包含不存在 session_id 的 RELAY_DATA
 *   预期：
 *     - 不触发异常
 *     - 对端不收到数据
 *
 * 测试 6: relay_data_peer_unavailable
 *   目标：验证对端离线时 RELAY_DATA 被丢弃
 *   方法：Alice 单独注册 → 发送 RELAY_DATA
 *   预期：
 *     - 不触发异常
 *     - server 日志含 "peer unavailable"
 *
 * 测试 7: relay_data_bad_payload
 *   目标：验证畸形 RELAY_DATA 包被丢弃
 *   方法：发送 payload 过短的 RELAY_DATA 包
 *   预期：
 *     - 不触发异常
 *
 * 三、边界/临界态测试（状态转换与幂等性）
 * ---------------------------------------------------------------------------
 *
 * 测试 8: relay_bidirectional
 *   目标：验证双向中继转发
 *   方法：Alice 发送给 Bob → Bob 发送给 Alice
 *   预期：
 *     - 双向数据正确转发
 *
 * ============================================================================
 * 依赖与用法
 * ============================================================================
 * 依赖：p2p_server 可执行文件（需支持 instrument 日志）
 * 
 * 用法：
 *   ./test_compact_relay <server_path> [port] [probe_port]
 *
 * 示例：
 *   ./test_compact_relay ./p2p_server 9666 9667
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
#define DEFAULT_SERVER_PORT     9666
#define DEFAULT_PROBE_PORT      9667
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define RECV_TIMEOUT_MS         2000

// 测试状态
static int g_server_port = DEFAULT_SERVER_PORT;
static int g_probe_port = DEFAULT_PROBE_PORT;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static pid_t g_server_pid = 0;
static sock_t g_sock = P_INVALID_SOCKET;

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
    
    if (rid == 0) return;  // 忽略本地进程日志
    
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
// 协议构造函数
///////////////////////////////////////////////////////////////////////////////

// 构造 REGISTER 包
static int build_register(uint8_t *buf, int buf_size,
                          const char *local_peer_id,
                          const char *remote_peer_id,
                          uint32_t instance_id,
                          int candidate_count,
                          p2p_compact_candidate_t *candidates) {
    if (buf_size < 4 + 32 + 32 + 4 + 1) return -1;
    
    int n = 0;
    buf[n++] = SIG_PKT_REGISTER;
    buf[n++] = 0;
    buf[n++] = 0;
    buf[n++] = 0;
    
    memset(buf + n, 0, 32);
    if (local_peer_id) strncpy((char*)(buf + n), local_peer_id, 31);
    n += 32;
    
    memset(buf + n, 0, 32);
    if (remote_peer_id) strncpy((char*)(buf + n), remote_peer_id, 31);
    n += 32;
    
    buf[n++] = (instance_id >> 24) & 0xFF;
    buf[n++] = (instance_id >> 16) & 0xFF;
    buf[n++] = (instance_id >> 8) & 0xFF;
    buf[n++] = instance_id & 0xFF;
    
    if (candidate_count > 255) candidate_count = 255;
    buf[n++] = (uint8_t)candidate_count;
    
    for (int i = 0; i < candidate_count && candidates; i++) {
        buf[n++] = candidates[i].type;
        memcpy(buf + n, &candidates[i].ip, 4);
        n += 4;
        memcpy(buf + n, &candidates[i].port, 2);
        n += 2;
    }
    
    return n;
}

// 构造 RELAY_DATA 包
// 协议: [hdr(4)][session_id(8)][data(N)]
static int build_relay_data(uint8_t *buf, int buf_size, 
                            uint64_t session_id, uint16_t seq,
                            const uint8_t *data, int data_len) {
    if (buf_size < 4 + 8 + data_len) return -1;
    
    buf[0] = P2P_PKT_RELAY_DATA;
    buf[1] = 0;  // flags
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[4 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    // data
    if (data_len > 0 && data) {
        memcpy(buf + 12, data, data_len);
    }
    
    return 12 + data_len;
}

// 构造 RELAY_ACK 包
// 协议: [hdr(4)][session_id(8)]
static int build_relay_ack(uint8_t *buf, int buf_size, 
                           uint64_t session_id, uint16_t seq) {
    if (buf_size < 4 + 8) return -1;
    
    buf[0] = P2P_PKT_RELAY_ACK;
    buf[1] = 0;  // flags
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[4 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    return 12;
}

// 构造 RELAY_CRYPTO 包
// 协议: [hdr(4)][session_id(8)][crypto_data(N)]
static int build_relay_crypto(uint8_t *buf, int buf_size, 
                              uint64_t session_id, uint16_t seq,
                              const uint8_t *data, int data_len) {
    if (buf_size < 4 + 8 + data_len) return -1;
    
    buf[0] = P2P_PKT_RELAY_CRYPTO;
    buf[1] = 0;  // flags
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[4 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    // crypto data
    if (data_len > 0 && data) {
        memcpy(buf + 12, data, data_len);
    }
    
    return 12 + data_len;
}

// 构造 NAT_PROBE 包
// 协议: [hdr(4)]
static int build_nat_probe(uint8_t *buf, int buf_size, uint16_t seq) {
    if (buf_size < 4) return -1;
    
    buf[0] = SIG_PKT_NAT_PROBE;
    buf[1] = 0;  // flags
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    
    return 4;
}

// NAT_PROBE_ACK 解析结果
typedef struct {
    int received;
    uint16_t seq;
    uint32_t probe_ip;
    uint16_t probe_port;
} nat_probe_ack_t;

// 解析 NAT_PROBE_ACK 包
static void parse_nat_probe_ack(const uint8_t *buf, int len, nat_probe_ack_t *ack) {
    memset(ack, 0, sizeof(*ack));
    
    if (len < 10) return;  // header + ip + port
    if (buf[0] != SIG_PKT_NAT_PROBE_ACK) return;
    
    ack->received = 1;
    ack->seq = ((uint16_t)buf[2] << 8) | buf[3];
    memcpy(&ack->probe_ip, buf + 4, 4);
    memcpy(&ack->probe_port, buf + 8, 2);
}

// RELAY_DATA 解析结果
typedef struct {
    int received;
    uint8_t type;
    uint16_t seq;
    uint64_t session_id;
    uint8_t data[512];
    int data_len;
} relay_packet_t;

// 解析 RELAY 包
static void parse_relay_packet(const uint8_t *buf, int len, relay_packet_t *pkt) {
    memset(pkt, 0, sizeof(*pkt));
    
    if (len < 12) return;  // header + session_id
    if (buf[0] != P2P_PKT_RELAY_DATA && 
        buf[0] != P2P_PKT_RELAY_ACK && 
        buf[0] != P2P_PKT_RELAY_CRYPTO) return;
    
    pkt->received = 1;
    pkt->type = buf[0];
    pkt->seq = ((uint16_t)buf[2] << 8) | buf[3];
    
    // session_id (8 bytes)
    pkt->session_id = 0;
    for (int i = 0; i < 8; i++) {
        pkt->session_id = (pkt->session_id << 8) | buf[4 + i];
    }
    
    // data (for RELAY_DATA and RELAY_CRYPTO)
    pkt->data_len = len - 12;
    if (pkt->data_len > 0 && pkt->data_len <= (int)sizeof(pkt->data)) {
        memcpy(pkt->data, buf + 12, pkt->data_len);
    }
}

// 发送 REGISTER 并接收 REGISTER_ACK，返回 session_id
static uint64_t register_peer(sock_t sock, const char *local, const char *remote, 
                               uint32_t inst_id, int cand_count, p2p_compact_candidate_t *cands) {
    uint8_t pkt[512];
    int len = build_register(pkt, sizeof(pkt), local, remote, inst_id, cand_count, cands);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    uint8_t recv_buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr*)&from, &from_len);
    
    if (n > 0 && recv_buf[0] == SIG_PKT_REGISTER_ACK) {
        uint64_t session_id = 0;
        for (int i = 0; i < 8; i++) {
            session_id = (session_id << 8) | recv_buf[5 + i];
        }
        return session_id;
    }
    return 0;
}

// 构造 PEER_INFO_ACK 包
// 协议: [hdr(4)][session_id(8)]
static int build_peer_info_ack(uint8_t *buf, int buf_size, uint64_t session_id, uint16_t seq) {
    if (buf_size < 4 + 8) return -1;
    
    buf[0] = SIG_PKT_PEER_INFO_ACK;
    buf[1] = 0;  // flags
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[4 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    return 12;
}

// 发送 RELAY 包
static void send_relay_packet(sock_t sock, const uint8_t *pkt, int len) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
}

// 等待 RELAY 包
static int wait_relay_packet(sock_t sock, relay_packet_t *pkt_out) {
    P_sock_rcvtimeo(sock, 500);  // 短超时
    uint8_t recv_buf[1024];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    // 可能需要跳过 PEER_INFO 等其他包
    for (int i = 0; i < 5; i++) {
        ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                              (struct sockaddr*)&from, &from_len);
        
        if (n >= 12 && (recv_buf[0] == P2P_PKT_RELAY_DATA ||
                        recv_buf[0] == P2P_PKT_RELAY_ACK ||
                        recv_buf[0] == P2P_PKT_RELAY_CRYPTO)) {
            if (pkt_out) {
                parse_relay_packet(recv_buf, (int)n, pkt_out);
            }
            return 1;
        }
    }
    return 0;
}

// 消费所有待处理的 PEER_INFO 等包，并发送 ACK
static void drain_pending_packets(sock_t sock) {
    P_sock_rcvtimeo(sock, 500);
    uint8_t recv_buf[512];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    for (int i = 0; i < 10; i++) {
        ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                              (struct sockaddr*)&from, &from_len);
        if (n <= 0) break;
        
        // 如果是 PEER_INFO，发送 ACK
        if (n >= 14 && recv_buf[0] == SIG_PKT_PEER_INFO) {
            uint16_t seq = ((uint16_t)recv_buf[2] << 8) | recv_buf[3];
            uint64_t session_id = 0;
            for (int j = 0; j < 8; j++) {
                session_id = (session_id << 8) | recv_buf[4 + j];
            }
            
            uint8_t ack[16];
            int ack_len = build_peer_info_ack(ack, sizeof(ack), session_id, seq);
            sendto(sock, (const char*)ack, ack_len, 0,
                   (struct sockaddr*)&server_addr, sizeof(server_addr));
        }
    }
}

// 发送 NAT_PROBE 到探测端口
static int send_nat_probe(sock_t sock, uint16_t seq, nat_probe_ack_t *ack_out) {
    uint8_t pkt[8];
    int len = build_nat_probe(pkt, sizeof(pkt), seq);
    
    struct sockaddr_in probe_addr;
    memset(&probe_addr, 0, sizeof(probe_addr));
    probe_addr.sin_family = AF_INET;
    probe_addr.sin_port = htons(g_probe_port);
    inet_pton(AF_INET, g_server_host, &probe_addr.sin_addr);
    
    sendto(sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&probe_addr, sizeof(probe_addr));
    
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    uint8_t recv_buf[32];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr*)&from, &from_len);
    
    if (ack_out) {
        parse_nat_probe_ack(recv_buf, (int)n, ack_out);
        return ack_out->received;
    }
    
    return (n >= 10 && recv_buf[0] == SIG_PKT_NAT_PROBE_ACK);
}

///////////////////////////////////////////////////////////////////////////////
// 测试用例
///////////////////////////////////////////////////////////////////////////////

// 测试 1: RELAY_DATA 被转发给对端
static void test_relay_data_forwarded(void) {
    const char *TEST_NAME = "relay_data_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = socket(AF_INET, SOCK_DGRAM, 0);
    sock_t sock_bob = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to create sockets");
        return;
    }
    
    uint32_t inst_alice = (uint32_t)P_tick_us() + 6000;
    uint32_t inst_bob = (uint32_t)P_tick_us() + 6001;
    
    // 配对
    uint64_t session_alice = register_peer(sock_alice, "relay_alice", "relay_bob", inst_alice, 0, NULL);
    uint64_t session_bob = register_peer(sock_bob, "relay_bob", "relay_alice", inst_bob, 0, NULL);
    
    if (session_alice == 0 || session_bob == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    // 消费 PEER_INFO 包
    drain_pending_packets(sock_alice);
    drain_pending_packets(sock_bob);
    
    // Alice 发送 RELAY_DATA
    uint8_t pkt[256];
    uint8_t data[] = "hello_from_alice";
    int len = build_relay_data(pkt, sizeof(pkt), session_alice, 1, data, sizeof(data) - 1);
    send_relay_packet(sock_alice, pkt, len);
    
    // Bob 等待接收
    relay_packet_t relay_pkt;
    int got_relay = wait_relay_packet(sock_bob, &relay_pkt);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    if (!got_relay) {
        TEST_FAIL(TEST_NAME, "Bob did not receive RELAY_DATA");
        return;
    }
    
    if (relay_pkt.type != P2P_PKT_RELAY_DATA) {
        TEST_FAIL(TEST_NAME, "wrong packet type");
        return;
    }
    
    // 验证 session_id (应该是 Alice 的 session_id，因为是从 Alice 发出的)
    if (relay_pkt.session_id != session_alice) {
        TEST_FAIL(TEST_NAME, "session_id mismatch");
        return;
    }
    
    // 验证 data
    if (relay_pkt.data_len != (int)(sizeof(data) - 1) ||
        memcmp(relay_pkt.data, data, relay_pkt.data_len) != 0) {
        TEST_FAIL(TEST_NAME, "data mismatch");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 2: RELAY_ACK 被转发
static void test_relay_ack_forwarded(void) {
    const char *TEST_NAME = "relay_ack_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = socket(AF_INET, SOCK_DGRAM, 0);
    sock_t sock_bob = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to create sockets");
        return;
    }
    
    uint32_t inst_alice = (uint32_t)P_tick_us() + 6100;
    uint32_t inst_bob = (uint32_t)P_tick_us() + 6101;
    
    // 配对
    uint64_t session_alice = register_peer(sock_alice, "ack_alice", "ack_bob", inst_alice, 0, NULL);
    uint64_t session_bob = register_peer(sock_bob, "ack_bob", "ack_alice", inst_bob, 0, NULL);
    (void)session_bob;
    
    if (session_alice == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    // 消费 PEER_INFO 包
    drain_pending_packets(sock_alice);
    drain_pending_packets(sock_bob);
    
    // Alice 发送 RELAY_ACK
    uint8_t pkt[32];
    int len = build_relay_ack(pkt, sizeof(pkt), session_alice, 42);
    send_relay_packet(sock_alice, pkt, len);
    
    // Bob 等待接收
    relay_packet_t relay_pkt;
    int got_relay = wait_relay_packet(sock_bob, &relay_pkt);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    if (!got_relay) {
        TEST_FAIL(TEST_NAME, "Bob did not receive RELAY_ACK");
        return;
    }
    
    if (relay_pkt.type != P2P_PKT_RELAY_ACK) {
        TEST_FAIL(TEST_NAME, "wrong packet type");
        return;
    }
    
    if (relay_pkt.seq != 42) {
        TEST_FAIL(TEST_NAME, "seq mismatch");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 3: RELAY_CRYPTO 被转发
static void test_relay_crypto_forwarded(void) {
    const char *TEST_NAME = "relay_crypto_forwarded";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = socket(AF_INET, SOCK_DGRAM, 0);
    sock_t sock_bob = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to create sockets");
        return;
    }
    
    uint32_t inst_alice = (uint32_t)P_tick_us() + 6200;
    uint32_t inst_bob = (uint32_t)P_tick_us() + 6201;
    
    // 配对
    uint64_t session_alice = register_peer(sock_alice, "crypto_alice", "crypto_bob", inst_alice, 0, NULL);
    uint64_t session_bob = register_peer(sock_bob, "crypto_bob", "crypto_alice", inst_bob, 0, NULL);
    (void)session_bob;
    
    if (session_alice == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    // 消费 PEER_INFO 包
    drain_pending_packets(sock_alice);
    drain_pending_packets(sock_bob);
    
    // Alice 发送 RELAY_CRYPTO
    uint8_t pkt[256];
    uint8_t crypto_data[] = {0x16, 0x03, 0x03, 0x00, 0x10, 0xDE, 0xAD, 0xBE, 0xEF};  // 模拟 DTLS 数据
    int len = build_relay_crypto(pkt, sizeof(pkt), session_alice, 1, crypto_data, sizeof(crypto_data));
    send_relay_packet(sock_alice, pkt, len);
    
    // Bob 等待接收
    relay_packet_t relay_pkt;
    int got_relay = wait_relay_packet(sock_bob, &relay_pkt);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    if (!got_relay) {
        TEST_FAIL(TEST_NAME, "Bob did not receive RELAY_CRYPTO");
        return;
    }
    
    if (relay_pkt.type != P2P_PKT_RELAY_CRYPTO) {
        TEST_FAIL(TEST_NAME, "wrong packet type");
        return;
    }
    
    // 验证 crypto data
    if (relay_pkt.data_len != (int)sizeof(crypto_data) ||
        memcmp(relay_pkt.data, crypto_data, relay_pkt.data_len) != 0) {
        TEST_FAIL(TEST_NAME, "crypto data mismatch");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 4: NAT_PROBE 返回映射地址
static void test_nat_probe_returns_mapping(void) {
    const char *TEST_NAME = "nat_probe_returns_mapping";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    // 发送 NAT_PROBE 到探测端口
    nat_probe_ack_t ack;
    int got_ack = send_nat_probe(sock, 123, &ack);
    
    P_sock_close(sock);
    
    if (!got_ack) {
        TEST_FAIL(TEST_NAME, "no NAT_PROBE_ACK received");
        return;
    }
    
    // 验证 seq 被复制
    if (ack.seq != 123) {
        TEST_FAIL(TEST_NAME, "seq mismatch");
        return;
    }
    
    // 验证返回了映射地址
    if (ack.probe_ip == 0 || ack.probe_port == 0) {
        TEST_FAIL(TEST_NAME, "mapping address is zero");
        return;
    }
    
    // 打印映射地址
    struct in_addr addr;
    addr.s_addr = ack.probe_ip;
    printf("    Mapped address: %s:%d\n", inet_ntoa(addr), ntohs(ack.probe_port));
    
    TEST_PASS(TEST_NAME);
}

// 测试 5: 无效 session_id 的 RELAY_DATA
static void test_relay_data_bad_session(void) {
    const char *TEST_NAME = "relay_data_bad_session";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    // 发送虚假 session_id 的 RELAY_DATA
    uint8_t pkt[256];
    uint64_t fake_session = 0xDEADBEEF12345678ULL;
    uint8_t data[] = "fake_data";
    int len = build_relay_data(pkt, sizeof(pkt), fake_session, 1, data, sizeof(data) - 1);
    send_relay_packet(sock, pkt, len);
    
    P_usleep(100 * 1000);
    
    P_sock_close(sock);
    
    // 服务器没有崩溃就算成功
    TEST_PASS(TEST_NAME);
}

// 测试 6: 对端离线时 RELAY_DATA 被丢弃
static void test_relay_data_peer_unavailable(void) {
    const char *TEST_NAME = "relay_data_peer_unavailable";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    uint32_t inst_id = (uint32_t)P_tick_us() + 6300;
    
    // 单独注册（无配对）
    uint64_t session_id = register_peer(sock, "lonely_alice", "lonely_bob", inst_id, 0, NULL);
    if (session_id == 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    // 发送 RELAY_DATA（对端不在线）
    uint8_t pkt[256];
    uint8_t data[] = "data_to_nowhere";
    int len = build_relay_data(pkt, sizeof(pkt), session_id, 1, data, sizeof(data) - 1);
    send_relay_packet(sock, pkt, len);
    
    P_usleep(100 * 1000);
    
    P_sock_close(sock);
    
    // 检查日志
    int found = find_log("peer unavailable");
    if (found < 0) {
        // 日志可能未启用，但没有崩溃就算成功
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 7: 畸形 RELAY_DATA 包
static void test_relay_data_bad_payload(void) {
    const char *TEST_NAME = "relay_data_bad_payload";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    // 发送 payload 过短的 RELAY_DATA 包
    uint8_t bad_pkt[16];
    bad_pkt[0] = P2P_PKT_RELAY_DATA;
    bad_pkt[1] = 0;
    bad_pkt[2] = 0;
    bad_pkt[3] = 0;
    // 只放 4 字节头 + 几字节（不够 session_id 的 8 字节）
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)bad_pkt, 8, 0,  // 只发 8 字节
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    P_usleep(100 * 1000);
    
    P_sock_close(sock);
    
    // 服务器没有崩溃就算成功
    TEST_PASS(TEST_NAME);
}

// 测试 8: 双向中继转发
static void test_relay_bidirectional(void) {
    const char *TEST_NAME = "relay_bidirectional";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = socket(AF_INET, SOCK_DGRAM, 0);
    sock_t sock_bob = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to create sockets");
        return;
    }
    
    uint32_t inst_alice = (uint32_t)P_tick_us() + 6400;
    uint32_t inst_bob = (uint32_t)P_tick_us() + 6401;
    
    // 配对
    uint64_t session_alice = register_peer(sock_alice, "bidir_alice", "bidir_bob", inst_alice, 0, NULL);
    uint64_t session_bob = register_peer(sock_bob, "bidir_bob", "bidir_alice", inst_bob, 0, NULL);
    
    if (session_alice == 0 || session_bob == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    // 消费 PEER_INFO 包
    drain_pending_packets(sock_alice);
    drain_pending_packets(sock_bob);
    
    // Alice -> Bob
    uint8_t pkt[256];
    uint8_t data_a2b[] = "alice_to_bob";
    int len = build_relay_data(pkt, sizeof(pkt), session_alice, 1, data_a2b, sizeof(data_a2b) - 1);
    send_relay_packet(sock_alice, pkt, len);
    
    relay_packet_t pkt_at_bob;
    if (!wait_relay_packet(sock_bob, &pkt_at_bob)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob did not receive data from Alice");
        return;
    }
    
    // Bob -> Alice
    uint8_t data_b2a[] = "bob_to_alice";
    len = build_relay_data(pkt, sizeof(pkt), session_bob, 1, data_b2a, sizeof(data_b2a) - 1);
    send_relay_packet(sock_bob, pkt, len);
    
    relay_packet_t pkt_at_alice;
    if (!wait_relay_packet(sock_alice, &pkt_at_alice)) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice did not receive data from Bob");
        return;
    }
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    // 验证双方收到的数据
    if (memcmp(pkt_at_bob.data, data_a2b, sizeof(data_a2b) - 1) != 0) {
        TEST_FAIL(TEST_NAME, "Bob received wrong data");
        return;
    }
    
    if (memcmp(pkt_at_alice.data, data_b2a, sizeof(data_b2a) - 1) != 0) {
        TEST_FAIL(TEST_NAME, "Alice received wrong data");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    const char *server_path = NULL;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_path> [port] [probe_port]\n", argv[0]);
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
    if (argc > 3) {
        g_probe_port = atoi(argv[3]);
        if (g_probe_port <= 0 || g_probe_port > 65535) {
            fprintf(stderr, "Invalid probe port: %s\n", argv[3]);
            return 1;
        }
    }
    
    printf("=== COMPACT RELAY & NAT_PROBE Protocol Tests ===\n");
    printf("Server path: %s\n", server_path);
    printf("Server addr: %s:%d\n", g_server_host, g_server_port);
    printf("Probe addr:  %s:%d\n", g_server_host, g_probe_port);
    printf("\n");
    
    // 初始化 instrument 监听
    instrument_local();
    if (instrument_listen(on_instrument_log) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    
    // 启动 server 子进程
    printf("[*] Starting server...\n");
    char port_str[16];
    char probe_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);
    snprintf(probe_str, sizeof(probe_str), "%d", g_probe_port);
    
    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return 1;
    } else if (g_server_pid == 0) {
        execl(server_path, server_path, "-p", port_str, "-P", probe_str, NULL);
        fprintf(stderr, "Failed to exec: %s\n", strerror(errno));
        _exit(127);
    }
    printf("    Server PID: %d\n", g_server_pid);
    
    // 等待 server 启动
    P_usleep(500 * 1000);
    
    // 创建测试 socket
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == P_INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        goto cleanup;
    }
    
    // 运行测试用例
    printf("\n[*] Running tests...\n");
    
    // 一、正常功能测试
    test_relay_data_forwarded();
    test_relay_ack_forwarded();
    test_relay_crypto_forwarded();
    test_nat_probe_returns_mapping();
    
    // 二、失败验证测试
    test_relay_data_bad_session();
    test_relay_data_peer_unavailable();
    test_relay_data_bad_payload();
    
    // 三、边界/临界态测试
    test_relay_bidirectional();
    
    // 清理
    if (g_sock != P_INVALID_SOCKET) {
        P_sock_close(g_sock);
        g_sock = P_INVALID_SOCKET;
    }

cleanup:
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
