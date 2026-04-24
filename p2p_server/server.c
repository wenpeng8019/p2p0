/*
 * P2P 信令服务器
 *
 * 支持两种信令模式：
 *
 * 1. COMPACT 模式 (UDP)
 *    - 对应客户端 p2p_signal_compact 模块
 *    - 无状态信令，基于 UDP 数据包交换
 *    - 紧凑集成：信令交换 + NAT端口检测 + 候选交换 + 数据中继，一个完整的 P2P 统一协议实现
 *
 * 2. RELAY 模式 (TCP)
 *    - 对应客户端 p2p_signal_relay 模块
 *    - 有状态信令，基于 TCP 长连接
 *    - 支持在线状态查询、以及基本数据中转功能，用于支持 ICE/STUN/TURN 协议架构实现的信令服务器
 *
 * 3. 关于 WSLAY 协议处理的关键机制
 *    - write_enabled（即 wslay_event_get_write_enabled）被置为 false 的时机
 *      > 在 opcode(WSLAY_CONNECTION_CLOSE) 写入完成后、或报错、或主动执行 wslay_event_shutdown_write
 *    - wslay_event_want_write() == true
 *      > write_enabled 且发送缓冲不为空
 *        注意，wslay_event_send 写入报错，不会清除发送缓冲队列，只会将 write_enabled 设置为 false
 *    - read_enabled（即 wslay_event_get_read_enabled）被置为 false 的时机
 *      > 报错、或主动执行 wslay_event_shutdown_read、或收到对端关闭（WSLAY_CONNECTION_CLOSE）
 *    - wslay_event_want_read() 等价于 read_enabled
 *
 *    + 关于关闭握手协议的处理：ws 协议要求一端在发送 close 协议字后，需等待对端返回 close 协议字，即双向确认关闭。
 *      > wslay 在收到对端的 WSLAY_CONNECTION_CLOSE 后会自动构造一个 close 命令到发送队列，并将 read_enabled 置为 false
 *      > wslay 在将 close 命令（收到 close 自动回复、或服务器主动发的）实际发送完成后会自动将 write_enabled 置为 false
 *      > 注意, 发送完 close 命令后，wslay 不会自动关闭 socket；主动发 close 也不会自动将 read_enabled 置为 false
 *        因为协议上允许在发送 close 后继续读取对端数据，直到收到对端 close 后才真正关闭连接。
 *        同样的，在主动发送 close 后，wslay 也不会为对方返回的 close 命令设置超时处理。
 */

#define MOD_TAG "P2P0d"

#include "p2p_compact.h"
#include "p2p_relay.h"
#ifdef WITH_WSLAY
#include "ws.h"
#include "p2p_wss.h"
#endif

#include "LANG.cn.h"

#include <signal.h>    /* signal() */

// 命令行参数定义
ARGS_I(false, port,         'p', "port",       LA_CS("Signaling server listen port (TCP+UDP)", LA_S9, 9));
ARGS_I(false, probe_port,   'P', "probe-port", LA_CS("NAT type detection port (0=disabled)", LA_S6, 6));
#ifdef WITH_WSLAY
ARGS_B(false, ws,           'w', "ws",         LA_CS("Enable WebSocket service on same TCP port", LA_S150, 150));
ARGS_I(false, ws_port,      'W', "ws-port",    LA_CS("WebSocket dedicated port (also enables --ws)", LA_S151, 151));
#endif
ARGS_B(false, relay,        'r', "relay",      LA_CS("Enable data relay support (COMPACT mode fallback)", LA_S4, 4));
ARGS_B(false, msg,          'm', "msg",        LA_CS("Enable MSG RPC support", LA_S5, 5));

static void cb_cn(const char* argv) { (void)argv;  lang_cn(); }
ARGS_PRE(cb_cn, cn,          0,  "cn",          LA_CS("Use Chinese language", LA_S10, 10));

//-----------------------------------------------------------------------------

static session_t*                   g_sessions = NULL;
static session_pair_t*              g_session_pairs = NULL;

static union {
    compact_client_t    compact;
    relay_client_t      relay;
    wss_client_t        wss;
}                                   g_client_slots[MAX_PEERS];
static client_t*                    g_clients = NULL;

#define as_client(i)       ((client_t*)&g_client_slots[i])
#define as_tcp_client(i)   ((tcp_client_t*)&g_client_slots[i])
#define as_ws_client(i)    ((ws_client_t*)&g_client_slots[i])

//-----------------------------------------------------------------------------

static buffer_item_t*               g_recycle[8];

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t        g_running = 1;

///////////////////////////////////////////////////////////////////////////////

#define CHUNK_BASE_SIZE  512

// 分配 frame buf
buffer_item_t* alloc_buffer(uint8_t flags) {
    int idx = flags >> 5;
    buffer_item_t *item = g_recycle[idx];
    if (item) g_recycle[idx] = item->next;
    else if (!((item = (buffer_item_t*)malloc(sizeof(buffer_item_t) + CHUNK_BASE_SIZE*(1<<idx))))) return NULL;
    item->flags = flags;
    item->refer = NULL;
    return item;
}

// 释放 frame buf
void free_buffer(buffer_item_t *buf_item) {
    int idx = buf_item->flags >> 5;
    buf_item->next = g_recycle[idx];
    g_recycle[idx] = buf_item;
}

///////////////////////////////////////////////////////////////////////////////

client_t*
find_client(const char *local_peer_id) {
    client_t *client = NULL;
    HASH_FIND_STR(g_clients, local_peer_id, client);
    return client;
}

bool
identify_client(client_t* c) {
    assert(c && c->proto >= 0);
    if (c->local_peer_id[0] == '\0') return false;
    assert(!find_client(c->local_peer_id));  // 注册前必须确保 local_peer_id 不存在
    HASH_ADD_STR(g_clients, local_peer_id, c);
    return true;
}

static inline void init_client(client_t* c, sock_t fd) {
    c->local_peer_id[0] = '\0';
    c->instance_id = 0;
    c->last_active = P_tick_ms();
    c->fd = fd;
    assert(!c->sessions);
}

client_t*
alloc_client(int8_t proto, sock_t fd) {

    for (int k = 0; k < MAX_PEERS; k++) { client_t* c = as_client(k);
        if (c->proto < 0) {
            c->proto = proto;
            init_client(c, fd);
            return c;
        }
    }
    return NULL;
}

void
free_client_base(client_t *c, void(*free_session)(session_t *s)) {

    while (c->sessions) free_session(c->sessions);

    if (c->hh.tbl) HASH_DELETE(hh, g_clients, c);

    // > PROTO_COMPACT 说明是 TCP 连接
    if (c->proto > PROTO_COMPACT) {

        if (c->proto == PROTO_WSS) {
            if (((ws_client_t*)c)->buf) {
                free_buffer(((ws_client_t*)c)->buf);
                ((ws_client_t*)c)->buf = NULL;
            }
            if (((ws_client_t*)c)->ws_ctx) {
                wslay_event_context_free(((ws_client_t*)c)->ws_ctx);
                ((ws_client_t*)c)->ws_ctx = NULL;
            }
        }

        if (c->fd != P_INVALID_SOCKET) {
            P_sock_close(c->fd);
            c->fd = P_INVALID_SOCKET;
        }
    }
    c->proto = -1;
}

