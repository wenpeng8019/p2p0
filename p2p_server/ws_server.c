/*
 * ws_server.c — 轻量 WebSocket 服务器实现
 *
 * 依赖：wslay（third_party/wslay），无其他外部依赖。
 * SHA-1 / Base64 在本文件内自包含实现，仅用于 WS 握手。
 */

#include "ws_server.h"
#include <stdc.h>          /* sock_t / P_INVALID_SOCKET / P_sock_close / P_sock_nonblock /
                            P_sock_is_wouldblock / P_sock_is_interrupted 等跟平台操作 */

#ifndef WITH_WSLAY
void ws_server_dummy(void) {}
#else

#include <wslay/wslay.h>

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define VALID_SOCK(s) ((s) != P_INVALID_SOCKET)

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

/* =========================================================================
 * 客户端槽位
 * ====================================================================== */

#define WS_SRV_HTTP_BUF  4096
#define WS_SRV_RECV_BUF  65536

typedef enum {
    WS_SLOT_FREE        = 0,
    WS_SLOT_HANDSHAKING = 1,
    WS_SLOT_OPEN        = 2,
    WS_SLOT_CLOSING     = 3,
} ws_slot_state_t;

typedef struct {
    ws_slot_state_t         state;
    sock_t                  fd;
    ws_client_id_t          id;          /* 1-based */
    wslay_event_context_ptr ws_ctx;
    void                   *ws_cbdata;   /* malloc 的 ws_srv_cbdata_t，与 ws_ctx 同生命周期 */

    /* HTTP 握手接收缓冲 */
    char                    http_buf[WS_SRV_HTTP_BUF];
    size_t                  http_buf_len;

    /* wslay 收取缓冲（HTTP 握手完成后的残余数据）*/
    uint8_t                 recv_buf[WS_SRV_RECV_BUF];
    size_t                  recv_buf_pos;
    size_t                  recv_buf_len;
} ws_slot_t;

/* =========================================================================
 * 服务器结构
 * ====================================================================== */

struct ws_server {
    sock_t             listen_fd;
    ws_server_cfg_t    cfg;
    ws_slot_t          slots[WS_SERVER_MAX_CLIENTS];
    int                client_count;
};

/* =========================================================================
 * socket 辅助
 * ====================================================================== */

static int ws_srv_nonblock(sock_t fd) {
    return P_sock_nonblock(fd, true) == E_NONE ? 0 : -1;
}

/* =========================================================================
 * wslay 服务端回调（每个槽位共用，通过 user_data 区分）
 * ====================================================================== */

typedef struct { ws_server_t *srv; ws_slot_t *slot; } ws_srv_cbdata_t;

static ssize_t sslot_recv_cb(wslay_event_context_ptr ctx,
                               uint8_t *buf, size_t len,
                               int flags, void *ud) {
    (void)flags;
    ws_srv_cbdata_t *d = (ws_srv_cbdata_t *)ud;
    ws_slot_t *slot = d->slot;

    if (slot->recv_buf_len > 0) {
        size_t cp = slot->recv_buf_len < len ? slot->recv_buf_len : len;
        memcpy(buf, slot->recv_buf + slot->recv_buf_pos, cp);
        slot->recv_buf_pos += cp; slot->recv_buf_len -= cp;
        return (ssize_t)cp;
    }
    ssize_t n;
    do {
        n = recv(slot->fd, (char*)buf, (int)len, 0);
    } while (n < 0 && P_sock_is_interrupted());
    if (n < 0) {
        wslay_event_set_error(ctx, P_sock_is_wouldblock()
                                   ? WSLAY_ERR_WOULDBLOCK
                                   : WSLAY_ERR_CALLBACK_FAILURE);
        return -1;
    }
    if (n == 0) { wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE); return -1; }
    return n;
}

static ssize_t sslot_send_cb(wslay_event_context_ptr ctx,
                               const uint8_t *data, size_t len,
                               int flags, void *ud) {
    (void)flags;
    ws_srv_cbdata_t *d = (ws_srv_cbdata_t *)ud;
    ws_slot_t *slot = d->slot;
    ssize_t n;
    do {
        n = send(slot->fd, (const char*)data, (int)len, 0);
    } while (n < 0 && P_sock_is_interrupted());
    if (n < 0) {
        wslay_event_set_error(ctx, P_sock_is_wouldblock()
                                   ? WSLAY_ERR_WOULDBLOCK
                                   : WSLAY_ERR_CALLBACK_FAILURE);
        return -1;
    }
    return n;
}

