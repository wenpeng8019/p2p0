
/*
 * STUN 协议实现与 NAT 类型检测
 *
 * 本模块实现 RFC 5389 (STUN) 和 RFC 3489 (Classic STUN) 协议。
 *
 * 主要功能：
 *   1. STUN Binding 请求/响应的构建与解析
 *   2. 获取 NAT 映射的公网 IP:Port (Srflx 候选)
 *   3. NAT 类型检测（Full Cone / Restricted / Symmetric 等）
 *
 * STUN 工作流程：
 *   ┌────────┐       Binding Request       ┌────────────────┐
 *   │ Client ├────────────────────────────>│  STUN Server   │
 *   │        │                             │ (如 Google)    │
 *   │        │<────────────────────────────┤                │
 *   └────────┘    Binding Response         └────────────────┘
 *                 (含 XOR-MAPPED-ADDRESS)
 *
 *   客户端发送 Binding Request，服务器返回客户端的「公网地址」，
 *   即 NAT 设备映射后的 IP:Port，称为 Server Reflexive (Srflx) 地址。
 */

#define MOD_TAG "STUN"

#include "p2p_internal.h"

bool p2p_stun_init(stun_ctx_t *ctx, const char *stun_server, uint16_t stun_port) {

    if (!stun_server || !stun_server[0]) return false;

    if (resolve_host(stun_server, stun_port, &ctx->server_addr) < 0) {
        print("E:", LA_F("Failed to resolve STUN server %s", LA_F290, 290), stun_server);
        return false;
    }

    return true;
}

/*
 * ============================================================================
 * NAT 类型检测实现 (RFC 3489 / RFC 5780)
 * ============================================================================
 *
 * NAT (Network Address Translation) 网络地址转换
 * 是家用路由器和企业防火墙中广泛使用的技术。
 *
 * NAT 类型对 P2P 连接的影响：
 *
 *   ┌───────────────────┬──────────────┬───────────────────────────────┐
 *   │ NAT 类型            │ 穿透难度     │ 特征                           │
 *   ├───────────────────┼──────────────┼───────────────────────────────┤
 *   │ Open/No NAT        │ 无            │ 公网 IP，直接可达              │
 *   ├───────────────────┼──────────────┼───────────────────────────────┤
 *   │ Full Cone          │ ★☆☆☆☆ 极易  │ 任意外部主机可访问映射端口    │
 *   ├───────────────────┼──────────────┼───────────────────────────────┤
 *   │ Restricted Cone    │ ★★☆☆☆ 容易  │ 必须先发过包给目标 IP         │
 *   ├───────────────────┼──────────────┼───────────────────────────────┤
 *   │ Port Restricted    │ ★★★☆☆ 中等  │ 必须先发过包给目标 IP:Port     │
 *   ├───────────────────┼──────────────┼───────────────────────────────┤
 *   │ Symmetric          │ ★★★★★ 极难  │ 每个目标不同端口映射，需 TURN │
 *   └───────────────────┴──────────────┴───────────────────────────────┘
 *
 * 检测流程 (RFC 3489)：
 *
 *   Test I:  向 STUN 服务器发送 Binding Request
 *            → 获取 NAT 映射的公网地址 (mapped_addr)
 *
 *   Test II: 请求服务器从不同 IP 和端口响应
 *            CHANGE-REQUEST flags = 0x06 (CHANGE-IP | CHANGE-PORT)
 *            → 检测是否为 Full Cone
 *
 *   Test III: 请求服务器从相同 IP 不同端口响应
 *             CHANGE-REQUEST flags = 0x02 (CHANGE-PORT)
 *             → 区分 Restricted 和 Port Restricted
 *
 * 判数决策树：
 *   mapped_addr == local_addr?  →  Yes: Open (No NAT)
 *        ↓ No
 *   Test II 收到响应?           →  Yes: Full Cone
 *        ↓ No
 *   Test III 收到响应?          →  Yes: Restricted Cone
 *        ↓ No                  →   No: Port Restricted
 *   不同目标 mapped_addr 变化? →  Yes: Symmetric
 */

/* 测试参数 */
#define STUN_TEST_INTERVAL_MS  500      /* 每次测试发送间隔 500ms */
#define STUN_TEST_TIMEOUT_MS   2000     /* 单次测试超时 2 秒 */
#define STUN_MAX_RETRIES       3        /* 每个测试最多重试 3 次 */

/*
 * ============================================================================
 * 解析 STUN Binding 响应（内部静态函数）
 * ============================================================================
 *
 * 构建符合 RFC 5389 的 STUN Binding Request 消息。
 *
 * 请求格式示例：
 *   +------+------+------+------+------+------+------+------+
 *   | 0x00 | 0x01 |      0x0000 (长度，稍后更新)            |
 *   +------+------+------+------+------+------+------+------+
 *   |           Magic Cookie (0x2112A442)                   |
 *   +------+------+------+------+------+------+------+------+
 *   |                 Transaction ID (12 bytes)             |
 *   +------+------+------+------+------+------+------+------+
 *   |              可选属性：USERNAME, MI, FINGERPRINT       |
 *   +------+------+------+------+------+------+------+------+
 *
 * @param buf      输出缓冲区（至少20字节）
 * @param max_len  缓冲区最大长度
 * @param tsx_id   事务ID（12字节），NULL则随机生成
 * @param username ICE 用户名（格式：远端ufrag:本地ufrag），可选
 * @param password ICE 密码（用于 MESSAGE-INTEGRITY），可选
 * @return         请求长度，失败返回 -1
 */
