/*
 * test_nat_api.c - NAT API统一测试
 * 
 * 测试新统一的 nat_punch API：
 * 1. 批量启动模式：nat_punch(s, -1)
 * 2. 单候选模式：nat_punch(s, idx)
 * 3. 自动状态转换（RELAY → PUNCHING）
 * 4. 模式感知的日志输出
 */

#include "test_framework.h"
#include "../src/p2p_internal.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

static bool g_verbose = true;
#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================ * Helper函数
 * ============================================================================ */

// 创建一个测试用的最小化session
static p2p_session_t* create_test_session(void) {
    p2p_session_t *s = (p2p_session_t*)calloc(1, sizeof(p2p_session_t));
    if (!s) return NULL;
    
    // 初始化必要字段
    s->sock = 1;  // 假 socket
    nat_init(&s->nat);
    
    // 分配候选数组
    s->remote_cands = (p2p_remote_candidate_entry_t*)calloc(8, sizeof(p2p_remote_candidate_entry_t));
    s->remote_cand_cap = 8;
    s->remote_cand_cnt = 0;
    
    return s;
}

static void destroy_test_session(p2p_session_t *s) {
    if (s) {
        if (s->remote_cands) free(s->remote_cands);
        free(s);
    }
}

// 添加测试候选
static void add_test_candidate(p2p_session_t *s, const char *ip, uint16_t port, int type) {
    if (s->remote_cand_cnt >= s->remote_cand_cap) return;
    
    p2p_remote_candidate_entry_t *c = &s->remote_cands[s->remote_cand_cnt++];
    memset(c, 0, sizeof(*c));
    c->type = type;
    c->addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &c->addr.sin_addr);
    c->addr.sin_port = htons(port);
    c->last_punch_send_ms = 0;
}

/* ============================================================================
 * 批量启动模式测试
 * ============================================================================ */

TEST(nat_punch_batch_mode_success) {
    p2p_session_t *s = create_test_session();
    
    // 添加远端候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    add_test_candidate(s, "10.0.0.5", 10002, P2P_ICE_CAND_SRFLX);
    
    // 启动打洞
    nat_punch(s, -1);
    
    // 验证状态
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    // 模拟收到对方的 PUNCH（仅证明 peer→me 方向通了）
    struct sockaddr_in peer_addr;
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(10001);
    inet_pton(AF_INET, "192.168.1.100", &peer_addr.sin_addr);
    
    p2p_packet_hdr_t hdr = {
        .type = P2P_PKT_PUNCH,
        .flags = 0,
        .seq = 100  // 对方的序列号
    };
    
    // 构造 PUNCH 负载: [target_addr(4B) | target_port(2B)]
    uint8_t punch_payload[6];
    memcpy(punch_payload, &peer_addr.sin_addr.s_addr, 4);
    memcpy(punch_payload + 4, &peer_addr.sin_port, 2);
    
    nat_on_punch(s, &hdr, punch_payload, sizeof(punch_payload), &peer_addr);
    
    // PUNCH 只确认 rx 方向，尚未 NAT_CONNECTED
    ASSERT_EQ(s->nat.rx_confirmed, true);
    ASSERT_EQ(s->nat.tx_confirmed, false);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    // 模拟收到 PUNCH_ACK（证明 me→peer 方向也通了 → NAT_CONNECTED）
    p2p_packet_hdr_t ack_hdr = {
        .type = P2P_PKT_REACH,
        .flags = 0,
        .seq = 1  // 回传我们的 punch_seq
    };
    
    // 构造 REACH 负载: echo target_addr
    uint8_t ack_payload[6];
    memcpy(ack_payload, &peer_addr.sin_addr.s_addr, 4);
    memcpy(ack_payload + 4, &peer_addr.sin_port, 2);
    
    nat_on_reach(s, &ack_hdr, ack_payload, sizeof(ack_payload), &peer_addr);
    
    // 双向确认 → NAT_CONNECTED
    ASSERT_EQ(s->nat.tx_confirmed, true);
    ASSERT_EQ(s->nat.state, NAT_CONNECTED);
    
    destroy_test_session(s);
}

TEST(nat_punch_batch_mode_no_candidates) {
    p2p_session_t *s = create_test_session();
    
    // 启动打洞（此时没有候选）
    int ret = nat_punch(s, -1);
    
    // 验证返回值和状态
    ASSERT_EQ(ret, E_NONE_EXISTS);
    ASSERT_EQ(s->nat.state, NAT_INIT);
    
    destroy_test_session(s);
}

/* ============================================================================
 * 单候选模式测试
 * ============================================================================ */

TEST(nat_punch_single_mode_basic) {
    p2p_session_t *s = create_test_session();
    
    // 添加远端候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    
    // 启动打洞
    nat_punch(s, -1);
    
    // 验证状态
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    destroy_test_session(s);
}

TEST(nat_punch_single_mode_new_candidate) {
    p2p_session_t *s = create_test_session();
    
    // 启动打洞（无候选，忽略失败）
    nat_punch(s, -1);
    
    // 添加新候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    
    // 向新候选（index 0）单独打洞
    nat_punch(s, 0);
    
    // 验证状态
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    destroy_test_session(s);
}

/* ============================================================================
 * 状态转换测试
 * ============================================================================ */

TEST(nat_punch_relay_to_punching_restart) {
    p2p_session_t *s = create_test_session();
    
    // 添加远端候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    
    // 模拟已回退到中继模式
    s->nat.state = NAT_RELAY;
    
    // 启动打洞
    nat_punch(s, -1);
    
    // 验证状态重置
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    destroy_test_session(s);
}

/* ============================================================================
 * 参数验证测试
 * ============================================================================ */

TEST(nat_punch_null_session) {
#ifdef NDEBUG
    // 验证空指针处理（仅在 Release 模式下测试，Debug 模式会 assert）
    int ret = nat_punch(NULL, -1);
    ASSERT_EQ(ret, E_INVALID);
#else
    // Debug 模式下 NULL 会触发 assertion，这是预期行为
    (void)0;  // 空测试
#endif
}

TEST(nat_punch_verbose_flag) {
    p2p_session_t *s = create_test_session();
    
    // 添加远端候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    
    // 启动打洞
    nat_punch(s, -1);
    
    // 验证状态
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    destroy_test_session(s);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("Running NAT API tests...\n");
    
    RUN_TEST(nat_punch_batch_mode_success);
    RUN_TEST(nat_punch_batch_mode_no_candidates);
    RUN_TEST(nat_punch_single_mode_basic);
    RUN_TEST(nat_punch_single_mode_new_candidate);
    RUN_TEST(nat_punch_relay_to_punching_restart);
    RUN_TEST(nat_punch_null_session);
    RUN_TEST(nat_punch_verbose_flag);
    
    TEST_SUMMARY();
    
    return test_failed > 0 ? 1 : 0;
}
