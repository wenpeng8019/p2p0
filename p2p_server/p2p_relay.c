//
// Created by 温朋 on 2026/4/18.
//
#define MOD_TAG "RELAY"

#include "p2p_relay.h"

#include "../src/p2p_signal_relay.h"

ARGS(relay);
ARGS(msg);

const char* PROTO_STR(uint8_t proto) {
    switch (proto)
    {
    case P2P_RLY_STA    : return "STA";
    case P2P_RLY_REG    : return "REG";
    case P2P_RLY_OFF    : return "OFF";
    case P2P_RLY_ALV    : return "ALV";
    case P2P_RLY_SYN0   : return "SYN0";
    case P2P_RLY_SYNC   : return "SYNC";
    case P2P_RLY_FIN    : return "FIN";
    case P2P_RLY_PKT    : return "PKT";
    case P2P_RLY_REQ    : return "REQ";
    case P2P_RLY_RSP    : return "RSP";
    default:
        return "UNKNOWN";
    }
}

#define RELAY_PEER(s)               ((relay_session_t*)PEER(s))
#define RELAY_CLIENT(s)             ((relay_client_t*)CLIENT(s))

#define CT_ERR_PROTOCOL             (CUSTOM_TCP_ERR_CUSTOM+0)      // 协议错误（未登录/非法状态）
#define CT_ERR_PEER_OFF             (CUSTOM_TCP_ERR_CUSTOM+1)      // 对端未完成 REG（未登录）或已 OFF（离线）
#define CT_ERR_UNREACHABLE          (CUSTOM_TCP_ERR_CUSTOM+2)      // 对方暂时不可达（对方已经 REG 但可能网络闪断或异常）
#define CT_ERR_INVALID              (CUSTOM_TCP_ERR_CUSTOM+3)      // 无效的参数或操作
#define CT_ERR_TIMEOUT              (CUSTOM_TCP_ERR_CUSTOM+4)      // 服务器转发请求超时
#define CT_ERR_BUSY                 (CUSTOM_TCP_ERR_CUSTOM+5)      // 会话忙（前一个转发尚未完成）

// 将内部 custom_tcp 错误码映射为线上协议状态码
// + CUSTOM_TCP_ERR_* 和 P2P_RLY_ERR_* 通过 ERR(N-1) 对齐；非错误码（< CUSTOM_TCP_ERR_DISCONNECTED）直接透传
#define CT_CODE(c)  ((uint8_t)(c) >= CUSTOM_TCP_ERR_DISCONNECTED \
                     ? P2P_RLY_ERR((uint8_t)(c) - CUSTOM_TCP_ERR_DISCONNECTED) \
                     : (uint8_t)(c))

// RELAY RPC 待确认链表（按 rpc_sent_time 排序，队头最早超时）
static relay_session_t*             g_relay_rpc_pending_head = NULL;
static relay_session_t*             g_relay_rpc_pending_rear = NULL;

static uint8_t                      g_relay_fatal[sizeof(buffer_item_t) + sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0)];
static custom_tcp_ctx_t             g_ctx;

///////////////////////////////////////////////////////////////////////////////

// 将 session 加到 RPC 待确认链表尾部
static void relay_pending_enqueue_rpc(relay_session_t *session) {
    session->rpc_pending_next = (relay_session_t*)(void*)-1;
    if (g_relay_rpc_pending_rear) {
        g_relay_rpc_pending_rear->rpc_pending_next = session;
        g_relay_rpc_pending_rear = session;
    } else {
        g_relay_rpc_pending_head = session;
        g_relay_rpc_pending_rear = session;
    }
}

// 将 session 从 RPC 待确认链表移除
static void relay_pending_remove_rpc(relay_session_t *session) {
    if (!g_relay_rpc_pending_head || !session->rpc_pending_next) return;

    if (g_relay_rpc_pending_head == session) {
        g_relay_rpc_pending_head = session->rpc_pending_next;
        session->rpc_pending_next = NULL;
        if (g_relay_rpc_pending_head == (void*)-1) {
            g_relay_rpc_pending_head = NULL;
            g_relay_rpc_pending_rear = NULL;
        }
        return;
    }

    relay_session_t *prev = g_relay_rpc_pending_head;
    while (prev->rpc_pending_next != session) {
        assert(prev->rpc_pending_next == (void*)-1);
        prev = prev->rpc_pending_next;
    }
    prev->rpc_pending_next = session->rpc_pending_next;
    if (session->rpc_pending_next == (void*)-1) {
        g_relay_rpc_pending_rear = prev;
    }
    session->rpc_pending_next = NULL;
}

//-----------------------------------------------------------------------------

// client 级的状态应答
// + STATUS 包不走 session 队列，直接挂到 client 上
static bool relay_send_status(relay_client_t *client, uint8_t req_type, uint8_t status_code) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(0, 0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = req_type;
    p[1] = CT_CODE(status_code);

    // 借用一个临时空 session 结构是不合适的，这里直接用 tcp_send 尝试发送
    ct_client_send((ct_client_t*)client, buf_item, false);
    return true;
}

// client/session 临界级的状态应答
// + SYN0 专用状态，状态包不走 session 队列，直接挂到 client 上。因为此时尚未建立会话，需要通过携带 remote_peer_id 来标识哪个对端连接请求出错
static bool relay_send_syn0_status(relay_client_t *client, const char *remote_peer_id, uint8_t status_code) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(1, 0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = P2P_RLY_SYN0;
    p[1] = CT_CODE(status_code);
    memset(p + 2, 0, P2P_PEER_ID_MAX);
    if (remote_peer_id) strncpy((char*)(p + 2), remote_peer_id, P2P_PEER_ID_MAX);

    ct_client_send((ct_client_t*)client, buf_item, false);
    return true;
}

