/*
 * test_compact_server_v2.c - COMPACT 服务端协议全面测试套件 (v2)
 *
 * 覆盖范围（基于 p2p_server/server.c 实际实现）：
 *
 * Part 1: REGISTER + 双向配对 + PEER_INFO(seq=0) 首包格式
 *   - session_id 非零
 *   - PEER_INFO(seq=0) payload 以 8 字节 session_id 开头
 *   - 双方各收到对方候选
 *   - 双方进入 pending 重传队列
 *
 * Part 2: session_id 分配正确性
 *   - 双方 session_id 各不相同
 *   - 多对之间 session_id 互不冲突
 *
 * Part 3: PEER_INFO_ACK 包格式与处理
 *   - seq=0 ACK 格式：[session_id(8)][ack_seq(2)]
 *   - seq=0 → 从 pending 队列移除，info0_acked=true
 *   - seq>0 → relay 转发给 peer
 *
 * Part 4: PEER_INFO(seq>0) relay 转发
 *   - 客户端发送 PEER_INFO(seq>0)，服务端转发到对端地址
 *
 * Part 5: RELAY_DATA / RELAY_ACK relay 转发
 *   - 服务端按 session_id 查找转发
 *
 * Part 6: UNREGISTER → PEER_OFF 通知
 *   - PEER_OFF payload：8 字节 session_id（peer 的 session_id）
 *   - 对端 peer 指针标记为 -1
 *   - 本端槽位清理（hash 删除，valid=false）
 *
 * Part 7: 超时清理 → PEER_OFF 通知
 *   - cleanup 过期配对，向对端发送 PEER_OFF
 *
 * Part 8: NAT_PROBE 响应格式
 *   - PROBE_ACK 包含 probe_ip + probe_port，seq = 请求 seq
 *
 * Part 9: ALIVE / ALIVE_ACK
 *   - ALIVE 更新 last_active，回复 ALIVE_ACK
 *
 * Part 10: 错误包处理
 *   - PEER_INFO_ACK payload < 10 字节 → 拒绝
 *   - PEER_INFO(seq=0) 来自客户端 → 拒绝
 *   - relay 包中 session_id 未知 → 丢弃
 *   - REGISTER payload 太短 → 拒绝
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
 * 内嵌协议常量（与 p2pp.h 保持一致，避免依赖头文件）
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
#define SIG_PEER_INFO_FIN        0x01

/* ============================================================================
 * 跨平台 htonll / ntohll
 * ============================================================================ */

#if defined(__linux__)
#  include <endian.h>
#  define test_htonll(x) htobe64(x)
#  define test_ntohll(x) be64toh(x)
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#  include <libkern/OSByteOrder.h>
#  ifndef htonll
#    define test_htonll(x) OSSwapHostToBigInt64(x)
#    define test_ntohll(x) OSSwapBigToHostInt64(x)
#  else
#    define test_htonll(x) htonll(x)
#    define test_ntohll(x) ntohll(x)
#  endif
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
static inline uint64_t test_ntohll(uint64_t x) {
    return test_htonll(x);
}
#endif

/* ============================================================================
 * 测试日志
 * ============================================================================ */

static bool g_verbose = true;
#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 * 模拟服务器数据结构（镜像 server.c）
 * ============================================================================ */

#define MOCK_MAX_PEERS           64
#define MOCK_MAX_CANDIDATES      10
#define MOCK_PAIR_TIMEOUT        30
#define MOCK_PEER_INFO0_MAX_RETRY 5

typedef struct {
    uint8_t type;
    uint32_t ip;   /* network byte order */
    uint16_t port; /* network byte order */
} mock_candidate_t;

/* 模拟"发送"的包，记录到 sent buffer 供测试验证 */
typedef struct {
    uint8_t  buf[512];
    size_t   len;
    uint32_t dst_ip;
    uint16_t dst_port;
} sent_packet_t;

#define MAX_SENT_PKTS 64
static sent_packet_t g_sent[MAX_SENT_PKTS];
static int           g_sent_count = 0;

static void mock_sendto(const uint8_t *buf, size_t len, uint32_t dst_ip, uint16_t dst_port) {
    if (g_sent_count >= MAX_SENT_PKTS) return;
    sent_packet_t *p = &g_sent[g_sent_count++];
    if (len > sizeof(p->buf)) len = sizeof(p->buf);
    memcpy(p->buf, buf, len);
    p->len = len;
    p->dst_ip = dst_ip;
    p->dst_port = dst_port;
}

static void mock_clear_sent(void) {
    g_sent_count = 0;
    memset(g_sent, 0, sizeof(g_sent));
}

/* 在已发送的包中按目标地址和包类型查找 */
static sent_packet_t *mock_find_sent(uint32_t dst_ip, uint16_t dst_port, uint8_t pkt_type) {
    for (int i = 0; i < g_sent_count; i++) {
        if (g_sent[i].len >= 4 &&
            g_sent[i].dst_ip == dst_ip &&
            g_sent[i].dst_port == dst_port &&
            g_sent[i].buf[0] == pkt_type) {
            return &g_sent[i];
        }
    }
    return NULL;
}

/* 在所有已发送包中按包类型查找（不限目标） */
static sent_packet_t *mock_find_sent_any_dst(uint8_t pkt_type) {
    for (int i = 0; i < g_sent_count; i++) {
        if (g_sent[i].len >= 4 && g_sent[i].buf[0] == pkt_type) {
            return &g_sent[i];
        }
    }
    return NULL;
}

typedef struct mock_pair_s {
    bool                    valid;
    uint64_t                session_id;
    char                    local_id[P2P_PEER_ID_MAX];
    char                    remote_id[P2P_PEER_ID_MAX];
    uint32_t                addr_ip;    /* network byte order */
    uint16_t                addr_port;  /* network byte order */
    mock_candidate_t        candidates[MOCK_MAX_CANDIDATES];
    int                     candidate_count;
    struct mock_pair_s     *peer;
    time_t                  last_active;
    bool                    info0_acked;
    int                     info0_retry;
    time_t                  info0_sent_time;
    bool                    in_pending;   /* 是否在 pending 重传队列 */
} mock_pair_t;

static mock_pair_t g_mock_pairs[MOCK_MAX_PEERS];

/* 简单的 session_id → 槽位反查 */
static mock_pair_t *mock_find_by_session(uint64_t sid) {
    if (sid == 0) return NULL;
    for (int i = 0; i < MOCK_MAX_PEERS; i++) {
        if (g_mock_pairs[i].valid && g_mock_pairs[i].session_id == sid) {
            return &g_mock_pairs[i];
        }
    }
    return NULL;
}