static void sslot_on_msg_cb(wslay_event_context_ptr ctx,
                              const struct wslay_event_on_msg_recv_arg *arg,
                              void *ud) {
    (void)ctx;
    ws_srv_cbdata_t *d = (ws_srv_cbdata_t *)ud;
    ws_server_t *srv   = d->srv;
    ws_slot_t   *slot  = d->slot;

    if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
        slot->state = WS_SLOT_CLOSING;
        return;
    }

    if (!srv->cfg.on_message) return;

    ws_srv_msg_type_t type;
    switch (arg->opcode) {
        case WSLAY_TEXT_FRAME:   type = WS_SRV_MSG_TEXT;   break;
        case WSLAY_BINARY_FRAME: type = WS_SRV_MSG_BINARY; break;
        case WSLAY_PING:         type = WS_SRV_MSG_PING;   break;
        case WSLAY_PONG:         type = WS_SRV_MSG_PONG;   break;
        default: return;
    }
    srv->cfg.on_message(srv, slot->id, type, arg->msg, arg->msg_length, srv->cfg.user_data);
}

/* =========================================================================
 * 升级握手
 * ====================================================================== */

static int ws_srv_do_handshake(ws_server_t *srv, ws_slot_t *slot) {
    /* 继续接收 HTTP 请求 */
    while (slot->http_buf_len < sizeof(slot->http_buf) - 1) {
        ssize_t n = recv(slot->fd, slot->http_buf + slot->http_buf_len,
                         (int)(sizeof(slot->http_buf)-1-slot->http_buf_len), 0);
        if (n <= 0) {
            if (n < 0 && P_sock_is_wouldblock()) break;  /* 下次继续 */
            return -1;  /* 断开 */
        }
        slot->http_buf_len += (size_t)n;
        slot->http_buf[slot->http_buf_len] = '\0';
        if (strstr(slot->http_buf, "\r\n\r\n")) break;
    }

    if (!strstr(slot->http_buf, "\r\n\r\n")) return 0;  /* 还没收完 */

    /* 解析 Sec-WebSocket-Key */
    const char *key_field = strstr(slot->http_buf, "Sec-WebSocket-Key:");
    if (!key_field) return -1;
    key_field += 18;
    while (*key_field == ' ') key_field++;
    char ws_key[32] = {0};
    int ki = 0;
    while (*key_field && *key_field != '\r' && *key_field != '\n' && ki < 31)
        ws_key[ki++] = *key_field++;

    /* 计算 Sec-WebSocket-Accept */
    char combined[64];
    snprintf(combined, sizeof(combined), "%s" WS_GUID, ws_key);
    ws_sha1_ctx_t sha;
    ws_sha1_init(&sha);
    ws_sha1_update(&sha, (const uint8_t*)combined, strlen(combined));
    uint8_t digest[WS_SHA1_LEN];
    ws_sha1_final(&sha, digest);
    char accept[29];
    ws_b64_sha1(digest, accept);

    /* 组装 101 响应 */
    char resp[512];
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "%s%s%s"
        "\r\n",
        accept,
        srv->cfg.sub_protocol ? "Sec-WebSocket-Protocol: " : "",
        srv->cfg.sub_protocol ? srv->cfg.sub_protocol       : "",
        srv->cfg.sub_protocol ? "\r\n"                      : "");

    /* 发送 101 响应：临时切回阻塞模式（响应仅 ~150 字节，不卡主循环）
     * 非阻塞 socket 上直接 send 可能返回 EWOULDBLOCK（虽然极少），
     * 阻塞一次性发完是最简洁的正确做法。 */
    P_sock_nonblock(slot->fd, false);
    {
        int sent = 0;
        while (sent < rlen) {
            ssize_t n;
            do { n = send(slot->fd, resp + sent, (int)(rlen - sent), 0); }
            while (n < 0 && P_sock_is_interrupted());
            if (n <= 0) { P_sock_nonblock(slot->fd, true); return -1; }
            sent += (int)n;
        }
    }
    P_sock_nonblock(slot->fd, true);  /* 恢复非阻塞 */

    /* 检查 HTTP 请求头之后是否有残余（WS frame 数据） */
    const char *body = strstr(slot->http_buf, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t leftover = slot->http_buf_len - (size_t)(body - slot->http_buf);
        if (leftover > 0 && leftover <= WS_SRV_RECV_BUF) {
            memcpy(slot->recv_buf, body, leftover);
            slot->recv_buf_pos = 0;
            slot->recv_buf_len = leftover;
        }
    }
    slot->http_buf_len = 0;

    /* 初始化 wslay 服务端上下文 */
    ws_srv_cbdata_t *cbdata = (ws_srv_cbdata_t *)malloc(sizeof(*cbdata));
    if (!cbdata) return -1;
    cbdata->srv  = srv;
    cbdata->slot = slot;

    static const struct wslay_event_callbacks cbs = {
        sslot_recv_cb,
        sslot_send_cb,
        NULL, /* genmask: 服务端不 mask */
        NULL, NULL, NULL,
        sslot_on_msg_cb
    };
    if (wslay_event_context_server_init(&slot->ws_ctx, &cbs, cbdata) != 0) {
        free(cbdata);
        return -1;
    }
    slot->ws_cbdata = cbdata;  /* 记录，随 slot 一起释放 */

    slot->state = WS_SLOT_OPEN;
    srv->client_count++;

    if (srv->cfg.on_connect)
        srv->cfg.on_connect(srv, slot->id, srv->cfg.user_data);

    return 1;
}

