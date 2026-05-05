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

// refer 哨兵值：TCP 写入已完成，但尚未收到对端应用层 ACK，item 暂不释放
// refer=session            → TCP 写入中（sent callback 触发时处理）
// refer=ITEM_REF_ACK_PENDING  → TCP 写完、等待应用层 ACK（ACK 到达后由业务逻辑释放）
#define ITEM_REF_ACK_PENDING        ((void*)(uintptr_t)1)
#define ITEM_REF_CLIENT_ERROR       ((void*)(uintptr_t)-1)
#define ITEM_REF_ALV_ACK            ((void*)(uintptr_t)2)  // ALV ACK 包正在发送队列中（内嵌缓冲，不可 free）

// RELAY RPC 待确认链表（按 rpc_sent_time 排序，队头最早超时）
static relay_session_t*             g_relay_rpc_pending_head = NULL;
static relay_session_t*             g_relay_rpc_pending_rear = NULL;

static uint8_t                      g_relay_fatal[sizeof(buffer_item_t) + sizeof(p2p_relay_hdr_t) + P2P_RLY_STA_PSZ(0, 0)];

///////////////////////////////////////////////////////////////////////////////

// 发送数据 buf 到 client 发送队列
static void relay_client_send(relay_client_t *client, buffer_item_t* buf_item) {

    // 前提是不能处于握手/closing 阶段
    assert(client->handshake == 0);

    buf_item->next = NULL;
    if (client->send_buff_rear) {
        client->send_buff_rear->next = buf_item;
        client->send_buff_rear = buf_item;
    } else {
        client->send_buff_head = client->send_buff_rear = buf_item;
    }

    if (client->base.fd != P_INVALID_SOCKET && client->last_error == 0)
        client->io |= TCP_IO_FLAG_WANT_WRITE;
}

// 发送数据 buf 到 session 发送队列
static void relay_session_send(relay_session_t *session, buffer_item_t* buf_item) {

    // 前提是对端必须在线
    // + 对方在线意味着 client 肯定未处于 握手/closing 阶段
    assert(PEER_ONLINE(session));

    relay_client_t *client = RELAY_CLIENT(session);

    // 添加到 session 的本地发送队列
    buf_item->next = NULL;
    if (session->send_rear) {
        session->send_rear->next = buf_item;
        session->send_rear = buf_item;
        return;
    }

    // 如果 session 发送队列之前为空
    assert(!session->send_next && !session->send_prev);
    session->send_head = session->send_rear = buf_item;

    // 将 session 加入 c 发送队列列表
    session->send_prev = client->send_sess_rear;
    session->send_next = NULL;
    if (client->send_sess_rear) {
        client->send_sess_rear->send_next = session;
        client->send_sess_rear = session;
    } else client->send_sess_head = client->send_sess_rear = session;

    if (client->base.fd != P_INVALID_SOCKET && client->last_error == 0)
        client->io |= TCP_IO_FLAG_WANT_WRITE;
}

// 清除 session 的发送队列
// + terminate:
//   false: 当前发送队列中的数据并不销毁，而是转移到 client 发送队列
//   true: 直接将当前发送队列中的数据销毁，无需继续发送给 client 端。
static void clear_session_sending(relay_session_t *session, bool terminate, bool all) {

    buffer_item_t *item = session->send_head;
    if (!item) {
        assert(!session->send_rear && !session->send_next && !session->send_prev);
        return;
    }

    relay_client_t *client = RELAY_CLIENT(session);

    // 如果正在发送当前 session 的数据，需要将它转为 client 级发送队列的第一项
    if (client->sending_sess == session) {
        client->sending_sess = NULL;

        session->send_head = item->next;
        if (!session->send_head) session->send_rear = NULL;

        item->next = client->send_buff_head;
        client->send_buff_head = item;
        if (!client->send_buff_rear) client->send_buff_rear = item;
    }

    if (session->send_head) {

        // 如果 session 的数据需要被销毁，即无需继续发送给 client 端
        if (terminate) {

            while((item = session->send_head)) {
                session->send_head = item->next;
                free_buffer(item);
            }
            session->send_rear = NULL;
        }
        // 如果需要将 session 现有的数据发送给 client 端，则将 session 的发送队列接入 client 的发送队列
        else {

            if (client->send_buff_head) {
                client->send_buff_rear->next = session->send_head;
                client->send_buff_rear = session->send_rear;
            }
            else {
                client->send_buff_head = session->send_head;
                client->send_buff_rear = session->send_rear;
            }
        }
    }

    // 如果 clear 的是 client 的所有 sessions 的发送队列
    if (all) session->send_next = session->send_prev = NULL;
    // 如果只 clear 一个 session 的发送队列
    else {
        // 将 sess 自身从 client 中的 sending sess 集合中移除
        if (session->send_prev) session->send_prev->send_next = session->send_next;
        else client->send_sess_head = session->send_next;
        if (session->send_next) session->send_next->send_prev = session->send_prev;
        else client->send_sess_rear = session->send_prev;
    }
}

