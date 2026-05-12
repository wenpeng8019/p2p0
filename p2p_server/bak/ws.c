/*
 * custom_ws.c — 通用 WebSocket 服务端基础设施实现（对应 custom_tcp.c）
 *
 * 关于 WSLAY 协议处理的关键机制
 *
 *    - write_enabled（即 wslay_event_get_write_enabled）。该值会在 init 是置为 true，然后在以下时机被置为 false：
 *      > wslay_event_send 过程中出现错误
 *      > close opcode 写入完成后
 *      > 主动执行 wslay_event_shutdown_write
 *    - wslay_event_want_write() == true
 *      > write_enabled 为 true，且发送缓冲队列不为空
 *      + 注意，wslay_event_send 写入报错，不会清除发送缓冲队列，只会将 write_enabled 设置为 false
 *        此外，wslay_event_send 写入报错，也不会导致 read_enabled 变为 false
 *
 *    - read_enabled（即 wslay_event_get_read_enabled）。该值会在 init 是置为 true，然后在以下时机被置为 false：
 *      > wslay_event_recv 过程中出现报错（入队 OOM、协议、数据过大溢出、负载无效等）
 *      > 触发 recv 执行接口设置了 would block 以为的错误，即网络 IO 错误。
 *      + 注意，wslay_event_recv 报错后，会自动发送一个 close 帧到发送队列。
 *        此外，recv 报错也不会导致 write_enabled 变为 false
 *      > 收到了 close 请求帧
 *      > 主动执行 wslay_event_shutdown_read
 *    - wslay_event_want_read() 等价于 read_enabled
 *
 *    + 关于关闭握手协议的处理：ws 协议要求一端在发送 close 协议字后，需等待对端返回 close 协议字，即双向确认关闭。
 *      > wslay 在收到对端的 WSLAY_CONNECTION_CLOSE 后会自动构造一个 close 命令到发送队列，并将 read_enabled 置为 false
 *      > wslay 在将 close 命令（收到 close 自动回复、或服务器主动发的）实际发送完成后会自动将 write_enabled 置为 false
 *      > 注意, 发送完 close 命令后，wslay 不会自动关闭 socket；主动发 close 也不会自动将 read_enabled 置为 false
 *        因为协议上允许在发送 close 后继续读取对端数据，直到收到对端 close 后才真正关闭连接。
 *        同样的，在主动发送 close 后，wslay 也不会为对方返回的 close 命令设置超时处理。
 *
 * 关于 WSLAY 架构性能
 * + 整体来说适合 demo 级应用
 *   内存全部都是 new/free；且数据收发都要经过内存拷贝
 * + 整体来说 WS 协议不是很复杂，必要时完全重写成本不算很高
 *   核心文件就两个 wslay_event(流程集成)、wslay_frame(协议解析和构造)
*/
#define MOD_TAG "CUSTOM_WS"

#include "ws.h"

#ifdef WITH_WS

#define BUF_FLAGS       BUF_FLAG_2048(0)
#define RESP_BUF_SZ     512

///////////////////////////////////////////////////////////////////////////////
// wslay 回调

static ssize_t ws_recv(wslay_event_context_ptr wsctx, uint8_t *buf, size_t len, int flags, void *ud) {
    (void)flags; if (!len) return 0;
    cw_client_t *client = (cw_client_t *)ud;

    // 若握手阶段已缓存了 WebSocket 帧数据，先从缓存回放
    if (client->buf) { uint8_t *buf0 = ITEM2BUF(client->buf);
        size_t n = client->len - client->pos;
        if (len < n) {
            memcpy(buf, buf0 + client->pos, len);
            client->pos += (uint16_t)len;
            return (ssize_t)len;
        }
        memcpy(buf, buf0 + client->pos, n);
        free_buf16(client->buf); client->buf = NULL;
        client->pos = client->len = 0;
        return (ssize_t)n;
    }

    ssize_t n;
    do n = recv(client->base.fd, (char *)buf, (int)len, 0); while (n < 0 && P_sock_is_interrupted());
    if (n > 0) return n;
    wslay_event_set_error(wsctx, n < 0 && P_sock_is_wouldblock() ? WSLAY_ERR_WOULDBLOCK : WSLAY_ERR_CALLBACK_FAILURE);
    client->io |= TCP_IO_FLAG_READ_BREAK;
    return -1;
}