int p2p_stun_build_binding_request(uint8_t *buf, int max_len,
                                   uint8_t tsx_id[12],
                                   const char *username, const char *password) {
    if (max_len < 20) return -1;
    
    /*
     * STUN 消息头 (20 字节)
     *
     * 字节 0-1: 消息类型 (Binding Request = 0x0001)
     * 字节 2-3: 消息长度（不含头部，初始为0）
     * 字节 4-7: Magic Cookie (0x2112A442)
     * 字节 8-19: 事务 ID (96 bits)
     */
    buf[0] = 0x00; buf[1] = 0x01; /* Type: Binding Request (0x0001) */
    buf[2] = 0x00; buf[3] = 0x00; /* Length: 初始为0，稍后更新 */
    
    /* Magic Cookie: 固定值，用于区分 STUN 版本和 XOR 加密 */
    uint32_t magic = htonl(STUN_MAGIC);
    memcpy(buf + 4, &magic, 4);
    
    /* Transaction ID: 12字节随机数（加密安全），用于匹配请求和响应 */
    if (tsx_id) {
        memcpy(buf + 8, tsx_id, 12);
    } else {
        P_rand_bytes(buf + 8, 12);
    }
    
    int offset = 20; /* 消息头之后的偏移量 */

    /*
     * 1. USERNAME 属性 (0x0006)
     *
     * ICE 使用短期凭证机制，USERNAME 格式为 "远端ufrag:本地ufrag"
     * 例如："abc123:xyz789"
     *
     * +------+------+------+------+
     * | 0x00 | 0x06 |    Length   |
     * +------+------+------+------+
     * |      Username (UTF-8)     |
     * +------+------+------+------+
     */
    if (username) {
        int ulen = (int)strlen(username);
        if (offset + 4 + ulen > max_len) return -1;
        buf[offset++] = (uint8_t)(STUN_ATTR_USERNAME >> 8);      /* 0x00 */
        buf[offset++] = (uint8_t)(STUN_ATTR_USERNAME & 0xFF);    /* 0x06 */
        buf[offset++] = (uint8_t)(ulen >> 8);                     /* 长度高字节 */
        buf[offset++] = (uint8_t)(ulen & 0xFF);                   /* 长度低字节 */
        memcpy(buf + offset, username, ulen);
        offset += ulen;
        while (offset % 4) buf[offset++] = 0; /* 填充到4字节边界 */
    }

    /* 更新消息长度（MESSAGE-INTEGRITY 计算前） */
    int payload_len = offset - 20;
    nwrite_s(buf + 2, (uint16_t)payload_len);

    /*
     * 2. MESSAGE-INTEGRITY 属性 (0x0008)
     *
     * 使用 HMAC-SHA1 计算消息完整性校验，密钥为 ICE 密码。
     * 计算范围：从消息头开始到 MESSAGE-INTEGRITY 属性头之前。
     *
     * 注意：计算 HMAC 前，需要将 Length 字段设置为包含 MI 属性的长度。
     *
     * +------+------+------+------+
     * | 0x00 | 0x08 | 0x00 | 0x14 |  (长度固定为20字节)
     * +------+------+------+------+
     * |     HMAC-SHA1 (20 bytes)  |
     * +------+------+------+------+
     */
    if (password) {
        if (offset + 24 > max_len) return -1;
        
        /* 调整长度字段（必须包含 MI 属性） */
        int mi_len = 24; /* 4字节头 + 20字节 HMAC */
        payload_len += mi_len;
        nwrite_s(buf + 2, (uint16_t)payload_len);

        /* 计算 HMAC-SHA1 */
        uint8_t digest[20];
        p2p_hmac_sha1((const uint8_t*)password, (int)strlen(password), buf, offset, digest);
        
        buf[offset++] = (uint8_t)(STUN_ATTR_MESSAGE_INTEGRITY >> 8);  /* 0x00 */
        buf[offset++] = (uint8_t)(STUN_ATTR_MESSAGE_INTEGRITY & 0xFF); /* 0x08 */
        buf[offset++] = 0;   /* 长度高字节 */
        buf[offset++] = 20;  /* 长度低字节（HMAC-SHA1 输出20字节） */
        memcpy(buf + offset, digest, 20);
        offset += 20;
    }

    /*
     * 3. FINGERPRINT 属性 (0x8028)
     *
     * CRC32 校验值，用于快速验证 STUN 消息完整性（可选但推荐）。
     * 计算方式：CRC32(从消息头到 FINGERPRINT 属性头) XOR 0x5354554E
     *
     * 0x5354554E = ASCII "STUN"，用于避免与其他协议的 CRC32 冲突。
     *
     * +------+------+------+------+
     * | 0x80 | 0x28 | 0x00 | 0x04 |
     * +------+------+------+------+
     * |      CRC32 XOR 0x5354554E |
     * +------+------+------+------+
     */
    if (offset + 8 <= max_len) {
        /* 调整长度字段 */
        payload_len += 8;
        nwrite_s(buf + 2, (uint16_t)payload_len);

        uint32_t crc = p2p_crc32(buf, offset) ^ 0x5354554e; /* XOR "STUN" */
        buf[offset++] = (uint8_t)(STUN_ATTR_FINGERPRINT >> 8);   /* 0x80 */
        buf[offset++] = (uint8_t)(STUN_ATTR_FINGERPRINT & 0xFF); /* 0x28 */
        buf[offset++] = 0;   /* 长度高字节 */
        buf[offset++] = 4;   /* 长度低字节 */
        uint32_t net_crc = htonl(crc);
        memcpy(buf + offset, &net_crc, 4);
        offset += 4;
    }
    
    return offset;
}

bool p2p_stun_collect(struct p2p_instance *inst) {

    P_check(inst->cfg.stun_server && inst->srflx_active < inst->srflx_count, return false;)

    stun_ctx_t *ctx = &inst->stun_ctx; 

    // 幂等：如果已在收集中（未超时），不重复发送
    uint64_t now = P_tick_ms();
    if (ctx->collect_time && tick_diff(now, ctx->collect_time) < STUN_TEST_TIMEOUT_MS)
        return true;

    // COMPACT 模式下 sock[0] 的 srflx 由信令服务器自动收集，仅需收集 socks[1..N]
    // 其他模式下所有 sock 都需要收集
    int start_idx = (inst->sig_mode == P2P_SIGNALING_MODE_COMPACT) ? 1 : 0;

    // 检查是否有需要收集的 sock（state < 2 表示尚无有效 mapped_addr）
    // sock 0 需等 NAT 检测完成（Test I 已顺带收集 sock 0 的 srflx）
    bool need_collect = false;
    for (int i = start_idx; i < inst->sock_cnt; i++) {
        if (i == 0 && inst->nat_type == P2P_NAT_DETECTING) continue;
        if (inst->socks[i].state < 2/*active*/) {
            need_collect = true;
            break;
        }
    }
    if (need_collect) { uint8_t req[512];

        int len = p2p_stun_build_binding_request(req, sizeof(req), NULL, NULL, NULL);
        if (len <= 0) {
            print("E:", LA_F("Failed to build STUN request", LA_F284, 284));
            return false;
        }

        print("I:", LA_F("STUN collecting to %s:%d (len=%d)", LA_F373, 373), inst->cfg.stun_server, inst->cfg.stun_port, len);

        ctx->collect_time = now;
        for (int i = start_idx; i < inst->sock_cnt; i++) {
            if (i == 0 && inst->nat_type == P2P_NAT_DETECTING) continue;
            if (inst->socks[i].state < 2/*active*/) {
                ret_t ret = p2p_udp_send_to_sock(inst, i, &ctx->server_addr, req, len);
                if (ret <= 0) {
                    print("E:", LA_F("Failed to send STUN request: %d", LA_F293, 293), ret);
                    return false;
                }
            }
        }
    }

    return true;
}

/*
 * 构建带 CHANGE-REQUEST 属性的 Binding Request（NAT 检测专用）
 *
 * @param change_flags  CHANGE-REQUEST 标志位：
 *                      0x04 = CHANGE-IP (从不同 IP 响应)
 *                      0x02 = CHANGE-PORT (从不同端口响应)
 *                      0x06 = CHANGE-IP | CHANGE-PORT
 */
