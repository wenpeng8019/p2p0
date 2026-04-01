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
 * 日志原则：
 * - 调试级别：printf 输出协议包详细信息（Send/Received XXX pkt...）
 * - 正式级别：print(level, ...) 输出带 MOD_TAG 前缀的结构化日志
 * 
 * 对于收包处理：
 *   > 收到包时，调试打印包的详细信息
 *   > 处理过程中出现错误，输出 W/E 级日志
 *   > 成功处理后，输出 V 级日志说明结果
 * 
 * 对于发包操作：
 *   > 发送前，调试打印包的详细信息（目标地址、包类型、关键字段等）
 *   > 发送失败，输出 W/E 级日志
 *   > 发送成功，输出 V 级日志说明发送结果
 * 
 * 对于状态管理：
 *   > 状态变更、重要操作时，输出 I 级日志
 *   > 超时、清理等异常情况，输出 W 级日志
 */

#define MOD_TAG "P2P0_SERVER"

#include <p2p.h>
#include "../src/p2p_common.h"

#include "LANG.h"
#include "LANG.cn.h"

#include <signal.h>    /* signal() */
#include "uthash.h"

// 命令行参数定义
ARGS_I(false, port,       'p', "port",       LA_CS("Signaling server listen port (TCP+UDP)", LA_S10, 10));
ARGS_I(false, probe_port, 'P', "probe-port", LA_CS("NAT type detection port (0=disabled)", LA_S7, 7));
ARGS_B(false, relay,      'r', "relay",      LA_CS("Enable data relay support (COMPACT mode fallback)", LA_S5, 5));
ARGS_B(false, msg,        'm', "msg",        LA_CS("Enable MSG RPC support", LA_S6, 6));

static void cb_cn(const char* argv) { (void)argv;  lang_cn(); }
ARGS_PRE(cb_cn, cn,         0,   "cn",       LA_CS("Use Chinese language", LA_S11, 11));

#define DEFAULT_PORT                    9333

// cleanup 过期配对/客户端的时间间隔（秒）
#define CLEANUP_INTERVAL_S              10

// 允许最大同时在线客户端数量
#define MAX_PEERS                       128

// 允许最大候选队列缓存数量
/* + 服务器为每个用户提供的候选缓存能力
 |   32 个候选可容纳大多数网络环境的完整候选集合，实际场景通常：20-30 个候选，32 提供充足余量
 | + 内存占用：COMPACT 模式 32×7字节=224B/用户，RELAY 模式 32×32字节=1KB/用户
*/
#define MAX_CANDIDATES_CONFIG           32
#define MAX_CANDIDATES_BY_PAYLOAD       ((P2P_MAX_PAYLOAD - (2 * P2P_PEER_ID_MAX + 8 + 1)) / sizeof(p2p_candidate_t))
#define MAX_CANDIDATES                  ((MAX_CANDIDATES_CONFIG) < (MAX_CANDIDATES_BY_PAYLOAD) ? (MAX_CANDIDATES_CONFIG) : (MAX_CANDIDATES_BY_PAYLOAD))

// COMPACT 模式配对超时时间（秒）
// 客户端在 REGISTERED 状态每 20 秒发一次 keepalive REGISTER，此值取 3 倍间隔
#define COMPACT_PAIR_TIMEOUT_S          90

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT_S          60

#define COMPACT_RETRY_INTERVAL_MS       1000    // COMPACT 模式重传检查间隔（毫秒）
// COMPACT 模式 PEER_INFO 重传参数
#define PEER_INFO0_RETRY_INTERVAL_MS    2000    // 重传间隔（毫秒）
#define PEER_INFO0_MAX_RETRY            5       // 最大重传次数

// COMPACT 模式 MSG RPC 重传参数
#define MSG_RPC_RETRY_INTERVAL_MS       1000    // MSG RPC 统一重传间隔（毫秒）
#define MSG_REQ_MAX_RETRY               5       // MSG_REQ 最大重传次数
#define MSG_RESP_MAX_RETRY              10      // MSG_RESP 最大重传次数（比 REQ 更多，确保 A 端收到）

// RELAY 模式 SYNC 参数（TCP 保证可靠传输，无需应用层重传）
#define RELAY_SYNC_CANDS_PER_PACKET         10      // 每包最大候选数

typedef struct session session_t;

typedef struct client {
    bool                    valid;
    char                    local_peer_id[P2P_PEER_ID_MAX];
    uint32_t                instance_id;
    uint64_t                last_active;
    session_t*              sessions;
} client_t;

typedef struct session_pair {
    bool                    valid;
    char                    peer_id[2][P2P_PEER_ID_MAX];   // hh_peer 复合 key 起始（与 remote_peer_id 连续）
    session_t*              sessions[2];                   // 双端会话指针
    UT_hash_handle          hh_peer;
} session_pair_t;

typedef struct session {
    struct client*          client;
    struct session*         prev;
    struct session*         next;
    session_pair_t*         pair;
    uint64_t                session_id;
    UT_hash_handle          hh_session;
} session_t;

static session_t*           g_sessions = NULL;
static session_pair_t*      g_session_pirs = NULL;

#pragma pack(push, 1)
typedef struct buffer_item {
    struct buffer_item*     next;
    void*                   refer;
    uint8_t                 flags;
} buffer_item_t;
#pragma pack(pop)
#define ITEM2BUF(item)      ((uint8_t*)(item + 1))
#define BUF2ITEM(buf)       (((buffer_item_t*)(buf)) - 1)

//-----------------------------------------------------------------------------

typedef struct relay_session {
    session_t               base;
    struct relay_session*   peer;
    
    /* 向对端发送的待处理队列 */
    buffer_item_t*          peer_pending;                       // 由对端主动来取，用于控制发送节奏
                                                                // + 即对端的发送队列最多只有来自本端的一个发送项
                                                                //   当对端发送完来自本端的项后，会来此继续取下一项
                                                                // ! 该值可以为 -1, 表示最后一个数据包正在对端的发送队列中
    
    /* 本地发送队列 */
    buffer_item_t*          send_head;
    buffer_item_t*          send_rear;
    struct relay_session*   send_prev;
    struct relay_session*   send_next;
} relay_session_t;

// RELAY 模式客户端（TCP 长连接）- 统一接收通道
typedef struct relay_client {
    client_t                base;
    sock_t                  fd;
    
    bool                    online_ack_pending;  // ONLINE_ACK 待发送标志（复用 recv_buf）
    
    uint8_t*                recv_buf;
    uint16_t                recv_len;
    
    relay_session_t*        sending_head;
    relay_session_t*        sending_rear;
    uint16_t                send_offset;
} relay_client_t;

static relay_client_t       g_relay_clients[MAX_PEERS];
static buffer_item_t*       g_relay_recycle = NULL;
static buffer_item_t*       g_relay_recycleS = NULL;

#define RELAY_FRAME_SIZE         (sizeof(p2p_relay_hdr_t) + P2P_MAX_PAYLOAD)
#define RELAY_SMALL_FRAME_SIZE   (sizeof(p2p_relay_hdr_t) + P2P_MAX_PAYLOAD / 4)

#define RELAY_BUF_FLAGS_SMALL     0x01    // 小包标志（提示服务器优先发送，减少延迟）
#define RELAY_BUF_FLAGS_SYNC_FIN  0x02    // SYNC 包尾部 FIN 标记（告知服务器这是最后一包候选）

//-----------------------------------------------------------------------------

// COMPACT 模式配对记录（UDP 无状态）
/* 注意：COMPACT 模式采用"配对缓存"机制：
 *   A 注册 (local=alice, remote=bob, candidates=[...])
 *   B 注册 (local=bob, remote=alice, candidates=[...])
 *   服务器检测到双向匹配后，同时向 A 和 B 发送对方的候选列表
 */

typedef struct compact_pair {
    bool                    valid;                              // 记录是否有效（无效意味着未分配或已回收）
    char                    local_peer_id[P2P_PEER_ID_MAX];     // 本端 ID
    char                    remote_peer_id[P2P_PEER_ID_MAX];    // 目标对端 ID
    uint32_t                instance_id;                        // 客户端本次 connect() 的实例 ID（每次客户重启就变，区分重传 vs 新会话）
    struct compact_pair*    peer;                               // 指向配对的对端。(void*)-1 表示对端已断开

    struct sockaddr_in      addr;                               // 公网地址（UDP 源地址）
    uint8_t                 addr_notify_seq;                    // 发给对端的地址变更通知序号（base_index，1..255 循环）

    p2p_candidate_t         candidates[MAX_CANDIDATES];         // 候选列表（网络格式，直接收发）
    int                     candidate_count;                    // 候选数量

    uint64_t                last_active;                        // 最后活跃时间（毫秒）
    uint64_t                session_id;                         // 会话 ID（服务器分配，唯一标识一个配对，64位随机数）

    // PEER_INFO(seq=0) 可靠传输（首包 + 地址变更通知）
    int                     info0_acked;                        // 是否已收到首包 ACK，-1 表示未收到确认但已放弃
    struct compact_pair*    info0_pending_next;                 // 待确认链表指针（-1 表示链表最后一个）
    uint64_t                info0_sent_time;                    // 当前待确认 seq=0 最近发送时间（毫秒）
    int                     info0_retry;                        // 当前待确认 seq=0 重传次数
    uint8_t                 info0_base_index;                   // 当前待确认 seq=0 的 base_index（0=首包，!=0 地址变更通知）

    // MSG RPC（请求-响应机制，共用字段存储两个阶段的数据）
    uint16_t                rpc_last_sid;                       // 最后一次完成或正在执行的 RPC 序列号（0=未使用）
    struct compact_pair*    rpc_pending_next;                   // RPC 待确认链表指针（NULL=空闲，-1=链表尾）
    uint64_t                rpc_sent_time;                      // 最后发送时间（毫秒）
    int                     rpc_retry;                          // 重传次数
    bool                    rpc_responding;                     // RPC 阶段（false=REQ等待对端，true=RESP等待确认）
    uint8_t                 rpc_code;                           // RPC 消息类型/响应码（REQ阶段=消息类型，RESP阶段=响应码）
    uint8_t                 rpc_flags;                          // RPC flags（RESP 阶段使用：PEER_OFFLINE/TIMEOUT）
    uint8_t                 rpc_data[P2P_MSG_DATA_MAX];         // RPC 数据缓冲区
    int                     rpc_data_len;                       // RPC 数据长度
    
    // uthash handles（支持多种索引方式）
    UT_hash_handle          hh;                                 // 按 session_id 索引（主索引，必须命名为 hh）
    UT_hash_handle          hh_peer;                            // 按 peer_key (local+remote) 索引（辅助索引）
} compact_pair_t;

#define PEER_ONLINE(p)      ((p)->peer && (p)->peer != (compact_pair_t*)(void*)-1)  // 判断对端是否在线（peer 指针为 (void*)-1 表示已断开）
#define PEER_OF(p)          (PEER_ONLINE(p) ? (p)->peer : NULL)

static compact_pair_t       g_compact_pairs[MAX_PEERS];


// uthash 哈希表（支持两种索引方式）
static compact_pair_t*      g_pairs_by_session = NULL;         // 按 session_id 索引
static compact_pair_t*      g_pairs_by_peer = NULL;            // 按 (local_peer_id, remote_peer_id) 索引

// PEER_INFO(seq=0) 待确认链表（仅包含已发送首包但未收到 ACK 的配对）
static compact_pair_t*      g_compact_info0_pending_head = NULL;
static compact_pair_t*      g_compact_info0_pending_rear = NULL;

// MSG RPC 待确认链表（统一管理 REQ 和 RESP 阶段，通过 rpc_responding 区分）
static compact_pair_t*      g_rpc_pending_head = NULL;
static compact_pair_t*      g_rpc_pending_rear = NULL;

//-----------------------------------------------------------------------------

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t g_running = 1;

///////////////////////////////////////////////////////////////////////////////

// 生成安全的随机 session_id（64位，加密安全，防止跨会话注入攻击）
static uint64_t generate_relay_session_id(void) {
    uint64_t id;
    session_t *existing;
    int attempts = 0;
    
    // 使用循环代替递归，避免极端情况下的栈溢出
    do {
        id = P_rand64();  // 使用 stdc.h 统一封装的加密安全随机数
        HASH_FIND(hh_session, g_sessions, &id, sizeof(uint64_t), existing);
        
        // 安全限制：虽然冲突概率极低（1/2^64），但在极端情况下提供保护
        if (++attempts > 1000) {
            print("F:", "Cannot generate unique relay_session_id after 1000 attempts\n");
            exit(1);
        }
    } while (existing);
    
    return id;
}

static uint64_t generate_compact_pair_id(void) {
    uint64_t id;
    compact_pair_t *existing;
    int attempts = 0;
    
    // 使用循环代替递归，避免极端情况下的栈溢出
    do {
        id = P_rand64();  // 使用 stdc.h 统一封装的加密安全随机数
        HASH_FIND(hh, g_pairs_by_session, &id, sizeof(uint64_t), existing);
        
        // 安全限制：虽然冲突概率极低（1/2^64），但在极端情况下提供保护
        if (++attempts > 1000) {
            print("F:", "Cannot generate unique compact_pair_id after 1000 attempts\n");
            exit(1);
        }
    } while (existing);
    
    return id;
}

