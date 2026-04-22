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

/* WS ICE RPC 超时链表（按 rpc_sent_time 排序，队头最早） */
static wss_session_t*            g_wss_rpc_pending_head = NULL;
static wss_session_t*            g_wss_rpc_pending_rear = NULL;

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

/* 判断 client 是否在线（已注册且 WS 连接有效） */
static inline bool wss_client_online(const wss_client_t *c) {
    return c && c->base.valid && c->cid != (ws_client_id_t)-1;
}

/* 生成 REG OK 的 features 位掩码（与 P2P_RLY_FEATURE_* 一致） */
static inline int wss_features(void) {
    int f = 0;
    if (ARGS_relay.i64) f |= P2P_RLY_FEATURE_RELAY;
    if (ARGS_msg.i64)   f |= P2P_RLY_FEATURE_MSG;
    return f;
}

/* RPC 超时链表操作（与 relay_pending_enqueue/remove_rpc 同构） */
static void wss_pending_rpc_enqueue(wss_session_t *s) {

    s->rpc_pending_next = (wss_session_t *)(void *)-1;
    if (g_wss_rpc_pending_rear) {
        g_wss_rpc_pending_rear->rpc_pending_next = s;
        g_wss_rpc_pending_rear = s;
    } else {
        g_wss_rpc_pending_head = s;
        g_wss_rpc_pending_rear = s;
    }
}

static void wss_pending_rpc_remove(wss_session_t *s) {
    if (!g_wss_rpc_pending_head || !s->rpc_pending_next) return;

    if (g_wss_rpc_pending_head == s) {
        g_wss_rpc_pending_head = s->rpc_pending_next;
        s->rpc_pending_next = NULL;
        if (g_wss_rpc_pending_head == (void *)-1) {
            g_wss_rpc_pending_head = NULL;
            g_wss_rpc_pending_rear = NULL;
        }
        return;
    }

    wss_session_t *prev = g_wss_rpc_pending_head;
    while (prev->rpc_pending_next != s) {
        if (prev->rpc_pending_next == (void *)-1) return;
        prev = prev->rpc_pending_next;
    }
    prev->rpc_pending_next = s->rpc_pending_next;
    if (s->rpc_pending_next == (void *)-1)
        g_wss_rpc_pending_rear = prev;
    s->rpc_pending_next = NULL;
}

//-----------------------------------------------------------------------------

/* 生成 SYNC0 应答（可选携带对端预缓存负载，从 ring buffer copy-out） */
static void wss_send_sync0(ws_client_id_t cid, const char *peer_id, uint32_t session_id,
                              bool online, const wss_session_t *cache) {
    size_t cached_len = cache ? cache->sync_len : 0;
    if (cached_len) {
        /* SYNC0 <peer_id> <session_id> online\n<payload> */
        size_t hdr = 8 + P2P_PEER_ID_MAX + 12 + 8;
        char *buf = malloc(hdr + 1 + cached_len + 1);
        if (buf) {
            int n = snprintf(buf, hdr, "SYNC0 %s %u online\n", peer_id, session_id);
            wss_sync_copy_out(cache, buf + n);
            buf[n + cached_len] = '\0';
            ws_send_text(cid, buf);
            free(buf);
        }
    } else {
        char buf[8 + P2P_PEER_ID_MAX + 12 + 8];
        snprintf(buf, sizeof(buf), "SYNC0 %s %u %s",
                 peer_id, session_id, online ? "online" : "offline");
        ws_send_text(cid, buf);
    }
}

/* 发送 SYNC0 confirm 通知：告知发送方预缓存负载已转发至对端 */
static void wss_send_sync0_confirm(ws_client_id_t cid, const char *peer_id, uint32_t session_id, int confirmed) {
    char buf[8 + P2P_PEER_ID_MAX + 12 + 16 + 12];
    snprintf(buf, sizeof(buf), "SYNC0 %s %u confirm %d",
             peer_id, session_id, confirmed);
    ws_send_text(cid, buf);
}