/* =========================================================================
 * 槽位关闭
 * ====================================================================== */

static void ws_slot_close(ws_server_t *srv, ws_slot_t *slot) {
    if (slot->state == WS_SLOT_FREE) return;

    if (slot->state == WS_SLOT_OPEN || slot->state == WS_SLOT_CLOSING) {
        srv->client_count--;
        if (srv->cfg.on_disconnect)
            srv->cfg.on_disconnect(srv, slot->id, srv->cfg.user_data);
    }

    if (slot->ws_ctx) {
        wslay_event_context_free(slot->ws_ctx);
        slot->ws_ctx = NULL;
    }
    if (slot->ws_cbdata) {
        free(slot->ws_cbdata);
        slot->ws_cbdata = NULL;
    }

    if (VALID_SOCK(slot->fd)) {
        P_sock_close(slot->fd);
        slot->fd = P_INVALID_SOCKET;
    }

    slot->state       = WS_SLOT_FREE;
    slot->http_buf_len = 0;
    slot->recv_buf_len = 0;
    slot->recv_buf_pos = 0;
}

/* =========================================================================
 * 生命周期
 * ====================================================================== */

ws_server_t *ws_server_create(const ws_server_cfg_t *cfg, uint16_t port) {
    ws_server_t *srv = (ws_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    if (cfg) srv->cfg = *cfg;

    /* 初始化所有槽位 */
    for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++) {
        srv->slots[i].fd = P_INVALID_SOCKET;
        srv->slots[i].id = i + 1;  /* 1-based */
    }

    /* port == 0：嵌入模式，不创建监听 socket，由外部注入已 accept 的 fd */
    if (port == 0) {
        srv->listen_fd = P_INVALID_SOCKET;
        return srv;
    }

    /* 创建监听 socket */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!VALID_SOCK(srv->listen_fd)) { free(srv); return NULL; }

    P_sock_reuseaddr(srv->listen_fd, true);
    ws_srv_nonblock(srv->listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(srv->listen_fd, 16) < 0) {
        P_sock_close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    return srv;
}

void ws_server_destroy(ws_server_t *srv) {
    if (!srv) return;
    for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++)
        ws_slot_close(srv, &srv->slots[i]);
    if (VALID_SOCK(srv->listen_fd)) P_sock_close(srv->listen_fd);
    free(srv);
}

/* =========================================================================
 * 主循环驱动
 * ====================================================================== */

