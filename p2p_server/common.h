//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_TYPE_H
#define P2P_TYPE_H

#include "../src/p2p_common.h"
#include <p2p.h>
#include "uthash.h"
#ifdef WITH_WSLAY
#include <wslay/wslay.h>
#endif

#include "LANG.h"

//-----------------------------------------------------------------------------

#define DEFAULT_PORT                    9333

#define MAX_PEERS                       128     /* 允许最大同时在线客户端数量 */

#define CLIENT_TIMEOUT_S                60      /* 客户端超时淘汰时间（秒） */

#define CLEANUP_INTERVAL_S              10      /* cleanup 过期客户端的时间间隔（秒） */

#define RETRY_INTERVAL_MS               1000    /* 可靠性重传间隔（毫秒） */

#define SELECT_TIMEOUT_MS               100     /* select 超时时间（毫秒） */

// 允许最大候选队列缓存数量
/* + 服务器为每个用户提供的候选缓存能力
 |   32 个候选可容纳大多数网络环境的完整候选集合，实际场景通常：20-30 个候选，32 提供充足余量
 | + 内存占用：COMPACT 模式 32×7字节=224B/用户，RELAY 模式 32×32字节=1KB/用户
*/
#define MAX_CANDIDATES_CONFIG           32
#define MAX_CANDIDATES_BY_PAYLOAD       ((P2P_MAX_PAYLOAD - (2 * P2P_PEER_ID_MAX + P2P_SESS_ID_SZ + 1)) / sizeof(p2p_candidate_t))
#define MAX_CANDIDATES                  ((MAX_CANDIDATES_CONFIG) < (MAX_CANDIDATES_BY_PAYLOAD) ? (MAX_CANDIDATES_CONFIG) : (MAX_CANDIDATES_BY_PAYLOAD))

// COMPACT 模式 MSG RPC 重传参数
#define RPC_RETRY_INTERVAL_MS           1000    // MSG RPC 统一重传间隔（毫秒）
#define REQ_MAX_RETRY                   5       // MSG_REQ 最大重传次数
#define RSP_MAX_RETRY                   10      // MSG_RSP 最大重传次数（比 REQ 更多，确保 A 端收到）

///////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)
typedef struct buffer_item {
    struct buffer_item*             next;
    void*                           refer;
    uint16_t                        len;    // buf 中有效数据长度（字节）
    uint8_t                         flags;  // 低 5 bit，表示自定义意义标识; 高 3 bit 表示内存 chunk 大小。即 512 * 2^(flags >> 5)
} buffer_item_t;
#pragma pack(pop)
#define ITEM2BUF(item)              ((uint8_t*)(item + 1))
#define BUF2ITEM(buf)               (((buffer_item_t*)(buf)) - 1)

#define BUF_FLAGS(sz_flag, flags)   ((sz_flag) | ((flags) & 0x1F))
#define BUF_FLAG_128(flags)         BUF_FLAGS(0<<5, flags)
#define BUF_FLAG_MTU(flags)         BUF_FLAGS(1<<5, flags)
#define BUF_FLAG_2048(flags)        BUF_FLAGS(2<<5, flags)
#define BUF_FLAG_4096(flags)        BUF_FLAGS(3<<5, flags)
#define BUF_FLAG_8192(flags)        BUF_FLAGS(4<<5, flags)
#define BUF_FLAG_16384(flags)       BUF_FLAGS(5<<5, flags)
#define BUF_FLAG_32768(flags)       BUF_FLAGS(6<<5, flags)
#define BUF_FLAG_65536(flags)       BUF_FLAGS(7<<5, flags)

static inline uint16_t buffer_size(uint16_t flags) { flags>>=5; return flags>1?(1 << flags)*512:flags?P2P_MTU:128; }
static inline uint16_t buffer_sz_flag(uint16_t size) {
    if (size <= 128u) return BUF_FLAG_128(0);
    if (size <= P2P_MTU) return BUF_FLAG_MTU(0);
    // 其余映射到 2048..65536 桶；注意避免 __builtin_clz(0) 的情况（前面已排除）
    unsigned exp = (32u - 9u) - (unsigned)__builtin_clz((unsigned)size - 1u);   // ceil(log2(size))
#if P2P_MTU <= 1024
    if (exp < 2u) exp = 2u;    // P2P_MTU+1..2048 统一归入 2048 桶
#endif
    return (uint16_t)(exp << 5);
}

buffer_item_t* alloc_buffer(uint8_t flags);
void free_buffer(buffer_item_t *buf_item);

typedef struct buffer_queue {
    buffer_item_t*                  head;
    buffer_item_t*                  rear;
} buffer_queue_t;

