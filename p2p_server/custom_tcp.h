//
// custom_tcp.h — 通用 TCP 长连接服务端基础设施
//
// 封装 TCP 长连接的完整生命周期：握手、正常收发、session 管理、优雅/强制关闭。
// 上层协议只需实现若干回调即可复用统一的收发调度、流量控制、错误处理逻辑。
//
// 支持两种 header 解析模式（由 recv_buf 是否为 NULL 决定，可在运行时切换）：
//   流模式 (recv_buf != NULL)：以边界标识符（hdr_rs）扫描数据流，定位 header 起始/结束
//   帧模式 (recv_buf == NULL)：以固定长度（hdr_sz）读取 header，支持动态扩展
//
// payload 零拷贝传递：
//   当 payload 跨越 recv_buf/socket 边界时，ct_handle_recv 会将已收到的部分，即 recv_buf 内的数据
//   直接（作为 payload0）传给应用（零拷贝），同时分配 payload1 用于接收剩余部分。
//   payload1->pos 指向 payload 数据的起点，注意
//   如果存在 payload0，则从 pos 开始的前置 len(payload0) 空间是为 payload0 预留的，
//   payload1 实际加载的数据从 pos + len(payload0) 开始
//   ⚠️ payload0 是 recv_buf 内部的借用切片，回调返回后 recv_buf 会被复用，禁止将 payload0 加入任何 buf 队列。
//
// payload_offset（前置保留空间）的用途：
//   应用可在 resolve_payload_len 中指定 payload_offset，框架会在分配 payload1 时在数据前预留该大小的空间。
//   典型用途：零拷贝同步转发 —— 应用直接在前置空间写入新 header，随后将整个 payload1（含新 header）
//   投入发送队列，无需额外拷贝 payload 数据体，即可实现接收到转发的端到端零拷贝。

#ifndef P2P_CUSTOM_TCP_H
#define P2P_CUSTOM_TCP_H

#include "common.h"

//-----------------------------------------------------------------------------
// Session 扩展字段（内联宏，供派生 session 结构体使用）

// + send_queue:  该 session 待发送的 buf_item 队列（内联结构体，calloc 后自动为空队列）
// + send_prev/next: 在 client 的 send_sess_head/rear 双向链表中的位置（队列非空时才挂入）
#define CUSTOM_TCP_SESSION   \
    buffer_queue_t                  send_queue; \
    struct ct_session*              send_prev; \
    struct ct_session*              send_next;

// 基础 session 类型（可通过 CUSTOM_TCP_SESSION 内联到派生结构体）
typedef struct ct_session {
    session_t                       base;
    CUSTOM_TCP_SESSION
} ct_session_t;

#define CT_CLIENT(s)                ((ct_client_t*)CLIENT(s))
#define CT_PEER(s)                  ((ct_session_t*)PEER(s))

//-----------------------------------------------------------------------------
/**
 * @brief                           Client 扩展字段（内联宏，供派生 client 结构体使用）
 * @details
 *  last_error                      最近一次错误码，0 表示无错误
 *  hdr_rs                          流模式下指向边界标识字节串；帧模式下指向固定 header 缓冲，由上层管理其生命周期
 *  hdr_sz                          流模式下为边界标识符长度；帧模式下为当前期望读取的 header 字节数，可动态调整
 *  recv_buf                        流模式接收缓冲。非 NULL 表示流模式，NULL 表示帧模式
 *                                  流模式下，pos 指向当前请求 header 起始，recv_cur 为边界扫描游标
 *                                  切换到帧模式时，若 recv_buf 仍有未消费数据，会暂存到 payload_buf
 *                                  注意：修改 recv_buf 时无需手动释放旧对象，框架会在回调后统一处理模式切换和资源释放
 *  recv_cur                        流模式下为边界扫描游标；帧模式下为 hdr_rs 已读取字节数
 *  payload_buf                     当前正在接收的 payload 缓冲，NULL 表示未进入 payload 接收阶段
 *                                  帧模式切换时，原 recv_buf 的未消费数据可能暂存在这里，并带 refer=(void*)-1 标记
 *                                  payload_buf->pos 指向 socket 读入数据的起始偏移，pos 之前为预留前置空间
 *  payload_cur                     payload_buf 中已读入的字节数，从 payload_buf->pos 起算
 *  send_buff_queue                 client 级发送队列，优先于 session 级发送队列
 *  send_sess_head/rear             当前有待发数据的 session 双向链表头尾
 *  sending_sess                    当前正在发送数据的 session，mid-packet 时保持不变
 *  sending_cur                     当前正在发送的 buf_item 已发送字节数
 */
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

