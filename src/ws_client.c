/*
 * ws_client.c — 轻量 WebSocket 客户端实现
 *
 * 依赖：wslay（third_party/wslay），无其他外部依赖。
 * SHA-1 和 Base64 编码在本文件内自包含实现（仅用于 WS 握手）。
 */

#include "ws_client.h"
#include "predefine.h"

#ifndef WITH_WSLAY
void ws_client_dummy(void) {}
#else

#include <wslay/wslay.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define SOCK_ERRNO  WSAGetLastError()
#  define EWOULDBLOCK_VAL WSAEWOULDBLOCK
#  define EINPROGRESS_VAL WSAEWOULDBLOCK
#  define sock_close(s) closesocket(s)
typedef SOCKET sock_t;
#  define INVALID_SOCK INVALID_SOCKET
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#  define SOCK_ERRNO  errno
#  define EWOULDBLOCK_VAL EWOULDBLOCK
#  define EINPROGRESS_VAL EINPROGRESS
#  define sock_close(s) close(s)
typedef int sock_t;
#  define INVALID_SOCK (-1)
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* =========================================================================
 * SHA-1（RFC 3174）— 仅用于 WS 握手 Sec-WebSocket-Accept
 * ====================================================================== */

#define SHA1_DIGEST_LEN 20

typedef struct {
    uint32_t h[5];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total;
} sha1_ctx_t;

