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

///////////////////////////////////////////////////////////////////////////////

/* 判断 client 是否在线（已注册且 WS 握手完成，ws_ctx 非空即握手已完成） */
static inline bool wss_client_online(const wss_client_t *c) {
    return c && c->base.fd != P_INVALID_SOCKET && c->ws_ctx;
}

/* 写入 n 字节（可绕回），返回 0=成功, -1=空间不足 */
static inline int wss_sync_write(wss_session_t *s, const uint8_t *src, size_t n) {
    if (n > WSS_SYNC_PAYLOAD_MAX - s->sync_len) return -1;
    if (!s->sync_buf) {
        s->sync_buf = alloc_buffer(BUF_FLAG_2048(0));
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

// 将 src_s 的 ring buffer 内容转发到 dst_c（经由 dst_s），并向 src_s 的 client 回复 confirm
//
// 协议约定（流式传输，无需 heap 分配）：
//   SYNC <dst_sid>\n<line>   — 发送一行 sync 数据（\n 作为行分隔）
//   SYNC <dst_sid>\n\n       — fin mark，表示本批次 sync 数据发送完毕
//
// 调用前提：dst_c 在线且 wslay 队列空闲
static void wss_flush_sync(wss_session_t *src_s, wss_session_t *dst_s, wss_client_t *dst_c) {
    size_t cached = src_s->sync_len;
    if (!cached) return;

    uint32_t dst_sid = dst_s->base.session_id;
    uint32_t src_sid = src_s->base.session_id;

    // 固定栈帧：帧头 "SYNC <dst_sid>\n" + 最多一行内容 + NUL
    char frame[P2P_WSS_CMD_SYNC_SZ + 12 + 1 + WSS_SYNC_PAYLOAD_MAX + 1];
    int hdr_len = snprintf(frame, sizeof(frame), P2P_WSS_CMD_SYNC_FMT, dst_sid);

    // 逐行扫描 ring buffer，每行发送一个 WebSocket text frame
    size_t remaining = cached;
    size_t pos       = src_s->sync_head;    /* pos 始终 < WSS_SYNC_PAYLOAD_MAX */
    const char *data = (const char*)ITEM2BUF(src_s->sync_buf);

    while (remaining > 0) {
        // 在 ring buffer 中查找下一个 \n（行分隔符）
        size_t line_len = 0;
        size_t scan     = pos;
        bool   has_nl   = false;

        for (size_t i = 0; i < remaining; i++) {
            if (data[scan] == '\n') { has_nl = true; break; }
            line_len++;
            scan = (scan + 1) % WSS_SYNC_PAYLOAD_MAX;
        }
        if (!has_nl) line_len = remaining;

        // 将行内容（可能绕回）拷贝到帧头后面
        size_t to_end = WSS_SYNC_PAYLOAD_MAX - pos;
        if (to_end >= line_len) {
            memcpy(frame + hdr_len, data + pos, line_len);
        } else {
            memcpy(frame + hdr_len,          data + pos, to_end);
            memcpy(frame + hdr_len + to_end, data,       line_len - to_end);
        }
        frame[hdr_len + line_len] = '\0';
        ws_send_text((ws_client_t*)dst_c, frame);

        size_t advance = line_len + (has_nl ? 1u : 0u);
        pos        = (pos + advance) % WSS_SYNC_PAYLOAD_MAX;
        remaining -= advance;
    }

    // 发送 fin mark: "SYNC <dst_sid>\n\n"
    frame[hdr_len]     = '\n';
    frame[hdr_len + 1] = '\0';
    ws_send_text((ws_client_t*)dst_c, frame);

    // 清空 ring buffer，释放动态内存
    free_buffer(src_s->sync_buf);
    src_s->sync_buf  = NULL;
    src_s->sync_head = src_s->sync_len = 0;

    // 通知 src 端已转发确认
    char confirm[48];
    snprintf(confirm, sizeof(confirm), P2P_WSS_RSP_SYNC_CONFIRM_FMT, src_sid, cached);
    ws_send_text((ws_client_t*)src_s->base.client, confirm);

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u (%zu bytes)\n", LA_F167, 167),
          "SYNC", src_sid, dst_sid, cached);
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

// 服务器生成 RPC 错误 RESP（二进制帧）
// 格式：[P2P_WSS_BIN_RESP][session_id(4)][sid(2)][code(1)]
static void wss_session_send_rpc_code(wss_session_t *s, uint16_t sid, uint8_t code) {
    wss_client_t *c = (wss_client_t*)s->base.client;
    if (!wss_client_online(c)) return;

    uint8_t buf[P2P_WSS_BIN_RPC_MIN_SZ];
    buf[0] = P2P_WSS_BIN_RSP;
    nwrite_l(buf + 1, s->base.session_id);
    nwrite_s(buf + 1 + P2P_SESS_ID_SZ, sid);
    buf[1 + P2P_SESS_ID_SZ + 2] = code;
    ws_send_data((ws_client_t*)c, buf, sizeof(buf));
}

//-----------------------------------------------------------------------------

// 释放会话：通知对端 FIN + 清理 RPC + 调用共享 free_session_base
static void wss_free_session(session_t *s) {
    if (!s) return;

    wss_session_t *ws_s = (wss_session_t*)s;
    if (ws_s->sync_buf) { free_buffer(ws_s->sync_buf); ws_s->sync_buf = NULL; }
    ws_s->sync_head = ws_s->sync_len = 0;
    ws_s->sync_sending = false;

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
            wss_session_send_rpc_code(peer_s, peer_s->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
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
wss_free_client(wss_client_t *client) {

    // if (!and_free) {
    //     client->base.last_active = P_tick_ms();
    //
    //     // 通知所有配对对端
    //     for (session_t *s = client->base.sessions; s; s = s->next) {
    //         if (!PEER_ONLINE(s)) continue;
    //
    //         wss_session_t *peer_s = (wss_session_t*)s->peer;
    //         wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    //         if (wss_client_online(peer_c)) {
    //             char buf[16];
    //             snprintf(buf, sizeof(buf), "FIN %u", peer_s->base.session_id);
    //             ws_send_text((ws_client_t*)peer_c, buf);
    //         }
    //     }
    //
    //     if (client->base.sessions) return;     /* 有会话，保留等重连 */
    // }

    free_client(&client->base);
}

///////////////////////////////////////////////////////////////////////////////

// 处理 SYNC0 消息：创建/恢复会话
static void wss_handle_syn0(wss_client_t *client, const char *remote_peer_id,
                            uint8_t *payload, size_t payload_len) {
    const char *PROTO = "SYN0";

    if (!*remote_peer_id) {
        print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), PROTO);
        ws_send_text((ws_client_t*)client, "SYNC0 FAIL invalid remote id");
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
    if (side < E_NONE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, (const char *)payload, side);
        ws_send_text((ws_client_t*)client, "SYNC0 FAIL internal");
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
    // + 否则如果对端在线，则后面会直接启动双方 sync0 同步
    if (!remote_s) {

        { char _b[8+P2P_PEER_ID_MAX+12+8]; snprintf(_b,sizeof(_b),"SYNC0 %s %u offline",remote_peer_id,local_s->base.session_id); ws_send_text((ws_client_t*)client,_b); }

        print("I:", LA_F("%s: '%s' -> '%s' created (id=%u, peer_zombie)\n", LA_F63, 63),
              PROTO, client->base.local_peer_id, remote_peer_id, local_s->base.session_id);

    }
    // 对端已在线，启动双方 sync0 同步
    else {

        // 建立双向引用关系
        if (!local_s->base.peer) local_s->base.peer = &remote_s->base;
        if (!remote_s->base.peer) remote_s->base.peer = &local_s->base;

        //-------

        { char _b[8+P2P_PEER_ID_MAX+12+8]; snprintf(_b,sizeof(_b),"SYNC0 %s %u online",remote_peer_id,local_s->base.session_id); ws_send_text((ws_client_t*)client,_b); }
        // 将 remote 端积压数据流式推送给 client（含 SYNC confirm → remote_c）
        if (remote_s->sync_len)
            wss_flush_sync(remote_s, local_s, client);

        //-------

        wss_client_t *remote_c = (wss_client_t*)remote_s->base.client;
        { char _b[8+P2P_PEER_ID_MAX+12+8]; snprintf(_b,sizeof(_b),"SYNC0 %s %u online",client->base.local_peer_id,remote_s->base.session_id); ws_send_text((ws_client_t*)remote_c,_b); }
        // 将 local 端积压数据流式推送给 remote_c（含 SYNC confirm → client）
        if (local_s->sync_len)
            wss_flush_sync(local_s, remote_s, remote_c);

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
        ws_send_text((ws_client_t*)client, buf);
        return;
    }

    // 已有数据在传输中：新数据留在 ring buffer，等 send_complete 触发下一批
    if (session->sync_sending) return;

    // 冷启动：当前无数据在传输，尝试立即转发
    if (!PEER_ONLINE(&session->base)) return;
    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) return;

    session->sync_sending = true;
    wss_flush_sync(session, peer_s, peer_c);
}

// 处理 FIN 消息：客户端主动断开会话
static void wss_handle_fin(wss_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: '%s' ses_id=%u\n", LA_F45, 45),
          PROTO, session->base.client->local_peer_id, session->base.session_id);

    wss_free_session(&session->base);
}

// 处理 PKT — P2P 数据包中继（重写 session_id，透传 payload）
// payload: [P2P_WSS_BIN_PKT][session_id(P2P_SESS_ID_SZ)][P2P hdr(4)][payload(N)]
static void wss_handle_pkt(wss_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "PKT";

    if (len < P2P_WSS_BIN_PKT_MIN_SZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) return;

    print("V:", LA_F("%s: sid=%u -> peer_sid=%u, data_len=%zu\n", LA_F168, 168),
          PROTO, session->base.session_id, peer_s->base.session_id, len - P2P_WSS_BIN_PKT_MIN_SZ);

    // 就地重写 session_id 为请求方的 session_id，并转发到请求方
    // + ws 内部会将 payload 数据进行 copy-out
    uint8_t* ptr = payload + 1;
    nwrite_l(ptr, peer_s->base.session_id);
    ws_send_data((ws_client_t*)peer_c, payload, len);
}

// 处理 REQ — RPC 请求转发
// payload: [P2P_WSS_BIN_REQ][session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][payload(N)]
static void wss_handle_req(wss_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "REQ";

    if (len < P2P_WSS_BIN_RPC_MIN_SZ) {
        print("E:", LA_F("%s: bad frame len=%zu\n", LA_F155, 155), PROTO, len);
        return;
    }

    uint8_t* ptr = payload + 1 + P2P_SESS_ID_SZ;
    uint16_t sid = nget_s(ptr);
    uint8_t  msg = payload[P2P_WSS_BIN_RPC_MIN_SZ-1];
    int data_len = (int)len - (int)P2P_WSS_BIN_RPC_MIN_SZ;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, session->base.client->local_peer_id, sid, msg, data_len);

    wss_session_t *peer_s = (wss_session_t*)session->base.peer;
    wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
    if (!wss_client_online(peer_c)) {
        print("W:", LA_F("%s: peer offline, sending error resp\n", LA_F64, 64), PROTO);
        wss_session_send_rpc_code(session, sid, P2P_RPC_ERR_PEER_OFF);
        return;
    }

    if (session->rpc_pending_sid) {
        print("W:", LA_F("%s: rpc busy (pending sid=%u)\n", LA_F67, 67), PROTO, session->rpc_pending_sid);
        wss_session_send_rpc_code(session, sid, P2P_RPC_ERR_TIMEOUT);
        return;
    }

    ptr = payload + 1;
    nwrite_l(ptr, peer_s->base.session_id);
    ws_send_data((ws_client_t*)peer_c, payload, len);

    // 转发 REQ 到对端，记录 pending sid（等 RSP 回来才解锁）
    session->rpc_pending_sid = sid;
    session->rpc_sent_time   = P_tick_ms();
    wss_pending_enqueue_rpc(session);
}