static ssize_t ws_send(wslay_event_context_ptr wsctx, const uint8_t *data, size_t len, int flags, void *ud) {
    (void)flags; if (!len) return 0;
    cw_client_t *client = (cw_client_t *)ud;
    ssize_t n;
    do n = send(client->base.fd, (const char *)data, (int)len, 0); while (n < 0 && P_sock_is_interrupted());
    if (n > 0) return n;
    wslay_event_set_error(wsctx, (n < 0 && P_sock_is_wouldblock()) ? WSLAY_ERR_WOULDBLOCK : WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
}

static void cw_cb_msg(wslay_event_context_ptr wsctx, const struct wslay_event_on_msg_recv_arg *arg, void *ud) {
    (void)wsctx;
    cw_client_t *client = (cw_client_t *)ud;

    // 客户端发起关闭请求；wslay 会自动入队 close 回复，并将 read_enabled 置为 false
    if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
        client->handshake = TCP_HS_FLAG_CLOSING;   // = -1，与 custom_tcp 语义对齐
        return;
    }

    if (arg->opcode == WSLAY_TEXT_FRAME) {
        if (client->ctx->handle_text)
            client->ctx->handle_text(client, arg->msg, arg->msg_length);
    } else if (arg->opcode == WSLAY_BINARY_FRAME) {
        if (client->ctx->handle_data)
            client->ctx->handle_data(client, arg->msg, arg->msg_length);
    }
}

static const struct wslay_event_callbacks s_wslay_cbs = {
    ws_recv, ws_send,
    NULL, NULL, NULL, NULL,
    cw_cb_msg
};

static bool ws_handshake_done(cw_client_t *client) {

    // 若握手期间积压了 WS 帧数据，恢复 buf 缓存状态（供 cw_cb_recv 回放）
    if (client->base.instance_id) {
        client->pos = ((uint16_t *)&client->base.instance_id)[0];
        client->len = ((uint16_t *)&client->base.instance_id)[1];
        client->base.instance_id = 0;
    } else {
        free_buf16(client->buf);
        client->buf = NULL;
        client->len = client->pos = 0;
    }

    if (wslay_event_context_server_init(&client->ws_ctx, &s_wslay_cbs, client) != 0) {
        print("E:", LA_F("wslay context init failed\n", LA_F133, 133));
        return false;
    }

    // HTTP 握手完成，进入正常 WS 收发阶段，这里会直接进入应用层的握手阶段处理
    client->handshake = 2;

    return true;
}

static ret_t ws_accept(char* req, char* resp_buf, size_t resp_buf_len, const char* sub_protocol);

///////////////////////////////////////////////////////////////////////////////
// Public API

bool
cw_init_client(cw_client_t *client) {

    client->buf = alloc_buf16(BUF_FLAGS);
    if (!client->buf) {
        print("E:", LA_F("[WS] OOM: cannot allocate handshake buffer\n", LA_F133, 133));
        return false;
    }

    client->ws_ctx = NULL;

    // 初始化 buf 头部 RESP_BUF_SZ(512) 字节保留作为响应缓冲，后部接收 HTTP 请求
    client->len = client->pos = RESP_BUF_SZ;

    TCP_CLIENT_INIT(client);
    return true;
}

void
cw_free_client(custom_ws_ctx_t *ctx, cw_client_t *client) {

    if (client->ws_ctx) {
        wslay_event_context_free(client->ws_ctx);
        client->ws_ctx = NULL;
    }

    if (client->buf) {
        free_buf16(client->buf);
        client->buf = NULL;
    }

    free_client_base(&client->base);
}