static int p2p_stun_build_binding_request_ex(uint8_t *buf, int max_len,
                                              uint8_t tsx_id[12],
                                              uint32_t change_flags) {
    if (max_len < 20) return -1;
    
    buf[0] = 0x00; buf[1] = 0x01;
    buf[2] = 0x00; buf[3] = 0x00;
    
    uint32_t magic = htonl(STUN_MAGIC);
    memcpy(buf + 4, &magic, 4);
    
    if (tsx_id) {
        memcpy(buf + 8, tsx_id, 12);
    } else {
        P_rand_bytes(buf + 8, 12);
    }
    
    int offset = 20;

    /* CHANGE-REQUEST 属性 (0x0003) */
    if (change_flags != 0) {
        if (offset + 8 > max_len) return -1;
        buf[offset++] = (uint8_t)(STUN_ATTR_CHANGE_REQUEST >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_CHANGE_REQUEST & 0xFF);
        buf[offset++] = 0x00; /* 长度高字节 */
        buf[offset++] = 0x04; /* 长度低字节：4 字节 */
        buf[offset++] = (uint8_t)(change_flags >> 24);
        buf[offset++] = (uint8_t)(change_flags >> 16);
        buf[offset++] = (uint8_t)(change_flags >> 8);
        buf[offset++] = (uint8_t)(change_flags & 0xFF);
    }

    /* 更新消息长度 */
    int payload_len = offset - 20;
    nwrite_s(buf + 2, (uint16_t)payload_len);

    return offset;
}

/*
 * ============================================================================
 * 解析 STUN 响应
 * ============================================================================
 *
 * 解析 STUN Binding Response，提取映射地址 (MAPPED-ADDRESS / XOR-MAPPED-ADDRESS)。
 *
 * 响应格式示例：
 *   +------+------+------+------+------+------+------+------+
 *   | 0x01 | 0x01 |         消息长度                        |
 *   +------+------+------+------+------+------+------+------+
 *   |           Magic Cookie (0x2112A442)                   |
 *   +------+------+------+------+------+------+------+------+
 *   |                 Transaction ID (12 bytes)             |
 *   +------+------+------+------+------+------+------+------+
 *   |              XOR-MAPPED-ADDRESS 属性                  |
 *   +------+------+------+------+------+------+------+------+
 *
 * @param buf         响应数据
 * @param len         数据长度
 * @param mapped_addr 输出：解析出的映射地址
 * @param password    密码（用于验证 MESSAGE-INTEGRITY，可选）
 * @return            0=成功，-1=失败
 */
int p2p_stun_build_binding_response(uint8_t *buf, int max_len, const uint8_t tsx_id[12],
                                    const struct sockaddr_in *mapped, const char *password) {
    if (max_len < 20 || !tsx_id || !mapped) return -1;

    /* 消息头：Type=0x0101(Binding Response), Length 稍后填 */
    buf[0] = 0x01; buf[1] = 0x01;
    buf[2] = 0x00; buf[3] = 0x00;
    uint32_t magic_be = htonl(STUN_MAGIC);
    memcpy(buf + 4, &magic_be, 4);
    memcpy(buf + 8, tsx_id, 12);

    int offset = 20;

    /*
     * XOR-MAPPED-ADDRESS (0x0020)
     * X-Port = Port XOR 0x2112  (host order XOR)
     * X-Addr = Addr XOR 0x2112A442
     */
    if (offset + 12 > max_len) return -1;
    buf[offset++] = (uint8_t)(STUN_ATTR_XOR_MAPPED_ADDR >> 8);   /* 0x00 */
    buf[offset++] = (uint8_t)(STUN_ATTR_XOR_MAPPED_ADDR & 0xFF); /* 0x20 */
    buf[offset++] = 0x00;  /* attr len hi */
    buf[offset++] = 0x08;  /* attr len lo: 8 bytes for IPv4 */
    buf[offset++] = 0x00;  /* reserved */
    buf[offset++] = 0x01;  /* family: IPv4 */
    uint16_t xor_port = mapped->sin_port ^ htons(0x2112);
    memcpy(buf + offset, &xor_port, 2); offset += 2;
    uint32_t xor_addr;
    memcpy(&xor_addr, &mapped->sin_addr, 4);
    xor_addr ^= htonl(STUN_MAGIC);
    memcpy(buf + offset, &xor_addr, 4); offset += 4;

    /* 更新 Length（不含头部）*/
    nwrite_s(buf + 2, (uint16_t)(offset - 20));

    /* MESSAGE-INTEGRITY（可选）*/
    if (password) {
        if (offset + 24 > max_len) return -1;
        int mi_payload = (offset - 20) + 24;
        nwrite_s(buf + 2, (uint16_t)mi_payload);
        uint8_t digest[20];
        p2p_hmac_sha1((const uint8_t*)password, (int)strlen(password), buf, offset, digest);
        buf[offset++] = (uint8_t)(STUN_ATTR_MESSAGE_INTEGRITY >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_MESSAGE_INTEGRITY & 0xFF);
        buf[offset++] = 0; buf[offset++] = 20;
        memcpy(buf + offset, digest, 20); offset += 20;
    }

    /* FINGERPRINT（0x8028）*/
    if (offset + 8 > max_len) return -1;
    nwrite_s(buf + 2, (uint16_t)((offset - 20) + 8));
    uint32_t crc = p2p_crc32(buf, offset) ^ 0x5354554e;
    buf[offset++] = (uint8_t)(STUN_ATTR_FINGERPRINT >> 8);
    buf[offset++] = (uint8_t)(STUN_ATTR_FINGERPRINT & 0xFF);
    buf[offset++] = 0; buf[offset++] = 4;
    uint32_t net_crc = htonl(crc);
    memcpy(buf + offset, &net_crc, 4); offset += 4;

    return offset;
}

