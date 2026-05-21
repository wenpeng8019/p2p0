//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_TYPE_H
#define P2P_TYPE_H

#include "../src/p2p_common.h"
#include <p2p.h>
#include "uthash.h"

#include "LANG.h"

//-----------------------------------------------------------------------------

#define DEFAULT_PORT                    9333

#define MAX_PEERS                       128     /* 允许最大同时在线客户端数量 */

#define DEFAULT_CLIENT_TIMEOUT_S        60      /* 客户端超时淘汰时间默认值（秒） */

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
#define REQ_MAX_RETRY                   5       // REQ 最大重传次数
#define RSP_MAX_RETRY                   10      // MSG_RSP 最大重传次数（比 REQ 更多，确保 A 端收到）

///////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)
typedef struct buf16_item {
    struct buf16_item*              next;
    void*                           refer;
    uint16_t                        len;        // buf 中有效数据长度（字节）
    uint16_t                        pos;        // buf 中有效数据的起始偏移位置
    uint8_t                         flags;      // 低 4 bit，表示自定义意义标识; 高 4 bit 表示内存 chunk 大小。即 512 * 2^(flags >> 4)
} buf16_item_t;
typedef struct buf32_item {
    struct buf16_item*              next;
    void*                           refer;
    uint32_t                        len;        // buf 中有效数据长度（字节）
    uint8_t                         flags;      // 低 4 bit，表示自定义意义标识; 高 4 bit 表示内存 chunk 大小。即 512 * 2^(flags >> 4)
    uint8_t                         reserved;   // 保留字节，确保 pos 属性内存对齐
    uint16_t                        pos;        // buf 中有效数据的起始偏移位置
} buf32_item_t;
#pragma pack(pop)
#define ITEM2BUF(item)              ((uint8_t*)((item) + 1))

#define BUF_FLAGS(sz_flag, flags)   ((uint8_t)(sz_flag) | ((flags) & (uint8_t)0xFu))
#define BUF_FLAG_128(flags)         BUF_FLAGS(0<<4, flags)
#define BUF_FLAG_256(flags)         BUF_FLAGS(1<<4, flags)
#define BUF_FLAG_512(flags)         BUF_FLAGS(2<<4, flags)
#define BUF_FLAG_MTU(flags)         BUF_FLAGS(3<<4, flags)
#define BUF_FLAG_2048(flags)        BUF_FLAGS(4<<4, flags)
#define BUF_FLAG_4096(flags)        BUF_FLAGS(5<<4, flags)
#define BUF_FLAG_8192(flags)        BUF_FLAGS(6<<4, flags)
#define BUF_FLAG_16384(flags)       BUF_FLAGS(7<<4, flags)
#define BUF_FLAG_32768(flags)       BUF_FLAGS(8<<4, flags)
#define BUF_FLAG_65536(flags)       BUF_FLAGS(9<<4, flags)

#define BUF_FLAG_32BIT(flags)       BUF_FLAGS(0xF, flags)
#define BUF_IS_32BIT(flags)         ((uint8_t)flags >> 4 == 0xF)
#define BUF32(item)                 ((buf32_item_t*)(item))

static inline uint16_t buffer_size(uint16_t flags) { flags>>=4; return flags==3?P2P_MTU:(1 << flags)*128; }
static inline uint16_t buffer_sz_flag(uint16_t size) {
    if (size <= 128) return 0;  // 最小分配池：128 字节
    unsigned exp = (32u - 7u) - (unsigned)__builtin_clz((unsigned)size - 1u);   // ceil(log2(size))
#if P2P_MTU > 1024
    if (exp == 4 && size <= P2P_MTU) exp = 3;
#else
    if (exp == 3 && size > P2P_MTU) exp = 4;
#endif
    return (uint16_t)(exp << 4);
}

buf16_item_t* alloc_buf16(uint8_t flags);
void free_buf16(buf16_item_t *buf_item);

static inline buf32_item_t* alloc_buf32(uint8_t flags, uint32_t size) {
    buf32_item_t *buf_item = (buf32_item_t*)malloc(sizeof(buf32_item_t) + size);
    if (!buf_item) return NULL;
    buf_item->next = NULL;
    buf_item->flags = BUF_FLAG_32BIT(flags);
    buf_item->refer = NULL;
    buf_item->len = size;
    buf_item->pos = 0;
    return buf_item;
}