void
free_client(client_t *c) {
    assert(c && c->proto >= 0);
    if (c->proto == PROTO_COMPACT) compact_free_client((compact_client_t*)c);
    else if (c->proto == PROTO_RELAY) relay_term_client((relay_client_t *)c, true);
    else if (c->proto == PROTO_WSS) wss_term_client((wss_client_t*)c, true);
    else assert(0 && "Invalid client proto");
}

bool
resident_client(client_t* client, int8_t proto, uint32_t instance_id, client_t* from) {

    assert(client != from);

    assert(!from || !from->sessions);

    // 此时说明之前的 client 连接已经断开，客户端发起新的连接，但状态保留
    if (client->proto == proto && client->instance_id == instance_id) {

        // 如果存在新分配的 from client，迁移 fd 和 last_active 到旧的 client
        if (from) {

            // 同实例重连：迁移 fd 到旧槽位，保留会话状态
            print("I:", LA_F("REG: '%s' reconnected (inst=%u), migrating\n", LA_F95, 95),
                   client->local_peer_id, instance_id);

            if (client->fd != P_INVALID_SOCKET) P_sock_close(client->fd);
            client->fd = from->fd;
            from->fd = P_INVALID_SOCKET;

            client->last_active = from->last_active;

            free_client(from);
        }
        else client->last_active = P_tick_ms();

        return true;
    }

    free_client(client);

    if (!from) {
        client->proto = proto;
        client->instance_id = instance_id;
        client->last_active = P_tick_ms();
    }

    return false;
}


// 生成安全的随机 session_id（32位，加密安全，防止跨会话注入攻击）
static uint32_t
generate_session_id(void) {
    uint32_t id;
    session_t *existing;
    int attempts = 0;
    
    // 使用循环代替递归，避免极端情况下的栈溢出
    do {
        id = P_rand32();  // 使用 stdc.h 统一封装的加密安全随机数
        HASH_FIND(hh, g_sessions, &id, sizeof(uint32_t), existing);
        
        // 安全限制：虽然冲突概率极低（1/2^32），但在极端情况下提供保护
        if (++attempts > 1000) {
            print("F:", "Cannot generate unique session_id after 1000 attempts\n");
            exit(1);
        }
    } while (existing);
    
    return id;
}

session_t*
find_session(uint32_t session_id) {
    session_t *s = NULL;
    HASH_FIND(hh, g_sessions, &session_id, P2P_SESS_ID_SZ, s);
    return s;
}

// 创建新会话对
ret_t
pair_session(client_t *client, const char *remote_peer_id,
             session_t **local_s, session_t **remote_s, size_t session_type_size) {
    if (!client || !remote_peer_id || !local_s || !remote_s) return E_INVALID;
    *local_s = NULL;
    *remote_s = NULL;

    // 查找和对端的 sess pair
    char peer_key[3 * P2P_PEER_ID_MAX];
    memset(peer_key, 0, sizeof(peer_key));
    strncpy(peer_key, client->local_peer_id, P2P_PEER_ID_MAX);
    strncpy(peer_key + P2P_PEER_ID_MAX, remote_peer_id, P2P_PEER_ID_MAX);
    session_pair_t *pair = NULL;
    HASH_FIND(hh, g_session_pairs, peer_key, 2 * P2P_PEER_ID_MAX, pair);
    if (!pair) {
        strncpy(peer_key + 2 * P2P_PEER_ID_MAX, client->local_peer_id, P2P_PEER_ID_MAX);
        HASH_FIND(hh, g_session_pairs, peer_key + P2P_PEER_ID_MAX, 2 * P2P_PEER_ID_MAX, pair);
    }

    session_t *s = NULL; int side = -1;

    // 如果不存在和对端的 sess pair，此时对端肯定未发起和本端的连接
    if (!pair) {

        // 创建 sess pair
        pair = (session_pair_t*)calloc(1, sizeof(session_pair_t));
        if (!pair) return E_OUT_OF_MEMORY;

        // 分配本端 sess 对象
        s = (session_t*)calloc(1, session_type_size);
        if (!s) { free(pair); return E_OUT_OF_MEMORY; }

        pair->valid = true;
        memcpy(pair->peer_id[0], client->local_peer_id, P2P_PEER_ID_MAX);
        memcpy(pair->peer_id[1], remote_peer_id, P2P_PEER_ID_MAX);
        HASH_ADD_KEYPTR(hh, g_session_pairs, pair->peer_id, 2 * P2P_PEER_ID_MAX, pair);

        // 本端初始作为 sess pair 的 left side
        side = 0;
    }
    // 如果 sess pair 左侧被置位
    else if (pair->sessions[0]) {

        // 如果当前已经完成配对，重复执行 sync0
        if (pair->sessions[1]) return E_DUPLICATE;

        // 如果对端存在（本端之前已经脱离）
        if (pair->sessions[0]->client != client) {
            *remote_s = pair->sessions[0]; side = 1;    // 本端位于 sess pair 的 right side
        }
        // 如果本端存在，对端已脱离
        else if ((s = pair->sessions[0])->peer == (void*)-1) {
            *local_s = s; side = 0;
        }
        // 如果是本端重复发起连接，拒绝并返回错误
        else {
            print("E:", LA_F("Duplicate session create blocked: '%s' -> '%s'\n", LA_F81, 81),
                  client->local_peer_id, remote_peer_id);
            return E_DUPLICATE;
        }
    }
    else { assert(pair->sessions[1]);

        if (pair->sessions[1]->client != client) {
            *remote_s = pair->sessions[1]; side = 0;    // 本端位于 sess pair 的 left side
        } else if ((s = pair->sessions[1])->peer == (void*)-1) {
            *local_s = s; side = 1;
        } else {
            print("E:", LA_F("Duplicate session create blocked: '%s' -> '%s'\n", LA_F81, 81),
                  client->local_peer_id, remote_peer_id);
            return E_DUPLICATE;
        }
    }

    // 如果对端已脱离，本端重新发起新的连接
    // + 对顿脱离时，会将本端的 pear 置位 -1
    // + 此时对端不存在，本端执行自身重置
    if (*local_s) {

        s->peer = NULL;

        // 重新分配 sess id，并重建索引
        s->session_id = generate_session_id();
        HASH_DELETE(hh, g_sessions, s);
        HASH_ADD(hh, g_sessions, session_id, sizeof(uint32_t), s);
        return side;
    }

    // 分配本端 sess 对象
    if (!s) {
        s = (session_t*)calloc(1, session_type_size);
        if (!s) return E_OUT_OF_MEMORY;
    }

    // 如果本端主动断开后，又重新发起新的连接
    // + 主动断开时，会将对端的 peer 置位 -1
    // + 不同信令模式下，对端此时的状态可能不同
    //   > TCP 连接模式下，可能对方还没收到 FIN 通知，也就是上次会话的下行发送队列可能还没空。
    //     此时，新的会话 sync0 也会排队。所以会话数据是完整的。
    //   > UDP 连接模式下，会话数据完整性是应用层来维护的，所以之前在发送 FIN 时，会话发送的数据肯定已经完整。
    //     此时，新的会话 sync0 可以立即发送
    // ! 注意，上次主动断开对端时，必须将对端的（派生）会话中的数据状态重置
    if (*remote_s && remote_s[0]->peer == (void*)-1) {

        remote_s[0]->peer = NULL;

        // 重新分配 sess id，并重建索引
        remote_s[0]->session_id = generate_session_id();
        HASH_DELETE(hh, g_sessions, *remote_s);
        HASH_ADD(hh, g_sessions, session_id, sizeof(uint32_t), *remote_s);
    }

    // 将 sess 和 pair 绑定
    pair->sessions[side] = s; s->pair = pair;

    // 将 sess 和 client 绑定
    s->client = client;
    s->prev = NULL;
    s->next = client->sessions;
    if (client->sessions) client->sessions->prev = s;
    client->sessions = s;

    // 分配 sess id
    s->session_id = generate_session_id();
    HASH_ADD(hh, g_sessions, session_id, sizeof(uint32_t), s);
    
    *local_s = s;
    return side;
}