// session 级的状态应答
static bool relay_session_send_status(relay_session_t *session, uint8_t req_type, uint8_t status_code) {

    assert(session && session->base.client);

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        ct_client_error(&g_ctx, CT_CLIENT(session), CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(2, 0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + payload_len);

    uint8_t *payload = (uint8_t *)(hdr + 1);
    payload[0] = req_type;
    payload[1] = CT_CODE(status_code);
    nwrite_l(payload + 2, session->base.session_id);

    ct_session_send((ct_session_t*)session, buf_item);
    return true;
}


// 发送 syn0 ack
// payload: [target_name(32)][session_id(P2P_SESS_ID_SZ)][[0xFF]
static bool relay_session_send_syn0_off(relay_session_t *session, const char *target_name) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    uint16_t payload_len = P2P_RLY_SYN0_S2C_PSZ(0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYN0;
    hdr->size = htons(payload_len);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    memset(payload, 0, P2P_PEER_ID_MAX);
    strncpy((char*)payload, target_name, P2P_PEER_ID_MAX - 1);
    nwrite_l(payload + P2P_PEER_ID_MAX, session->base.session_id);

    payload[P2P_PEER_ID_MAX + P2P_SESS_ID_SZ] = 0xFF;

    ct_session_send((ct_session_t*)session, buf_item);
    return true;
}

// 发送 sync confirm
// payload: [session_id(P2P_SESS_ID_SZ)][sid(1)]
static bool relay_session_send_sync_confirm(relay_session_t *session, uint8_t sid) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    uint16_t payload_len = P2P_RLY_SYNC_CONFIRM_PSZ;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC;
    hdr->size = htons(payload_len);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_l(payload, session->base.session_id);
    payload[P2P_SESS_ID_SZ] = sid;

    ct_session_send((ct_session_t*)session, buf_item);
    return true;
}

// 发送 FIN 包，通知对端会话结束
// payload: [session_id(P2P_SESS_ID_SZ)]
static bool relay_session_send_fin(relay_session_t *session) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), "FIN");
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    p2p_relay_hdr_t* hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_FIN;
    hdr->size = htons(P2P_RLY_FIN_PSZ);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_FIN_PSZ);
    uint8_t* payload = (uint8_t*)(hdr+1);
    nwrite_l(payload, session->base.session_id);

    ct_session_send((ct_session_t*)session, buf_item);
    return true;
}

// 向 RPC 请求方发送应答 code
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)]
static bool relay_session_send_rpc_code(relay_session_t *session, uint16_t sid, uint8_t code) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("E:", LA_F("send failed(OOM)\n", LA_F100, 100));
        ct_client_error(&g_ctx, CT_CLIENT(session), CUSTOM_TCP_ERR_INTERNAL, true);
        return false;
    }

    uint16_t payload_len = P2P_RLY_RPC_MIN_PSZ;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_RSP;
    hdr->size = htons(payload_len);
    buf_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    nwrite_l(p, session->base.session_id);
    nwrite_s(p + P2P_SESS_ID_SZ, sid);
    p[P2P_SESS_ID_SZ + 2] = code;

    ct_session_send((ct_session_t*)session, buf_item);
    return true;
}

///////////////////////////////////////////////////////////////////////////////

// 对端 TCP 写入完成后的回调（通用，SYNC/PKT 通道均使用）
// PKT：移出队头，队满→写完才发 READY，启动下一项
// SYNC：TCP 写完但保留队头，将 refer 改为 REFER_ACK_PENDING 等待应用层 ACK
static void relay_handle_peer_sent(ct_session_t *ct_session, buffer_item_t *buf_item) {

    assert(PEER_ONLINE(ct_session));

    relay_session_t *session = (relay_session_t*)ct_session;

    p2p_relay_hdr_t *p_hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    if (p_hdr->type == P2P_RLY_PKT) {

        // PKT：写入完成，移出队头
        assert(session->pkt_peer_send_cnt > 0 && session->pkt_peer_send[0] == buf_item);
        buf_item->refer = NULL;  // 通知调用方可释放

        // 移出队头（RELAY_PEER_Q_MAX == 2）
        session->pkt_peer_send[0] = session->pkt_peer_send[1];
        session->pkt_peer_send[1] = NULL;
        session->pkt_peer_send_cnt--;

        // 启动下一项（如有）
        if (session->pkt_peer_send_cnt > 0) {
            session->pkt_peer_send[0]->refer = session;
            ct_session_send(CT_PEER(session), session->pkt_peer_send[0]);
        }
        // 队列从满 → 非满：之前因队满未发 READY，现在补发
        if (session->pkt_peer_send_cnt == RELAY_PEER_Q_MAX - 1)
            relay_session_send_status(session, P2P_RLY_PKT, P2P_RLY_CODE_READY);

    } else { // SYNC/SYN0：TCP 写入完成，但队头继续持有 item 等待应用层 ACK

        assert(session->sync_peer_send_cnt > 0 && session->sync_peer_send[0] == buf_item);
        buf_item->refer = ITEM_REF_ACK_PENDING;
    }
}

// 清理一个通道的所有队列项，并将存活项转发给 dst
// arr[0] 若 TCP 写入中（refer!=NULL 且非 REFER_ACK_PENDING）：设 refer=NULL 让 sent-callback 释放
// 其余（REFER_ACK_PENDING 或 refer==NULL）：直接 relay_session_send(dst, ...)
static void relay_ch_break_forward(buffer_item_t **arr, uint8_t *cnt, relay_session_t *dst) {
    for (uint8_t i = 0; i < *cnt; i++) {
        buffer_item_t *it = arr[i]; arr[i] = NULL;
        if (it->refer != NULL && it->refer != ITEM_REF_ACK_PENDING) it->refer = NULL;
        else ct_session_send((ct_session_t*)dst, it);
    }
    *cnt = 0;
}

// 清理一个通道的所有队列项并释放
static void relay_ch_break_free(buffer_item_t **arr, uint8_t *cnt) {
    for (uint8_t i = 0; i < *cnt; i++) {
        buffer_item_t *it = arr[i]; arr[i] = NULL;
        if (it->refer != NULL && it->refer != ITEM_REF_ACK_PENDING) it->refer = NULL;
        else free_buffer(it);
    }
    *cnt = 0;
}

