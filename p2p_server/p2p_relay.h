//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_P2P_RELAY_H
#define P2P_P2P_RELAY_H

#include "common.h"

typedef struct relay_session {
    session_t                       base;

    uint8_t                         last_sid;                   // 会话同步的最后一个 sid

    /* 向对端发送的待处理队列（SYNC 通道：SYN0 / SYNC） */
    buffer_item_t*                  sync_peer_send;             // 待发给对端的 SYNC 数据（含缓存的 SYN0）
    buffer_item_t*                  sync_peer_sending;          // 当前正在发给对端的 SYNC 数据（TCP 写入中 或 等待应用层 ACK）
                                                                // refer=session → TCP写入中；refer=REFER_ACK_PENDING → 等待 ACK；=NULL 空闲

    /* 向对端发送的待处理队列（PKT 通道：数据中继） */
    buffer_item_t*                  pkt_peer_send;              // 待发给对端的 PKT 数据
    buffer_item_t*                  pkt_peer_sending;           // 当前正在发给对端的 PKT 数据

    /* RPC 忙标志（独立于 peer_send 的并行通道）*/
    uint16_t                        rpc_last_sid;
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
    TCP_CLIENT

    uint8_t                         last_error;                 

    uint8_t*                        recv_buf;
    uint16_t                        recv_len;

    buffer_item_t*                  send_buff_head;
    buffer_item_t*                  send_buff_rear;
    relay_session_t*                send_sess_head;
    relay_session_t*                send_sess_rear;
    relay_session_t*                sending_sess;               // 当前正在发送的 session; =NULL 表示正在发送 buff
    uint16_t                        sending_offset;
} relay_client_t;

void
relay_init(void);

bool
relay_init_client(relay_client_t* c);
void
relay_free_client(relay_client_t *client);

void
relay_handle_recv(relay_client_t *client);
void
relay_handle_send(relay_client_t *client);
void
relay_retry_pending(uint64_t now);


#endif //P2P_P2P_RELAY_H