// 创建新会话对
static int build_session(client_t *client, const char *remote_peer_id,
                         session_t **local_s, session_t **remote_s, size_t session_type_size) {
    if (!client || !remote_peer_id || !local_s || !remote_s) return -1;
    *local_s = NULL;
    *remote_s = NULL;

    session_t *s = (session_t*)calloc(1, session_type_size);
    if (!s) {
        return -1;
    }

    char peer_key[3 * P2P_PEER_ID_MAX];
    memset(peer_key, 0, sizeof(peer_key));
    strncpy(peer_key, client->local_peer_id, P2P_PEER_ID_MAX);
    strncpy(peer_key + P2P_PEER_ID_MAX, remote_peer_id, P2P_PEER_ID_MAX);

    session_pair_t *pair = NULL;
    HASH_FIND(hh_peer, g_session_pirs, peer_key, 2 * P2P_PEER_ID_MAX, pair);
    if (!pair) {
        strncpy(peer_key + 2 * P2P_PEER_ID_MAX, client->local_peer_id, P2P_PEER_ID_MAX);
        HASH_FIND(hh_peer, g_session_pirs, peer_key + P2P_PEER_ID_MAX, 2 * P2P_PEER_ID_MAX, pair);
    }

    int side = 0;
    session_t *opposite_s = NULL;
    if (!pair) {

        pair = (session_pair_t*)calloc(1, sizeof(session_pair_t));
        if (!pair) {
            free(s);
            return -1;
        }

        pair->valid = true;
        memcpy(pair->peer_id[0], client->local_peer_id, P2P_PEER_ID_MAX);
        memcpy(pair->peer_id[1], remote_peer_id, P2P_PEER_ID_MAX);
        HASH_ADD_KEYPTR(hh_peer, g_session_pirs, pair->peer_id, 2 * P2P_PEER_ID_MAX, pair);

        side = 0;
        opposite_s = NULL;
    }
    else if (pair->sessions[0]) { assert(!pair->sessions[1]);

        if (pair->sessions[0]->client == client) {
            print("E:", LA_F("Duplicate session create blocked: '%s' -> '%s'\n", LA_F266, 266),
                    client->local_peer_id, remote_peer_id);
            free(s);
            return -1;
        }
        side = 1;
        opposite_s = pair->sessions[0];
    }
    else { assert(pair->sessions[1]);

        if (pair->sessions[1]->client == client) {
            print("E:", LA_F("Duplicate session create blocked: '%s' -> '%s'\n", LA_F266, 266),
                    client->local_peer_id, remote_peer_id);
            free(s);
            return -1;
        }
        side = 0;
        opposite_s = pair->sessions[1];
    } 

    pair->sessions[side] = s;
    s->pair = pair;

    s->client = client;
    s->prev = NULL;
    s->next = client->sessions;
    if (client->sessions) client->sessions->prev = s;
    client->sessions = s;

    s->session_id = generate_relay_session_id();
    HASH_ADD(hh_session, g_sessions, session_id, sizeof(uint64_t), s);
    
    *local_s = s;
    *remote_s = opposite_s;
    return side;
}

// 释放会话
static void free_session(session_t *s) {
    if (!s) return;

    client_t *client = s->client;
    if (client) {
        if (s->prev) s->prev->next = s->next;
        else         client->sessions = s->next;
        if (s->next) s->next->prev = s->prev;
        s->prev = s->next = NULL;
    }

    HASH_DELETE(hh_session, g_sessions, s);

    session_pair_t *pair = s->pair;
    if (pair) {
        for (int i = 0; i < 2; ++i) {
            if (pair->sessions[i] == s) {
                pair->sessions[i] = NULL;
                break;
            }
        }
        if (!pair->sessions[0] && !pair->sessions[1]) {
            HASH_DELETE(hh_peer, g_session_pirs, pair);
            free(pair);
        }
    }

    free(s);
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "RELAY"

// TCP 发送辅助函数：异步发送，遇到 WOULDBLOCK 则加入发送队列
// 说明：用于发送小消息（ACK、header 等）
//      先尝试立即发送，若发送缓冲区满则依赖主循环的异步发送机制
// 返回: 0=全部发送完成, +1=WOULDBLOCK, -1=连接关闭(EOF), -2=真错误
// len_io: 输入=希望发送字节数，输出=实际发送字节数
static int tcp_send(relay_client_t *client, const void *buf, size_t *len_io, const char *reason) {
    if (!len_io) return -2;
    size_t len = *len_io; *len_io = 0;
    if (!client || client->fd == P_INVALID_SOCKET) return -2;
    if (!reason) reason = "unknown";

    while (*len_io < len) {
        ssize_t n = send(client->fd, (const char *)buf + *len_io, len - *len_io, 0);
        if (n < 0) {
            if (P_sock_is_interrupted()) continue;
            if (P_sock_is_wouldblock()) return 1;
            print("E:", LA_F("send(%s) failed: errno=%d\n", LA_F278, 278), reason, P_sock_errno());
            return -2;
        }
        if (n == 0) {
            print("I:", LA_F("Client closed connection (EOF on send, reason=%s)\n", LA_F265, 265), reason);
            return -1;
        }
        *len_io += (size_t)n;
    }
    return 0;
}

// TCP 接收辅助函数：异步接收，遇到 WOULDBLOCK 立即返回
// 返回: 0=全部接收完成, +1=WOULDBLOCK, -1=连接关闭(EOF), -2=真错误
// len_io: 输入=希望接收字节数，输出=实际接收字节数
static int tcp_recv(relay_client_t *client, void *buf, size_t *len_io) {
    if (!len_io) return -2;
    size_t len = *len_io; *len_io = 0;
    if (!client || client->fd == P_INVALID_SOCKET) return -2;

    while (*len_io < len) {
        ssize_t n = recv(client->fd, (char *)buf + *len_io, len - *len_io, 0);
        if (n < 0) {
            if (P_sock_is_interrupted()) continue;
            if (P_sock_is_wouldblock()) return 1;
            print("E:", LA_F("recv() failed: errno=%d\n", LA_F277, 277), P_sock_errno());
            return -2;
        }
        if (n == 0) {
            print("I:", LA_F("% Client closed connection (EOF on recv)\n", LA_F249, 249));
            return -1;
        }
        *len_io += (size_t)n;
    }
    return 0;
}

static buffer_item_t* relay_buf_alloc(uint16_t len) {

    buffer_item_t **recycle_head; size_t capacity; uint8_t flags;
    if (len <= RELAY_SMALL_FRAME_SIZE) {
        recycle_head = &g_relay_recycleS;
        capacity = RELAY_SMALL_FRAME_SIZE;
        flags = RELAY_BUF_FLAGS_SMALL;
    } else {
        recycle_head = &g_relay_recycle;
        capacity = RELAY_FRAME_SIZE;
        flags = 0;
    }

    buffer_item_t *item = *recycle_head;
    if (item) *recycle_head = item->next;
    else if (!(item = (buffer_item_t*)malloc(sizeof(buffer_item_t) + capacity))) return NULL;
    else item->flags = flags;
    item->refer = NULL;
    return item;
}

static void relay_buf_free(buffer_item_t *buf_item) {

    buf_item->flags &= RELAY_BUF_FLAGS_SMALL;  // 清除小包以外的所有其他标志
    if (buf_item->flags & RELAY_BUF_FLAGS_SMALL) {
        buf_item->next = g_relay_recycleS;
        g_relay_recycleS = buf_item;
        return;
    }
    buf_item->next = g_relay_recycle;
    g_relay_recycle = buf_item;
}

static void relay_free_session(relay_session_t *s) {

    if (s->peer) {
        relay_session_t *peer = s->peer; s->peer = NULL;
        peer->peer = NULL;
        relay_free_session(peer);
    }

    // 从 client->sending 链表中摘除当前 session（双向链表 O(1)）
    if (s->base.client && (s->send_prev || s->send_next
                          || ((relay_client_t*)s->base.client)->sending_head == s)) {
        relay_client_t *client = (relay_client_t*)s->base.client;
        if (s->send_prev) s->send_prev->send_next = s->send_next;
        else              client->sending_head = s->send_next;
        if (s->send_next) s->send_next->send_prev = s->send_prev;
        else              client->sending_rear = s->send_prev;
        s->send_prev = s->send_next = NULL;
        client->send_offset = 0;
    }

    // 释放发送队列
    while (s->send_head) {
        buffer_item_t *next = s->send_head->next;
        relay_buf_free(s->send_head);
        s->send_head = next;
    }
    s->send_rear = NULL;

    // 释放对端待处理项
    if (s->peer_pending) {
        if (s->peer_pending != (buffer_item_t *)-1) {
            relay_buf_free(s->peer_pending);
        }
        s->peer_pending = NULL;
    }

    free_session(&s->base);
}

static void relay_clear_client(relay_client_t *c) {

    P_sock_close(c->fd);
    c->fd = P_INVALID_SOCKET;

    c->online_ack_pending = false;
    c->recv_len = 0;
    if (c->recv_buf) {
        relay_buf_free(BUF2ITEM(c->recv_buf));
        c->recv_buf = NULL;
    }
    for (relay_session_t *p = c->sending_head; p; ) {
        relay_session_t *nx = p->send_next;
        p->send_prev = p->send_next = NULL;
        p = nx;
    }
    c->sending_head = c->sending_rear = NULL;
    c->send_offset = 0;

    while (c->base.sessions) {
        relay_free_session((relay_session_t*)c->base.sessions);
    }
    c->base.local_peer_id[0] = 0;
    c->base.valid = false;
}

//-----------------------------------------------------------------------------

static void relay_session_send(relay_session_t *s, buffer_item_t* buf_item) {

    assert(s && s->base.client);

    // 添加到 session 的本地发送队列
    buf_item->next = NULL;
    if (s->send_rear) {
        s->send_rear->next = buf_item;
        s->send_rear = buf_item;
        return;
    }

    // 发送队列原本为空，添加 session 到客户端的发送链表尾部（如果不在链表中）
    assert(!s->send_next && !s->send_prev);
    s->send_head = s->send_rear = buf_item;

    relay_client_t *client = (relay_client_t*)s->base.client;
    s->send_prev = client->sending_rear;
    s->send_next = NULL;
    if (client->sending_rear) {
        client->sending_rear->send_next = s;
        client->sending_rear = s;
    } else client->sending_head = client->sending_rear = s;
}

static void relay_session_send_sync0_ack(relay_session_t *s, uint8_t online) {

    assert(s && s->base.session_id && s->base.client);
    relay_client_t *client = (relay_client_t*)s->base.client;

    uint16_t payload_len = P2P_RLY_SYNC0_ACK_PSZ;
    buffer_item_t *buf_item = relay_buf_alloc(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("W:", LA_F("SYNC0_ACK queue busy for '%s', drop\n", LA_F273, 273), client->base.local_peer_id);
        return;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC0_ACK;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_ll(payload, s->base.session_id);

    payload[P2P_RLY_SESS_ID_PSZ] = (uint8_t)(online ? 1 : 0);

    relay_session_send(s, buf_item);
}

static void relay_session_send_sync_ack(relay_session_t *s, uint8_t confirmed_count) {

    assert(s && s->base.session_id && s->base.client);
    relay_client_t *client = (relay_client_t*)s->base.client;

    uint16_t payload_len = P2P_RLY_SYNC_ACK_PSZ;
    buffer_item_t *buf_item = relay_buf_alloc(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("W:", LA_F("SYNC_ACK queue busy for '%s', drop\n", LA_F274, 274), client->base.local_peer_id);
        return;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC_ACK;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_ll(payload, s->base.session_id);

    payload[P2P_RLY_SESS_ID_PSZ] = confirmed_count;

    relay_session_send(s, buf_item);
}

static void relay_session_send_complete(relay_session_t *s, buffer_item_t* buf_item) {

    // 如果 buf_item 是 P2P_RLY_SYNC 的最后一个 fin 包，回复独立 SYNC_ACK (confirmed=0) 作为 fin 确认
    if (buf_item && buf_item->flags & RELAY_BUF_FLAGS_SYNC_FIN) {
        relay_session_send_sync_ack(s, 0);
    }

    // 如果 s->peer_pending，将其添加到当前的发送队列
    buffer_item_t *pending = s->peer_pending;
    if (pending) { assert(s->peer);

        // 如果对端发送完成的是最后一个数据包
        if (pending == (buffer_item_t*)(void*)-1) {
            s->peer_pending = NULL;
            return;
        }
        
        // 添加数据包到对端的发送队列
        pending->refer = s;
        relay_session_send(s->peer, pending);
        s->peer_pending = (buffer_item_t*)(void*)-1;    // 标记对端正在发送最后一个数据包

        p2p_relay_hdr_t *p_hdr = (p2p_relay_hdr_t *)ITEM2BUF(pending);
        assert(p_hdr->type != P2P_RLY_SYNC0);           // pending 为 SYNC0 的情况，会两端首次握手（handle_relay_sync0）时处理

        // 如果是 P2P_RLY_SYNC，则回复 P2P_RLY_SYNC_ACK
        if (p_hdr->type == P2P_RLY_SYNC) {
            uint8_t candidate_count = ((uint8_t *)(p_hdr + 1))[P2P_RLY_SESS_ID_PSZ];
            if (candidate_count)
                relay_session_send_sync_ack(s, candidate_count);
        }
    }
}

static void relay_send_status(relay_client_t *client, uint8_t req_type, uint8_t status_code) {

    buffer_item_t *buf_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
    if (!buf_item) return;

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STATUS;
    hdr->size = htons(P2P_RLY_STATUS_PSZ);
    uint8_t *p = (uint8_t *)(hdr + 1);
    p[0] = req_type;
    p[1] = status_code;

    // STATUS 包不走 session 队列，直接挂到 client 上
    // 借用一个临时空 session 结构是不合适的，这里直接用 tcp_send 尝试发送
    size_t len = sizeof(p2p_relay_hdr_t) + P2P_RLY_STATUS_PSZ;
    tcp_send(client, ITEM2BUF(buf_item), &len, "STATUS");
    relay_buf_free(buf_item);
}

//-----------------------------------------------------------------------------

// 处理 SYNC0 消息（首次同步）
// payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
static void handle_relay_sync0(relay_client_t *client, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC0";

    if (len < P2P_PEER_ID_MAX + 1) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F257, 257), PROTO, len);
        return;
    }
    uint8_t cand_count = payload[P2P_PEER_ID_MAX];
    uint32_t expect_len = P2P_RLY_SYNC0_PSZ(cand_count);
    if (len != expect_len) {
        print("E:", LA_F("%s: bad payload(cnt=%d, len=%u, expected=%u)\n", LA_F255, 255),
               PROTO, cand_count, len, expect_len);
        return;
    }

    payload[P2P_PEER_ID_MAX - 1] = '\0';
    
    // build_session 返回：side (>=0: side=0|1, <0: error), local_s, remote_s
    relay_session_t *local_s = NULL, *remote_s = NULL;
    int side = build_session(&client->base, (const char *)payload, (session_t**)&local_s, (session_t**)&remote_s, sizeof(relay_session_t));
    if (side < 0) {
        print("E:", LA_F("%s: build_session failed for '%s'\n", LA_F258, 258), PROTO, (const char *)payload);
        return;
    }

    // 截断候选计数
    if (cand_count > MAX_CANDIDATES) 
        cand_count = MAX_CANDIDATES;

    print("V:", LA_F("%s: local='%s', remote='%s', side=%d, peer_online=%d, cands=%d\n", LA_F261, 261),
           PROTO, client->base.local_peer_id, (const char *)payload, side, remote_s ? 1 : 0, cand_count);

    // 立即返回 SYNC0_ACK（会话建立确认）
    // + SYNC0_ACK 告知会话建立结果：session_id + 对端在线状态
    relay_session_send_sync0_ack(local_s, remote_s && ((relay_client_t*)remote_s->base.client)->fd != P_INVALID_SOCKET);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync0_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync0_item = relay_buf_alloc(RELAY_FRAME_SIZE);
        if (!sync0_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F253, 253), PROTO);
            return;
        }

        // 标记该包是为了零拷贝转发而构造的，后续处理时会区分对待
        ((p2p_relay_hdr_t *)client->recv_buf)->size = 0;

        // 构造零拷贝转发的 SYNC0 包
        hdr = (p2p_relay_hdr_t *)(client->recv_buf + P2P_PEER_ID_MAX - P2P_RLY_SESS_ID_PSZ);
        hdr->size = P2P_RLY_SYNC_PSZ(cand_count, false);

        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(sync0_item);
        client->recv_len = 0;
        sync0_item = item;
    }
    else {

        sync0_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
        if (!sync0_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F253, 253), PROTO);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync0_item));
        hdr->size = P2P_RLY_SYNC_PSZ(0, false);
    }
    hdr->type = P2P_RLY_SYNC0;
    hdr->size = htons(hdr->size);

    // 对端已在线
    if (remote_s) {

        // 建立双向引用关系
        if (!local_s->peer) local_s->peer = remote_s;
        if (!remote_s->peer) remote_s->peer = local_s;

        //-------

        assert(!local_s->peer_pending);

        // 本端 SYNC0 转发给对端前，需要写入对端 session_id
        uint8_t* sid = (uint8_t*)(hdr+1);
        nwrite_ll(sid, remote_s->base.session_id);
        
        // 添加到对端发送队列（零拷贝转发），设置 refer 触发传完后的 complete 回调
        sync0_item->refer = local_s;
        local_s->peer_pending = (buffer_item_t*)-1;
        relay_session_send(remote_s, sync0_item);

        //-------

        assert(remote_s->peer_pending && remote_s->peer_pending != (buffer_item_t*)-1);

        buffer_item_t *remote_sync0_item = remote_s->peer_pending;
        if (remote_sync0_item) { remote_s->peer_pending = NULL;

            hdr = (p2p_relay_hdr_t *)ITEM2BUF(remote_sync0_item);

            // 对端 SYNC0 转发给本端前，需要写入本端 session_id
            uint8_t *cached_sid = (uint8_t*)(hdr+1);
            if (hdr->size == 0) cached_sid += P2P_PEER_ID_MAX - P2P_RLY_SESS_ID_PSZ;
            nwrite_ll(cached_sid, local_s->base.session_id);

            // 添加到本端发送队列，也要设置 refer
            remote_sync0_item->refer = remote_s;
            remote_s->peer_pending = (buffer_item_t*)-1;
            relay_session_send(local_s, remote_sync0_item);

            // 如果对端 sync0 发送的同步数据，发送 SYNC_ACK 告知对端同步数据已（确认）转发
            if (hdr->size == 0) {
                uint8_t remote_cand_count = cached_sid[P2P_RLY_SESS_ID_PSZ];
                relay_session_send_sync_ack(remote_s, remote_cand_count);
            }
        }

        //-------

        // 如果本端 sync0 发送的同步数据，发送 SYNC_ACK 告知本端同步数据已（确认）转发
        if (cand_count) relay_session_send_sync_ack(local_s, cand_count);

        print("I:", LA_F("%s: forwarded to peer, cands=%d\n", LA_F260, 260), PROTO, cand_count);
    }
    // 对端未在线，将 sync0 包缓存到 local_s->peer_pending
    else {
        
        if (local_s->peer_pending) {
            relay_buf_free(local_s->peer_pending);
        }
        local_s->peer_pending = sync0_item;

        print("W:", LA_F("%s: peer '%s' offline, cached cands=%d\n", LA_F262, 262), 
               PROTO, (const char *)payload, cand_count);
    }
}

