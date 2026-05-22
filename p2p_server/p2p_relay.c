//
// Created by 温朋 on 2026/4/18.
//
#define MOD_TAG "RELAY"

#include "p2p_relay.h"

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

// RELAY RPC 待确认队列（按 rpc_sent_time 排序，队头最早超时）
static timeout_queue_t              g_relay_rpc_pending_q;

static uint8_t                      g_relay_fatal[sizeof(buf16_item_t) + sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0)];
static ct_client_ctx_t              g_ctx;

#define RLY_ERR_2_CT_ERR(err)       (CUSTOM_TCP_ERR_DISCONNECTED + (err) - P2P_ERR_DISCONNECTED)

///////////////////////////////////////////////////////////////////////////////

// client 级的状态应答
// + STATUS 包不走 session 队列，直接挂到 client 上
static bool relay_send_status(relay_client_t *client, uint8_t req_type, uint8_t status_code) {

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
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
    p[1] = status_code;

    // 借用一个临时空 session 结构是不合适的，这里直接用 tcp_send 尝试发送
    ct_client_send((ct_client_t*)client, buf_item, false);
    return true;
}

// client/session 临界级的状态应答
// + SYN0 专用状态，状态包不走 session 队列，直接挂到 client 上。因为此时尚未建立会话，需要通过携带 remote_peer_id 来标识哪个对端连接请求出错
static bool relay_send_syn0_status(relay_client_t *client, const char *remote_peer_id, uint8_t status_code) {

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
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
    p[1] = status_code;
    memset(p + 2, 0, P2P_PEER_ID_MAX);
    if (remote_peer_id) strncpy((char*)(p + 2), remote_peer_id, P2P_PEER_ID_MAX);

    ct_client_send((ct_client_t*)client, buf_item, false);
    return true;
}

// session 级的状态应答
static bool relay_session_send_status(relay_session_t *session, uint8_t req_type, uint8_t status_code) {

    assert(session && session->base.client);

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
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
    payload[1] = status_code;
    nwrite_l(payload + 2, session->base.session_id);

    ct_session_send((ct_session_t*)session, buf_item);
    return true;
}


// 发送 syn0 ack
// payload: [target_name(32)][session_id(P2P_SESS_ID_SZ)][[0xFF]
static bool relay_session_send_syn0_off(relay_session_t *session, const char *target_name) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
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

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
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

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
    if (!buf_item) {
        print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F222, 222), "FIN");
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

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_512(0));
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

///////////////////////////////////////////////////////////////////////////////

static void relay_session_init(session_t* s) {
    relay_session_t* session = (relay_session_t*)s;
    BUF_R_INIT(&session->sync_peer_send, session->sync_peer_slots, RELAY_PEER_Q_MAX);
    BUF_R_INIT(&session->pkt_peer_send, session->pkt_peer_slots, RELAY_PEER_Q_MAX);
}

