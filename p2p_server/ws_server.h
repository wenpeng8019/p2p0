/*
 * ws_server — 轻量 WebSocket 服务器（p2p_server 可选组件）
 *
 * 基于 wslay 事件 API（服务端模式），提供简单的多客户端 WebSocket 服务。
 * 适合作为 WebRTC 信令通道的接入端。
 *
 * 主要特性：
 *   - 单线程、poll 驱动，嵌入现有事件循环
 *   - 支持最多 WS_SERVER_MAX_CLIENTS 个并发连接
 *   - 自包含 HTTP Upgrade 握手（SHA-1 + Base64 内置）
 *   - 回调驱动：on_connect / on_message / on_disconnect
 *   - 支持向单个客户端发送或广播
 *
 * 典型用法：
 *   ws_server_t *srv = ws_server_create(&cfg, 8080);
 *   while (running) {
 *       ws_server_update(srv);
 *   }
 *   ws_server_destroy(srv);
 */

#ifndef WS_SERVER_H
#define WS_SERVER_H

#ifdef WITH_WSLAY

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 限制
 * ---------------------------------------------------------------------- */
#ifndef WS_SERVER_MAX_CLIENTS
#  define WS_SERVER_MAX_CLIENTS 32
#endif

/* -------------------------------------------------------------------------
 * 前向声明
 * ---------------------------------------------------------------------- */
typedef struct ws_server ws_server_t;

/* -------------------------------------------------------------------------
 * 消息类型
 * ---------------------------------------------------------------------- */
typedef enum {
    WS_SRV_MSG_TEXT   = 0x01,
    WS_SRV_MSG_BINARY = 0x02,
    WS_SRV_MSG_PING   = 0x09,
    WS_SRV_MSG_PONG   = 0x0A,
} ws_srv_msg_type_t;

/* -------------------------------------------------------------------------
 * 客户端 ID（从 1 起，0 表示无效）
 * ---------------------------------------------------------------------- */
typedef int ws_client_id_t;

/* -------------------------------------------------------------------------
 * 回调
 * ---------------------------------------------------------------------- */

/* 新客户端握手完成 */
typedef void (*ws_srv_on_connect_cb)(ws_server_t *srv,
                                     ws_client_id_t cid,
                                     void *user_data);

/* 收到客户端消息 */
typedef void (*ws_srv_on_message_cb)(ws_server_t *srv,
                                     ws_client_id_t cid,
                                     ws_srv_msg_type_t type,
                                     const uint8_t *data, size_t len,
                                     void *user_data);

/* 客户端断开 */
typedef void (*ws_srv_on_disconnect_cb)(ws_server_t *srv,
                                        ws_client_id_t cid,
                                        void *user_data);

/* -------------------------------------------------------------------------
 * 配置
 * ---------------------------------------------------------------------- */
typedef struct {
    ws_srv_on_connect_cb    on_connect;     /* 可为 NULL */
    ws_srv_on_message_cb    on_message;     /* 可为 NULL */
    ws_srv_on_disconnect_cb on_disconnect;  /* 可为 NULL */
    void                   *user_data;      /* 透传给所有回调 */

    /* 允许的 WebSocket 子协议（如 "webrtc-signal"），NULL 表示不校验 */
    const char             *sub_protocol;
} ws_server_cfg_t;

/* -------------------------------------------------------------------------
 * 生命周期
 * ---------------------------------------------------------------------- */

/* 创建服务器，绑定并监听 port */
ws_server_t *ws_server_create(const ws_server_cfg_t *cfg, uint16_t port);

/* 销毁服务器，关闭所有连接和监听 socket */
void ws_server_destroy(ws_server_t *srv);

/* -------------------------------------------------------------------------
 * 主循环驱动
 * ---------------------------------------------------------------------- */

/* 处理新连接 + 现有客户端收发，应每帧调用一次 */
void ws_server_update(ws_server_t *srv);

/* -------------------------------------------------------------------------
 * 发送
 * ---------------------------------------------------------------------- */

/* 向指定客户端发送文本帧，返回 0 成功，-1 失败 */
int ws_server_send_text(ws_server_t *srv, ws_client_id_t cid, const char *text);

/* 向指定客户端发送二进制帧，返回 0 成功，-1 失败 */
int ws_server_send_binary(ws_server_t *srv, ws_client_id_t cid,
                           const uint8_t *data, size_t len);

/* 向所有已连接客户端广播文本帧 */
void ws_server_broadcast_text(ws_server_t *srv, const char *text);

/* 断开指定客户端（发送 Close 帧）*/
void ws_server_disconnect(ws_server_t *srv, ws_client_id_t cid, uint16_t code);

/* -------------------------------------------------------------------------
 * 查询
 * ---------------------------------------------------------------------- */

/* 返回当前在线客户端数量 */
int ws_server_client_count(const ws_server_t *srv);

#ifdef __cplusplus
}
#endif

#endif /* WITH_WSLAY */

#endif /* WS_SERVER_H */