/* 服务器生成 RPC 错误 RESP（二进制帧） */
static void wss_send_rpc_code(wss_session_t *s, uint16_t sid, uint8_t code) {
    wss_client_t *c = (wss_client_t *)s->base.client;
    if (!wss_client_online(c)) return;

    uint8_t buf[P2P_WSS_BIN_RESP_MIN];
    buf[0] = P2P_WSS_BIN_RESP;
    nwrite_l(buf + 1, s->base.session_id);
    nwrite_s(buf + 1 + P2P_SESS_ID_PSZ, sid);
    buf[1 + P2P_SESS_ID_PSZ + 2] = code;
    ws_send_data(c->cid, buf, sizeof(buf));
}

//-----------------------------------------------------------------------------

/* WS ICE RPC 超时检查（与 retry_relay_rpc_pending 同构） */
void retry_wss_pending(uint64_t now) {

    while (g_wss_rpc_pending_head) {
        wss_session_t *s = g_wss_rpc_pending_head;

        if (tick_diff(now, s->rpc_sent_time) < MSG_REQ_MAX_RETRY * MSG_RPC_RETRY_INTERVAL_MS) return;

        /* 移除队头 */
        g_wss_rpc_pending_head = s->rpc_pending_next;
        if (g_wss_rpc_pending_head == (void *)-1) {
            g_wss_rpc_pending_head = NULL;
            g_wss_rpc_pending_rear = NULL;
        }
        s->rpc_pending_next = NULL;

        uint16_t sid = s->rpc_pending_sid;
        s->rpc_pending_sid = 0;

        print("W:", "[WSS] RPC timeout: sid=%u (ses_id=%u)\n", sid, s->base.session_id);
        wss_send_rpc_code(s, sid, P2P_MSG_ERR_TIMEOUT);
    }
}

/* 释放会话：通知对端 + 清理 RPC + 调用共享 free_session */
static void wss_free_session(wss_session_t *s) {
    if (!s) return;

    s->sync_head = s->sync_len = 0;

    /* 清理本端 RPC 待确认状态 */
    if (s->rpc_pending_sid) {
        wss_pending_rpc_remove(s);
        s->rpc_pending_sid = 0;
    }

    if (PEER_ONLINE(s)) {
        wss_session_t *peer_s = s->peer;
        wss_client_t  *peer_c = (wss_client_t *)peer_s->base.client;

        /* 对端有等待本端的 RESP → 返回 peer_offline 错误 */
        if (peer_s->rpc_pending_sid) {
            wss_send_rpc_code(peer_s, peer_s->rpc_pending_sid, P2P_MSG_ERR_PEER_OFFLINE);
            wss_pending_rpc_remove(peer_s);
            peer_s->rpc_pending_sid = 0;
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "FIN %u", peer_s->base.session_id);
        if (wss_client_online(peer_c))
            ws_send_text(peer_c->cid, buf);

        peer_s->peer = (wss_session_t *)(void *)-1;   /* 标记对端伙伴已断开 */
    }

    free_session_base(&s->base);
}

/* 使 client 失效
 * do_free=false: 标记离线（僵尸态）+ 通知对端，有会话则保留等重连
 * do_free=true:  硬销毁（清除所有会话 + 移除注册 + free）
 */
void wss_invalidate_client(wss_client_t *client, bool do_free) {

    client->cid = (ws_client_id_t)-1;

    if (!do_free) {
        client->base.last_active = P_tick_ms();

        /* 通知所有配对对端 */
        for (session_t *s = client->base.sessions; s; s = s->next) {
            wss_session_t *ws_s = (wss_session_t *)s;
            if (!PEER_ONLINE(ws_s)) continue;

            wss_client_t *peer_c = (wss_client_t *)ws_s->peer->base.client;
            char buf[16];
            snprintf(buf, sizeof(buf), "FIN %u", ws_s->peer->base.session_id);
            if (wss_client_online(peer_c))
                ws_send_text(peer_c->cid, buf);
        }

        if (client->base.sessions) return;  /* 有会话，保留等重连 */
    }

    HASH_DELETE(hh, g_wss_clients, client);
    free_client(&client->base, FREE_SESSION_BASE(wss_free_session));
    free(client);
}

//-----------------------------------------------------------------------------

