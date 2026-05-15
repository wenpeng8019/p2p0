//
// Created by 温朋 on 2026/4/18.
//
#define MOD_TAG "CUSTOM_TCP"

#include "custom_tcp.h"

#define ITEM_REF_CLIENT_ERROR       ((void*)(uintptr_t)-1)

#define STREAM_HEADER_RECV_MAX      128

///////////////////////////////////////////////////////////////////////////////

// 发送数据 buf 到 client 发送队列
void ct_client_send(ct_client_t *client, buf16_item_t* buf_item, bool immediate) {

    // 前提是不能处于握手/closing 阶段
    assert(client->handshake == 0);

    // 高优先级：插到当前正在发送包之后（若有），否则插到队头
    if (immediate) {

        // 如果存在正在发送的包，则将新包插入到正在发送的包之后
        if (client->sending_cur && !client->sending_sess) { assert(client->send_buff_queue.head);
            BUF_Q_AFTER(&client->send_buff_queue, client->send_buff_queue.head, buf_item)
            return;
        }

        // 添加到队列的最前面
        BUF_Q_PUSH(&client->send_buff_queue, buf_item)
        if (buf_item->next) return;                             // 如果队列之前不空，直接返回

    } else {
        BUF_Q_APPEND(&client->send_buff_queue, buf_item)
        if (client->send_buff_queue.head != buf_item) return;  // 如果队列之前不空，直接返回
    }

    if (TCP_REACHABLE(client)) {
        assert(client->base.fd != P_INVALID_SOCKET && client->last_error == 0);
        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }
}

// 发送数据 buf 到 session 发送队列
void ct_session_send(ct_session_t *session, buf16_item_t* buf_item) {

    // session 存在意味 client 肯定未处于握手/closing 阶段
    ct_client_t *client = CT_CLIENT(session);

    // 添加到 session 的本地发送队列
    BUF_Q_APPEND(&session->send_queue, buf_item)
    if (session->send_queue.head != buf_item) return;         // 如果队列之前不空，直接返回

    // 如果 session 发送队列之前为空
    assert(!session->send_next && !session->send_prev);

    // 将 session 加入 c 发送队列列表
    session->send_prev = client->send_sess_rear;
    session->send_next = NULL;
    if (client->send_sess_rear) {
        client->send_sess_rear->send_next = session;
        client->send_sess_rear = session;
    } else client->send_sess_head = client->send_sess_rear = session;

    if (TCP_REACHABLE(client)) {
        assert(client->base.fd != P_INVALID_SOCKET && client->last_error == 0);
        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }
}

// 清除 session 的发送队列
// + terminate:
//   false: 当前发送队列中的数据并不销毁，而是转移到 client 发送队列
//   true: 直接将当前发送队列中的数据销毁，无需继续发送给 client 端。
static void clear_session_sending(ct_session_t *session, bool terminate, bool all) {

    buf16_item_t *item = session->send_queue.head;
    if (!item) {
        assert(!session->send_queue.rear && !session->send_next && !session->send_prev);
        return;
    }

    ct_client_t *client = CT_CLIENT(session);

    // 如果正在发送当前 session 的数据，需要将它转为 client 级发送队列的第一项
    if (client->sending_sess == session) {
        client->sending_sess = NULL;
        BUF_Q_POP(&session->send_queue, item)
        BUF_Q_PUSH(&client->send_buff_queue, item)
    }

    if (session->send_queue.head) {

        // 如果 session 的数据需要被销毁，即无需继续发送给 client 端
        if (terminate) {
            BUF_Q_CLEAR(&session->send_queue, it, free_buffer(it);)
        }
        // 如果需要将 session 现有的数据发送给 client 端，则将 session 的发送队列接入 client 的发送队列
        else {
            BUF_Q_MV(&client->send_buff_queue, &session->send_queue)
        }
    }

    // 如果 clear 的是 client 的所有 sessions 的发送队列
    if (all) session->send_next = session->send_prev = NULL;
    // 如果只 clear 一个 session 的发送队列
    else {
        // 将 sess 自身从 client 中的 sending sess 集合中移除
        if (session->send_prev) session->send_prev->send_next = session->send_next;
        else client->send_sess_head = session->send_next;
        if (session->send_next) session->send_next->send_prev = session->send_prev;
        else client->send_sess_rear = session->send_prev;
    }
}

// 清除 client 的发送队列（除了正在发送中）
static void clear_client_sending(ct_client_t *client) {

    // 此时所有 session 肯定都已经释放
    assert(!client->base.sessions && !client->sending_sess);

    // 释放 client 级发送队列
    if (client->send_buff_queue.head) {
        buf16_item_t *item = client->send_buff_queue.head;
        if (client->sending_cur) { // 如果当前正在发送一个包，跳过（确保它是完整发送）
            client->send_buff_queue.rear = item; item = item->next;
            client->send_buff_queue.rear->next = NULL;
        }
        else client->send_buff_queue.head = client->send_buff_queue.rear = NULL;
        while (item) {
            buf16_item_t *next = item->next;
            // 如果是静态数据包，则标记为 NULL（未在发送队列中）；否则正常释放
            if (item->refer == ITEM_REF_STATIC) item->refer = NULL;
            else free_buf16(item);
            item = next;
        }
    }
}

//-----------------------------------------------------------------------------

// 关闭/销毁某个会话
void ct_close_session(ct_client_ctx_t* ctx, ct_session_t *session, bool terminate) {

    // 如果已和对端 session 建立连接
    if (PEER_ONLINE(session)) {
        ctx->session_break(session, CT_PEER(session), terminate ? SESS_BREAK_TERM : SESS_BREAK_CLOSE);
    }

    // 清除 session 的 sending 队列
    clear_session_sending(session, terminate, false);

    // 释放 session
    free_session_base(&session->base);
}

