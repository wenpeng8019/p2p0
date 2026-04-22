//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_P2P_COMPACT_H
#define P2P_P2P_COMPACT_H

#include "common.h"

typedef struct compact_session {
    session_t                       base;

    uint8_t                         addr_notify_seq;            // 发给对端的地址变更通知序号（base_index，1..255 循环）

    p2p_candidate_t                 candidates[MAX_CANDIDATES]; // 候选列表（网络格式，直接收发）
    int                             candidate_count;            // 候选数量

    // SYNC(seq=0) 可靠传输（首包 + 地址变更通知）
    // sync0_acked 状态机：
    //   0  = 初始，未收到过来自客户端的 SYNC0_ACK
    //   1  = 客户端对服务器返回的 SYNC0_ACK 的（二次）确认。服务器在达成该状态前，确保不向客户端转发来自对端的 SYNC0
    //   2  = 客户端对（服务器转发的）对端 SYNC0 的 ACK 确认
    //  -1  = 重传超时放弃
    int                             sync0_acked;
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

    UT_hash_handle                  hh;                         // 按 auth_key 索引（client↔server 鉴权查找）
} compact_client_t;

#define COMPACT_CLIENT(s)   ((compact_client_t*)((session_t*)(s))->client)
#define COMPACT_PEER(s)     ((compact_session_t*)((session_t*)(s))->peer)

void
compact_init(void);

bool
compact_init_client(compact_client_t* c, struct sockaddr_in *from);
void
compact_free_client(compact_client_t *c);

void
compact_handle_signaling(sock_t udp_fd, uint8_t *buf, size_t len, struct sockaddr_in *from);
void
compact_retry_pending(sock_t udp_fd, uint64_t now);


#endif //P2P_P2P_COMPACT_H
