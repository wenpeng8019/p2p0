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
#define WSS_ITEM_REF_SYN0_CACHE     ((void*)(uintptr_t)3)

///////////////////////////////////////////////////////////////////////////////

static void wss_sync_send_head(wss_session_t *dst_s) {
    if (BUF_R_EMPTY(&dst_s->sync_peer_send)) return;
    if (!TCP_SENDABLE(dst_s->base.client)) return;

    buf16_item_t *head = BUF_R_FRONT(&dst_s->sync_peer_send);
    if (head->refer == WSS_ITEM_REF_SYN0_CACHE) return;
    const char *text = (const char*)ITEM2BUF(head) + 10;
    unsigned session_id = 0;
    unsigned sid_u = 0;
    if (sscanf(text, P2P_WSS_CMD_SYNC "%x %x", &session_id, &sid_u) != 2 || sid_u > 0xFFu) return;
    if (head->refer == ITEM_REF_ACK_PENDING) return;
    head->refer = (void*)dst_s;
    cw_session_send((ct_session_t*)dst_s, head);
}

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
    item->pos = 10;
    item->len = total;
    if (cw_build_frame(WS_OP_BINARY, item) == E_NONE)
        cw_session_send((ct_session_t*)s, item);
}

///////////////////////////////////////////////////////////////////////////////

static buf16_item_t* wss_build_syn0_cache_item(wss_client_t *client,                    // NOLINTNEXTLINE(readability-non-const-parameter)
                                               uint8_t *msg, size_t msg_len,            // NOLINTNEXTLINE(readability-non-const-parameter)
                                               uint8_t *payload, size_t payload_len,
                                               buf16_item_t *payload_item) {
    char hdr[8 + P2P_PEER_ID_MAX + 12 + 8];
    int hdr_len = snprintf(hdr, sizeof(hdr), P2P_WSS_RSP_SYN0_REG_FMT,
                           client->base.local_peer_id, 0u);
    size_t old_hdr_len = payload ? (size_t)(payload - msg) : msg_len;
    size_t total = 10u + (size_t)hdr_len + payload_len;

    if (hdr_len <= 0) return NULL;

    if (payload_item) {
        uint32_t item_pos = BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->pos : payload_item->pos;
        uint8_t *item_msg = ITEM2BUF(payload_item) + item_pos;
        uint32_t capacity = BUF_IS_32BIT(payload_item->flags) ? BUF32(payload_item)->len : buffer_size(payload_item->flags);

        if (item_msg == msg && total <= capacity) {
            if ((size_t)hdr_len != old_hdr_len && payload_len)
                memmove(item_msg + hdr_len, payload, payload_len);
            memcpy(item_msg, hdr, (size_t)hdr_len);
            if (payload_item == ((ct_client_t*)client)->payload_buf)
                ((ct_client_t*)client)->payload_buf = NULL;
            if (BUF_IS_32BIT(payload_item->flags)) {
                BUF32(payload_item)->pos = item_pos;
                BUF32(payload_item)->len = item_pos + (uint32_t)((size_t)hdr_len + payload_len);
            } else {
                payload_item->pos = (uint16_t)item_pos;
                payload_item->len = (uint16_t)(item_pos + (size_t)hdr_len + payload_len);
            }
            payload_item->refer = WSS_ITEM_REF_SYN0_CACHE;
            return payload_item;
        }
    }

    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag((uint16_t)total), 0));
    if (!item) return NULL;

    uint8_t *buf = ITEM2BUF(item) + 10;
    memcpy(buf, hdr, (size_t)hdr_len);
    if (payload_len)
        memcpy(buf + hdr_len, payload, payload_len);
    item->len = (uint16_t)total;
    item->refer = WSS_ITEM_REF_SYN0_CACHE;
    return item;
}

static void wss_send_syn0_online_item(wss_session_t *to, buf16_item_t *item,
                                      const char *peer_id, uint32_t session_id) {
    char session_id_hex[9];
    uint8_t *sid_pos;

    snprintf(session_id_hex, sizeof(session_id_hex), "%08X", session_id);
    sid_pos = ITEM2BUF(item) + 10 + P2P_WSS_RSP_SYN0_SZ + strlen(peer_id) + 1u;
    memcpy(sid_pos, session_id_hex, 8u);

    item->refer = NULL;
    item->pos = 10;
    if (cw_build_frame(WS_OP_TEXT, item) == E_NONE) {
        ct_session_send((ct_session_t*)to, item);
    }
}

static void wss_session_init(session_t* s) {
    wss_session_t* session = (wss_session_t*)s;
    BUF_R_INIT(&session->sync_peer_send, session->sync_peer_slots, WSS_PEER_Q_MAX);
    BUF_R_INIT(&session->pkt_peer_send, session->pkt_peer_slots, WSS_PEER_Q_MAX);
}

