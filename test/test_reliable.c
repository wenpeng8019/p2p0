/*
 * test_reliable.c - Reliable 传输层 (ARQ) 全面单元测试
 * 
 * 测试覆盖：
 * 1. 滑动窗口管理
 * 2. 序列号处理
 * 3. 数据包发送和接收
 * 4. ACK/SACK 处理
 * 5. RTT 估计
 * 6. 乱序接收
 * 7. 窗口满/空边界
 * 8. 重传逻辑
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#ifdef _WIN32
#  include <winsock2.h>   /* provides struct timeval */
#  include <windows.h>
   /* gettimeofday polyfill for MSVC */
   static int gettimeofday(struct timeval *tv, void *tz) {
       FILETIME ft;
       unsigned long long tt;
       (void)tz;
       GetSystemTimeAsFileTime(&ft);
       tt  = (unsigned long long)ft.dwHighDateTime << 32;
       tt |= (unsigned long long)ft.dwLowDateTime;
       tt /= 10;                       /* 100-ns → µs */
       tt -= 11644473600000000ULL;     /* epoch: 1601-01-01 → 1970-01-01 */
       tv->tv_sec  = (long)(tt / 1000000);
       tv->tv_usec = (long)(tt % 1000000);
       return 0;
   }
   /* nanosleep polyfill for MSVC (struct timespec is in <time.h> since VS2015) */
   static int nanosleep(const struct timespec *req, struct timespec *rem) {
       (void)rem;
       Sleep((DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000));
       return 0;
   }
#else
#  include <sys/time.h>
#endif

/* ============================================================================
 * Reliable 层定义（从 p2p_transport.h 和 p2pp.h 提取）
 * ============================================================================ */

#define P2P_MAX_PAYLOAD 1200      /* UDP payload 最大长度 */
#define RELIABLE_WINDOW 32        /* 滑动窗口大小 */
#define RELIABLE_RTO_INIT 200     /* 初始 RTO (ms) */
#define RELIABLE_RTO_MAX 2000     /* 最大 RTO (ms) */

/* 时间函数 */
static inline uint64_t time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/* 序列号差值计算（处理回绕） */
static inline int16_t seq_diff(uint16_t a, uint16_t b) {
    return (int16_t)(a - b);
}

/* 检查序列号是否在窗口内 */
static inline int seq_in_window(uint16_t seq, uint16_t base, int window) {
    int16_t d = seq_diff(seq, base);
    return d >= 0 && d < window;
}

/* 重传队列条目 */
typedef struct {
    uint8_t  data[P2P_MAX_PAYLOAD];
    int      len;
    uint16_t seq;
    uint64_t send_time;
    int      retx_count;
    int      acked;
} retx_entry_t;

/* Reliable 传输层状态 */
typedef struct {
    /* 发送端 */
    uint16_t     send_seq;
    uint16_t     send_base;
    retx_entry_t send_buf[RELIABLE_WINDOW];
    int          send_count;

    /* 接收端 */
    uint16_t     recv_base;
    uint8_t      recv_bitmap[RELIABLE_WINDOW];
    uint8_t      recv_data[RELIABLE_WINDOW][P2P_MAX_PAYLOAD];
    int          recv_lens[RELIABLE_WINDOW];

    /* RTT 估计 */
    int          srtt;
    int          rttvar;
    int          rto;
} reliable_t;

/* ============================================================================
 * Reliable 层实现（从 p2p_trans_reliable.c 提取并简化）
 * ============================================================================ */

void reliable_init(reliable_t *r) {
    memset(r, 0, sizeof(*r));
    r->rto = RELIABLE_RTO_INIT;
    r->srtt = 0;
    r->rttvar = 0;
}

int reliable_window_avail(const reliable_t *r) {
    return RELIABLE_WINDOW - r->send_count;
}

int reliable_send_pkt(reliable_t *r, const uint8_t *data, int len) {
    if (r->send_count >= RELIABLE_WINDOW) return -1;
    if (len > P2P_MAX_PAYLOAD) return -1;

    int idx = r->send_seq % RELIABLE_WINDOW;
    retx_entry_t *e = &r->send_buf[idx];
    memcpy(e->data, data, len);
    e->len = len;
    e->seq = r->send_seq;
    e->send_time = 0;
    e->retx_count = -1;
    e->acked = 0;

    r->send_seq++;
    r->send_count++;
    return 0;
}

