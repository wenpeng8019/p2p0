/*
 * 基于滑动窗口和 SACK 的数据包级 ARQ
 */

#include "p2p_udp.h"
#include "p2p_internal.h"

#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Implementation
///////////////////////////////////////////////////////////////////////////////

static inline int seq_in_window(uint16_t seq, uint16_t base, int window) {
    int16_t d = seq_diff(seq, base);
    return d >= 0 && d < window;
}

static void reliable_init(reliable_t *r) {
    memset(r, 0, sizeof(*r));
    r->rto = RELIABLE_RTO_INIT;
    r->srtt = 0;
    r->rttvar = 0;
}

int reliable_window_avail(const reliable_t *r) {
    return RELIABLE_WINDOW - r->send_count;
}

/*
 * 将数据包排队进行可靠传输
 * 成功返回 0，窗口已满返回 -1
 * Queue a packet for reliable delivery.
 * Returns 0 on success, -1 if window is full.
 */
int reliable_send_pkt(reliable_t *r, const uint8_t *data, int len) {
    if (r->send_count >= RELIABLE_WINDOW) return -1;
    if (len > P2P_MAX_PAYLOAD) return -1;

    int idx = r->send_seq % RELIABLE_WINDOW;
    retx_entry_t *e = &r->send_buf[idx];
    memcpy(e->data, data, len);
    e->len = len;
    e->seq = r->send_seq;
    e->send_time = 0;       // 0 = 尚未发送，将在下次 tick 时发送
    e->retx_count = -1;     // -1 = 初始为待处理发送
    e->acked = 0;

    r->send_seq++;
    r->send_count++;
    return 0;
}

/*
 * 出队下一个按序接收的数据包
 * 成功返回 0，无可用数据返回 -1
 */
int reliable_recv_pkt(reliable_t *r, uint8_t *buf, int *out_len) {
    int idx = r->recv_base % RELIABLE_WINDOW;
    if (!r->recv_bitmap[idx]) return -1;

    memcpy(buf, r->recv_data[idx], r->recv_lens[idx]);
    *out_len = r->recv_lens[idx];
    r->recv_bitmap[idx] = 0;
    r->recv_base++;
    return 0;
}

/*
 * 处理传入的 DATA 数据包
 */
int reliable_on_data(reliable_t *r, uint16_t seq, const uint8_t *payload, int len) {
    if (!seq_in_window(seq, r->recv_base, RELIABLE_WINDOW))
        return 0;  // 超出窗口，忽略

    int idx = seq % RELIABLE_WINDOW;
    if (!r->recv_bitmap[idx]) {
        memcpy(r->recv_data[idx], payload, len);
        r->recv_lens[idx] = len;
        r->recv_bitmap[idx] = 1;
    }

    return 1;  // 应当发送 ACK
}

/*
 * 处理传入的 ACK
 * ACK 载荷格式：[ ack_seq: u16 | sack_bits: u32 ]  (6 字节)
 * ack_seq = 累积确认（所有 < ack_seq 的都已确认）
 * sack_bits = ack_seq 之后的选择性确认位图
 */
