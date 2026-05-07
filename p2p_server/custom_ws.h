//
// custom_ws.h — 通用 WebSocket 服务端抽象（对应 custom_tcp.h）
//
// 将 HTTP 握手、wslay 帧收发、连接关闭等基础设施统一封装，
// 上层协议（如 p2p_wss）只需实现少量回调即可复用完整的 WS 连接生命周期管理。
//

#ifndef P2P_CUSTOM_WS_H
#define P2P_CUSTOM_WS_H

#include "common.h"

#ifdef WITH_WSLAY
#include <wslay/wslay.h>
#endif

typedef struct custom_ws_ctx custom_ws_ctx_t;

// I/O 关闭状态标志（占用 TCP_IO_FLAG_CUSTOM_BIT 起始的两位）
#define CW_IO_FLAG_CLOSING      (1<<TCP_IO_FLAG_CUSTOM_BIT)       // 本端已发 close 帧，等待对端响应或超时

// WS client 基础字段宏（供派生结构体内联，对应 CUSTOM_TCP_CLIENT）
#define CUSTOM_WS_CLIENT \
    wslay_event_context_ptr         ws_ctx;     \
    custom_ws_ctx_t*                ctx;        \
    buffer_item_t*                  buf;        \
    uint16_t                        len;        \
    uint16_t                        pos;

// WS 基础 client 类型（对应 ct_client_t）
typedef struct cw_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_WS_CLIENT
} cw_client_t;

#define CW_CLIENT(c)    ((cw_client_t*)(c))

//-----------------------------------------------------------------------------
// 回调类型

// 收到 text 帧（UTF-8 消息）
typedef void (*cw_handle_text_cb)(cw_client_t *client, const uint8_t *msg, size_t len);

// 收到 binary 帧
typedef void (*cw_handle_data_cb)(cw_client_t *client, const uint8_t *data, size_t len);

// 发送队列全部发送完成（清空）时调用
typedef void (*cw_handle_send_complete_cb)(cw_client_t *client);

//-----------------------------------------------------------------------------

struct custom_ws_ctx {
    const char*                     sub_protocol;           // WebSocket 子协议名，NULL 表示不协商
    cw_handle_text_cb               handle_text;            // 收到 text 帧（nullable）
    cw_handle_data_cb               handle_data;            // 收到 binary 帧（nullable）
    cw_handle_send_complete_cb      handle_send_complete;   // 发送队列清空（nullable）
};

//-----------------------------------------------------------------------------
// Public API

bool
cw_init_client(cw_client_t *client);
void
cw_free_client(custom_ws_ctx_t *ctx, cw_client_t *client);

// tcp sock 接收/发送处理
void
cw_handle_recv(custom_ws_ctx_t *ctx, cw_client_t *client);
void
cw_handle_send(custom_ws_ctx_t *ctx, cw_client_t *client);

// 服务器主动触发的 grace close 超时检查
void
cw_retry_closing(custom_ws_ctx_t *ctx, cw_client_t *client, uint64_t now);

// 发送 text / binary 帧
ret_t cw_send_text(cw_client_t *client, const char *text);
ret_t cw_send_data(cw_client_t *client, const uint8_t *data, size_t len);

// ws 协议允许发送 close 帧来优雅地关闭连接
// + 但作为服务器应用，似乎没有主动触发优雅关闭客户端的场景
ret_t cw_close(cw_client_t *client, uint16_t code);

#endif // P2P_CUSTOM_WS_H
