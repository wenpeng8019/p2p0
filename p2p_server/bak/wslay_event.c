#include "wslay_event.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "wslay_queue.h"
#include "wslay_frame.h"

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

static const uint8_t utf8d[] = {
    /*
     * The first part of the table maps bytes to character classes that
     * to reduce the size of the transition table and create bitmasks.
     */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3, 11, 6, 6, 6, 5, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,

    /*
     * The second part is a transition table that maps a combination
     * of a state of the automaton and a character class to a state.
     */
    0, 12, 24, 36, 60, 96, 84, 12, 12, 12, 48, 72, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 0, 12, 12, 12, 12, 12, 0, 12, 0, 12, 12, 12, 24, 12, 12, 12, 12, 12, 24, 12, 24, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 24, 12, 12, 12, 12, 12, 24, 12, 12, 12, 12, 12, 12, 12, 24, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 36, 12, 36, 12, 12, 12, 36, 12, 12, 12, 12, 12, 36, 12, 36, 12, 12,
    12, 36, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
};

static uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
    uint32_t type = utf8d[byte];

    *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6) : (0xff >> type) & (byte);

    *state = utf8d[256 + *state + type];
    return *state;
}

/* End of utf8 dfa */

static ssize_t wslay_event_frame_recv_callback(uint8_t* buf, size_t len, int flags, void* user_data) {
    struct wslay_event_frame_user_data* e = (struct wslay_event_frame_user_data*)user_data;
    return e->ctx->callbacks.recv_callback(e->ctx, buf, len, flags, e->user_data);
}

static ssize_t wslay_event_frame_send_callback(const uint8_t* data, size_t len, int flags, void* user_data) {
    struct wslay_event_frame_user_data* e = (struct wslay_event_frame_user_data*)user_data;
    return e->ctx->callbacks.send_callback(e->ctx, data, len, flags, e->user_data);
}

static int wslay_event_frame_genmask_callback(uint8_t* buf, size_t len, void* user_data) {
    struct wslay_event_frame_user_data* e = (struct wslay_event_frame_user_data*)user_data;
    return e->ctx->callbacks.genmask_callback(e->ctx, buf, len, e->user_data);
}

static void wslay_event_imsg_reset(struct wslay_event_imsg* m) {

    m->opcode = 0xffu;
    m->utf8state = UTF8_ACCEPT;

    if (m->chunks) {
        while (!wslay_queue_empty(m->chunks)) {
            struct wslay_event_byte_chunk* c = wslay_queue_top(m->chunks); wslay_queue_pop(m->chunks);
            free(c->data); free(c);
        }
    }
}

static int wslay_event_imsg_append_chunk(struct wslay_event_imsg* m, size_t len) {
    if (len == 0) {
        return 0;
    }
    int r;

    struct wslay_event_byte_chunk* chunk = (struct wslay_event_byte_chunk*)malloc(sizeof(struct wslay_event_byte_chunk));
    if (chunk == NULL) {
        return WSLAY_ERR_NOMEM;
    }
    memset(chunk, 0, sizeof(struct wslay_event_byte_chunk));
    if (len) {
        (chunk)->data = (uint8_t*)malloc(len);
        if ((chunk)->data == NULL) {
            free(chunk);
            return WSLAY_ERR_NOMEM;
        }
        (chunk)->data_length = len;
    }

    if ((r = wslay_queue_push(m->chunks, chunk)) != 0) {
        return r;
    }
    m->msg_length += len;
    return 0;
}

static uint8_t* wslay_event_flatten_queue(struct wslay_queue* queue, size_t len) {
    if (len == 0) {
        return NULL;
    }

    size_t off = 0;
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
        return NULL;
    }

    while (!wslay_queue_empty(queue)) {
        struct wslay_event_byte_chunk* chunk = wslay_queue_top(queue); wslay_queue_pop(queue);
        memcpy(buf+off, chunk->data, chunk->data_length);
        off += chunk->data_length;
        free(chunk->data); free(chunk);
        assert(off <= len);
    }
    assert(len == off);
    return buf;
}

static void omsg_free(struct wslay_event_omsg* m) {
    if (!m) return;
    free(m->data);
    free(m);
}