// 释放会话
void
free_session_base(session_t *s) {
    assert(s && s->pair);

    // 解除和 pair 的绑定关系
    session_pair_t *pair = s->pair;
    if (pair->sessions[0] == s) pair->sessions[0] = NULL; else pair->sessions[1] = NULL;

    // 如果对端之前在线，则标记对端会话已断开（-1）
    // + 注意，此时需要保留对端的 session id，以及和本端 session id 建立的 pair 关系
    if (PEER_ONLINE(s->peer)) {
        s->peer->peer = (session_t*)-1;
    }
    else if (!pair->sessions[0] && !pair->sessions[1]) {
        HASH_DELETE(hh, g_session_pairs, pair);
        free(pair);
    }

    // 解除和 client 的绑定关系
    client_t *client = s->client;
    if (client) {
        if (s->prev) s->prev->next = s->next;
        else         client->sessions = s->next;
        if (s->next) s->next->prev = s->prev;
        s->prev = s->next = NULL;
    }

    HASH_DELETE(hh, g_sessions, s);
    free(s);
}

///////////////////////////////////////////////////////////////////////////////

// UDP 发送 + 统一日志
ssize_t
udp_send(sock_t udp_fd, const void *buf, int len, const struct sockaddr_in *to, const char *PROTO) {
    ssize_t sent = sendto(udp_fd, (const char *)buf, len, 0,
                          (const struct sockaddr *)to, sizeof(*to));
    if (sent == (ssize_t)len)
        printf(LA_F("[UDP] %s send to %s:%d, len=%d\n", LA_F137, 137),
               PROTO, inet_ntoa(to->sin_addr), ntohs(to->sin_port), (int)sent);
    else
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F136, 136),
              PROTO, inet_ntoa(to->sin_addr), ntohs(to->sin_port), P_sock_errno());
    return sent;
}

/*
 * TCP 接收辅助函数：异步接收，遇到 WOULDBLOCK 立即返回
 * 返回: 0=全部接收完成, +1=WOULDBLOCK, -1=连接关闭(EOF), -2=真错误
 * r_sz: 输入=希望接收字节数，输出=实际接收字节数
 */
int
tcp_recv(tcp_client_t* client, void *buf, size_t *r_sz) {
    assert(client->base.proto >=0 && client->base.fd != P_INVALID_SOCKET);

    size_t len = *r_sz; *r_sz = 0;
    while (*r_sz < len) {
        ssize_t n = recv(client->base.fd, (char *)buf + *r_sz, len - *r_sz, 0);
        if (n == 0) {
            print("I:", LA_F("[TCP] conn closed (EOF on recv)\n", LA_F11, 11));
            return -1;
        }
        if (n < 0) {
            if (P_sock_is_interrupted()) continue;
            if (P_sock_is_wouldblock()) return 1;
            print("E:", LA_F("[TCP] recv failed(%d)\n", LA_F183, 183), P_sock_errno());
            return -2;
        }
        *r_sz += (size_t)n;
    }
    return 0;
}

/*
 * TCP 发送辅助函数：异步发送，遇到 WOULDBLOCK 则加入发送队列
 * 说明：用于发送小消息（ACK、header 等）
 *      先尝试立即发送，若发送缓冲区满则依赖主循环的异步发送机制
 * 返回: 0=全部发送完成, +1=WOULDBLOCK, -1=连接关闭(EOF), -2=真错误
 *      len_io: 输入=希望发送字节数，输出=实际发送字节数
 */
int
tcp_send(tcp_client_t* client, const void *buf, size_t *w_sz, const char *reason) {
    assert(client->base.proto >=0 && client->base.fd != P_INVALID_SOCKET);
    if (!w_sz) return -2;

    size_t len = *w_sz; *w_sz = 0;
    if (!reason) reason = "unknown";

    while (*w_sz < len) {
        ssize_t n = send(client->base.fd, (const char *)buf + *w_sz, len - *w_sz, 0);
        if (n == 0) {
            print("I:", LA_F("[TCP] conn closed (EOF on send, reason=%s)\n", LA_F182, 182), reason);
            return -1;
        }
        if (n < 0) {
            if (P_sock_is_interrupted()) continue;
            if (P_sock_is_wouldblock()) return 1;
            print("E:", LA_F("[TCP]  send(%s) failed(%d)\n", LA_F144, 144), reason, P_sock_errno());
            return -2;
        }
        *w_sz += (size_t)n;
    }
    return 0;
}

ret_t
ws_send_text(ws_client_t* client, const char *text) {
    assert(!client->ws_handshake);

    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_TEXT_FRAME;
    msg.msg        = (const uint8_t *)text;
    msg.msg_length = strlen(text);
    int r = wslay_event_queue_msg(client->ws_ctx, &msg);
    if (r < 0) {
        print("E:", LA_F("[WS] queue text msg failed(%d)\n", LA_F197, 197), r);
        return -1;
    }
    assert(wslay_event_want_write(client->ws_ctx));
    return 0;
}

ret_t
ws_send_data(ws_client_t* client, const uint8_t *data, size_t len) {
    assert(!client->ws_handshake);

    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_BINARY_FRAME;
    msg.msg        = data;
    msg.msg_length = len;
    int r = wslay_event_queue_msg(client->ws_ctx, &msg);
    if (r < 0) {
        print("E:", LA_F("[WS] queue text data failed(%d)\n", LA_F196, 196), r);
        return -1;
    }
    assert(wslay_event_want_write(client->ws_ctx));
    return 0;
}

