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

#define WS_OP_CONTINUATION  0x0     // 续帧（分片消息的后续帧）
#define WS_OP_TEXT          0x1     // 文本帧（UTF-8 编码）
#define WS_OP_BINARY        0x2     // 二进制帧
#define WS_OP_CLOSE         0x8     // 连接关闭帧（控制帧，payload 可含 2 字节状态码）
#define WS_OP_PING          0x9     // Ping 帧（控制帧，payload <= 125 字节）
#define WS_OP_PONG          0xA     // Pong 帧（控制帧，回复 ping 或主动心跳）

//-----------------------------------------------------------------------------
// WS client 扩展字段（内联宏，供派生结构体使用）
//
// + ws_ctx:       指向所属的 custom_ws_ctx_t（init 时保存，回调中用于访问 ws 级配置）
// + ws_hdr_buf:    14 字节静态缓冲，帧模式下作为 hdr_rs 使用（存放 WS 帧头字节）
// + ws_opcode:     当前帧的 opcode（FIN bit 在 bit7：0x80 | opcode）
// + ws_frag_q:     分片帧的聚合队列（收到非 FIN 帧时追加，head==NULL 表示无分片进行中）
// + ws_frag_len:   ws_frag_q 中已聚合的总字节数
// + ws_utf8state:  TEXT 帧 UTF-8 DFA 当前状态（0=ACCEPT；跨分片保持；RFC 6455 §8.1）
#define CUSTOM_WS_CLIENT \
    struct custom_ws_ctx*           ws_ctx;         \
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
} cw_client_t;

#define CW_CLIENT(c)     ((ct_client_t*)(c))

// I/O 关闭状态标志（复用 TCP_IO_FLAG_CUSTOM_BIT）
#define CW_IO_FLAG_CLOSING  (1 << TCP_IO_FLAG_CUSTOM_BIT)  // 本端已发 close 帧，等待对端 close 或超时

//-----------------------------------------------------------------------------
// 回调类型

// 收到完整 WS 数据帧（text 或 binary）时调用
// + opcode: WS_OP_TEXT 或 WS_OP_BINARY
// + payload0: recv_buf/frag_buf 内的零拷贝切片（⚠️ 禁止加入 buf 队列）
// + payload1: 剩余 payload 的单独分配缓冲（NULL 表示无）
// + payload_offset 由 resolve_payload_len 指定，payload1 前置空间可写入新帧头实现零拷贝转发
typedef void (*cw_handle_frame_cb)(cw_client_t *client, uint8_t opcode,
                                   buf16_item_t *payload0, buf16_item_t *payload1);

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

typedef struct custom_ws_ctx {
    custom_tcp_ctx_t                base;

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
} custom_ws_ctx_t;

//-----------------------------------------------------------------------------
// Public API

// 初始化 WS 协议上下文（绑定内部回调到 ctx->base）
// + 必须在设置应用层回调之前调用（或之后调用，不影响，因 base 字段独立）
void
cw_ctx_init(custom_ws_ctx_t *ctx);

// 初始化 WS client
// + 必须在 accept 后、第一次 cw_handle_recv 之前调用
bool
cw_init_client(cw_client_t *client, custom_ws_ctx_t *ctx);

// 强制释放 WS client（关闭所有 session，释放所有缓冲）
void
cw_free_client(custom_ws_ctx_t *ctx, cw_client_t *client);

// 发送 WS 帧
// + opcode: WS_OP_TEXT / WS_OP_BINARY
// + buf_item: 上层分配的 buf_item，框架接管所有权，发送完成后自动释放
// + payload_pos: buf_item 中 payload 数据的起始偏移（允许 buf_item 前置有帧头预留空间）
//   框架会在 [0, payload_pos) 区间写入 WS 帧头，因此 payload_pos >= 10（最大帧头长度）
ret_t
cw_send_frame(cw_client_t *client, uint8_t opcode, buf16_item_t *buf_item, uint16_t payload_pos);

// 发送 WS close 帧（code=1000 表示正常关闭）
ret_t
cw_send_close(cw_client_t *client, uint16_t code);

// grace close 超时检查（在 server 的定期 cleanup 中调用）
void
cw_retry_closing(custom_ws_ctx_t *ctx, cw_client_t *client, uint64_t now);

//-----------------------------------------------------------------------------

#endif // P2P_CUSTOM_WS_H

