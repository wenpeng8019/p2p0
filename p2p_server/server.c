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
ARGS_I(false, port,       'p', "port",       LA_CS("Signaling server listen port (TCP+UDP)", LA_S9, 9));
ARGS_I(false, probe_port, 'P', "probe-port", LA_CS("NAT type detection port (0=disabled)", LA_S6, 6));
ARGS_B(false, relay,      'r', "relay",      LA_CS("Enable data relay support (COMPACT mode fallback)", LA_S4, 4));
ARGS_B(false, msg,        'm', "msg",        LA_CS("Enable MSG RPC support", LA_S5, 5));

static void cb_cn(const char* argv) { (void)argv;  lang_cn(); }
ARGS_PRE(cb_cn, cn,         0,   "cn",       LA_CS("Use Chinese language", LA_S10, 10));

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
#define MAX_CANDIDATES_BY_PAYLOAD       ((P2P_MAX_PAYLOAD - (2 * P2P_PEER_ID_MAX + P2P_SESS_ID_PSZ + 1)) / sizeof(p2p_candidate_t))
#define MAX_CANDIDATES                  ((MAX_CANDIDATES_CONFIG) < (MAX_CANDIDATES_BY_PAYLOAD) ? (MAX_CANDIDATES_CONFIG) : (MAX_CANDIDATES_BY_PAYLOAD))

// COMPACT 模式配对超时时间（秒）
// 客户端在 REGISTERED 状态每 20 秒发一次 keepalive REGISTER，此值取 3 倍间隔
#define COMPACT_PAIR_TIMEOUT_S          90

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT_S          60

#define COMPACT_RETRY_INTERVAL_MS       1000    // COMPACT 模式重传检查间隔（毫秒）

// COMPACT 模式 SYNC 重传参数
#define SYNC0_RETRY_INTERVAL_MS         2000    // 重传间隔（毫秒）
#define SYNC0_MAX_RETRY                 5       // 最大重传次数

// COMPACT 模式 MSG RPC 重传参数
#define MSG_RPC_RETRY_INTERVAL_MS       1000    // MSG RPC 统一重传间隔（毫秒）
#define MSG_REQ_MAX_RETRY               5       // MSG_REQ 最大重传次数
#define MSG_RESP_MAX_RETRY              10      // MSG_RESP 最大重传次数（比 REQ 更多，确保 A 端收到）

// RELAY 模式 SYNC 参数（TCP 保证可靠传输，无需应用层重传）
#define RELAY_SYNC_CANDS_PER_PACKET     10      // 每包最大候选数

typedef struct session session_t;

typedef struct client {
    bool                            valid;
    char                            local_peer_id[P2P_PEER_ID_MAX];
    uint32_t                        instance_id;
    uint64_t                        last_active;
    session_t*                      sessions;
} client_t;

typedef struct session_pair {
    bool                            valid;
    char                            peer_id[2][P2P_PEER_ID_MAX];   // hh_peer 复合 key 起始（与 remote_peer_id 连续）
    session_t*                      sessions[2];                   // 双端会话指针
    UT_hash_handle                  hh_peer;
} session_pair_t;

struct session {
    struct client*                  client;
    struct session*                 prev;
    struct session*                 next;
    session_pair_t*                 pair;
    uint32_t                        session_id;
    UT_hash_handle                  hh_session;
};

static session_t*                   g_sessions = NULL;
static session_pair_t*              g_session_pairs = NULL;

#pragma pack(push, 1)
typedef struct buffer_item {
    struct buffer_item*             next;
    void*                           refer;
    uint8_t                         flags;
} buffer_item_t;
#pragma pack(pop)
#define ITEM2BUF(item)              ((uint8_t*)(item + 1))
#define BUF2ITEM(buf)               (((buffer_item_t*)(buf)) - 1)

//-----------------------------------------------------------------------------

typedef struct relay_session {
    session_t                       base;
    struct relay_session*           peer;
    
    /* 向对端发送的待处理队列 */
    buffer_item_t*                  peer_pending;               // 由对端主动来取，用于控制发送节奏
                                                                // + 即对端的发送队列最多只有来自本端的一个发送项
                                                                //   当对端发送完来自本端的项后，会来此继续取下一项
                                                                // ! 该值可以为 -1, 表示最后一个数据包正在对端的发送队列中

    /* MSG RPC 忙标志（独立于 peer_pending 的并行通道）*/
    uint16_t                        rpc_pending_sid;            // RPC 生命周期锁：0=空闲，非0=进行中的 RPC sid
                                                                // 全程：REQ→转发→RESP→转发回来才解锁
                                                                // RESP 返回时验证 sid 一致性
    uint64_t                        rpc_sent_time;              // RPC 发起时间戳（毫秒，用于超时检测）
    struct relay_session*           rpc_pending_next;           // RPC 待确认链表指针（NULL=不在链表中，-1=链表尾）
    
    /* 本地发送队列 */
    buffer_item_t*                  send_head;
    buffer_item_t*                  send_rear;
    struct relay_session*           send_prev;
    struct relay_session*           send_next;
} relay_session_t;

// RELAY 模式客户端（TCP 长连接）- 统一接收通道
typedef struct relay_client {
    client_t                        base;
    sock_t                          fd;
    
    bool                            online_ack_pending;         // ONLINE_ACK 待发送标志（复用 recv_buf）
    
    uint8_t*                        recv_buf;
    uint16_t                        recv_len;
    
    relay_session_t*                sending_head;
    relay_session_t*                sending_rear;
    uint16_t                        send_offset;
} relay_client_t;

static relay_client_t               g_relay_clients[MAX_PEERS];
static buffer_item_t*               g_relay_recycle = NULL;
static buffer_item_t*               g_relay_recycleS = NULL;

// RELAY RPC 待确认链表（按 rpc_sent_time 排序，队头最早超时）
static relay_session_t*             g_relay_rpc_pending_head = NULL;
static relay_session_t*             g_relay_rpc_pending_rear = NULL;

#define RELAY_FRAME_SIZE            (sizeof(p2p_relay_hdr_t) + P2P_MAX_PAYLOAD)
#define RELAY_SMALL_FRAME_SIZE      (sizeof(p2p_relay_hdr_t) + P2P_MAX_PAYLOAD / 4)

#define RELAY_BUF_FLAGS_SMALL       0x01    // 小包标志（提示服务器优先发送，减少延迟）
#define RELAY_BUF_FLAGS_SYNC_FIN    0x02    // SYNC 包尾部 FIN 标记（告知服务器这是最后一包候选）

//-----------------------------------------------------------------------------

// COMPACT 模式配对记录（UDP 无状态）
/* 注意：COMPACT 模式采用"配对缓存"机制：
 *   A 注册 (local=alice, remote=bob, candidates=[...])
 *   B 注册 (local=bob, remote=alice, candidates=[...])
 *   服务器检测到双向匹配后，同时向 A 和 B 发送对方的候选列表
 */

typedef struct compact_session {
    session_t                       base;
    struct compact_session*         peer;

    uint8_t                         addr_notify_seq;            // 发给对端的地址变更通知序号（base_index，1..255 循环）

    p2p_candidate_t                 candidates[MAX_CANDIDATES]; // 候选列表（网络格式，直接收发）
    int                             candidate_count;            // 候选数量

    // SYNC(seq=0) 可靠传输（首包 + 地址变更通知）
    int                             sync0_acked;                // 是否已收到首包 ACK，-1 表示未收到确认但已放弃
    struct compact_session*         sync0_pending_next;         // 待确认链表指针（-1 表示链表最后一个）
    uint64_t                        sync0_sent_time;            // 当前待确认 seq=0 最近发送时间（毫秒）
    int                             sync0_retry;                // 当前待确认 seq=0 重传次数
    uint8_t                         sync0_base_index;           // 当前待确认 seq=0 的 base_index（0=首包，!=0 地址变更通知）

    // MSG RPC（请求-响应机制，共用字段存储两个阶段的数据）
    uint16_t                        rpc_last_sid;               // 最后一次完成或正在执行的 RPC 序列号（0=未使用）
    struct compact_session*         rpc_pending_next;           // RPC 待确认链表指针（NULL=空闲，-1=链表尾）
    uint64_t                        rpc_sent_time;              // 最后发送时间（毫秒）
    int                             rpc_retry;                  // 重传次数
    bool                            rpc_responding;             // RPC 阶段（false=REQ等待对端，true=RESP等待确认）
    uint8_t                         rpc_code;                   // RPC 消息类型/响应码（REQ阶段=消息类型，RESP阶段=响应码）
    uint8_t                         rpc_flags;                  // RPC flags（RESP 阶段使用：PEER_OFFLINE/TIMEOUT）
    uint8_t                         rpc_data[P2P_MSG_DATA_MAX]; // RPC 数据缓冲区
    int                             rpc_data_len;               // RPC 数据长度

} compact_session_t;

typedef struct compact_client {
    client_t                        base;

    struct sockaddr_in              addr;                       // 公网地址（UDP 源地址）
    uint64_t                        auth_key;                   // client↔server 认证令牌（ONLINE_ACK 分配，OFFLINE/ALIVE/SYNC0 鉴权用）

    UT_hash_handle                  hh_client;                  // 按 auth_key 索引（client↔server 鉴权查找）
} compact_client_t;

// COMPACT 模式客户端数组和按 auth_key 查找的哈希表
static compact_client_t             g_compact_clients[MAX_PEERS];
static compact_client_t*            g_compact_clients_by_auth = NULL;

// SYNC(seq=0) 待确认链表（仅包含已发送首包但未收到 ACK 的配对）
static compact_session_t*           g_compact_sync0_pending_head = NULL;
static compact_session_t*           g_compact_sync0_pending_rear = NULL;

// MSG RPC 待确认链表（统一管理 REQ 和 RESP 阶段，通过 rpc_responding 区分）
static compact_session_t*           g_compact_rpc_pending_head = NULL;
static compact_session_t*           g_compact_rpc_pending_rear = NULL;

#define PEER_ONLINE(s)      ((s)->peer && (s)->peer != (compact_session_t*)(void*)-1)  // 判断对端是否在线（peer 指针为 (void*)-1 表示已断开）
#define PEER_OF(s)          (PEER_ONLINE(s) ? (s)->peer : NULL)

//-----------------------------------------------------------------------------

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t g_running = 1;

///////////////////////////////////////////////////////////////////////////////