static int wslay_event_verify_rsv_bits(wslay_event_context_ptr ctx, uint8_t rsv) {
    return ((rsv & ~ctx->allowed_rsv_bits) == 0);
}

int wslay_event_queue_msg_ex(wslay_event_context_ptr ctx, const struct wslay_event_msg* arg,
                             uint8_t rsv/* 默认 = WSLAY_RSV_NONE */) {

    int r;
    if (ctx->write_enabled && (ctx->close_status & WSLAY_CLOSE_QUEUED) != 0) {
        return WSLAY_ERR_NO_MORE_MSG;
    }
    /* RSV1 is not allowed for control frames */
    if ((wslay_is_ctrl_frame(arg->opcode) && (arg->msg_length > 125 || wslay_get_rsv1(rsv))) ||
        !wslay_event_verify_rsv_bits(ctx, rsv)) {
        return WSLAY_ERR_INVALID_ARGUMENT;
    }

    struct wslay_event_omsg* omsg = (struct wslay_event_omsg*)malloc(sizeof(struct wslay_event_omsg));
    if (!omsg) {
        return WSLAY_ERR_NOMEM;
    }
    memset(omsg, 0, sizeof(struct wslay_event_omsg));
    (omsg)->fin = 1;
    (omsg)->opcode = arg->opcode;
    (omsg)->rsv = rsv;
    (omsg)->type = WSLAY_NON_FRAGMENTED;
    if (arg->msg_length) {
        (omsg)->data = (uint8_t*)malloc(arg->msg_length);
        if (!(omsg)->data) {
            free(omsg);
            return WSLAY_ERR_NOMEM;
        }
        memcpy((omsg)->data, arg->msg, arg->msg_length);
        (omsg)->data_length = arg->msg_length;
    }

    if (wslay_is_ctrl_frame(arg->opcode)) {
        if ((r = wslay_queue_push(ctx->send_ctrl_queue, omsg)) != 0) {
            return r;
        }
    }
    else {
        if ((r = wslay_queue_push(ctx->send_queue, omsg)) != 0) {
            return r;
        }
    }
    ++ctx->queued_msg_count;
    ctx->queued_msg_length += arg->msg_length;
    return 0;
}

int wslay_event_queue_fragmented_msg_ex(wslay_event_context_ptr ctx, const struct wslay_event_fragmented_msg* arg,
                                        uint8_t rsv/* 默认 = WSLAY_RSV_NONE */) {
    int r;
    if (ctx->write_enabled && (ctx->close_status & WSLAY_CLOSE_QUEUED) != 0) {
        return WSLAY_ERR_NO_MORE_MSG;
    }
    if (wslay_is_ctrl_frame(arg->opcode) || !wslay_event_verify_rsv_bits(ctx, rsv)) {
        return WSLAY_ERR_INVALID_ARGUMENT;
    }

    struct wslay_event_omsg* omsg = (struct wslay_event_omsg*)malloc(sizeof(struct wslay_event_omsg));
    if (!omsg) {
        return WSLAY_ERR_NOMEM;
    }
    memset(omsg, 0, sizeof(struct wslay_event_omsg));
    (omsg)->opcode = arg->opcode;
    (omsg)->rsv = rsv;
    (omsg)->type = WSLAY_FRAGMENTED;
    (omsg)->source = arg->source;
    (omsg)->read_callback = arg->read_callback;


    if ((r = wslay_queue_push(ctx->send_queue, omsg)) != 0) {
        return r;
    }
    ++ctx->queued_msg_count;
    return 0;
}

