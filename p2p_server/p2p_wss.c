//
// Created by 温朋 on 2026/4/19.
//
#define MOD_TAG "WSS"

#include "p2p_wss.h"

#include <stdarg.h>

ARGS(relay);
ARGS(msg);

// WSS RPC 待确认队列（按 rpc_sent_time 排序，队头最早超时）
static timeout_queue_t              g_wss_rpc_pending_q;

static cw_client_ctx_t              g_wss_ctx;

#define WSS_PEER(s)                 ((wss_session_t*)PEER(s))
#define WSS_CLIENT(s)               ((wss_client_t*)CLIENT(s))

// 前置声明（定义在后面，wss_init 需要引用）
static void wss_handle_frame(cw_client_ctx_t *ctx, cw_client_t *c, uint8_t opcode,
                             uint8_t *payload, uint32_t payload_len, buf16_item_t *buf_item);
static buf16_item_t* wss_handle_handshake_cb(cw_client_ctx_t *ctx, cw_client_t **t_client, uint8_t opcode,
                                             uint8_t *payload, uint32_t payload_len, buf16_item_t *buf_item);
static void wss_handle_peer_sent(ct_client_ctx_t *ct_ctx, ct_session_t *session, buf16_item_t *buf_item);
static void wss_session_break(ct_client_ctx_t *ct_ctx, ct_session_t *ct_session, ct_session_t *ct_peer, break_mode_e break_mode);
static bool wss_parse_sync_frame_sid(const buf16_item_t *item, uint8_t *sid_out);
static ret_t wss_send_printf(cw_client_t *client, uint32_t expect_sz, const char *fmt, ...);

///////////////////////////////////////////////////////////////////////////////

static inline void wss_item_set_range(buf16_item_t *item, uint16_t pos, uint32_t len) {
    if (BUF_IS_32BIT(item->flags)) {
        BUF32(item)->pos = pos;
        BUF32(item)->len = len;
    } else {
        item->pos = pos;
        item->len = (uint16_t)len;
    }
}

static buf16_item_t* wss_take_payload_item(wss_client_t *client,
                                           const uint8_t *payload, size_t payload_len,
                                           buf16_item_t *payload_item) {
    if (!payload_len) return NULL;

    if (!payload_item) {
        buf16_item_t *copy = alloc_buffer(0, 10u + payload_len);
        if (!copy) return NULL;
        memcpy(ITEM2BUF(copy) + 10, payload, payload_len);
        wss_item_set_range(copy, 10, 10u + (uint32_t)payload_len);
        return copy;
    }

    uint8_t *dst = ITEM2BUF(payload_item) + 10;
    if (dst != payload)
        memmove(dst, payload, payload_len);
    wss_item_set_range(payload_item, 10, 10u + (uint32_t)payload_len);
    ((ct_client_t*)client)->payload_buf = NULL;
    return payload_item;
}

static buf16_item_t* wss_append_payload_item(buf16_item_t *base, buf16_item_t *tail) {
    size_t base_len = base ? (size_t)((BUF_IS_32BIT(base->flags) ? BUF32(base)->len : base->len)
                                      - (BUF_IS_32BIT(base->flags) ? BUF32(base)->pos : base->pos)) : 0;
    size_t tail_len = tail ? (size_t)((BUF_IS_32BIT(tail->flags) ? BUF32(tail)->len : tail->len)
                                      - (BUF_IS_32BIT(tail->flags) ? BUF32(tail)->pos : tail->pos)) : 0;
    buf16_item_t *merged;

    if (!base) return tail;
    if (!tail) return base;

    merged = alloc_buffer(0, 10u + base_len + tail_len);
    if (!merged) return NULL;
        memcpy(ITEM2BUF(merged) + 10,
            ITEM2BUF(base) + (BUF_IS_32BIT(base->flags) ? BUF32(base)->pos : base->pos),
            base_len);
        memcpy(ITEM2BUF(merged) + 10 + base_len,
            ITEM2BUF(tail) + (BUF_IS_32BIT(tail->flags) ? BUF32(tail)->pos : tail->pos),
            tail_len);
    wss_item_set_range(merged, 10, 10u + (uint32_t)(base_len + tail_len));

    free_buffer(base);
    free_buffer(tail);
    return merged;
}

static inline uint8_t wss_sync_sid_alloc(wss_session_t *s) {
    uint8_t sid = s->last_sid;
    sid = (sid == 0xFFu) ? 1u : (uint8_t)(sid + 1u);
    if (sid == 0) sid = 1;
    s->last_sid = sid;
    return sid;
}

static buf16_item_t* wss_sync_take_syn0_payload(wss_session_t *s) {
    if (BUF_R_EMPTY(&s->sync_peer_send)) return NULL;
    buf16_item_t *front = BUF_R_FRONT(&s->sync_peer_send);
    if (front->refer != NULL) return NULL;
    if (wss_parse_sync_frame_sid(front, &(uint8_t){0})) return NULL;
    BUF_R_POP(&s->sync_peer_send);
    return front;
}

static bool wss_parse_sync_frame_sid(const buf16_item_t *item, uint8_t *sid_out) {
    const char *text = (const char*)ITEM2BUF((buf16_item_t*)item) + 10;
    unsigned session_id = 0;
    unsigned sid = 0;

    if (sscanf(text, P2P_WSS_CMD_SYNC "%x %x", &session_id, &sid) != 2 || sid > 0xFFu)
        return false;

    *sid_out = (uint8_t)sid;
    return true;
}

static buf16_item_t* wss_build_syn0_online_frame(const char *peer_id, uint32_t session_id, buf16_item_t *payload_item) {
    size_t payload_len = payload_item ? (size_t)((BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->len : payload_item->len)
                                               - (BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->pos : payload_item->pos)) : 0;
    char hdr[8 + P2P_PEER_ID_MAX + 12 + 8];
    int hdr_len = snprintf(hdr, sizeof(hdr), P2P_WSS_RSP_SYN0_REG_FMT, peer_id, session_id);
    uint16_t total;
    buf16_item_t *item;
    uint8_t *buf;

    if (hdr_len <= 0) return NULL;
    total = (uint16_t)(10u + (size_t)hdr_len + payload_len);
    item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) return NULL;

    buf = ITEM2BUF(item) + 10;
    memcpy(buf, hdr, (size_t)hdr_len);
    if (payload_len)
        memcpy(buf + hdr_len,
               ITEM2BUF(payload_item) + (BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->pos : payload_item->pos),
               payload_len);
    item->len = total;
    return item;
}

static const char* wss_req_type_str(uint8_t req_type) {
    switch (req_type) {
    case P2P_WSS_BIN_PKT: return "PKT";
    case P2P_WSS_BIN_REQ: return "REQ";
    case P2P_WSS_BIN_RSP: return "RSP";
    default: return "BIN";
    }
}