// 生成安全的随机 session_id（32位，加密安全，防止跨会话注入攻击）
static uint32_t generate_session_id(void) {
    uint32_t id;
    session_t *existing;
    int attempts = 0;
    
    // 使用循环代替递归，避免极端情况下的栈溢出
    do {
        id = P_rand32();  // 使用 stdc.h 统一封装的加密安全随机数
        HASH_FIND(hh_session, g_sessions, &id, sizeof(uint32_t), existing);
        
        // 安全限制：虽然冲突概率极低（1/2^32），但在极端情况下提供保护
        if (++attempts > 1000) {
            print("F:", "Cannot generate unique session_id after 1000 attempts\n");
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
    HASH_FIND(hh_peer, g_session_pairs, peer_key, 2 * P2P_PEER_ID_MAX, pair);
    if (!pair) {
        strncpy(peer_key + 2 * P2P_PEER_ID_MAX, client->local_peer_id, P2P_PEER_ID_MAX);
        HASH_FIND(hh_peer, g_session_pairs, peer_key + P2P_PEER_ID_MAX, 2 * P2P_PEER_ID_MAX, pair);
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
        HASH_ADD_KEYPTR(hh_peer, g_session_pairs, pair->peer_id, 2 * P2P_PEER_ID_MAX, pair);

        side = 0;
        opposite_s = NULL;
    }
    else if (pair->sessions[0]) { assert(!pair->sessions[1]);

        if (pair->sessions[0]->client == client) {
            print("E:", LA_F("Duplicate session create blocked: '%s' -> '%s'\n", LA_F80, 80),
                    client->local_peer_id, remote_peer_id);
            free(s);
            return -1;
        }
        side = 1;
        opposite_s = pair->sessions[0];
    }
    else { assert(pair->sessions[1]);

        if (pair->sessions[1]->client == client) {
            print("E:", LA_F("Duplicate session create blocked: '%s' -> '%s'\n", LA_F80, 80),
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

    s->session_id = generate_session_id();
    HASH_ADD(hh_session, g_sessions, session_id, sizeof(uint32_t), s);
    
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
            HASH_DELETE(hh_peer, g_session_pairs, pair);
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
            print("E:", LA_F("send(%s) failed: errno=%d\n", LA_F148, 148), reason, P_sock_errno());
            return -2;
        }
        if (n == 0) {
            print("I:", LA_F("Client closed connection (EOF on send, reason=%s)\n", LA_F78, 78), reason);
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
            print("E:", LA_F("recv() failed: errno=%d\n", LA_F146, 146), P_sock_errno());
            return -2;
        }
        if (n == 0) {
            print("I:", LA_F("% Client closed connection (EOF on recv)\n", LA_F11, 11));
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
    else if (!((item = (buffer_item_t*)malloc(sizeof(buffer_item_t) + capacity)))) return NULL;
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

// forward declaration（retry_relay_rpc_pending 需要调用）
static void relay_session_send_rpc_error(relay_session_t *s, uint16_t sid, uint8_t code);

// 将 session 加入 RELAY RPC 待确认链表尾部
static inline void enqueue_relay_rpc_pending(relay_session_t *s) {
    s->rpc_pending_next = (relay_session_t*)(void*)-1;
    if (g_relay_rpc_pending_rear) {
        g_relay_rpc_pending_rear->rpc_pending_next = s;
        g_relay_rpc_pending_rear = s;
    } else {
        g_relay_rpc_pending_head = s;
        g_relay_rpc_pending_rear = s;
    }
}

// 从 RELAY RPC 待确认链表移除
static void remove_relay_rpc_pending(relay_session_t *s) {
    if (!g_relay_rpc_pending_head || !s->rpc_pending_next) return;

    if (g_relay_rpc_pending_head == s) {
        g_relay_rpc_pending_head = s->rpc_pending_next;
        s->rpc_pending_next = NULL;
        if (g_relay_rpc_pending_head == (void*)-1) {
            g_relay_rpc_pending_head = NULL;
            g_relay_rpc_pending_rear = NULL;
        }
        return;
    }

    relay_session_t *prev = g_relay_rpc_pending_head;
    while (prev->rpc_pending_next != s) {
        if (prev->rpc_pending_next == (void*)-1) return;
        prev = prev->rpc_pending_next;
    }
    prev->rpc_pending_next = s->rpc_pending_next;
    if (s->rpc_pending_next == (void*)-1) {
        g_relay_rpc_pending_rear = prev;
    }
    s->rpc_pending_next = NULL;
}

// 检查 RELAY RPC 超时（队列按时间排序，未超时即短路返回）
static void retry_relay_rpc_pending(uint64_t now) {
    if (!g_relay_rpc_pending_head) return;

    for (;;) {
        relay_session_t *s = g_relay_rpc_pending_head;

        // 队列按时间排序，未超时即全部未超时
        if (tick_diff(now, s->rpc_sent_time) < MSG_REQ_MAX_RETRY * MSG_RPC_RETRY_INTERVAL_MS) return;

        // 移除队头
        g_relay_rpc_pending_head = s->rpc_pending_next;
        if (g_relay_rpc_pending_head == (void*)-1) {
            g_relay_rpc_pending_head = NULL;
            g_relay_rpc_pending_rear = NULL;
        }
        s->rpc_pending_next = NULL;

        // 向请求方发送超时错误 RESP
        uint16_t sid = s->rpc_pending_sid;
        s->rpc_pending_sid = 0;

        print("W:", "RELAY RPC timeout: sid=%u (ses_id=%" PRIu32 ")\n", sid, s->base.session_id);
        relay_session_send_rpc_error(s, sid, P2P_MSG_ERR_TIMEOUT);

        if (!g_relay_rpc_pending_head) return;
    }
}

static void relay_free_session(relay_session_t *s) {

    if (s->peer) {
        relay_session_t *peer = s->peer; s->peer = NULL;
        peer->peer = NULL;

        // 通知对端会话结束（对应 COMPACT 的 compact_free_session 行为）
        // 直接写入 TCP socket 而非入 send queue：入队后立刻被 relay_free_session(peer) 清掉会导致 FIN 丢失
        relay_client_t *peer_client = (relay_client_t *)peer->base.client;
        if (peer_client && peer_client->fd != P_INVALID_SOCKET) {
            uint8_t fin_buf[sizeof(p2p_relay_hdr_t) + P2P_RLY_FIN_PSZ];
            p2p_relay_hdr_t *fhdr = (p2p_relay_hdr_t *)fin_buf;
            fhdr->type = P2P_RLY_FIN;
            fhdr->size = htons(P2P_RLY_FIN_PSZ);
            nwrite_l(fin_buf + sizeof(*fhdr), peer->base.session_id);
            size_t flen = sizeof(fin_buf);
            tcp_send(peer_client, fin_buf, &flen, "FIN");  /* best-effort，尽力发送 */
        }

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

    // 从 RPC 待确认链表移除并清除忙标志
    if (s->rpc_pending_sid) {
        remove_relay_rpc_pending(s);
        s->rpc_pending_sid = 0;
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

static void relay_send_error(relay_client_t *client, uint8_t req_type, uint8_t status_code) {

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

static void relay_session_send_sync0_ack(relay_session_t *s, uint8_t online) {

    assert(s && s->base.session_id && s->base.client);
    relay_client_t *client = (relay_client_t*)s->base.client;

    uint16_t payload_len = P2P_RLY_SYNC0_ACK_PSZ;
    buffer_item_t *buf_item = relay_buf_alloc(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("W:", LA_F("SYNC0_ACK queue busy for '%s', drop\n", LA_F105, 105), client->base.local_peer_id);
        return;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC0_ACK;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_l(payload, s->base.session_id);

    payload[P2P_SESS_ID_PSZ] = (uint8_t)(online ? 1 : 0);

    relay_session_send(s, buf_item);
}

static void relay_session_send_sync_ack(relay_session_t *s, uint8_t confirmed_count) {

    assert(s && s->base.session_id && s->base.client);
    relay_client_t *client = (relay_client_t*)s->base.client;

    uint16_t payload_len = P2P_RLY_SYNC_ACK_PSZ;
    buffer_item_t *buf_item = relay_buf_alloc(sizeof(p2p_relay_hdr_t) + payload_len);
    if (!buf_item) {
        print("W:", LA_F("SYNC_ACK queue busy for '%s', drop\n", LA_F106, 106), client->base.local_peer_id);
        return;
    }

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_SYNC_ACK;
    hdr->size = htons(payload_len);
    uint8_t *payload = (uint8_t*)(hdr + 1);
    nwrite_l(payload, s->base.session_id);

    payload[P2P_SESS_ID_PSZ] = confirmed_count;

    relay_session_send(s, buf_item);
}

static void relay_session_send_status(relay_session_t *s, uint8_t req_type, uint8_t status_code) {

    assert(s && s->base.client);

    buffer_item_t *buf_item = relay_buf_alloc(sizeof(p2p_relay_hdr_t) + P2P_RLY_STATUS_PSZ);
    if (!buf_item) return;

    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_STATUS;
    hdr->size = htons(P2P_RLY_STATUS_PSZ);

    uint8_t *payload = (uint8_t *)(hdr + 1);
    payload[0] = req_type;
    payload[1] = status_code;

    relay_session_send(s, buf_item);
}

// 向 RPC 请求方发送 RPC 错误响应
// payload: [session_id(8)][sid(2)][code(1)]
static void relay_session_send_rpc_error(relay_session_t *s, uint16_t sid, uint8_t code) {

    buffer_item_t *buf_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
    if (!buf_item) {
        print("E:", LA_F("RPC_ERR: OOM\n", LA_F99, 99));
        return;
    }

    uint16_t payload_len = P2P_RLY_RESP_MIN_PSZ;
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)ITEM2BUF(buf_item);
    hdr->type = P2P_RLY_RESP;
    hdr->size = htons(payload_len);

    uint8_t *p = (uint8_t *)(hdr + 1);
    nwrite_l(p, s->base.session_id);
    nwrite_s(p + P2P_SESS_ID_PSZ, sid);
    p[P2P_SESS_ID_PSZ + 2] = code;

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
            uint8_t candidate_count = ((uint8_t *)(p_hdr + 1))[P2P_SESS_ID_PSZ];
            if (candidate_count)
                relay_session_send_sync_ack(s, candidate_count);
        }
        else if (p_hdr->type == P2P_RLY_DATA) {
            relay_session_send_status(s, p_hdr->type, P2P_RLY_CODE_READY);
        }
    }

}

//-----------------------------------------------------------------------------

// 处理 SYNC0 消息（首次同步）
// payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
static void handle_relay_sync0(relay_client_t *client, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC0";

    if (len < P2P_PEER_ID_MAX + 1) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F40, 40), PROTO, len);
        return;
    }
    uint8_t cand_count = payload[P2P_PEER_ID_MAX];
    uint32_t expect_len = P2P_RLY_SYNC0_PSZ(cand_count);
    if (len != expect_len) {
        print("E:", LA_F("%s: bad payload(cnt=%d, len=%u, expected=%u)\n", LA_F38, 38),
               PROTO, cand_count, len, expect_len);
        return;
    }

    payload[P2P_PEER_ID_MAX - 1] = '\0';
    
    // build_session 返回：side (>=0: side=0|1, <0: error), local_s, remote_s
    relay_session_t *local_s = NULL, *remote_s = NULL;
    int side = build_session(&client->base, (const char *)payload, (session_t**)&local_s, (session_t**)&remote_s, sizeof(relay_session_t));
    if (side < 0) {
        print("E:", LA_F("%s: build_session failed for '%s'\n", LA_F43, 43), PROTO, (const char *)payload);
        return;
    }

    // 截断候选计数
    if (cand_count > MAX_CANDIDATES) 
        cand_count = MAX_CANDIDATES;

    print("V:", LA_F("%s: local='%s', remote='%s', side=%d, peer_online=%d, cands=%d\n", LA_F54, 54),
           PROTO, client->base.local_peer_id, (const char *)payload, side, remote_s ? 1 : 0, cand_count);

    // 立即返回 SYNC0_ACK（会话建立确认）
    // + SYNC0_ACK 告知会话建立结果：session_id + 对端在线状态
    relay_session_send_sync0_ack(local_s, remote_s && ((relay_client_t*)remote_s->base.client)->fd != P_INVALID_SOCKET);

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync0_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync0_item = relay_buf_alloc(RELAY_FRAME_SIZE);
        if (!sync0_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
            return;
        }

        // 标记该包是为了零拷贝转发而构造的，后续处理时会区分对待
        ((p2p_relay_hdr_t *)client->recv_buf)->size = 0;

        // 构造零拷贝转发的 SYNC0 包
        hdr = (p2p_relay_hdr_t *)(client->recv_buf + P2P_PEER_ID_MAX - P2P_SESS_ID_PSZ);
        hdr->size = P2P_RLY_SYNC_PSZ(cand_count, false);

        buffer_item_t* item = BUF2ITEM(client->recv_buf);
        client->recv_buf = ITEM2BUF(sync0_item);
        client->recv_len = 0;
        sync0_item = item;
    }
    else {

        sync0_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
        if (!sync0_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
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
        nwrite_l(sid, remote_s->base.session_id);
        
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
            if (hdr->size == 0) cached_sid += P2P_PEER_ID_MAX - P2P_SESS_ID_PSZ;
            nwrite_l(cached_sid, local_s->base.session_id);

            // 添加到本端发送队列，也要设置 refer
            remote_sync0_item->refer = remote_s;
            remote_s->peer_pending = (buffer_item_t*)-1;
            relay_session_send(local_s, remote_sync0_item);

            // 如果对端 sync0 发送的同步数据，发送 SYNC_ACK 告知对端同步数据已（确认）转发
            if (hdr->size == 0) {
                uint8_t remote_cand_count = cached_sid[P2P_SESS_ID_PSZ];
                relay_session_send_sync_ack(remote_s, remote_cand_count);
            }
        }

        //-------

        // 如果本端 sync0 发送的同步数据，发送 SYNC_ACK 告知本端同步数据已（确认）转发
        if (cand_count) relay_session_send_sync_ack(local_s, cand_count);

        print("I:", LA_F("%s: forwarded to peer, cands=%d\n", LA_F47, 47), PROTO, cand_count);
    }
    // 对端未在线，将 sync0 包缓存到 local_s->peer_pending
    else {
        
        if (local_s->peer_pending) {
            relay_buf_free(local_s->peer_pending);
        }
        local_s->peer_pending = sync0_item;

        print("W:", LA_F("%s: peer '%s' offline, cached cands=%d\n", LA_F62, 62), 
               PROTO, (const char *)payload, cand_count);
    }
}

// 处理 SYNC 消息（候选同步）
static void handle_relay_sync(relay_client_t *client, relay_session_t *s, uint8_t *payload, uint16_t len) {
    const char *PROTO = "SYNC";

    if (len < P2P_RLY_SYNC_PSZ(0, false)) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F40, 40), PROTO, len);
        return;
    }

    uint8_t cand_count = payload[P2P_SESS_ID_PSZ];
    uint16_t payload_sz = P2P_RLY_SYNC_PSZ(cand_count, false);
    if (len == payload_sz + 1u) {

        if (payload[payload_sz] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F37, 37), PROTO, payload[payload_sz]);
            return;
        }
    }
    else if (len != payload_sz) {

        print("E:", LA_F("%s: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n", LA_F39, 39),
               PROTO, (unsigned)cand_count, len, payload_sz);

        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    buffer_item_t *sync_item; p2p_relay_hdr_t *hdr;
    if (cand_count) {

        sync_item = relay_buf_alloc(RELAY_FRAME_SIZE);
        if (!sync_item) {
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
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
            print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
            return;
        }

        hdr = (p2p_relay_hdr_t *)(ITEM2BUF(sync_item));
        hdr->type = P2P_RLY_SYNC;
        hdr->size = P2P_RLY_SYNC_PSZ(0, true);
        hdr->size = htons(hdr->size);

        payload = (uint8_t*)(hdr+1) + P2P_SESS_ID_PSZ;
        payload[0] = 0;
        payload[1] = P2P_RLY_SYNC_FIN_MARKER;
    }

    // 交换写入对端的 session_id
    uint8_t *sid_ptr = (uint8_t *)(hdr + 1);
    nwrite_l(sid_ptr, s->peer->base.session_id);

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
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F40, 40), PROTO, len);
        return;
    }

    print("I:", LA_F("%s: close ses_id=%" PRIu32 "\n", LA_F44, 44), PROTO, s->base.session_id);

    // 先断开 peer 引用，防止 relay_free_session(s) 递归释放 peer session
    // peer session 保持存活，FIN 通过其发送队列可靠送达对端
    relay_session_t *peer = s->peer;
    s->peer = NULL;
    if (peer) peer->peer = NULL;

    if (peer) {

        buffer_item_t *buf_item = relay_buf_alloc(RELAY_SMALL_FRAME_SIZE);
        if (!buf_item) {
            print("E:", LA_F("%s: OOM for relay buffer\n", LA_F26, 26), PROTO);
            relay_free_session(s);
            return;
        }

        // 如果对端还有待转发的数据，先发送对端的数据，再发送 FIN，确保数据完整性
        if (s->peer_pending && s->peer_pending != ((buffer_item_t *)-1)) {
            relay_session_send(peer, s->peer_pending);
        }
        s->peer_pending = NULL;

        p2p_relay_hdr_t* hdr = (p2p_relay_hdr_t *)(ITEM2BUF(buf_item));
        hdr->type = P2P_RLY_FIN;
        hdr->size = htons(P2P_RLY_FIN_PSZ);
        payload = (uint8_t*)(hdr+1);
        nwrite_l(payload, peer->base.session_id);

        relay_session_send(peer, buf_item);
    }

    relay_free_session(s);  // s->peer == NULL，不会递归释放 peer session
}