/* 处理 SYNC0 消息：创建/恢复会话 */
static void wss_handle_sync0(wss_client_t *client, const char *remote_peer_id, const char *payload) {
    if (!remote_peer_id || remote_peer_id[0] == '\0') {
        ws_send_text(client->cid, "SYNC0 FAIL empty peer_id");
        return;
    }

    /* payload 长度预检 */
    size_t pay_len = (payload && payload[0]) ? strlen(payload) : 0;
    if (pay_len > WSS_SYNC_PAYLOAD_MAX) {
        ws_send_text(client->cid, "SYNC0 FAIL payload too large");
        return;
    }

    wss_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, remote_peer_id,
                               (session_t **)&local_s, (session_t **)&remote_s,
                               sizeof(wss_session_t));

    if (side == E_OUT_OF_MEMORY) {
        ws_send_text(client->cid, "SYNC0 FAIL OOM");
        return;
    }

    /* 重复 SYNC0：幂等返回已有会话状态（WSS 特有，relay 视为错误） */
    if (side == E_DUPLICATE) {
        for (session_t *s = client->base.sessions; s; s = s->next) {
            session_pair_t *pair = s->pair;
            if (!pair) continue;
            int my_side = (pair->sessions[0] == s) ? 0 : 1;
            if (strcmp(pair->peer_id[1 - my_side], remote_peer_id) != 0) continue;

            wss_session_t *ws_s = (wss_session_t *)s;

            /* 对端标记已死亡，尝试重新配对 */
            if (ws_s->peer == (wss_session_t *)(void *)-1) {
                ws_s->peer = NULL;
                session_t *other = pair->sessions[1 - my_side];
                if (other) {
                    wss_session_t *other_ws = (wss_session_t *)other;
                    if (!other_ws->peer || other_ws->peer == (wss_session_t *)(void *)-1) {
                        ws_s->peer = other_ws;
                        other_ws->peer = ws_s;
                        print("I:", "[WSS] re-paired '%s' <-> '%s'\n",
                              client->base.local_peer_id, remote_peer_id);
                    }
                }
            }

            /* 追加本端预缓存 */
            bool payload_busy = (wss_sync_cat(ws_s, payload) != 0);

            bool peer_online = PEER_ONLINE(ws_s)
                && wss_client_online((wss_client_t *)ws_s->peer->base.client);

            if (peer_online) {
                wss_client_t *peer_c = (wss_client_t *)ws_s->peer->base.client;
                wss_session_t *peer_s = ws_s->peer;

                /* 记录待确认大小（交换后指针转移/清空） */
                int local_confirmed  = (int)ws_s->sync_len;
                int peer_confirmed   = (int)peer_s->sync_len;

                /* 取出对端缓存发给本端 */
                wss_send_sync0(client->cid, remote_peer_id, s->session_id,
                                  true, peer_s);
                peer_s->sync_head = peer_s->sync_len = 0;

                /* 取出本端缓存发给对端 */
                wss_send_sync0(peer_c->cid, client->base.local_peer_id,
                                  peer_s->base.session_id,
                                  true, ws_s);
                ws_s->sync_head = ws_s->sync_len = 0;

                /* 发送 confirm（各方得知自己的缓存已被转发） */
                if (local_confirmed)
                    wss_send_sync0_confirm(client->cid, remote_peer_id,
                                              s->session_id, local_confirmed);
                if (peer_confirmed)
                    wss_send_sync0_confirm(peer_c->cid, client->base.local_peer_id,
                                              peer_s->base.session_id, peer_confirmed);
            } else {
                wss_send_sync0(client->cid, remote_peer_id, s->session_id,
                                  false, NULL);
            }

            /* 新增 payload 超出缓存上限 → busy */
            if (payload_busy) {
                char busy[8 + P2P_PEER_ID_MAX + 12 + 8];
                snprintf(busy, sizeof(busy), "SYNC0 %s %u busy",
                         remote_peer_id, s->session_id);
                ws_send_text(client->cid, busy);
            }
            return;
        }
        return;  /* E_DUPLICATE but session not found — shouldn't happen */
    }

    if (side < E_NONE) {
        print("E:", "[WSS] build_session failed: '%s' -> '%s' (err=%d)\n",
              client->base.local_peer_id, remote_peer_id, side);
        ws_send_text(client->cid, "SYNC0 FAIL internal");
        return;
    }

    print("V:", "[WSS] SYNC0: local='%s', remote='%s', side=%d, peer_online=%d\n",
          client->base.local_peer_id, remote_peer_id, side, remote_s ? 1 : 0);

    /* 写入本端 payload（新建 session，缓冲为空，直接 write） */
    if (pay_len) wss_sync_write(local_s, payload, pay_len);

    /* remote_s 非空 → 对端已注册会话（配对成功） */
    if (remote_s) {

        /* 建立双向引用 */
        if (!local_s->peer) local_s->peer = remote_s;
        if (!remote_s->peer || remote_s->peer == (wss_session_t *)(void *)-1)
            remote_s->peer = local_s;

        wss_client_t *remote_c = (wss_client_t *)remote_s->base.client;

        if (wss_client_online(remote_c)) {
            /* 双方在线：交换预缓存 */
            int local_confirmed  = (int)local_s->sync_len;
            int peer_confirmed   = (int)remote_s->sync_len;

            /* 取出对端缓存发给本端 */
            wss_send_sync0(client->cid, remote_peer_id,
                              local_s->base.session_id,
                              true, remote_s);
            remote_s->sync_head = remote_s->sync_len = 0;

            /* 取出本端缓存发给对端 */
            wss_send_sync0(remote_c->cid, client->base.local_peer_id,
                              remote_s->base.session_id,
                              true, local_s);
            local_s->sync_head = local_s->sync_len = 0;

            /* 发送 confirm */
            if (local_confirmed)
                wss_send_sync0_confirm(client->cid, remote_peer_id,
                                          local_s->base.session_id, local_confirmed);
            if (peer_confirmed)
                wss_send_sync0_confirm(remote_c->cid, client->base.local_peer_id,
                                          remote_s->base.session_id, peer_confirmed);

            print("I:", "[WSS] paired '%s' <-> '%s' (session=%u/%u)\n",
                  client->base.local_peer_id, remote_peer_id,
                  local_s->base.session_id, remote_s->base.session_id);
        } else {
            /* 对端有会话但 WS 已断开（僵尸态），通知本端离线 */
            wss_send_sync0(client->cid, remote_peer_id,
                              local_s->base.session_id, false, NULL);

            print("I:", "[WSS] session created: '%s' -> '%s' (id=%u, peer_zombie)\n",
                  client->base.local_peer_id, remote_peer_id, local_s->base.session_id);
        }
    }
    /* 对端未注册会话 */
    else {
        wss_send_sync0(client->cid, remote_peer_id,
                          local_s->base.session_id, false, NULL);

        print("I:", "[WSS] session created: '%s' -> '%s' (id=%u, peer_absent)\n",
              client->base.local_peer_id, remote_peer_id, local_s->base.session_id);
    }
}