int reliable_recv_pkt(reliable_t *r, uint8_t *buf, int *out_len) {
    int idx = r->recv_base % RELIABLE_WINDOW;
    if (!r->recv_bitmap[idx]) return -1;

    memcpy(buf, r->recv_data[idx], r->recv_lens[idx]);
    *out_len = r->recv_lens[idx];
    r->recv_bitmap[idx] = 0;
    r->recv_base++;
    return 0;
}

int reliable_on_data(reliable_t *r, uint16_t seq, const uint8_t *payload, int len) {
    if (!seq_in_window(seq, r->recv_base, RELIABLE_WINDOW))
        return 0;

    int idx = seq % RELIABLE_WINDOW;
    if (!r->recv_bitmap[idx]) {
        memcpy(r->recv_data[idx], payload, len);
        r->recv_lens[idx] = len;
        r->recv_bitmap[idx] = 1;
    }

    return 1;
}

int reliable_on_ack(reliable_t *r, uint16_t ack_seq, uint32_t sack_bits) {
    uint64_t now = time_ms();

    /* 累积 ACK */
    while (seq_diff(ack_seq, r->send_base) > 0) {
        int idx = r->send_base % RELIABLE_WINDOW;
        retx_entry_t *e = &r->send_buf[idx];
        if (!e->acked) {
            e->acked = 1;
            r->send_count--;

            /* RTT 估计（仅非重传包） */
            if (e->retx_count == 0 && e->send_time > 0) {
                int rtt = (int)(now - e->send_time);
                if (r->srtt == 0) {
                    r->srtt = rtt;
                    r->rttvar = rtt / 2;
                } else {
                    int delta = (rtt > r->srtt) ? (rtt - r->srtt) : (r->srtt - rtt);
                    r->rttvar = (3 * r->rttvar + delta) / 4;
                    r->srtt = (7 * r->srtt + rtt) / 8;
                }
                r->rto = r->srtt + 4 * r->rttvar;
                if (r->rto < 50) r->rto = 50;
                if (r->rto > RELIABLE_RTO_MAX) r->rto = RELIABLE_RTO_MAX;
            }
        }
        r->send_base++;
    }

    /* SACK 位图: SACK[i] represents seq = ack_seq + i */
    for (int i = 0; i < 32; i++) {
        if (sack_bits & (1u << i)) {
            uint16_t s = ack_seq + i;
            if (seq_in_window(s, r->send_base, RELIABLE_WINDOW)) {
                int idx = s % RELIABLE_WINDOW;
                if (!r->send_buf[idx].acked) {
                    r->send_buf[idx].acked = 1;
                    r->send_count--;
                }
            }
        }
    }

    return 0;
}

/* ============================================================================
 * 测试辅助函数
 * ============================================================================ */

static reliable_t sender;
static reliable_t receiver;

static void init_reliable_pair(void) {
    reliable_init(&sender);
    reliable_init(&receiver);
}

/* 构造测试数据包 */
static void make_packet(uint8_t *buf, int len, uint8_t pattern) {
    for (int i = 0; i < len; i++) {
        buf[i] = (uint8_t)((pattern + i) % 256);
    }
}

/* 验证数据包内容 */
static bool verify_packet(const uint8_t *buf, int len, uint8_t pattern) {
    for (int i = 0; i < len; i++) {
        if (buf[i] != (uint8_t)((pattern + i) % 256)) {
            return false;
        }
    }
    return true;
}

/* 模拟发送数据包（发送方 -> 接收方） */
static void simulate_send(reliable_t *from, reliable_t *to, uint16_t seq) {
    int idx = seq % RELIABLE_WINDOW;
    retx_entry_t *e = &from->send_buf[idx];
    e->send_time = time_ms();
    e->retx_count = 0;
    
    /* 模拟传输 */
    reliable_on_data(to, seq, e->data, e->len);
}