// 处理 DATA 消息（零拷贝转发）
static void handle_relay_data(relay_client_t *client, relay_session_t *s, uint8_t *payload, uint16_t len) {
    const char *PROTO = "DATA";

    if (len < P2P_SESS_ID_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F40, 40), PROTO, len);
        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发：分配新 recv_buf，将当前 recv_buf 直接作为转发包
    buffer_item_t *new_recv = relay_buf_alloc(RELAY_FRAME_SIZE);
    if (!new_recv) {
        print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
        relay_session_send_status(s, P2P_RLY_DATA, P2P_RLY_ERR_INTERNAL);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, s->peer->base.session_id);

    if (s->peer_pending) {
        assert(s->peer_pending == (buffer_item_t *)-1);
        s->peer_pending = buf_item;
    }
    else {
        s->peer_pending = buf_item;
        relay_session_send_complete(s, NULL);
    }
}

// 处理 MSG_REQ 消息（零拷贝转发）
// payload: [session_id(8)][sid(2)][msg(1)][data(N)]
static void handle_relay_req(relay_client_t *client, relay_session_t *s, uint8_t *payload, uint16_t len) {
    const char *PROTO = "MSG_REQ";

    if (len < P2P_RLY_REQ_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F40, 40), PROTO, len);
        return;
    }

    uint16_t sid = nget_s(payload + P2P_SESS_ID_PSZ);
    uint8_t  msg = payload[P2P_SESS_ID_PSZ + 2];
    int data_len = (int)len - (int)P2P_RLY_REQ_MIN_PSZ;

    print("V:", LA_F("%s: '%s' sid=%u msg=%u data_len=%d\n", LA_F25, 25),
          PROTO, client->base.local_peer_id, sid, msg, data_len);

    // 检查对端是否在线
    if (!s->peer || !s->peer->base.client
     || ((relay_client_t*)s->peer->base.client)->fd == P_INVALID_SOCKET) {
        print("W:", LA_F("%s: peer offline, sending error resp\n", LA_F63, 63), PROTO);
        relay_session_send_rpc_error(s, sid, P2P_MSG_ERR_PEER_OFFLINE);
        return;
    }

    // rpc_pending_sid 忙检查
    if (s->rpc_pending_sid) {
        print("W:", LA_F("%s: rpc busy (pending sid=%u)\n", LA_F66, 66), PROTO, s->rpc_pending_sid);
        relay_session_send_status(s, P2P_RLY_REQ, P2P_RLY_ERR_BUSY);
        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发：分配新 recv_buf，将当前 recv_buf 直接作为转发包
    buffer_item_t *new_recv = relay_buf_alloc(RELAY_FRAME_SIZE);
    if (!new_recv) {
        print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
        relay_session_send_status(s, P2P_RLY_REQ, P2P_RLY_ERR_INTERNAL);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为对端的 session_id
    nwrite_l(payload, s->peer->base.session_id);

    // 转发 REQ 到对端，记录 pending sid（等 RESP 回来才解锁）
    buf_item->refer = NULL;
    s->rpc_pending_sid = sid;
    s->rpc_sent_time = P_tick_ms();
    enqueue_relay_rpc_pending(s);
    relay_session_send(s->peer, buf_item);
}

// 处理 MSG_RESP 消息（零拷贝转发）
// payload: [session_id(8)][sid(2)][code(1)][data(N)]
static void handle_relay_resp(relay_client_t *client, relay_session_t *s, uint8_t *payload, uint16_t len) {
    const char *PROTO = "MSG_RESP";

    if (len < P2P_RLY_RESP_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%u)\n", LA_F40, 40), PROTO, len);
        return;
    }

    uint16_t sid  = nget_s(payload + P2P_SESS_ID_PSZ);
    uint8_t  code = payload[P2P_SESS_ID_PSZ + 2];
    int data_len  = (int)len - (int)P2P_RLY_RESP_MIN_PSZ;

    print("V:", LA_F("%s: '%s' sid=%u code=%u data_len=%d\n", LA_F24, 24),
          PROTO, client->base.local_peer_id, sid, code, data_len);

    // 检查对端（请求方）是否在线
    if (!s->peer || !s->peer->base.client
     || ((relay_client_t*)s->peer->base.client)->fd == P_INVALID_SOCKET) {
        print("W:", LA_F("%s: requester offline, discarding\n", LA_F65, 65), PROTO);
        return;
    }

    // 验证 sid 与请求方 pending sid 一致
    if (s->peer->rpc_pending_sid != sid) {
        print("W:", LA_F("%s: sid mismatch (got=%u, pending=%u), discarding\n", LA_F67, 67),
              PROTO, sid, s->peer->rpc_pending_sid);
        return;
    }

    assert(client->recv_buf && payload == client->recv_buf + sizeof(p2p_relay_hdr_t));

    // 零拷贝转发
    buffer_item_t *new_recv = relay_buf_alloc(RELAY_FRAME_SIZE);
    if (!new_recv) {
        print("E:", LA_F("%s: OOM for zero-copy recv buffer\n", LA_F27, 27), PROTO);
        relay_session_send_status(s, P2P_RLY_RESP, P2P_RLY_ERR_INTERNAL);
        return;
    }

    buffer_item_t *buf_item = BUF2ITEM(client->recv_buf);
    client->recv_buf = ITEM2BUF(new_recv);
    client->recv_len = 0;

    // 就地重写 session_id 为请求方的 session_id
    nwrite_l(payload, s->peer->base.session_id);

    // 转发 RESP 到请求方，解锁 rpc_pending_sid（RPC 生命周期完成）
    buf_item->refer = NULL;
    remove_relay_rpc_pending(s->peer);
    s->peer->rpc_pending_sid = 0;
    relay_session_send(s->peer, buf_item);
}

//-----------------------------------------------------------------------------

