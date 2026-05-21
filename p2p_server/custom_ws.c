//
// custom_ws.c — 基于 custom_tcp 的 WebSocket 服务端基础封装实现
//
// 握手阶段：流模式，以 "\r\n\r\n" 为边界扫描完整 HTTP header
// 正常阶段：帧模式，14 字节静态缓冲，动态扩展解析 WS 帧头（RFC 6455）
//
// WS 帧头布局（服务端接收）：
//   byte0:  FIN(1) RSV(3) opcode(4)
//   byte1:  MASK(1) payload_len(7)   — 服务端接收时 MASK 必须为 1
//   [2..3]:  扩展 payload_len=126 时的 16bit 长度
//   [2..9]:  扩展 payload_len=127 时的 64bit 长度
//   [2..5] 或 [4..7] 或 [10..13]: mask key (4 bytes)
//
// 服务端发送帧头布局（无 mask）：
//   byte0:  FIN(1) RSV(3) opcode(4)
//   byte1:  0 payload_len(7)
//   [2..3]:  扩展 payload_len=126 时
//   [2..9]:  扩展 payload_len=127 时
//
#define MOD_TAG "CUSTOM_WS"

#include "custom_ws.h"

ARGS(client_timeout);

// HTTP 握手 recv 缓冲大小（存放 HTTP 请求 header，流模式 recv_buf 使用 2K）
#define CW_HTTP_BUF_FLAGS           BUF_FLAG_2048(0)

// WS 帧头最大字节数
#define CW_WS_HDR_MAX               10

// 静态帧标识。此时数据包的 hdr 从 buf[0] 开始。对应的 pos 表示 payload 数据的起点
#define CW_BUF_FLAG_STATIC          0x4

static uint8_t                      s_hdr_sizes[4] = { 0, 2, 4, 10 };

///////////////////////////////////////////////////////////////////////////////

// WS GUID（RFC 6455）
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// http 握手处理，返回 HTTP 响应文本（不含 "\r\n\r\n" 尾）
static ret_t cw_http_accept(const char *req, char *resp_buf, const char *sub_protocol);

// UTF-8 验证（RFC 6455 §8.1：文本帧 payload 必须是合法 UTF-8）
// 使用 Bjoern Hoehrmann 的 DFA（https://bjoern.hoehrmann.de/utf-8/decoder/dfa/）
// state=0（WS_UTF8_ACCEPT）表示已完成一个合法序列；state=12 表示非法；其余为中间态
#define WS_UTF8_ACCEPT 0u
#define WS_UTF8_REJECT 12u

// 逐字节推进 DFA；返回 false 表示遇到非法 UTF-8（state 已变为 REJECT）
static bool cw_utf8_check(uint32_t *state, const uint8_t *data, uint32_t len);

// RFC 6455 §7.4.1: close 状态码合法性（1000-1011 不含 1004/1005/1006，或 3000-4999）
static bool cw_valid_close_code(uint16_t c) {
    return (c >= 1000 && c <= 1011 && c != 1004 && c != 1005 && c != 1006) ||
           (c >= 3000 && c <= 4999);
}

static void cw_unmask(uint8_t *buf, uint32_t len, const uint8_t mask[4]) {
    uint32_t i;
    for (i = 0; i + 4 <= len; i += 4) {
        buf[i]   ^= mask[0];
        buf[i+1] ^= mask[1];
        buf[i+2] ^= mask[2];
        buf[i+3] ^= mask[3];
    }
    for (; i < len; i++) buf[i] ^= mask[i & 3];
}

static void cw_unmask2(uint8_t *buf1, uint32_t len1, const uint8_t *buf0, uint32_t len0, const uint8_t mask[4]) {
    uint32_t i = 0;
    for (; i + 4 <= len0; i += 4) {
        buf1[i]   = buf0[i]   ^ mask[0];
        buf1[i+1] = buf0[i+1] ^ mask[1];
        buf1[i+2] = buf0[i+2] ^ mask[2];
        buf1[i+3] = buf0[i+3] ^ mask[3];
    }
    if (i >= len0) cw_unmask(buf1 + len0, len1 - len0, mask);
    else {
        for (; i < len0; i++) buf1[i] = buf0[i] ^ mask[i & 3];
        for (; i < len1; i++) buf1[i] ^= mask[i & 3];
    }
}

///////////////////////////////////////////////////////////////////////////////
// WS 帧头解析（帧模式 resolve_payload_len）