ret_t
ws_close(ws_client_t *client, uint16_t code) {

    int r = wslay_event_queue_close(client->ws_ctx, code, NULL, 0);
    if (r < 0) {
        print("E:", LA_F("[WS] queue close(%u) proto failed(%d)\n", LA_F195, 195), code, r);
        return -1;
    }
    assert(wslay_event_want_write(client->ws_ctx));
    client->io |= WSS_IO_FLAG_CLOSING;
    client->base.last_active = P_tick_ms();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static ssize_t ws_cb_recv(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, int flags, void *ud) {
    (void)flags; if (!len) return 0;
    wss_client_t *client = (wss_client_t *)ud;

    if (client->buf) { uint8_t* buf0 = ITEM2BUF(client->buf);
        size_t n = client->len - client->pos;
        if (len < n) {
            memcpy(buf, buf0 + client->pos, len);
            client->pos += len;
            return (ssize_t)len;
        }
        memcpy(buf, buf0 + client->pos, n);
        free_buffer(client->buf); client->buf = NULL;
        client->pos = client->len = 0;
        return (ssize_t)n;
    }
    ssize_t n;
    do n = recv(client->base.fd, (char*)buf, (int)len, 0); while (n < 0 && P_sock_is_interrupted());
    if (n > 0) return n;
    wslay_event_set_error(ctx, n < 0 && P_sock_is_wouldblock() ? WSLAY_ERR_WOULDBLOCK : WSLAY_ERR_CALLBACK_FAILURE);
    client->io |= TCP_IO_FLAG_READ_BREAK;
    return -1;
}

static ssize_t ws_cb_send(wslay_event_context_ptr ctx, const uint8_t *data, size_t len, int flags, void *ud) {
    (void)flags; if (!len) return 0;
    wss_client_t *client = (wss_client_t *)ud;
    ssize_t n;
    do n = send(client->base.fd, (const char*)data, (int)len, 0); while (n < 0 && P_sock_is_interrupted());
    if (n > 0) return n;
    wslay_event_set_error(ctx, (n < 0 && P_sock_is_wouldblock()) ? WSLAY_ERR_WOULDBLOCK : WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
}

static void ws_cb_msg(wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg *arg, void *ud) {
    (void)ctx;
    wss_client_t *client = (wss_client_t *)ud;

    // 如果客户端执行了关闭操作
    // + 根据 ws 协议，客户端会等待服务器回复 close 协议字
    //   此时 wslay 会自动发送 close 协议字到发送队列，并将 read_enabled 置为 false
    if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
        client->io |= WSS_IO_FLAG_PEER_CLOSING;
        return;
    }

    if (arg->opcode == WSLAY_TEXT_FRAME) {
        /* wslay 分配堆内存，回调返回后立即释放，可安全写入；
         * 协议要求末尾为 '\n'，就地替换为 '\0' 使 msg 成为合法 C 字符串 */
        uint8_t *msg = (uint8_t *)arg->msg;
        if (arg->msg_length > 0 && msg[arg->msg_length - 1] == '\n') {
            msg[arg->msg_length - 1] = '\0';
            wss_handle_message(client, msg, arg->msg_length - 1);
        }
    } else if (arg->opcode == WSLAY_BINARY_FRAME) {
        wss_handle_data(client, arg->msg, arg->msg_length);
    }
}

static int handle_ws_handshake(ws_client_t* client) {

    const int RESP_BUF_SZ = 512;
    char* buf = (char*)ITEM2BUF(client->buf);
    size_t buf_sz = BUF_SIZE(client->buf->flags);

    // handshake 读取阶段
    if (client->ws_handshake > 0) {

        // buf 前面保留 512 字节空间作为 RESP_BUF，后面接收 HTTP 请求
        if (!client->len) {
            client->len = RESP_BUF_SZ;
            client->pos = RESP_BUF_SZ;
        }

        // 接收 HTTP 请求
        while (client->len < buf_sz-1) {

            ssize_t n = recv(client->base.fd, buf + client->len, buf_sz-1-client->len, 0);
            if (n == 0) {
                print("I:", LA_F("Client closed connection (EOF on recv during handshake)\n", LA_F79, 79));
                return -1;
            }
            if (n < 0) {
                if (P_sock_is_interrupted()) continue;
                if (P_sock_is_wouldblock()) return 1;
                print("E:", LA_F("recv failed during handshake: errno=%d\n", LA_F200, 200), P_sock_errno());
                return -2;
            }
            buf[client->len += n] = '\0';

            // 查找最近 '\r'
            if (buf[client->pos] != '\r') {
                char* p = strchr(buf + client->pos, '\r');
                if (!p) { client->pos = client->len; continue; }
                client->pos = p - buf;
            }
            LOOP_RN:
            if (client->pos + 4 > client->len) continue;
            if (strncmp(buf + client->pos, "\r\n\r\n", 4) == 0) break;
            char* p = strchr(buf + client->pos + 1, '\r');
            if (p) { client->pos = p - buf; goto LOOP_RN; }
            client->pos = client->len;
        }
        buf[client->pos] = 0;

        ret_t r = ws_accept(buf + RESP_BUF_SZ, buf, RESP_BUF_SZ, NULL);
        if (r < E_NONE) {
            print("E:", LA_F("WebSocket handshake failed: invalid request\n", LA_F143, 143));
            return -1;
        }

        // 标记进入 handshake 应答写入阶段
        client->ws_handshake = -1;

        // 如果 buf 包含了 HTTP 请求以外的后续（WebSocket 消息）数据
        client->pos += 4;
        if (client->len > client->pos) {
            // 这里将 pos/len 状态保存到 client->base.instance_id，该阶段 instance_id 还没有意义
            ((uint16_t*)&client->base.instance_id)[0] = client->pos;
            ((uint16_t*)&client->base.instance_id)[1] = client->len;
        } else client->base.instance_id = 0;

        client->len = r;
        client->pos = 0;
        client->io |= TCP_IO_FLAG_WANT_WRITE;       // 标记 io writing
        client->io &= ~TCP_IO_FLAG_WANT_READ;       // handshake 应答写入完成前，暂停读取
    }

    // handshake 应答写入阶段
    assert(client->ws_handshake < 0);
    while (client->pos < client->len) {

        ssize_t n = send(client->base.fd, buf + client->pos, client->len-client->pos, 0);
        if (n == 0) {
            print("I:", LA_F("Client closed connection (EOF on recv during handshake)\n", LA_F79, 79));
            return -1;
        }
        if (n < 0) {
            if (P_sock_is_interrupted()) continue;
            if (P_sock_is_wouldblock()) return 1;
            print("E:", LA_F("send failed during handshake: errno=%d\n", LA_F202, 202), P_sock_errno());
            return -2;
        }
        client->pos += n;
    }

    // 如果 handshake 读取阶段已经接收了 WebSocket 消息数据，恢复缓存状态
    if (client->base.instance_id) {
        client->pos =  ((uint16_t*)&client->base.instance_id)[0];
        client->len = ((uint16_t*)&client->base.instance_id)[1];
        client->base.instance_id = 0;
    } else {
        free_buffer(client->buf);
        client->buf = NULL;
        client->len = client->pos = 0;
    }

    // 写入完成，即 ws 握手完成，进入正常的 WebSocket 消息处理阶段
    client->io &= ~TCP_IO_FLAG_WANT_WRITE;
    client->ws_handshake = 0;

    static const struct wslay_event_callbacks cbs = {
        ws_cb_recv,
        ws_cb_send,
        NULL,
        NULL,
        NULL,
        NULL,
        ws_cb_msg
    };
    if (wslay_event_context_server_init(&client->ws_ctx, &cbs, client) != 0) {
        return -1;
    }

    assert(wslay_event_want_read(client->ws_ctx));

    // 启动读取（握手写入期间会暂停读取）
    client->io |= TCP_IO_FLAG_WANT_READ; int r;

    // 先执行一次读取（暂停期间可能有数据积压）
    // +  wslay recv 报错，会发送 close 帧数据包，而不是关闭 sock 连接
    assert(!(client->io & TCP_IO_FLAG_READ_BREAK));
    do {
        r = wslay_event_recv(client->ws_ctx);
        if (r != 0) {
            print("E:", LA_F("WebSocket recv callback error: errno=%d\n", LA_F180, 180), r);
            client->io &= ~TCP_IO_FLAG_WANT_READ;
            break;
        }
        if (!wslay_event_want_read(client->ws_ctx)) {
            client->io &= ~TCP_IO_FLAG_WANT_READ;
            break;
        }
    }
    while (!(client->io & TCP_IO_FLAG_READ_BREAK));
    client->io &= ~TCP_IO_FLAG_READ_BREAK;

    // wslay recv 报错不会导致 write_enabled 变为 false
    assert(wslay_event_get_write_enabled(client->ws_ctx));
    return 0;
}

//-----------------------------------------------------------------------------

// 处理 NAT 探测请求
static void handle_probe(sock_t probe_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {

    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    // NAT_PROBE: [hdr(4)] = 4 bytes
    if (len < 4 || buf[0] != SIG_PKT_NAT) return;
    const char* PROTO = "NAT";

    uint16_t req_seq = ((uint16_t)buf[2] << 8) | buf[3];

    printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F135, 135),
           PROTO, from_str, req_seq, buf[1], len);

    // 构造应答包（NAT_PROBE_ACK）
    // [hdr(4)][probe_ip(4)][probe_port(2)] = 10 bytes
    const char* PROTO_ACK = "NAT_ACK";
    buf[0] = SIG_PKT_NAT_ACK;
    buf[1] = 0;                                     /* flags */
    buf[2] = (uint8_t)(req_seq >> 8);               /* seq hi (复制请求的 seq) */
    buf[3] = (uint8_t)(req_seq & 0xFF);             /* seq lo */
    memcpy(buf + 4, &from->sin_addr.s_addr, 4);     /* probe_ip   */
    memcpy(buf + 8, &from->sin_port, 2);            /* probe_port */

    print("V:", LA_F("Send %s: mapped=%s:%d\n", LA_F111, 111),
          PROTO_ACK, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    udp_send(probe_fd, buf, 4 + SIG_PKT_NAT_ACK_PSZ, from, PROTO_ACK);
}

///////////////////////////////////////////////////////////////////////////////

// 信号处理函数
#if P_WIN
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            print("I: \n%s\n", LA_S("Received shutdown signal, exiting gracefully...", LA_S4, 4));
            g_running = 0;
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        print("I: \n%s\n", LA_S("Received shutdown signal, exiting gracefully...", LA_S7, 7));
        g_running = 0;
    }
}
#endif

