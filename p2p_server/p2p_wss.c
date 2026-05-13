//
// Created by 温朋 on 2026/4/19.
//
#define MOD_TAG "WSS"

#include "p2p_wss.h"

ARGS(relay);
ARGS(msg);

// WSS RPC 待确认链表（按 rpc_sent_time 排序，队头最早超时）
static wss_session_t*               g_wss_rpc_pending_head = NULL;
static wss_session_t*               g_wss_rpc_pending_rear = NULL;

static cw_client_ctx_t              g_wss_ctx;

#define WSS_PEER(s)                 ((wss_session_t*)PEER(s))
#define WSS_CLIENT(s)               ((wss_client_t*)CLIENT(s))

// 前置声明（定义在后面，wss_init 需要引用）
static void wss_handle_frame(cw_client_t *base, uint8_t opcode, buf16_item_t *payload0, buf16_item_t *payload1);
static void wss_handle_peer_sent(ct_session_t *session, buf16_item_t *buf_item);
static void wss_session_break(ct_session_t *ct_session, ct_session_t *ct_peer, break_mode_e break_mode);
static void wss_pending_enqueue_rpc(wss_session_t *s);
static void wss_pending_remove_rpc(wss_session_t *s);

///////////////////////////////////////////////////////////////////////////////

/* 判断 client 是否在线（已注册且 WS 握手完成） */
static inline bool wss_client_online(const wss_client_t *c) {
    return c && c->base.fd != P_INVALID_SOCKET && !TCP_HS_IS_HANDSHAKING((ct_client_t*)c);
}

/* 写入 n 字节（可绕回），返回 0=成功, -1=空间不足 */
static inline int wss_sync_write(wss_session_t *s, const uint8_t *src, size_t n) {
    if (n > WSS_SYNC_PAYLOAD_MAX - s->sync_len) return -1;
    if (!s->sync_buf) {
        s->sync_buf = alloc_buf16(BUF_FLAG_2048(0));
        if (!s->sync_buf) return -1;
    }
    char *data = (char*)ITEM2BUF(s->sync_buf);
    size_t tail = (s->sync_head + s->sync_len) % WSS_SYNC_PAYLOAD_MAX;
    size_t to_end = WSS_SYNC_PAYLOAD_MAX - tail;
    if (n <= to_end) {
        memcpy(data + tail, src, n);
    } else {
        memcpy(data + tail, src, to_end);
        memcpy(data, src + to_end, n - to_end);
    }
    s->sync_len += n;
    return 0;
}

// 分配并发送一个 WS text frame（小型文本，如协议应答）
// 分配足够空间：10字节WS帧头预留 + 文本内容
static ret_t wss_send_text(cw_client_t *client, const char *str) {
    size_t text_len = strlen(str);
    uint16_t total = (uint16_t)(10 + text_len);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) return E_OUT_OF_MEMORY;
    uint8_t *buf = ITEM2BUF(item) + 10;
    memcpy(buf, str, text_len);
    item->len = total;
    return cw_send_frame(client, WS_OP_TEXT, item, 10);
}

// 构造 SYNC 转发帧：帧头 "SYNC <dst_sid>\n" + ring buffer 内容(连续化) + "\n"(fin mark)
// 返回 buf16_item_t*，payload_pos=10（预留 WS 帧头）；NULL 表示 OOM
// 同时清空 src_s 的 ring buffer
static buf16_item_t* wss_build_sync_frame(wss_session_t *src_s, uint32_t dst_sid) {
    size_t cached = src_s->sync_len;
    if (!cached) return NULL;

    // "SYNC <dst_sid>\n" 前缀：最大 "SYNC " + 10位数字 + "\n" = 16字节
    char hdr_text[20];
    int hdr_len = snprintf(hdr_text, sizeof(hdr_text), P2P_WSS_CMD_SYNC_FMT, dst_sid);

    // 帧总大小：10(WS hdr reserve) + hdr_text + ring_data + '\n'(fin)
    uint16_t total = (uint16_t)(10 + hdr_len + cached + 1);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) return NULL;

    uint8_t *buf = ITEM2BUF(item) + 10;
    memcpy(buf, hdr_text, hdr_len);
    buf += hdr_len;

    // 线性化 ring buffer
    const char *data = (const char*)ITEM2BUF(src_s->sync_buf);
    size_t pos = src_s->sync_head;
    size_t to_end = WSS_SYNC_PAYLOAD_MAX - pos;
    if (to_end >= cached) {
        memcpy(buf, data + pos, cached);
    } else {
        memcpy(buf, data + pos, to_end);
        memcpy(buf + to_end, data, cached - to_end);
    }
    buf += cached;
    *buf = '\n';   // fin mark

    item->len = total;

    // 清空 ring buffer
    free_buf16(src_s->sync_buf);
    src_s->sync_buf  = NULL;
    src_s->sync_head = src_s->sync_len = 0;

    return item;
}

