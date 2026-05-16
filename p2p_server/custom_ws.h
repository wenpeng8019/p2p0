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
// WS client 扩展字段（内联宏，供派生结构体使用）
//
// + ws_ctx:        指向所属的 custom_ws_ctx_t（init 时保存，回调中用于访问 ws 级配置）
// + ws_hdr_buf:    14 字节静态缓冲，帧模式下作为 hdr_rs 使用（存放 WS 帧头字节）
// + ws_opcode:     当前帧的 opcode（FIN bit 在 bit7：0x80 | opcode）
// + ws_frag_q:     分片帧的聚合队列（收到非 FIN 帧时追加，head==NULL 表示无分片进行中）
// + ws_frag_len:   ws_frag_q 中已聚合的总字节数
// + ws_utf8state:  TEXT 帧 UTF-8 DFA 当前状态（0=ACCEPT；跨分片保持；RFC 6455 §8.1）
#define CUSTOM_WS_CLIENT \
    struct cw_client_ctx*           ws_ctx;         \
    uint8_t                         ws_hdr_buf[14]; \
    uint8_t                         ws_opcode;      \
    buffer_queue_t                  ws_frag_q;      \
    uint32_t                        ws_frag_len;    \
    uint32_t                        ws_utf8state;

// 基础 WS client 类型（可通过 CUSTOM_WS_CLIENT 内联到派生结构体）
typedef struct cw_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_TCP_CLIENT
    CUSTOM_WS_CLIENT
    uint8_t                         close_frame_buf[sizeof(buf16_item_t) + 4];
} cw_client_t;

#define CW_CLIENT(c)     ((cw_client_t*)(c))

// I/O 关闭状态标志（复用 TCP_IO_FLAG_CUSTOM_BIT）
#define CW_IO_FLAG_CLOSING  (1 << TCP_IO_FLAG_CUSTOM_BIT)  // 本端已发 close 帧，等待对端 close 或超时

//-----------------------------------------------------------------------------
// 回调类型

// 收到完整 WS 数据帧（text 或 binary）时调用
// + opcode: WS_OP_TEXT 或 WS_OP_BINARY
// + payload: 指向 payload 数据的指针。对于分片聚合帧，该值为 NULL，且 payload_len > 0
// + payload_len: payload 数据总字节数（分片场景下为全部分片之和）
// + buf_item: payload 所属的 buf_item（可用于零拷贝转发），即对应 custom_tcp 框架中的 payload1。
//             如果 payload 的所属 buf_item 是 custom_tcp 框架中的 payload0，则此时的 buf_item 为 NULL
//             此时无法零拷贝转发，如过上层需要转发，则需要自行复制到新 buf_item 中
typedef void (*cw_handle_frame_cb)(cw_client_t *client, uint8_t opcode,
                                   uint8_t *payload, uint32_t payload_len,
                                   buf16_item_t *buf_item);

// 收到 ping 帧时调用（nullable）；框架已自动发送 pong 回复，回调仅供通知
typedef void (*cw_handle_ping_cb)(cw_client_t *client, const uint8_t *data, uint8_t len);

// 对端发起 close 握手时调用（nullable）；框架已自动入队 close 回复，回调仅供通知
typedef void (*cw_handle_close_cb)(cw_client_t *client, uint16_t code);

// 握手（HTTP Upgrade）完成时调用（nullable）
// + 可在此进行鉴权、子协议协商结果处理等
// + 返回 false 表示拒绝连接，框架会发送 403 并关闭连接
typedef bool (*cw_handshake_done_cb)(cw_client_t *client);

//-----------------------------------------------------------------------------
// 协议上下文（每个 WS 子协议类型共享一个）

typedef struct cw_client_ctx {
    ct_client_ctx_t                 base;

    // WS 子协议名（HTTP 握手 Sec-WebSocket-Protocol 字段，NULL 表示不协商）
    const char*                     sub_protocol;

    // 握手完成回调（nullable）；返回 false 拒绝连接
    cw_handshake_done_cb            handshake_done;

    // 收到数据帧（text/binary）时调用
    cw_handle_frame_cb              handle_frame;

    // 收到 ping 帧时调用（nullable）
    cw_handle_ping_cb               handle_ping;

    // 对端发起 close 时调用（nullable）
    cw_handle_close_cb              handle_close;

} cw_client_ctx_t;

//-----------------------------------------------------------------------------
// Public API

// 初始化 WS 协议上下文（绑定内部回调到 ctx->base）
// + 必须在设置应用层回调之前调用（或之后调用，不影响，因 base 字段独立）
void
cw_ctx_init(cw_client_ctx_t *ctx);

// 初始化 WS client
// + 必须在 accept 后、第一次 cw_handle_recv 之前调用
bool
cw_init_client(cw_client_t *client, cw_client_ctx_t *ctx);

// 强制释放 WS client（关闭所有 session，释放所有缓冲）
void
cw_free_client(cw_client_ctx_t *ctx, cw_client_t *client);

// 发送 WS 帧
// + opcode: WS_OP_TEXT / WS_OP_BINARY / WS_OP_CONTINUATION
// + buf_item: 上层分配的 buf_item，框架接管所有权，发送完成后自动释放
// + payload_offset: buf_item 中 payload 数据的起始偏移（允许 buf_item 前置有帧头预留空间）
//   框架会在 [0, payload_offset) 区间写入 WS 帧头，因此 payload_offset >= 有效的 hdr size
//   这里的有效 hdr size 为: 2 + 2/4/6
ret_t
cw_send_frame(cw_client_t *client, uint8_t opcode,
              buf16_item_t *buf_item, uint16_t payload_offset,
              bool immediate);

// 发送 WS close 帧（code=1000 表示正常关闭）
ret_t
cw_send_close(cw_client_t *client, uint16_t code);

// grace close 超时检查（在 server 的定期 cleanup 中调用）
void
cw_retry_closing(cw_client_ctx_t *ctx, cw_client_t *client, uint64_t now);

//-----------------------------------------------------------------------------

#endif // P2P_CUSTOM_WS_H