static int wslay_event_context_init(wslay_event_context_ptr* ctx, bool server,
                                    const struct wslay_event_callbacks* callbacks, void* user_data) {

    int i, r;
    struct wslay_frame_callbacks frame_callbacks = {
        wslay_event_frame_send_callback,
        wslay_event_frame_recv_callback,
        wslay_event_frame_genmask_callback
    };
    *ctx = (wslay_event_context_ptr)malloc(sizeof(struct wslay_event_context));
    if (!*ctx) {
        return WSLAY_ERR_NOMEM;
    }
    memset(*ctx, 0, sizeof(struct wslay_event_context));
    (*ctx)->callbacks = *callbacks;

    (*ctx)->user_data = user_data;
    (*ctx)->frame_user_data.ctx = *ctx;
    (*ctx)->frame_user_data.user_data = user_data;
    if ((r = wslay_frame_context_init(&(*ctx)->frame_ctx, &frame_callbacks,
                                      &(*ctx)->frame_user_data)) != 0) {
        wslay_event_context_free(*ctx);
        return r;
    }
    (*ctx)->read_enabled = (*ctx)->write_enabled = 1;
    (*ctx)->send_queue = wslay_queue_new();
    if (!(*ctx)->send_queue) {
        wslay_event_context_free(*ctx);
        return WSLAY_ERR_NOMEM;
    }
    (*ctx)->send_ctrl_queue = wslay_queue_new();
    if (!(*ctx)->send_ctrl_queue) {
        wslay_event_context_free(*ctx);
        return WSLAY_ERR_NOMEM;
    }
    (*ctx)->queued_msg_count = 0;
    (*ctx)->queued_msg_length = 0;
    for (i = 0; i < 2; ++i) {
        wslay_event_imsg_reset(&(*ctx)->imsgs[i]);
        (*ctx)->imsgs[i].chunks = wslay_queue_new();
        if (!(*ctx)->imsgs[i].chunks) {
            wslay_event_context_free(*ctx);
            return WSLAY_ERR_NOMEM;
        }
    }
    (*ctx)->imsg = &(*ctx)->imsgs[0];
    (*ctx)->obufmark = (*ctx)->obuflimit = (*ctx)->obuf;
    (*ctx)->status_code_sent = WSLAY_CODE_ABNORMAL_CLOSURE;
    (*ctx)->status_code_recv = WSLAY_CODE_ABNORMAL_CLOSURE;
    (*ctx)->max_recv_msg_length = (1u << 31) - 1;

    (*ctx)->server = server;
    return 0;
}

void wslay_event_context_free(wslay_event_context_ptr ctx) {

    int i;
    if (!ctx) {
        return;
    }
    for (i = 0; i < 2; ++i) {

        if (ctx->imsgs[i].chunks) {
            while (!wslay_queue_empty(ctx->imsgs[i].chunks)) {
                struct wslay_event_byte_chunk* c = wslay_queue_top(ctx->imsgs[i].chunks);
                wslay_queue_pop(ctx->imsgs[i].chunks);
                free(c->data); free(c);
            }
        }
        wslay_queue_free(ctx->imsgs[i].chunks);
    }
    if (ctx->send_queue) {
        while (!wslay_queue_empty(ctx->send_queue)) {
            omsg_free(wslay_queue_top(ctx->send_queue));
            wslay_queue_pop(ctx->send_queue);
        }
        wslay_queue_free(ctx->send_queue);
    }
    if (ctx->send_ctrl_queue) {
        while (!wslay_queue_empty(ctx->send_ctrl_queue)) {
            omsg_free(wslay_queue_top(ctx->send_ctrl_queue));
            wslay_queue_pop(ctx->send_ctrl_queue);
        }
        wslay_queue_free(ctx->send_ctrl_queue);
    }
    wslay_frame_context_free(ctx->frame_ctx);
    omsg_free(ctx->omsg);
    free(ctx);
}

static int wslay_event_queue_close_wrapper(wslay_event_context_ptr ctx, uint16_t status_code,
                                            const uint8_t* reason, size_t reason_length) {

    if (reason_length > 123) {
        return WSLAY_ERR_INVALID_ARGUMENT;
    }

    ctx->read_enabled = 0;

    if (ctx->write_enabled && (ctx->close_status & WSLAY_CLOSE_QUEUED) != 0) {
        return 0;
    }

    uint16_t ncode; uint8_t msg[128]; size_t msg_length;
    if (status_code == 0) msg_length = 0;
    else { ncode = htons(status_code);
        memcpy(msg, &ncode, 2);
        if (reason_length) {
            memcpy(msg+2, reason, reason_length);
        }
        msg_length = reason_length + 2;
    }
    struct wslay_event_msg arg;
    arg.opcode = WSLAY_CONNECTION_CLOSE;
    arg.msg = msg;
    arg.msg_length = msg_length;
    int r = wslay_event_queue_msg(ctx, &arg);
    if (r == 0) {
        ctx->close_status |= WSLAY_CLOSE_QUEUED;
    }
    return r;
}