void
cw_handle_recv(custom_ws_ctx_t *ctx, cw_client_t *client) {

    // 如果处于 WS 协议的 handshake 阶段
    if (client->handshake == TCP_HS_FLAG_HANDSHAKING) {

        char *buf = (char *)ITEM2BUF(client->buf);
        size_t buf_sz = BUF_SIZE(client->buf->flags) - 1; // 这里保留一个字节用于追加 \0
        while (client->len < buf_sz - 1) {

            size_t sz = buf_sz - client->len;
            int r = tcp_recv((tcp_client_t*)client, buf + client->len, &sz, "WS");
            if (r > 0) return;
            if (r < 0) {
                cw_free_client(ctx, client);
                return;
            }
            buf[client->len += (uint16_t)sz] = '\0';

            // 扫描 \r\n\r\n（HTTP 头结束标志）
            if (buf[client->pos] != '\r') {
                char *p = strchr(buf + client->pos, '\r');
                if (!p) { client->pos = client->len; continue; }
                client->pos = (uint16_t)(p - buf);
            }
            LOOP_RN:
            if (client->pos + 4 > client->len) continue;
            if (memcmp(buf + client->pos, "\r\n\r\n", 4) == 0) break;
            char *p = strchr(buf + client->pos + 1, '\r');
            if (p) { client->pos = (uint16_t)(p - buf); goto LOOP_RN; }
            client->pos = client->len;
        }
        buf[client->pos] = '\0';

        // 注: 初始 HTTP 请求数据被读取到 buf + RESP_BUF_SZ 之后的部分，而 buf 的前 RESP_BUF_SZ 字节被保留作为这里的握手应答缓冲
        ret_t resp = ws_accept(buf + RESP_BUF_SZ, buf, RESP_BUF_SZ, ctx->sub_protocol);
        if (resp < E_NONE) {
            print("E:", LA_F("[WS] handshake failed: invalid HTTP request\n", LA_F143, 143));
            cw_free_client(ctx, client);
            return;
        }

        // 返回握手应答响应
        // + 握手阶段可以直接接入，而无需再次等待 writable 周期判定
        size_t sz = resp;
        int r = tcp_send((tcp_client_t*)client, buf, &sz, "");
        if (r < 0) {
            cw_free_client(ctx, client);
            return;
        }

        // 如果 would block，则标记进入握手写入阶段
        if (r > 0) {

            // 如果 recv 的数据流中还包含了 HTTP 请求之后的 WS 帧数据
            // + 由于写入阶段会复用 len/pos 属性，所以这里使用一个技巧，将它们暂存到 instance_id 中，以便在 handshake_done 恢复状态
            client->pos += 4; // 跳过 "\r\n\r\n"
            if (client->len > client->pos) {
                ((uint16_t *)&client->base.instance_id)[0] = client->pos;
                ((uint16_t *)&client->base.instance_id)[1] = client->len;
            } else client->base.instance_id = 0;    // instance_id = 0 代表没有积压数据

            client->len = (uint16_t)resp;
            client->pos = (uint16_t)sz;

            // 进入握手写阶段，同时暂停接收数据，等握手（ACK 发送）完成后再继续
            client->io &= ~TCP_IO_FLAG_WANT_READ;
            client->io |= TCP_IO_FLAG_WANT_WRITE;
            return;
        }

        // ws 握手（应答发送）完成（注意，这里完成后，可以继续执行 recv 处理）
        if (!ws_handshake_done(client)) {
            cw_free_client(ctx, client);
            return;
        }

        assert(wslay_event_want_read(client->ws_ctx));
    }

    // 正常 WS 帧接收
    // + wslay_event_want_read 默认只会读取一个有效帧，所以这里需要循环处理
    assert(!(client->io & TCP_IO_FLAG_READ_BREAK));     // 用于 hook wslay 内部的 recv would block
    do {
        int r = wslay_event_recv(client->ws_ctx);

        // 如果 wslay 内部 recv 出错
        // + 此时 wslay 内部会尝试入队 close 帧并将 read_enabled 置为 false
        if (r != 0) {
            print("E:", LA_F("[WS] recv failed(%d)\n", LA_F198, 198), r);
            assert(!wslay_event_want_read(client->ws_ctx));
        }

        // 如果 recv 处理之后，read_enabled 被置为 false
        if (!wslay_event_want_read(client->ws_ctx)) {

            // 如果写入已经被关闭
            if (!wslay_event_want_write(client->ws_ctx)) {
                print("I:", LA_F("[WS] connection closed by peer\n", 0, 0));
                cw_free_client(ctx, client);
                return;
            }

            // 同步 io 标志
            client->io &= ~TCP_IO_FLAG_WANT_READ;
            break;
        }
    } while (!(client->io & TCP_IO_FLAG_READ_BREAK));
    client->io &= ~TCP_IO_FLAG_READ_BREAK;
}