// 发送 SYNC confirm 文本帧给 src_c（通知已转发 bytes 字节）
static void wss_send_sync_confirm(wss_session_t *src_s, size_t bytes) {
    wss_client_t *src_c = (wss_client_t*)src_s->base.client;
    if (!wss_client_online(src_c)) return;
    char confirm[48];
    snprintf(confirm, sizeof(confirm), P2P_WSS_RSP_SYNC_CONFIRM_FMT, src_s->base.session_id, bytes);
    wss_send_text((cw_client_t*)src_c, confirm);
}

// 将 src_s ring buffer 构造成 WS text frame，入队到 dst_s 的 sync_peer_send 队列
// 同时向 src_s 的 client 回复 confirm（因为 ring buffer 已从 src_s 中移走）
// 返回 false 表示队列已满或 OOM
static bool wss_enqueue_sync(wss_session_t *src_s, wss_session_t *dst_s) {
    if (dst_s->sync_peer_send_cnt >= WSS_PEER_Q_MAX) return false;

    size_t cached = src_s->sync_len;
    buf16_item_t *item = wss_build_sync_frame(src_s, dst_s->base.session_id);
    if (!item) return false;

    // 入队
    dst_s->sync_peer_send[dst_s->sync_peer_send_cnt] = item;
    dst_s->sync_peer_send_cnt++;

    // 若队头（刚入队且是第一项），立即发送
    if (dst_s->sync_peer_send_cnt == 1) {
        item->refer = (void*)dst_s;
        ct_session_send((ct_session_t*)dst_s, item);
    }

    // 通知 src 端已转发（ring buffer 已被 build_sync_frame 清空）
    wss_send_sync_confirm(src_s, cached);

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u (%zu bytes)\n", LA_F167, 167),
          "SYNC", src_s->base.session_id, dst_s->base.session_id, cached);
    return true;
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

// 服务器生成 RPC 错误 RSP（二进制帧）
// 格式：[P2P_WSS_BIN_RSP][session_id(4)][sid(2)][code(1)]
static void wss_session_send_rpc_code(wss_session_t *s, uint16_t sid, uint8_t code) {
    wss_client_t *c = (wss_client_t*)s->base.client;
    if (!wss_client_online(c)) return;

    uint16_t total = (uint16_t)(10 + P2P_WSS_BIN_RPC_MIN_SZ);
    buf16_item_t *item = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total), 0));
    if (!item) return;
    uint8_t *buf = ITEM2BUF(item) + 10;
    buf[0] = P2P_WSS_BIN_RSP;
    nwrite_l(buf + 1, s->base.session_id);
    nwrite_s(buf + 1 + P2P_SESS_ID_SZ, sid);
    buf[1 + P2P_SESS_ID_SZ + 2] = code;
    item->len = total;
    cw_send_frame((cw_client_t*)c, WS_OP_BINARY, item, 10);
}

//-----------------------------------------------------------------------------

// 清理一个 SYNC/PKT 通道的所有队列项，并将存活项转发给 dst（镜像 relay_ch_break_forward）
static void wss_ch_break_forward(buf16_item_t **arr, uint8_t *cnt, wss_session_t *dst) {
    for (uint8_t i = 0; i < *cnt; i++) {
        buf16_item_t *it = arr[i]; arr[i] = NULL;
        if (it->refer != NULL && it->refer != ITEM_REF_ACK_PENDING) it->refer = NULL;
        else ct_session_send((ct_session_t*)dst, it);
    }
    *cnt = 0;
}

// 清理一个通道的所有队列项并释放
static void wss_ch_break_free(buf16_item_t **arr, uint8_t *cnt) {
    for (uint8_t i = 0; i < *cnt; i++) {
        buf16_item_t *it = arr[i]; arr[i] = NULL;
        if (it->refer != NULL && it->refer != ITEM_REF_ACK_PENDING) it->refer = NULL;
        else free_buf16(it);
    }
    *cnt = 0;
}

// 发送 FIN 文本帧给对端 session 的 client
static void wss_session_send_fin(wss_session_t *session) {
    wss_client_t *c = (wss_client_t*)session->base.client;
    if (!wss_client_online(c)) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "FIN %u", session->base.session_id);
    wss_send_text((cw_client_t*)c, buf);
}