static int wslay_event_is_valid_status_code(uint16_t status_code) {
    return (1000 <= status_code && status_code <= 1011 && status_code != 1004 && status_code != 1005 && status_code != 1006) ||
           (3000 <= status_code && status_code <= 4999);
}

int wslay_event_recv(wslay_event_context_ptr ctx) {

    struct wslay_frame_iocb iocb; ssize_t r;

    while (ctx->read_enabled) {
        memset(&iocb, 0, sizeof(iocb));

        r = wslay_frame_recv(ctx->frame_ctx, &iocb);
        if (r < 0) {

            if (r != WSLAY_ERR_WANT_READ || (ctx->error != WSLAY_ERR_WOULDBLOCK && ctx->error != 0)) {
                if ((r = wslay_event_queue_close_wrapper(ctx, 0, NULL, 0)) != 0) return r;
                return WSLAY_ERR_CALLBACK_FAILURE;
            }

            break;
        }

        int new_frame = 0;

        // 如果设置的 RSV bits 无效；或者（有效的）RSV1 bit 被设置到了 ctrl frame 或 continuation frame 上；又或者 server/client 关系不一致
        // + 协议报错
        if (!wslay_event_verify_rsv_bits(ctx, iocb.rsv) ||
            (wslay_get_rsv1(iocb.rsv) && (wslay_is_ctrl_frame(iocb.opcode) || iocb.opcode == WSLAY_CONTINUATION_FRAME)) ||
            (ctx->server && !iocb.mask) || (!ctx->server && iocb.mask)) {

            if ((r = wslay_event_queue_close_wrapper(ctx, WSLAY_CODE_PROTOCOL_ERROR, NULL, 0)) != 0) return r;
            break;
        }

        // 如果当前未处于分段连续帧之间（首次接收一个消息，0xffu 是 reset 后的初始值）
        if (ctx->imsg->opcode == 0xffu) {

            // 此时允许的新帧类型
            if (iocb.opcode == WSLAY_TEXT_FRAME ||
                iocb.opcode == WSLAY_BINARY_FRAME ||
                iocb.opcode == WSLAY_CONNECTION_CLOSE ||
                iocb.opcode == WSLAY_PING ||
                iocb.opcode == WSLAY_PONG) {

                ctx->imsg->opcode = iocb.opcode;
                ctx->imsg->fin = iocb.fin;
                ctx->imsg->rsv = iocb.rsv;
                ctx->imsg->msg_length = 0;
                new_frame = 1;
            }
            else {
                if ((r = wslay_event_queue_close_wrapper(ctx, WSLAY_CODE_PROTOCOL_ERROR, NULL, 0)) != 0) return r;
                break;
            }
        }
        // 如果当前处于分段连续帧之间，且当前帧数据已经完整接收完毕（ipayloadlen/ipayloadoff 置 0, 但未执行 reset，也就是还没有收到 fin 包）
        // + 注意 ws 允许在 fragment series 中途插入 ctrl frame；
        //   因此这里允许的新帧除了 continuation 帧之外，还可以是 ctrl frame
        else if (ctx->ipayloadlen == 0 && ctx->ipayloadoff == 0) {

            if (iocb.opcode == WSLAY_CONTINUATION_FRAME) {
                ctx->imsg->fin = iocb.fin;
            }
            // 如果是插播的 ctrl 帧，则临时切换到 imsgs[1] 缓冲
            else if (iocb.opcode == WSLAY_CONNECTION_CLOSE || iocb.opcode == WSLAY_PING || iocb.opcode == WSLAY_PONG) {
                ctx->imsg = &ctx->imsgs[1];
                ctx->imsg->opcode = iocb.opcode;
                ctx->imsg->fin = iocb.fin;
                ctx->imsg->rsv = iocb.rsv;
                ctx->imsg->msg_length = 0;
            }
            else {
                if ((r = wslay_event_queue_close_wrapper (ctx, WSLAY_CODE_PROTOCOL_ERROR, NULL, 0)) != 0) return r;
                break;
            }

            new_frame = 1;
        }

        // 对于一个新的数据帧
        if (new_frame) {

            // 避免数据过大溢出
            if (ctx->imsg->msg_length + iocb.payload_length > ctx->max_recv_msg_length) {
                if ((r = wslay_event_queue_close_wrapper (ctx, WSLAY_CODE_MESSAGE_TOO_BIG, NULL, 0)) != 0) return r;
                break;
            }
            ctx->ipayloadlen = iocb.payload_length;

            if (ctx->callbacks.on_frame_recv_start_callback) {
                struct wslay_event_on_frame_recv_start_arg arg;
                arg.fin = iocb.fin;
                arg.rsv = iocb.rsv;
                arg.opcode = iocb.opcode;
                arg.payload_length = iocb.payload_length;
                ctx->callbacks.on_frame_recv_start_callback(ctx, &arg, ctx->user_data);
            }

            // 如果需要缓存数据，分配帧数据缓存
            if (!(ctx->config & WSLAY_CONFIG_NO_BUFFERING) || wslay_is_ctrl_frame(iocb.opcode)) {
                if ((r = wslay_event_imsg_append_chunk(ctx->imsg, iocb.payload_length)) != 0) {
                    ctx->read_enabled = 0;
                    return r;
                }
            }
        }

        /* If RSV1 bit is set then it is too early for utf-8 validation */
        //
        if ((ctx->imsg->opcode == WSLAY_TEXT_FRAME && !wslay_get_rsv1(ctx->imsg->rsv)) ||
            ctx->imsg->opcode == WSLAY_CONNECTION_CLOSE) {

            size_t i = 0;
            if (ctx->imsg->opcode == WSLAY_CONNECTION_CLOSE && ctx->ipayloadoff < 2) {
                i = 2 - (size_t)ctx->ipayloadoff;
            }
            for (; i < iocb.data_length; ++i) { uint32_t codep;
                if (decode(&ctx->imsg->utf8state, &codep, iocb.data[i]) == UTF8_REJECT) {
                    if ((r = wslay_event_queue_close_wrapper(ctx, WSLAY_CODE_INVALID_FRAME_PAYLOAD_DATA, NULL, 0)) != 0) return r;
                    break;
                }
            }
        }
        if (ctx->imsg->utf8state == UTF8_REJECT) break;

        if (ctx->callbacks.on_frame_recv_chunk_callback) {
            struct wslay_event_on_frame_recv_chunk_arg arg;
            arg.data = iocb.data;
            arg.data_length = iocb.data_length;
            ctx->callbacks.on_frame_recv_chunk_callback(ctx, &arg, ctx->user_data);
        }

        // 将当前读取到的(部分)帧数据，保存到帧的缓存中
        if (iocb.data_length > 0) {
            if (!(ctx->config & WSLAY_CONFIG_NO_BUFFERING) || wslay_is_ctrl_frame(iocb.opcode)) {
                struct wslay_event_byte_chunk* chunk = wslay_queue_tail(ctx->imsg->chunks);
                memcpy(chunk->data+ctx->ipayloadoff, iocb.data, iocb.data_length);
            }
            ctx->ipayloadoff += iocb.data_length;
        }

        // 如果当前帧数据已经完整接收完毕
        if (ctx->ipayloadoff == ctx->ipayloadlen) {

            if (ctx->imsg->fin && (ctx->imsg->opcode == WSLAY_TEXT_FRAME || ctx->imsg->opcode == WSLAY_CONNECTION_CLOSE) &&
                ctx->imsg->utf8state != UTF8_ACCEPT/* UTF8_ACCEPT 是 reset 后的初始值 */) {

                if ((r = wslay_event_queue_close_wrapper(ctx, WSLAY_CODE_INVALID_FRAME_PAYLOAD_DATA, NULL, 0)) != 0) return r;
                break;
            }

            if (ctx->callbacks.on_frame_recv_end_callback) {
                ctx->callbacks.on_frame_recv_end_callback(ctx, ctx->user_data);
            }

            // 如果接收完一个完整的帧序列
            if (ctx->imsg->fin) {

                if (ctx->callbacks.on_msg_recv_callback ||
                    ctx->imsg->opcode == WSLAY_CONNECTION_CLOSE ||
                    ctx->imsg->opcode == WSLAY_PING) {

                    struct wslay_event_on_msg_recv_arg arg;
                    uint16_t status_code = 0;
                    uint8_t* msg = NULL;
                    size_t msg_length = 0;

                    // 如果开启了缓存模式，又或者是 ctrl msg
                    // + 注意，目前的程序逻辑设置 WSLAY_CONFIG_NO_BUFFERING 会存在 bug，即 free(NULL)
                    if (!(ctx->config & WSLAY_CONFIG_NO_BUFFERING) || wslay_is_ctrl_frame(iocb.opcode)) {

                        // 拼成一个连续的数据
                        msg = wslay_event_flatten_queue(ctx->imsg->chunks, ctx->imsg->msg_length);
                        if (ctx->imsg->msg_length && !msg) {
                            ctx->read_enabled = 0;
                            return WSLAY_ERR_NOMEM;
                        }
                        msg_length = ctx->imsg->msg_length;
                    }

                    // 对于 close 帧的处理
                    if (ctx->imsg->opcode == WSLAY_CONNECTION_CLOSE) {

                        const uint8_t* reason; size_t reason_length;
                        if (ctx->imsg->msg_length >= 2) {

                            memcpy(&status_code, msg, 2);
                            status_code = ntohs(status_code);
                            if (!wslay_event_is_valid_status_code(status_code)) {
                                free(msg);
                                if ((r = wslay_event_queue_close_wrapper(ctx, WSLAY_CODE_PROTOCOL_ERROR, NULL, 0)) != 0) return r;
                                break;
                            }
                            reason = msg + 2;
                            reason_length = ctx->imsg->msg_length - 2;
                        }
                        else {
                            reason = NULL;
                            reason_length = 0;
                        }

                        ctx->close_status |= WSLAY_CLOSE_RECEIVED;
                        ctx->status_code_recv = status_code == 0 ? WSLAY_CODE_NO_STATUS_RCVD : status_code;

                        // 返回应答 close 帧，并正常停止接收数据
                        if ((r = wslay_event_queue_close_wrapper(ctx, status_code, reason, reason_length)) != 0) {
                            free(msg);
                            return r;
                        }
                    }
                    // 对于 ping 帧的处理
                    else if (ctx->imsg->opcode == WSLAY_PING) {

                        // 自动返回 PONG 帧
                        struct wslay_event_msg pong_arg;
                        pong_arg.opcode = WSLAY_PONG;
                        pong_arg.msg = msg;
                        pong_arg.msg_length = ctx->imsg->msg_length;
                        if ((r = wslay_event_queue_msg(ctx, &pong_arg)) && r != WSLAY_ERR_NO_MORE_MSG) {
                            ctx->read_enabled = 0;
                            free(msg);
                            return r;
                        }
                    }

                    // 触发 frame 回调
                    if (ctx->callbacks.on_msg_recv_callback) {
                        arg.opcode = ctx->imsg->opcode;
                        arg.rsv = ctx->imsg->rsv;
                        arg.msg = msg;
                        arg.msg_length = msg_length;
                        arg.status_code = status_code;
                        ctx->error = 0;
                        ctx->callbacks.on_msg_recv_callback(ctx, &arg, ctx->user_data);
                    }
                    free(msg);
                }

                // 重置帧序列
                wslay_event_imsg_reset(ctx->imsg);
                if (ctx->imsg == &ctx->imsgs[1])
                    ctx->imsg = &ctx->imsgs[0];
            }

            // 清空(重置)当前帧数据
            ctx->ipayloadlen = ctx->ipayloadoff = 0;
        }
    }
    return 0;
}

