
#include "p2p_internal.h"

///////////////////////////////////////////////////////////////////////////////

/*
 * PseudoTCP（拥塞控制）逻辑
 * 
 * 本模块实现类TCP的拥塞控制算法，管理可靠层的窗口大小和定时策略。
 * 
 * 核心概念：
 * - cwnd (拥塞窗口): 发送方允许发送的最大字节数
 * - ssthresh (慢启动阈值): 区分慢启动和拥塞避免阶段的阈值
 * - MSS (最大分段大小): 单个数据包的最大负载
 * 
 * AIMD (加性增乘性减) 算法：
 * - 收到 ACK 时：cwnd 线性增加（加性增）
 * - 检测到丢包时：cwnd 减半（乘性减）
 */

#define MSS 1200           /* 最大分段大小，单位：字节 */
#define INITIAL_CWND (2 * MSS)  /* 初始拥塞窗口 */
#define MIN_CWND (2 * MSS)      /* 最小拥塞窗口 */

static void p2p_pseudotcp_init(struct p2p_session *s) {
    s->tcp.cwnd = INITIAL_CWND;
    s->tcp.ssthresh = 65535;
    s->tcp.dup_acks = 0;
    s->tcp.cc_state = 0; /* 慢启动阶段 */
}

/* 
 * 收到累积 ACK 时调用
 * 
 * 实现 AIMD 逻辑：
 * - 慢启动阶段 (cwnd < ssthresh): cwnd 每收到一个 ACK 增加 1 MSS（指数增长）
 * - 拥塞避免阶段 (cwnd >= ssthresh): cwnd 每 RTT 增加约 1 MSS（线性增长）
 */
void p2p_pseudotcp_on_ack(struct p2p_session *s, uint16_t ack_seq) {
    (void)ack_seq;
    if (s->tcp.cwnd == 0) return;  /* not in pseudoTCP mode — avoid div/0 */
    if (s->tcp.cwnd < s->tcp.ssthresh) {
        /* 慢启动：指数增长 */
        s->tcp.cwnd += MSS;
    } else {
        /* 拥塞避免：线性增长 */
        s->tcp.cwnd += (MSS * MSS) / s->tcp.cwnd;
    }
    s->tcp.dup_acks = 0;
    s->tcp.last_ack = time_ms();
}

/* 
 * 检测到丢包时调用（超时或三次重复 ACK）
 * 
 * 乘性减策略：
 * - ssthresh 设为当前 cwnd 的一半
 * - cwnd 重置为最小值
 */
void p2p_pseudotcp_on_loss(struct p2p_session *s) {
    s->tcp.ssthresh = s->tcp.cwnd / 2;
    if (s->tcp.ssthresh < MIN_CWND) s->tcp.ssthresh = MIN_CWND;
    s->tcp.cwnd = MIN_CWND;
    s->tcp.dup_acks = 0;
    P2P_LOG_WARN("ptcp", MSG(MSG_PSEUDOTCP_CONGESTION), s->tcp.ssthresh, s->tcp.cwnd);
}

/*
 * 执行拥塞感知的重传
 * 
 * 根据当前拥塞窗口大小限制在途数据包数量，
 * 并在超时时触发重传。
 */
static void p2p_pseudotcp_tick(struct p2p_session *s) {
    reliable_t *r = &s->reliable;
    uint64_t now = time_ms();

    /* 
     * 在 PseudoTCP 模式下，根据 cwnd 限制在途数据包数量
     * 在途字节数 = 未确认数据包数 * MSS（近似值）
     */
    int in_flight = r->send_count * MSS;

    for (int i = 0; i < RELIABLE_WINDOW; i++) {
        uint16_t seq = r->send_base + i;
        if (seq_diff(seq, r->send_seq) >= 0) break;

        int idx = seq % RELIABLE_WINDOW;
        retx_entry_t *e = &r->send_buf[idx];
        if (e->acked) continue;

        /* 窗口检查：超过 cwnd 则停止发送 */
        if (in_flight >= (int)s->tcp.cwnd) break;

        if (e->send_time == 0 || now - e->send_time >= (uint64_t)r->rto) {
            /* 发送/重传数据包 */
            udp_send_packet(s->sock, &s->active_addr, P2P_PKT_DATA, 0, e->seq, e->data, e->len);
            
            if (e->send_time != 0) {
                /* 通过超时检测到丢包 */
                p2p_pseudotcp_on_loss(s);
                r->rto = (r->rto * 3) / 2; /* RTO 退避，每次增加 50% */
            }
            
            e->send_time = now;
            e->retx_count++;
        }
    }
}
/*
 * PseudoTCP 传输层实现
 */
static int pseudotcp_init(struct p2p_session *s) {
    p2p_pseudotcp_init(s);
    return 0;
}

static int pseudotcp_send(struct p2p_session *s, const void *buf, int len) {
    return reliable_send_pkt(&s->reliable, buf, len);
}

static void pseudotcp_tick(struct p2p_session *s) {
    p2p_pseudotcp_tick(s);
    int is_relay_mode = (s->path == P2P_PATH_RELAY);
    reliable_tick_ack(&s->reliable, s->sock, &s->active_addr, is_relay_mode);
}

const p2p_transport_ops_t p2p_trans_pseudotcp = {
    .name = "PseudoTCP",
    .init = pseudotcp_init,
    .send_data = pseudotcp_send,
    .tick = pseudotcp_tick,
    .on_packet = NULL /* 暂时由通用逻辑分发 */
};