static ret_t cw_resolve_payload_len(ct_client_t *client, uint8_t *hdr_buf, uint16_t hdr_len,
                                    uint32_t *payload_len, uint16_t *payload_offset) {
    
    // HTTP 握手阶段没有 payload
    if (client->handshake == TCP_HS_FLAG_HANDSHAKING) { *payload_len = 0; *payload_offset = 0; return E_NONE; }
    
    uint8_t len7 = hdr_buf[1] & 0x7F;

    // 首次默认 base hdr 为 2 字节
    if (hdr_len == 2) {

        // 作为服务端，必须携带 mask 信息，即需要执行解掩码操作（RFC 6455 §5.3）
        if (!(hdr_buf[1] & 0x80)) return E_INVALID;

        // 动态扩展 hdr_sz 为完整帧头大小（含扩展长度和 mask key）
        //   初始 hdr_sz=2 → 读完 2 字节后调用：
        //     payload_len7 == 126 → 需要再读 2 字节扩展长度 + 4 字节 mask → hdr_sz=8
        //     payload_len7 == 127 → 需要再读 8 字节扩展长度 + 4 字节 mask → hdr_sz=14
        //     其他                 → 需要再读 4 字节 mask → hdr_sz=6
        if (len7 == 126) client->hdr_sz = 8;        // 2 + 2(ext/int16) + 4(mask)
        else if (len7 == 127) client->hdr_sz = 14;  // 2 + 8(ext/int64) + 4(mask)
        else client->hdr_sz = 6;                    // 2 + 4(mask)

        return 1;  // 需要继续读取
    }

    // 第二次调用：已读完 mask key 和扩展长度
    uint8_t mask_off;
    if      (len7 == 127) { *payload_len = (uint32_t)nget_ll(hdr_buf + 2); mask_off = 10; }
    else if (len7 == 126) { *payload_len = nget_s(hdr_buf + 2);            mask_off = 4;  }
    else                  { *payload_len = len7;                           mask_off = 2;  }

    // 保存 mask key 到固定位置（hdr_buf[10..13] 作为 mask key 暂存区，方便 handle_proto 统一从此读取）
    // + 由于 mask_off == 10 与 mask_off == 4/2 + 4 区间不重合，可以直接复制
    if (mask_off != 10) {
        hdr_buf[10] = hdr_buf[mask_off];
        hdr_buf[11] = hdr_buf[mask_off+1];
        hdr_buf[12] = hdr_buf[mask_off+2];
        hdr_buf[13] = hdr_buf[mask_off+3];
    }

    // 这里默认为上层零拷贝转发预留 WS 头空间
    *payload_offset = 10;
    return E_NONE;
}