// 处理 SYN0 消息：创建/恢复会话
static void wss_handle_syn0(wss_client_t *client, const char *remote_peer_id,
                            uint8_t *content, size_t content_len,
                            uint8_t *msg, size_t msg_len, buf16_item_t *payload_item) {
    const char *PROTO = "SYN0";
    buf16_item_t *cache_item = NULL;

    if (!*remote_peer_id) {
        print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), PROTO);
        cw_client_printf((cw_client_t*)client, false,
                sizeof("SYN0 FAIL invalid remote id"), "%s", "SYN0 FAIL invalid remote id");
        return;
    }

    if (content_len) {
        cache_item = wss_build_syn0_cache_item(client, msg, msg_len,
                                               content, content_len, payload_item);
        if (!cache_item) {
            cw_client_printf((cw_client_t*)client, false,
                    sizeof("SYN0 FAIL OOM"), "%s", "SYN0 FAIL OOM");
            return;
        }
    }

    wss_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, remote_peer_id,
                              (session_t**)&local_s, (session_t**)&remote_s,
                              sizeof(wss_session_t), wss_session_init);

    if (side == E_OUT_OF_MEMORY) {
        print("E:", LA_F("%s: OOM building session '%s' -> '%s'\n", LA_F37, 37),
              PROTO, client->base.local_peer_id, remote_peer_id);
        cw_client_printf((cw_client_t*)client, false,
                sizeof("SYN0 FAIL OOM"), "%s", "SYN0 FAIL OOM");
        return;
    }
    if (side < E_NONE && side != E_DUPLICATE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, remote_peer_id, side);
        cw_client_printf((cw_client_t*)client, false,
                sizeof("SYN0 FAIL internal"), "%s", "SYN0 FAIL internal");
        return;
    }

    if (local_s->last_sid) {
        print("E:", LA_F("%s: deprecated (ses_id=%u, sid=%u), drop\n", LA_F207, 207),
              PROTO, local_s->base.session_id, local_s->last_sid);
        cw_client_printf((cw_client_t*)client, false,
                sizeof("SYN0 FAIL protocol"), "%s", "SYN0 FAIL protocol");;
        return;
    }

    // 处理 SYN0 重复请求：幂等返回响应
    if (side == E_DUPLICATE) {
        print("V:", LA_F("%s: duplicate SYN0 (ses_id=%u), resend response\n", LA_F211, 211),
              PROTO, local_s->base.session_id);
        if (cache_item) {
            buf16_item_t *syn0_item = NULL;
            if (!BUF_R_EMPTY(&local_s->sync_peer_send) &&
                BUF_R_FRONT(&local_s->sync_peer_send)->refer == WSS_ITEM_REF_SYN0_CACHE) {
                syn0_item = BUF_R_FRONT(&local_s->sync_peer_send);
                BUF_R_POP(&local_s->sync_peer_send);
            }
            if (syn0_item) {
                free_buffer(syn0_item);
            }
            BUF_R_PUSH(&local_s->sync_peer_send, cache_item);
        }
        // 根据对端在线状态返回相应响应
        if (remote_s) {
            buf16_item_t *remote_syn0 = NULL;
            if (!BUF_R_EMPTY(&remote_s->sync_peer_send) &&
                BUF_R_FRONT(&remote_s->sync_peer_send)->refer == WSS_ITEM_REF_SYN0_CACHE) {
                remote_syn0 = BUF_R_FRONT(&remote_s->sync_peer_send);
                BUF_R_POP(&remote_s->sync_peer_send);
            }
            if (remote_syn0)
                wss_send_syn0_online_item(remote_s, remote_syn0,
                                          remote_s->base.client->local_peer_id, local_s->base.session_id);
        } else {
            cw_client_printf((cw_client_t*)client, false, 8u + P2P_PEER_ID_MAX + 12u + 8u,
                            "SYN0 %s %08X offline", remote_peer_id, local_s->base.session_id);
        }
        return;
    }

    print("V:", LA_F("%s: local='%s', remote='%s', online=%d, payload=%u\n", LA_F162, 162),
            PROTO, client->base.local_peer_id, remote_peer_id, remote_s ? 1 : 0, (uint32_t)content_len);


    if (content_len) {
        buf16_item_t *syn0_item = NULL;
        if (!BUF_R_EMPTY(&local_s->sync_peer_send) &&
            BUF_R_FRONT(&local_s->sync_peer_send)->refer == WSS_ITEM_REF_SYN0_CACHE) {
            syn0_item = BUF_R_FRONT(&local_s->sync_peer_send);
            BUF_R_POP(&local_s->sync_peer_send);
        }
        if (syn0_item) {
            free_buffer(syn0_item);
        }
        BUF_R_PUSH(&local_s->sync_peer_send, cache_item);
    }

    // 如果对方不在线，立刻返回 sync0 offline
    if (!remote_s) {

        cw_client_printf((cw_client_t*)client, false,
                8u + P2P_PEER_ID_MAX + 12u + 8u, "SYN0 %s %08X offline", remote_peer_id, local_s->base.session_id);

        print("I:", LA_F("%s: '%s' -> '%s' created (id=%u, peer_zombie)\n", LA_F63, 63),
              PROTO, client->base.local_peer_id, remote_peer_id, local_s->base.session_id);

    }
    // 对端已在线，启动双方 sync0 同步
    else {

        //-------

        {
            buf16_item_t *remote_syn0 = NULL;
            if (!BUF_R_EMPTY(&remote_s->sync_peer_send) &&
                BUF_R_FRONT(&remote_s->sync_peer_send)->refer == WSS_ITEM_REF_SYN0_CACHE) {
                remote_syn0 = BUF_R_FRONT(&remote_s->sync_peer_send);
                BUF_R_POP(&remote_s->sync_peer_send);
            }
            if (remote_syn0) {
                wss_send_syn0_online_item(remote_s, remote_syn0,
                                          remote_s->base.client->local_peer_id, local_s->base.session_id);
            } else {
                cw_client_printf((cw_client_t*)client, false,
                        8u + P2P_PEER_ID_MAX + 12u + 8u, "SYN0 %s %08X online", remote_peer_id, local_s->base.session_id);
            }
        }
        wss_sync_send_head(local_s);

        //-------

        wss_client_t *remote_c = (wss_client_t*)remote_s->base.client;
        {
            buf16_item_t *local_syn0 = NULL;
            if (!BUF_R_EMPTY(&local_s->sync_peer_send) &&
                BUF_R_FRONT(&local_s->sync_peer_send)->refer == WSS_ITEM_REF_SYN0_CACHE) {
                local_syn0 = BUF_R_FRONT(&local_s->sync_peer_send);
                BUF_R_POP(&local_s->sync_peer_send);
            }
            if (local_syn0) {
                wss_send_syn0_online_item(remote_s, local_syn0,
                                          client->base.local_peer_id, remote_s->base.session_id);
            } else {
                cw_client_printf((cw_client_t*)remote_c, false,
                        8u + P2P_PEER_ID_MAX + 12u + 8u, "SYN0 %s %08X online", client->base.local_peer_id, remote_s->base.session_id);
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
static void wss_handle_sync(wss_session_t *session, uint8_t sid,
                            uint8_t *content, size_t content_len,
                            uint8_t *msg, size_t msg_len, buf16_item_t *payload_item) {
    wss_client_t *client = WSS_CLIENT(session);
    uint32_t session_id = session->base.session_id;
    char hdr_text[sizeof("SYNC 00000000 00\n")];
    int hdr_len;
    uint32_t item_pos;
    uint8_t *item_msg;

    if (!content_len) {
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_INVALID);
        return;
    }
    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!peer_s) {
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_PEER_OFF);
        return;
    }

    // 去重：重复包重发 confirm 即可，不重复转发
    if (sid == session->last_sid) {
        print("W:", LA_F("%s: ses_id=%u, dup sid=%u, resend confirm\n", LA_F217, 217),
              "SYNC", session->base.session_id, sid);
        if (TCP_SENDABLE(session->base.client))
            cw_session_printf((ct_session_t*)session, 48u,
                            P2P_WSS_RSP_SYNC_CONFIRM_FMT, session->base.session_id, sid);
        return;
    }

    // 验证同步序的一致性
    if (!uint8_circle_newer(sid, session->last_sid)) {
        print("E:", LA_F("%s: deprecated (ses_id=%u, sid=%u, last=%u), drop\n", LA_F209, 209),
              "SYNC", session->base.session_id, sid, session->last_sid);
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_PROTOCOL);
        return;
    }

    // 忙检查（控速）
    if (BUF_R_FULL(&peer_s->sync_peer_send)) {
        cw_session_printf((ct_session_t*)session, 48u, P2P_WSS_RSP_SYNC_BUSY_FMT, session_id, sid);
        return;
    }

    session->last_sid = sid;

    // todo check
    if (!payload_item) {
        print("E:", LA_F("%s: missing recv item for zero-copy forward\n", LA_F234, 234), "SYNC");
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_PROTOCOL);
        return;
    }

    assert(!BUF_IS_32BIT(payload_item->flags));

    item_pos = payload_item->pos;
    item_msg = ITEM2BUF(payload_item) + item_pos;
    hdr_len = snprintf(hdr_text, sizeof(hdr_text), P2P_WSS_CMD_SYNC_FMT, peer_s->base.session_id, sid);
    if (item_msg != msg || hdr_len <= 0 || (size_t)hdr_len > msg_len) {
        print("E:", LA_F("%s: invalid recv layout for zero-copy forward\n", LA_F233, 233), "SYNC");
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session_id, "SYNC", P2P_ERR_PROTOCOL);
        return;
    }

    memcpy(msg, hdr_text, (size_t)hdr_len);
    if (payload_item == ((ct_client_t*)client)->payload_buf)
        ((ct_client_t*)client)->payload_buf = NULL;
    payload_item->pos = (uint16_t)item_pos;
    payload_item->len = (uint16_t)(item_pos + msg_len);


    BUF_R_PUSH(&peer_s->sync_peer_send, payload_item);
    wss_sync_send_head(peer_s);

    if (TCP_SENDABLE(session->base.client))
        cw_session_printf((ct_session_t*)session, 48u,
                        P2P_WSS_RSP_SYNC_CONFIRM_FMT, session->base.session_id, sid);

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u sync_sid=%u\n", LA_F240, 240),
          "SYNC", session->base.session_id, peer_s->base.session_id, sid);

}