static inline buf16_item_t* alloc_buffer(uint8_t flags, uint32_t size) {
    if (size > 65536) return (buf16_item_t*)alloc_buf32(flags, size);
    return alloc_buf16(BUF_FLAGS(buffer_sz_flag((uint16_t)size), flags));
}
#define free_buffer(item) if (BUF_IS_32BIT(item->flags)) free(item); else free_buf16(item)

typedef struct buffer_queue {
    buf16_item_t*                  head;
    buf16_item_t*                  rear;
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
    for (buf16_item_t *it = (q)->head; it; it = it->next) {             \
        __VA_ARGS__                                                     \
    }
#define BUF_Q_CLEAR(q, it, ...)                                         \
    for (buf16_item_t *it = (q)->head; it; it = (q)->head) {            \
        (q)->head = it->next; __VA_ARGS__                               \
    } (q)->rear = NULL;

#define BUF_Q_JOIN(q, u8to)                                             \
    for (buf16_item_t* item = (q)->head; item; item = item->next) {     \
        if (BUF_IS_32BIT(item->flags)) {                                \
            memcpy(u8to, ITEM2BUF((buf32_item_t*)item), ((buf32_item_t*)item)->len);    \
            u8to += ((buf32_item_t*)item)->len;                                         \
        } else {                                                        \
            memcpy(u8to, ITEM2BUF(item), item->len);                    \
            u8to += item->len;                                          \
        }                                                               \
    }

typedef struct buffer_round {
    buf16_item_t**                  slots;      // 指针数组
    int16_t                         size;       // 负数表示空（绝对值为容量），正数表示非空（值为容量）
    uint16_t                        r;          // 读指针（队头）
    uint16_t                        w;          // 写指针（队尾）
} buffer_round_t;

// 初始化循环队列（需要预先分配 slots 数组）
#define BUF_R_INIT(rq, slot_array, capacity) do {           \
    (rq)->slots = (slot_array);                             \
    (rq)->size = -(int16_t)(capacity);                      \
    (rq)->r = 0;                                            \
    (rq)->w = 0;                                            \
} while(0)

// 判断循环队列是否为空（size 为负数表示空）
#define BUF_R_EMPTY(rq)                                     \
    ((rq)->size < 0)

// 获取容量（size 的绝对值）
#define BUF_R_CAPACITY(rq)                                  \
    ((uint16_t)((rq)->size < 0 ? -(rq)->size : (rq)->size))

// 判断循环队列是否已满（size 为正且读写指针相遇）
#define BUF_R_FULL(rq)                                      \
    ((rq)->size > 0 && (rq)->r == (rq)->w)

// 获取循环队列中的元素数量
#define BUF_R_COUNT(rq)                                     \
    ((rq)->size < 0 ? 0 : ((rq)->w >= (rq)->r ? (rq)->w - (rq)->r : (rq)->size + (rq)->w - (rq)->r))

// 获取队首元素 !! 调用者需要确保队列不空
#define BUF_R_FRONT(rq)                                     \
    (rq)->slots[(rq)->r]

// 从队首移除（在使用 BUF_R_FRONT 读取数据后调用，移动读指针）
#define BUF_R_POP(rq) if ((rq)->size>0) do {                \
    (rq)->r = ((rq)->r + 1) % (rq)->size;                   \
    if ((rq)->r == (rq)->w) (rq)->size = -(rq)->size;       \
} while (0)

// 从队首出队到变量（读取并自动移动读指针）
#define BUF_R_POP_TO(item, rq) do {                         \
    if ((rq)->size < 0) item = NULL;                        \
    else { item = (rq)->slots[(rq)->r];                     \
        (rq)->r = ((rq)->r + 1) % (rq)->size;               \
        if ((rq)->r == (rq)->w) (rq)->size = -(rq)->size;   \
    }                                                       \
} while (0)

// 获取队尾元素 !! 调用者需要确保队列不空
#define BUF_R_LAST(rq)                                      \
    (rq)->slots[(rq)->w ? (rq)->w - 1 : (rq)->size-1]

// 获取队尾可写入位置（返回 buf16_item_t* 的地址，队列满时返回 NULL）
#define BUF_R_BACK(rq)                                      \
    (BUF_R_FULL(rq) ? NULL : &(rq)->slots[(rq)->w])

// 提交写入到队尾（入队成功时 item 保持不变，队列满时 item 置为 NULL 表示失败）
// 备注：调用后检查 item 是否为 NULL 可判断入队是否成功
#define BUF_R_PUSH(rq, item) do {                           \
    if ((rq)->r == (rq)->w) {                               \
        if ((rq)->size < 0) {                               \
            (rq)->slots[0] = item;                          \
            (rq)->r = 0; (rq)->w = 1;                       \
            if ((rq)->size < -1) (rq)->size = -(rq)->size;  \
        } else item = NULL;  /* 队列满，置空表示失败 */     \
    } else { (rq)->slots[(rq)->w] = item;                   \
        (rq)->w = ((rq)->w + 1) % (rq)->size;               \
    }                                                       \
} while(0)