// session_break 回调（对标 relay_session_break）
// 触发时机：其中一端连接断开或主动下线
static void wss_session_break(ct_session_t *ct_session, ct_session_t *ct_peer, break_mode_e break_mode) {

    assert(PEER_ONLINE(ct_session) && PEER_ONLINE(ct_peer));

    wss_session_t *session = (wss_session_t*)ct_session;
    wss_session_t *peer    = (wss_session_t*)ct_peer;

    // 存在对端发起的 REQ 等待本端 RSP
    if (peer->rpc_pending_sid) {
        wss_session_send_rpc_code(peer, peer->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
        wss_pending_remove_rpc(peer);
        peer->rpc_sent_time   = 0;
        peer->rpc_pending_sid = 0;
    }

    // 存在本端发起的 REQ 等待对端 RSP
    if (session->rpc_pending_sid) {
        if (break_mode != SESS_BREAK_TERM)
            wss_session_send_rpc_code(session, session->rpc_pending_sid, P2P_RPC_ERR_BREAK);
        wss_pending_remove_rpc(session);
        session->rpc_sent_time   = 0;
        session->rpc_pending_sid = 0;
    }

    if (break_mode == SESS_BREAK_STOP) return;

    // 清理本端队列，转发剩余项给对端
    wss_ch_break_forward(session->sync_peer_send, &session->sync_peer_send_cnt, peer);
    wss_ch_break_forward(session->pkt_peer_send,  &session->pkt_peer_send_cnt,  peer);

    // 向对端发送 FIN
    wss_session_send_fin(peer);

    if (break_mode == SESS_BREAK_CLOSE) {
        wss_ch_break_forward(peer->sync_peer_send, &peer->sync_peer_send_cnt, session);
        wss_ch_break_forward(peer->pkt_peer_send,  &peer->pkt_peer_send_cnt,  session);
    } else {
        wss_ch_break_free(peer->sync_peer_send, &peer->sync_peer_send_cnt);
        wss_ch_break_free(peer->pkt_peer_send,  &peer->pkt_peer_send_cnt);
    }

    // 清理本端 ring buffer
    if (session->sync_buf) { free_buf16(session->sync_buf); session->sync_buf = NULL; }
    session->sync_head = session->sync_len = 0;
}

///////////////////////////////////////////////////////////////////////////////

cw_client_ctx_t*
wss_init(void) {
    g_wss_ctx.base.base.free    = wss_free_client;
    g_wss_ctx.base.base.migrate = ct_migrate_client;

    cw_ctx_init(&g_wss_ctx);
    g_wss_ctx.base.handle_peer_sent = wss_handle_peer_sent;
    g_wss_ctx.base.session_break    = wss_session_break;
    g_wss_ctx.base.max_payload_len  = WSS_MAX_PAYLOAD;
    g_wss_ctx.base.fatal_item       = NULL;                 // fatal_item: 静态错误帧，WS 协议层只需发 close，不需要像 relay 那样单独构造
    g_wss_ctx.base.error_item       = NULL;

    g_wss_ctx.sub_protocol   = "p2p";
    g_wss_ctx.handle_frame   = wss_handle_frame;
    g_wss_ctx.handshake_done = NULL;
    g_wss_ctx.handle_ping    = NULL;
    g_wss_ctx.handle_close   = NULL;
    return &g_wss_ctx;
}

bool
wss_init_client(wss_client_t *c) {
    return cw_init_client((cw_client_t*)c, &g_wss_ctx);
}

void
wss_free_client(client_t *client) {
    cw_free_client(&g_wss_ctx, (cw_client_t*)client);
}

///////////////////////////////////////////////////////////////////////////////

// 处理 SYNC0 消息：创建/恢复会话
static void wss_handle_syn0(wss_client_t *client, const char *remote_peer_id,
                            uint8_t *payload, size_t payload_len) {
    const char *PROTO = "SYN0";

    if (!*remote_peer_id) {
        print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), PROTO);
        wss_send_text((cw_client_t*)client, "SYNC0 FAIL invalid remote id");
        return;
    }

    wss_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, remote_peer_id,
                              (session_t**)&local_s, (session_t**)&remote_s,
                              sizeof(wss_session_t));

    if (side == E_OUT_OF_MEMORY) {
        print("E:", LA_F("%s: OOM building session '%s' -> '%s'\n", LA_F37, 37),
              PROTO, client->base.local_peer_id, remote_peer_id);
        wss_send_text((cw_client_t*)client, "SYNC0 FAIL OOM");
        return;
    }
    if (side < E_NONE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, (const char *)payload, side);
        wss_send_text((cw_client_t*)client, "SYNC0 FAIL internal");
        return;
    }

    if (payload_len > WSS_SYNC_PAYLOAD_MAX)
        payload_len = WSS_SYNC_PAYLOAD_MAX;

    print("V:", LA_F("%s: local='%s', remote='%s', online=%d, sync_cache=%u\n", LA_F162, 162),
          PROTO, client->base.local_peer_id, remote_peer_id, side, remote_s ? 1 : 0, (uint32_t)payload_len);

    if (payload_len) {
        wss_sync_write(local_s, payload, payload_len);
    }

    // 如果对方不在线，立刻返回 sync0 offline
    if (!remote_s) {

        { char _b[8+P2P_PEER_ID_MAX+12+8]; snprintf(_b,sizeof(_b),"SYNC0 %s %u offline",remote_peer_id,local_s->base.session_id); wss_send_text((cw_client_t*)client,_b); }

        print("I:", LA_F("%s: '%s' -> '%s' created (id=%u, peer_zombie)\n", LA_F63, 63),
              PROTO, client->base.local_peer_id, remote_peer_id, local_s->base.session_id);

    }
    // 对端已在线，启动双方 sync0 同步
    else {

        // 建立双向引用关系
        if (!local_s->base.peer) local_s->base.peer = &remote_s->base;
        if (!remote_s->base.peer) remote_s->base.peer = &local_s->base;

        //-------

        { char _b[8+P2P_PEER_ID_MAX+12+8]; snprintf(_b,sizeof(_b),"SYNC0 %s %u online",remote_peer_id,local_s->base.session_id); wss_send_text((cw_client_t*)client,_b); }
        // 将 remote 端积压数据入队到 local_s（通过 sync_peer_send 队列发给 client）
        if (remote_s->sync_len)
            wss_enqueue_sync(remote_s, local_s);

        //-------

        wss_client_t *remote_c = (wss_client_t*)remote_s->base.client;
        { char _b[8+P2P_PEER_ID_MAX+12+8]; snprintf(_b,sizeof(_b),"SYNC0 %s %u online",client->base.local_peer_id,remote_s->base.session_id); wss_send_text((cw_client_t*)remote_c,_b); }
        // 将 local 端积压数据入队到 remote_s（通过 sync_peer_send 队列发给 remote_c）
        if (local_s->sync_len)
            wss_enqueue_sync(local_s, remote_s);

        //-------

        print("I:", LA_F("%s: '%s' <-> '%s' paired (ses=%u/%u)\n", LA_F152, 152),
              PROTO, client->base.local_peer_id, remote_peer_id,
              local_s->base.session_id, remote_s->base.session_id);
    }
}