// 处理 SYNC 消息（候选同步）
static void handle_relay_sync(relay_client_t *client, relay_session_t *s, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC";

    if (len < P2P_RLY_SYNC_PSZ(0, false)) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F257, 257), PROTO, len);
        return;
    }

    uint8_t cand_count = payload[P2P_RLY_SESS_ID_PSZ];
    uint16_t payload_sz = P2P_RLY_SYNC_PSZ(cand_count, false);
    if (len == payload_sz + 1u) {

        if (payload[payload_sz] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F254, 254), PROTO, payload[payload_sz]);
            return;
        }
    }
    else if (len != payload_sz) {

        print("E:", LA_F("%s: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n", LA_F256, 256),
               PROTO, (unsigned)cand_count, len, payload_sz);

        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync_item = relay_buf_alloc(RELAY_FRAME_SIZE);
        if (!sync_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F253, 253), PROTO);
            return;
        }

        hdr = (p2p_relay_hdr_t *)client->recv_buf;
        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(sync_item);
        client->recv_len = 0;
        sync_item = item;        
    }
    else {

        sync_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
        if (!sync_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F253, 253), PROTO);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync_item));
        hdr->type = P2P_RLY_SYNC;
        hdr->size = P2P_RLY_SYNC_PSZ(0, true);
        hdr->size = htons(hdr->size);

        payload = (uint8_t*)(hdr+1) + P2P_RLY_SESS_ID_PSZ;
        payload[0] = 0;
        payload[1] = P2P_RLY_SYNC_FIN_MARKER;
    }

    // 交换写入对端的 session_id
    uint8_t *sid_ptr = (uint8_t *)(hdr + 1);
    nwrite_ll(sid_ptr, s->peer->base.session_id);

    // 如果是最后一个 SYNC 数据包，设置标志位
    // + 发送完成后，可以对此进行额外特殊处理（即回复一个独立 SYNC_ACK:confirmed=0，作为 SYN FIN 完成通知）
    if (len == payload_sz + 1u || !cand_count) {
        sync_item->flags |= RELAY_BUF_FLAGS_SYNC_FIN;
    }

    if (s->peer_pending) {
        assert(s->peer_pending == (buffer_item_t *)-1);
        s->peer_pending = sync_item;
    }
    else { s->peer_pending = sync_item;
        relay_session_send_complete(s, NULL);
    }
}

// 处理 FIN 消息（会话结束）
static void handle_relay_fin(relay_session_t *s, uint8_t *payload, uint16_t len) {
    const char *PROTO = "FIN";

    if (len != P2P_RLY_FIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F257, 257), PROTO, len);
        return;
    }

    print("I:", LA_F("%s: close ses_id=%" PRIu64 "\n", LA_F259, 259), PROTO, s->base.session_id);

    if (s->peer) {

        buffer_item_t *buf_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
        if (!buf_item) {
            print("E:", LA_F("%s: OOM for relay buffer\n", LA_F252, 252), PROTO);
            relay_free_session(s);
            return;
        }

        // 如果对端还有待转发的数据，先发送对端的数据，再发送 FIN，确保数据完整性
        if (s->peer_pending && s->peer_pending != ((buffer_item_t *)-1)) {
            relay_session_send(s->peer, s->peer_pending);
        }
        s->peer_pending = NULL;

        p2p_relay_hdr_t* hdr = (p2p_relay_hdr_t *)(ITEM2BUF(buf_item));
        hdr->type = P2P_RLY_FIN;
        hdr->size = htons(P2P_RLY_FIN_PSZ);
        payload = (uint8_t*)(hdr+1);
        nwrite_ll(payload, s->peer->base.session_id);

        relay_session_send(s->peer, buf_item);
    }

    relay_free_session(s);
}

// 处理 DATA 消息
static void handle_relay_data(relay_client_t *client, relay_session_t *s, const uint8_t *payload, uint16_t len) {
    const char *PROTO = "DATA";

    if (len < 8) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F257, 257), PROTO, len);
        return;
    }

    // relay_session_t *peer_s = s->peer;
    // relay_client_t *peer_client = (relay_client_t*)peer_s->base.client;
    // if (!peer_client || peer_client->fd == P_INVALID_SOCKET) {
    //     printf(LA_F("[TCP] W: Session %" PRIu64 " peer offline\n", LA_F228, 228), session_id);
    //     return;
    // }

}

//-----------------------------------------------------------------------------

// 处理 RELAY 模式信令（TCP 长连接）- 统一接收+分发架构
// 架构：client 统一接收完整消息到 recv_buf，解析后分发给对应的处理函数
// 流程：read header → read full payload → dispatch → reset buffer
static void handle_relay_signaling(int idx) {
    relay_client_t *client = &g_relay_clients[idx]; 
    sock_t fd = client->fd; assert(client->recv_buf);

    client->base.last_active = P_tick_ms();
    for(;;) {

        // 状态检查：ONLINE_ACK 未完成前不应接收新消息
        // + 此时 recv_buf 已被 ONLINE_ACK 复用 send_buf，接收新消息会覆盖未发送的 ACK 内容
        if (client->online_ack_pending) {
            print("E:", LA_F("% Client sent data before ONLINE_ACK completed\n", LA_F251, 251));
            goto disconnect;
        }

        // 读取 header (3字节)
        while (client->recv_len < sizeof(p2p_relay_hdr_t)) {
            size_t need = sizeof(p2p_relay_hdr_t) - client->recv_len;
            int rc = tcp_recv(client, client->recv_buf + client->recv_len, &need);
            if (rc > 0) return;
            if (rc < 0) goto disconnect;
            client->recv_len += (uint16_t)need;
        }
        
        // 解析 header
        uint8_t type = client->recv_buf[0]; uint8_t* ptr = client->recv_buf + 1;
        uint16_t payload_len = nget_s(ptr);
        if (payload_len > P2P_MAX_PAYLOAD) {
            print("E:", LA_F("bad payload len %u\n", LA_F276, 276), payload_len);
            goto disconnect;
        }
        
        // 读取完整 payload
        uint16_t total_need = sizeof(p2p_relay_hdr_t) + payload_len;
        while (client->recv_len < total_need) {
            size_t need = total_need - client->recv_len;
            int rc = tcp_recv(client, client->recv_buf + client->recv_len, &need);
            if (rc > 0) return;
            if (rc < 0) goto disconnect;
            client->recv_len += (uint16_t)need;
        }
        
        // 完整消息已接收，分发处理
        uint8_t *payload = client->recv_buf + sizeof(p2p_relay_hdr_t);

        if (type == P2P_RLY_ONLINE) {

            // 处理 ONLINE 消息：[name(32)][instance_id(4)]
            if (payload_len != P2P_PEER_ID_MAX + 4) {
                print("E:", LA_F("ONLINE: bad payload(len=%u, expected=%u)\n", LA_F270, 270), 
                       payload_len, (uint32_t)(P2P_PEER_ID_MAX + 4));
                goto disconnect;
            }
            // 禁止重复 ONLINE
            if (client->base.local_peer_id[0]) {
                print("E:", LA_F("ONLINE: duplicate from '%s'\n", LA_F271, 271), client->base.local_peer_id);
                goto disconnect;
            }
            
            memcpy(client->base.local_peer_id, payload, P2P_PEER_ID_MAX);
            client->base.local_peer_id[P2P_PEER_ID_MAX-1] = '\0';
            nread_l(&client->base.instance_id, payload + P2P_PEER_ID_MAX);

            // 查找是否存在同名的已登录 client（断网重连场景）
            // MAX_PEERS=128，ONLINE 低频操作，线性扫描足够
            relay_client_t *old = NULL;
            for (int i = 0; i < MAX_PEERS; i++) {
                relay_client_t *c = &g_relay_clients[i];
                if (c != client && c->base.valid && c->base.local_peer_id[0]
                    && strcmp(c->base.local_peer_id, client->base.local_peer_id) == 0) {
                    old = c; break;
                }
            }

            // 如果存在同名 client，根据 instance_id 判断是否同实例重连
            if (old) {
                if (old->base.instance_id == client->base.instance_id) {
                    // 同实例重连：迁移 fd 到旧槽位，保留会话状态
                    print("I:", LA_F("ONLINE: '%s' reconnected (inst=%u), migrating fd\n", LA_F269, 269),
                           client->base.local_peer_id, client->base.instance_id);
                    P_sock_close(old->fd);
                    old->fd = client->fd;
                    old->base.last_active = P_tick_ms();
                    old->online_ack_pending = false;
                    old->recv_len = 0;
                    // 交换 recv_buf：旧的释放，新的移交
                    if (old->recv_buf) relay_buf_free(BUF2ITEM(old->recv_buf));
                    old->recv_buf = client->recv_buf;
                    client->recv_buf = NULL;
                    // 当前槽位置为无效（fd 已移交，不 close）
                    client->fd = P_INVALID_SOCKET;
                    client->base.local_peer_id[0] = 0;
                    client->base.valid = false;
                    client = old;
                } else {
                    // 新实例：销毁旧 client 的所有状态
                    print("I:", LA_F("ONLINE: '%s' new instance (old=%u, new=%u), destroying old\n", LA_F268, 268),
                           client->base.local_peer_id, old->base.instance_id, client->base.instance_id);
                    relay_clear_client(old);
                }
            }
            
            print("I:", LA_F("ONLINE: '%s' came online (inst=%u)\n", LA_F267, 267),
                     client->base.local_peer_id, client->base.instance_id);
            
            // 就地修改 recv_buf 为 ONLINE_ACK (复用缓冲区)
            p2p_relay_hdr_t *ack_hdr = (p2p_relay_hdr_t *)client->recv_buf;
            ack_hdr->type = P2P_RLY_ONLINE_ACK;
            ack_hdr->size = htons(P2P_RLY_ONLINE_ACK_PSZ);
            uint8_t *ack_payload = (uint8_t*)(ack_hdr+1);
            ack_payload[0/* features */] = 0;
            if (ARGS_relay.i64) ack_payload[0] |= P2P_RLY_FEATURE_RELAY;
            if (ARGS_msg.i64) ack_payload[0] |= P2P_RLY_FEATURE_MSG;
            ack_payload[1/* candidate_sync_max */] = (uint8_t)RELAY_SYNC_CANDS_PER_PACKET;
            
            // 尝试立即发送（WOULDBLOCK 循环直到发送完或）
            size_t ack_len = sizeof(p2p_relay_hdr_t) + P2P_RLY_ONLINE_ACK_PSZ;
            int rc = tcp_send(client, client->recv_buf, &ack_len, "ONLINE_ACK");
            if (rc > 0) { // WOULDBLOCK，标记待发送
                client->online_ack_pending = true;
                client->recv_len = ack_len;
                return;  // 注意：此时不加入哈希表，等 ACK 发送完成后再加入
            }
            if (rc < 0) goto disconnect;            
        }
        // 除 ONLINE 外，所有消息都要求已完成登录
        else if (!client->base.local_peer_id[0]) {
            print("E:", LA_F("type=%u rejected: client not logged in\n", LA_F281, 281), (unsigned)type);
            relay_send_status(client, type, P2P_RLY_ERR_NOT_ONLINE);
            goto disconnect;
        }
        else if (type == P2P_RLY_ALIVE);    // 心跳包：last_active 已在循环入口更新，无需额外处理
        else if (type == P2P_RLY_SYNC0) {
            handle_relay_sync0(client, payload, payload_len);
        }
        else {
            if (payload_len < P2P_RLY_SESS_ID_PSZ) {
                print("E:", LA_F("bad payload len %u (type=%u)\n", LA_F275, 275), payload_len, (unsigned)type);
                client->recv_len = 0;
                continue;
            }

            uint64_t session_id;
            nread_ll(&session_id, payload);
            session_t *s = NULL;
            HASH_FIND(hh_session, g_sessions, &session_id, P2P_RLY_SESS_ID_PSZ, s);
            if (s == NULL || s->client != &client->base) {
                print("W:", LA_F("unknown ses_id=%" PRIu64 " (type=%u)\n", LA_F282, 282), session_id, (unsigned)type);
                client->recv_len = 0;
                continue;
            }

            relay_session_t *rs = (relay_session_t*)s;

            // FIN 不需要对端在线（单边关闭）
            if (type == P2P_RLY_FIN) {
                handle_relay_fin(rs, payload, payload_len);
            }
            // SYNC / DATA 等转发操作需要对端已连接
            else if (!rs->peer) {
                print("W:", LA_F("ses_id=%" PRIu64 " peer not connected (type=%u)\n", LA_F280, 280), session_id, (unsigned)type);
                relay_send_status(client, type, P2P_RLY_ERR_PEER_OFFLINE);
            }
            // SYNC 转发需要前一个转发已完成（peer_pending 为空或 -1）
            else if (type == P2P_RLY_SYNC && rs->peer_pending && rs->peer_pending != (buffer_item_t*)-1) {
                print("W:", LA_F("ses_id=%" PRIu64 " busy (pending sync)\n", LA_F279, 279), session_id);
                relay_send_status(client, type, P2P_RLY_ERR_BUSY);
            }
            else switch (type) {
            case P2P_RLY_SYNC:
                handle_relay_sync(client, rs, payload, payload_len);
                break;
            case P2P_RLY_DATA:
                handle_relay_data(client, rs, payload, payload_len);
                break;
            default:
                print("E:", LA_F("unsupported type=%u (ses_id=%" PRIu64 ")\n", LA_F283, 283),
                       (unsigned)type, session_id);
                goto disconnect;
            }
        }
        
        // 重置缓冲区，准备接收下一个消息
        client->recv_len = 0;
    }
    
disconnect:
    if (client->base.local_peer_id[0]) {
        print("I:", LA_F("'%s' disconnected\n", LA_F263, 263), client->base.local_peer_id);
    } else {
        print("I:", LA_F("% Client disconnected (not yet logged in)\n", LA_F250, 250));
    }
    relay_clear_client(client);
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {

    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) { relay_client_t *c = &g_relay_clients[i];

        if (!c->base.valid || c->fd == P_INVALID_SOCKET) continue;
        if (tick_diff(now, c->base.last_active) <= RELAY_CLIENT_TIMEOUT_S * 1000) continue;

        print("W:", LA_F("'%s' timeout (inactive for %.1f sec)\n", LA_F264, 264), 
               c->base.local_peer_id, tick_diff(now, c->base.last_active) / 1000.0);
        
        relay_clear_client(c);
    }
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "COMPACT"

// 发送 REGISTER_ACK: [hdr(4)][status(1)][session_id(8)][instance_id(4)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 26字节
// status: SIG_REGACK_PEER_OFFLINE(0) / SIG_REGACK_PEER_ONLINE(1) / 2(error)
static void send_register_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, uint8_t status, uint64_t session_id, uint32_t instance_id) {
    const char* PROTO = "REGISTER_ACK";

    uint8_t ack[26];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_REGISTER_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    if (status <= 1) {
        if (ARGS_relay.i64)    hdr->flags |= SIG_REGACK_FLAG_RELAY;
        if (ARGS_msg.i64)      hdr->flags |= SIG_REGACK_FLAG_MSG;
        ack[4] = status;
        nwrite_ll(ack + 5, session_id);
        nwrite_l(ack + 13, instance_id);
        ack[17] = MAX_CANDIDATES;
        memcpy(ack + 18, &to->sin_addr.s_addr, 4);
        memcpy(ack + 22, &to->sin_port, 2);
        uint16_t probe = htons((uint16_t)ARGS_probe_port.i64);
        memcpy(ack + 24, &probe, 2);

        print("V:", LA_F("Send %s: status=%s, max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, ses_id=%" PRIu64 ", inst_id=%u\n", LA_F75, 75),
              PROTO, status ? "peer_online" : "peer_offline", MAX_CANDIDATES,
              ARGS_relay.i64 ? "yes" : "no", ARGS_msg.i64 ? "yes" : "no",
              inet_ntoa(to->sin_addr), ntohs(to->sin_port), 
              (int)ARGS_probe_port.i64, session_id, instance_id);
    } else {
        
        ack[4] = status;
        memset(ack + 5, 0, 21);
        print("V:", LA_F("Send %s: status=error (no slot available)\n", LA_F76, 76), PROTO);
    }

    ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (const struct sockaddr *)to, sizeof(*to));
    if (n != sizeof(ack))
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F117, 117), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n", LA_F182, 182), PROTO, to_str, hdr->flags, (int)n);     
}