/* 构造 ACK */
static void build_ack(const reliable_t *r, uint16_t *ack_seq, uint32_t *sack_bits) {
    *ack_seq = r->recv_base;
    *sack_bits = 0;
    
    for (int i = 0; i < 32 && i < RELIABLE_WINDOW; i++) {
        int idx = (r->recv_base + i) % RELIABLE_WINDOW;
        if (r->recv_bitmap[idx])
            *sack_bits |= (1u << i);
    }
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */

/* 测试1：基本初始化 */
TEST(basic_initialization) {
    reliable_t r;
    reliable_init(&r);
    
    ASSERT(r.send_seq == 0);
    ASSERT(r.send_base == 0);
    ASSERT(r.send_count == 0);
    ASSERT(r.recv_base == 0);
    ASSERT(r.rto == RELIABLE_RTO_INIT);
    ASSERT(r.srtt == 0);
    ASSERT(reliable_window_avail(&r) == RELIABLE_WINDOW);
}

/* 测试2：发送单个数据包 */
TEST(send_single_packet) {
    init_reliable_pair();
    
    uint8_t data[100];
    make_packet(data, 100, 0);
    
    int ret = reliable_send_pkt(&sender, data, 100);
    ASSERT(ret == 0);
    ASSERT(sender.send_seq == 1);
    ASSERT(sender.send_count == 1);
    ASSERT(reliable_window_avail(&sender) == RELIABLE_WINDOW - 1);
}

/* 测试3：接收单个数据包 */
TEST(receive_single_packet) {
    init_reliable_pair();
    
    uint8_t send_data[100];
    make_packet(send_data, 100, 0x42);
    
    /* 发送方准备数据 */
    reliable_send_pkt(&sender, send_data, 100);
    
    /* 模拟传输 */
    simulate_send(&sender, &receiver, 0);
    
    /* 接收方接收 */
    uint8_t recv_data[200];
    int len;
    int ret = reliable_recv_pkt(&receiver, recv_data, &len);
    ASSERT(ret == 0);
    ASSERT(len == 100);
    ASSERT(verify_packet(recv_data, 100, 0x42));
    ASSERT(receiver.recv_base == 1);
}

/* 测试4：发送窗口满 */
TEST(send_window_full) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 填满发送窗口 */
    for (int i = 0; i < RELIABLE_WINDOW; i++) {
        make_packet(data, 100, (uint8_t)i);
        int ret = reliable_send_pkt(&sender, data, 100);
        ASSERT(ret == 0);
    }
    
    ASSERT(sender.send_count == RELIABLE_WINDOW);
    ASSERT(reliable_window_avail(&sender) == 0);
    
    /* 再发送应该失败 */
    int ret = reliable_send_pkt(&sender, data, 100);
    ASSERT(ret == -1);
}

/* 测试5：ACK 处理 - 累积确认 */
TEST(ack_cumulative) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 发送 5 个包 */
    for (int i = 0; i < 5; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
        simulate_send(&sender, &receiver, (uint16_t)i);
    }
    
    ASSERT(sender.send_count == 5);
    
    /* 接收方构造 ACK（确认到 seq=5） */
    uint16_t ack_seq = 5;
    uint32_t sack = 0;
    
    reliable_on_ack(&sender, ack_seq, sack);
    
    /* 验证发送方状态 */
    ASSERT(sender.send_base == 5);
    ASSERT(sender.send_count == 0);
    ASSERT(reliable_window_avail(&sender) == RELIABLE_WINDOW);
}

/* 测试6：SACK 处理 - 选择性确认 */
TEST(sack_selective_ack) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 发送 5 个包 */
    for (int i = 0; i < 5; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
    }
    
    /* 模拟丢包：只传输 seq=0, 2, 3, 4（seq=1 丢失） */
    simulate_send(&sender, &receiver, 0);
    simulate_send(&sender, &receiver, 2);
    simulate_send(&sender, &receiver, 3);
    simulate_send(&sender, &receiver, 4);
    
    /* 读取 seq=0，recv_base 前进到 1 */
    uint8_t recv_data[200];
    int len;
    reliable_recv_pkt(&receiver, recv_data, &len);
    
    /* 接收方构造 ACK */
    uint16_t ack_seq;
    uint32_t sack;
    build_ack(&receiver, &ack_seq, &sack);
    
    /* ack_seq = 1（下一个期望），sack 标记 2,3,4 */
    ASSERT(ack_seq == 1);
    ASSERT(sack & (1u << 1));  /* seq=2 收到（offset 1 from base） */
    ASSERT(sack & (1u << 2));  /* seq=3 收到（offset 2 from base） */
    ASSERT(sack & (1u << 3));  /* seq=4 收到（offset 3 from base） */
    
    /* 发送方处理 ACK */
    reliable_on_ack(&sender, ack_seq, sack);
    
    /* seq=0 被累积确认，seq=2,3,4 被 SACK 确认 */
    ASSERT(sender.send_base == 1);
    ASSERT(sender.send_count == 1);  /* 只剩 seq=1 未确认 */
}