// 处理 RELAY 模式信令（TCP 长连接）- 统一接收+分发架构
// 架构：client 统一接收完整消息到 recv_buf，解析后分发给对应的处理函数
// 流程：read header → read full payload → dispatch → reset buffer
static void handle_relay_signaling(int idx) {
    relay_client_t *client = &g_relay_clients[idx]; 
    assert(client->recv_buf);

    client->base.last_active = P_tick_ms();
    for(;;) {

        // 状态检查：ONLINE_ACK 未完成前不应接收新消息
        // + 此时 recv_buf 已被 ONLINE_ACK 复用 send_buf，接收新消息会覆盖未发送的 ACK 内容
        if (client->online_ack_pending) {
            print("E:", LA_F("% Client sent data before ONLINE_ACK completed\n", LA_F12, 12));
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
            print("E:", LA_F("bad payload len %u\n", LA_F143, 143), payload_len);
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
            if (payload_len != P2P_RLY_ONLINE_PSZ) {
                print("E:", LA_F("ONLINE: bad payload(len=%u, expected=%u)\n", LA_F95, 95), 
                       payload_len, (uint32_t)(P2P_PEER_ID_MAX + 4));
                goto disconnect;
            }
            // 禁止重复 ONLINE
            if (client->base.local_peer_id[0]) {
                print("E:", LA_F("ONLINE: duplicate from '%s'\n", LA_F96, 96), client->base.local_peer_id);
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
                    print("I:", LA_F("ONLINE: '%s' reconnected (inst=%u), migrating fd\n", LA_F94, 94),
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
                    print("I:", LA_F("ONLINE: '%s' new instance (old=%u, new=%u), destroying old\n", LA_F93, 93),
                           client->base.local_peer_id, old->base.instance_id, client->base.instance_id);
                    relay_clear_client(old);
                }
            }
            
            print("I:", LA_F("ONLINE: '%s' came online (inst=%u)\n", LA_F92, 92),
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
            print("E:", LA_F("type=%u rejected: client not logged in\n", LA_F151, 151), (unsigned)type);
            relay_send_error(client, type, P2P_RLY_ERR_NOT_ONLINE);
            goto disconnect;
        }
        else if (type == P2P_RLY_ALIVE) {} // 心跳包：last_active 已在循环入口更新，无需额外处理
        else if (type == P2P_RLY_SYNC0) {
            handle_relay_sync0(client, payload, payload_len);
        }
        else {
            if (payload_len < P2P_SESS_ID_PSZ) {
                print("E:", LA_F("bad payload len %u (type=%u)\n", LA_F142, 142), payload_len, (unsigned)type);
                client->recv_len = 0;
                continue;
            }

            uint32_t session_id;
            nread_l(&session_id, payload);
            session_t *s = NULL;
            HASH_FIND(hh_session, g_sessions, &session_id, P2P_SESS_ID_PSZ, s);
            if (s == NULL || s->client != &client->base) {
                print("W:", LA_F("unknown ses_id=%" PRIu32 " (type=%u)\n", LA_F152, 152), session_id, (unsigned)type);
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

                // REQ 特殊处理：即通过 RPC 的 resp + code 返回错误吗，而非通用的 P2P_RLY_STATUS 错误码
                if (type == P2P_RLY_REQ && payload_len >= P2P_RLY_REQ_MIN_PSZ) {
                    uint8_t* sid_ptr = payload + P2P_SESS_ID_PSZ;
                    relay_session_send_rpc_error(rs, nget_s(sid_ptr), P2P_MSG_ERR_PEER_OFFLINE);
                } else {
                    print("W:", LA_F("ses_id=%" PRIu32 " peer not connected (type=%u)\n", LA_F150, 150), session_id, (unsigned)type);
                    relay_send_error(client, type, P2P_RLY_ERR_PEER_OFFLINE);
                }
            }
            // SYNC / DATA 转发时，最多允许一个在发、一个待发，超过则返回 BUSY
            else if ((type == P2P_RLY_SYNC || type == P2P_RLY_DATA)
                     && rs->peer_pending && rs->peer_pending != (buffer_item_t*)-1) {

                print("W:", LA_F("ses_id=%" PRIu32 " busy (pending relay)\n", LA_F149, 149), session_id);
                relay_session_send_status(rs, type, P2P_RLY_ERR_BUSY);
            }
            else switch (type) {
            case P2P_RLY_SYNC:
                handle_relay_sync(client, rs, payload, payload_len);
                break;
            case P2P_RLY_DATA:
                handle_relay_data(client, rs, payload, payload_len);
                break;
            case P2P_RLY_REQ:
                handle_relay_req(client, rs, payload, payload_len);
                break;
            case P2P_RLY_RESP:
                handle_relay_resp(client, rs, payload, payload_len);
                break;
            default:
                print("E:", LA_F("unsupported type=%u (ses_id=%" PRIu32 ")\n", LA_F153, 153),
                       (unsigned)type, session_id);
                goto disconnect;
            }
        }
        
        // 重置缓冲区，准备接收下一个消息
        client->recv_len = 0;
    }
    
disconnect:
    if (client->base.local_peer_id[0]) {
        print("I:", LA_F("'%s' disconnected\n", LA_F71, 71), client->base.local_peer_id);
    } else {
        print("I:", LA_F("Client disconnected (not yet logged in)\n", LA_F79, 79));
    }
    relay_clear_client(client);
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {

    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) { relay_client_t *c = &g_relay_clients[i];

        if (!c->base.valid || c->fd == P_INVALID_SOCKET) continue;
        if (tick_diff(now, c->base.last_active) <= RELAY_CLIENT_TIMEOUT_S * 1000) continue;

        print("W:", LA_F("'%s' timeout (inactive for %.1f sec)\n", LA_F72, 72), 
               c->base.local_peer_id, tick_diff(now, c->base.last_active) / 1000.0);
        
        relay_clear_client(c);
    }
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "COMPACT"

// 从 compact_session 指针获取所属 compact_client
#define COMPACT_CLIENT(cs)  ((compact_client_t*)(cs)->base.client)

// 获取 session 对应的远端 peer_id（通过 session_pair_t）
static inline const char* cs_remote_peer(const compact_session_t *cs) {
    const session_pair_t *pair = cs->base.pair;
    if (!pair) return "";
    int side = (pair->sessions[0] == &cs->base) ? 0 : 1;
    return pair->peer_id[1 - side];
}

//-----------------------------------------------------------------------------
// session / client 生命周期管理

// forward declarations
static void remove_compact_sync0_pending(compact_session_t *cs);
static void remove_compact_rpc_pending(compact_session_t *cs);
static void compact_send_fin(sock_t udp_fd, compact_session_t *cs, const char *reason);
static void compact_transition_to_resp_pending(sock_t udp_fd, compact_session_t *requester, uint64_t now,
                                       uint8_t flags, uint8_t code, const uint8_t *data, int len);

static void compact_free_session(sock_t udp_fd, compact_session_t *cs) {
    if (cs->sync0_pending_next) remove_compact_sync0_pending(cs);
    if (cs->rpc_pending_next)   remove_compact_rpc_pending(cs);

    // 通知对端断开，并标记对端 peer 指针为 -1
    if (PEER_ONLINE(cs)) {
        cs->peer->peer = (compact_session_t*)(void*)-1;
        compact_send_fin(udp_fd, cs->peer, "peer_disconnect");
    }

    free_session(&cs->base);
}

static void compact_clear_client(sock_t udp_fd, compact_client_t *c) {
    // 释放所有 session（free_session 内部摘除链表节点）
    while (c->base.sessions) {
        compact_free_session(udp_fd, (compact_session_t*)c->base.sessions);
    }
    // 从 auth 哈希表移除
    if (c->auth_key) {
        HASH_DELETE(hh_client, g_compact_clients_by_auth, c);
        c->auth_key = 0;
    }
    c->base.local_peer_id[0] = 0;
    c->base.valid = false;
}

//-----------------------------------------------------------------------------

// 发送 ONLINE_ACK: [hdr(4)][instance_id(4)][auth_key(8)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 25字节
// auth_key=0 表示服务器拒绝（无可用槽位）
static void compact_send_online_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, uint64_t auth_key, uint32_t instance_id) {
    const char* PROTO = "ONLINE_ACK";

    uint8_t ack[sizeof(p2p_packet_hdr_t) + SIG_PKT_ONLINE_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_ONLINE_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    if (auth_key != 0) {
        if (ARGS_relay.i64)    hdr->flags |= SIG_ONACK_FLAG_RELAY;
        if (ARGS_msg.i64)      hdr->flags |= SIG_ONACK_FLAG_MSG;

        int ofz = sizeof(p2p_packet_hdr_t);
        nwrite_l(ack + ofz, instance_id); ofz += (int)sizeof(instance_id);
        nwrite_ll(ack + ofz, auth_key); ofz += (int)sizeof(auth_key);
        ack[ofz++] = MAX_CANDIDATES;
        memcpy(ack + ofz, &to->sin_addr.s_addr, 4); ofz += 4;
        memcpy(ack + ofz, &to->sin_port, 2); ofz += 2;
        uint16_t probe = htons((uint16_t)ARGS_probe_port.i64);
        memcpy(ack + ofz, &probe, 2); ofz += 2;

        print("V:", LA_F("Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%" PRIu64 ", inst_id=%u\n", LA_F111, 111),
              PROTO, MAX_CANDIDATES,
              ARGS_relay.i64 ? "yes" : "no", ARGS_msg.i64 ? "yes" : "no",
              inet_ntoa(to->sin_addr), ntohs(to->sin_port),
              (int)ARGS_probe_port.i64, auth_key, instance_id);
    } else {
        memset(ack + sizeof(p2p_packet_hdr_t), 0, SIG_PKT_ONLINE_ACK_PSZ);
        print("V:", LA_F("Send %s: rejected (no slot available)\n", LA_F113, 113), PROTO);
    }

    ssize_t sent = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (const struct sockaddr *)to, sizeof(*to));
    if (sent != (ssize_t)sizeof(ack))
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F135, 135), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n", LA_F138, 138), PROTO, to_str, hdr->flags, (int)sent);     
}

// 发送 SYNC0_ACK: [hdr(4)][session_id(4)][online(1)] = 9字节
static void compact_send_sync0_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, uint32_t session_id, uint8_t online) {
    const char* PROTO = "SYNC0_ACK";

    uint8_t ack[sizeof(p2p_packet_hdr_t) + SIG_PKT_SYNC0_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_SYNC0_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(ack + ofz, session_id); ofz += P2P_SESS_ID_PSZ;
    ack[ofz++] = online;

    print("V:", LA_F("Send %s: ses_id=%" PRIu32 ", peer=%s\n", LA_F114, 114),
          PROTO, session_id, online ? "online" : "offline");

    ssize_t sent = sendto(udp_fd, (const char *)ack, ofz, 0, (const struct sockaddr *)to, sizeof(*to));
    if (sent != (ssize_t)ofz)
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F135, 135), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F137, 137), PROTO, to_str, (int)sent);
}

// 发送 FIN 通知给 cs 所代表的 session（"通知 cs 的持有方：对端已断开"）
static void compact_send_fin(sock_t udp_fd, compact_session_t *cs, const char *reason) {
    const char* PROTO = "FIN";

    compact_client_t *client = COMPACT_CLIENT(cs);
    uint8_t pkt[sizeof(p2p_packet_hdr_t) + SIG_PKT_FIN_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_FIN; hdr->flags = 0; hdr->seq = htons(0);

    nwrite_l(pkt + sizeof(p2p_packet_hdr_t), cs->base.session_id);

    print("V:", LA_F("Send %s: peer='%s', reason=%s, ses_id=%" PRIu32 "\n", LA_F112, 112),
          PROTO, client->base.local_peer_id, reason, cs->base.session_id);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, sizeof(pkt), 0,
           (struct sockaddr *)&client->addr, sizeof(client->addr));
    if (sent != (ssize_t)sizeof(pkt))
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F139, 139),
              PROTO, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F140, 140),
               PROTO, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), (int)sent);
}

// 发送首次对端候选推送（base_index=0）或地址变更通知（base_index != 0 为循环通知序号）
// base_index=0: SIG_PKT_SYNC0，payload: [session_id(4)][0x00(1)][cand_cnt(1)][candidates]
// base_index!=0: SIG_PKT_SYNC（seq=0），payload: [session_id(4)][notify_seq(1)][1][candidate]
static void compact_send_sync0(sock_t udp_fd, compact_session_t *cs, uint8_t base_index) {
    const char* PROTO = base_index == 0 ? "SYNC0" : "SYNC";

    assert(cs && PEER_ONLINE(cs));

    compact_client_t *client    = COMPACT_CLIENT(cs);
    compact_session_t *peer     = cs->peer;
    compact_client_t  *peer_cli = COMPACT_CLIENT(peer);

    uint8_t pkt[sizeof(p2p_packet_hdr_t) + P2P_PEER_ID_MAX + 2 + MAX_CANDIDATES * sizeof(p2p_candidate_t)];
    p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)pkt;
    resp_hdr->flags = 0;
    resp_hdr->seq = htons(0);

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, cs->base.session_id); ofz += P2P_SESS_ID_PSZ;

    int cand_cnt;

    if (base_index == 0) {

        resp_hdr->type = SIG_PKT_SYNC0;

        cand_cnt = 1 + peer->candidate_count;
        pkt[ofz++] = 0;
        pkt[ofz++] = (uint8_t)cand_cnt;

        // 第一个候选：对端的公网地址
        p2p_candidate_t wire_cand;
        wire_cand.type = 1; // srflx
        sockaddr_to_p2p_wire(&peer_cli->addr, &wire_cand.addr);
        wire_cand.priority = 0;
        memcpy(pkt + ofz, &wire_cand, sizeof(p2p_candidate_t));
        ofz += sizeof(p2p_candidate_t);

        for (int i = 0; i < peer->candidate_count; i++) {
            memcpy(pkt + ofz, &peer->candidates[i], sizeof(p2p_candidate_t));
            ofz += sizeof(p2p_candidate_t);
        }

        print("V:", LA_F("Send %s: cands=%d, ses_id=%" PRIu32 ", peer='%s'\n", LA_F109, 109),
              PROTO, cand_cnt, cs->base.session_id, client->base.local_peer_id);
    }
    else {

        resp_hdr->type = SIG_PKT_SYNC;

        cand_cnt = 1;
        pkt[ofz++] = base_index;
        pkt[ofz++] = 1;

        p2p_candidate_t wire_cand2;
        wire_cand2.type = 1; // srflx
        sockaddr_to_p2p_wire(&peer_cli->addr, &wire_cand2.addr);
        wire_cand2.priority = 0;
        memcpy(pkt + ofz, &wire_cand2, sizeof(p2p_candidate_t));
        ofz += sizeof(p2p_candidate_t);

        print("V:", LA_F("Send %s: base_index=%u, cands=%d, ses_id=%" PRIu32 ", peer='%s'\n", LA_F108, 108),
              PROTO, base_index, cand_cnt, cs->base.session_id, client->base.local_peer_id);
    }

    ssize_t sent = sendto(udp_fd, (const char *)pkt, ofz, 0, (struct sockaddr *)&client->addr, sizeof(client->addr));
    if (sent != (ssize_t)ofz)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F139, 139),
              PROTO, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F140, 140),
               PROTO, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), (int)sent);
}

// 发送 MSG_REQ_ACK
static void compact_send_msg_req_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str,
                                     uint32_t session_id, uint16_t sid, uint8_t status) {
    const char* PROTO = "MSG_REQ_ACK";

    uint8_t ack[sizeof(p2p_packet_hdr_t) + SIG_PKT_MSG_REQ_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_MSG_REQ_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(ack + ofz, session_id); ofz += P2P_SESS_ID_PSZ;
    nwrite_s(ack + ofz, sid); ofz += 2;
    ack[ofz++] = status;

    print("V:", LA_F("Send %s: ses_id=%" PRIu32 ", sid=%u, status=%u\n", LA_F118, 118),
          PROTO, session_id, sid, status);

    ssize_t sent = sendto(udp_fd, (const char *)ack, ofz, 0, (const struct sockaddr *)to, sizeof(*to));
    if (sent != (ssize_t)ofz)
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F135, 135), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F137, 137), PROTO, to_str, (int)sent);
}