// 发送 PEER_OFF 通知: [hdr(4)][session_id(8)] = 12字节
// 并标记对端已断开 (peer->peer = -1)
static void send_peer_off(sock_t udp_fd, compact_pair_t *peer, const char *reason) {
    const char* PROTO = "PEER_OFF";

    uint8_t pkt[4 + 8];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_PEER_OFF; hdr->flags = 0; hdr->seq = htons(0);
    nwrite_ll(pkt + 4, peer->session_id);

    print("V:", LA_F("Send %s: peer='%s', reason=%s, ses_id=%" PRIu64 "\n", LA_F69, 69),
          PROTO, peer->local_peer_id, reason, peer->session_id);

    ssize_t n = sendto(udp_fd, (const char *)pkt, sizeof(pkt), 0,
           (struct sockaddr *)&peer->addr, sizeof(peer->addr));
    if (n != (ssize_t)sizeof(pkt))
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F121, 121),
              PROTO, inet_ntoa(peer->addr.sin_addr), ntohs(peer->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F122, 122),
               PROTO, inet_ntoa(peer->addr.sin_addr), ntohs(peer->addr.sin_port), (int)n);

    peer->peer = (compact_pair_t*)(void*)-1;
}

// 发送 PEER_INFO(seq=0)（base_index=0 首包；base_index!=0 地址变更通知）
static void send_peer_info_seq0(sock_t udp_fd, compact_pair_t *pair, uint8_t base_index) {
    const char* PROTO = "PEER_INFO";

    assert(pair && PEER_ONLINE(pair));

    uint8_t pkt[4 + P2P_PEER_ID_MAX + 2 + MAX_CANDIDATES * sizeof(p2p_candidate_t)];  // 包头 + session_id + base_index + candidates
    p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)pkt;
    resp_hdr->type = SIG_PKT_PEER_INFO;
    resp_hdr->flags = 0;
    resp_hdr->seq = htons(0);

    nwrite_ll(pkt + 4, pair->session_id);
    pkt[12] = base_index;

    int resp_len = 14;
    int cand_cnt = 0;
    if (base_index == 0) {

        // 首包：候选数量 = 对端公网地址(1) + 对端注册的候选列表(candidate_count)
        cand_cnt = 1 + pair->peer->candidate_count;
        pkt[13] = (uint8_t)cand_cnt;
        
        // 第一个候选：对端的公网地址（服务器观察到的 UDP 源地址）
        p2p_candidate_t wire_cand;
        wire_cand.type = 1; // srflx
        sockaddr_to_p2p_wire(&pair->peer->addr, &wire_cand.addr);
        wire_cand.priority = 0;
        memcpy(pkt + resp_len, &wire_cand, sizeof(p2p_candidate_t));
        resp_len += sizeof(p2p_candidate_t);
        
        // 后续候选：对端注册时提供的候选列表（已是网络格式，直接复制）
        for (int i = 0; i < pair->peer->candidate_count; i++) {
            memcpy(pkt + resp_len, &pair->peer->candidates[i], sizeof(p2p_candidate_t));
            resp_len += sizeof(p2p_candidate_t);
        }
    }
    else {

        // 地址变更通知：仅发送 1 个公网候选地址（来自对端当前 UDP 源地址）
        cand_cnt = 1;
        pkt[13] = 1;
        p2p_candidate_t wire_cand2;
        wire_cand2.type = 1; // srflx
        sockaddr_to_p2p_wire(&pair->peer->addr, &wire_cand2.addr);
        wire_cand2.priority = 0;
        memcpy(pkt + resp_len, &wire_cand2, sizeof(p2p_candidate_t));
        resp_len += sizeof(p2p_candidate_t);
    }

    print("V:", LA_F("Send %s: base_index=%u, cands=%d, ses_id=%" PRIu64 ", peer='%s'\n", LA_F67, 67),
          PROTO, base_index, cand_cnt, pair->session_id, pair->local_peer_id);

    ssize_t n = sendto(udp_fd, (const char *)pkt, resp_len, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));
    if (n != resp_len)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F121, 121),
              PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F122, 122),
               PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port), (int)n);
}

// 发送 MSG_REQ_ACK: [hdr(4)][session_id(8)][sid(2)][status(1)] = 15字节
// status: 0=成功, 1=失败
static void send_msg_req_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, 
                             uint64_t session_id, uint16_t sid, uint8_t status) {
    const char* PROTO = "MSG_REQ_ACK";

    uint8_t ack[4 + 8 + 2 + 1];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_MSG_REQ_ACK;
    hdr->flags = 0;
    hdr->seq = 0;
    nwrite_ll(ack + 4, session_id);
    nwrite_s(ack + 12, sid);
    ack[14] = status;

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, status=%u\n", LA_F74, 74),
          PROTO, session_id, sid, status);

    ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (const struct sockaddr *)to, sizeof(*to));
    if (n != (ssize_t)sizeof(ack))
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F117, 117), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F181, 181), PROTO, to_str, (int)n);
}

// 发送 MSG_REQ 给对端（Server→对端 relay）
// 协议格式: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=SIG_MSG_FLAG_RELAY
// msg=消息类型，msg=0 时对端自动 echo
static void send_msg_req_to_peer(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_REQ";

    assert(pair && PEER_ONLINE(pair));
    assert(pair->rpc_pending_next && !pair->rpc_responding);

    uint8_t pkt[4 + 8 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_REQ;
    hdr->flags = SIG_MSG_FLAG_RELAY;  // 标识这是 Server→对端 的 relay 包
    hdr->seq = 0;

    int n = 4;
    nwrite_ll(pkt + n, pair->peer->session_id); n += 8;  // 对端的 session_id
    nwrite_s(pkt + n, pair->rpc_last_sid); n += 2;
    pkt[n++] = pair->rpc_code;
    if (pair->rpc_data_len > 0) {
        memcpy(pkt + n, pair->rpc_data, pair->rpc_data_len);
        n += pair->rpc_data_len;
    }

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, msg=%u, data_len=%d, peer='%s'\n", LA_F71, 71),
          PROTO, pair->peer->session_id, pair->rpc_last_sid, pair->rpc_code, pair->rpc_data_len,
          pair->peer->local_peer_id);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
    if (sent != n)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F121, 121),
              PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n", LA_F123, 123),
               PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port),
               hdr->flags, (int)sent, pair->rpc_retry);
}

// 发送 MSG_RESP_ACK 给 B 端（Server→B）
// 协议格式: [session_id(8)][sid(2)]
// 说明: 确认收到 B 端的 MSG_RESP，让 B 停止重发
static void send_msg_resp_ack_to_responder(sock_t udp_fd, const struct sockaddr_in *addr, 
                                           const char *peer_id, uint64_t session_id, uint16_t sid) {
    const char* PROTO = "MSG_RESP_ACK";

    uint8_t pkt[4 + 8 + 2];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    nwrite_ll(pkt + 4, session_id);
    nwrite_s(pkt + 12, sid);

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, peer='%s'\n", LA_F73, 73),
          PROTO, session_id, sid, peer_id);

    ssize_t n = sendto(udp_fd, (const char *)pkt, sizeof(pkt), 0, (struct sockaddr *)addr, sizeof(*addr));
    if (n != (ssize_t)sizeof(pkt))
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F121, 121),
              PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F122, 122),
               PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), (int)n);
}

// 发送 MSG_RESP 给请求方（Server→A，从缓存的 rpc_* 字段构建）
// 协议格式: [session_id(8)][sid(2)][msg(1)][data(N)]（正常响应，msg=响应码）
//          [session_id(8)][sid(2)]（错误响应，flags 中标识错误类型）
// 说明: 包含 session_id 用于 A 端验证响应合法性
static void send_msg_resp_to_requester(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_RESP";

    assert(pair && pair->rpc_responding);

    uint8_t pkt[4 + 8 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP;
    hdr->flags = pair->rpc_flags;
    hdr->seq = 0;

    int n = 4;
    nwrite_ll(pkt + n, pair->session_id); n += 8;
    nwrite_s(pkt + n, pair->rpc_last_sid); n += 2;
    
    // 如果是正常响应，包含 msg(响应码) 和 data（复用 rpc_code 和 rpc_data 字段）
    if (!(pair->rpc_flags & (SIG_MSG_FLAG_PEER_OFFLINE | SIG_MSG_FLAG_TIMEOUT))) {
        pkt[n++] = pair->rpc_code;
        if (pair->rpc_data_len > 0) {
            memcpy(pkt + n, pair->rpc_data, pair->rpc_data_len);
            n += pair->rpc_data_len;
        }
    }

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n", LA_F72, 72),
          PROTO, pair->session_id, pair->rpc_last_sid, pair->local_peer_id, pair->rpc_flags, pair->rpc_code, pair->rpc_data_len);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));
    if (sent != n)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F121, 121),
                PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n", LA_F123, 123),
                PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port),
                pair->rpc_flags, (int)sent, pair->rpc_retry);
}