// 处理 RESP — RPC 响应转发
// payload: [P2P_WSS_BIN_RSP][session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][payload(N)]
static void wss_handle_rsp(wss_session_t *session, uint8_t *payload, uint16_t len) {
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

    // 就地重写 session_id 为请求方的 session_id，并转发到请求方
    // + ws 内部会将 payload 数据进行 copy-out
    ptr = payload + 1;
    nwrite_l(ptr, peer_s->base.session_id);
    ws_send_data((ws_client_t*)peer_c, payload, len);

    // 解锁 rpc_pending_sid（RPC 生命周期完成），释放 pending 状态
    wss_pending_remove_rpc(peer_s);
    peer_s->rpc_pending_sid = 0;
}

//-----------------------------------------------------------------------------

// 处理 WSS 模式信令（WebSocket 文本帧）
// !! wslay 并未确保 msg 以 \0 结尾
void wss_handle_message(wss_client_t *client, const uint8_t *msg, size_t len) {
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
            ws_send_text((ws_client_t*)client, "REG FAIL duplicate reg");
            goto error_proto;
        }

        char *remote_id = (char*)msg + P2P_WSS_CMD_REG_SZ;

        char *inst_id = strrchr(remote_id, ' ');
        if (!inst_id || inst_id == remote_id) {
            print("E:", LA_F("%s: invalid REG format\n", LA_F160, 160), PROTO);
            ws_send_text((ws_client_t*)client, "REG FAIL invalid instance_id");
            goto error_proto;
        }

        ln_trim;
        *inst_id++ = '\0';

        if (!remote_id[0] || strlen(remote_id) > P2P_PEER_ID_MAX) {
             print("E:", LA_F("%s: invalid peer id\n", LA_F96, 96), PROTO);
                   ws_send_text((ws_client_t*)client, "REG FAIL empty peer id");
            goto error_proto;
        }
        uint32_t instance_id = (uint32_t)strtoul(inst_id, NULL, 10);
        if (instance_id == 0) {
            ws_send_text((ws_client_t*)client, "REG FAIL invalid instance id");
            goto error_proto;
        }

        wss_client_t *reg = (wss_client_t*)find_client(remote_id);
        if (reg) { assert(reg != client);

            if (resident_client(&reg->base, PROTO_WSS, instance_id, &client->base)) {
                client = reg;
                print("I:", LA_F("%s: '%s' reconnected & reactive (inst=%u)\n", LA_F98, 98),
                      PROTO, client->base.local_peer_id, client->base.instance_id);
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
            ws_send_text((ws_client_t*)client, ok);
        }

//        // 通知已配对会话双方 + 转发预缓存负载（重连场景）
//        for (session_t *s = client->base.sessions; s; s = s->next) {
//            wss_session_t *ws_s = (wss_session_t*)s;
//            if (!PEER_ONLINE(s)) continue;
//
//            wss_session_t *peer_s = (wss_session_t*)s->peer;
//            wss_client_t  *peer_c = (wss_client_t*)peer_s->base.client;
//            if (!wss_client_online(peer_c)) continue;
//
//            int ws_confirmed   = (int)ws_s->sync_len;
//            int peer_confirmed = (int)peer_s->sync_len;
//
//            wss_send_sync0(peer_c, client->base.local_peer_id, peer_s->base.session_id, true, ws_s);
//            ws_s->sync_head = ws_s->sync_len = 0;
//
//            wss_send_sync0(client, peer_c->base.local_peer_id, s->session_id, true, peer_s);
//            peer_s->sync_head = peer_s->sync_len = 0;
//
//            if (ws_confirmed)
//                wss_session_send_sync0_confirm(client, peer_c->base.local_peer_id, s->session_id, ws_confirmed);
//            if (peer_confirmed)
//                wss_session_send_sync0_confirm(peer_c, client->base.local_peer_id, peer_s->base.session_id, peer_confirmed);
//        }
        return;
    }

    if (!client->base.local_peer_id[0]) {
        print("E:", LA_F("%s: rejected for not reg\n", LA_F147, 147), (char*)msg);
        goto error_proto;
    }

    // PROTO: OFF
    // + 主动下线，立即释放服务器资源
    if (strcmp((char*)msg, P2P_WSS_CMD_OFF) == 0) {
        print("I:", LA_F("%s: '%s'\n", LA_F72, 72), "OFF",
              client->base.local_peer_id);
        wss_free_client(client, true);
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

        char *sid = (char*)msg + P2P_WSS_CMD_SYNC_SZ;
        ln_trim;

        uint32_t session_id = (uint32_t)strtoul(sid, NULL, 10);
        session_t *s = find_session(session_id);
        if (!s || s->client != &client->base) {
            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "FIN", session_id, client->base.local_peer_id);
            return;
        }

        wss_handle_fin((wss_session_t*)s);
        return;
    }

    // SYNC <session_id>\n<payload>
    if (strncmp((char*)msg, P2P_WSS_CMD_SYNC, P2P_WSS_CMD_SYNC_SZ) == 0) {

        char *sid = (char*)msg + P2P_WSS_CMD_SYNC_SZ;
        uint8_t *payload = (uint8_t*)ln+1;
        ln_trim;

        uint32_t session_id = (uint32_t)strtoul(sid, NULL, 10);
        session_t *s = find_session(session_id);
        if (!s || s->client != &client->base) {
            print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148),
                  "SYNC", session_id, client->base.local_peer_id);
            ws_send_text((ws_client_t*)client, "SYNC FAIL unknown session");
            return;
        }

        wss_handle_sync((wss_session_t*)s, payload, len - (size_t)(payload - msg));
        return;
    }