// 分配一个预期大小的文本 payload 缓冲，snprintf 写入后按实际长度构建并发送 WS text frame。
static ret_t wss_send_printf(cw_client_t *client, uint32_t expect_sz, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    buf16_item_t *item = cw_vprintf_frame(expect_sz, fmt, args);
    va_end(args);
    if (!item) return E_OUT_OF_MEMORY;

    ret_t r = cw_client_send(client, item, false);
    if (r != E_NONE) free_buffer(item);
    return r;
}

// 按 peer_id 转发 SDP 文本并回复发送方 SDP OK/FAIL
static void wss_handle_sdp(wss_client_t *src_c, const char *remote_peer_id,
                           const uint8_t *sdp, size_t sdp_len) {
    const char *PROTO = "SDP";

    if (!remote_peer_id || !remote_peer_id[0]) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, "", "empty peer id");
        return;
    }
    if (strlen(remote_peer_id) > P2P_PEER_ID_MAX) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "peer id too long");
        return;
    }
    if (!sdp || sdp_len == 0) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "empty sdp");
        return;
    }

    wss_client_t *dst_c = (wss_client_t*)find_client(remote_peer_id);
    if (!dst_c || dst_c == src_c
        || dst_c->base.proto != PROTO_WSS
        || !dst_c->base.local_peer_id[0]
        || (dst_c->io & WSS_IO_FLAG_CLOSING)
        || !TCP_SENDABLE(dst_c)
        || !TCP_REACHABLE(dst_c)) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "peer unreachable");
        return;
    }

    char hdr[8 + P2P_PEER_ID_MAX + 4];
    int hdr_len = snprintf(hdr, sizeof(hdr), P2P_WSS_CMD_SDP_FMT, src_c->base.local_peer_id);
    if (hdr_len <= 0 || (size_t)hdr_len + sdp_len > UINT16_MAX - 10u) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "too large");
        return;
    }

    uint16_t total = (uint16_t)(10u + (size_t)hdr_len + sdp_len);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "OOM");
        return;
    }

    uint8_t *buf = ITEM2BUF(item) + 10;
    memcpy(buf, hdr, (size_t)hdr_len);
    memcpy(buf + hdr_len, sdp, sdp_len);
    item->len = total;

    if (cw_build_frame(WS_OP_TEXT, item, 10) != E_NONE ||
        cw_client_send((cw_client_t*)dst_c, item, false) != E_NONE) {
        wss_send_printf((cw_client_t*)src_c, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "peer unreachable");
        return;
    }

    wss_send_printf((cw_client_t*)src_c, 32u + P2P_PEER_ID_MAX,
                    P2P_WSS_RSP_SDP_OK_FMT, remote_peer_id);

    print("V:", LA_F("%s: '%s' -> '%s' (%zu bytes)\n", LA_F167, 167),
          PROTO, src_c->base.local_peer_id, remote_peer_id, sdp_len);
}

// 构造 SYNC 转发帧：帧头 "SYNC <dst_session_id_hex> <sid_hex>\n" + payload + "\n"(fin mark)
// 返回 buf16_item_t*，payload_pos=10（预留 WS 帧头）；NULL 表示 OOM
static buf16_item_t* wss_build_sync_frame(uint32_t dst_session_id, uint8_t sid, const buf16_item_t *payload_item) {
    size_t payload_len = payload_item ? (size_t)((BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->len : payload_item->len)
                                               - (BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->pos : payload_item->pos)) : 0;
    if (!payload_len) return NULL;

    // "SYNC <session_id_hex> <sid_hex>\n" 前缀
    char hdr_text[24];
    int hdr_len = snprintf(hdr_text, sizeof(hdr_text), P2P_WSS_CMD_SYNC_FMT, dst_session_id, sid);
    if (hdr_len <= 0) return NULL;

    // 帧总大小：10(WS hdr reserve) + hdr_text + payload + '\n'(fin)
    uint16_t total = (uint16_t)(10 + hdr_len + payload_len + 1);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) return NULL;

    uint8_t *buf = ITEM2BUF(item) + 10;
    memcpy(buf, hdr_text, hdr_len);
    buf += hdr_len;
    memcpy(buf, ITEM2BUF(payload_item) + (BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->pos : payload_item->pos), payload_len);
    buf += payload_len;
    *buf = '\n';   // fin mark

    item->len = total;

    return item;
}

// 发送 SYNC confirm 文本帧给 src_c（通知对应 sid 已转发）
static void wss_send_sync_confirm(wss_session_t *src_s, uint8_t sid) {
    wss_client_t *src_c = (wss_client_t*)src_s->base.client;
    if (!TCP_SENDABLE(src_c)) return;
    wss_send_printf((cw_client_t*)src_c, 48u,
                    P2P_WSS_RSP_SYNC_CONFIRM_FMT, src_s->base.session_id, sid);
}

static void wss_sync_send_head(wss_session_t *dst_s) {
    if (BUF_R_EMPTY(&dst_s->sync_peer_send)) return;
    if (!TCP_SENDABLE(dst_s->base.client)) return;

    buf16_item_t *head = BUF_R_FRONT(&dst_s->sync_peer_send);
    uint8_t sid = 0;
    if (!wss_parse_sync_frame_sid(head, &sid)) return;
    if (head->refer == ITEM_REF_ACK_PENDING) return;
    head->refer = (void*)dst_s;
    cw_session_send((ct_session_t*)dst_s, head);
}

static bool wss_enqueue_sync(wss_session_t *src_s, wss_session_t *dst_s, buf16_item_t *payload_item) {
    uint8_t sid;
    buf16_item_t *item;

    if (BUF_R_FULL(&dst_s->sync_peer_send)) return false;

    sid = wss_sync_sid_alloc(src_s);
    item = wss_build_sync_frame(dst_s->base.session_id, sid, payload_item);
    if (!item) return false;

    BUF_R_PUSH(&dst_s->sync_peer_send, item);
    wss_sync_send_head(dst_s);
    wss_send_sync_confirm(src_s, sid);

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u sync_sid=%u\n", LA_F167, 167),
          "SYNC", src_s->base.session_id, dst_s->base.session_id, sid);

    free_buffer(payload_item);
    return true;
}

//-----------------------------------------------------------------------------