// 清除 client 的发送队列（除了正在发送中）
static void clear_client_sending(relay_client_t *client) {

    // 此时所有 session 肯定都已经释放
    assert(!client->base.sessions && !client->sending_sess);

    // 释放 client 级发送队列
    if (client->send_buff_head) {
        buffer_item_t *item = client->send_buff_head;
        if (client->sending_offset) { // 如果当前正在发送一个包，跳过（确保它是完整发送）
            client->send_buff_rear = item; item = item->next;
            client->send_buff_rear->next = NULL;
        }
        else client->send_buff_head = client->send_buff_rear = NULL;
        while (item) {
            buffer_item_t *next = item->next;
            // ALV ACK 使用内嵌缓冲，只重置 refer，不 free
            if (item->refer == ITEM_REF_ALV_ACK) item->refer = NULL;
            else free_buffer(item);
            item = next;
        }
    }
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

static void client_fatal(relay_client_t *client);

// client 级的状态应答
// + STATUS 包不走 session 队列，直接挂到 client 上
static bool relay_send_status(relay_client_t *client, uint8_t req_type, uint8_t status_code) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        client_fatal(client);
        return false;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(0, 0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = req_type;
    p[1] = status_code;

    // 借用一个临时空 session 结构是不合适的，这里直接用 tcp_send 尝试发送
    relay_client_send(client, buf_item);
    return true;
}

// client/session 临界级的状态应答
// + SYN0 专用状态，状态包不走 session 队列，直接挂到 client 上。因为此时尚未建立会话，需要通过携带 remote_peer_id 来标识哪个对端连接请求出错
static bool relay_send_syn0_status(relay_client_t *client, const char *remote_peer_id, uint8_t status_code) {

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        client_fatal(client);
        return false;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(1, 0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = P2P_RLY_SYN0;
    p[1] = status_code;
    memset(p + 2, 0, P2P_PEER_ID_MAX);
    if (remote_peer_id) strncpy((char*)(p + 2), remote_peer_id, P2P_PEER_ID_MAX);

    relay_client_send(client, buf_item);
    return true;
}

// session 级的状态应答
static bool relay_session_send_status(relay_session_t *session, uint8_t req_type, uint8_t status_code) {

    assert(session && session->base.client);

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        client_fatal((relay_client_t*)session->base.client);
        return false;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(2, 0);
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


// 发送 syn0 ack
// payload: [target_name(32)][session_id(P2P_SESS_ID_SZ)][[0xFF]
static bool relay_session_send_syn0_off(relay_session_t *session, const char *target_name) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        client_fatal(client);
        return false;
    }

    uint16_t payload_len = P2P_RLY_SYN0_S2C_PSZ(0);
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
// payload: [session_id(P2P_SESS_ID_SZ)][sid(1)]
static bool relay_session_send_sync_confirm(relay_session_t *session, uint8_t sid) {

    assert(session && session->base.session_id && session->base.client);
    relay_client_t *client = (relay_client_t*)session->base.client;

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_512(0));
    if (!buf_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        client_fatal(client);
        return false;
    }

    uint16_t payload_len = P2P_RLY_SYNC_CONFIRM_PSZ;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_l(payload, session->base.session_id);
    payload[P2P_SESS_ID_SZ] = sid;

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
        client_fatal(client);
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
        client_fatal((relay_client_t*)session->base.client);
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

// 将本端队头 item 通过对端 TCP 通道发出（通用，SYNC/PKT 均使用）
// item 必须是对应通道数组的 [0]（队头），且 refer==NULL（尚未启动发送）
static void relay_peer_send(relay_session_t *session, buffer_item_t *item) {

    assert(PEER_ONLINE(session) && TCP_PEER_REACHABLE(session));
    assert(item->refer == NULL);

    item->refer = session;

    // S→C confirm / READY 均延迟至对端 ACK 后发出:
    // SYNC/SYN0 → 等待 C→S confirm（relay_handle_sync 中处理）
    // PKT       → 队满时写完才发 READY，否则入队时立即发（relay_handle_pkt / relay_peer_sent）

    relay_session_send(RELAY_PEER(session), item);
}

// 对端 TCP 写入完成后的回调（通用，SYNC/PKT 通道均使用）
// PKT：移出队头，队满→写完才发 READY，启动下一项
// SYNC：TCP 写完但保留队头，将 refer 改为 REFER_ACK_PENDING 等待应用层 ACK
static void relay_peer_sent(relay_session_t *session, buffer_item_t *buf_item) {

    assert(PEER_ONLINE(session));

    p2p_relay_hdr_t *p_hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    if (p_hdr->type == P2P_RLY_PKT) {
        // PKT：写入完成，移出队头
        assert(session->pkt_peer_send_cnt > 0 && session->pkt_peer_send[0] == buf_item);
        buf_item->refer = NULL;  // 通知调用方可释放
        bool was_full = (session->pkt_peer_send_cnt >= RELAY_PEER_Q_MAX);
        // 移出队头（RELAY_PEER_Q_MAX == 2）
        session->pkt_peer_send[0] = session->pkt_peer_send[1];
        session->pkt_peer_send[1] = NULL;
        session->pkt_peer_send_cnt--;
        // 队列从满 → 非满：之前因队满未发 READY，现在补发
        if (was_full)
            relay_session_send_status(session, P2P_RLY_PKT, P2P_RLY_CODE_READY);
        // 启动下一项（如有）
        if (session->pkt_peer_send_cnt > 0 && TCP_PEER_REACHABLE(session))
            relay_peer_send(session, session->pkt_peer_send[0]);
    } else {
        // SYNC/SYN0：TCP 写入完成，但队头继续持有 item 等待应用层 ACK
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
        else relay_session_send(dst, it);
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
static void relay_break_session(relay_session_t *session, relay_session_t *peer, break_mode_e break_mode) {

    // 前提是双方在线
    assert(PEER_ONLINE(session) && PEER_ONLINE(peer));

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

// 关闭/销毁会话
static void relay_close_session(relay_session_t *session, bool terminate, bool all) {

    // 如果已和对端 session 建立连接
    if (PEER_ONLINE(session)) {
        relay_break_session(session, RELAY_PEER(session), terminate ? SESS_BREAK_TERM : SESS_BREAK_CLOSE);
    }

    // 清除 session 的 sending 队列
    clear_session_sending(session, terminate, all);

    // 释放 session
    free_session_base(&session->base);
}

///////////////////////////////////////////////////////////////////////////////

void
relay_init(void) {

    buffer_item_t *fatal_item = (buffer_item_t*)g_relay_fatal;
    p2p_relay_hdr_t *fatal_hdr = (p2p_relay_hdr_t*)ITEM2BUF(fatal_item);
    fatal_hdr->type = P2P_RLY_STA;
    fatal_hdr->size = htons(P2P_RLY_STA_PSZ(0, 0));
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
    c->sending_sess = NULL;
    c->sending_offset = 0;

    // 预初始化内嵌 ALV ACK 缓冲
    buffer_item_t *alv_item = (buffer_item_t*)c->alv_ack_buf;
    alv_item->refer = NULL;
    alv_item->next  = NULL;
    p2p_relay_hdr_t *alv_hdr = (p2p_relay_hdr_t*)ITEM2BUF(alv_item);
    alv_hdr->type = P2P_RLY_ALV;
    alv_hdr->size = 0;

    return true;
}

// 释放 client
void
relay_free_client(relay_client_t *client) {

    while (client->base.sessions)
        relay_close_session((relay_session_t*)client->base.sessions, true, true);
    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;
    client->sending_sess = NULL;

    // 释放 recv buf
    if (client->recv_buf) {
        free_buffer(BUF2ITEM(client->recv_buf));
        client->recv_buf = NULL;
    }

    client->sending_offset = 0;     // 确保正在发送中的数据包也被清除
    clear_client_sending(client);

    free_client_base(&client->base);
}

// 优雅的关闭 client
static void client_off(relay_client_t *client) {

    // 中断所有 session
    while (client->base.sessions)
        relay_close_session((relay_session_t*)client->base.sessions, false, true);
    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;
    client->sending_sess = NULL;

    // 释放 recv buf
    if (client->recv_buf) {
        free_buffer(BUF2ITEM(client->recv_buf));
        client->recv_buf = NULL;
    }

    // 如果发送队列不为空，标记为 closing（send 完成后会自动完成 term），否则直接 term
    if (client->send_buff_head) {
        client->handshake = TCP_HS_FLAG_CLOSING;
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

    // 通过超时机制来释放 client
    client->base.last_active = P_tick_ms();
}

static void client_fatal(relay_client_t *client) {

    while (client->base.sessions)
        relay_close_session((relay_session_t*)client->base.sessions, true, true);
    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;
    client->sending_sess = NULL;

    // 清除除了正在发送的包以外的所有待发送数据
    clear_client_sending(client);

    if (!client->last_error)
        client->last_error = P2P_RLY_ERR_INTERNAL;

    // 追加 fatal 作为最后一项
    if (client->send_buff_head) {
        client->send_buff_head->next = ((buffer_item_t*)&g_relay_fatal);
        assert((client->io & TCP_IO_FLAG_WANT_WRITE));
    }
    else {
        client->send_buff_head = (buffer_item_t*)&g_relay_fatal;
        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }
    client->send_buff_rear = (buffer_item_t*)&g_relay_fatal;

    client->io &= ~TCP_IO_FLAG_WANT_READ;                // 停止接收数据
}

static void client_error(relay_client_t *client, uint8_t req_type) {

    assert(client->last_error);

    buffer_item_t *err_item = alloc_buffer(BUF_FLAG_512(0));
    if (!err_item) {
        print("F:", LA_F("send failed(OOM)\n", LA_F100, 100));
        client_fatal(client);
        return;
    }

    uint16_t payload_len = P2P_RLY_STA_PSZ(0, 0);
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(err_item);
    hdr->type = P2P_RLY_STA;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = req_type;
    p[1] = client->last_error;

    // 标记该 buf_item 是 error 包
    err_item->refer = ITEM_REF_CLIENT_ERROR;

    // 如果当前正在发送 client 队列的数据包，将 err_item 作为正在发送的数据包的下一项
    if (client->sending_offset && !client->sending_sess) { assert(client->send_buff_head);

        err_item->next = client->send_buff_head->next;
        client->send_buff_head->next = err_item;
        if (!err_item->next) client->send_buff_rear = err_item;

        assert((client->io & TCP_IO_FLAG_WANT_WRITE));
    }
    // 否则将 err_item 作为 client 发送队列的第一项
    else {

        err_item->next = client->send_buff_head;
        client->send_buff_head = err_item;
        if (!client->send_buff_rear) client->send_buff_rear = err_item;

        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }

    client->io &= ~TCP_IO_FLAG_WANT_READ;                // 停止接收数据
}

///////////////////////////////////////////////////////////////////////////////

// 处理 SYN0 消息（首次同步）
// payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
static void relay_handle_syn0(relay_client_t *client, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYN0";

    if (len < P2P_RLY_SYN0_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        relay_send_status(client, P2P_RLY_REG, P2P_RLY_ERR_PROTOCOL);
        return;
    }
    if (!*payload) {
        print("E:", LA_F("%s: invalid remote id\n", LA_F142, 142), PROTO);
        relay_send_status(client, P2P_RLY_REG, P2P_RLY_ERR_PROTOCOL);
        return;
    }
    uint8_t cand_count = payload[P2P_PEER_ID_MAX];
    uint32_t expect_len = P2P_RLY_SYN0_PSZ(cand_count);
    if (len != expect_len) {
        print("E:", LA_F("%s: bad payload(cnt=%d, len=%u, expected=%u)\n", LA_F39, 39),
               PROTO, cand_count, len, expect_len);
        relay_send_status(client, P2P_RLY_REG, P2P_RLY_ERR_PROTOCOL);
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
        client_fatal(client);
        return;
    }
    if (side < E_NONE) {
        print("E:", LA_F("%s: build session to '%s' failed(%d)\n", LA_F44, 44), PROTO, (const char *)payload, side);
        // todo syn0 status 必要性
        relay_send_syn0_status(client, (const char *)payload, P2P_RLY_ERR_PROTOCOL);
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
            client_fatal(client);
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

        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(syn0_item);
        client->recv_len = 0;
        syn0_item = item;
    }
    else {

        syn0_item = alloc_buffer(BUF_FLAG_512(0));
        if (!syn0_item) {
            print("E:", LA_F("%s: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            client_fatal(client);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(syn0_item));
        hdr->type = P2P_RLY_SYN0;
        hdr->size = htons(P2P_RLY_SYN0_S2C_PSZ(0));
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
        relay_session_send(remote_s, syn0_item);

        //-------

        assert(remote_s->sync_peer_send_cnt == 1 && remote_s->sync_peer_send[0]->refer == NULL); // 对端肯定存在缓存的 SYN0

        buffer_item_t *remote_syn0_item = remote_s->sync_peer_send[0];
        hdr = (p2p_relay_hdr_t *)ITEM2BUF(remote_syn0_item);

        // 对端 SYN0 转发给本端前，需要写入本端 session_id（位于 source_name 之后）
        sid = (uint8_t*)(hdr+1) + P2P_PEER_ID_MAX;
        nwrite_l(sid, local_s->base.session_id);

        // 添加到本端发送队列，也要设置 refer
        remote_syn0_item->refer = remote_s;
        relay_session_send(local_s, remote_syn0_item);

        //-------

        // 双方 SYN0 互相转发完成；各自收到对方的 SYN0 即为隐式 ACK，无需单独发 confirm

        print("I:", LA_F("%s: %s <-> %s forward\n", LA_F48, 48),
              PROTO, client->base.local_peer_id, (const char *)payload);
    }
}

// 处理 SYNC 消息（候选同步 / C→S confirm）
// 上行: [session_id(P2P_SESS_ID_SZ)][sid(1)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
// confirm: [session_id(P2P_SESS_ID_SZ)][sid(1)][confirmed_count(1)]  ← 客户端确认收到服务器下发的 SYNC
static void relay_handle_sync(relay_client_t *client, relay_session_t *session, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC";

    if (len < P2P_RLY_SYNC_CONFIRM_PSZ) {
        print("E:", LA_F("%session: bad payload(len=%u)\n", LA_F156, 156), PROTO, len);
        return;
    }

    uint8_t sid = payload[P2P_SESS_ID_SZ];

    // C→S confirm：客户端确认收到服务器下发（转发）的 SYNC，len 恰好等于 confirm 包大小
    if (len == P2P_RLY_SYNC_CONFIRM_PSZ) {
        print("V:", LA_F("%session: ses_id=%u, confirm sid=%u\n", LA_F165, 165),
              PROTO, session->base.session_id, sid);

        // 对端（B）应用层 ACK：释放暂存缓冲区，并将 confirm 转发给原发送方（A）
        if (PEER_ONLINE(session)) {
            relay_session_t *peer = RELAY_PEER(session);  // peer = A（原始发送方）
            if (peer->sync_peer_send_cnt > 0 && peer->sync_peer_send[0]->refer == ITEM_REF_ACK_PENDING) {
                relay_session_send_sync_confirm(peer, sid);
                free_buffer(peer->sync_peer_send[0]);
                peer->sync_peer_send[0] = peer->sync_peer_send[1];
                peer->sync_peer_send[1] = NULL;
                peer->sync_peer_send_cnt--;
                // ACK 到达，启动 A 下一个待发的 SYNC（如有）
                if (peer->sync_peer_send_cnt > 0 && TCP_PEER_REACHABLE(peer))
                    relay_peer_send(peer, peer->sync_peer_send[0]);
            }
        }
        return;
    }

    uint8_t cand_count = payload[P2P_SESS_ID_SZ + 1];
    uint16_t payload_sz = P2P_RLY_SYNC_PSZ(cand_count, false);
    if (len == payload_sz + 1u) {

        if (payload[payload_sz] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%session: bad FIN marker=0x%02x\n", LA_F38, 38), PROTO, payload[payload_sz]);
            return;
        }
    }
    else if (len != payload_sz) {

        print("E:", LA_F("%session: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n", LA_F40, 40),
               PROTO, sid, (unsigned)cand_count, len, payload_sz);
        return;
    }

    // 去重：重复包重发 confirm 即可，不重复转发
    if (sid != 0 && sid == session->last_sid) {
        print("W:", LA_F("%session: ses_id=%u, dup sid=%u, resend confirm\n", LA_F145, 145),
              PROTO, session->base.session_id, sid);
        relay_session_send_sync_confirm(session, sid);
        return;
    }
    session->last_sid = sid;

    // SYN0 隐式 ACK：本端首个 SYNC 上行视为对服务器下发 SYN0 的确认
    if (PEER_ONLINE(session)) {
        relay_session_t *peer = RELAY_PEER(session);
        if (peer->sync_peer_send_cnt > 0 && peer->sync_peer_send[0]->refer == ITEM_REF_ACK_PENDING) {
            p2p_relay_hdr_t *sent_hdr = (p2p_relay_hdr_t*)ITEM2BUF(peer->sync_peer_send[0]);
            if (sent_hdr->type == P2P_RLY_SYN0) {
                free_buffer(peer->sync_peer_send[0]);
                peer->sync_peer_send[0] = peer->sync_peer_send[1];
                peer->sync_peer_send[1] = NULL;
                peer->sync_peer_send_cnt--;
                // ACK 到达，启动对端（A）下一个待发的 SYNC（如有）
                if (peer->sync_peer_send_cnt > 0 && TCP_PEER_REACHABLE(peer))
                    relay_peer_send(peer, peer->sync_peer_send[0]);
            }
        }
    }

    print("V:", LA_F("%session: ses_id=%u, sid=%u, cands=%d\n", LA_F165, 165),
          PROTO, session->base.session_id, sid, cand_count);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync_item = alloc_buffer(BUF_FLAG_MTU(0));
        if (!sync_item) {
            print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            client_fatal(client);
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
            print("E:", LA_F("%session: alloc buffer failed(OOM)\n", LA_F28, 28), PROTO);
            client_fatal(client);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync_item));
        hdr->type = P2P_RLY_SYNC;
        hdr->size = htons(P2P_RLY_SYNC_PSZ(0, true));

        uint8_t *p = (uint8_t*)(hdr+1) + P2P_SESS_ID_SZ;
        p[0] = sid;                     // 透传 sid
        p[1] = 0;                       // cand_count = 0
        p[2] = P2P_RLY_SYNC_FIN_MARKER;
    }

    // 交换写入对端的 session_id
    uint8_t *sid_ptr = (uint8_t *)(hdr + 1);
    nwrite_l(sid_ptr, session->base.peer->session_id);

    assert(session->sync_peer_send_cnt < RELAY_PEER_Q_MAX);
    session->sync_peer_send[session->sync_peer_send_cnt++] = sync_item;
    // 如果这是队列中的首个项且对端可达，立即发送
    if (session->sync_peer_send_cnt == 1 && TCP_PEER_REACHABLE(session)) {
        relay_peer_send(session, session->sync_peer_send[0]);
    }
}

// 处理 FIN 消息（会话结束）
// payload: [session_id(P2P_SESS_ID_SZ)]
static void relay_handle_fin(relay_session_t *session) {
    const char *PROTO = "FIN";

    print("I:", LA_F("%session: close ses_id=%u\n", LA_F158, 158), PROTO, session->base.session_id);

    // 向对端发送 FIN 说明本端已经关闭了连接
    // + 销毁本端的 sess（销毁操作会向对端发送 FIN）
    relay_close_session(session, true, false);
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

    assert(session->pkt_peer_send_cnt < RELAY_PEER_Q_MAX);
    session->pkt_peer_send[session->pkt_peer_send_cnt++] = buf_item;
    // 队列未满：立即发 READY（上游可立即发下一个）；队满：等队头写完后再发（由 relay_peer_sent 处理）
    if (session->pkt_peer_send_cnt < RELAY_PEER_Q_MAX)
        relay_session_send_status(session, P2P_RLY_PKT, P2P_RLY_CODE_READY);
    // 如果这是队列中的首个项且对端可达，立即发送
    if (session->pkt_peer_send_cnt == 1 && TCP_PEER_REACHABLE(session))
        relay_peer_send(session, session->pkt_peer_send[0]);
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

// 处理 RELAY 模式信令（TCP 长连接）- 统一接收+分发架构
void relay_handle_recv(relay_client_t *client) {
    assert(client->base.proto == PROTO_RELAY);
    assert(client->recv_buf);

    client->base.last_active = P_tick_ms(); uint8_t type = 0;
    for(;;client->recv_len = 0) {

        // 握手写阶段，禁止接收新消息（此时应该已经取消了 TCP_IO_FLAG_WANT_READ）
        // + 相应的，该阶段的 recv_buf 会被 handshake ack 复用作为 send_buf
        assert(!client->handshake || (TCP_HS_IS_HANDSHAKING(client) && !(client->io & TCP_IO_FLAG_WANT_WRITE)));

        // 读取 header (3字节)
        while (client->recv_len < sizeof(p2p_relay_hdr_t)) {
            size_t need = sizeof(p2p_relay_hdr_t) - client->recv_len;
            int rc = tcp_recv((tcp_client_t*)client, client->recv_buf + client->recv_len, &need);
            if (rc > 0) return;
            if (rc < 0) {
                client->last_error = P2P_RLY_ERR_IO;
                if (TCP_HS_IS_HANDSHAKING(client)) goto error_handshake;
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
            if (TCP_HS_IS_HANDSHAKING(client)) goto error_handshake;
            goto error;
        }

        // 读取完整 payload
        uint16_t total_need = sizeof(p2p_relay_hdr_t) + payload_len;
        while (client->recv_len < total_need) {
            size_t need = total_need - client->recv_len;
            int rc = tcp_recv((tcp_client_t*)client, client->recv_buf + client->recv_len, &need);
            if (rc > 0) return;
            if (rc < 0) {
                client->last_error = P2P_RLY_ERR_IO;
                if (TCP_HS_IS_HANDSHAKING(client)) goto error_handshake;
                goto error;
            }
            client->recv_len += (uint16_t)need;
        }
        uint8_t *payload = client->recv_buf + sizeof(p2p_relay_hdr_t);

        // 分发处理
        if (type == P2P_RLY_REG) { const char* PROTO = "REG";

            // 重复 REG（当前未处于握手阶段）
            // + local_peer_id 是否为空，等价于 handshake recv 阶段
            if (!TCP_HS_IS_HANDSHAKING(client)) { assert(client->base.local_peer_id[0]);
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
            assert(!client->sending_sess);
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

                if (TCP_HS_IS_HANDSHAKING(reg)) {
                    client->last_error = P2P_RLY_ERR_INVALID;
                    print("E:", LA_F("%s: request simultaneously for '%s'\n", 0, 0), PROTO, reg->base.local_peer_id);
                    goto error_handshake;
                }

                uint8_t* recv_buf = client->recv_buf; uint16_t recv_len = client->recv_len;
                client->recv_buf = NULL; client->recv_len = 0;

                // 如果 instance_id 一致（断网重连）
                if (resident_client(&reg->base, PROTO_RELAY, instance_id, &client->base)) {
                    
                    client = reg;
                    client->last_error = 0;                     // 重置错误状态
                    client->io |= TCP_IO_FLAG_WANT_READ;        // 重新激活读取（之前断网时会被关闭）
                    if (client->send_buff_head || client->send_sess_head)
                        client->io |= TCP_IO_FLAG_WANT_WRITE;   // 如果存在未完成的发送，重新激活写入

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

                // 如果 would block，则标记进入握手写入阶段
                if (r > 0) {
                    // 进入握手写阶段，同时暂停接收数据，等握手（ACK 发送）完成后再继续
                    client->io &= ~TCP_IO_FLAG_WANT_READ;
                    client->io |= TCP_IO_FLAG_WANT_WRITE;
                    client->recv_len = ack_len;
                    return;
                }
            }
        }
        // 除 REG 外，所有请求都要求当前已经完成握手
        else if (client->handshake != 0) { assert(TCP_HS_IS_HANDSHAKING(client));   // closing 已经禁止接收了
            print("E:", LA_F("%s: rejected for not reg\n", LA_F147, 147), PROTO_STR(type));
            goto error_handshake;
        }
        else if (type == P2P_RLY_OFF) {
            print("I:", LA_F("%s: '%s'\n", LA_F72, 72), PROTO_STR(type), client->base.local_peer_id);
            client_off(client);
            return;
        }
        // 心跳包：使用预分配内嵌缓冲，高优先级插队，去重（上一个未发完则忽略）
        else if (type == P2P_RLY_ALV) {
            buffer_item_t *alv_item = (buffer_item_t*)client->alv_ack_buf;
            if (alv_item->refer == ITEM_REF_ALV_ACK) {
                print("V:", LA_F("%s: prev ALV ACK still pending, skip\n", LA_F166, 166), "ALV");
            } else {
                alv_item->refer = ITEM_REF_ALV_ACK;
                alv_item->next  = NULL;
                // 高优先级：插到当前正在发送包之后（如有），否则插到队头
                if (client->sending_offset && !client->sending_sess) { assert(client->send_buff_head);
                    alv_item->next = client->send_buff_head->next;
                    client->send_buff_head->next = alv_item;
                    if (!alv_item->next) client->send_buff_rear = alv_item;
                } else {
                    alv_item->next = client->send_buff_head;
                    client->send_buff_head = alv_item;
                    if (!client->send_buff_rear) client->send_buff_rear = alv_item;
                    client->io |= TCP_IO_FLAG_WANT_WRITE;
                }
            }
        }
        else if (type == P2P_RLY_SYN0) {
            relay_handle_syn0(client, payload, payload_len);
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
            // SYNC 队满则返回 BUSY
            else if (type == P2P_RLY_SYNC && session->sync_peer_send_cnt >= RELAY_PEER_Q_MAX) {
                print("W:", LA_F("%s: ses_id=%u busy (pending sync)\n", LA_F145, 145), PROTO_STR(type), session_id);
                relay_session_send_status(session, type, P2P_RLY_ERR_BUSY);
            }
            // PKT 队满则返回 BUSY
            else if (type == P2P_RLY_PKT && session->pkt_peer_send_cnt >= RELAY_PEER_Q_MAX) {
                print("W:", LA_F("%s: ses_id=%u busy (pending pkt)\n", LA_F145, 145), PROTO_STR(type), session_id);
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
    client->recv_len = 0;           // 清除已读取的部分
    client_unreachable(client);     // 执行 unreachable 处理
    client_error(client, type);     // 执行 error 处理（会根据 last_error 进行相应处理）
    return;
// 大多数 Internal(例如 OOM) 错误，它们会导致（之前或之后的）数据完整性被破坏，或无法再继续安全的运行
// + 此时会清除所有发送队列（因为数据完整性已破坏，即使发送过去也没有意义），
//   但除了正在发送的数据包（因为得保证传输的数据的完整性，否则后面的 fatal 应答包就没有意义）
error_fatal:
    assert(client->last_error);
    client_fatal(client);
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
    hdr->size = P2P_RLY_STA_PSZ(0, 0);
    size_t len = sizeof(p2p_relay_hdr_t);
    client->recv_buf[len] = P2P_RLY_REG;
    client->recv_buf[len+1] = client->last_error;
    len += hdr->size; hdr->size = htons(P2P_RLY_STA_PSZ(0, 0));

    ssize_t r = tcp_send((tcp_client_t*)client, client->recv_buf, &len, "REG_ACK");
    if (r > 0) {
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

    // 如果当前处于握手阶段
    // + 此时会复用 recv_buf 作为 send_buf，recv_len 作为已发送长度
    if (TCP_HS_IS_HANDSHAKING(client)) {

        size_t ack_sz = sizeof(p2p_relay_hdr_t) + P2P_RLY_REG_S2C_PSZ;
        size_t len = ack_sz - client->recv_len;
        int rc = tcp_send((tcp_client_t*)client, client->recv_buf + client->recv_len, &len, "REG_ACK");
        if (rc < 0) {
            relay_free_client(client);    // 握手阶段发送失败，直接释放 client
            return;
        }

        // 握手应答发送完成
        if (len > 0 && (client->recv_len += len) >= ack_sz) { client->recv_len = 0;

            // 握手完成
            client->handshake = 0;
            print("V:", LA_F("%s sent to '%s'\n", LA_F179, 179), "REG_ACK", client->base.local_peer_id);

            // 如果（发送的是）握手阶段的错误应答，直接释放 client
            if (client->last_error) {
                relay_free_client(client);
                return;
            }

            // 启动正常读写（握手完成）
            client->io |= TCP_IO_FLAG_WANT_READ;
            client->io &= ~TCP_IO_FLAG_WANT_WRITE;      // 初始时还没有要写入的数据
        }

        // REG_ACK 未完成时，跳过其他处理
        if (client->handshake) return;
    }

    relay_session_t *sending_session = client->send_sess_head; buffer_item_t *item = client->send_buff_head;
    if (sending_session || item) { assert(client->io & TCP_IO_FLAG_WANT_WRITE);

        // 如果正在发送 session 数据包（mid-packet），继续发送当前 session
        if (client->sending_sess && client->sending_offset) { assert(sending_session);
            sending_session = client->sending_sess;
            item = sending_session->send_head;
        }
        // 否则优先发送 client 级别的包；没有 client 包时，从游标 session 发送
        else if (!item) { assert(sending_session);
            if (!client->sending_sess) client->sending_sess = sending_session;
            else sending_session = client->sending_sess;
            item = sending_session->send_head;
        }

        const p2p_relay_hdr_t *hdr = (const p2p_relay_hdr_t *)ITEM2BUF(item);
        const uint16_t len = (uint16_t)(sizeof(p2p_relay_hdr_t) + ntohs(hdr->size));
        size_t send_sz = len - client->sending_offset;
        int rc = tcp_send((tcp_client_t*)client, (const char *)hdr + client->sending_offset, &send_sz, PROTO_STR(hdr->type));
        if (rc < 0) {

            // 如果当前发生了 fatal 错误，则发送失败后直接销毁 client
            if (item == (buffer_item_t*)g_relay_fatal || item->next == (buffer_item_t*)g_relay_fatal) {
                relay_free_client(client);
                return;
            }

            client->sending_offset = 0;     // 清除已发送的部分
            client_unreachable(client);     // 执行 unreachable 处理

            // 直接关闭连接
            client->last_error = P2P_RLY_ERR_IO;
            P_sock_close(client->base.fd);
            client->base.fd = P_INVALID_SOCKET;

            // 此时不再返回错误状态码了，直接停止读写（等待重连或超时回收）
            client->io &= ~(TCP_IO_FLAG_WANT_READ|TCP_IO_FLAG_WANT_WRITE);
            return;
        }

        // 当前 item 发送完成
        if (send_sz > 0 && (client->sending_offset += (int)send_sz) >= len) { client->sending_offset = 0;

            // 如果是 client 级的 item
            if (item==client->send_buff_head) {

                // 发送 client sending 队列的下一项
                if (!((client->send_buff_head = item->next)))
                    client->send_buff_rear = NULL;

                // 如果发送的是 fatal 错误包，发送完成后直接销毁 client
                if (item == (buffer_item_t*)g_relay_fatal) {
                    relay_free_client(client);
                    return;
                }

                bool is_error_item = (item->refer == ITEM_REF_CLIENT_ERROR);
                bool is_alv_item   = (item->refer == ITEM_REF_ALV_ACK);

                // ALV ACK 使用内嵌缓冲，不 free；其余正常释放
                if (is_alv_item) item->refer = NULL;
                else free_buffer(item);

                // 如果发送的是错误包，发送完成后直接关闭连接并停止写入（等待客户端重连或超时回收）
                if (is_error_item) { assert(client->last_error && !(client->io & TCP_IO_FLAG_WANT_READ));
                    P_sock_close(client->base.fd);
                    client->base.fd = P_INVALID_SOCKET;
                    client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                    return;
                }
            }
            // 对于 session 级的 item
            else { assert(item==sending_session->send_head && item != (buffer_item_t*)g_relay_fatal);

                // 如果发送的是对端发过来的数据
                if (item->refer) { assert(item->refer != ITEM_REF_ACK_PENDING && item->refer != ITEM_REF_CLIENT_ERROR);
                    relay_peer_sent((relay_session_t*)item->refer, item);
                }

                // 将当前 session 的 sending 队列切换到下一项，如果发送队列不为空
                if ((sending_session->send_head = item->next)) {

                    // 轮询下一个（session sending 队列不为空的）session
                    client->sending_sess = sending_session->send_next ? sending_session->send_next : client->send_sess_head;
                }
                // 如果当前 session 发送队列已空，从 client 中移除该 session
                // + 同时切换到下一个（session sending 队列不为空的）session
                else { sending_session->send_rear = NULL;

                    relay_session_t *next = sending_session->send_next;
                    if (sending_session->send_prev) sending_session->send_prev->send_next = next;
                    else client->send_sess_head = next;
                    if (next) {
                        next->send_prev = sending_session->send_prev;
                        client->sending_sess = next;
                    }
                    else {
                        client->send_sess_rear = sending_session->send_prev;
                        client->sending_sess = client->send_sess_head;
                    }
                    sending_session->send_next = NULL;
                    sending_session->send_prev = NULL;
                }

                // 如果 item 没有被（relay_peer_sent）标记为待 ACK 状态，则直接释放
                if (item->refer != ITEM_REF_ACK_PENDING) free_buffer(item);
            }

            // 如果全部发送完成
            if (!client->send_sess_head && !client->send_buff_head) {
                client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                if (!(client->io & TCP_IO_FLAG_WANT_READ)) {
                    if (TCP_HS_IS_CLOSING(client)) relay_free_client(client);
                    else { P_sock_close(client->base.fd);
                        client->base.fd = P_INVALID_SOCKET;
                    }
                }
            }
        }
    }
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