// 所有会话
static void custom_close_all_sessions(ct_client_ctx_t* ctx, ct_client_t* client, bool terminate) {


    while (client->base.sessions) { ct_session_t* session = (ct_session_t*)client->base.sessions;

        // 如果已和对端 session 建立连接
        if (PEER_ONLINE(session)) {
            ctx->session_break(session, CT_PEER(session), terminate ? SESS_BREAK_TERM : SESS_BREAK_CLOSE);
        }

        // 清除 session 的 sending 队列
        clear_session_sending(session, terminate, true);

        // 释放 session
        free_session_base(&session->base);
    }

    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;
    client->sending_sess = NULL;
}

///////////////////////////////////////////////////////////////////////////////

bool
ct_init_client(ct_client_t* client) {

    buf16_item_t *buf_item = alloc_buf16(BUF_FLAG_MTU(0));
    if (!buf_item) {
        print("E:", LA_F("[TCP] OOM: cannot allocate recv buffer for new client\n", LA_F133, 133));
        return false;
    }

    client->last_error = 0;

    client->recv_buf = buf_item;
    client->recv_cur = 0;
    
    client->send_buff_queue.head = NULL;
    client->send_buff_queue.rear = NULL;
    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;
    client->sending_sess = NULL;
    client->sending_cur = 0;

    TCP_CLIENT_INIT(client);
    return true;
}

void
ct_migrate_client(client_t *to_base, client_t *from_base) {
    ct_client_t *to   = (ct_client_t*)to_base;
    ct_client_t *from = (ct_client_t*)from_base;

    // recv_buf 所有权转移（to 已由 ct_init_client 分配，需先释放）
    if (to->recv_buf) {
        if (to->recv_buf->next) free_buf16(to->recv_buf->next);
        free_buf16(to->recv_buf);
    }
    to->recv_buf      = from->recv_buf;  from->recv_buf  = NULL;

    // hdr_rs 已读入内容迁移（帧模式：将 from->hdr_rs 中已读字节拷贝到 to->hdr_rs）
    // + 拷贝上限取 to->hdr_sz，防止 from->recv_cur 超出 to->hdr_rs 缓冲区大小
    if (to->hdr_rs && from->hdr_rs && from->recv_cur > 0)
        memcpy(to->hdr_rs, from->hdr_rs, to->hdr_sz);
    to->recv_cur      = from->recv_cur;  from->recv_cur  = 0;

    // payload_buf 所有权转移
    if (to->payload_buf) {
        if (to->payload_buf->next) free_buf16(to->payload_buf->next);
        free_buffer(to->payload_buf); 
    }
    to->payload_buf  = from->payload_buf; from->payload_buf = NULL;
    to->payload_cur  = from->payload_cur; from->payload_cur = 0;
}

// 释放 client
void
ct_free_client(ct_client_ctx_t* ctx, ct_client_t *client) {

    if (client->base.sessions)
        custom_close_all_sessions(ctx, client, true);

    // 释放 recv buf
    if (client->recv_buf) {
        if (client->recv_buf->next) free_buf16(client->recv_buf->next);
        free_buf16(client->recv_buf);
        client->recv_buf = NULL;
    }
    // 释放 payload buf
    if (client->payload_buf) {
        if (client->payload_buf->next) free_buf16(client->payload_buf->next);
        free_buffer(client->payload_buf);
        client->payload_buf = NULL;
    }

    client->sending_cur = 0;     // 确保正在发送中的数据包也被清除
    clear_client_sending(client);

    free_client_base(&client->base);
}

// 优雅的关闭 client
void 
ct_client_off(ct_client_ctx_t* ctx, ct_client_t *client) {

    // 中断所有 session
    if (client->base.sessions)
        custom_close_all_sessions(ctx, client, false);

    // 释放 recv buf
    if (client->recv_buf) {
        if (client->recv_buf->next) free_buf16(client->recv_buf->next);
        free_buf16(client->recv_buf);
        client->recv_buf = NULL;
    }
    if (client->payload_buf) {
        if (client->payload_buf->next) free_buf16(client->payload_buf->next);
        free_buffer(client->payload_buf);
        client->payload_buf = NULL;
    }

    // 如果发送队列不为空，标记为 closing（send 完成后会自动完成 term），否则直接 term
    if (client->send_buff_queue.head) {
        client->handshake = TCP_HS_FLAG_CLOSING;
        client->io &= ~TCP_IO_FLAG_WANT_READ;           // 停止接收数据
    }
    else free_client_base(&client->base);
}

void
ct_client_error(ct_client_ctx_t* ctx, ct_client_t *client, int16_t error, bool fatal) {

    assert(error);
    client->last_error = error;

    client->io &= ~TCP_IO_FLAG_WANT_READ;               // 停止接收数据

    if (!fatal && ctx->error_item) {

        buf16_item_t *err_item = ctx->error_item(client, false);
        if (err_item) {
            err_item->refer = ITEM_REF_CLIENT_ERROR;    // 标记该 buf_item 是 error 包
            ct_client_send(client, err_item, true);
            return;
        }

        print("E:", LA_F("make err(%d) resp failed(OOM)\n", LA_F229, 229), client->last_error);
        client->last_error = CUSTOM_TCP_ERR_INTERNAL;
    }

    // 终止所有 session
    if (client->base.sessions)
        custom_close_all_sessions(ctx, client, true);

    // 清除除了正在发送的包以外的所有待发送数据
    clear_client_sending(client);

    // 追加 fatal 作为最后一项（如果 fatal_item 为 NULL，直接释放 client）
    if (!ctx->fatal_item) {
        ct_free_client(ctx, client);
        return;
    }
    
    if (client->send_buff_queue.rear) {
        client->send_buff_queue.rear->next = ctx->fatal_item;
        assert((client->io & TCP_IO_FLAG_WANT_WRITE));
    }
    else { assert(!client->send_buff_queue.head);
        client->send_buff_queue.head = ctx->fatal_item;
        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }
    client->send_buff_queue.rear = ctx->fatal_item;
}