// 基础 client 类型（可通过 CUSTOM_TCP_CLIENT 内联到派生结构体）
typedef struct ct_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_TCP_CLIENT
} ct_client_t;

typedef struct ct_client_ctx ct_client_ctx_t;

//-----------------------------------------------------------------------------
// 错误码

#define CUSTOM_TCP_ERR_DISCONNECTED 1   // 对端正常断开连接（EOF）
#define CUSTOM_TCP_ERR_IO           2   // 网络 I/O 错误（连接异常/读写失败）
#define CUSTOM_TCP_ERR_OVERFLOW     3   // 请求数据超出允许的最大 payload 长度
#define CUSTOM_TCP_ERR_INTERNAL     4   // 服务器内部错误（OOM 等），客户端应断开重连
#define CUSTOM_TCP_ERR_PROTOCOL     5   // 协议格式错误（无法解析 header 或 handshake 无应答）
#define CUSTOM_TCP_ERR_CUSTOM       6   // 上层自定义错误起始值

//-----------------------------------------------------------------------------
// buf_item->refer 特殊标记

// 标记 buf_item 正等待 ACK 确认，发送完成后不立即释放（由上层在 handle_peer_sent 中处理）
#define ITEM_REF_ACK_PENDING        ((void*)(uintptr_t)1)
// 标记 buf_item 是静态数据（非堆分配），发送完成后仅清除 refer 标记，不 free
#define ITEM_REF_STATIC             ((void*)(uintptr_t)2)

//-----------------------------------------------------------------------------
// 回调类型

/**
 * @brief                           解析 header，并给出 payload 长度与前置保留空间大小
 * @param client
 * @param hdr_buf                   header 数据
 * @param hdr_len                   header 长度
 * @param payload_len               输出参数，payload 总字节数
 * @param payload_offset            输出参数，payload 前置保留空间字节数
 *                                  该值用于让上层在 payload 前直接写入新的 header，从而实现零拷贝转发
 * @return
 *  <0                             解析报错
 *  >0                             需要更多 header 数据（仅帧模式）
 *                                 此时上层需同步更新 client->hdr_sz，框架会继续读取并再次调用本回调
 *                                 流模式下 header 首次解析时就必须完整，因此必须返回 0
 *  =0                             解析成功
 */
typedef ret_t (*ct_resolve_payload_len_cb)(ct_client_t* client, uint8_t* hdr_buf, uint16_t hdr_len,
                                           uint32_t* payload_len, uint16_t* payload_offset);

/**
 * @brief                           握手阶段收到完整消息时触发
 * @param ctx
 * @param client
 * @param hdr_buf                   header 数据。流模式下指向 recv_buf 内部，帧模式下指向 hdr_rs
 * @param hdr_len                   header 长度
 * @param payload0                  recv_buf 内部的 payload 片段，零拷贝借用，可能为 NULL
 *                                  注意：payload0 不能加入任何 buf 队列，回调返回后 recv_buf 会被复用
 * @param payload1                  单独分配的 payload 缓冲，可能为 NULL
 *                                  若存在 payload0，则 payload1->pos 开始的前置 len(payload0) 空间是为 payload0 预留的
 *                                  若 resolve_payload_len 给出了 payload_offset，则 payload1->pos == payload_offset，否则为 0
 * @return                          返回非 NULL 的 ack buf_item 表示握手成功
 *                                  返回 NULL 且 last_error==0 表示协议错误
 *                                  约束：ack 必须是 buf16_item_t，不能是 32-bit buffer
 */