/* 处理 SYNC 消息：按 session_id 路由转发（对齐 relay: 重写 session_id 后投递） */
static void wss_handle_sync(wss_session_t *ws_s, const char *payload) {

    wss_client_t *client = (wss_client_t *)ws_s->base.client;
    uint32_t session_id = ws_s->base.session_id;

    size_t pay_len = payload ? strlen(payload) : 0;
    if (!pay_len) return;

    /* SYNC 前提：session 已配对，只需检查对端 WS 连接是否有效 */
    wss_session_t *peer_s = ws_s->peer;
    wss_client_t *peer_c = (wss_client_t *)peer_s->base.client;
    if (wss_client_online(peer_c)) {
        /* 构造 "SYNC <peer_session_id>\n<payload>" */
        size_t buf_len = 5 + 12 + 1 + pay_len;
        char *msg = (char *)malloc(buf_len + 1);
        if (msg) {
            int n = snprintf(msg, buf_len + 1, "SYNC %u\n", peer_s->base.session_id);
            memcpy(msg + n, payload, pay_len + 1);
            ws_send_text(peer_c->cid, msg);
            free(msg);
        }
        /* confirm */
        char confirm[48];
        snprintf(confirm, sizeof(confirm), "SYNC %u confirm %d",
                 session_id, (int)pay_len);
        ws_send_text(client->cid, confirm);

        print("V:", "[WSS] SYNC forwarded: sid=%u -> peer_sid=%u (%zu bytes)\n",
              session_id, peer_s->base.session_id, pay_len);
        return;
    }

    /* 对端 WS 离线（僵尸态）→ 缓存 */
    if (wss_sync_cat(ws_s, payload) != 0) {
        char busy[48];
        snprintf(busy, sizeof(busy), "SYNC %u busy", session_id);
        ws_send_text(client->cid, busy);
        return;
    }

    print("V:", "[WSS] SYNC cached: sid=%u (%zu bytes, peer offline)\n",
          session_id, pay_len);
}