void
ct_reactive_client(ct_client_ctx_t* ctx, ct_client_t *client) { (void)ctx;

    client->last_error = 0;                     // 重置错误状态
    client->io |= TCP_IO_FLAG_WANT_READ;        // 重新激活读取（之前断网时会被关闭）
    if (client->send_buff_queue.head || client->send_sess_head)
        client->io |= TCP_IO_FLAG_WANT_WRITE;   // 如果存在未完成的发送，重新激活写入
}

///////////////////////////////////////////////////////////////////////////////

static inline void prepare_next_recv(ct_client_t *client, buf16_item_t* recv_buf) {

    if (recv_buf != client->recv_buf) {
        // 如果之前是帧模式，转为流模式
        if (!recv_buf) {

            // 之前存在暂存（未消费完成）的 recv_buf
            // ! 帧模式处理完成后默认会自动释放 payload_buf，除非该 payload_buf 是之前暂存（未消费完成）的特殊 recv_buf
            if (client->payload_buf) { assert(client->payload_buf->refer == (void*)-1 && !client->payload_buf->next);
                client->payload_buf->next = client->recv_buf;       // 新的流模式 recv_buf 需要延迟切换，所以作为缓释 recv_buf
                client->recv_buf = client->payload_buf;
                client->payload_buf = NULL;
            }
        }
        // 如果之前是流模式，转为帧模式
        else if (!client->recv_buf) { assert(!client->payload_buf);
            if (recv_buf->next) {                                   // 之前设置的缓释 recv_buf 已经没有意义，即还没完成延迟切换就又切换了
                free_buf16(recv_buf->next); recv_buf->next = NULL;
            }

            // 如果数据已完全消费，直接释放 buf（清零游标后下面会释放）
            // ! 对于流模式，需要在每帧处理完成后，检查释放 recv_buf；因为它不像流模式那样，可以在下次读取更多数据前，自动重置或释放 recv_buf
            if (recv_buf->pos == recv_buf->len) client->recv_cur = 0;

            // 如果还有未消费完成的数据，需要作为特殊的 payload_buf 暂存，refer == (void*)-1 作为标记
            if (client->recv_cur) {
                recv_buf->refer = (void*)-1; client->payload_buf = recv_buf;
            } else free_buf16(recv_buf);
        }
        // 如果只是调整流模式的 recv_buf
        else { assert(!client->recv_buf->pos && !client->recv_buf->len && !client->recv_buf->next);   // 新设置的 recv_buf 内容应该是空的

            // 如果之前的 recv_buf 已经全部被消费完成则直接释放
            // + 事实上，这里不释放也可以，因为流模式支持在下次读取更多数据前，自动重置或释放 recv_buf
            if (!client->recv_cur) { assert(!recv_buf->pos && !recv_buf->len);
                free_buf16(recv_buf);
            }
            // 否则新设置的 recv_buf 需要进行延迟切换，即需要作为之前的 recv_buf 的缓释 recv_buf
            else {
                // 如果之前已经有一个缓释的 recv_buf，则先释放它（也就是之前还没来得及切换就又切换了）
                if (recv_buf->next) free_buf16(recv_buf->next);
                recv_buf->next = client->recv_buf;
                client->recv_buf = recv_buf;
            }
        }
    }
}