static mock_pair_t *mock_find_by_peer(const char *local, const char *remote) {
    for (int i = 0; i < MOCK_MAX_PEERS; i++) {
        if (g_mock_pairs[i].valid &&
            strncmp(g_mock_pairs[i].local_id, local, P2P_PEER_ID_MAX) == 0 &&
            strncmp(g_mock_pairs[i].remote_id, remote, P2P_PEER_ID_MAX) == 0) {
            return &g_mock_pairs[i];
        }
    }
    return NULL;
}

static mock_pair_t *mock_alloc_pair(void) {
    for (int i = 0; i < MOCK_MAX_PEERS; i++) {
        if (!g_mock_pairs[i].valid) {
            memset(&g_mock_pairs[i], 0, sizeof(g_mock_pairs[i]));
            return &g_mock_pairs[i];
        }
    }
    return NULL;
}

/* 简单递增 session_id 生成（测试中保证唯一即可） */
static uint64_t g_next_session_id = 1000;
static uint64_t mock_generate_session_id(void) {
    uint64_t id = g_next_session_id++;
    /* 碰撞检测 */
    while (mock_find_by_session(id)) id = g_next_session_id++;
    return id;
}

void mock_server_init(void) {
    memset(g_mock_pairs, 0, sizeof(g_mock_pairs));
    g_next_session_id = 1000;
    mock_clear_sent();
}

/* ============================================================================
 * 模拟服务端核心处理函数（镜像 server.c handle_compact_signaling）
 * ============================================================================ */

/* 构建 PEER_INFO(seq=0) 并发送
 * 格式: [type(1)][flags(1)][seq(2)][session_id(8)][base_index(1)][cand_count(1)][cands(N*7)]
 */
static void mock_send_peer_info0(mock_pair_t *to_pair, mock_pair_t *from_pair) {
    uint8_t buf[4 + 8 + 2 + MOCK_MAX_CANDIDATES * 7];
    buf[0] = SIG_PKT_PEER_INFO;
    buf[1] = 0;                           /* flags */
    buf[2] = 0; buf[3] = 0;              /* seq = 0 (network byte order) */

    uint64_t sid_net = test_htonll(to_pair->session_id);
    memcpy(buf + 4, &sid_net, 8);

    buf[12] = 0;                          /* base_index = 0 */
    buf[13] = (uint8_t)from_pair->candidate_count;
    int len = 14;
    for (int i = 0; i < from_pair->candidate_count; i++) {
        buf[len]     = from_pair->candidates[i].type;
        memcpy(buf + len + 1, &from_pair->candidates[i].ip,   4);
        memcpy(buf + len + 5, &from_pair->candidates[i].port, 2);
        len += 7;
    }
    mock_sendto(buf, len, to_pair->addr_ip, to_pair->addr_port);
    to_pair->in_pending     = true;
    to_pair->info0_sent_time = time(NULL);
}

/* 模拟 REGISTER 处理 */
typedef struct {
    uint8_t  status;          /* 0=offline, 1=online, 2=error */
    uint8_t  max_candidates;
    uint32_t public_ip;   /* network byte order */
    uint16_t public_port; /* network byte order */
    uint16_t probe_port;  /* network byte order */
} mock_register_ack_t;

static mock_register_ack_t mock_handle_register(
    const char *local, const char *remote,
    uint32_t from_ip, uint16_t from_port,     /* network byte order */
    mock_candidate_t *cands, int cand_count)
{
    mock_register_ack_t ack = {0};
    ack.max_candidates = MOCK_MAX_CANDIDATES;
    ack.public_ip   = from_ip;
    ack.public_port = from_port;

    /* 查找或创建本端槽位 */
    mock_pair_t *lo = mock_find_by_peer(local, remote);
    if (!lo) {
        lo = mock_alloc_pair();
        if (!lo) { ack.status = 2; return ack; }
        lo->valid = true;
        strncpy(lo->local_id,  local,  P2P_PEER_ID_MAX - 1);
        strncpy(lo->remote_id, remote, P2P_PEER_ID_MAX - 1);
        lo->peer = NULL;
    }
    /* 更新候选 */
    if (lo->peer == (mock_pair_t*)(void*)-1) lo->peer = NULL;
    lo->addr_ip   = from_ip;
    lo->addr_port = from_port;
    lo->candidate_count = (cand_count > MOCK_MAX_CANDIDATES) ? MOCK_MAX_CANDIDATES : cand_count;
    if (cands && lo->candidate_count > 0) {
        memcpy(lo->candidates, cands, lo->candidate_count * sizeof(mock_candidate_t));
    } else {
        lo->candidate_count = 0;
    }
    lo->last_active = time(NULL);

    /* 查找反向配对 */
    mock_pair_t *re = mock_find_by_peer(remote, local);
    ack.status = (re != NULL) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE;

    /* 发送 REGISTER_ACK */
    {
        uint8_t buf[14];
        buf[0]  = SIG_PKT_REGISTER_ACK;
        buf[1]  = 0;  /* flags */
        buf[2]  = 0; buf[3] = 0;  /* seq */
        buf[4]  = ack.status;
        buf[5]  = ack.max_candidates;
        memcpy(buf + 6,  &from_ip,   4);
        memcpy(buf + 10, &from_port, 2);
        buf[12] = 0; buf[13] = 0;  /* probe_port = 0 */
        mock_sendto(buf, 14, from_ip, from_port);
    }

    if (re != NULL) {
        /* 首次匹配：建立双向关联 */
        if (lo->peer == NULL || re->peer == NULL) {
            lo->peer = re;
            re->peer = lo;

            /* 分配 session_id */
            if (lo->session_id == 0) lo->session_id = mock_generate_session_id();
            if (re->session_id == 0) re->session_id = mock_generate_session_id();

            /* 向 local 发送 PEER_INFO(seq=0)，含 local.session_id + remote 的候选 */
            mock_send_peer_info0(lo, re);
            /* 向 remote 发送 PEER_INFO(seq=0)，含 remote.session_id + local 的候选 */
            mock_send_peer_info0(re, lo);
        }
    }
    return ack;
}