// 处理 SYN0 消息（首次同步）
// payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
// 注：SYN0 的 sid=0 固定，省略传输；后续 SYNC 从 sid=1 起始
static void relay_handle_syn0(relay_client_t *client, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYN0";

    if (len < P2P_RLY_SYN0_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_send_syn0_status(client, (const char *)payload, P2P_ERR_PROTOCOL);
        return;
    }

    uint8_t cand_count = payload[P2P_PEER_ID_MAX];
    uint32_t expect_len = P2P_RLY_SYN0_PSZ(cand_count);
    if (len != expect_len) {
        print("E:", LA_F("%s: bad payload(cnt=%d, len=%u, expected=%u)\n", LA_F39, 39), PROTO, cand_count, len, expect_len);
        relay_send_syn0_status(client, (const char *)payload, P2P_ERR_PROTOCOL);
        return;
    }
    payload[P2P_PEER_ID_MAX] = '\0';

    if (cand_count > MAX_CANDIDATES) {
        print("E:", LA_F("%s: too many candidates(cnt=%d)\n", LA_F241, 241), PROTO, cand_count);
        relay_send_syn0_status(client, (const char *)payload, P2P_ERR_PROTOCOL);
        return;
    }

    // 构建会话
    relay_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&client->base, (const char *)payload,
                       (session_t**)&local_s, (session_t**)&remote_s,
                       sizeof(relay_session_t), relay_session_init);
    if (side == E_OUT_OF_MEMORY) {
        print("E:", LA_F("%s: build session to '%s' failed(OOM)", LA_F157, 157), PROTO, (const char *)payload);
        ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
        return;
    }
    if (side < E_NONE && side != E_DUPLICATE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, (const char *)payload, side);
        relay_send_syn0_status(client, (const char *)payload, P2P_ERR_PROTOCOL);
        return;
    }

    print("V:", LA_F("%s: local='%s', remote='%s', online=%d, cands=%d\n", LA_F55, 55),
           PROTO, client->base.local_peer_id, (const char *)payload, remote_s ? 1 : 0, cand_count);

    // 失序的 syn0 请求
    if (local_s->last_sid) {
        print("E:", LA_F("%s: deprecated (ses_id=%u, sid=%u), drop\n", LA_F207, 207),
              PROTO, local_s->base.session_id, local_s->last_sid);
        relay_send_syn0_status(client, (const char *)payload, P2P_ERR_PROTOCOL);
        return;
    }

    // 重复的 SYN0 请求
    if (side == E_DUPLICATE) {

        // 如果对端不可达，幂等返回 offline ACK
        if (!remote_s || !TCP_REACHABLE(remote_s->base.client)) {
            print("V:", LA_F("%s: duplicate SYN0 (ses_id=%u), resend ACK\n", LA_F210, 210),
                  PROTO, local_s->base.session_id);
            relay_session_send_syn0_off(local_s, (const char *)payload);
        }
        return;
    }

    // 构造 syn0 协议包
    buf16_item_t *syn0_item; p2p_relay_hdr_t *hdr; uint8_t *p;
    if (cand_count) {

        syn0_item = alloc_buf16(BUF_FLAG_MTU(0));
        if (!syn0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
            return;
        }

        // 构建 S2C SYN0 包：[relay_hdr(3)][peer_id(P2P_PEER_ID_MAX)][session_id_placeholder(P2P_SESS_ID_SZ)][cand_count(1)][candidates(N*23)]
        hdr = (p2p_relay_hdr_t*)ITEM2BUF(syn0_item); len = P2P_RLY_SYN0_S2C_PSZ(cand_count);
        p = (uint8_t*)(hdr + 1);

        // 从 payload 复制候选（payload[P2P_PEER_ID_MAX+1..]）
        memcpy(p + P2P_RLY_SYN0_S2C_PSZ(0), payload + P2P_PEER_ID_MAX + 1, cand_count * sizeof(p2p_candidate_t));
    }
    else {

        syn0_item = alloc_buf16(BUF_FLAG_128(0));
        if (!syn0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            ct_client_error(&g_ctx, (ct_client_t*)client, CUSTOM_TCP_ERR_INTERNAL, true);
            return;
        }

        // 构造 S2C SYN0 包：[relay_hdr(3)][peer_id(P2P_PEER_ID_MAX)][session_id_placeholder(P2P_SESS_ID_SZ)][cand_count(1)=0]
        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(syn0_item)); len = P2P_RLY_SYN0_S2C_PSZ(0);
        p = (uint8_t*)(hdr + 1);
    }
    syn0_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + len);
    hdr->type = P2P_RLY_SYN0;
    hdr->size = htons(len);
    memset(p, 0, P2P_PEER_ID_MAX);
    strncpy((char*)p, client->base.local_peer_id, P2P_PEER_ID_MAX);
    p[P2P_PEER_ID_MAX + P2P_SESS_ID_SZ] = cand_count;

    // 添加到本端同步队列
    BUF_R_PUSH(&local_s->sync_peer_send, syn0_item);

    // 如果对方不在线，立刻返回 syn0 offline
    // + 并将 syn0 包缓存到 BUF_R_FRONT(&local_s->sync_peer_send)，后面会直接启动双方 syn0 同步
    if (!remote_s) {

        // 立刻返回 syn0 offline（即不添加到对端发送队列，保持 refer 为 NULL，等对端上线时再触发发送）
        relay_session_send_syn0_off(local_s, (const char *) payload);

        print("I:", LA_F("%s: peer '%s' offline, pending\n", LA_F163, 163),
              PROTO, (const char *)payload);
    }
    // 对端已在线，启动双方 syn0 同步
    else {

        //-------
        // 向对端发送（转发）本端 SYN0 包

        // 添加到对端发送队列

        p = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;                    // 本端 SYN0 转发给对端前，需要写入对端 session_id（位于 peer id 之后）
        nwrite_l(p, remote_s->base.session_id);

        syn0_item->refer = local_s;                                 // 设置 refer 用于触发传完后的 complete 回调
        ct_session_send((ct_session_t*)remote_s, syn0_item);        // 向对端发送（转发） SYN0 包

        //-------

        // 如果对端暂不可达，立刻返回 syn0 offline
        if (!TCP_REACHABLE(remote_s->base.client)) {

            // 立刻返回 syn0 offline（syn0 前面已发到对端的发送队列，对端网络恢复后会继续发送）
            relay_session_send_syn0_off(local_s, (const char *) payload);

            print("I:", LA_F("%s: peer '%s' unreachable, pending\n", LA_F235, 235),
                  PROTO, (const char *)payload);

            return;
        }

        //-------
        // 对于对端先发起的 syn0 请求（即对端正在等待本端上线）

        buf16_item_t *remote_syn0_item = BUF_R_FRONT(&remote_s->sync_peer_send);
        hdr = (p2p_relay_hdr_t *)ITEM2BUF(remote_syn0_item);

        // 添加到本端发送队列

        p = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;                    // 对端 SYN0 转发给本端前，需要写入本端 session_id（位于 peer id 之后）
        nwrite_l(p, local_s->base.session_id);

        remote_syn0_item->refer = remote_s;                         // 设置 refer，用于触发传完后的 complete 回调
        ct_session_send((ct_session_t*)local_s, remote_syn0_item);  // 向本端发送（转发）对端 SYN0 包

        //-------

        // 双方 SYN0 互相转发完成；各自收到对方的 SYN0 即为隐式 ACK，无需单独发 confirm
        print("I:", LA_F("%s: %s <-> %s forward\n", LA_F48, 48),
              PROTO, client->base.local_peer_id, (const char *)payload);
    }
}