// 处理 FIN 消息：客户端主动断开会话
static void wss_handle_fin(wss_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: '%s' ses_id=%u\n", LA_F45, 45),
          PROTO, session->base.client->local_peer_id, session->base.session_id);

    ct_session_close(&g_wss_ctx.base.base, &session->base, true, false);
}

// 处理 PKT — P2P 数据包中继（重写 session_id，零拷贝转发）
// 使用 payload1（已预留 10 字节 WS 帧头空间），直接入队到对端 pkt_peer_send
// payload 布局（在 payload1 中，从 payload1->pos 起）: [type(1)][session_id(4)][P2P hdr(4)][data(N)]
static void wss_handle_pkt(cw_client_ctx_t *ctx, wss_session_t *session, uint8_t *payload, uint16_t len, buf16_item_t *buf_item) {
    const char *PROTO = "PKT";

    if (len < P2P_WSS_BIN_PKT_MIN_SZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_ERR_INVALID);
        return;
    }

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!PEER_ONLINE(session)) {
        cw_session_printf((ct_session_t*)session, 96u,
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
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_ERR_BUSY);
        return;   // payload1 由调用方（handle_frame）释放
    }

    ct_client_t* c = CT_CLIENT(session);
    buf_item = ct_forward_payload(c, payload, len, 10, buf_item);
    if (!buf_item) {
        print("E:", LA_F("alloc buf failed(OOM)\n", LA_F259, 259));
        ct_client_error(&ctx->base, c, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    // 首次入队时，需要冷启动发送
    if (BUF_R_EMPTY(&peer_s->pkt_peer_send)) {
        if (cw_build_frame(WS_OP_BINARY, buf_item) == E_NONE) {
            buf_item->refer = (void*)peer_s;
            cw_session_send((ct_session_t*)peer_s, buf_item);
        }
    }
    BUF_R_PUSH(&session->pkt_peer_send, buf_item);
    if (!BUF_R_FULL(&peer_s->pkt_peer_send))
        cw_session_printf((ct_session_t*)session, 96u,
                      P2P_WSS_RSP_STA_FMT, session->base.session_id, "PKT", P2P_CODE_READY);
}

// 处理 REQ — RPC 请求转发（零拷贝）
static void wss_handle_req(cw_client_ctx_t *ctx, wss_session_t *session, uint8_t *payload, uint16_t len, buf16_item_t *buf_item) {
    const char *PROTO = "REQ";

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%u\n", LA_F155, 155), PROTO, len);
        cw_session_printf((ct_session_t*)session, 96u,
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
        print("E:", LA_F("alloc buf failed(OOM)\n", LA_F259, 259));
        ct_client_error(&ctx->base, c, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    if (cw_build_frame(WS_OP_BINARY, buf_item) == E_NONE) {
        buf_item->refer = (void*)peer_s;
        cw_session_send((ct_session_t*)peer_s, buf_item);
    }

    session->rpc_pending_sid = sid;
    session->rpc_sent_time   = P_tick_ms();
    TQ_ADD(&g_wss_rpc_pending_q, session, session->rpc_sent_time);
}

// 处理 RSP — RPC 响应转发（零拷贝）
static void wss_handle_rsp(cw_client_ctx_t *ctx, wss_session_t *session, uint8_t *payload, uint16_t len, buf16_item_t *buf_item) {
    const char *PROTO = "RSP";

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%u\n", LA_F155, 155), PROTO, len);
        cw_session_printf((ct_session_t*)session, 96u,
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
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "RSP", P2P_ERR_PEER_OFF);
        return;
    }

    if (peer_s->rpc_pending_sid != sid) {
        print("W:", LA_F("%s: sid mismatch (got=%u, pending=%u), discarding\n", LA_F68, 68),
              PROTO, sid, peer_s->rpc_pending_sid);
        cw_session_printf((ct_session_t*)session, 96u,
                        P2P_WSS_RSP_STA_FMT, session->base.session_id, "RSP", P2P_ERR_INVALID);
        return;
    }

    nwrite_l(payload + 1, peer_s->base.session_id);

    ct_client_t* c = CT_CLIENT(session);
    buf_item = ct_forward_payload(c, payload, len, 10, buf_item);
    if (!buf_item) {
        print("E:", LA_F("alloc buf failed(OOM)\n", LA_F259, 259));
        ct_client_error(&ctx->base, c, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    if (cw_build_frame(WS_OP_BINARY, buf_item) == E_NONE) {
        buf_item->refer = (void*)peer_s;
        cw_session_send((ct_session_t*)peer_s, buf_item);
    }

    if (TQ_INQ(&g_wss_rpc_pending_q, peer_s)) TQ_RM(&g_wss_rpc_pending_q, peer_s);
    peer_s->rpc_pending_sid = 0;
}

// 按 peer_id 转发 SDP 文本并回复发送方 SDP OK/FAIL
static void wss_handle_sdp(wss_client_t *src_c, const char *remote_peer_id, const uint8_t *sdp, size_t sdp_len) {
    const char *PROTO = "SDP";

    if (!remote_peer_id || !remote_peer_id[0]) {
        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, "", "empty peer id");
        return;
    }
    if (strlen(remote_peer_id) > P2P_PEER_ID_MAX) {
        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "peer id too long");
        return;
    }
    if (!sdp || sdp_len == 0) {
        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
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
        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "peer unreachable");
        return;
    }

    char hdr[8 + P2P_PEER_ID_MAX + 4];
    int hdr_len = snprintf(hdr, sizeof(hdr), P2P_WSS_CMD_SDP_FMT, src_c->base.local_peer_id);
    if (hdr_len <= 0 || (size_t)hdr_len + sdp_len > UINT16_MAX - 10u) {
        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "too large");
        return;
    }

    uint16_t total = (uint16_t)(10u + (size_t)hdr_len + sdp_len);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) {
        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "OOM");
        return;
    }

    uint8_t *buf = ITEM2BUF(item) + 10;
    memcpy(buf, hdr, (size_t)hdr_len);
    memcpy(buf + hdr_len, sdp, sdp_len);
    item->len = total;
    item->pos = 10;
    if (cw_build_frame(WS_OP_TEXT, item) != E_NONE) {

        cw_client_send((cw_client_t*)dst_c, item, false);

        cw_client_printf((cw_client_t*)src_c, false, 128u + P2P_PEER_ID_MAX,
                        P2P_WSS_RSP_SDP_FAIL_FMT, remote_peer_id, "peer unreachable");
        return;
    }

    cw_client_printf((cw_client_t*)src_c, false, 32u + P2P_PEER_ID_MAX,
                    P2P_WSS_RSP_SDP_OK_FMT, remote_peer_id);

    print("V:", LA_F("%s: '%s' -> '%s' (%zu bytes)\n", LA_F167, 167),
          PROTO, src_c->base.local_peer_id, remote_peer_id, sdp_len);
}