// 停止/终止会话
static void relay_session_break(ct_session_t *ct_session, ct_session_t *ct_peer, break_mode_e break_mode) {

    // 前提是双方在线
    assert(PEER_ONLINE(ct_session) && PEER_ONLINE(ct_peer));

    relay_session_t *session = (relay_session_t*)ct_session, *peer = (relay_session_t*)ct_peer;

    // 存在对端发起的 req
    if (peer->rpc_pending_sid) {
        relay_session_send_rpc_code(peer, peer->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
        relay_pending_remove_rpc(peer);
        peer->rpc_sent_time = 0;
        peer->rpc_pending_sid = 0;
    }

    // 存在本端发起的 req
    if (session->rpc_pending_sid) {

        // 如果依然可以向本地发送数据
        if (break_mode != SESS_BREAK_TERM)
            relay_session_send_rpc_code(session, session->rpc_pending_sid, P2P_RPC_ERR_BREAK);

        relay_pending_remove_rpc(session);
        session->rpc_sent_time = 0;
        session->rpc_pending_sid = 0;
    }

    // 如果只是（unreachable）暂停通讯的状态（即会话和数据完整性不会被破坏）
    if (break_mode == SESS_BREAK_STOP) return;

    // 解除本端两个通道的 in-flight 引用，转发队列剩余项给对端
    relay_ch_break_forward(session->sync_peer_send, &session->sync_peer_send_cnt, peer);
    relay_ch_break_forward(session->pkt_peer_send,  &session->pkt_peer_send_cnt,  peer);

    // 向对端发送最后一个 FIN 包
    relay_session_send_fin(peer);

    // 如果依然可以向本地发送数据
    if (break_mode == SESS_BREAK_CLOSE) {
        relay_ch_break_forward(peer->sync_peer_send, &peer->sync_peer_send_cnt, session);
        relay_ch_break_forward(peer->pkt_peer_send,  &peer->pkt_peer_send_cnt,  session);
    }
    else {
        relay_ch_break_free(peer->sync_peer_send, &peer->sync_peer_send_cnt);
        relay_ch_break_free(peer->pkt_peer_send,  &peer->pkt_peer_send_cnt);
    }

    session->last_sid = peer->last_sid = 0;
    session->rpc_last_sid = peer->rpc_last_sid = 0;
}


///////////////////////////////////////////////////////////////////////////////

// 处理 SYN0 消息（首次同步）
// payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
// 注：SYN0 的 sid=0 固定，省略传输；后续 SYNC 从 sid=1 起始
static void relay_handle_syn0(relay_client_t *client, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYN0";

    if (len < P2P_RLY_SYN0_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_send_syn0_status(client, (const char *)payload, CT_ERR_PROTOCOL);
        return;
    }

    uint8_t cand_count = payload[P2P_PEER_ID_MAX];
    uint32_t expect_len = P2P_RLY_SYN0_PSZ(cand_count);
    if (len != expect_len) {
        print("E:", LA_F("%s: bad payload(cnt=%d, len=%u, expected=%u)\n", LA_F39, 39), PROTO, cand_count, len, expect_len);
        relay_send_syn0_status(client, (const char *)payload, CT_ERR_PROTOCOL);
        return;
    }
    payload[P2P_PEER_ID_MAX] = '\0';

    // 构建会话
    relay_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, (const char *)payload,
                       (session_t**)&local_s, (session_t**)&remote_s,
                       sizeof(relay_session_t));
    if (side == E_OUT_OF_MEMORY) {
        print("E:", LA_F("%s: build session to '%s' failed(OOM)", LA_F157, 157), PROTO, (const char *)payload);
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }
    if (side < E_NONE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, (const char *)payload, side);
        relay_send_syn0_status(client, (const char *)payload, CT_ERR_PROTOCOL);
        return;
    }

    // 截断候选计数
    if (cand_count > MAX_CANDIDATES)
        cand_count = MAX_CANDIDATES;

    print("V:", LA_F("%s: local='%s', remote='%s', online=%d, cands=%d\n", LA_F55, 55),
           PROTO, client->base.local_peer_id, (const char *)payload, remote_s ? 1 : 0, cand_count);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 构造 syn0 协议包
    // + 如果存在初始 sync 数据，则优先使用零拷贝 forward 方案（即直接转发客户端的 recv_buf）
    buffer_item_t *syn0_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        syn0_item = alloc_buffer(BUF_FLAG_MTU(0));
        if (!syn0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
            return;
        }

        // memmove 为 session_id 腾出空间，替换 target_name → source_name
        memmove(payload + P2P_PEER_ID_MAX + P2P_SESS_ID_SZ,
                payload + P2P_PEER_ID_MAX,
                1 + cand_count * (int)sizeof(p2p_candidate_t));
        memset(payload, 0, P2P_PEER_ID_MAX);
        strncpy((char*)payload, client->base.local_peer_id, P2P_PEER_ID_MAX);

        hdr = (p2p_relay_hdr_t *)client->recv_buf;
        assert(hdr->type == P2P_RLY_SYN0);
        hdr->size = htons(P2P_RLY_SYN0_S2C_PSZ(cand_count));
        BUF2ITEM(client->recv_buf)->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_SYN0_S2C_PSZ(cand_count));

        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(syn0_item);
        client->recv_len = 0;
        syn0_item = item;
    }
    else {

        syn0_item = alloc_buffer(BUF_FLAG_512(0));
        if (!syn0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(syn0_item));
        hdr->type = P2P_RLY_SYN0;
        hdr->size = htons(P2P_RLY_SYN0_S2C_PSZ(0));
        syn0_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_SYN0_S2C_PSZ(0));
        uint8_t *p = (uint8_t*)(hdr + 1);
        memset(p, 0, P2P_PEER_ID_MAX);
        strncpy((char*)p, client->base.local_peer_id, P2P_PEER_ID_MAX);
        p[P2P_PEER_ID_MAX + P2P_SESS_ID_SZ] = 0; // cand_count = 0
    }

    // 如果对方不在线，立刻返回 syn0 offline
    // + 并将 syn0 包缓存到 local_s->sync_peer_send[0]，后面会直接启动双方 syn0 同步
    if (!remote_s) {

        if (local_s->sync_peer_send_cnt > 0) {
            free_buffer(local_s->sync_peer_send[0]);
            local_s->sync_peer_send[0] = NULL;
            local_s->sync_peer_send_cnt = 0;
        }

        local_s->sync_peer_send[0] = syn0_item;
        local_s->sync_peer_send_cnt = 1;
        // refer 保持 NULL：对端上线时再转发，refer 将由 relay_handle_syn0 在线路径设置
        relay_session_send_syn0_off(local_s, (const char *) payload);

        print("I:", LA_F("%s: peer '%s' offline, cached cands=%d\n", LA_F163, 163),
              PROTO, (const char *)payload, cand_count);
    }
    // 对端已在线，启动双方 syn0 同步
    else {

        // 建立双向引用关系
        if (!local_s->base.peer) local_s->base.peer = (session_t*)remote_s;
        if (!remote_s->base.peer) remote_s->base.peer = (session_t*)local_s;

        //-------

        assert(local_s->sync_peer_send_cnt == 0 && local_s->pkt_peer_send_cnt == 0);  // 本端不可能存在挂起的 SYN0

        // 本端 SYN0 转发给对端前，需要写入对端 session_id（位于 source_name 之后）
        uint8_t* sid = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;
        nwrite_l(sid, remote_s->base.session_id);

        // 添加到本端队列，设置 refer 触发传完后的 complete 回调
        syn0_item->refer = local_s;
        local_s->sync_peer_send[0] = syn0_item;
        local_s->sync_peer_send_cnt = 1;
        ct_session_send((ct_session_t*)remote_s, syn0_item);

        //-------

        assert(remote_s->sync_peer_send_cnt == 1 && remote_s->sync_peer_send[0]->refer == NULL); // 对端肯定存在缓存的 SYN0

        buffer_item_t *remote_syn0_item = remote_s->sync_peer_send[0];
        hdr = (p2p_relay_hdr_t *)ITEM2BUF(remote_syn0_item);

        // 对端 SYN0 转发给本端前，需要写入本端 session_id（位于 source_name 之后）
        sid = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;
        nwrite_l(sid, local_s->base.session_id);

        // 添加到本端发送队列，也要设置 refer
        remote_syn0_item->refer = remote_s;
        ct_session_send((ct_session_t*)local_s, remote_syn0_item);

        //-------

        // 双方 SYN0 互相转发完成；各自收到对方的 SYN0 即为隐式 ACK，无需单独发 confirm

        print("I:", LA_F("%s: %s <-> %s forward\n", LA_F48, 48),
              PROTO, client->base.local_peer_id, (const char *)payload);
    }
}

