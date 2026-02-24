/*
 * test_nat_api.c - NAT API统一测试
 * 
 * 测试新统一的 nat_punch API：
 * 1. 批量启动模式：nat_punch(s, NULL, verbose)
 * 2. 单候选模式：nat_punch(s, &addr, 0)
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
    c->cand.type = type;
    c->cand.addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &c->cand.addr.sin_addr);
    c->cand.addr.sin_port = htons(port);
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
    nat_punch(s, NULL);
    
    // 验证状态
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    // 模拟收到 PUNCH_ACK
    struct sockaddr_in peer_addr;
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(10001);
    inet_pton(AF_INET, "192.168.1.100", &peer_addr.sin_addr);
    
    nat_on_packet(s, P2P_PKT_PUNCH_ACK, NULL, 0, &peer_addr);
    
    // 验证状态转换
    ASSERT_EQ(s->nat.state, NAT_CONNECTED);
    
    destroy_test_session(s);
}

TEST(nat_punch_batch_mode_no_candidates) {
    p2p_session_t *s = create_test_session();
    
    // 启动打洞（此时没有候选）
    int ret = nat_punch(s, NULL);
    
    // 验证返回值和状态
    ASSERT_EQ(ret, -1);
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
    nat_punch(s, NULL);
    
    // 验证状态
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    destroy_test_session(s);
}

TEST(nat_punch_single_mode_new_candidate) {
    p2p_session_t *s = create_test_session();
    
    // 启动打洞
    nat_punch(s, NULL);
    
    // 添加新候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    
    // 再次调用打洞，应该只向新候选发送
    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    new_addr.sin_port = htons(10001);
    inet_pton(AF_INET, "192.168.1.100", &new_addr.sin_addr);
    nat_punch(s, &new_addr);
    
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
    nat_punch(s, NULL);
    
    // 验证状态重置
    ASSERT_EQ(s->state, P2P_STATE_INIT);
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    destroy_test_session(s);
}

/* ============================================================================
 * 参数验证测试
 * ============================================================================ */

TEST(nat_punch_null_session) {
    // 验证空指针处理
    int ret = nat_punch(NULL, NULL);
    ASSERT_EQ(ret, -1);
}

TEST(nat_punch_verbose_flag) {
    p2p_session_t *s = create_test_session();
    
    // 添加远端候选
    add_test_candidate(s, "192.168.1.100", 10001, P2P_ICE_CAND_HOST);
    
    // 启动打洞
    nat_punch(s, NULL);
    
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