// 处理 SYNC 消息（候选同步 / C→S confirm）
// 上行: [session_id(P2P_SESS_ID_SZ)][sid(1)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
// confirm: [session_id(P2P_SESS_ID_SZ)][sid(1)][confirmed_count(1)]  ← 客户端确认收到服务器下发的 SYNC
static void relay_handle_sync(relay_client_t *client, relay_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC";

    if (len < P2P_RLY_SYNC_CONFIRM_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_PROTOCOL);
        return;
    }

    uint8_t sid = payload[P2P_SESS_ID_SZ];
    if (!sid) {
        print("E:", LA_F("%s: bad payload(sid=0)\n", LA_F205, 205), PROTO);
        relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_PROTOCOL);
        return;
    }

    if (!PEER_ONLINE(session)) {
        print("E:", LA_F("%s: ses_id=%u, peer offline, drop sid=%u\n", LA_F220, 220),
              PROTO, session->base.session_id, sid);
        relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_PEER_OFF);
        return;
    }

    // C→S confirm：客户端确认收到服务器下发（转发）的 SYNC，len 恰好等于 confirm 包大小
    if (len == P2P_RLY_SYNC_CONFIRM_PSZ) {

        print("V:", LA_F("%s: ses_id=%u, confirm sid=%u\n", LA_F165, 165),
              PROTO, session->base.session_id, sid);

        // 获取 peer（sync 的原始发送方）
        relay_session_t *peer = RELAY_PEER(session);
        if (!BUF_R_EMPTY(&peer->sync_peer_send) && BUF_R_FRONT(&peer->sync_peer_send)->refer == ITEM_REF_ACK_PENDING) {

            bool full = false; uint8_t pending_sid = 0;
            if (BUF_R_FULL(&peer->sync_peer_send)) { full = true;
                pending_sid = ((uint8_t*)ITEM2BUF(BUF_R_LAST(&peer->sync_peer_send)))[sizeof(p2p_relay_hdr_t) + P2P_SESS_ID_SZ];
            }

            free_buffer(BUF_R_FRONT(&peer->sync_peer_send));
            BUF_R_POP(&peer->sync_peer_send);

            // 如果 peer sess 的 SYNC 队列不空，发送下一个 SYNC
            if (!BUF_R_EMPTY(&peer->sync_peer_send)) {
                BUF_R_FRONT(&peer->sync_peer_send)->refer = peer;
                ct_session_send((ct_session_t*)session, BUF_R_FRONT(&peer->sync_peer_send));
            }
            // 队列从满→非满：B 之前因队满未收到 confirm，现在补发
            // sid 取队头项的 sid（B 发送的、当时因队满被推迟 confirm 的那一项）
            if (full) relay_session_send_sync_confirm(peer, pending_sid);
        }
        return;
    }

    uint8_t cand_count = payload[P2P_SESS_ID_SZ + 1];
    uint16_t payload_sz = P2P_RLY_SYNC_PSZ(cand_count, false);
    if (len == payload_sz + 1u) {

        if (payload[payload_sz] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F38, 38), PROTO, payload[payload_sz]);
            relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_PROTOCOL);
            return;
        }
    }
    else if (len != payload_sz) {
        print("E:", LA_F("%s: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n", LA_F40, 40),
               PROTO, sid, (unsigned)cand_count, len, payload_sz);
        relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_PROTOCOL);
        return;
    }

    // 去重：重复包重发 confirm 即可，不重复转发
    if (sid == session->last_sid) {
        print("W:", LA_F("%s: ses_id=%u, dup sid=%u, resend confirm\n", LA_F217, 217),
            PROTO, session->base.session_id, sid);
        relay_session_send_sync_confirm(session, sid);
        return;
    }

    // 验证同步序的一致性
    if (!uint16_circle_newer(sid, session->last_sid)) {
        print("E:", LA_F("%s: deprecated (ses_id=%u, sid=%u, last=%u), drop\n", LA_F209, 209),
              PROTO, session->base.session_id, sid, session->last_sid);
        relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_PROTOCOL);
        return;
    }

    // 忙检查（控速）
    if (BUF_R_FULL(&session->sync_peer_send)) {
        print("W:", LA_F("%s: busy (ses_id=%u, sid=%u), pending\n", LA_F206, 206),
              PROTO, session->base.session_id, sid);
        relay_session_send_status(session, P2P_RLY_SYNC, P2P_ERR_BUSY);
        return;
    }


    // SYN0 隐式 ACK：本端首个 SYNC 上行视为是对服务器下发的 SYN0 的确认
    relay_session_t *peer = RELAY_PEER(session);
    if (!BUF_R_EMPTY(&peer->sync_peer_send) && BUF_R_FRONT(&peer->sync_peer_send)->refer == ITEM_REF_ACK_PENDING) {
        if (((p2p_relay_hdr_t*)ITEM2BUF(BUF_R_FRONT(&peer->sync_peer_send)))->type == P2P_RLY_SYN0) {

            free_buffer(BUF_R_FRONT(&peer->sync_peer_send));
            BUF_R_POP(&peer->sync_peer_send);

            // 如果 peer sess 的 SYNC 队列不空，发送下一个 SYNC
            if (!BUF_R_EMPTY(&peer->sync_peer_send)) {
                BUF_R_FRONT(&peer->sync_peer_send)->refer = peer;
                ct_session_send((ct_session_t*)session, BUF_R_FRONT(&peer->sync_peer_send));
            }
        }
    }

    print("V:", LA_F("%s: ses_id=%u, sid=%u, cands=%d\n", LA_F221, 221),
          PROTO, session->base.session_id, sid, cand_count);

    buf16_item_t *sync_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        // 零拷贝转发：直接复用 payload1（框架在 payload 前预留了 sizeof(relay_hdr) 字节）
        hdr = (p2p_relay_hdr_t*)ITEM2BUF(payload1);
        hdr->type = P2P_RLY_SYNC;
        hdr->size = htons((uint16_t)len);   // len = 完整 payload 长度（含 session_id）
        payload1->pos = 0;                   // 暴露预留的 relay_hdr 前缀
        ((ct_client_t*)client)->payload_buf = NULL;  // 接管所有权，通知框架跳过释放
        sync_item = payload1;
    }
    else {
        // cand_count==0 的 FIN 包，构造新转发包（保留 sid）
        sync_item = alloc_buf16(BUF_FLAG_512(0));
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

    // 替换转发身份（交换写入对端的 session_id）
    uint8_t *sid_ptr = (uint8_t *)(hdr + 1);
    nwrite_l(sid_ptr, session->base.peer->session_id);

    // 推进更新同步序列 id
    session->last_sid = sid;

    // 在首次入队时，执行冷启动发送
    if (BUF_R_EMPTY(&session->sync_peer_send)) {
        sync_item->refer = session;
        ct_session_send((ct_session_t*)peer, sync_item);
    }
    BUF_R_PUSH(&session->sync_peer_send, sync_item);
    // 队列未满时立即 confirm；队满时等 A 的 confirm 释放槽后补发（流控）
    if (!BUF_R_FULL(&session->sync_peer_send))
        relay_session_send_sync_confirm(session, sid);
}

