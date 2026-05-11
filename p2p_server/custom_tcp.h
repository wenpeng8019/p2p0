//
// Created by 温朋 on 2026/4/18.
//

#ifndef P2P_CUSTOM_TCP_H
#define P2P_CUSTOM_TCP_H

#include "common.h"

#define CUSTOM_TCP_SESSION   \
    buffer_queue_t*                 send_queue; \
    struct ct_session*              send_prev; \
    struct ct_session*              send_next;

typedef struct ct_session {
    session_t                       base;
    CUSTOM_TCP_SESSION
} ct_session_t;

#define CT_CLIENT(s)                ((ct_client_t*)CLIENT(s))
#define CT_PEER(s)                  ((ct_session_t*)PEER(s))

#define CUSTOM_TCP_CLIENT   \
    int16_t                         last_error;         \
    uint8_t*                        hdr_rs;             \
    uint16_t                        hdr_sz;             \
    buf16_item_t*                   recv_buf;           \
    uint16_t                        recv_cur;           \
    buf16_item_t*                   payload_buf;        \
    uint32_t                        payload_cur;        \
    buffer_queue_t                  send_buff_queue;    \
    ct_session_t*                   send_sess_head;     \
    ct_session_t*                   send_sess_rear;     \
    ct_session_t*                   sending_sess;       \
    uint32_t                        sending_cur;

// RELAY 模式客户端（TCP 长连接）- 统一接收通道
typedef struct ct_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_TCP_CLIENT
} ct_client_t;

#define CUSTOM_TCP_ERR_DISCONNECTED 1      // 网络端口连接
#define CUSTOM_TCP_ERR_IO           2      // 网络 I/O 错误（连接异常/读写失败）
#define CUSTOM_TCP_ERR_OVERFLOW     3      // 请求协议包数据过大（size 超出限制）
#define CUSTOM_TCP_ERR_INTERNAL     4      // 服务器内部错误。此时应该断开和服务器的连接，等待重连恢复
#define CUSTOM_TCP_ERR_PROTOCOL     5
#define CUSTOM_TCP_ERR_CUSTOM       6

#define ITEM_REF_ACK_PENDING        ((void*)(uintptr_t)1)
#define ITEM_REF_STATIC             ((void*)(uintptr_t)2)

typedef bool (*ct_resolve_payload_len_cb)(uint8_t* hdr_buf, uint16_t hdr_len,
                                          uint32_t* payload_len, uint16_t* payload_offset);
typedef buf16_item_t* (*ct_handle_handshake_cb)(ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                                                buf16_item_t* payload0, buf16_item_t* payload1);
typedef buf16_item_t* (*ct_handshake_finish_cb)(ct_client_t *client);
typedef void (*ct_handle_proto_cb)(ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                                   buf16_item_t* payload0, buf16_item_t* payload1);

typedef void (*ct_handle_peer_sent_cb)(ct_session_t *session, buf16_item_t *buf_item);
typedef void (*ct_session_break_cb)(ct_session_t *session, ct_session_t *peer, break_mode_e break_mode);

typedef void (*ct_client_unreachable_cb)(ct_client_t *client, bool readOrWrite);
typedef buf16_item_t* (*ct_error_item_cb)(ct_client_t *client, bool handshake);

typedef struct custom_tcp_ctx {
    uint32_t                        max_payload_len;
    ct_resolve_payload_len_cb       resolve_payload_len;
    ct_handle_handshake_cb          handle_handshake;
    ct_handshake_finish_cb          handshake_finish/* nullable */;
    ct_handle_proto_cb              handle_proto;
    ct_handle_peer_sent_cb          handle_peer_sent;
    ct_session_break_cb             session_break;
    ct_client_unreachable_cb        client_unreachable/* nullable */;
    buf16_item_t*                   fatal_item;
    ct_error_item_cb                error_item;
} custom_tcp_ctx_t;

bool
ct_init_client(ct_client_t* client);
void
ct_free_client(custom_tcp_ctx_t* ctx, ct_client_t *client);
void
ct_reactive_client(custom_tcp_ctx_t* ctx, ct_client_t *client);

void
ct_client_error(custom_tcp_ctx_t* ctx, ct_client_t *client, int16_t error, bool fatal);
void
ct_client_off(custom_tcp_ctx_t* ctx, ct_client_t *client);

void
ct_close_session(custom_tcp_ctx_t* ctx, ct_session_t *session, bool terminate);

void
ct_handle_recv(custom_tcp_ctx_t* ctx, ct_client_t *client, const char* SP);
void
ct_handle_send(custom_tcp_ctx_t* ctx, ct_client_t *client, const char* SP);

void
ct_client_send(ct_client_t *client, buf16_item_t* buf_item, bool immediate);
void 
ct_session_send(ct_session_t *session, buf16_item_t* buf_item);

#endif //P2P_CUSTOM_TCP_H