void
cw_handle_send(custom_ws_ctx_t *ctx, cw_client_t *client) {

    // 如果处于 WS 协议的 handshake 应答写入阶段
    if (client->handshake == TCP_HS_FLAG_HANDSHAKING) {

        size_t sz = client->len - client->pos;
        int r = tcp_send((tcp_client_t*)client, ITEM2BUF(client->buf) + client->pos, &sz, "");
        if (r < 0) {
            cw_free_client(ctx, client);
            return;
        }

        if (!sz || (client->pos += sz) < sz) return;

        // ws 握手（应答发送）完成
        if (!ws_handshake_done(client)) {
            cw_free_client(ctx, client);
            return;
        }

        assert(wslay_event_want_read(client->ws_ctx));
        client->io |= TCP_IO_FLAG_WANT_READ;
        client->io &= ~TCP_IO_FLAG_WANT_WRITE;

        // 握手应答写入完成后，可以继续处理 recv（写入期间可能积压了新的请求数据）
        cw_handle_recv(ctx, client);
        if (!wslay_event_want_write(client->ws_ctx)) return;
    }

    // 正常 WS 帧发送
    // + 注: wslay_event_send 默认会一次性发送所有数据，直到 would block 或报错
    assert(wslay_event_want_write(client->ws_ctx));
    int r = wslay_event_send(client->ws_ctx);
    if (r != 0) {
        print("E:", LA_F("[WS] send failed(code=%d)\n", LA_F194, 194), r);
        return;
    }

    // 全部数据发送完成
    if (!wslay_event_want_write(client->ws_ctx)) {

        // 如果已经停止读取
        if (!wslay_event_want_read(client->ws_ctx)) {
            print("I:", LA_F("[WS] connection closed & done\n", 0, 0));
            cw_free_client(ctx, client);
            return;
        }
        // 触发发送完成回调
        //if (ctx->handle_send_complete) ctx->handle_send_complete(client);
    }
}

void
cw_retry_closing(custom_ws_ctx_t *ctx, cw_client_t *client, uint64_t now) {

    if (client->ws_ctx && (client->io & CW_IO_FLAG_CLOSING) &&
        tick_diff(now, client->base.last_active) >= CLIENT_TIMEOUT_S * 1000) {
        print("I:", LA_F("[WS] close timeout, force closing\n", LA_F192, 192));
        cw_free_client(ctx, client);
    }
}

///////////////////////////////////////////////////////////////////////////////
// 发送接口

ret_t
cw_send_text(cw_client_t *client, const char *text) {
    assert(!client->handshake);

    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_TEXT_FRAME;
    msg.msg        = (const uint8_t *)text;
    msg.msg_length = strlen(text);
    int r = wslay_event_queue_msg(client->ws_ctx, &msg);
    if (r < 0) {
        print("E:", LA_F("[WS] queue text msg failed(%d)\n", LA_F197, 197), r);
        return -1;
    }
    assert(wslay_event_want_write(client->ws_ctx));
    return 0;
}

ret_t
cw_send_data(cw_client_t *client, const uint8_t *data, size_t len) {
    assert(!client->handshake);

    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_BINARY_FRAME;
    msg.msg        = data;
    msg.msg_length = len;
    int r = wslay_event_queue_msg(client->ws_ctx, &msg);
    if (r < 0) {
        print("E:", LA_F("[WS] queue binary msg failed(%d)\n", LA_F196, 196), r);
        return -1;
    }
    assert(wslay_event_want_write(client->ws_ctx));
    return 0;
}

