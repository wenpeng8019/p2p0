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

// HTTP 握手 recv 缓冲大小（存放 HTTP 请求 header，流模式 recv_buf 使用 8K）
#define CW_HTTP_BUF_FLAGS   BUF_FLAG_8192(0)
// HTTP 应答最大长度（需容纳 101 Switching Protocols 响应）
#define CW_HTTP_RSP_MAX    512
// WS 帧头最大字节数
#define CW_WS_HDR_MAX       14
// WS GUID（RFC 6455）
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

///////////////////////////////////////////////////////////////////////////////
// UTF-8 验证（RFC 6455 §8.1：文本帧 payload 必须是合法 UTF-8）
// 使用 Bjoern Hoehrmann 的 DFA（https://bjoern.hoehrmann.de/utf-8/decoder/dfa/）
// state=0（WS_UTF8_ACCEPT）表示已完成一个合法序列，state=12 表示非法，其余为中间态
#define WS_UTF8_ACCEPT 0u
#define WS_UTF8_REJECT 12u

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

static ret_t cw_http_accept(const char *req, char *resp_buf, size_t resp_buf_len, const char *sub_protocol);

///////////////////////////////////////////////////////////////////////////////
// WS 帧头解析（帧模式 resolve_payload_len）

static ret_t cw_resolve_payload_len(ct_client_t *client, uint8_t *hdr_buf, uint16_t hdr_len,
                                    uint32_t *payload_len, uint16_t *payload_offset) {
    
    // HTTP 握手阶段没有 payload
    if (TCP_HS_IS_HANDSHAKING(client)) { *payload_len = 0; *payload_offset = 0; return E_NONE; }
    
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

    *payload_offset = 0;    // 默认不预留；上层子协议可在自己的 resolve 中覆盖
    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// 控制帧辅助

// RFC 6455 §7.4.1: close 状态码合法性（1000-1011 不含 1004/1005/1006，或 3000-4999）
static bool cw_valid_close_code(uint16_t c) {
    return (c >= 1000 && c <= 1011 && c != 1004 && c != 1005 && c != 1006) ||
           (c >= 3000 && c <= 4999);
}

///////////////////////////////////////////////////////////////////////////////
// 掩码解除（客户端发来的帧 payload 必须用 mask key XOR 还原）

static void cw_unmask(uint8_t *data, uint32_t len, const uint8_t mask[4]) {
    uint32_t i;
    for (i = 0; i + 4 <= len; i += 4) {
        data[i]   ^= mask[0];
        data[i+1] ^= mask[1];
        data[i+2] ^= mask[2];
        data[i+3] ^= mask[3];
    }
    for (; i < len; i++) data[i] ^= mask[i & 3];
}

///////////////////////////////////////////////////////////////////////////////
// custom_tcp 回调实现

// 握手阶段（HTTP Upgrade 请求）处理
// + hdr_buf: recv_buf 中 HTTP 请求文本（不含 "\r\n\r\n" 尾）
// + hdr_len: HTTP header 文本长度
// + payload0/payload1: HTTP header 之后不应有 payload（WS 协议握手阶段不携带 body）
static buf16_item_t *cw_tcp_handle_handshake(ct_client_t **pclient,
                                              uint8_t *hdr_buf, uint16_t hdr_len,
                                              buf16_item_t *payload0, buf16_item_t *payload1) {
    (void)payload0; (void)payload1;
    ct_client_t *client = *pclient;
    cw_client_t *cwc = (cw_client_t*)client;
    cw_client_ctx_t *wctx = cwc->ws_ctx;

    // hdr_buf 指向流模式 recv_buf 内部，内容是不含 "\r\n\r\n" 的 HTTP 请求文本
    // 需要 NUL 结尾才能用 strstr；hdr_buf 后紧跟 recv_buf 中剩余数据，
    // 此时 recv_buf->pos = header 起始，recv_cur = header 结束（\r\n\r\n 之前）
    // 框架传入的 hdr_buf 本身没有 \0，但 recv_buf 里 recv_cur 之后的字节是 \r\n\r\n，
    // 可以暂时覆写（此时 hdr_buf[hdr_len] 指向 '\r'），用完即恢复
    uint8_t saved = hdr_buf[hdr_len];
    hdr_buf[hdr_len] = '\0';

    // 利用 recv_buf 前置空间（pos 之前）写入 HTTP 应答（最多 CW_HTTP_RSP_MAX 字节）
    // recv_buf->pos 是本次 header 起始偏移，前面的空间在流模式中是已消费区域，可以复用
    buf16_item_t *recv_buf = client->recv_buf;
    uint8_t *resp_buf;
    size_t resp_buf_sz;
    if (recv_buf->pos >= CW_HTTP_RSP_MAX) {
        // pos 之前有足够空间，直接用
        resp_buf = ITEM2BUF(recv_buf);
        resp_buf_sz = CW_HTTP_RSP_MAX;
    } else {
        // 空间不足，分配一个临时缓冲（正常不应发生，HTTP 请求一般不会在 recv_buf 起始处）
        // 分配 128 字节足够容纳 101 响应
        buf16_item_t *resp_item = alloc_buf16(BUF_FLAG_512(0));
        if (!resp_item) {
            hdr_buf[hdr_len] = saved;
            client->last_error = CUSTOM_TCP_ERR_INTERNAL;
            return NULL;
        }
        resp_buf = ITEM2BUF(resp_item);
        resp_buf_sz = 512;
        // 将 resp_item 挂到 recv_buf->next 作为应答缓冲（框架不会使用 next，安全）
        // 但这样生命期管理复杂；更简单：直接在应答 buf_item 里构造整个应答
        ret_t r = cw_http_accept((const char*)hdr_buf, (char*)resp_buf, resp_buf_sz, wctx->sub_protocol);
        hdr_buf[hdr_len] = saved;
        if (r < E_NONE) {
            print("E:", LA_F("[WS] HTTP handshake rejected\n", LA_F143, 143));
            free_buf16(resp_item);
            client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
            return NULL;
        }
        // 可选回调：上层鉴权
        if (wctx->handshake_done && !wctx->handshake_done(cwc)) {
            free_buf16(resp_item);
            client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
            return NULL;
        }
        resp_item->len = (uint16_t)r;
        resp_item->pos = 0;
        return resp_item;
    }

    ret_t r = cw_http_accept((const char*)hdr_buf, (char*)resp_buf, resp_buf_sz, wctx->sub_protocol);
    hdr_buf[hdr_len] = saved;
    if (r < E_NONE) {
        print("E:", LA_F("[WS] HTTP handshake rejected\n", LA_F143, 143));
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        return NULL;
    }

    // 可选回调：上层鉴权
    if (wctx->handshake_done && !wctx->handshake_done(cwc)) {
        client->last_error = CUSTOM_TCP_ERR_PROTOCOL;
        return NULL;
    }

    // 将应答存入 recv_buf 前置空间，构造一个引用它的 buf_item
    // 注意：recv_buf 自身不能直接作为 ack_item 返回（框架 handshake 后会继续复用 recv_buf 读数据）
    // 正确做法：分配一个独立的 ack buf_item（不依赖 recv_buf）
    buf16_item_t *ack = alloc_buf16(BUF_FLAG_512(0));
    if (!ack) {
        client->last_error = CUSTOM_TCP_ERR_INTERNAL;
        return NULL;
    }
    memcpy(ITEM2BUF(ack), resp_buf, (size_t)r);
    ack->len = (uint16_t)r;
    ack->pos = 0;
    return ack;
}

static void cw_tcp_handshake_finish(ct_client_t *client) {
    cw_client_t *cwc = (cw_client_t*)client;
    
    // 切换到帧模式：将 recv_buf 设为 NULL 触发框架切换
    client->recv_buf = NULL;
    
    // 设置帧模式参数
    client->hdr_rs = cwc->ws_hdr_buf;  // 14 字节静态帧头缓冲
    client->hdr_sz = 2;                // 初始读取 2 字节基础帧头
    // 结束握手阶段，进入正常帧收发阶段
    client->handshake = 0;
}

static void cw_tcp_handle_proto(ct_client_t *client, uint8_t *hdr_buf, uint16_t hdr_len,
                                buf16_item_t *payload0, buf16_item_t *payload1) {
    (void)hdr_len;
    cw_client_t *cwc = (cw_client_t*)client;
    cw_client_ctx_t *wctx = cwc->ws_ctx;

    uint8_t opcode = hdr_buf[0] & 0x0F;
    uint8_t fin    = (hdr_buf[0] >> 7) & 1;
    uint8_t masked = hdr_buf[1] & 0x80;

    // 重置帧模式 hdr_sz 为 2（下一帧）
    client->hdr_sz = 2;

    // RFC 6455 §5.2: RSV1/2/3 必须为 0（未协商任何扩展时）
    if (hdr_buf[0] & 0x70) {
        cw_send_close(cwc, 1002);
        return;
    }

    // 服务端要求客户端帧必须有 mask（RFC 6455 §5.3）
    if (!masked) {
        ct_client_error(&wctx->base, client, CUSTOM_TCP_ERR_PROTOCOL, false);
        return;
    }

    // mask key 已被 cw_resolve_payload_len 统一复制到 hdr_buf[10..13]
    const uint8_t *mask = hdr_buf + 10;

    // 解除 payload mask（in-place）
    // payload0 是 recv_buf 内的切片（零拷贝），payload1 是独立缓冲
    // 两段在逻辑上连续，mask 偏移需要跨段衔接
    uint32_t plen0 = 0;
    if (payload0) {
        plen0 = payload0->len - payload0->pos;
        cw_unmask(ITEM2BUF(payload0) + payload0->pos, plen0, mask);
        if (payload1) {
            uint8_t *p1buf = ITEM2BUF(payload1) + payload1->pos;
            uint32_t p1len = payload1->len - payload1->pos;
            for (uint32_t i = 0; i < p1len; i++)
                p1buf[i] ^= mask[(plen0 + i) & 3];
        }
    } else if (payload1) {
        uint8_t *p1buf = ITEM2BUF(payload1) + payload1->pos;
        uint32_t p1len = payload1->len - payload1->pos;
        cw_unmask(p1buf, p1len, mask);
    }

    // 控制帧处理（control frames: close/ping/pong）
    if (opcode >= WS_OP_CLOSE) {
        // RFC 6455 §5.5: 控制帧必须 FIN，且 payload ≤ 125 字节
        uint32_t ctrl_total = plen0 + (payload1 ? (payload1->len - payload1->pos) : 0u);
        if (!fin || ctrl_total > 125) {
            cw_send_close(cwc, 1002);
            return;
        }
        uint8_t *ctrl_data = payload0 ? ITEM2BUF(payload0) + payload0->pos : NULL;
        uint8_t  ctrl_len  = (uint8_t)ctrl_total;
        if (opcode == WS_OP_CLOSE) {
            uint16_t code = (ctrl_len >= 2 && ctrl_data) ? (uint16_t)((ctrl_data[0]<<8)|ctrl_data[1]) : 1000;
            // RFC 6455 §7.4.1: 状态码必须合法
            if (ctrl_len >= 2 && ctrl_data && !cw_valid_close_code(code)) {
                cw_send_close(cwc, 1002);
                return;
            }
            // RFC 6455 §5.5.1: close frame reason string（第 2 字节起）必须是合法 UTF-8
            if (ctrl_len > 2 && ctrl_data) {
                uint32_t tmp = WS_UTF8_ACCEPT;
                if (!cw_utf8_check(&tmp, ctrl_data + 2, ctrl_len - 2) || tmp != WS_UTF8_ACCEPT) {
                    cw_send_close(cwc, 1007);
                    return;
                }
            }
            if (wctx->handle_close) wctx->handle_close(cwc, code);
            // 自动回复 close
            cw_send_close(cwc, code);
        } else if (opcode == WS_OP_PING) {
            if (wctx->handle_ping) wctx->handle_ping(cwc, ctrl_data, ctrl_len);
            // 自动回复 pong，携带相同 payload
            buf16_item_t *pong = alloc_buf16(BUF_FLAG_128(0));
            if (pong) {
                uint8_t *pb = ITEM2BUF(pong);
                pb[0] = 0x8A;  // FIN | PONG
                pb[1] = ctrl_len;
                if (ctrl_len && ctrl_data) memcpy(pb + 2, ctrl_data, ctrl_len);
                pong->len = (uint16_t)(2 + ctrl_len);
                pong->pos = 0;
                ct_client_send(client, pong, true);  // 高优先级发送
            }
        }
        // WS_OP_PONG：无需处理
        return;
    }

    // RFC 6455 §5.2: 保留 opcode（0x3-0x7 数据帧、0xB-0xF 控制帧）必须拒绝
    if ((opcode >= 0x3 && opcode <= 0x7) || opcode >= 0xB) {
        cw_send_close(cwc, 1002);
        return;
    }

    // RFC 6455 §5.4: 分片状态机一致性
    // + CONTINUATION 帧必须在 TEXT/BINARY 分片序列进行中（ws_opcode != 0）
    // + 新 TEXT/BINARY 帧不可在已有分片序列进行中出现
    bool in_frag = (cwc->ws_opcode != 0);
    if (opcode == WS_OP_CONTINUATION && !in_frag) {
        cw_send_close(cwc, 1002);
        return;
    }
    if ((opcode == WS_OP_TEXT || opcode == WS_OP_BINARY) && in_frag) {
        cw_send_close(cwc, 1002);
        return;
    }

    // RFC 6455 §8.1：TEXT 帧（含分片）payload 必须是合法 UTF-8，逐片增量验证
    // opcode==TEXT 时重置 DFA（新消息），CONTINUATION 时沿用上片状态跨片累积
    bool is_text = (opcode == WS_OP_TEXT) ||
                   (opcode == WS_OP_CONTINUATION && cwc->ws_opcode == WS_OP_TEXT);
    if (is_text) {
        if (opcode == WS_OP_TEXT) cwc->ws_utf8state = WS_UTF8_ACCEPT;  // 新消息重置
        uint32_t *st = &cwc->ws_utf8state;
        bool ok = (plen0 == 0) || cw_utf8_check(st, ITEM2BUF(payload0) + payload0->pos, plen0);
        if (ok && payload1) {
            uint32_t p1len = payload1->len - payload1->pos;
            ok = cw_utf8_check(st, ITEM2BUF(payload1) + payload1->pos, p1len);
        }
        if (ok && fin && *st != WS_UTF8_ACCEPT) ok = false;  // FIN 时截断于多字节序列中间
        if (!ok) {
            cw_send_close(cwc, 1007);
            return;
        }
    }

    // 数据帧（text/binary/continuation）
    if (opcode == WS_OP_CONTINUATION || !fin) {
        // 分片帧：聚合到 ws_frag_q
        // 第一个分片：opcode 非 0，后续 continuation: opcode == 0
        if (opcode != WS_OP_CONTINUATION) cwc->ws_opcode = opcode;  // 记录起始帧 opcode

        uint32_t add_len = plen0 + (payload1 ? (payload1->len - payload1->pos) : 0);

        // payload0 是 recv_buf 内的零拷贝切片，必须复制一份再入队
        if (plen0) {
            buf16_item_t *p0copy = alloc_buf16(BUF_FLAGS(buffer_sz_flag(plen0 ? plen0 : 1), 0));
            if (!p0copy) goto oom;
            memcpy(ITEM2BUF(p0copy), ITEM2BUF(payload0) + payload0->pos, plen0);
            p0copy->len = (uint16_t)plen0;
            p0copy->pos = 0;
            BUF_Q_APPEND(&cwc->ws_frag_q, p0copy);
        }
        // payload1 是框架独立分配的缓冲，直接接管所有权追加入队（零拷贝）
        if (payload1) {
            BUF_Q_APPEND(&cwc->ws_frag_q, payload1);
            client->payload_buf = NULL;  // 所有权已转移，通知框架跳过释放
        }
        cwc->ws_frag_len += add_len;

        if (!fin) return;   // 还有后续分片

        // 最后一个分片：组装完成，回调上层（以链表头作为 payload0 交付）
        if (wctx->handle_frame) {
            wctx->handle_frame(cwc, cwc->ws_opcode, cwc->ws_frag_q.head, NULL);
        } else {
            BUF_Q_CLEAR(&cwc->ws_frag_q, it, free_buf16(it););
        }
        cwc->ws_frag_q.head = cwc->ws_frag_q.rear = NULL;
        cwc->ws_frag_len = 0;
        cwc->ws_opcode = 0;   // 分片序列结束，重置状态（下一帧 CONTINUATION 视为协议错误）
        return;
    }

    // 完整帧（非分片）：直接回调
    if (wctx->handle_frame)
        wctx->handle_frame(cwc, opcode, payload0, payload1);
    return;

oom:
    print("E:", LA_F("[WS] OOM in fragment reassembly\n", LA_F226, 226));
    if (cwc->ws_frag_q.head) {
        BUF_Q_CLEAR(&cwc->ws_frag_q, it, free_buf16(it););
        cwc->ws_frag_len = 0;
    }
    ct_client_error(&wctx->base, client, CUSTOM_TCP_ERR_INTERNAL, true);
}

///////////////////////////////////////////////////////////////////////////////
// Public API

bool cw_init_client(cw_client_t *client, cw_client_ctx_t *ctx) {
    (void)ctx;

    // 分配 HTTP 握手接收缓冲（流模式）
    buf16_item_t *buf = alloc_buf16(CW_HTTP_BUF_FLAGS);
    if (!buf) {
        print("E:", LA_F("[WS] OOM: cannot allocate HTTP recv buffer\n", LA_F227, 227));
        return false;
    }

    // 初始化 custom_tcp 字段（流模式，hdr_rs="\r\n\r\n"，hdr_sz=4）
    // 注意：recv_buf 由 ct_init_client 分配，这里替换为更大的 HTTP 缓冲
    ct_client_t *ctc = (ct_client_t*)client;
    ctc->recv_buf = buf;
    ctc->recv_cur = 0;
    ctc->send_buff_queue.head = ctc->send_buff_queue.rear = NULL;
    ctc->send_sess_head = ctc->send_sess_rear = NULL;
    ctc->sending_sess = NULL;
    ctc->sending_cur = 0;
    ctc->last_error = 0;
    ctc->payload_buf = NULL;
    ctc->payload_cur = 0;

    // 握手阶段：流模式，以 "\r\n\r\n" 为边界
    static const uint8_t CRLF2[4] = {'\r','\n','\r','\n'};
    ctc->hdr_rs = (uint8_t*)CRLF2;
    ctc->hdr_sz = 4;

    // 初始化 WS 专有字段
    client->ws_ctx = ctx;
    memset(client->ws_hdr_buf, 0, sizeof(client->ws_hdr_buf));
    client->ws_opcode = 0;
    client->ws_frag_q.head = client->ws_frag_q.rear = NULL;
    client->ws_frag_len = 0;
    client->ws_utf8state = WS_UTF8_ACCEPT;

    TCP_CLIENT_INIT(client);
    return true;
}

void cw_free_client(cw_client_ctx_t *ctx, cw_client_t *client) {
    if (client->ws_frag_q.head) {
        BUF_Q_CLEAR(&client->ws_frag_q, it, free_buf16(it););
        client->ws_frag_len = 0;
    }
    ct_free_client(&ctx->base, (ct_client_t*)client);
}

// 构造并发送 WS 帧头（服务端，无 mask）
// + buf_item: 上层分配的缓冲，[0, payload_pos) 为预留空间，[payload_pos, len) 为 payload
// + payload_pos 必须 >= 实际帧头长度（由 payload 大小决定）
ret_t cw_send_frame(cw_client_t *client, uint8_t opcode, buf16_item_t *buf_item, uint16_t payload_pos) {

    assert(!client->handshake);

    uint32_t plen; uint8_t hdr[10]; uint8_t hdr_sz;
    if (BUF_IS_32BIT(buf_item->flags)) {
        plen = BUF32(buf_item)->len - payload_pos;
    } else {
        plen = buf_item->len - payload_pos;
    }

    hdr[0] = 0x80 | (opcode & 0x0F);   // FIN=1, RSV=0, opcode
    if (plen <= 125) {
        hdr[1] = (uint8_t)plen;
        hdr_sz = 2;
    } else if (plen <= 65535) {
        hdr[1] = 126;
        hdr[2] = (uint8_t)(plen >> 8);
        hdr[3] = (uint8_t)(plen);
        hdr_sz = 4;
    } else {
        hdr[1] = 127;
        hdr[2] = 0; hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;
        hdr[6] = (uint8_t)(plen >> 24);
        hdr[7] = (uint8_t)(plen >> 16);
        hdr[8] = (uint8_t)(plen >> 8);
        hdr[9] = (uint8_t)(plen);
        hdr_sz = 10;
    }

    if (payload_pos < hdr_sz) {
        print("E:", LA_F("[WS] send_frame: payload_pos(%u) < hdr_sz(%u)\n", LA_F228, 228), payload_pos, hdr_sz);
        free_buffer(buf_item);
        return E_INVALID;
    }

    // 将帧头写入 payload 前的预留空间末尾（紧靠 payload）
    uint8_t *buf_start;
    if (BUF_IS_32BIT(buf_item->flags)) {
        buf_start = ITEM2BUF(BUF32(buf_item));
        BUF32(buf_item)->pos = payload_pos - hdr_sz;
    } else {
        buf_start = ITEM2BUF(buf_item);
        buf_item->pos = payload_pos - hdr_sz;
    }
    memcpy(buf_start + (payload_pos - hdr_sz), hdr, hdr_sz);

    ct_client_send((ct_client_t*)client, buf_item, false);
    return E_NONE;
}

ret_t cw_send_close(cw_client_t *client, uint16_t code) {
    buf16_item_t *item = alloc_buf16(BUF_FLAG_128(0));
    if (!item) return E_OUT_OF_MEMORY;
    uint8_t *buf = ITEM2BUF(item);
    // WS close 帧：FIN=1, opcode=0x8, payload=2字节状态码
    buf[0] = 0x88;   // FIN | CLOSE
    buf[1] = 2;      // payload len = 2
    buf[2] = (uint8_t)(code >> 8);
    buf[3] = (uint8_t)(code);
    item->len = 4;
    item->pos = 0;
    client->io |= CW_IO_FLAG_CLOSING;
    client->base.last_active = P_tick_ms();
    // 使用 immediate=false，让 close 帧排在队尾，确保先发送已入队的应用层消息（如 REG_ACK）
    ct_client_send((ct_client_t*)client, item, false);
    // 交由基类统一执行关闭收口：停止接收、清理 sessions / recv_buf，并保持 close 帧继续发送
    ct_client_off(&client->ws_ctx->base, (ct_client_t*)client);
    return E_NONE;
}

void cw_retry_closing(cw_client_ctx_t *ctx, cw_client_t *client, uint64_t now) {
    if ((client->io & CW_IO_FLAG_CLOSING) &&
        tick_diff(now, client->base.last_active) >= CLIENT_TIMEOUT_S * 1000) {
        print("I:", LA_F("[WS] close timeout, force closing\n", LA_F192, 192));
        cw_free_client(ctx, client);
    }
}

///////////////////////////////////////////////////////////////////////////////
// custom_tcp_ctx_t 初始化助手
// + 将 WS 回调绑定到 tcp ctx，供 cw_init_client 调用方使用（一次性初始化）

void cw_ctx_init(cw_client_ctx_t *ctx) {
    ctx->base.resolve_payload_len    = cw_resolve_payload_len;
    ctx->base.handle_handshake       = cw_tcp_handle_handshake;
    ctx->base.handshake_finish       = cw_tcp_handshake_finish;
    ctx->base.handle_proto           = cw_tcp_handle_proto;
    // fatal_item/error_item/client_unreachable 由上层填充
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
     dst[j++]=t[(v>>18)&63];dst[j++]=t[(v>>12)&63];dst[j++]=t[(v>>6)&63];dst[j++]='=';}
    dst[28]='\0';
}

// 处理 HTTP 升级请求，构造 101 应答
// + req:        HTTP 请求文本（\0 结尾，内容从 \r\n\r\n 之前截断）
// + resp_buf:   应答写入缓冲
// + resp_buf_len: 缓冲长度
// + sub_protocol: 子协议名（NULL 或空串表示不协商）
// + 返回应答长度（>0），或 E_INVALID / E_OUT_OF_CAPACITY
static ret_t cw_http_accept(const char *req, char *resp_buf, size_t resp_buf_len, const char *sub_protocol) {
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
    return n < (int)resp_buf_len ? (ret_t)n : E_OUT_OF_CAPACITY;
}
///////////////////////////////////////////////////////////////////////////////