//-----------------------------------------------------------------------------

// 从待确认链表移除
static void remove_info0_pending(compact_pair_t *pair) {

    if (!g_compact_info0_pending_head || !pair->info0_pending_next) return;

    // 如果是头节点
    if (g_compact_info0_pending_head == pair) {
        g_compact_info0_pending_head = pair->info0_pending_next;
        pair->info0_pending_next = NULL;
        if (g_compact_info0_pending_head == (void*)-1) {
            g_compact_info0_pending_head = NULL;
            g_compact_info0_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_compact_info0_pending_head;
    while (prev->info0_pending_next != pair) {
        if (prev->info0_pending_next == (void*)-1) return;  // 没有找到
        prev = prev->info0_pending_next;
    }

    prev->info0_pending_next = pair->info0_pending_next;
    
    // 如果移除的是尾节点，更新尾指针
    if (pair->info0_pending_next == (void*)-1) {
        g_compact_info0_pending_rear = prev;
    }
    
    pair->info0_pending_next = NULL;
}

// 将配对加入 PEER_INFO(seq=0) 待确认链表
static void enqueue_info0_pending(compact_pair_t *pair, uint8_t base_index, uint64_t now) {

    if (pair->info0_pending_next) {
        remove_info0_pending(pair);
    }

    pair->info0_base_index = base_index;
    pair->info0_retry = 0;
    pair->info0_sent_time = now;

    pair->info0_pending_next = (compact_pair_t*)(void*)-1;
    if (g_compact_info0_pending_rear) {
        g_compact_info0_pending_rear->info0_pending_next = pair;
        g_compact_info0_pending_rear = pair;
    } else {
        g_compact_info0_pending_head = pair;
        g_compact_info0_pending_rear = pair;
    }
}

// 检查并重传未确认的 PEER_INFO 包
static void retry_info0_pending(sock_t udp_fd, uint64_t now) {

    if (!g_compact_info0_pending_head) return;

    for(;;) {

        // 队列按时间排序，一旦遇到未超时的节点，后面都不会超时
        if (tick_diff(now, g_compact_info0_pending_head->info0_sent_time) < PEER_INFO0_RETRY_INTERVAL_MS) {
            return;
        }

        // 将第一项移除
        compact_pair_t *q = g_compact_info0_pending_head;
        g_compact_info0_pending_head = q->info0_pending_next;

        // 检查是否超过最大重传次数
        if (q->info0_retry >= PEER_INFO0_MAX_RETRY) {
            
            // 超过最大重传次数，从链表移除（放弃）
            print("W:", LA_F("PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n", LA_F63, 63),
                   q->local_peer_id, q->remote_peer_id, q->info0_retry);

            q->info0_pending_next = NULL;
            if (q->info0_base_index == 0) {
                q->info0_acked = -1;  // 首包失败：标记为已放弃
            }

            // 如果这是最后一项
            if (g_compact_info0_pending_head == (void*)-1) {
                g_compact_info0_pending_head = NULL;
                g_compact_info0_pending_rear = NULL;
                return;
            }
        }
        // 重传
        else {
            // 检查对端是否仍在线（可能在 pending 期间断开）
            if (!PEER_ONLINE(q)) {
                // 对端已断开，从链表移除
                q->info0_pending_next = NULL;
                if (g_compact_info0_pending_head == (void*)-1) {
                    g_compact_info0_pending_head = NULL;
                    g_compact_info0_pending_rear = NULL;
                    return;
                }
                continue;  // 处理下一个
            }

            send_peer_info_seq0(udp_fd, q, q->info0_base_index);

            // 更新时间和重传次数
            q->info0_retry++;
            q->info0_sent_time = now;

            // 如果这是最后一项
            if (g_compact_info0_pending_head == (void*)-1) {
                g_compact_info0_pending_head = q;
            }
            // 重新加到队尾（因为时间更新了，按时间排序）
            else {
                q->info0_pending_next = (compact_pair_t*)(void*)-1;
                g_compact_info0_pending_rear->info0_pending_next = q;
                g_compact_info0_pending_rear = q;
            }

            print("V:", LA_F("PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%" PRIu64 ")\n", LA_F62, 62),
                   q->local_peer_id, q->remote_peer_id,
                   q->info0_retry, PEER_INFO0_MAX_RETRY, q->session_id);

            if (g_compact_info0_pending_head == q) return;
        }
    }
}

//-----------------------------------------------------------------------------
// MSG RPC 链表管理和处理函数

// 缓存响应数据到 pair->resp_* 并从 REQ 链表移到 RESP 链表
static void transition_to_resp_pending(sock_t udp_fd, compact_pair_t *requester, uint64_t now,
                                       uint8_t flags, uint8_t code, const uint8_t *data, int len);

// 从 RPC 待确认链表移除
static void remove_rpc_pending(compact_pair_t *pair) {

    if (!g_rpc_pending_head || !pair->rpc_pending_next) return;

    if (g_rpc_pending_head == pair) {
        g_rpc_pending_head = pair->rpc_pending_next;
        pair->rpc_pending_next = NULL;
        if (g_rpc_pending_head == (void*)-1) {
            g_rpc_pending_head = NULL;
            g_rpc_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_rpc_pending_head;
    while (prev->rpc_pending_next != pair) {
        if (prev->rpc_pending_next == (void*)-1) return;
        prev = prev->rpc_pending_next;
    }

    prev->rpc_pending_next = pair->rpc_pending_next;
    if (pair->rpc_pending_next == (void*)-1) {
        g_rpc_pending_rear = prev;
    }
    pair->rpc_pending_next = NULL;
}

// 将配对加入 RPC 待确认链表
static inline void enqueue_rpc_pending(compact_pair_t *pair) {

    pair->rpc_pending_next = (compact_pair_t*)(void*)-1;
    if (g_rpc_pending_rear) {
        g_rpc_pending_rear->rpc_pending_next = pair;
        g_rpc_pending_rear = pair;
    } else {
        g_rpc_pending_head = pair;
        g_rpc_pending_rear = pair;
    }
}

// 检查并重传 RPC（统一处理 REQ 和 RESP 阶段的超时重传）
// 队列按发送时间排序（出队-重传-入队），未超时即短路返回
static void retry_rpc_pending(sock_t udp_fd, uint64_t now) {

    if (!g_rpc_pending_head) return;

    for (;;) {

        // 队列按时间排序，一旦遇到未超时的节点，后面都不会超时
        if (tick_diff(now, g_rpc_pending_head->rpc_sent_time) < MSG_RPC_RETRY_INTERVAL_MS) {
            return;
        }

        // 将第一项从队列头部移除
        compact_pair_t *q = g_rpc_pending_head;
        g_rpc_pending_head = q->rpc_pending_next;
        if (g_rpc_pending_head == (void*)-1) {
            g_rpc_pending_head = NULL;
            g_rpc_pending_rear = NULL;
        }

        // REQ 阶段：等待对端响应
        if (!q->rpc_responding) {

            // 检查对端是否离线
            if (!PEER_ONLINE(q)) {
                print("W:", LA_F("MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%" PRIu64 ")\n", LA_F54, 54),
                      q->local_peer_id, q->rpc_last_sid, q->session_id);

                transition_to_resp_pending(udp_fd, q, now, SIG_MSG_FLAG_PEER_OFFLINE, 0, NULL, 0);
            }
            // 超过最大重传次数，发送超时错误
            else if (q->rpc_retry >= MSG_REQ_MAX_RETRY) {
                print("W:", LA_F("MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%" PRIu64 ")\n", LA_F53, 53),
                      q->rpc_retry, q->local_peer_id, q->rpc_last_sid, q->session_id);

                transition_to_resp_pending(udp_fd, q, now, SIG_MSG_FLAG_TIMEOUT, 0, NULL, 0);
            }
            // 重传 MSG_REQ 给对端
            else {
                send_msg_req_to_peer(udp_fd, q);
                q->rpc_retry++;
                q->rpc_sent_time = now;

                // 重新加入队尾（保持时间排序）
                enqueue_rpc_pending(q);

                print("V:", LA_F("MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%" PRIu64 ")\n", LA_F55, 55),
                      q->local_peer_id, q->peer->local_peer_id,
                      q->rpc_last_sid, q->rpc_retry, MSG_REQ_MAX_RETRY, q->session_id);

                if (g_rpc_pending_head == q) return;
            }
        }
        // RESP 阶段：等待请求方确认
        else {

            // 超过最大重传次数，放弃
            if (q->rpc_retry >= MSG_RESP_MAX_RETRY) {
                print("W:", LA_F("MSG_RESP gave up after %d retries, sid=%u (ses_id=%" PRIu64 ")\n", LA_F56, 56),
                      q->rpc_retry, q->rpc_last_sid, q->session_id);

                q->rpc_pending_next = NULL;
                q->rpc_responding = false;
                q->rpc_retry = 0;
            }
            // 从缓存重传 MSG_RESP
            else {
                q->rpc_retry++;
                send_msg_resp_to_requester(udp_fd, q);
                q->rpc_sent_time = now;

                // 重新加入队尾（保持时间排序）
                enqueue_rpc_pending(q);

                print("V:", LA_F("MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%" PRIu64 ")\n", LA_F57, 57),
                      q->local_peer_id, q->rpc_last_sid, q->rpc_retry, MSG_RESP_MAX_RETRY, q->session_id);

                if (g_rpc_pending_head == q) return;
            }
        }

        // 队列已空
        if (!g_rpc_pending_head) return;
    }
}


// 缓存响应数据到 pair->rpc_* 并从 REQ 阶段转换到 RESP 阶段
static void transition_to_resp_pending(sock_t udp_fd, compact_pair_t *requester, uint64_t now,
                                       uint8_t flags, uint8_t code, const uint8_t *data, int len) {

    // 状态转换为 RESP：直接复用 rpc_code 和 rpc_data 字段存储响应
    requester->rpc_responding = true;
    // rpc_last_sid 保持不变（RESP 的 sid 等于对应 REQ 的 sid）
    requester->rpc_flags = flags;
    
    // 响应的 code 和 data 直接保存到 rpc_code 和 rpc_data（复用）
    requester->rpc_code = code;
    requester->rpc_data_len = 0;
    if (len > 0 && data) {
        memcpy(requester->rpc_data, data, len);
        requester->rpc_data_len = len;
    }

    // 发送并加入 RPC 链表（RESP 阶段）
    requester->rpc_sent_time = now;
    requester->rpc_retry = 0;
    enqueue_rpc_pending(requester);
    send_msg_resp_to_requester(udp_fd, requester);
}

//-----------------------------------------------------------------------------

// 检测地址变更并通知对端（任何包都可能触发 NAT 重绑定检测）
// 返回 true 表示地址发生了变化
static bool check_addr_change(sock_t udp_fd, compact_pair_t *pair, const struct sockaddr_in *from) {

    if (memcmp(&pair->addr, from, sizeof(*from)) == 0) return false;

    // 更新为最新地址
    pair->addr = *from;

    // 如果对方在线，通知对方
    if (PEER_ONLINE(pair)) {

        // 首包已成功确认（info0_acked > 0），立即发送地址变更通知
        if (pair->peer->info0_acked > 0) {

            pair->peer->addr_notify_seq = (uint8_t)(pair->peer->addr_notify_seq + 1);
            if (pair->peer->addr_notify_seq == 0) pair->peer->addr_notify_seq = 1;

            send_peer_info_seq0(udp_fd, pair->peer, pair->peer->addr_notify_seq);
            enqueue_info0_pending(pair->peer, pair->peer->addr_notify_seq, P_tick_ms());

            print("I:", LA_F("Addr changed for '%s', notifying '%s' (ses_id=%" PRIu64 ")\n", LA_F48, 48),
                  pair->local_peer_id, pair->peer->local_peer_id, pair->peer->session_id);
        }
        // 首包未确认（info0_acked == 0）或已放弃（info0_acked < 0）
        else if (pair->peer->info0_acked == 0) {
            // 正在同步中，延期发送地址变更通知（等 ACK 后再发）
            // 设置 addr_notify_seq = 1 标记有延期通知
            if (pair->peer->addr_notify_seq == 0) pair->peer->addr_notify_seq = 1;

            print("I:", LA_F("Addr changed for '%s', defer notification until first ACK (ses_id=%" PRIu64 ")\n", LA_F46, 46),
                  pair->local_peer_id, pair->peer->session_id);
        }
        // info0_acked < 0：首包已放弃，不再发送任何通知
        else print("W:", LA_F("Addr changed for '%s', but first info packet was abandoned (ses_id=%" PRIu64 ")\n", LA_F45, 45),
                   pair->local_peer_id, pair->peer->session_id);
    }

    return true;
}

// 处理 COMPACT 模式信令（UDP 无状态，对应 p2p_signal_compact 模块）
static void handle_compact_signaling(sock_t udp_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {
    
    if (len < 4) return;  // 至少需要包头
    
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4; size_t payload_len = len - 4;
    
    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // SIG_PKT_REGISTER: [local_peer_id(32)][remote_peer_id(32)][instance_id(4)][candidate_count(1)][candidates(N*7)]
    switch (hdr->type) {
    case SIG_PKT_REGISTER: { const char* PROTO = "REGISTER";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len <= P2P_PEER_ID_MAX * 2 + 4) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        // 解析 instance_id
        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX * 2);
        if (instance_id == 0) {
            print("E:", LA_F("%s: invalid instance_id=0 from %s\n", LA_F31, 31), PROTO, from_str);
            return;
        }

        // 直接以 local_peer_id + remote_peer_id 作为 key 检索
        compact_pair_t *existing = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, existing);

        // 如果重复注册（客户端未收到 ACK 前的重传）
        if (existing && existing->instance_id == instance_id) {

            check_addr_change(udp_fd, existing, from);
            existing->last_active = P_tick_ms();

            send_register_ack(udp_fd, from, from_str, 
                              PEER_ONLINE(existing) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE,
                              existing->session_id, instance_id);
            return;
        }

        // 首次注册 或 instance_id 变更

        // payload 直接指向 peer_key：[local_peer_id(32)][remote_peer_id(32)]
        const char *local_peer_id = (const char *)payload;
        const char *remote_peer_id = (const char *)(payload + P2P_PEER_ID_MAX);

        // 解析候选列表
        int candidate_count = 0;
        p2p_candidate_t candidates[MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));
        size_t cand_offset = P2P_PEER_ID_MAX * 2 + 4;
        candidate_count = payload[cand_offset];
        if (candidate_count > MAX_CANDIDATES) {
            candidate_count = MAX_CANDIDATES;
        }
        cand_offset++;
        for (int i = 0; i < candidate_count && cand_offset + sizeof(p2p_candidate_t) <= payload_len; i++) {
            memcpy(&candidates[i], payload + cand_offset, sizeof(p2p_candidate_t));
            cand_offset += sizeof(p2p_candidate_t);
        }

        print("V:", LA_F("%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n", LA_F23, 23),
               PROTO, P2P_PEER_ID_MAX, local_peer_id, P2P_PEER_ID_MAX, remote_peer_id, instance_id, candidate_count);

        // 如果配对不存在（首次注册），分配一个空位
        int i;
        if (existing) i = (int)(existing - g_compact_pairs);
        else {
            for (i = 0; i < MAX_PEERS; i++) {
                if (!g_compact_pairs[i].valid) { 
                    g_compact_pairs[i].valid = true;
                    
                    memcpy(g_compact_pairs[i].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
                    memcpy(g_compact_pairs[i].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
                    g_compact_pairs[i].instance_id = instance_id;
                    g_compact_pairs[i].peer = NULL;

                    // 添加到 peer_key/session 索引
                    HASH_ADD(hh_peer, g_pairs_by_peer, local_peer_id, P2P_PEER_ID_MAX * 2, &g_compact_pairs[i]);

                    g_compact_pairs[i].info0_pending_next = NULL;
                    g_compact_pairs[i].rpc_pending_next = NULL;

                    break;
                }
            }
            // 无法分配槽位，发送错误 ACK
            if (i == MAX_PEERS) {
                send_register_ack(udp_fd, from, from_str, 2, 0, instance_id);
                return;
            }
        }
        compact_pair_t *local = &g_compact_pairs[i];

        // 如果是 instance_id 变更（客户端重启），必须重置旧会话
        if (existing) { assert(local->instance_id != instance_id);

            print("I:", LA_F("%s from '%.*s': new instance(old=%u new=%u), resetting session\n", LA_F18, 18),
                   PROTO, P2P_PEER_ID_MAX, local_peer_id, local->instance_id, instance_id);

            // 通知对端下线（如果对端在线且有 session_id）
            if (PEER_ONLINE(local)) send_peer_off(udp_fd, local->peer, "reregister");

            // 从待确认链表移除
            if (local->info0_pending_next) remove_info0_pending(local);

            // 清理 RPC pending 状态（可能有旧会话的 RPC 正在重传）
            if (local->rpc_pending_next) remove_rpc_pending(local);

            // 从 session_id 索引移除
            if (local->session_id != 0) HASH_DELETE(hh, g_pairs_by_session, local);
        }
        local->addr = *from;
        local->instance_id = instance_id;
        local->peer = NULL;

        // 分配 session id
        local->session_id = generate_compact_pair_id();
        HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), local);

        // 重置 session 数据
        local->addr_notify_seq = 0;
        local->info0_acked = 0;
        local->rpc_last_sid = 0;

        // 记录本端的候选列表
        local->candidate_count = candidate_count;
        if (candidate_count) {
            memcpy(local->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count);
        }

        // 查找反向配对：构造反向 peer_key [remote_peer_id][local_peer_id]
        char reverse_key[P2P_PEER_ID_MAX * 2];
        memcpy(reverse_key, payload + P2P_PEER_ID_MAX, P2P_PEER_ID_MAX);
        memcpy(reverse_key + P2P_PEER_ID_MAX, payload, P2P_PEER_ID_MAX);
        compact_pair_t* remote = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, reverse_key, P2P_PEER_ID_MAX * 2, remote);
        int remote_idx = remote ? (int)(remote - g_compact_pairs) : -1;

        // 如果之前是（自己之前是已连接过的）对端断开连接，重新连接时重置对端状态
        if (local->peer == (compact_pair_t*)(void*)-1) local->peer = NULL;

        local->last_active = P_tick_ms();

        // 发送 REGISTER_ACK（无需确认重发，客户端会在收到 ACK 前一直重试注册请求）
        send_register_ack(udp_fd, from, from_str, 
                          (remote_idx >= 0) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE,
                          local->session_id, instance_id);

        if (remote_idx >= 0) {

            remote = &g_compact_pairs[remote_idx];

            // 对端之前的连接已断开（peer == -1），需重置其 session 后重新 pairing
            // + 对端客户端收到不同 session_id 的 PEER_INFO 0 应该自动重置状态
            if (remote->peer == (compact_pair_t*)-1) {

                print("I:", LA_F("%s: reset '%.*s'(disconnected) session for re-pairing\n", LA_F42, 42),
                       PROTO, P2P_PEER_ID_MAX, remote->local_peer_id);

                // 清理 remote 的 pending 状态
                if (remote->info0_pending_next) remove_info0_pending(remote);
                if (remote->rpc_pending_next) remove_rpc_pending(remote);

                // 重置 remote 的 session id
                HASH_DELETE(hh, g_pairs_by_session, remote);
                remote->session_id = generate_compact_pair_id();
                HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), remote);

                // 重置 session 数据
                remote->addr_notify_seq = 0;
                remote->info0_acked = 0;
                remote->rpc_last_sid = 0;

                remote->peer = NULL;
            }

            // 首次匹配成功：双方都准备好（peer == NULL）
            if (local->peer == NULL && remote->peer == NULL) {

                // 建立双向关联
                local->peer = remote; remote->peer = local;

                // session_id 已在 REGISTER 阶段分配
                assert(local->session_id != 0 && remote->session_id != 0);

                // 向双方发送服务器维护的首个 PEER_INFO(seq=0, base_index=0)
                send_peer_info_seq0(udp_fd, local, 0);
                enqueue_info0_pending(local, 0, local->last_active);

                send_peer_info_seq0(udp_fd, remote, 0);
                enqueue_info0_pending(remote, 0, local->last_active);

                print("I:", LA_F("Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n", LA_F64, 64),
                       P2P_PEER_ID_MAX, local_peer_id, remote->candidate_count,
                       P2P_PEER_ID_MAX, remote_peer_id, local->candidate_count);
            }
            // 已建立 pairing，地址变更通知已在 check_addr_change() 中统一处理
            else assert(local->peer == remote && remote->peer == local);

        } else print("V:", LA_F("%s: waiting for peer '%.*s' to register\n", LA_F44, 44), PROTO, P2P_PEER_ID_MAX, remote_peer_id);
    } break;

    // SIG_PKT_UNREGISTER: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端主动断开时发送，请求服务器立即释放配对槽位。
    // 【服务端可选实现】如果不处理此包类型，客户端自动降级为 COMPACT_PAIR_TIMEOUT_S 超时清除机制。
    case SIG_PKT_UNREGISTER: { const char* PROTO = "UNREGISTER";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < P2P_PEER_ID_MAX * 2) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        // 直接用 hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, pair);
        
        if (pair && pair->valid) {

            char local_peer_id[P2P_PEER_ID_MAX + 1] = {0};
            char remote_peer_id[P2P_PEER_ID_MAX + 1] = {0};
            memcpy(local_peer_id, pair->local_peer_id, P2P_PEER_ID_MAX);
            memcpy(remote_peer_id, pair->remote_peer_id, P2P_PEER_ID_MAX);

            print("V:", LA_F("%s: accepted, releasing slot for '%s' -> '%s'\n", LA_F24, 24),
                   PROTO, local_peer_id, remote_peer_id);

            // 向对端发送 PEER_OFF 通知
            if (PEER_ONLINE(pair)) {
                send_peer_off(udp_fd, pair->peer, "unregister");
            }
            
            // 从待确认链表移除
            if (pair->info0_pending_next) remove_info0_pending(pair);

            // 从 MSG RPC 链表移除
            if (pair->rpc_pending_next) remove_rpc_pending(pair);

            // 从哈希表删除
            HASH_DELETE(hh, g_pairs_by_session, pair);
            HASH_DELETE(hh_peer, g_pairs_by_peer, pair);

            pair->valid = false;
        }
    } break;

    // SIG_PKT_ALIVE: [session_id(8)]
    // 客户端定期发送以保持槽位活跃，更新 last_active 时间
    case SIG_PKT_ALIVE: { const char* PROTO = "ALIVE";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 8) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);

        // 直接用 session_id hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);        
        if (pair && pair->valid) {

            print("V:", LA_F("%s accepted, peer='%s', ses_id=%" PRIu64 "\n", LA_F13, 13),
                  PROTO, pair->local_peer_id, session_id);

            pair->last_active = P_tick_ms();
            check_addr_change(udp_fd, pair, from);

            // 回复 ACK
            {   const char* ACK_PROTO = "ALIVE_ACK";

                uint8_t ack[4];
                p2p_pkt_hdr_encode(ack, SIG_PKT_ALIVE_ACK, 0, 0);

                print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", peer='%s'\n", LA_F70, 70),
                      ACK_PROTO, session_id, pair->local_peer_id);

                ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (struct sockaddr *)from, sizeof(*from));
                if (n != (ssize_t)sizeof(ack))
                    print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F117, 117), ACK_PROTO, from_str, P_sock_errno());
                else
                    printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F181, 181), ACK_PROTO, from_str, (int)n);
            }
        }
    } break;

    // SIG_PKT_PEER_INFO_ACK: ACK 确认收到 PEER_INFO 包
    // 格式: [hdr(4)][session_id(8)]，确认序号使用 hdr->seq
    case SIG_PKT_PEER_INFO_ACK: { const char* PROTO = "PEER_INFO_ACK";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 8) {  // session_id(8)
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);
        uint16_t ack_seq = ntohs(hdr->seq);
        if (ack_seq > 16) {
            print("E:", LA_F("%s: invalid seq=%u\n", LA_F33, 33), PROTO, ack_seq);
            return;
        }

        // 通过 session_id 查找对应的配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);

        print("V:", LA_F("%s accepted, seq=%u, ses_id=%" PRIu64 "\n", LA_F14, 14),
              PROTO, ack_seq, session_id);

        // ack_seq=0 的 ACK 是对服务器发送的首个 PEER_INFO 的确认，服务器需要处理
        if (ack_seq == 0) {

            if (pair && pair->valid) {
                check_addr_change(udp_fd, pair, from);

                // 标记为已确认，从待确认链表移除
                bool is_first_ack = (!pair->info0_acked && pair->info0_base_index == 0);
                if (is_first_ack) {
                    pair->info0_acked = 1;

                    print("V:", LA_F("%s: confirmed '%s', retries=%d (ses_id=%" PRIu64 ")\n", LA_F29, 29),
                           PROTO, pair->local_peer_id, pair->info0_retry, session_id);
                }

                // 从待确认链表移除（首包和地址变更通知都走 seq=0 ACK）
                if (pair->info0_pending_next) {
                    remove_info0_pending(pair);
                }

                pair->info0_base_index = 0;
                pair->info0_retry = 0;
                pair->info0_sent_time = 0;

                // 如果首包 ACK 且有延期的地址变更通知（addr_notify_seq != 0），则立即发送地址变更通知
                if (is_first_ack && pair->addr_notify_seq != 0) {

                    send_peer_info_seq0(udp_fd, pair, pair->addr_notify_seq);
                    enqueue_info0_pending(pair, pair->addr_notify_seq, P_tick_ms());

                    print("I:", LA_F("Addr changed for '%s', deferred notifying '%s' (ses_id=%" PRIu64 ")\n", LA_F47, 47),
                          pair->peer->local_peer_id, pair->local_peer_id, pair->peer->session_id);
                }
            }
            else print("W:", LA_F("%s for unknown ses_id=%" PRIu64 "\n", LA_F15, 15), PROTO, session_id);
        }
        // ack_seq≠0 的 ACK 是客户端之间的确认，服务器只负责 relay 转发
        else {
            
            if (pair && pair->valid && PEER_ONLINE(pair)) {
                check_addr_change(udp_fd, pair, from);

                // 转发给对方：将 payload 中的 session_id 改写为对端的 session_id
                nwrite_ll((uint8_t *)payload, pair->peer->session_id);

                sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                       (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                print("V:", LA_F("Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", LA_F65, 65),
                       PROTO, ack_seq, pair->local_peer_id, pair->remote_peer_id, session_id);
            }
            else print("W:", LA_F("Cannot relay %s: ses_id=%" PRIu64 " (peer unavailable)\n", LA_F49, 49), PROTO, session_id);
        }

    } break;

    // SIG_PKT_PEER_INFO/P2P_PKT_REACH/P2P_PKT_DATA/P2P_PKT_ACK/P2P_PKT_CRYPTO (/RELAYED): relay 转发给对方
    // 格式：所有包都包含 session_id(8) 在 payload 开头
    case P2P_PKT_DATA:
    case P2P_PKT_ACK:
    case P2P_PKT_CRYPTO:
    case P2P_PKT_CONN:
    case P2P_PKT_CONN_ACK:
    case P2P_PKT_REACH:
        if (!(hdr->flags & P2P_DATA_FLAG_SESSION)) {
            print("E:", LA_F("[Relay] %s: missing SESSION flag, dropped\n", LA_F86, 86),
                  (hdr->type == SIG_PKT_PEER_INFO) ? "PEER_INFO" :
                  (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                  (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                  (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                  (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                  (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH");
            return;
        }
    case SIG_PKT_PEER_INFO: {
        const char* PROTO = (hdr->type == SIG_PKT_PEER_INFO) ? "PEER_INFO" :
                           (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                           (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                           (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                           (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                           (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        // 所有需要 relay 的包格式都是 [session_id(8)][...]
        if (payload_len < 8) {  // session_id 8 bytes
            print("E:", LA_F("[Relay] %s: bad payload(len=%zu)\n", LA_F85, 85), PROTO, payload_len);
            return;
        }

        // SIG_PKT_PEER_INFO 特殊处理：seq=0 是服务器维护的包，不应该出现在这里
        if (hdr->type == SIG_PKT_PEER_INFO && hdr->seq == 0) {
            print("E:", LA_F("[Relay] %s seq=0 from client %s (server-only, dropped)\n", LA_F83, 83), PROTO, from_str);
            return;
        }

        uint64_t session_id = nget_ll(payload);

        // 根据 session_id 查找配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
        if (!pair || !pair->valid) {
            print("W:", LA_F("[Relay] %s for unknown ses_id=%" PRIu64 " (dropped)\n", LA_F81, 81), PROTO, session_id);
            return;
        }

        // 对方不存在，丢弃
        if (!PEER_ONLINE(pair)) {
            print("W:", LA_F("[Relay] %s for ses_id=%" PRIu64 ": peer unavailable (dropped)\n", LA_F80, 80), PROTO, session_id);
            return;
        }

        check_addr_change(udp_fd, pair, from);

        print("V:", LA_F("%s accepted, '%s' -> '%s', ses_id=%" PRIu64 "\n", LA_F12, 12),
              PROTO, pair->local_peer_id, pair->remote_peer_id, session_id);

        // 转发给对方：将 payload 中的 session_id 改写为对端的 session_id
        nwrite_ll((uint8_t *)payload, pair->peer->session_id);

        sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
               (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

        if (hdr->type == SIG_PKT_PEER_INFO || hdr->type == P2P_PKT_REACH || 
            hdr->type == P2P_PKT_DATA || hdr->type == P2P_PKT_CRYPTO) {
            print("V:", LA_F("[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", LA_F82, 82),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else {
            print("V:", LA_F("[Relay] %s: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", LA_F84, 84),
                   PROTO, pair->local_peer_id, pair->remote_peer_id, session_id);
        }
    } break;

    // SIG_PKT_MSG_REQ: A→Server 或 B→Server（通过 flags 区分）
    // A→Server: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=0，msg=消息类型
    // B→Server（不会有这种情况，B 只发 MSG_RESP）
    case SIG_PKT_MSG_REQ: { const char* PROTO = "MSG_REQ";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        // MSG_REQ 只能是 A→Server（flags=0）
        if (hdr->flags & SIG_MSG_FLAG_RELAY) {
            print("E:", LA_F("%s: invalid relay flag from client\n", LA_F32, 32), PROTO);
            return;
        }

        int msg_data_len = (int)(payload_len - 11);
        if (msg_data_len > P2P_MSG_DATA_MAX) {
            print("E:", LA_F("%s: data too large (len=%d)\n", LA_F30, 30), PROTO, msg_data_len);
            return;
        }

        // 解析验证 session_id 和 sid 不为 0
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);

        // 验证 session_id 和 sid 不为 0
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu64 " or sid=%u\n", LA_F34, 34), 
                   PROTO, session_id, sid);
            return;
        }

        // 解析 msg、data
        uint8_t msg = payload[10];
        const uint8_t *msg_data = payload + 11;

        // 查找 A 端的配对记录（通过 session_id 快速索引）
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);
        if (!requester || !requester->valid) {
            print("W:", LA_F("%s: requester not found for ses_id=%" PRIu64 "\n", LA_F41, 41), PROTO, session_id);
            return;
        }

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 ", sid=%u, msg=%u, len=%d\n", LA_F26, 26),
               PROTO, session_id, sid, msg, msg_data_len);

        // 先检查对端是否在线（如果对端下线，pending RPC 会在 UNREGISTER/超时时清理）
        if (!PEER_ONLINE(requester)) {

            print("W:", LA_F("%s: peer '%s' not online, rejecting sid=%u\n", LA_F40, 40),
                   PROTO, requester->remote_peer_id, sid);

            send_msg_req_ack(udp_fd, from, from_str, session_id, sid, 1);
            return;
        }

        // 统一进行地址变更检测（NAT 重绑定）
        check_addr_change(udp_fd, requester, from);

        // 检查是否有挂起的 RPC（REQ 或 RESP 阶段）
        if (requester->rpc_pending_next) {

            // 如果是重传同一个 sid（客户端未收到 ACK 前重传）→ 幂等处理
            if (sid == requester->rpc_last_sid) {

                // REQ 阶段：立即回复成功 ACK
                if (!requester->rpc_responding) {

                    send_msg_req_ack(udp_fd, from, from_str, requester->session_id, sid, 0);

                    print("V:", LA_F("%s retransmit, resend ACK, sid=%u (ses_id=%" PRIu64 ")\n", LA_F21, 21),
                          PROTO, sid, requester->session_id);
                }
                // RESP 阶段：客户端重传旧请求，忽略（等待 RESP_ACK）
                else {
                    print("V:", LA_F("%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%" PRIu64 ")\n", LA_F20, 20),
                          PROTO, sid, requester->session_id);
                }
                return;
            }

            // 如果 sid 过时
            if (!uint16_circle_newer(sid, requester->rpc_last_sid)) {
                print("V:", LA_F("%s: obsolete sid=%u (current=%u), ignoring\n", LA_F37, 37),
                      PROTO, sid, requester->rpc_last_sid);
                return;
            }

            // 如果是新的 sid（客户端中断旧请求发起新请求）→ 取消旧的，接受新请求
            print("I:", LA_F("%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%" PRIu64 ")\n", LA_F19, 19),
                  PROTO, sid, requester->rpc_last_sid, requester->rpc_responding, requester->session_id);
            
            // 清理旧的 RPC 状态（无论是 REQ 还是 RESP）
            remove_rpc_pending(requester);
            
            // 继续处理新请求（下面的代码）
        }

        // IDLE 状态下，验证新 sid 比最后完成的 rpc_last_sid 更新（rpc_last_sid=0 时无须检查）
        if (requester->rpc_last_sid != 0 && !uint16_circle_newer(sid, requester->rpc_last_sid)) {
            print("V:", LA_F("%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n", LA_F38, 38),
                  PROTO, sid, requester->rpc_last_sid);
            return;
        }

        // 缓存请求
        requester->rpc_last_sid = sid;
        requester->rpc_responding = false;
        requester->rpc_code = msg;
        requester->rpc_data_len = msg_data_len;
        if (msg_data_len > 0) memcpy(requester->rpc_data, msg_data, msg_data_len);

        // 发送 REQ_ACK status=0（成功，开始中转）
        send_msg_req_ack(udp_fd, from, from_str, requester->session_id, sid, 0);

        // 立即向对端转发 MSG_REQ
        requester->rpc_sent_time = P_tick_ms();
        requester->rpc_retry = 0;
        enqueue_rpc_pending(requester);
        send_msg_req_to_peer(udp_fd, requester);

        print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%" PRIu64 ")\n", LA_F17, 17),
               PROTO, requester->local_peer_id, requester->peer->local_peer_id,
               sid, msg, requester->session_id);
    } break;

    // SIG_PKT_MSG_RESP: B→Server（只能是 B 对 Server relay 的 MSG_REQ 的响应）
    // 格式: [session_id(8)][sid(2)][code(1)][data(N)]，code=响应码
    case SIG_PKT_MSG_RESP: { const char* PROTO = "MSG_RESP";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {  // session_id(8) + sid(2) + code(1) 响应码
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        int resp_len = (int)(payload_len - 11);
        if (resp_len > P2P_MSG_DATA_MAX) {
            print("E:", LA_F("%s: data too large (len=%d)\n", LA_F30, 30), PROTO, resp_len);
            return;
        }

        // 解析验证 session_id 和 sid 不为 0
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu64 " or sid=%u\n", LA_F34, 34), 
                  PROTO, session_id, sid);
            return;
        }
        
        // 根据 session_id 查找 responder（B 端，响应方）
        compact_pair_t *responder = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), responder);        
        if (!responder || !responder->valid) {
            print("W:", LA_F("%s: unknown session_id=%" PRIu64 "\n", LA_F43, 43), PROTO, session_id);
            return;
        }

        if (!PEER_ONLINE(responder)) {
            print("W:", LA_F("%s: peer '%s' not online for session_id=%" PRIu64 "\n", LA_F39, 39),
                  PROTO, responder->remote_peer_id, session_id);
            return;
        }

        // 解析 code、data
        uint8_t resp_code = payload[10];
        const uint8_t *resp_data = payload + 11;

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 ", sid=%u, code=%u, len=%d\n", LA_F25, 25),
              PROTO, session_id, sid, resp_code, resp_len);

        // MSG_RESP 来自响应方（B），检查 B 的地址变更
        check_addr_change(udp_fd, responder, from);

        // 可靠重发机制：无条件立即发送 MSG_RESP_ACK 给响应方（让对端停止重发）
        send_msg_resp_ack_to_responder(udp_fd, from, responder->local_peer_id, responder->session_id, sid);

        // peer 是 requester（A）
        compact_pair_t *requester = responder->peer;

        // 检查是否有匹配的挂起请求（REQ 阶段）
        if (!requester->rpc_pending_next || requester->rpc_responding || requester->rpc_last_sid != sid) {
            print("W:", LA_F("%s: no matching pending msg (sid=%u, expected=%u)\n", LA_F36, 36),
                  PROTO, sid, requester->rpc_last_sid);
            return;
        }

        // 缓存响应数据并从 REQ 链表移到 RESP 链表，同时发送给请求方
        remove_rpc_pending(requester);
        transition_to_resp_pending(udp_fd, requester, P_tick_ms(), 0, resp_code, resp_data, resp_len);

        print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u (ses_id=%" PRIu64 ")\n", LA_F16, 16),
              PROTO, responder->local_peer_id, requester->local_peer_id,
              sid, requester->session_id);
    } break;

    // SIG_PKT_MSG_RESP_ACK: A→Server（A 确认收到 MSG_RESP）
    // 格式: [session_id(8)][sid(2)]
    case SIG_PKT_MSG_RESP_ACK: { const char* PROTO = "MSG_RESP_ACK";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 10) {  // session_id(8) + sid(2)
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F28, 28), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);

        // 验证 session_id 和 sid 不为 0
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu64 " or sid=%u\n", LA_F34, 34), 
                   PROTO, session_id, sid);
            return;
        }

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 ", sid=%u\n", LA_F27, 27),
               PROTO, session_id, sid);

        // 根据 session_id 查找 requester
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);
        if (!requester || !requester->valid) {
            print("W:", LA_F("%s: unknown session_id=%" PRIu64 "\n", LA_F43, 43), PROTO, session_id);
            return;
        }

        // 检查是否有匹配的挂起响应转发
        if (!requester->rpc_responding || requester->rpc_last_sid != sid) {
            print("V:", LA_F("%s: no matching pending msg (sid=%u)\n", LA_F35, 35), PROTO, sid);
            return;
        }

        // 清理状态
        remove_rpc_pending(requester);
        requester->rpc_responding = false;
        requester->rpc_retry = 0;

        print("I:", LA_F("%s: RPC complete for '%s', sid=%u (ses_id=%" PRIu64 ")\n", LA_F22, 22),
               PROTO, requester->local_peer_id, sid, requester->session_id);
    } break;
    
    default:
        print("W:", LA_F("Unknown packet type 0x%02x from %s\n", LA_F79, 79), hdr->type, from_str);
        break;
    } // switch
}