int main(int argc, char *argv[]) {

    // 初始化语言系统
    LA_init();

    // 设置语言钩子
    P_lang = lang_cstr;

    //-------------------------
    // 参数和验证
    //-------------------------

    // 设置命令行帮助信息
    ARGS_usage(NULL,
               LA_S("Description:\n"
                    "  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n"
                    "  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n"
                    "  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n"
                    "\n"
                    "Examples:\n"
                    "  $0                              # Default: port 9333, no probe, no relay\n"
                    "  $0 -p 8888                      # Listen on port 8888\n"
                    "  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n"
                    "  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n"
                    "  $0 --cn -p 8888                 # Chinese language\n"
                    "\n"
                    "Note: Run without arguments to use default configuration (port 9333)", LA_S3, 3));

    // 解析命令行参数（如果无参数，使用默认配置不显示帮助）
    if (argc > 1) {
        ARGS_parse(argc, argv,
            &ARGS_DEF_port,
            &ARGS_DEF_probe_port,
            &ARGS_DEF_relay,
            &ARGS_DEF_msg,
            &ARGS_DEF_ws,
            &ARGS_DEF_ws_port,
            &ARGS_DEF_cn,
            NULL);
    }

    // --ws-port 隐含开启 --ws
    if (ARGS_ws_port.i64 > 0) ARGS_ws.i64 = 1;

    // 获取参数值（设置默认值）
    int port = ARGS_port.i64 ? (int)ARGS_port.i64 : DEFAULT_PORT;

    // 验证端口范围
    if (port <= 0 || port > 65535) {
        print("E:", LA_F("Invalid port number %d (range: 1-65535)\n", LA_F83, 83), port);
        ARGS_print(argv[0]);
        return 1;
    }
    if (ARGS_probe_port.i64 < 0 || ARGS_probe_port.i64 > 65535) {
        print("E:", LA_F("Invalid probe port %d (range: 0-65535)\n", LA_F84, 84), (int)ARGS_probe_port.i64);
        ARGS_print(argv[0]);
        return 1;
    }

    //-------------------------
    // 初始化
    //-------------------------

    if (P_net_init() != E_NONE) {
        print("E:", LA_F("net init failed\n", LA_F140, 140));
        return 1;
    }

    // 初始化随机数生成器（用于生成安全的 session_id）
    P_rand_init();

    // 初始化信令服务模块
    compact_init();
    relay_init();
    wss_init();

    // 打印服务器配置信息
    print("I:", LA_F("Starting P2P signal server on port %d\n", LA_F120, 120), port);
    print("I:", LA_F("NAT probe: %s (port %d)\n", LA_F92, 92), 
          ARGS_probe_port.i64 > 0 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1), 
          (int)ARGS_probe_port.i64);
    print("I:", LA_F("Relay support: %s\n", LA_F102, 102), 
          ARGS_relay.i64 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1));
#ifdef WITH_WSLAY
    if (!ARGS_ws.i64) {
        print("I:", "WebSocket service: disabled\n");
    } else if (ARGS_ws_port.i64 > 0) {
        print("I:", "WebSocket service: enabled (dedicated port %d)\n", (int)ARGS_ws_port.i64);
    } else {
        print("I:", "WebSocket service: enabled (same port %d)\n", port);
    }
#endif

    //-------------------------
    // 初始化网络
    //-------------------------

    int sock_opt_on = 1;

    // 创建 UDP 套接字（用于 COMPACT 信令模式）
    sock_t udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == P_INVALID_SOCKET) {
        print("E:", "UDP socket failed(%d)\n", P_sock_errno());
        return 1;
    }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sock_opt_on, sizeof(sock_opt_on));

    // 创建 TCP 监听套接字（用于 Relay 信令模式）
    sock_t listen_fd[2]; int listen_n = 1;
    listen_fd[0] = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd[0] == P_INVALID_SOCKET) {
        print("E:", "TCP socket failed(%d)\n", P_sock_errno());
        return 1;
    }
    setsockopt(listen_fd[0], SOL_SOCKET, SO_REUSEADDR, (const char *)&sock_opt_on, sizeof(sock_opt_on));

#ifdef WITH_WSLAY
    if (ARGS_ws.i64 && ARGS_ws_port.i64 > 0) {
        listen_fd[1] = socket(AF_INET, SOCK_STREAM, 0); ++listen_n;
        if (listen_fd[1] == P_INVALID_SOCKET) {
            print("E:", "WS socket failed(%d)\n", P_sock_errno());
            return 1;
        }
        setsockopt(listen_fd[1], SOL_SOCKET, SO_REUSEADDR, (const char *)&sock_opt_on, sizeof(sock_opt_on));
    } else listen_fd[1] = P_INVALID_SOCKET;
