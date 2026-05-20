//
// custom_ws.h — 基于 custom_tcp 的 WebSocket 服务端基础封装
//
// 将 HTTP 握手和 WS 帧收发统一封装，上层只需实现少量回调即可复用完整的 WS 连接生命周期。
//
// 工作原理：
//   握手阶段：流模式（recv_buf != NULL），hdr_rs="\r\n\r\n"，扫描 HTTP header 结束边界
//   正常阶段：帧模式（recv_buf == NULL），hdr_rs 指向 14 字节静态缓冲，动态扩展解析 WS 帧头
//
// WS 帧头解析（RFC 6455）：
//   2 字节 base header → 根据 payload len 字段决定是否扩展到 4/10 字节（server 端无 mask 读取）
//   客户端必须对 payload 做 mask，框架在 handle_proto 回调前会自动解掉 mask
//
// payload_offset 用途同 custom_tcp：上层在 resolve_payload_len 中指定前置保留空间，
//   可在接收到帧后直接在前置空间写入新帧头，实现零拷贝转发。
//
// 注意：custom_ws 不依赖 wslay，完全基于 custom_tcp 原生帧模式实现。

#ifndef P2P_CUSTOM_WS_H
#define P2P_CUSTOM_WS_H

#include "custom_tcp.h"

//-----------------------------------------------------------------------------
// WS opcode 常量（RFC 6455 §5.2）
// 0x3–0x7: 保留非控制帧；0xB–0xF: 保留控制帧

#define WS_OP_CONTINUATION          0x0   // 续帧（分片消息的后续帧）
#define WS_OP_TEXT                  0x1   // 文本帧（UTF-8 编码）
#define WS_OP_BINARY                0x2   // 二进制帧
#define WS_OP_CLOSE                 0x8   // 连接关闭帧（控制帧，payload 可含 2 字节状态码）
#define WS_OP_PING                  0x9   // Ping 帧（控制帧，payload <= 125 字节）
#define WS_OP_PONG                  0xA   // Pong 帧（控制帧，回复 ping 或主动心跳）

// WS frame hdr size class（占用 buf_item->flags 低 2 bit）
#define CW_BUF_FLAG_HDR_SIZE        0x3
#define CW_BUF_HDR_2                0x1   // payload_len <= 125，对应 2 字节 hdr
#define CW_BUF_HDR_4                0x2   // 126 <= payload_len <= 65535，对应 4 字节 hdr
#define CW_BUF_HDR_10               0x3   // payload_len > 65535，对应 10 字节 hdr

//-----------------------------------------------------------------------------
// WebSocket Close Code 预定义（RFC 6455 §7.4.1）
//
// 1000~1011: 标准定义
// 1004/1005/1006/1015: 仅用于内部，不允许实际发送
// 3000~4999: 应用自定义
#define WS_CLOSE_NORMAL             1000  // 正常关闭，连接完成了它的目的
#define WS_CLOSE_GOING_AWAY         1001  // 端点离开（如服务器关闭/浏览器跳转）
#define WS_CLOSE_PROTOCOL_ERROR     1002  // 协议错误
#define WS_CLOSE_UNSUPPORTED_DATA   1003  // 不支持的数据类型
//      WS_CLOSE_RESERVED           1004  // 保留，不得使用
//      WS_CLOSE_NO_STATUS          1005  // 仅内部使用，不得发送
//      WS_CLOSE_ABNORMAL           1006  // 仅内部使用，不得发送
#define WS_CLOSE_INVALID_DATA       1007  // 非法数据内容（如文本帧非 UTF-8）
#define WS_CLOSE_POLICY_VIOLATION   1008  // 策略原因关闭
#define WS_CLOSE_MESSAGE_TOO_BIG    1009  // 消息过大
#define WS_CLOSE_MANDATORY_EXT      1010  // 缺少必要扩展
#define WS_CLOSE_INTERNAL_ERROR     1011  // 服务器内部错误
// 扩展: 部分实现
#define WS_CLOSE_SERVICE_RESTART    1012  // 服务重启（扩展/部分实现）
#define WS_CLOSE_TRY_AGAIN_LATER    1013  // 临时不可用，建议稍后重试（扩展）
#define WS_CLOSE_BAD_GATEWAY        1014  // 网关错误（扩展）
#define WS_CLOSE_TLS_HANDSHAKE      1015  // TLS 握手失败，仅内部使用
// 3000~4999: 应用自定义