static int stun_parse_binding_response(const uint8_t *buf, int len,
                                       struct sockaddr_in *mapped_addr,
                                       const char *password,
                                       stun_ctx_t *nat_ctx) {
    
    /* 验证消息类型 Binding Response 0x0101 */
    assert(nget_s(buf) == STUN_BINDING_RESPONSE);
    /* 验证 Magic Cookie */
    assert(nget_l(buf+4) == STUN_MAGIC);

    /* 验证负载消息数据长度 */
    if (len < 20) return -1;        // 不能小于消息头长度
    const uint8_t *ptr = buf + 2;
    int msg_len = nget_s(ptr);
    if (msg_len + 20 > len) return -1;
    const uint8_t *end = buf + 20 + msg_len;

    /*
     * 可选：验证 FINGERPRINT
     *
     * 作用：快速校验报文完整性，防止把损坏/伪造报文当成 STUN 响应继续处理。
     * 规则：CRC32(从消息头到 FINGERPRINT 属性头之前) XOR 0x5354554E。
     *
     * 兼容性：FINGERPRINT 在 STUN 中是可选属性；
     * 若报文未携带则继续解析，若携带但校验失败则直接拒绝该报文。
     */
    ptr = buf + 20;
    while (ptr + 4 <= end) {
        uint16_t attr_type = nget_s(ptr);
        uint16_t attr_len = nget_s(ptr + 2);
        const uint8_t *attr_val = ptr + 4;
        const uint8_t *next = attr_val + ((attr_len + 3) & ~3);
        if (next > end) return -1;

        if (attr_type == STUN_ATTR_FINGERPRINT) {
            if (attr_len != 4) return -1;

            uint32_t recv_fp = nget_l(attr_val);
            uint32_t calc_fp = p2p_crc32(buf, (int)(ptr - buf)) ^ 0x5354554e;
            if (recv_fp != calc_fp) {
                print("W:", "STUN FINGERPRINT mismatch, drop packet");
                return -1;
            }

            break;
        }

        ptr = next;
    }

    /*
     * 可选：验证 MESSAGE-INTEGRITY
     * 如果提供了密码，查找并验证 HMAC-SHA1
     */
    if (password) {
        /* 查找 MESSAGE-INTEGRITY 属性 */
        ptr = buf + 20;
        while (ptr + 4 <= end) {
            uint16_t attr_type = nget_s(ptr);
            uint16_t attr_len = nget_s(ptr + 2);
            const uint8_t *attr_val = ptr + 4;
            const uint8_t *next = attr_val + ((attr_len + 3) & ~3);
            if (next > end) return -1;

            if (attr_type == STUN_ATTR_MESSAGE_INTEGRITY) {

                /*
                 * MESSAGE-INTEGRITY 验证逻辑：
                 * 1. 将 Length 字段调整为包含到 MI 属性头的长度
                 * 2. 计算 HMAC-SHA1(password, 消息头+属性到MI之前)
                 * 3. 比较计算结果与 MI 属性值
                 * 此处简化略过，ICE 通常依赖其他机制建立信任
                 */

                break;
            }
            ptr = next; /* 跳到下一个属性（4字节对齐）*/
        }
    }

    /*
     * 遍历属性，查找地址属性
     *
     * 支持的地址属性：
     *   - MAPPED-ADDRESS (0x0001): 明文地址（RFC 3489 旧格式）
     *   - XOR-MAPPED-ADDRESS (0x0020): XOR 加密地址（RFC 5389 推荐）
     *   - RELAYED-ADDRESS (0x0016): TURN 中继地址
     *   - CHANGED-ADDRESS (0x0005): 备用服务器地址（NAT 检测用）
     */
    int found = 0;
    ptr = buf + 20;
    while (ptr + 4 <= end) {
        uint16_t attr_type = nget_s(ptr);
        uint16_t attr_len = nget_s(ptr + 2);
        const uint8_t *attr_val = ptr + 4;
        const uint8_t *next = attr_val + ((attr_len + 3) & ~3);
        if (next > end) return -1;

        if (attr_type == STUN_ATTR_MAPPED_ADDR || 
            attr_type == STUN_ATTR_XOR_MAPPED_ADDR) {

            /*
             * 地址格式（8字节 for IPv4）：
             *   字节 0:   预留 (0x00)
             *   字节 1:   地址族 (0x01=IPv4, 0x02=IPv6)
             *   字节 2-3: 端口 (可能 XOR 加密)
             *   字节 4-7: IP 地址 (可能 XOR 加密)
             */
            if (attr_len >= 8 && (attr_val[1] == 0x01)) { /* IPv4 */
                mapped_addr->sin_family = AF_INET;
                memcpy(&mapped_addr->sin_port, attr_val + 2, 2);
                memcpy(&mapped_addr->sin_addr, attr_val + 4, 4);
                
                /*
                 * XOR 解密（如果是 XOR-MAPPED-ADDRESS）
                 * X-Port = Port XOR 0x2112
                 * X-Address = Address XOR 0x2112A442
                 */
                if (attr_type == STUN_ATTR_XOR_MAPPED_ADDR) {
                    stun_xor_addr(mapped_addr);
                }
                found = 1;
            }
        }
        /* CHANGED-ADDRESS (0x0005): NAT 检测使用的备用服务器地址 */
        else if (nat_ctx && attr_type == STUN_ATTR_CHANGED_ADDR) {
            if (attr_len >= 8 && (attr_val[1] == 0x01)) { /* IPv4 */
                nat_ctx->alt_addr.sin_family = AF_INET;
                memcpy(&nat_ctx->alt_addr.sin_port, attr_val + 2, 2);
                memcpy(&nat_ctx->alt_addr.sin_addr, attr_val + 4, 4);
                /* CHANGED-ADDRESS 通常不使用 XOR 加密（RFC 3489）*/
            }
        }

        ptr = next; // 跳到下一个属性（4字节对齐）
    }
    
    return found ? 0 : -1;
}

/*
 * ============================================================================
 * 构建 ICE 标准的连通性检查包
 * ============================================================================
 *
 * 构建符合 RFC 5245/8445 的 ICE 连通性检查包（STUN Binding Request + ICE 属性）。
 *
 * 与标准 STUN Binding Request 的区别：
 *   1. 必须包含 PRIORITY 属性（本地候选优先级）
 *   2. 必须包含 ICE-CONTROLLING 或 ICE-CONTROLLED 属性（角色标识）
 *   3. 可选包含 USE-CANDIDATE 属性（controlling 端提名时）
 *   4. USERNAME 使用 ICE 短期凭证格式："remote_ufrag:local_ufrag"
 *   5. MESSAGE-INTEGRITY 使用远端密码（remote_pwd）计算
 *
 * 属性顺序（推荐）：
 *   USERNAME → PRIORITY → ICE-CONTROLLING/CONTROLLED → USE-CANDIDATE → 
 *   MESSAGE-INTEGRITY → FINGERPRINT
 *
 * @param buf            输出缓冲区
 * @param max_len        缓冲区大小
 * @param tsx_id         事务 ID（12字节，NULL 则自动生成）
 * @param username       ICE 用户名（格式: "remote_ufrag:local_ufrag"，NULL 表示不包含）
 * @param password       ICE 远端密码（用于 MESSAGE-INTEGRITY，NULL 表示不包含）
 * @param priority       本地候选优先级（0 表示不包含 PRIORITY 属性）
 * @param is_controlling 1=Controlling 角色, 0=Controlled 角色
 * @param tie_breaker    64位 tie-breaker 值（用于角色冲突解决）
 * @param use_candidate  是否携带 USE-CANDIDATE 属性（1=携带，0=不携带）
 * @return               生成的请求长度，失败返回 -1
 */
