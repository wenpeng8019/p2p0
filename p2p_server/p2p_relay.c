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

// RELAY RPC 待确认链表（按 rpc_sent_time 排序，队头最早超时）
static relay_session_t*             g_relay_rpc_pending_head = NULL;
static relay_session_t*             g_relay_rpc_pending_rear = NULL;

static uint8_t                      g_relay_fatal[sizeof(buffer_item_t) + sizeof(p2p_relay_hdr_t) + P2P_RLY_STATUS_PSZ(0, 0)];

///////////////////////////////////////////////////////////////////////////////

static void clear_session_sending(relay_session_t *session, int terminate) {

    if (!session->send_head) {
        assert(!session->send_rear && !session->send_next && !session->send_prev);
        return;
    }

    relay_client_t *client;

    // 如果 session 的数据需要被销毁，即无需继续发送给 client 端
    if (terminate) {

        do {
            buffer_item_t *item = session->send_head->next;
            free_buffer(session->send_head);
            session->send_head = item;
        } while(session->send_head);

        // 如果 client 也被 term 了，此时无需维护 client 中的 sending sess 集合
        if (terminate < 0) return;

        client = RELAY_CLIENT(session);
    }
    // 如果需要将 session 现有的数据发送给 client 端，则将 session 的发送队列接入 client 的发送队列
    else {

        client = RELAY_CLIENT(session);

        if (client->send_buff_head) {
            client->send_buff_rear->next = session->send_head;
            client->send_buff_rear = session->send_rear;
        }
        else {
            client->send_buff_head = session->send_head;
            client->send_buff_rear = session->send_rear;
        }

    }

    // 将 sess 自身从 client 中的 sending sess 集合中移除
    if (session->send_prev) session->send_prev->send_next = session->send_next;
    else client->send_sess_head = session->send_next;
    if (session->send_next) session->send_next->send_prev = session->send_prev;
    else client->send_sess_rear = session->send_prev;
}

static void clear_client_sending(relay_client_t *client) {

    // 释放 client 级发送队列
    if (client->send_buff_head) {
        buffer_item_t *item = client->send_buff_head;
        if (client->sending_offset && !client->sending_sess) { // 如果当前正在发送一个包，跳过（确保它是完整发送）
            client->send_buff_rear = item; item = item->next;
            client->send_buff_rear->next = NULL;
        }
        else client->send_buff_head = client->send_buff_rear = NULL;
        while (item) {
            buffer_item_t *next = item->next;
            free_buffer(item);
            item = next;
        }
    }

    // 释放 session 级发送队列
    relay_session_t *sending = client->sending_sess;
    if (sending) { assert(client->sending_offset);
        if (sending->send_prev) sending->send_prev->send_next = sending->send_next;
        else client->send_sess_head = sending->send_next;
        if (sending->send_next) sending->send_next->send_prev = sending->send_prev;
        else client->send_sess_rear = sending->send_prev;

        buffer_item_t *item = sending->send_head->next;
        client->send_buff_rear = sending->send_head; client->send_buff_rear->next = NULL;
        while (item) {
            buffer_item_t *next = item->next;
            if (item->refer) {
                relay_session_t* peer_sess = item->refer;
                assert(peer_sess->peer_sending == item);
                peer_sess->peer_sending = NULL;
            }
            free_buffer(item);
            item = next;
        }
    }
    relay_session_t *sess;
    while((sess = client->send_sess_head)) {
        client->send_sess_head = sess->send_next;
        if (sess->send_next) { sess->send_next = NULL; client->send_sess_head->send_prev = NULL; }
        else client->send_sess_rear = NULL;
        while(sess->send_head) {
            buffer_item_t *item = sess->send_head->next;
            if (item->refer) {
                relay_session_t* peer_sess = item->refer;
                assert(peer_sess->peer_sending == item);
                peer_sess->peer_sending = NULL;
            }
            free_buffer(sess->send_head);
            sess->send_head = item;
        }
        sess->send_rear = NULL;
    }
    if (sending) {
        if (sending->send_head->refer) {
            relay_session_t* peer_sess = sending->send_head->refer;
            assert(peer_sess->peer_sending == sending->send_head);
            peer_sess->peer_sending = NULL;
        }
        sending->send_prev = sending->send_next = NULL;
        client->send_sess_head = client->send_sess_rear = sending;
    }
}

static inline bool is_client_online(const relay_client_t *client) {
    // 已完成握手、且没有关闭、或（因错误而）正在关闭
    if (client->base.local_peer_id[0] && (client->io & TCP_IO_FLAG_WANT_READ)) {
        assert(client->base.fd != P_INVALID_SOCKET && !client->last_error);
        return true;
    } return false;
}

static inline bool is_peer_reachable(const relay_session_t *session) {
    assert(PEER_ONLINE(session) && RELAY_CLIENT(RELAY_PEER(session))->base.local_peer_id[0]);
    return TCP_PEER_REACHABLE(session);
}

// 发送数据 buf 到 client 发送队列
static void relay_send(relay_client_t *client, buffer_item_t* buf_item) {

    assert(client->base.proto == PROTO_RELAY && is_client_online(client));

    buf_item->next = NULL;
    if (client->send_buff_rear) {
        client->send_buff_rear->next = buf_item;
        client->send_buff_rear = buf_item;
    } else {
        client->send_buff_head = client->send_buff_rear = buf_item;
    }

    client->io |= TCP_IO_FLAG_WANT_WRITE;
}

// 发送数据 buf 到 session 发送队列
// + 这里的 session 一般都是对端的 session
static void relay_session_send(relay_session_t *session, buffer_item_t* buf_item) {

    assert(session && session->base.client);

    relay_client_t *c = (relay_client_t*)session->base.client;
    assert(c->base.proto == PROTO_RELAY && is_client_online(c));

    // 添加到 session 的本地发送队列
    buf_item->next = NULL;
    if (session->send_rear) {
        session->send_rear->next = buf_item;
        session->send_rear = buf_item;
        return;
    }

    // session 发送队列为空
    assert(!session->send_next && !session->send_prev);
    session->send_head = session->send_rear = buf_item;

    // 将 session 加入 c 发送队列列表
    session->send_prev = c->send_sess_rear;
    session->send_next = NULL;
    if (c->send_sess_rear) {
        c->send_sess_rear->send_next = session;
        c->send_sess_rear = session;
    } else c->send_sess_head = c->send_sess_rear = session;

    c->io |= TCP_IO_FLAG_WANT_WRITE;
}

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