#endif

    // 创建 NAT 探测 UDP 套接字（可选，仅当配置了 probe_port 时）
    sock_t probe_fd = P_INVALID_SOCKET;
    if (ARGS_probe_port.i64 > 0) {
        probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (probe_fd == P_INVALID_SOCKET) {
            print("E:", "probe UDP socket failed(%d)\n", P_sock_errno());
            return 1;
        }
        setsockopt(probe_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sock_opt_on, sizeof(sock_opt_on));
    }

    // 绑定监听端口（TCP 和 UDP 同一端口）
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        print("E:", "UDP bind failed(%d)\n", P_sock_errno());
        return 1;
    }
    if (bind(listen_fd[0], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        print("E:", "TCP bind failed(%d)\n", P_sock_errno());
        return 1;
    }
    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd[0], 10);
    print("I:", LA_F("P2P Signaling Server listening on port %d (TCP + UDP)...\n", LA_F99, 99), port);

    if (ARGS_ws.i64) {
#ifdef WITH_WSLAY
        if (listen_fd[1] != P_INVALID_SOCKET) {
            addr.sin_port = htons((uint16_t)ARGS_ws_port.i64);
            if (bind(listen_fd[1], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                print("E:", "WebSocket TCP bind failed(%d)\n", P_sock_errno());
                return 1;
            }
            listen(listen_fd[1], 10);
            print("I:", LA_F("WebSocket service listening on port %d\n", LA_F181, 181), (int)ARGS_ws_port.i64);

            ARGS_ws.i64 = 0; // ARGS_ws 后面的含义变为：是否将 WebSocket 服务嵌入到 P2P 服务端口中
        }
#else
        ARGS_ws.i64 = 0;  /* 编译时未启用 WebSocket 支持，强制禁用 */
        print("W:", "WebSocket support not compiled in, WS disabled\n");
#endif
    }

    // 绑定 NAT 探测端口（独立端口，客户端用同一本地端口发包，服务器在此处看到不同映射地址）
    if (probe_fd != P_INVALID_SOCKET) {
        addr.sin_port = htons((uint16_t)ARGS_probe_port.i64);
        if (bind(probe_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            print("E:", LA_F("probe UDP bind failed(%d)\n", LA_F141, 141), P_sock_errno());
            P_sock_close(probe_fd);
            probe_fd = P_INVALID_SOCKET;
            ARGS_probe_port.i64 = 0;  /* 绑定失败，禁用探测功能 */
            print("W:", LA_F("NAT probe disabled (bind failed)\n", LA_F90, 90));
        } 
        else {
            print("I:", LA_F("NAT probe socket listening on port %d\n", LA_F91, 91), (int)ARGS_probe_port.i64);
        }
    }

    print("I:", "p2p0 service started successfully\n");

    //-------------------------
    // 注册程序退出处理和信号
    //-------------------------

    // 注册信号处理
#if P_WIN
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        print("W:", "%s", LA_S("Failed to set console ctrl handler\n", 0));
    }
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  /* 屏蔽 SIGPIPE：对端 socket 关闭时 send() 返回 EPIPE 而不是 kill 进程 */
#endif

    //-------------------------
    // 程序运行
    //-------------------------

    // 主循环
    fd_set read_fds; fd_set write_fds; uint64_t last_cleanup = P_tick_ms(), last_retry = last_cleanup;
    while (g_running) { uint64_t now = P_tick_ms();

        //-------------------------
        // 可靠性重试处理
        //-------------------------

        // 检查并重传未确认的 SYNC 包 + MSG RPC 包（每秒检查一次）
        if (tick_diff(now, last_retry) >= RETRY_INTERVAL_MS) { last_retry = now;
            compact_retry_pending(udp_fd, now);
            relay_retry_pending(now);
#ifdef WITH_WSLAY
            retry_wss_pending(now);
#endif
        }

        //-------------------------
        // （多路复用）端口访问 / 以及超时回收处理
        //-------------------------

        // 设置要监听的套接口 fd
        // + TCP listen + TCP clients + UDP + probe UDP + 客户端...
        // + max_fd 必须是所有监听套接字中数值最大的那个（Windows 不使用此值，但 POSIX 需要正确设置）
        FD_ZERO(&read_fds); FD_ZERO(&write_fds);
#if !P_WIN
        sock_t max_fd = 0;
#endif

        FD_SET(udp_fd, &read_fds); max_fd = udp_fd;
        FD_SET(listen_fd[0], &read_fds); P_FD_MAX(max_fd, listen_fd[0]);
#ifdef WITH_WSLAY
        if (listen_fd[1] != P_INVALID_SOCKET) { FD_SET(listen_fd[1], &read_fds); P_FD_MAX(max_fd, listen_fd[1]); }
#endif
        if (probe_fd != P_INVALID_SOCKET) { FD_SET(probe_fd, &read_fds); P_FD_MAX(max_fd, probe_fd); }

        // 添加有效的 TCP 客户端套接字到监听集合中
        if (tick_diff(now, last_cleanup) >= CLEANUP_INTERVAL_S * 1000) { last_cleanup = now;
            for (int i = 0; i < MAX_PEERS; i++) { int8_t m = as_client(i)->proto;
                if (m < 0) continue;

                // client 超时淘汰检测
                if (tick_diff(now, as_client(i)->last_active) >= CLIENT_TIMEOUT_S * 1000) {
                    print("W:", LA_F("'%s' timeout & cleanup (inactive for %.1f sec)\n", LA_F73, 73),
                           as_client(i)->local_peer_id, tick_diff(now, as_client(i)->last_active) / 1000.0);
                    if (m == PROTO_COMPACT) compact_free_client(&g_client_slots[i].compact);
                    else if (m == PROTO_RELAY) relay_term_client(&g_client_slots[i].relay, true);
                    else if (m == PROTO_WSS) wss_term_client(&g_client_slots[i].wss, true);
                    else if (m == 127) {
                        P_sock_close(as_client(i)->fd);
                        as_client(i)->fd = P_INVALID_SOCKET;
                        as_client(i)->proto = -1;
                    }
                }
                else if (m != PROTO_COMPACT) {

                    // 最干净的自定义协议模块内部 io 状态的同步方案是在此拦截检测
                    /* + 作为服务端，读取状态往往都是全生命期的，所以主要关注的是写入状态的同步
                    *   写入状态的变更来源主要包括三类：自动响应（在读取解析请求时自动恢复的）；应用层主动触发的写入；以及协议库内部非响应式自动触发（如计时器）
                    *   前两种情况可以通过写入 api 封装、读取完成后检测来同步拦截，但第三种情况就很难进行同步拦截
                    * + 所以最好的方案，就是在每次访问 io 状态前，进行同步置位；然后在 io 操作完成后，检测并取消置位
                    */
                    if (m == PROTO_WSS && !as_ws_client(i)->ws_handshake) {
                        if (wslay_event_want_write(as_ws_client(i)->ws_ctx)) as_tcp_client(i)->io |= TCP_IO_FLAG_WANT_WRITE;
                        else as_tcp_client(i)->io &= ~TCP_IO_FLAG_WANT_WRITE;
                    }
                    if (as_tcp_client(i)->io) {
                        assert(as_client(i)->fd != P_INVALID_SOCKET);
                        if (as_tcp_client(i)->io & TCP_IO_FLAG_WANT_READ) FD_SET(as_client(i)->fd, &read_fds);
                        if (as_tcp_client(i)->io & TCP_IO_FLAG_WANT_WRITE) FD_SET(as_client(i)->fd, &write_fds);
                        P_FD_MAX(max_fd, as_client(i)->fd);
                    }
                }
            }
        } else {
            for (int i = 0; i < MAX_PEERS; i++) {
                if (as_client(i)->proto <= 0) continue;
                if (as_client(i)->proto == PROTO_WSS && !as_ws_client(i)->ws_handshake) {
                    if (wslay_event_want_write(as_ws_client(i)->ws_ctx)) as_tcp_client(i)->io |= TCP_IO_FLAG_WANT_WRITE;
                    else as_tcp_client(i)->io &= ~TCP_IO_FLAG_WANT_WRITE;
                }
                if (as_tcp_client(i)->io) {
                    assert(as_client(i)->fd != P_INVALID_SOCKET);
                    if (as_tcp_client(i)->io & TCP_IO_FLAG_WANT_READ) FD_SET(as_client(i)->fd, &read_fds);
                    if (as_tcp_client(i)->io & TCP_IO_FLAG_WANT_WRITE) FD_SET(as_client(i)->fd, &write_fds);
                    P_FD_MAX(max_fd, as_client(i)->fd);
                }
            }
        }

        struct timeval tv = {0, SELECT_TIMEOUT_MS * 1000};
        int sel_ret = select(max_fd + 1, &read_fds, &write_fds, NULL, &tv);
        if (sel_ret < 0) {
            if (P_sock_is_interrupted()) continue;  // 被信号打断，继续循环
            print("E:", LA_F("select failed(%d)\n", LA_F201, 201), P_sock_errno());
            break;
        }

        //-------------------------------
        // 监听端口操作
        //-------------------------------

        // 如果存在新的 TCP 连接请求，accept 并将其添加到客户端列表中
        for (int i = 0; i < listen_n; i++) {
            if (!FD_ISSET(listen_fd[i], &read_fds)) continue;

            socklen_t addr_len = sizeof(addr);
            sock_t client_fd = accept(listen_fd[i], (struct sockaddr *)&addr, &addr_len);

            // 设置为非阻塞模式，避免慢客户端阻塞整个服务器事件循环
            if (P_sock_nonblock(client_fd, true) != E_NONE) {
                print("W:", LA_F("[TCP] Failed to set client socket to non-blocking mode\n", LA_F130, 130));
            }

            // 查找一个空闲槽位来存储这个新的连接
            int k = 0;
            for (; k < MAX_PEERS; k++) {
                if (as_client(k)->proto < 0) {

                    // WebSocket 监听端口
                    if (i == 1) {

                        assert(!as_ws_client(k)->ws_ctx && !as_ws_client(k)->buf);
                        as_ws_client(k)->buf = alloc_buffer(BUF_FLAG_2048(0));
                        if (!as_ws_client(k)->buf) {
                            print("E:", LA_F("Failed to allocate buffer for new WebSocket client\n", LA_F173, 173));
                            P_sock_close(client_fd);
                            break;
                        }
                        as_ws_client(k)->pos = as_ws_client(k)->len = 0;
                        as_ws_client(k)->ws_handshake = 1;  // 标记为握手阶段

                        if (!wss_init_client(&g_client_slots[k].wss)) {
                            print("E:", LA_F("Failed to initialize %s client\n", LA_F174, 174), "WS/ICE");
                            free_buffer(as_ws_client(k)->buf); as_ws_client(k)->buf = NULL;
                            P_sock_close(client_fd);
                            break;
                        }
                        as_client(k)->proto = PROTO_WSS;
                        print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                                "WS/ICE", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), k);
                    }
                    // 如果是多模态混合端口
                    else if (ARGS_ws.i64) as_client(k)->proto = 127;  // 标记为"暂定"模式的客户端
                    // TCP/Relay 监听端口
                    else {
                        if (!relay_init_client(&g_client_slots[k].relay)) {
                            print("E:", LA_F("Failed to initialize %s client\n", LA_F174, 174), "TCP/RELAY");
                            P_sock_close(client_fd);
                            break;
                        }
                        as_client(k)->proto = PROTO_RELAY;
                        print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                                "TCP/RELAY", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), k);
                    }
                    init_client(as_client(k), client_fd);
                    as_tcp_client(k)->io = TCP_IO_FLAG_WANT_READ;  /* 新连接默认进入读取状态，等待客户端发送数据 */
                    break;
                }
            }
            if (k == MAX_PEERS) {
                print("W:", LA_F("[TCP] Max peers reached, rejecting connection\n", LA_F131, 131));
                P_sock_close(client_fd);
            }
        }

        //-------------------------------
        // 数据端口 IO 操作
        //-------------------------------

        // UDP 监听端口收到数据包（COMPACT 模式的信令交互）
        if (FD_ISSET(udp_fd, &read_fds)) {

            uint8_t buf[P2P_MTU]; struct sockaddr_in from; socklen_t from_len = sizeof(from);
            size_t n = recvfrom(udp_fd, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
            if (n > 0) {
                compact_handle_signaling(udp_fd, buf, n, &from);
            }
        }

        // NAT 探测 UDP 收到数据包（也是 COMPACT 模式的信令交互）
        if (probe_fd != P_INVALID_SOCKET && FD_ISSET(probe_fd, &read_fds)) {

            uint8_t buf[64]; struct sockaddr_in from; socklen_t from_len = sizeof(from);
            size_t n = recvfrom(probe_fd, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
            if (n > 0) {
                handle_probe(probe_fd, buf, n, &from);
            }
        }

        // 处理 TCP/IO（先发送后接收，单次遍历）
        for (int i = 0; i < MAX_PEERS; i++) { int8_t m = as_client(i)->proto;
            if (m <= 0 || as_client(i)->fd == P_INVALID_SOCKET) continue;
            ws_client_t* ws_client = (m == PROTO_WSS) ? as_ws_client(i) : NULL;

            // 处理数据接收
            if ((as_tcp_client(i)->io & TCP_IO_FLAG_WANT_READ)
                && FD_ISSET(as_client(i)->fd, &read_fds)) {

                // 对于多模态混合端口，需要先根据数据包内容判断协议类型
                if (m == 127) {
#ifdef WITH_WSLAY
                    uint8_t buf[1]; ssize_t n = recv(as_client(i)->fd, (char *)buf, sizeof(buf), MSG_PEEK);
                    if (n > 0) {
                        // WebSocket 握手请求的特征。即 HTTP GET 请求行，也就是以 "GET " 开头
                        if (buf[0] == 'G') {
                            print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                                  "WS/ICE", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), i);
                            if (!wss_init_client(&g_client_slots[i].wss)) {
                                P_sock_close(as_client(i)->fd);
                                as_client(i)->fd = P_INVALID_SOCKET;
                                as_client(i)->proto = -1;
                                print("E:", LA_F("Failed to initialize WS/ICE client for slot %d\n", LA_F176, 176), i);
                                continue;
                            }
                            ws_client = as_ws_client(i);
                            assert(!ws_client->ws_ctx && !ws_client->buf);
                            ws_client->buf = alloc_buffer(BUF_FLAG_2048(0));
                            if (!ws_client->buf) {
                                print("E:", LA_F("Failed to allocate buffer for new WebSocket client\n", LA_F173, 173));
                                P_sock_close(as_client(i)->fd);
                                as_client(i)->fd = P_INVALID_SOCKET;
                                as_client(i)->proto = -1;
                                continue;
                            }
                            ws_client->pos = ws_client->len = 0;
                            ws_client->ws_handshake = 1;  // 标记为握手阶段
                            as_client(i)->proto = m = PROTO_WSS;

                        } else { 
                            print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                                  "TCP/RELAY", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), i);
                            if (!relay_init_client(&g_client_slots[i].relay)) {
                                P_sock_close(as_client(i)->fd);
                                as_client(i)->fd = P_INVALID_SOCKET;
                                as_client(i)->proto = -1;
                                print("E:", LA_F("Failed to initialize TCP/RELAY client for slot %d\n", LA_F175, 175), i);
                                continue;
                            }
                            as_client(i)->proto = m = PROTO_RELAY;
                        }
                    } else {
                        if (n == 0)
                            print("I:", LA_F("Client closed during protocol detection (slot %d)\n", LA_F172, 172), i);
                        else
                            print("E:", LA_F("Failed to peek client data for protocol detection (slot %d)\n", LA_F177, 177), i);
                        P_sock_close(as_client(i)->fd);
                        as_client(i)->fd = P_INVALID_SOCKET;
                        as_client(i)->proto = -1;   // 释放槽位
                        continue;
                    }
#else
                    assert(false);
#endif
                }

                if (m == PROTO_RELAY) relay_handle_recv(&g_client_slots[i].relay);