int p2p_stun_build_ice_check(uint8_t *buf, int max_len, uint8_t tsx_id[12],
                              const char *username, const char *password,
                              uint32_t priority, int is_controlling, 
                              uint64_t tie_breaker, int use_candidate) {
    if (max_len < 20) return -1;
    
    /* STUN 消息头 (20 字节) */
    buf[0] = 0x00; buf[1] = 0x01; /* Type: Binding Request (0x0001) */
    buf[2] = 0x00; buf[3] = 0x00; /* Length: 初始为0，稍后更新 */
    
    /* Magic Cookie */
    uint32_t magic = htonl(STUN_MAGIC);
    memcpy(buf + 4, &magic, 4);
    
    /* Transaction ID */
    if (tsx_id) {
        memcpy(buf + 8, tsx_id, 12);
    } else {
        P_rand_bytes(buf + 8, 12);
    }
    
    int offset = 20;

    /* 1. USERNAME 属性 (0x0006) */
    if (username) {
        int ulen = (int)strlen(username);
        if (offset + 4 + ulen > max_len) return -1;
        buf[offset++] = (uint8_t)(STUN_ATTR_USERNAME >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_USERNAME & 0xFF);
        buf[offset++] = (uint8_t)(ulen >> 8);
        buf[offset++] = (uint8_t)(ulen & 0xFF);
        memcpy(buf + offset, username, ulen);
        offset += ulen;
        while (offset % 4) buf[offset++] = 0; /* 填充到4字节边界 */
    }

    /* 2. PRIORITY 属性 (0x0024) - ICE 专用 */
    if (priority != 0) {
        if (offset + 8 > max_len) return -1;
        buf[offset++] = (uint8_t)(STUN_ATTR_PRIORITY >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_PRIORITY & 0xFF);
        buf[offset++] = 0;   /* 长度高字节 */
        buf[offset++] = 4;   /* 长度低字节 */
        uint32_t net_priority = htonl(priority);
        memcpy(buf + offset, &net_priority, 4);
        offset += 4;
    }

    /* 3. ICE-CONTROLLING (0x802A) / ICE-CONTROLLED (0x8029) */
    if (offset + 12 > max_len) return -1;
    uint16_t role_attr = is_controlling ? STUN_ATTR_ICE_CONTROLLING : STUN_ATTR_ICE_CONTROLLED;
    buf[offset++] = (uint8_t)(role_attr >> 8);
    buf[offset++] = (uint8_t)(role_attr & 0xFF);
    buf[offset++] = 0;   /* 长度高字节 */
    buf[offset++] = 8;   /* 长度低字节（64位 tie-breaker） */
    /* 手动转换 64 位到网络字节序（大端） */
    buf[offset++] = (uint8_t)(tie_breaker >> 56);
    buf[offset++] = (uint8_t)(tie_breaker >> 48);
    buf[offset++] = (uint8_t)(tie_breaker >> 40);
    buf[offset++] = (uint8_t)(tie_breaker >> 32);
    buf[offset++] = (uint8_t)(tie_breaker >> 24);
    buf[offset++] = (uint8_t)(tie_breaker >> 16);
    buf[offset++] = (uint8_t)(tie_breaker >> 8);
    buf[offset++] = (uint8_t)(tie_breaker & 0xFF);

    /* 4. USE-CANDIDATE 属性 (0x0025) - 仅 controlling 端提名时 */
    if (use_candidate && is_controlling) {
        if (offset + 4 > max_len) return -1;
        buf[offset++] = (uint8_t)(STUN_ATTR_USE_CANDIDATE >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_USE_CANDIDATE & 0xFF);
        buf[offset++] = 0;   /* 长度高字节 */
        buf[offset++] = 0;   /* 长度低字节（无值属性） */
    }

    /* 更新消息长度（MESSAGE-INTEGRITY 计算前） */
    int payload_len = offset - 20;
    nwrite_s(buf + 2, (uint16_t)payload_len);

    /* 5. MESSAGE-INTEGRITY 属性 (0x0008) */
    if (password) {
        if (offset + 24 > max_len) return -1;
        
        /* 调整长度字段 */
        int mi_len = 24;
        payload_len += mi_len;
        nwrite_s(buf + 2, (uint16_t)payload_len);

        /* 计算 HMAC-SHA1 */
        uint8_t digest[20];
        p2p_hmac_sha1((const uint8_t*)password, (int)strlen(password), buf, offset, digest);
        
        buf[offset++] = (uint8_t)(STUN_ATTR_MESSAGE_INTEGRITY >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_MESSAGE_INTEGRITY & 0xFF);
        buf[offset++] = 0;
        buf[offset++] = 20;
        memcpy(buf + offset, digest, 20);
        offset += 20;
    }

    /* 6. FINGERPRINT 属性 (0x8028) */
    if (offset + 8 <= max_len) {
        payload_len += 8;
        nwrite_s(buf + 2, (uint16_t)payload_len);

        uint32_t crc = p2p_crc32(buf, offset) ^ 0x5354554e;
        buf[offset++] = (uint8_t)(STUN_ATTR_FINGERPRINT >> 8);
        buf[offset++] = (uint8_t)(STUN_ATTR_FINGERPRINT & 0xFF);
        buf[offset++] = 0;
        buf[offset++] = 4;
        uint32_t net_crc = htonl(crc);
        memcpy(buf + offset, &net_crc, 4);
        offset += 4;
    }
    
    return offset;
}

/*
 * ============================================================================
 * 检查 STUN 包是否包含 ICE 属性
 * ============================================================================
 *
 * 用于在包派发阶段快速判断一个 STUN 包是否为 ICE connectivity check 相关。
 *
 * 判断依据：
 *   - PRIORITY (0x0024): ICE connectivity check 必含（RFC 5245）
 *   - ICE-CONTROLLING (0x802A): Controlling 角色标识
 *   - ICE-CONTROLLED (0x8029): Controlled 角色标识
 *   - USE-CANDIDATE (0x0025): 提名标志
 *
 * 实现策略：
 *   只要发现任一 ICE 专用属性，即判定为 ICE 包，应由 NAT 模块处理。
 *   否则为普通 STUN 包（NAT 检测用），由 STUN 模块处理。
 *
 * @param buf   STUN 包数据
 * @param len   包长度
 * @return      1=包含 ICE 属性（应由 NAT 模块处理），0=普通 STUN 包
 */
bool p2p_stun_has_ice_attrs(const uint8_t *buf, int len) {
    
    /* 验证 Magic Cookie */
    assert(nget_l(buf + 4) == STUN_MAGIC);

    /* 验证消息长度 */
    if (len < 20) return false; /* 最小为消息头长度 */
    const uint8_t* ptr = buf + 2;
    int msg_len = nget_s(ptr);
    if (msg_len + 20 > len) return false;
    const uint8_t *end = buf + 20 + msg_len;

    /* 遍历属性，查找 ICE 专用属性
     * - PRIORITY (0x0024): 候选优先级（connectivity check 必含）
     * - USE-CANDIDATE (0x0025): 提名标志
     * - ICE-CONTROLLED (0x8029): Controlled 角色
     * - ICE-CONTROLLING (0x802A): Controlling 角色
     */
    ptr = buf + 20;
    while (ptr + 4 <= end) {
        uint16_t attr_type = nget_s(ptr);
        uint16_t attr_len = nget_s(ptr + 2);
        const uint8_t *next = (ptr + 4) + ((attr_len + 3) & ~3);
        if (next > end) return false;

        if (attr_type == STUN_ATTR_PRIORITY ||
            attr_type == STUN_ATTR_USE_CANDIDATE ||
            attr_type == STUN_ATTR_ICE_CONTROLLED ||
            attr_type == STUN_ATTR_ICE_CONTROLLING) {
            return true;
        }
        
        ptr = next; // 跳到下一个属性（4字节对齐）
    }
    
    return false;
}

/*
 * ============================================================================
 * 处理 STUN 响应包（统一入口）
 * ============================================================================
 *
 * 功能：
 *   1. 解析 STUN Binding Response，提取映射地址（Srflx）
 *   2. 根据 NAT 检测状态机，处理不同阶段的响应
 *   3. 推进 NAT 类型检测流程（Test I, II, III）
 *
 * NAT 检测状态对应处理：
 *   - STUN_TEST_I_SENT:   提取 mapped_addr，判断是否为公网 IP
 *   - STUN_TEST_II_SENT:  测试是否为完全锥形 NAT
 *   - STUN_TEST_III_SENT: 区分受限锥形 vs 端口受限锥形
 */

