/*
 * P2P 信令服务器
 *
 * 支持三种信令模式：
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
 * 3. WSS 模式 (TCP)
 *    - 对应客户端 p2p_signal_wss 模块
 *    - 基于 WebSocket 的信令模式，支持浏览器端的 P2P 通信
 * 
 * 关于 uthash 的使用：
 * 1. HASH_ADD 不要求 hh 成员被初始化为 0，但 HASH_FIND 要求 hh 成员被初始化为 0。
 * 2. HASH_DELETE 不会自动将 hh 成员（包括 tbl 指针）置为 NULL
 */

#define MOD_TAG "P2P0d"

#include "p2p_compact.h"
#include "p2p_relay.h"
#ifdef WITH_WS
#include "p2p_wss.h"
#endif

#include "LANG.cn.h"

#include <signal.h>    /* signal() */

// 命令行参数定义
ARGS_I(false, port,         'p', "port",       LA_CS("Signaling server listen port (TCP+UDP)", LA_S9, 9));
ARGS_I(false, probe_port,   'P', "probe-port", LA_CS("NAT type detection port (0=disabled)", LA_S6, 6));
ARGS_I(false, client_timeout,'t', "client-timeout", "Client timeout cleanup in seconds");
#ifdef WITH_WS
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

#define CLIENTS(i)                  ((client_t*)&g_client_slots[i])
#define TCP_CLIENTS(i)              ((tcp_client_t*)&g_client_slots[i])
#define CT_CLIENTS(i)               ((ct_client_t*)&g_client_slots[i])

client_ctx_t*                       g_contexts[PROTO_NUM];

//-----------------------------------------------------------------------------

static buf16_item_t*                g_recycle[10];

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t        g_running = 1;

///////////////////////////////////////////////////////////////////////////////

// 分配 frame buf
// + 注: malloc 分配的内存默认肯定都是至少 8 字节对齐的，而 buffer_item_t 的成员排列满足连续内存对齐原则
//   所以强转 buffer_item_t* 后可以直接访问 buffer_item_t 的成员
buf16_item_t* alloc_buf16(uint8_t flags) {
    int idx = flags >> 4; if (idx > 9) return NULL;
    buf16_item_t *item = g_recycle[idx];
    if (item) g_recycle[idx] = item->next;
    else if (!((item = (buf16_item_t*)malloc(sizeof(buf16_item_t) + idx==3?P2P_MTU:(1 << idx)*128)))) return NULL;
    item->next = NULL;
    item->flags = flags;
    item->refer = NULL;
    item->len = 0;
    item->pos = 0;
    return item;
}

// 释放 frame buf
void free_buf16(buf16_item_t *buf_item) {
    int idx = buf_item->flags >> 4;
    buf_item->next = g_recycle[idx];
    g_recycle[idx] = buf_item;
}

///////////////////////////////////////////////////////////////////////////////

static void do_free_session(session_t *s) {

    // 解除和 client 的绑定关系
    client_t *c = s->client;
    if (c) {
        if (s->prev) s->prev->next = s->next;
        else c->sessions = s->next;
        if (s->next) s->next->prev = s->prev;
        s->prev = s->next = NULL;
    }

    // 解除和 pair 的绑定关系
    session_pair_t *pair = s->pair;
    if (pair->sessions[0] == s) pair->sessions[0] = NULL; else pair->sessions[1] = NULL;

    // 如果对端之前在线，则标记对端会话已断开（-1）
    // + 注意，此时需要保留对端的 session id，以及和本端 session id 建立的 pair 关系
    if (PEER_VALID(s->peer)) {
        s->peer->peer = (session_t*)-1;
    }
    else if (!pair->sessions[0] && !pair->sessions[1]) {
        HASH_DELETE(hh, g_session_pairs, pair);
        free(pair);
    }

    HASH_DELETE(hh, g_sessions, s);
    free(s);
}

void
free_session(session_t *s, bool terminate) {
    assert(s && s->pair && s->client && s->client->proto >= 0);
    client_ctx_t* ctx = g_contexts[s->client->proto];

    // 如果已和对端 session 建立连接
    if (PEER_ONLINE(s)) {
        ctx->cb_break(ctx, s, PEER(s), terminate ? SESS_BREAK_TERM : SESS_BREAK_CLOSE);
    }

    if (ctx->cb_close) {
        ctx->cb_close(ctx, s, terminate, false);
    }

    do_free_session(s);
}