// 处理 SYNC 消息：按 session_id 路由转发
static void wss_handle_sync(wss_session_t *session, const uint8_t *payload, size_t payload_len) {
    wss_client_t *client = (wss_client_t*)session->base.client;
    uint32_t session_id = session->base.session_id;

    // 写入缓存（无论对端状态）
    if (!payload_len || wss_sync_write(session, payload, payload_len) != 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), P2P_WSS_RSP_SYNC_BUSY_FMT, session_id);
        wss_send_text((cw_client_t*)client, buf);
        return;
    }

    // 对端不在线：数据留在 ring buffer
    if (!PEER_ONLINE(&session->base)) return;
    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) return;

    // 尝试入队发送；队满时留在 ring buffer 等下次 handle_peer_sent 触发
    wss_enqueue_sync(session, peer_s);
}

// 处理 FIN 消息：客户端主动断开会话
static void wss_handle_fin(wss_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: '%s' ses_id=%u\n", LA_F45, 45),
          PROTO, session->base.client->local_peer_id, session->base.session_id);

    ct_close_session(&g_wss_ctx.base, (ct_session_t*)session, true);
}

// 处理 PKT — P2P 数据包中继（重写 session_id，零拷贝转发）
// 使用 payload1（已预留 10 字节 WS 帧头空间），直接入队到对端 pkt_peer_send
// payload 布局（在 payload1 中，从 payload1->pos 起）: [type(1)][session_id(4)][P2P hdr(4)][data(N)]
static void wss_handle_pkt(wss_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "PKT";

    if (len < P2P_WSS_BIN_PKT_MIN_SZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!wss_client_online((wss_client_t*)peer_s->base.client)) return;

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u, data_len=%u\n", LA_F168, 168),
          PROTO, session->base.session_id, peer_s->base.session_id, len - P2P_WSS_BIN_PKT_MIN_SZ);

    // 就地重写 session_id → 对端视角的 session_id
    nwrite_l(payload + 1, peer_s->base.session_id);

    // 检查 pkt_peer_send 队列是否已满
    if (peer_s->pkt_peer_send_cnt >= WSS_PEER_Q_MAX) {
        print("W:", LA_F("%s: pkt queue full, dropping\n", 0, 0), PROTO);
        return;   // payload1 由调用方（handle_frame）释放
    }

    // 零拷贝：将 payload1 前置空间作为 WS 帧头区域，直接入队
    payload1->pos = 0;         // 暴露前置的 WS 帧头预留空间
    // payload1 的 payload_pos 在 resolve_payload_len 中已预留 10 字节；payload 从 payload1->pos+10 开始
    // cw_send_frame 会在 [0, payload_pos) 写入帧头
    uint16_t payload_pos = (uint16_t)(payload - ITEM2BUF(payload1));

    peer_s->pkt_peer_send[peer_s->pkt_peer_send_cnt] = payload1;
    peer_s->pkt_peer_send_cnt++;

    if (peer_s->pkt_peer_send_cnt == 1) {
        // 队头：立即发送
        payload1->refer = (void*)peer_s;
        cw_send_frame((cw_client_t*)peer_s->base.client, WS_OP_BINARY, payload1, payload_pos);
    }
    // 否则在队列中等待，handle_peer_sent 时再发
}