// 服务器生成 RPC 错误 RSP（二进制帧）
// 格式：[P2P_WSS_BIN_RSP][session_id(4)][sid(2)][code(1)]
static void wss_session_send_rpc_code(wss_session_t *s, uint16_t sid, uint8_t code) {
    wss_client_t *c = (wss_client_t*)s->base.client;
    if (!TCP_SENDABLE(c)) return;

    uint16_t total = (uint16_t)(10 + P2P_WSS_BIN_RPC_MIN_SZ);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) return;
    uint8_t *buf = ITEM2BUF(item) + 10;
    buf[0] = P2P_WSS_BIN_RSP;
    nwrite_l(buf + 1, s->base.session_id);
    nwrite_s(buf + 1 + P2P_SESS_ID_SZ, sid);
    buf[1 + P2P_SESS_ID_SZ + 2] = code;
    item->len = total;
    if (cw_build_frame(WS_OP_BINARY, item, 10) == E_NONE)
        cw_client_send((cw_client_t*)c, item, false);
}

//-----------------------------------------------------------------------------

// 清理一个 SYNC/PKT 通道的所有队列项，并将存活项转发给 dst（镜像 relay_ch_break_forward）
static void wss_ch_break_forward(buffer_round_t *rq, wss_session_t *dst) {
    if (BUF_R_EMPTY(rq)) return;
    
    buf16_item_t *front = BUF_R_FRONT(rq);
    if (front->refer == ITEM_REF_ACK_PENDING) {
        free_buf16(front);
        BUF_R_POP(rq);
    }

    BUF_R_FOR(rq, it,
        if (it->refer) it->refer = NULL;                // 如果正在发送中，取消 refer（转为由对方发送完成后自动释放）
        else ct_session_send((ct_session_t*)dst, it);   // 直接发送到对方的发送队列中，且不添加 refer
    )
    BUF_R_CLEAR(rq);
}

// 清理一个通道的所有队列项并释放
static void wss_ch_break_free(buffer_round_t *rq) {
    BUF_R_FOR(rq, it, free_buf16(it);)
    BUF_R_CLEAR(rq);
}

// 发送 FIN 文本帧给对端 session 的 client
static void wss_session_send_fin(wss_session_t *session) {
    wss_client_t *c = (wss_client_t*)session->base.client;
    if (!TCP_SENDABLE(c)) return;
    wss_send_printf((cw_client_t*)c, 16u, "FIN %u", session->base.session_id);
}