#ifdef WITH_WSLAY
                else if (ws_client) {

                    // 对于握手读取阶段
                    if (ws_client->ws_handshake > 0) {

                        if (handle_ws_handshake(ws_client) < 0) {

                            print("E:", LA_F("[WS] client closed during handshake (slot %d)\n", LA_F193, 193), i);

                            P_sock_close(ws_client->base.fd);
                            ws_client->base.fd = P_INVALID_SOCKET;
                            wss_term_client((wss_client_t*)ws_client, true);
                            continue;
                        }
                    }
                    else {
                        assert(!(ws_client->io & TCP_IO_FLAG_READ_BREAK));
                        do {
                            int r = wslay_event_recv(ws_client->ws_ctx);
                            if (r != 0) {

                                // wslay recv 报错会尝试发送错误码，同时将 read_enabled 置为 false，所以这里无需关闭连接
                                assert(!wslay_event_want_read(ws_client->ws_ctx));
                                ws_client->io &= ~TCP_IO_FLAG_WANT_READ;
                                print("E:", LA_F("[WS] recv failed(%d) (slot %d)\n", LA_F198, 198), r, i);
                                break;
                            }
                            // 此外 wslay 在收到客户端发来的 close 帧时，也会将 read_enabled 置为 false
                            if (!wslay_event_want_read(ws_client->ws_ctx)) {
                                ws_client->io &= ~TCP_IO_FLAG_WANT_READ;
                                break;
                            }
                        } while (!(ws_client->io & TCP_IO_FLAG_READ_BREAK));
                        ws_client->io &= ~TCP_IO_FLAG_READ_BREAK;
                    }
                }