//-----------------------------------------------------------------------------
/**
 * @brief                           WS client 扩展字段（内联宏，供派生结构体使用）
 * @details
 *  ws_hdr_buf                      14 字节静态缓冲，帧模式下作为 hdr_rs 使用，用于存放 WS 帧头字节
 *  ws_opcode                       当前帧的 opcode，FIN bit 位于 bit7，即 0x80 | opcode
 *  ws_frag_q                       分片帧聚合队列，收到非 FIN 帧时追加，空队列表示当前没有分片进行中
 *  ws_frag_len                     ws_frag_q 中已聚合的总字节数
 *  ws_utf8state                    TEXT 帧 UTF-8 DFA 当前状态，跨分片保持，0 表示 ACCEPT
 *  last_reason                     可选的 WS close frame，由 cw_alloc_frame(WS_OP_CLOSE, 2 + reason_len) 分配
 *                                  pos 指向 WS payload 起始处，[pos..pos+1] 留给 close code，reason 从 [pos+2..len) 开始
 */
#define CUSTOM_WS_CLIENT \
    uint8_t                         ws_hdr_buf[14]; \
    uint8_t                         ws_opcode;      \
    buffer_queue_t                  ws_frag_q;      \
    uint32_t                        ws_frag_len;    \
    uint32_t                        ws_utf8state;   \
    buf16_item_t*                   last_reason;    \
    uint8_t                         ws_close_frame_buf[sizeof(buf16_item_t) + 4];

// 基础 WS client 类型（可通过 CUSTOM_WS_CLIENT 内联到派生结构体）
typedef struct cw_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_TCP_CLIENT
    CUSTOM_WS_CLIENT
} cw_client_t;

#define CW_CLIENT(s)     ((cw_client_t*)CLIENT(s))

// I/O 关闭状态标志（复用 TCP_IO_FLAG_CUSTOM_BIT）
#define CW_IO_FLAG_CLOSING  (1 << TCP_IO_FLAG_CUSTOM_BIT)  // 本端已发 close 帧，等待对端 close 或超时

typedef struct cw_client_ctx cw_client_ctx_t;

//-----------------------------------------------------------------------------
// 回调类型

/**
 * @brief                           应用层握手回调
 * @param ctx
 * @param t_client                  当前 client 指针地址，可在回调内替换为 resident 后的新 client
 * @param opcode                    WS_OP_TEXT 或 WS_OP_BINARY
 * @param payload                   payload 数据指针。握手阶段不支持分片聚合，因此这里必定非 NULL
 * @param payload_len               payload 总字节数
 * @param buf_item                  payload 所属的 buf_item，可直接复用来构造并返回应答
 * @return                          返回握手应答 frame；返回 NULL 则表示握手失败
 */
typedef buf16_item_t* (*cw_handle_handshake_cb)(cw_client_ctx_t *ctx, cw_client_t ** t_client, uint8_t opcode,
                                                uint8_t *payload, uint32_t payload_len,
                                                buf16_item_t *buf_item);

/**
 * @brief                           收到完整 WS 数据帧时调用
 * @param ctx
 * @param client
 * @param opcode                    WS_OP_TEXT 或 WS_OP_BINARY
 * @param payload                   payload 数据指针。若为分片聚合帧，则这里为 NULL，实际总长度由 payload_len 给出
 * @param payload_len               payload 总字节数，分片场景下为所有分片聚合后的总和
 * @param buf_item                  payload 所属的 buf_item，可用于零拷贝转发
 *                                  若 payload 来自 custom_tcp 的 payload0，则这里为 NULL，需要上层自行复制后转发
 * @return                          无
 */
typedef void (*cw_handle_frame_cb)(cw_client_ctx_t *ctx, cw_client_t *client, uint8_t opcode,
                                   uint8_t *payload, uint32_t payload_len,
                                   buf16_item_t *buf_item);

/**
 * @brief                           收到 ping 帧时调用
 * @param ctx
 * @param client
 * @param data
 * @param len
 * @return                          无
 */
typedef void (*cw_handle_ping_cb)(cw_client_ctx_t *ctx, cw_client_t *client, const uint8_t *data, uint8_t len);

/**
 * @brief                           对端发起 close 握手时调用
 * @param ctx
 * @param client
 * @param code
 * @return                          无
 */
typedef void (*cw_handle_close_cb)(cw_client_ctx_t *ctx, cw_client_t *client, uint16_t code);

/**
 * @brief                           HTTP Upgrade 握手完成时调用
 * @param ctx
 * @param client
 * @return                          返回 false 表示拒绝连接，框架会发送 403 并关闭连接
 */