static uint32_t sha1_rot(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_process_block(sha1_ctx_t *ctx, const uint8_t *block) {
    uint32_t w[80], a, b, c, d, e, f, k, tmp;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4]   << 24) | ((uint32_t)block[i*4+1] << 16)
             | ((uint32_t)block[i*4+2] <<  8) |  (uint32_t)block[i*4+3];
    }
    for (i = 16; i < 80; i++)
        w[i] = sha1_rot(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3]; e = ctx->h[4];
    for (i = 0; i < 80; i++) {
        if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        tmp = sha1_rot(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rot(b, 30); b = a; a = tmp;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c;
    ctx->h[3] += d; ctx->h[4] += e;
}

static void sha1_init(sha1_ctx_t *ctx) {
    ctx->h[0]=0x67452301u; ctx->h[1]=0xEFCDAB89u; ctx->h[2]=0x98BADCFEu;
    ctx->h[3]=0x10325476u; ctx->h[4]=0xC3D2E1F0u;
    ctx->buf_len = 0; ctx->total = 0;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len) {
    ctx->total += len;
    while (len > 0) {
        size_t copy = 64 - ctx->buf_len;
        if (copy > len) copy = len;
        memcpy(ctx->buf + ctx->buf_len, data, copy);
        ctx->buf_len += (uint32_t)copy;
        data += copy; len -= copy;
        if (ctx->buf_len == 64) {
            sha1_process_block(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

static void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_LEN]) {
    uint64_t bit_len = ctx->total * 8;
    uint8_t pad = 0x80;
    sha1_update(ctx, &pad, 1);
    pad = 0;
    while (ctx->buf_len != 56) sha1_update(ctx, &pad, 1);
    uint8_t len_be[8];
    for (int i = 7; i >= 0; i--) { len_be[i] = (uint8_t)(bit_len & 0xFF); bit_len >>= 8; }
    sha1_update(ctx, len_be, 8);
    for (int i = 0; i < 5; i++) {
        digest[i*4]   = (uint8_t)(ctx->h[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->h[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->h[i] >>  8);
        digest[i*4+3] = (uint8_t)(ctx->h[i]      );
    }
}

/* =========================================================================
 * Base64 编码（RFC 4648）
 * ====================================================================== */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* dst 需至少 ((src_len + 2) / 3 * 4 + 1) 字节 */
static void base64_encode(const uint8_t *src, size_t src_len, char *dst) {
    size_t i = 0, j = 0;
    while (i < src_len) {
        uint32_t a = i < src_len ? src[i++] : 0;
        uint32_t b = i < src_len ? src[i++] : 0;
        uint32_t cc= i < src_len ? src[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | cc;
        dst[j++] = b64_table[(triple >> 18) & 0x3F];
        dst[j++] = b64_table[(triple >> 12) & 0x3F];
        dst[j++] = (src_len - (i - (i <= src_len ? 0 : i - src_len)) >= 2 || i > src_len + 1)
                   ? '=' : b64_table[(triple >> 6) & 0x3F];
        dst[j++] = (src_len - (i - (i <= src_len ? 0 : i - src_len)) >= 1 || i > src_len)
                   ? '=' : b64_table[triple & 0x3F];
    }
    dst[j] = '\0';
}

/* 简化版：直接处理固定 20 字节 SHA-1 输出 → 28 字符 base64 */
static void base64_encode_sha1(const uint8_t src[20], char dst[29]) {
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j;
    for (i = 0, j = 0; i < 18; i += 3) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i+1] << 8) | src[i+2];
        dst[j++] = t[(v>>18)&63]; dst[j++] = t[(v>>12)&63];
        dst[j++] = t[(v>>6)&63];  dst[j++] = t[v&63];
    }
    /* 最后 2 字节（20 % 3 == 2） */
    { uint32_t v = ((uint32_t)src[18] << 16) | ((uint32_t)src[19] << 8);
      dst[j++] = t[(v>>18)&63]; dst[j++] = t[(v>>12)&63];
      dst[j++] = t[(v>>6)&63];  dst[j++] = '='; }
    dst[28] = '\0';
}

/* =========================================================================
 * 内部结构
 * ====================================================================== */

#define WS_RECV_BUF_SIZE 65536
#define WS_SEND_BUF_SIZE 65536
#define WS_HTTP_BUF_SIZE 4096
#define WS_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

struct ws_client {
    ws_client_state_t        state;
    sock_t                   fd;

    /* 配置（shallow copy） */
    ws_client_cfg_t      cfg;
    char                     host[256];
    uint16_t                 port;
    char                     path[256];

    /* wslay */
    wslay_event_context_ptr  ws_ctx;

    /* 收发缓冲 */
    uint8_t                  recv_buf[WS_RECV_BUF_SIZE];
    size_t                   recv_buf_pos;
    size_t                   recv_buf_len;

    /* HTTP 握手缓冲（发送请求 + 接收响应） */
    char                     http_buf[WS_HTTP_BUF_SIZE];
    size_t                   http_buf_len;   /* 已接收字节数 */
    int                      http_sent;      /* 请求已发完标志 */
    size_t                   http_send_pos;  /* 请求发送进度 */
    size_t                   http_send_len;  /* 请求总长度 */

    /* 握手中生成的 expected accept key */
    char                     accept_key[32];
};

/* =========================================================================
 * socket 辅助
 * ====================================================================== */

static int set_nonblock(sock_t fd) {
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

/* =========================================================================
 * wslay 回调（客户端侧）
 * ====================================================================== */

static ssize_t wslay_recv_cb(wslay_event_context_ptr ctx,
                              uint8_t *buf, size_t len,
                              int flags, void *user_data) {
    (void)ctx; (void)flags;
    ws_client_t *c = (ws_client_t *)user_data;

    if (c->recv_buf_len > 0) {
        /* 从内部缓冲读取已收到的字节 */
        size_t copy = c->recv_buf_len < len ? c->recv_buf_len : len;
        memcpy(buf, c->recv_buf + c->recv_buf_pos, copy);
        c->recv_buf_pos += copy;
        c->recv_buf_len -= copy;
        return (ssize_t)copy;
    }

    /* 直接从 socket 读，循环处理 EINTR */
    ssize_t n;
    do {
#ifdef _WIN32
        n = (ssize_t)recv(c->fd, (char*)buf, (int)len, 0);
#else
        n = recv(c->fd, buf, len, 0);
#endif
    } while (n < 0 && SOCK_ERRNO == EINTR);
    if (n < 0) {
        int e = SOCK_ERRNO;
        if (e == EWOULDBLOCK_VAL) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
        return -1;
    }
    if (n == 0) {
        wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        return -1;
    }
    return n;
}

static ssize_t wslay_send_cb(wslay_event_context_ptr ctx,
                              const uint8_t *data, size_t len,
                              int flags, void *user_data) {
    (void)ctx; (void)flags;
    ws_client_t *c = (ws_client_t *)user_data;
    ssize_t n;
    do {
#ifdef _WIN32
        n = (ssize_t)send(c->fd, (const char*)data, (int)len, 0);
#else
        n = send(c->fd, data, len, 0);
#endif
    } while (n < 0 && SOCK_ERRNO == EINTR);
    if (n < 0) {
        int e = SOCK_ERRNO;
        if (e == EWOULDBLOCK_VAL) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
        return -1;
    }
    return n;
}

static int wslay_genmask_cb(wslay_event_context_ptr ctx,
                             uint8_t *buf, size_t len,
                             void *user_data) {
    (void)ctx; (void)user_data;
    /* 客户端必须发送 masked 帧，生成随机掩码 */
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand() & 0xFF);
    }
    return 0;
}

static void wslay_on_msg_recv_cb(wslay_event_context_ptr ctx,
                                  const struct wslay_event_on_msg_recv_arg *arg,
                                  void *user_data) {
    (void)ctx;
    ws_client_t *c = (ws_client_t *)user_data;

    if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
        c->state = WS_CLIENT_CLOSING;
        if (c->cfg.on_close) {
            c->cfg.on_close(c, arg->status_code, (const char*)arg->msg, c->cfg.user_data);
        }
        return;
    }

    if (!c->cfg.on_message) return;

    ws_msg_type_t type;
    switch (arg->opcode) {
        case WSLAY_TEXT_FRAME:   type = WS_MSG_TEXT;   break;
        case WSLAY_BINARY_FRAME: type = WS_MSG_BINARY; break;
        case WSLAY_PING:         type = WS_MSG_PING;   break;
        case WSLAY_PONG:         type = WS_MSG_PONG;   break;
        default: return;
    }
    c->cfg.on_message(c, type, arg->msg, arg->msg_length, c->cfg.user_data);
}

/* =========================================================================
 * 生命周期
 * ====================================================================== */

ws_client_t *ws_client_create(const ws_client_cfg_t *cfg) {
    ws_client_t *c = (ws_client_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->state = WS_CLIENT_CLOSED;
    c->fd    = INVALID_SOCK;
    if (cfg) c->cfg = *cfg;
    srand((unsigned)time(NULL));
    return c;
}

void ws_client_destroy(ws_client_t *c) {
    if (!c) return;
    if (c->ws_ctx) { wslay_event_context_free(c->ws_ctx); c->ws_ctx = NULL; }
    if (c->fd != INVALID_SOCK) { sock_close(c->fd); c->fd = INVALID_SOCK; }
    free(c);
}

ws_client_state_t ws_client_state(const ws_client_t *c) {
    return c ? c->state : WS_CLIENT_CLOSED;
}

/* =========================================================================
 * 连接发起
 * ====================================================================== */

int ws_client_connect(ws_client_t *c,
                           const char *host, uint16_t port,
                           const char *path) {
    if (!c || c->state != WS_CLIENT_CLOSED) return -1;

    strncpy(c->host, host, sizeof(c->host) - 1);
    c->port = port;
    strncpy(c->path, path ? path : "/", sizeof(c->path) - 1);

    /* 解析地址 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_aton(host, &addr.sin_addr) == 0) {
        /* 尝试 getaddrinfo 解析 */
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", port);
        if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;
        addr = *(struct sockaddr_in *)res->ai_addr;
        freeaddrinfo(res);
    }

    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd == INVALID_SOCK) return -1;

    if (set_nonblock(c->fd) != 0) {
        sock_close(c->fd); c->fd = INVALID_SOCK; return -1;
    }

    int r = connect(c->fd, (struct sockaddr *)&addr, sizeof(addr));
    if (r == 0) {
        /* 立即连通（本地环回等） */
        c->state = WS_CLIENT_HANDSHAKING;
    } else {
        int e = SOCK_ERRNO;
        if (e == EINPROGRESS_VAL) {
            c->state = WS_CLIENT_CONNECTING;
        } else {
            sock_close(c->fd); c->fd = INVALID_SOCK; return -1;
        }
    }

    /* 准备 HTTP Upgrade 请求 */
    /* 生成随机 16 字节 Sec-WebSocket-Key（base64 = 24 字节） */
    uint8_t ws_key_raw[16];
    for (int i = 0; i < 16; i++) ws_key_raw[i] = (uint8_t)(rand() & 0xFF);
    char ws_key_b64[25];
    /* 对 16 字节进行 base64 编码 */
    {
        static const char t[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int i, j;
        for (i = 0, j = 0; i < 15; i += 3) {
            uint32_t v = ((uint32_t)ws_key_raw[i]<<16)|((uint32_t)ws_key_raw[i+1]<<8)|ws_key_raw[i+2];
            ws_key_b64[j++]=t[(v>>18)&63]; ws_key_b64[j++]=t[(v>>12)&63];
            ws_key_b64[j++]=t[(v>>6)&63];  ws_key_b64[j++]=t[v&63];
        }
        /* 最后 1 字节（16 % 3 == 1） */
        { uint32_t v = (uint32_t)ws_key_raw[15] << 16;
          ws_key_b64[j++]=t[(v>>18)&63]; ws_key_b64[j++]=t[(v>>12)&63];
          ws_key_b64[j++]='='; ws_key_b64[j++]='='; }
        ws_key_b64[24] = '\0';
    }

    /* 计算期望的 accept key */
    {
        char combined[64];
        snprintf(combined, sizeof(combined), "%s" WS_WS_GUID, ws_key_b64);
        sha1_ctx_t sha;
        sha1_init(&sha);
        sha1_update(&sha, (const uint8_t*)combined, strlen(combined));
        uint8_t digest[20];
        sha1_final(&sha, digest);
        base64_encode_sha1(digest, c->accept_key);
    }

    /* 构造 HTTP Upgrade 请求 */
    int n = snprintf(c->http_buf, sizeof(c->http_buf),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s"
        "\r\n",
        c->path, c->host, c->port, ws_key_b64,
        c->cfg.extra_headers ? c->cfg.extra_headers : "");

    c->http_send_len = (size_t)n;
    c->http_send_pos = 0;
    c->http_sent     = 0;
    c->http_buf_len  = 0;

    return 0;
}

void ws_client_close(ws_client_t *c, uint16_t status_code) {
    if (!c || c->state != WS_CLIENT_OPEN) return;
    if (!c->ws_ctx) return;
    struct wslay_event_msg msg;
    uint8_t buf[2];
    buf[0] = (uint8_t)(status_code >> 8);
    buf[1] = (uint8_t)(status_code & 0xFF);
    msg.opcode     = WSLAY_CONNECTION_CLOSE;
    msg.msg        = buf;
    msg.msg_length = 2;
    wslay_event_queue_msg(c->ws_ctx, &msg);
    c->state = WS_CLIENT_CLOSING;
}

/* =========================================================================
 * 主循环驱动
 * ====================================================================== */

static void ws_client_finish_connect(ws_client_t *c) {
    /* 检查 TCP 连接是否完成 */
    int err = 0;
    socklen_t errlen = sizeof(err);
    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen) < 0 || err != 0) {
        c->state = WS_CLIENT_ERROR;
        sock_close(c->fd); c->fd = INVALID_SOCK;
        if (c->cfg.on_close) c->cfg.on_close(c, 0, "tcp connect failed", c->cfg.user_data);
        return;
    }
    c->state = WS_CLIENT_HANDSHAKING;
}