// 处理 FIN 消息（会话结束）
// payload: [session_id(P2P_SESS_ID_SZ)]
static void relay_handle_fin(relay_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: close ses_id=%u\n", LA_F158, 158), PROTO, session->base.session_id);

    // 向对端发送 FIN 说明本端已经关闭了连接
    // + 销毁本端的 sess（销毁操作会向对端发送 FIN）
    ct_close_session(&g_ctx, (ct_session_t*)session, true);
}

// 处理 SYNC 消息（候选同步 / C→S confirm）
// 上行: [session_id(P2P_SESS_ID_SZ)][sid(1)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
// confirm: [session_id(P2P_SESS_ID_SZ)][sid(1)][confirmed_count(1)]  ← 客户端确认收到服务器下发的 SYNC
static void relay_handle_sync(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC";

    if (len < P2P_RLY_SYNC_CONFIRM_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_PROTOCOL);
        return;
    }

    uint8_t sid = payload[P2P_SESS_ID_SZ];
    if (!sid) {
        print("E:", LA_F("%s: bad payload(sid=0)\n", LA_F156, 156), PROTO);
        relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_PROTOCOL);
        return;
    }

    if (!PEER_ONLINE(session)) {
        print("E:", LA_F("%s: ses_id=%u, peer offline, drop sid=%u\n", LA_F145, 145),
              PROTO, session->base.session_id, sid);
        relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_PEER_OFF);
        return;
    }

    // C→S confirm：客户端确认收到服务器下发（转发）的 SYNC，len 恰好等于 confirm 包大小
    if (len == P2P_RLY_SYNC_CONFIRM_PSZ) {

        print("V:", LA_F("%s: ses_id=%u, confirm sid=%u\n", LA_F165, 165),
              PROTO, session->base.session_id, sid);

        // 获取 peer（sync 的原始发送方）
        relay_session_t *peer = RELAY_PEER(session);
        if (peer->sync_peer_send_cnt > 0 && peer->sync_peer_send[0]->refer == ITEM_REF_ACK_PENDING) {

            free_buffer(peer->sync_peer_send[0]);
            peer->sync_peer_send[0] = peer->sync_peer_send[1];
            peer->sync_peer_send[1] = NULL;
            peer->sync_peer_send_cnt--;

            // 如果 peer sess 的 SYNC 队列不空，发送下一个 SYNC
            if (peer->sync_peer_send_cnt > 0) {
                peer->sync_peer_send[0]->refer = peer;
                ct_session_send((ct_session_t*)session, peer->sync_peer_send[0]);
            }
            // 队列从满→非满：B 之前因队满未收到 confirm，现在补发
            // sid 取队头项的 sid（B 发送的、当时因队满被推迟 confirm 的那一项）
            if (peer->sync_peer_send_cnt == RELAY_PEER_Q_MAX - 1) {
                uint8_t pending_sid = ((uint8_t*)ITEM2BUF(peer->sync_peer_send[0]))[sizeof(p2p_relay_hdr_t) + P2P_SESS_ID_SZ];
                relay_session_send_sync_confirm(peer, pending_sid);
            }
        }
        return;
    }

    uint8_t cand_count = payload[P2P_SESS_ID_SZ + 1];
    uint16_t payload_sz = P2P_RLY_SYNC_PSZ(cand_count, false);
    if (len == payload_sz + 1u) {

        if (payload[payload_sz] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F38, 38), PROTO, payload[payload_sz]);
            relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_PROTOCOL);
            return;
        }
    }
    else if (len != payload_sz) {
        print("E:", LA_F("%s: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n", LA_F40, 40),
               PROTO, sid, (unsigned)cand_count, len, payload_sz);
        relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_PROTOCOL);
        return;
    }

    // 去重：重复包重发 confirm 即可，不重复转发
    if (sid == session->last_sid) {
        print("W:", LA_F("%s: ses_id=%u, dup sid=%u, resend confirm\n", LA_F145, 145),
            PROTO, session->base.session_id, sid);
        relay_session_send_sync_confirm(session, sid);
        return;
    }

    // 验证同步序的一致性
    if (!uint16_circle_newer(sid, session->last_sid)) {
        print("W:", LA_F("%s: ses_id=%u, stale sid=%u (last=%u), drop\n", LA_F145, 145),
            PROTO, session->base.session_id, sid, session->last_sid);
        relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_PROTOCOL);
        return;
    }

    // 忙检查（控速）
    if (session->sync_peer_send_cnt >= RELAY_PEER_Q_MAX) {
        print("W:", LA_F("%s: ses_id=%u busy (pending %s)\n", LA_F145, 145),
              PROTO, session->base.session_id, "sync");
        relay_session_send_status(session, P2P_RLY_SYNC, CT_ERR_BUSY);
        return;
    }

    session->last_sid = sid;

    // SYN0 隐式 ACK：本端首个 SYNC 上行视为是对服务器下发的 SYN0 的确认
    relay_session_t *peer = RELAY_PEER(session);
    if (peer->sync_peer_send_cnt > 0 && peer->sync_peer_send[0]->refer == ITEM_REF_ACK_PENDING) {
        if (((p2p_relay_hdr_t*)ITEM2BUF(peer->sync_peer_send[0]))->type == P2P_RLY_SYN0) {

            free_buffer(peer->sync_peer_send[0]);
            peer->sync_peer_send[0] = peer->sync_peer_send[1];
            peer->sync_peer_send[1] = NULL;
            peer->sync_peer_send_cnt--;

            // 如果 peer sess 的 SYNC 队列不空，发送下一个 SYNC
            if (peer->sync_peer_send_cnt > 0) {
                peer->sync_peer_send[0]->refer = peer;
                ct_session_send((ct_session_t*)session, peer->sync_peer_send[0]);
            }
        }
    }

    print("V:", LA_F("%s: ses_id=%u, sid=%u, cands=%d\n", LA_F165, 165),
          PROTO, session->base.session_id, sid, cand_count);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync_item = alloc_buffer(BUF_FLAG_MTU(0));
        if (!sync_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
            return;
        }

        hdr = (p2p_relay_hdr_t *)client->recv_buf;
        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(sync_item);
        client->recv_len = 0;
        sync_item = item;
    }
    else {
        // cand_count==0 的 FIN 包，构造新转发包（保留 sid）
        sync_item = alloc_buffer(BUF_FLAG_512(0));
        if (!sync_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync_item));
        hdr->type = P2P_RLY_SYNC;
        hdr->size = htons(P2P_RLY_SYNC_PSZ(0, true));
        sync_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_SYNC_PSZ(0, true));

        uint8_t *p = (uint8_t*)(hdr+1) + P2P_SESS_ID_SZ;
        p[0] = sid;                     // 透传 sid
        p[1] = 0;                       // cand_count = 0
        p[2] = P2P_RLY_SYNC_FIN_MARKER;
    }

    // 交换写入对端的 session_id
    uint8_t *sid_ptr = (uint8_t *)(hdr + 1);
    nwrite_l(sid_ptr, session->base.peer->session_id);

    // 发送入队并在首次时冷启动发送
    session->sync_peer_send[session->sync_peer_send_cnt++] = sync_item;
    if (session->sync_peer_send_cnt == 1) {
        sync_item->refer = session;
        ct_session_send((ct_session_t*)peer, sync_item);
    }
    // 队列未满时立即 confirm；队满时等 A 的 confirm 释放槽后补发（流控）
    if (session->sync_peer_send_cnt < RELAY_PEER_Q_MAX)
        relay_session_send_sync_confirm(session, sid);
}