static int16_t ws_proto(ct_client_ctx_t *ctx, ct_client_t *c,
                         const uint8_t *hdr_buf, uint16_t hdr_len,
                         buf16_item_t *payload0, buf16_item_t *payload1,
                         uint8_t** r_payload, uint32_t* r_payload_len) { (void)hdr_len;
    cw_client_t *client = (cw_client_t*)c;


    uint8_t opcode = hdr_buf[0] & 0x0F;         // Byte[0] bit0-3: opcode
    uint8_t fin    = (hdr_buf[0] >> 7) & 1;     // Byte[0] bit7: FIN
    uint8_t masked = hdr_buf[1] & 0x80;         // Byte[1] bit7: MASK

    // 重置帧模式 hdr_sz 为 2（下一帧）
    c->hdr_sz = 2;

    // RFC 6455 §5.2: RSV1/2/3 必须为 0（未协商任何扩展时）
    if (hdr_buf[0] & 0x70) {                    // Byte[0] bit4-6: RSV1/2/3
        print("E:", LA_F("[WS] RSV bit set in opcode %u\n", LA_F255, 255), opcode);
        return WS_CLOSE_PROTOCOL_ERROR;
    }

    // 服务端要求客户端帧必须有 mask（RFC 6455 §5.3）
    if (!masked) {
        print("E:", LA_F("[WS] Client frame missing mask\n", LA_F249, 249));
        ct_client_error(ctx, c, WS_CLOSE_PROTOCOL_ERROR, false);
        return WS_CLOSE_PROTOCOL_ERROR;
    }

    // RFC 6455 §5.2: 保留 opcode（0x3-0x7 数据帧、0xB-0xF 控制帧）必须拒绝
    if ((opcode >= 0x3 && opcode <= 0x7) || opcode >= 0xB) {
        print("E:", LA_F("[WS] Reserved opcode %u\n", LA_F256, 256), opcode);
        return WS_CLOSE_PROTOCOL_ERROR;
    }

    // RFC 6455 §5.4: 分片状态机一致性
    // + CONTINUATION 帧必须在 TEXT/BINARY 分片序列进行中（ws_opcode != 0）
    // + 新 TEXT/BINARY 帧不可在已有分片序列进行中出现
    if (client->ws_opcode != 0) {
        if (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY) {
            print("E:", LA_F("[WS] New %s without fragmentation end\n", LA_F254, 254), opcode == WS_OP_TEXT ? "TEXT" : "BINARY");
            return WS_CLOSE_PROTOCOL_ERROR;
        }
    } else if (opcode == WS_OP_CONTINUATION) {
        print("E:", LA_F("[WS] CONTINUATION frame without fragmentation going\n", LA_F248, 248));
        return WS_CLOSE_PROTOCOL_ERROR;
    }

    // RFC 6455 §5.5: 控制帧必须 FIN，且 payload ≤ 125 字节
    // + 这里 hdr len > 6 等价于 payload len > 125 字节
    if (opcode >= WS_OP_CLOSE && (!fin || hdr_len > 6)) {
        print("E:", LA_F("[WS] Invalid control frame: opcode=%u fin=%u hdr_len=%u\n", LA_F253, 253), opcode, fin, hdr_len - 4);
        return WS_CLOSE_PROTOCOL_ERROR;
    }

    // 解 mask（in-place）
    // + mask key 已被 cw_resolve_payload_len 统一复制到 hdr_buf[10..13]
    //   另外，直接将 payload0 的数据归并到 payload1（如果需要）
    uint8_t *payload; uint32_t payload_len; const uint8_t *mask = hdr_buf + 10;
    if (payload1) {
        payload = ITEM2BUF(payload1) + payload1->pos;
        payload_len = payload1->len - payload1->pos;
        if (payload0)
            cw_unmask2(payload, payload_len,
                  ITEM2BUF(payload0) + payload0->pos,
                  payload0->len - payload0->pos, mask);
        else cw_unmask(payload, payload_len, mask);
    } else if (payload0) {
        payload = ITEM2BUF(payload0) + payload0->pos;
        payload_len = payload0->len - payload0->pos;
        cw_unmask(payload, payload_len, mask);
    } else { payload = NULL; payload_len = 0; }

    // 对于控制帧处理（control frames: close/ping/pong）
    if (opcode >= WS_OP_CLOSE) {

        if (opcode == WS_OP_CLOSE) {

            uint16_t code = 1000;
            if (payload) {

                // 读取状态码
                if (payload_len >= 2) { code = (uint16_t)((payload[0]<<8)|payload[1]);

                    // RFC 6455 §7.4.1: 状态码必须合法
                    if (!cw_valid_close_code(code)) {
                        print("E:", LA_F("[WS] Invalid close code %u\n", LA_F252, 252), code);
                        return WS_CLOSE_PROTOCOL_ERROR;
                    }
                }

                // RFC 6455 §5.5.1: close frame reason string（第 2 字节起）必须是合法 UTF-8
                if (payload_len > 2) {
                    uint32_t tmp = WS_UTF8_ACCEPT;
                    if (!cw_utf8_check(&tmp, payload + 2, payload_len - 2) || tmp != WS_UTF8_ACCEPT) {
                        print("E:", LA_F("[WS] Invalid UTF-8 in close reason\n", LA_F251, 251));
                        return WS_CLOSE_PROTOCOL_ERROR;
                    }
                }
            }

            if (c->handshake) return WS_CLOSE_NORMAL;

            if (((cw_client_ctx_t*)ctx)->handle_close)
                ((cw_client_ctx_t*)ctx)->handle_close((cw_client_ctx_t*)ctx, client, code);

            // 自动回复 close
            buf16_item_t* close_frame = (buf16_item_t*)client->ws_close_frame_buf;
            if (close_frame->refer) close_frame = NULL;
            else { close_frame->refer = ITEM_REF_STATIC;
                uint8_t* ptr = ITEM2BUF(close_frame) + 2;
                nwrite_s(ptr, code);
            }

            // 交由基类统一执行关闭收口：停止接收、清理 sessions / recv_buf，并保持 close 帧继续发送
            ct_client_off(ctx, (ct_client_t*)client, close_frame);
        }
        else {

            if (c->handshake) return WS_CLOSE_PROTOCOL_ERROR;

            if (opcode == WS_OP_PING) {

                if (((cw_client_ctx_t*)ctx)->handle_ping)
                    ((cw_client_ctx_t*)ctx)->handle_ping((cw_client_ctx_t*)ctx, client, payload, payload_len);

                // 自动回复 pong，携带相同 payload

                // 如果可以 zero-copy 直接转发
                if (payload1) {
                    if (cw_build_frame(WS_OP_PONG, payload1) == E_NONE)
                        cw_client_send(client, payload1, true);
                }
                else {
                    buf16_item_t *pong = alloc_buf16(BUF_FLAG_128(0));
                    if (pong) {
                        uint8_t *pb = ITEM2BUF(pong);
                        pb[0] = 0x8A;  // FIN | PONG
                        pb[1] = payload_len;
                        if (payload_len && payload) memcpy(pb + 2, payload, payload_len);
                        pong->len = (uint16_t)(2 + payload_len);
                        ct_client_send(c, pong, true);  // 高优先级发送
                    }
                }
            }

            // 对于 pong 帧，框架无需处理
        }

        return 0;
    }

    // RFC 6455 §8.1：TEXT 帧（含分片）payload 必须是合法 UTF-8，逐片增量验证
    // + opcode==TEXT 时重置 DFA（新消息），CONTINUATION 时沿用上片状态跨片累积
    if ((opcode == WS_OP_TEXT) ||
        (opcode == WS_OP_CONTINUATION && client->ws_opcode == WS_OP_TEXT)) {
        if (opcode == WS_OP_TEXT) client->ws_utf8state = WS_UTF8_ACCEPT;        // 新消息重置
        bool ok = !payload_len || cw_utf8_check(&client->ws_utf8state, payload, payload_len);
        if (ok && fin && client->ws_utf8state != WS_UTF8_ACCEPT) ok = false;    // FIN 时要确保 UTF-8 序列完整，即不能截断于多字节序列中间
        if (!ok) {
            print("E:", LA_F("[WS] Invalid UTF-8 in TEXT frame\n", LA_F250, 250));
            return WS_CLOSE_PROTOCOL_ERROR;
        }
    }

    // 对于片段帧（text/binary/continuation）
    if (opcode == WS_OP_CONTINUATION || !fin) {

        if (c->handshake) return WS_CLOSE_PROTOCOL_ERROR;

        // 第一个分片：
        if (opcode != WS_OP_CONTINUATION) client->ws_opcode = opcode;   // 记录起始帧 opcode

        // 聚合到 ws_frag_q
        if (payload_len) { client->ws_frag_len += payload_len;

            if (payload1) { c->payload_buf = NULL;                      // 所有权已转移，避免框架释放
                BUF_Q_APPEND(&client->ws_frag_q, payload1);
            } else { assert(payload0);
                buf16_item_t *p0copy = alloc_buf16(BUF_FLAGS(buffer_sz_flag(payload_len ? payload_len : 1), 0));
                if (!p0copy) goto oom;
                memcpy(ITEM2BUF(p0copy), ITEM2BUF(payload0) + payload0->pos, payload_len);
                p0copy->len = (uint16_t)payload_len;
                p0copy->pos = 0;
                BUF_Q_APPEND(&client->ws_frag_q, p0copy);
            }
        }

        // 最后一个分片，返回 opcode
        *r_payload = NULL; *r_payload_len = client->ws_frag_len;
        return fin ? client->ws_opcode : 0;
    }

    // 完整帧（非分片）
    *r_payload = payload; *r_payload_len = payload_len;
    return opcode;

oom:
    print("E:", LA_F("[WS] OOM in fragment reassembly\n", LA_F226, 226));
    if (client->ws_frag_q.head) {
        BUF_Q_CLEAR(&client->ws_frag_q, it, free_buf16(it););
        client->ws_frag_len = 0;
    }
    return -1;
}