// session_break 回调（对标 relay_session_break）
// 触发时机：其中一端连接断开或主动下线
static void wss_session_break(ct_client_ctx_t *ct_ctx, ct_session_t *ct_session, ct_session_t *ct_peer, break_mode_e break_mode) { (void)ct_ctx;

    assert(PEER_ONLINE(ct_session) && PEER_ONLINE(ct_peer));

    wss_session_t *session = (wss_session_t*)ct_session;
    wss_session_t *peer    = (wss_session_t*)ct_peer;

    // 存在对端发起的 REQ 等待本端 RSP
    if (peer->rpc_pending_sid) {
        wss_session_send_rpc_code(peer, peer->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
        if (TQ_INQ(&g_wss_rpc_pending_q, peer)) TQ_RM(&g_wss_rpc_pending_q, peer);
        peer->rpc_sent_time   = 0;
        peer->rpc_pending_sid = 0;
    }

    // 存在本端发起的 REQ 等待对端 RSP，// fixme 如果是错误导致的 stop，这里 send 数据会断言报错
    if (session->rpc_pending_sid) {
        if (break_mode != SESS_BREAK_TERM)
            wss_session_send_rpc_code(session, session->rpc_pending_sid, P2P_RPC_ERR_BREAK);
        if (TQ_INQ(&g_wss_rpc_pending_q, session)) TQ_RM(&g_wss_rpc_pending_q, session);
        session->rpc_sent_time   = 0;
        session->rpc_pending_sid = 0;
    }

    if (break_mode == SESS_BREAK_STOP) return;

    // 清理本端队列，转发剩余项给对端
    wss_ch_break_forward(&session->sync_peer_send, peer);
    wss_ch_break_forward(&session->pkt_peer_send,  peer);

    // 向对端发送 FIN
    wss_session_send_fin(peer);

    if (break_mode == SESS_BREAK_CLOSE) {
        wss_ch_break_forward(&peer->sync_peer_send, session);
        wss_ch_break_forward(&peer->pkt_peer_send,  session);
    } else {
        wss_ch_break_free(&peer->sync_peer_send);
        wss_ch_break_free(&peer->pkt_peer_send);
    }

    session->last_sid = peer->last_sid = 0;
}

///////////////////////////////////////////////////////////////////////////////

static bool wss_init_client(client_t *c) {
    return cw_init_client((cw_client_t*)c, &g_wss_ctx);
}

static void wss_free_client(client_t *client) {
    cw_free_client(&g_wss_ctx, (cw_client_t*)client);
}

cw_client_ctx_t*
wss_init(void) {

    // 初始化 RPC 待确认队列
    TQ_INIT(&g_wss_rpc_pending_q,
            REQ_MAX_RETRY * RPC_RETRY_INTERVAL_MS,
            offsetof(wss_session_t, rpc_pending_prev),
            offsetof(wss_session_t, rpc_pending_next),
            offsetof(wss_session_t, rpc_sent_time));

    g_wss_ctx.base.max_payload_len  = WSS_MAX_PAYLOAD;
    g_wss_ctx.base.base.free        = wss_free_client;
    g_wss_ctx.base.base.init        = wss_init_client;
    g_wss_ctx.base.base.migrate     = ct_migrate_client;
    g_wss_ctx.base.handle_peer_sent = wss_handle_peer_sent;
    g_wss_ctx.base.session_break    = wss_session_break;

    cw_ctx_init(&g_wss_ctx);
    g_wss_ctx.sub_protocol   = "p2p";
    g_wss_ctx.handle_handshake = wss_handle_handshake_cb;
    g_wss_ctx.handle_frame     = wss_handle_frame;
    g_wss_ctx.handle_ping      = NULL;
    g_wss_ctx.handle_close     = NULL;
    return &g_wss_ctx;
}

///////////////////////////////////////////////////////////////////////////////

// 处理 SYN0 消息：创建/恢复会话
static void wss_handle_syn0(wss_client_t *client, const char *remote_peer_id,
                            uint8_t *payload, size_t payload_len, buf16_item_t *payload_item) {
    const char *PROTO = "SYN0";
    buf16_item_t *cache_item = NULL;

    if (!*remote_peer_id) {
        print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), PROTO);
        wss_send_printf((cw_client_t*)client, sizeof("SYN0 FAIL invalid remote id"), "%s", "SYN0 FAIL invalid remote id");
        return;
    }

    if (payload_len) {
        cache_item = wss_take_payload_item(client, payload, payload_len, payload_item);
        if (!cache_item) {
            wss_send_printf((cw_client_t*)client, sizeof("SYN0 FAIL OOM"), "%s", "SYN0 FAIL OOM");
            return;
        }
    }

    wss_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, remote_peer_id,
                              (session_t**)&local_s, (session_t**)&remote_s,
                              sizeof(wss_session_t));

    if (side == E_OUT_OF_MEMORY) {
        print("E:", LA_F("%s: OOM building session '%s' -> '%s'\n", LA_F37, 37),
              PROTO, client->base.local_peer_id, remote_peer_id);
        wss_send_printf((cw_client_t*)client, sizeof("SYN0 FAIL OOM"), "%s", "SYN0 FAIL OOM");
        return;
    }
    if (side < E_NONE && side != E_DUPLICATE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, remote_peer_id, side);
        wss_send_printf((cw_client_t*)client, sizeof("SYN0 FAIL internal"), "%s", "SYN0 FAIL internal");
        return;
    }

    // 处理 SYN0 重复请求：幂等返回响应
    if (side == E_DUPLICATE) {
        print("V:", LA_F("%s: duplicate SYN0 (ses_id=%u), resend response\n", LA_F211, 211),
              PROTO, local_s->base.session_id);
        if (cache_item) {
            buf16_item_t *syn0_item = wss_sync_take_syn0_payload(local_s);
            buf16_item_t *merged = wss_append_payload_item(syn0_item, cache_item);
            if (!merged && syn0_item) {
                wss_send_printf((cw_client_t*)client, sizeof("SYN0 FAIL OOM"), "%s", "SYN0 FAIL OOM");
                return;
            }
            if (merged) BUF_R_PUSH(&local_s->sync_peer_send, merged);
        }
        // 根据对端在线状态返回相应响应
        if (remote_s) {
            buf16_item_t *remote_syn0 = wss_sync_take_syn0_payload(remote_s);
            buf16_item_t *resp = wss_build_syn0_online_frame(remote_peer_id, local_s->base.session_id, remote_syn0);
            if (resp) {
                if (cw_build_frame(WS_OP_TEXT, resp, 10) == E_NONE)
                    cw_client_send((cw_client_t*)client, resp, false);
            }
            if (remote_syn0) {
                free_buffer(remote_syn0);
            }
        } else {
            wss_send_printf((cw_client_t*)client, 8u + P2P_PEER_ID_MAX + 12u + 8u,
                            "SYN0 %s %08X offline", remote_peer_id, local_s->base.session_id);
        }
        return;
    }

    // pair_session 成功后，若本端 session 为新建，初始化本端 SYNC/PKT 发送环形队列
    if (PAIR_IS_LOCAL_NEW(side)) {
        BUF_R_INIT(&local_s->sync_peer_send, local_s->sync_peer_slots, WSS_PEER_Q_MAX);
        BUF_R_INIT(&local_s->pkt_peer_send, local_s->pkt_peer_slots, WSS_PEER_Q_MAX);
    }

    print("V:", LA_F("%s: local='%s', remote='%s', online=%d, payload=%u\n", LA_F162, 162),
            PROTO, client->base.local_peer_id, remote_peer_id, remote_s ? 1 : 0, (uint32_t)payload_len);


    if (payload_len) {
        buf16_item_t *syn0_item = wss_sync_take_syn0_payload(local_s);
        buf16_item_t *merged = wss_append_payload_item(syn0_item, cache_item);
        if (!merged && syn0_item) {
            wss_send_printf((cw_client_t*)client, sizeof("SYN0 FAIL OOM"), "%s", "SYN0 FAIL OOM");
            return;
        }
        if (merged) BUF_R_PUSH(&local_s->sync_peer_send, merged);
    }

    // 如果对方不在线，立刻返回 sync0 offline
    if (!remote_s) {

        wss_send_printf((cw_client_t*)client, 8u + P2P_PEER_ID_MAX + 12u + 8u,
                        "SYN0 %s %08X offline", remote_peer_id, local_s->base.session_id);

        print("I:", LA_F("%s: '%s' -> '%s' created (id=%u, peer_zombie)\n", LA_F63, 63),
              PROTO, client->base.local_peer_id, remote_peer_id, local_s->base.session_id);

    }
    // 对端已在线，启动双方 sync0 同步
    else {

        // 建立双向引用关系
        if (!local_s->base.peer) local_s->base.peer = &remote_s->base;
        if (!remote_s->base.peer) remote_s->base.peer = &local_s->base;

        //-------

        {
            buf16_item_t *remote_syn0 = wss_sync_take_syn0_payload(remote_s);
            buf16_item_t *resp = wss_build_syn0_online_frame(remote_peer_id, local_s->base.session_id, remote_syn0);
            if (resp) {
                if (cw_build_frame(WS_OP_TEXT, resp, 10) == E_NONE)
                    cw_client_send((cw_client_t*)client, resp, false);
            } else {
                wss_send_printf((cw_client_t*)client, 8u + P2P_PEER_ID_MAX + 12u + 8u,
                                "SYN0 %s %08X online", remote_peer_id, local_s->base.session_id);
            }
            if (remote_syn0) {
                free_buffer(remote_syn0);
            }
        }
        wss_sync_send_head(local_s);

        //-------

        wss_client_t *remote_c = (wss_client_t*)remote_s->base.client;
        {
            buf16_item_t *local_syn0 = wss_sync_take_syn0_payload(local_s);
            buf16_item_t *resp = wss_build_syn0_online_frame(client->base.local_peer_id, remote_s->base.session_id, local_syn0);
            if (resp) {
                if (cw_build_frame(WS_OP_TEXT, resp, 10) == E_NONE)
                    cw_client_send((cw_client_t*)remote_c, resp, false);
            } else {
                wss_send_printf((cw_client_t*)remote_c, 8u + P2P_PEER_ID_MAX + 12u + 8u,
                                "SYN0 %s %08X online", client->base.local_peer_id, remote_s->base.session_id);
            }
            if (local_syn0) {
                free_buffer(local_syn0);
            }
        }
        wss_sync_send_head(remote_s);

        //-------

        print("I:", LA_F("%s: '%s' <-> '%s' paired (ses=%u/%u)\n", LA_F152, 152),
              PROTO, client->base.local_peer_id, remote_peer_id,
              local_s->base.session_id, remote_s->base.session_id);
    }
}