error_internal:
error_proto:
    print("V:", LA_F("unknown msg from '%s': %.32s\n", LA_F203, 203),
          client->base.local_peer_id, msg);
}

// 处理 WSS 模式信令（WebSocket 二进制帧）— PKT 中继 + RPC
void wss_handle_data(wss_client_t *client, const uint8_t *data, size_t len) {
    assert(client->base.proto == PROTO_WSS);

    if (len < 1 + P2P_SESS_ID_SZ) return;

    client->base.last_active = P_tick_ms();

    uint8_t  type       = data[0];
    uint8_t* ptr        = (uint8_t*)data + 1;
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
            ptr = (uint8_t*)data + 1 + P2P_SESS_ID_SZ;
            wss_session_send_rpc_code(ws_s, nget_s(ptr), P2P_RPC_ERR_PEER_OFF);
        }
        return;
    }

    switch (type) {
    case P2P_WSS_BIN_PKT:
        wss_handle_pkt(ws_s, (uint8_t*)data, len); break;
    case P2P_WSS_BIN_REQ:
        wss_handle_req(ws_s, (uint8_t*)data, len); break;
    case P2P_WSS_BIN_RSP:
        wss_handle_rsp(ws_s, (uint8_t*)data, len); break;
    default:
        print("W:", LA_F("BIN: unknown type=0x%02x from '%s'\n", LA_F149, 149),
              type, client->base.local_peer_id);
        break;
    }
}