#endif
            }

            // 处理数据发送
            if ((as_tcp_client(i)->io & TCP_IO_FLAG_WANT_WRITE)
                && FD_ISSET(as_client(i)->fd, &write_fds)) {

                if (m == PROTO_RELAY) relay_handle_send(&g_client_slots[i].relay);
#ifdef WITH_WSLAY
                else if (ws_client) {

                    // 对于握手写入阶段
                    if (ws_client->ws_handshake < 0) {

                        int r = handle_ws_handshake(ws_client);
                        if (r < 0) {

                            print("I:", LA_F("[WS] client closed during handshake (slot %d)\n", LA_F193, 193), i);

                            // 注意，handshake 写入返回报错，只会发生在 ws ctx init 之前，所以这里直接关闭连接，无需发送 close 帧
                            P_sock_close(ws_client->base.fd);
                            ws_client->base.fd = P_INVALID_SOCKET;
                            wss_term_client((wss_client_t*)ws_client, true);
                            continue;
                        }

                        // 握手写入完成后，会执行一次读取，这可能会产生新的写入
                        // + 注意，安全拦截写入状态时机，除了每次主动发送数据时，还应该在接收处理完成之后
                        //   因为 wslay 内部会在特定逻辑条件下，自动响应并发送特定数据包（如 close 帧）
                        //   同样，也不能舍弃主动发送数据时的写入状态设置，因为在非 recv/resp（应答响应）的场景下，应用层也可能主动触发数据发送
                        if (r || !wslay_event_want_write(ws_client->ws_ctx))
                            continue;

                        ws_client->io |= TCP_IO_FLAG_WANT_WRITE;
                    }
                    else assert(wslay_event_want_write(ws_client->ws_ctx));

                    // wslay send 会尝试一次性将所有数据发送，除非报错、或 would block
                    int r = wslay_event_send(ws_client->ws_ctx);
                    if (r != 0) { assert(!wslay_event_want_write(ws_client->ws_ctx));

                        ws_client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                        print("E:", LA_F("[WS] conn closed during send: errno=%d (slot %d)\n", LA_F194, 194), r, i);

                        // 目前策略是，读取失败会尝试返回错误吗，如果发送失败则直接关闭连接（不再尝试发送 close 帧了）
                        P_sock_close(ws_client->base.fd);
                        ws_client->base.fd = P_INVALID_SOCKET;

                        // todo 是否释放 ws_ctx，如何重连？
                        wss_term_client((wss_client_t*)ws_client, false);
                    }
                    // 全部发送完成。注意，当发送完 close 协议字后，write_enabled 会被自动置为 false
                    else if (!wslay_event_want_write(ws_client->ws_ctx)
                             && wss_handle_send_complete(ws_client)) {
                        ws_client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                    }
                }
#endif
            }

#ifdef WITH_WSLAY
            // closing
            if (ws_client) {

                // 如果收到来自客户端的 close 帧
                if (ws_client->io & WSS_IO_FLAG_PEER_CLOSING) {

                    // 如果写关闭，说明已经回复 close 帧（或报错）
                    if (!wslay_event_get_write_enabled(ws_client->ws_ctx)) {

                        print("I:", LA_F("[WS] Client closed (slot %d)\n", LA_F191, 191), i);

                        P_sock_close(ws_client->base.fd);
                        ws_client->base.fd = P_INVALID_SOCKET;
                        wss_term_client((wss_client_t*)ws_client, true);
                    }
                } else if (ws_client->io & WSS_IO_FLAG_CLOSING &&
                           tick_diff(now, ws_client->base.last_active) >= CLIENT_TIMEOUT_S * 1000) {

                    print("I:", LA_F("[WS] Closing timeout, force close client (slot %d)\n", LA_F192, 192), i);

                    P_sock_close(ws_client->base.fd);
                    ws_client->base.fd = P_INVALID_SOCKET;
                    wss_term_client((wss_client_t*)ws_client, true);
                }
            }
#endif
        }

    } // while (g_running)

    //-------------------------------
    // 运行结束，开始清理资源
    //-------------------------------

    // 清理资源
    print("I: \n%s", LA_S("Shutting down...\n", LA_S8, 8));
    
    // 关闭所有客户端连接
    for (int i = 0; i < MAX_PEERS; i++) {
        if (as_client(i)->proto > 0) {
            if (as_client(i)->fd != P_INVALID_SOCKET) {
                P_sock_close(as_client(i)->fd);
                as_client(i)->fd = P_INVALID_SOCKET;
            }
            if (as_client(i)->proto == PROTO_RELAY) relay_term_client(&g_client_slots[i].relay, true);
#ifdef WITH_WSLAY
            else if (as_client(i)->proto == PROTO_WSS) wss_term_client(&g_client_slots[i].wss, true);
#endif
        } else if (as_client(i)->proto == PROTO_COMPACT) {
            compact_free_client(&g_client_slots[i].compact);
        }
    }
    
    // 关闭监听套接字
    for (int i = 0; i < listen_n; i++) {
        if (listen_fd[i] != P_INVALID_SOCKET) {
            P_sock_close(listen_fd[i]);
            listen_fd[i] = P_INVALID_SOCKET;
        }
    }

    P_sock_close(udp_fd);
    if (probe_fd != P_INVALID_SOCKET) {
        P_sock_close(probe_fd);
        probe_fd = P_INVALID_SOCKET;
    }

    P_net_cleanup();

    print("I:", LA_F("Goodbye!\n", LA_F82, 82));
    return 0;
}