#define ACCEPT_BUF_SZ   256

// 握手阶段（HTTP Upgrade 请求）处理
// + hdr_buf: recv_buf 中 HTTP 请求文本（不含 "\r\n\r\n" 尾）
// + hdr_len: HTTP header 文本长度
// + payload0/payload1: HTTP header 之后不应有 payload（WS 协议握手阶段不携带 body）
static buf16_item_t *cw_tcp_handle_handshake(ct_client_ctx_t* ctx, ct_client_t *c,
                                             uint8_t *hdr_buf, uint16_t hdr_len,
                                             buf16_item_t *payload0, buf16_item_t *payload1) {
    cw_client_t *client = (cw_client_t*)c;

    if (c->handshake == TCP_HS_FLAG_HANDSHAKING) { assert(payload0 == NULL && payload1 == NULL);  // HTTP 握手阶段不应有 payload

        payload0 = alloc_buf16(BUF_FLAG_256(0));
        if (!payload0) {
            c->last_error = CUSTOM_TCP_ERR_INTERNAL;
            return NULL;
        }
        uint8_t *resp_buf = ITEM2BUF(payload0);

        hdr_buf[hdr_len] = '\0';
        ret_t r = cw_http_accept((const char*)hdr_buf, (char*)resp_buf, ((cw_client_ctx_t*)ctx)->sub_protocol);
        if (r < E_NONE) {
            print("E:", LA_F("[WS] HTTP accept rejected(%d)\n", LA_F143, 143));
            c->last_error = CUSTOM_TCP_ERR_PROTOCOL;
            free_buf16(payload0);
            return NULL;
        }
        payload0->len = (uint16_t)r;
    }
    else if (c->handshake > TCP_HS_FLAG_HANDSHAKING) {
        assert(((cw_client_ctx_t*)ctx)->handle_handshake);

        uint8_t* payload; uint32_t payload_len;
        uint16_t code = ws_proto(ctx, c, client->ws_hdr_buf, client->hdr_sz, payload0, payload1, &payload, &payload_len);
        assert(code > 0);

        if (code >= WS_CLOSE_NORMAL) {
            client->last_error = (int16_t)code;
            return NULL;
        }

        assert(client->ws_opcode == 0);  // 握手阶段不应有分片帧

        payload0 = ((cw_client_ctx_t*)ctx)->handle_handshake((cw_client_ctx_t*)ctx, client, code, payload, payload_len, payload1);
        if (payload0) {
            if (payload0 == payload1) client->payload_buf = NULL;  // 所有权已转移，避免框架释放

            P_check(!BUF_IS_32BIT(payload0->flags),
                print("E:", LA_F("[WS] handshake ack must stay 16-bit\n", 0, 0));
                free_buffer(payload0);
                c->last_error = CUSTOM_TCP_ERR_INTERNAL;
                return NULL;
            )

            P_check(payload0->pos <= payload0->len,
                print("E:", LA_F("[WS] invalid handshake ack pos=%u len=%u\n", 0, 0), payload0->pos, payload0->len);
                free_buf16(payload0);
                c->last_error = CUSTOM_TCP_ERR_INTERNAL;
                return NULL;
            )

            uint8_t hdr_sz = s_hdr_sizes[payload0->flags & CW_BUF_FLAG_HDR_SIZE];
            P_check(hdr_sz,
                print("E:", LA_F("[WS] invalid handshake ack hdr_sz=%u pos=%u\n", 0, 0), hdr_sz, payload0->pos);
                free_buf16(payload0);
                c->last_error = CUSTOM_TCP_ERR_INTERNAL;
                return NULL;
            )

            if (payload0->flags & CW_BUF_FLAG_STATIC)
                payload0->pos = 0;
        }
    }
    else assert(false);

    return payload0;
}

static void cw_tcp_handshake_finish(ct_client_ctx_t *ctx, ct_client_t *c) {
    cw_client_t *client = (cw_client_t*)c;

    if (c->handshake > TCP_HS_FLAG_HANDSHAKING) {
        c->handshake = 0;
        return;
    }

    // 切换到帧模式
    // + 将 recv_buf 设为 NULL 触发框架切换
    c->recv_buf = NULL;

    // 设置帧模式参数
    c->hdr_rs = client->ws_hdr_buf;     // 14 字节静态帧头缓冲
    c->hdr_sz = 2;                      // 初始读取 2 字节基础帧头

    // 如果需要，执行应用层的握手处理
    if (((cw_client_ctx_t*)ctx)->handle_handshake)
        c->handshake = TCP_HS_FLAG_HANDSHAKING + 1;

    // 结束握手阶段，进入正常帧收发阶段
    else c->handshake = 0;
}