/* 处理 FIN 消息：客户端主动断开会话 */
static void wss_handle_fin(wss_session_t *ws_s) {

    print("I:", "[WSS] FIN from '%s' (session_id=%u)\n",
          ws_s->base.client->local_peer_id, ws_s->base.session_id);

    wss_free_session(ws_s);
}

/* 处理 PACKET — P2P 数据包中继（重写 session_id，透传 payload） */
static void wss_handle_packet(wss_session_t *ws_s, const uint8_t *data, size_t len) {

    if (len < P2P_WSS_BIN_PACKET_MIN) {
        print("E:", "[WSS] PACKET: bad frame len=%zu\n", len);
        return;
    }

    wss_session_t *peer_s = ws_s->peer;
    wss_client_t  *peer_c = (wss_client_t *)peer_s->base.client;
    if (!wss_client_online(peer_c)) return;

    /* 重写 session_id → 对端 session_id，转发整帧 */
    uint8_t *fwd = (uint8_t *)malloc(len);
    if (!fwd) return;
    memcpy(fwd, data, len);
    nwrite_l(fwd + 1, peer_s->base.session_id);
    ws_send_data(peer_c->cid, fwd, len);
    free(fwd);
}

/* 处理 REQ — RPC 请求转发 */
static void wss_handle_req(wss_session_t *ws_s, const uint8_t *data, size_t len) {

    if (len < P2P_WSS_BIN_REQ_MIN) {
        print("E:", "[WSS] REQ: bad frame len=%zu\n", len);
        return;
    }

    uint16_t sid = nget_s(data + 1 + P2P_SESS_ID_PSZ);
    uint8_t  msg = data[1 + P2P_SESS_ID_PSZ + 2];
    (void)msg;

    print("V:", "[WSS] REQ: '%s' sid=%u msg=%u len=%zu\n",
          ws_s->base.client->local_peer_id, sid, msg, len);

    /* 对端检查 */
    wss_session_t *peer_s = ws_s->peer;
    wss_client_t  *peer_c = (wss_client_t *)peer_s->base.client;
    if (!wss_client_online(peer_c)) {
        wss_send_rpc_code(ws_s, sid, P2P_MSG_ERR_PEER_OFFLINE);
        return;
    }

    /* RPC 忙检查 */
    if (ws_s->rpc_pending_sid) {
        print("W:", "[WSS] REQ: rpc busy (pending sid=%u)\n", ws_s->rpc_pending_sid);
        wss_send_rpc_code(ws_s, sid, P2P_MSG_ERR_TIMEOUT);  /* 客户端视同超时重试 */
        return;
    }

    /* 重写 session_id → 对端 session_id，转发 */
    // todo 优化避免分配内存
    uint8_t *fwd = (uint8_t *)malloc(len);
    if (!fwd) return;
    memcpy(fwd, data, len);
    nwrite_l(fwd + 1, peer_s->base.session_id);
    ws_send_data(peer_c->cid, fwd, len);
    free(fwd);

    /* 记录 pending（等 RESP 回来解锁） */
    ws_s->rpc_pending_sid  = sid;
    ws_s->rpc_sent_time    = P_tick_ms();
    wss_pending_rpc_enqueue(ws_s);
}