//-----------------------------------------------------------------------------

static buf16_item_t* wss_handle_handshake(cw_client_ctx_t *ctx, cw_client_t *client, uint8_t opcode,
                                          uint8_t *payload, uint32_t payload_len,
                                          buf16_item_t *buf_item) {
    (void)ctx; (void)buf_item;
    assert(!client->base.local_peer_id[0]);
    const char *PROTO = "REG";
    const char *close_reason = NULL;
    uint32_t reason_len;

    if (client->last_reason) {
        free_buf16(client->last_reason);
        client->last_reason = NULL;
    }

    if (opcode != WS_OP_TEXT) {
        print("E:", LA_F("%s: rejected for not reg(%s)\n", LA_F236, 236), PROTO, "bin frame");
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "not registered";
        goto error_close;
    }

    uint8_t *line_end = memchr(payload, '\n', payload_len);
    if (!line_end) {
        print("E:", LA_F("%s: rejected for not reg(%s)\n", LA_F236, 236), PROTO, "invalid format");
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        close_reason = "invalid instance_id";
        goto error_close;
    }
    size_t line_len = (size_t)(line_end - payload);
    while (line_len > 0 && (payload[line_len - 1] == '\n' || payload[line_len - 1] == '\r'))
        --line_len;

    if (line_len <= P2P_WSS_CMD_REG_SZ || memcmp(payload, P2P_WSS_CMD_REG, P2P_WSS_CMD_REG_SZ) != 0) {
        print("E:", LA_F("%s: rejected for not reg(%.*s)\n", LA_F147, 147), PROTO, (int)line_len, payload);
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
    if (reg) { assert((cw_client_t*)reg != client);

        if (TCP_HS_IS_HANDSHAKING(reg)) {
            print("E:", LA_F("%s: request simultaneously for '%s'\n", LA_F216, 216), PROTO, reg->base.local_peer_id);
            client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
            close_reason = "duplicate reg";
            goto error_close;
        }

        if (restore_client_from(&client->base, &reg->base)) {

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

            activate_client(&client->base, 1);

            print("I:", LA_F("%s: '%s' reconnected & reactive (inst=%u)\n", LA_F231, 231),
                  PROTO, client->base.local_peer_id, client->base.instance_id);
        } else {
            print("I:", LA_F("%s: '%s' reconnected & renew (inst=%u)\n", LA_F232, 232),
                  PROTO, reg->base.local_peer_id, instance_id);

            reg = NULL;
        }
    } else {
        print("I:", LA_F("%s: '%s' new REG (inst=%u)\n", LA_F230, 230), PROTO,
              client->base.local_peer_id, instance_id);
    }

    if (!reg) {
        client->base.instance_id = instance_id;
        identify_client(&client->base, remote_id);
    }

    buf16_item_t *ack = cw_printf_frame(48u, "REG OK %d %d", WSS_MAX_PAYLOAD,
                                        (ARGS_relay.i64 ? P2P_RLY_FEATURE_RELAY : 0) |
                                        (ARGS_msg.i64 ? P2P_RLY_FEATURE_MSG : 0));
    if (!ack) client->last_error = CUSTOM_TCP_ERR_INTERNAL;
    return ack;

error_close:
    reason_len = (uint32_t)strlen(close_reason);
    client->last_reason = cw_alloc_frame(WS_OP_CLOSE, 2u + reason_len);
    if (client->last_reason)
        memcpy(ITEM2BUF(client->last_reason) + client->last_reason->pos + 2u, close_reason, reason_len);
    if (!client->last_reason && !client->last_error)
        client->last_error = CUSTOM_TCP_ERR_INTERNAL;
    return NULL;
}

// 处理 WSS 模式信令（WebSocket 文本帧）
static void wss_handle_text(cw_client_ctx_t *ctx, wss_client_t *client, const uint8_t *msg, size_t len, buf16_item_t *payload_item) {
    (void)ctx;
    assert(client->base.local_peer_id[0]);

    if (len == 0) goto error_proto;

    char* ln = (char*)strnstr((const char*)msg, "\n", len);
    if (!ln) goto error_proto;
    *ln = '\0';
    #define ln_trim while (ln[-1] == '\n' || ln[-1] == '\r') *--ln = '\0'

    // REG
    if (strcmp((char*)msg, P2P_WSS_CMD_REG) == 0) {
        print("E:", LA_F("%s: rejected for not reg\n", LA_F237, 237), (char*)msg);
        cw_close((cw_client_t*)client, WS_CLOSE_PROTOCOL_ERROR, NULL);
        return;
    }

    // OFF
    if (strcmp((char*)msg, P2P_WSS_CMD_OFF) == 0) {
        print("I:", LA_F("%s: '%s'\n", LA_F72, 72), "OFF",
              client->base.local_peer_id);
        cw_close((cw_client_t*)client, 0, NULL);
        return;
    }

    // SDP <remote_peer_id>\n<sdp>
    if (strncmp((char*)msg, P2P_WSS_CMD_SDP, P2P_WSS_CMD_SDP_SZ) == 0) {
        char *remote_id = (char*)msg + P2P_WSS_CMD_SDP_SZ;

        // 解析 sdp 内容
        uint8_t *sdp = (uint8_t*)(ln + 1); size_t sdp_len = len - (size_t)(sdp - msg);
        while (sdp_len > 0 && (sdp[sdp_len - 1] == '\n' || sdp[sdp_len - 1] == '\r')) --sdp_len;

        ln_trim;
        wss_handle_sdp(client, remote_id, sdp, sdp_len);
        return;
    }

    // SYN0 <remote_peer_id>[\n<payload>]
    if (strncmp((char*)msg, P2P_WSS_CMD_SYN0, P2P_WSS_CMD_SYN0_SZ) == 0) {
        char *remote_id = (char*)msg + P2P_WSS_CMD_SYN0_SZ;

        uint8_t *content = (uint8_t*)ln+1;

        ln_trim;
        wss_handle_syn0(client, remote_id, content, len - (content - msg),
                        (uint8_t*)msg, len, payload_item);
        return;
    }

    // FIN <session_id>
    if (strncmp((char*)msg, P2P_WSS_CMD_FIN, P2P_WSS_CMD_FIN_SZ) == 0) {
        char *sess_id_str = (char*)msg + P2P_WSS_CMD_FIN_SZ;

        ln_trim;
        uint32_t sess_id = (uint32_t)strtoul(sess_id_str, NULL, 10);
        session_t *s = find_session(sess_id);
        if (!s || s->client != &client->base) {

            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "FIN", sess_id, client->base.local_peer_id);

            cw_client_printf((cw_client_t*)client, false, 96u,
                            P2P_WSS_RSP_STA_FMT, sess_id, "FIN", P2P_ERR_INVALID);
            return;
        }

        wss_handle_fin((wss_session_t*)s);
        return;
    }

    // SYNC <session_id_hex> <sid_hex> [confirm]
    if (strncmp((char*)msg, P2P_WSS_CMD_SYNC, P2P_WSS_CMD_SYNC_SZ) == 0) {

        char *headline = (char*)msg + P2P_WSS_CMD_SYNC_SZ;
        unsigned sess_id = 0, sid = 0; char op[16] = {0};
        if (sscanf(headline, "%x %x %15s", &sess_id, &sess_id, op) < 2 || sid > 0xFFu) {
            cw_client_printf((cw_client_t*)client, false, 96u,
                            P2P_WSS_RSP_STA_FMT, 0, "SYNC", P2P_ERR_PROTOCOL);
            return;
        }
        session_t *s = find_session(sess_id);

        // 如果是作为 PEER 返回的 SYNC CONFIRM 包
        if (strcmp(op, "confirm") == 0) {
            if (!s || s->client != &client->base || !PEER_ONLINE(s)) return;
            wss_session_t *src_s = (wss_session_t*)s->peer;

            if (BUF_R_EMPTY(&src_s->sync_peer_send)) return;

            buf16_item_t *front = BUF_R_FRONT(&src_s->sync_peer_send);
            unsigned expected_sess_id = 0, expected_sid = 0;

            if (front->refer == ITEM_REF_ACK_PENDING
                && sscanf((const char*)ITEM2BUF(front) + 10, P2P_WSS_CMD_SYNC "%x %x", &expected_sess_id, &expected_sid) == 2
                && (uint8_t)expected_sid == sid) {

                front->refer = NULL;
                free_buffer(front);
                BUF_R_POP(&src_s->sync_peer_send);

                if (!BUF_R_EMPTY(&src_s->sync_peer_send)) {
                    front = BUF_R_FRONT(&src_s->sync_peer_send);
                    front->refer = (void*)src_s;
                    cw_session_send((ct_session_t*)src_s, front);
                }
            }
            return;
        }

        // 对于本端发起的 SYNC 包：<session_id_hex> <sid_hex>\n<payload>

        if (!s || s->client != &client->base) {
            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "SYNC", sess_id, client->base.local_peer_id);
            cw_client_printf((cw_client_t*)client, false,
                    sizeof("SYNC FAIL unknown session"), "%s", "SYNC FAIL unknown session");
            return;
        }

        // 否则为 SYNC <session_id_hex> <sid_hex>\n<payload>
        uint8_t *content = (uint8_t*)ln+1;
        wss_handle_sync((wss_session_t*)s, sid,
                        content, len - (size_t)(content - msg),
                        (uint8_t*)msg, len, payload_item);
        return;
    }

