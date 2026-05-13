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
//   当 payload 跨越 recv_buf/socket 边界时，ct_handle_recv 会将已收到的部分（payload0）
//   直接指向 recv_buf 内的数据传给应用（零拷贝），同时分配 payload1 用于接收剩余部分。
//   payload1->pos 初始指向 socket 读入数据的起始偏移，[payload1->pos - already, payload1->pos)
//   这段前置空间可供应用按需将 payload0 拷入，以获得完整的连续缓冲区。
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
// Client 扩展字段（内联宏，供派生 client 结构体使用）

// + last_error:       最近一次错误码（0 表示无错误）
// + hdr_rs:           流模式：指向边界标识字节串；帧模式：指向固定 header 缓冲（外部分配，生命期由上层管理）
// + hdr_sz:           流模式：边界标识符长度；帧模式：当前期望读取的 header 字节数（可动态调整）
// + recv_buf:         流模式接收缓冲（非 NULL = 流模式，NULL = 帧模式）
//                     流模式：pos 指向当前请求 header 的起始，recv_cur 是边界扫描游标
//                     切换到帧模式时，若 recv_buf 尚有未消费数据，会保留为 payload_buf（缓释）
// + recv_cur:         流模式：边界扫描游标；帧模式：hdr_rs 已读取字节数
// + payload_buf:      当前正在接收的 payload 缓冲；NULL 表示未进入 payload 接收阶段
//                     + 帧模式切换时，原 recv_buf 中未消费的数据会暂存在此（refer=(void*)-1 标记）
//                     + payload_buf->pos 指向 socket 读入数据的起始偏移；pos 之前为预留前置空间
// + payload_cur:      payload_buf 中已读入的字节数（从 payload_buf->pos 起算）
// + send_buff_queue:  client 级发送队列（优先于 session 队列发送）
// + send_sess_head/rear: 有待发数据的 session 组成的双向链表
// + sending_sess:     当前正在发送数据的 session（mid-packet 时保持）
// + sending_cur:      当前正在发送的 buf_item 已发送字节数
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

// 解析 header，填充 payload_len（payload 总字节数）和 payload_offset（payload 前置保留空间字节数）
// + payload_offset：框架在分配 payload1 时会在数据前预留该大小的空间，供应用写入新 header 后
//   直接将 payload1 投入转发队列，实现零拷贝转发（不需要 payload_offset 时填 0）
// + 返回 false 表示需要更多 header 数据（帧模式专用，用于动态扩展 header size）
//   此时需将 client->hdr_sz 更新为新的期望读取长度，框架会继续读取后再次调用
// + 流模式下 header 已完整，必须返回 true
typedef bool (*ct_resolve_payload_len_cb)(ct_client_t* client, uint8_t* hdr_buf, uint16_t hdr_len,
                                          uint32_t* payload_len, uint16_t* payload_offset);

// 握手阶段收到完整消息时调用
// + *client: 当前握手 client 指针的地址。回调可修改 *client 来切换 client 实例（见【client swap 机制】）
// + hdr_buf/hdr_len: header 数据（流模式指向 recv_buf 内部，帧模式指向 hdr_rs）
// + payload0: recv_buf 内部的 payload 片段（零拷贝，NULL 表示无）
//   ⚠️ payload0 禁止加入任何 buf 队列（回调返回后 recv_buf 会被复用）
// + payload1: 单独分配的 payload 缓冲（大 payload 跨 socket 读取时使用，NULL 表示无）
//   - payload1->pos 指向 socket 读入数据的起始；[pos - len(payload0), pos) 为前置预留空间
// + 返回非 NULL 的 ack buf_item 表示握手成功；返回 NULL 且 last_error==0 表示协议错误
// + 注意：回调期间不应调整 recv_buf（框架有 assert 保证）
//
// 【client swap 机制】重连握手时，回调可通过修改 *client 将 from→reg 的切换通知框架：
//   1. 回调内调用 resident_client(reg, proto, inst, from)：迁移 fd 至 reg，free_client(from) 释放当前 client
//   2. 将 *client 更新为 reg（旧槽位指针）
//   框架检测到 *client 变化后，跳过对已释放的 from client 的状态访问（payload/recv_buf 均已无效），
//   改由 ct_client_send 向 reg 直接投递 ACK 并调用 handshake_finish，完成握手流程。
typedef buf16_item_t* (*ct_handle_handshake_cb)(ct_client_t **client, uint8_t* hdr_buf, uint16_t hdr_len,
                                                buf16_item_t* payload0, buf16_item_t* payload1);

// 握手 ACK 发送完成后调用（nullable）
// + 可在此调整 recv_buf（如切换模式、调整缓冲大小）
typedef void (*ct_handshake_finish_cb)(ct_client_t *client);

// 正常阶段收到完整消息时调用
// + payload0/payload1 语义同 handle_handshake（payload0 同样禁止加入 buf 队列）
// + 回调内可通过修改 client->recv_buf 在流/帧模式间切换
//   框架会在回调返回后通过 prepare_next_recv 处理模式切换的状态一致性
typedef void (*ct_handle_proto_cb)(ct_client_t *client, uint8_t* hdr_buf, uint16_t hdr_len,
                                   buf16_item_t* payload0, buf16_item_t* payload1);

// session 级 buf_item 发送完成时调用（buf_item->refer 指向对端 session）
// + 用于实现对端感知的发送确认（如中继转发的流量控制）
typedef void (*ct_handle_peer_sent_cb)(ct_session_t *session, buf16_item_t *buf_item);

// session 配对关系被打破时调用
// + SESS_BREAK_STOP: session 保留，仅断开配对关系（如对端暂时不可达）
// + SESS_BREAK_CLOSE: session 将被销毁，可向 client 返回状态（软退出）
// + SESS_BREAK_TERM: session 将被销毁，不再向 client 返回任何数据（硬退出）
typedef void (*ct_session_break_cb)(ct_session_t *session, ct_session_t *peer, break_mode_e break_mode);