// 处理 PKT 消息（零拷贝转发）
// payload: session_id(P2P_SESS_ID_SZ)][P2P hdr(4)][data]
static void relay_handle_pkt(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "PKT";

    if (!PEER_ONLINE(session)) {
        print("E:", LA_F("%s: ses_id=%u, peer offline, drop pkt\n", 0, 0),
              PROTO, session->base.session_id);
        relay_session_send_status(session, P2P_RLY_PKT, CT_ERR_PEER_OFF);
        return;
    }

    // 忙检查（控速）
    if (session->pkt_peer_send_cnt >= RELAY_PEER_Q_MAX) {
        print("W:", LA_F("%s: ses_id=%u busy (pending %s)\n", LA_F145, 145),
              PROTO, session->base.session_id, "pkt");
        relay_session_send_status(session, P2P_RLY_PKT, CT_ERR_BUSY);
        return;
    }

    print("V:", LA_F("%s: ses_id=%u, data_len=%u\n", LA_F166, 166), PROTO,
          session->base.session_id, len - P2P_SESS_ID_SZ);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发：分配新 recv_buf，将当前 recv_buf 直接作为转发包
    buffer_item_t *new_recv = alloc_buffer(BUF_FLAG_MTU(0));
    if (!new_recv) {
        print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, session->base.peer->session_id);

    // 发送入队并在首次时冷启动发送
    session->pkt_peer_send[session->pkt_peer_send_cnt++] = buf_item;
    if (session->pkt_peer_send_cnt == 1) {
        buf_item->refer = session;
        ct_session_send(CT_PEER(session), buf_item);
    }
    // 如果队列未满，发送状态通知（可以继续发送）
    if (session->pkt_peer_send_cnt < RELAY_PEER_Q_MAX)
        relay_session_send_status(session, P2P_RLY_PKT, P2P_RLY_CODE_READY);
}