void
ct_handle_recv(ct_client_ctx_t* ctx, ct_client_t *client, const char* SP) {

    assert(client->hdr_rs && client->hdr_sz);

    uint8_t* buf; uint16_t sz, payload_offset; uint32_t payload_len, len; size_t io;
    buf16_item_t *payload_item, *buf_item, *bak_item, *ack_item =  NULL;
    client->base.last_active = P_tick_ms(); int r; int16_t error;
    for(assert(client->handshake >= 0);;) {

        // 握手写阶段，禁止接收新消息（此时应该已经取消了 TCP_IO_FLAG_WANT_READ）
        // + 相应的，该阶段的 recv_buf 会被 handshake ack 复用作为 send_buf
        assert(client->handshake <= 0 || (TCP_HS_IS_HANDSHAKING(client) && !(client->io & TCP_IO_FLAG_WANT_WRITE)));

        payload_item = client->payload_buf;

        // 以流模式获取 header，即通过结尾标识符来确定边界（此时 hdr_rs 表达的是一个结尾标识字节串）
        if (client->recv_buf) {

            // 如果未处于加载 payload 阶段，即处于加载 hdr 阶段、或之前读取缓存未被全部消费完成
            if (!payload_item) {

                buf_item = client->recv_buf; assert(!buf_item->refer);
                buf = ITEM2BUF(buf_item); sz = buffer_size(buf_item->flags);

                // 检索 hdr 边界标识
                for (;;) {

                    // 如果需要加载更多数据
                    // + 这里 recv_cur 是对 boundary 进行检索匹配的游标指针（对应的 pos 则指向本次请求数据，即 header 的起始点）
                    if ((len = client->recv_cur + client->hdr_sz) > buf_item->len) {
                        RECV_MORE:

                        // 如果之前存在已消费的数据，则将其清除（为后续读取腾出空间）
                        if (buf_item->pos) {
                            buf_item->len -= buf_item->pos;
                            client->recv_cur -= buf_item->pos;
                            len -= buf_item->pos;

                            // 如果存在延迟切换中的 recv_buf，则执行切换
                            if (buf_item->next) { bak_item = buf_item->next;
                                assert(!bak_item->pos && !bak_item->len && !bak_item->next);
                                sz = buffer_size(buf_item->flags);
                                if (buf_item->len) {
                                    if (buf_item->len >= sz) {
                                        error = CUSTOM_TCP_ERR_OVERFLOW;
                                        if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                                        goto error;
                                    }
                                    memcpy(ITEM2BUF(bak_item), buf + buf_item->pos, buf_item->len);
                                    bak_item->len = buf_item->len;
                                }
                                free_buf16(buf_item);
                                client->recv_buf = buf_item = bak_item;
                            }
                            else {
                                if (buf_item->len) memmove(buf, buf + buf_item->pos, buf_item->len);
                                buf_item->pos = 0;
                            }
                        }
                        else assert(!buf_item->next);

                        // 超出预设缓存大小限制
                        if (len > sz) {
                            error = CUSTOM_TCP_ERR_OVERFLOW;
                            if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                            goto error;
                        }

                        // 计算需要读取的数据长度
                        // + 这里可以设置一个最大读取 chunk size。因为对于流式（动态边界）模式，避免不了对非对齐的后续数据进行移动的操作
                        //   而这个 chunk size 值越小，可能移动的数据量也就越小，性能也就越好（但读取次数的增多，也会导致系统调用的性能开销增加）
                        io = sz - buf_item->len;
                        if (io > STREAM_HEADER_RECV_MAX) io = STREAM_HEADER_RECV_MAX;

                        r = tcp_recv((tcp_client_t*)client, buf + buf_item->len, &io, SP);
                        if (r > 0) return;
                        if (r < 0) {
                            error = r < -1 ? CUSTOM_TCP_ERR_IO : CUSTOM_TCP_ERR_DISCONNECTED;
                            if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                            goto error;
                        }

                        if (!io) continue;
                        buf_item->len += io;
                    }

                    // 如果已经找到 hdr 标识的第一个字节
                    if (buf[client->recv_cur] == *client->hdr_rs) { assert(len <= buf_item->len);
                        LOOP_CMP:
                        if (memcmp(buf + client->recv_cur, client->hdr_rs, client->hdr_sz) == 0) break;
                    }
                    // 检索 hdr 标识的第一个字节
                    while (++client->recv_cur < buf_item->len) {
                        if (buf[client->recv_cur] == *client->hdr_rs) {
                            len = client->recv_cur + client->hdr_sz;
                            if (len > buf_item->len) goto RECV_MORE;
                            goto LOOP_CMP;
                        }
                    }
                    goto RECV_MORE;
                }

                buf += buf_item->pos;                               // 记录 hdr 指针
                sz = client->recv_cur - buf_item->pos;              // 记录 hdr len

                // 解析 payload len, payload offset
                // + 这里允许应用层给 payload 留出一个 offset 前置空间，以便实现将上行的 payload 包直接作为下行的应答包、或中继转发包
                // ! 作为流模式的 header 协议，此时获得的 header 应该是完整的，所以要求必须解析出有效的 payload_len 值
                r = ctx->resolve_payload_len(client, buf, sz, &payload_len, &payload_offset);
                if (r != E_NONE) {
                    print("E:", LA_F("[CT] resolve payload len failed(%d)\n", 0, 0), r);
                    error = CUSTOM_TCP_ERR_PROTOCOL;
                    if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                    goto error;
                }

                // 安全检查：payload 过大溢出
                // TODO(WS): RFC 6455 §7.4.1 要求发送 close(1009 Message Too Big) 而非直接断 TCP
                //           当前底层无法感知上层是否为 WS 协议，暂用 CUSTOM_TCP_ERR_OVERFLOW 统一处理
                if (payload_len > ctx->max_payload_len) {
                    error = CUSTOM_TCP_ERR_OVERFLOW;
                    if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                    goto error;
                }

                client->recv_cur += client->hdr_sz;                 // 跳过 hdr boundary，指向后面 payload 数据的起始地址

                // 如果没有 payload 信息，说明已经完备，直接触发协议处理回调
                if (!payload_len) {

                    buf_item->pos = client->recv_cur;               // 修改 pos 为 payload 数据的起始地址

                    // 触发应用协议层回调处理
                    if (TCP_HS_IS_HANDSHAKING(client)) {

                        ack_item = ctx->handle_handshake(&client, buf, sz, NULL, NULL);
                        assert(buf_item == client->recv_buf);       // handle_handshake 期间不应调整（下一次请求的）recv_buf
                        goto handshake;
                    }

                    ctx->handle_proto(client, buf, sz, NULL, NULL);
                    if (buf_item != client->recv_buf) {
                        if (!client->recv_buf) {                    // 如果变成帧模式
                            client->payload_buf = buf_item;         // 将 recv_buf 后续数据作为 payload_buf
                            buf_item->refer = (void*)-1;            // 标记 payload_buf 为后续数据
                        } else {                                    // 如果调整了 recv_buf（如大小）
                            if (buf_item->next)
                                free_buf16(buf_item->next);
                            buf_item->next = client->recv_buf;      // 作为延迟切换项
                            client->recv_buf = buf_item;
                        }
                    }
                    continue;
                }

                // 如果 payload 已经完备，直接触发协议处理回调
                if (buf_item->len >= client->recv_cur + payload_len) {

                    buf_item->pos = client->recv_cur;               // 修改 pos 为 payload 数据的起始地址
                    client->recv_cur += payload_len;                // 跳过 payload，指向后续数据的起始位置

                    len = buf_item->len;                            // 备份 recv_buf 的 len（即包括 payload 以及后续数据部分）
                    buf_item->len = client->recv_cur;               // 修改 recv_buf 的 len 使其仅包含当前 payload 的数据大小

                    // 触发应用协议层回调处理
                    if (TCP_HS_IS_HANDSHAKING(client)) {
                        ack_item = ctx->handle_handshake(&client, buf, sz, buf_item, NULL);
                        assert(buf_item == client->recv_buf);       // handle_handshake 期间不应调整（下一次请求的）recv_buf
                        if (ack_item) {
                            buf_item->len = len;                    // 恢复 recv_buf 的 len
                            buf_item->pos = client->recv_cur;       // 推进 recv_buf 的 pos（下一次请求的起始位置）
                        }
                        goto handshake;
                    }

                    ctx->handle_proto(client, buf, sz, buf_item, NULL);
                    buf_item->len = len;                            // 恢复 recv_buf 的 len
                    buf_item->pos = client->recv_cur;               // 推进 recv_buf 的 pos（下一次请求的起始位置）
                    if (buf_item != client->recv_buf) {
                        if (!client->recv_buf) {                    // 如果变成帧模式
                            client->payload_buf = buf_item;         // 将 recv_buf 后续数据作为 payload_buf
                            buf_item->refer = (void*)-1;            // 标记 payload_buf 为后续数据
                        } else {                                    // 如果调整了 recv_buf（如大小）
                            if (buf_item->next)
                                free_buf16(buf_item->next);
                            buf_item->next = client->recv_buf;      // 作为延迟切换项
                            client->recv_buf = buf_item;
                        }
                    }
                    continue;
                }

                // 此时 recv_buf->pos 指向 hdr; client->recv_cur 指向 payload 的起始位置，但 payload 数据尚未完全加载完成

                // 分配新的 payload buffer
                payload_item = alloc_buffer(0, payload_len += payload_offset);
                if (!payload_item) {
                    error = CUSTOM_TCP_ERR_INTERNAL;
                    if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                    ct_client_error(ctx, client, error, true);
                    return;
                }
                payload_item->len = payload_len;
                payload_item->pos = payload_offset;

                client->payload_buf = payload_item;
                client->payload_cur = payload_offset + (buf_item->len - client->recv_cur);
                payload_item->pos = client->payload_cur;                // 指向实际读入数据的起始位置；[payload_offset, pos) 为预留空间供应用将 payload0 拷贝至此以获得连续缓冲
            }
        }
        // 以帧模式获取 header，即固定 header 结构 size
        else do {

            if (!payload_item) {
                ext_hdr:
                while (client->recv_cur < client->hdr_sz) {
                    io = client->hdr_sz - client->recv_cur;
                    r = tcp_recv((tcp_client_t*)client, client->hdr_rs + client->recv_cur, &io, NULL);
                    if (r > 0) return;
                    if (r < 0) {
                        error = r < -1 ? CUSTOM_TCP_ERR_IO : CUSTOM_TCP_ERR_DISCONNECTED;
                        if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                        goto error;
                    }
                    client->recv_cur += (uint16_t)io;
                }

                // 解析 payload size, payload offset
                // + 作为帧模式的 header 协议，允许根据对已有的 hdr 部分的解析来动态调整后续的 hdr 类型尺寸
                r = ctx->resolve_payload_len(client, client->hdr_rs, client->hdr_sz, &payload_len, &payload_offset);
                if (r < 0) {
                    print("E:", LA_F("[CT] resolve payload len failed(%d)\n", 0, 0), r);
                    error = CUSTOM_TCP_ERR_PROTOCOL;
                    if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                    goto error;
                }
                if (r) { assert(client->recv_cur < client->hdr_sz);
                    goto ext_hdr;
                }
            }
            // 如果存在缓释的 recv_buf
            else if (payload_item->refer) { assert(!payload_item->next);    // 这里不应该有缓释的 recv_buf（延迟切换项）

                assert(client->recv_cur == payload_item->pos);
                buf = ITEM2BUF(payload_item) + payload_item->pos;           // 获取 payload 中已消费之后的剩余数据
                sz  = payload_item->len - payload_item->pos; assert(sz);    // 获取 payload 中已消费之后的剩余部分长度
                for (;;) {

                    // 如果已消费剩余部分不足一个 hdr 的长度，删除 payload，并继续读取后续的 hdr
                    if (sz < client->hdr_sz) {
                        if (sz) memcpy(client->hdr_rs, buf, sz);            // 复制剩余的部分到 hdr 缓冲
                        client->recv_cur = sz;                              // recv_cur 变为 hdr 读取游标
                        free_buf16(payload_item);                           // 释放缓释的 recv_buf
                        client->payload_buf = NULL;
                        goto ext_hdr;                                       // 继续加载更多 hdr
                    }

                    // 解析 payload size, payload offset
                    // + 如果动态扩展需要读取更多的 hdr size，则继续重新读取 hdr
                    len = client->hdr_sz;
                    r = ctx->resolve_payload_len(client, buf, client->hdr_sz, &payload_len, &payload_offset);
                    if (r < 0) {
                        print("E:", LA_F("[CT] resolve payload len failed(%d)\n", 0, 0), r);
                        error = CUSTOM_TCP_ERR_PROTOCOL;
                        if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                        goto error;
                    }
                    if (r) { assert(client->hdr_sz > len);
                        continue;
                    }

                    if (payload_len > ctx->max_payload_len) {
                        error = CUSTOM_TCP_ERR_OVERFLOW;
                        if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                        goto error;
                    }

                    client->recv_cur += client->hdr_sz;                     // 跳过 hdr，指向后面 payload 数据的起始地址
                    payload_item->pos = client->recv_cur;                   // 修改 pos 为 payload 数据的起始地址

                    // 如果已读剩余部分可以构成完备的 payload 数据
                    if (!payload_len) {


                        // 触发应用协议层回调处理
                        if (TCP_HS_IS_HANDSHAKING(client)) {

                            ack_item = ctx->handle_handshake(&client, buf, client->hdr_sz, NULL, NULL);
                            assert(!client->recv_buf);                      // handle_handshake 期间不应调整（下一次请求的）recv_buf
                            goto handshake;
                        }

                        ctx->handle_proto(client, buf, client->hdr_sz, NULL, NULL);
                        if (client->recv_buf) {                             // 重新变成流模式
                            payload_item->next = client->recv_buf;          // 作为之前未消费完成 recv_buf 的延迟切换项
                            payload_item->refer = NULL;
                            client->recv_buf = payload_item;
                            client->payload_buf = NULL;
                        }

                        sz -= client->hdr_sz;
                        continue;
                    }
                    if (sz >= client->hdr_sz + payload_len) {

                        client->recv_cur += payload_len;                    // 跳过 payload，指向后续数据的起始位置

                        len = payload_item->len;                            // 备份 recv_buf 的 len（即包括 payload 以及后续数据部分）
                        payload_item->len = client->recv_cur;               // 修改 recv_buf 的 len 使其仅包含当前 payload 的数据大小

                        // 触发应用协议层回调处理
                        if (TCP_HS_IS_HANDSHAKING(client)) {
                            ack_item = ctx->handle_handshake(&client, buf, client->hdr_sz, payload_item, NULL);
                            assert(!client->recv_buf);                      // handle_handshake 期间不应调整（下一次请求的）recv_buf
                            if (ack_item) {
                                payload_item->len = len;                    // 恢复 recv_buf 的 len
                                payload_item->pos = client->recv_cur;       // 推进 recv_buf 的 pos（下一次请求的起始位置）
                            }
                            goto handshake;
                        }

                        ctx->handle_proto(client, buf, client->hdr_sz, payload_item, NULL);
                        payload_item->len = len;                            // 恢复 recv_buf 的 len
                        payload_item->pos = client->recv_cur;               // 推进 recv_buf 的 pos（下一次请求的起始位置）
                        if (client->recv_buf) {                             // 重新变成流模式
                            payload_item->next = client->recv_buf;          // 作为之前未消费完成 recv_buf 的延迟切换项
                            payload_item->refer = NULL;
                            client->recv_buf = payload_item;
                            client->payload_buf = NULL;
                        }

                        sz = payload_item->len - client->recv_cur;
                        continue;
                    }

                    // 缓释的 recv_buf 这里会被全部消费完成，此外还需要加载更多的 payload 数据（recv_buf 只含有部分 payload 数据、或只含有 hdr 数据）
                    // + 此时 recv_buf->pos 指向 payload 的起始位置（如果存在），同时 recv_buf->pos == client->recv_cur

                    memcpy(client->hdr_rs, buf, client->hdr_sz);            // 复制数据到 hdr 缓存
                    if (sz == client->hdr_sz) {                             // 如果只含有 hdr 数据，即数据全部消费完成，直接释放
                        free_buf16(payload_item);
                        client->payload_buf = NULL;
                    }

                    client->recv_cur = client->hdr_sz;                      // recv_cur 变为 hdr 读取游标
                    break;
                }
            }
            else break;

            // 分配新的 payload buffer
            payload_item = alloc_buffer(0, payload_len += payload_offset);
            if (!payload_item) {
                error = CUSTOM_TCP_ERR_INTERNAL;
                if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                ct_client_error(ctx, client, error, true);
                return;
            }
            if (BUF_IS_32BIT(payload_item->flags)) {
                BUF32(payload_item)->len = payload_len;
                BUF32(payload_item)->pos = payload_offset;
                buf = ITEM2BUF(BUF32(payload_item));
            } else {
                payload_item->len = payload_len;
                payload_item->pos = payload_offset;
                buf = ITEM2BUF(payload_item);
            }

            client->payload_cur = payload_offset;                           // 跳过 offset 前置空间

            // 如果存在未消费完成的 recv_buf，将其复制到 payload buffer
            if (client->payload_buf) { assert(client->payload_buf->refer);
                sz = client->payload_buf->len - client->payload_buf->pos;
                client->payload_cur += sz;                                  // 跳过已加载（复制）的部分
                memcpy(buf + payload_offset, ITEM2BUF(client->payload_buf) + client->payload_buf->pos, sz);
                free_buf16(client->payload_buf);
            }
            client->payload_buf = payload_item;                             // 始终更新 payload_buf（帧模式首包 payload_buf=NULL 的情况）

        } while (0);

        // 读取完整 payload
        assert(payload_item == client->payload_buf && !payload_item->refer);
        if (BUF_IS_32BIT(payload_item->flags)) {
            buf = ITEM2BUF(BUF32(payload_item)); len = BUF32(payload_item)->len;
        } else { buf = ITEM2BUF(payload_item); len = client->payload_buf->len; }
        while (client->payload_cur < len) {
            io = len - client->payload_cur;
            r = tcp_recv((tcp_client_t*)client, buf + client->payload_cur, &io, SP);
            if (r > 0) return;
            if (r < 0) {
                error = r < -1 ? CUSTOM_TCP_ERR_IO : CUSTOM_TCP_ERR_DISCONNECTED;
                if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                goto error;
            }
            client->payload_cur += (uint32_t)io;
        }

        buf_item = client->recv_buf; bak_item = buf_item;               // 备份 recv_buf，因为 handle_proto 可能会重置它
        if (buf_item) {
            buf = ITEM2BUF(buf_item) + buf_item->pos;                   // 获取 hdr（此时 pos 指向 hdr 的起始地址）
            sz = client->recv_cur - client->hdr_sz - buf_item->pos;     // 计算 hdr size（此时 recv_cur 指向 payload 的起始地址）
            if (buf_item->len > client->recv_cur)                       // 如果 payload 部分不为空
                buf_item->pos = client->recv_cur;                       // pos 指向 payload 的起始位置
            else buf_item = NULL;
        }
        else { buf = client->hdr_rs; sz = client->hdr_sz; }

        if (TCP_HS_IS_HANDSHAKING(client)) {
            ack_item = ctx->handle_handshake(&client, buf, sz, buf_item, payload_item);
            assert(bak_item == client->recv_buf);                       // handle_handshake 期间不应调整（下一次请求的）recv_buf

            if (client->payload_buf) {
                free_buffer(payload_item);
                client->payload_buf = NULL;
            }
            client->payload_cur = 0;

            client->recv_cur = 0;
            if (bak_item) bak_item->pos = bak_item->len = 0;            // 此时的 recv_buf（如果存在）肯定已经全部被消费完成
            goto handshake;
        }
        ctx->handle_proto(client, buf, sz, buf_item, payload_item);

        if (client->payload_buf) {
            free_buffer(payload_item);                                  // 若回调置 payload_buf=NULL 表示已接管所有权，跳过释放
            client->payload_buf = NULL;
        }
        client->payload_cur = 0;

        client->recv_cur = 0;
        if (bak_item) bak_item->pos = bak_item->len = 0;
        goto prepare_next;

    handshake:  // 握手阶段可以直接写入，而无需再次等待 writable 周期判定。但注意，由于支持重连前移机制，此时 client 可能已不是初始分配的 client

        if (!ack_item) {

            // 握手成功必须返回应答包
            if (!client->last_error) error = CUSTOM_TCP_ERR_PROTOCOL;

            // 握手阶段报错但不返回应答，则直接释放 client
            if (!ctx->error_item || !((ack_item = ctx->error_item(client, true)))) {
                ct_free_client(ctx, client);
                return;
            }
        }
        else assert(!BUF_IS_32BIT(ack_item->flags) && !client->last_error);

        io = ack_item->len;
        r = tcp_send((tcp_client_t*)client, ITEM2BUF(ack_item), &io, SP);
        if (r < 0) {
            ct_free_client(ctx, client);    // 握手阶段发送失败，直接释放 client
            return;
        }

        // 如果 would block，则标记进入握手写入阶段
        if (r > 0) {
            BUF_Q_PUSH(&client->send_buff_queue, ack_item)
            client->sending_cur = io;

            // 进入握手写阶段，同时暂停接收数据，等握手（ACK 发送）完成后再继续
            client->io &= ~TCP_IO_FLAG_WANT_READ;
            client->io |= TCP_IO_FLAG_WANT_WRITE;
            return;
        }

        free_buf16(ack_item);

        // （应答直接发送完成）如果握手阶段存在失败，直接释放 client
        if (client->last_error) {
            ct_free_client(ctx, client);
            return;
        }

        // 握手阶段可以直接写入，而无需再次等待 writable 周期判定

        if (!ctx->handshake_finish) {
            client->handshake = 0;
            continue;
        }
        bak_item = client->recv_buf;        // handshake_finish 可能会重置 recv_buf，所以这里需要备份
        ctx->handshake_finish(client);

    prepare_next:
        prepare_next_recv(client, bak_item);
    }   // for(;;)


// 网络 I/O 等非致命错误，也就是不破坏（之前/已发生的）数据完整性的错误
// + 此时会清除 recv_buf 中的数据、关闭读取，同时（最后）发送一个错误状态码，并等发送完成后会自动关闭连接
error: assert(error && !client->last_error);

    // 清除已读取的部分
    client->recv_cur = 0;

    // session 执行 stop 处理
    if (client->base.sessions) {
        for(session_t *sess = client->base.sessions, *peer; sess; sess = sess->next) { peer = sess->peer;
            if (PEER_VALID(peer) && TCP_REACHABLE(peer->client)) {
                ctx->session_break((ct_session_t*)sess, (ct_session_t*)peer, SESS_BREAK_STOP);
            }
        }
    }

    // client 执行 unreachable 处理
    if (ctx->client_unreachable)
        ctx->client_unreachable(client, true);

    // 报错处理（这会停止接收新的请求数据，也会禁止再发送新消息，即错误是最后一个消息，所以必须在最后执行）
    ct_client_error(ctx, client, error, false);

    // 重新计时，等待客户端（重置）处理，并通过超时机制来释放 client
    client->base.last_active = P_tick_ms();
}

