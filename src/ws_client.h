/*
 * ws_client — 轻量 WebSocket 客户端
 *
 * 封装 wslay 事件 API（客户端模式），用于与浏览器的 WebRTC 信令通道交互。
 *
 * 主要特性：
 *   - 纯 C99，无外部依赖（SHA-1 和 Base64 内置）
 *   - 非阻塞 TCP + wslay 帧层，集成在应用主循环中调用
 *   - 支持 Text / Binary / Ping / Close 帧
 *   - 回调驱动：on_open / on_message / on_close
 *
 * 典型用法：
 *   ws_client_t *c = ws_client_create(&cfg);
 *   ws_client_connect(c, "192.168.1.1", 8080, "/signal");
 *   while (running) {
 *       ws_client_update(c);
 *       // 按需发送
 *       ws_client_send_text(c, "{\"type\":\"offer\",...}");
 *   }
 *   ws_client_destroy(c);
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#ifdef WITH_WSLAY

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 前向声明
 * ---------------------------------------------------------------------- */
typedef struct ws_client ws_client_t;

/* -------------------------------------------------------------------------
 * 连接状态
 * ---------------------------------------------------------------------- */
typedef enum {
    WS_CLIENT_CLOSED      = 0,   /* 未连接 / 已关闭 */
    WS_CLIENT_CONNECTING  = 1,   /* TCP 连接中（非阻塞）*/
    WS_CLIENT_HANDSHAKING = 2,   /* HTTP Upgrade 握手中 */
    WS_CLIENT_OPEN        = 3,   /* WebSocket 已就绪 */
    WS_CLIENT_CLOSING     = 4,   /* 正在发送 Close 帧 */
    WS_CLIENT_ERROR       = 5,   /* 不可恢复错误 */
} ws_client_state_t;

/* -------------------------------------------------------------------------
 * 消息类型（与 wslay opcode 对应）
 * ---------------------------------------------------------------------- */
typedef enum {
    WS_MSG_TEXT   = 0x01,
    WS_MSG_BINARY = 0x02,
    WS_MSG_CLOSE  = 0x08,
    WS_MSG_PING   = 0x09,
    WS_MSG_PONG   = 0x0A,
} ws_msg_type_t;

/* -------------------------------------------------------------------------
 * 回调类型
 * ---------------------------------------------------------------------- */

/* WebSocket 握手完成，连接已就绪 */
typedef void (*ws_client_on_open_cb)(ws_client_t *c, void *user_data);

/* 收到完整消息帧 */
typedef void (*ws_client_on_message_cb)(ws_client_t *c,
                                        ws_msg_type_t type,
                                        const uint8_t *data, size_t len,
                                        void *user_data);

/* 连接已关闭（状态码 / 原因可为 0/NULL）*/
typedef void (*ws_client_on_close_cb)(ws_client_t *c,
                                      uint16_t status_code,
                                      const char *reason,
                                      void *user_data);

/* -------------------------------------------------------------------------
 * 配置结构
 * ---------------------------------------------------------------------- */
typedef struct {
    ws_client_on_open_cb    on_open;      /* 可为 NULL */
    ws_client_on_message_cb on_message;   /* 可为 NULL */
    ws_client_on_close_cb   on_close;     /* 可为 NULL */
    void                   *user_data;    /* 透传给所有回调 */

    /* 额外 HTTP 头（如 "Authorization: Bearer xxx\r\n"），可为 NULL */
    const char             *extra_headers;
} ws_client_cfg_t;

/* -------------------------------------------------------------------------
 * 生命周期
 * ---------------------------------------------------------------------- */

/* 创建客户端实例，不发起连接 */
ws_client_t *ws_client_create(const ws_client_cfg_t *cfg);

/* 销毁实例，关闭 socket（不发送 Close 帧）*/
void ws_client_destroy(ws_client_t *c);

/* -------------------------------------------------------------------------
 * 连接 / 断开
 * ---------------------------------------------------------------------- */

/*
 * 发起非阻塞连接。
 * host  — 主机名或 IP（仅支持 IPv4 和 IPv6 数字地址，不做 DNS 解析）
 * port  — 端口
 * path  — WebSocket 路径，如 "/signal"
 * 返回 0 成功（连接异步进行），-1 失败。
 */
int ws_client_connect(ws_client_t *c,
                          const char *host, uint16_t port,
                          const char *path);

/*
 * 发送 Close 帧并进入 CLOSING 状态（socket 在下次 update 后关闭）。
 * status_code = 1000 表示正常关闭。
 */
void ws_client_close(ws_client_t *c, uint16_t status_code);

/* -------------------------------------------------------------------------
 * 主循环驱动（需在应用主循环中周期性调用）
 * ---------------------------------------------------------------------- */

/*
 * 推进连接状态机：处理 TCP 连接完成、HTTP 握手、wslay 收发。
 * 触发对应回调。
 * 应每帧或每次事件循环调用一次。
 */
void ws_client_update(ws_client_t *c);

/* -------------------------------------------------------------------------
 * 发送
 * ---------------------------------------------------------------------- */

/* 发送 UTF-8 文本帧（data 以 '\0' 结尾），返回 0 成功，-1 失败 */
int ws_client_send_text(ws_client_t *c, const char *text);

/* 发送二进制帧，返回 0 成功，-1 失败 */
int ws_client_send_binary(ws_client_t *c,
                               const uint8_t *data, size_t len);

/* -------------------------------------------------------------------------
 * 查询
 * ---------------------------------------------------------------------- */
ws_client_state_t ws_client_state(const ws_client_t *c);

#ifdef __cplusplus
}
#endif

#endif /* WITH_WSLAY */

#endif /* WS_CLIENT_H */