// 清空循环队列
#define BUF_R_CLEAR(rq) do {                                \
    if ((rq)->size > 0) (rq)->size = -(rq)->size;           \
    (rq)->r = (rq)->w = 0;                                  \
} while(0)

// 遍历循环队列中的所有元素
#define BUF_R_FOR(rq, it, ...)                      \
    if ((rq)->size > 0) {                           \
        for (uint16_t _r = (rq)->r; _r != (rq)->w; _r = (_r + 1) % (rq)->size) { \
            buf16_item_t *it = (rq)->slots[_r];     \
            __VA_ARGS__                             \
        }                                           \
    }

typedef struct buffer_stream {
    buffer_queue_t*                 queue;
    buf16_item_t*                   current;
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

typedef struct timeout_queue {
    uint32_t                        timeout_limit;                  // 超时限制
    uint32_t                        obj_prev_offset;                // obj 中的 prev 成员的偏移位置（字节）
    uint32_t                        obj_next_offset;                // obj 中的 next 成员的偏移位置（字节）
    uint32_t                        obj_time_offset;                // obj 中的 time 成员的偏移位置（字节）
    void*                           head;                           // 队头对象指针（head->prev 指向队尾）
} timeout_queue_t;

#define TQ_INIT(q, _timeout_limit, _obj_prev_offset, _obj_next_offset, _obj_time_offset) do { \
    (q)->timeout_limit = (_timeout_limit);                                                  \
    (q)->obj_prev_offset = (_obj_prev_offset);                                              \
    (q)->obj_next_offset = (_obj_next_offset);                                              \
    (q)->obj_time_offset = (_obj_time_offset);                                              \
    (q)->head = NULL;                                                                       \
} while(0)

#define TQ_INQ(q, obj)                                                                      \
    (*(void**)((uint8_t*)(obj) + (q)->obj_prev_offset) != NULL)

#define TQ_ADD(q, obj, time) do {                                                           \
    *(uint64_t*)((uint8_t*)(obj) + (q)->obj_time_offset) = (time);                          \
    *(void**)((uint8_t*)(obj) + (q)->obj_next_offset) = NULL;                               \
    if ((q)->head) {                                                                        \
        void* rear = *(void**)((uint8_t*)(q)->head + (q)->obj_prev_offset);                 \
        *(void**)((uint8_t*)(obj) + (q)->obj_prev_offset) = rear;                           \
        *(void**)((uint8_t*)(rear) + (q)->obj_next_offset) = (obj);                         \
        *(void**)((uint8_t*)(q)->head + (q)->obj_prev_offset) = (obj);                      \
    } else {                                                                                \
        (q)->head = (obj);                                                                  \
        *(void**)((uint8_t*)(obj) + (q)->obj_prev_offset) = (obj);                          \
    }                                                                                       \
} while(0)

#define TQ_RM(q, obj) do {                                                                  \
    void* prev = *(void**)((uint8_t*)(obj) + (q)->obj_prev_offset);                         \
    void* next = *(void**)((uint8_t*)(obj) + (q)->obj_next_offset);                         \
    if ((obj) == (q)->head) {                                                               \
        if (next) {                                                                         \
            (q)->head = next;                                                               \
            *(void**)((uint8_t*)(next) + (q)->obj_prev_offset) = prev;                      \
        } else (q)->head = NULL;                                                            \
    } else {                                                                                \
        *(void**)((uint8_t*)(prev) + (q)->obj_next_offset) = next;                          \
        if (next) *(void**)((uint8_t*)(next) + (q)->obj_prev_offset) = prev;                \
        else *(void**)((uint8_t*)(q)->head + (q)->obj_prev_offset) = prev;                  \
    }                                                                                       \
    *(void**)((uint8_t*)(obj) + (q)->obj_prev_offset) = NULL;                               \
    *(void**)((uint8_t*)(obj) + (q)->obj_next_offset) = NULL;                               \
} while(0)

