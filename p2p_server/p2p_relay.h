//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_RELAY_H
#define P2P_RELAY_H

#include "common.h"
#include "custom_tcp.h"

// 每个通道（SYNC / PKT）向对端发送的最大深度（队列容量）
// 超过此深度时向上游返回 BUSY，由上游控速
#define RELAY_PEER_Q_MAX  2u

typedef struct relay_session {
    session_t                       base;
    CUSTOM_TCP_SESSION

    /* SYNC 同步服务相关字段 */
    uint8_t                         last_sid;                           // 会话同步的最后一个 sid
    buf16_item_t*                   sync_peer_slots[RELAY_PEER_Q_MAX];  // 循环队列的 slots 数组（指针）
    buffer_round_t                  sync_peer_send;                     // 发送队列（循环缓冲）
                                                                        // 队头 FRONT 的发送状态由 refer 判定：
                                                                        //   refer=NULL            → 待发但未启动（对端暂不可达）
                                                                        //   refer=session         → TCP 写入中
                                                                        //   refer=REFER_ACK_PENDING → 等待应用层 ACK

    /* PKT 中继服务相关字段 */
    buf16_item_t*                   pkt_peer_slots[RELAY_PEER_Q_MAX];   // 循环队列的 slots 数组（指针）
    buffer_round_t                  pkt_peer_send;                      // 发送队列（循环缓冲）

    /* RPC 服务相关字段 */
    uint16_t                        rpc_last_sid;                       // 最后一个 RPC sid，维护 RPC 序列一致性（sid 循环递增）
    uint16_t                        rpc_pending_sid;                    // RPC 生命周期锁：0=空闲，非0=进行中的 RPC sid
                                                                        // 全程：REQ→转发→RSP→转发回来才解锁
                                                                        // RSP 返回时验证 sid 一致性
    uint64_t                        rpc_sent_time;                      // RPC 发起时间戳（毫秒，用于超时检测）
    struct relay_session*           rpc_pending_next;                   // RPC 待确认链表指针（NULL=不在链表中，-1=链表尾）

} relay_session_t;

// RELAY 模式客户端（TCP 长连接）- 统一接收通道
typedef struct relay_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_TCP_CLIENT

    // 帧模式（frame mode）的 header 缓冲（4 字节，用于 hdr_rs 指向）
    uint8_t                         hdr_buf[sizeof(p2p_relay_hdr_t)];

    // 预分配的 ALV ACK 缓冲（内嵌，避免每次动态分配）
    uint8_t                         alv_ack_buf[sizeof(buf16_item_t) + sizeof(p2p_relay_hdr_t)];

} relay_client_t;

ct_client_ctx_t*
relay_init(void);

bool
relay_init_client(relay_client_t* client);
void
relay_free_client(client_t *client);

void
relay_retry_pending(uint64_t now);


#endif //P2P_RELAY_H