void
clear_sessions(client_t *c, bool terminate) {
    assert(c && c->proto >= 0);
    if (!c->sessions) return;
    client_ctx_t* ctx = g_contexts[c->proto];

    if (ctx->cb_clear) ctx->cb_clear(ctx, c, true);

    while (c->sessions) { session_t* s = c->sessions;

        // 如果已和对端 session 建立连接
        if (PEER_ONLINE(s)) {
            ctx->cb_break(ctx, s, PEER(s), terminate ? SESS_BREAK_TERM : SESS_BREAK_CLOSE);
        }

        if (ctx->cb_close) {
            ctx->cb_close(ctx, s, terminate, true);
        }

        do_free_session(s);
    }

    if (ctx->cb_clear) ctx->cb_clear(ctx, c, false);
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

// 创建一个单端会话
ret_t
solo_session(client_t *client, session_t **local_s,
             size_t session_type_size, void(*init)(session_t* session)) {
    (void)client;(void)local_s;(void)session_type_size;(void)init;
    return -1;
}

// 创建新会话对
ret_t
pair_session(client_t *c, const char *remote_peer_id,
             session_t **local_s, session_t **remote_s,
             size_t session_type_size, void(*init)(session_t* session)) {
    assert(c && c->proto >= 0);
    if (!remote_peer_id || !local_s || !remote_s) return E_INVALID;
    *local_s = NULL; *remote_s = NULL;

    // 查找和对端的 sess pair
    char peer_key[3 * P2P_PEER_ID_MAX];
    memset(peer_key, 0, sizeof(peer_key));
    strncpy(peer_key, c->local_peer_id, P2P_PEER_ID_MAX);
    strncpy(peer_key + P2P_PEER_ID_MAX, remote_peer_id, P2P_PEER_ID_MAX);
    session_pair_t *pair = NULL;
    HASH_FIND(hh, g_session_pairs, peer_key, 2 * P2P_PEER_ID_MAX, pair);
    if (!pair) {
        strncpy(peer_key + 2 * P2P_PEER_ID_MAX, c->local_peer_id, P2P_PEER_ID_MAX);
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

        memcpy(pair->peer_id[0], c->local_peer_id, strnlen(c->local_peer_id, P2P_PEER_ID_MAX));
        memcpy(pair->peer_id[1], remote_peer_id, strnlen(remote_peer_id, P2P_PEER_ID_MAX));
        HASH_ADD_KEYPTR(hh, g_session_pairs, pair->peer_id, 2 * P2P_PEER_ID_MAX, pair);

        // 本端初始作为 sess pair 的 left side
        side = 0;
    }
    // 如果 sess pair 左侧被置位
    else if (pair->sessions[0]) {

        // 如果当前已经完成配对，重复配对请求
        if (pair->sessions[1]) {

            // 确定本端和对端
            if (pair->sessions[0]->client == c) { *local_s = pair->sessions[0]; side = 0; *remote_s = pair->sessions[1]; }
            else { *local_s = pair->sessions[1]; side = 1; *remote_s = pair->sessions[0]; }

            assert((*local_s)->peer != (void*)-1 || (*remote_s)->peer != (void*)-1);    // 双方不可能同时处于被对方断开的状态

            // 如果本端主动断开后，又重新发起新的连接。但此时对端还未确认（即未发起新的连接）
            // + 之前主动断开会将对端的 peer 置位 -1
            if ((*remote_s)->peer == (void*)-1) {
                assert(!(*local_s)->peer);
                *remote_s = NULL;
                return E_DUPLICATE;
            }

            // 如果双方都不是被对方断开的状态，则此时双方肯定已经彼此打通了
            if ((*local_s)->peer != (void*)-1) {
                assert((*local_s)->peer && (*remote_s)->peer);
                return E_DUPLICATE;
            }

            // 之前本端被对端断开，且对端已经重新连接，此时是本端重新连接
            assert(!(*remote_s)->peer);
        }

        else if (pair->sessions[0]->client != c) { *remote_s = pair->sessions[0];       // 如果对端存在（本端之前已经脱离）
            assert(!(*remote_s)->peer || (*remote_s)->peer == (void*)-1);
            side = 1;                                                                   // 本端将位于 pair 的 right side
        } else { *local_s = s = pair->sessions[0];                                      // 如果本端存在（对端之前已经脱离）
            if (!s->peer) return E_DUPLICATE;                                           // 如果是本端重复发起连接（对端不是已脱离状态）
            assert(s->peer == (void*)-1);
            side = 0;                                                                   // 本端位于 pair 的 left side
        }
    }
    else { assert(pair->sessions[1]);

        if (pair->sessions[1]->client != c) { *remote_s = pair->sessions[1];
            assert(!(*remote_s)->peer || (*remote_s)->peer == (void*)-1);
            side = 0;
        } else { *local_s = s = pair->sessions[1];
            if (!s->peer) return E_DUPLICATE;
            assert(s->peer == (void*)-1);
            side = 1;
        }
    }

    if (*local_s) { assert(s->peer == (void*)-1);                                       // 如果对端已脱离（对端脱离时，会将本端的 peer 置位 -1）

        // 重新分配 sess id，并重建索引
        s->session_id = generate_session_id();
        HASH_DELETE(hh, g_sessions, s);
        HASH_ADD(hh, g_sessions, session_id, sizeof(uint32_t), s);

        if (*remote_s) { s->peer = *remote_s; remote_s[0]->peer = s; }                  // 如果对端存在，完成两端的打通
        else s->peer = NULL;                                                            // 重置本端为新的连接
        return side;
    }

    // 分配本端 sess 对象
    if (!s) {

        s = (session_t*)calloc(1, session_type_size);
        if (!s) return E_OUT_OF_MEMORY;

        assert(*remote_s);

        // 如果本端主动断开后，又重新发起新的连接，但此时对端还未确认（即未发起新的连接）
        if ((*remote_s)->peer == (void*)-1) {
            *remote_s = NULL;                                                           // 视对端不存在
        }
        else { assert(!(*remote_s)->peer);                                              // 对端已发起对自己的连接
            remote_s[0]->peer = s;                                                      // 完成两端的打通
            s->peer = *remote_s;
        }
    }
    else assert(!*remote_s);

    *local_s = s;

    // 将 sess 和 pair 绑定
    pair->sessions[side] = s; s->pair = pair;

    // 将 sess 添加到 client 中
    s->prev = NULL; s->next = c->sessions;
    if (c->sessions) c->sessions->prev = s;
    c->sessions = s;
    s->client = c;

    // 分配 sess id，并构建索引
    s->session_id = generate_session_id();
    HASH_ADD(hh, g_sessions, session_id, sizeof(uint32_t), s);

    // 初始化新的 session
    if (init) init(s);

    return side;
}

///////////////////////////////////////////////////////////////////////////////

void
free_client(client_t *c) {
    assert(c && c->proto >= 0);
    client_ctx_t* ctx = g_contexts[c->proto];

    if (c->sessions)
        clear_sessions(c, true);

    ctx->cb_free(ctx, c);

    if (c->hh.tbl) {
        HASH_DELETE(hh, g_clients, c);
        c->hh.tbl = NULL;
    }

    // > PROTO_COMPACT 说明是 TCP 连接
    if (c->proto > PROTO_COMPACT) {

        if (c->fd != P_INVALID_SOCKET) {
            P_sock_close(c->fd);
        }
    }
    c->fd = P_INVALID_SOCKET;
    c->proto = -1;
}

client_t*
find_client(const char *peer_id) {
    client_t *client = NULL;
    HASH_FIND_STR(g_clients, peer_id, client);
    return client;
}

bool
identify_client(client_t* c, const char peer_id[P2P_PEER_ID_MAX]) {
    assert(c && c->proto >= 0);
    if (c->local_peer_id[0] != '\0') return false;

    memcpy(c->local_peer_id, peer_id, P2P_PEER_ID_MAX);
    c->local_peer_id[P2P_PEER_ID_MAX] = '\0';

    assert(!find_client(c->local_peer_id));  // 注册前必须确保 local_peer_id 不存在
    HASH_ADD_STR(g_clients, local_peer_id, c);
    return true;
}

static inline bool init_client(client_t* c) {

    assert(c->proto >= 0 && c->fd != P_INVALID_SOCKET);
    client_ctx_t* ctx = g_contexts[c->proto];

    c->local_peer_id[0] = '\0';
    c->instance_id = 0;
    c->last_active = P_tick_ms();
    if (ctx->cb_reset && !ctx->cb_reset(ctx, c, true)) {
        c->proto = -1; c->fd = P_INVALID_SOCKET;
        return false;
    }
    assert(!c->sessions);
    return true;
}

client_t*
alloc_client(uint8_t proto, sock_t fd) {
    assert(proto < 127 && fd != P_INVALID_SOCKET);

    for (int k = 0; k < MAX_PEERS; k++) { client_t* c = CLIENTS(k);
        if (c->proto < 0) {
            c->proto = (int8_t)proto; c->fd = fd;
            return init_client(c) ? c : NULL;
        }
    }
    return NULL;
}

int
restore_client_as(client_t* c, int8_t proto, sock_t fd, uint32_t instance_id) {

    assert(c->proto >= 0 && *c->local_peer_id);

    if (c->proto == proto && c->instance_id == instance_id) {
        c->fd = fd;
        c->last_active = P_tick_ms();
        return 1;
    }

    // 先将之前的释放
    free_client(c);

    // 初始化为新的 client
    c->proto = proto; c->fd = fd;
    if (!init_client(c)) return -1;
    c->instance_id = instance_id;
    return 0;
}

bool
restore_client_from(client_t* c, client_t* from) {

    assert(c != from && c->proto >= 0 && from->proto >= 0);
    assert(!*c->local_peer_id && !c->sessions) ;
    client_ctx_t* ctx = g_contexts[c->proto];

    bool r = c->proto == from->proto && c->instance_id != from->instance_id;
    if (r) {

        memcpy(c->local_peer_id, from->local_peer_id, P2P_PEER_ID_MAX+1);
        HASH_REPLACE_STR(g_clients, local_peer_id, c, from); from->hh.tbl = NULL;

        c->sessions = from->sessions; from->sessions = NULL;

        if (ctx->cb_migrate) ctx->cb_migrate(ctx, c, from);
    }

    free_client(from);
    return r;
}

bool
activate_client(client_t* c, int active) {
    assert(c && c->proto >= 0);
    client_ctx_t* ctx = g_contexts[c->proto];

    if (!ctx->cb_activate) return false;
    if (!active) return ctx->cb_activate(ctx, c, 0);

    if (active < 0) {

        if (!ctx->cb_activate(ctx, c, 0)) return false;

        for(session_t *s = c->sessions, *ps; s; s = s->next) { ps = s->peer;
            if (PEER_VALID(ps) && ctx->cb_activate(ctx, ps->client, 0))
                ctx->cb_break(ctx, s, ps, SESS_BREAK_STOP);         // 任何一端 deactivate，双方的 session 就会 stop
        }

        ctx->cb_activate(ctx, c, active);

        c->last_active = P_tick_ms();                               // 记录失效前的最后活跃时间，供后续 cleanup 使用
    }
    else {
        if (!ctx->cb_activate(ctx, c, 0)) return false;

        ctx->cb_activate(ctx, c, active);
    }

    return true;
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
tcp_recv(tcp_client_t* client, void *buf, size_t *r_sz, const char* SP) {
    assert(client->base.proto >=0 && client->base.fd != P_INVALID_SOCKET);

    ret_t r = P_recv_nonblock(client->base.fd, buf, r_sz, 0);
    
    if (r == E_NONE_CONTEXT) {
        if (client->handshake)
            print("I:", LA_F("[%s] conn closed during handshake(%d) (EOF on recv)\n", LA_F224, 224),
                  SP?SP:"TCP", (int)client->handshake);
        else print("I:", LA_F("[%s] conn closed (EOF on recv)\n", LA_F11, 11), SP?SP:"TCP");
        return -1;
    }
    
    if (r < 0) {
        if (client->handshake)
            print("E:", LA_F("[%s] recv failed(%d) during handshake(%d) \n", LA_F183, 183),
                  SP?SP:"TCP", E_EXT_CODE(r), (int)client->handshake);
        else print("E:", LA_F("[%s] recv failed(%d)\n", LA_F225, 225), SP?SP:"TCP", E_EXT_CODE(r));
        return -2;
    }
    
    return r;  // 0=完成, 1=WOULDBLOCK
}

/*
 * TCP 发送辅助函数：异步发送，遇到 WOULDBLOCK 则加入发送队列
 * 说明：用于发送小消息（ACK、header 等）
 *      先尝试立即发送，若发送缓冲区满则依赖主循环的异步发送机制
 * 返回: 0=全部发送完成, +1=WOULDBLOCK, -1=连接关闭(EOF), -2=真错误
 *      w_sz: 输入=希望发送字节数，输出=实际发送字节数
 */
int
tcp_send(tcp_client_t* client, const void *buf, size_t *w_sz, const char *SP) {
    assert(client->base.proto >=0 && client->base.fd != P_INVALID_SOCKET);

    ret_t r = P_send_nonblock(client->base.fd, buf, w_sz, 0);
    
    if (r == E_NONE_CONTEXT) {
        if (client->handshake)
            print("I:", LA_F("[%s] conn closed during handshake(%d) (EOF on send)\n", LA_F243, 243),
                  SP?SP:"TCP", (int)client->handshake);
        else print("I:", LA_F("[%s] conn closed (EOF on send)\n", LA_F182, 182), SP?SP:"TCP");
        return -1;
    }
    
    if (r < 0) {
        if (client->handshake)
            print("E:", LA_F("[%s] send failed(%d) during handshake(%d)\n", LA_F244, 244),
                  SP?SP:"TCP", E_EXT_CODE(r), (int)client->handshake);
        else print("E:", LA_F("[%s] send failed(%d)\n", LA_F144, 144), SP?SP:"TCP", E_EXT_CODE(r));
        return -2;
    }
    
    return r;  // 0=完成, 1=WOULDBLOCK
}

//-----------------------------------------------------------------------------

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
    (void)g_client_slots[0].compact;
    (void)g_client_slots[0].relay;
    (void)g_client_slots[0].wss;

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
            &ARGS_DEF_client_timeout,
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
    if (!ARGS_client_timeout.i64) ARGS_client_timeout.i64 = DEFAULT_CLIENT_TIMEOUT_S;

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

    // 初始化 client 槽位（全部标记为空闲）
    for (int i = 0; i < MAX_PEERS; i++) CLIENTS(i)->proto = -1;

    // 初始化信令服务模块
    g_contexts[PROTO_COMPACT] = compact_init();
    g_contexts[PROTO_RELAY] = (client_ctx_t*)relay_init();
    g_contexts[PROTO_WSS] = (client_ctx_t*)wss_init();

    // 打印服务器配置信息
    print("I:", LA_F("Starting P2P signal server on port %d\n", LA_F120, 120), port);
    print("I:", LA_F("NAT probe: %s (port %d)\n", LA_F92, 92), 
          ARGS_probe_port.i64 > 0 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1), 
          (int)ARGS_probe_port.i64);
        print("I:", "Client timeout: %u sec\n", (unsigned)ARGS_client_timeout.i64);
    print("I:", LA_F("Relay support: %s\n", LA_F102, 102), 
          ARGS_relay.i64 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1));