#define TQ_RETRY(q, now, it, ...)                                                           \
    while ((it = (q)->head)) {                                                              \
        uint64_t obj_time = *(uint64_t*)((uint8_t*)(it) + (q)->obj_time_offset);            \
        if (tick_diff(now, obj_time) < (q)->timeout_limit) break;                           \
        void* next = *(void**)((uint8_t*)(it) + (q)->obj_next_offset);                      \
        if (next) {                                                                         \
            (q)->head = next;                                                               \
            *(void**)((uint8_t*)(next) + (q)->obj_prev_offset) = *(void**)((uint8_t*)(it) + (q)->obj_prev_offset); \
        } else (q)->head = NULL;                                                            \
        *(void**)((uint8_t*)(it) + (q)->obj_prev_offset) = NULL;                            \
        *(void**)((uint8_t*)(it) + (q)->obj_next_offset) = NULL;                            \
        __VA_ARGS__                                                                         \
    }

//-----------------------------------------------------------------------------

enum {
    PROTO_COMPACT,
    PROTO_RELAY,
    PROTO_WSS,                      /* Websocket Session Sync */
    PROTO_NUM
};

typedef struct session session_t;
typedef struct client_ctx client_ctx_t;

typedef struct client {
    int8_t                          proto;                          // -1=INV, 0=PROTO_COMPACT, 1=PROTO_RELAY, 2=PROTO_WSS
    char                            local_peer_id[P2P_PEER_ID_MAX+1];
    uint32_t                        instance_id;
    sock_t                          fd;
    uint64_t                        last_active;
    session_t*                      sessions;
    UT_hash_handle                  hh;
} client_t;

static inline bool client_identified(client_t* c) { return c->hh.tbl != NULL; }