static struct wslay_event_omsg* wslay_event_send_ctrl_queue_pop(wslay_event_context_ptr ctx) {

    // 如果 closing
    if (ctx->close_status & WSLAY_CLOSE_QUEUED) {

        // 删除 close 以外的 ctrl msg
        while (!wslay_queue_empty(ctx->send_ctrl_queue)) {
            struct wslay_event_omsg* msg = wslay_queue_top(ctx->send_ctrl_queue); wslay_queue_pop(ctx->send_ctrl_queue);
            if (msg->opcode == WSLAY_CONNECTION_CLOSE) {
                return msg;
            }
            omsg_free(msg);
        }
        return NULL;
    }

    struct wslay_event_omsg* msg = wslay_queue_top(ctx->send_ctrl_queue); wslay_queue_pop(ctx->send_ctrl_queue);
    return msg;
}

int wslay_event_send(wslay_event_context_ptr ctx) {
    struct wslay_frame_iocb iocb;
    ssize_t r;

    while (ctx->write_enabled
           && (!wslay_queue_empty(ctx->send_queue) || !wslay_queue_empty(ctx->send_ctrl_queue) || ctx->omsg)) {

        // 如果没有正在发送的
        if (!ctx->omsg) {

            // 从队列获取一项，优先获取 ctrl msg
            if (wslay_queue_empty(ctx->send_ctrl_queue)) {
                ctx->omsg = wslay_queue_top(ctx->send_queue); wslay_queue_pop(ctx->send_queue);
            } else { ctx->omsg = wslay_event_send_ctrl_queue_pop(ctx);
                if (ctx->omsg == NULL) break;
            }

            // 如果不是分段数据，增加 fin 截止标识
            if (ctx->omsg->type == WSLAY_NON_FRAGMENTED) {
                ctx->omsg->fin = 1;
                ctx->opayloadlen = ctx->omsg->data_length;
                ctx->opayloadoff = 0;
            }
        }
        // 如果待发但还没发的数据不是 ctrl msg，且存在新的 ctrl msg，则优先发送 ctrl msg
        else if (!wslay_is_ctrl_frame(ctx->omsg->opcode) && ctx->frame_ctx->ostate == PREP_HEADER && !wslay_queue_empty(ctx->send_ctrl_queue)) {

            // 将待发数据重新入队
            if ((r = wslay_queue_push_front(ctx->send_queue, ctx->omsg)) != 0) {
                ctx->write_enabled = 0;
                return r;
            }

            ctx->omsg = wslay_event_send_ctrl_queue_pop(ctx);
            if (ctx->omsg == NULL) break;

            // ctrl msg 肯定是 WSLAY_NON_FRAGMENTED
            ctx->omsg->fin = 1;
            ctx->opayloadlen = ctx->omsg->data_length;
            ctx->opayloadoff = 0;
        }

        // 对于非分段数据
        if (ctx->omsg->type == WSLAY_NON_FRAGMENTED) {

            memset(&iocb, 0, sizeof(iocb));
            iocb.opcode = ctx->omsg->opcode;
            iocb.fin = 1;
            iocb.rsv = ctx->omsg->rsv;
            iocb.mask = ctx->server ^ 1;                    // 客户端上行必须掩码，服务器下行无需掩码
            iocb.payload_length = ctx->opayloadlen;
            iocb.data = ctx->omsg->data + ctx->opayloadoff;
            iocb.data_length = ctx->opayloadlen - ctx->opayloadoff;
            r = wslay_frame_send(ctx->frame_ctx, &iocb);
            if (r < 0) {
                if (r != WSLAY_ERR_WANT_WRITE || (ctx->error != WSLAY_ERR_WOULDBLOCK && ctx->error != 0)) {
                    ctx->write_enabled = 0;
                    return WSLAY_ERR_CALLBACK_FAILURE;
                }
                break;  // would block
            }

            ctx->opayloadoff += r;
            if (ctx->opayloadoff == ctx->opayloadlen) {

                ctx->queued_msg_length -= ctx->omsg->data_length;
                --ctx->queued_msg_count;

                // 如果发送完成的是 close 帧
                if (ctx->omsg->opcode == WSLAY_CONNECTION_CLOSE) {

                    ctx->write_enabled = 0;
                    ctx->close_status |= WSLAY_CLOSE_SENT;

                    uint16_t status_code = 0;
                    if (ctx->omsg->data_length >= 2) {
                        memcpy(&status_code, ctx->omsg->data, 2);
                        status_code = ntohs(status_code);
                    }
                    ctx->status_code_sent = status_code == 0 ? WSLAY_CODE_NO_STATUS_RCVD : status_code;
                }

                omsg_free(ctx->omsg);
                ctx->omsg = NULL;
            }
        }
        // 对于分段数据
        else {

            // 如果没有数据要发
            if (ctx->omsg->fin == 0 && ctx->obuflimit == ctx->obufmark) {

                // 获取动态发送的数据（如中转的数据流、或大数据）
                int eof = 0;
                r = ctx->omsg->read_callback(ctx, ctx->obuf, sizeof(ctx->obuf), &ctx->omsg->source, &eof, ctx->user_data);
                if (r < 0) {
                    ctx->write_enabled = 0;
                    return WSLAY_ERR_CALLBACK_FAILURE;
                }

                // 如果没有任何要发送的数据，结束发送（等待下个周期继续发送该数据流）
                if (r == 0 && eof == 0) break;

                ctx->obuflimit = ctx->obuf + r;     // 设置发送数据末尾截止位置指针
                if (eof) ctx->omsg->fin = 1;
                ctx->opayloadlen = r;
                ctx->opayloadoff = 0;
            }

            memset(&iocb, 0, sizeof(iocb));
            iocb.opcode         = ctx->omsg->opcode;
            iocb.fin            = ctx->omsg->fin;
            iocb.rsv            = ctx->omsg->rsv;
            iocb.mask           = ctx->server ? 0 : 1;
            iocb.payload_length = ctx->opayloadlen;
            iocb.data           = ctx->obufmark;
            iocb.data_length    = ctx->obuflimit - ctx->obufmark;
            r = wslay_frame_send(ctx->frame_ctx, &iocb);
            if (r < 0) {
                if (r != WSLAY_ERR_WANT_WRITE || (ctx->error != WSLAY_ERR_WOULDBLOCK && ctx->error != 0)) {
                    ctx->write_enabled = 0;
                    return WSLAY_ERR_CALLBACK_FAILURE;
                }
                break;  // would block
            }

            ctx->obufmark += r;                     // 推进发送完成的数据指针
            if (ctx->obufmark == ctx->obuflimit) {  // 如果全部发送完成

                // 重置发送缓冲区
                ctx->obufmark = ctx->obuflimit = ctx->obuf;

                // 如果是最后一个包
                if (ctx->omsg->fin) {
                    --ctx->queued_msg_count;

                    omsg_free(ctx->omsg);
                    ctx->omsg = NULL;
                }
                // 标记状态为继续发送
                else {
                    ctx->omsg->opcode = WSLAY_CONTINUATION_FRAME;
                    /* RSV1 is not set on continuation frames */
                    ctx->omsg->rsv = ctx->omsg->rsv & ~WSLAY_RSV1_BIT;
                }
            }
            else {
                break;
            }
        }
    }
    return 0;
}