// 处理 RPC_REQ 消息（零拷贝转发）
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
static void relay_handle_req(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "REQ";

    if (len < P2P_RLY_RPC_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_session_send_status(session, P2P_RLY_REQ, CT_ERR_PROTOCOL);
        return;
    }

    uint8_t* ptr = payload + P2P_SESS_ID_SZ;
    uint16_t sid = nget_s(ptr);
    uint8_t  msg = ptr[2];
    uint16_t data_len = len - P2P_RLY_RPC_MIN_PSZ;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, client->base.local_peer_id, sid, msg, data_len);

    if (!sid) {
        print("E:", LA_F("%s: invalid sid=0\n", 0, 0), PROTO);
        relay_session_send_status(session, P2P_RLY_REQ, CT_ERR_INVALID);
        return;
    }

    if (session->rpc_last_sid && !uint16_circle_newer(sid, session->rpc_last_sid)) {
        print("E:", LA_F("%s: sid too old (got=%u, pending=%u), discarding\n", 0, 0),
              PROTO, sid, session->rpc_pending_sid);
        relay_session_send_status(session, P2P_RLY_REQ, CT_ERR_INVALID);
        return;
    }

    // 忙检查（控速）
    if (session->rpc_pending_sid) {
        print("E:", LA_F("%s: rpc busy (pending sid=%u)\n", LA_F67, 67), PROTO, session->rpc_pending_sid);
        relay_session_send_status(session, P2P_RLY_REQ, CT_ERR_BUSY);
        return;
    }

    // 检查对端是否可达
    if (!PEER_ONLINE(session) || !TCP_PEER_REACHABLE(session)) {
        print("E:", LA_F("%s: peer offline, sending error resp\n", LA_F64, 64), PROTO);
        relay_session_send_rpc_code(session, sid, P2P_RPC_ERR_PEER_OFF);
        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发：分配新 recv_buf，将当前 recv_buf 直接作为转发包
    buffer_item_t *new_recv = alloc_buffer(BUF_FLAG_MTU(0));
    if (!new_recv) {
        print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    session->rpc_last_sid = sid;

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, session->base.peer->session_id);
    buf_item->refer = NULL;
    ct_session_send(CT_PEER(session), buf_item);

    // 转发 REQ 到对端，记录 pending sid（等 RSP 回来才解锁）
    session->rpc_pending_sid = sid;
    session->rpc_sent_time = P_tick_ms();
    relay_pending_enqueue_rpc(session);
}

