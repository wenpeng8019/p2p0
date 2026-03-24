/*
 * P2P 通道发送层 — 统一发送接口 + 中继/加密适配
 */

#include "p2p_internal.h"
#include "p2p_dtls.h"

ret_t p2p_udp_send_packet(p2p_session_t *s, const struct sockaddr_in *addr,
                          uint8_t type, uint8_t flags, uint16_t seq,
                          const void *payload, int payload_len) {

    if (P2P_HDR_SIZE + payload_len > P2P_MTU) return E_INVALID;

    uint8_t hdr[4]; int n = 1;
    p2p_pkt_hdr_encode(hdr, type, flags, seq);
    sock_msg_t msgs[2];
    P_msg_set(&msgs[0], hdr, sizeof(hdr));
    if (payload_len > 0 && payload)
        P_msg_set(&msgs[n++], payload, payload_len);

    return P_msg_send_to(s->sock, msgs, n, addr);
}

ret_t p2p_turn_send_packet(p2p_session_t *s, const struct sockaddr_in *addr,
                           uint8_t type, uint8_t flags, uint16_t seq,
                           const void *payload, int payload_len) {

    int total = P2P_HDR_SIZE + payload_len;
    if (total > P2P_MTU) return E_INVALID;

    uint8_t hdr[4]; int n = 1;
    p2p_pkt_hdr_encode(hdr, type, flags, seq);
    sock_msg_t msgs[2];
    P_msg_set(&msgs[0], hdr, sizeof(hdr));
    if (payload_len > 0 && payload)
        P_msg_set(&msgs[n++], payload, payload_len);

    return p2p_turn_send_indication(s, addr, msgs, n);
}

///////////////////////////////////////////////////////////////////////////////

/*
 * p2p_send_packet — 统一发送入口
 *
 * 所有传输模块（reliable / pseudotcp / sctp）统一调用此函数替代 udp_send_packet。
 * 调用者始终传递"基础"类型（DATA / ACK），中继封装和加密由本函数处理。
 *
 * 发送路径:
 *   1. DTLS 加密 → encrypt_send → p2p_send_dtls_record
 *   2. TURN 中继（P2P_PATH_RELAY + TURN_ALLOCATED）→ Send Indication
 *   3. Compact 信令转发（P2P_PATH_SIGNALING）→ session_id 封装
 *   4. 直连 → p2p_udp_send_packet
 */
int p2p_send_packet(p2p_session_t *s, const struct sockaddr_in *addr,
                       uint8_t type, uint8_t flags, uint16_t seq,
                       const void *payload, int payload_len) {

    /* 加密路径: 有 payload 时加密，控制包（CONN/CONN_ACK）无 payload 不加密 */
    if (payload && s->dtls && s->dtls->is_ready(s)) {

        uint8_t plain[P2P_HDR_SIZE + P2P_MAX_PAYLOAD];
        p2p_pkt_hdr_encode(plain, type, flags, seq);
        if (payload_len > 0)
            memcpy(plain + P2P_HDR_SIZE, payload, payload_len);

        return s->dtls->encrypt_send(s, addr, plain, P2P_HDR_SIZE + payload_len);
    }

    /* TURN 中继路径: 将原始 P2P 包通过 Send Indication 发送 */
    if (s->path_type == P2P_PATH_RELAY) {
        if (s->turn.state != TURN_ALLOCATED) {
            print("E:", LA_F("RELAY path but TURN not allocated", LA_F429, 429));
            return -1;
        }
        return p2p_turn_send_packet(s, addr, type, flags, seq, payload, payload_len);
    }

    /* 信令转发路径: 调用信令模式特定的中转接口 */
    if (s->path_type == P2P_PATH_SIGNALING) {
        if (!s->signaling_relay_fn) {
            print("E:", LA_F("SIGNALING path but signaling relay not available", LA_F319, 319));
            return -1;
        }
        return s->signaling_relay_fn(s, type, flags, seq, payload, payload_len);
    }

    return p2p_udp_send_packet(s, addr, type, flags, seq, payload, payload_len);
}

/*
 * p2p_send_dtls_record — 发送原始 DTLS 记录
 *
 * 加密模块（MbedTLS / OpenSSL）的 BIO 输出最终调用此函数。
 *
 * 这里 P2P_PKT_CRYPTO 包自身的 flags 和 seq 字段不使用
 * 密文所负载的明文的 flags 和 seq 被封装在加密数据中，由加密模块处理
 * 对于 signaling relay 中转添加 session_id 的情况，
 * 信令中转接口（signaling_relay_fn）会自动处理封装，无需本函数关心
 */
void p2p_send_dtls_record(p2p_session_t *s, const struct sockaddr_in *addr,
                       const void *dtls_record, int record_len) {

    /* TURN 中继: CRYPTO 包通过 Send Indication 发送 */
    if (s->path_type == P2P_PATH_RELAY) {
        if (s->turn.state != TURN_ALLOCATED) {
            print("W:", LA_F("RELAY path but TURN not allocated (dtls)", LA_F434, 434));
            return;
        }
        p2p_turn_send_packet(s, addr, P2P_PKT_CRYPTO, 0, 0, dtls_record, record_len);
        return;
    }

    /* 信令转发: CRYPTO 包通过信令中转接口发送 */
    if (s->path_type == P2P_PATH_SIGNALING) {
        if (!s->signaling_relay_fn) {
            print("E:", LA_F("SIGNALING path but signaling relay not available", LA_F319, 319));
            return;
        }
        s->signaling_relay_fn(s, P2P_PKT_CRYPTO, 0, 0, dtls_record, record_len);
        return;
    }

    p2p_udp_send_packet(s, addr, P2P_PKT_CRYPTO, 0, 0, dtls_record, record_len);
}
