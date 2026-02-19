/*
 * PseudoTCP 拥塞控制独立测试
 * 
 * 测试 AIMD (加性增乘性减) 拥塞控制算法的各种边界条件和状态转换。
 * 
 * 主要测试点：
 * - 初始化状态
 * - 慢启动阶段（指数增长）
 * - 拥塞避免阶段（线性增长）
 * - 丢包检测和恢复
 * - cwnd/ssthresh 边界条件
 * - 阶段转换
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ============================================================================
 * 常量定义（从 p2p_trans_pseudotcp.c 复制）
 * ============================================================================ */

#define MSS 1200                /* 最大分段大小 */
#define INITIAL_CWND (2 * MSS)  /* 初始拥塞窗口 = 2400 字节 */
#define MIN_CWND (2 * MSS)      /* 最小拥塞窗口 = 2400 字节 */
#define INITIAL_SSTHRESH 65535  /* 初始慢启动阈值 */

/* ============================================================================
 * PseudoTCP 拥塞控制结构（简化版）
 * ============================================================================ */

typedef struct {
    uint32_t cwnd;       /* 拥塞窗口 (字节) */
    uint32_t ssthresh;   /* 慢启动阈值 (字节) */
    uint32_t dup_acks;   /* 重复 ACK 计数 */
    uint32_t cc_state;   /* 拥塞控制状态：0=慢启动, 1=拥塞避免 */
    uint64_t last_ack;   /* 上次收到 ACK 的时间戳 */
} pseudotcp_t;

/* ============================================================================
 * PseudoTCP 实现（从 p2p_trans_pseudotcp.c 提取）
 * ============================================================================ */

void pseudotcp_init(pseudotcp_t *tcp) {
    tcp->cwnd = INITIAL_CWND;
    tcp->ssthresh = INITIAL_SSTHRESH;
    tcp->dup_acks = 0;
    tcp->cc_state = 0; /* 慢启动 */
    tcp->last_ack = 0;
}

void pseudotcp_on_ack(pseudotcp_t *tcp) {
    if (tcp->cwnd < tcp->ssthresh) {
        /* 慢启动：指数增长 */
        tcp->cwnd += MSS;
        tcp->cc_state = 0;
    } else {
        /* 拥塞避免：线性增长 */
        tcp->cwnd += (MSS * MSS) / tcp->cwnd;
        tcp->cc_state = 1;
    }
    tcp->dup_acks = 0;
}

void pseudotcp_on_loss(pseudotcp_t *tcp) {
    tcp->ssthresh = tcp->cwnd / 2;
    if (tcp->ssthresh < MIN_CWND) {
        tcp->ssthresh = MIN_CWND;
    }
    tcp->cwnd = MIN_CWND;
    tcp->dup_acks = 0;
}

/* 模拟收到重复 ACK */
void pseudotcp_on_dup_ack(pseudotcp_t *tcp) {
    tcp->dup_acks++;
    if (tcp->dup_acks >= 3) {
        /* 快速重传触发 */
        pseudotcp_on_loss(tcp);
    }
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */

/* 测试1：初始化 */
TEST(basic_initialization) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    ASSERT(tcp.cwnd == INITIAL_CWND);
    ASSERT(tcp.ssthresh == INITIAL_SSTHRESH);
    ASSERT(tcp.dup_acks == 0);
    ASSERT(tcp.cc_state == 0);
}

/* 测试2：慢启动阶段（指数增长） */
TEST(slow_start_exponential_growth) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    uint32_t expected_cwnd = INITIAL_CWND;
    
    /* 收到 5 个 ACK，cwnd 应该每次增加 MSS */
    for (int i = 0; i < 5; i++) {
        pseudotcp_on_ack(&tcp);
        expected_cwnd += MSS;
        ASSERT(tcp.cwnd == expected_cwnd);
        ASSERT(tcp.cc_state == 0); /* 仍在慢启动 */
    }
}

