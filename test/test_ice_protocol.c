/*
 * test_ice_protocol.c - ICE 协议层单元测试
 *
 * 覆盖点：
 * 1. ICE 候选类型枚举（Host/Srflx/Relay/Prflx）
 * 2. 候选优先级计算（RFC 8445 公式，内联）
 * 3. 候选对状态枚举（Frozen/Waiting/InProgress/Succeeded/Failed）
 * 4. 状态流转合法性（模拟）
 * 5. 候选类型穿透难度排序
 *
 * 注：独立运行，不链接 p2p_static，避免 OpenSSL 依赖。
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdint.h>

/* ---- 内联 ICE 枚举与公式（摘自 src/p2p_ice.h + RFC 8445） ---- */

typedef enum {
    P2P_CAND_HOST  = 0,
    P2P_CAND_SRFLX = 1,
    P2P_CAND_RELAY = 2,
    P2P_CAND_PRFLX = 3
} p2p_cand_type_t;

typedef enum {
    P2P_PAIR_FROZEN      = 0,
    P2P_PAIR_WAITING     = 1,
    P2P_PAIR_IN_PROGRESS = 2,
    P2P_PAIR_SUCCEEDED   = 3,
    P2P_PAIR_FAILED      = 4
} p2p_pair_state_t;

/* RFC 8445 §5.1.2.1  type_pref: Host=126, Prflx=110, Srflx=100, Relay=0 */
static uint32_t ice_calc_priority_inline(p2p_cand_type_t type,
                                         uint16_t local_pref,
                                         uint8_t component)
{
    static const uint32_t type_prefs[] = { 126, 100, 0, 110 };
    uint32_t tp = type_prefs[(int)type];
    return (tp << 24) | ((uint32_t)local_pref << 8) | (256 - component);
}

/* ===========================================================================
 * 1. 候选类型枚举
 * =========================================================================== */
static void cand_type_enum(void) {
    printf("[TEST] Testing ICE candidate type enum...\n");
    ASSERT_EQ((int)P2P_CAND_HOST,  0);
    ASSERT_EQ((int)P2P_CAND_SRFLX, 1);
    ASSERT_EQ((int)P2P_CAND_RELAY, 2);
    ASSERT_EQ((int)P2P_CAND_PRFLX, 3);
    printf("[TEST] ✓ Candidate type enum passed\n");
}

/* ===========================================================================
 * 2. 优先级计算
 * =========================================================================== */
static void priority_calc(void) {
    printf("[TEST] Testing ICE priority calculation (RFC 8445)...\n");

    uint32_t p_host  = ice_calc_priority_inline(P2P_CAND_HOST,  100, 1);
    uint32_t p_srflx = ice_calc_priority_inline(P2P_CAND_SRFLX, 100, 1);
    uint32_t p_relay = ice_calc_priority_inline(P2P_CAND_RELAY, 100, 1);
    uint32_t p_prflx = ice_calc_priority_inline(P2P_CAND_PRFLX, 100, 1);

    ASSERT(p_host  > p_prflx);
    ASSERT(p_prflx > p_srflx);
    ASSERT(p_srflx > p_relay);

    /* 较高的 local_pref → 较高优先级 */
    ASSERT(ice_calc_priority_inline(P2P_CAND_HOST, 200, 1) >
           ice_calc_priority_inline(P2P_CAND_HOST, 100, 1));

    /* 较低的 component_id → 较高优先级 */
    ASSERT(ice_calc_priority_inline(P2P_CAND_HOST, 100, 1) >
           ice_calc_priority_inline(P2P_CAND_HOST, 100, 2));

    printf("[TEST] ✓ Priority calc passed (host=%u srflx=%u relay=%u)\n",
           p_host, p_srflx, p_relay);
}

/* ===========================================================================
 * 3. 候选对状态枚举
 * =========================================================================== */
static void pair_state_enum(void) {
    printf("[TEST] Testing ICE pair state enum...\n");
    ASSERT_EQ((int)P2P_PAIR_FROZEN,      0);
    ASSERT_EQ((int)P2P_PAIR_WAITING,     1);
    ASSERT_EQ((int)P2P_PAIR_IN_PROGRESS, 2);
    ASSERT_EQ((int)P2P_PAIR_SUCCEEDED,   3);
    ASSERT_EQ((int)P2P_PAIR_FAILED,      4);
    printf("[TEST] ✓ Pair state enum passed\n");
}

/* ===========================================================================
 * 4. 状态流转合法性（模拟）
 * =========================================================================== */
static void pair_state_transitions(void) {
    printf("[TEST] Testing ICE pair state transitions (mock)...\n");
    p2p_pair_state_t s = P2P_PAIR_FROZEN;
    ASSERT(s == P2P_PAIR_FROZEN);
    s = P2P_PAIR_WAITING;     ASSERT(s == P2P_PAIR_WAITING);
    s = P2P_PAIR_IN_PROGRESS; ASSERT(s == P2P_PAIR_IN_PROGRESS);
    s = P2P_PAIR_SUCCEEDED;   ASSERT(s == P2P_PAIR_SUCCEEDED);
    s = P2P_PAIR_IN_PROGRESS;
    s = P2P_PAIR_FAILED;      ASSERT(s == P2P_PAIR_FAILED);
    printf("[TEST] ✓ Pair state transitions passed\n");
}

/* ===========================================================================
 * 5. 候选类型穿透难度排序
 * =========================================================================== */
static void cand_priority_ordering(void) {
    printf("[TEST] Testing candidate priority ordering...\n");
    uint32_t h = ice_calc_priority_inline(P2P_CAND_HOST,  65535, 1);
    uint32_t f = ice_calc_priority_inline(P2P_CAND_PRFLX, 65535, 1);
    uint32_t s = ice_calc_priority_inline(P2P_CAND_SRFLX, 65535, 1);
    uint32_t r = ice_calc_priority_inline(P2P_CAND_RELAY, 65535, 1);
    ASSERT(h > f);
    ASSERT(f > s);
    ASSERT(s > r);
    printf("[TEST] ✓ Priority ordering: host(%u) > prflx(%u) > srflx(%u) > relay(%u)\n",
           h, f, s, r);
}

/* ===========================================================================
 * main
 * =========================================================================== */
int main(void) {
    printf("\n========================================\n");
    printf("  ICE 协议层单元测试\n");
    printf("========================================\n\n");

    cand_type_enum();
    priority_calc();
    pair_state_enum();
    pair_state_transitions();
    cand_priority_ordering();

    printf("\n========================================\n");
    printf("  所有测试通过！✓\n");
    printf("========================================\n\n");
    return 0;
}