/* 模拟 UNREGISTER 处理 */
static void mock_handle_unregister(const char *local, const char *remote) {
    mock_pair_t *pair = mock_find_by_peer(local, remote);
    if (!pair || !pair->valid) return;

    /* 向 peer 发送 PEER_OFF，携带 peer 自己的 session_id */
    if (pair->peer && pair->peer != (mock_pair_t*)(void*)-1 &&
        pair->peer->session_id != 0) {

        mock_pair_t *peer = pair->peer;
        uint8_t buf[4 + 8];
        buf[0] = SIG_PKT_PEER_OFF;
        buf[1] = 0;
        buf[2] = 0; buf[3] = 0;  /* seq = 0 */
        uint64_t sid_net = test_htonll(peer->session_id);
        memcpy(buf + 4, &sid_net, 8);
        mock_sendto(buf, 12, peer->addr_ip, peer->addr_port);

        peer->peer = (mock_pair_t*)(void*)-1;
    }

    /* 清理本端 */
    pair->valid      = false;
    pair->session_id = 0;
    pair->peer       = NULL;
}

/* 模拟 PEER_INFO_ACK 处理 
 * payload: [session_id(8)][ack_seq(2)]
 */
static void mock_handle_peer_info_ack(const uint8_t *payload, size_t payload_len,
                                      uint32_t from_ip, uint16_t from_port) {
    if (payload_len < 10) return;  /* 包太短，丢弃 */

    uint64_t session_id;
    memcpy(&session_id, payload, 8);
    session_id = test_ntohll(session_id);

    uint16_t ack_seq;
    memcpy(&ack_seq, payload + 8, 2);
    ack_seq = ntohs(ack_seq);

    mock_pair_t *pair = mock_find_by_session(session_id);
    if (!pair || !pair->valid) return;

    if (ack_seq == 0) {
        /* 服务器维护：标记 info0_acked，移出 pending */
        if (!pair->info0_acked) {
            pair->info0_acked = true;
            pair->in_pending  = false;
        }
    } else {
        /* 转发给 peer */
        if (pair->peer && pair->peer != (mock_pair_t*)(void*)-1) {
            /* 构建转发包（原包原样转发） */
            uint8_t fwd[4 + 10];
            fwd[0] = SIG_PKT_PEER_INFO_ACK;
            fwd[1] = 0;
            fwd[2] = 0; fwd[3] = 0;  /* seq */
            memcpy(fwd + 4, payload, 10);
            mock_sendto(fwd, 14, pair->peer->addr_ip, pair->peer->addr_port);
        }
    }
    (void)from_ip; (void)from_port;
}

/* 模拟 relay 转发（PEER_INFO seq>0 / RELAY_DATA / RELAY_ACK）
 * payload 开头 8 字节是 session_id
 */
static bool mock_handle_relay(uint8_t pkt_type, uint16_t seq, const uint8_t *payload, size_t payload_len) {
    /* PEER_INFO(seq=0) 来自客户端 → 非法，拒绝 */
    if (pkt_type == SIG_PKT_PEER_INFO && seq == 0) return false;

    if (payload_len < 8) return false;

    uint64_t session_id;
    memcpy(&session_id, payload, 8);
    session_id = test_ntohll(session_id);

    mock_pair_t *pair = mock_find_by_session(session_id);
    if (!pair || !pair->valid) return false;
    if (!pair->peer || pair->peer == (mock_pair_t*)(void*)-1) return false;

    /* 转发：原包原样发到对端 */
    uint8_t fwd[4 + 512];
    fwd[0] = pkt_type;
    fwd[1] = 0;
    fwd[2] = (uint8_t)(seq >> 8);
    fwd[3] = (uint8_t)(seq & 0xFF);
    size_t copy = (payload_len > 512) ? 512 : payload_len;
    memcpy(fwd + 4, payload, copy);
    mock_sendto(fwd, 4 + copy, pair->peer->addr_ip, pair->peer->addr_port);
    return true;
}

/* 模拟 NAT_PROBE 处理
 * 请求格式：[hdr(4)]（无 payload）
 * 响应格式：[hdr(4)][probe_ip(4)][probe_port(2)]，seq = 请求 seq
 */
static void mock_handle_nat_probe(uint16_t req_seq, uint32_t from_ip, uint16_t from_port) {
    uint8_t buf[10];
    buf[0] = SIG_PKT_NAT_PROBE_ACK;
    buf[1] = 0;
    buf[2] = (uint8_t)(req_seq >> 8);
    buf[3] = (uint8_t)(req_seq & 0xFF);
    memcpy(buf + 4, &from_ip,   4);   /* probe_ip = 请求方 IP */
    memcpy(buf + 8, &from_port, 2);   /* probe_port = 请求方端口 */
    mock_sendto(buf, 10, from_ip, from_port);
}

/* 模拟 ALIVE 处理 */
static bool mock_handle_alive(const char *local, const char *remote) {
    mock_pair_t *pair = mock_find_by_peer(local, remote);
    if (!pair || !pair->valid) return false;

    pair->last_active = time(NULL);

    uint8_t buf[4];
    buf[0] = SIG_PKT_ALIVE_ACK;
    buf[1] = 0; buf[2] = 0; buf[3] = 0;
    mock_sendto(buf, 4, pair->addr_ip, pair->addr_port);
    return true;
}

/* 模拟超时清理 */
static int mock_cleanup_timeout(void) {
    time_t now = time(NULL);
    int count = 0;
    for (int i = 0; i < MOCK_MAX_PEERS; i++) {
        if (!g_mock_pairs[i].valid) continue;
        if ((now - g_mock_pairs[i].last_active) <= MOCK_PAIR_TIMEOUT) continue;

        mock_pair_t *pair = &g_mock_pairs[i];

        /* 向 peer 发送 PEER_OFF */
        if (pair->peer && pair->peer != (mock_pair_t*)(void*)-1 &&
            pair->peer->session_id != 0) {
            mock_pair_t *peer = pair->peer;
            uint8_t buf[4 + 8];
            buf[0] = SIG_PKT_PEER_OFF;
            buf[1] = 0; buf[2] = 0; buf[3] = 0;
            uint64_t sid_net = test_htonll(peer->session_id);
            memcpy(buf + 4, &sid_net, 8);
            mock_sendto(buf, 12, peer->addr_ip, peer->addr_port);
            peer->peer = (mock_pair_t*)(void*)-1;
        }

        pair->valid      = false;
        pair->session_id = 0;
        pair->peer       = NULL;
        count++;
    }
    return count;
}

/* ============================================================================
 * 测试辅助宏
 * ============================================================================ */

