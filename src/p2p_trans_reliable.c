/*
 * 基于滑动窗口和 SACK 的数据包级 ARQ
 */

#include "p2p_udp.h"
#include "p2p_internal.h"
#include "p2p_log.h"
#include "p2p_lang.h"

#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Implementation
///////////////////////////////////////////////////////////////////////////////

static inline int seq_in_window(uint16_t seq, uint16_t base, int window) {
    int16_t d = seq_diff(seq, base);
    return d >= 0 && d < window;
}

void reliable_init(reliable_t *r) {
    memset(r, 0, sizeof(*r));
    r->rto = RELIABLE_RTO_INIT;
    r->srtt = 0;
    r->rttvar = 0;
    P2P_LOG_DEBUG("RELIABLE", "%s rto=%d win=%d",
                  MSG(MSG_RELIABLE_INIT), RELIABLE_RTO_INIT, RELIABLE_WINDOW);
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
    if (r->send_count >= RELIABLE_WINDOW) {
        P2P_LOG_WARN("RELIABLE", "%s send_count=%d", MSG(MSG_RELIABLE_WINDOW_FULL), r->send_count);
        return -1;
    }
    if (len > P2P_MAX_PAYLOAD) {
        P2P_LOG_WARN("RELIABLE", "%s len=%d max=%d", MSG(MSG_RELIABLE_PKT_TOO_LARGE), len, P2P_MAX_PAYLOAD);
        return -1;
    }

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
    P2P_LOG_TRACE("RELIABLE", "%s seq=%u len=%d inflight=%d",
                  MSG(MSG_RELIABLE_PKT_QUEUED), e->seq, len, r->send_count);
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
    if (!seq_in_window(seq, r->recv_base, RELIABLE_WINDOW)) {
        P2P_LOG_DEBUG("RELIABLE", "%s seq=%u base=%u",
                      MSG(MSG_RELIABLE_OUT_OF_WINDOW), seq, r->recv_base);
        return 0;  // 超出窗口，忽略
    }

    int idx = seq % RELIABLE_WINDOW;
    if (!r->recv_bitmap[idx]) {
        memcpy(r->recv_data[idx], payload, len);
        r->recv_lens[idx] = len;
        r->recv_bitmap[idx] = 1;
        P2P_LOG_TRACE("RELIABLE", "%s seq=%u len=%d base=%u",
                      MSG(MSG_RELIABLE_DATA_STORED), seq, len, r->recv_base);
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

            // PseudoTCP：在 ACK 时更新窗口（仅当启用拥塞控制时，避免 cwnd=0 除零崩溃）
            struct p2p_session *s = (struct p2p_session *)((char *)r - offsetof(struct p2p_session, reliable));
            if (s->cfg.use_pseudotcp)
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
                P2P_LOG_DEBUG("RELIABLE", "%s rtt=%dms srtt=%d rttvar=%d rto=%d",
                              MSG(MSG_RELIABLE_RTT_UPDATE), rtt, r->srtt, r->rttvar, r->rto);
            }
        }
        r->send_base++;
    }
    P2P_LOG_DEBUG("RELIABLE", "%s ack_seq=%u send_base=%u inflight=%d",
                  MSG(MSG_RELIABLE_ACK_PROCESSED), ack_seq, r->send_base, r->send_count);

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

    // SACK 位图：第 i 位 = recv_base + 1 + i（与接收方解读一致：ack_seq + 1 + i）
    // 注：循环上限用 RELIABLE_WINDOW-1 而非 RELIABLE_WINDOW，避免环形缓冲区回绕导致漏报已确认包
    uint32_t sack = 0;
    for (int i = 0; i < 32 && i < RELIABLE_WINDOW - 1; i++) {
        int idx = (r->recv_base + 1 + i) % RELIABLE_WINDOW;
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
void reliable_tick_ack(reliable_t *r, int sock, const struct sockaddr_in *addr, int is_relay_mode) {
    if (r->recv_base > 0 || r->recv_bitmap[r->recv_base % RELIABLE_WINDOW]) {
        uint8_t ack_payload[6];
        build_ack_payload(r, ack_payload);
        uint16_t ack_seq = ((uint16_t)ack_payload[0] << 8) | ack_payload[1];
        uint32_t sack = ((uint32_t)ack_payload[2] << 24) | ((uint32_t)ack_payload[3] << 16)
                      | ((uint32_t)ack_payload[4] << 8)  | (uint32_t)ack_payload[5];
        P2P_LOG_DEBUG("RELIABLE", "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",
                      ack_seq, sack, r->recv_base,
                      addr ? inet_ntoa(addr->sin_addr) : "?",
                      addr ? ntohs(addr->sin_port) : 0);
        // 中继模式使用 RELAY_ACK，直连P2P使用 ACK
        uint8_t pkt_type = is_relay_mode ? P2P_PKT_RELAY_ACK : P2P_PKT_ACK;
        udp_send_packet(sock, addr, pkt_type, 0, 0, ack_payload, 6);
    }
}

/*
 * 周期 tick：发送/重传数据包 + 发 ACK
 *
 * 每次 p2p_update 调用一次，负责：
 *   1. 首次发送队列中 send_time==0 的包
 *   2. 对超过 RTO 未确认的包进行指数退避重传
 *   3. 和 reliable_tick_ack 一起发送 ACK
 */
void reliable_tick(reliable_t *r, int sock, const struct sockaddr_in *addr, int is_relay_mode) {
    if (!addr) return;
    uint64_t now = time_ms();
    uint8_t pkt_type = is_relay_mode ? P2P_PKT_RELAY_DATA : P2P_PKT_DATA;

    /* 遍历所有未确认的发送条目 */
    int window = (uint16_t)(r->send_seq - r->send_base);
    for (int i = 0; i < window && i < RELIABLE_WINDOW; i++) {
        int idx = (r->send_base + i) % RELIABLE_WINDOW;
        retx_entry_t *e = &r->send_buf[idx];
        if (e->acked) continue;

        if (e->send_time == 0) {
            /* 首次发送 */
            udp_send_packet(sock, addr, pkt_type, 0, e->seq, e->data, e->len);
            e->send_time = now;
            e->retx_count = 0;
        } else if ((int)(now - e->send_time) >= r->rto) {
            /* 超时重传 + 指数退避 */
            udp_send_packet(sock, addr, pkt_type, 0, e->seq, e->data, e->len);
            e->send_time = now;
            e->retx_count++;
            r->rto = r->rto * 2;
            if (r->rto > RELIABLE_RTO_MAX) r->rto = RELIABLE_RTO_MAX;
            P2P_LOG_WARN("RELIABLE", "重传 seq=%u retx=%d rto=%d",
                         e->seq, e->retx_count, r->rto);
        }
    }

    /* 发送 ACK */
    reliable_tick_ack(r, sock, addr, is_relay_mode);
}

/*
 * 注：reliable 作为基础传输层，由 p2p.c 和高级传输层直接调用：
 *   - reliable_init()        → p2p_create() 初始化
 *   - reliable_send_pkt()    → stream_flush_to_reliable()
 *   - reliable_tick_ack()    → PseudoTCP tick 中调用
 *   - reliable_on_data()     → p2p_update() 接收 DATA 包
 *   - reliable_on_ack()      → p2p_update() 接收 ACK 包
 *
 * 不再暴露为 p2p_transport_ops_t 对象，避免不必要的 VTable 间接调用。
 */
