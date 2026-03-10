/*
 * P2P 加密层 — 统一发送接口 + DTLS 记录输出
 */

#include "p2p_internal.h"
#include "p2p_dtls.h"

/*
 * dtls_output_raw — 发送原始 DTLS 记录
 *
 * 加密模块（MbedTLS / OpenSSL）的 BIO 输出最终调用此函数。
 * 自动处理中继模式 session_id 封装。
 *
 * 直连: [P2P_HDR: CRYPTO, 0, 0] [dtls_record]
 * 中继: [P2P_HDR: RELAY_CRYPTO, 0, 0] [session_id(8B) | dtls_record]
 */
void dtls_output_raw(p2p_session_t *s, const struct sockaddr_in *addr,
                       const void *dtls_record, int record_len) {

    uint8_t pkt[P2P_HDR_SIZE + sizeof(uint64_t) + P2P_MTU + 64];

    /* TURN 中继: CRYPTO 包通过 Send Indication 发送 */
    if (s->path == P2P_PATH_RELAY && s->turn.state == TURN_ALLOCATED) {
        p2p_pkt_hdr_encode(pkt, P2P_PKT_CRYPTO, 0, 0);
        memcpy(pkt + P2P_HDR_SIZE, dtls_record, record_len);
        p2p_turn_send_indication(s, addr, pkt, P2P_HDR_SIZE + record_len);
        return;
    }

    /* Compact 信令转发: RELAY_CRYPTO + session_id */
    if (s->path == P2P_PATH_SIGNALING) {
        p2p_pkt_hdr_encode(pkt, P2P_PKT_RELAY_CRYPTO, 0, 0);
        nwrite_ll(pkt + P2P_HDR_SIZE, s->sig_compact_ctx.session_id);
        memcpy(pkt + P2P_HDR_SIZE + sizeof(uint64_t), dtls_record, record_len);
        udp_send_to(s->sock, addr, pkt,
                    P2P_HDR_SIZE + (int)sizeof(uint64_t) + record_len);
    } else {
        /* 直连: CRYPTO */
        p2p_pkt_hdr_encode(pkt, P2P_PKT_CRYPTO, 0, 0);
        memcpy(pkt + P2P_HDR_SIZE, dtls_record, record_len);
        udp_send_to(s->sock, addr, pkt, P2P_HDR_SIZE + record_len);
    }
}

/*
 * dtls_send_packet — 统一发送入口
 *
 * 所有传输模块（reliable / pseudotcp / sctp）统一调用此函数替代 udp_send_packet。
 * 调用者始终传递"基础"类型（DATA / ACK），中继封装和加密由本函数处理。
 *
 * 发送路径:
 *   1. DTLS 加密 → encrypt_send → dtls_output_raw
 *   2. TURN 中继（P2P_PATH_RELAY + TURN_ALLOCATED）→ Send Indication
 *   3. Compact 信令转发（P2P_PATH_SIGNALING）→ session_id 封装
 *   4. 直连 → udp_send_packet
 */
int dtls_send_packet(p2p_session_t *s, const struct sockaddr_in *addr,
                       uint8_t type, uint8_t flags, uint16_t seq,
                       const void *payload, int payload_len) {

    /* 加密路径: encrypt_send 内部调用 dtls_output_raw */
    if (s->dtls && s->dtls->is_ready(s)) {
        return s->dtls->encrypt_send(s, addr, type, flags, seq, payload, payload_len);
    }

    /* TURN 中继路径: 将原始 P2P 包通过 Send Indication 发送 */
    if (s->path == P2P_PATH_RELAY && s->turn.state == TURN_ALLOCATED) {
        uint8_t pkt[P2P_MTU];
        if (P2P_HDR_SIZE + payload_len > P2P_MTU) return -1;
        p2p_pkt_hdr_encode(pkt, type, flags, seq);
        if (payload_len > 0 && payload)
            memcpy(pkt + P2P_HDR_SIZE, payload, payload_len);
        return p2p_turn_send_indication(s, addr, pkt, P2P_HDR_SIZE + payload_len);
    }

    /* Compact 信令转发路径: 封装 session_id */
    if (s->path == P2P_PATH_SIGNALING) {
        uint8_t relay_pkt[sizeof(uint64_t) + P2P_MAX_PAYLOAD];
        nwrite_ll(relay_pkt, s->sig_compact_ctx.session_id);
        memcpy(relay_pkt + sizeof(uint64_t), payload, payload_len);
        uint8_t relay_type = (type == P2P_PKT_ACK) ? P2P_PKT_RELAY_ACK : P2P_PKT_RELAY_DATA;
        return udp_send_packet(s->sock, addr, relay_type, flags, seq,
                               relay_pkt, (int)(sizeof(uint64_t) + payload_len));
    }

    return udp_send_packet(s->sock, addr, type, flags, seq, payload, payload_len);
}