// 发送 fatal 到 client 发送队列
// + 将队列中正在发送的数据包以外的其他数据包释放，并将 fatal 作为数据队列的最后一项
static void relay_send_fatal(relay_client_t *client) {

    // 清除除了正在发送的包以外的所有待发送数据
    clear_client_sending(client);

    // 追加 fatal 作为最后一项
    if (client->send_buff_head) client->send_buff_head->next = ((buffer_item_t*)&g_relay_fatal);
    else client->send_buff_head = (buffer_item_t*)&g_relay_fatal;
    client->send_buff_rear = (buffer_item_t*)&g_relay_fatal;

    if (!client->last_error)
        client->last_error = P2P_RLY_ERR_INTERNAL;
    client->io &= ~TCP_IO_FLAG_WANT_READ;                // 立即停止读取（全部发送完后会自动 term 关闭）
}

// client 级的状态应答
// + STATUS 包不走 session 队列，直接挂到 client 上
static bool relay_send_status(relay_client_t *client, uint8_t req_type, uint8_t status_code) {

    uint16_t payload_len = P2P_RLY_STATUS_PSZ(0, 0);
    buffer_item_t *buf_item = alloc_buffer(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        relay_send_fatal(client);
        return false;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = req_type;
    p[1] = status_code;

    // 借用一个临时空 session 结构是不合适的，这里直接用 tcp_send 尝试发送
    relay_send(client, buf_item);
    return true;
}

// client/session 临界级的状态应答
// + SYN0 专用状态，状态包不走 session 队列，直接挂到 client 上。因为此时尚未建立会话，需要通过携带 remote_peer_id 来标识哪个对端连接请求出错
static bool relay_send_syn0_status(relay_client_t *client, const char *remote_peer_id, uint8_t status_code) {

    uint16_t payload_len = P2P_RLY_STATUS_PSZ(1, 0);
    buffer_item_t *buf_item = alloc_buffer(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        relay_send_fatal(client);
        return false;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = P2P_RLY_SYN0;
    p[1] = status_code;
    memset(p + 2, 0, P2P_PEER_ID_MAX);
    if (remote_peer_id) strncpy((char*)(p + 2), remote_peer_id, P2P_PEER_ID_MAX);

    relay_send(client, buf_item);
    return true;
}

// session 级的状态应答
static bool relay_session_send_status(relay_session_t *session, uint8_t req_type, uint8_t status_code) {

    assert(session && session->base.client);

    uint16_t payload_len = P2P_RLY_STATUS_PSZ(2, 0);
    buffer_item_t *buf_item = alloc_buffer(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        relay_send_fatal((relay_client_t*)session->base.client);
        return false;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);

    uint8_t *payload = (uint8_t *)(hdr + 1);
    payload[0] = req_type;
    payload[1] = status_code;
    nwrite_l(payload + 2, session->base.session_id);

    relay_session_send(session, buf_item);
    return true;
}


// 发送 sync0 ack
// payload: [target_name(32)][session_id(P2P_SESS_ID_SZ)][[0xFF]
static bool relay_session_send_syn0_off(relay_session_t *session, const char *target_name) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    uint16_t payload_len = P2P_RLY_SYN0_S2C_PSZ(0);
    buffer_item_t *buf_item = alloc_buffer(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        relay_send_fatal(client);
        return false;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYN0;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    memset(payload, 0, P2P_PEER_ID_MAX);
    strncpy((char*)payload, target_name, P2P_PEER_ID_MAX - 1);
    nwrite_l(payload + P2P_PEER_ID_MAX, session->base.session_id);

    payload[P2P_PEER_ID_MAX + P2P_SESS_ID_SZ] = 0xFF;

    relay_session_send(session, buf_item);
    return true;
}

// 发送 sync confirm
// payload: [session_id(P2P_SESS_ID_SZ)][confirmed_count(1)]
static bool relay_session_send_sync_confirm(relay_session_t *session, uint8_t confirmed_count) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    uint16_t payload_len = P2P_RLY_SYNC_PSZ(0, false);
    buffer_item_t *buf_item = alloc_buffer(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        relay_send_fatal(client);
        return false;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_l(payload, session->base.session_id);

    payload[P2P_SESS_ID_SZ] = confirmed_count;

    relay_session_send(session, buf_item);
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
        relay_send_fatal(client);
        return false;
    }

    p2p_relay_hdr_t* hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_FIN;
    hdr->size = htons(P2P_RLY_FIN_PSZ);
    uint8_t* payload = (uint8_t*)(hdr+1);
    nwrite_l(payload, session->base.session_id);

    relay_session_send(session, buf_item);
    return true;
}

// 向 RPC 请求方发送应答 code
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)]
static bool relay_session_send_rpc_code(relay_session_t *session, uint16_t sid, uint8_t code) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("E:", LA_F("send failed(OOM)\n", LA_F100, 100));
        relay_send_fatal((relay_client_t*)session->base.client);
        return false;
    }

    uint16_t payload_len = P2P_RLY_RPC_MIN_PSZ;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_RSP;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    nwrite_l(p, session->base.session_id);
    nwrite_s(p + P2P_SESS_ID_SZ, sid);
    p[P2P_SESS_ID_SZ + 2] = code;

    relay_session_send(session, buf_item);
    return true;
}

///////////////////////////////////////////////////////////////////////////////