// 处理 FIN 消息（会话结束）
// payload: [session_id(P2P_SESS_ID_SZ)]
static void relay_handle_fin(relay_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%s: close ses_id=%u\n", LA_F158, 158), PROTO, session->base.session_id);

    // 销毁该对话（会话的 break 处理会向对端发送 FIN 包）
    free_session((session_t*)session, true);
}

// 处理 PKT 消息（零拷贝转发）
// + 该通道使用 STATUS(P2P_RLY_PKT, BUSY/READY) 做显式背压流控
// payload: session_id(P2P_SESS_ID_SZ)][P2P hdr(4)][data]
static void relay_handle_pkt(relay_client_t *client, relay_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "PKT";

    if (!PEER_ONLINE(session)) {
        print("E:", LA_F("%s: ses_id=%u, peer offline, drop pkt\n", LA_F218, 218),
              PROTO, session->base.session_id);
        relay_session_send_status(session, P2P_RLY_PKT, P2P_ERR_PEER_OFF);
        return;
    }

    // 忙检查（控速）
    if (BUF_R_FULL(&session->pkt_peer_send)) {
        print("W:", LA_F("%s: busy (ses_id=%u), pending\n", LA_F145, 145),
              PROTO, session->base.session_id);
        relay_session_send_status(session, P2P_RLY_PKT, P2P_ERR_BUSY);
        return;
    }

    print("V:", LA_F("%s: ses_id=%u, data_len=%u\n", LA_F166, 166), PROTO,
          session->base.session_id, len - P2P_SESS_ID_SZ);

    // 零拷贝转发：直接复用 payload1（框架在 payload 前预留了 sizeof(relay_hdr) 字节）
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t*)ITEM2BUF(payload1);
    hdr->type = P2P_RLY_PKT;
    hdr->size = htons((uint16_t)len);
    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, session->base.peer->session_id);
    payload1->pos = 0;
    ((ct_client_t*)client)->payload_buf = NULL;  // 接管所有权
    buf16_item_t *buf_item = payload1;

    // 发送入队并在首次时冷启动发送
    if (BUF_R_EMPTY(&session->pkt_peer_send)) {
        buf_item->refer = session;
        ct_session_send(CT_PEER(session), buf_item);
    }
    BUF_R_PUSH(&session->pkt_peer_send, buf_item);
    // 如果队列未满，发送状态通知（可以继续发送）
    if (!BUF_R_FULL(&session->pkt_peer_send))
        relay_session_send_status(session, P2P_RLY_PKT, P2P_CODE_READY);
}

// 处理 RPC_REQ 消息（零拷贝转发）
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
static void relay_handle_req(relay_client_t *client, relay_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "REQ";

    if (len < P2P_RLY_RPC_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_ERR_PROTOCOL);
        return;
    }

    uint8_t* ptr = payload + P2P_SESS_ID_SZ;
    uint16_t sid = nget_s(ptr);
    uint8_t  msg = ptr[2];
    uint16_t data_len = len - P2P_RLY_RPC_MIN_PSZ;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, client->base.local_peer_id, sid, msg, data_len);

    if (!sid) {
        print("E:", LA_F("%s: invalid sid=0\n", LA_F212, 212), PROTO);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_ERR_INVALID);
        return;
    }

    if (session->rpc_last_sid && !uint16_circle_newer(sid, session->rpc_last_sid)) {
        print("E:", LA_F("%s: deprecated (ses_id=%u, sid=%u, last=%u), discarding\n", LA_F208, 208),
              PROTO, session->base.session_id, sid, session->rpc_last_sid);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_ERR_INVALID);
        return;
    }

    // 忙检查（控速）
    if (session->rpc_pending_sid) {
        print("E:", LA_F("%s: busy (ses_id=%u, sid=%u), pending\n", LA_F206, 206),
              PROTO, session->base.session_id, session->rpc_pending_sid);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_ERR_BUSY);
        return;
    }

    // 检查对端是否可达
    if (!PEER_ONLINE(session) || !TCP_PEER_REACHABLE(session)) {
        print("E:", LA_F("%s: peer offline, sending error resp\n", LA_F64, 64), PROTO);
        relay_session_send_rpc_code(session, sid, P2P_RPC_ERR_PEER_OFF);
        return;
    }

    session->rpc_last_sid = sid;

    // 零拷贝转发：直接复用 payload1
    p2p_relay_hdr_t *req_hdr = (p2p_relay_hdr_t*)ITEM2BUF(payload1);
    req_hdr->type = P2P_RLY_REQ;
    req_hdr->size = htons((uint16_t)len);
    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, session->base.peer->session_id);
    payload1->pos = 0;
    ((ct_client_t*)client)->payload_buf = NULL;  // 接管所有权
    buf16_item_t *buf_item = payload1;
    buf_item->refer = NULL;
    ct_session_send(CT_PEER(session), buf_item);

    // 转发 REQ 到对端，记录 pending sid（等 RSP 回来才解锁）
    session->rpc_pending_sid = sid;
    session->rpc_sent_time = P_tick_ms();
    TQ_ADD(&g_relay_rpc_pending_q, session, session->rpc_sent_time);
}