/* 测试3：慢启动到拥塞避免的转换 */
TEST(slow_start_to_congestion_avoidance) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    /* 将 ssthresh 设为较小的值以便快速到达 */
    tcp.ssthresh = INITIAL_CWND + 3 * MSS;
    
    /* 慢启动：收到 3 个 ACK */
    for (int i = 0; i < 3; i++) {
        pseudotcp_on_ack(&tcp);
    }
    
    /* 此时 cwnd = 2400 + 3*1200 = 6000, 等于 ssthresh */
    ASSERT(tcp.cwnd == tcp.ssthresh);
    
    /* 下一个 ACK 应该进入拥塞避免阶段 */
    uint32_t cwnd_before = tcp.cwnd;
    pseudotcp_on_ack(&tcp);
    
    /* 拥塞避免：增量应该是 MSS*MSS/cwnd */
    uint32_t expected_increase = (MSS * MSS) / cwnd_before;
    ASSERT(tcp.cwnd == cwnd_before + expected_increase);
    ASSERT(tcp.cc_state == 1); /* 拥塞避免 */
}

/* 测试4：拥塞避免阶段（线性增长） */
TEST(congestion_avoidance_linear_growth) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    /* 直接设置为拥塞避免阶段 */
    tcp.cwnd = 10000;
    tcp.ssthresh = 8000; /* cwnd > ssthresh */
    
    uint32_t cwnd_before = tcp.cwnd;
    pseudotcp_on_ack(&tcp);
    
    /* 增量应该是 MSS*MSS/cwnd */
    uint32_t expected_increase = (MSS * MSS) / cwnd_before;
    ASSERT(tcp.cwnd == cwnd_before + expected_increase);
    ASSERT(tcp.cc_state == 1);
}

/* 测试5：丢包后的拥塞响应 */
TEST(loss_detection_and_recovery) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    /* 先增长 cwnd */
    tcp.cwnd = 10000;
    tcp.ssthresh = 65535;
    
    /* 触发丢包 */
    pseudotcp_on_loss(&tcp);
    
    /* ssthresh 应该是原 cwnd 的一半 */
    ASSERT(tcp.ssthresh == 5000);
    /* cwnd 重置为最小值 */
    ASSERT(tcp.cwnd == MIN_CWND);
    /* 重复 ACK 计数清零 */
    ASSERT(tcp.dup_acks == 0);
}

/* 测试6：丢包后 ssthresh 不低于最小值 */
TEST(loss_ssthresh_minimum) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    /* 设置一个很小的 cwnd */
    tcp.cwnd = MIN_CWND + 100;
    
    pseudotcp_on_loss(&tcp);
    
    /* ssthresh = cwnd/2 = 1300, 但不应该小于 MIN_CWND */
    ASSERT(tcp.ssthresh == MIN_CWND);
    ASSERT(tcp.cwnd == MIN_CWND);
}

/* 测试7：重复 ACK 计数 */
TEST(duplicate_ack_counting) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 10000;
    
    /* 收到 2 个重复 ACK，不应该触发快速重传 */
    pseudotcp_on_dup_ack(&tcp);
    ASSERT(tcp.dup_acks == 1);
    ASSERT(tcp.cwnd == 10000); /* 不变 */
    
    pseudotcp_on_dup_ack(&tcp);
    ASSERT(tcp.dup_acks == 2);
    ASSERT(tcp.cwnd == 10000); /* 不变 */
}

/* 测试8：三次重复 ACK 触发快速重传 */
TEST(fast_retransmit_on_three_dup_acks) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 10000;
    
    /* 收到 3 个重复 ACK */
    pseudotcp_on_dup_ack(&tcp);
    pseudotcp_on_dup_ack(&tcp);
    pseudotcp_on_dup_ack(&tcp);
    
    /* 应该触发快速重传，效果等同于 on_loss */
    ASSERT(tcp.ssthresh == 5000);
    ASSERT(tcp.cwnd == MIN_CWND);
    ASSERT(tcp.dup_acks == 0); /* 清零 */
}