bool
wss_handle_send_complete(ws_client_t *client) {
    wss_client_t *wss = (wss_client_t*)client;
    bool has_pending = false;

    // client 的 wslay 队列刚排空，检查每条会话的对端是否有积压数据需要转发到本 client
    for (session_t *s = wss->base.sessions; s; s = s->next) {
        if (!PEER_ONLINE(s)) continue;

        wss_session_t *local_s = (wss_session_t*)s;
        wss_session_t *peer_s  = (wss_session_t*)s->peer;
        wss_client_t  *peer_c  = (wss_client_t*)peer_s->base.client;

        // peer_s 有数据正在传输中（sync_sending=true）
        if (!peer_s->sync_sending) continue;

        // 源端已离线：无法发 confirm，数据留在 ring buffer 等 SYNC0 online 重投
        if (!wss_client_online(peer_c)) {
            peer_s->sync_sending = false;
            continue;
        }

        // ring buffer 还有积压数据，继续转发下一批
        if (peer_s->sync_len > 0) {
            wss_flush_sync(peer_s, local_s, wss);
            has_pending = true;
        } else peer_s->sync_sending = false;       // 缓存已空，本轮传输完成，清除标志
    }

    // 返回 true 表示队列已空可清除 WANT_WRITE；false 表示已新增帧需继续发送
    return !has_pending;
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