/* 处理 RESP — RPC 响应转发 */
static void wss_handle_resp(wss_session_t *ws_s, const uint8_t *data, size_t len) {
    if (len < P2P_WSS_BIN_RESP_MIN) {
        print("E:", "[WSS] RESP: bad frame len=%zu\n", len);
        return;
    }

    uint16_t sid  = nget_s(data + 1 + P2P_SESS_ID_PSZ);
    uint8_t  code = data[1 + P2P_SESS_ID_PSZ + 2];

    print("V:", "[WSS] RESP: '%s' sid=%u code=%u\n",
          ws_s->base.client->local_peer_id, sid, code);

    /* 请求方检查 */
    wss_session_t *peer_s = ws_s->peer;
    wss_client_t  *peer_c = (wss_client_t *)peer_s->base.client;
    if (!wss_client_online(peer_c)) return;

    /* sid 校验 */
    if (peer_s->rpc_pending_sid != sid) {
        print("W:", "[WSS] RESP: sid mismatch (got=%u, pending=%u)\n",
              sid, peer_s->rpc_pending_sid);
        return;
    }

    /* 重写 session_id → 请求方 session_id，转发 */
    // todo 优化避免分配内存
    uint8_t *fwd = (uint8_t *)malloc(len);
    if (!fwd) return;
    memcpy(fwd, data, len);
    nwrite_l(fwd + 1, peer_s->base.session_id);
    ws_send_data(peer_c->cid, fwd, len);
    free(fwd);

    /* 解锁 RPC pending */
    wss_pending_rpc_remove(peer_s);
    peer_s->rpc_pending_sid = 0;
}

//-----------------------------------------------------------------------------