// 清理过期的 COMPACT 模式配对记录
static void cleanup_compact_pairs(sock_t udp_fd) {

    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_compact_pairs[i].valid ||
                tick_diff(now, g_compact_pairs[i].last_active) <= COMPACT_PAIR_TIMEOUT_S * 1000) continue;

        print("W:", LA_F("Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n", LA_F78, 78), 
               g_compact_pairs[i].local_peer_id, g_compact_pairs[i].remote_peer_id,
               tick_diff(now, g_compact_pairs[i].last_active) / 1000.0);
        
        // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
        if (PEER_ONLINE(&g_compact_pairs[i]) && 
            g_compact_pairs[i].peer->session_id != 0) {
            send_peer_off(udp_fd, g_compact_pairs[i].peer, "timeout");
        }
        
        // 从待确认链表移除
        if (g_compact_pairs[i].info0_pending_next) {
            remove_info0_pending(&g_compact_pairs[i]);
        }

        // 从 MSG RPC 链表移除
        if (g_compact_pairs[i].rpc_pending_next) {
            remove_rpc_pending(&g_compact_pairs[i]);
        }

        // 从哈希表删除
        if (g_compact_pairs[i].session_id != 0) {
            HASH_DELETE(hh, g_pairs_by_session, &g_compact_pairs[i]);
        }
        HASH_DELETE(hh_peer, g_pairs_by_peer, &g_compact_pairs[i]);

        g_compact_pairs[i].peer = NULL;  // 清空指针
        g_compact_pairs[i].session_id = 0;
        g_compact_pairs[i].addr_notify_seq = 0;
        g_compact_pairs[i].info0_base_index = 0;
        g_compact_pairs[i].info0_retry = 0;
        g_compact_pairs[i].info0_sent_time = 0;
        g_compact_pairs[i].rpc_last_sid = 0;
        g_compact_pairs[i].rpc_responding = false;
        g_compact_pairs[i].rpc_data_len = 0;
        g_compact_pairs[i].rpc_pending_next = NULL;
        g_compact_pairs[i].valid = false;
    }
}

