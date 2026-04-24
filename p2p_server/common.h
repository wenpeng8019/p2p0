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

///////////////////////////////////////////////////////////////////////////////

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

//-----------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct buffer_item {
    struct buffer_item*             next;
    void*                           refer;
    uint8_t                         flags;  // 低 5 bit，表示自定义意义标识; 高 3 bit 表示内存 chunk 大小。即 512 * 2^(flags >> 5)
} buffer_item_t;
#pragma pack(pop)
#define ITEM2BUF(item)              ((uint8_t*)(item + 1))
#define BUF2ITEM(buf)               (((buffer_item_t*)(buf)) - 1)

#define BUF_SIZE(flags)             (((flags) & 0xE0) == 0x20 ? P2P_MTU : 512 * (1 << ((flags) >> 5)))
#define BUF_FLAG_512(flags)         (0x00 | ((flags) & 0x1F))
#define BUF_FLAG_MTU(flags)         (0x20 | ((flags) & 0x1F))
#define BUF_FLAG_2048(flags)        (0x40 | ((flags) & 0x1F))
#define BUF_FLAG_4096(flags)        (0x60 | ((flags) & 0x1F))
#define BUF_FLAG_8192(flags)        (0x80 | ((flags) & 0x1F))
#define BUF_FLAG_16384(flags)       (0xA0 | ((flags) & 0x1F))
#define BUF_FLAG_32768(flags)       (0xC0 | ((flags) & 0x1F))
#define BUF_FLAG_65536(flags)       (0xE0 | ((flags) & 0x1F))

buffer_item_t* alloc_buffer(uint8_t flags);
void free_buffer(buffer_item_t *buf_item);

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

#define TCP_CLIENT  \
    uint8_t                         io;                 // bit 0: 当前是否需要读取；bit 1: 当前是否需要写入

typedef struct tcp_client
{
    client_t                        base;
    TCP_CLIENT
} tcp_client_t;

/*
 * @prame ws_handshake: 1:handshake recv; -1:handshake send; 0:handshake done
*/
#define WS_CLIENT  \
    wslay_event_context_ptr         ws_ctx; \
    int8_t                          ws_handshake; \
    buffer_item_t*                  buf; \
    uint16_t                        len; \
    uint16_t                        pos;

typedef struct ws_client
{
    client_t                        base;
    TCP_CLIENT
    WS_CLIENT
} ws_client_t;

typedef struct session_pair {
    bool                            valid;
    char                            peer_id[2][P2P_PEER_ID_MAX+1];    // hh_peer 复合 key 起始（与 remote_peer_id 连续）
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

#define TCP_IO_FLAG_WANT_READ       (1<<0)
#define TCP_IO_FLAG_WANT_WRITE      (1<<1)
#define TCP_IO_FLAG_READ_BREAK      (1<<2)  /* sock 出错或 would block，停止继续读取  */
#define TCP_IO_FLAG_WRITE_BREAK     (1<<3)  /* sock 出错或 would block，停止继续写入  */
#define TCP_IO_FLAG_CUSTOM_BIT      4

#define PEER_ONLINE(s)      ((s)->peer && (void*)(s)->peer != (void*)-1)  // 判断对端是否在线（peer 指针为 (void*)-1 表示已断开）


client_t*
find_client(const char *local_peer_id);

// 分配一个指定派生协议类型的 client 对象
// + 主要用于 UDP/COMPACT 协议调用，因为 TCP/xxx 协议在 accept 建链时就已经自动分配了 client 对象
client_t*
alloc_client(int8_t proto, sock_t fd);

// 由派生协议 client 调用，基类会根据协议类型自动执行不同派生协议类型的释放操作
void
free_client(client_t *c);

// 由派生协议 client 调用，用于实现基类的释放
void
free_client_base(client_t *c, void(*free_session)(session_t *s));

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

ssize_t
udp_send(sock_t udp_fd, const void *buf, int len, const struct sockaddr_in *to, const char *PROTO);

int
tcp_send(tcp_client_t* client, const void *buf, size_t *w_sz, const char *reason);
int
tcp_recv(tcp_client_t* client, void *buf, size_t *r_sz);

ret_t
ws_send_text(ws_client_t* client, const char *text);
ret_t
ws_send_data(ws_client_t* client, const uint8_t *data, size_t len);
ret_t
ws_close(ws_client_t *client, uint16_t code);

///////////////////////////////////////////////////////////////////////////////

#endif //P2P_TYPE_H