int reliable_on_ack(reliable_t *r, uint16_t ack_seq, uint32_t sack_bits) {
    uint64_t now = time_ms();

    // 根据累积 ACK 推进 send_base
    while (seq_diff(ack_seq, r->send_base) > 0) {
        int idx = r->send_base % RELIABLE_WINDOW;
        retx_entry_t *e = &r->send_buf[idx];
        if (!e->acked) {
            e->acked = 1;
            r->send_count--;

            // PseudoTCP：在 ACK 时更新窗口
            struct p2p_session *s = (struct p2p_session *)((char *)r - offsetof(struct p2p_session, reliable));
            p2p_pseudotcp_on_ack(s, ack_seq);

            // 更新 RTT 估算（仅针对非重传数据包）
            if (e->retx_count == 0 && e->send_time > 0) {
                int rtt = (int)(now - e->send_time);
                if (r->srtt == 0) {
                    r->srtt = rtt;
                    r->rttvar = rtt / 2;
                } else {
                    r->rttvar = (3 * r->rttvar + abs(r->srtt - rtt)) / 4;
                    r->srtt = (7 * r->srtt + rtt) / 8;
                }
                r->rto = r->srtt + 4 * r->rttvar;
                if (r->rto < 50) r->rto = 50;
                if (r->rto > RELIABLE_RTO_MAX) r->rto = RELIABLE_RTO_MAX;
            }
        }
        r->send_base++;
    }

    // SACK 位图：第 i 位 = ack_seq + 1 + i
    for (int i = 0; i < 32; i++) {
        if (sack_bits & (1u << i)) {
            uint16_t s = ack_seq + 1 + i;
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

/*
 * 根据当前接收状态构建 ACK 载荷
 */
static int build_ack_payload(const reliable_t *r, uint8_t *buf) {
    // 累积 ACK：recv_base（它之前的所有内容都已接收）
    uint16_t ack_seq = r->recv_base;
    buf[0] = (uint8_t)(ack_seq >> 8);
    buf[1] = (uint8_t)(ack_seq & 0xFF);

    // SACK 位图：recv_base+0 到 recv_base+31 的位
    uint32_t sack = 0;
    for (int i = 0; i < 32 && i < RELIABLE_WINDOW; i++) {
        int idx = (r->recv_base + i) % RELIABLE_WINDOW;
        if (r->recv_bitmap[idx])
            sack |= (1u << i);
    }
    buf[2] = (uint8_t)(sack >> 24);
    buf[3] = (uint8_t)(sack >> 16);
    buf[4] = (uint8_t)(sack >> 8);
    buf[5] = (uint8_t)(sack);

    return 6;
}

/*
 * 周期性 tick：重传 + 发送 ACK + 刷新待处理数据
 * 仅在需要时发送 ACK
 */
void reliable_tick_ack(reliable_t *r, int sock, const struct sockaddr_in *addr) {
    if (r->recv_base > 0 || r->recv_bitmap[r->recv_base % RELIABLE_WINDOW]) {
        uint8_t ack_payload[6];
        build_ack_payload(r, ack_payload);
        udp_send_packet(sock, addr, P2P_PKT_ACK, 0, 0, ack_payload, 6);
    }
}

/*
 * 标准 ARQ 重传循环
 */
static int reliable_tick(reliable_t *r, int sock, const struct sockaddr_in *addr) {
    uint64_t now = time_ms();
    
    for (int i = 0; i < RELIABLE_WINDOW; i++) {
        uint16_t seq = r->send_base + i;
        if (seq_diff(seq, r->send_seq) >= 0) break;

        int idx = seq % RELIABLE_WINDOW;
        retx_entry_t *e = &r->send_buf[idx];
        if (e->acked) continue;

        if (e->send_time == 0 || now - e->send_time >= (uint64_t)r->rto) {
            udp_send_packet(sock, addr, P2P_PKT_DATA, 0, e->seq, e->data, e->len);
            if (e->send_time != 0) {
                r->rto = (r->rto * 3) / 2;
            }
            e->send_time = now;
            e->retx_count++;
        }
    }

    reliable_tick_ack(r, sock, addr);
    return 0;
}

/*
 * ============================================================================
 * Transport VTable 实现 (Simple ARQ)
 * ============================================================================
 */

static int trans_reliable_init(struct p2p_session *s) {
    reliable_init(&s->reliable);
    return 0;
}

static int trans_reliable_send(struct p2p_session *s, const void *buf, int len) {
    return reliable_send_pkt(&s->reliable, buf, len);
}

static void trans_reliable_tick(struct p2p_session *s) {
    /*
     * 在简单模式下，我们使用原本的 reliable_tick
     */
    reliable_tick(&s->reliable, s->sock, &s->active_addr);
}

const p2p_transport_ops_t p2p_trans_reliable = {
    .name = "SimpleARQ",
    .init = trans_reliable_init,
    .send_data = trans_reliable_send,
    .tick = trans_reliable_tick
};