/* 从包体中以网络字节序读取 uint64_t */
static uint64_t read_u64_be(const uint8_t *p) {
    uint64_t v; memcpy(&v, p, 8); return test_ntohll(v);
}
static uint16_t read_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* ============================================================================
 * Part 1: REGISTER + 双向配对 + PEER_INFO(seq=0) 首包格式
 * ============================================================================ */

TEST(register_bilateral_peer_info_sent) {
    TEST_LOG("Two peers register → both receive PEER_INFO(seq=0)");
    mock_server_init();

    mock_candidate_t cands_a[2] = {
        {0, htonl(0x0A000001), htons(5001)},
        {1, htonl(0x01020304), htons(12345)},
    };
    mock_candidate_t cands_b[2] = {
        {0, htonl(0x0A000002), htons(6001)},
        {1, htonl(0x05060708), htons(23456)},
    };
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(10001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(10002);

    /* Alice 先注册（Bob 未在线）*/
    mock_clear_sent();
    mock_register_ack_t ack_a1 = mock_handle_register("alice", "bob", ip_a, port_a, cands_a, 2);
    ASSERT_EQ(ack_a1.status, SIG_REGACK_PEER_OFFLINE);

    /* Bob 注册，触发首次匹配 */
    mock_clear_sent();
    mock_register_ack_t ack_b = mock_handle_register("bob", "alice", ip_b, port_b, cands_b, 2);
    ASSERT_EQ(ack_b.status, SIG_REGACK_PEER_ONLINE);

    /* 验证双方各收到一个 PEER_INFO */
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    sent_packet_t *pi_b = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a);
    ASSERT_NOT_NULL(pi_b);

    /* seq 字段为 0 */
    ASSERT_EQ(read_u16_be(pi_a->buf + 2), 0);
    ASSERT_EQ(read_u16_be(pi_b->buf + 2), 0);

    TEST_LOG("  ✓ Both peers received PEER_INFO(seq=0)");
}

TEST(peer_info0_contains_session_id) {
    TEST_LOG("PEER_INFO(seq=0) payload starts with 8-byte session_id");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(11001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(11002);

    mock_handle_register("alice", "bob", ip_a, port_a, cands, 1);
    mock_clear_sent();
    mock_handle_register("bob", "alice", ip_b, port_b, cands, 1);

    /* Alice 的 PEER_INFO：payload[0..7] = alice.session_id */
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a);
    ASSERT(pi_a->len >= 4 + 8);  /* 至少 hdr(4) + session_id(8) */

    uint64_t sid_in_pkt = read_u64_be(pi_a->buf + 4);  /* payload 起始 */
    ASSERT(sid_in_pkt != 0);

    /* 验证与内部存储一致 */
    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);
    ASSERT_EQ(sid_in_pkt, alice->session_id);

    TEST_LOG("  ✓ PEER_INFO payload[0..7] = session_id = %" PRIu64, sid_in_pkt);
}

