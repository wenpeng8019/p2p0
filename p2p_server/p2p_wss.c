//
// Created by 温朋 on 2026/4/19.
//

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "WSS"

#include "p2p_wss.h"

ARGS(relay);
ARGS(msg);

// WSS RPC 待确认链表（按 rpc_sent_time 排序，队头最早超时）
static wss_session_t*               g_wss_rpc_pending_head = NULL;
static wss_session_t*               g_wss_rpc_pending_rear = NULL;

///////////////////////////////////////////////////////////////////////////////

/* 写入 n 字节（可绕回），返回 0=成功, -1=空间不足 */
static inline int wss_sync_write(wss_session_t *s, const char *src, size_t n) {
    if (n > WSS_SYNC_PAYLOAD_MAX - s->sync_len) return -1;
    size_t tail = (s->sync_head + s->sync_len) % WSS_SYNC_PAYLOAD_MAX;
    size_t to_end = WSS_SYNC_PAYLOAD_MAX - tail;
    if (n <= to_end) {
        memcpy(s->sync_data + tail, src, n);
    } else {
        memcpy(s->sync_data + tail, src, to_end);
        memcpy(s->sync_data, src + to_end, n - to_end);
    }
    s->sync_len += n;
    return 0;
}

/* 追加 payload 到 sync ring buffer，多次追加以 \n 分隔
 * 返回: 0=成功, -1=空间不足（busy） */
static int wss_sync_cat(wss_session_t *s, const char *payload) {
    if (!payload || !payload[0]) return 0;
    size_t new_len = strlen(payload);
    size_t sep = (s->sync_len > 0) ? 1 : 0;        /* \n 分隔追加部分 */
    if (sep + new_len > WSS_SYNC_PAYLOAD_MAX - s->sync_len) return -1;
    if (sep) wss_sync_write(s, "\n", 1);
    wss_sync_write(s, payload, new_len);
    return 0;
}

/* copy-out：将 ring buffer 内容拷贝到连续 dest，返回拷贝字节数 */
static inline size_t wss_sync_copy_out(const wss_session_t *s, char *dest) {
    if (!s->sync_len) return 0;
    size_t first = WSS_SYNC_PAYLOAD_MAX - s->sync_head;
    if (first >= s->sync_len) {
        memcpy(dest, s->sync_data + s->sync_head, s->sync_len);
    } else {
        memcpy(dest, s->sync_data + s->sync_head, first);
        memcpy(dest + first, s->sync_data, s->sync_len - first);
    }
    return s->sync_len;
}

/* 判断 client 是否在线（已注册且 WS 连接有效，握手完成） */
static inline bool wss_client_online(const wss_client_t *c) {
    return c && c->base.fd != P_INVALID_SOCKET && !c->ws_handshake && c->ws_ctx;
}

/* 生成 features 位掩码（与 P2P_RLY_FEATURE_* 一致） */
static inline int wss_features(void) {
    int f = 0;
    if (ARGS_relay.i64) f |= P2P_RLY_FEATURE_RELAY;
    if (ARGS_msg.i64)   f |= P2P_RLY_FEATURE_MSG;
    return f;
}

// 将 session 加到 RPC 待确认链表尾部
static void wss_pending_enqueue_rpc(wss_session_t *s) {
    s->rpc_pending_next = (wss_session_t*)(void*)-1;
    if (g_wss_rpc_pending_rear) {
        g_wss_rpc_pending_rear->rpc_pending_next = s;
        g_wss_rpc_pending_rear = s;
    } else {
        g_wss_rpc_pending_head = s;
        g_wss_rpc_pending_rear = s;
    }
}

// 将 session 从 RPC 待确认链表移除
static void wss_pending_remove_rpc(wss_session_t *s) {
    if (!g_wss_rpc_pending_head || !s->rpc_pending_next) return;

    if (g_wss_rpc_pending_head == s) {
        g_wss_rpc_pending_head = s->rpc_pending_next;
        s->rpc_pending_next = NULL;
        if (g_wss_rpc_pending_head == (void*)-1) {
            g_wss_rpc_pending_head = NULL;
            g_wss_rpc_pending_rear = NULL;
        }
        return;
    }

    wss_session_t *prev = g_wss_rpc_pending_head;
    while (prev->rpc_pending_next != s) {
        assert(prev->rpc_pending_next != (void*)-1);
        prev = prev->rpc_pending_next;
    }
    prev->rpc_pending_next = s->rpc_pending_next;
    if (s->rpc_pending_next == (void*)-1) {
        g_wss_rpc_pending_rear = prev;
    }
    s->rpc_pending_next = NULL;
}