// 发送 MSG_REQ 给对端（Server→对端 relay）
static void compact_send_msg_req_to_peer(sock_t udp_fd, compact_session_t *cs) {
    const char* PROTO = "MSG_REQ";

    assert(cs && PEER_ONLINE(cs));
    assert(cs->rpc_pending_next && !cs->rpc_responding);

    compact_session_t *peer     = cs->peer;
    compact_client_t  *peer_cli = COMPACT_CLIENT(peer);

    uint8_t pkt[sizeof(p2p_packet_hdr_t) + P2P_SESS_ID_PSZ + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_REQ;
    hdr->flags = SIG_MSG_FLAG_RELAY;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, peer->base.session_id); ofz += P2P_SESS_ID_PSZ;
    nwrite_s(pkt + ofz, cs->rpc_last_sid); ofz += 2;
    pkt[ofz++] = cs->rpc_code;
    if (cs->rpc_data_len > 0) {
        memcpy(pkt + ofz, cs->rpc_data, cs->rpc_data_len);
        ofz += cs->rpc_data_len;
    }

    print("V:", LA_F("Send %s: ses_id=%" PRIu32 ", sid=%u, msg=%u, data_len=%d, peer='%s'\n", LA_F115, 115),
          PROTO, peer->base.session_id, cs->rpc_last_sid, cs->rpc_code, cs->rpc_data_len,
          peer_cli->base.local_peer_id);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, ofz, 0, (struct sockaddr *)&peer_cli->addr, sizeof(peer_cli->addr));
    if (sent != (ssize_t)ofz)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F139, 139),
              PROTO, inet_ntoa(peer_cli->addr.sin_addr), ntohs(peer_cli->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n", LA_F141, 141),
               PROTO, inet_ntoa(peer_cli->addr.sin_addr), ntohs(peer_cli->addr.sin_port),
               hdr->flags, (int)sent, cs->rpc_retry);
}

// 发送 MSG_RESP_ACK 给 B 端（Server→B）
static void compact_send_msg_resp_ack_to_responder(sock_t udp_fd, const struct sockaddr_in *addr,
                                                   const char *peer_id, uint32_t session_id, uint16_t sid) {
    const char* PROTO = "MSG_RESP_ACK";

    uint8_t pkt[sizeof(p2p_packet_hdr_t) + SIG_PKT_MSG_RESP_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, session_id); ofz += P2P_SESS_ID_PSZ;
    nwrite_s(pkt + ofz, sid); ofz += 2;

    print("V:", LA_F("Send %s: ses_id=%" PRIu32 ", sid=%u, peer='%s'\n", LA_F117, 117),
          PROTO, session_id, sid, peer_id);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, ofz, 0, (struct sockaddr *)addr, sizeof(*addr));
    if (sent != (ssize_t)ofz)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F139, 139),
              PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F140, 140),
               PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), (int)sent);
}

// 发送 MSG_RESP 给请求方（Server→A）
static void compact_send_msg_resp_to_requester(sock_t udp_fd, compact_session_t *cs) {
    const char* PROTO = "MSG_RESP";

    assert(cs && cs->rpc_responding);

    compact_client_t *client = COMPACT_CLIENT(cs);
    uint8_t pkt[sizeof(p2p_packet_hdr_t) + P2P_SESS_ID_PSZ + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP;
    hdr->flags = cs->rpc_flags;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, cs->base.session_id); ofz += P2P_SESS_ID_PSZ;
    nwrite_s(pkt + ofz, cs->rpc_last_sid); ofz += 2;

    if (!(cs->rpc_flags & (SIG_MSG_FLAG_PEER_OFFLINE | SIG_MSG_FLAG_TIMEOUT))) {
        pkt[ofz++] = cs->rpc_code;
        if (cs->rpc_data_len > 0) {
            memcpy(pkt + ofz, cs->rpc_data, cs->rpc_data_len);
            ofz += cs->rpc_data_len;
        }
    }

    print("V:", LA_F("Send %s: ses_id=%" PRIu32 ", sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n", LA_F116, 116),
          PROTO, cs->base.session_id, cs->rpc_last_sid, client->base.local_peer_id, cs->rpc_flags, cs->rpc_code, cs->rpc_data_len);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, ofz, 0, (struct sockaddr *)&client->addr, sizeof(client->addr));
    if (sent != (ssize_t)ofz)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F139, 139),
                PROTO, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n", LA_F141, 141),
                PROTO, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port),
                cs->rpc_flags, (int)sent, cs->rpc_retry);
}

//-----------------------------------------------------------------------------
// SYNC(seq=0) 待确认链表管理

// 从待确认链表移除
static void remove_compact_sync0_pending(compact_session_t *cs) {

    if (!g_compact_sync0_pending_head || !cs->sync0_pending_next) return;

    if (g_compact_sync0_pending_head == cs) {
        g_compact_sync0_pending_head = cs->sync0_pending_next;
        cs->sync0_pending_next = NULL;
        if (g_compact_sync0_pending_head == (compact_session_t*)(void*)-1) {
            g_compact_sync0_pending_head = NULL;
            g_compact_sync0_pending_rear = NULL;
        }
        return;
    }

    compact_session_t *prev = g_compact_sync0_pending_head;
    while (prev->sync0_pending_next != cs) {
        if (prev->sync0_pending_next == (compact_session_t*)(void*)-1) return;
        prev = prev->sync0_pending_next;
    }

    prev->sync0_pending_next = cs->sync0_pending_next;

    if (cs->sync0_pending_next == (compact_session_t*)(void*)-1) {
        g_compact_sync0_pending_rear = prev;
    }

    cs->sync0_pending_next = NULL;
}

// 将 session 加入 SYNC(seq=0) 待确认链表
static void enqueue_compact_sync0_pending(compact_session_t *cs, uint8_t base_index, uint64_t now) {

    if (cs->sync0_pending_next) {
        remove_compact_sync0_pending(cs);
    }

    cs->sync0_base_index = base_index;
    cs->sync0_retry = 0;
    cs->sync0_sent_time = now;

    cs->sync0_pending_next = (compact_session_t*)(void*)-1;
    if (g_compact_sync0_pending_rear) {
        g_compact_sync0_pending_rear->sync0_pending_next = cs;
        g_compact_sync0_pending_rear = cs;
    } else {
        g_compact_sync0_pending_head = cs;
        g_compact_sync0_pending_rear = cs;
    }
}

// 检查并重传未确认的 SYNC 包
static void retry_compact_sync0_pending(sock_t udp_fd, uint64_t now) {

    if (!g_compact_sync0_pending_head) return;

    for(;;) {

        if (tick_diff(now, g_compact_sync0_pending_head->sync0_sent_time) < SYNC0_RETRY_INTERVAL_MS) {
            return;
        }

        compact_session_t *q = g_compact_sync0_pending_head;
        g_compact_sync0_pending_head = q->sync0_pending_next;

        if (q->sync0_retry >= SYNC0_MAX_RETRY) {

            print("W:", LA_F("SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n", LA_F103, 103),
                   COMPACT_CLIENT(q)->base.local_peer_id, cs_remote_peer(q), q->sync0_retry);

            q->sync0_pending_next = NULL;
            if (q->sync0_base_index == 0) {
                q->sync0_acked = -1;
            }

            if (g_compact_sync0_pending_head == (compact_session_t*)(void*)-1) {
                g_compact_sync0_pending_head = NULL;
                g_compact_sync0_pending_rear = NULL;
                return;
            }
        }
        else {
            if (!PEER_ONLINE(q)) {
                q->sync0_pending_next = NULL;
                if (g_compact_sync0_pending_head == (compact_session_t*)(void*)-1) {
                    g_compact_sync0_pending_head = NULL;
                    g_compact_sync0_pending_rear = NULL;
                    return;
                }
                continue;
            }

            compact_send_sync0(udp_fd, q, q->sync0_base_index);

            q->sync0_retry++;
            q->sync0_sent_time = now;

            if (g_compact_sync0_pending_head == (compact_session_t*)(void*)-1) {
                g_compact_sync0_pending_head = q;
            } else {
                q->sync0_pending_next = (compact_session_t*)(void*)-1;
                g_compact_sync0_pending_rear->sync0_pending_next = q;
                g_compact_sync0_pending_rear = q;
            }

            print("V:", LA_F("SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%" PRIu32 ")\n", LA_F102, 102),
                   COMPACT_CLIENT(q)->base.local_peer_id, cs_remote_peer(q),
                   q->sync0_retry, SYNC0_MAX_RETRY, q->base.session_id);

            if (g_compact_sync0_pending_head == q) return;
        }
    }
}


//-----------------------------------------------------------------------------
// MSG RPC 待确认链表管理

// 从 RPC 待确认链表移除
static void remove_compact_rpc_pending(compact_session_t *cs) {

    if (!g_compact_rpc_pending_head || !cs->rpc_pending_next) return;

    if (g_compact_rpc_pending_head == cs) {
        g_compact_rpc_pending_head = cs->rpc_pending_next;
        cs->rpc_pending_next = NULL;
        if (g_compact_rpc_pending_head == (compact_session_t*)(void*)-1) {
            g_compact_rpc_pending_head = NULL;
            g_compact_rpc_pending_rear = NULL;
        }
        return;
    }

    compact_session_t *prev = g_compact_rpc_pending_head;
    while (prev->rpc_pending_next != cs) {
        if (prev->rpc_pending_next == (compact_session_t*)(void*)-1) return;
        prev = prev->rpc_pending_next;
    }

    prev->rpc_pending_next = cs->rpc_pending_next;
    if (cs->rpc_pending_next == (compact_session_t*)(void*)-1) {
        g_compact_rpc_pending_rear = prev;
    }
    cs->rpc_pending_next = NULL;
}

// 将 session 加入 RPC 待确认链表
static inline void enqueue_compact_rpc_pending(compact_session_t *cs) {

    cs->rpc_pending_next = (compact_session_t*)(void*)-1;
    if (g_compact_rpc_pending_rear) {
        g_compact_rpc_pending_rear->rpc_pending_next = cs;
        g_compact_rpc_pending_rear = cs;
    } else {
        g_compact_rpc_pending_head = cs;
        g_compact_rpc_pending_rear = cs;
    }
}

// 检查并重传 RPC（统一处理 REQ 和 RESP 阶段）
static void retry_compact_rpc_pending(sock_t udp_fd, uint64_t now) {

    if (!g_compact_rpc_pending_head) return;

    for (;;) {

        if (tick_diff(now, g_compact_rpc_pending_head->rpc_sent_time) < MSG_RPC_RETRY_INTERVAL_MS) {
            return;
        }

        compact_session_t *q = g_compact_rpc_pending_head;
        g_compact_rpc_pending_head = q->rpc_pending_next;
        if (g_compact_rpc_pending_head == (compact_session_t*)(void*)-1) {
            g_compact_rpc_pending_head = NULL;
            g_compact_rpc_pending_rear = NULL;
        }

        if (!q->rpc_responding) {

            if (!PEER_ONLINE(q)) {
                print("W:", LA_F("MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%" PRIu32 ")\n", LA_F85, 85),
                      COMPACT_CLIENT(q)->base.local_peer_id, q->rpc_last_sid, q->base.session_id);

                compact_transition_to_resp_pending(udp_fd, q, now, SIG_MSG_FLAG_PEER_OFFLINE, 0, NULL, 0);
            }
            else if (q->rpc_retry >= MSG_REQ_MAX_RETRY) {
                print("W:", LA_F("MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%" PRIu32 ")\n", LA_F84, 84),
                      q->rpc_retry, COMPACT_CLIENT(q)->base.local_peer_id, q->rpc_last_sid, q->base.session_id);

                compact_transition_to_resp_pending(udp_fd, q, now, SIG_MSG_FLAG_TIMEOUT, 0, NULL, 0);
            }
            else {
                compact_send_msg_req_to_peer(udp_fd, q);
                q->rpc_retry++;
                q->rpc_sent_time = now;
                enqueue_compact_rpc_pending(q);

                print("V:", LA_F("MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%" PRIu32 ")\n", LA_F86, 86),
                      COMPACT_CLIENT(q)->base.local_peer_id, COMPACT_CLIENT(q->peer)->base.local_peer_id,
                      q->rpc_last_sid, q->rpc_retry, MSG_REQ_MAX_RETRY, q->base.session_id);

                if (g_compact_rpc_pending_head == q) return;
            }
        }
        else {

            if (q->rpc_retry >= MSG_RESP_MAX_RETRY) {
                print("W:", LA_F("MSG_RESP gave up after %d retries, sid=%u (ses_id=%" PRIu32 ")\n", LA_F87, 87),
                      q->rpc_retry, q->rpc_last_sid, q->base.session_id);

                q->rpc_pending_next = NULL;
                q->rpc_responding = false;
                q->rpc_retry = 0;
            }
            else {
                q->rpc_retry++;
                compact_send_msg_resp_to_requester(udp_fd, q);
                q->rpc_sent_time = now;
                enqueue_compact_rpc_pending(q);

                print("V:", LA_F("MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%" PRIu32 ")\n", LA_F88, 88),
                      COMPACT_CLIENT(q)->base.local_peer_id, q->rpc_last_sid, q->rpc_retry, MSG_RESP_MAX_RETRY, q->base.session_id);

                if (g_compact_rpc_pending_head == q) return;
            }
        }

        if (!g_compact_rpc_pending_head) return;
    }
}