/* WS on_message 回调 */
void wss_on_message(ws_server_t *srv, ws_client_id_t cid, char *msg, size_t len, void *user_data) {
    (void)user_data;
    if (len == 0) return;

    /* 查找当前 cid 对应的 client */
    wss_client_t *client = NULL;
    { wss_client_t *c, *tmp;
        HASH_ITER(hh, g_wss_clients, c, tmp) {
            if (c->cid == cid) { client = c; break; }
        }
    }

    if (strncmp(msg, "REG ", 4) == 0) {
        char *peer_id = msg + 4;
        size_t n = strlen(peer_id);
        while (n > 0 && (peer_id[n - 1] == '\n' || peer_id[n - 1] == '\r')) peer_id[--n] = '\0';

        // 解析 instance_id（最后一个空格分隔的十进制数）
        char *sp = strrchr(peer_id, ' ');
        if (!sp || sp == peer_id) {
            ws_server_send_text(srv, cid, "REG FAIL invalid instance_id");
            return;
        }
        *sp = '\0';
        uint32_t instance_id = (uint32_t)strtoul(sp + 1, NULL, 10);
        if (instance_id == 0) {
            ws_server_send_text(srv, cid, "REG FAIL invalid instance_id");
            return;
        }

        if (!peer_id[0]) {
            ws_server_send_text(srv, cid, "REG FAIL empty peer_id");
            return;
        }

        // 查找已有 client（按 peer_id）
        wss_client_t *by_name = NULL;
        HASH_FIND_STR(g_wss_clients, peer_id, by_name);

        if (by_name) {
            if (by_name->base.instance_id == instance_id) {
                if (by_name->cid == cid) {
                    /* 同一连接 + 同一实例 → 幂等 */
                    char ok[48]; snprintf(ok, sizeof(ok), "REG OK %d %d", WSS_SYNC_PAYLOAD_MAX, wss_features());
                    ws_server_send_text(srv, cid, ok);
                    return;
                }
                /* 同实例 + 不同 cid → 网络重连：保留会话 */
                if (wss_client_online(by_name)) {
                    print("W:", "[WSS] peer '%s' reconnect, kick old cid=%d\n",
                          peer_id, by_name->cid);
                    ws_server_disconnect(srv, by_name->cid, 1000);
                }

                by_name->cid = cid;
                by_name->base.valid = true;
                by_name->base.last_active = P_tick_ms();

                print("I:", "[WSS] peer '%s' reconnected (inst=%u, cid=%d)\n",
                      peer_id, instance_id, cid);
                { char ok[48]; snprintf(ok, sizeof(ok), "REG OK %d %d", WSS_SYNC_PAYLOAD_MAX, wss_features());
                  ws_server_send_text(srv, cid, ok); }

                /* 通知已配对会话的双方 + 转发预缓存负载 */
                for (session_t *s = by_name->base.sessions; s; s = s->next) {
                    wss_session_t *ws_s = (wss_session_t *)s;
                    if (!PEER_ONLINE(ws_s)) continue;

                    wss_session_t *peer_s = ws_s->peer;
                    wss_client_t  *peer_c = (wss_client_t *)peer_s->base.client;

                    if (wss_client_online(peer_c)) {

                        // 记录待确认大小（转发后清空）
                        int ws_confirmed   = (int)ws_s->sync_len;
                        int peer_confirmed = (int)peer_s->sync_len;

                        // 给对端：通知重连方上线 + 转发重连方缓存给对端
                        wss_send_sync0(peer_c->cid, by_name->base.local_peer_id, peer_s->base.session_id, true, ws_s);
                        ws_s->sync_head = ws_s->sync_len = 0;

                        // 给重连方：通知对端在线 + 转发对端缓存给重连方
                        wss_send_sync0(by_name->cid, peer_c->base.local_peer_id, s->session_id, true, peer_s);
                        peer_s->sync_head = peer_s->sync_len = 0;

                        // 发送 confirm
                        if (ws_confirmed)
                            wss_send_sync0_confirm(by_name->cid, peer_c->base.local_peer_id, s->session_id, ws_confirmed);
                        if (peer_confirmed)
                            wss_send_sync0_confirm(peer_c->cid, by_name->base.local_peer_id, peer_s->base.session_id, peer_confirmed);
                    }
                }
                return;
            }

            /* 不同 instance_id → 客户端重启：销毁旧 client 及所有会话 */
            print("I:", "[WSS] peer '%s' new instance (old=%u, new=%u), destroying old\n",
                  peer_id, by_name->base.instance_id, instance_id);
            wss_invalidate_client(by_name, true);
            by_name = NULL;
            /* 落入下方新建 client 逻辑 */
        }

        // 同一 cid 已有其他 peer_id？先移除
        if (client) {
            print("W:", "[WSS] cid=%d re-register as '%s' (was '%s'), clearing old\n",
                  cid, peer_id, client->base.local_peer_id);
            wss_invalidate_client(client, true);
        }

        // 新建 client
        wss_client_t *nc = (wss_client_t *)calloc(1, sizeof(*nc));
        if (!nc) {
            ws_server_send_text(srv, cid, "REG FAIL OOM");
            return;
        }
        nc->base.valid = true;
        strncpy(nc->base.local_peer_id, peer_id, P2P_PEER_ID_MAX - 1);
        nc->base.local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
        nc->base.instance_id = instance_id;
        nc->base.last_active = P_tick_ms();
        nc->cid = cid;
        HASH_ADD_KEYPTR(hh, g_wss_clients, nc->base.local_peer_id,
                        strlen(nc->base.local_peer_id), nc);

        print("I:", "[WSS] peer '%s' registered (inst=%u, cid=%d)\n", peer_id, instance_id, cid);
        { char ok[48]; snprintf(ok, sizeof(ok), "REG OK %d %d", WSS_SYNC_PAYLOAD_MAX, wss_features());
          ws_server_send_text(srv, cid, ok); }
        return;
    }

    // OFF — 主动下线，立即释放服务器资源
    if (strncmp(msg, "OFF", 3) == 0 && (msg[3] == '\0' || msg[3] == '\n' || msg[3] == '\r')) {
        if (client) {
            print("I:", "[WSS] peer '%s' OFF (cid=%d)\n",
                  client->base.local_peer_id, cid);
            wss_invalidate_client(client, true);
        }
        return;
    }

    if (!client) {
        ws_server_send_text(srv, cid, "REG FAIL not registered");
        return;
    }

    client->base.last_active = P_tick_ms();

    if (strncmp(msg, "SYNC0 ", 6) == 0) {
        char *remote = msg + 6;
        char *nl = strchr(remote, '\n');
        const char *payload = "";
        if (nl) { *nl = '\0'; payload = nl + 1; }
        size_t n = strlen(remote);
        while (n > 0 && (remote[n - 1] == '\n' || remote[n - 1] == '\r'))
            remote[--n] = '\0';
        wss_handle_sync0(client, remote, payload);
        return;
    }

    if (strncmp(msg, "SYNC ", 5) == 0) {
        char *sid_str = msg + 5;
        char *nl = strchr(sid_str, '\n');
        const char *payload = "";
        if (nl) { *nl = '\0'; payload = nl + 1; }
        uint32_t session_id = (uint32_t)strtoul(sid_str, NULL, 10);

        wss_session_t *s = (wss_session_t*)find_session(session_id);
        if (!s) {
            print("W:", "[WSS] SYNC: unknown session_id=%u from '%s'\n",
                  session_id, client->base.local_peer_id);
            return;
        }
        wss_handle_sync(s, payload);
        return;
    }

    if (strncmp(msg, "FIN ", 4) == 0) {
        uint32_t sid = (uint32_t)strtoul(msg + 4, NULL, 10);

        wss_session_t *s = (wss_session_t*)find_session(sid);
        if (!s) {
            print("W:", "[WSS] FIN: unknown session_id=%u from '%s'\n",
                  sid, client->base.local_peer_id);
            return;
        }
        wss_handle_fin(s);
        return;
    }

    print("V:", "[WSS] unknown msg from cid=%d: %.32s...\n", cid, msg);
}