#define BUF_Q_APPEND(q, item) (item)->next = NULL;                      \
    if ((q)->rear) (q)->rear->next = item; else (q)->head = item; (q)->rear = item;
#define BUF_Q_AFTER(q, p, item)                                         \
    if (!(((item)->next = p->next))) (q)->rear = item; (p)->next = item;
#define BUF_Q_PUSH(q, item)                                             \
    (item)->next = (q)->head; (q)->head = item; if (!(q)->rear) (q)->rear = item;

// ! 注意, 下面三个移除操作都不会清理 item->next，调用方如有需要应自行处理
#define BUF_Q_POP(q, item) assert((q)->head == (item));                 \
    if (!(((q)->head = (item)->next))) (q)->rear = NULL;
#define BUF_Q_POP_TO(item, q)                                           \
    if ((item = (q)->head) && !(((q)->head = (item)->next))) (q)->rear = NULL;
#define BUF_Q_RM(q, p, item)                                            \
    if (!p) { assert((q)->head == (item));                              \
        (q)->head = (item)->next; if (!(q)->head) (q)->rear = NULL;     \
    } else { assert(p->next == (item));                                 \
        p->next = (item)->next; if ((q)->rear == (item)) (q)->rear = p; \
    }

// 这里将 from_q 追加到 to_q 的后面，等价于 to_q 插入到 from_q 的前面
#define BUF_Q_MV(to_q, from_q)                                          \
    if ((to_q)->rear) (to_q)->rear->next = (from_q)->head; else (to_q)->head = (from_q)->head;      \
    if ((from_q)->rear) { (to_q)->rear = (from_q)->rear; (from_q)->head = (from_q)->rear = NULL; }

#define BUF_Q_FOR(q, it, ...)                                           \
    for (buffer_item_t *it = (q)->head; it; it = it->next) {            \
        __VA_ARGS__                                                     \
    }
#define BUF_Q_CLEAR(q, it, ...)                                         \
    for (buffer_item_t *it = (q)->head; it; it = (q)->head) {           \
        (q)->head = it->next; __VA_ARGS__                               \
    } (q)->rear = NULL;

#define BUF_Q_JOIN(q, u8to)                         \
    for (buffer_item_t* item = (q)->head; item; item = item->next) {  \
        memcpy(u8to, ITEM2BUF(item), item->len);    \
        u8to += item->len;                          \
    }

typedef struct buffer_stream {
    buffer_queue_t*                 queue;
    buffer_item_t*                  current;
    uint32_t                        cur;
} buffer_stream_t;

#define BUF_S_RESET(s, q)                           \
    s->queue = q;                                   \
    s->current = (q) ? (q)->head : NULL;            \
    s->cur = 0

static inline uint32_t BUF_S_read(buffer_stream_t* s, uint8_t* buf, uint32_t len) {
    if (!s->current) return 0;
    uint32_t read = 0;
    while (read < len) {
        uint32_t r = s->current->len - s->cur, n = (len-read); n = r < n ? r : n;
        if (buf) { memcpy(buf+read, ITEM2BUF(s->current) + s->cur, n); buf += n; } read += n;
        if ((s->cur += n) >= s->current->len) {
            if (!((s->current = s->current->next))) break;
            s->cur = 0;
        }
    }
    return read;
}

//-----------------------------------------------------------------------------

#define PROTO_COMPACT               0
#define PROTO_RELAY                 1
#define PROTO_WSS                   2   /* Websocket Session Sync */

typedef struct session session_t;

typedef struct client {
    int8_t                          proto;              // -1=INV, 0=COMPACT, 1=RELAY, 2=WSS
    char                            local_peer_id[P2P_PEER_ID_MAX+1];
    uint32_t                        instance_id;
    sock_t                          fd;
    uint64_t                        last_active;
    session_t*                      sessions;
    UT_hash_handle                  hh;
} client_t;

static inline bool client_identified(client_t* c) { return c->hh.tbl != NULL; }

typedef struct session_pair {
    bool                            valid;
    char                            peer_id[2][P2P_PEER_ID_MAX+1];  // hh_peer 复合 key 起始（与 remote_peer_id 连续）
    session_t*                      sessions[2];                    // 双端会话指针
    UT_hash_handle                  hh;
} session_pair_t;

struct session {
    struct client*                  client;
    struct session*                 prev;
    struct session*                 next;
    session_pair_t*                 pair;
    struct session*                 peer;
    uint32_t                        session_id;
    UT_hash_handle                  hh;
};