// client 变为不可达时调用（nullable）
// + readOrWrite: true = 读错误，false = 写错误
typedef void (*ct_client_unreachable_cb)(ct_client_t *client, bool readOrWrite);

// 构造错误应答 buf_item（填充错误响应内容到 buf_item 后返回，框架负责发送和释放）
// + handshake: true 表示发生在握手阶段（此时 last_error 已设置）
// + 返回 NULL 表示 OOM，框架将直接强制释放 client
typedef buf16_item_t* (*ct_error_item_cb)(ct_client_t *client, bool handshake);

//-----------------------------------------------------------------------------
// 协议上下文（每个协议类型共享一个，所有 client 共用）

// 协议上下文扩展字段（内联宏，供派生协议上下文结构体使用）
// + 参考 CUSTOM_TCP_CLIENT/ct_client_t 的宏继承模式
#define CUSTOM_TCP_CTX \
    uint32_t                        max_payload_len;            \
    ct_resolve_payload_len_cb       resolve_payload_len;        \
    ct_handle_handshake_cb          handle_handshake;           \
    ct_handshake_finish_cb          handshake_finish;           \
    ct_handle_proto_cb              handle_proto;               \
    ct_handle_peer_sent_cb          handle_peer_sent;           \
    ct_session_break_cb             session_break;              \
    ct_client_unreachable_cb        client_unreachable;         \
    buf16_item_t*                   fatal_item;                 \
    ct_error_item_cb                error_item;

typedef struct ct_client_ctx {
    client_ctx_t                    base;                       // 继承 client_ctx_t（必须为第一个成员）
    CUSTOM_TCP_CTX
} ct_client_ctx_t;

//-----------------------------------------------------------------------------
// Public API

// 初始化 client：分配 MTU 大小的 recv_buf，清零所有发送队列状态，设置 handshake=HANDSHAKING、io=WANT_READ
// + 调用方需事先将 hdr_rs/hdr_sz 设置好（指向 header 缓冲及其大小）
bool
ct_init_client(ct_client_t* client);

// 接收状态迁移（resident_client 的 client_ctx_t.migrate 默认实现）
// + 将 from 的 recv_buf/recv_cur/hdr_rs 内容/payload_buf/payload_cur 转移到 to
// + 迁移后 from 对应字段清零，确保 free 时不重复释放
// + relay、wss 等使用 custom_tcp 框架的模块，将此函数注册为其 client_ctx_t.migrate
void
ct_migrate_client(client_t *to, client_t *from);

// 强制释放 client：关闭所有 session（TERM），释放 recv/payload buf，清空发送队列，调用 free_client_base
void
ct_free_client(ct_client_ctx_t* ctx, ct_client_t *client);

// 重连激活：重置 last_error，恢复 WANT_READ，如有未完成发送则恢复 WANT_WRITE
// + 适用于 TCP 断线重连后，原 client 槽位复用的场景
void
ct_reactive_client(ct_client_ctx_t* ctx, ct_client_t *client);

// 报告错误：
// + fatal=false（非致命）：清除 WANT_READ，向 client 发送一个错误应答包，发送完成后关闭连接
// + fatal=true（致命）：终止所有 session，清空发送队列，追加 fatal_item 后关闭连接
void
ct_client_error(ct_client_ctx_t* ctx, ct_client_t *client, int16_t error, bool fatal);

// 优雅关闭 client：中断所有 session（CLOSE），释放 recv buf
// + 如发送队列非空：标记为 CLOSING，停止读取，等发送完成后自动调用 free_client_base
// + 如发送队列为空：直接调用 free_client_base
void
ct_client_off(ct_client_ctx_t* ctx, ct_client_t *client);

// 关闭单个 session
// + terminate=false：session 的待发数据转移到 client 队列继续发送
// + terminate=true：session 的待发数据直接丢弃
void
ct_close_session(ct_client_ctx_t* ctx, ct_session_t *session, bool terminate);

// 向 client 发送队列追加 buf_item（仅在 handshake==0 时调用）
// + immediate=true：高优先级，插入到当前正在发送的包之后（或队头），确保优先发出
// + immediate=false：追加到队尾
// + 如 client 可达（WANT_READ），自动设置 WANT_WRITE
void
ct_client_send(ct_client_t *client, buf16_item_t* buf_item, bool immediate);

// 向 session 发送队列追加 buf_item（session 存在即意味 client handshake==0）
// + 队列原本为空时，将 session 挂入 client 的 send_sess 链表，并设置 WANT_WRITE
void
ct_session_send(ct_session_t *session, buf16_item_t* buf_item);

//-----------------------------------------------------------------------------
// server 层调度接口（实现 socket 数据 I/O）

// 接收处理（WANT_READ & select 就绪时调用）
// + 内部循环处理握手和正常消息的读取、解析、分发，直到 would block 或出错
// + SP：子协议名称标签（用于日志，NULL 时默认显示 "TCP"）
void
ct_handle_recv(ct_client_ctx_t* ctx, ct_client_t *client, const char* SP);

// 发送处理（WANT_WRITE & select 就绪时调用）
// + 按优先级发送：client 队列 > session 队列（session 间轮询）
// + 发送完成后自动清除 WANT_WRITE；若处于 CLOSING 状态则调用 free_client_base
// + SP：子协议名称标签（用于日志，NULL 时默认显示 "TCP"）
void
ct_handle_send(ct_client_ctx_t* ctx, ct_client_t *client, const char* SP);

//-----------------------------------------------------------------------------

#endif //P2P_CUSTOM_TCP_H

