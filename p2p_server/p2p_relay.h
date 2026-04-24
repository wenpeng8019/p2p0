//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_P2P_RELAY_H
#define P2P_P2P_RELAY_H

#include "common.h"

#define RELAY_BUF_FLAGS_SYNC_FIN    0x01    // SYNC 包尾部 FIN 标记（告知服务器这是最后一包候选）

typedef struct relay_session {
    session_t                       base;

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
    TCP_CLIENT

    uint8_t*                        recv_buf;
    uint16_t                        recv_len;

    bool                            reg_ack_pending;            // REG ACK 待发送标志（复用 recv_buf）

    buffer_item_t*                  sending_buff_head;
    buffer_item_t*                  sending_buff_rear;
    relay_session_t*                sending_sess_head;
    relay_session_t*                sending_sess_rear;
    int                             send_offset;                // <0 表示当前正在发送的是 buff; >=0 表示正在发送 sess，值为已发送字节数
} relay_client_t;

void
relay_init(void);

bool
relay_init_client(relay_client_t* c);
void
relay_term_client(relay_client_t *c, bool and_free);

void
relay_handle_recv(relay_client_t *client);
void
relay_handle_send(relay_client_t *client);
void
relay_retry_pending(uint64_t now);

#endif //P2P_P2P_RELAY_H