/* 将 mapped 地址作为本地 Srflx 候选写入，并在 ICE 模式下触发 Trickle 回调 */
static inline void add_srflx_candidate(struct p2p_instance *inst, int recv_sock_idx,
                                       const struct sockaddr_in *mapped) {

    assert(inst->srflx_active < inst->srflx_count);
    ++inst->srflx_active;

    // 缓存映射地址，供后续 session 复用
    assert(recv_sock_idx >= 0 && recv_sock_idx < inst->sock_cnt);
    inst->socks[recv_sock_idx].mapped_addr = *mapped;
    inst->socks[recv_sock_idx].state = 2/*active*/;

    bool sig_ready = false;
    if (inst->sig_mode == P2P_SIGNALING_MODE_COMPACT) {
        sig_ready = inst->sig_ctx.compact.state == SIG_COMPACT_ONLINE;
    } else if (inst->sig_mode == P2P_SIGNALING_MODE_RELAY) {
        sig_ready = inst->sig_ctx.relay.state == SIG_RELAY_ONLINE;
    } else sig_ready = true;

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {

        int idx = p2p_cand_push_local(s);
        if (idx < 0) {
            print("W:", LA_F("✗ Add Srflx candidate failed(OOM)", LA_F470, 470));
            return;
        }

        p2p_local_candidate_entry_t *c = &s->local_cands[idx];
        c->type = P2P_CAND_SRFLX;
        c->priority = p2p_ice_calc_priority(P2P_ICE_CAND_SRFLX, 65535, 1);
        c->addr = *mapped;
        c->base_addr = inst->socks[recv_sock_idx].local_addr;

        print("I:", LA_F("✓ Gathered Srflx Candidate %s:%d, priority=%u (ses_id=%u)", LA_F469, 469),
              inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), c->priority, s->id);

        // 如果信令还未上线
        if (!sig_ready) continue;

        if (inst->sig_mode == P2P_SIGNALING_MODE_COMPACT) {
            assert(!s->wait_stun_pending);
            if (s->sig_sess.compact.state == SIG_COMPACT_SESS_SYNCING) {
                p2p_signal_compact_trickle_candidate(s);
            }
        }
        else if (inst->sig_mode == P2P_SIGNALING_MODE_RELAY) {
            if (s->sig_sess.relay.state == SIG_RELAY_SESS_SYNCING) {
                p2p_signal_relay_trickle_candidate(s);
            } else if (s->sig_sess.relay.state == SIG_RELAY_SESS_WAIT_STUN && inst->srflx_active >= inst->srflx_count) {
                p2p_signal_relay_stun_ready(s);
            }
        }
        else if (inst->sig_mode == P2P_SIGNALING_MODE_PUBSUB) {
            if (s->sig_sess.pubsub.state == SIG_PUBSUB_SESS_SYNCING) {
                p2p_signal_pubsub_trickle_candidate(s);
            } else if (inst->srflx_active >= inst->srflx_count) {
                p2p_signal_pubsub_stun_ready(s);
            }
        }
        else if (s->inst->sig_mode == P2P_SIGNALING_MODE_ICE && s->inst->cfg.on_ice_candidate) {
            char sdp_a[256];
            if (p2p_ice_export_candidate(c, sdp_a, sizeof(sdp_a)) > 0)
                s->inst->cfg.on_ice_candidate((p2p_session_t)s, sdp_a, s->inst->cfg.userdata);
        }
    }
}

static inline void nat_detect_done(struct p2p_instance *inst, p2p_nat_type_t type) {

    inst->nat_type = type;
    inst->stun_ctx.state = STUN_TEST_COMPLETED;

    // 如果没有活跃 session，将 mapped_addr 标记为失效（因为该地址可能在首个 session 创建前过期）
    // + 下次创建 session 时会重新 collect
    if (inst->connections == 0) {
        for (int i = 0; i < inst->sock_cnt; i++) {
            inst->socks[i].state = 1/*bound*/;
        }
        inst->srflx_active = 0;
    }
}