static void ws_client_do_handshake(ws_client_t *c) {
    /* 发送 HTTP 请求 */
    if (!c->http_sent) {
        while (c->http_send_pos < c->http_send_len) {
#ifdef _WIN32
            int n = send(c->fd, c->http_buf + c->http_send_pos,
                         (int)(c->http_send_len - c->http_send_pos), 0);
#else
            ssize_t n = send(c->fd, c->http_buf + c->http_send_pos,
                              c->http_send_len - c->http_send_pos, 0);
#endif
            if (n <= 0) {
                int e = SOCK_ERRNO;
                if (e == EWOULDBLOCK_VAL) return;  /* 下次继续 */
                c->state = WS_CLIENT_ERROR;
                return;
            }
            c->http_send_pos += (size_t)n;
        }
        c->http_sent = 1;
        c->http_buf_len = 0;  /* 清空，准备接收响应 */
        c->http_buf[0] = '\0';
    }

    /* 接收 HTTP 响应 */
    while (c->http_buf_len < sizeof(c->http_buf) - 1) {
#ifdef _WIN32
        int n = recv(c->fd, c->http_buf + c->http_buf_len,
                     (int)(sizeof(c->http_buf) - 1 - c->http_buf_len), 0);
#else
        ssize_t n = recv(c->fd, c->http_buf + c->http_buf_len,
                          sizeof(c->http_buf) - 1 - c->http_buf_len, 0);
#endif
        if (n <= 0) {
            int e = SOCK_ERRNO;
            if (e == EWOULDBLOCK_VAL) break;   /* 下次继续 */
            c->state = WS_CLIENT_ERROR;
            return;
        }
        c->http_buf_len += (size_t)n;
        c->http_buf[c->http_buf_len] = '\0';

        /* 检查头部是否接收完毕 */
        if (strstr(c->http_buf, "\r\n\r\n")) break;
    }

    /* 检查是否收到完整响应头 */
    if (!strstr(c->http_buf, "\r\n\r\n")) return;  /* 还没收完 */

    /* 验证 101 Switching Protocols */
    if (!strstr(c->http_buf, "101") || !strstr(c->http_buf, "Upgrade")) {
        c->state = WS_CLIENT_ERROR;
        sock_close(c->fd); c->fd = INVALID_SOCK;
        if (c->cfg.on_close) c->cfg.on_close(c, 0, "handshake rejected", c->cfg.user_data);
        return;
    }

    /* 验证 Sec-WebSocket-Accept */
    const char *accept_str = strstr(c->http_buf, "Sec-WebSocket-Accept:");
    if (accept_str) {
        accept_str += 21;  /* 跳过字段名和冒号 */
        while (*accept_str == ' ') accept_str++;
        /* 比较 28 字节 base64 */
        if (strncmp(accept_str, c->accept_key, 28) != 0) {
            c->state = WS_CLIENT_ERROR;
            sock_close(c->fd); c->fd = INVALID_SOCK;
            if (c->cfg.on_close) c->cfg.on_close(c, 0, "accept key mismatch", c->cfg.user_data);
            return;
        }
    }

    /* 握手成功，初始化 wslay 客户端上下文 */
    static const struct wslay_event_callbacks cbs = {
        wslay_recv_cb,
        wslay_send_cb,
        wslay_genmask_cb,
        NULL, NULL, NULL,
        wslay_on_msg_recv_cb
    };
    if (wslay_event_context_client_init(&c->ws_ctx, &cbs, c) != 0) {
        c->state = WS_CLIENT_ERROR;
        return;
    }

    c->state = WS_CLIENT_OPEN;

    /* 检查响应头之后是否有残余数据（已属于 WebSocket 帧）*/
    const char *body_start = strstr(c->http_buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t leftover = c->http_buf_len - (size_t)(body_start - c->http_buf);
        if (leftover > 0 && leftover <= WS_RECV_BUF_SIZE) {
            memcpy(c->recv_buf, body_start, leftover);
            c->recv_buf_pos = 0;
            c->recv_buf_len = leftover;
        }
    }
    c->http_buf_len = 0;

    if (c->cfg.on_open) c->cfg.on_open(c, c->cfg.user_data);
}