/* 测试9：正常 ACK 清除重复 ACK 计数 */
TEST(normal_ack_clears_dup_acks) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.dup_acks = 2;
    pseudotcp_on_ack(&tcp);
    
    ASSERT(tcp.dup_acks == 0);
}

/* 测试10：慢启动大窗口增长 */
TEST(slow_start_large_window) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.ssthresh = 100000; /* 很大的阈值 */
    
    /* 收到 10 个 ACK */
    for (int i = 0; i < 10; i++) {
        pseudotcp_on_ack(&tcp);
    }
    
    /* cwnd 应该增长到 INITIAL_CWND + 10*MSS */
    ASSERT(tcp.cwnd == INITIAL_CWND + 10 * MSS);
    ASSERT(tcp.cc_state == 0); /* 仍在慢启动 */
}

/* 测试11：拥塞避免大量 ACK */
TEST(congestion_avoidance_many_acks) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 12000;
    tcp.ssthresh = 10000;
    
    /* 收到 100 个 ACK */
    for (int i = 0; i < 100; i++) {
        uint32_t cwnd_before = tcp.cwnd;
        pseudotcp_on_ack(&tcp);
        
        /* 每次增长应该是 MSS*MSS/cwnd_before */
        uint32_t expected_increase = (MSS * MSS) / cwnd_before;
        ASSERT(tcp.cwnd == cwnd_before + expected_increase);
    }
}

/* 测试12：多次丢包恢复循环 */
TEST(multiple_loss_recovery_cycles) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    /* 第一次增长 */
    tcp.cwnd = 20000;
    
    /* 第一次丢包 */
    pseudotcp_on_loss(&tcp);
    ASSERT(tcp.ssthresh == 10000);
    ASSERT(tcp.cwnd == MIN_CWND);
    
    /* 恢复增长 */
    tcp.cwnd = 15000;
    
    /* 第二次丢包 */
    pseudotcp_on_loss(&tcp);
    ASSERT(tcp.ssthresh == 7500);
    ASSERT(tcp.cwnd == MIN_CWND);
    
    /* 第三次 */
    tcp.cwnd = 8000;
    pseudotcp_on_loss(&tcp);
    ASSERT(tcp.ssthresh == 4000);
    ASSERT(tcp.cwnd == MIN_CWND);
}

/* 测试13：边界条件 - cwnd 刚好等于 ssthresh */
TEST(cwnd_equals_ssthresh) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 10000;
    tcp.ssthresh = 10000;
    
    uint32_t cwnd_before = tcp.cwnd;
    pseudotcp_on_ack(&tcp);
    
    /* cwnd == ssthresh，应该进入拥塞避免 */
    uint32_t expected_increase = (MSS * MSS) / cwnd_before;
    ASSERT(tcp.cwnd == cwnd_before + expected_increase);
    ASSERT(tcp.cc_state == 1);
}

/* 测试14：边界条件 - cwnd 略小于 ssthresh */
TEST(cwnd_just_below_ssthresh) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 9999;
    tcp.ssthresh = 10000;
    
    pseudotcp_on_ack(&tcp);
    
    /* cwnd < ssthresh，应该慢启动 */
    ASSERT(tcp.cwnd == 9999 + MSS);
    ASSERT(tcp.cc_state == 0);
}

/* 测试15：边界条件 - cwnd 略大于 ssthresh */
TEST(cwnd_just_above_ssthresh) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 10001;
    tcp.ssthresh = 10000;
    
    uint32_t cwnd_before = tcp.cwnd;
    pseudotcp_on_ack(&tcp);
    
    /* cwnd > ssthresh，应该拥塞避免 */
    uint32_t expected_increase = (MSS * MSS) / cwnd_before;
    ASSERT(tcp.cwnd == cwnd_before + expected_increase);
    ASSERT(tcp.cc_state == 1);
}