// 缓存响应数据并从 REQ 阶段转换到 RESP 阶段
static void compact_transition_to_resp_pending(sock_t udp_fd, compact_session_t *requester, uint64_t now,
                                       uint8_t flags, uint8_t code, const uint8_t *data, int len) {

    requester->rpc_responding = true;
    requester->rpc_flags = flags;
    requester->rpc_code = code;
    requester->rpc_data_len = 0;
    if (len > 0 && data) {
        memcpy(requester->rpc_data, data, len);
        requester->rpc_data_len = len;
    }
    requester->rpc_sent_time = now;
    requester->rpc_retry = 0;
    enqueue_compact_rpc_pending(requester);
    compact_send_msg_resp_to_requester(udp_fd, requester);
}

//-----------------------------------------------------------------------------

// 检测地址变更并通知对端（所有已配对 session 均会发出通知）
static bool check_addr_change(sock_t udp_fd, compact_client_t *client, const struct sockaddr_in *from) {

    if (memcmp(&client->addr, from, sizeof(*from)) == 0) return false;

    client->addr = *from;

    for (session_t *sbase = client->base.sessions; sbase; sbase = sbase->next) {
        compact_session_t *cs = (compact_session_t*)sbase;
        if (!PEER_ONLINE(cs)) continue;

        compact_session_t *peer = cs->peer;
        if (peer->sync0_acked > 0) {
            peer->addr_notify_seq = (uint8_t)(peer->addr_notify_seq + 1);
            if (peer->addr_notify_seq == 0) peer->addr_notify_seq = 1;

            compact_send_sync0(udp_fd, peer, peer->addr_notify_seq);
            enqueue_compact_sync0_pending(peer, peer->addr_notify_seq, P_tick_ms());

            print("I:", LA_F("Addr changed for '%s', notifying '%s' (ses_id=%" PRIu32 ")\n", LA_F76, 76),
                  client->base.local_peer_id, COMPACT_CLIENT(peer)->base.local_peer_id, peer->base.session_id);
        }
        else if (peer->sync0_acked == 0) {
            if (peer->addr_notify_seq == 0) peer->addr_notify_seq = 1;

            print("I:", LA_F("Addr changed for '%s', defer notification until first ACK (ses_id=%" PRIu32 ")\n", LA_F74, 74),
                  client->base.local_peer_id, peer->base.session_id);
        }
        else {
            print("W:", LA_F("Addr changed for '%s', but first info packet was abandoned (ses_id=%" PRIu32 ")\n", LA_F73, 73),
                   client->base.local_peer_id, peer->base.session_id);
        }
    }

    return true;
}

// 查找 client 下与 remote_peer_id 配对的 compact_session（不创建新会话）
static compact_session_t* compact_find_session(compact_client_t *client, const char *remote_peer_id) {
    char peer_key[P2P_PEER_ID_MAX * 2];

    memcpy(peer_key, client->base.local_peer_id, P2P_PEER_ID_MAX);
    memcpy(peer_key + P2P_PEER_ID_MAX, remote_peer_id, P2P_PEER_ID_MAX);
    session_pair_t *pair = NULL;
    HASH_FIND(hh_peer, g_session_pairs, peer_key, P2P_PEER_ID_MAX * 2, pair);

    if (!pair) {
        memcpy(peer_key, remote_peer_id, P2P_PEER_ID_MAX);
        memcpy(peer_key + P2P_PEER_ID_MAX, client->base.local_peer_id, P2P_PEER_ID_MAX);
        HASH_FIND(hh_peer, g_session_pairs, peer_key, P2P_PEER_ID_MAX * 2, pair);
    }

    if (!pair) return NULL;

    for (int i = 0; i < 2; i++) {
        if (pair->sessions[i] && pair->sessions[i]->client == &client->base) {
            return (compact_session_t*)pair->sessions[i];
        }
    }
    return NULL;
}