// 处理 SYNC 消息：按 session_id 路由转发
static void wss_handle_sync(wss_session_t *session, uint8_t sid, const uint8_t *payload, size_t payload_len,
                            buf16_item_t *payload_item) {
    wss_client_t *client = (wss_client_t*)session->base.client;
    uint32_t session_id = session->base.session_id;
    buf16_item_t *cache_item;

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!payload_len) {
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_INVALID);
        return;
    }
    if (!peer_s) {
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_PEER_OFF);
        return;
    }
    if (BUF_R_FULL(&peer_s->sync_peer_send)) {
        wss_send_printf((cw_client_t*)client, 48u, P2P_WSS_RSP_SYNC_BUSY_FMT, session_id, sid);
        return;
    }

    cache_item = wss_take_payload_item(client, payload, payload_len, payload_item);
    if (!cache_item) {
        wss_send_printf((cw_client_t*)client, 48u, P2P_WSS_RSP_SYNC_BUSY_FMT, session_id, sid);
        return;
    }

    if (!wss_enqueue_sync(session, peer_s, cache_item)) {
        wss_send_printf((cw_client_t*)client, 48u, P2P_WSS_RSP_SYNC_BUSY_FMT, session_id, sid);
        free_buffer(cache_item);
    }
}

// 处理 FIN 消息：客户端主动断开会话
static void wss_handle_fin(wss_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: '%s' ses_id=%u\n", LA_F45, 45),
          PROTO, session->base.client->local_peer_id, session->base.session_id);

    ct_session_close(&g_wss_ctx.base, (ct_session_t*)session, true);
}

