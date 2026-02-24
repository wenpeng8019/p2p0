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
    TEST_LOG("Testing nat_punch batch mode (addr=NULL)");
    
    p2p_session_t *s = create_test_session();
    ASSERT(s != NULL);
    
    // 添加3个候选
    add_test_candidate(s, "192.168.1.1", 8001, P2P_ICE_CAND_HOST);
    add_test_candidate(s, "8.8.8.8", 8002, P2P_ICE_CAND_SRFLX);
    add_test_candidate(s, "1.1.1.1", 8003, P2P_ICE_CAND_RELAY);
    
    // 批量启动打洞
    int ret = nat_punch(s, NULL, 0);  // verbose=0
    
    // 验证：应该成功
    ASSERT_EQ(ret, 0);
    
    // 验证状态转换：INIT → PUNCHING
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    // 验证所有候选的时间戳已更新
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        ASSERT_GT(s->remote_cands[i].last_punch_send_ms, 0);
    }
    
    TEST_LOG("  ✓ Batch mode started PUNCHING state");
    TEST_LOG("  ✓ All %d candidates timestamped", s->remote_cand_cnt);
    
    destroy_test_session(s);
}

TEST(nat_punch_batch_mode_no_candidates) {
    TEST_LOG("Testing nat_punch batch mode with 0 candidates");
    
    p2p_session_t *s = create_test_session();
    ASSERT(s != NULL);
    
    // 不添加任何候选
    ASSERT_EQ(s->remote_cand_cnt, 0);
    
    // 尝试批量启动
    int ret = nat_punch(s, NULL, 0);
    
    // 验证：应该失败（返回-1）
    ASSERT_EQ(ret, -1);
    
    // 状态不应改变
    ASSERT_EQ(s->nat.state, NAT_INIT);
    
    TEST_LOG("  ✓ Batch mode returns -1 when no candidates");
    
    destroy_test_session(s);
}

/* ============================================================================
 * 单候选模式测试
 * ============================================================================ */

TEST(nat_punch_single_mode_basic) {
    TEST_LOG("Testing nat_punch single mode (addr!=NULL)");
    
    p2p_session_t *s = create_test_session();
    ASSERT(s != NULL);
    
    // 添加一个候选到数组
    add_test_candidate(s, "192.168.1.100", 9000, P2P_ICE_CAND_HOST);
    
    // 进入PUNCHING状态
    s->nat.state = NAT_PUNCHING;
    s->nat.punch_start = time_ms();
    
    // 向该地址单独打洞
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.100", &addr.sin_addr);
    addr.sin_port = htons(9000);
    
    int ret = nat_punch(s, &addr, 0);
    
    // 验证：应该成功
    ASSERT_EQ(ret, 0);
    
    // 验证：对应候选的时间戳已更新
    ASSERT_GT(s->remote_cands[0].last_punch_send_ms, 0);
    
    TEST_LOG("  ✓ Single mode updates target candidate timestamp");
    
    destroy_test_session(s);
}

TEST(nat_punch_single_mode_new_candidate) {
    TEST_LOG("Testing nat_punch single mode with new candidate (Trickle ICE)");
    
    p2p_session_t *s = create_test_session();
    ASSERT(s != NULL);
    
    // 已经有一个候选
    add_test_candidate(s, "192.168.1.1", 8001, P2P_ICE_CAND_HOST);
    
    // 进入PUNCHING状态
    s->nat.state = NAT_PUNCHING;
    
    // 向新地址打洞（模拟Trickle ICE收到新候选）
    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "8.8.8.8", &new_addr.sin_addr);
    new_addr.sin_port = htons(9999);
    
    // 先添加到列表
    add_test_candidate(s, "8.8.8.8", 9999, P2P_ICE_CAND_SRFLX);
    
    // 然后打洞
    int ret = nat_punch(s, &new_addr, 0);
    
    // 验证：应该成功
    ASSERT_EQ(ret, 0);
    
    // 验证：新候选的时间戳已设置
    ASSERT_GT(s->remote_cands[1].last_punch_send_ms, 0);
    
    TEST_LOG("  ✓ Single mode supports Trickle ICE (new candidates)");
    
    destroy_test_session(s);
}