static void
relay_break_session(relay_session_t *session, relay_session_t *peer, break_mode_e break_mode) {

    if (break_mode == SESS_BREAK_STOP) { TCP_REACHABLE(peer->base.client);

        // 存在本端发起的 req
        if (session->rpc_pending_sid) {
            relay_session_send_rpc_code(session, session->rpc_pending_sid, P2P_RPC_ERR_BREAK);
            relay_pending_remove_rpc(session);
            session->rpc_sent_time = 0;
            session->rpc_pending_sid = 0;
        }

        // 存在对端发起的 req
        if (peer->rpc_pending_sid) {
            relay_session_send_rpc_code(peer, peer->rpc_pending_sid, P2P_RPC_ERR_PEER_OFF);
            relay_pending_remove_rpc(peer);
            peer->rpc_sent_time = 0;
            peer->rpc_pending_sid = 0;
        }
    }
    else { PEER_ONLINE(peer);

        relay_session_send_fin(peer);

        if (peer->peer_sending) {
            peer->peer_sending->refer = NULL;
            peer->peer_sending = NULL;
        }
        if (peer->peer_send) {
            free_buffer(peer->peer_send);
            peer->peer_send = NULL;
        }

        if (peer->rpc_pending_next) relay_pending_remove_rpc(peer);
        peer->rpc_pending_sid = 0;
        peer->rpc_sent_time = 0;

        // 如果本端还有待发送给对端的数据，直接发送给对端
        if (session->peer_send && session->peer_send != ((buffer_item_t *)-1)) {
            buffer_item_t *pending = session->peer_send;
            session->peer_send = NULL;
            relay_send((relay_client_t*)peer->base.client, pending);
        }
        session->peer_send = NULL;

        // 释放对端待处理项
        if (session->peer_send) {
            if (session->peer_send != (buffer_item_t *)-1) {
                free_buffer(session->peer_send);
            }
            session->peer_send = NULL;
        }

        // 从 RPC 待确认链表移除并清除忙标志
        if (session->rpc_pending_sid) {
            relay_pending_remove_rpc(session);
            session->rpc_pending_sid = 0;
        }
    }
}

static void relay_peer_send(relay_session_t *session) {

    assert(PEER_ONLINE(session) && TCP_PEER_REACHABLE(session));

    // 添加数据包到对端的发送队列
    assert(session->peer_send && !session->peer_sending);
    session->peer_sending = session->peer_send;
    session->peer_send = NULL;

    session->peer_sending->refer = session;

    p2p_relay_hdr_t *p_hdr = (p2p_relay_hdr_t *)ITEM2BUF(session->peer_sending);
    assert(p_hdr->type != P2P_RLY_SYN0);           // SYN0 为 session 首个数据，不会在此时发生

    // 如果是 P2P_RLY_SYNC，则回复 P2P_RLY_SYNC confirm
    if (p_hdr->type == P2P_RLY_SYNC) {
        uint8_t candidate_count = ((uint8_t *)(p_hdr + 1))[P2P_SESS_ID_SZ];
        if (candidate_count) relay_session_send_sync_confirm(session, candidate_count);
    }
    else if (p_hdr->type == P2P_RLY_PKT) {
        relay_session_send_status(session, p_hdr->type, P2P_RLY_CODE_READY);
    }

    relay_session_send(RELAY_PEER(session), session->peer_sending);
}

static void relay_peer_sent(relay_session_t *session, buffer_item_t* buf_item) {

    assert(session->peer_sending == buf_item);
    session->peer_sending = NULL;

    if (buf_item->flags & RELAY_BUF_FLAGS_SYNC_FIN) {

        if (!relay_session_send_sync_confirm(session, 0)) {
            // todo
        }
    }

    // 如果没有待发送数据（全部发送完成），或者对端已离线（停止发送），则直接返回
    if (session->peer_send && TCP_PEER_REACHABLE(session)) {

        relay_peer_send(session);
    }
}

// 释放 session
static void relay_close_session(relay_session_t *session, int terminate) {

    // 如果已和对端 session 建立连接
    if (PEER_ONLINE(session)) {
        relay_break_session(session, RELAY_PEER(session), terminate ? SESS_BREAK_TERM : SESS_BREAK_CLOSE);
    }

    assert(!PEER_ONLINE(session) && !session->peer_send && !session->peer_sending);

    clear_session_sending(session, terminate);

    free_session_base(&session->base);
}

///////////////////////////////////////////////////////////////////////////////

void
relay_init(void) {

    buffer_item_t *fatal_item = (buffer_item_t*)g_relay_fatal;
    p2p_relay_hdr_t *fatal_hdr = (p2p_relay_hdr_t*)ITEM2BUF(fatal_item);
    fatal_hdr->type = P2P_RLY_STA;
    fatal_hdr->size = htons(P2P_RLY_STATUS_PSZ(0, 0));
    ((uint8_t*)(fatal_hdr + 1))[1] = P2P_RLY_ERR_INTERNAL;
}

bool
relay_init_client(relay_client_t* c) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_MTU(0));
    if (!buf_item) {
        print("E:", LA_F("[TCP] OOM: cannot allocate recv buffer for new client\n", LA_F133, 133));
        return false;
    }

    c->recv_buf = ITEM2BUF(buf_item);
    c->recv_len = 0;
    c->send_buff_head = NULL;
    c->send_buff_rear = NULL;
    c->send_sess_head = NULL;
    c->send_sess_rear = NULL;
    c->sending_offset = 0;

    return true;
}

// 释放 client
void
relay_free_client(relay_client_t *client) {

    while (client->base.sessions)
        relay_close_session((relay_session_t*)client->base.sessions, -1);
    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;

    client->sending_offset = 0;
    client->sending_sess = NULL;
    clear_client_sending(client);

    free_client_base(&client->base);
}

static void client_off(relay_client_t *client) {

    while (client->base.sessions)
        relay_close_session((relay_session_t*)client->base.sessions, 0);

    assert(!client->send_sess_head);
    client->send_sess_head = client->send_sess_rear = NULL;
    client->sending_sess = NULL;

    // 如果发送队列不为空，标记为 closing（send 完成后会自动完成 term），否则直接 term
    if (client->send_buff_head) {
        client->handshake = -1;
        client->io &= ~TCP_IO_FLAG_WANT_READ;
    }
    else free_client_base(&client->base);
}