// 处理 PKT — P2P 数据包中继（重写 session_id，零拷贝转发）
// 使用 payload1（已预留 10 字节 WS 帧头空间），直接入队到对端 pkt_peer_send
// payload 布局（在 payload1 中，从 payload1->pos 起）: [type(1)][session_id(4)][P2P hdr(4)][data(N)]
static void wss_handle_pkt(cw_client_ctx_t *ctx, wss_session_t *session, uint8_t *payload, uint16_t len, buf16_item_t *buf_item) {
    const char *PROTO = "PKT";
    wss_client_t *client = (wss_client_t*)session->base.client;

    if (len < P2P_WSS_BIN_PKT_MIN_SZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_ERR_INVALID);
        return;
    }

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!PEER_ONLINE(session)) {
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_ERR_PEER_OFF);
        return;
    }

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u, data_len=%u\n", LA_F168, 168),
          PROTO, session->base.session_id, peer_s->base.session_id, len - P2P_WSS_BIN_PKT_MIN_SZ);

    // 就地重写 session_id → 对端视角的 session_id
    nwrite_l(payload + 1, peer_s->base.session_id);

    // 检查 pkt_peer_send 队列是否已满
    if (BUF_R_FULL(&peer_s->pkt_peer_send)) {
        print("W:", LA_F("%s: pkt queue full, reply busy\n", LA_F214, 214), PROTO);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_ERR_BUSY);
        return;   // payload1 由调用方（handle_frame）释放
    }

    ct_client_t* c = CT_CLIENT(session);
    buf_item = ct_forward_payload(c, payload, len, 10, buf_item);
    if (!buf_item) {
        print("E:", LA_F("alloc buf failed(OOM)\n", 0, 0));
        ct_client_error(&ctx->base, c, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    // 入队并在首次时冷启动发送
    if (BUF_R_EMPTY(&peer_s->pkt_peer_send)) {
        buf_item->refer = (void*)peer_s;
        if (cw_build_frame(WS_OP_BINARY, buf_item, buf_item->pos) == E_NONE)
            cw_client_send((cw_client_t*)peer_s->base.client, buf_item, false);
    }
    BUF_R_PUSH(&peer_s->pkt_peer_send, buf_item);
    if (!BUF_R_FULL(&peer_s->pkt_peer_send))
        wss_send_printf((cw_client_t*)client, 96u,
                P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_CODE_READY);
}

// 处理 REQ — RPC 请求转发（零拷贝）
static void wss_handle_req(cw_client_ctx_t *ctx, wss_session_t *session, uint8_t *payload, uint16_t len, buf16_item_t *buf_item) {
    const char *PROTO = "REQ";
    wss_client_t *client = (wss_client_t*)session->base.client;

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%u\n", LA_F155, 155), PROTO, len);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "REQ", P2P_ERR_INVALID);
        return;
    }

    uint8_t* ptr = payload + 1 + P2P_SESS_ID_SZ;
    uint16_t sid = nget_s(ptr);
    uint8_t  msg = payload[P2P_WSS_BIN_RPC_MIN_SZ-1];
    int data_len = (int)len - (int)P2P_WSS_BIN_RPC_MIN_SZ;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, session->base.client->local_peer_id, sid, msg, data_len);

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!PEER_ONLINE(session) || !TCP_PEER_REACHABLE(session)) {
        print("W:", LA_F("%s: peer offline, sending error resp\n", LA_F64, 64), PROTO);
        wss_session_send_rpc_code(session, sid, P2P_RPC_ERR_PEER_OFF);
        return;
    }

    if (session->rpc_pending_sid) {
        print("W:", LA_F("%s: rpc busy (pending sid=%u)\n", LA_F67, 67), PROTO, session->rpc_pending_sid);
        wss_session_send_rpc_code(session, sid, P2P_RPC_ERR_TIMEOUT);
        return;
    }

    nwrite_l(payload + 1, peer_s->base.session_id);

    ct_client_t* c = CT_CLIENT(session);
    buf_item = ct_forward_payload(c, payload, len, 10, buf_item);
    if (!buf_item) {
        print("E:", LA_F("alloc buf failed(OOM)\n", 0, 0));
        ct_client_error(&ctx->base, c, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    buf_item->refer = (void*)peer_s;
    if (cw_build_frame(WS_OP_BINARY, buf_item, buf_item->pos) == E_NONE)
        cw_client_send((cw_client_t*)peer_s->base.client, buf_item, false);

    session->rpc_pending_sid = sid;
    session->rpc_sent_time   = P_tick_ms();
    TQ_ADD(&g_wss_rpc_pending_q, session, session->rpc_sent_time);
}

// 处理 RSP — RPC 响应转发（零拷贝）
static void wss_handle_rsp(cw_client_ctx_t *ctx, wss_session_t *session, uint8_t *payload, uint16_t len, buf16_item_t *buf_item) {
    const char *PROTO = "RSP";
    wss_client_t *client = (wss_client_t*)session->base.client;

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%u\n", LA_F155, 155), PROTO, len);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "RSP", P2P_ERR_INVALID);
        return;
    }

    uint8_t* ptr = payload + 1 + P2P_SESS_ID_SZ;
    uint16_t sid  = nget_s(ptr);
    uint8_t  code = payload[P2P_WSS_BIN_RPC_MIN_SZ-1];
    int data_len  = (int)len - (int)P2P_WSS_BIN_RPC_MIN_SZ;

    print("V:", LA_F("%s: '%s' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, session->base.client->local_peer_id, sid, code, data_len);

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!PEER_ONLINE(session)) {
        print("W:", LA_F("%s: requester offline, discarding\n", LA_F66, 66), PROTO);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "RSP", P2P_ERR_PEER_OFF);
        return;
    }

    if (peer_s->rpc_pending_sid != sid) {
        print("W:", LA_F("%s: sid mismatch (got=%u, pending=%u), discarding\n", LA_F68, 68),
              PROTO, sid, peer_s->rpc_pending_sid);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "RSP", P2P_ERR_INVALID);
        return;
    }

    nwrite_l(payload + 1, peer_s->base.session_id);

    ct_client_t* c = CT_CLIENT(session);
    buf_item = ct_forward_payload(c, payload, len, 10, buf_item);
    if (!buf_item) {
        print("E:", LA_F("alloc buf failed(OOM)\n", 0, 0));
        ct_client_error(&ctx->base, c, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    buf_item->refer = (void*)peer_s;
    if (cw_build_frame(WS_OP_BINARY, buf_item, buf_item->pos) == E_NONE)
        cw_client_send((cw_client_t*)peer_s->base.client, buf_item, false);

    if (TQ_INQ(&g_wss_rpc_pending_q, peer_s)) TQ_RM(&g_wss_rpc_pending_q, peer_s);
    peer_s->rpc_pending_sid = 0;
}

//-----------------------------------------------------------------------------

// handle_peer_sent 回调（对标 relay_handle_peer_sent）
// PKT：移出队头，启动下一项；队满→满−1时补发 READY 状态
// SYNC：TCP写出完成，设 ACK_PENDING，等待对端 SYNC confirm（或连接断开时在 session_break 清理）
static void wss_handle_peer_sent(ct_client_ctx_t *ct_ctx, ct_session_t *ct_session, buf16_item_t *buf_item) { (void)ct_ctx;

    assert(PEER_ONLINE(ct_session));
    wss_session_t *session = (wss_session_t*)ct_session;

    // 判断是 PKT 还是 SYNC：WS 帧头在 payload_pos 之前，opcode 在 buf_item->pos 偏移0
    // 简单判断：pkt_peer_send[0] == buf_item → PKT
    if (!BUF_R_EMPTY(&session->pkt_peer_send) && BUF_R_FRONT(&session->pkt_peer_send) == buf_item) {

        buf_item->refer = NULL;  // 允许调用方释放

        bool was_full = BUF_R_FULL(&session->pkt_peer_send);
        BUF_R_POP(&session->pkt_peer_send);

        if (!BUF_R_EMPTY(&session->pkt_peer_send)) {
            buf16_item_t *next = BUF_R_FRONT(&session->pkt_peer_send);
            next->refer = (void*)session;
            // 恢复 payload_pos：零拷贝时 pos=0，payload 在 10 字节之后
            uint8_t *payload_start = ITEM2BUF(next) + 10;
            uint16_t payload_pos = (uint16_t)(payload_start - ITEM2BUF(next));
            if (cw_build_frame(WS_OP_BINARY, next, payload_pos) == E_NONE)
                cw_client_send((cw_client_t*)session->base.client, next, false);
        }
        if (was_full && session->base.peer)
            wss_send_printf((cw_client_t*)session->base.peer->client, 96u,
                            P2P_WSS_RSP_STA_FMT, session->base.peer->session_id,
                            "PKT", P2P_CODE_READY);

    } else if (!BUF_R_EMPTY(&session->sync_peer_send) && BUF_R_FRONT(&session->sync_peer_send) == buf_item) {

        // SYNC：TCP 写入完成，保留队头，设 ACK_PENDING 等待应用层 confirm
        buf_item->refer = ITEM_REF_ACK_PENDING;
    }
}

// SYNC confirm 到来时调用（由 wss_handle_message 中的 "SYNC <sid> confirm" 分支调用）
    // 移出 sync_peer_send 队头，并启动下一个待发项（若存在）
static void wss_on_sync_confirmed(wss_session_t *dst_s, uint8_t sid) {
    if (BUF_R_EMPTY(&dst_s->sync_peer_send)) return;

    buf16_item_t *head = BUF_R_FRONT(&dst_s->sync_peer_send);
    assert(head->refer == ITEM_REF_ACK_PENDING);

    uint8_t expected_sid = 0;
    if (!wss_parse_sync_frame_sid(head, &expected_sid) || expected_sid != sid)
        return;

    // 记录队列是否满（在释放队头之前检查）
    bool was_full = BUF_R_FULL(&dst_s->sync_peer_send);

    head->refer = NULL;
    free_buf16(head);

    BUF_R_POP(&dst_s->sync_peer_send);

    // 队头若已就绪（排队时未能立即发送），继续发送
    if (!BUF_R_EMPTY(&dst_s->sync_peer_send)) {
        buf16_item_t *next = BUF_R_FRONT(&dst_s->sync_peer_send);
        next->refer = (void*)dst_s;
        cw_session_send((ct_session_t*)dst_s, next);
    }

    (void)was_full;
}

static buf16_item_t* wss_handle_handshake_cb(cw_client_ctx_t *ctx, cw_client_t **t_client, uint8_t opcode,
                                             uint8_t *payload, uint32_t payload_len,
                                             buf16_item_t *buf_item) {
    (void)ctx; (void)buf_item;
    wss_client_t *client = (wss_client_t*)*t_client;
    assert(!client->base.local_peer_id[0]);
    const char *PROTO = "REG";
    const char *close_reason = NULL;

    if (client->last_reason) {
        free_buf16(client->last_reason);
        client->last_reason = NULL;
    }

    if (opcode != WS_OP_TEXT) {
        print("E:", LA_F("%s: rejected for not reg(%s)\n", LA_F147, 147), PROTO, wss_req_type_str(opcode));
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "not registered";
        goto error_close;
    }

    uint8_t *line_end = memchr(payload, '\n', payload_len);
    if (!line_end) {
        print("E:", LA_F("%s: rejected for not reg(%s)\n", LA_F160, 160), PROTO, "invalid format");
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "invalid instance_id";
        goto error_close;
    }
    size_t line_len = (size_t)(line_end - payload);
    while (line_len > 0 && (payload[line_len - 1] == '\n' || payload[line_len - 1] == '\r'))
        --line_len;

    if (line_len <= P2P_WSS_CMD_REG_SZ || memcmp(payload, P2P_WSS_CMD_REG, P2P_WSS_CMD_REG_SZ) != 0) {
        print("E:", LA_F("%s: rejected for not reg(%s)\n", LA_F147, 147), PROTO);
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "not registered";
        goto error_close;
    }

    const uint8_t *inst_ptr = NULL;
    for (const uint8_t *scan = payload + line_len; scan > payload + P2P_WSS_CMD_REG_SZ; ) {
        --scan;
        if (*scan == ' ') {
            inst_ptr = scan + 1;
            line_len = (size_t)(scan - payload);
            break;
        }
    }

    if (!inst_ptr || line_len <= P2P_WSS_CMD_REG_SZ) {
        print("E:", LA_F("%s: invalid REG format\n", LA_F160, 160), PROTO);
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "invalid instance_id";
        goto error_close;
    }

    char remote_id[P2P_PEER_ID_MAX + 1];
    size_t remote_len = line_len - P2P_WSS_CMD_REG_SZ;
    if (!remote_len || remote_len > P2P_PEER_ID_MAX) {
        print("E:", LA_F("%s: invalid peer id\n", LA_F96, 96), PROTO);
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "empty peer id";
        goto error_close;
    }
    memcpy(remote_id, payload + P2P_WSS_CMD_REG_SZ, remote_len);
    remote_id[remote_len] = '\0';

    char inst_buf[16];
    size_t inst_len = (size_t)((payload + (line_end - payload)) - inst_ptr);
    while (inst_len > 0 && (inst_ptr[inst_len - 1] == '\n' || inst_ptr[inst_len - 1] == '\r')) --inst_len;
    if (!inst_len || inst_len >= sizeof(inst_buf)) {
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "invalid instance id";
        goto error_close;
    }
    memcpy(inst_buf, inst_ptr, inst_len);
    inst_buf[inst_len] = '\0';

    char *endptr = NULL;
    uint32_t instance_id = (uint32_t)strtoul(inst_buf, &endptr, 10);
    if (!instance_id || !endptr || *endptr != '\0') {
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "invalid instance id";
        goto error_close;
    }

    wss_client_t *reg = (wss_client_t*)find_client(remote_id);
    if (reg) { assert(reg != client);

        if (TCP_HS_IS_HANDSHAKING(reg)) {
            print("E:", LA_F("%s: request simultaneously for '%s'\n", LA_F216, 216), PROTO, reg->base.local_peer_id);
            client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
            close_reason = "duplicate reg";
            goto error_close;
        }

        if (resident_client(&reg->base, PROTO_WSS, instance_id, &client->base)) {
            *t_client = (cw_client_t*)reg;
            client = reg;

            for (session_t *sess = client->base.sessions; sess; sess = sess->next) {
                wss_session_t *ws_sess = (wss_session_t*)sess;
                if (!BUF_R_EMPTY(&ws_sess->sync_peer_send) &&
                    BUF_R_FRONT(&ws_sess->sync_peer_send)->refer == ITEM_REF_ACK_PENDING) {
                    BUF_R_FRONT(&ws_sess->sync_peer_send)->refer = ws_sess;
                    cw_session_send((ct_session_t*)ws_sess, BUF_R_FRONT(&ws_sess->sync_peer_send));
                } else {
                    wss_sync_send_head(ws_sess);
                }
            }

            ct_reactive_client((ct_client_t*)client);

            print("I:", LA_F("%s: '%s' reconnected & reactive (inst=%u)\n", LA_F98, 98),
                  PROTO, client->base.local_peer_id, client->base.instance_id);
        } else {
            print("I:", LA_F("%s: '%s' reconnected & renew (inst=%u)\n", LA_F153, 153),
                  PROTO, reg->base.local_peer_id, instance_id);
        }
    } else {
        client->base.instance_id = instance_id;
        memcpy(client->base.local_peer_id, remote_id, remote_len + 1);

        identify_client(&client->base);

        print("I:", LA_F("%s: '%s' new REG (inst=%u)\n", LA_F93, 93), PROTO,
              client->base.local_peer_id, instance_id);
    }

    buf16_item_t *ack = cw_printf_frame(48u, "REG OK %d %d", WSS_MAX_PAYLOAD,
                                        (ARGS_relay.i64 ? P2P_RLY_FEATURE_RELAY : 0) |
                                        (ARGS_msg.i64 ? P2P_RLY_FEATURE_MSG : 0));
    if (!ack) client->last_error = CUSTOM_TCP_ERR_INTERNAL;
    return ack;

error_close:
    if (close_reason) {
        uint32_t reason_len = (uint32_t)strlen(close_reason);
        client->last_reason = cw_alloc_frame(WS_OP_CLOSE, 2u + reason_len);
        if (client->last_reason)
            memcpy(ITEM2BUF(client->last_reason) + client->last_reason->pos + 2u, close_reason, reason_len);
    }
    if (!client->last_reason && !client->last_error)
        client->last_error = CUSTOM_TCP_ERR_INTERNAL;
    return NULL;
}

// 处理 WSS 模式信令（WebSocket 文本帧）
static void wss_handle_text(cw_client_ctx_t *ctx, wss_client_t *client, const uint8_t *msg, size_t len, buf16_item_t *payload_item) {

    if (len == 0) goto error_proto;

    char* ln = (char*)strnstr((const char*)msg, "\n", len);
    if (!ln) goto error_proto;
    *ln = '\0';
    #define ln_trim while (ln[-1] == '\n' || ln[-1] == '\r') *--ln = '\0'

    if (!client->base.local_peer_id[0]) {
        print("E:", LA_F("%s: rejected for not reg\n", LA_F147, 147), (char*)msg);
        cw_close(ctx, (cw_client_t*)client, WS_CLOSE_PROTOCOL_ERROR, NULL);
        return;
    }

    // PROTO: OFF
    if (strcmp((char*)msg, P2P_WSS_CMD_OFF) == 0) {
        print("I:", LA_F("%s: '%s'\n", LA_F72, 72), "OFF",
              client->base.local_peer_id);
        cw_close(ctx, (cw_client_t*)client, 0, NULL);
        return;
    }

    // SDP <remote_peer_id>\n<sdp>
    if (strncmp((char*)msg, P2P_WSS_CMD_SDP, P2P_WSS_CMD_SDP_SZ) == 0) {
        char *remote_id = (char*)msg + P2P_WSS_CMD_SDP_SZ;
        uint8_t *sdp = (uint8_t*)(ln + 1);
        size_t sdp_len = len - (size_t)(sdp - msg);

        ln_trim;
        while (sdp_len > 0 && (sdp[sdp_len - 1] == '\n' || sdp[sdp_len - 1] == '\r')) {
            --sdp_len;
        }

        wss_handle_sdp(client, remote_id, sdp, sdp_len);
        return;
    }

    // SYN0 <remote_peer_id>[\n<payload>]
    if (strncmp((char*)msg, P2P_WSS_CMD_SYN0, P2P_WSS_CMD_SYN0_SZ) == 0) {

        char *remote_id = (char*)msg + P2P_WSS_CMD_SYN0_SZ;
        uint8_t *payload = (uint8_t*)ln+1;
        ln_trim;

        wss_handle_syn0(client, remote_id, payload, len - (payload - msg), payload_item);
        return;
    }

    // FIN <session_id>
    if (strncmp((char*)msg, P2P_WSS_CMD_FIN, P2P_WSS_CMD_FIN_SZ) == 0) {

        char *sid_str = (char*)msg + P2P_WSS_CMD_FIN_SZ;
        ln_trim;

        uint32_t session_id = (uint32_t)strtoul(sid_str, NULL, 10);
        session_t *s = find_session(session_id);
        if (!s || s->client != &client->base) {
            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "FIN", session_id, client->base.local_peer_id);
            wss_send_printf((cw_client_t*)client, 96u,
                            P2P_WSS_RSP_STA_FMT, session_id, "FIN", P2P_ERR_INVALID);
            return;
        }

        wss_handle_fin((wss_session_t*)s);
        return;
    }

    // SYNC <session_id_hex> <sid_hex> confirm   — SYNC 已到达对端确认（ACK_PENDING 解锁）
    if (strncmp((char*)msg, P2P_WSS_CMD_SYNC, P2P_WSS_CMD_SYNC_SZ) == 0) {

        char *after = (char*)msg + P2P_WSS_CMD_SYNC_SZ;
        unsigned session_id_u = 0;
        unsigned sync_sid_u = 0;
        char op[16] = {0};

        if (sscanf(after, "%x %x %15s", &session_id_u, &sync_sid_u, op) < 2 || sync_sid_u > 0xFFu) {
            wss_send_printf((cw_client_t*)client, 96u,
                            P2P_WSS_RSP_STA_FMT, 0, "SYNC", P2P_ERR_PROTOCOL);
            return;
        }

        uint32_t session_id = (uint32_t)session_id_u;
        uint8_t sync_sid = (uint8_t)sync_sid_u;

        if (strcmp(op, "confirm") == 0) {
            session_t *s = find_session(session_id);
            if (s && s->client == &client->base) {
                wss_on_sync_confirmed((wss_session_t*)s, sync_sid);
            }
            return;
        }

        // 否则为 SYNC <session_id_hex> <sid_hex>\n<payload>
        // Actually ln was ln = first \n already zeroed; payload is after it
        uint8_t *payload = (uint8_t*)(ln + 1);

        session_t *s = find_session(session_id);
        if (!s || s->client != &client->base) {
            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "SYNC", session_id, client->base.local_peer_id);
            wss_send_printf((cw_client_t*)client, sizeof("SYNC FAIL unknown session"), "%s", "SYNC FAIL unknown session");
            return;
        }

        wss_handle_sync((wss_session_t*)s, sync_sid, payload, len - (size_t)(payload - msg), payload_item);
        return;
    }