/* 测试7：乱序接收 */
TEST(out_of_order_receive) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 发送 5 个包 */
    for (int i = 0; i < 5; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
    }
    
    /* 乱序接收：4, 2, 0, 3, 1 */
    simulate_send(&sender, &receiver, 4);
    simulate_send(&sender, &receiver, 2);
    simulate_send(&sender, &receiver, 0);
    simulate_send(&sender, &receiver, 3);
    simulate_send(&sender, &receiver, 1);
    
    /* 按序读取（应该按 0,1,2,3,4 的顺序） */
    for (int i = 0; i < 5; i++) {
        uint8_t recv_data[200];
        int len;
        int ret = reliable_recv_pkt(&receiver, recv_data, &len);
        ASSERT(ret == 0);
        ASSERT(verify_packet(recv_data, 100, (uint8_t)i));
    }
    
    ASSERT(receiver.recv_base == 5);
}

/* 测试8：丢包后重传 */
TEST(packet_loss_retransmission) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 发送 3 个包 */
    for (int i = 0; i < 3; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
    }
    
    /* 模拟丢包：只传输 seq=0, 2 */
    simulate_send(&sender, &receiver, 0);
    simulate_send(&sender, &receiver, 2);
    
    /* 读取 seq=0 */
    uint8_t recv_data[200];
    int len;
    reliable_recv_pkt(&receiver, recv_data, &len);
    
    /* 发送方收到 ACK（ack_seq=1, sack 标记 seq=2） */
    uint16_t ack_seq;
    uint32_t sack;
    build_ack(&receiver, &ack_seq, &sack);
    reliable_on_ack(&sender, ack_seq, sack);
    
    /* seq=0 和 seq=2 已确认，seq=1 未确认 */
    ASSERT(sender.send_count == 1);
    
    /* 重传 seq=1 */
    simulate_send(&sender, &receiver, 1);
    
    /* 读取 seq=1 和 seq=2 */
    reliable_recv_pkt(&receiver, recv_data, &len);
    reliable_recv_pkt(&receiver, recv_data, &len);
    
    /* 再次发 ACK */
    build_ack(&receiver, &ack_seq, &sack);
    reliable_on_ack(&sender, ack_seq, sack);
    
    /* 所有包都已确认 */
    ASSERT(sender.send_count == 0);
}

/* 测试9：RTT 估计 */
TEST(rtt_estimation) {
    init_reliable_pair();
    
    uint8_t data[100];
    make_packet(data, 100, 0);
    
    /* 发送数据包 */
    reliable_send_pkt(&sender, data, 100);
    
    uint64_t start_time = time_ms();
    sender.send_buf[0].send_time = start_time;
    sender.send_buf[0].retx_count = 0;
    
    /* 模拟传输和接收 */
    reliable_on_data(&receiver, 0, sender.send_buf[0].data, 100);
    
    /* 模拟 10ms RTT */
    struct timespec ts = {0, 10000000};  /* 10ms */
    nanosleep(&ts, NULL);
    
    /* 发送 ACK */
    reliable_on_ack(&sender, 1, 0);
    
    /* 验证 RTT 更新 */
    ASSERT(sender.srtt > 0);
    ASSERT(sender.rto > 0);
    ASSERT(sender.rto >= 50);  /* 最小 RTO */
    ASSERT(sender.rto <= RELIABLE_RTO_MAX);
}

/* 测试10：序列号回绕 */
TEST(sequence_number_wrap) {
    init_reliable_pair();
    
    /* 设置序列号接近最大值 */
    sender.send_seq = 65530;
    sender.send_base = 65530;
    receiver.recv_base = 65530;
    
    uint8_t data[100];
    /* Send 10 packets: (65530 + 10 = 65540 = 4 after wrap) */
    for (int i = 0; i < 10; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
        simulate_send(&sender, &receiver, (uint16_t)(65530 + i));
    }
    
    /* 验证序列号正确回绕 */
    uint16_t expected = (uint16_t)(65530 + 10);
    ASSERT(sender.send_seq == expected);  /* Should wrap to 4 */
    
    /* 接收所有包 */
    for (int i = 0; i < 10; i++) {
        uint8_t recv_data[200];
        int len;
        int ret = reliable_recv_pkt(&receiver, recv_data, &len);
        ASSERT(ret == 0);
        ASSERT(verify_packet(recv_data, 100, (uint8_t)i));
    }
}