static void client_unreachable(relay_client_t *client) {

    // 执行 unreachable 处理
    for(session_t *sess = client->base.sessions, *peer; sess; sess = sess->next) { peer = sess->peer;
        // 如果 peer 在线，且之前不是 unreachable 状态
        if (PEER_VALID(peer) && TCP_REACHABLE(peer->client)) {
            relay_break_session((relay_session_t*)sess, (relay_session_t*)peer, SESS_BREAK_STOP);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

// 处理 SYN0 消息（首次同步）
// payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
static void relay_handle_syn0(relay_client_t *client, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYN0";

    if (len < P2P_RLY_SYN0_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }
    if (!*payload) {
        print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), PROTO);
        return;
    }
    uint8_t cand_count = payload[P2P_PEER_ID_MAX];
    uint32_t expect_len = P2P_RLY_SYN0_PSZ(cand_count);
    if (len != expect_len) {
        print("E:", LA_F("%s: bad payload(cnt=%d, len=%u, expected=%u)\n", LA_F39, 39),
               PROTO, cand_count, len, expect_len);
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
        relay_send_fatal(client);
        return;
    }
    if (side < E_NONE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, (const char *)payload, side);
        relay_send_syn0_status(client, (const char *)payload, P2P_RLY_ERR_PROTOCOL);
        return;
    }

    // 截断候选计数
    if (cand_count > MAX_CANDIDATES)
        cand_count = MAX_CANDIDATES;

    print("V:", LA_F("%s: local='%s', remote='%s', online=%d, cands=%d\n", LA_F55, 55),
           PROTO, client->base.local_peer_id, (const char *)payload, remote_s ? 1 : 0, cand_count);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 构造 sync0 协议包
    // + 如果存在初始 sync 数据，则优先使用零拷贝 forward 方案（即直接转发客户端的 recv_buf）
    buffer_item_t *sync0_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync0_item = alloc_buffer(BUF_FLAG_MTU(0));
        if (!sync0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            relay_send_fatal(client);
            return;
        }

        // memmove 为 session_id 腾出空间，替换 target_name → source_name
        memmove(payload + P2P_PEER_ID_MAX + P2P_SESS_ID_SZ,
                payload + P2P_PEER_ID_MAX,
                1 + cand_count * (int)sizeof(p2p_candidate_t));
        memset(payload, 0, P2P_PEER_ID_MAX);
        strncpy((char*)payload, client->base.local_peer_id, P2P_PEER_ID_MAX - 1);

        hdr = (p2p_relay_hdr_t *)client->recv_buf;
        hdr->size = htons(P2P_RLY_SYN0_S2C_PSZ(cand_count));

        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(sync0_item);
        client->recv_len = 0;
        sync0_item = item;
    }
    else {

        sync0_item = alloc_buffer(BUF_FLAG_512(0));
        if (!sync0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            relay_send_fatal(client);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync0_item));
        uint8_t *p = (uint8_t*)(hdr + 1);
        memset(p, 0, P2P_PEER_ID_MAX);
        strncpy((char*)p, client->base.local_peer_id, P2P_PEER_ID_MAX - 1);
        // session_id 由后续 nwrite_l 写入
        p[P2P_PEER_ID_MAX + P2P_SESS_ID_SZ] = 0; // cand_count = 0
        hdr->size = htons(P2P_RLY_SYN0_S2C_PSZ(0));
    }
    hdr->type = P2P_RLY_SYN0;

    // 如果对方不在线，立刻返回 sync0 offline
    // + 并将 sync0 包缓存到 local_s->peer_sending，后面会直接启动双方 sync0 同步
    if (!remote_s) {

        if (local_s->peer_sending)
            free_buffer(local_s->peer_sending);

        local_s->peer_sending = sync0_item;

        relay_session_send_syn0_off(local_s, (const char *) payload);

        print("I:", LA_F("%s: peer '%s' offline, cached cands=%d\n", LA_F163, 163),
              PROTO, (const char *)payload, cand_count);
    }
    // 对端已在线，启动双方 sync0 同步
    else {

        // 建立双向引用关系
        if (!local_s->base.peer) local_s->base.peer = (session_t*)remote_s;
        if (!remote_s->base.peer) remote_s->base.peer = (session_t*)local_s;

        //-------

        assert(!local_s->peer_sending && !local_s->peer_send);  // 本端不可能存在挂起的 SYN0

        // 本端 SYN0 转发给对端前，需要写入对端 session_id（位于 source_name 之后）
        uint8_t* sid = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;
        nwrite_l(sid, remote_s->base.session_id);

        // 添加到对端发送队列（零拷贝转发），设置 refer 触发传完后的 complete 回调
        sync0_item->refer = local_s;
        local_s->peer_sending = sync0_item;
        relay_session_send(remote_s, sync0_item);

        //-------

        assert(remote_s->peer_sending && !remote_s->peer_send); // 对端肯定存在挂起的 SYNC

        buffer_item_t *remote_sync0_item = remote_s->peer_sending;
        hdr = (p2p_relay_hdr_t *)ITEM2BUF(remote_sync0_item);

        // 对端 SYN0 转发给本端前，需要写入本端 session_id（位于 source_name 之后）
        sid = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;
        nwrite_l(sid, local_s->base.session_id);

        // 添加到本端发送队列，也要设置 refer
        remote_sync0_item->refer = remote_s;
        relay_session_send(local_s, remote_sync0_item);

        // 如果对端 sync0 携带了同步数据，发送 SYNC_ACK 告知对端同步数据已（确认）转发
        uint8_t remote_cand_count = sid[P2P_SESS_ID_SZ];
        if (remote_cand_count) {
            relay_session_send_sync_confirm(remote_s, remote_cand_count);
        }

        //-------

        // 如果本端 sync0 发送的同步数据，发送 SYNC_ACK 告知本端同步数据已（确认）转发
        if (cand_count) relay_session_send_sync_confirm(local_s, cand_count);

        //-------

        print("I:", LA_F("%s: %s <-> %s forward\n", LA_F48, 48),
              PROTO, client->base.local_peer_id, (const char *)payload);
    }
}