typedef struct session_pair {
    char                            peer_id[2][P2P_PEER_ID_MAX];    // hh_peer 复合 key 起始（与 remote_peer_id 连续）
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
#define PEER_VALID(ps)              ((ps) && (void*)(ps) != (void*)-1)
#define PEER_ONLINE(s)              PEER_VALID(PEER(s))

#define TCP_PEER_SENDABLE(s)        TCP_SENDABLE(PEER(s)->client)
#define TCP_PEER_REACHABLE(s)       TCP_REACHABLE(PEER(s)->client)

typedef enum break_mode {
    SESS_BREAK_STOP,                // 会话及其连接关系会保留，核心场景为 client 变为 unreachable
    SESS_BREAK_CLOSE,               // 会话会被销毁，但允许向 client 返回状态数据信息，核心场景是 client（延迟）软退出
    SESS_BREAK_TERM,                // 会话会被销毁，且不会再向 client 返回状态数据信息，核心场景是 client/session （立刻）硬退出
} break_mode_e;

struct client_ctx {

    /**
     * @brief                       释放派生 client 对象
     * @details                     必填回调；派生实现最终应调用底层释放逻辑
     */
    void (*cb_free)   (client_ctx_t* ctx, client_t *c);

    /**
     * @brief                       初始化或重置 client 对象
     * @details                     可为空
     */
    bool (*cb_reset)  (client_ctx_t* ctx, client_t *c, bool init);

    /**
     * @brief                       迁移派生 client 对象状态
     * @details                     可为空，由 resident_client 调用
     */
    void (*cb_migrate)(client_ctx_t* ctx, client_t *to, client_t *from);

    /**
     * @brief                       获取或调整 client 的 active 状态
     * @details                     可为空。active 参数中，0 表示查询，正值表示激活，负值表示失活
     */
    bool (*cb_activate)(client_ctx_t* ctx, client_t *c, int active);

    /**
     * @brief                       执行 session 的终止或中断
     * @details                     如果协议创建了 session 对象，则该回调必须实现；否则可为空
     */
    void (*cb_break)  (client_ctx_t* ctx, session_t *s, session_t *ps, break_mode_e break_mode);

    /**
     * @brief                       执行 session 的销毁和资源回收
     * @details                     可为空。相比 cb_break，cb_close 关注 session 对象自身资源
     */
    void (*cb_close)  (client_ctx_t* ctx, session_t *s, bool terminate, bool clearing);

    /**
     * @brief                       在清除所有 session 前后触发
     * @details                     可为空。preOrPost 为 true 表示清除前，false 表示清除后
     */
    void (*cb_clear)  (client_ctx_t* ctx, client_t *c, bool preOrPost);
};

/**
 * @brief                           释放 client
 * @param c
 * @return                          无
 */
void
free_client(client_t *c);

/**
 * @brief                           分配一个指定协议类型的 client 对象
 * @param proto
 * @param fd
 * @return                          新分配的 client；失败时返回 NULL
 */
client_t*
alloc_client(uint8_t proto, sock_t fd);

/**
 * @brief                           尝试恢复之前已经存在的 client。如果失败则将之前 client free，并将其初始化为新的 client
 * @param c                         已存在的目标 client
 * @param proto                     当前请求的 proto
 * @param fd                        当前请求的 fd
 * @param instance_id               当前请求的 instance_id
 * @return                          <0: 初始化为新的 client 失败(当前 client 无效); 0: 初始化为新的 client; 1: 成功恢复为之前的 client
 */
int
restore_client_as(client_t* c, int8_t proto, sock_t fd, uint32_t instance_id);

/**
 * @brief                           尝试从之前已经存在的 client 恢复，并将之前的 client free
 * @param c                         当前请求的新的 client
 * @param from                      之前已经存在的 client
 * @return                          是否成功
 */

bool
restore_client_from(client_t* c, client_t* from);

/**
 * @brief                           通过 peer_id 查找 client
 * @param peer_id
 * @return                          找到则返回 client，否则返回 NULL
 */
client_t*
find_client(const char *peer_id);

/**
 * @brief                           注册 client 的 local_peer_id，并加入全局索引
 * @param c
 * @param peer_id
 * @return                          是否成功
 */
bool
identify_client(client_t* c, const char peer_id[P2P_PEER_ID_MAX]);

/**
 * @brief                           调整 client 的活跃状态，并触发 cb_activate 回调
 * @param c
 * @param active
 * @return                          当前或调整后的活跃状态
 */
bool
activate_client(client_t* c, int active);

//-----------------------------------------------------------------------------

/**
 * @brief                           释放 session
 * @param s
 * @param terminate                 true 表示硬终止，此时 session 中未发完的数据不再保证完整
 * @return                          无
 */
void
free_session(session_t *s, bool terminate);

/**
 * @brief                           释放 client 的所有 session
 * @param c
 * @param terminate
 * @return                          无
 */
void
clear_sessions(client_t *c, bool terminate);

/**
 * @brief                           创建一个单端会话
 * @param client
 * @param local_s
 * @param session_type_size
 * @param init
 * @return                          0 表示成功，其他值表示失败
 */
ret_t
solo_session(client_t *client, session_t **local_s,
             size_t session_type_size, void(*init)(session_t* session));

/**
 * @brief                           创建或加入一个与远端配对的会话
 * @param c
 * @param remote_peer_id
 * @param local_s                   始终返回本端会话对象
 * @param remote_s                  对端在线时返回其会话对象，否则返回 NULL
 * @param session_type_size
 * @param init
 * @return
 *  <0                             错误码
 *  =0                             local_s 位于 pair 的 left side
 *  =1                             local_s 位于 pair 的 right side
 */
ret_t
pair_session(client_t *c, const char *remote_peer_id,
             session_t **local_s, session_t **remote_s,
             size_t session_type_size, void(*init)(session_t* session));

/**
 * @brief                           通过 session_id 查找会话对象
 * @param session_id
 * @return                          找到则返回 session，否则返回 NULL
 */
session_t*
find_session(uint32_t session_id);

//-----------------------------------------------------------------------------

/**
 * @brief                           TCP client 扩展字段
 * @details                         handshake 中，0 表示握手完成，正值表示握手中，负值表示 closing
 */
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

#define TCP_SENDABLE(c)             (((tcp_client_t*)c)->handshake == 0)
#define TCP_REACHABLE(c)            (((tcp_client_t*)c)->io & TCP_IO_FLAG_WANT_READ)

//-----------------------------------------------------------------------------

/**
 * @brief                           通过 UDP 套接字发送数据
 * @param udp_fd
 * @param buf
 * @param len
 * @param to
 * @param PROTO
 * @return                          发送结果
 */
ssize_t
udp_send(sock_t udp_fd, const void *buf, int len, const struct sockaddr_in *to, const char *PROTO);

/**
 * @brief                           通过 TCP client 发送数据
 * @param client
 * @param buf
 * @param w_sz
 * @param SP
 * @return                          发送结果
 */
int
tcp_send(tcp_client_t* client, const void *buf, size_t *w_sz, const char *SP);

/**
 * @brief                           通过 TCP client 接收数据
 * @param client
 * @param buf
 * @param r_sz
 * @param SP                        子协议名；为 NULL 时默认显示为 "TCP"
 * @return                          接收结果
 */
int
tcp_recv(tcp_client_t* client, void *buf, size_t *r_sz, const char * SP);

///////////////////////////////////////////////////////////////////////////////

#endif //P2P_TYPE_H