/* 测试11：窗口滑动 */
TEST(window_sliding) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 填满窗口 */
    for (int i = 0; i < RELIABLE_WINDOW; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
        simulate_send(&sender, &receiver, (uint16_t)i);
    }
    
    ASSERT(reliable_window_avail(&sender) == 0);
    
    /* 确认前 10 个包 */
    reliable_on_ack(&sender, 10, 0);
    
    /* 窗口滑动，释放 10 个槽位 */
    ASSERT(sender.send_base == 10);
    ASSERT(reliable_window_avail(&sender) == 10);
    
    /* 可以继续发送 */
    for (int i = 0; i < 10; i++) {
        make_packet(data, 100, (uint8_t)(RELIABLE_WINDOW + i));
        int ret = reliable_send_pkt(&sender, data, 100);
        ASSERT(ret == 0);
    }
}

/* 测试12：接收窗口外的包 */
TEST(receive_out_of_window) {
    init_reliable_pair();
    
    uint8_t data[100];
    make_packet(data, 100, 0);
    
    /* 尝试接收窗口外的包（seq 太大） */
    int ret = reliable_on_data(&receiver, RELIABLE_WINDOW + 10, data, 100);
    ASSERT(ret == 0);  /* 应该被忽略 */
    
    /* 验证未存储 */
    ASSERT(receiver.recv_bitmap[(RELIABLE_WINDOW + 10) % RELIABLE_WINDOW] == 0);
}

/* 测试13：重复接收相同序列号 */
TEST(duplicate_receive) {
    init_reliable_pair();
    
    uint8_t data1[100];
    uint8_t data2[100];
    make_packet(data1, 100, 0xAA);
    make_packet(data2, 100, 0xBB);
    
    /* 第一次接收 seq=0 */
    reliable_on_data(&receiver, 0, data1, 100);
    
    /* 第二次接收相同 seq=0（不同数据） */
    reliable_on_data(&receiver, 0, data2, 100);
    
    /* 读取数据，应该是第一次的 */
    uint8_t recv_data[200];
    int len;
    reliable_recv_pkt(&receiver, recv_data, &len);
    ASSERT(verify_packet(recv_data, 100, 0xAA));
}

/* 测试14：满窗口压力测试 */
TEST(full_window_stress) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 循环：填满 -> 确认 -> 填满 -> 确认 */
    for (int round = 0; round < 5; round++) {
        /* 填满窗口 */
        for (int i = 0; i < RELIABLE_WINDOW; i++) {
            make_packet(data, 100, (uint8_t)i);
            int ret = reliable_send_pkt(&sender, data, 100);
            ASSERT(ret == 0);
        }
        
        /* 传输所有包 */
        for (int i = 0; i < RELIABLE_WINDOW; i++) {
            simulate_send(&sender, &receiver, (uint16_t)(round * RELIABLE_WINDOW + i));
        }
        
        /* 发送 ACK */
        uint16_t ack_seq = (uint16_t)((round + 1) * RELIABLE_WINDOW);
        reliable_on_ack(&sender, ack_seq, 0);
        
        /* 验证窗口清空 */
        ASSERT(sender.send_count == 0);
        ASSERT(reliable_window_avail(&sender) == RELIABLE_WINDOW);
    }
}

/* 测试15：部分 SACK */
TEST(partial_sack) {
    init_reliable_pair();
    
    uint8_t data[100];
    
    /* 发送 10 个包 */
    for (int i = 0; i < 10; i++) {
        make_packet(data, 100, (uint8_t)i);
        reliable_send_pkt(&sender, data, 100);
    }
    
    /* 只接收偶数包：0, 2, 4, 6, 8 */
    for (int i = 0; i < 10; i += 2) {
        simulate_send(&sender, &receiver, (uint16_t)i);
    }
    /* Read seq=0, recv_base advances to 1 */
    uint8_t recv_data[200];
    int len;
    reliable_recv_pkt(&receiver, recv_data, &len);
    
    /* 构造 ACK */
    uint16_t ack_seq;
    uint32_t sack;
    build_ack(&receiver, &ack_seq, &sack);
    
    /* ack_seq=1, SACK 位图标记相对于 base 的偏移 */
    ASSERT(ack_seq == 1);
    ASSERT(sack & (1u << 1));  /* seq=2 (offset 1) */
    ASSERT(sack & (1u << 3));  /* seq=4 (offset 3) */
    ASSERT(sack & (1u << 5));  /* seq=6 (offset 5) */
    ASSERT(sack & (1u << 7));  /* seq=8 (offset 7) */
    ASSERT(sack & (1u << 7));  /* seq=8 */
    
    /* 发送方处理后，奇数包未确认 */
    reliable_on_ack(&sender, ack_seq, sack);
    ASSERT(sender.send_count == 5);  /* 1, 3, 5, 7, 9 未确认 */
}