// 处理 RELAY 模式信令发送（TCP 长连接）- 统一队列发送
void
ct_handle_send(ct_client_ctx_t* ctx, ct_client_t *client, const char* SP) {

    // 如果当前处于握手阶段
    // + 此时会复用 recv_buf 作为 send_buf，recv_cur 作为已发送长度
    if (TCP_HS_IS_HANDSHAKING(client)) {

        assert(client->send_buff_queue.head);
        buf16_item_t* buf_item = client->send_buff_queue.head; 
        assert(buf_item == client->send_buff_queue.rear);                           // 握手阶段 send_queue 只会有一个 buf_item
        assert(!BUF_IS_32BIT(buf_item->flags) && buf_item->pos < buf_item->len);    // 握手阶段只允许 buf16_item_t（ACK/错误包），禁止 32-bit 大包
        
        // 首次发送时对齐到有效起始位置，后续 sending_cur 统一表示绝对偏移
        if (!client->sending_cur) client->sending_cur = buf_item->pos;

        assert(client->sending_cur >= buf_item->pos && client->sending_cur <= buf_item->len);
        size_t sz = buf_item->len, io = sz - client->sending_cur;
        int r = tcp_send((tcp_client_t*)client, ITEM2BUF(buf_item) + client->sending_cur, &io, SP);
        if (r < 0) {
            ct_free_client(ctx, client);    // 握手阶段发送失败，直接释放 client
            return;
        }

        if (!io || (client->sending_cur += io) < sz) return;

        client->sending_cur = 0;
        BUF_Q_POP(&client->send_buff_queue, buf_item);
        free_buf16(buf_item);

        // 握手（应答发送）完成
        print("V:", LA_F("handshake<%d> sent to '%s'\n", LA_F179, 179), client->handshake, client->base.local_peer_id);

        // 如果（发送的是）握手阶段的错误应答，直接释放 client
        if (client->last_error) {
            ct_free_client(ctx, client);
            return;
        }

        if (ctx->handshake_finish) {
            buf_item = client->recv_buf;
            ctx->handshake_finish(client);
            prepare_next_recv(client, buf_item);
        } else client->handshake = 0;

        // 启动正常读写
        client->io |= TCP_IO_FLAG_WANT_READ;
        client->io &= ~TCP_IO_FLAG_WANT_WRITE;      // 初始时还没有要写入的数据

        // 如果还没有完成握手（例如子协议的二阶段握手）
        if (TCP_HS_IS_HANDSHAKING(client)) return;
    }

    for (;;) {
        ct_session_t *sending_session = client->send_sess_head; buf16_item_t *item = client->send_buff_queue.head;

        // 全部发送完成
        if (!sending_session && !item) {
            client->io &= ~TCP_IO_FLAG_WANT_WRITE;
            if (!(client->io & TCP_IO_FLAG_WANT_READ)) {
                if (TCP_HS_IS_CLOSING(client)) ct_free_client(ctx, client);
                else { P_sock_close(client->base.fd);
                    client->base.fd = P_INVALID_SOCKET;
                }
            }
            return;
        }
        assert(client->io & TCP_IO_FLAG_WANT_WRITE);

        // 如果正在发送 session 数据包（mid-packet），继续发送当前 session
        if (client->sending_sess && client->sending_cur) { assert(sending_session);
            sending_session = client->sending_sess;
            item = sending_session->send_queue.head;
        }
        // 否则优先发送 client 级别的包；没有 client 包时，从游标 session 发送
        else if (!item) { assert(sending_session);
            if (!client->sending_sess) client->sending_sess = sending_session;
            else sending_session = client->sending_sess;
            item = sending_session->send_queue.head;
        }

        uint8_t* buf; uint32_t sz; uint16_t pos;
        if (BUF_IS_32BIT(item->flags)) { buf32_item_t *buf32 = BUF32(item);
            buf = ITEM2BUF(buf32); sz = buf32->len; pos = buf32->pos;
        } else { buf = ITEM2BUF(item); sz = item->len; pos = item->pos; }
        assert(pos < sz);

        // 首次发送时对齐到有效起始位置，后续 sending_cur 统一表示绝对偏移
        if (!client->sending_cur) client->sending_cur = pos;

        assert(client->sending_cur >= pos && client->sending_cur <= sz);
        size_t io = sz - client->sending_cur;

        int rc = tcp_send((tcp_client_t*)client, buf + client->sending_cur, &io, SP);
        if (rc < 0) {

            // 如果当前发生了 fatal 错误，则发送失败后直接销毁 client
            if (item == ctx->fatal_item || item->next == ctx->fatal_item) {
                ct_free_client(ctx, client);
                return;
            }

            // 清除已发送的部分
            client->sending_cur = 0;
            // 执行 unreachable 处理
            for(session_t *sess = client->base.sessions, *peer; sess; sess = sess->next) { peer = sess->peer;
                if (PEER_VALID(peer) && TCP_REACHABLE(peer->client)) {
                    ctx->session_break((ct_session_t*)sess, (ct_session_t*)peer, SESS_BREAK_STOP);
                }
            }
            if (ctx->client_unreachable)
                ctx->client_unreachable(client, false);
            client->base.last_active = P_tick_ms();             // 重新计时，通过超时机制来释放 client

            // 直接关闭连接
            client->last_error = CUSTOM_TCP_ERR_IO;
            P_sock_close(client->base.fd);
            client->base.fd = P_INVALID_SOCKET;

            // 此时不再返回错误状态码了，直接停止读写（等待重连或超时回收）
            client->io &= ~(TCP_IO_FLAG_WANT_READ|TCP_IO_FLAG_WANT_WRITE);
            return;
        }
        if (rc > 0) return;     // would block，等待下次可写事件

        // 部分发送：更新偏移量，等下次可写事件继续
        if (!io || (client->sending_cur += (uint32_t)io) < sz) return;
        client->sending_cur = 0;

        // 当前 item 发送完成，推进发送队列

        // 如果是 client 级的 item
        if (item==client->send_buff_queue.head) {

            // 发送 client sending 队列的下一项
            BUF_Q_POP(&client->send_buff_queue, item);

            // 如果发送的是 fatal 错误包，发送完成后直接销毁 client
            if (item == ctx->fatal_item) {
                ct_free_client(ctx, client);
                return;
            }

            // 如果静态内嵌缓冲，不执行 free 操作，仅标记为 NULL（未在发送队列中，即发送完成）；否则正常释放
            if (item->refer == ITEM_REF_STATIC) item->refer = NULL;
            else { bool is_error = item->refer == ITEM_REF_CLIENT_ERROR;

                free_buffer(item);

                // 如果发送的是错误包，发送完成后直接关闭连接并停止写入（等待客户端重连或超时回收）
                if (is_error) { assert(client->last_error && !(client->io & TCP_IO_FLAG_WANT_READ));
                    P_sock_close(client->base.fd);
                    client->base.fd = P_INVALID_SOCKET;
                    client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                    return;
                }
            }
        }
        // 对于 session 级的 item（将 item 从 sess 的 send_queue 中移除
        // + 同时轮询下一个（session sending 队列不为空的）session，即 sess 平权遍历
        else { assert(item==sending_session->send_queue.head && item != ctx->fatal_item);

            // 将当前 session 的 sending 队列切换到下一项，同时如果发送队列不为空，轮询切换到下一个 sess
            if ((sending_session->send_queue.head = item->next)) {

                client->sending_sess = sending_session->send_next ? sending_session->send_next : client->send_sess_head;
            }
            // 如果当前 session 发送队列已空，从 client 中移除该 session，并直接切换到下一个 sess
            else { sending_session->send_queue.rear = NULL;

                ct_session_t *next = sending_session->send_next;
                if (sending_session->send_prev) sending_session->send_prev->send_next = next;
                else client->send_sess_head = next;
                if (next) {
                    next->send_prev = sending_session->send_prev;
                    client->sending_sess = next;
                }
                else {
                    client->send_sess_rear = sending_session->send_prev;
                    client->sending_sess = client->send_sess_head;
                }
                sending_session->send_next = NULL;
                sending_session->send_prev = NULL;
            }

            // 如果发送的是对端发过来的数据
            if (item->refer && ctx->handle_peer_sent) {
                ctx->handle_peer_sent((ct_session_t*)item->refer, item);
            }

            // 如果 item 没有被（handle_peer_sent）标记为待 ACK 状态，则直接释放
            if (item->refer != ITEM_REF_ACK_PENDING) { free_buffer(item); }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