#ifdef WITH_WS
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

#ifdef WITH_WS
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
#ifdef WITH_WS
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
#ifdef WITH_WS
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
#ifdef WITH_WS
        if (listen_fd[1] != P_INVALID_SOCKET) { FD_SET(listen_fd[1], &read_fds); P_FD_MAX(max_fd, listen_fd[1]); }
#endif
        if (probe_fd != P_INVALID_SOCKET) { FD_SET(probe_fd, &read_fds); P_FD_MAX(max_fd, probe_fd); }

        // 添加有效的 TCP 客户端套接字到监听集合中
        if (tick_diff(now, last_cleanup) >= CLEANUP_INTERVAL_S * 1000) { last_cleanup = now;
            for (int i = 0; i < MAX_PEERS; i++) { int8_t m = CLIENTS(i)->proto;
                if (m < 0) continue;

                // client 超时淘汰检测
                if (tick_diff(now, CLIENTS(i)->last_active) >= (uint64_t)ARGS_client_timeout.i64 * 1000u) {
                    print("W:", LA_F("'%s' timeout & cleanup (inactive for %.1f sec)\n", LA_F73, 73),
                          CLIENTS(i)->local_peer_id, tick_diff(now, CLIENTS(i)->last_active) / 1000.0);
                    if (m >= 0 && m < PROTO_NUM) free_client(CLIENTS(i));
                    else if (m == 127) {
                        P_sock_close(CLIENTS(i)->fd);
                        CLIENTS(i)->fd = P_INVALID_SOCKET;
                        CLIENTS(i)->proto = -1;
                    }
                }
                else if (m != PROTO_COMPACT) {
#ifdef WITH_WSLAY
                    // 最干净的自定义协议模块内部 io 状态的同步方案是在此拦截检测
                    /* + 作为服务端，读取状态往往都是全生命期的，所以主要关注的是写入状态的同步
                    *   写入状态的变更来源主要包括三类：自动响应（在读取解析请求时自动恢复的）；应用层主动触发的写入；以及协议库内部非响应式自动触发（如计时器）
                    *   前两种情况可以通过写入 api 封装、读取完成后检测来同步拦截，但第三种情况就很难进行同步拦截
                    * + 所以最好的方案，就是在每次访问 io 状态前，进行同步置位；然后在 io 操作完成后，检测并取消置位
                    */
                    if (m == PROTO_WSS && WS_CLIENTS(i)->ws_ctx) { // wslay 已初始化（HTTP 握手完成，handshake==0 或 ==-1）
                        if (wslay_event_want_write(WS_CLIENTS(i)->ws_ctx)) TCP_CLIENTS(i)->io |= TCP_IO_FLAG_WANT_WRITE;
                        else TCP_CLIENTS(i)->io &= ~TCP_IO_FLAG_WANT_WRITE;
                    }
#endif
                    if (TCP_CLIENTS(i)->io) {
                        assert(CLIENTS(i)->fd != P_INVALID_SOCKET);
                        if (TCP_CLIENTS(i)->io & TCP_IO_FLAG_WANT_READ) FD_SET(CLIENTS(i)->fd, &read_fds);
                        if (TCP_CLIENTS(i)->io & TCP_IO_FLAG_WANT_WRITE) FD_SET(CLIENTS(i)->fd, &write_fds);
                        P_FD_MAX(max_fd, CLIENTS(i)->fd);
                    }
                }
            }
        } else {
            for (int i = 0; i < MAX_PEERS; i++) { int8_t m2 = CLIENTS(i)->proto;
                if (m2 <= 0) continue;
#ifdef WITH_WSLAY
                if (m2 == PROTO_WSS && WS_CLIENTS(i)->ws_ctx) {
                    if (wslay_event_want_write(WS_CLIENTS(i)->ws_ctx)) TCP_CLIENTS(i)->io |= TCP_IO_FLAG_WANT_WRITE;
                    else TCP_CLIENTS(i)->io &= ~TCP_IO_FLAG_WANT_WRITE;
                }
#endif
                if (TCP_CLIENTS(i)->io) {
                    assert(CLIENTS(i)->fd != P_INVALID_SOCKET);
                    if (TCP_CLIENTS(i)->io & TCP_IO_FLAG_WANT_READ) FD_SET(CLIENTS(i)->fd, &read_fds);
                    if (TCP_CLIENTS(i)->io & TCP_IO_FLAG_WANT_WRITE) FD_SET(CLIENTS(i)->fd, &write_fds);
                    P_FD_MAX(max_fd, CLIENTS(i)->fd);
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
                if (CLIENTS(k)->proto < 0) {

                    CLIENTS(k)->fd = client_fd;

                    // WebSocket 监听端口
                    if (i == 1) { CLIENTS(k)->proto = PROTO_WSS;
                        if (!init_client(CLIENTS(k))) {
                            print("E:", LA_F("Failed to initialize %s client\n", LA_F174, 174), "WS/ICE");
                            P_sock_close(client_fd);
                            break;
                        }
                        print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                                "WS/ICE", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), k);
                    }
                    // 如果是多模态混合端口
                    else if (ARGS_ws.i64) CLIENTS(k)->proto = 127;  // 标记为"暂定"模式的客户端
                    // TCP/Relay 监听端口
                    else { CLIENTS(k)->proto = PROTO_RELAY;
                        if (!init_client(CLIENTS(k))) {
                            print("E:", LA_F("Failed to initialize %s client\n", LA_F174, 174), "TCP/RELAY");
                            P_sock_close(client_fd);
                            break;
                        }
                        print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                                "TCP/RELAY", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), k);
                    }
                    TCP_CLIENTS(k)->io = TCP_IO_FLAG_WANT_READ;  /* 新连接默认进入读取状态，等待客户端发送数据 */
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

        // 处理 TCP/IO
        for (int i = 0; i < MAX_PEERS; i++) { int8_t m = CLIENTS(i)->proto;
            if (m <= 0 || CLIENTS(i)->fd == P_INVALID_SOCKET) continue;
            ct_client_t* ws_client = (m == PROTO_WSS) ? CT_CLIENTS(i) : NULL;

            // 处理数据接收
            if ((TCP_CLIENTS(i)->io & TCP_IO_FLAG_WANT_READ) && FD_ISSET(CLIENTS(i)->fd, &read_fds)) {

                // 对于多模态混合端口，需要先根据数据包内容判断协议类型
                if (m == 127) {
#ifdef WITH_WS
                    uint8_t buf[1]; size_t n = sizeof(buf);  //ssize_t n = recv(CLIENTS(i)->fd, (char *)buf, sizeof(buf), MSG_PEEK);
                    ret_t r = P_recv_nonblock(CLIENTS(i)->fd, buf, &n, MSG_PEEK);
                    if (r > 0) continue; // would block
                    if (r < 0) {
                        if (r == E_NONE_CONTEXT) print("I:", LA_F("Client closed during protocol detection (slot %d)\n", LA_F172, 172), i);
                        else print("E:", LA_F("Failed to peek client data for protocol detection (slot %d), errno=%d\n", LA_F177, 177), i, r);
                        P_sock_close(CLIENTS(i)->fd);
                        CLIENTS(i)->fd = P_INVALID_SOCKET;
                        CLIENTS(i)->proto = -1;
                        continue;
                    }

                    sock_t saved_fd = CLIENTS(i)->fd;

                    // WebSocket 握手请求的特征。即 HTTP GET 请求行，也就是以 "GET " 开头
                    if (buf[0] == 'G') { CLIENTS(i)->proto = m = PROTO_WSS;
                        print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                              "WS/ICE", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), i);

                        if (!init_client(CLIENTS(i))) {
                            P_sock_close(saved_fd);
                            print("E:", LA_F("Failed to initialize WS/ICE client for slot %d\n", LA_F176, 176), i);
                            continue;
                        }
                        ws_client = CT_CLIENTS(i);
                    } else { CLIENTS(i)->proto = m = PROTO_RELAY;
                        print("I:", LA_F("New %s client connected from %s:%d, assigned slot %d\n", LA_F178, 178),
                              "TCP/RELAY", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), i);

                        if (!init_client(CLIENTS(i))) {
                            P_sock_close(saved_fd);
                            print("E:", LA_F("Failed to initialize TCP/RELAY client for slot %d\n", LA_F175, 175), i);
                            continue;
                        }
                    }
