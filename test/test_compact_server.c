/*
 * test_compact_server.c - COMPACT 服务端协议全面测试套件
 *
 * 覆盖范围（基于 p2p_server/server.c 实际实现）：
 *
 * Part 1:  REGISTER + 双向配对 + PEER_INFO(seq=0) 首包格式
 * Part 2:  session_id 分配正确性
 * Part 3:  PEER_INFO_ACK 包格式与处理
 * Part 4:  PEER_INFO(seq>0) relay 转发
 * Part 5:  RELAY_DATA / RELAY_ACK relay 转发
 * Part 6:  UNREGISTER -> PEER_OFF 通知
 * Part 7:  超时清理 -> PEER_OFF 通知
 * Part 8:  NAT_PROBE 响应格式
 * Part 9:  ALIVE / ALIVE_ACK
 * Part 10: 错误包处理
 * Part 11: REGISTER_ACK 字段（relay flag、probe_port、max_candidates）
 * Part 12: 候选列表边界（截断上限、空候选、非对称候选数）
 * Part 13: 离线缓存与首次匹配
 * Part 14: 地址变化、超时重连
 * Part 15: peer 指针状态机（NULL->valid->-1->NULL）
 * Part 16: 多对隔离与槽位边界
 *
 * 编译独立：不依赖 p2p_static 库，仅链接平台基础库
 */

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

#ifndef _WIN32
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#else
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

/* ============================================================================
 * 内嵌协议常量
 * ============================================================================ */

#define P2P_PEER_ID_MAX          32

#define SIG_PKT_REGISTER         0x80
#define SIG_PKT_REGISTER_ACK     0x81
#define SIG_PKT_ALIVE            0x82
#define SIG_PKT_ALIVE_ACK        0x83
#define SIG_PKT_PEER_INFO        0x84
#define SIG_PKT_PEER_INFO_ACK    0x85
#define SIG_PKT_NAT_PROBE        0x86
#define SIG_PKT_NAT_PROBE_ACK    0x87
#define SIG_PKT_UNREGISTER       0x88
#define SIG_PKT_PEER_OFF         0x89

#define P2P_PKT_RELAY_DATA       0xA0
#define P2P_PKT_RELAY_ACK        0xA1

#define SIG_REGACK_PEER_OFFLINE  0
#define SIG_REGACK_PEER_ONLINE   1
#define SIG_REGACK_FLAG_RELAY    0x01

/* ============================================================================
 * 跨平台 htonll / ntohll
 * ============================================================================ */

#if defined(__linux__)
#  include <endian.h>
#  define test_htonll(x) htobe64(x)
#  define test_ntohll(x) be64toh(x)
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#  include <libkern/OSByteOrder.h>
#  define test_htonll(x) OSSwapHostToBigInt64(x)
#  define test_ntohll(x) OSSwapBigToHostInt64(x)
#elif defined(_WIN32)
static inline uint64_t test_htonll(uint64_t x) {
    return ((uint64_t)htonl((uint32_t)x) << 32) | htonl((uint32_t)(x >> 32));
}
static inline uint64_t test_ntohll(uint64_t x) {
    return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
}
#else
static inline uint64_t test_htonll(uint64_t x) {
    uint32_t hi = (uint32_t)(x >> 32), lo = (uint32_t)x;
    return ((uint64_t)htonl(lo) << 32) | htonl(hi);
}
static inline uint64_t test_ntohll(uint64_t x) { return test_htonll(x); }
#endif

static bool g_verbose = true;
#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 * Mock infrastructure
 * ============================================================================ */

#define MOCK_MAX_PEERS       64
#define MOCK_MAX_CANDIDATES  10
#define MOCK_PAIR_TIMEOUT    30

typedef struct {
    uint8_t  buf[512];
    size_t   len;
    uint32_t dst_ip;
    uint16_t dst_port;
} sent_packet_t;

#define MAX_SENT_PKTS 128
static sent_packet_t g_sent[MAX_SENT_PKTS];
static int           g_sent_count = 0;

static void mock_sendto(const uint8_t *buf, size_t len,
                        uint32_t dst_ip, uint16_t dst_port) {
    if (g_sent_count >= MAX_SENT_PKTS) return;
    sent_packet_t *p = &g_sent[g_sent_count++];
    if (len > sizeof(p->buf)) len = sizeof(p->buf);
    memcpy(p->buf, buf, len);
    p->len = len; p->dst_ip = dst_ip; p->dst_port = dst_port;
}

static void mock_clear_sent(void) {
    g_sent_count = 0; memset(g_sent, 0, sizeof(g_sent));
}

static sent_packet_t *mock_find_sent(uint32_t dst_ip, uint16_t dst_port, uint8_t t) {
    for (int i = 0; i < g_sent_count; i++)
        if (g_sent[i].len >= 4 && g_sent[i].dst_ip == dst_ip &&
            g_sent[i].dst_port == dst_port && g_sent[i].buf[0] == t) return &g_sent[i];
    return NULL;
}

static sent_packet_t *mock_find_sent_any(uint8_t t) {
    for (int i = 0; i < g_sent_count; i++)
        if (g_sent[i].len >= 4 && g_sent[i].buf[0] == t) return &g_sent[i];
    return NULL;
}

typedef struct mock_pair_s {
    bool                valid;
    uint64_t            session_id;
    char                local_id[P2P_PEER_ID_MAX];
    char                remote_id[P2P_PEER_ID_MAX];
    uint32_t            addr_ip;
    uint16_t            addr_port;
    uint8_t             cand_type[MOCK_MAX_CANDIDATES];
    uint32_t            cand_ip[MOCK_MAX_CANDIDATES];
    uint16_t            cand_port[MOCK_MAX_CANDIDATES];
    int                 candidate_count;
    struct mock_pair_s *peer;
    time_t              last_active;
    bool                info0_acked;
    bool                in_pending;
    uint8_t             addr_notify_seq;    // 地址变更通知序号（1-255 循环）
} mock_pair_t;

static mock_pair_t g_mock_pairs[MOCK_MAX_PEERS];
static bool        g_relay_enabled = false;
static int         g_probe_port    = 0;
static uint64_t    g_next_sid      = 1000;

static mock_pair_t *mock_find_by_session(uint64_t sid) {
    if (!sid) return NULL;
    for (int i = 0; i < MOCK_MAX_PEERS; i++)
        if (g_mock_pairs[i].valid && g_mock_pairs[i].session_id == sid)
            return &g_mock_pairs[i];
    return NULL;
}

static mock_pair_t *mock_find_by_peer(const char *local, const char *remote) {
    for (int i = 0; i < MOCK_MAX_PEERS; i++)
        if (g_mock_pairs[i].valid &&
            strncmp(g_mock_pairs[i].local_id,  local,  P2P_PEER_ID_MAX) == 0 &&
            strncmp(g_mock_pairs[i].remote_id, remote, P2P_PEER_ID_MAX) == 0)
            return &g_mock_pairs[i];
    return NULL;
}

static mock_pair_t *mock_alloc_pair(void) {
    for (int i = 0; i < MOCK_MAX_PEERS; i++)
        if (!g_mock_pairs[i].valid) {
            memset(&g_mock_pairs[i], 0, sizeof(g_mock_pairs[i]));
            return &g_mock_pairs[i];
        }
    return NULL;
}

static uint64_t mock_generate_sid(void) {
    uint64_t id = g_next_sid++;
    while (mock_find_by_session(id)) id = g_next_sid++;
    return id;
}

static void mock_server_init(void) {
    memset(g_mock_pairs, 0, sizeof(g_mock_pairs));
    g_next_sid = 1000; g_relay_enabled = false; g_probe_port = 0;
    mock_clear_sent();
}