/* ============================================================================
 * 状态转换测试
 * ============================================================================ */

TEST(nat_punch_relay_to_punching_restart) {
    TEST_LOG("Testing nat_punch auto-restart from RELAY state");
    
    p2p_session_t *s = create_test_session();
    ASSERT(s != NULL);
    
    // 添加候选
    add_test_candidate(s, "8.8.8.8", 8888, P2P_ICE_CAND_SRFLX);
    
    // 设置为RELAY状态（打洞超时后的降级）
    s->nat.state = NAT_RELAY;
    uint64_t old_punch_start = time_ms() - 10000;  // 假装之前超时了
    s->nat.punch_start = old_punch_start;
    
    // 收到新候选，尝试重新打洞
    struct sockaddr_in new_addr;
    new_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "1.2.3.4", &new_addr.sin_addr);
    new_addr.sin_port = htons(7777);
    
    int ret = nat_punch(s, &new_addr, 0);
    
    // 验证：应该成功
    ASSERT_EQ(ret, 0);
    
    // 验证：状态自动切换回PUNCHING
    ASSERT_EQ(s->nat.state, NAT_PUNCHING);
    
    // 验证：punch_start 被重置为当前时间（重新开始打洞窗口）
    uint64_t now = time_ms();
    ASSERT_GT(s->nat.punch_start, old_punch_start);  // 应该比旧的晚
    ASSERT_GE(s->nat.punch_start, now - 100);       // 应该是最近的时间
    
    TEST_LOG("  ✓ RELAY → PUNCHING auto-restart on new candidate");
    TEST_LOG("  ✓ Punch window reset correctly");
    
    destroy_test_session(s);
}

/* ============================================================================
 * 参数验证测试
 * ============================================================================ */

TEST(nat_punch_null_session) {
    TEST_LOG("Testing nat_punch with NULL session");
    
    // NULL session应该安全返回
    int ret = nat_punch(NULL, NULL, 0);
    ASSERT_EQ(ret, -1);
    
    TEST_LOG("  ✓ NULL session handled safely");
}

TEST(nat_punch_verbose_flag) {
    TEST_LOG("Testing nat_punch verbose flag (batch mode)");
    
    p2p_session_t *s = create_test_session();
    ASSERT(s != NULL);
    
    // 添加候选
    add_test_candidate(s, "192.168.1.1", 8001, P2P_ICE_CAND_HOST);
    add_test_candidate(s, "8.8.8.8", 8002, P2P_ICE_CAND_SRFLX);
    
    // verbose=1（仅验证不崩溃，实际日志输出需要手动观察）
    int ret = nat_punch(s, NULL, 1);
    
    // 验证：应该成功
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(s->nat.verbose, 1);
    
    TEST_LOG("  ✓ Verbose flag set correctly");
    
    destroy_test_session(s);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(int argc, char **argv) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            g_verbose = false;
        }
    }

    printf("\n=== NAT API Tests ===\n\n");

    // 批量模式测试
    RUN_TEST(nat_punch_batch_mode_success);
    RUN_TEST(nat_punch_batch_mode_no_candidates);
    
    // 单候选模式测试
    RUN_TEST(nat_punch_single_mode_basic);
    RUN_TEST(nat_punch_single_mode_new_candidate);
    
    // 状态转换测试
    RUN_TEST(nat_punch_relay_to_punching_restart);
    
    // 参数验证测试
    RUN_TEST(nat_punch_null_session);
    RUN_TEST(nat_punch_verbose_flag);

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", test_passed);
    printf("Failed: %d\n", test_failed);
    printf("Total:  %d\n", test_passed + test_failed);

    return (test_failed == 0) ? 0 : 1;
}