#else
                    assert(false);
#endif
                }

                if (m == PROTO_RELAY) ct_handle_recv((ct_client_ctx_t*)g_contexts[PROTO_RELAY], CT_CLIENTS(i), NULL);
#ifdef WITH_WS
                else if (ws_client) ct_handle_recv((ct_client_ctx_t*)g_contexts[PROTO_WSS], CT_CLIENTS(i), "WS"); // fixme 这里的操作可能会导致 client 被销毁，从而无需再执行后面
#endif
            }

            // 处理数据发送
            if ((TCP_CLIENTS(i)->io & TCP_IO_FLAG_WANT_WRITE)
                && FD_ISSET(CLIENTS(i)->fd, &write_fds)) {

                if (m == PROTO_RELAY) ct_handle_send((ct_client_ctx_t*)g_contexts[PROTO_RELAY], CT_CLIENTS(i), NULL);
#ifdef WITH_WS
                else if (ws_client) ct_handle_send((ct_client_ctx_t*)g_contexts[PROTO_WSS], CT_CLIENTS(i), "WS");
#endif
            }

#ifdef WITH_WS
            if (ws_client) cw_retry_closing((cw_client_t*)ws_client, now);
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
        if (CLIENTS(i)->proto < 0) continue;
        if (CLIENTS(i)->proto < PROTO_NUM) free_client(CLIENTS(i));
        else if (CLIENTS(i)->fd != P_INVALID_SOCKET) {
            P_sock_close(CLIENTS(i)->fd);
            CLIENTS(i)->fd = P_INVALID_SOCKET;
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
