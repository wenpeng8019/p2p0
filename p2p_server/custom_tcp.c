//
// Created by 温朋 on 2026/4/18.
//
#define MOD_TAG "CUSTOM_TCP"

#include "custom_tcp.h"

#define ITEM_REF_CLIENT_ERROR       ((void*)(uintptr_t)-1)

///////////////////////////////////////////////////////////////////////////////

// 发送数据 buf 到 client 发送队列
void ct_client_send(ct_client_t *client, buffer_item_t* buf_item, bool immediate) {

    // 前提是不能处于握手/closing 阶段
    assert(client->handshake == 0);

    // 高优先级：插到当前正在发送包之后（若有），否则插到队头
    if (immediate) {

        // 如果存在正在发送的包，则将新包插入到正在发送的包之后
        if (client->sending_offset && !client->sending_sess) { assert(client->send_buff_head);
            buf_item->next = client->send_buff_head->next;
            client->send_buff_head->next = buf_item;
            if (!buf_item->next) client->send_buff_rear = buf_item;
            return;
        }

        // 添加到队列的最前面
        buf_item->next = client->send_buff_head;
        client->send_buff_head = buf_item;
        if (client->send_buff_rear) return;     // 如果队列之前不空，直接返回

    } else {

        buf_item->next = NULL;
        if (client->send_buff_rear) {
            client->send_buff_rear->next = buf_item;
            client->send_buff_rear = buf_item;
            return;
        }

        client->send_buff_head = buf_item;
    }
    client->send_buff_rear = buf_item;

    if (TCP_REACHABLE(client)) {
        assert(client->base.fd != P_INVALID_SOCKET && client->last_error == 0);
        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }
}