// 处理 REQ — RPC 请求转发（零拷贝）
static void wss_handle_req(wss_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "REQ";

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%u\n", LA_F155, 155), PROTO, len);
        return;
    }

    uint8_t* ptr = payload + 1 + P2P_SESS_ID_SZ;
    uint16_t sid = nget_s(ptr);
    uint8_t  msg = payload[P2P_WSS_BIN_RPC_MIN_SZ-1];
    int data_len = (int)len - (int)P2P_WSS_BIN_RPC_MIN_SZ;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, session->base.client->local_peer_id, sid, msg, data_len);

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!wss_client_online((wss_client_t*)peer_s->base.client)) {
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

    payload1->pos = 0;
    uint16_t payload_pos = (uint16_t)(payload - ITEM2BUF(payload1));
    payload1->refer = (void*)peer_s;
    cw_send_frame((cw_client_t*)peer_s->base.client, WS_OP_BINARY, payload1, payload_pos);

    session->rpc_pending_sid = sid;
    session->rpc_sent_time   = P_tick_ms();
    wss_pending_enqueue_rpc(session);
}

// 处理 RSP — RPC 响应转发（零拷贝）
static void wss_handle_rsp(wss_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "RSP";

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%u\n", LA_F41, 41), PROTO, len);
        return;
    }

    uint8_t* ptr = payload + 1 + P2P_SESS_ID_SZ;
    uint16_t sid  = nget_s(ptr);
    uint8_t  code = payload[P2P_WSS_BIN_RPC_MIN_SZ-1];
    int data_len  = (int)len - (int)P2P_WSS_BIN_RPC_MIN_SZ;

    print("V:", LA_F("%s: '%s' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, session->base.client->local_peer_id, sid, code, data_len);

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    if (!wss_client_online((wss_client_t*)peer_s->base.client)) {
        print("W:", LA_F("%s: requester offline, discarding\n", LA_F66, 66), PROTO);
        return;
    }

    if (peer_s->rpc_pending_sid != sid) {
        print("W:", LA_F("%s: sid mismatch (got=%u, pending=%u), discarding\n", LA_F68, 68),
              PROTO, sid, peer_s->rpc_pending_sid);
        return;
    }

    nwrite_l(payload + 1, peer_s->base.session_id);

    payload1->pos = 0;
    uint16_t payload_pos = (uint16_t)(payload - ITEM2BUF(payload1));
    payload1->refer = (void*)peer_s;
    cw_send_frame((cw_client_t*)peer_s->base.client, WS_OP_BINARY, payload1, payload_pos);

    wss_pending_remove_rpc(peer_s);
    peer_s->rpc_pending_sid = 0;
}

//-----------------------------------------------------------------------------

// handle_peer_sent 回调（对标 relay_handle_peer_sent）
// PKT：移出队头，启动下一项；队满→满−1时补发 READY 状态
// SYNC：TCP写出完成，设 ACK_PENDING，等待对端 SYNC confirm（或连接断开时在 session_break 清理）
static void wss_handle_peer_sent(ct_session_t *ct_session, buf16_item_t *buf_item) {

    assert(PEER_ONLINE(ct_session));
    wss_session_t *session = (wss_session_t*)ct_session;

    // 判断是 PKT 还是 SYNC：WS 帧头在 payload_pos 之前，opcode 在 buf_item->pos 偏移0
    // 简单判断：pkt_peer_send[0] == buf_item → PKT
    if (session->pkt_peer_send_cnt > 0 && session->pkt_peer_send[0] == buf_item) {

        buf_item->refer = NULL;  // 允许调用方释放

        session->pkt_peer_send[0] = session->pkt_peer_send[1];
        session->pkt_peer_send[1] = NULL;
        session->pkt_peer_send_cnt--;

        if (session->pkt_peer_send_cnt > 0) {
            buf16_item_t *next = session->pkt_peer_send[0];
            next->refer = (void*)session;
            // 恢复 payload_pos：零拷贝时 pos=0，payload 在 10 字节之后
            uint8_t *payload_start = ITEM2BUF(next) + 10;
            uint16_t payload_pos = (uint16_t)(payload_start - ITEM2BUF(next));
            cw_send_frame((cw_client_t*)session->base.client, WS_OP_BINARY, next, payload_pos);
        }
        // 如需要 READY 信号（WSS 协议中为可选），可在此扩展

    } else if (session->sync_peer_send_cnt > 0 && session->sync_peer_send[0] == buf_item) {

        // SYNC：TCP 写入完成，保留队头，设 ACK_PENDING 等待应用层 confirm
        buf_item->refer = ITEM_REF_ACK_PENDING;

        // 检查是否还有积压的 ring buffer 数据（本端 src_s 又有新数据入队）
        // 注：SYNC confirm 到来时通过 wss_handle_sync_confirm 继续推进
    }
}

// SYNC confirm 到来时调用（由 wss_handle_message 中的 "SYNC <sid> confirm" 分支调用）
// 移出 sync_peer_send 队头，若 ring buffer 仍有数据继续入队
static void wss_on_sync_confirmed(wss_session_t *dst_s) {
    if (dst_s->sync_peer_send_cnt == 0) return;

    buf16_item_t *head = dst_s->sync_peer_send[0];
    assert(head->refer == ITEM_REF_ACK_PENDING);
    head->refer = NULL;
    free_buf16(head);

    dst_s->sync_peer_send[0] = dst_s->sync_peer_send[1];
    dst_s->sync_peer_send[1] = NULL;
    dst_s->sync_peer_send_cnt--;

    // 队头若已就绪（排队时未能立即发送），继续发送
    if (dst_s->sync_peer_send_cnt > 0) {
        buf16_item_t *next = dst_s->sync_peer_send[0];
        next->refer = (void*)dst_s;
        ct_session_send((ct_session_t*)dst_s, next);
    }

    // 若 src_s ring buffer 仍有积压数据，再次入队
    if (!PEER_ONLINE(&dst_s->base)) return;
    wss_session_t *src_s = (wss_session_t*)dst_s->base.peer;
    if (src_s->sync_len > 0 && dst_s->sync_peer_send_cnt < WSS_PEER_Q_MAX)
        wss_enqueue_sync(src_s, dst_s);
}

//-----------------------------------------------------------------------------

// 处理 WSS 模式信令（WebSocket 文本帧）
static void wss_handle_text(wss_client_t *client, const uint8_t *msg, size_t len) {
    assert(client->base.proto == PROTO_WSS);

    if (len == 0) return;

    client->base.last_active = P_tick_ms();

    char* ln = (char*)strnstr((const char*)msg, "\n", len);
    if (!ln) goto error_proto;
    *ln = '\0';
    #define ln_trim while (ln[-1] == '\n' || ln[-1] == '\r') *--ln = '\0'

        // PROTO: REG <peer_id> <instance_id>
    if (strncmp((char*)msg, P2P_WSS_CMD_REG, P2P_WSS_CMD_REG_SZ) == 0) { const char *PROTO = "REG";

        // 重复 REG
        if (client->base.local_peer_id[0]) {
            print("E:", LA_F("%s: duplicate from '%s'\n", LA_F97, 97), PROTO, client->base.local_peer_id);
            wss_send_text((cw_client_t*)client, "REG FAIL duplicate reg");
            goto error_proto;
        }

        char *remote_id = (char*)msg + P2P_WSS_CMD_REG_SZ;

        char *inst_id = strrchr(remote_id, ' ');
        if (!inst_id || inst_id == remote_id) {
            print("E:", LA_F("%s: invalid REG format\n", LA_F160, 160), PROTO);
            wss_send_text((cw_client_t*)client, "REG FAIL invalid instance_id");
            goto error_proto;
        }

        ln_trim;
        *inst_id++ = '\0';

        if (!remote_id[0] || strlen(remote_id) > P2P_PEER_ID_MAX) {
             print("E:", LA_F("%s: invalid peer id\n", LA_F96, 96), PROTO);
             wss_send_text((cw_client_t*)client, "REG FAIL empty peer id");
            goto error_proto;
        }
        uint32_t instance_id = (uint32_t)strtoul(inst_id, NULL, 10);
        if (instance_id == 0) {
            wss_send_text((cw_client_t*)client, "REG FAIL invalid instance id");
            goto error_proto;
        }

        wss_client_t *reg = (wss_client_t*)find_client(remote_id);
        if (reg) { assert(reg != client);

            if (resident_client(&reg->base, PROTO_WSS, instance_id, &client->base)) {
                client = reg;
                print("I:", LA_F("%s: '%s' reconnected & reactive (inst=%u)\n", LA_F98, 98),
                      PROTO, client->base.local_peer_id, client->base.instance_id);

                // 重连场景：重发 sync_peer_send 队头中处于 ACK_PENDING 状态的 SYNC 帧
                for (session_t *sess = client->base.sessions; sess; sess = sess->next) {
                    wss_session_t *ws_sess = (wss_session_t*)sess;
                    if (ws_sess->sync_peer_send_cnt > 0 &&
                        ws_sess->sync_peer_send[0]->refer == ITEM_REF_ACK_PENDING) {
                        ws_sess->sync_peer_send[0]->refer = (void*)ws_sess;
                        ct_session_send((ct_session_t*)sess, ws_sess->sync_peer_send[0]);
                    }
                }

                ct_reactive_client(&g_wss_ctx.base, (ct_client_t*)client);

            } else {
                print("I:", LA_F("%s: '%s' reconnected & renew (inst=%u)\n", LA_F153, 153),
                      PROTO, reg->base.local_peer_id, instance_id);
            }

        } else {
            assert(!client->base.sessions);
            assert(!client_identified(&client->base));

            print("I:", LA_F("%s: '%s' new REG (inst=%u)\n", LA_F93, 93),
                  PROTO, remote_id, instance_id);

            strcpy(client->base.local_peer_id, remote_id);
            client->base.instance_id = instance_id;

            identify_client(&client->base);
        }

        // 回复 REG ACK
        {
            char ok[48]; snprintf(ok, sizeof(ok), "REG OK %d %d", WSS_SYNC_PAYLOAD_MAX,
                              (ARGS_relay.i64 ? P2P_RLY_FEATURE_RELAY : 0) | (ARGS_msg.i64 ? P2P_RLY_FEATURE_MSG : 0));
            wss_send_text((cw_client_t*)client, ok);
        }
        return;
    }

    if (!client->base.local_peer_id[0]) {
        print("E:", LA_F("%s: rejected for not reg\n", LA_F147, 147), (char*)msg);
        goto error_proto;
    }

    // PROTO: OFF
    if (strcmp((char*)msg, P2P_WSS_CMD_OFF) == 0) {
        print("I:", LA_F("%s: '%s'\n", LA_F72, 72), "OFF",
              client->base.local_peer_id);
        ct_client_off(&g_wss_ctx.base, (ct_client_t*)client);
        return;
    }

    // SYNC0 <remote_peer_id>[\n<payload>]
    if (strncmp((char*)msg, P2P_WSS_CMD_SYN0, P2P_WSS_CMD_SYN0_SZ) == 0) {

        char *remote_id = (char*)msg + P2P_WSS_CMD_SYN0_SZ;
        uint8_t *payload = (uint8_t*)ln+1;
        ln_trim;

        wss_handle_syn0(client, remote_id, payload, len - (payload - msg));
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
            return;
        }

        wss_handle_fin((wss_session_t*)s);
        return;
    }

    // SYNC <session_id> confirm <bytes>   — SYNC 已到达对端确认（ACK_PENDING 解锁）
    if (strncmp((char*)msg, P2P_WSS_CMD_SYNC, P2P_WSS_CMD_SYNC_SZ) == 0) {

        char *after = (char*)msg + P2P_WSS_CMD_SYNC_SZ;
        uint32_t session_id = (uint32_t)strtoul(after, NULL, 10);

        // 检查是否 "SYNC <sid> confirm" 字符串
        char *space = strchr(after, ' ');
        if (space && strncmp(space + 1, "confirm", 7) == 0) {
            session_t *s = find_session(session_id);
            if (s && s->client == &client->base) {
                wss_on_sync_confirmed((wss_session_t*)s);
            }
            return;
        }

        // 否则为 SYNC <session_id>\n<payload>
        // Actually ln was ln = first \n already zeroed; payload is after it
        uint8_t *payload = (uint8_t*)(ln + 1);

        session_t *s = find_session(session_id);
        if (!s || s->client != &client->base) {
            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "SYNC", session_id, client->base.local_peer_id);
            wss_send_text((cw_client_t*)client, "SYNC FAIL unknown session");
            return;
        }

        wss_handle_sync((wss_session_t*)s, payload, len - (size_t)(payload - msg));
        return;
    }