// 处理 SYNC 消息（候选同步）
// payload: [session_id(P2P_SESS_ID_SZ)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
static void relay_handle_sync(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC";

    if (len < P2P_RLY_SYNC_PSZ(0, false)) {
        print("E:", LA_F("%session: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    uint8_t cand_count = payload[P2P_SESS_ID_SZ];
    uint16_t payload_sz = P2P_RLY_SYNC_PSZ(cand_count, false);
    if (len == payload_sz + 1u) {

        if (payload[payload_sz] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%session: bad FIN marker=0x%02x\n", LA_F38, 38), PROTO, payload[payload_sz]);
            return;
        }
    }
    else if (len != payload_sz) {

        print("E:", LA_F("%session: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n", LA_F40, 40),
               PROTO, (unsigned)cand_count, len, payload_sz);
        return;
    }

    print("V:", LA_F("%session: ses_id=%u, cands=%d\n", LA_F165, 165), PROTO, session->base.session_id, cand_count);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync_item = alloc_buffer(BUF_FLAG_MTU(0));
        if (!sync_item) {
            print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            relay_send_fatal(client);
            return;
        }

        hdr = (p2p_relay_hdr_t *)client->recv_buf;
        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(sync_item);
        client->recv_len = 0;
        sync_item = item;
    }
    else {

        sync_item = alloc_buffer(BUF_FLAG_512(0));
        if (!sync_item) {
            print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            relay_send_fatal(client);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync_item));
        hdr->type = P2P_RLY_SYNC;
        hdr->size = P2P_RLY_SYNC_PSZ(0, true);
        hdr->size = htons(hdr->size);

        payload = (uint8_t*)(hdr+1) + P2P_SESS_ID_SZ;
        payload[0] = 0;
        payload[1] = P2P_RLY_SYNC_FIN_MARKER;
    }

    // 交换写入对端的 session_id
    uint8_t *sid_ptr = (uint8_t *)(hdr + 1);
    nwrite_l(sid_ptr, session->base.peer->session_id);

    // 如果是最后一个 SYNC 数据包，设置标志位
    // + 发送完成后，可以对此进行额外特殊处理（即回复一个独立 SYNC_ACK:confirmed=0，作为 SYN FIN 完成通知）
    if (len == payload_sz + 1u || !cand_count) {
        sync_item->flags |= RELAY_BUF_FLAGS_SYNC_FIN;
    }

    assert(!session->peer_send);
    session->peer_send = sync_item;
    if (!session->peer_sending && is_peer_reachable(session)) {
        relay_peer_send(session);
    }
}

// 处理 FIN 消息（会话结束）
// payload: [session_id(P2P_SESS_ID_SZ)]
static void relay_handle_fin(relay_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%session: close ses_id=%u\n", LA_F158, 158), PROTO, session->base.session_id);

    // 向对端发送 FIN 说明本端已经关闭了连接
    // + 销毁本端的 sess（销毁操作会向对端发送 FIN）
    relay_close_session(session, 1);
}

// 处理 PKT 消息（零拷贝转发）
// payload: session_id(P2P_SESS_ID_SZ)][P2P hdr(4)][data]
static void relay_handle_pkt(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "DATA";

    if (len < P2P_SESS_ID_SZ) {
        print("E:", LA_F("%session: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    print("V:", LA_F("%session: ses_id=%u, data_len=%u\n", LA_F166, 166), PROTO,
          nget_l(payload), len - P2P_SESS_ID_SZ);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发：分配新 recv_buf，将当前 recv_buf 直接作为转发包
    buffer_item_t *new_recv = alloc_buffer(BUF_FLAG_MTU(0));
    if (!new_recv) {
        print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
        // 对于数据中转的内存分配失败，并不返回 fatal 错误
        relay_session_send_status(session, P2P_RLY_PKT, P2P_RLY_ERR_INTERNAL);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, session->base.peer->session_id);

    assert(!session->peer_send);
    session->peer_send = buf_item;
    if (!session->peer_sending && is_peer_reachable(session)) {
        relay_peer_send(session);
    }
}

// 处理 RPC_REQ 消息（零拷贝转发）
// payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
static void relay_handle_req(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "REQ";

    if (len < P2P_RLY_RPC_MIN_PSZ) {
        print("E:", LA_F("%session: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    uint8_t* ptr = payload + P2P_SESS_ID_SZ;
    uint16_t sid = nget_s(ptr);
    uint8_t  msg = ptr[2];
    uint16_t data_len = len - P2P_RLY_RPC_MIN_PSZ;

    print("V:", LA_F("%session: '%session' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, client->base.local_peer_id, sid, msg, data_len);

    // 检查对端是否在线
    if (!session->base.peer || !session->base.peer->client
        || ((relay_client_t*)session->base.peer->client)->base.fd == P_INVALID_SOCKET) {
        print("W:", LA_F("%session: peer offline, sending error resp\n", LA_F64, 64), PROTO);
        relay_session_send_rpc_code(session, sid, P2P_RPC_ERR_PEER_OFF);
        return;
    }

    if (!uint16_circle_newer(sid, session->rpc_last_sid)) {
        print("W:", LA_F("%session: sid too old (got=%u, pending=%u), discarding\n", 0, 0),
              PROTO, sid, session->rpc_pending_sid);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_RLY_ERR_INVALID);
        return;
    }

    // rpc_pending_sid 忙检查
    if (session->rpc_pending_sid) {
        print("W:", LA_F("%session: rpc busy (pending sid=%u)\n", LA_F67, 67), PROTO, session->rpc_pending_sid);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_RLY_ERR_BUSY);
        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发：分配新 recv_buf，将当前 recv_buf 直接作为转发包
    buffer_item_t *new_recv = alloc_buffer(BUF_FLAG_MTU(0));
    if (!new_recv) {
        print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
        relay_session_send_status(session, P2P_RLY_REQ, P2P_RLY_ERR_INTERNAL);
        return;
    }

    session->rpc_last_sid = sid;

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, session->base.peer->session_id);
    buf_item->refer = NULL;
    relay_session_send((relay_session_t*)session->base.peer, buf_item);

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
        print("E:", LA_F("%session: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    uint8_t* ptr_sid = payload + P2P_SESS_ID_SZ;
    uint16_t sid  = nget_s(ptr_sid);
    uint8_t  code = payload[P2P_SESS_ID_SZ + 2];
    int data_len  = (int)len - (int)P2P_RLY_RPC_MIN_PSZ;

    print("V:", LA_F("%session: '%session' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, client->base.local_peer_id, sid, code, data_len);

    // 检查对端（请求方）是否在线
    if (!session->base.peer || !session->base.peer->client ||
        ((relay_client_t*)session->base.peer->client)->base.fd == P_INVALID_SOCKET) {
        print("W:", LA_F("%session: requester offline, discarding\n", LA_F66, 66), PROTO);
        return;
    }

    // 验证 sid 与请求方 pending sid 一致
    if (((relay_session_t*)session->base.peer)->rpc_pending_sid != sid) {
        print("W:", LA_F("%session: sid mismatch (got=%u, pending=%u), discarding\n", LA_F68, 68),
              PROTO, sid, ((relay_session_t*)session->base.peer)->rpc_pending_sid);
        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发
    buffer_item_t *new_recv = alloc_buffer(BUF_FLAG_MTU(0));
    if (!new_recv) {
        print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
        relay_session_send_status(session, P2P_RLY_RSP, P2P_RLY_ERR_INTERNAL);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为请求方的 session_id，并转发到请求方
    nwrite_l(payload, session->base.peer->session_id);
    buf_item->refer = NULL;
    relay_session_send((relay_session_t*)session->base.peer, buf_item);

    // 解锁 rpc_pending_sid（RPC 生命周期完成），释放 pending 状态
    relay_pending_remove_rpc((relay_session_t*)session->base.peer);
    ((relay_session_t*)session->base.peer)->rpc_pending_sid = 0;
}

//-----------------------------------------------------------------------------

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

// 处理 RELAY 模式信令（TCP 长连接）- 统一接收+分发架构
void relay_handle_recv(relay_client_t *client) {
    assert(client->base.proto == PROTO_RELAY);
    assert(client->recv_buf);

    client->base.last_active = P_tick_ms(); uint8_t type = 0;
    for(;;client->recv_len = 0) {

        // 握手写阶段，禁止接收新消息（此时应该已经取消了 TCP_IO_FLAG_WANT_READ）
        // + 相应的，此时 recv_buf 已被 handshake ack 复用作为 send_buf
        assert(!client->handshake);

        // 读取 header (3字节)
        while (client->recv_len < sizeof(p2p_relay_hdr_t)) {
            size_t need = sizeof(p2p_relay_hdr_t) - client->recv_len;
            int rc = tcp_recv((tcp_client_t*)client, client->recv_buf + client->recv_len, &need);
            if (rc > 0) return;
            if (rc < 0) {
                if (!client->base.local_peer_id[0]) goto error_handshake;
                if (rc < -1) client->last_error = P2P_RLY_ERR_IO;
                goto error;
            }
            client->recv_len += (uint16_t)need;
        }

        // 解析 header
        type = client->recv_buf[0];
        uint8_t* ptr = client->recv_buf + 1;
        uint16_t payload_len = nget_s(ptr);
        if (payload_len > P2P_MAX_PAYLOAD) {
            print("E:", LA_F("bad payload(len=%u)\n", LA_F139, 139), payload_len);
            client->last_error = P2P_RLY_ERR_OVERFLOW;
            if (!client->base.local_peer_id[0]) goto error_handshake;
            goto error;
        }

        // 读取完整 payload
        uint16_t total_need = sizeof(p2p_relay_hdr_t) + payload_len;
        while (client->recv_len < total_need) {
            size_t need = total_need - client->recv_len;
            int rc = tcp_recv((tcp_client_t*)client, client->recv_buf + client->recv_len, &need);
            if (rc > 0) return;
            if (rc < 0) {
                if (!client->base.local_peer_id[0]) goto error_handshake;
                if (rc < -1) client->last_error = P2P_RLY_ERR_IO;
                goto error;
            }
            client->recv_len += (uint16_t)need;
        }
        uint8_t *payload = client->recv_buf + sizeof(p2p_relay_hdr_t);

        // 分发处理
        if (type == P2P_RLY_REG) { const char* PROTO = "REG";

            // 重复 REG
            // + local_peer_id 是否为空，等价于 handshake recv 阶段
            if (client->base.local_peer_id[0]) {
                print("E:", LA_F("%s: duplicate from '%s'\n", LA_F97, 97), PROTO, client->base.local_peer_id);
                relay_send_status(client, type, P2P_RLY_ERR_PROTOCOL);
                continue;
            }

            assert(!client->base.sessions);
            assert(!client_identified(&client->base));

            // 由于这里维护了单一实例，所以此时的 client 肯定是新分配的
            assert(!client->recv_len);
            assert(!client->send_buff_head);
            assert(!client->send_buff_rear);
            assert(!client->send_sess_head);
            assert(!client->send_sess_rear);
            assert(!client->sending_offset);


            // 处理 REG 消息：[name(32)][instance_id(4)]
            if (payload_len != P2P_RLY_REG_PSZ) {
                print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, payload_len);
                goto error_handshake;
            }

            if (!*payload) {
                print("E:", LA_F("%s: invalid peer id\n", LA_F96, 96), PROTO);
                goto error_handshake;
            }

            ptr = payload + P2P_PEER_ID_MAX;
            uint32_t instance_id = nget_l(ptr);
            if (instance_id == 0) {
                print("E:", LA_F("%s: invalid instance id\n", LA_F94, 94), PROTO);
                goto error_handshake;
            }

            // 查找是否存在同名的已登录 client（断网重连场景）
            relay_client_t *reg = (relay_client_t*)find_client((char*)payload);
            if (reg) { assert(reg != client);

                uint8_t* recv_buf = client->recv_buf; uint16_t recv_len = client->recv_len;
                client->recv_buf = NULL; client->recv_len = 0;

                // 如果 instance_id 一致（断网重连）
                if (resident_client(&reg->base, PROTO_RELAY, instance_id, &client->base)) {
                    client = reg;
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
            }

            // 回复 REG ACK
            {
                // 就地修改 recv_buf 为 REG_ACK (复用缓冲区)
                p2p_relay_hdr_t *ack_hdr = (p2p_relay_hdr_t *)client->recv_buf;
                ack_hdr->type = P2P_RLY_REG;
                ack_hdr->size = htons(P2P_RLY_REG_S2C_PSZ);
                uint8_t *ack_payload = (uint8_t*)(ack_hdr+1);
                ack_payload[0/* features */] = 0;
                if (ARGS_relay.i64) ack_payload[0] |= P2P_RLY_FEATURE_RELAY;
                if (ARGS_msg.i64) ack_payload[0] |= P2P_RLY_FEATURE_MSG;
                ack_payload[1/* candidate_sync_max */] = (uint8_t)MAX_CANDIDATES;

                // 此时发送队列应该是空的，可以直接发送 ACK，且以 recv_buf 作为发送缓冲区（零拷贝）
                size_t ack_len = sizeof(p2p_relay_hdr_t) + P2P_RLY_REG_S2C_PSZ;
                ssize_t r = tcp_send((tcp_client_t*)client, client->recv_buf, &ack_len, "REG_ACK");
                if (r < 0) {
                    relay_free_client(client);    // 握手阶段发送失败，直接释放 client
                    return;
                }

                // 如果 would block，则标记为 reg_ack_pending
                // + 注意：此时不加入索引表，等 ACK 发送完成后再加入
                if (r > 0) {
                    client->handshake = 1;
                    client->io &= ~TCP_IO_FLAG_WANT_READ;           // 握手写阶段，暂停接收数据，等握手（ACK 发送）完成后再继续
                    client->io |= TCP_IO_FLAG_WANT_WRITE;
                    client->recv_len = ack_len;
                    return;
                }
            }

            // 添加到索引表（握手完成）
            identify_client(&client->base);
        }
        // 除 REG 外，所有消息都要求已完成登录。这里的 REG 等价于 handshake
        else if (!client->base.local_peer_id[0]) {
            print("E:", LA_F("%s: rejected for not reg\n", LA_F147, 147), PROTO_STR(type));
            goto error_handshake;
        }
        else if (type == P2P_RLY_OFF) {
            print("I:", LA_F("%s: '%s'\n", LA_F72, 72), PROTO_STR(type), client->base.local_peer_id);
            client->io &= ~TCP_IO_FLAG_WANT_READ;
            client_off(client);
            return;
        }
        // 心跳包：last_active 已在循环入口更新，就地改写 recv_buf 为 ALIVE_ACK 回复
        // ! 这里直接发送 ACK（3 bytes 数据），理论上不应出现 WOULDBLOCK，如果发生了也无妨，等下次心跳再回复即可
        else if (type == P2P_RLY_ALV) {
            buffer_item_t *item = alloc_buffer(BUF_FLAG_512(0));
            if (!item) {
                print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), "ALIVE");
                client->last_error = P2P_RLY_ERR_INTERNAL;
                goto error_fatal;
            }
            p2p_relay_hdr_t* hdr = (p2p_relay_hdr_t*)ITEM2BUF(item);
            hdr->type = P2P_RLY_ALV;
            hdr->size = 0;
            relay_send(client, item);
        }
        else if (type == P2P_RLY_SYN0) {
            if (*payload) {
                print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), "SYN0");
                relay_send_status(client, type, P2P_RLY_ERR_PROTOCOL);
            }
            else relay_handle_syn0(client, payload, payload_len);
            continue;
        }
        else {
            if (payload_len < P2P_SESS_ID_SZ) {
                print("E:", LA_F("%s: bad payload(%u)\n", LA_F138, 138), PROTO_STR(type), payload_len);
                relay_send_status(client, type, P2P_RLY_ERR_PROTOCOL);
                continue;
            }

            uint32_t session_id;
            nread_l(&session_id, payload);
            session_t *s = find_session(session_id);
            if (!s || s->client != &client->base) {
                print("W:", LA_F("%s: unknown ses_id=%u\n", LA_F148, 148), PROTO_STR(type), session_id);
                relay_send_status(client, type, P2P_RLY_ERR_PROTOCOL);
                continue;
            }

            relay_session_t *session = (relay_session_t*)s;

            // FIN 不需要对端在线（单边关闭）
            if (type == P2P_RLY_FIN) {
                relay_handle_fin(session);
            }
            // 其他会话操作要求对端在线
            else if (!PEER_ONLINE(s)) {

                // REQ 特殊处理：即通过 RPC 的 resp + code 返回错误吗，而非通用的 P2P_RLY_STA 错误码
                if (type == P2P_RLY_REQ && payload_len >= P2P_RLY_RPC_MIN_PSZ) {
                    uint8_t* ptr_sid = payload + P2P_SESS_ID_SZ;
                    relay_session_send_rpc_code(session, nget_s(ptr_sid), P2P_RPC_ERR_PEER_OFF);
                } else {
                    print("W:", LA_F("%s: ses_id=%u peer not connected\n", LA_F146, 146), PROTO_STR(type), session_id);
                    relay_session_send_status(session, type, P2P_RLY_ERR_PEER_OFF);
                }
            }
            // SYNC / PKT 转发时，最多允许一个在发、一个待发，超过则返回 BUSY
            else if ((type == P2P_RLY_SYNC || type == P2P_RLY_PKT) && session->peer_send) {

                print("W:", LA_F("%s: ses_id=%u busy (pending relay)\n", LA_F145, 145), PROTO_STR(type), session_id);
                relay_session_send_status(session, type, P2P_RLY_ERR_BUSY);
            }
            else switch (type) {
            case P2P_RLY_SYNC:
                relay_handle_sync(client, session, payload, payload_len);
                break;
            case P2P_RLY_PKT:
                relay_handle_pkt(client, session, payload, payload_len);
                break;
            case P2P_RLY_REQ:
                relay_handle_req(client, session, payload, payload_len);
                break;
            case P2P_RLY_RSP:
                relay_handle_rsp(client, session, payload, payload_len);
                break;
            default:
                print("E:", LA_F("unsupported type=%u (ses_id=%u)\n", LA_F204, 204),
                      (unsigned)type, session_id);
                relay_send_status(client, type, P2P_RLY_ERR_PROTOCOL);
            }
        }
    }

// 网络 I/O 等非致命错误，也就是不破坏（之前/已发生的）数据完整性的错误
// + 此时会清除 recv_buf 中的数据、关闭读取，同时（最后）发送一个错误状态码，并等发送完成后会自动关闭连接
error:
    client->recv_len = 0;
    client_unreachable(client);
    relay_send_status(client, type, client->last_error);
    client->io &= ~TCP_IO_FLAG_WANT_READ;
    return;
// 大多数 Internal(例如 OOM) 错误，它们会导致（之前或之后的）数据完整性被破坏，或无法再继续安全的运行
// + 此时会清除所有发送队列（因为数据完整性已破坏，即使发送过去也没有意义），
//   但除了正在发送的数据包（因为得保证传输的数据的完整性，否则后面的 fatal 应答包就没有意义）
error_fatal:
    assert(client->last_error);
    relay_send_fatal(client);
    return;
// 握手阶段的错误
// + 此时还没有完成身份确认（更没有其他后续数据），而握手信令自身被视为原子性事务
//   所以此时在回复错误状态码后会直接销毁 client 对象
error_handshake:
    assert(!client->base.local_peer_id[0]);

    if (!client->last_error) client->last_error = P2P_RLY_ERR_PROTOCOL;

    // 此时可以直接复用 recv_buf 作为发送缓冲区
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)client->recv_buf;
    hdr->type = P2P_RLY_STA;
    hdr->size = P2P_RLY_STATUS_PSZ(0, 0);
    size_t len = sizeof(p2p_relay_hdr_t);
    client->recv_buf[len] = P2P_RLY_REG;
    client->recv_buf[len+1] = client->last_error;
    len += hdr->size; hdr->size = htons(len);

    ssize_t r = tcp_send((tcp_client_t*)client, client->recv_buf, &len, "REG_ACK");
    if (r > 0) {
        client->handshake = 1;
        client->io |= TCP_IO_FLAG_WANT_WRITE;
        client->io &= ~TCP_IO_FLAG_WANT_READ;
        client->recv_len = len;
        return;
    }
    // 发送失败、或回复完成，直接释放 client
    relay_free_client(client);
}

// 处理 RELAY 模式信令发送（TCP 长连接）- 统一队列发送
void relay_handle_send(relay_client_t *client) {

    // 当前正在发送 REG_ACK，也就是处于握手阶段
    // + 此时还没有 session，复用 recv_buf 作为 send_buf，recv_len 作为已发送长度
    if (client->handshake > 0) {

        size_t ack_sz = sizeof(p2p_relay_hdr_t) + P2P_RLY_REG_S2C_PSZ;
        size_t len = ack_sz - client->recv_len;
        int rc = tcp_send((tcp_client_t*)client, client->recv_buf + client->recv_len, &len, "REG_ACK");
        if (rc < 0) {
            relay_free_client(client);    // 握手阶段发送失败，直接释放 client
            return;
        }

        if (len > 0) { client->recv_len += len;

            // REG_ACK 发送完成
            if (client->recv_len >= ack_sz) { client->recv_len = 0;

                client->handshake = 0;
                print("V:", LA_F("REG_ACK sent to '%s'\n", LA_F179, 179), client->base.local_peer_id);

                // 如果发送完成的是握手阶段的错误应答，直接释放 client
                if (client->last_error) {
                    relay_free_client(client);
                    return;
                }

                // 启动正常读写（握手完成）
                client->io |= TCP_IO_FLAG_WANT_READ;
                client->io &= ~TCP_IO_FLAG_WANT_WRITE;

                // 添加到索引表
                identify_client(&client->base);
            }
        }

        // REG_ACK 未完成时，跳过其他处理
        if (client->handshake) return;
    }

    relay_session_t *sending_session = client->send_sess_head; buffer_item_t *item = client->send_buff_head;
    if (sending_session || item) { assert(client->io & TCP_IO_FLAG_WANT_WRITE);

        // 除非当前正在发送 session 级别的包，否则优先发送 client 级别的包
        if (!item || client->sending_sess) { assert(sending_session);
            item = sending_session->send_head;
        }

        const p2p_relay_hdr_t *hdr = (const p2p_relay_hdr_t *)ITEM2BUF(item);
        const uint16_t len = (uint16_t)(sizeof(p2p_relay_hdr_t) + ntohs(hdr->size));
        size_t send_sz = len - client->sending_offset;
        int rc = tcp_send((tcp_client_t*)client, (const char *)hdr + client->sending_offset, &send_sz, PROTO_STR(hdr->type));
        if (rc < 0) {

            if (rc < -1) client->last_error = P2P_RLY_ERR_IO;
            P_sock_close(client->base.fd);
            client->base.fd = P_INVALID_SOCKET;

            client->sending_offset = 0;
            client_unreachable(client);
            client->io &= ~(TCP_IO_FLAG_WANT_READ|TCP_IO_FLAG_WANT_WRITE);
            return;
        }

        // 当前 session 发送完成
        if (send_sz > 0 && (client->sending_offset += (int)send_sz) >= len) { client->sending_offset = 0;

            // 删除已发送完成的 item
            if (item==client->send_buff_head) {
                if (!((client->send_buff_head = item->next))) client->send_buff_rear = NULL;
            } else { assert(item==sending_session->send_head);

                if (item->refer) {
                    relay_peer_sent((relay_session_t*)item->refer, item);
                }

                if (!((sending_session->send_head = item->next))) {
                    sending_session->send_rear = NULL;

                    // 如果 session 发送队列已空，发送下一条待发送 session
                    client->send_sess_head = sending_session->send_next;
                    if (client->send_sess_head) client->send_sess_head->send_prev = NULL;
                    else client->send_sess_rear = NULL;
                    sending_session->send_next = NULL;
                }
            }

            // 删除已发送完成的 item
            free_buffer(item);

            // 如果全部发送完成
            if (!client->send_sess_head && !client->send_buff_head) {
                client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                if (!(client->io & TCP_IO_FLAG_WANT_READ)) {
                    if (client->handshake < 0) relay_free_client(client);
                    else { P_sock_close(client->base.fd);
                        client->base.fd = P_INVALID_SOCKET;
                    }
                }
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