/* 测试16：大数据包边界 */
TEST(large_packet_boundary) {
    init_reliable_pair();
    
    uint8_t data[P2P_MAX_PAYLOAD];
    make_packet(data, P2P_MAX_PAYLOAD, 0);
    
    /* 发送最大长度包 */
    int ret = reliable_send_pkt(&sender, data, P2P_MAX_PAYLOAD);
    ASSERT(ret == 0);
    
    /* 超过最大长度应该失败 */
    ret = reliable_send_pkt(&sender, data, P2P_MAX_PAYLOAD + 1);
    ASSERT(ret == -1);
}

/* 测试17：空窗口读取 */
TEST(read_from_empty_window) {
    init_reliable_pair();
    
    uint8_t buf[100];
    int len;
    
    /* 从空接收窗口读取 */
    int ret = reliable_recv_pkt(&receiver, buf, &len);
    ASSERT(ret == -1);
}

/* Test: skip packet read */
TEST(skip_packet_read) {
    reliable_t sender, receiver;
    reliable_init(&sender);
    reliable_init(&receiver);
    
    uint8_t data[100];
    sender.send_seq = 0;
    make_packet(data, 100, 1);
    reliable_send_pkt(&sender, data, 100);  /* This will get seq=0 */
    
    make_packet(data, 100, 2);
    reliable_send_pkt(&sender, data, 100);  /* This will get seq=1 */
    
    make_packet(data, 100, 3);
    reliable_send_pkt(&sender, data, 100);  /* This will get seq=2 */
    
    /* 只传输 seq=1, 2（跳过 seq=0） */
    simulate_send(&sender, &receiver, 1);
    simulate_send(&sender, &receiver, 2);
    
    /* Try to read, but seq=0 not arrived, should fail */
    uint8_t recv_data[200];
    int len;
    int ret = reliable_recv_pkt(&receiver, recv_data, &len);
    ASSERT(ret == -1);
    
    /* Send seq=0 */
    simulate_send(&sender, &receiver, 0);
    
    /* Now can read in order: seq=0, seq=1, seq=2 */
    ret = reliable_recv_pkt(&receiver, recv_data, &len);
    ASSERT(ret == 0);
    ASSERT(verify_packet(recv_data, 100, 1));  /* seq=0 has pattern=1 */
    
    ret = reliable_recv_pkt(&receiver, recv_data, &len);
    ASSERT(ret == 0);
    ASSERT(verify_packet(recv_data, 100, 2));  /* seq=1 has pattern=2 */
    
    ret = reliable_recv_pkt(&receiver, recv_data, &len);
    ASSERT(ret == 0);
    ASSERT(verify_packet(recv_data, 100, 3));  /* seq=2 has pattern=3 */
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n========================================\n");
    printf("Reliable Transport Layer (ARQ) Tests\n");
    printf("Window Size: %d packets\n", RELIABLE_WINDOW);
    printf("Max Payload: %d bytes\n", P2P_MAX_PAYLOAD);
    printf("========================================\n\n");
    
    RUN_TEST(basic_initialization);
    RUN_TEST(send_single_packet);
    RUN_TEST(receive_single_packet);
    RUN_TEST(send_window_full);
    RUN_TEST(ack_cumulative);
    RUN_TEST(sack_selective_ack);
    RUN_TEST(out_of_order_receive);
    RUN_TEST(packet_loss_retransmission);
    RUN_TEST(rtt_estimation);
    RUN_TEST(sequence_number_wrap);
    RUN_TEST(window_sliding);
    RUN_TEST(receive_out_of_window);
    RUN_TEST(duplicate_receive);
    RUN_TEST(full_window_stress);
    RUN_TEST(partial_sack);
    RUN_TEST(large_packet_boundary);
    RUN_TEST(read_from_empty_window);
    RUN_TEST(skip_packet_read);
    
    printf("\n========================================\n");
    printf("Test Results: ");
    if (test_failed == 0) {
        printf(COLOR_GREEN "%d passed" COLOR_RESET, test_passed);
    } else {
        printf(COLOR_RED "%d failed" COLOR_RESET ", %d passed", test_failed, test_passed);
    }
    printf("\n========================================\n\n");
    
    return (test_failed == 0) ? 0 : 1;
}