//-----------------------------------------------------------------------------

// 发送 SYNC0 应答（可选携带对端预缓存负载，从 ring buffer copy-out）
// 格式：SYNC0 <peer_id> <session_id> online[\n<payload>] | SYNC0 <peer_id> <session_id> offline
static void wss_send_sync0(wss_client_t *c, const char *peer_id, uint32_t session_id,
                           bool online, const wss_session_t *cache) {
    size_t cached_len = cache ? cache->sync_len : 0;
    if (cached_len) {
        size_t hdr = 8 + P2P_PEER_ID_MAX + 12 + 8;
        char *buf = malloc(hdr + 1 + cached_len + 1);
        if (buf) {
            int n = snprintf(buf, hdr, "SYNC0 %s %u online\n", peer_id, session_id);
            wss_sync_copy_out(cache, buf + n);
            buf[n + cached_len] = '\0';
            ws_send_text((ws_client_t*)c, buf);
            free(buf);
        }
    } else {
        char buf[8 + P2P_PEER_ID_MAX + 12 + 8];
        snprintf(buf, sizeof(buf), "SYNC0 %s %u %s",
                 peer_id, session_id, online ? "online" : "offline");
        ws_send_text((ws_client_t*)c, buf);
    }
}

// 发送 SYNC0 confirm 通知：告知发送方预缓存负载已转发至对端
// 格式：SYNC0 <peer_id> <session_id> confirm <bytes>
static void wss_send_sync0_confirm(wss_client_t *c, const char *peer_id, uint32_t session_id,
                                   int confirmed) {
    char buf[8 + P2P_PEER_ID_MAX + 12 + 16 + 12];
    snprintf(buf, sizeof(buf), "SYNC0 %s %u confirm %d", peer_id, session_id, confirmed);
    ws_send_text((ws_client_t*)c, buf);
}

// 服务器生成 RPC 错误 RESP（二进制帧）
// 格式：[P2P_WSS_BIN_RESP][session_id(4)][sid(2)][code(1)]
static void wss_session_send_rpc_code(wss_session_t *s, uint16_t sid, uint8_t code) {
    wss_client_t *c = (wss_client_t*)s->base.client;
    if (!wss_client_online(c)) return;

    uint8_t buf[P2P_WSS_BIN_RSP_MIN];
    buf[0] = P2P_WSS_BIN_RSP;
    nwrite_l(buf + 1, s->base.session_id);
    nwrite_s(buf + 1 + P2P_SESS_ID_PSZ, sid);
    buf[1 + P2P_SESS_ID_PSZ + 2] = code;
    ws_send_data((ws_client_t*)c, buf, sizeof(buf));
}

//-----------------------------------------------------------------------------

// 释放会话：通知对端 FIN + 清理 RPC + 调用共享 free_session_base
static void wss_free_session(session_t *s) {
    if (!s) return;

    wss_session_t *ws_s = (wss_session_t*)s;
    ws_s->sync_head = ws_s->sync_len = 0;

    // 清理本端 RPC 待确认状态
    if (ws_s->rpc_pending_sid) {
        wss_pending_remove_rpc(ws_s);
        ws_s->rpc_pending_sid = 0;
    }

    if (PEER_ONLINE(s)) {
        wss_session_t *peer_s = (wss_session_t*)s->peer;
        wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;

        // 对端有等待本端的 RESP → 返回 peer_offline 错误
        if (peer_s->rpc_pending_sid) {
            wss_session_send_rpc_code(peer_s, peer_s->rpc_pending_sid, P2P_MSG_ERR_PEER_OFFLINE);
            wss_pending_remove_rpc(peer_s);
            peer_s->rpc_pending_sid = 0;
        }

        if (wss_client_online(peer_c)) {
            char buf[16];
            snprintf(buf, sizeof(buf), "FIN %u", peer_s->base.session_id);
            ws_send_text((ws_client_t*)peer_c, buf);
        }

        peer_s->base.peer = (session_t*)(void*)-1;     /* 标记对端伙伴已断开 */
    }

    free_session_base(s);
}