/* WS on_data 回调 — 二进制帧（PACKET 中继 + MSG RPC） */
void wss_on_data(ws_server_t *srv, ws_client_id_t cid, const uint8_t *data, size_t len, void *user_data) {
    (void)srv; (void)user_data;
    if (len < P2P_WSS_BIN_HDR_SIZE) return;      /* 至少 type(1)+session_id(4) */

    /* 查找 client */
    wss_client_t *client = NULL;
    { wss_client_t *c, *tmp;
        HASH_ITER(hh, g_wss_clients, c, tmp) {
            if (c->cid == cid) { client = c; break; }
        }
    }
    if (!client) return;

    client->base.last_active = P_tick_ms();

    uint8_t  type       = data[0];
    uint32_t session_id = nget_l(data + 1);

    /* 查找 session */
    session_t *_s = NULL;
    HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
    wss_session_t *ws_s = (wss_session_t *)_s;
    if (!ws_s || _s->client != &client->base) {
        print("W:", "[WSS] BIN: unknown session_id=%u type=0x%02x from '%s'\n",
              session_id, type, client->base.local_peer_id);
        return;
    }

    switch (type) {
    case P2P_WSS_BIN_PACKET: wss_handle_packet(ws_s, data, len); break;
    case P2P_WSS_BIN_REQ:    wss_handle_req(ws_s, data, len);    break;
    case P2P_WSS_BIN_RESP:   wss_handle_resp(ws_s, data, len);   break;
    default:
        print("W:", "[WSS] BIN: unknown type=0x%02x from '%s'\n", type, client->base.local_peer_id);
        break;
    }
}

/* WS on_disconnect 回调：标记离线 + 通知对端 */
void wss_on_disconnect(ws_server_t *srv, ws_client_id_t cid, void *user_data) {
    (void)srv; (void)user_data;

    /* 查找当前 cid 对应的 client */
    wss_client_t *client = NULL;
    {   wss_client_t *c, *tmp;
        HASH_ITER(hh, g_wss_clients, c, tmp) {
            if (c->cid == cid) { client = c; break; }
        }
    }
    if (!client) return;

    print("I:", "[WSS] peer '%s' disconnected (cid=%d)\n",
          client->base.local_peer_id, cid);

    wss_invalidate_client(client, false);
}

bool init_wss_client(wss_client_t* c) {

    buffer_item_t *buf_item = alloc_buffer(RELAY_FRAME_SIZE);
    if (!buf_item) {
        print("E:", LA_F("[TCP] OOM: cannot allocate recv buffer for new client\n", LA_F133, 133));
        P_sock_close(fd);
        return false;
    }

    c->base.valid = true;
    c->base.last_active = P_tick_ms();
    c->base.local_peer_id[0] = '\0';
    c->base.instance_id = 0;
    c->base.sessions = NULL;

    c->fd = fd;
    c->online_ack_pending = false;
    c->recv_buf = ITEM2BUF(buf_item);
    c->recv_len = 0;
    c->sending_buff_head = NULL;
    c->sending_buff_rear = NULL;
    c->sending_sess_head = NULL;
    c->sending_sess_rear = NULL;
    c->send_offset = 0;

    return true;
}