static void mock_send_peer_info0(mock_pair_t *to_pair, mock_pair_t *from_pair) {
    uint8_t buf[4 + 8 + 2 + MOCK_MAX_CANDIDATES * 7];
    buf[0]=SIG_PKT_PEER_INFO; buf[1]=0; buf[2]=0; buf[3]=0;
    uint64_t sid_net = test_htonll(to_pair->session_id);
    memcpy(buf+4, &sid_net, 8);
    buf[12] = 0;
    buf[13] = (uint8_t)from_pair->candidate_count;
    int len = 14;
    for (int i = 0; i < from_pair->candidate_count; i++) {
        buf[len] = from_pair->cand_type[i];
        memcpy(buf+len+1, &from_pair->cand_ip[i],   4);
        memcpy(buf+len+5, &from_pair->cand_port[i], 2);
        len += 7;
    }
    mock_sendto(buf, len, to_pair->addr_ip, to_pair->addr_port);
    to_pair->in_pending = true; to_pair->info0_acked = false;
    to_pair->last_active = time(NULL);
}

static void mock_send_addr_change_notify(mock_pair_t *to_pair, mock_pair_t *from_pair, uint8_t base_index) {
    uint8_t buf[4 + 8 + 2 + 7];  // 包头(4) + session_id(8) + base_index(1) + count(1) + candidate(7)
    buf[0]=SIG_PKT_PEER_INFO; buf[1]=0; buf[2]=0; buf[3]=0;  // seq=0
    uint64_t sid_net = test_htonll(to_pair->session_id);
    memcpy(buf+4, &sid_net, 8);
    buf[12] = base_index;  // base_index!=0 表示地址变更通知
    buf[13] = 1;           // candidate_count 必须为 1
    // 候选地址：from_pair 的新公网地址
    buf[14] = 1;  // type = Srflx
    memcpy(buf+15, &from_pair->addr_ip,   4);
    memcpy(buf+19, &from_pair->addr_port, 2);
    mock_sendto(buf, 21, to_pair->addr_ip, to_pair->addr_port);
    to_pair->in_pending = true;  // 加入待确认队列
}

typedef struct {
    uint8_t  status;
    uint8_t  max_candidates;
    uint32_t public_ip;
    uint16_t public_port;
    uint16_t probe_port;
    uint8_t  relay_flag;
} mock_register_ack_t;

typedef struct { uint8_t type; uint32_t ip; uint16_t port; } mock_cand_t;

static mock_register_ack_t mock_handle_register(
    const char *local, const char *remote,
    uint32_t from_ip, uint16_t from_port,
    const mock_cand_t *cands, int cand_count)
{
    mock_register_ack_t ack = {0};
    ack.max_candidates = MOCK_MAX_CANDIDATES;
    ack.public_ip      = from_ip;
    ack.public_port    = from_port;
    ack.probe_port     = (g_probe_port > 0) ? htons((uint16_t)g_probe_port) : 0;
    ack.relay_flag     = g_relay_enabled ? SIG_REGACK_FLAG_RELAY : 0;

    mock_pair_t *lo = mock_find_by_peer(local, remote);
    if (!lo) {
        lo = mock_alloc_pair();
        if (!lo) { ack.status = 2; return ack; }
        lo->valid = true;
        strncpy(lo->local_id,  local,  P2P_PEER_ID_MAX - 1);
        strncpy(lo->remote_id, remote, P2P_PEER_ID_MAX - 1);
        lo->peer = NULL;
    }
    
    // 检测地址是否变化
    bool addr_changed = (lo->addr_ip != from_ip || lo->addr_port != from_port);
    
    if (lo->peer == (mock_pair_t*)(void*)-1) lo->peer = NULL;
    lo->addr_ip = from_ip; lo->addr_port = from_port;
    int cap = (cand_count > MOCK_MAX_CANDIDATES) ? MOCK_MAX_CANDIDATES : cand_count;
    lo->candidate_count = cap;
    for (int i = 0; i < cap; i++) {
        lo->cand_type[i] = cands ? cands[i].type : 0;
        lo->cand_ip[i]   = cands ? cands[i].ip   : 0;
        lo->cand_port[i] = cands ? cands[i].port  : 0;
    }
    lo->last_active = time(NULL);

    mock_pair_t *re = (remote[0] != '\0') ? mock_find_by_peer(remote, local) : NULL;
    ack.status = re ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE;

    {
        uint8_t buf[14];
        buf[0] = SIG_PKT_REGISTER_ACK; buf[1] = ack.relay_flag; buf[2] = 0; buf[3] = 0;
        buf[4] = ack.status; buf[5] = ack.max_candidates;
        memcpy(buf+6,  &from_ip,        4);
        memcpy(buf+10, &from_port,      2);
        memcpy(buf+12, &ack.probe_port, 2);
        mock_sendto(buf, 14, from_ip, from_port);
    }
    
    if (re && (lo->peer == NULL || re->peer == NULL)) {
        // 首次配对
        lo->peer = re; re->peer = lo;
        if (!lo->session_id) lo->session_id = mock_generate_sid();
        if (!re->session_id) re->session_id = mock_generate_sid();
        mock_send_peer_info0(lo, re);
        mock_send_peer_info0(re, lo);
    } else if (re && lo->peer == re && addr_changed) {
        // 已配对但地址变化：向对端发送地址变更通知
        if (re->info0_acked) {
            re->addr_notify_seq = (uint8_t)(re->addr_notify_seq + 1);
            if (re->addr_notify_seq == 0) re->addr_notify_seq = 1;  // 0 保留给首包
            mock_send_addr_change_notify(re, lo, re->addr_notify_seq);
        }
    }
    return ack;
}

static void mock_handle_unregister(const char *local, const char *remote) {
    mock_pair_t *pair = mock_find_by_peer(local, remote);
    if (!pair) return;
    if (pair->peer && pair->peer != (mock_pair_t*)(void*)-1 && pair->peer->session_id) {
        mock_pair_t *peer = pair->peer;
        uint8_t buf[12]; buf[0]=SIG_PKT_PEER_OFF; buf[1]=0; buf[2]=0; buf[3]=0;
        uint64_t n = test_htonll(peer->session_id); memcpy(buf+4, &n, 8);
        mock_sendto(buf, 12, peer->addr_ip, peer->addr_port);
        peer->peer = (mock_pair_t*)(void*)-1;
    }
    pair->valid = false; pair->session_id = 0; pair->peer = NULL;
}

static void mock_handle_peer_info_ack(const uint8_t *payload, size_t plen,
                                      uint32_t from_ip, uint16_t from_port) {
    if (plen < 10) return;
    uint64_t sid; memcpy(&sid, payload, 8); sid = test_ntohll(sid);
    uint16_t ack_seq; memcpy(&ack_seq, payload+8, 2); ack_seq = ntohs(ack_seq);
    mock_pair_t *pair = mock_find_by_session(sid);
    if (!pair) return;
    if (ack_seq == 0) {
        if (!pair->info0_acked) { pair->info0_acked = true; pair->in_pending = false; }
    } else if (pair->peer && pair->peer != (mock_pair_t*)(void*)-1) {
        uint8_t fwd[14]; fwd[0]=SIG_PKT_PEER_INFO_ACK; fwd[1]=0; fwd[2]=0; fwd[3]=0;
        memcpy(fwd+4, payload, 10);
        mock_sendto(fwd, 14, pair->peer->addr_ip, pair->peer->addr_port);
    }
    (void)from_ip; (void)from_port;
}

static bool mock_handle_relay(uint8_t pkt_type, uint16_t seq,
                               const uint8_t *payload, size_t plen) {
    if (pkt_type == SIG_PKT_PEER_INFO && seq == 0) return false;
    if (plen < 8) return false;
    uint64_t sid; memcpy(&sid, payload, 8); sid = test_ntohll(sid);
    mock_pair_t *pair = mock_find_by_session(sid);
    if (!pair || !pair->peer || pair->peer == (mock_pair_t*)(void*)-1) return false;
    uint8_t fwd[4 + 512];
    fwd[0] = pkt_type; fwd[1] = 0;
    fwd[2] = (uint8_t)(seq >> 8); fwd[3] = (uint8_t)(seq & 0xFF);
    size_t cp = (plen > 512) ? 512 : plen;
    memcpy(fwd+4, payload, cp);
    mock_sendto(fwd, 4+cp, pair->peer->addr_ip, pair->peer->addr_port);
    return true;
}