///////////////////////////////////////////////////////////////////////////////

void
wss_init(void) {
    // WSS 模块无需预分配静态资源
}

bool
wss_init_client(wss_client_t *c) {
    // 调用方（server.c）已负责设置 proto / fd / ws_handshake / buf / len / pos
    // 此处仅初始化 WSS 特有的应用层状态
    (void)c;
    return true;
}

// 使 client 失效
// do_free=false: 标记离线（等重连）+ 通知对端，有会话则保留
// do_free=true:  硬销毁（清除所有会话 + 移除注册 + 释放）
void
wss_term_client(wss_client_t *client, bool do_free) {

    if (!do_free) {
        client->base.last_active = P_tick_ms();

        // 通知所有配对对端
        for (session_t *s = client->base.sessions; s; s = s->next) {
            if (!PEER_ONLINE(s)) continue;

            wss_session_t *peer_s = (wss_session_t*)s->peer;
            wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
            if (wss_client_online(peer_c)) {
                char buf[16];
                snprintf(buf, sizeof(buf), "FIN %u", peer_s->base.session_id);
                ws_send_text((ws_client_t*)peer_c, buf);
            }
        }

        if (client->base.sessions) return;     /* 有会话，保留等重连 */
    }

    free_client(&client->base);
}

//-----------------------------------------------------------------------------