// 处理 RPC_RSP 消息（零拷贝转发）
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][data(N)]
static void relay_handle_rsp(relay_client_t *client, relay_session_t *session, buf16_item_t *payload1, uint8_t *payload, uint16_t len) {
    const char *PROTO = "RSP";

    if (len < P2P_RLY_RPC_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_session_send_status(session, P2P_RLY_RSP, P2P_ERR_PROTOCOL);
        return;
    }

    uint8_t* ptr_sid = payload + P2P_SESS_ID_SZ;
    uint16_t sid  = nget_s(ptr_sid);
    uint8_t  code = payload[P2P_SESS_ID_SZ + 2];
    int data_len  = (int)len - (int)P2P_RLY_RPC_MIN_PSZ;

    print("V:", LA_F("%s: '%s' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, client->base.local_peer_id, sid, code, data_len);

    if (!sid) {
        print("W:", LA_F("%s: invalid sid=0\n", LA_F212, 212), PROTO);
        relay_session_send_status(session, P2P_RLY_RSP, P2P_ERR_INVALID);
        return;
    }

    if (!PEER_ONLINE(session)) {
        print("E:", LA_F("%s: ses_id=%u, peer offline, drop rsp\n", LA_F219, 219),
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

    // 零拷贝转发：直接复用 payload1
    p2p_relay_hdr_t *rsp_hdr = (p2p_relay_hdr_t*)ITEM2BUF(payload1);
    rsp_hdr->type = P2P_RLY_RSP;
    rsp_hdr->size = htons((uint16_t)len);
    // 就地重写 session_id 为请求方的 session_id，并转发到请求方
    nwrite_l(payload, peer->base.session_id);
    payload1->pos = 0;
    ((ct_client_t*)client)->payload_buf = NULL;  // 接管所有权
    buf16_item_t *buf_item = payload1;
    buf_item->refer = NULL;
    ct_session_send((ct_session_t*)peer, buf_item);

    // 解锁 rpc_pending_sid（RPC 生命周期完成），释放 pending 状态
    if (TQ_INQ(&g_relay_rpc_pending_q, peer)) TQ_RM(&g_relay_rpc_pending_q, peer);
    peer->rpc_pending_sid = 0;
    peer->rpc_sent_time = 0;
}

//-----------------------------------------------------------------------------

static ret_t relay_resolve_payload_len(ct_client_t* client, uint8_t* hdr_buf, uint16_t hdr_len,
                                       uint32_t* payload_len, uint16_t* payload_offset) {
    (void)client;
    assert(hdr_len == sizeof(p2p_relay_hdr_t));
    ++hdr_buf; nread_s(payload_len, hdr_buf);
    *payload_offset = sizeof(p2p_relay_hdr_t);  // 预留 relay_hdr 前缀供零拷贝重写
    return E_NONE;
}

// NOLINTNEXTLINE(readability-non-const-parameter)
static buf16_item_t* relay_handle_handshake(ct_client_ctx_t *ctx, ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                                             buf16_item_t* payload0, buf16_item_t* payload1) { (void)ctx;
    assert(hdr_len == sizeof(p2p_relay_hdr_t));
    assert(!client->base.local_peer_id[0]);
    const char* PROTO = "REG";

    // 握手阶段只支持 REG 请求（REG 本质就是握手请求）
    if (((p2p_relay_hdr_t*)hdr_buf)->type != P2P_RLY_REG) {
        print("E:", LA_F("%s: rejected for not reg(%s)\n", LA_F236, 236), PROTO, PROTO_STR(((p2p_relay_hdr_t*)hdr_buf)->type));
        client->last_error = RLY_ERR_2_CT_ERR(P2P_ERR_PROTOCOL);
        return NULL;
    }

    buf16_item_t *payload_item = payload0 ? payload0 : payload1;
    if (!payload_item) {
        print("E:", LA_F("%s: missing payload\n", LA_F213, 213), PROTO);
        client->last_error = RLY_ERR_2_CT_ERR(P2P_ERR_PROTOCOL);
        return NULL;
    }
    uint8_t *payload = ITEM2BUF(payload_item) + payload_item->pos;
    uint16_t payload_len = payload_item->len - payload_item->pos;

    // 处理 REG 消息：[name(32)][instance_id(4)]
    if (payload_len != P2P_RLY_REG_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, payload_len);
        client->last_error = RLY_ERR_2_CT_ERR(P2P_ERR_PROTOCOL);
        return NULL;
    }

    if (!*payload) {
        print("E:", LA_F("%s: invalid peer id\n", LA_F96, 96), PROTO);
        client->last_error = RLY_ERR_2_CT_ERR(P2P_ERR_PROTOCOL);
        return NULL;
    }

    uint8_t* ptr = payload + P2P_PEER_ID_MAX;
    uint32_t instance_id = nget_l(ptr);
    if (instance_id == 0) {
        print("E:", LA_F("%s: invalid instance id\n", LA_F94, 94), PROTO);
        client->last_error = RLY_ERR_2_CT_ERR(P2P_ERR_PROTOCOL);
        return NULL;
    }

    // 查找是否存在同名的已登录 client（断网重连场景）
    relay_client_t *reg = (relay_client_t*)find_client((char*)payload);
    if (reg) { assert((ct_client_t*)reg != client);

        if (TCP_HS_IS_HANDSHAKING(reg)) {
            print("E:", LA_F("%s: request simultaneously for '%s'\n", LA_F216, 216), PROTO,
                  reg->base.local_peer_id);
            client->last_error = RLY_ERR_2_CT_ERR(P2P_ERR_INVALID);
            return NULL;
        }

        // 帧模式：无 recv_buf/recv_len 需要保存
        // 如果 instance_id 一致（断网重连）
        if (restore_client_from(&client->base, &reg->base)) {

            // SYNC/SYN0 的 ACK_PENDING 项：TCP 已写出但客户端未确认（连接已断无法确保到达）
            // 重置 refer 并重新入队，让新连接重新投递
            for (session_t *sess = client->base.sessions; sess; sess = sess->next) {
                relay_session_t *peer = RELAY_PEER(sess);
                if (PEER_VALID(peer) && !BUF_R_EMPTY(&peer->sync_peer_send) &&
                    BUF_R_FRONT(&peer->sync_peer_send)->refer == ITEM_REF_ACK_PENDING) {
                    BUF_R_FRONT(&peer->sync_peer_send)->refer = peer;
                    ct_session_send((ct_session_t*)sess, BUF_R_FRONT(&peer->sync_peer_send));
                }
            }

            activate_client(&client->base, 1);

            print("I:", LA_F("%s: '%.*s' reconnected & reactive (inst=%u)\n", LA_F98, 98), PROTO,
                   P2P_PEER_ID_MAX, payload, instance_id);
        }
        // 将之前实例重置（强制旧连接失效），激活新实例
        else {
            print("I:", LA_F("%s: '%.*s' reconnected & renew (inst=%u)\n", LA_F153, 153), PROTO,
                  P2P_PEER_ID_MAX, payload, instance_id);

            reg = NULL;
        }
    }
    else {
        print("I:", LA_F("%s: '%.*s' new REG (inst=%u)\n", LA_F93, 93), PROTO,
              P2P_PEER_ID_MAX, payload, instance_id);
    }

    if (!reg) {
        client->base.instance_id = instance_id;
        identify_client(&client->base, (char*)payload);
    }

    // 构造 REG ACK 包
    buf16_item_t *ack = alloc_buf16(BUF_FLAG_128(0));
    if (!ack) { client->last_error = CUSTOM_TCP_ERR_INTERNAL; return NULL; }
    p2p_relay_hdr_t *ack_hdr = (p2p_relay_hdr_t*)ITEM2BUF(ack);
    ack_hdr->type = P2P_RLY_REG;
    ack_hdr->size = htons(P2P_RLY_REG_S2C_PSZ);
    ack->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_REG_S2C_PSZ);
    uint8_t *ack_payload = (uint8_t*)(ack_hdr + 1);
    ack_payload[0] = 0;
    if (ARGS_relay.i64) ack_payload[0] |= P2P_RLY_FEATURE_RELAY;
    if (ARGS_msg.i64)   ack_payload[0] |= P2P_RLY_FEATURE_MSG;
    ack_payload[1] = (uint8_t)MAX_CANDIDATES;
    return ack;
}

// NOLINTNEXTLINE(readability-non-const-parameter)
static void relay_handle_proto(ct_client_ctx_t *ctx, ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                               buf16_item_t* payload0, buf16_item_t* payload1) { (void)ctx;
    assert(hdr_len == sizeof(p2p_relay_hdr_t));
    assert(client->base.local_peer_id[0]);

    buf16_item_t *payload_item = payload0 ? payload0 : payload1;
    uint8_t *payload = payload_item ? (ITEM2BUF(payload_item) + payload_item->pos) : NULL;
    uint16_t payload_len = payload_item ? (payload_item->len - payload_item->pos) : 0;
    uint8_t type = ((p2p_relay_hdr_t*)hdr_buf)->type;
    switch (type) {
    case  P2P_RLY_REG:
        print("E:", LA_F("%s: duplicate from '%s'\n", LA_F97, 97), "REG", client->base.local_peer_id);
        relay_send_status((relay_client_t*)client, type, P2P_ERR_PROTOCOL);
        return;

    case P2P_RLY_OFF:
        print("I:", LA_F("%s: '%s'\n", LA_F72, 72), PROTO_STR(type), client->base.local_peer_id);
        ct_client_off(&g_ctx, client, NULL);
        return;

    case P2P_RLY_ALV: {
        // 心跳包：使用预分配静态缓冲，高优先级插队，去重（上一个未发完则忽略）
        buf16_item_t *alv_item = (buf16_item_t*)((relay_client_t*)client)->alv_ack_buf;
        if (alv_item->refer == ITEM_REF_STATIC) {
            print("V:", LA_F("%s: prev ALV ACK still pending, skip\n", LA_F215, 215), "ALV");
        } else {
            alv_item->refer = ITEM_REF_STATIC;
            ct_client_send(client, alv_item, true);
        }
    } return;

    case P2P_RLY_SYN0:

        // 注：下面的两个验证错误，由于无法解析出正确 remote id，所以只能按 client 级（而非 syn0 级）状态码进行回复
        if (payload_len < P2P_PEER_ID_MAX) {
            print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), "SYN0", payload_len);
            relay_send_status((relay_client_t*)client, P2P_RLY_SYN0, P2P_ERR_PROTOCOL);
            return;
        }
        if (!*payload) {
            print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), "SYN0");
            relay_send_status((relay_client_t*)client, P2P_RLY_SYN0, P2P_ERR_PROTOCOL);
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
        relay_send_status((relay_client_t*)client, type, P2P_ERR_PROTOCOL);
        return;
    }

    uint32_t session_id;
    nread_l(&session_id, payload);
    session_t *s = session_id ? find_session(session_id) : NULL;
    if (!s || s->client != &client->base) {
        print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148), PROTO_STR(type), session_id);
        relay_send_status((relay_client_t*)client, type, P2P_ERR_PROTOCOL);
        return;
    }

    relay_session_t *session = (relay_session_t*)s;
    switch (type) {
    case  P2P_RLY_FIN:  // FIN 不需要对端在线（这里的 session 是 pair 机制，FIN 用于删除本端 session）
        relay_handle_fin(session);
        break;
    case P2P_RLY_SYNC:
        relay_handle_sync((relay_client_t*)client, session, payload1, payload, payload_len);
        break;
    case P2P_RLY_PKT:
        relay_handle_pkt((relay_client_t*)client, session, payload1, payload, payload_len);
        break;
    case P2P_RLY_REQ:
        relay_handle_req((relay_client_t*)client, session, payload1, payload, payload_len);
        break;
    case P2P_RLY_RSP:
        relay_handle_rsp((relay_client_t*)client, session, payload1, payload, payload_len);
        break;
    default:
        print("E:", LA_F("unsupported type=%u (ses_id=%u)\n", LA_F204, 204),
              (unsigned)type, session_id);
        relay_send_status((relay_client_t*)client, type, P2P_ERR_PROTOCOL);
    }
}