// NOLINTNEXTLINE(readability-non-const-parameter)
static void cw_tcp_handle_proto(ct_client_ctx_t *ctx, ct_client_t *c, uint8_t *hdr_buf, uint16_t hdr_len,
                                buf16_item_t *payload0, buf16_item_t *payload1) { (void)hdr_buf;
    (void)hdr_len;
    cw_client_t *client = (cw_client_t*)c;
    cw_client_ctx_t *context = (cw_client_ctx_t*)ctx;

    uint8_t* payload; uint32_t payload_len;
    int16_t code = ws_proto(ctx, c, client->ws_hdr_buf, client->hdr_sz, payload0, payload1, &payload, &payload_len);
    if (!code) return;

    if (code < 0) {
        ct_client_error(ctx, c, WS_CLOSE_INTERNAL_ERROR, true);
        return;
    }
    if (code >= WS_CLOSE_NORMAL) {
        ct_client_error(ctx, c, code, false);
        return;
    }

    if (payload == NULL) {
        if (context->handle_frame) {
            context->handle_frame(context, client, client->ws_opcode, NULL, client->ws_frag_len, client->ws_frag_q.head);
            client->ws_frag_q.head = client->ws_frag_q.rear = NULL;
        } else { BUF_Q_CLEAR(&client->ws_frag_q, it, free_buf16(it);); }
        client->ws_frag_len = 0; client->ws_opcode = 0;
    } else if (context->handle_frame)
        context->handle_frame(context, client, code, payload, payload_len, payload1);
}

///////////////////////////////////////////////////////////////////////////////
// Public API

// 构造并发送 WS 帧头（服务端，无 mask）
// + buf_item: 上层分配的缓冲，[0, payload_pos) 为预留空间，[payload_pos, len) 为 payload
// + payload_pos 必须 >= 实际帧头长度（由 payload 大小决定）
buf16_item_t *cw_alloc_frame(uint8_t opcode, uint32_t payload_len) {

    uint8_t hdr_sz; uint8_t hdr[10]; uint8_t frame_flag;
    hdr[0] = 0x80 | (opcode & 0x0F);
    if (payload_len <= 125) { hdr_sz = 2; frame_flag = CW_BUF_HDR_2;
        hdr[1] = (uint8_t)payload_len;
    } else if (payload_len <= 65535) { hdr_sz = 4; frame_flag = CW_BUF_HDR_4;
        hdr[1] = 126;
        hdr[2] = (uint8_t)(payload_len >> 8);
        hdr[3] = (uint8_t)payload_len;
    } else { hdr_sz = 10; frame_flag = CW_BUF_HDR_10;
        hdr[1] = 127;
        hdr[2] = 0; hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;
        hdr[6] = (uint8_t)(payload_len >> 24);
        hdr[7] = (uint8_t)(payload_len >> 16);
        hdr[8] = (uint8_t)(payload_len >> 8);
        hdr[9] = (uint8_t)payload_len;
    }

    payload_len += (uint32_t)hdr_sz;

    buf16_item_t *item = alloc_buffer(frame_flag|CW_BUF_FLAG_STATIC, payload_len);
    if (!item) return NULL;

    uint8_t *buf = ITEM2BUF(item);
    memcpy(buf, hdr, hdr_sz);
    if (BUF_IS_32BIT(item->flags)) {
        BUF32(item)->pos = hdr_sz;
        BUF32(item)->len = payload_len;
    } else {
        item->pos = hdr_sz;
        item->len = (uint16_t)payload_len;
    }
    return item;
}

buf16_item_t *cw_vprintf_frame(uint32_t expect_sz, const char *fmt, va_list args) {
    P_check(expect_sz, return NULL;)

    buf16_item_t *item = alloc_buffer(0, 10u + expect_sz);
    if (!item) return NULL;

    int n = vsnprintf((char*)ITEM2BUF(item) + 10, (size_t)expect_sz, fmt, args);
    if (n < 0) {
        free_buffer(item);
        return NULL;
    }

    uint32_t text_len = (uint32_t)n;
    if (text_len >= expect_sz) text_len = expect_sz - 1;

    if (BUF_IS_32BIT(item->flags)) {
        BUF32(item)->pos = 10;
        BUF32(item)->len = 10u + text_len;
    } else {
        item->pos = 10;
        item->len = (uint16_t)(10u + text_len);
    }

    if (cw_build_frame(WS_OP_TEXT, item) != E_NONE) return NULL;
    return item;
}

// 构造一个 ws frame：帧头信息、以及 payload 数据在 buf 中的起始位置（payload_offset 的前面是帧头数据）
ret_t cw_build_frame(uint8_t opcode, buf16_item_t *buf_item) {

    uint8_t *buf; uint32_t len; uint16_t* pos_ptr;
    if (BUF_IS_32BIT(buf_item->flags)) {
        buf = ITEM2BUF(BUF32(buf_item)); len = BUF32(buf_item)->len; pos_ptr = &BUF32(buf_item)->pos;
    } else {
        buf = ITEM2BUF(buf_item); len = buf_item->len; pos_ptr = &buf_item->pos;
    }
    uint32_t payload_len = len - *pos_ptr;

    uint8_t hdr_sz; uint8_t hdr[10]; uint8_t frame_flag;
    hdr[0] = 0x80 | (opcode & 0x0F);   // FIN=1, RSV=0, opcode
    if (payload_len <= 125) { hdr_sz = 2; frame_flag = CW_BUF_HDR_2;
        hdr[1] = (uint8_t)payload_len;
    } else if (payload_len <= 65535) { hdr_sz = 4; frame_flag = CW_BUF_HDR_4;
        hdr[1] = 126;
        hdr[2] = (uint8_t)(payload_len >> 8);
        hdr[3] = (uint8_t)(payload_len);
    } else { hdr_sz = 10; frame_flag = CW_BUF_HDR_10;
        hdr[1] = 127;
        hdr[2] = 0; hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;
        hdr[6] = (uint8_t)(payload_len >> 24);
        hdr[7] = (uint8_t)(payload_len >> 16);
        hdr[8] = (uint8_t)(payload_len >> 8);
        hdr[9] = (uint8_t)(payload_len);
    }

    if (buf_item->pos < hdr_sz) {
        print("E:", LA_F("[WS] bad payload pos(%u) < hdr_sz(%u)\n", LA_F228, 228), buf_item->pos, hdr_sz);
        free_buffer(buf_item);
        return E_INVALID;
    }

    // 将帧头写入 payload 前的预留空间末尾（紧靠 payload），但保持 pos 仍指向 payload
    buf_item->flags = (uint8_t)((buf_item->flags & (uint8_t)~CW_BUF_FLAG_HDR_SIZE) | frame_flag);
    *pos_ptr = (uint16_t)(*pos_ptr - hdr_sz);
    memcpy(buf + (*pos_ptr), hdr, hdr_sz);

    return E_NONE;
}