error_proto:
    wss_send_printf((cw_client_t*)client, 96u,
                    P2P_WSS_RSP_STA_FMT, 0, "TXT", P2P_ERR_PROTOCOL);
    print("V:", LA_F("unknown msg from '%s': %.32s\n", LA_F203, 203),
          client->base.local_peer_id, msg);
}

// handle_frame 回调（custom_ws 框架调用，opcode = TEXT 或 BINARY）
// custom_ws 已在回调前完成 payload0/payload1 合并和解 mask，此处直接使用 payload 指针
static void wss_handle_frame(cw_client_ctx_t *ctx, cw_client_t *c, uint8_t opcode,
                             uint8_t *payload, uint32_t payload_len, buf16_item_t *buf_item) {
    assert(c->base.proto == PROTO_WSS);
    wss_client_t *client = (wss_client_t*)c;

    client->base.last_active = P_tick_ms();

    if (opcode == WS_OP_TEXT) {
        wss_handle_text(ctx, client, payload, payload_len, buf_item);
        return;
    }

    if (payload_len < 1 + P2P_SESS_ID_SZ) {
        print("E:", LA_F(": bad payload(%u)\n", 0, 0), payload_len);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, 0,
                        wss_req_type_str(payload_len ? payload[0] : 0), P2P_ERR_INVALID);
        return;
    }
    uint8_t  type       = payload[0];
    uint8_t* ptr        = payload + 1;
    uint32_t session_id = nget_l(ptr);

    wss_session_t *ws_s = (wss_session_t*)find_session(session_id);
    if (!ws_s || ws_s->base.client != &client->base) {
        print("W:", LA_F(": unknown ses_id=%u type=0x%02x from '%s'\n", LA_F171, 171),
              session_id, type, client->base.local_peer_id);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id,
                        wss_req_type_str(type), P2P_ERR_INVALID);
        return;
    }

    if (!PEER_ONLINE(&ws_s->base)) {
        print("W:", LA_F("BIN 0x%02x: ses_id=%u peer not connected\n", LA_F170, 170), type, session_id);
        if (type == P2P_WSS_BIN_REQ && payload_len >= P2P_WSS_BIN_RPC_MIN_SZ) {
            ptr = payload + 1 + P2P_SESS_ID_SZ;
            wss_session_send_rpc_code(ws_s, nget_s(ptr), P2P_RPC_ERR_PEER_OFF);
        } else {
            wss_send_printf((cw_client_t*)client, 96u,
                            P2P_WSS_RSP_STA_FMT, ws_s->base.session_id,
                            wss_req_type_str(type), P2P_ERR_PEER_OFF);
        }
        return;
    }

    switch (type) {
    case P2P_WSS_BIN_PKT:
        wss_handle_pkt(ctx, ws_s, payload, payload_len, buf_item);
        break;
    case P2P_WSS_BIN_REQ:
        wss_handle_req(ctx, ws_s, payload, payload_len, buf_item);
        break;
    case P2P_WSS_BIN_RSP:
        wss_handle_rsp(ctx, ws_s, payload, payload_len, buf_item);
        break;
    default:
        print("W:", LA_F("BIN: unknown type=0x%02x from '%s'\n", LA_F149, 149),
              type, client->base.local_peer_id);
        wss_send_printf((cw_client_t*)client, 96u,
                        P2P_WSS_RSP_STA_FMT, ws_s->base.session_id,
                        wss_req_type_str(type), P2P_ERR_INVALID);
        break;
    }
}

//-----------------------------------------------------------------------------

// 检查 WSS RPC 超时（队列按时间排序，未超时即短路返回）
void retry_wss_pending(uint64_t now) {

    wss_session_t *s;
    TQ_RETRY(&g_wss_rpc_pending_q, now, s,
        uint16_t sid = s->rpc_pending_sid;
        s->rpc_pending_sid = 0;
        print("W:", LA_F("[W] RPC timeout: sid=%u (ses_id=%u)\n", LA_F199, 199), sid, s->base.session_id);
        wss_session_send_rpc_code(s, sid, P2P_RPC_ERR_TIMEOUT);
    )
}

///////////////////////////////////////////////////////////////////////////////