void ws_server_update(ws_server_t *srv) {
    if (!srv) return;

    /* 接受新连接（嵌入模式 listen_fd==P_INVALID_SOCKET 时跳过）*/
    if (VALID_SOCK(srv->listen_fd)) {
    for (;;) {
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        sock_t fd = accept(srv->listen_fd, (struct sockaddr*)&peer_addr, &peer_len);
        if (!VALID_SOCK(fd)) break;

        /* 找空闲槽位 */
        ws_slot_t *slot = NULL;
        for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++) {
            if (srv->slots[i].state == WS_SLOT_FREE) { slot = &srv->slots[i]; break; }
        }
        if (!slot) {
            P_sock_close(fd);  /* 满了，拒绝 */
            break;
        }

        ws_srv_nonblock(fd);
        slot->fd    = fd;
        slot->state = WS_SLOT_HANDSHAKING;
        slot->http_buf_len = 0;
        slot->recv_buf_len = 0;
        slot->recv_buf_pos = 0;
    }
    } /* if VALID_SOCK(listen_fd) */

    /* 处理每个活跃槽位 */
    for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++) {
        ws_slot_t *slot = &srv->slots[i];
        if (slot->state == WS_SLOT_FREE) continue;

        if (slot->state == WS_SLOT_HANDSHAKING) {
            int r = ws_srv_do_handshake(srv, slot);
            if (r < 0) ws_slot_close(srv, slot);
            /* r == 0: 还没接收完，下次继续 */
            continue;
        }

        if (slot->state == WS_SLOT_OPEN || slot->state == WS_SLOT_CLOSING) {
            /* 读取新到达的 socket 数据 */
            if (slot->recv_buf_len == 0) {
                slot->recv_buf_pos = 0;
                ssize_t n = recv(slot->fd, (char*)slot->recv_buf, WS_SRV_RECV_BUF, 0);
                if (n == 0) {
                    ws_slot_close(srv, slot);
                    continue;
                } else if (n > 0) {
                    slot->recv_buf_len = (size_t)n;
                }
                /* n < 0 且 EWOULDBLOCK：无新数据，继续 */
            }

            if (wslay_event_want_read(slot->ws_ctx))
                wslay_event_recv(slot->ws_ctx);
            if (wslay_event_want_write(slot->ws_ctx))
                wslay_event_send(slot->ws_ctx);

            /* 检查是否需要关闭 */
            if (slot->state == WS_SLOT_CLOSING ||
                (!wslay_event_want_read(slot->ws_ctx) &&
                 !wslay_event_want_write(slot->ws_ctx))) {
                ws_slot_close(srv, slot);
            }
        }
    }
}

/* =========================================================================
 * 发送
 * ====================================================================== */

static ws_slot_t *find_slot(ws_server_t *srv, ws_client_id_t cid) {
    for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++) {
        if (srv->slots[i].id == cid && srv->slots[i].state == WS_SLOT_OPEN)
            return &srv->slots[i];
    }
    return NULL;
}

int ws_server_send_text(ws_server_t *srv, ws_client_id_t cid, const char *text) {
    ws_slot_t *slot = find_slot(srv, cid);
    if (!slot || !slot->ws_ctx) return -1;
    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_TEXT_FRAME;
    msg.msg        = (const uint8_t *)text;
    msg.msg_length = strlen(text);
    return wslay_event_queue_msg(slot->ws_ctx, &msg) == 0 ? 0 : -1;
}

int ws_server_send_binary(ws_server_t *srv, ws_client_id_t cid,
                           const uint8_t *data, size_t len) {
    ws_slot_t *slot = find_slot(srv, cid);
    if (!slot || !slot->ws_ctx) return -1;
    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_BINARY_FRAME;
    msg.msg        = data;
    msg.msg_length = len;
    return wslay_event_queue_msg(slot->ws_ctx, &msg) == 0 ? 0 : -1;
}

void ws_server_broadcast_text(ws_server_t *srv, const char *text) {
    for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++) {
        if (srv->slots[i].state == WS_SLOT_OPEN)
            ws_server_send_text(srv, srv->slots[i].id, text);
    }
}

void ws_server_disconnect(ws_server_t *srv, ws_client_id_t cid, uint16_t code) {
    ws_slot_t *slot = find_slot(srv, cid);
    if (!slot || !slot->ws_ctx) return;
    struct wslay_event_msg msg;
    uint8_t buf[2];
    buf[0] = (uint8_t)(code >> 8); buf[1] = (uint8_t)(code & 0xFF);
    msg.opcode = WSLAY_CONNECTION_CLOSE; msg.msg = buf; msg.msg_length = 2;
    wslay_event_queue_msg(slot->ws_ctx, &msg);
    slot->state = WS_SLOT_CLOSING;
}

int ws_server_client_count(const ws_server_t *srv) {
    return srv ? srv->client_count : 0;
}

int ws_server_inject_fd(ws_server_t *srv, ws_srv_fd_t fd) {
    if (!srv) return -1;
    ws_slot_t *slot = NULL;
    for (int i = 0; i < WS_SERVER_MAX_CLIENTS; i++) {
        if (srv->slots[i].state == WS_SLOT_FREE) { slot = &srv->slots[i]; break; }
    }
    if (!slot) return -1;

    ws_srv_nonblock((sock_t)fd);
    slot->fd           = (sock_t)fd;
    slot->state        = WS_SLOT_HANDSHAKING;
    slot->http_buf_len = 0;
    slot->recv_buf_len = 0;
    slot->recv_buf_pos = 0;
    return 0;
}

#endif /* WITH_WSLAY */