typedef buf16_item_t* (*ct_handle_handshake_cb)(ct_client_ctx_t *ctx, ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                                                buf16_item_t* payload0, buf16_item_t* payload1);

/**
 * @brief                           握手 ACK 发送完成后触发
 * @param ctx
 * @param client
 * @return                          无
 */
typedef void (*ct_handshake_finish_cb)(ct_client_ctx_t *ctx, ct_client_t *client);

/**
 * @brief                           正常阶段收到完整消息时触发
 * @param ctx
 * @param client
 * @param hdr_buf                   header 数据
 * @param hdr_len                   header 长度
 * @param payload0                  recv_buf 内部借用的 payload 片段，语义同握手阶段
 * @param payload1                  独立 payload 缓冲，语义同握手阶段
 * @return                          无
 */
typedef void (*ct_handle_proto_cb)(ct_client_ctx_t *ctx, ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                                   buf16_item_t* payload0, buf16_item_t* payload1);

/**
 * @brief                           session 级 buf_item 发送完成时触发
 * @param ctx
 * @param session
 * @param buf_item
 * @return                          无
 */
typedef void (*ct_handle_peer_sent_cb)(ct_client_ctx_t *ctx, ct_session_t *session, buf16_item_t *buf_item);

/**
 * @brief                           client 变为不可达时触发
 * @param ctx
 * @param client
 * @param readOrWrite               true 表示读错误，false 表示写错误
 * @return                          无
 */
typedef void (*ct_client_unreachable_cb)(ct_client_ctx_t *ctx, ct_client_t *client, bool readOrWrite);

/**
 * @brief                           构造错误应答 buf_item，由框架负责发送和释放
 * @param client
 * @return                          返回 NULL 表示 OOM，此时框架按 fatal 处理
 *                                  握手阶段返回值必须是 buf16_item_t，不能是 32-bit buffer
 */
typedef buf16_item_t* (*ct_error_item_cb)(ct_client_t *client);

//-----------------------------------------------------------------------------
// 协议上下文（每个协议类型共享一个，所有 client 共用）

/**
 * @brief                           协议上下文扩展字段（内联宏，供派生协议上下文结构体使用）
 * @details                         参考 CUSTOM_TCP_CLIENT/ct_client_t 的宏继承模式
 *                                  其中 handshake_finish、handle_peer_sent、client_unreachable、fatal_item、error_item 可为空
 */
#define CUSTOM_TCP_CTX \
    uint32_t                        max_payload_len;            \
    ct_resolve_payload_len_cb       resolve_payload_len;        \
    ct_handle_handshake_cb          handle_handshake;           \
    ct_handshake_finish_cb          handshake_finish;           /* nullable: 握手完成回调（可选） */ \
    ct_handle_proto_cb              handle_proto;               \
    ct_handle_peer_sent_cb          handle_peer_sent;           /* nullable: 对端发送确认回调（可选） */ \
    ct_client_unreachable_cb        client_unreachable;         /* nullable: client 不可达回调（可选） */ \
    buf16_item_t*                   fatal_item;                 /* nullable: 致命错误应答包，NULL 时直接释放 client */ \
    ct_error_item_cb                error_item;                 /* nullable: 错误应答构造回调，NULL 时按 fatal 处理 */

struct ct_client_ctx {
    client_ctx_t                    base;                       // 继承 client_ctx_t（必须为第一个成员）
    CUSTOM_TCP_CTX
};

/**
 * @brief                           将（请求包）payload 数据直接转换为（转发）应答包。以实现尽量少的资源开销，例如零拷贝转发
 * @param client
 * @param payload                   请求包 payload 数据（指向 recv_buf 内部，零拷贝）
 * @param payload_len               请求包 payload 长度
 * @param payload_offset            转为应答包后的 payload 在 buf_item 中的偏移（如果该值等于 payload - payload_item 则零拷贝）
 * @param payload_item              payload 所属 buf_item。如果该值为 NULL，则会分配新的 buf_item（无法零拷贝）
 * @return
 */