void
cw_client_send(cw_client_t *client, buf16_item_t *frame, bool immediate) {

    if (frame->flags & CW_BUF_FLAG_STATIC)
        frame->pos = 0;

    ct_client_send((ct_client_t*)client, frame, immediate);
}

void
cw_session_send(ct_session_t *session, buf16_item_t *frame) {

    if (frame->flags & CW_BUF_FLAG_STATIC)
        frame->pos = 0;

    ct_session_send(session, frame);
}

ret_t cw_close(cw_client_t *client, uint16_t code, const char* reason/* nullable */) {

    clear_sessions((client_t*)client, false);

    if (!code) code = WS_CLOSE_NORMAL;

    ret_t r = E_NONE;
    if (reason && *reason) {
        size_t reason_len = strlen(reason);
        if (reason_len > 123u) reason_len = 123u;

        buf16_item_t *frame = cw_alloc_frame(WS_OP_CLOSE, 2 + reason_len);
        if (frame) {
            uint8_t *buf = ITEM2BUF(frame) + frame->pos;
            nwrite_s(buf, code);
            if (reason_len) memcpy(buf + 2, reason, reason_len);

            cw_client_send(client, frame, false);
        }  else r = E_OUT_OF_MEMORY;

        code = WS_CLOSE_INTERNAL_ERROR;
    }

    buf16_item_t* close_frame = (buf16_item_t*)client->ws_close_frame_buf;
    if (!close_frame->refer) {
        uint8_t *buf = ITEM2BUF(close_frame) + 2;
        nwrite_s(buf, code);
        close_frame->refer = ITEM_REF_STATIC;
        ct_client_send((ct_client_t*)client, close_frame, false);
    }

    client->io |= CW_IO_FLAG_CLOSING;
    client->base.last_active = P_tick_ms();

    return r;
}

void cw_retry_closing(cw_client_t *client, uint64_t now) {
    if ((client->io & CW_IO_FLAG_CLOSING) &&
        tick_diff(now, client->base.last_active) >= (uint64_t)ARGS_client_timeout.i64 * 1000u) {
        print("I:", LA_F("[WS] close timeout, force closing\n", LA_F192, 192));
        free_client(&client->base);
    }
}

///////////////////////////////////////////////////////////////////////////////
// custom_tcp_ctx_t 初始化助手
// + 将 WS 回调绑定到 tcp ctx，供 cw_init_client 调用方使用（一次性初始化）

static void client_free(cw_client_t *client) {

    if (client->ws_frag_q.head) {
        BUF_Q_CLEAR(&client->ws_frag_q, it, free_buf16(it););
        client->ws_frag_len = 0;
        client->ws_opcode = 0;
    }
    if (client->last_reason) {
        free_buf16(client->last_reason);
        client->last_reason = NULL;
    }
}

void cw_client_free(client_ctx_t* ctx, client_t *c) {
    client_free((cw_client_t*)c);
    ct_client_free(ctx, c);
}

bool cw_client_reset(client_ctx_t* ctx, client_t *c, bool init) { (void)ctx;
    cw_client_t *client = (cw_client_t*)c;

    // 分配 HTTP 握手接收缓冲（流模式）
    buf16_item_t *recv_buf = alloc_buf16(CW_HTTP_BUF_FLAGS);
    if (!recv_buf) {
        print("E:", LA_F("[WS] OOM: cannot allocate HTTP recv buffer\n", LA_F227, 227));
        return false;
    }
    static const uint8_t CRLF2[4] = {'\r','\n','\r','\n'};

    ct_client_reset(ctx, c, true);

    if (init) {

        // 初始化 WS 专有字段
        client->ws_opcode = 0;
        client->ws_frag_q.head = client->ws_frag_q.rear = NULL;
        client->ws_frag_len = 0;

        client->last_reason = NULL;

        uint8_t *buf = client->ws_close_frame_buf + sizeof(buf16_item_t);
        buf[0] = 0x88;      // FIN | CLOSE
        buf[1] = 2;         // payload len = 2
        ((buf16_item_t*)client->ws_close_frame_buf)->len   = 4;
    }
    else client_free(client);

    // 初始化 custom_tcp 字段（流模式，hdr_rs="\r\n\r\n"，hdr_sz=4）
    // 注意：recv_buf 由 ct_init_client 分配，这里替换为更大的 HTTP 缓冲
    ct_client_t *ct_c = (ct_client_t*)c;
    ct_c->recv_buf = recv_buf;

    // 握手阶段：流模式，以 "\r\n\r\n" 为边界
    ct_c->hdr_rs = (uint8_t*)CRLF2;
    ct_c->hdr_sz = 4;

    client->ws_utf8state = WS_UTF8_ACCEPT;

    return true;
}