// 处理 COMPACT 模式信令（UDP 无状态，对应 p2p_signal_compact 模块）
static void handle_compact_signaling(sock_t udp_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {

    if (len < 4) return;

    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4; size_t payload_len = len - 4;

    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    switch (hdr->type) {
    case SIG_PKT_ONLINE: { const char* PROTO = "ONLINE";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_ONLINE_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX);
        if (instance_id == 0) {
            print("E:", LA_F("%s: invalid instance_id=0 from %s\n", LA_F49, 49), PROTO, from_str);
            return;
        }

        const char *local_peer_id = (const char *)payload;

        // 扫描客户端数组，查找匹配的 local_peer_id
        compact_client_t *existing = NULL;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_compact_clients[i].base.valid &&
                memcmp(g_compact_clients[i].base.local_peer_id, local_peer_id, P2P_PEER_ID_MAX) == 0) {
                existing = &g_compact_clients[i];
                break;
            }
        }

        // 重传（instance_id 相同）：幂等响应
        if (existing && existing->base.instance_id == instance_id) {

            check_addr_change(udp_fd, existing, from);
            existing->base.last_active = P_tick_ms();

            compact_send_online_ack(udp_fd, from, from_str, existing->auth_key, instance_id);
            return;
        }

        print("V:", LA_F("%s: accepted, local='%.*s', inst_id=%u\n", LA_F29, 29),
               PROTO, P2P_PEER_ID_MAX, local_peer_id, instance_id);

        // 找空位或复用现有槽（instance_id 变更 = 客户端重启）
        compact_client_t *client = existing;
        if (!client) {
            for (int i = 0; i < MAX_PEERS; i++) {
                if (!g_compact_clients[i].base.valid) {
                    client = &g_compact_clients[i];
                    break;
                }
            }
        }

        // 无可用槽位
        if (!client) {
            compact_send_online_ack(udp_fd, from, from_str, 0, instance_id);
            return;
        }

        // 新 instance_id：重置旧会话
        if (existing) {
            print("I:", LA_F("%s from '%.*s': new instance(old=%u new=%u), resetting\n", LA_F19, 19),
                   PROTO, P2P_PEER_ID_MAX, local_peer_id, existing->base.instance_id, instance_id);
            compact_clear_client(udp_fd, existing);
        }

        // 初始化客户端槽位
        client->base.valid = true;
        memcpy(client->base.local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
        client->base.instance_id = instance_id;
        client->base.last_active = P_tick_ms();
        client->base.sessions = NULL;
        client->addr = *from;

        // 生成 auth_key 并加入哈希表
        do { client->auth_key = P_rand64(); } while (!client->auth_key);
        HASH_ADD(hh_client, g_compact_clients_by_auth, auth_key, sizeof(uint64_t), client);

        compact_send_online_ack(udp_fd, from, from_str, client->auth_key, instance_id);

        print("V:", LA_F("%s: auth_key=%" PRIu64 " assigned for '%.*s'\n", LA_F35, 35),
               PROTO, client->auth_key, P2P_PEER_ID_MAX, local_peer_id);
    } break;

    // SIG_PKT_OFFLINE: [auth_key(8)]
    case SIG_PKT_OFFLINE: { const char* PROTO = "OFFLINE";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < (int)SIG_PKT_OFFLINE_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint64_t auth_key = 0;
        nread_ll(&auth_key, payload);
        if (auth_key == 0) {
            print("E:", LA_F("%s: invalid auth_key=0 from %s\n", LA_F48, 48), PROTO, from_str);
            return;
        }

        compact_client_t *client = NULL;
        HASH_FIND(hh_client, g_compact_clients_by_auth, &auth_key, sizeof(uint64_t), client);

        if (client) {
            print("V:", LA_F("%s: accepted, releasing slot for '%s'\n", LA_F30, 30),
                   PROTO, client->base.local_peer_id);
            compact_clear_client(udp_fd, client);
        }
    } break;

    // SIG_PKT_ALIVE: [auth_key(8)]
    case SIG_PKT_ALIVE: { const char* PROTO = "ALIVE";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_ALIVE_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint64_t auth_key = nget_ll(payload);

        compact_client_t *client = NULL;
        HASH_FIND(hh_client, g_compact_clients_by_auth, &auth_key, sizeof(uint64_t), client);
        if (client) {

            print("V:", LA_F("%s accepted, peer='%s', auth_key=%" PRIu64 "\n", LA_F14, 14),
                  PROTO, client->base.local_peer_id, auth_key);

            client->base.last_active = P_tick_ms();
            check_addr_change(udp_fd, client, from);

            {   const char* ACK_PROTO = "ALIVE_ACK";

                uint8_t ack[4];
                p2p_pkt_hdr_encode(ack, SIG_PKT_ALIVE_ACK, 0, 0);

                print("V:", LA_F("Send %s: auth_key=%" PRIu64 ", peer='%s'\n", LA_F107, 107),
                      ACK_PROTO, auth_key, client->base.local_peer_id);

                ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (struct sockaddr *)from, sizeof(*from));
                if (n != (ssize_t)sizeof(ack))
                    print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F135, 135), ACK_PROTO, from_str, P_sock_errno());
                else
                    printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F137, 137), ACK_PROTO, from_str, (int)n);
            }
        } else {
            print("W:", LA_F("%s: unknown auth_key=%" PRIu64 " from %s\n", LA_F69, 69), PROTO, auth_key, from_str);
        }
    } break;

    // SIG_PKT_SYNC0: [auth_key(8)][remote_peer_id(32)][candidate_count(1)][candidates(N*sizeof(p2p_candidate_t))]
    // + 客户端请求连接对方
    case SIG_PKT_SYNC0: { const char* PROTO = "SYNC0";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_SYNC0_PSZ(0)) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint64_t auth_key = 0;
        nread_ll(&auth_key, payload);
        if (auth_key == 0) {
            print("E:", LA_F("%s: invalid auth_key=0 from %s\n", LA_F48, 48), PROTO, from_str);
            return;
        }

        compact_client_t *local_client = NULL;
        HASH_FIND(hh_client, g_compact_clients_by_auth, &auth_key, sizeof(uint64_t), local_client);
        if (!local_client) {
            print("W:", LA_F("%s: unknown auth_key=%" PRIu64 " from %s\n", LA_F69, 69), PROTO, auth_key, from_str);
            return;
        }

        const char *remote_peer_id = (const char *)(payload + sizeof(uint64_t));

        // 解析候选列表
        int candidate_count = (uint8_t)payload[sizeof(uint64_t) + P2P_PEER_ID_MAX];
        if (candidate_count > (int)MAX_CANDIDATES) candidate_count = MAX_CANDIDATES;
        p2p_candidate_t candidates[MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));
        size_t cand_offset = sizeof(uint64_t) + P2P_PEER_ID_MAX + 1;
        for (int i = 0; i < candidate_count && cand_offset + sizeof(p2p_candidate_t) <= payload_len; i++) {
            memcpy(&candidates[i], payload + cand_offset, sizeof(p2p_candidate_t));
            cand_offset += sizeof(p2p_candidate_t);
        }

        local_client->base.last_active = P_tick_ms();
        check_addr_change(udp_fd, local_client, from);

        // 查找或创建会话
        compact_session_t *local = compact_find_session(local_client, remote_peer_id);
        if (local) {
            // 已有会话（同一 instance_id 的重传或自动重连）
            // + 若上一个伙伴已死亡（peer==-1），清除标记，并重置 SYNC0 确认状态（让服务器可重新推送 SYNC0）
            if (local->peer == (compact_session_t*)(void*)-1) {
                local->peer = NULL;
                local->sync0_acked = 0;         // 重置：允许服务器重新推送 SYNC0（含新对端候选地址）
                local->addr_notify_seq = 0;     // 重置地址变更序列号
                print("I:", LA_F("%s: '%.*s' cleared stale peer marker, ready for re-pair\n", LA_F23, 23),
                       PROTO, P2P_PEER_ID_MAX, local_client->base.local_peer_id);

                // Case B：新对端已经发过 SYNC0 但被 skip（pair 里有等待中的 session）
                // 先更新候选再配对，让对端拿到最新地址
                local->candidate_count = candidate_count;
                if (candidate_count) memcpy(local->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count);

                session_pair_t *pair = local->base.pair;
                if (pair) {
                    for (int _i = 0; _i < 2; _i++) {
                        compact_session_t *waiting = (compact_session_t*)pair->sessions[_i];
                        if (waiting && waiting != local && waiting->peer == NULL) {
                            local->peer = waiting; waiting->peer = local;
                            print("I:", LA_F("%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n", LA_F53, 53),
                                  PROTO, P2P_PEER_ID_MAX, local_client->base.local_peer_id,
                                  P2P_PEER_ID_MAX, remote_peer_id);
                            break;
                        }
                    }
                }
            }
        } else {

            session_t *local_s = NULL, *remote_s = NULL;
            int side = build_session(&local_client->base, remote_peer_id,
                                     &local_s, &remote_s, sizeof(compact_session_t));
            if (side < 0 || !local_s) {
                print("E:", LA_F("%s: build_session failed for '%.*s'\n", LA_F42, 42), PROTO, P2P_PEER_ID_MAX,
                      local_client->base.local_peer_id);
                return;
            }
            local = (compact_session_t*)local_s;

            // 对端已经创建了会话，双向配对
            // + peer==-1 表示对端会话的上一个伙伴已崩溃（e.g. SIGKILL），且对端从未重发 SYNC0 刷新候选
            // + 此时不立即配对，等对端用新 instance_id 重新注册（或重发 SYNC0 清除标记）后再配对
            if (remote_s) {
                compact_session_t *remote_cs = (compact_session_t*)remote_s;
                if (remote_cs->peer != (compact_session_t*)(void*)-1) {
                    local->peer = remote_cs; remote_cs->peer = local;
                    print("I:", LA_F("%s: paired '%.*s' <-> '%.*s'\n", LA_F59, 59),
                           PROTO, P2P_PEER_ID_MAX, local_client->base.local_peer_id,
                           P2P_PEER_ID_MAX, remote_peer_id);
                } else {
                    print("I:", LA_F("%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n", LA_F68, 68),
                           PROTO, P2P_PEER_ID_MAX, local_client->base.local_peer_id,
                           P2P_PEER_ID_MAX, remote_peer_id);
                }
            }
        }

        // 更新候选列表
        local->candidate_count = candidate_count;
        if (candidate_count) {
            memcpy(local->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count);
        }

        print("V:", LA_F("%s: auth_key=%" PRIu64 ", cands=%d from %s\n", LA_F36, 36),
               PROTO, auth_key, candidate_count, from_str);

        // 发送 SYNC0_ACK
        compact_send_sync0_ack(udp_fd, from, from_str, local->base.session_id, PEER_ONLINE(local));

        // 已配对，触发 SYNC0
        if (PEER_ONLINE(local)) {

            compact_session_t *remote = local->peer;
            if (!local->sync0_pending_next && local->sync0_acked == 0) {
                compact_send_sync0(udp_fd, local, 0);
                enqueue_compact_sync0_pending(local, 0, local_client->base.last_active);
            }
            if (!remote->sync0_pending_next && remote->sync0_acked == 0) {
                compact_send_sync0(udp_fd, remote, 0);
                enqueue_compact_sync0_pending(remote, 0, local_client->base.last_active);
            }

            print("I:", LA_F("SYNC0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n", LA_F104, 104),
                   P2P_PEER_ID_MAX, local_client->base.local_peer_id, local->candidate_count,
                   P2P_PEER_ID_MAX, remote_peer_id, remote->candidate_count);
        }
    } break;

    // SIG_PKT_SYNC0_ACK（client→server）
    // + 客户端确认收到服务器 SYNC0，停止可靠性重传机制
    case SIG_PKT_SYNC0_ACK: { const char* PROTO = "SYNC0_ACK";

        printf(LA_F("[UDP] %s recv from %s, len=%zu\n", LA_F133, 133),
               PROTO, from_str, len);

        if (payload_len < SIG_PKT_SYNC0_ACK_C2S_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint32_t session_id = nget_l(payload);
        session_t *_s = NULL;
        HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
        compact_session_t *cs = (compact_session_t*)_s;

        if (cs) {
            check_addr_change(udp_fd, COMPACT_CLIENT(cs), from);

            if (!cs->sync0_acked) { cs->sync0_acked = 1;

                print("V:", LA_F("%s: confirmed '%s', retries=%d (ses_id=%" PRIu32 ")\n", LA_F45, 45),
                       PROTO, COMPACT_CLIENT(cs)->base.local_peer_id, cs->sync0_retry, session_id);
            }

            if (cs->sync0_pending_next) {
                remove_compact_sync0_pending(cs);
            }

            cs->sync0_base_index = 0;
            cs->sync0_retry = 0;
            cs->sync0_sent_time = 0;

            // 有延期的地址变更通知，立即发送
            if (cs->addr_notify_seq != 0) {

                compact_send_sync0(udp_fd, cs, cs->addr_notify_seq);
                enqueue_compact_sync0_pending(cs, cs->addr_notify_seq, P_tick_ms());

                print("I:", LA_F("Addr changed for '%s', deferred notifying '%s' (ses_id=%" PRIu32 ")\n", LA_F75, 75),
                      COMPACT_CLIENT(cs->peer)->base.local_peer_id, COMPACT_CLIENT(cs)->base.local_peer_id,
                      cs->peer->base.session_id);
            }
        }
        else print("W:", LA_F("%s for unknown ses_id=%" PRIu32 "\n", LA_F16, 16), PROTO, session_id);

    } break;

    // SIG_PKT_SYNC_ACK: 
    case SIG_PKT_SYNC_ACK: { const char* PROTO = "SYNC_ACK";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_SYNC_ACK_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint32_t session_id = nget_l(payload);
        uint16_t ack_seq = ntohs(hdr->seq);
        if (ack_seq > 16) {
            print("E:", LA_F("%s: invalid seq=%u\n", LA_F51, 51), PROTO, ack_seq);
            return;
        }

        session_t *_s = NULL;
        HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
        compact_session_t *cs = (compact_session_t*)_s;

        print("V:", LA_F("%s accepted, seq=%u, ses_id=%" PRIu32 "\n", LA_F15, 15),
              PROTO, ack_seq, session_id);

        // 如果是客户端确认收到 addr change SYNC 包，则停止可靠性重传机制
        if (ack_seq == 0) {

            if (cs) {
                check_addr_change(udp_fd, COMPACT_CLIENT(cs), from);

                if (cs->sync0_pending_next) {
                    remove_compact_sync0_pending(cs);
                }

                cs->sync0_base_index = 0;
                cs->sync0_retry = 0;
                cs->sync0_sent_time = 0;

                print("V:", LA_F("%s: addr-notify confirmed '%s' (ses_id=%" PRIu32 ")\n", LA_F34, 34),
                       PROTO, COMPACT_CLIENT(cs)->base.local_peer_id, session_id);
            }
            else print("W:", LA_F("%s for unknown ses_id=%" PRIu32 "\n", LA_F16, 16), PROTO, session_id);
        }
        // ack_seq≠0 的 ACK 是客户端之间的确认，服务器负责 relay 转发
        else {

            if (cs && PEER_ONLINE(cs)) {
                check_addr_change(udp_fd, COMPACT_CLIENT(cs), from);

                nwrite_l((uint8_t *)payload, cs->peer->base.session_id);

                sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                       (struct sockaddr *)&COMPACT_CLIENT(cs->peer)->addr,
                       sizeof(COMPACT_CLIENT(cs->peer)->addr));

                print("V:", LA_F("Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu32 ")\n", LA_F100, 100),
                       PROTO, ack_seq, COMPACT_CLIENT(cs)->base.local_peer_id,
                       cs_remote_peer(cs), session_id);
            }
            else print("W:", LA_F("Cannot relay %s: ses_id=%" PRIu32 " (peer unavailable)\n", LA_F77, 77), PROTO, session_id);
        }

    } break;

    // SIG_PKT_SYNC / relay 数据包
    case P2P_PKT_DATA:
    case P2P_PKT_ACK:
    case P2P_PKT_CRYPTO:
    case P2P_PKT_CONN:
    case P2P_PKT_CONN_ACK:
    case P2P_PKT_REACH:
        if (!(hdr->flags & P2P_RELAY_FLAG_SESSION)) {
            print("E:", LA_F("[Relay] %s: missing SESSION flag, dropped\n", LA_F128, 128),
                  (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                  (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                  (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                  (hdr->type == SIG_PKT_SYNC) ? "SYNC" :
                  (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                  (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH");
            return;
        }
    case SIG_PKT_SYNC: {
        const char* PROTO = (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                           (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                           (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                           (hdr->type == SIG_PKT_SYNC) ? "SYNC" :
                           (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                           (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < (int)P2P_SESS_ID_PSZ) {
            print("E:", LA_F("[Relay] %s: bad payload(len=%zu)\n", LA_F127, 127), PROTO, payload_len);
            return;
        }

        if (hdr->type == SIG_PKT_SYNC && hdr->seq == 0) {
            print("E:", LA_F("[Relay] %s seq=0 from client %s (server-only, dropped)\n", LA_F125, 125), PROTO, from_str);
            return;
        }

        uint32_t session_id = nget_l(payload);

        session_t *_s = NULL;
        HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
        compact_session_t *cs = (compact_session_t*)_s;
        if (!cs) {
            print("W:", LA_F("[Relay] %s for unknown ses_id=%" PRIu32 " (dropped)\n", LA_F123, 123), PROTO, session_id);
            return;
        }

        if (!PEER_ONLINE(cs)) {
            print("W:", LA_F("[Relay] %s for ses_id=%" PRIu32 ": peer unavailable (dropped)\n", LA_F122, 122), PROTO, session_id);
            return;
        }

        check_addr_change(udp_fd, COMPACT_CLIENT(cs), from);

        print("V:", LA_F("%s accepted, '%s' -> '%s', ses_id=%" PRIu32 "\n", LA_F13, 13),
              PROTO, COMPACT_CLIENT(cs)->base.local_peer_id, cs_remote_peer(cs), session_id);

        nwrite_l((uint8_t *)payload, cs->peer->base.session_id);

        sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
               (struct sockaddr *)&COMPACT_CLIENT(cs->peer)->addr,
               sizeof(COMPACT_CLIENT(cs->peer)->addr));

        if (hdr->type == SIG_PKT_SYNC || hdr->type == P2P_PKT_REACH ||
            hdr->type == P2P_PKT_DATA || hdr->type == P2P_PKT_CRYPTO) {
            print("V:", LA_F("[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu32 ")\n", LA_F124, 124),
                   PROTO, ntohs(hdr->seq), COMPACT_CLIENT(cs)->base.local_peer_id,
                   cs_remote_peer(cs), session_id);
        } else {
            print("V:", LA_F("[Relay] %s: '%s' -> '%s' (ses_id=%" PRIu32 ")\n", LA_F126, 126),
                   PROTO, COMPACT_CLIENT(cs)->base.local_peer_id, cs_remote_peer(cs), session_id);
        }
    } break;

    // SIG_PKT_MSG_REQ: A→Server
    case SIG_PKT_MSG_REQ: { const char* PROTO = "MSG_REQ";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_MSG_REQ_MIN_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        if (hdr->flags & SIG_MSG_FLAG_RELAY) {
            print("E:", LA_F("%s: invalid relay flag from client\n", LA_F50, 50), PROTO);
            return;
        }

        int msg_data_len = (int)(payload_len - SIG_PKT_MSG_REQ_MIN_PSZ);
        if (msg_data_len > P2P_MSG_DATA_MAX) {
            print("E:", LA_F("%s: data too large (len=%d)\n", LA_F46, 46), PROTO, msg_data_len);
            return;
        }

        uint32_t session_id = nget_l(payload);
        uint16_t sid = nget_s(payload + 4);

        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu32 " or sid=%u\n", LA_F52, 52),
                   PROTO, session_id, sid);
            return;
        }

        uint8_t msg = payload[6];
        const uint8_t *msg_data = payload + 7;

        session_t *_s = NULL;
        HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
        compact_session_t *requester = (compact_session_t*)_s;
        if (!requester) {
            print("W:", LA_F("%s: requester not found for ses_id=%" PRIu32 "\n", LA_F64, 64), PROTO, session_id);
            return;
        }

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu32 ", sid=%u, msg=%u, len=%d\n", LA_F32, 32),
               PROTO, session_id, sid, msg, msg_data_len);

        if (!PEER_ONLINE(requester)) {

            print("W:", LA_F("%s: peer '%s' not online, rejecting sid=%u\n", LA_F61, 61),
                   PROTO, cs_remote_peer(requester), sid);

            compact_send_msg_req_ack(udp_fd, from, from_str, session_id, sid, 1);
            return;
        }

        check_addr_change(udp_fd, COMPACT_CLIENT(requester), from);

        if (requester->rpc_pending_next) {

            if (sid == requester->rpc_last_sid) {

                if (!requester->rpc_responding) {

                    compact_send_msg_req_ack(udp_fd, from, from_str, requester->base.session_id, sid, 0);

                    print("V:", LA_F("%s retransmit, resend ACK, sid=%u (ses_id=%" PRIu32 ")\n", LA_F22, 22),
                          PROTO, sid, requester->base.session_id);
                }
                else {
                    print("V:", LA_F("%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%" PRIu32 ")\n", LA_F21, 21),
                          PROTO, sid, requester->base.session_id);
                }
                return;
            }

            if (!uint16_circle_newer(sid, requester->rpc_last_sid)) {
                print("V:", LA_F("%s: obsolete sid=%u (current=%u), ignoring\n", LA_F57, 57),
                      PROTO, sid, requester->rpc_last_sid);
                return;
            }

            print("I:", LA_F("%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%" PRIu32 ")\n", LA_F20, 20),
                  PROTO, sid, requester->rpc_last_sid, requester->rpc_responding, requester->base.session_id);

            remove_compact_rpc_pending(requester);
        }

        if (requester->rpc_last_sid != 0 && !uint16_circle_newer(sid, requester->rpc_last_sid)) {
            print("V:", LA_F("%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n", LA_F58, 58),
                  PROTO, sid, requester->rpc_last_sid);
            return;
        }

        requester->rpc_last_sid = sid;
        requester->rpc_responding = false;
        requester->rpc_code = msg;
        requester->rpc_data_len = msg_data_len;
        if (msg_data_len > 0) memcpy(requester->rpc_data, msg_data, msg_data_len);

        compact_send_msg_req_ack(udp_fd, from, from_str, requester->base.session_id, sid, 0);

        requester->rpc_sent_time = P_tick_ms();
        requester->rpc_retry = 0;
        enqueue_compact_rpc_pending(requester);
        compact_send_msg_req_to_peer(udp_fd, requester);

        print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%" PRIu32 ")\n", LA_F18, 18),
               PROTO, COMPACT_CLIENT(requester)->base.local_peer_id,
               COMPACT_CLIENT(requester->peer)->base.local_peer_id,
               sid, msg, requester->base.session_id);
    } break;

    // SIG_PKT_MSG_RESP: B→Server
    case SIG_PKT_MSG_RESP: { const char* PROTO = "MSG_RESP";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_MSG_RESP_MIN_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        int resp_len = (int)(payload_len - SIG_PKT_MSG_RESP_MIN_PSZ);
        if (resp_len > P2P_MSG_DATA_MAX) {
            print("E:", LA_F("%s: data too large (len=%d)\n", LA_F46, 46), PROTO, resp_len);
            return;
        }

        uint32_t session_id = nget_l(payload);
        uint16_t sid = nget_s(payload + 4);
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu32 " or sid=%u\n", LA_F52, 52),
                  PROTO, session_id, sid);
            return;
        }

        session_t *_s = NULL;
        HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
        compact_session_t *responder = (compact_session_t*)_s;
        if (!responder) {
            print("W:", LA_F("%s: unknown session_id=%" PRIu32 "\n", LA_F70, 70), PROTO, session_id);
            return;
        }

        if (!PEER_ONLINE(responder)) {
            print("W:", LA_F("%s: peer '%s' not online for session_id=%" PRIu32 "\n", LA_F60, 60),
                  PROTO, cs_remote_peer(responder), session_id);
            return;
        }

        uint8_t resp_code = payload[6];
        const uint8_t *resp_data = payload + 7;

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu32 ", sid=%u, code=%u, len=%d\n", LA_F31, 31),
              PROTO, session_id, sid, resp_code, resp_len);

        check_addr_change(udp_fd, COMPACT_CLIENT(responder), from);

        compact_send_msg_resp_ack_to_responder(udp_fd, from, COMPACT_CLIENT(responder)->base.local_peer_id,
                                       responder->base.session_id, sid);

        compact_session_t *requester = responder->peer;

        if (!requester->rpc_pending_next || requester->rpc_responding || requester->rpc_last_sid != sid) {
            print("W:", LA_F("%s: no matching pending msg (sid=%u, expected=%u)\n", LA_F56, 56),
                  PROTO, sid, requester->rpc_last_sid);
            return;
        }

        remove_compact_rpc_pending(requester);
        compact_transition_to_resp_pending(udp_fd, requester, P_tick_ms(), 0, resp_code, resp_data, resp_len);

        print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u (ses_id=%" PRIu32 ")\n", LA_F17, 17),
              PROTO, COMPACT_CLIENT(responder)->base.local_peer_id,
              COMPACT_CLIENT(requester)->base.local_peer_id,
              sid, requester->base.session_id);
    } break;

    // SIG_PKT_MSG_RESP_ACK: A→Server（A 确认收到 MSG_RESP）
    case SIG_PKT_MSG_RESP_ACK: { const char* PROTO = "MSG_RESP_ACK";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_MSG_RESP_ACK_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F41, 41), PROTO, payload_len);
            return;
        }

        uint32_t session_id = nget_l(payload);
        uint16_t sid = nget_s(payload + 4);

        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu32 " or sid=%u\n", LA_F52, 52),
                   PROTO, session_id, sid);
            return;
        }

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu32 ", sid=%u\n", LA_F33, 33),
               PROTO, session_id, sid);

        session_t *_s = NULL;
        HASH_FIND(hh_session, g_sessions, &session_id, sizeof(uint32_t), _s);
        compact_session_t *requester = (compact_session_t*)_s;
        if (!requester) {
            print("W:", LA_F("%s: unknown session_id=%" PRIu32 "\n", LA_F70, 70), PROTO, session_id);
            return;
        }

        if (!requester->rpc_responding || requester->rpc_last_sid != sid) {
            print("V:", LA_F("%s: no matching pending msg (sid=%u)\n", LA_F55, 55), PROTO, sid);
            return;
        }

        remove_compact_rpc_pending(requester);
        requester->rpc_responding = false;
        requester->rpc_retry = 0;

        print("I:", LA_F("%s: RPC complete for '%s', sid=%u (ses_id=%" PRIu32 ")\n", LA_F28, 28),
               PROTO, COMPACT_CLIENT(requester)->base.local_peer_id, sid, requester->base.session_id);
    } break;

    default:
        print("W:", LA_F("Unknown packet type 0x%02x from %s\n", LA_F121, 121), hdr->type, from_str);
        break;
    } // switch
}