error_proto:
    print("V:", LA_F("unknown msg from '%s': %.32s\n", LA_F203, 203),
          client->base.local_peer_id, msg);
}

// 处理 WSS 模式二进制帧（PKT 中继 + RPC）
static void wss_handle_binary(wss_client_t *client, buf16_item_t *payload1,
                               uint8_t *data, uint16_t len) {
    assert(client->base.proto == PROTO_WSS);

    if (len < 1 + P2P_SESS_ID_SZ) return;

    client->base.last_active = P_tick_ms();

    uint8_t  type       = data[0];
    uint8_t* ptr        = data + 1;
    uint32_t session_id = nget_l(ptr);

    wss_session_t *ws_s = (wss_session_t*)find_session(session_id);
    if (!ws_s || ws_s->base.client != &client->base) {
        print("W:", LA_F("BIN: unknown ses_id=%u type=0x%02x from '%s'\n", LA_F171, 171),
              session_id, type, client->base.local_peer_id);
        return;
    }

    if (!PEER_ONLINE(&ws_s->base)) {
        print("W:", LA_F("BIN 0x%02x: ses_id=%u peer not connected\n", LA_F170, 170), type, session_id);
        if (type == P2P_WSS_BIN_REQ && len >= P2P_WSS_BIN_RPC_MIN_SZ) {
            ptr = data + 1 + P2P_SESS_ID_SZ;
            wss_session_send_rpc_code(ws_s, nget_s(ptr), P2P_RPC_ERR_PEER_OFF);
        }
        return;
    }

    bool consumed_payload1 = false;
    switch (type) {
    case P2P_WSS_BIN_PKT:
        wss_handle_pkt(ws_s, payload1, data, len);
        consumed_payload1 = true;
        break;
    case P2P_WSS_BIN_REQ:
        wss_handle_req(ws_s, payload1, data, len);
        consumed_payload1 = true;
        break;
    case P2P_WSS_BIN_RSP:
        wss_handle_rsp(ws_s, payload1, data, len);
        consumed_payload1 = true;
        break;
    default:
        print("W:", LA_F("BIN: unknown type=0x%02x from '%s'\n", LA_F149, 149),
              type, client->base.local_peer_id);
        break;
    }

    // 若 payload1 已被零拷贝消费（入队），框架不应再释放它
    if (consumed_payload1 && payload1 && payload1->refer != NULL) {
        // payload1->refer is set to session pointer (non-NULL, non-STATIC)
        // Tell framework not to free: ct_client callback leaves refer as-is
        // The framework checks payload_buf->refer after handle_proto returns;
        // setting client->payload_buf = NULL prevents double-free
        ((ct_client_t*)client)->payload_buf = NULL;
    }
}