static inline buf16_item_t*
ct_forward_payload(ct_client_t *client,
                   const uint8_t* payload, uint32_t payload_len, uint16_t payload_offset,
                   buf16_item_t *payload_item) {
    assert(payload_item == client->payload_buf);
    if (payload_item) {
        uint8_t *dst = ITEM2BUF(payload_item) + payload_offset;
        if (dst != payload)
            memmove(dst, payload, payload_len);
        if (BUF_IS_32BIT(payload_item->flags)) {
            BUF32(payload_item)->pos = payload_offset;
            BUF32(payload_item)->len = payload_offset + payload_len;
        } else {
            payload_item->pos = payload_offset;
            payload_item->len = payload_offset + payload_len;
        }
        client->payload_buf = NULL;
    } else { payload_item = alloc_buffer(0, payload_offset + payload_len);
        if (!payload_item) return NULL;
        if (BUF_IS_32BIT(payload_item->flags)) {
            BUF32(payload_item)->pos = payload_offset;
            BUF32(payload_item)->len = payload_offset + payload_len;
        } else {
            payload_item->pos = payload_offset;
            payload_item->len = payload_offset + payload_len;
        }
        memcpy(ITEM2BUF(payload_item) + payload_offset, payload, payload_len);
    }
    return payload_item;
}

//-----------------------------------------------------------------------------
// Public API

void
ct_ctx_init(ct_client_ctx_t *ctx);

// custom tcp 可被重载的接口实现
void ct_client_free(client_ctx_t* ctx, client_t *c);
bool ct_client_reset(client_ctx_t* ctx, client_t *c, bool init);
void ct_client_migrate(client_ctx_t* ctx, client_t *to, client_t *from);
bool ct_client_activate(client_ctx_t* ctx, client_t *c, int active);
void ct_session_clear(client_ctx_t* ctx, client_t *c, bool preOrPost);
void ct_session_close(client_ctx_t* ctx, session_t *s, bool terminate, bool clearing);

//-----------------------------------------------------------------------------

/**
 * @brief                           向 client 发送队列追加 buf_item
 * @param client
 * @param buf_item
 * @param immediate                 true 表示高优先级插队，false 表示追加到队尾
 * @return                          无
 */
void
ct_client_send(ct_client_t *client, buf16_item_t* buf_item, bool immediate);

/**
 * @brief                           向 session 发送队列追加 buf_item
 * @param session
 * @param buf_item
 * @return                          无
 */
void
ct_session_send(ct_session_t *session, buf16_item_t* buf_item);

//-----------------------------------------------------------------------------
// 调度派发接口

/**
 * @brief                           优雅关闭 client
 * @param ctx
 * @param client
 * @param last_item                 可选的最后一个发送项
 * @return                          无
 */
void
ct_client_off(ct_client_ctx_t* ctx, ct_client_t *client, buf16_item_t* last_item);

/**
 * @brief                           报告 client 错误，并按 fatal 策略执行关闭或销毁
 * @param ctx
 * @param client
 * @param error
 * @param fatal                     true 表示致命错误，false 表示最后返回一个错误应答后关闭
 * @return                          无
 */
void
ct_client_error(ct_client_ctx_t* ctx, ct_client_t *client, int16_t error, bool fatal);

/**
 * @brief                           处理 client 的接收路径
 * @param ctx
 * @param client
 * @param SP                        子协议名称标签，NULL 时默认显示为 "TCP"
 * @return                          无
 */
void
ct_handle_recv(ct_client_ctx_t* ctx, ct_client_t *client, const char* SP);

/**
 * @brief                           处理 client 的发送路径
 * @param ctx
 * @param client
 * @param SP                        子协议名称标签，NULL 时默认显示为 "TCP"
 * @return                          无
 */
void
ct_handle_send(ct_client_ctx_t* ctx, ct_client_t *client, const char* SP);

//-----------------------------------------------------------------------------

#endif //P2P_CUSTOM_TCP_H