static buf16_item_t* cw_error_item(ct_client_t *c) {
    cw_client_t *client = (cw_client_t*)c;

    // HTTP 握手阶段无法发送 WS close 帧，返回 NULL 框架会直接关闭连接
    if (c->handshake == TCP_HS_FLAG_HANDSHAKING) {
        assert(!client->last_reason);
        return NULL;
    }

    int16_t s_tc_err_code[CUSTOM_TCP_ERR_CUSTOM-1] = {
        /* CUSTOM_TCP_ERR_DISCONNECTED */ WS_CLOSE_GOING_AWAY,
        /* CUSTOM_TCP_ERR_IO */ WS_CLOSE_BAD_GATEWAY,
        /* CUSTOM_TCP_ERR_OVERFLOW */ WS_CLOSE_MESSAGE_TOO_BIG,
        /* CUSTOM_TCP_ERR_INTERNAL */ WS_CLOSE_INTERNAL_ERROR,
        /* CUSTOM_TCP_ERR_PROTOCOL */ WS_CLOSE_PROTOCOL_ERROR,
        /* CUSTOM_TCP_ERR_CUSTOM */ 
    };

    uint16_t ws_code = c->last_error;
    if (!ws_code) ws_code = CUSTOM_TCP_ERR_PROTOCOL;
    else if (ws_code < CUSTOM_TCP_ERR_CUSTOM)
        ws_code = s_tc_err_code[ws_code];

    buf16_item_t *close_frame; uint8_t* buf;
    if (client->last_reason) {
        close_frame = client->last_reason;
        client->last_reason = NULL;

        assert(!BUF_IS_32BIT(close_frame->flags));

        uint16_t payload_offset = close_frame->pos;
        uint16_t total_len = close_frame->len;
        P_check(payload_offset + 2 <= total_len,
                print("E:", LA_F("[WS] invalid last_reason payload pos=%u len=%u\n", 0, 0), payload_offset, total_len);
                free_buf16(close_frame);
                goto fallback_close;)

        uint16_t payload_len = (uint16_t)(total_len - payload_offset);
        uint8_t hdr_sz = s_hdr_sizes[close_frame->flags & CW_BUF_FLAG_HDR_SIZE];
        if (!hdr_sz) hdr_sz = payload_len <= 125 ? 2 : 4;

        P_check(payload_offset >= hdr_sz,
                print("E:", LA_F("[WS] invalid last_reason hdr_sz=%u pos=%u\n", 0, 0), hdr_sz, payload_offset);
                free_buf16(close_frame);
                goto fallback_close;)

        buf = ITEM2BUF(close_frame) + payload_offset - hdr_sz;
        if ((buf[0] & 0x0F) != WS_OP_CLOSE) {
            print("E:", LA_F("[WS] invalid last_reason frame\n", LA_F257, 257));
            free_buf16(close_frame);
            goto fallback_close;
        }

        if (payload_len > 125) {
            print("W:", LA_F("[WS] truncate last_reason payload_len=%u to 125\n", LA_F258, 258), payload_len);
            hdr_sz = 2;
            buf = ITEM2BUF(close_frame) + payload_offset - hdr_sz;
            buf[0] = 0x80 | WS_OP_CLOSE;
            buf[1] = 125;
            payload_len = 125;
            close_frame->len = (uint16_t)(payload_offset + payload_len);
            close_frame->flags = (uint8_t)((close_frame->flags & (uint8_t)~CW_BUF_FLAG_HDR_SIZE) | CW_BUF_HDR_2);
        }

        nwrite_s(ITEM2BUF(close_frame) + payload_offset, ws_code);
        close_frame->pos = (uint16_t)(payload_offset - hdr_sz);
        return close_frame;
    }

fallback_close:
    close_frame = (buf16_item_t*)client->ws_close_frame_buf;
    if (close_frame->refer) return NULL;
    close_frame->refer = ITEM_REF_STATIC;
    buf = ITEM2BUF(close_frame) + 2;
    nwrite_s(buf, ws_code);
    return close_frame;
}

//-----------------------------------------------------------------------------

void cw_ctx_init(cw_client_ctx_t *ctx) {

    if (!ctx->base.base.cb_free) ctx->base.base.cb_free = cw_client_free;
    if (!ctx->base.base.cb_reset) ctx->base.base.cb_reset = cw_client_reset;

    ctx->base.resolve_payload_len = cw_resolve_payload_len;
    ctx->base.handle_handshake = cw_tcp_handle_handshake;
    ctx->base.handshake_finish = cw_tcp_handshake_finish;
    ctx->base.handle_proto = cw_tcp_handle_proto;
    ct_ctx_init(&ctx->base);

    ctx->base.error_item = cw_error_item;
    ctx->base.fatal_item = (buf16_item_t*)ctx->fatal_frame_buf;
    uint8_t *buf = ctx->fatal_frame_buf + sizeof(buf16_item_t);
    buf[0] = 0x88;      // FIN | CLOSE
    buf[1] = 2;         // payload len = 2
    ((buf16_item_t*)ctx->fatal_frame_buf)->len   = 4;
}

///////////////////////////////////////////////////////////////////////////////
// SHA-1 + Base64（自包含，不依赖外部库）