static void mock_handle_nat_probe(uint16_t req_seq, uint32_t from_ip, uint16_t from_port) {
    uint8_t buf[10];
    buf[0]=SIG_PKT_NAT_PROBE_ACK; buf[1]=0;
    buf[2]=(uint8_t)(req_seq>>8); buf[3]=(uint8_t)(req_seq&0xFF);
    memcpy(buf+4, &from_ip, 4); memcpy(buf+8, &from_port, 2);
    mock_sendto(buf, 10, from_ip, from_port);
}

static bool mock_handle_alive(const char *local, const char *remote) {
    mock_pair_t *pair = mock_find_by_peer(local, remote);
    if (!pair) return false;
    pair->last_active = time(NULL);
    uint8_t buf[4] = {SIG_PKT_ALIVE_ACK, 0, 0, 0};
    mock_sendto(buf, 4, pair->addr_ip, pair->addr_port);
    return true;
}

static int mock_cleanup_timeout(void) {
    time_t now = time(NULL); int count = 0;
    for (int i = 0; i < MOCK_MAX_PEERS; i++) {
        if (!g_mock_pairs[i].valid || (now - g_mock_pairs[i].last_active) <= MOCK_PAIR_TIMEOUT)
            continue;
        mock_pair_t *pair = &g_mock_pairs[i];
        if (pair->peer && pair->peer != (mock_pair_t*)(void*)-1 && pair->peer->session_id) {
            mock_pair_t *peer = pair->peer;
            uint8_t buf[12]; buf[0]=SIG_PKT_PEER_OFF; buf[1]=0; buf[2]=0; buf[3]=0;
            uint64_t n = test_htonll(peer->session_id); memcpy(buf+4, &n, 8);
            mock_sendto(buf, 12, peer->addr_ip, peer->addr_port);
            peer->peer = (mock_pair_t*)(void*)-1;
        }
        pair->valid = false; pair->session_id = 0; pair->peer = NULL;
        count++;
    }
    return count;
}

static uint64_t read_u64_be(const uint8_t *p) {
    uint64_t v; memcpy(&v, p, 8); return test_ntohll(v);
}
static uint16_t read_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* ============================================================================
 * Part 1: REGISTER + bilateral PEER_INFO(seq=0)
 * ============================================================================ */

TEST(register_bilateral_peer_info_sent) {
    TEST_LOG("Two peers register -> both receive PEER_INFO(seq=0)");
    mock_server_init();
    mock_cand_t ca = {0, htonl(0x0A000001), htons(5001)};
    mock_cand_t cb = {0, htonl(0x0A000002), htons(6001)};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(10001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(10002);
    mock_register_ack_t ack1 = mock_handle_register("alice","bob",  ip_a,port_a,&ca,1);
    ASSERT_EQ(ack1.status, SIG_REGACK_PEER_OFFLINE);
    mock_clear_sent();
    mock_register_ack_t ack2 = mock_handle_register("bob",  "alice",ip_b,port_b,&cb,1);
    ASSERT_EQ(ack2.status, SIG_REGACK_PEER_ONLINE);
    ASSERT_NOT_NULL(mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO));
    ASSERT_NOT_NULL(mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO));
}