static buf16_item_t* relay_error_item(ct_client_t *client) {

    buf16_item_t *item = alloc_buf16(BUF_FLAG_128(0));
    if (!item) return NULL;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t*)ITEM2BUF(item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(P2P_RLY_STA_PSZ(0, 0));
    item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0));
    uint8_t* p = (uint8_t*)(hdr + 1);
    p[0] = client->handshake ? P2P_RLY_REG : P2P_RLY_STA;
    p[1] = P2P_ERR(client->last_error - 1);
    return item;
}

///////////////////////////////////////////////////////////////////////////////

// 对端 TCP 写入完成后的回调（通用，SYNC/PKT 通道均使用）
// PKT：移出队头，队满→写完才发 READY，启动下一项
// SYNC：TCP 写完但保留队头，将 refer 改为 REFER_ACK_PENDING 等待应用层 ACK
static void relay_handle_peer_sent(ct_client_ctx_t *ctx, ct_session_t *s, buf16_item_t *buf_item) { (void)ctx;

    assert(PEER_ONLINE(s));

    relay_session_t *session = (relay_session_t*)s;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);

    // PKT：写入完成
    if (hdr->type == P2P_RLY_PKT) {
        assert(!BUF_R_EMPTY(&session->pkt_peer_send) && BUF_R_FRONT(&session->pkt_peer_send) == buf_item);

        bool full = BUF_R_FULL(&session->pkt_peer_send);

        // 标记该 buf_item 已经处理完成（可被释放），并移出队头
        buf_item->refer = NULL;
        BUF_R_POP(&session->pkt_peer_send);

        // 启动发送下一项（如有）
        if (!BUF_R_EMPTY(&session->pkt_peer_send)) {
            BUF_R_FRONT(&session->pkt_peer_send)->refer = session;
            ct_session_send(CT_PEER(session), BUF_R_FRONT(&session->pkt_peer_send));
        }

        // 队列从满 → 非满：之前因队满未发 READY，现在补发（控制速度）
        if (full) relay_session_send_status(session, P2P_RLY_PKT, P2P_CODE_READY);
    }
    // SYNC/SYN0：写入完成
    else {
        assert(!BUF_R_EMPTY(&session->sync_peer_send) && BUF_R_FRONT(&session->sync_peer_send) == buf_item);

        // 标记该 buf_item 需要等待应用层 ACK，暂不释放
        buf_item->refer = ITEM_REF_ACK_PENDING;
    }
}