typedef bool (*cw_handshake_done_cb)(cw_client_ctx_t *ctx, cw_client_t *client);

//-----------------------------------------------------------------------------
// 协议上下文（每个 WS 子协议类型共享一个）

struct cw_client_ctx {
    ct_client_ctx_t                 base;

    /**
     * @brief                       WS 子协议名，对应 HTTP 握手中的 Sec-WebSocket-Protocol 字段
     */
    const char*                     sub_protocol;

    /**
     * @brief                       应用层握手处理回调
     */
    cw_handle_handshake_cb          handle_handshake;

    /**
     * @brief                       收到数据帧时调用
     */
    cw_handle_frame_cb              handle_frame;

    /**
     * @brief                       收到 ping 帧时调用，可为空
     */
    cw_handle_ping_cb               handle_ping;

    /**
     * @brief                       对端发起 close 时调用，可为空
     */
    cw_handle_close_cb              handle_close;

    uint8_t                         fatal_frame_buf[sizeof(buf16_item_t) + 4];
};

///////////////////////////////////////////////////////////////////////////////
// Public API

/**
 * @brief                           初始化 WS 协议上下文，并绑定内部回调到 ctx->base
 * @param ctx
 * @return                          无
 */
void
cw_ctx_init(cw_client_ctx_t *ctx);

// custom ws 可被重载的接口实现
void cw_client_free(client_ctx_t* ctx, client_t *c);
bool cw_client_reset(client_ctx_t* ctx, client_t *c, bool init);

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief                           分配一个完整的 WS frame 缓冲
 * @param opcode
 * @param payload_len               WS payload 总长度
 * @return                          返回后可直接向 ITEM2BUF(item) + item->pos 写入 payload；len 已固定为整帧总长
 */
buf16_item_t*
cw_alloc_frame(uint8_t opcode, uint32_t payload_len);

/**
 * @brief                           分配一个 WS text frame，并将 printf 格式化结果写入 payload
 * @param expect_sz                 预期 payload 缓冲大小，包含结尾 '\0' 预留
 * @param fmt
 * @param args
 * @return                          返回后 frame->pos 指向 payload 起始处
 */
buf16_item_t*
cw_vprintf_frame(uint32_t expect_sz, const char *fmt, va_list args);

static inline buf16_item_t* cw_printf_frame(uint32_t expect_sz, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    buf16_item_t *item = cw_vprintf_frame(expect_sz, fmt, args);
    va_end(args);
    return item;
}

/**
 * @brief                           在已有 buf_item 上构建 WS frame
 * @param opcode                    WS_OP_TEXT、WS_OP_BINARY 或 WS_OP_CONTINUATION
 * @param buf_item                  上层分配的 buf_item
 * @param payload_offset            payload 数据在 buf_item 中的起始偏移
 *                                  框架会在 [0, payload_offset) 区间写入 WS 帧头，因此该值必须不小于有效 hdr size
 * @return                          0 表示成功，其他值表示失败
 */
ret_t
cw_build_frame(uint8_t opcode, buf16_item_t *buf_item, uint16_t payload_offset);

//-----------------------------------------------------------------------------

/**
 * @brief                           发送已构建完成的 WS frame 到 client
 * @param client
 * @param frame                     由 cw_alloc_frame 或 cw_build_frame 生成
 * @param immediate
 * @return                          0 表示成功，其他值表示失败
 */
ret_t
cw_client_send(cw_client_t *client, buf16_item_t *frame, bool immediate);

/**
 * @brief                           发送已构建完成的 WS frame 到 session
 * @param session
 * @param frame                     由 cw_alloc_frame 或 cw_build_frame 生成
 * @return                          0 表示成功，其他值表示失败
 */
ret_t
cw_session_send(ct_session_t *session, buf16_item_t *frame);

//-----------------------------------------------------------------------------

/**
 * @brief                           服务端主动发起 graceful close
 * @param client
 * @param code                      0 或 WS_CLOSE_NORMAL 表示正常关闭
 * @param reason                    可选 close reason，受 WS 控制帧 125 字节限制
 * @return                          0 表示成功，其他值表示失败
 */
ret_t
cw_close(cw_client_t *client, uint16_t code, const char* reason/* nullable */);

/**
 * @brief                           检查 graceful close 是否超时
 * @param client
 * @param now
 * @return                          无
 */
void
cw_retry_closing(cw_client_t *client, uint64_t now);

///////////////////////////////////////////////////////////////////////////////

#endif // P2P_CUSTOM_WS_H