ret_t
cw_close(cw_client_t *client, uint16_t code) {

    int r = wslay_event_queue_close(client->ws_ctx, code, NULL, 0);
    if (r < 0) {
        print("E:", LA_F("[WS] queue close(%u) failed(%d)\n", LA_F195, 195), code, r);
        return -1;
    }
    assert(wslay_event_want_write(client->ws_ctx));
    client->io |= CW_IO_FLAG_CLOSING;
    client->base.last_active = P_tick_ms();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

#define WS_SHA1_LEN 20

typedef struct {
    uint32_t h[5];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total;
} ws_sha1_ctx_t;

static uint32_t ws_sha1_rot(uint32_t x, int n) { return (x<<n)|(x>>(32-n)); }

static void ws_sha1_block(ws_sha1_ctx_t *ctx, const uint8_t *blk) {
    uint32_t w[80], a,b,c,d,e,f,k,tmp; int i;
    for (i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)
                            |((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for (i=16;i<80;i++) w[i]=ws_sha1_rot(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=ctx->h[0];b=ctx->h[1];c=ctx->h[2];d=ctx->h[3];e=ctx->h[4];
    for (i=0;i<80;i++) {
        if      (i<20){f=(b&c)|(~b&d);          k=0x5A827999u;}
        else if (i<40){f=b^c^d;                 k=0x6ED9EBA1u;}
        else if (i<60){f=(b&c)|(b&d)|(c&d);     k=0x8F1BBCDCu;}
        else          {f=b^c^d;                 k=0xCA62C1D6u;}
        tmp=ws_sha1_rot(a,5)+f+e+k+w[i];
        e=d;d=c;c=ws_sha1_rot(b,30);b=a;a=tmp;
    }
    ctx->h[0]+=a;ctx->h[1]+=b;ctx->h[2]+=c;ctx->h[3]+=d;ctx->h[4]+=e;
}

static void ws_sha1_init(ws_sha1_ctx_t *ctx) {
    ctx->h[0]=0x67452301u;ctx->h[1]=0xEFCDAB89u;ctx->h[2]=0x98BADCFEu;
    ctx->h[3]=0x10325476u;ctx->h[4]=0xC3D2E1F0u;
    ctx->buf_len=0;ctx->total=0;
}

static void ws_sha1_update(ws_sha1_ctx_t *ctx, const uint8_t *data, size_t len) {
    ctx->total+=len;
    while (len>0) {
        size_t cp=64-ctx->buf_len; if(cp>len)cp=len;
        memcpy(ctx->buf+ctx->buf_len,data,cp);
        ctx->buf_len+=(uint32_t)cp; data+=cp; len-=cp;
        if(ctx->buf_len==64){ws_sha1_block(ctx,ctx->buf);ctx->buf_len=0;}
    }
}

static void ws_sha1_final(ws_sha1_ctx_t *ctx, uint8_t d[WS_SHA1_LEN]) {
    uint64_t bl=ctx->total*8; uint8_t p=0x80;
    ws_sha1_update(ctx,&p,1); p=0;
    while(ctx->buf_len!=56)ws_sha1_update(ctx,&p,1);
    uint8_t lb[8]; for(int i=7;i>=0;i--){lb[i]=(uint8_t)(bl&0xFF);bl>>=8;}
    ws_sha1_update(ctx,lb,8);
    for(int i=0;i<5;i++){d[i*4]=(uint8_t)(ctx->h[i]>>24);d[i*4+1]=(uint8_t)(ctx->h[i]>>16);
                          d[i*4+2]=(uint8_t)(ctx->h[i]>>8);d[i*4+3]=(uint8_t)(ctx->h[i]);}
}

/* 将 20 字节 SHA-1 摘要编码为 28+1 字节 base64 字符串 */
static void ws_b64_sha1(const uint8_t src[20], char dst[29]) {
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i,j;
    for(i=0,j=0;i<18;i+=3){
        uint32_t v=((uint32_t)src[i]<<16)|((uint32_t)src[i+1]<<8)|src[i+2];
        dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j++]=t[v&63];
    }
    {uint32_t v=((uint32_t)src[18]<<16)|((uint32_t)src[19]<<8);
     dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j++]='=';}
    dst[28]='\0';
}

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

ret_t ws_accept(char* req, char* resp_buf, size_t resp_buf_len, const char* sub_protocol) {

    int n = 0;

    // 解析 Sec-WebSocket-Key
    const char *key_field = strstr(req, "Sec-WebSocket-Key:");
    if (!key_field) return E_INVALID;
    key_field += 18; while (*key_field == ' ') key_field++;
    char ws_key[32] = {0};
    while (*key_field && *key_field != '\r' && *key_field != '\n' && n < 31)
        ws_key[n++] = *key_field++;

    if (sub_protocol && !*sub_protocol) sub_protocol = NULL;

    // 计算 Sec-WebSocket-Accept
    char combined[64];
    snprintf(combined, sizeof(combined), "%s" WS_GUID, ws_key);
    ws_sha1_ctx_t sha;
    ws_sha1_init(&sha);
    ws_sha1_update(&sha, (const uint8_t*)combined, strlen(combined));
    uint8_t digest[WS_SHA1_LEN];
    ws_sha1_final(&sha, digest);
    char accept[29];
    ws_b64_sha1(digest, accept);

    // 构造 101 响应
    n = snprintf(resp_buf, resp_buf_len,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "%s%s%s"
        "\r\n",
        accept,
        sub_protocol ? "Sec-WebSocket-Protocol: " : "",
        sub_protocol ? sub_protocol               : "",
        sub_protocol ? "\r\n"                     : "");

    return n < (int)resp_buf_len ? n : E_OUT_OF_CAPACITY;
}

#endif // WITH_WS