// 处理 SYNC0 消息：创建/恢复会话
static void wss_handle_sync0(wss_client_t *client, const char *remote_peer_id, const char *payload) {
    const char *PROTO = "SYNC0";

    if (!remote_peer_id || remote_peer_id[0] == '\0') {
        ws_send_text((ws_client_t*)client, "SYNC0 FAIL empty peer_id");
        return;
    }

    size_t pay_len = (payload && payload[0]) ? strlen(payload) : 0;
    if (pay_len > WSS_SYNC_PAYLOAD_MAX) {
        ws_send_text((ws_client_t*)client, "SYNC0 FAIL payload too large");
        return;
    }

    wss_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, remote_peer_id,
                              (session_t**)&local_s, (session_t**)&remote_s,
                              sizeof(wss_session_t));

    if (side == E_OUT_OF_MEMORY) {
        print("E:", LA_F("%s: OOM building session '%s' -> '%s'\n", LA_F37, 37),
              PROTO, client->base.local_peer_id, remote_peer_id);
        ws_send_text((ws_client_t*)client, "SYNC0 FAIL OOM");
        return;
    }

    // 重复 SYNC0：幂等返回已有会话状态（WSS 特有）
    if (side == E_DUPLICATE) {
        for (session_t *s = client->base.sessions; s; s = s->next) {
            session_pair_t *pair = s->pair;
            if (!pair) continue;
            int my_side = (pair->sessions[0] == s) ? 0 : 1;
            if (strcmp(pair->peer_id[1 - my_side], remote_peer_id) != 0) continue;

            wss_session_t *ws_s = (wss_session_t*)s;

            // 对端标记已死亡，尝试重新配对
            if (ws_s->base.peer == (session_t*)(void*)-1) {
                ws_s->base.peer = NULL;
                session_t *other = pair->sessions[1 - my_side];
                if (other) {
                    wss_session_t *other_ws = (wss_session_t*)other;
                    if (!other_ws->base.peer || other_ws->base.peer == (session_t*)(void*)-1) {
                        ws_s->base.peer = &other_ws->base;
                        other_ws->base.peer = &ws_s->base;
                        print("I:", LA_F("%s: re-paired '%s' <-> '%s'\n", LA_F48, 48),
                              PROTO, client->base.local_peer_id, remote_peer_id);
                    }
                }
            }

            bool payload_busy = (wss_sync_cat(ws_s, payload) != 0);

            bool peer_online = PEER_ONLINE(s)
                && wss_client_online((wss_client_t*)ws_s->base.peer->client);

            if (peer_online) {
                wss_session_t *peer_s = (wss_session_t*)ws_s->base.peer;
                wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;

                int local_confirmed = (int)ws_s->sync_len;
                int peer_confirmed  = (int)peer_s->sync_len;

                wss_send_sync0(client, remote_peer_id, s->session_id, true, peer_s);
                peer_s->sync_head = peer_s->sync_len = 0;

                wss_send_sync0(peer_c, client->base.local_peer_id, peer_s->base.session_id, true, ws_s);
                ws_s->sync_head = ws_s->sync_len = 0;

                if (local_confirmed)
                    wss_send_sync0_confirm(client, remote_peer_id, s->session_id, local_confirmed);
                if (peer_confirmed)
                    wss_send_sync0_confirm(peer_c, client->base.local_peer_id, peer_s->base.session_id, peer_confirmed);
            } else {
                wss_send_sync0(client, remote_peer_id, s->session_id, false, NULL);
            }

            if (payload_busy) {
                char busy[8 + P2P_PEER_ID_MAX + 12 + 8];
                snprintf(busy, sizeof(busy), "SYNC0 %s %u busy", remote_peer_id, s->session_id);
                ws_send_text((ws_client_t*)client, busy);
            }
            return;
        }
        return;
    }

    if (side < E_NONE) {
        print("E:", LA_F("%s: build session failed '%s' -> '%s' (err=%d)\n", LA_F44, 44),
              PROTO, client->base.local_peer_id, remote_peer_id, side);
        ws_send_text((ws_client_t*)client, "SYNC0 FAIL internal");
        return;
    }

    print("V:", LA_F("%s: local='%s', remote='%s', side=%d, peer_online=%d\n", LA_F55, 55),
          PROTO, client->base.local_peer_id, remote_peer_id, side, remote_s ? 1 : 0);

    if (pay_len) wss_sync_write(local_s, payload, pay_len);

    if (remote_s) {
        if (!local_s->base.peer) local_s->base.peer = &remote_s->base;
        if (!remote_s->base.peer || remote_s->base.peer == (session_t*)(void*)-1)
            remote_s->base.peer = &local_s->base;

        wss_client_t *remote_c = (wss_client_t*)remote_s->base.client;

        if (wss_client_online(remote_c)) {
            int local_confirmed = (int)local_s->sync_len;
            int peer_confirmed  = (int)remote_s->sync_len;

            wss_send_sync0(client, remote_peer_id, local_s->base.session_id, true, remote_s);
            remote_s->sync_head = remote_s->sync_len = 0;

            wss_send_sync0(remote_c, client->base.local_peer_id, remote_s->base.session_id, true, local_s);
            local_s->sync_head = local_s->sync_len = 0;

            if (local_confirmed)
                wss_send_sync0_confirm(client, remote_peer_id, local_s->base.session_id, local_confirmed);
            if (peer_confirmed)
                wss_send_sync0_confirm(remote_c, client->base.local_peer_id, remote_s->base.session_id, peer_confirmed);

            print("I:", LA_F("%s: '%s' <-> '%s' paired (ses=%u/%u)\n", LA_F48, 48),
                  PROTO, client->base.local_peer_id, remote_peer_id,
                  local_s->base.session_id, remote_s->base.session_id);
        } else {
            wss_send_sync0(client, remote_peer_id, local_s->base.session_id, false, NULL);
            print("I:", LA_F("%s: '%s' -> '%s' created (id=%u, peer_zombie)\n", LA_F63, 63),
                  PROTO, client->base.local_peer_id, remote_peer_id, local_s->base.session_id);
        }
    } else {
        wss_send_sync0(client, remote_peer_id, local_s->base.session_id, false, NULL);
        print("I:", LA_F("%s: '%s' -> '%s' created (id=%u, peer_absent)\n", LA_F63, 63),
              PROTO, client->base.local_peer_id, remote_peer_id, local_s->base.session_id);
    }
}