TEST(peer_info0_seq_field_is_zero) {
    TEST_LOG("PEER_INFO header.seq == 0");
    mock_server_init();
    mock_cand_t c = {0, htonl(0x0A000001), htons(5000)};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(11001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(11002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_clear_sent();
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    sent_packet_t *pi = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi);
    ASSERT_EQ(read_u16_be(pi->buf + 2), 0);
}

TEST(peer_info0_contains_session_id) {
    TEST_LOG("PEER_INFO payload[0..7] == recipient session_id");
    mock_server_init();
    mock_cand_t c = {0, htonl(0x0A000001), htons(5000)};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(12001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(12002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_clear_sent();
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a);
    ASSERT(pi_a->len >= 12);
    uint64_t sid_in_pkt = read_u64_be(pi_a->buf + 4);
    ASSERT(sid_in_pkt != 0);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    ASSERT_NOT_NULL(alice);
    ASSERT_EQ(sid_in_pkt, alice->session_id);
}

TEST(peer_info0_contains_remote_candidates) {
    TEST_LOG("PEER_INFO to Alice carries Bob's 3 candidates");
    mock_server_init();
    mock_cand_t ca = {0, htonl(0x0A000001), htons(5000)};
    mock_cand_t cb[3] = {{0,htonl(0x0B000001),htons(6000)},
                         {1,htonl(0x02020202),htons(7000)},
                         {2,htonl(0xC0A80001),htons(3478)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(13001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(13002);
    mock_handle_register("alice","bob",  ip_a,port_a,&ca,1);
    mock_clear_sent();
    mock_handle_register("bob",  "alice",ip_b,port_b,cb,3);
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a);
    ASSERT(pi_a->len >= 14);
    ASSERT_EQ(pi_a->buf[12], 0);
    ASSERT_EQ(pi_a->buf[13], 3);
    ASSERT_EQ(pi_a->len, (size_t)(14 + 3*7));
    sent_packet_t *pi_b = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_b);
    ASSERT_EQ(pi_b->buf[13], 1);
}

TEST(register_first_match_only_sends_peer_info_once) {
    TEST_LOG("Re-register when paired does NOT resend PEER_INFO");
    mock_server_init();
    mock_cand_t c = {0, htonl(0x0A000001), htons(5000)};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(14001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(14002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_clear_sent();
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    int first = 0;
    for (int i = 0; i < g_sent_count; i++) if (g_sent[i].buf[0] == SIG_PKT_PEER_INFO) first++;
    ASSERT_EQ(first, 2);
    mock_clear_sent();
    mock_handle_register("alice","bob",ip_a,port_a,&c,1);
    int re = 0;
    for (int i = 0; i < g_sent_count; i++) if (g_sent[i].buf[0] == SIG_PKT_PEER_INFO) re++;
    ASSERT_EQ(re, 0);
}

/* ============================================================================
 * Part 2: session_id assignment
 * ============================================================================ */

TEST(session_id_zero_before_match) {
    TEST_LOG("session_id == 0 until both peers register");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(20001),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    ASSERT_NOT_NULL(alice);
    ASSERT_EQ(alice->session_id, (uint64_t)0);
}

TEST(session_id_nonzero_after_match) {
    TEST_LOG("Both sids non-zero after bilateral match");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(21001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(21002),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",  "alice");
    ASSERT(alice->session_id != 0);
    ASSERT(bob->session_id   != 0);
}

TEST(session_id_distinct_per_direction) {
    TEST_LOG("alice_sid != bob_sid");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(22001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(22002),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",  "alice");
    ASSERT(alice->session_id != bob->session_id);
}

TEST(session_id_unique_across_pairs) {
    TEST_LOG("6 sids across 3 pairs all distinct");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("a1","b1",htonl(0x7F000001),htons(23001),&c,1);
    mock_handle_register("b1","a1",htonl(0x7F000001),htons(23002),&c,1);
    mock_handle_register("a2","b2",htonl(0x7F000001),htons(23003),&c,1);
    mock_handle_register("b2","a2",htonl(0x7F000001),htons(23004),&c,1);
    mock_handle_register("a3","b3",htonl(0x7F000001),htons(23005),&c,1);
    mock_handle_register("b3","a3",htonl(0x7F000001),htons(23006),&c,1);
    const char *names[6][2] = {{"a1","b1"},{"b1","a1"},{"a2","b2"},
                                {"b2","a2"},{"a3","b3"},{"b3","a3"}};
    uint64_t sids[6];
    for (int i = 0; i < 6; i++) {
        mock_pair_t *p = mock_find_by_peer(names[i][0], names[i][1]);
        ASSERT_NOT_NULL(p); sids[i] = p->session_id; ASSERT(sids[i] != 0);
    }
    for (int i = 0; i < 6; i++) for (int j = i+1; j < 6; j++) ASSERT(sids[i] != sids[j]);
}

/* ============================================================================
 * Part 3: PEER_INFO_ACK
 * ============================================================================ */

TEST(peer_info_ack_seq0_clears_pending) {
    TEST_LOG("PEER_INFO_ACK(seq=0) -> info0_acked=true, in_pending=false");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(30001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(30002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    uint8_t pl[10]; uint64_t n = test_htonll(alice->session_id);
    memcpy(pl,&n,8); pl[8]=pl[9]=0;
    mock_handle_peer_info_ack(pl, 10, ip_a, port_a);
    ASSERT_EQ(alice->info0_acked, true);
    ASSERT_EQ(alice->in_pending,  false);
}

TEST(peer_info_ack_seq0_short_payload_dropped) {
    TEST_LOG("PEER_INFO_ACK payload < 10 bytes dropped");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(31001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(31002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    bool before = alice->info0_acked;
    uint8_t short_pl[4] = {0,1,2,3};
    mock_handle_peer_info_ack(short_pl, 4, ip_a, port_a);
    ASSERT_EQ(alice->info0_acked, before);
}

TEST(peer_info_ack_seq_positive_relayed) {
    TEST_LOG("PEER_INFO_ACK(seq=3) relayed to peer");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(32001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(32002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    uint8_t pl[10]; uint64_t n = test_htonll(alice->session_id); memcpy(pl,&n,8);
    uint16_t aseq = htons(3); memcpy(pl+8, &aseq, 2);
    mock_clear_sent();
    mock_handle_peer_info_ack(pl, 10, ip_a, port_a);
    sent_packet_t *fwd = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO_ACK);
    ASSERT_NOT_NULL(fwd);
    ASSERT_EQ(read_u16_be(fwd->buf + 4 + 8), 3);
}

TEST(peer_info_ack_seq0_idempotent) {
    TEST_LOG("Duplicate PEER_INFO_ACK(seq=0) is idempotent");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(33001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(33002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    uint8_t pl[10]; uint64_t n = test_htonll(alice->session_id);
    memcpy(pl,&n,8); pl[8]=pl[9]=0;
    mock_handle_peer_info_ack(pl, 10, ip_a, port_a);
    mock_handle_peer_info_ack(pl, 10, ip_a, port_a);
    ASSERT_EQ(alice->info0_acked, true);
}

/* ============================================================================
 * Part 4: PEER_INFO(seq>0) relay
 * ============================================================================ */

TEST(peer_info_seq_positive_relayed) {
    TEST_LOG("PEER_INFO(seq=2) relayed, seq preserved");
    mock_server_init();
    mock_cand_t ca = {0, htonl(0x0A000001), htons(5000)};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(40001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(40002);
    mock_handle_register("alice","bob",  ip_a,port_a,&ca,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&ca,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    uint8_t pl[17];
    uint64_t n = test_htonll(alice->session_id); memcpy(pl,&n,8);
    pl[8]=0; pl[9]=1; pl[10]=0; memcpy(pl+11,&ca.ip,4); memcpy(pl+15,&ca.port,2);
    mock_clear_sent();
    ASSERT_EQ(mock_handle_relay(SIG_PKT_PEER_INFO, 2, pl, sizeof(pl)), true);
    sent_packet_t *fwd = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(fwd);
    ASSERT_EQ(read_u16_be(fwd->buf + 2), 2);
}

TEST(peer_info_seq0_from_client_rejected) {
    TEST_LOG("PEER_INFO(seq=0) from client -> rejected");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(41001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(41002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    uint8_t pl[8]; uint64_t n = test_htonll(alice->session_id); memcpy(pl,&n,8);
    mock_clear_sent();
    ASSERT_EQ(mock_handle_relay(SIG_PKT_PEER_INFO, 0, pl, 8), false);
    ASSERT_EQ(g_sent_count, 0);
}

/* ============================================================================
 * Part 5: RELAY_DATA / RELAY_ACK
 * ============================================================================ */

TEST(relay_data_forwarded_to_peer) {
    TEST_LOG("RELAY_DATA forwarded, payload intact");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(50001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(50002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    uint8_t pl[15];
    uint64_t n = test_htonll(alice->session_id); memcpy(pl,&n,8);
    pl[8]=0; pl[9]=5; pl[10]='h'; pl[11]='e'; pl[12]='l'; pl[13]='l'; pl[14]='o';
    mock_clear_sent();
    ASSERT_EQ(mock_handle_relay(P2P_PKT_RELAY_DATA, 1, pl, sizeof(pl)), true);
    sent_packet_t *fwd = mock_find_sent(ip_b, port_b, P2P_PKT_RELAY_DATA);
    ASSERT_NOT_NULL(fwd);
    ASSERT_EQ(fwd->buf[4 + 10], 'h');
}

TEST(relay_ack_forwarded_to_peer) {
    TEST_LOG("RELAY_ACK forwarded to peer");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(51001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(51002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    uint8_t pl[10]; uint64_t n = test_htonll(bob->session_id);
    memcpy(pl,&n,8); pl[8]=0; pl[9]=7;
    mock_clear_sent();
    ASSERT_EQ(mock_handle_relay(P2P_PKT_RELAY_ACK, 0, pl, sizeof(pl)), true);
    ASSERT_NOT_NULL(mock_find_sent(ip_a, port_a, P2P_PKT_RELAY_ACK));
}

TEST(relay_unknown_session_dropped) {
    TEST_LOG("relay with unknown session_id dropped");
    mock_server_init();
    uint8_t pl[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    mock_clear_sent();
    ASSERT_EQ(mock_handle_relay(P2P_PKT_RELAY_DATA, 1, pl, 8), false);
    ASSERT_EQ(g_sent_count, 0);
}

TEST(relay_payload_too_short_dropped) {
    TEST_LOG("relay payload < 8 bytes dropped");
    mock_server_init();
    uint8_t pl[4] = {1,2,3,4};
    mock_clear_sent();
    ASSERT_EQ(mock_handle_relay(P2P_PKT_RELAY_DATA, 1, pl, 4), false);
    ASSERT_EQ(g_sent_count, 0);
}

/* ============================================================================
 * Part 6: UNREGISTER -> PEER_OFF
 * ============================================================================ */

TEST(unregister_sends_peer_off_with_peer_session_id) {
    TEST_LOG("UNREGISTER -> PEER_OFF with peer session_id");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(60001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(60002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    uint64_t bob_sid = bob->session_id;
    mock_clear_sent();
    mock_handle_unregister("alice","bob");
    sent_packet_t *poff = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_OFF);
    ASSERT_NOT_NULL(poff);
    ASSERT_EQ(poff->len, (size_t)12);
    ASSERT_EQ(read_u64_be(poff->buf + 4), bob_sid);
}

TEST(unregister_clears_slot) {
    TEST_LOG("UNREGISTER: slot no longer valid");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(61001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(61002),&c,1);
    mock_handle_unregister("alice","bob");
    ASSERT_NULL(mock_find_by_peer("alice","bob"));
}

TEST(unregister_marks_peer_disconnected) {
    TEST_LOG("UNREGISTER: peer->peer == (void*)-1");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(62001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(62002),&c,1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    mock_handle_unregister("alice","bob");
    ASSERT_EQ(bob->peer, (mock_pair_t*)(void*)-1);
}

TEST(unregister_no_peer_off_when_unpaired) {
    TEST_LOG("UNREGISTER before pairing -> no PEER_OFF");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(63001),&c,1);
    mock_clear_sent();
    mock_handle_unregister("alice","bob");
    ASSERT_NULL(mock_find_sent_any(SIG_PKT_PEER_OFF));
}

/* ============================================================================
 * Part 7: Timeout cleanup
 * ============================================================================ */

TEST(timeout_sends_peer_off) {
    TEST_LOG("Timeout: PEER_OFF to surviving peer");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(70001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(70002);
    mock_handle_register("alice","bob",  ip_a,port_a,&c,1);
    mock_handle_register("bob",  "alice",ip_b,port_b,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",  "alice");
    uint64_t bob_sid   = bob->session_id;
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 5;
    mock_clear_sent();
    ASSERT_EQ(mock_cleanup_timeout(), 1);
    sent_packet_t *poff = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_OFF);
    ASSERT_NOT_NULL(poff);
    ASSERT_EQ(read_u64_be(poff->buf + 4), bob_sid);
}

TEST(timeout_invalidates_pair) {
    TEST_LOG("Timeout: timed-out slot unfindable");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(71001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(71002),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 5;
    mock_cleanup_timeout();
    ASSERT_NULL(mock_find_by_peer("alice","bob"));
}

TEST(timeout_leaves_active_pairs_intact) {
    TEST_LOG("Active pairs not affected by timeout");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(72001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(72002),&c,1);
    ASSERT_EQ(mock_cleanup_timeout(), 0);
    ASSERT_NOT_NULL(mock_find_by_peer("alice","bob"));
}

TEST(timeout_marks_surviving_peer_disconnected) {
    TEST_LOG("Timeout: surviving peer->peer == (void*)-1");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(73001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(73002),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",  "alice");
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 5;
    mock_cleanup_timeout();
    ASSERT_EQ(bob->peer, (mock_pair_t*)(void*)-1);
}

/* ============================================================================
 * Part 8: NAT_PROBE
 * ============================================================================ */

TEST(nat_probe_ack_format) {
    TEST_LOG("NAT_PROBE_ACK: type + seq + probe_ip + probe_port");
    mock_server_init();
    uint32_t ip = htonl(0x01020304); uint16_t port = htons(54321);
    mock_clear_sent();
    mock_handle_nat_probe(42, ip, port);
    sent_packet_t *ack = mock_find_sent(ip, port, SIG_PKT_NAT_PROBE_ACK);
    ASSERT_NOT_NULL(ack);
    ASSERT_EQ(ack->len, (size_t)10);
    ASSERT_EQ(read_u16_be(ack->buf + 2), 42);
    uint32_t probe_ip;   memcpy(&probe_ip,   ack->buf+4, 4); ASSERT_EQ(probe_ip,   ip);
    uint16_t probe_port; memcpy(&probe_port, ack->buf+8, 2); ASSERT_EQ(probe_port, port);
}

TEST(nat_probe_seq_echo_various) {
    TEST_LOG("NAT_PROBE seq echoed for boundary values");
    mock_server_init();
    uint32_t ip = htonl(0xC0A80001); uint16_t port = htons(9999);
    uint16_t seqs[] = {0, 1, 255, 1000, 65535};
    for (int i = 0; i < 5; i++) {
        mock_clear_sent();
        mock_handle_nat_probe(seqs[i], ip, port);
        sent_packet_t *ack = mock_find_sent(ip, port, SIG_PKT_NAT_PROBE_ACK);
        ASSERT_NOT_NULL(ack);
        ASSERT_EQ(read_u16_be(ack->buf + 2), seqs[i]);
    }
}

/* ============================================================================
 * Part 9: ALIVE / ALIVE_ACK
 * ============================================================================ */

TEST(alive_returns_alive_ack) {
    TEST_LOG("ALIVE -> ALIVE_ACK (4 bytes)");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(80001);
    mock_handle_register("alice","bob",ip_a,port_a,&c,1);
    mock_clear_sent();
    ASSERT_EQ(mock_handle_alive("alice","bob"), true);
    sent_packet_t *ack = mock_find_sent(ip_a, port_a, SIG_PKT_ALIVE_ACK);
    ASSERT_NOT_NULL(ack);
    ASSERT_EQ(ack->len, (size_t)4);
}

TEST(alive_updates_last_active) {
    TEST_LOG("ALIVE updates last_active timestamp");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(81001),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    time_t old = time(NULL) - 5000; alice->last_active = old;
    mock_handle_alive("alice","bob");
    ASSERT(alice->last_active > old);
}

TEST(alive_unknown_peer_returns_false) {
    TEST_LOG("ALIVE for unknown peer -> false, no reply");
    mock_server_init();
    mock_clear_sent();
    ASSERT_EQ(mock_handle_alive("nobody","nobody2"), false);
    ASSERT_EQ(g_sent_count, 0);
}

/* ============================================================================
 * Part 10: Error handling
 * ============================================================================ */

TEST(peer_info_ack_session_id_zero_ignored) {
    TEST_LOG("PEER_INFO_ACK(session_id=0) -> ignored");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",  htonl(0x7F000001),htons(91001),&c,1);
    mock_handle_register("bob",  "alice",htonl(0x7F000001),htons(91002),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    bool before = alice->info0_acked;
    uint8_t pl[10] = {0};
    mock_handle_peer_info_ack(pl, 10, htonl(0x7F000001), htons(91001));
    ASSERT_EQ(alice->info0_acked, before);
}

TEST(register_ack_public_address_echoed) {
    TEST_LOG("REGISTER_ACK echoes public address");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip = htonl(0x5F2A1B0C); uint16_t port = htons(44444);
    mock_clear_sent();
    mock_handle_register("alice","bob",ip,port,&c,1);
    sent_packet_t *ack = mock_find_sent(ip, port, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(ack);
    uint32_t pub_ip; uint16_t pub_port;
    memcpy(&pub_ip,   ack->buf+6,  4);
    memcpy(&pub_port, ack->buf+10, 2);
    ASSERT_EQ(pub_ip, ip); ASSERT_EQ(pub_port, port);
}

TEST(register_ack_max_candidates_field) {
    TEST_LOG("REGISTER_ACK buf[5] == MOCK_MAX_CANDIDATES");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip = htonl(0x7F000001); uint16_t port = htons(92001);
    mock_clear_sent();
    mock_register_ack_t ack = mock_handle_register("alice","bob",ip,port,&c,1);
    ASSERT_EQ(ack.max_candidates, MOCK_MAX_CANDIDATES);
    sent_packet_t *pkt = mock_find_sent(ip, port, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(pkt);
    ASSERT_EQ(pkt->len, (size_t)14);
    ASSERT_EQ(pkt->buf[5], MOCK_MAX_CANDIDATES);
}

/* ============================================================================
 * Part 11: REGISTER_ACK relay flag and probe_port
 * ============================================================================ */

TEST(register_ack_relay_flag_when_enabled) {
    TEST_LOG("REGISTER_ACK: relay flag set when g_relay_enabled=true");
    mock_server_init();
    g_relay_enabled = true;
    mock_cand_t c = {0, 0, 0};
    uint32_t ip = htonl(0x7F000001); uint16_t port = htons(93001);
    mock_clear_sent();
    mock_register_ack_t ack = mock_handle_register("alice","bob",ip,port,&c,1);
    ASSERT_EQ(ack.relay_flag, SIG_REGACK_FLAG_RELAY);
    sent_packet_t *pkt = mock_find_sent(ip, port, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(pkt);
    ASSERT_EQ(pkt->buf[1] & SIG_REGACK_FLAG_RELAY, SIG_REGACK_FLAG_RELAY);
}

TEST(register_ack_no_relay_flag_when_disabled) {
    TEST_LOG("REGISTER_ACK: relay flag absent when disabled");
    mock_server_init();
    g_relay_enabled = false;
    mock_cand_t c = {0, 0, 0};
    uint32_t ip = htonl(0x7F000001); uint16_t port = htons(93002);
    mock_clear_sent();
    mock_handle_register("alice","bob",ip,port,&c,1);
    sent_packet_t *pkt = mock_find_sent(ip, port, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(pkt);
    ASSERT_EQ(pkt->buf[1] & SIG_REGACK_FLAG_RELAY, 0);
}

TEST(register_ack_probe_port_field) {
    TEST_LOG("REGISTER_ACK probe_port = 3479");
    mock_server_init();
    g_probe_port = 3479;
    mock_cand_t c = {0, 0, 0};
    uint32_t ip = htonl(0x7F000001); uint16_t port = htons(94001);
    mock_clear_sent();
    mock_register_ack_t ack = mock_handle_register("alice","bob",ip,port,&c,1);
    ASSERT_EQ(ntohs(ack.probe_port), 3479);
    sent_packet_t *pkt = mock_find_sent(ip, port, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(pkt);
    uint16_t pport; memcpy(&pport, pkt->buf+12, 2);
    ASSERT_EQ(ntohs(pport), 3479);
}

TEST(register_ack_probe_port_zero_when_not_configured) {
    TEST_LOG("REGISTER_ACK probe_port = 0 when not configured");
    mock_server_init();
    g_probe_port = 0;
    mock_cand_t c = {0, 0, 0};
    mock_register_ack_t ack = mock_handle_register("alice","bob",
        htonl(0x7F000001),htons(94002),&c,1);
    ASSERT_EQ(ack.probe_port, 0);
}

/* ============================================================================
 * Part 12: Candidate list boundaries
 * ============================================================================ */

TEST(candidate_count_capped_at_max) {
    TEST_LOG("Candidate count capped at MOCK_MAX_CANDIDATES");
    mock_server_init();
    mock_cand_t cands[MOCK_MAX_CANDIDATES + 3];
    for (int i = 0; i < MOCK_MAX_CANDIDATES + 3; i++) {
        cands[i].type = 0;
        cands[i].ip   = htonl((uint32_t)(0x0A000000 + i));
        cands[i].port = htons((uint16_t)(5000 + i));
    }
    mock_handle_register("charlie","dave",htonl(0x7F000001),htons(95001),
                         cands, MOCK_MAX_CANDIDATES + 3);
    mock_pair_t *pair = mock_find_by_peer("charlie","dave");
    ASSERT_NOT_NULL(pair);
    ASSERT_EQ(pair->candidate_count, MOCK_MAX_CANDIDATES);
}

TEST(zero_candidates_accepted) {
    TEST_LOG("0 candidates accepted");
    mock_server_init();
    mock_handle_register("eve","frank",htonl(0x7F000001),htons(95002),NULL,0);
    mock_pair_t *pair = mock_find_by_peer("eve","frank");
    ASSERT_NOT_NULL(pair);
    ASSERT_EQ(pair->candidate_count, 0);
}

TEST(asymmetric_candidate_counts) {
    TEST_LOG("Bilateral pairing with asymmetric candidate counts");
    mock_server_init();
    mock_cand_t ca[2] = {{0,htonl(0x0A000001),htons(5000)},
                         {1,htonl(0x01020304),htons(12345)}};
    mock_cand_t cb[3] = {{0,htonl(0x0A000002),htons(6000)},
                         {1,htonl(0x05060708),htons(23456)},
                         {2,htonl(0xC0A80001),htons(3478)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(96001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(96002);
    mock_handle_register("alice","bob",  ip_a,port_a,ca,2);
    mock_clear_sent();
    mock_handle_register("bob",  "alice",ip_b,port_b,cb,3);
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    sent_packet_t *pi_b = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a); ASSERT_NOT_NULL(pi_b);
    ASSERT_EQ(pi_a->buf[13], 3);
    ASSERT_EQ(pi_b->buf[13], 2);
}

/* ============================================================================
 * Part 13: Offline cache & first match
 * ============================================================================ */

TEST(offline_cache_bilateral_pairing) {
    TEST_LOG("Offline cache: Alice waits, Bob registers -> bilateral pairing");
    mock_server_init();
    mock_cand_t ca[4] = {{0,htonl(0x0A000001),htons(5000)},
                         {1,htonl(0x01020304),htons(12345)},
                         {0,htonl(0x0A000002),htons(5001)},
                         {2,htonl(0xC0A80001),htons(3478)}};
    mock_cand_t cb[3] = {{0,htonl(0x0A000002),htons(6000)},
                         {1,htonl(0x05060708),htons(23456)},
                         {2,htonl(0xC0A80002),htons(3479)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(97001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(97002);
    mock_register_ack_t ack1 = mock_handle_register("alice","bob",ip_a,port_a,ca,4);
    ASSERT_EQ(ack1.status, SIG_REGACK_PEER_OFFLINE);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    ASSERT_NOT_NULL(alice);
    ASSERT_EQ(alice->candidate_count, 4);
    ASSERT_NULL(alice->peer);
    mock_clear_sent();
    mock_register_ack_t ack2 = mock_handle_register("bob","alice",ip_b,port_b,cb,3);
    ASSERT_EQ(ack2.status, SIG_REGACK_PEER_ONLINE);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    ASSERT_NOT_NULL(bob);
    ASSERT_EQ(alice->peer, bob);
    ASSERT_EQ(bob->peer,   alice);
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    sent_packet_t *pi_b = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a); ASSERT_NOT_NULL(pi_b);
    ASSERT_EQ(pi_a->buf[13], 3);
    ASSERT_EQ(pi_b->buf[13], 4);
}

TEST(both_online_bilateral_notification) {
    TEST_LOG("Both online: bilateral pairing on second register");
    mock_server_init();
    mock_cand_t ca = {0,htonl(0x0A000001),htons(5000)};
    mock_cand_t cb = {0,htonl(0x0A000002),htons(6000)};
    mock_handle_register("peer_a","peer_b",htonl(0x7F000001),htons(98001),&ca,1);
    mock_handle_register("peer_b","peer_a",htonl(0x7F000001),htons(98002),&cb,1);
    mock_pair_t *pa = mock_find_by_peer("peer_a","peer_b");
    mock_pair_t *pb = mock_find_by_peer("peer_b","peer_a");
    ASSERT_NOT_NULL(pa); ASSERT_NOT_NULL(pb);
    ASSERT_EQ(pa->peer, pb);
    ASSERT_EQ(pb->peer, pa);
}

/* ============================================================================
 * Part 14: Address change & reconnect
 * ============================================================================ */

TEST(address_change_updates_slot) {
    TEST_LOG("Re-register with new address updates addr_ip/addr_port");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    uint32_t ip1 = htonl(0x0A000001); uint16_t port1 = htons(5000);
    uint32_t ip2 = htonl(0x0A000063); uint16_t port2 = htons(9999);
    mock_handle_register("alice","bob",ip1,port1,&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    ASSERT_EQ(alice->addr_ip,   ip1);
    ASSERT_EQ(alice->addr_port, port1);
    mock_handle_register("alice","bob",ip2,port2,&c,1);
    ASSERT_EQ(alice->addr_ip,   ip2);
    ASSERT_EQ(alice->addr_port, port2);
}

TEST(address_change_sends_notify_to_peer) {
    TEST_LOG("Address change sends PEER_INFO(seq=0, base_index!=0) to peer");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    
    /* Step 1: Alice 和 Bob 配对 */
    uint32_t alice_ip1 = htonl(0x0A000001);
    uint16_t alice_port1 = htons(5001);
    mock_handle_register("alice","bob", alice_ip1, alice_port1, &c, 1);
    mock_handle_register("bob","alice",  htonl(0x0A000002), htons(5002), &c, 1);
    
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    ASSERT_NOT_NULL(alice);
    ASSERT_NOT_NULL(bob);
    ASSERT_EQ(alice->peer, bob);
    
    /* Step 2: 模拟 Bob 确认收到首个 PEER_INFO(seq=0) */
    mock_clear_sent();
    bob->info0_acked = true;
    
    /* Step 3: Alice 地址变化，重新注册 */
    uint32_t alice_ip2 = htonl(0x0A000099);  // 新 IP
    uint16_t alice_port2 = htons(9999);      // 新端口
    mock_handle_register("alice","bob", alice_ip2, alice_port2, &c, 1);
    
    /* Step 4: 验证服务器向 Bob 发送了地址变更通知 */
    sent_packet_t *notify = mock_find_sent(bob->addr_ip, bob->addr_port, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(notify);
    ASSERT_GE(notify->len, 14);  // 至少有包头(4) + session_id(8) + base_index(1) + count(1)
    
    /* 验证包格式：seq=0 */
    ASSERT_EQ(notify->buf[0], SIG_PKT_PEER_INFO);
    uint16_t seq = (uint16_t)(notify->buf[2] << 8) | notify->buf[3];
    ASSERT_EQ(seq, 0);
    
    /* 验证 base_index != 0（地址变更通知） */
    uint8_t base_index = notify->buf[12];
    ASSERT_NEQ(base_index, 0);
    ASSERT_EQ(base_index, 1);  // 首次地址变更，序号为 1
    
    /* 验证 candidate_count = 1 */
    uint8_t cand_count = notify->buf[13];
    ASSERT_EQ(cand_count, 1);
    
    /* 验证候选地址是 Alice 的新公网地址 */
    ASSERT_EQ(notify->len, 14 + 7);  // 包头(4) + session_id(8) + base_index(1) + count(1) + candidate(7)
    uint8_t cand_type = notify->buf[14];
    ASSERT_EQ(cand_type, 1);  // Srflx
    
    uint32_t cand_ip;
    memcpy(&cand_ip, notify->buf + 15, 4);
    ASSERT_EQ(cand_ip, alice_ip2);
    
    uint16_t cand_port;
    memcpy(&cand_port, notify->buf + 19, 2);
    ASSERT_EQ(cand_port, alice_port2);
    
    TEST_LOG("  ✓ Server sent PEER_INFO(seq=0, base_index=1, count=1) to Bob");
    TEST_LOG("  ✓ Notified Alice's new address: type=1(Srflx)");
}

TEST(address_change_notify_seq_increment) {
    TEST_LOG("Multiple address changes increment base_index (1->2->3)");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    
    /* 配对并确认首包 */
    mock_handle_register("alice","bob", htonl(0x0A000001), htons(5001), &c, 1);
    mock_handle_register("bob","alice", htonl(0x0A000002), htons(5002), &c, 1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    bob->info0_acked = true;
    
    /* 第一次地址变更 */
    mock_clear_sent();
    mock_handle_register("alice","bob", htonl(0x0A000010), htons(6001), &c, 1);
    sent_packet_t *notify1 = mock_find_sent(bob->addr_ip, bob->addr_port, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(notify1);
    ASSERT_EQ(notify1->buf[12], 1);  // base_index = 1
    
    /* 模拟 Bob 确认收到 */
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    alice->info0_acked = true;
    
    /* 第二次地址变更 */
    mock_clear_sent();
    mock_handle_register("alice","bob", htonl(0x0A000020), htons(7001), &c, 1);
    sent_packet_t *notify2 = mock_find_sent(bob->addr_ip, bob->addr_port, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(notify2);
    ASSERT_EQ(notify2->buf[12], 2);  // base_index = 2
    
    alice->info0_acked = true;
    
    /* 第三次地址变更 */
    mock_clear_sent();
    mock_handle_register("alice","bob", htonl(0x0A000030), htons(8001), &c, 1);
    sent_packet_t *notify3 = mock_find_sent(bob->addr_ip, bob->addr_port, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(notify3);
    ASSERT_EQ(notify3->buf[12], 3);  // base_index = 3
    
    TEST_LOG("  ✓ base_index increments: 1 -> 2 -> 3");
}

TEST(address_change_notify_not_sent_before_info0_ack) {
    TEST_LOG("Address change notify NOT sent if peer hasn't ACKed PEER_INFO(seq=0)");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    
    /* 配对但未确认首包 */
    mock_handle_register("alice","bob", htonl(0x0A000001), htons(5001), &c, 1);
    mock_handle_register("bob","alice", htonl(0x0A000002), htons(5002), &c, 1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    bob->info0_acked = false;  // 关键：未确认首包
    
    /* Alice 地址变化 */
    mock_clear_sent();
    mock_handle_register("alice","bob", htonl(0x0A000099), htons(9999), &c, 1);
    
    /* 验证：不应该向 Bob 发送地址变更通知 */
    sent_packet_t *notify = mock_find_sent(bob->addr_ip, bob->addr_port, SIG_PKT_PEER_INFO);
    ASSERT_NULL(notify);  // 没有发送通知
    
    TEST_LOG("  ✓ Notify blocked until peer ACKs PEER_INFO(seq=0)");
}

TEST(address_change_no_notify_if_same_address) {
    TEST_LOG("No notify sent if address unchanged");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    
    uint32_t alice_ip = htonl(0x0A000001);
    uint16_t alice_port = htons(5001);
    
    /* 配对并确认 */
    mock_handle_register("alice","bob", alice_ip, alice_port, &c, 1);
    mock_handle_register("bob","alice", htonl(0x0A000002), htons(5002), &c, 1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    bob->info0_acked = true;
    
    /* Alice 重新注册但地址不变 */
    mock_clear_sent();
    mock_handle_register("alice","bob", alice_ip, alice_port, &c, 1);  // 相同地址
    
    /* 验证：不应该发送地址变更通知 */
    sent_packet_t *notify = mock_find_sent(bob->addr_ip, bob->addr_port, SIG_PKT_PEER_INFO);
    ASSERT_NULL(notify);
    
    TEST_LOG("  ✓ No notify when address unchanged");
}

TEST(reconnect_after_timeout) {
    TEST_LOG("Re-register after timeout: new slot with peer=NULL");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(99001),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 1;
    ASSERT_EQ(mock_cleanup_timeout(), 1);
    ASSERT_NULL(mock_find_by_peer("alice","bob"));
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(99001),&c,1);
    mock_pair_t *alice2 = mock_find_by_peer("alice","bob");
    ASSERT_NOT_NULL(alice2);
    ASSERT_NULL(alice2->peer);
}

/* ============================================================================
 * Part 15: Peer pointer state machine (NULL -> valid -> -1 -> NULL)
 * ============================================================================ */

TEST(peer_pointer_state_machine) {
    TEST_LOG("NULL -> valid -> -1 -> NULL lifecycle");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};

    /* State 1: NULL (unpaired) */
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(100001),&c,1);
    mock_pair_t *alice = mock_find_by_peer("alice","bob");
    ASSERT_NULL(alice->peer);

    /* State 2: valid pointer (paired) */
    mock_handle_register("bob","alice",htonl(0x7F000001),htons(100002),&c,1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    ASSERT_EQ(alice->peer, bob);
    ASSERT_EQ(bob->peer,   alice);

    /* State 3: (void*)-1 (peer timed out) */
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 1;
    mock_cleanup_timeout();
    ASSERT_EQ(bob->peer, (mock_pair_t*)(void*)-1);

    /* State 4: NULL (reset on re-register) */
    mock_handle_register("bob","alice",htonl(0x7F000001),htons(100002),&c,1);
    bob = mock_find_by_peer("bob","alice");
    ASSERT_NOT_NULL(bob);
    ASSERT_NULL(bob->peer);
}

TEST(peer_pointer_reset_via_unregister) {
    TEST_LOG("peer pointer: valid -> -1 via UNREGISTER");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",htonl(0x7F000001),htons(101001),&c,1);
    mock_handle_register("bob","alice",htonl(0x7F000001),htons(101002),&c,1);
    mock_pair_t *bob = mock_find_by_peer("bob","alice");
    ASSERT_NOT_NULL(bob->peer);
    ASSERT(bob->peer != (mock_pair_t*)(void*)-1);
    mock_handle_unregister("alice","bob");
    ASSERT_EQ(bob->peer, (mock_pair_t*)(void*)-1);
}

/* ============================================================================
 * Part 16: Multi-pair isolation & slot limits
 * ============================================================================ */

TEST(multiple_pairs_isolated) {
    TEST_LOG("3 independent pairs do not cross-pair");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_handle_register("alice","bob",   htonl(0x7F000001),htons(110001),&c,1);
    mock_handle_register("bob","alice",   htonl(0x7F000001),htons(110002),&c,1);
    mock_handle_register("charlie","dave",htonl(0x7F000001),htons(110003),&c,1);
    mock_handle_register("dave","charlie",htonl(0x7F000001),htons(110004),&c,1);
    mock_handle_register("eve","frank",   htonl(0x7F000001),htons(110005),&c,1);
    mock_handle_register("frank","eve",   htonl(0x7F000001),htons(110006),&c,1);
    mock_pair_t *alice   = mock_find_by_peer("alice",  "bob");
    mock_pair_t *bob     = mock_find_by_peer("bob",    "alice");
    mock_pair_t *charlie = mock_find_by_peer("charlie","dave");
    mock_pair_t *dave    = mock_find_by_peer("dave",   "charlie");
    mock_pair_t *eve     = mock_find_by_peer("eve",    "frank");
    mock_pair_t *frank   = mock_find_by_peer("frank",  "eve");
    ASSERT_EQ(alice->peer,   bob);
    ASSERT_EQ(charlie->peer, dave);
    ASSERT_EQ(eve->peer,     frank);
    ASSERT(alice->peer != charlie && alice->peer != eve);
    ASSERT(charlie->peer != alice && charlie->peer != eve);
}

TEST(slot_overflow_returns_error) {
    TEST_LOG("Register beyond MOCK_MAX_PEERS -> status=2 (no slot)");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    for (int i = 0; i < MOCK_MAX_PEERS; i++) {
        char local[32], remote[32];
        snprintf(local,  sizeof(local),  "peer_%02d", i);
        snprintf(remote, sizeof(remote), "tgt_%02d",  i);
        mock_handle_register(local, remote,
            htonl(0x7F000001), htons((uint16_t)(10000 + i)), &c, 1);
    }
    mock_register_ack_t overflow = mock_handle_register("overflow","target",
        htonl(0x7F000001), htons(19999), &c, 1);
    ASSERT_EQ(overflow.status, 2);
}

TEST(empty_remote_id_accepted_no_pairing) {
    TEST_LOG("Empty remote_id: registered but never paired");
    mock_server_init();
    mock_cand_t c = {0, 0, 0};
    mock_register_ack_t ack = mock_handle_register("alice","",
        htonl(0x7F000001),htons(111001),&c,1);
    ASSERT_EQ(ack.status, SIG_REGACK_PEER_OFFLINE);
    mock_pair_t *pair = mock_find_by_peer("alice","");
    ASSERT_NOT_NULL(pair);
    ASSERT_NULL(pair->peer);
    ASSERT_EQ(pair->session_id, (uint64_t)0);
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    printf("\n========================================\n");
    printf("COMPACT Server Complete Test Suite\n");
    printf("========================================\n\n");

    printf("Part 1: REGISTER + bilateral PEER_INFO(seq=0)\n");
    printf("----------------------------------------\n");
    RUN_TEST(register_bilateral_peer_info_sent);
    RUN_TEST(peer_info0_seq_field_is_zero);
    RUN_TEST(peer_info0_contains_session_id);
    RUN_TEST(peer_info0_contains_remote_candidates);
    RUN_TEST(register_first_match_only_sends_peer_info_once);

    printf("\nPart 2: session_id assignment\n");
    printf("----------------------------------------\n");
    RUN_TEST(session_id_zero_before_match);
    RUN_TEST(session_id_nonzero_after_match);
    RUN_TEST(session_id_distinct_per_direction);
    RUN_TEST(session_id_unique_across_pairs);

    printf("\nPart 3: PEER_INFO_ACK format & handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_info_ack_seq0_clears_pending);
    RUN_TEST(peer_info_ack_seq0_short_payload_dropped);
    RUN_TEST(peer_info_ack_seq_positive_relayed);
    RUN_TEST(peer_info_ack_seq0_idempotent);

    printf("\nPart 4: PEER_INFO(seq>0) relay\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_info_seq_positive_relayed);
    RUN_TEST(peer_info_seq0_from_client_rejected);

    printf("\nPart 5: RELAY_DATA / RELAY_ACK forwarding\n");
    printf("----------------------------------------\n");
    RUN_TEST(relay_data_forwarded_to_peer);
    RUN_TEST(relay_ack_forwarded_to_peer);
    RUN_TEST(relay_unknown_session_dropped);
    RUN_TEST(relay_payload_too_short_dropped);

    printf("\nPart 6: UNREGISTER -> PEER_OFF\n");
    printf("----------------------------------------\n");
    RUN_TEST(unregister_sends_peer_off_with_peer_session_id);
    RUN_TEST(unregister_clears_slot);
    RUN_TEST(unregister_marks_peer_disconnected);
    RUN_TEST(unregister_no_peer_off_when_unpaired);

    printf("\nPart 7: Timeout cleanup -> PEER_OFF\n");
    printf("----------------------------------------\n");
    RUN_TEST(timeout_sends_peer_off);
    RUN_TEST(timeout_invalidates_pair);
    RUN_TEST(timeout_leaves_active_pairs_intact);
    RUN_TEST(timeout_marks_surviving_peer_disconnected);

    printf("\nPart 8: NAT_PROBE\n");
    printf("----------------------------------------\n");
    RUN_TEST(nat_probe_ack_format);
    RUN_TEST(nat_probe_seq_echo_various);

    printf("\nPart 9: ALIVE / ALIVE_ACK\n");
    printf("----------------------------------------\n");
    RUN_TEST(alive_returns_alive_ack);
    RUN_TEST(alive_updates_last_active);
    RUN_TEST(alive_unknown_peer_returns_false);

    printf("\nPart 10: Error handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_info_ack_session_id_zero_ignored);
    RUN_TEST(register_ack_public_address_echoed);
    RUN_TEST(register_ack_max_candidates_field);

    printf("\nPart 11: REGISTER_ACK relay flag and probe_port\n");
    printf("----------------------------------------\n");
    RUN_TEST(register_ack_relay_flag_when_enabled);
    RUN_TEST(register_ack_no_relay_flag_when_disabled);
    RUN_TEST(register_ack_probe_port_field);
    RUN_TEST(register_ack_probe_port_zero_when_not_configured);

    printf("\nPart 12: Candidate list boundaries\n");
    printf("----------------------------------------\n");
    RUN_TEST(candidate_count_capped_at_max);
    RUN_TEST(zero_candidates_accepted);
    RUN_TEST(asymmetric_candidate_counts);

    printf("\nPart 13: Offline cache & first match\n");
    printf("----------------------------------------\n");
    RUN_TEST(offline_cache_bilateral_pairing);
    RUN_TEST(both_online_bilateral_notification);

    printf("\nPart 14: Address change & reconnect\n");
    printf("----------------------------------------\n");
    RUN_TEST(address_change_updates_slot);
    RUN_TEST(address_change_sends_notify_to_peer);
    RUN_TEST(address_change_notify_seq_increment);
    RUN_TEST(address_change_notify_not_sent_before_info0_ack);
    RUN_TEST(address_change_no_notify_if_same_address);
    RUN_TEST(reconnect_after_timeout);

    printf("\nPart 15: Peer pointer state machine\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_pointer_state_machine);
    RUN_TEST(peer_pointer_reset_via_unregister);

    printf("\nPart 16: Multi-pair isolation & slot limits\n");
    printf("----------------------------------------\n");
    RUN_TEST(multiple_pairs_isolated);
    RUN_TEST(slot_overflow_returns_error);
    RUN_TEST(empty_remote_id_accepted_no_pairing);

    printf("\n");
    TEST_SUMMARY();
#ifdef _WIN32
    WSACleanup();
#endif
    return (test_failed > 0) ? 1 : 0;
}