static void ws_client_do_ws(ws_client_t *c) {
    if (!c->ws_ctx) return;

    /* 读取新到达的 socket 数据到 recv_buf */
    if (c->recv_buf_len == 0) {
        c->recv_buf_pos = 0;
#ifdef _WIN32
        int n = recv(c->fd, (char*)c->recv_buf, WS_RECV_BUF_SIZE, 0);
#else
        ssize_t n = recv(c->fd, c->recv_buf, WS_RECV_BUF_SIZE, 0);
#endif
        if (n > 0) {
            c->recv_buf_len = (size_t)n;
        } else if (n == 0) {
            /* 对端关闭 */
            c->state = WS_CLIENT_CLOSED;
            if (c->cfg.on_close) c->cfg.on_close(c, 1001, "eof", c->cfg.user_data);
            return;
        }
        /* n < 0 且 EWOULDBLOCK：无新数据，忽略 */
    }

    if (wslay_event_want_read(c->ws_ctx)) {
        wslay_event_recv(c->ws_ctx);
    }
    if (wslay_event_want_write(c->ws_ctx)) {
        wslay_event_send(c->ws_ctx);
    }

    /* 检查 wslay 是否已进入关闭状态 */
    if (!wslay_event_want_read(c->ws_ctx) && !wslay_event_want_write(c->ws_ctx)) {
        if (c->state == WS_CLIENT_CLOSING || c->state == WS_CLIENT_OPEN) {
            c->state = WS_CLIENT_CLOSED;
            sock_close(c->fd); c->fd = INVALID_SOCK;
            wslay_event_context_free(c->ws_ctx); c->ws_ctx = NULL;
        }
    }
}