// 处理 SYNC 消息：按 session_id 路由转发
static void wss_handle_sync(wss_session_t *ws_s, const char *payload) {
    const char *PROTO = "SYNC";

    wss_client_t *client = (wss_client_t*)ws_s->base.client;
    uint32_t session_id = ws_s->base.session_id;

    size_t pay_len = payload ? strlen(payload) : 0;
    if (!pay_len) return;

    wss_session_t *peer_s = (wss_session_t*)ws_s->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (wss_client_online(peer_c)) {
        size_t buf_len = 5 + 12 + 1 + pay_len;
        char *msg = (char*)malloc(buf_len + 1);
        if (msg) {
            int n = snprintf(msg, buf_len + 1, "SYNC %u\n", peer_s->base.session_id);
            memcpy(msg + n, payload, pay_len + 1);
            ws_send_text((ws_client_t*)peer_c, msg);
            free(msg);
        }
        char confirm[48];
        snprintf(confirm, sizeof(confirm), "SYNC %u confirm %d", session_id, (int)pay_len);
        ws_send_text((ws_client_t*)client, confirm);

        print("V:", LA_F("%s: sid=%u -> peer_sid=%u (%zu bytes)\n", LA_F35, 35),
              PROTO, session_id, peer_s->base.session_id, pay_len);
        return;
    }

    if (wss_sync_cat(ws_s, payload) != 0) {
        char busy[48];
        snprintf(busy, sizeof(busy), "SYNC %u busy", session_id);
        ws_send_text((ws_client_t*)client, busy);
        return;
    }

    print("V:", LA_F("%s: sid=%u cached (%zu bytes, peer offline)\n", LA_F34, 34),
          PROTO, session_id, pay_len);
}

// 处理 FIN 消息：客户端主动断开会话
static void wss_handle_fin(wss_session_t *ws_s) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: '%s' ses_id=%u\n", LA_F45, 45),
          PROTO, ws_s->base.client->local_peer_id, ws_s->base.session_id);

    wss_free_session(&ws_s->base);
}

// 处理 PACKET — P2P 数据包中继（重写 session_id，透传 payload）
static void wss_handle_packet(wss_session_t *ws_s, const uint8_t *data, size_t len) {
    const char *PROTO = "PACKET";

    if (len < P2P_WSS_BIN_PKT_MIN) {
        print("E:", LA_F("%s: bad frame len=%zu\n", LA_F41, 41), PROTO, len);
        return;
    }

    wss_session_t *peer_s = (wss_session_t*)ws_s->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) return;

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u, data_len=%zu\n", LA_F34, 34),
          PROTO, ws_s->base.session_id, peer_s->base.session_id, len - P2P_WSS_BIN_HDR_SIZE);

    // todo: 优化避免 malloc
    uint8_t *fwd = (uint8_t*)malloc(len);
    if (!fwd) return;
    memcpy(fwd, data, len);
    nwrite_l(fwd + 1, peer_s->base.session_id);
    ws_send_data((ws_client_t*)peer_c, fwd, len);
    free(fwd);
}

// 处理 REQ — RPC 请求转发
static void wss_handle_req(wss_session_t *ws_s, const uint8_t *data, size_t len) {
    const char *PROTO = "MSG_REQ";

    if (len < P2P_WSS_BIN_REQ_MIN) {
        print("E:", LA_F("%s: bad frame len=%zu\n", LA_F41, 41), PROTO, len);
        return;
    }

    uint16_t sid = nget_s(data + 1 + P2P_SESS_ID_PSZ);
    uint8_t  msg = data[1 + P2P_SESS_ID_PSZ + 2];
    int data_len = (int)len - (int)P2P_WSS_BIN_REQ_MIN;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, ws_s->base.client->local_peer_id, sid, msg, data_len);

    wss_session_t *peer_s = (wss_session_t*)ws_s->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) {
        print("W:", LA_F("%s: peer offline, sending error resp\n", LA_F64, 64), PROTO);
        wss_session_send_rpc_code(ws_s, sid, P2P_MSG_ERR_PEER_OFFLINE);
        return;
    }

    if (ws_s->rpc_pending_sid) {
        print("W:", LA_F("%s: rpc busy (pending sid=%u)\n", LA_F67, 67), PROTO, ws_s->rpc_pending_sid);
        wss_session_send_rpc_code(ws_s, sid, P2P_MSG_ERR_TIMEOUT);
        return;
    }

    // todo: 优化避免 malloc
    uint8_t *fwd = (uint8_t*)malloc(len);
    if (!fwd) return;
    memcpy(fwd, data, len);
    nwrite_l(fwd + 1, peer_s->base.session_id);
    ws_send_data((ws_client_t*)peer_c, fwd, len);
    free(fwd);

    ws_s->rpc_pending_sid = sid;
    ws_s->rpc_sent_time   = P_tick_ms();
    wss_pending_enqueue_rpc(ws_s);
}