// 发送数据 buf 到 session 发送队列
void ct_session_send(ct_session_t *session, buffer_item_t* buf_item) {

    // session 存在意味 client 肯定未处于握手/closing 阶段
    ct_client_t *client = CT_CLIENT(session);

    // 添加到 session 的本地发送队列
    buf_item->next = NULL;
    if (session->send_rear) {
        session->send_rear->next = buf_item;
        session->send_rear = buf_item;
        return;
    }

    // 如果 session 发送队列之前为空
    assert(!session->send_next && !session->send_prev);
    session->send_head = session->send_rear = buf_item;

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

    buffer_item_t *item = session->send_head;
    if (!item) {
        assert(!session->send_rear && !session->send_next && !session->send_prev);
        return;
    }

    ct_client_t *client = CT_CLIENT(session);

    // 如果正在发送当前 session 的数据，需要将它转为 client 级发送队列的第一项
    if (client->sending_sess == session) {
        client->sending_sess = NULL;

        session->send_head = item->next;
        if (!session->send_head) session->send_rear = NULL;

        item->next = client->send_buff_head;
        client->send_buff_head = item;
        if (!client->send_buff_rear) client->send_buff_rear = item;
    }

    if (session->send_head) {

        // 如果 session 的数据需要被销毁，即无需继续发送给 client 端
        if (terminate) {

            while((item = session->send_head)) {
                session->send_head = item->next;
                free_buffer(item);
            }
            session->send_rear = NULL;
        }
        // 如果需要将 session 现有的数据发送给 client 端，则将 session 的发送队列接入 client 的发送队列
        else {

            if (client->send_buff_head) {
                client->send_buff_rear->next = session->send_head;
                client->send_buff_rear = session->send_rear;
            }
            else {
                client->send_buff_head = session->send_head;
                client->send_buff_rear = session->send_rear;
            }
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
    if (client->send_buff_head) {
        buffer_item_t *item = client->send_buff_head;
        if (client->sending_offset) { // 如果当前正在发送一个包，跳过（确保它是完整发送）
            client->send_buff_rear = item; item = item->next;
            client->send_buff_rear->next = NULL;
        }
        else client->send_buff_head = client->send_buff_rear = NULL;
        while (item) {
            buffer_item_t *next = item->next;
            // 如果是静态数据包，则标记为 NULL（未在发送队列中）；否则正常释放
            if (item->refer == ITEM_REF_STATIC) item->refer = NULL;
            else free_buffer(item);
            item = next;
        }
    }
}

//-----------------------------------------------------------------------------

// 关闭/销毁某个会话
void ct_close_session(custom_tcp_ctx_t* ctx, ct_session_t *session, bool terminate) {

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
static void custom_close_all_sessions(custom_tcp_ctx_t* ctx, ct_client_t* client, bool terminate) {


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

    buffer_item_t *buf_item = alloc_buffer(BUF_FLAG_MTU(0));
    if (!buf_item) {
        print("E:", LA_F("[TCP] OOM: cannot allocate recv buffer for new client\n", LA_F133, 133));
        return false;
    }

    client->last_error = 0;

    client->recv_buf = ITEM2BUF(buf_item);
    client->recv_len = 0;
    
    client->send_buff_head = NULL;
    client->send_buff_rear = NULL;
    client->send_sess_head = NULL;
    client->send_sess_rear = NULL;
    client->sending_sess = NULL;
    client->sending_offset = 0;

    TCP_CLIENT_INIT(client);
    return true;
}

// 释放 client
void
ct_free_client(custom_tcp_ctx_t* ctx, ct_client_t *client) {

    if (client->base.sessions)
        custom_close_all_sessions(ctx, client, true);

    // 释放 recv buf
    if (client->recv_buf) {
        free_buffer(BUF2ITEM(client->recv_buf));
        client->recv_buf = NULL;
    }

    client->sending_offset = 0;     // 确保正在发送中的数据包也被清除
    clear_client_sending(client);

    free_client_base(&client->base);
}

// 优雅的关闭 client
void 
ct_client_off(custom_tcp_ctx_t* ctx, ct_client_t *client) {

    // 中断所有 session
    if (client->base.sessions)
        custom_close_all_sessions(ctx, client, false);

    // 释放 recv buf
    if (client->recv_buf) {
        free_buffer(BUF2ITEM(client->recv_buf));
        client->recv_buf = NULL;
    }

    // 如果发送队列不为空，标记为 closing（send 完成后会自动完成 term），否则直接 term
    if (client->send_buff_head) {
        client->handshake = TCP_HS_FLAG_CLOSING;
        client->io &= ~TCP_IO_FLAG_WANT_READ;           // 停止接收数据
    }
    else free_client_base(&client->base);
}

void
ct_client_error(custom_tcp_ctx_t* ctx, ct_client_t *client, int error, bool fatal) {

    assert(error);
    client->last_error = error;

    client->io &= ~TCP_IO_FLAG_WANT_READ;               // 停止接收数据

    if (!fatal) {

        buffer_item_t *err_item = alloc_buffer(BUF_FLAG_512(0));
        if (err_item) {
            ctx->error_item(client, err_item);
            err_item->refer = ITEM_REF_CLIENT_ERROR;    // 标记该 buf_item 是 error 包
            ct_client_send(client, err_item, true);
            return;
        }

        print("E:", LA_F("make err(%d) resp failed(OOM)\n", 0, 0), client->last_error);
        client->last_error = CUSTOM_TCP_ERR_INTERNAL;
    }

    // 终止所有 session
    if (client->base.sessions)
        custom_close_all_sessions(ctx, client, true);

    // 清除除了正在发送的包以外的所有待发送数据
    clear_client_sending(client);

    // 追加 fatal 作为最后一项
    if (client->send_buff_rear) {
        client->send_buff_rear->next = ctx->fatal_item;
        assert((client->io & TCP_IO_FLAG_WANT_WRITE));
    }
    else { assert(!client->send_buff_head);
        client->send_buff_head = ctx->fatal_item;
        client->io |= TCP_IO_FLAG_WANT_WRITE;
    }
    client->send_buff_rear = ctx->fatal_item;
}

void
ct_reactive_client(custom_tcp_ctx_t* ctx, ct_client_t *client) { (void)ctx;

    client->last_error = 0;                     // 重置错误状态
    client->io |= TCP_IO_FLAG_WANT_READ;        // 重新激活读取（之前断网时会被关闭）
    if (client->send_buff_head || client->send_sess_head)
        client->io |= TCP_IO_FLAG_WANT_WRITE;   // 如果存在未完成的发送，重新激活写入
}

///////////////////////////////////////////////////////////////////////////////

// 处理 RELAY 模式信令（TCP 长连接）- 统一接收+分发架构
void 
ct_handle_recv(custom_tcp_ctx_t* ctx, ct_client_t *client) {
    assert(client->base.proto == PROTO_RELAY);
    assert(client->recv_buf);

    client->base.last_active = P_tick_ms(); int error = 0;
    for(;;client->recv_len = 0) {

        // 握手写阶段，禁止接收新消息（此时应该已经取消了 TCP_IO_FLAG_WANT_READ）
        // + 相应的，该阶段的 recv_buf 会被 handshake ack 复用作为 send_buf
        assert(!client->handshake || (TCP_HS_IS_HANDSHAKING(client) && !(client->io & TCP_IO_FLAG_WANT_WRITE)));

        // 如果以流模式获取 header，即通过结尾符来确定边界
        uint16_t hdr_len;
        if (ctx->hdr_ends) {

            // todo
            hdr_len = 0;
        }
        // 如果以帧模式获取 header，即固定 header 结构 size
        else {

            hdr_len = ctx->hdr_len;
            while (client->recv_len < ctx->hdr_len) {
                size_t need = ctx->hdr_len - client->recv_len;
                int r = tcp_recv((tcp_client_t*)client, client->recv_buf + client->recv_len, &need, NULL);
                if (r > 0) return;
                if (r < 0) {
                    error = r < -1 ? CUSTOM_TCP_ERR_IO : CUSTOM_TCP_ERR_DISCONNECTED;
                    if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                    goto error;
                }
                client->recv_len += (uint16_t)need;
            }
        }

        // 解析 payload size
        uint32_t payload_size = ctx->resolve_payload_len(client->recv_buf, hdr_len);
        if (payload_size > ctx->max_payload_len) {
            error = CUSTOM_TCP_ERR_OVERFLOW;
            if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
            goto error;
        }

        // 读取完整 payload
        size_t size = hdr_len + payload_size;
        while (client->recv_len < size) {
            size_t n = size - client->recv_len;
            int r = tcp_recv((tcp_client_t*)client, client->recv_buf + client->recv_len, &n, NULL);
            if (r > 0) return;
            if (r < 0) {
                error = r < -1 ? CUSTOM_TCP_ERR_IO : CUSTOM_TCP_ERR_DISCONNECTED;
                if (TCP_HS_IS_HANDSHAKING(client)) goto handshake;
                goto error;
            }
            client->recv_len += (uint16_t)n;
        }
        uint8_t *payload = client->recv_buf + hdr_len;

        if (TCP_HS_IS_HANDSHAKING(client)) {

            error = ctx->handle_handshake(client, client->recv_buf, hdr_len, payload, payload_size);

        handshake:  // 握手阶段可以直接接入，而无需再次等待 writable 周期判定

            assert(!client_identified(&client->base) && !client->base.sessions);

            // 握手阶段的 send_buf 应该直接复用 recv_buf
            assert(!client->send_buff_head);
            assert(!client->send_buff_rear);
            assert(!client->send_sess_head);
            assert(!client->send_sess_rear);
            assert(!client->sending_sess);
            assert(!client->sending_offset);

            if (error) {
                client->last_error = error;
                ctx->error_item(client, BUF2ITEM(client->recv_buf));
            }

            size_t sz = BUF2ITEM(client->recv_buf)->len;
            int r = tcp_send((tcp_client_t*)client, client->recv_buf, &sz, "");
            if (r < 0) {
                ct_free_client(ctx, client);    // 握手阶段发送失败，直接释放 client
                return;
            }

            // 如果 would block，则标记进入握手写入阶段
            if (r > 0) {
                client->recv_len = sz;

                // 进入握手写阶段，同时暂停接收数据，等握手（ACK 发送）完成后再继续
                client->io &= ~TCP_IO_FLAG_WANT_READ;
                client->io |= TCP_IO_FLAG_WANT_WRITE;
                return;
            }

            // 握手阶段存在失败，直接释放 client
            if (error) {
                ct_free_client(ctx, client);
                return;
            }

            client->recv_len = 0;

            // 握手（应答发送）完成（注意，这里完成后，可以继续执行 recv 处理）
            client->handshake = 0;
            print("V:", LA_F("%s sent to '%s'\n", LA_F179, 179), "", client->base.local_peer_id);
        }
        else {

            BUF2ITEM(client->recv_buf)->len = (uint16_t)size;
            ctx->handle_proto(client, client->recv_buf, hdr_len, payload, payload_size);
        }

    }   // for(;;client->recv_len = 0)

// 网络 I/O 等非致命错误，也就是不破坏（之前/已发生的）数据完整性的错误
// + 此时会清除 recv_buf 中的数据、关闭读取，同时（最后）发送一个错误状态码，并等发送完成后会自动关闭连接
error:

    // 清除已读取的部分
    client->recv_len = 0;

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

    // 报错处理（这会停止接收新的请求数据）
    ct_client_error(ctx, client, error, false);

    // 重新计时，等待客户端（重置）处理，并通过超时机制来释放 client
    client->base.last_active = P_tick_ms();
}

// 处理 RELAY 模式信令发送（TCP 长连接）- 统一队列发送
void 
ct_handle_send(custom_tcp_ctx_t* ctx, ct_client_t *client) {

    // 如果当前处于握手阶段
    // + 此时会复用 recv_buf 作为 send_buf，recv_len 作为已发送长度
    if (TCP_HS_IS_HANDSHAKING(client)) {

        size_t sz = BUF2ITEM(client->recv_buf)->len, len = sz - client->recv_len;
        int r = tcp_send((tcp_client_t*)client, client->recv_buf + client->recv_len, &len, "");
        if (r < 0) {
            ct_free_client(ctx, client);    // 握手阶段发送失败，直接释放 client
            return;
        }

        if (!len || (client->recv_len += len) < sz) return;

        // 握手（应答发送）完成
        client->handshake = 0;
        print("V:", LA_F("%s sent to '%s'\n", LA_F179, 179), "", client->base.local_peer_id);

        // 如果（发送的是）握手阶段的错误应答，直接释放 client
        if (client->last_error) {
            ct_free_client(ctx, client);
            return;
        }

        // 启动正常读写（握手完成）
        client->io |= TCP_IO_FLAG_WANT_READ;
        client->io &= ~TCP_IO_FLAG_WANT_WRITE;      // 初始时还没有要写入的数据
    }

    for (;;) {
        ct_session_t *sending_session = client->send_sess_head; buffer_item_t *item = client->send_buff_head;

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
        if (client->sending_sess && client->sending_offset) { assert(sending_session);
            sending_session = client->sending_sess;
            item = sending_session->send_head;
        }
        // 否则优先发送 client 级别的包；没有 client 包时，从游标 session 发送
        else if (!item) { assert(sending_session);
            if (!client->sending_sess) client->sending_sess = sending_session;
            else sending_session = client->sending_sess;
            item = sending_session->send_head;
        }

        //const p2p_custom_hdr_t *hdr = (const p2p_custom_hdr_t *)ITEM2BUF(item);
        //const uint16_t len = (uint16_t)(sizeof(p2p_custom_hdr_t) + ntohs(hdr->size));
        const uint16_t sz = BUF2ITEM(item)->len;
        size_t len = sz - client->sending_offset;
        int rc = tcp_send((tcp_client_t*)client, ITEM2BUF(item) + client->sending_offset, &len, "");
        if (rc < 0) {

            // 如果当前发生了 fatal 错误，则发送失败后直接销毁 client
            if (item == ctx->fatal_item || item->next == ctx->fatal_item) {
                ct_free_client(ctx, client);
                return;
            }

            // 清除已发送的部分
            client->sending_offset = 0;
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
        if ((client->sending_offset += (int)len) < sz) return;
        client->sending_offset = 0;

        // 当前 item 发送完成，推进发送队列

        // 如果是 client 级的 item
        if (item==client->send_buff_head) {

            // 发送 client sending 队列的下一项
            if (!((client->send_buff_head = item->next)))
                client->send_buff_rear = NULL;

            // 如果发送的是 fatal 错误包，发送完成后直接销毁 client
            if (item == ctx->fatal_item) {
                ct_free_client(ctx, client);
                return;
            }

            // 如果静态内嵌缓冲，不执行 free 操作，仅标记为 NULL（未在发送队列中，即发送完成）；否则正常释放
            if (item->refer == ITEM_REF_STATIC) item->refer = NULL;
            else { free_buffer(item);

                // 如果发送的是错误包，发送完成后直接关闭连接并停止写入（等待客户端重连或超时回收）
                if (item->refer == ITEM_REF_CLIENT_ERROR) { assert(client->last_error && !(client->io & TCP_IO_FLAG_WANT_READ));
                    P_sock_close(client->base.fd);
                    client->base.fd = P_INVALID_SOCKET;
                    client->io &= ~TCP_IO_FLAG_WANT_WRITE;
                    return;
                }
            }
        }
        // 对于 session 级的 item
        else { assert(item==sending_session->send_head && item != ctx->fatal_item);

            // 如果发送的是对端发过来的数据
            if (item->refer) { assert(item->refer != ITEM_REF_ACK_PENDING && item->refer != ITEM_REF_CLIENT_ERROR);
                ctx->handle_peer_sent((ct_session_t*)item->refer, item);
            }

            // 将当前 session 的 sending 队列切换到下一项，如果发送队列不为空
            if ((sending_session->send_head = item->next)) {

                // 轮询下一个（session sending 队列不为空的）session
                client->sending_sess = sending_session->send_next ? sending_session->send_next : client->send_sess_head;
            }
            // 如果当前 session 发送队列已空，从 client 中移除该 session
            // + 同时切换到下一个（session sending 队列不为空的）session
            else { sending_session->send_rear = NULL;

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

            // 如果 item 没有被（custom_peer_sent）标记为待 ACK 状态，则直接释放
            if (item->refer != ITEM_REF_ACK_PENDING) free_buffer(item);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