// 处理 RPC_RSP 消息（零拷贝转发）
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][data(N)]
static void relay_handle_rsp(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "RSP";

    if (len < P2P_RLY_RPC_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_session_send_status(session, P2P_RLY_RSP, CT_ERR_PROTOCOL);
        return;
    }

    uint8_t* ptr_sid = payload + P2P_SESS_ID_SZ;
    uint16_t sid  = nget_s(ptr_sid);
    uint8_t  code = payload[P2P_SESS_ID_SZ + 2];
    int data_len  = (int)len - (int)P2P_RLY_RPC_MIN_PSZ;

    print("V:", LA_F("%s: '%s' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, client->base.local_peer_id, sid, code, data_len);

    if (!sid) {
        print("W:", LA_F("%s: invalid sid=0\n", 0, 0), PROTO);
        relay_session_send_status(session, P2P_RLY_RSP, CT_ERR_INVALID);
        return;
    }

    if (!PEER_ONLINE(session)) {
        print("E:", LA_F("%s: ses_id=%u, peer offline, drop rsp\n", 0, 0),
              PROTO, session->base.session_id);
        return;
    }

    relay_session_t* peer = RELAY_PEER(session);

    // 验证 sid 与请求方 pending sid 一致
    if (peer->rpc_pending_sid != sid) {
        print("W:", LA_F("%s: sid mismatch (got=%u, pending=%u), discarding\n", LA_F68, 68),
              PROTO, sid, peer->rpc_pending_sid);
        return;
    }

    // peer unreachable 触发的 break 会清除其 rpc_pending_sid
    assert(TCP_PEER_REACHABLE(session));

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发
    buffer_item_t *new_recv = alloc_buffer(BUF_FLAG_MTU(0));
    if (!new_recv) {
        print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为请求方的 session_id，并转发到请求方
    nwrite_l(payload, peer->base.session_id);
    buf_item->refer = NULL;
    ct_session_send((ct_session_t*)peer, buf_item);

    // 解锁 rpc_pending_sid（RPC 生命周期完成），释放 pending 状态
    relay_pending_remove_rpc(peer);
    peer->rpc_pending_sid = 0;
    peer->rpc_sent_time = 0;
}

//-----------------------------------------------------------------------------

static uint32_t relay_resolve_payload_len(uint8_t* hdr_buf, uint16_t hdr_len) {
    assert(hdr_len == sizeof(p2p_relay_hdr_t));
    return ntohs(((p2p_relay_hdr_t*)hdr_buf)->size);
}

static int relay_handle_handshake(ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len, uint8_t *payload, uint16_t payload_len) {
    assert(hdr_len == sizeof(p2p_relay_hdr_t));
    assert(!client->base.local_peer_id[0]);

    // 握手阶段只支持 REG 请求（REG 本质就是握手请求）
    if (((p2p_relay_hdr_t*)hdr_buf)->type != P2P_RLY_REG) {
        print("E:", LA_F("%s: rejected for not reg\n", LA_F147, 147), PROTO_STR(((p2p_relay_hdr_t*)hdr_buf)->type));
        return CT_ERR_PROTOCOL;
    }

    const char* PROTO = "REG";

    // 处理 REG 消息：[name(32)][instance_id(4)]
    if (payload_len != P2P_RLY_REG_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, payload_len);
        return CT_ERR_PROTOCOL;
    }

    if (!*payload) {
        print("E:", LA_F("%s: invalid peer id\n", LA_F96, 96), PROTO);
        return CT_ERR_PROTOCOL;
    }

    uint8_t* ptr = payload + P2P_PEER_ID_MAX;
    uint32_t instance_id = nget_l(ptr);
    if (instance_id == 0) {
        print("E:", LA_F("%s: invalid instance id\n", LA_F94, 94), PROTO);
        return CT_ERR_PROTOCOL;
    }

    // 查找是否存在同名的已登录 client（断网重连场景）
    relay_client_t *reg = (relay_client_t*)find_client((char*)payload);
    if (reg) { assert((ct_client_t*)reg != client);

        if (TCP_HS_IS_HANDSHAKING(reg)) {
            client->last_error = CT_ERR_INVALID;
            print("E:", LA_F("%s: request simultaneously for '%s'\n", 0, 0), PROTO, reg->base.local_peer_id);
            return CT_ERR_PROTOCOL;
        }

        uint8_t* recv_buf = client->recv_buf; uint16_t recv_len = client->recv_len;
        client->recv_buf = NULL; client->recv_len = 0;

        // 如果 instance_id 一致（断网重连）
        if (resident_client(&reg->base, PROTO_RELAY, instance_id, &client->base)) {

            client = (ct_client_t*)reg;

            // SYNC/SYN0 的 ACK_PENDING 项：TCP 已写出但客户端未确认（连接已断无法确保到达）
            // 重置 refer 并重新入队，让新连接重新投递
            for (session_t *sess = client->base.sessions; sess; sess = sess->next) {
                relay_session_t *peer = RELAY_PEER(sess);
                if (PEER_VALID(peer) && peer->sync_peer_send_cnt > 0 &&
                    peer->sync_peer_send[0]->refer == ITEM_REF_ACK_PENDING) {
                    peer->sync_peer_send[0]->refer = peer;
                    ct_session_send((ct_session_t*)sess, peer->sync_peer_send[0]);
                }
            }

            ct_reactive_client(&g_ctx, (ct_client_t*)client);

            print("I:", LA_F("%s: '%s' reconnected & reactive (inst=%u)\n", LA_F98, 98), PROTO,
                   client->base.local_peer_id, client->base.instance_id);
        }
        // 将之前实例重置（强制旧连接失效），激活新实例
        else {
            print("I:", LA_F("%s: '%s' reconnected & renew (inst=%u)\n", LA_F153, 153), PROTO,
                  client->base.local_peer_id, client->base.instance_id);
        }

        client->recv_buf = recv_buf; client->recv_len = recv_len;
    }
    else {
        print("I:", LA_F("%s: '%s' new REG (inst=%u)\n", LA_F93, 93), PROTO,
              client->base.local_peer_id, client->base.instance_id);

        client->base.instance_id = instance_id;
        memcpy(client->base.local_peer_id, payload, P2P_PEER_ID_MAX);
        client->base.local_peer_id[P2P_PEER_ID_MAX] = '\0';

        // 添加到索引（激活 client 身份）
        identify_client(&client->base);
    }

    // 回复 REG ACK
    {
        // 就地修改 hdr_buf/payload 为 REG_ACK (复用 client->recv_buf 缓冲区)
        ((p2p_relay_hdr_t *)hdr_buf)->size = htons(P2P_RLY_REG_S2C_PSZ);
        BUF2ITEM(hdr_buf)->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_REG_S2C_PSZ);
        payload[0/* features */] = 0;
        if (ARGS_relay.i64) payload[0] |= P2P_RLY_FEATURE_RELAY;
        if (ARGS_msg.i64) payload[0] |= P2P_RLY_FEATURE_MSG;
        payload[1/* candidate_sync_max */] = (uint8_t)MAX_CANDIDATES;
    }

    return 0;
}

static void relay_handle_proto(ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len, uint8_t *payload, uint16_t payload_len) {
    assert(hdr_len == sizeof(p2p_relay_hdr_t));
    assert(client->base.local_peer_id[0]);

    uint8_t type = ((p2p_relay_hdr_t*)hdr_buf)->type;
    switch (type) {
    case  P2P_RLY_REG:
        print("E:", LA_F("%s: duplicate from '%s'\n", LA_F97, 97), "REG", client->base.local_peer_id);
        relay_send_status((relay_client_t*)client, type, CT_ERR_PROTOCOL);
        return;

    case P2P_RLY_OFF:
        print("I:", LA_F("%s: '%s'\n", LA_F72, 72), PROTO_STR(type), client->base.local_peer_id);
        ct_client_off(&g_ctx, client);
        return;

    case P2P_RLY_ALV: {
        // 心跳包：使用预分配静态缓冲，高优先级插队，去重（上一个未发完则忽略）
        buffer_item_t *alv_item = (buffer_item_t*)((relay_client_t*)client)->alv_ack_buf;
        if (alv_item->refer == ITEM_REF_STATIC) {
            print("V:", LA_F("%s: prev ALV ACK still pending, skip\n", 0, 0), "ALV");
        } else {
            alv_item->refer = ITEM_REF_STATIC;
            ct_client_send(client, alv_item, true);
        }
    } return;

    case P2P_RLY_SYN0:

        // 注：下面的两个验证错误，由于无法解析出正确 remote id，所以只能按 client 级（而非 syn0 级）状态码进行回复
        if (payload_len < P2P_PEER_ID_MAX) {
            print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), "SYN0", payload_len);
            relay_send_status((relay_client_t*)client, P2P_RLY_SYN0, CT_ERR_PROTOCOL);
            return;
        }
        if (!*payload) {
            print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), "SYN0");
            relay_send_status((relay_client_t*)client, P2P_RLY_SYN0, CT_ERR_PROTOCOL);
            return;
        }

        relay_handle_syn0((relay_client_t*)client, payload, payload_len);
        return;
    default:
         break;
    }

    // 注：下面的两个验证错误，由于无法解析出正确 session_id/session，所以只能按 client 级（而非 session 级）状态码进行回复
    if (payload_len < P2P_SESS_ID_SZ) {
        print("E:", LA_F("%s: bad payload(%u)\n", LA_F138, 138), PROTO_STR(type), payload_len);
        relay_send_status((relay_client_t*)client, type, CT_ERR_PROTOCOL);
        return;
    }

    uint32_t session_id;
    nread_l(&session_id, payload);
    session_t *s = session_id ? find_session(session_id) : NULL;
    if (!s || s->client != &client->base) {
        print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148), PROTO_STR(type), session_id);
        relay_send_status((relay_client_t*)client, type, CT_ERR_PROTOCOL);
        return;
    }

    relay_session_t *session = (relay_session_t*)s;
    switch (type) {
    case  P2P_RLY_FIN:  // FIN 不需要对端在线（这里的 session 是 pair 机制，FIN 用于删除本端 session）
        relay_handle_fin(session);
        break;
    case P2P_RLY_SYNC:
        relay_handle_sync((relay_client_t*)client, session, payload, payload_len);
        break;
    case P2P_RLY_PKT:
        relay_handle_pkt((relay_client_t*)client, session, payload, payload_len);
        break;
    case P2P_RLY_REQ:
        relay_handle_req((relay_client_t*)client, session, payload, payload_len);
        break;
    case P2P_RLY_RSP:
        relay_handle_rsp((relay_client_t*)client, session, payload, payload_len);
        break;
    default:
        print("E:", LA_F("unsupported type=%u (ses_id=%u)\n", LA_F204, 204),
              (unsigned)type, session_id);
        relay_send_status((relay_client_t*)client, type, CT_ERR_PROTOCOL);
    }
}

