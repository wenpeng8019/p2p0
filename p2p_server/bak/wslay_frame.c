
#include "wslay_frame.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "wslay_net.h"

#define wslay_min(A, B) (((A) < (B)) ? (A) : (B))

int wslay_frame_context_init(wslay_frame_context_ptr* ctx,
                             const struct wslay_frame_callbacks* callbacks,
                             void* user_data) {
    *ctx = (wslay_frame_context_ptr)malloc(sizeof(struct wslay_frame_context));
    if (*ctx == NULL) {
        return -1;
    }
    memset(*ctx, 0, sizeof(struct wslay_frame_context));
    (*ctx)->istate = RECV_HEADER1;
    (*ctx)->ireqread = 2;
    (*ctx)->ostate = PREP_HEADER;
    (*ctx)->user_data = user_data;
    (*ctx)->ibufmark = (*ctx)->ibuflimit = (*ctx)->ibuf;
    (*ctx)->callbacks = *callbacks;
    return 0;
}

void wslay_frame_context_free(wslay_frame_context_ptr ctx) {
    free(ctx);
}

ssize_t wslay_frame_send(wslay_frame_context_ptr ctx, struct wslay_frame_iocb* iocb) {

    if (iocb->data_length > iocb->payload_length) {
        return WSLAY_ERR_INVALID_ARGUMENT;
    }

    // 发送之前的准备工作：构造帧头
    if (ctx->ostate == PREP_HEADER) {

        if (wslay_is_ctrl_frame(iocb->opcode) && iocb->payload_length > 125) {
            return WSLAY_ERR_INVALID_ARGUMENT;
        }

        uint8_t* hdptr = ctx->oheader;
        memset(ctx->oheader, 0, sizeof(ctx->oheader));
        *hdptr |= (iocb->fin << 7) & 0x80u;                 // Byte[0] bit 7: FIN
        *hdptr |= (iocb->rsv << 4) & 0x70u;                 // Byte[0] bit 4-6: RSV1-3
        *hdptr |= iocb->opcode & 0xfu;                      // Byte[0] bit 0-3: opcode
        ++hdptr;

        *hdptr |= (iocb->mask << 7) & 0x80u;                // Byte[1] bit 7: MASK
        if (iocb->payload_length < 126) {                   // Byte[1] bit 0-6: payload length (if <126)
            *hdptr |= iocb->payload_length;
            ++hdptr;
        } else if (iocb->payload_length < (1 << 16)) {      // Byte[1] bit 0-6: 126, followed by 2 bytes of payload length
            *hdptr |= 126;
            ++hdptr;
            uint16_t len = htons(iocb->payload_length);
            memcpy(hdptr, &len, 2);
            hdptr += 2;
        }
        else if (iocb->payload_length < (1ull << 63)) {     // Byte[1] bit 0-6: 127, followed by 8 bytes of payload length
            *hdptr |= 127;
            ++hdptr;
            uint64_t len = hton64(iocb->payload_length);
            memcpy(hdptr, &len, 8);
            hdptr += 8;
        }
        else return WSLAY_ERR_INVALID_ARGUMENT;

        // 如果需要掩码
        if (iocb->mask) {

            // 生成随机掩码
            if (ctx->callbacks.genmask_callback(ctx->omaskkey, 4, ctx->user_data) != 0) {
                return WSLAY_ERR_INVALID_CALLBACK;
            }
            ctx->omask = 1;
            memcpy(hdptr, ctx->omaskkey, 4);
            hdptr += 4;
        }

        // 初始化要发送的 header 数据对象信息
        ctx->ostate = SEND_HEADER;
        ctx->oheadermark = ctx->oheader;
        ctx->oheaderlimit = hdptr;
        ctx->opayloadlen = iocb->payload_length;
        ctx->opayloadoff = 0;
    }

    // 发送包头
    if (ctx->ostate == SEND_HEADER) {

        ptrdiff_t len = ctx->oheaderlimit - ctx->oheadermark;

        int flags = 0;
        if (iocb->data_length > 0) flags |= WSLAY_MSG_MORE; // 给 callback tips: 还有后续数据要发，即 payload 不空

        ssize_t r = ctx->callbacks.send_callback(ctx->oheadermark, len, flags, ctx->user_data);
        if (r <= 0) return WSLAY_ERR_WANT_WRITE;
        if (r > len) return WSLAY_ERR_INVALID_CALLBACK;

        ctx->oheadermark += r;
        if (ctx->oheadermark != ctx->oheaderlimit)
            return WSLAY_ERR_WANT_WRITE;
        ctx->ostate = SEND_PAYLOAD;
    }

    // 发送负载
    if (ctx->ostate == SEND_PAYLOAD) {

        size_t totallen = 0;
        if (iocb->data_length > 0) {

            // 如果需要掩码
            if (ctx->omask) {

                // 循环分段进行掩码和发送
                uint8_t temp[4096];
                const uint8_t *datamark = iocb->data,
                              *datalimit = iocb->data + iocb->data_length;
                while (datamark < datalimit) {
                    size_t datalen = datalimit - datamark;

                    const uint8_t* writelimit = datamark + wslay_min(sizeof(temp), datalen);
                    size_t writelen = writelimit - datamark;
                    for (size_t i = 0; i < writelen; ++i) {
                        temp[i] = datamark[i] ^ ctx->omaskkey[(ctx->opayloadoff + i) % 4];
                    }

                    ssize_t r = ctx->callbacks.send_callback(temp, writelen, 0, ctx->user_data);
                    if (r <= 0) {
                        if (totallen > 0) break;
                        return WSLAY_ERR_WANT_WRITE;
                    }
                    if ((size_t)r > writelen) {
                        return WSLAY_ERR_INVALID_CALLBACK;
                    }
                    datamark += r;
                    ctx->opayloadoff += r;
                    totallen += r;
                }
            }
            else {
                ssize_t r = ctx->callbacks.send_callback(iocb->data, iocb->data_length, 0, ctx->user_data);
                if (r <= 0) return WSLAY_ERR_WANT_WRITE;
                if ((size_t)r > iocb->data_length) {
                    return WSLAY_ERR_INVALID_CALLBACK;
                }
                ctx->opayloadoff += r;
                totallen = r;
            }
        }

        // 发送完成，重新初始化标记发送下一个包
        if (ctx->opayloadoff == ctx->opayloadlen) {
            ctx->ostate = PREP_HEADER;
        }

        return totallen;
    }

    return WSLAY_ERR_INVALID_ARGUMENT;
}