// 清理一个通道的所有队列项，并将存活项转发给目标会话
static void relay_break_forward(buffer_round_t *rq, relay_session_t *to) {
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

// 停止/终止会话
static void relay_session_break(client_ctx_t* ctx, session_t *s, session_t *ps, break_mode_e break_mode) { (void)ctx;

    // 前提是双方在线
    assert(PEER_ONLINE(s) && PEER_ONLINE(ps));

    relay_session_t *session = (relay_session_t*)s, *peer = (relay_session_t*)ps;

    // 存在对端发起的 req
    if (peer->rpc_pending_sid) {
        relay_session_send_rpc_code(peer, peer->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
        if (TQ_INQ(&g_relay_rpc_pending_q, peer)) TQ_RM(&g_relay_rpc_pending_q, peer);
        peer->rpc_sent_time = 0;
        peer->rpc_pending_sid = 0;
    }

    // 存在本端发起的 req
    if (session->rpc_pending_sid) {

        // 如果依然可以向本地发送数据
        if (break_mode != SESS_BREAK_TERM)
            relay_session_send_rpc_code(session, session->rpc_pending_sid, P2P_RPC_ERR_BREAK);

        if (TQ_INQ(&g_relay_rpc_pending_q, session)) TQ_RM(&g_relay_rpc_pending_q, session);
        session->rpc_sent_time = 0;
        session->rpc_pending_sid = 0;
    }

    // 如果只是（unreachable）暂停通讯的状态（即会话和数据完整性不会被破坏）
    if (break_mode == SESS_BREAK_STOP) return;

    // 如果是 grace 关闭会话
    if (break_mode == SESS_BREAK_CLOSE) {   // 将对端剩余数据发给本端
        relay_break_forward(&peer->sync_peer_send, session);
        relay_break_forward(&peer->pkt_peer_send,  session);
    } else {                                // 将对端要发给本端的数据全部释放
        relay_break_forward(&peer->sync_peer_send, NULL);
        relay_break_forward(&peer->pkt_peer_send,  NULL);
    }

    // 把剩余数据发给对端
    relay_break_forward(&session->sync_peer_send, peer);
    relay_break_forward(&session->pkt_peer_send,  peer);

    // 向对端发最后一个 FIN 包
    relay_session_send_fin(peer);

    session->last_sid = peer->last_sid = 0;
    session->rpc_last_sid = peer->rpc_last_sid = 0;
}

static bool relay_client_reset(client_ctx_t* ctx, client_t *c, bool init) {

    relay_client_t* client = (relay_client_t*)c;

    if (!ct_client_reset(ctx, c, init)) {
        return false;
    }

    if (init) {

        // 预初始化内嵌 ALV ACK 缓冲
        buf16_item_t *alv_item = (buf16_item_t*)client->alv_ack_buf;
        alv_item->refer = NULL;
        alv_item->next  = NULL;
        p2p_relay_hdr_t *alv_hdr = (p2p_relay_hdr_t*)ITEM2BUF(alv_item);
        alv_hdr->type = P2P_RLY_ALV;
        alv_hdr->size = 0;
        alv_item->len = sizeof(p2p_relay_hdr_t);
    }

    ((ct_client_t*)client)->hdr_rs = client->hdr_buf;
    ((ct_client_t*)client)->hdr_sz = sizeof(p2p_relay_hdr_t);

    return true;
}

ct_client_ctx_t*
relay_init(void) {

    ct_ctx_init(&g_ctx);

    g_ctx.base.cb_reset = relay_client_reset;
    g_ctx.base.cb_break = relay_session_break;

    g_ctx.max_payload_len = P2P_MAX_PAYLOAD;
    g_ctx.resolve_payload_len = relay_resolve_payload_len;
    g_ctx.handle_handshake = relay_handle_handshake;
    g_ctx.handle_proto = relay_handle_proto;
    g_ctx.handle_peer_sent = relay_handle_peer_sent;
    g_ctx.error_item = relay_error_item;
    g_ctx.fatal_item = (buf16_item_t*)g_relay_fatal;

    buf16_item_t *fatal_item = (buf16_item_t*)g_relay_fatal;
    p2p_relay_hdr_t *fatal_hdr = (p2p_relay_hdr_t*)ITEM2BUF(fatal_item);
    fatal_hdr->type = P2P_RLY_STA;
    fatal_hdr->size = htons(P2P_RLY_STA_PSZ(0, 0));
    fatal_item->len = (uint16_t)(sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0));
    ((uint8_t*)(fatal_hdr + 1))[1] = P2P_ERR_INTERNAL;

    // 初始化 RPC 待确认队列
    TQ_INIT(&g_relay_rpc_pending_q,
            REQ_MAX_RETRY * RPC_RETRY_INTERVAL_MS,
            offsetof(relay_session_t, rpc_pending_prev),
            offsetof(relay_session_t, rpc_pending_next),
            offsetof(relay_session_t, rpc_sent_time));

    return &g_ctx;
}

// 检查 RELAY RPC 超时（队列按时间排序，未超时即短路返回）
void relay_retry_pending(uint64_t now) {

    relay_session_t *s;
    TQ_RETRY(&g_relay_rpc_pending_q, now, s,
        // 向请求方发送超时错误 RSP
        uint16_t sid = s->rpc_pending_sid;
        s->rpc_pending_sid = 0;
        print("W:", "[R] RPC timeout: sid=%u (ses_id=%u)\n", sid, s->base.session_id);
        relay_session_send_rpc_code(s, sid, P2P_RPC_ERR_TIMEOUT);
    )
}

///////////////////////////////////////////////////////////////////////////////