#define CLIENT(s)                   ((session_t*)(s))->client
#define PEER(s)                     ((session_t*)(s))->peer
#define PEER_VALID(p)               ((p) && (void*)(p) != (void*)-1)
#define PEER_ONLINE(s)              PEER_VALID(PEER(s))

#define TCP_PEER_REACHABLE(s)       TCP_REACHABLE(PEER(s)->client)

typedef enum break_mode {
    SESS_BREAK_STOP,                // 会话及其连接关系会保留，核心场景为 client 变为 unreachable
    SESS_BREAK_CLOSE,               // 会话会被销毁，但允许向 client 返回状态数据信息，核心场景是 client（延迟）软退出
    SESS_BREAK_TERM,                // 会话会被销毁，且不会再向 client 返回状态数据信息，核心场景是 client/session （立刻）硬退出
} break_mode_e;

//-----------------------------------------------------------------------------

client_t*
find_client(const char *local_peer_id);

// 由派生协议 client 调用，基类会根据协议类型自动执行不同派生协议类型的释放操作
void
free_client(client_t *c);

// 由派生协议 client 调用，用于实现基类的释放
void
free_client_base(client_t *c);


// 分配一个指定派生协议类型的 client 对象
// + 主要用于 UDP/COMPACT 协议调用，因为 TCP/xxx 协议在 accept 建链时就已经自动分配了 client 对象
client_t*
alloc_client(int8_t proto, sock_t fd);

// 将同一个客户端的两个槽合并，确保 client 为单一实例
// + client 为之前存在的，而 from 则会根据 UDP/TCP 的不同特性而选择是否为 NULL
//   > TCP 每个连接都对应一个 client，所以 from 为新连接分配的 client 对象，这里要将新的 client 归并到之前的 client
//   > UDP 没有独立连接的概念，所以协议会自行根据一个 AUTH KEY 来维持唯一性
bool
resident_client(client_t* client, int8_t proto, uint32_t instance_id, client_t* from/* nullable */);

// 唯一标识客户端（注册 local_peer_id，并加入全局索引哈希表）
bool
identify_client(client_t* c);



session_t*
find_session(uint32_t session_id);

// 进行本端和远程的会话配对
ret_t
pair_session(client_t *client, const char *remote_peer_id,
             session_t **local_s, session_t **remote_s,
             size_t session_type_size);

// 由派生协议 session 调用，用于实现基类的释放
void
free_session_base(session_t *s);

//-----------------------------------------------------------------------------

// TCP 握手状态：0:握手完成; >0:握手中; <0:closing;
#define TCP_CLIENT  \
    uint8_t                         io;         \
    int8_t                          handshake;

// 该标记用于设定 TCP 是否对该 sock 进行相应的 I/O 操作
#define TCP_IO_FLAG_WANT_READ       (1<<0)
#define TCP_IO_FLAG_WANT_WRITE      (1<<1)
// 该标记用于监听 ws 等第三方协议库 I/O 操作后的 sock 状态
#define TCP_IO_FLAG_READ_BREAK      (1<<2)  /* sock 出错或 would block，停止继续读取  */
#define TCP_IO_FLAG_WRITE_BREAK     (1<<3)  /* sock 出错或 would block，停止继续写入  */
// 自定义 flag 的 bit 起始位
#define TCP_IO_FLAG_CUSTOM_BIT      4

#define TCP_HS_FLAG_HANDSHAKING     (1)
#define TCP_HS_FLAG_CLOSING         (-1)
#define TCP_HS_IS_HANDSHAKING(c)    (((tcp_client_t*)c)->handshake > 0)
#define TCP_HS_IS_CLOSING(c)        (((tcp_client_t*)c)->handshake < 0)

typedef struct tcp_client
{
    client_t                        base;
    TCP_CLIENT
} tcp_client_t;

#define TCP_CLIENT_INIT(c) \
    (c)->io = TCP_IO_FLAG_WANT_READ; \
    (c)->handshake = TCP_HS_FLAG_HANDSHAKING

#define TCP_REACHABLE(c)            (((tcp_client_t*)c)->io & TCP_IO_FLAG_WANT_READ)

//-----------------------------------------------------------------------------

ssize_t
udp_send(sock_t udp_fd, const void *buf, int len, const struct sockaddr_in *to, const char *PROTO);

int
tcp_send(tcp_client_t* client, const void *buf, size_t *w_sz, const char *PROTO);

// SP（SUB PROTOCOL）为 NULL 时，默认为 "TCP"
int
tcp_recv(tcp_client_t* client, void *buf, size_t *r_sz, const char * SP);

///////////////////////////////////////////////////////////////////////////////

#endif //P2P_TYPE_H