// handle_frame 回调（custom_ws 框架调用，opcode = TEXT 或 BINARY）
static void wss_handle_frame(cw_client_t *base, uint8_t opcode,
                              buf16_item_t *payload0, buf16_item_t *payload1) {
    wss_client_t *client = (wss_client_t*)base;

    // 将 payload0 + payload1 合并成连续缓冲（TEXT 消息通常较小）
    // 对于 BINARY 零拷贝转发：只用 payload1，payload0 通常为空
    if (opcode == WS_OP_TEXT) {
        // 合并到临时缓冲（TEXT 消息通常 < 256 字节；使用栈或 payload1 空间）
        uint8_t *msg = NULL;
        uint16_t total_len = 0;

        if (!payload0 || payload0->len == payload0->pos) {
            // 只有 payload1
            msg = ITEM2BUF(payload1) + payload1->pos;
            total_len = payload1->len - payload1->pos;
        } else {
            // payload0 + payload1：需要拼接（一般不常见）
            uint16_t p0_len = payload0->len - payload0->pos;
            uint16_t p1_len = payload1 ? (payload1->len - payload1->pos) : 0;
            total_len = p0_len + p1_len;
            // 利用 payload1 的前置空间（已预留 10 字节）做临时拼接
            if (payload1 && payload1->pos >= p0_len) {
                payload1->pos -= p0_len;
                memcpy(ITEM2BUF(payload1) + payload1->pos,
                       ITEM2BUF(payload0) + payload0->pos, p0_len);
                msg = ITEM2BUF(payload1) + payload1->pos;
            } else {
                // 极罕见：前置空间不够，分配临时缓冲
                buf16_item_t *tmp = alloc_buf16(BUF_FLAGS(buffer_sz_flag(total_len + 1), 0));
                if (!tmp) return;
                memcpy(ITEM2BUF(tmp), ITEM2BUF(payload0) + payload0->pos, p0_len);
                if (p1_len) memcpy(ITEM2BUF(tmp) + p0_len, ITEM2BUF(payload1) + payload1->pos, p1_len);
                ((char*)ITEM2BUF(tmp))[total_len] = '\0';
                tmp->pos = 0; tmp->len = total_len;
                wss_handle_text(client, ITEM2BUF(tmp), total_len);
                free_buf16(tmp);
                return;
            }
        }
        wss_handle_text(client, msg, total_len);

    } else { // WS_OP_BINARY
        uint8_t *data = NULL;
        uint16_t len  = 0;

        if (!payload0 || payload0->len == payload0->pos) {
            data = ITEM2BUF(payload1) + payload1->pos;
            len  = payload1->len - payload1->pos;
        } else {
            uint16_t p0_len = payload0->len - payload0->pos;
            uint16_t p1_len = payload1 ? (payload1->len - payload1->pos) : 0;
            len = p0_len + p1_len;
            if (payload1 && payload1->pos >= p0_len) {
                payload1->pos -= p0_len;
                memcpy(ITEM2BUF(payload1) + payload1->pos,
                       ITEM2BUF(payload0) + payload0->pos, p0_len);
                data = ITEM2BUF(payload1) + payload1->pos;
            } else {
                // 极罕见：前置空间不够
                buf16_item_t *tmp = alloc_buf16(BUF_FLAGS(buffer_sz_flag(len + 1), 0));
                if (!tmp) return;
                memcpy(ITEM2BUF(tmp), ITEM2BUF(payload0) + payload0->pos, p0_len);
                if (p1_len) memcpy(ITEM2BUF(tmp) + p0_len, ITEM2BUF(payload1) + payload1->pos, p1_len);
                tmp->pos = 0; tmp->len = len;
                wss_handle_binary(client, tmp, ITEM2BUF(tmp), len);
                free_buf16(tmp);
                return;
            }
        }
        wss_handle_binary(client, payload1, data, len);
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

        print("W:", LA_F("[W] RPC timeout: sid=%u (ses_id=%u)\n", LA_F199, 199), sid, s->base.session_id);
        wss_session_send_rpc_code(s, sid, P2P_RPC_ERR_TIMEOUT);
    }
}

///////////////////////////////////////////////////////////////////////////////