void wslay_event_set_error(wslay_event_context_ptr ctx, int val) {
    ctx->error = val;
}

int wslay_event_want_read(wslay_event_context_ptr ctx) {
    return ctx->read_enabled;
}

int wslay_event_want_write(wslay_event_context_ptr ctx) {
    return ctx->write_enabled &&
    (!wslay_queue_empty(ctx->send_queue) ||
        !wslay_queue_empty(ctx->send_ctrl_queue) || ctx->omsg);
}

int wslay_event_get_close_received(wslay_event_context_ptr ctx) {
    return (ctx->close_status & WSLAY_CLOSE_RECEIVED) > 0;
}

int wslay_event_get_close_sent(wslay_event_context_ptr ctx) {
    return (ctx->close_status & WSLAY_CLOSE_SENT) > 0;
}

void wslay_event_config_set_allowed_rsv_bits(wslay_event_context_ptr ctx, uint8_t rsv) {
    /* We currently only allow WSLAY_RSV1_BIT or WSLAY_RSV_NONE */
    ctx->allowed_rsv_bits = rsv & WSLAY_RSV1_BIT;
}

void wslay_event_config_set_no_buffering(wslay_event_context_ptr ctx, int val) {
    if (val) {
        ctx->config |= WSLAY_CONFIG_NO_BUFFERING;
    }
    else {
        ctx->config &= ~WSLAY_CONFIG_NO_BUFFERING;
    }
}