//-----------------------------------------------------------------------------

// 处理 NAT 探测请求
static void handle_probe(sock_t probe_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {

    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    // NAT_PROBE: [hdr(4)] = 4 bytes
    if (len < 4 || buf[0] != SIG_PKT_NAT_PROBE) return;
    const char* PROTO = "NAT_PROBE";

    uint16_t req_seq = ((uint16_t)buf[2] << 8) | buf[3];

    printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F179, 179),
           PROTO, from_str, req_seq, buf[1], len);

    // 构造应答包（NAT_PROBE_ACK）
    // [hdr(4)][probe_ip(4)][probe_port(2)] = 10 bytes
    const char* PROTO_ACK = "NAT_PROBE_ACK";
    buf[0] = SIG_PKT_NAT_PROBE_ACK;
    buf[1] = 0;                                     /* flags */
    buf[2] = (uint8_t)(req_seq >> 8);               /* seq hi (复制请求的 seq) */
    buf[3] = (uint8_t)(req_seq & 0xFF);             /* seq lo */
    memcpy(buf + 4, &from->sin_addr.s_addr, 4);     /* probe_ip   */
    memcpy(buf + 8, &from->sin_port, 2);            /* probe_port */

    print("V:", LA_F("Send %s: mapped=%s:%d\n", LA_F68, 68),
          PROTO_ACK, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    ssize_t n = sendto(probe_fd, (const char *)buf, 10, 0, (struct sockaddr *)from, sizeof(*from));
    if (n != 10)
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F117, 117), PROTO_ACK, from_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n", LA_F180, 180),
               PROTO_ACK, from_str, req_seq, (int)n);
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "p2p0d"

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
        print("I: \n%s\n", LA_S("Received shutdown signal, exiting gracefully...", LA_S8, 8));
        g_running = 0;
    }
}
#endif