///////////////////////////////////////////////////////////////////////////////

void relay_error_item(ct_client_t *client, buffer_item_t* buffer_item) {

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t*)ITEM2BUF(buffer_item);
    // 握手错误时复用 recv_buf（类型字段已含请求类型）；其他情况用 P2P_RLY_STA
    uint8_t orig_type = !client->base.local_peer_id[0] ? hdr->type : P2P_RLY_STA;
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(P2P_RLY_STA_PSZ(0, 0));
    buffer_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0));
    uint8_t* payload = (uint8_t*)(hdr + 1);
    payload[0] = orig_type;
    payload[1] = CT_CODE(client->last_error);
}

custom_tcp_ctx_t*
relay_init(void) {

    buffer_item_t *fatal_item = (buffer_item_t*)g_relay_fatal;
    p2p_relay_hdr_t *fatal_hdr = (p2p_relay_hdr_t*)ITEM2BUF(fatal_item);
    fatal_hdr->type = P2P_RLY_STA;
    fatal_hdr->size = htons(P2P_RLY_STA_PSZ(0, 0));
    fatal_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0));
    ((uint8_t*)(fatal_hdr + 1))[1] = P2P_RLY_ERR_INTERNAL;

    g_ctx.hdr_len = sizeof(p2p_relay_hdr_t);
    g_ctx.max_payload_len = P2P_MAX_PAYLOAD;
    g_ctx.resolve_payload_len = relay_resolve_payload_len;
    g_ctx.handle_handshake = relay_handle_handshake;
    g_ctx.handle_proto = relay_handle_proto;
    g_ctx.handle_peer_sent = relay_handle_peer_sent;
    g_ctx.session_break = relay_session_break;
    g_ctx.fatal_item = (buffer_item_t*)&g_relay_fatal;
    g_ctx.error_item = relay_error_item;
    return &g_ctx;
}

bool
relay_init_client(relay_client_t* client) {

    if (!ct_init_client((ct_client_t*)client)) return false;

    // 预初始化内嵌 ALV ACK 缓冲
    buffer_item_t *alv_item = (buffer_item_t*)client->alv_ack_buf;
    alv_item->refer = NULL;
    alv_item->next  = NULL;
    p2p_relay_hdr_t *alv_hdr = (p2p_relay_hdr_t*)ITEM2BUF(alv_item);
    alv_hdr->type = P2P_RLY_ALV;
    alv_hdr->size = 0;
    alv_item->len = sizeof(p2p_relay_hdr_t);

    return true;
}

// 释放 client
void
relay_free_client(relay_client_t *client) {

    ct_free_client(&g_ctx, (ct_client_t*)client);
}

// 检查 RELAY RPC 超时（队列按时间排序，未超时即短路返回）
void relay_retry_pending(uint64_t now) {

    while (g_relay_rpc_pending_head) { relay_session_t *s = g_relay_rpc_pending_head;

        // 队列按时间排序，未超时即全部未超时
        if (tick_diff(now, s->rpc_sent_time) < REQ_MAX_RETRY * RPC_RETRY_INTERVAL_MS) return;

        // 移除队头
        g_relay_rpc_pending_head = s->rpc_pending_next;
        if (g_relay_rpc_pending_head == (void*)-1) {
            g_relay_rpc_pending_head = g_relay_rpc_pending_rear = NULL;
        }
        s->rpc_pending_next = NULL;

        // 向请求方发送超时错误 RSP
        uint16_t sid = s->rpc_pending_sid;
        s->rpc_pending_sid = 0;

        print("W:", "[R] RPC timeout: sid=%u (ses_id=%u)\n", sid, s->base.session_id);
        relay_session_send_rpc_code(s, sid, P2P_RPC_ERR_TIMEOUT);
    }
}

///////////////////////////////////////////////////////////////////////////////