/* 测试16：拥塞避免的增长速率 */
TEST(congestion_avoidance_growth_rate) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 12000;
    tcp.ssthresh = 10000;
    
    /* 模拟一个 RTT：收到 cwnd/MSS 个 ACK */
    int acks_per_rtt = tcp.cwnd / MSS; /* 10 个 ACK */
    
    uint32_t cwnd_start = tcp.cwnd;
    for (int i = 0; i < acks_per_rtt; i++) {
        pseudotcp_on_ack(&tcp);
    }
    
    /* 一个 RTT 后，cwnd 应该约增加 1 MSS */
    /* 由于是整数除法，可能有一些误差 */
    ASSERT(tcp.cwnd > cwnd_start);
    ASSERT(tcp.cwnd <= cwnd_start + MSS * 2); /* 增长不超过 2*MSS */
}

/* 测试17：ssthresh 在丢包后成为新的目标 */
TEST(ssthresh_becomes_new_target_after_loss) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    tcp.cwnd = 20000;
    pseudotcp_on_loss(&tcp);
    
    uint32_t new_ssthresh = tcp.ssthresh; /* 10000 */
    ASSERT(tcp.cwnd == MIN_CWND);
    
    /* 慢启动恢复到 ssthresh */
    while (tcp.cwnd < new_ssthresh) {
        uint32_t cwnd_before = tcp.cwnd;
        pseudotcp_on_ack(&tcp);
        ASSERT(tcp.cwnd == cwnd_before + MSS); /* 指数增长 */
        ASSERT(tcp.cc_state == 0);
    }
    
    /* 超过 ssthresh 后进入拥塞避免 */
    pseudotcp_on_ack(&tcp);
    ASSERT(tcp.cc_state == 1);
}

/* 测试18：最小 cwnd 约束 */
TEST(minimum_cwnd_constraint) {
    pseudotcp_t tcp;
    pseudotcp_init(&tcp);
    
    /* 即使 cwnd 很小，丢包后也不应该低于 MIN_CWND */
    tcp.cwnd = MIN_CWND;
    
    pseudotcp_on_loss(&tcp);
    
    ASSERT(tcp.cwnd == MIN_CWND);
    ASSERT(tcp.ssthresh == MIN_CWND);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n========================================\n");
    printf("PseudoTCP Congestion Control Tests\n");
    printf("MSS: %d bytes\n", MSS);
    printf("Initial CWND: %d bytes\n", INITIAL_CWND);
    printf("Initial SSTHRESH: %d bytes\n", INITIAL_SSTHRESH);
    printf("========================================\n\n");

    RUN_TEST(basic_initialization);
    RUN_TEST(slow_start_exponential_growth);
    RUN_TEST(slow_start_to_congestion_avoidance);
    RUN_TEST(congestion_avoidance_linear_growth);
    RUN_TEST(loss_detection_and_recovery);
    RUN_TEST(loss_ssthresh_minimum);
    RUN_TEST(duplicate_ack_counting);
    RUN_TEST(fast_retransmit_on_three_dup_acks);
    RUN_TEST(normal_ack_clears_dup_acks);
    RUN_TEST(slow_start_large_window);
    RUN_TEST(congestion_avoidance_many_acks);
    RUN_TEST(multiple_loss_recovery_cycles);
    RUN_TEST(cwnd_equals_ssthresh);
    RUN_TEST(cwnd_just_below_ssthresh);
    RUN_TEST(cwnd_just_above_ssthresh);
    RUN_TEST(congestion_avoidance_growth_rate);
    RUN_TEST(ssthresh_becomes_new_target_after_loss);
    RUN_TEST(minimum_cwnd_constraint);

    printf("\n========================================\n");
    printf("Test Results: ");
    if (test_failed > 0) {
        printf("%d failed, ", test_failed);
    }
    printf("%d passed\n", test_passed);
    printf("========================================\n\n");

    return test_failed > 0 ? 1 : 0;
}