#define WS_SHA1_LEN 20
typedef struct { uint32_t h[5]; uint8_t buf[64]; uint32_t buf_len; uint64_t total; } ws_sha1_ctx_t;
static uint32_t ws_sha1_rot(uint32_t x, int n) { return (x<<n)|(x>>(32-n)); }
static void ws_sha1_block(ws_sha1_ctx_t *ctx, const uint8_t *blk) {
    uint32_t w[80],a,b,c,d,e,f,k,tmp; int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|blk[i*4+3];
    for(i=16;i<80;i++) w[i]=ws_sha1_rot(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=ctx->h[0];b=ctx->h[1];c=ctx->h[2];d=ctx->h[3];e=ctx->h[4];
    for(i=0;i<80;i++){
        if(i<20){f=(b&c)|(~b&d);k=0x5A827999u;}
        else if(i<40){f=b^c^d;k=0x6ED9EBA1u;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDCu;}
        else{f=b^c^d;k=0xCA62C1D6u;}
        tmp=ws_sha1_rot(a,5)+f+e+k+w[i]; e=d;d=c;c=ws_sha1_rot(b,30);b=a;a=tmp;
    }
    ctx->h[0]+=a;ctx->h[1]+=b;ctx->h[2]+=c;ctx->h[3]+=d;ctx->h[4]+=e;
}
static void ws_sha1_init(ws_sha1_ctx_t *ctx) {
    ctx->h[0]=0x67452301u;ctx->h[1]=0xEFCDAB89u;ctx->h[2]=0x98BADCFEu;
    ctx->h[3]=0x10325476u;ctx->h[4]=0xC3D2E1F0u; ctx->buf_len=0;ctx->total=0;
}
static void ws_sha1_update(ws_sha1_ctx_t *ctx, const uint8_t *data, size_t len) {
    ctx->total+=len;
    while(len>0){ size_t cp=64-ctx->buf_len; if(cp>len)cp=len;
        memcpy(ctx->buf+ctx->buf_len,data,cp); ctx->buf_len+=(uint32_t)cp; data+=cp; len-=cp;
        if(ctx->buf_len==64){ws_sha1_block(ctx,ctx->buf);ctx->buf_len=0;} }
}
static void ws_sha1_final(ws_sha1_ctx_t *ctx, uint8_t d[WS_SHA1_LEN]) {
    uint64_t bl=ctx->total*8; uint8_t p=0x80;
    ws_sha1_update(ctx,&p,1); p=0;
    while(ctx->buf_len!=56) ws_sha1_update(ctx,&p,1);
    uint8_t lb[8]; for(int i=7;i>=0;i--){lb[i]=(uint8_t)(bl&0xFF);bl>>=8;}
    ws_sha1_update(ctx,lb,8);
    for(int i=0;i<5;i++){d[i*4]=(uint8_t)(ctx->h[i]>>24);d[i*4+1]=(uint8_t)(ctx->h[i]>>16);
                         d[i*4+2]=(uint8_t)(ctx->h[i]>>8);d[i*4+3]=(uint8_t)(ctx->h[i]);}
}
static void ws_b64_sha1(const uint8_t src[20], char dst[29]) {
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i,j;
    for(i=0,j=0;i<18;i+=3){
        uint32_t v=((uint32_t)src[i]<<16)|((uint32_t)src[i+1]<<8)|src[i+2];
        dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j++]=t[v&63];
    }
    {uint32_t v=((uint32_t)src[18]<<16)|((uint32_t)src[19]<<8);
     dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j]='=';}
    dst[28]='\0';
}

// 处理 HTTP 升级请求，构造 101 应答
// + req:        HTTP 请求文本（\0 结尾，内容从 \r\n\r\n 之前截断）
// + resp_buf:   应答写入缓冲
// + resp_buf_len: 缓冲长度
// + sub_protocol: 子协议名（NULL 或空串表示不协商）
// + 返回应答长度（>0），或 E_INVALID / E_OUT_OF_CAPACITY
static ret_t cw_http_accept(const char *req, char *resp_buf, const char *sub_protocol) {
    int n = 0;
    const char *key_field = strstr(req, "Sec-WebSocket-Key:");
    if (!key_field) return E_INVALID;
    key_field += 18; while (*key_field == ' ') key_field++;
    char ws_key[32] = {0};
    while (*key_field && *key_field != '\r' && *key_field != '\n' && n < 31)
        ws_key[n++] = *key_field++;
    if (sub_protocol && !*sub_protocol) sub_protocol = NULL;
    char combined[64];
    snprintf(combined, sizeof(combined), "%s" WS_GUID, ws_key);
    ws_sha1_ctx_t sha; ws_sha1_init(&sha);
    ws_sha1_update(&sha, (const uint8_t*)combined, strlen(combined));
    uint8_t digest[WS_SHA1_LEN]; ws_sha1_final(&sha, digest);
    char accept[29]; ws_b64_sha1(digest, accept);
    n = snprintf(resp_buf, ACCEPT_BUF_SZ,
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
    return n < ACCEPT_BUF_SZ ? (ret_t)n : E_OUT_OF_CAPACITY;
}

///////////////////////////////////////////////////////////////////////////////

static const uint8_t g_ws_utf8d[364] = {
    /* byte → char-class（0‥11） */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
    /* state × char-class → next-state */
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12,0,12,12,12,12,12,0,12,0,12,12,  12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
};

// 逐字节推进 DFA；返回 false 表示遇到非法 UTF-8（state 已变为 REJECT）
static bool cw_utf8_check(uint32_t *state, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t type = g_ws_utf8d[data[i]];
        *state = g_ws_utf8d[256 + *state + type];
        if (*state == WS_UTF8_REJECT) return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