#define WSLAY_AVAIL_IBUF(ctx) ((size_t)(ctx->ibuflimit - ctx->ibufmark))

static ssize_t wslay_recv(wslay_frame_context_ptr ctx) {

    if (ctx->ibufmark != ctx->ibuf) {
        ptrdiff_t len = WSLAY_AVAIL_IBUF(ctx);
        memmove(ctx->ibuf, ctx->ibufmark, len);
        ctx->ibuflimit = ctx->ibuf + len;
        ctx->ibufmark = ctx->ibuf;
    }

    ssize_t r = ctx->callbacks.recv_callback(ctx->ibuflimit, ctx->ibuf + sizeof(ctx->ibuf) - ctx->ibuflimit,
                                        0, ctx->user_data);
    if (r <= 0) return WSLAY_ERR_WANT_READ;
    ctx->ibuflimit += r;

    return r;
}

ssize_t wslay_frame_recv(wslay_frame_context_ptr ctx, struct wslay_frame_iocb* iocb) {
    ssize_t r;

    // 接收 header 的前两个字节
    if (ctx->istate == RECV_HEADER1) {
        uint8_t fin, opcode, rsv, payloadlen;

        if (WSLAY_AVAIL_IBUF(ctx) < ctx->ireqread) {        // 如果不足 input req read
            if ((r = wslay_recv(ctx)) <= 0) return r;       // 读取直至 would block | err
            if (WSLAY_AVAIL_IBUF(ctx) < ctx->ireqread)      // 如果读取后依然不足，返回
                return WSLAY_ERR_WANT_READ;
        }

        fin = (ctx->ibufmark[0] >> 7) & 1;          // Byte[0] bit8: fin
        rsv = (ctx->ibufmark[0] >> 4) & 7;          // Byte[0] bit5-7: rsv
        opcode = ctx->ibufmark[0] & 0xfu;           // Byte[0] bit1-4: opcode
        ++ctx->ibufmark;

        ctx->iom.opcode = opcode;
        ctx->iom.fin = fin;
        ctx->iom.rsv = rsv;

        ctx->imask = (ctx->ibufmark[0] >> 7) & 1;   // Byte[1] bit8: mask
        payloadlen = ctx->ibufmark[0] & 0x7fu;      // Byte[1] bit1-7: payload len
        ++ctx->ibufmark;

        // ctrl msg payload len 必须 <= 125，且必须是 fin 包
        if (wslay_is_ctrl_frame(opcode) && (payloadlen > 125 || !fin)) {
            return WSLAY_ERR_PROTO;
        }

        // payload len == 126 表示后面 2 字节是 payload len，即数据长度为 uint16_t
        if (payloadlen == 126) {
            ctx->istate = RECV_EXT_PAYLOADLEN;
            ctx->ireqread = 2;
        }
        // payload len == 127 表示后面 8 字节是 payload len，即数据长度为 uint64_t
        else if (payloadlen == 127) {
            ctx->istate = RECV_EXT_PAYLOADLEN;
            ctx->ireqread = 8;
        }
        // payload len <= 125 表示实际大小
        else {
            ctx->ipayloadlen = payloadlen;
            ctx->ipayloadoff = 0;
        }
    }

    // 接收 ext payload size
    if (ctx->istate == RECV_EXT_PAYLOADLEN) {

        if (WSLAY_AVAIL_IBUF(ctx) < ctx->ireqread) {
            if ((r = wslay_recv(ctx)) <= 0) return r;
            if (WSLAY_AVAIL_IBUF(ctx) < ctx->ireqread)
                return WSLAY_ERR_WANT_READ;
        }

        ctx->ipayloadlen = 0;
        ctx->ipayloadoff = 0;

        memcpy((uint8_t*)&ctx->ipayloadlen + (8-ctx->ireqread), ctx->ibufmark, ctx->ireqread);
        ctx->ipayloadlen = ntoh64(ctx->ipayloadlen);
        ctx->ibufmark += ctx->ireqread;

        // 对于 uint64 大小，实际 payload len 不能小于 uint16 所能表示的最大值（否则应该直接用 uint16 的 payload len 协议），且最高位(符号位)不能置位
        if (ctx->ireqread == 8) {
            if (ctx->ipayloadlen < (1 << 16) || ctx->ipayloadlen & (1ull << 63)) return WSLAY_ERR_PROTO;
        }
        // 对于 uint16 大小，实际 payload len 不能小于 126（否则应该直接用非扩展的 payload len 协议）
        else if (ctx->ipayloadlen < 126) return WSLAY_ERR_PROTO;
    }

    // 如果带有 MASK（4 Byte）信息，继续读取 MASK 信息
    if (ctx->imask) {
        ctx->istate = RECV_MASKKEY;
        ctx->ireqread = 4;
    }
    else ctx->istate = RECV_PAYLOAD;


    // 接收 mask key
    if (ctx->istate == RECV_MASKKEY) {

        if (WSLAY_AVAIL_IBUF(ctx) < ctx->ireqread) {
            if ((r = wslay_recv(ctx)) <= 0) return r;
            if (WSLAY_AVAIL_IBUF(ctx) < ctx->ireqread)
                return WSLAY_ERR_WANT_READ;
        }

        memcpy(ctx->imaskkey, ctx->ibufmark, 4);
        ctx->ibufmark += 4;
        ctx->istate = RECV_PAYLOAD;
    }

    // 接收 payload
    if (ctx->istate == RECV_PAYLOAD) {

        uint64_t rempayloadlen = ctx->ipayloadlen - ctx->ipayloadoff;
        if (WSLAY_AVAIL_IBUF(ctx) == 0 && rempayloadlen > 0) {
            if ((r = wslay_recv(ctx)) <= 0) return r;
        }

        // 缓存中读取的 payload 数据的起始位置
        uint8_t* readmark = ctx->ibufmark;
        // 缓存中读取的 payload 数据的截止位置（如果 payload 未全部加载完，则截止位置就是缓存的末尾；否则就是完整 payload 的末尾）
        uint8_t* readlimit = WSLAY_AVAIL_IBUF(ctx) < rempayloadlen ? ctx->ibuflimit : ctx->ibufmark + rempayloadlen;

        // 如果带有 MASK 信息，则解 MASK
        // + 客户端 -> 服务端：必须带 mask; 即客户端每帧都用随机 mask key，使线上原始字节不可预测
        //   因为 ws 握手之后的数据流就是 ws 二进制流，即不再是 http 协议流。
        //   如果不混淆，客户端请求的一个 ws 二进制流，其内容就可能恰好类似于一个 HTTP 协议流，从而误导客户端到服务器之间的中间设备（如缓存、代理等）
        //   因为这些设备如果不支持 ws 协议，就可能会按默认普通 http 协议进行解析处理
        if (ctx->imask) {
            // 解 MASK
            for (; ctx->ibufmark != readlimit; ++ctx->ibufmark, ++ctx->ipayloadoff) {
                ctx->ibufmark[0] ^= ctx->imaskkey[ctx->ipayloadoff % 4];
            }
        }
        else {
            ctx->ibufmark = readlimit;                  // ibufmark 重新指向已读取 payload 数据的末尾，即未读取部分的起始位置
            ctx->ipayloadoff += readlimit - readmark;
        }

        iocb->opcode = ctx->iom.opcode;
        iocb->payload_length = ctx->ipayloadlen;
        iocb->mask = ctx->imask;
        iocb->fin = ctx->iom.fin;
        iocb->rsv = ctx->iom.rsv;
        iocb->data = readmark;                          // 本次读取 payload chunk 的起始位置
        iocb->data_length = ctx->ibufmark - readmark;   // 本次读取 payload chunk 的长度

        // 如果负载读取完成，重新初始化读取下一帧
        if (ctx->ipayloadlen == ctx->ipayloadoff) {
            ctx->istate = RECV_HEADER1;
            ctx->ireqread = 2;
        }

        return iocb->data_length;
    }

    return WSLAY_ERR_INVALID_ARGUMENT;
}