// 处理 RESP — RPC 响应转发
static void wss_handle_resp(wss_session_t *ws_s, const uint8_t *data, size_t len) {
    const char *PROTO = "RSP";

    if (len < P2P_WSS_BIN_RSP_MIN) {
        print("E:", LA_F("%s: bad frame len=%zu\n", LA_F41, 41), PROTO, len);
        return;
    }

    uint16_t sid  = nget_s(data + 1 + P2P_SESS_ID_PSZ);
    uint8_t  code = data[1 + P2P_SESS_ID_PSZ + 2];
    int data_len  = (int)len - (int)P2P_WSS_BIN_RSP_MIN;

    print("V:", LA_F("%s: '%s' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, ws_s->base.client->local_peer_id, sid, code, data_len);

    wss_session_t *peer_s = (wss_session_t*)ws_s->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) {
        print("W:", LA_F("%s: requester offline, discarding\n", LA_F66, 66), PROTO);
        return;
    }

    if (peer_s->rpc_pending_sid != sid) {
        print("W:", LA_F("%s: sid mismatch (got=%u, pending=%u), discarding\n", LA_F68, 68),
              PROTO, sid, peer_s->rpc_pending_sid);
        return;
    }

    // todo: 优化避免 malloc
    uint8_t *fwd = (uint8_t*)malloc(len);
    if (!fwd) return;
    memcpy(fwd, data, len);
    nwrite_l(fwd + 1, peer_s->base.session_id);
    ws_send_data((ws_client_t*)peer_c, fwd, len);
    free(fwd);

    wss_pending_remove_rpc(peer_s);
    peer_s->rpc_pending_sid = 0;
}

//-----------------------------------------------------------------------------

// 处理 WSS 模式信令（WebSocket 文本帧）
// !! wslay 并未确保 msg 以 \0 结尾
void wss_on_message(wss_client_t *client, const uint8_t *msg, size_t len) {
    assert(client->base.proto == PROTO_WSS);

    if (len == 0) return;

    client->base.last_active = P_tick_ms();

    /* 调用方已将末尾 '\n' 替换为 '\0'（见 ws_cb_msg），直接按 C 字符串处理 */
    char *m = (char*)msg;

    // PROTO: REG <peer_id> <instance_id>
    if (strncmp(m, P2P_WSS_CMD_REG, P2P_WSS_CMD_REG_SZ) == 0) { const char *PROTO = "REG";

        char *peer_id = m + P2P_WSS_CMD_REG_SZ;

        char *inst_id = strrchr(peer_id, ' ');
        if (!inst_id || inst_id == peer_id) {
            print("E:", LA_F("%s: invalid REG format\n", 0, 0), PROTO);
            ws_send_text((ws_client_t*)client, "REG FAIL invalid instance_id");
            goto error_proto;
            return;
        }

        *inst_id++ = '\0';

        if (!peer_id[0]) {
            ws_send_text((ws_client_t*)client, "REG FAIL empty peer_id");
            return;
        }
        uint32_t instance_id = (uint32_t)strtoul(inst_id, NULL, 10);
        if (instance_id == 0) {
            ws_send_text((ws_client_t*)client, "REG FAIL invalid instance_id");
            return;
        }

        wss_client_t *reg = (wss_client_t*)find_client(peer_id);
        if (reg) { assert(reg != client);

            if (resident_client(&reg->base, PROTO_WSS, instance_id, &client->base)) {
                client = reg;
                print("I:", LA_F("%s: '%s' reconnected & reactive (inst=%u)\n", LA_F98, 98),
                      PROTO, client->base.local_peer_id, client->base.instance_id);
            } else {
                print("I:", LA_F("%s: '%s' reconnected & renew (inst=%u)\n", 0, 0),
                      PROTO, reg->base.local_peer_id, instance_id);
            }
        } else {
            assert(!client_identified(&client->base));
            assert(!client->base.local_peer_id[0]);

            size_t peer_len = strnlen(peer_id, P2P_PEER_ID_MAX);
            memcpy(client->base.local_peer_id, peer_id, peer_len);
            client->base.local_peer_id[peer_len] = '\0';
            client->base.instance_id = instance_id;

            print("I:", LA_F("%s: '%s' new REG (inst=%u)\n", LA_F93, 93),
                  PROTO, peer_id, instance_id);

            identify_client(&client->base);
        }

        { char ok[48]; snprintf(ok, sizeof(ok), P2P_WSS_RSP_REG_OK_FMT, (unsigned)WSS_SYNC_PAYLOAD_MAX, (unsigned)wss_features());
          ws_send_text((ws_client_t*)client, ok); }

        // 通知已配对会话双方 + 转发预缓存负载（重连场景）
        for (session_t *s = client->base.sessions; s; s = s->next) {
            wss_session_t *ws_s = (wss_session_t*)s;
            if (!PEER_ONLINE(s)) continue;

            wss_session_t *peer_s = (wss_session_t*)s->peer;
            wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
            if (!wss_client_online(peer_c)) continue;

            int ws_confirmed   = (int)ws_s->sync_len;
            int peer_confirmed = (int)peer_s->sync_len;

            wss_send_sync0(peer_c, client->base.local_peer_id, peer_s->base.session_id, true, ws_s);
            ws_s->sync_head = ws_s->sync_len = 0;

            wss_send_sync0(client, peer_c->base.local_peer_id, s->session_id, true, peer_s);
            peer_s->sync_head = peer_s->sync_len = 0;

            if (ws_confirmed)
                wss_send_sync0_confirm(client, peer_c->base.local_peer_id, s->session_id, ws_confirmed);
            if (peer_confirmed)
                wss_send_sync0_confirm(peer_c, client->base.local_peer_id, peer_s->base.session_id, peer_confirmed);
        }
        return;
    }

    // PROTO: OFF
    // + 主动下线，立即释放服务器资源
    if (strncmp(m, P2P_WSS_CMD_OFF, P2P_WSS_CMD_OFF_SZ) == 0 && m[P2P_WSS_CMD_OFF_SZ] == '\0') {
        if (client->base.local_peer_id[0]) {
            print("I:", LA_F("OFF: '%s' (fd=%d)\n", LA_F72, 72),
                  client->base.local_peer_id, client->base.fd);
            wss_term_client(client, true);
        }
        return;
    }

    if (!client->base.local_peer_id[0]) {
        ws_send_text((ws_client_t*)client, "REG FAIL not registered\n");
        return;
    }

    // SYNC0 <remote_peer_id>[\n<payload>]
    if (strncmp(m, P2P_WSS_CMD_SYNC0, P2P_WSS_CMD_SYNC0_SZ) == 0) {
        char *remote = m + P2P_WSS_CMD_SYNC0_SZ;
        char *nl = strchr(remote, '\n');
        const char *payload = "";
        if (nl) { *nl = '\0'; payload = nl + 1; }
        wss_handle_sync0(client, remote, payload);
        return;
    }

    // SYNC <session_id>[\n<payload>]
    if (strncmp(m, P2P_WSS_CMD_SYNC, P2P_WSS_CMD_SYNC_SZ) == 0) {
        char *sid_str = m + P2P_WSS_CMD_SYNC_SZ;
        char *nl = strchr(sid_str, '\n');
        const char *payload = "";
        if (nl) { *nl = '\0'; payload = nl + 1; }
        uint32_t session_id = (uint32_t)strtoul(sid_str, NULL, 10);

        wss_session_t *s = (wss_session_t*)find_session(session_id);
        if (!s || s->base.client != &client->base) {
            print("W:", LA_F("SYNC: unknown ses_id=%u from '%s'\n", LA_F148, 148),
                  session_id, client->base.local_peer_id);
            return;
        }
        if (!PEER_ONLINE(&s->base)) {
            print("W:", LA_F("SYNC: ses_id=%u peer not connected\n", LA_F146, 146), session_id);
            return;
        }
        wss_handle_sync(s, payload);
        return;
    }

    // FIN <session_id>
    if (strncmp(m, P2P_WSS_CMD_FIN, P2P_WSS_CMD_FIN_SZ) == 0) {
        uint32_t sid = (uint32_t)strtoul(m + P2P_WSS_CMD_FIN_SZ, NULL, 10);

        wss_session_t *s = (wss_session_t*)find_session(sid);
        if (!s || s->base.client != &client->base) {
            print("W:", LA_F("FIN: unknown ses_id=%u from '%s'\n", LA_F148, 148),
                  sid, client->base.local_peer_id);
            return;
        }
        wss_handle_fin(s);
        return;
    }

error_proto:
    print("V:", LA_F("unknown msg from '%s': %.32s\n", LA_F72, 72),
          client->base.local_peer_id, m);
}

// 处理 WSS 模式信令（WebSocket 二进制帧）— PACKET 中继 + MSG RPC
void wss_on_data(wss_client_t *client, const uint8_t *data, size_t len) {
    assert(client && client->base.proto == PROTO_WSS);

    if (len < P2P_WSS_BIN_HDR_SIZE) return;

    client->base.last_active = P_tick_ms();

    uint8_t  type       = data[0];
    uint32_t session_id = nget_l(data + 1);

    wss_session_t *ws_s = (wss_session_t*)find_session(session_id);
    if (!ws_s || ws_s->base.client != &client->base) {
        print("W:", LA_F("BIN: unknown ses_id=%u type=0x%02x from '%s'\n", LA_F148, 148),
              session_id, type, client->base.local_peer_id);
        return;
    }

    if (!PEER_ONLINE(&ws_s->base)) {
        print("W:", LA_F("BIN 0x%02x: ses_id=%u peer not connected\n", LA_F146, 146), type, session_id);
        if (type == P2P_WSS_BIN_REQ && len >= P2P_WSS_BIN_REQ_MIN) {
            uint16_t sid = nget_s(data + 1 + P2P_SESS_ID_PSZ);
            wss_session_send_rpc_code(ws_s, sid, P2P_MSG_ERR_PEER_OFFLINE);
        }
        return;
    }

    switch (type) {
    case P2P_WSS_BIN_PKT: wss_handle_packet(ws_s, data, len); break;
    case P2P_WSS_BIN_REQ:    wss_handle_req(ws_s, data, len);    break;
    case P2P_WSS_BIN_RSP:    wss_handle_resp(ws_s, data, len);   break;
    default:
        print("W:", LA_F("BIN: unknown type=0x%02x from '%s'\n", LA_F149, 149),
              type, client->base.local_peer_id);
        break;
    }
}

//-----------------------------------------------------------------------------

// 检查 WSS RPC 超时（队列按时间排序，未超时即短路返回）
void retry_wss_pending(uint64_t now) {

    while (g_wss_rpc_pending_head) { wss_session_t *s = g_wss_rpc_pending_head;

        if (tick_diff(now, s->rpc_sent_time) < REQ_MAX_RETRY * RPC_RETRY_INTERVAL_MS) return;

        g_wss_rpc_pending_head = s->rpc_pending_next;
        if (g_wss_rpc_pending_head == (void*)-1) {
            g_wss_rpc_pending_head = g_wss_rpc_pending_rear = NULL;
        }
        s->rpc_pending_next = NULL;

        uint16_t sid = s->rpc_pending_sid;
        s->rpc_pending_sid = 0;

        print("W:", LA_F("[W] RPC timeout: sid=%u (ses_id=%u)\n", 0, 0), sid, s->base.session_id);
        wss_session_send_rpc_code(s, sid, P2P_MSG_ERR_TIMEOUT);
    }
}

///////////////////////////////////////////////////////////////////////////////