int main(int argc, char *argv[]) {

    // 初始化语言系统
    LA_init();

    // 设置语言钩子
    P_lang = lang_cstr;

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
                    "Note: Run without arguments to use default configuration (port 9333)", 0, 0));

    // 解析命令行参数（如果无参数，使用默认配置不显示帮助）
    if (argc > 1) {
        ARGS_parse(argc, argv,
            &ARGS_DEF_port,
            &ARGS_DEF_probe_port,
            &ARGS_DEF_relay,
            &ARGS_DEF_msg,
            &ARGS_DEF_cn,
            NULL);
    }

    // 获取参数值（设置默认值）
    int port = ARGS_port.i64 ? (int)ARGS_port.i64 : DEFAULT_PORT;

    // 验证端口范围
    if (port <= 0 || port > 65535) {
        print("E:", LA_F("Invalid port number %d (range: 1-65535)\n", LA_F51, 51), port);
        ARGS_print(argv[0]);
        return 1;
    }
    if (ARGS_probe_port.i64 < 0 || ARGS_probe_port.i64 > 65535) {
        print("E:", LA_F("Invalid probe port %d (range: 0-65535)\n", LA_F52, 52), (int)ARGS_probe_port.i64);
        ARGS_print(argv[0]);
        return 1;
    }
    
    if (P_net_init() != E_NONE) {
        print("E:", LA_F("net init failed\n", LA_F124, 124));
        return 1;
    }

    // 初始化随机数生成器（用于生成安全的 session_id）
    P_rand_init();

    // 打印服务器配置信息
    print("I:", LA_F("Starting P2P signal server on port %d\n", LA_F77, 77), port);
    print("I:", LA_F("NAT probe: %s (port %d)\n", LA_F60, 60), 
          ARGS_probe_port.i64 > 0 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1), 
          (int)ARGS_probe_port.i64);
    print("I:", LA_F("Relay support: %s\n", LA_F66, 66), 
          ARGS_relay.i64 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1));

    // 注册信号处理
#if P_WIN
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        print("W:", "%s", LA_S("Failed to set console ctrl handler\n", 0));
    }
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    // 创建 TCP 监听套接字（用于 Relay 信令模式）
    sock_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == P_INVALID_SOCKET) {
        print("E:", "TCP socket failed(%d)\n", P_sock_errno());
        return 1;
    }
    int sockopt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));

    // 创建 UDP 套接字（用于 COMPACT 信令模式）
    sock_t udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == P_INVALID_SOCKET) {
        print("E:", "UDP socket failed(%d)\n", P_sock_errno());
        return 1;
    }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));

    // 创建 NAT 探测 UDP 套接字（可选，仅当配置了 probe_port 时）
    sock_t probe_fd = P_INVALID_SOCKET;
    if (ARGS_probe_port.i64 > 0) {
        probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (probe_fd == P_INVALID_SOCKET) {
            print("E:", "probe UDP socket failed(%d)\n", P_sock_errno());
            return 1;
        }
        setsockopt(probe_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));
    }

    // 绑定监听端口（TCP 和 UDP 同一端口）
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
       if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
       
        print("E:", "TCP bind failed(%d)\n", P_sock_errno());
        return 1;
    }
    if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        print("E:", "UDP bind failed(%d)\n", P_sock_errno());
        return 1;
    }

    // 绑定 NAT 探测端口（独立端口，客户端用同一本地端口发包，服务器在此处看到不同映射地址）
    if (probe_fd != P_INVALID_SOCKET) {
        struct sockaddr_in probe_addr = {0};
        probe_addr.sin_family = AF_INET;
        probe_addr.sin_addr.s_addr = INADDR_ANY;
        probe_addr.sin_port = htons((uint16_t)ARGS_probe_port.i64);
        if (bind(probe_fd, (struct sockaddr *)&probe_addr, sizeof(probe_addr)) < 0) {

            print("E:", LA_F("probe UDP bind failed(%d)\n", LA_F125, 125), P_sock_errno());
            P_sock_close(probe_fd);
            probe_fd = P_INVALID_SOCKET;
            ARGS_probe_port.i64 = 0;  /* 绑定失败，禁用探测功能 */
            print("W:", LA_F("NAT probe disabled (bind failed)\n", LA_F58, 58));
        } 
        else {
            print("I:", LA_F("NAT probe socket listening on port %d\n", LA_F59, 59), (int)ARGS_probe_port.i64);
        }
    }

    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd, 10);
    print("I:", LA_F("P2P Signaling Server listening on port %d (TCP + UDP)...\n", LA_F61, 61), port);

    // 主循环
    fd_set read_fds;
    uint64_t last_cleanup = P_tick_ms(), last_compact_retry_check = last_cleanup;
    while (g_running) {

        uint64_t now = P_tick_ms();

        // 周期清理过期的 COMPACT 配对记录和 Relay 客户端连接
        if (tick_diff(now, last_cleanup) >= CLEANUP_INTERVAL_S * 1000) {
            cleanup_compact_pairs(udp_fd);
            cleanup_relay_clients();
            last_cleanup = now;
        }
        
        // 检查并重传未确认的 PEER_INFO 包 + MSG RPC 包（每秒检查一次）
        if (tick_diff(now, last_compact_retry_check) >= COMPACT_RETRY_INTERVAL_MS) {
            if (g_compact_info0_pending_head) retry_info0_pending(udp_fd, now);
            if (g_rpc_pending_head)   retry_rpc_pending(udp_fd, now);
            last_compact_retry_check = now;
        }

        // 设置要监听的套接口 fd
        // + TCP listen + TCP clients + UDP + probe UDP + 客户端...
        // + max_fd 必须是所有监听套接字中数值最大的那个（Windows 不使用此值，但 POSIX 需要正确设置）
        int max_fd = 0;
        fd_set write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(listen_fd, &read_fds);
        FD_SET(udp_fd, &read_fds);
        if (probe_fd != P_INVALID_SOCKET) FD_SET(probe_fd, &read_fds);
#if !P_WIN
        max_fd = (int)((listen_fd > udp_fd) ? listen_fd : udp_fd);
        if (probe_fd != P_INVALID_SOCKET && (int)probe_fd > max_fd) max_fd = (int)probe_fd;
#endif
        // 添加有效的 TCP 客户端套接字到监听集合中
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].base.valid && g_relay_clients[i].fd != P_INVALID_SOCKET) {
                FD_SET(g_relay_clients[i].fd, &read_fds);                
                // 如果有待发送的 ONLINE_ACK 或 session 数据，监听可写事件
                if (g_relay_clients[i].online_ack_pending || g_relay_clients[i].sending_head)
                    FD_SET(g_relay_clients[i].fd, &write_fds);                
#if !P_WIN
                if ((int)g_relay_clients[i].fd > max_fd) max_fd = (int)g_relay_clients[i].fd;
#endif
            }
        }

        // 等待套接口数据（超时1秒，用于周期性清理）
        struct timeval tv = {1, 0};
        int sel_ret = select(max_fd + 1, &read_fds, &write_fds, NULL, &tv);
        if (sel_ret < 0) {
            if (P_sock_is_interrupted()) continue;  // 被信号打断，继续循环
            print("E:", LA_F("select failed(%d)\n", LA_F126, 126), P_sock_errno());
            break;
        }

        //-------------------------------

        // 如果存在新的 TCP 连接请求，accept 并将其添加到客户端列表中
        if (FD_ISSET(listen_fd, &read_fds)) {

            struct sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
            sock_t client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            
            // 设置为非阻塞模式，避免慢客户端阻塞整个服务器事件循环
            if (P_sock_nonblock(client_fd, true) != E_NONE) {
                print("W:", LA_F("[TCP] Failed to set client socket to non-blocking mode\n", LA_F213, 213));
            }
            
            int i = 0;
            for (i = 0; i < MAX_PEERS; i++) {

                // 查找一个空闲槽位来存储这个新的连接
                if (!g_relay_clients[i].base.valid) {

                    buffer_item_t *buf_item = relay_buf_alloc(RELAY_FRAME_SIZE);
                    if (!buf_item) {
                        print("E:", LA_F("[TCP] OOM: cannot allocate recv buffer for new client\n", LA_F217, 217));
                        P_sock_close(client_fd);
                        i = MAX_PEERS + 1;
                        break;
                    }

                    g_relay_clients[i].base.valid = true;
                    g_relay_clients[i].base.last_active = P_tick_ms();
                    g_relay_clients[i].base.local_peer_id[0] = '\0';
                    g_relay_clients[i].base.instance_id = 0;
                    g_relay_clients[i].base.sessions = NULL;

                    g_relay_clients[i].fd = client_fd;
                    g_relay_clients[i].online_ack_pending = false;
                    g_relay_clients[i].recv_buf = ITEM2BUF(buf_item);
                    g_relay_clients[i].recv_len = 0;
                    g_relay_clients[i].sending_head = NULL;
                    g_relay_clients[i].sending_rear = NULL;
                    g_relay_clients[i].send_offset = 0;

                    print("V:", LA_F("[TCP] New connection from %s:%d\n", LA_F101, 101), 
                          inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    break;
                }
            }
            if (i == MAX_PEERS) {
                print("W:", LA_F("[TCP] Max peers reached, rejecting connection\n", LA_F99, 99));
                P_sock_close(client_fd);
            }
        }
        
        // UDP 监听端口收到数据包（COMPACT 模式的信令交互）
        if (FD_ISSET(udp_fd, &read_fds)) {

            uint8_t buf[P2P_MTU]; struct sockaddr_in from; socklen_t from_len = sizeof(from);
            size_t n = recvfrom(udp_fd, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
            if (n > 0) {
                handle_compact_signaling(udp_fd, buf, n, &from);
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

        // 处理 Relay 模式的 TCP 事件（先发送后接收，单次遍历）
        for (int i = 0; i < MAX_PEERS; i++) {
            if (!g_relay_clients[i].base.valid || g_relay_clients[i].fd == P_INVALID_SOCKET) continue;
            
            relay_client_t *client = &g_relay_clients[i];
            relay_session_t *sending_session = client->sending_head;

            // 如果当前正在等待发送中的数据
            if ((client->online_ack_pending || sending_session) 
                && FD_ISSET(client->fd, &write_fds)) {

                // 当前正在发送 ONLINE_ACK
                // + 此时还没有 session，复用 recv_buf 作为 send_buf，recv_len 作为已发送长度
                if (client->online_ack_pending) {

                    size_t ack_total = sizeof(p2p_relay_hdr_t) + 2;
                    size_t len = ack_total - client->recv_len;
                    int rc = tcp_send(client, client->recv_buf + client->recv_len, &len, "ONLINE_ACK pending");
                    if (rc < 0) {
                        P_sock_close(client->fd);
                        client->fd = P_INVALID_SOCKET;
                        client->base.valid = false;
                        client->online_ack_pending = false;
                        client->recv_len = 0;
                        if (client->recv_buf) {
                            relay_buf_free(BUF2ITEM(client->recv_buf));
                            client->recv_buf = NULL;
                        }
                        while (client->base.sessions) {
                            relay_free_session((relay_session_t*)client->base.sessions);
                        }
                        continue;
                    }

                    if (len > 0) { client->recv_len += len;

                        // ONLINE_ACK 发送完成
                        if (client->recv_len >= ack_total) { client->recv_len = 0;
                            client->online_ack_pending = false;                        
                            print("V:", LA_F("ONLINE_ACK sent to '%s'\n", LA_F272, 272), client->base.local_peer_id);
                        }
                    }

                    // ONLINE_ACK 未完成时，跳过其他处理
                    if (client->online_ack_pending) continue;
                }
                // 2. 处理 session 发送队列（与 ONLINE_ACK 分支互斥）
                else { assert(sending_session->send_head);

                    buffer_item_t *item = sending_session->send_head;
                    const p2p_relay_hdr_t *hdr = (const p2p_relay_hdr_t *)ITEM2BUF(item);

                    // 原始入站 SYNC0 零拷贝包：首发时切换到重映射头部位置发送
                    if (hdr->size == 0) *(uint8_t**)&hdr += P2P_PEER_ID_MAX - P2P_RLY_SESS_ID_PSZ;

                    // 零拷贝包：buf[0].size=0 是标记，实际长度从 overlaid header（偏移处）读取
                    const uint16_t len = (uint16_t)(sizeof(p2p_relay_hdr_t) + ntohs(hdr->size));
                    size_t remaining = len - client->send_offset;
                    int rc = tcp_send(client, (const char *)hdr + client->send_offset, &remaining, "session queue");
                    if (rc < 0) {
                        relay_clear_client(client);
                        continue;
                    }

                    if (remaining > 0) { client->send_offset += (uint32_t)remaining;

                        // 当前 session 发送完成
                        if (client->send_offset >= len) { client->send_offset = 0;

                            // 如果 item 有 refer，说明这是一个需要发送完成回调的包
                            if (item->refer) {
                                relay_session_send_complete((relay_session_t*)item->refer, item);
                            }

                            // 删除已发送完成的 item
                            if (!(sending_session->send_head = item->next))
                                sending_session->send_rear = NULL;
                            relay_buf_free(item);
                            
                            // 如果 session 发送队列已空，发送下一条待发送 session
                            if (!sending_session->send_head) {
                                client->sending_head = sending_session->send_next;
                                if (client->sending_head) client->sending_head->send_prev = NULL;
                                else                      client->sending_rear = NULL;
                                sending_session->send_next = NULL;
                            }
                        }
                    }
                }
            }
            
            // 3. 处理接收数据（信令交互）
            if (FD_ISSET(g_relay_clients[i].fd, &read_fds)) {
                handle_relay_signaling(i);
            }
        }

    } // while (g_running)

    // 清理资源
    print("I: \n%s", LA_S("Shutting down...\n", LA_S9, 9));
    
    // 关闭所有客户端连接
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_relay_clients[i].base.valid && g_relay_clients[i].fd != P_INVALID_SOCKET) {
            P_sock_close(g_relay_clients[i].fd);
        }
        if (g_relay_clients[i].recv_buf) {
            relay_buf_free(BUF2ITEM(g_relay_clients[i].recv_buf));
            g_relay_clients[i].recv_buf = NULL;
        }
        g_relay_clients[i].recv_len = 0;
    }
    
    // 关闭监听套接字
    P_sock_close(listen_fd);
    P_sock_close(udp_fd);
    if (probe_fd != P_INVALID_SOCKET) P_sock_close(probe_fd);
    
    P_net_cleanup();

    print("I:", LA_F("Goodbye!\n", LA_F50, 50));
    return 0;
}