void ws_client_update(ws_client_t *c) {
    if (!c) return;
    switch (c->state) {
        case WS_CLIENT_CONNECTING:  ws_client_finish_connect(c); break;
        case WS_CLIENT_HANDSHAKING: ws_client_do_handshake(c);   break;
        case WS_CLIENT_OPEN:
        case WS_CLIENT_CLOSING:     ws_client_do_ws(c);           break;
        default: break;
    }
}

/* =========================================================================
 * 发送
 * ====================================================================== */

int ws_client_send_text(ws_client_t *c, const char *text) {
    if (!c || c->state != WS_CLIENT_OPEN || !c->ws_ctx) return -1;
    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_TEXT_FRAME;
    msg.msg        = (const uint8_t *)text;
    msg.msg_length = strlen(text);
    return wslay_event_queue_msg(c->ws_ctx, &msg) == 0 ? 0 : -1;
}

int ws_client_send_binary(ws_client_t *c,
                               const uint8_t *data, size_t len) {
    if (!c || c->state != WS_CLIENT_OPEN || !c->ws_ctx) return -1;
    struct wslay_event_msg msg;
    msg.opcode     = WSLAY_BINARY_FRAME;
    msg.msg        = data;
    msg.msg_length = len;
    return wslay_event_queue_msg(c->ws_ctx, &msg) == 0 ? 0 : -1;
}

#endif /* WITH_WSLAY */