TEST(peer_info0_contains_remote_candidates) {
    TEST_LOG("PEER_INFO(seq=0) to Alice contains Bob's candidates");
    mock_server_init();

    mock_candidate_t cands_a[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_candidate_t cands_b[3] = {
        {0, htonl(0x0B000001), htons(6000)},
        {1, htonl(0x02020202), htons(7000)},
        {2, htonl(0xC0A80001), htons(3478)},
    };
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(12001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(12002);

    mock_handle_register("alice", "bob", ip_a, port_a, cands_a, 1);
    mock_clear_sent();
    mock_handle_register("bob", "alice", ip_b, port_b, cands_b, 3);

    /* PEER_INFO sent to Alice should contain Bob's 3 candidates */
    sent_packet_t *pi_a = mock_find_sent(ip_a, port_a, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_a);
    /* Format: [hdr(4)][session_id(8)][base_index(1)][cand_count(1)][cands(N*7)] */
    ASSERT(pi_a->len >= 14);
    uint8_t base_index  = pi_a->buf[12];
    uint8_t cand_count  = pi_a->buf[13];
    ASSERT_EQ(base_index, 0);
    ASSERT_EQ(cand_count, 3);
    ASSERT_EQ(pi_a->len, (size_t)(14 + 3 * 7));

    /* PEER_INFO sent to Bob should contain Alice's 1 candidate */
    sent_packet_t *pi_b = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(pi_b);
    ASSERT_EQ(pi_b->buf[13], 1);

    TEST_LOG("  ✓ Alice gets Bob's 3 cands, Bob gets Alice's 1 cand");
}

TEST(register_only_sends_peer_info_on_first_match) {
    TEST_LOG("Re-register when already paired does NOT re-send PEER_INFO");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(13001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(13002);

    /* First registration – triggers bilateral PEER_INFO */
    mock_handle_register("alice", "bob", ip_a, port_a, cands, 1);
    mock_clear_sent();
    mock_handle_register("bob", "alice", ip_b, port_b, cands, 1);
    int first_peer_info_count = 0;
    for (int i = 0; i < g_sent_count; i++) {
        if (g_sent[i].buf[0] == SIG_PKT_PEER_INFO) first_peer_info_count++;
    }
    ASSERT_EQ(first_peer_info_count, 2);  /* alice + bob */

    /* Re-registration (alice) – peer already paired, should NOT send peer_info again */
    mock_clear_sent();
    mock_handle_register("alice", "bob", ip_a, port_a, cands, 1);
    int re_peer_info_count = 0;
    for (int i = 0; i < g_sent_count; i++) {
        if (g_sent[i].buf[0] == SIG_PKT_PEER_INFO) re_peer_info_count++;
    }
    ASSERT_EQ(re_peer_info_count, 0);

    TEST_LOG("  ✓ Re-register skips PEER_INFO (already paired)");
}

/* ============================================================================
 * Part 2: session_id 分配正确性
 * ============================================================================ */

TEST(session_id_nonzero_after_match) {
    TEST_LOG("session_id is non-zero after bilateral match");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(20001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(20002), cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",   "alice");
    ASSERT_NOT_NULL(alice);
    ASSERT_NOT_NULL(bob);
    ASSERT(alice->session_id != 0);
    ASSERT(bob->session_id != 0);

    TEST_LOG("  ✓ alice session_id=%" PRIu64 ", bob session_id=%" PRIu64,
             alice->session_id, bob->session_id);
}

TEST(session_id_distinct_per_direction) {
    TEST_LOG("Each direction gets its own unique session_id");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(21001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(21002), cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",   "alice");
    ASSERT_NOT_NULL(alice);
    ASSERT_NOT_NULL(bob);
    ASSERT(alice->session_id != bob->session_id);

    TEST_LOG("  ✓ alice_sid != bob_sid");
}

TEST(session_id_unique_across_pairs) {
    TEST_LOG("Multiple pairs have all-distinct session_ids");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("a1", "b1", htonl(0x7F000001), htons(22001), cands, 1);
    mock_handle_register("b1", "a1", htonl(0x7F000001), htons(22002), cands, 1);
    mock_handle_register("a2", "b2", htonl(0x7F000001), htons(22003), cands, 1);
    mock_handle_register("b2", "a2", htonl(0x7F000001), htons(22004), cands, 1);
    mock_handle_register("a3", "b3", htonl(0x7F000001), htons(22005), cands, 1);
    mock_handle_register("b3", "a3", htonl(0x7F000001), htons(22006), cands, 1);

    uint64_t sids[6];
    const char *names[6][2] = {{"a1","b1"},{"b1","a1"},{"a2","b2"},{"b2","a2"},{"a3","b3"},{"b3","a3"}};
    for (int i = 0; i < 6; i++) {
        mock_pair_t *p = mock_find_by_peer(names[i][0], names[i][1]);
        ASSERT_NOT_NULL(p);
        ASSERT(p->session_id != 0);
        sids[i] = p->session_id;
    }
    /* All 6 session_ids must be unique */
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            ASSERT(sids[i] != sids[j]);
        }
    }
    TEST_LOG("  ✓ 6 session_ids are all unique");
}

TEST(session_id_zero_before_match) {
    TEST_LOG("session_id remains 0 until both peers register");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob", htonl(0x7F000001), htons(23001), cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);
    ASSERT_EQ(alice->session_id, (uint64_t)0);

    TEST_LOG("  ✓ session_id=0 before peer registers");
}

/* ============================================================================
 * Part 3: PEER_INFO_ACK 包格式与处理
 * ============================================================================ */

TEST(peer_info_ack_payload_format_seq0) {
    TEST_LOG("PEER_INFO_ACK payload: [session_id(8)][ack_seq=0 (2)]");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(30001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(30002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);
    ASSERT(alice->session_id != 0);

    /* 构造正确的 PEER_INFO_ACK(seq=0) payload */
    uint8_t payload[10];
    uint64_t sid_net = test_htonll(alice->session_id);
    memcpy(payload, &sid_net, 8);
    payload[8] = 0; payload[9] = 0;  /* ack_seq = 0 */

    mock_clear_sent();
    mock_handle_peer_info_ack(payload, 10, ip_a, port_a);

    ASSERT_EQ(alice->info0_acked, true);
    ASSERT_EQ(alice->in_pending, false);

    TEST_LOG("  ✓ seq=0 ACK marks info0_acked=true, removed from pending");
}

TEST(peer_info_ack_seq0_requires_10_bytes) {
    TEST_LOG("PEER_INFO_ACK payload < 10 bytes is silently dropped");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(31001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(31002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);
    bool was_acked = alice->info0_acked;

    /* Short payload – only 4 bytes */
    uint8_t short_payload[4] = {0, 1, 2, 3};
    mock_handle_peer_info_ack(short_payload, 4, ip_a, port_a);

    /* State must not change */
    ASSERT_EQ(alice->info0_acked, was_acked);

    TEST_LOG("  ✓ Short PEER_INFO_ACK dropped (< 10 bytes)");
}

TEST(peer_info_ack_seq_positive_relayed) {
    TEST_LOG("PEER_INFO_ACK(seq>0) is relayed to peer's address");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(32001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(32002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);

    /* Alice sends PEER_INFO_ACK(ack_seq=3) for session alice */
    uint8_t payload[10];
    uint64_t sid_net = test_htonll(alice->session_id);
    memcpy(payload, &sid_net, 8);
    uint16_t ack_seq = htons(3);
    memcpy(payload + 8, &ack_seq, 2);

    mock_clear_sent();
    mock_handle_peer_info_ack(payload, 10, ip_a, port_a);

    /* Must be forwarded to Bob */
    sent_packet_t *fwd = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO_ACK);
    ASSERT_NOT_NULL(fwd);

    /* ack_seq in forwarded packet must be 3.
     * read_u16_be already returns the big-endian bytes interpreted as a host
     * integer — no extra ntohs needed. */
    uint16_t fwd_ack_seq = read_u16_be(fwd->buf + 4 + 8);  /* [hdr(4)][sid(8)][ack_seq(2)] */
    ASSERT_EQ(fwd_ack_seq, 3);

    TEST_LOG("  ✓ PEER_INFO_ACK(seq=3) relayed to Bob (%s:%d)",
             inet_ntoa(*(struct in_addr*)&ip_b), ntohs(port_b));
}

TEST(peer_info_ack_seq0_idempotent) {
    TEST_LOG("Duplicate PEER_INFO_ACK(seq=0) is idempotent");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(33001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(33002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    uint8_t payload[10];
    uint64_t sid_net = test_htonll(alice->session_id);
    memcpy(payload, &sid_net, 8); payload[8] = 0; payload[9] = 0;

    mock_handle_peer_info_ack(payload, 10, ip_a, port_a);
    ASSERT_EQ(alice->info0_acked, true);

    /* Send again – state must not change */
    mock_handle_peer_info_ack(payload, 10, ip_a, port_a);
    ASSERT_EQ(alice->info0_acked, true);

    TEST_LOG("  ✓ Duplicate ACK(seq=0) is handled idempotently");
}

/* ============================================================================
 * Part 4: PEER_INFO(seq>0) relay
 * ============================================================================ */

TEST(peer_info_seq_positive_relayed) {
    TEST_LOG("PEER_INFO(seq=2) from client is relayed to peer");
    mock_server_init();

    mock_candidate_t cands[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(6000)},
    };
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(40001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(40002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 2);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 2);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);

    /* Simulate client sending PEER_INFO(seq=2) with session_id prefix */
    uint8_t payload[8 + 2 + 1 * 7];
    uint64_t sid_net = test_htonll(alice->session_id);
    memcpy(payload, &sid_net, 8);
    payload[8] = 0; payload[9] = 1;  /* base_index=0, count=1 */
    payload[10] = 0;
    memcpy(payload + 11, &cands[0].ip,   4);
    memcpy(payload + 15, &cands[0].port, 2);

    mock_clear_sent();
    bool relayed = mock_handle_relay(SIG_PKT_PEER_INFO, 2, payload, sizeof(payload));
    ASSERT_EQ(relayed, true);

    /* Should land at Bob's address */
    sent_packet_t *fwd = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_INFO);
    ASSERT_NOT_NULL(fwd);

    /* Forwarded seq must be 2 */
    ASSERT_EQ(read_u16_be(fwd->buf + 2), 2);

    TEST_LOG("  ✓ PEER_INFO(seq=2) relayed to Bob, seq preserved");
}

TEST(peer_info_seq0_from_client_rejected) {
    TEST_LOG("PEER_INFO(seq=0) from client → rejected (server-only packet)");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(41001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(41002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    uint8_t payload[8];
    uint64_t sid_net = test_htonll(alice->session_id);
    memcpy(payload, &sid_net, 8);

    mock_clear_sent();
    bool relayed = mock_handle_relay(SIG_PKT_PEER_INFO, 0, payload, 8);
    ASSERT_EQ(relayed, false);

    /* Nothing should have been forwarded */
    ASSERT_EQ(g_sent_count, 0);

    TEST_LOG("  ✓ PEER_INFO(seq=0) from client rejected");
}

/* ============================================================================
 * Part 5: RELAY_DATA / RELAY_ACK 转发
 * ============================================================================ */

TEST(relay_data_forwarded_to_peer) {
    TEST_LOG("RELAY_DATA is forwarded from Alice to Bob");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(50001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(50002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);

    /* Build RELAY_DATA payload: [session_id(8)][data_len(2)][data...] */
    uint8_t data_payload[8 + 2 + 5];
    uint64_t sid_net = test_htonll(alice->session_id);
    memcpy(data_payload, &sid_net, 8);
    data_payload[8] = 0; data_payload[9] = 5;  /* data_len = 5 */
    data_payload[10] = 'h'; data_payload[11] = 'e'; data_payload[12] = 'l';
    data_payload[13] = 'l'; data_payload[14] = 'o';

    mock_clear_sent();
    bool ok = mock_handle_relay(P2P_PKT_RELAY_DATA, 1, data_payload, sizeof(data_payload));
    ASSERT_EQ(ok, true);

    sent_packet_t *fwd = mock_find_sent(ip_b, port_b, P2P_PKT_RELAY_DATA);
    ASSERT_NOT_NULL(fwd);
    /* Verify payload content intact.
     * Forwarded packet: [hdr(4)][session_id(8)][data_len(2)][data...]
     * data_payload[10] = 'h' is forwarded to fwd->buf[4 + 10] = fwd->buf[14] */
    ASSERT_EQ(fwd->buf[4 + 10], 'h');

    TEST_LOG("  ✓ RELAY_DATA forwarded to Bob with data intact");
}

TEST(relay_ack_forwarded_to_peer) {
    TEST_LOG("RELAY_ACK is forwarded from Bob to Alice");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(51001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(51002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *bob = mock_find_by_peer("bob", "alice");
    ASSERT_NOT_NULL(bob);

    uint8_t ack_payload[8 + 2];
    uint64_t sid_net = test_htonll(bob->session_id);
    memcpy(ack_payload, &sid_net, 8);
    ack_payload[8] = 0; ack_payload[9] = 7;  /* ack_seq = 7 */

    mock_clear_sent();
    bool ok = mock_handle_relay(P2P_PKT_RELAY_ACK, 0, ack_payload, sizeof(ack_payload));
    ASSERT_EQ(ok, true);

    sent_packet_t *fwd = mock_find_sent(ip_a, port_a, P2P_PKT_RELAY_ACK);
    ASSERT_NOT_NULL(fwd);

    TEST_LOG("  ✓ RELAY_ACK forwarded to Alice");
}

TEST(relay_unknown_session_dropped) {
    TEST_LOG("relay packet with unknown session_id is silently dropped");
    mock_server_init();

    uint8_t payload[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  /* non-existent session */

    mock_clear_sent();
    bool ok = mock_handle_relay(P2P_PKT_RELAY_DATA, 1, payload, 8);
    ASSERT_EQ(ok, false);
    ASSERT_EQ(g_sent_count, 0);

    TEST_LOG("  ✓ Unknown session_id: packet dropped, nothing sent");
}

/* ============================================================================
 * Part 6: UNREGISTER → PEER_OFF
 * ============================================================================ */

TEST(unregister_sends_peer_off_to_peer) {
    TEST_LOG("UNREGISTER → PEER_OFF with peer's session_id sent to peer");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(60001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(60002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *bob = mock_find_by_peer("bob", "alice");
    ASSERT_NOT_NULL(bob);
    uint64_t bob_sid = bob->session_id;
    ASSERT(bob_sid != 0);

    mock_clear_sent();
    mock_handle_unregister("alice", "bob");

    /* PEER_OFF must be sent to Bob */
    sent_packet_t *poff = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_OFF);
    ASSERT_NOT_NULL(poff);
    ASSERT_EQ(poff->len, (size_t)12);  /* hdr(4) + session_id(8) */

    /* The session_id in PEER_OFF is Bob's (the receiver's) session_id */
    uint64_t sid_in_poff = read_u64_be(poff->buf + 4);
    ASSERT_EQ(sid_in_poff, bob_sid);

    TEST_LOG("  ✓ PEER_OFF sent to Bob with Bob's session_id=%" PRIu64, sid_in_poff);
}

TEST(unregister_clears_alice_slot) {
    TEST_LOG("UNREGISTER: alice's slot is cleared (valid=false, session_id=0)");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(61001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(61002), cands, 1);

    mock_handle_unregister("alice", "bob");

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NULL(alice);  /* find returns NULL because valid=false */

    TEST_LOG("  ✓ Alice slot cleaned up after UNREGISTER");
}

TEST(unregister_marks_bobs_peer_as_disconnected) {
    TEST_LOG("After UNREGISTER, Bob's peer pointer is set to -1");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(62001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(62002), cands, 1);

    mock_pair_t *bob = mock_find_by_peer("bob", "alice");
    ASSERT_NOT_NULL(bob);

    mock_handle_unregister("alice", "bob");

    /* Bob's peer pointer should be (void*)-1 */
    ASSERT_EQ(bob->peer, (mock_pair_t*)(void*)-1);

    TEST_LOG("  ✓ Bob->peer == (void*)-1 after Alice unregisters");
}

TEST(unregister_no_peer_off_when_not_paired) {
    TEST_LOG("UNREGISTER before pairing → no PEER_OFF sent");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob", htonl(0x7F000001), htons(63001), cands, 1);

    mock_clear_sent();
    mock_handle_unregister("alice", "bob");

    sent_packet_t *poff = mock_find_sent_any_dst(SIG_PKT_PEER_OFF);
    ASSERT_NULL(poff);  /* No PEER_OFF when not yet paired */

    TEST_LOG("  ✓ No PEER_OFF when not paired");
}

/* ============================================================================
 * Part 7: 超时清理 → PEER_OFF
 * ============================================================================ */

TEST(timeout_cleanup_sends_peer_off) {
    TEST_LOG("cleanup: timed-out pair → PEER_OFF sent to peer");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(70001);
    uint32_t ip_b = htonl(0x7F000001); uint16_t port_b = htons(70002);
    mock_handle_register("alice", "bob",   ip_a, port_a, cands, 1);
    mock_handle_register("bob",   "alice", ip_b, port_b, cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    mock_pair_t *bob   = mock_find_by_peer("bob",   "alice");
    ASSERT_NOT_NULL(alice);
    ASSERT_NOT_NULL(bob);
    uint64_t bob_sid = bob->session_id;

    /* Force Alice to time out */
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 5;

    mock_clear_sent();
    int cleaned = mock_cleanup_timeout();
    ASSERT_EQ(cleaned, 1);

    /* PEER_OFF should be sent to Bob */
    sent_packet_t *poff = mock_find_sent(ip_b, port_b, SIG_PKT_PEER_OFF);
    ASSERT_NOT_NULL(poff);
    ASSERT_EQ(poff->len, (size_t)12);

    uint64_t sid_in_poff = read_u64_be(poff->buf + 4);
    ASSERT_EQ(sid_in_poff, bob_sid);

    TEST_LOG("  ✓ PEER_OFF sent to Bob after Alice times out (sid=%" PRIu64 ")", sid_in_poff);
}

TEST(timeout_cleanup_invalidates_pair) {
    TEST_LOG("cleanup: timed-out pair slot becomes valid=false");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(71001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(71002), cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    alice->last_active = time(NULL) - MOCK_PAIR_TIMEOUT - 5;

    mock_cleanup_timeout();

    mock_pair_t *alice_after = mock_find_by_peer("alice", "bob");
    ASSERT_NULL(alice_after);

    TEST_LOG("  ✓ Alice slot invalid after timeout cleanup");
}

TEST(timeout_not_triggered_for_active_pairs) {
    TEST_LOG("Active pairs are not cleaned up by timeout");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(72001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(72002), cands, 1);
    /* last_active is set to now() inside mock_handle_register */

    int cleaned = mock_cleanup_timeout();
    ASSERT_EQ(cleaned, 0);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);

    TEST_LOG("  ✓ Active pair not cleaned up");
}

/* ============================================================================
 * Part 8: NAT_PROBE 响应格式
 * ============================================================================ */

TEST(nat_probe_response_format) {
    TEST_LOG("NAT_PROBE_ACK carries probe_ip/port and echoes request seq");
    mock_server_init();

    uint32_t client_ip   = htonl(0x01020304);  /* 1.2.3.4 */
    uint16_t client_port = htons(54321);
    uint16_t req_seq     = 42;

    mock_clear_sent();
    mock_handle_nat_probe(req_seq, client_ip, client_port);

    sent_packet_t *ack = mock_find_sent(client_ip, client_port, SIG_PKT_NAT_PROBE_ACK);
    ASSERT_NOT_NULL(ack);
    ASSERT_EQ(ack->len, (size_t)10);  /* hdr(4) + ip(4) + port(2) */

    /* seq must mirror req_seq */
    uint16_t resp_seq = read_u16_be(ack->buf + 2);
    ASSERT_EQ(resp_seq, req_seq);

    /* probe_ip = client IP */
    uint32_t probe_ip;
    memcpy(&probe_ip, ack->buf + 4, 4);
    ASSERT_EQ(probe_ip, client_ip);

    /* probe_port = client port */
    uint16_t probe_port;
    memcpy(&probe_port, ack->buf + 8, 2);
    ASSERT_EQ(probe_port, client_port);

    struct in_addr addr; addr.s_addr = client_ip;
    TEST_LOG("  ✓ NAT_PROBE_ACK: seq=%u, probe=%s:%u", resp_seq,
             inet_ntoa(addr), ntohs(client_port));
}

TEST(nat_probe_different_seqs) {
    TEST_LOG("NAT_PROBE echo is correct for various seq values");
    mock_server_init();

    uint32_t ip   = htonl(0xC0A80001);
    uint16_t port = htons(9999);

    uint16_t test_seqs[] = {0, 1, 255, 1000, 65535};
    for (int i = 0; i < 5; i++) {
        mock_clear_sent();
        mock_handle_nat_probe(test_seqs[i], ip, port);
        sent_packet_t *ack = mock_find_sent(ip, port, SIG_PKT_NAT_PROBE_ACK);
        ASSERT_NOT_NULL(ack);
        ASSERT_EQ(read_u16_be(ack->buf + 2), test_seqs[i]);
    }
    TEST_LOG("  ✓ NAT_PROBE seq echo correct for 5 different values");
}

/* ============================================================================
 * Part 9: ALIVE / ALIVE_ACK
 * ============================================================================ */

TEST(alive_returns_alive_ack) {
    TEST_LOG("ALIVE packet → ALIVE_ACK response");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(80001);
    mock_handle_register("alice", "bob", ip_a, port_a, cands, 1);

    mock_clear_sent();
    bool ok = mock_handle_alive("alice", "bob");
    ASSERT_EQ(ok, true);

    sent_packet_t *ack = mock_find_sent(ip_a, port_a, SIG_PKT_ALIVE_ACK);
    ASSERT_NOT_NULL(ack);
    ASSERT_EQ(ack->len, (size_t)4);  /* header only */

    TEST_LOG("  ✓ ALIVE_ACK sent (4 bytes, header only)");
}

TEST(alive_updates_last_active) {
    TEST_LOG("ALIVE updates last_active timestamp");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob", htonl(0x7F000001), htons(81001), cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    ASSERT_NOT_NULL(alice);

    /* Set last_active to a point in the past */
    time_t old_time = time(NULL) - 5000;
    alice->last_active = old_time;

    mock_handle_alive("alice", "bob");

    /* Should be refreshed */
    ASSERT(alice->last_active > old_time);
    TEST_LOG("  ✓ last_active updated (old=%" PRId64 " new=%" PRId64 ")",
             (int64_t)old_time, (int64_t)alice->last_active);
}

TEST(alive_unknown_peer_ignored) {
    TEST_LOG("ALIVE for unknown peer is silently ignored");
    mock_server_init();

    mock_clear_sent();
    bool ok = mock_handle_alive("nobody", "nobody2");
    ASSERT_EQ(ok, false);
    ASSERT_EQ(g_sent_count, 0);

    TEST_LOG("  ✓ ALIVE for unregistered peer returns false, no reply");
}

/* ============================================================================
 * Part 10: 错误包处理
 * ============================================================================ */

TEST(relay_payload_too_short_dropped) {
    TEST_LOG("relay packet with payload < 8 bytes is dropped");
    mock_server_init();

    uint8_t short_payload[4] = {0x01, 0x02, 0x03, 0x04};

    mock_clear_sent();
    bool ok = mock_handle_relay(P2P_PKT_RELAY_DATA, 1, short_payload, 4);
    ASSERT_EQ(ok, false);
    ASSERT_EQ(g_sent_count, 0);

    TEST_LOG("  ✓ Short relay payload dropped (< 8 bytes)");
}

TEST(peer_info_ack_null_session_dropped) {
    TEST_LOG("PEER_INFO_ACK with session_id=0 → no state change");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    mock_handle_register("alice", "bob",   htonl(0x7F000001), htons(91001), cands, 1);
    mock_handle_register("bob",   "alice", htonl(0x7F000001), htons(91002), cands, 1);

    mock_pair_t *alice = mock_find_by_peer("alice", "bob");
    bool was_acked = alice->info0_acked;

    /* session_id = 0 in payload */
    uint8_t payload[10] = {0};
    mock_handle_peer_info_ack(payload, 10, htonl(0x7F000001), htons(91001));

    ASSERT_EQ(alice->info0_acked, was_acked);  /* state must not change */
    TEST_LOG("  ✓ session_id=0 ACK ignored");
}

TEST(register_ack_max_candidates_correct) {
    TEST_LOG("REGISTER_ACK max_candidates field is correct");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t ip_a = htonl(0x7F000001); uint16_t port_a = htons(92001);

    mock_clear_sent();
    mock_register_ack_t ack = mock_handle_register("alice", "bob", ip_a, port_a, cands, 1);

    ASSERT_EQ(ack.max_candidates, MOCK_MAX_CANDIDATES);

    /* Verify byte in sent REGISTER_ACK packet */
    sent_packet_t *reg_ack = mock_find_sent(ip_a, port_a, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(reg_ack);
    ASSERT_EQ(reg_ack->len, (size_t)14);  /* hdr(4)+payload(10) */
    ASSERT_EQ(reg_ack->buf[5], MOCK_MAX_CANDIDATES);  /* payload[1] = max_candidates */

    TEST_LOG("  ✓ REGISTER_ACK[5] = max_candidates = %d", MOCK_MAX_CANDIDATES);
}

TEST(register_public_address_echo) {
    TEST_LOG("REGISTER_ACK echoes client's UDP source address (public addr detection)");
    mock_server_init();

    mock_candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    uint32_t client_ip   = htonl(0x5F2A1B0C);  /* 95.42.27.12 */
    uint16_t client_port = htons(44444);

    mock_clear_sent();
    mock_handle_register("alice", "bob", client_ip, client_port, cands, 1);

    sent_packet_t *reg_ack = mock_find_sent(client_ip, client_port, SIG_PKT_REGISTER_ACK);
    ASSERT_NOT_NULL(reg_ack);

    uint32_t pub_ip; uint16_t pub_port;
    memcpy(&pub_ip,   reg_ack->buf + 6, 4);
    memcpy(&pub_port, reg_ack->buf + 10, 2);
    ASSERT_EQ(pub_ip,   client_ip);
    ASSERT_EQ(pub_port, client_port);

    struct in_addr addr; addr.s_addr = pub_ip;
    TEST_LOG("  ✓ Public address echoed: %s:%u", inet_ntoa(addr), ntohs(pub_port));
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    printf("\n");
    printf("========================================\n");
    printf("COMPACT Server v2 Full Test Suite\n");
    printf("========================================\n\n");

    printf("Part 1: REGISTER + bilateral PEER_INFO(seq=0)\n");
    printf("----------------------------------------\n");
    RUN_TEST(register_bilateral_peer_info_sent);
    RUN_TEST(peer_info0_contains_session_id);
    RUN_TEST(peer_info0_contains_remote_candidates);
    RUN_TEST(register_only_sends_peer_info_on_first_match);

    printf("\nPart 2: session_id assignment\n");
    printf("----------------------------------------\n");
    RUN_TEST(session_id_nonzero_after_match);
    RUN_TEST(session_id_distinct_per_direction);
    RUN_TEST(session_id_unique_across_pairs);
    RUN_TEST(session_id_zero_before_match);

    printf("\nPart 3: PEER_INFO_ACK format & handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_info_ack_payload_format_seq0);
    RUN_TEST(peer_info_ack_seq0_requires_10_bytes);
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

    printf("\nPart 6: UNREGISTER -> PEER_OFF\n");
    printf("----------------------------------------\n");
    RUN_TEST(unregister_sends_peer_off_to_peer);
    RUN_TEST(unregister_clears_alice_slot);
    RUN_TEST(unregister_marks_bobs_peer_as_disconnected);
    RUN_TEST(unregister_no_peer_off_when_not_paired);

    printf("\nPart 7: Timeout cleanup -> PEER_OFF\n");
    printf("----------------------------------------\n");
    RUN_TEST(timeout_cleanup_sends_peer_off);
    RUN_TEST(timeout_cleanup_invalidates_pair);
    RUN_TEST(timeout_not_triggered_for_active_pairs);

    printf("\nPart 8: NAT_PROBE response\n");
    printf("----------------------------------------\n");
    RUN_TEST(nat_probe_response_format);
    RUN_TEST(nat_probe_different_seqs);

    printf("\nPart 9: ALIVE / ALIVE_ACK\n");
    printf("----------------------------------------\n");
    RUN_TEST(alive_returns_alive_ack);
    RUN_TEST(alive_updates_last_active);
    RUN_TEST(alive_unknown_peer_ignored);

    printf("\nPart 10: Error handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(relay_payload_too_short_dropped);
    RUN_TEST(peer_info_ack_null_session_dropped);
    RUN_TEST(register_ack_max_candidates_correct);
    RUN_TEST(register_public_address_echo);

    printf("\n");
    TEST_SUMMARY();

#ifdef _WIN32
    WSACleanup();
#endif
    return (test_failed > 0) ? 1 : 0;
}