error_proto:
    cw_client_printf((cw_client_t*)client, false, 96u,
                    P2P_WSS_RSP_STA_FMT, 0, "TXT", P2P_ERR_PROTOCOL);
    print("V:", LA_F("unknown msg from '%s': %.32s\n", LA_F203, 203),
          client->base.local_peer_id, msg);
}

static const char* wss_req_type_str(uint8_t req_type) {
    switch (req_type) {
    case P2P_WSS_BIN_PKT: return "PKT";
    case P2P_WSS_BIN_REQ: return "REQ";
    case P2P_WSS_BIN_RSP: return "RSP";
    default: return "UNKNOWN";
    }
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
        print("E:", LA_F(": bad payload(%u)\n", LA_F242, 242), payload_len);
        cw_client_printf((cw_client_t*)client, false, 96u,
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
        cw_client_printf((cw_client_t*)client, false, 96u,
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
            cw_client_printf((cw_client_t*)client, false, 96u,
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
        cw_client_printf((cw_client_t*)client, false, 96u,
                        P2P_WSS_RSP_STA_FMT, ws_s->base.session_id,
                        wss_req_type_str(type), P2P_ERR_INVALID);
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////

// handle_peer_sent 回调（对标 relay_handle_peer_sent）
// PKT：移出队头，启动下一项；队满→满−1时补发 READY 状态
// SYNC：TCP写出完成，设 ACK_PENDING，等待对端 SYNC confirm（或连接断开时在 session_break 清理）
static void wss_handle_peer_sent(ct_client_ctx_t *ct_ctx, ct_session_t *ct_session, buf16_item_t *buf_item) { (void)ct_ctx;

    assert(PEER_ONLINE(ct_session));
    wss_session_t *session = (wss_session_t*)ct_session;

    // 判断是 PKT 还是 SYNC：WS 帧头在 payload_pos 之前，opcode 在 buf_item->pos 偏移0
    // 简单判断：pkt_peer_send[0] == buf_item → PKT
    if (!BUF_R_EMPTY(&session->pkt_peer_send) && BUF_R_FRONT(&session->pkt_peer_send) == buf_item) {

        bool full = BUF_R_FULL(&session->pkt_peer_send);

        // 标记该 buf_item 已经处理完成（可被释放），并移出队头
        buf_item->refer = NULL;  // 允许调用方释放
        BUF_R_POP(&session->pkt_peer_send);

        // 启动发送下一项（如有）
        if (!BUF_R_EMPTY(&session->pkt_peer_send)) {
            buf16_item_t *next = BUF_R_FRONT(&session->pkt_peer_send);
            if (cw_build_frame(WS_OP_BINARY, next) == E_NONE) {
                next->refer = session;
                cw_session_send((ct_session_t*)session, next);
            }
        }

        if (full && session->base.peer)
            cw_session_printf((ct_session_t*)session, 96u,
                            P2P_WSS_RSP_STA_FMT, session->base.peer->session_id,
                            "PKT", P2P_CODE_READY);

    } else if (!BUF_R_EMPTY(&session->sync_peer_send) && BUF_R_FRONT(&session->sync_peer_send) == buf_item) {

        // SYNC：TCP 写入完成，保留队头，设 ACK_PENDING 等待应用层 confirm
        buf_item->refer = ITEM_REF_ACK_PENDING;
    }
}

// 清理一个通道的所有队列项，并将存活项转发给目标会话
static void wss_break_forward(buffer_round_t *rq, wss_session_t *to) {
    if (BUF_R_EMPTY(rq)) return;

    buf16_item_t *front = BUF_R_FRONT(rq);
    if (front->refer == ITEM_REF_ACK_PENDING) {
        free_buffer(front);
        BUF_R_POP(rq);
    }

    BUF_R_FOR(rq, it,
        if (it->refer) it->refer = NULL;                        // 如果正在发送中，取消 refer（转为由对方发送完成后自动释放）
        else if (to) ct_session_send((ct_session_t*)to, it);    // 直接发送到对方的发送队列中，且不添加 refer
        else free_buffer(it);
    )
    BUF_R_CLEAR(rq);
}

// session_break 回调（对标 relay_session_break）
// 触发时机：其中一端连接断开或主动下线
static void wss_session_break(client_ctx_t *ctx, session_t *s, session_t *ps, break_mode_e break_mode) { (void)ctx;

    // 前提是双方在线
    assert(PEER_ONLINE(s) && PEER_ONLINE(ps));

    wss_session_t *session = (wss_session_t*)s; wss_session_t *peer = (wss_session_t*)ps;

    // 存在对端发起的 REQ 等待本端 RSP
    if (peer->rpc_pending_sid) {
        wss_session_send_rpc_code(peer, peer->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
        if (TQ_INQ(&g_wss_rpc_pending_q, peer)) TQ_RM(&g_wss_rpc_pending_q, peer);
        peer->rpc_sent_time   = 0;
        peer->rpc_pending_sid = 0;
    }

    // 存在本端发起的 REQ 等待对端 RSP
    if (session->rpc_pending_sid) {

        if (break_mode != SESS_BREAK_TERM)
            wss_session_send_rpc_code(session, session->rpc_pending_sid, P2P_RPC_ERR_BREAK);

        if (TQ_INQ(&g_wss_rpc_pending_q, session)) TQ_RM(&g_wss_rpc_pending_q, session);
        session->rpc_sent_time   = 0;
        session->rpc_pending_sid = 0;
    }

    if (break_mode == SESS_BREAK_STOP) return;

    if (break_mode == SESS_BREAK_CLOSE) {
        wss_break_forward(&peer->sync_peer_send, session);
        wss_break_forward(&peer->pkt_peer_send,  session);
    } else {
        wss_break_forward(&peer->sync_peer_send, NULL);
        wss_break_forward(&peer->pkt_peer_send, NULL);
    }

    // 把剩余数据发给对端
    wss_break_forward(&session->sync_peer_send, peer);
    wss_break_forward(&session->pkt_peer_send,  peer);

    // 向对端发送 FIN
    cw_session_printf((ct_session_t*)peer, 16u, "FIN %u", peer->base.session_id);

    session->last_sid = peer->last_sid = 0;
}


cw_client_ctx_t*
wss_init(void) {

    cw_ctx_init(&g_wss_ctx);

    g_wss_ctx.base.max_payload_len  = WSS_MAX_PAYLOAD;
    g_wss_ctx.base.base.cb_activate = ct_client_activate;
    g_wss_ctx.base.base.cb_break    = wss_session_break;
    g_wss_ctx.base.handle_peer_sent = wss_handle_peer_sent;

    g_wss_ctx.sub_protocol   = "p2p";
    g_wss_ctx.handle_handshake = wss_handle_handshake;
    g_wss_ctx.handle_frame     = wss_handle_frame;

    // 初始化 RPC 待确认队列
    TQ_INIT(&g_wss_rpc_pending_q,
            REQ_MAX_RETRY * RPC_RETRY_INTERVAL_MS,
            offsetof(wss_session_t, rpc_pending_prev),
            offsetof(wss_session_t, rpc_pending_next),
            offsetof(wss_session_t, rpc_sent_time));

    return &g_wss_ctx;
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