// 清理过期的 COMPACT 模式客户端（超时未活跃）
static void cleanup_compact_clients(sock_t udp_fd) {

    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_compact_clients[i].base.valid) continue;
        if (tick_diff(now, g_compact_clients[i].base.last_active) <= COMPACT_PAIR_TIMEOUT_S * 1000) continue;

        print("W:", LA_F("Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n", LA_F120, 120),
               g_compact_clients[i].base.local_peer_id,
               tick_diff(now, g_compact_clients[i].base.last_active) / 1000.0);

        compact_clear_client(udp_fd, &g_compact_clients[i]);
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

    printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F134, 134),
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

    print("V:", LA_F("Send %s: mapped=%s:%d\n", LA_F110, 110),
          PROTO_ACK, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    ssize_t n = sendto(probe_fd, (const char *)buf, 4 + SIG_PKT_NAT_PROBE_ACK_PSZ, 0, (struct sockaddr *)from, sizeof(*from));
    if (n != 4 + SIG_PKT_NAT_PROBE_ACK_PSZ)
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F135, 135), PROTO_ACK, from_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n", LA_F136, 136),
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
        print("I: \n%s\n", LA_S7, 7("Received shutdown signal, exiting gracefully...", LA_S8, 8));
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
                    "Note: Run without arguments to use default configuration (port 9333)", LA_S3, 3));

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
        print("E:", LA_F("Invalid port number %d (range: 1-65535)\n", LA_F82, 82), port);
        ARGS_print(argv[0]);
        return 1;
    }
    if (ARGS_probe_port.i64 < 0 || ARGS_probe_port.i64 > 65535) {
        print("E:", LA_F("Invalid probe port %d (range: 0-65535)\n", LA_F83, 83), (int)ARGS_probe_port.i64);
        ARGS_print(argv[0]);
        return 1;
    }
    
    if (P_net_init() != E_NONE) {
        print("E:", LA_F("net init failed\n", LA_F144, 144));
        return 1;
    }

    // 初始化随机数生成器（用于生成安全的 session_id）
    P_rand_init();

    // 打印服务器配置信息
    print("I:", LA_F("Starting P2P signal server on port %d\n", LA_F119, 119), port);
    print("I:", LA_F("NAT probe: %s (port %d)\n", LA_F91, 91), 
          ARGS_probe_port.i64 > 0 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1), 
          (int)ARGS_probe_port.i64);
    print("I:", LA_F("Relay support: %s\n", LA_F101, 101), 
          ARGS_relay.i64 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1));

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

            print("E:", LA_F("probe UDP bind failed(%d)\n", LA_F145, 145), P_sock_errno());
            P_sock_close(probe_fd);
            probe_fd = P_INVALID_SOCKET;
            ARGS_probe_port.i64 = 0;  /* 绑定失败，禁用探测功能 */
            print("W:", LA_F("NAT probe disabled (bind failed)\n", LA_F89, 89));
        } 
        else {
            print("I:", LA_F("NAT probe socket listening on port %d\n", LA_F90, 90), (int)ARGS_probe_port.i64);
        }
    }

    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd, 10);
    print("I:", LA_F("P2P Signaling Server listening on port %d (TCP + UDP)...\n", LA_F98, 98), port);

    // 主循环
    fd_set read_fds;
    uint64_t last_cleanup = P_tick_ms(), last_compact_retry_check = last_cleanup;
    while (g_running) {

        uint64_t now = P_tick_ms();

        // 周期清理过期的 COMPACT 配对记录和 Relay 客户端连接
        if (tick_diff(now, last_cleanup) >= CLEANUP_INTERVAL_S * 1000) {
            cleanup_compact_clients(udp_fd);
            cleanup_relay_clients();
            last_cleanup = now;
        }
        
        // 检查并重传未确认的 SYNC 包 + MSG RPC 包（每秒检查一次）
        if (tick_diff(now, last_compact_retry_check) >= COMPACT_RETRY_INTERVAL_MS) {
            if (g_compact_sync0_pending_head) retry_compact_sync0_pending(udp_fd, now);
            if (g_compact_rpc_pending_head) retry_compact_rpc_pending(udp_fd, now);
            if (g_relay_rpc_pending_head)  retry_relay_rpc_pending(now);
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
            print("E:", LA_F("select failed(%d)\n", LA_F147, 147), P_sock_errno());
            break;
        }

        //-------------------------------

        // 如果存在新的 TCP 连接请求，accept 并将其添加到客户端列表中
        if (FD_ISSET(listen_fd, &read_fds)) {

            struct sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
            sock_t client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            
            // 设置为非阻塞模式，避免慢客户端阻塞整个服务器事件循环
            if (P_sock_nonblock(client_fd, true) != E_NONE) {
                print("W:", LA_F("[TCP] Failed to set client socket to non-blocking mode\n", LA_F129, 129));
            }
            
            int i = 0;
            for (i = 0; i < MAX_PEERS; i++) {

                // 查找一个空闲槽位来存储这个新的连接
                if (!g_relay_clients[i].base.valid) {

                    buffer_item_t *buf_item = relay_buf_alloc(RELAY_FRAME_SIZE);
                    if (!buf_item) {
                        print("E:", LA_F("[TCP] OOM: cannot allocate recv buffer for new client\n", LA_F132, 132));
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

                    print("V:", LA_F("[TCP] New connection from %s:%d\n", LA_F131, 131), 
                          inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    break;
                }
            }
            if (i == MAX_PEERS) {
                print("W:", LA_F("[TCP] Max peers reached, rejecting connection\n", LA_F130, 130));
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
                            print("V:", LA_F("ONLINE_ACK sent to '%s'\n", LA_F97, 97), client->base.local_peer_id);
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
                    if (hdr->size == 0) *(uint8_t**)&hdr += P2P_PEER_ID_MAX - P2P_SESS_ID_PSZ;

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
                            if (!((sending_session->send_head = item->next)))
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
    print("I: \n%s", LA_S8, 8("Shutting down...\n", LA_S9, 9));
    
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

    print("I:", LA_F("Goodbye!\n", LA_F81, 81));
    return 0;
}