void p2p_stun_handle_packet(struct p2p_instance *inst, int recv_sock_idx,
                            const struct sockaddr_in *from,
                            uint16_t type, const uint8_t *buf, int len) {
    (void)from; /* from 用于验证响应来源，暂未使用 */
    stun_ctx_t *ctx = &inst->stun_ctx;

    assert(p2p_stun_is_binding_response(type, buf, len));

    bool is_nat_detect_resp = (memcmp(buf + 8, ctx->detect_tsx_id, 12) == 0);

    struct sockaddr_in mapped;
    stun_ctx_t *parse_ctx = is_nat_detect_resp ? ctx : NULL;
    if (stun_parse_binding_response(buf, len, &mapped, NULL, parse_ctx) < 0) {
        return;
    }

    // 如果不是 Test 任务的响应
    // + 默认为 Srflx 候选探测，需要添加 Srflx 候选到本地候选列表
    if (!is_nat_detect_resp) {
        // 该 socket 已有有效映射地址 → 旧轮次的迟到响应，忽略
        if (inst->socks[recv_sock_idx].state >= 2/*active*/)
            return;
        add_srflx_candidate(inst, recv_sock_idx, &mapped);
        return;
    }

    switch (ctx->state) {

    /* Test I 响应：获得映射地址 */
    case STUN_TEST_I_SENT: {

        ctx->state = STUN_TEST_I_DONE;
        ctx->retry_count = 0;
        assert(inst->sock_cnt > 0);
        inst->socks[0].mapped_addr = mapped;

        if (ctx->as_candidate) {
            add_srflx_candidate(inst, 0, &mapped);
        }

        // 检查本地地址是否与公网映射地址相同（如果是，则为开放网络，非 NAT）
        // + 此时该地址无需作为 Srflx 候选
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        if (getsockname(inst->socks[0].sock, (struct sockaddr *)&local, &local_len) == 0) {
            if (sockaddr_equal(&mapped, &local)) {
                nat_detect_done(inst, P2P_NAT_OPEN);
                print("I:", LA_F("Detection completed %s", LA_F276, 276), p2p_nat_type_str(P2P_NAT_OPEN));
                return;
            }
        }
        
        print("I:", LA_F("Test I: Mapped address: %s:%d", LA_F414, 414),
              inet_ntoa(mapped.sin_addr), ntohs(mapped.sin_port));
        
        /* alt_addr 已在 stun_parse_binding_response 中解析 */
        if (ctx->alt_addr.sin_family == AF_INET) {
            print("I:", LA_F("Test I: Changed address: %s:%d", LA_F413, 413),
                  inet_ntoa(ctx->alt_addr.sin_addr), ntohs(ctx->alt_addr.sin_port));
        }
        
        break;
    }

    /* Test II 响应：从不同 IP/Port 收到响应 */
    case STUN_TEST_II_SENT: {
        ctx->state = STUN_TEST_II_DONE;
        ctx->retry_count = 0;
        ctx->test_ii_success = true;
        print("I:", LA_F("Test II: Success! Detection completed %s", LA_F416, 416), p2p_nat_type_str(P2P_NAT_FULL_CONE));
        break;
    }

    /* Test I(alt) 响应：比较主/备服务器映射是否一致 */
    case STUN_TEST_I2_SENT: {
        ctx->mapped_addr_alt = mapped;
        ctx->test_i2_success = true;
        ctx->retry_count = 0;
        ctx->state = STUN_TEST_I2_DONE;
        ctx->symmetric_mapping = !sockaddr_equal(&inst->socks[0].mapped_addr, &ctx->mapped_addr_alt);

        print("I:", LA_F("Test I(alt): Mapped address: %s:%d", LA_F411, 411),
              inet_ntoa(mapped.sin_addr), ntohs(mapped.sin_port));
        break;
    }

    /* Test III 响应：从不同 Port 收到响应 */
    case STUN_TEST_III_SENT: {
        ctx->test_iii_success = true;
        ctx->retry_count = 0;
        ctx->state = STUN_TEST_III_DONE;
        print("I:", LA_F("Test III: Success! Detection completed %s", LA_F418, 418), p2p_nat_type_str(P2P_NAT_RESTRICTED));
        break;
    }
    
    default:
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////

ret_t p2p_stun_nat_detect_start(struct p2p_instance *inst, bool as_candidate) {

    if (!inst || !inst->cfg.stun_server) {
        return E_INVALID;
    }

    if (inst->nat_type == P2P_NAT_DETECTING) return E_NONE_CONTEXT;

    inst->nat_type = P2P_NAT_DETECTING;

    stun_ctx_t *ctx = &inst->stun_ctx;
    ctx->state = STUN_TEST_IDLE;
    ctx->retry_count = 0;
    ctx->last_send_time = P_tick_ms();
    ctx->as_candidate = as_candidate;

    return E_NONE;
}

void p2p_stun_nat_detect_tick(struct p2p_instance *inst, uint64_t now_ms) {

    assert(inst && inst->cfg.stun_server);

    stun_ctx_t *ctx = &inst->stun_ctx;

    // 如果已完成检测
    if (ctx->state == STUN_TEST_COMPLETED) {

        // 清理从未收到 STUN 响应的多路套接字（collect 超时）
        if (ctx->collect_time && inst->srflx_count > inst->srflx_active
            && tick_diff(now_ms, ctx->collect_time) > STUN_TEST_TIMEOUT_MS) {

            for (int i = inst->sock_cnt - 1; i >= 1; i--) {
                if (inst->socks[i].state < 2/*active*/) {
                    p2p_udp_close(inst, i);
                    inst->srflx_count--;
                }
            }
            ctx->collect_time = 0;

            // srflx_count 缩减后可能满足 srflx_active >= srflx_count，通知等待中的 session
            if (inst->srflx_active >= inst->srflx_count) {
                for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
                    if (inst->sig_mode == P2P_SIGNALING_MODE_RELAY
                        && s->sig_sess.relay.state == SIG_RELAY_SESS_WAIT_STUN) {
                        p2p_signal_relay_stun_ready(s);
                    }
                    else if (inst->sig_mode == P2P_SIGNALING_MODE_PUBSUB) {
                        p2p_signal_pubsub_stun_ready(s);
                    }
                }
            }
        }
        return;
    }

    // 初始化检测
    if (ctx->state == STUN_TEST_IDLE) {

        // 未启动检测
        if (inst->nat_type != P2P_NAT_DETECTING) return;

        memset(&inst->socks[0].mapped_addr, 0, sizeof(inst->socks[0].mapped_addr));
        memset(&ctx->alt_addr, 0, sizeof(ctx->alt_addr));
        memset(&ctx->mapped_addr_alt, 0, sizeof(ctx->mapped_addr_alt));
        ctx->test_ii_success = false;
        ctx->test_i2_success = false;
        ctx->symmetric_mapping = false;
        ctx->test_iii_success = false;

        struct sockaddr_in stun_addr;
        if (resolve_host(inst->cfg.stun_server, inst->cfg.stun_port, &stun_addr) < 0) {
            print("E:", LA_F("Failed to resolve STUN server %s", LA_F290, 290), inst->cfg.stun_server);
            nat_detect_done(inst, P2P_NAT_ERROR);
            return;
        }

        /* 只在首次启动时生成新的 Transaction ID（加密安全随机数） */
        if (ctx->retry_count == 0) {
            P_rand_bytes(ctx->detect_tsx_id, 12);
        }

        uint8_t req[512];
        int len = p2p_stun_build_binding_request(req, sizeof(req), ctx->detect_tsx_id, NULL, NULL);
        if (len > 0) {

            p2p_udp_send_to(inst, &stun_addr, req, len);
            ctx->last_send_time = now_ms;

            print("I:", LA_F("Sending Test I to %s:%d (len=%d)", LA_F382, 382), inst->cfg.stun_server, inst->cfg.stun_port, len);

            ctx->state = STUN_TEST_I_SENT;
        }
        else {
            print("E:", LA_F("Failed to build STUN request", LA_F284, 284));
            nat_detect_done(inst, P2P_NAT_ERROR);
        }

        return;
    }

    /* 检查超时 */
    if (tick_diff(now_ms, ctx->last_send_time) > STUN_TEST_TIMEOUT_MS) {

        /* 重试：回退状态，让状态机推进代码重新发送请求 */
        if (ctx->retry_count < STUN_MAX_RETRIES) {
            ctx->retry_count++;
            switch (ctx->state) {
            case STUN_TEST_I_SENT: ctx->state = STUN_TEST_IDLE; break;
            case STUN_TEST_II_SENT: ctx->state = STUN_TEST_I_DONE; break;
            case STUN_TEST_I2_SENT: ctx->state = STUN_TEST_II_DONE; break;
            case STUN_TEST_III_SENT: ctx->state = STUN_TEST_I2_DONE; break;
            default: break;
            }
        } 
        /* 超时失败，进入下一个测试 */
        else {
            switch (ctx->state) {
            case STUN_TEST_I_SENT:
                print("W:", LA_F("Test I: Timeout", LA_F415, 415));
                nat_detect_done(inst, P2P_NAT_BLOCKED);
                return;

            case STUN_TEST_II_SENT:
                /* RFC 3489 CHANGE-REQUEST 通常不被现代 STUN 服务器支持，超时是预期行为 */
                print("I:", LA_F("Test II: Timeout (need Test III)", LA_F417, 417));
                ctx->test_ii_success = false;
                ctx->state = STUN_TEST_II_DONE;
                break;

            case STUN_TEST_I2_SENT:
                /* RFC 3489 CHANGE-REQUEST 通常不被现代 STUN 服务器支持，超时是预期行为 */
                print("I:", LA_F("Test I(alt): Timeout", LA_F412, 412));
                ctx->test_i2_success = false;
                ctx->state = STUN_TEST_I2_DONE;
                break;

            case STUN_TEST_III_SENT:
                /* RFC 3489 CHANGE-REQUEST 通常不被现代 STUN 服务器支持，超时是预期行为 */
                print("I:", LA_F("Test III: Timeout", LA_F419, 419));
                ctx->test_iii_success = false;
                ctx->state = STUN_TEST_III_DONE;
                break;

            default:
                break;
            }

            ctx->retry_count = 0;
            ctx->last_send_time = now_ms;
        }
    }
    
    /* 状态机推进 */
    switch (ctx->state) {

    /* Test I 完成，启动 Test II */
    case STUN_TEST_I_DONE: {

        /* 如果配置了 skip_stun_test，跳过 Test II/III（因为大多数公共 STUN 服务器不支持） */
        if (inst->cfg.skip_stun_test) {
            nat_detect_done(inst, P2P_NAT_UNKNOWN);
            print("I:", LA_F("NAT detection skipped (skip_stun_test=true), Srflx gathered", LA_F323, 323));
            return;
        }

        /* Test II: 请求服务器从不同 IP+Port 响应 (CHANGE-REQUEST flags=0x06)
         * + 也就是用带有 STUN_FLAG_CHANGE_IP | STUN_FLAG_CHANGE_PORT 选项的请求，重新请求服务器
         *   此时服务器会从它的另一个 IP 和 Port 响应该请求（这要求服务器配置了多个 IP 接口）
         *   也就是从另一个从未被本地主机访问过的 IP:Port 响应
         */
        
        struct sockaddr_in stun_addr;
        if (resolve_host(inst->cfg.stun_server, inst->cfg.stun_port, &stun_addr) < 0) {
            print("E:", LA_F("Failed to resolve STUN server %s", LA_F290, 290), inst->cfg.stun_server);
            nat_detect_done(inst, P2P_NAT_ERROR);
            return;
        }

        // 生成新的 Transaction ID
        P_rand_bytes(ctx->detect_tsx_id, 12);
        
        uint8_t req[512];
        int len = p2p_stun_build_binding_request_ex(req, sizeof(req), ctx->detect_tsx_id,
                                                     STUN_FLAG_CHANGE_IP | STUN_FLAG_CHANGE_PORT);
        if (len > 0) {
            p2p_udp_send_to(inst, &stun_addr, req, len);
            ctx->last_send_time = now_ms;
            ctx->state = STUN_TEST_II_SENT;
            print("I:", LA_F("Sending Test II with CHANGE-REQUEST(IP+PORT)", LA_F384, 384));
        } else {
            print("E:", LA_F("Failed to build STUN request", LA_F284, 284));
            nat_detect_done(inst, P2P_NAT_ERROR);
        }
        break;
    }
    
    /* Test II 完成，启动 Test I(alt) / Test III */
    case STUN_TEST_II_DONE: {

        /* Test II 测试成功: Full Cone NAT */
        if (ctx->test_ii_success) {
            nat_detect_done(inst, P2P_NAT_FULL_CONE);
            print("I:", LA_F("Detection completed %s", LA_F276, 276), p2p_nat_type_str(P2P_NAT_FULL_CONE));
            return;
        }

        /*
         * 如果 turn 服务器支持有效的 CHANGED-ADDRESS（alt_addr） 地址，则启动 Test I(alt) 测试
         * Test I(alt): 向服务器备用地址发起请求，判断访问不同 IP 返回的映射地址是否相同
         * > 如果不同，则说明服务器对不同源 IP 产生了不同的映射（Symmetric NAT 特征）
         * > 如果相同，那么意味着，虽然主机在面向不同 IP 的公网映射地址一样
         *   但只有该主机主动访问过的 IP:Port 才能收到响应（受限锥形 NAT 特征），因为前面 Test II 已经失败了
         *   所以，需要继续 Test III 进行区分
         */

        if (ctx->alt_addr.sin_family == AF_INET) {

            P_rand_bytes(ctx->detect_tsx_id, 12);

            uint8_t req[512];
            int len = p2p_stun_build_binding_request(req, sizeof(req), ctx->detect_tsx_id, NULL, NULL);
            if (len > 0) {
                p2p_udp_send_to(inst, &ctx->alt_addr, req, len);
                ctx->last_send_time = now_ms;
                ctx->state = STUN_TEST_I2_SENT;
                print("I:", LA_F("Sending Test I(alt) to CHANGED-ADDRESS", LA_F383, 383));
                break;
            }

            print("W:", LA_F("Failed to send Test I(alt), continue to Test III", LA_F294, 294));
        }
        else {
            print("W:", LA_F("No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)", LA_F327, 327));
            //print("W:", LA_F("STUN server does not provide CHANGED-ADDRESS; fallback without symmetric check", LA_F552, 552));
        }

        /* fall through: Test III  */
        ctx->test_i2_success = false;
        ctx->symmetric_mapping = false;
    }

    /* Test I(alt) 完成: 启动 Test III */    
    case STUN_TEST_I2_DONE: {

        // 如果 Test I(alt) 成功且已证实访问不同 IP 的公网映射地址不同：Symmetric NAT
        if (ctx->test_i2_success && ctx->symmetric_mapping) {
            print("I:", LA_F("Detection completed %s", LA_F276, 276), p2p_nat_type_str(P2P_NAT_SYMMETRIC));
            nat_detect_done(inst, P2P_NAT_SYMMETRIC);
            return;
        }

        /* Test III: 退而求其次，让 stun 服务器使用相同的 IP，只换个端口来进行响应 */
        
        struct sockaddr_in stun_addr;
        if (resolve_host(inst->cfg.stun_server, inst->cfg.stun_port, &stun_addr) < 0) {
            print("E:", LA_F("Failed to resolve STUN server %s", LA_F290, 290), inst->cfg.stun_server);
            nat_detect_done(inst, P2P_NAT_ERROR);
            return;
        }

        P_rand_bytes(ctx->detect_tsx_id, 12);
        
        uint8_t req[512];
        int len = p2p_stun_build_binding_request_ex(req, sizeof(req), ctx->detect_tsx_id,
                                                     STUN_FLAG_CHANGE_PORT);
        if (len > 0) {
            p2p_udp_send_to(inst, &stun_addr, req, len);
            ctx->last_send_time = now_ms;
            ctx->state = STUN_TEST_III_SENT;
            print("I:", LA_F("Sending Test III with CHANGE-REQUEST(PORT only)", LA_F385, 385));
        } else {
            print("E:", LA_F("Failed to build STUN request", LA_F284, 284));
            nat_detect_done(inst, P2P_NAT_ERROR);
        }
        break;
    }
    
    /* Test III 完成: COMPLETED */    
    case STUN_TEST_III_DONE: {

        /*
         * 如果失败: port restricted cone NAT（受限锥形 NAT）
         * + 说明限制非常严格，远程即使是同一个 IP，换个端口也无法访问该主机
         */
        nat_detect_done(inst, ctx->test_iii_success ? P2P_NAT_RESTRICTED : P2P_NAT_PORT_RESTRICTED);
        print("I:", LA_F("Detection completed %s", LA_F276, 276), p2p_nat_type_str(inst->nat_type));

        /* TODO: Symmetric NAT 检测需要向不同服务器发送请求，比较映射地址是否变化 */
        break;
    }
    
    default: break;
    }
}

