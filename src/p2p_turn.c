
#define MOD_TAG "TURN"

#include "p2p_internal.h"

/* ============================================================================
 * TURN 消息类型定义（RFC 5766）
 * ============================================================================ */

/* Allocate 方法 (Method = 0x003) */
#define TURN_ALLOCATE_REQUEST   0x0003  /* Class=Request(00)   -> 0x0003 */
#define TURN_ALLOCATE_SUCCESS   0x0103  /* Class=Success(10)   -> 0x0103 */
#define TURN_ALLOCATE_ERROR     0x0113  /* Class=Error(11)     -> 0x0113 */

/* Refresh 方法 (Method = 0x004) */
#define TURN_REFRESH_REQUEST    0x0004
#define TURN_REFRESH_SUCCESS    0x0104
#define TURN_REFRESH_ERROR      0x0114

/* Send 方法 (Method = 0x006) - 仅 Indication */
#define TURN_SEND_INDICATION    0x0016  /* Class=Indication(01) -> 0x0016 */

/* Data 方法 (Method = 0x007) - 仅 Indication */
#define TURN_DATA_INDICATION    0x0017  /* Class=Indication(01) -> 0x0017 */

/* CreatePermission 方法 (Method = 0x008) */
#define TURN_CREATE_PERM_REQUEST  0x0008
#define TURN_CREATE_PERM_SUCCESS  0x0108
#define TURN_CREATE_PERM_ERROR    0x0118

/* ChannelBind 方法 (Method = 0x009) - 预留 */
#define TURN_CHANNEL_BIND_REQUEST  0x0009
#define TURN_CHANNEL_BIND_SUCCESS  0x0109
#define TURN_CHANNEL_BIND_ERROR    0x0119

/* ============================================================================
 * TURN/STUN 属性类型定义
 * ============================================================================ */

#define STUN_ATTR_MAPPED_ADDRESS       0x0001
#define STUN_ATTR_USERNAME             0x0006
#define STUN_ATTR_MESSAGE_INTEGRITY    0x0008
#define STUN_ATTR_ERROR_CODE           0x0009
#define STUN_ATTR_UNKNOWN_ATTRIBUTES   0x000A
#define STUN_ATTR_LIFETIME             0x000D
#define STUN_ATTR_XOR_PEER_ADDRESS     0x0012  /* 对端地址（Send/Data/CreatePermission） */
#define STUN_ATTR_DATA                 0x0013  /* 中继数据负载 */
#define STUN_ATTR_REALM                0x0014
#define STUN_ATTR_NONCE                0x0015
#define STUN_ATTR_XOR_RELAYED_ADDRESS  0x0016  /* 中继地址 */
#define STUN_ATTR_REQUESTED_TRANSPORT  0x0019
#define STUN_ATTR_XOR_MAPPED_ADDRESS   0x0020
#define STUN_ATTR_FINGERPRINT          0x8028

#define TRANSPORT_UDP  17

/* Refresh 提前量（在 lifetime 到期前 60 秒刷新） */
#define TURN_REFRESH_MARGIN_S  60

/* Permission 刷新间隔（权限 5 分钟过期，提前 1 分钟刷新） */
#define TURN_PERM_REFRESH_S    240

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/* 写入 STUN 属性头（4 字节），返回更新后的偏移 */
static int attr_hdr(uint8_t *buf, int off, uint16_t type, uint16_t len) {
    buf[off]   = (uint8_t)(type >> 8);
    buf[off+1] = (uint8_t)(type & 0xFF);
    buf[off+2] = (uint8_t)(len >> 8);
    buf[off+3] = (uint8_t)(len & 0xFF);
    return off + 4;
}

/* 填充到 4 字节对齐 */
static int pad4(uint8_t *buf, int off) {
    while (off & 3) buf[off++] = 0;
    return off;
}

/* 写入 STUN 消息头（20 字节） */
static void write_stun_hdr(uint8_t *buf, uint16_t type, uint16_t body_len) {
    buf[0] = (uint8_t)(type >> 8);
    buf[1] = (uint8_t)(type & 0xFF);
    buf[2] = (uint8_t)(body_len >> 8);
    buf[3] = (uint8_t)(body_len & 0xFF);
    uint32_t magic = htonl(STUN_MAGIC);
    memcpy(buf + 4, &magic, 4);
    P_rand_bytes(buf + 8, 12);
}

/* 更新 STUN 头部中的消息体长度字段 */
static void update_body_len(uint8_t *buf, int off) {
    uint16_t body = (uint16_t)(off - 20);
    buf[2] = (uint8_t)(body >> 8);
    buf[3] = (uint8_t)(body & 0xFF);
}

/*
 * 追加 MESSAGE-INTEGRITY (HMAC-SHA1, 24 bytes) + FINGERPRINT (CRC32, 8 bytes)
 *
 * MESSAGE-INTEGRITY 计算规则（RFC 5389 Section 15.4）:
 *   1. 先将 Length 字段设为「到 MI 结尾的长度」（包含 MI 头 + 值 = 24 字节）
 *   2. 以 key 为密钥对「从消息头到 MI 属性头之前」的数据做 HMAC-SHA1
 *   3. 再追加 FINGERPRINT: 将 Length 设为「到 FP 结尾的长度」，CRC32 XOR 0x5354554E
 */
static int append_integrity(uint8_t *buf, int off, const uint8_t key[16]) {
    /* MESSAGE-INTEGRITY */
    uint16_t mi_body = (uint16_t)(off - 20 + 24);
    buf[2] = (uint8_t)(mi_body >> 8);
    buf[3] = (uint8_t)(mi_body & 0xFF);

    uint8_t digest[20];
    p2p_hmac_sha1(key, 16, buf, off, digest);

    off = attr_hdr(buf, off, STUN_ATTR_MESSAGE_INTEGRITY, 20);
    memcpy(buf + off, digest, 20);
    off += 20;

    /* FINGERPRINT */
    uint16_t fp_body = (uint16_t)(off - 20 + 8);
    buf[2] = (uint8_t)(fp_body >> 8);
    buf[3] = (uint8_t)(fp_body & 0xFF);

    uint32_t crc = p2p_crc32(buf, off) ^ 0x5354554e;
    off = attr_hdr(buf, off, STUN_ATTR_FINGERPRINT, 4);
    uint32_t net_crc = htonl(crc);
    memcpy(buf + off, &net_crc, 4);
    off += 4;

    return off;
}

/*
 * 追加认证三元组: USERNAME + REALM + NONCE
 *
 * 长期凭证机制（RFC 5389 Section 10.2）:
 *   key = MD5(username ":" realm ":" password)
 *   这三个属性必须出现在 MESSAGE-INTEGRITY 之前
 */
static int append_auth_attrs(uint8_t *buf, int off, const turn_ctx_t *t, const char *user) {
    int ulen = (int)strlen(user);
    off = attr_hdr(buf, off, STUN_ATTR_USERNAME, (uint16_t)ulen);
    memcpy(buf + off, user, ulen);
    off = pad4(buf, off + ulen);

    int rlen = (int)strlen(t->realm);
    off = attr_hdr(buf, off, STUN_ATTR_REALM, (uint16_t)rlen);
    memcpy(buf + off, t->realm, rlen);
    off = pad4(buf, off + rlen);

    int nlen = (int)strlen(t->nonce);
    off = attr_hdr(buf, off, STUN_ATTR_NONCE, (uint16_t)nlen);
    memcpy(buf + off, t->nonce, nlen);
    off = pad4(buf, off + nlen);

    return off;
}

/*
 * 写入 XOR 地址属性（IPv4）
 *
 * XOR 编码规则（RFC 5389 Section 15.2）:
 *   X-Port    = Port XOR 0x2112 (Magic Cookie 高 16 位)
 *   X-Address = Address XOR 0x2112A442 (完整 Magic Cookie)
 */
static int append_xor_addr(uint8_t *buf, int off, uint16_t attr_type, const struct sockaddr_in *addr) {
    off = attr_hdr(buf, off, attr_type, 8);
    buf[off]   = 0;       /* reserved */
    buf[off+1] = 0x01;    /* family: IPv4 */
    uint16_t xport = ntohs(addr->sin_port) ^ 0x2112;
    buf[off+2] = (uint8_t)(xport >> 8);
    buf[off+3] = (uint8_t)(xport & 0xFF);
    uint32_t xaddr = ntohl(addr->sin_addr.s_addr) ^ STUN_MAGIC;
    buf[off+4] = (uint8_t)(xaddr >> 24);
    buf[off+5] = (uint8_t)(xaddr >> 16);
    buf[off+6] = (uint8_t)(xaddr >> 8);
    buf[off+7] = (uint8_t)(xaddr & 0xFF);
    return off + 8;
}

/*
 * 从属性值中解析 XOR 地址（IPv4）
 *
 * 返回: true=解析成功, false=非 IPv4 或长度不足
 */
static bool parse_xor_addr(const uint8_t *val, int len, struct sockaddr_in *out) {
    if (len < 8 || val[1] != 0x01) return false;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    memcpy(&out->sin_port, val + 2, 2);
    out->sin_port ^= htons(0x2112);
    memcpy(&out->sin_addr, val + 4, 4);
    uint32_t *ap = (uint32_t *)&out->sin_addr;
    *ap ^= htonl(STUN_MAGIC);
    return true;
}

/*
 * 计算长期凭证密钥: key = MD5(user ":" realm ":" pass)
 */
static void compute_key(turn_ctx_t *t, const char *user, const char *pass) {
    char concat[512];
    int n = snprintf(concat, sizeof(concat), "%s:%s:%s", user, t->realm, pass);
    if (n > 0 && n < (int)sizeof(concat)) {
        p2p_md5((const uint8_t *)concat, n, t->key);
        t->has_key = true;
    }
}

/* 解析 TURN 服务器地址 */
static bool resolve_server(turn_ctx_t *t, const char *host, int port) {
    memset(&t->server_addr, 0, sizeof(t->server_addr));
    t->server_addr.sin_family = AF_INET;
    t->server_addr.sin_port = htons(port ? port : 3478);
    struct hostent *he = gethostbyname(host);
    if (!he) return false;
    memcpy(&t->server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    return true;
}

/* 检查某个 IP 是否已有 Permission */
static bool has_permission(const turn_ctx_t *t, const struct sockaddr_in *addr) {
    for (int i = 0; i < t->perm_count; i++) {
        if (t->perms[i].sin_addr.s_addr == addr->sin_addr.s_addr)
            return true;
    }
    return false;
}

/* ============================================================================
 * Refresh Request（带认证）
 *
 * 用于延长 TURN 分配的生存时间。LIFETIME=0 可主动释放分配。
 *
 * 消息结构:
 *   [STUN Header (20)]
 *   [LIFETIME (8)]       — 可选，缺省使用服务器默认值
 *   [USERNAME + REALM + NONCE (variable)]
 *   [MESSAGE-INTEGRITY (24)]
 *   [FINGERPRINT (8)]
 * ============================================================================ */
static int turn_refresh(p2p_session_t *s) {
    turn_ctx_t *t = &s->turn;
    if (t->state != TURN_ALLOCATED || !t->has_key) return -1;
    if (!s->cfg.turn_user) return -1;

    print("V: %s", "TURN sending Refresh");

    uint8_t buf[768];
    write_stun_hdr(buf, TURN_REFRESH_REQUEST, 0);

    int off = 20;

    /* 认证 */
    off = append_auth_attrs(buf, off, t, s->cfg.turn_user);
    off = append_integrity(buf, off, t->key);

    return p2p_udp_send_to(s, &t->server_addr, buf, off);
}

/* ============================================================================
 * Allocate Request（带认证，401 后重试）
 *
 * 消息结构:
 *   [STUN Header (20)]
 *   [REQUESTED-TRANSPORT (8)]
 *   [USERNAME (4 + padded)]
 *   [REALM (4 + padded)]
 *   [NONCE (4 + padded)]
 *   [MESSAGE-INTEGRITY (24)]
 *   [FINGERPRINT (8)]
 * ============================================================================ */
static int allocate_auth(p2p_session_t *s) {
    turn_ctx_t *t = &s->turn;

    if (!t->has_key) {
        if (!s->cfg.turn_user || !s->cfg.turn_pass) {
            print("E:", LA_F("TURN auth required but no credentials configured", LA_F360, 360));
            t->state = TURN_FAILED;
            if (s->turn_pending > 0) s->turn_pending--;
            return -1;
        }
        compute_key(t, s->cfg.turn_user, s->cfg.turn_pass);
    }

    print("I: %s", "Retrying Allocate with long-term credentials");

    uint8_t buf[768];
    write_stun_hdr(buf, TURN_ALLOCATE_REQUEST, 0);

    int off = 20;

    /* REQUESTED-TRANSPORT: UDP */
    off = attr_hdr(buf, off, STUN_ATTR_REQUESTED_TRANSPORT, 4);
    buf[off] = TRANSPORT_UDP; buf[off+1] = 0; buf[off+2] = 0; buf[off+3] = 0;
    off += 4;

    /* USERNAME + REALM + NONCE */
    off = append_auth_attrs(buf, off, t, s->cfg.turn_user);

    /* MESSAGE-INTEGRITY + FINGERPRINT */
    off = append_integrity(buf, off, t->key);

    t->state = TURN_AUTHENTICATING;
    return p2p_udp_send_to(s, &t->server_addr, buf, off);
}


/* ============================================================================
 * 初始化
 * ============================================================================ */
void p2p_turn_init(turn_ctx_t *t) {
    memset(t, 0, sizeof(*t));
    t->state = TURN_IDLE;
}

void p2p_turn_reset(p2p_session_t *s) {
    turn_ctx_t *t = &s->turn;

    /* 主动释放 TURN 分配（Refresh lifetime=0，RFC 5766 Section 5） */
    if (t->state == TURN_ALLOCATED && t->has_key && s->cfg.turn_user) {
        uint8_t buf[768];
        write_stun_hdr(buf, TURN_REFRESH_REQUEST, 0);

        int off = 20;
        off = attr_hdr(buf, off, STUN_ATTR_LIFETIME, 4);
        buf[off] = 0; buf[off+1] = 0; buf[off+2] = 0; buf[off+3] = 0;
        off += 4;

        off = append_auth_attrs(buf, off, t, s->cfg.turn_user);
        off = append_integrity(buf, off, t->key);

        p2p_udp_send_to(s, &t->server_addr, buf, off);
        print("V: %s", "TURN Refresh(lifetime=0) sent");
    }

    memset(t, 0, sizeof(*t));
    t->state = TURN_IDLE;
}

/* ============================================================================
 * Allocate Request（首次，无认证）
 *
 * 消息结构:
 *   [STUN Header (20)]
 *   [REQUESTED-TRANSPORT (8)]
 *   Total: 28 bytes
 *
 * 大多数 TURN 服务器会回复 401 Unauthorized，要求认证
 * ============================================================================ */
int p2p_turn_allocate(p2p_session_t *s) {
    if (!s->cfg.turn_server) return -1;

    turn_ctx_t *t = &s->turn;

    if (!resolve_server(t, s->cfg.turn_server, s->cfg.turn_port)) {
        print("E:", LA_F("Failed to resolve TURN server: %s", LA_F248, 248), s->cfg.turn_server);
        t->state = TURN_FAILED;
        return -1;
    }

    print("I:", LA_F("Sending Allocate Request to %s:%d", LA_F325, 325),
                 s->cfg.turn_server, s->cfg.turn_port ? s->cfg.turn_port : 3478);

    uint8_t buf[64];
    write_stun_hdr(buf, TURN_ALLOCATE_REQUEST, 8);

    /* REQUESTED-TRANSPORT: UDP (17) */
    int off = 20;
    off = attr_hdr(buf, off, STUN_ATTR_REQUESTED_TRANSPORT, 4);
    buf[off] = TRANSPORT_UDP; buf[off+1] = 0; buf[off+2] = 0; buf[off+3] = 0;
    off += 4;

    int ret = p2p_udp_send_to(s, &t->server_addr, buf, off);
    if (ret <= 0) return -1;
    t->state = TURN_ALLOCATING;
    s->turn_pending++;
    return 0;
}

/* ============================================================================
 * Send Indication（无认证）
 *
 * 通过 TURN 中继向对端发送数据。
 * Indication 不需要 MESSAGE-INTEGRITY（RFC 5766 Section 10.1）。
 *
 * 消息结构:
 *   [STUN Header (20)]
 *   [XOR-PEER-ADDRESS (12)]
 *   [DATA-HDR (2+2)]
 * ============================================================================ */
ret_t p2p_turn_send_indication(p2p_session_t *s, const struct sockaddr_in *peer_addr,
                               const sock_msg_t msg[4], int num) {
    if (num > 4) return E_INVALID;
    turn_ctx_t *t = &s->turn;
    if (t->state != TURN_ALLOCATED) return E_NONE_CONTEXT;

    sock_msg_t msgs[6]; int len = 0;
    for(int i=0;i<num;++i) {
        msgs[1+i] = msg[i]; len += P_msg_len(&msg[i]);
    }

    uint8_t buf[20 + 12 + 4/* header(20) + xor-peer(12) + data-attr-hdr(2+2) */];
    if (len > P2P_MTU - (int)sizeof(buf)) return E_OUT_OF_CAPACITY;
    P_msg_set(&msgs[0], buf, sizeof(buf));

    // header
    write_stun_hdr(buf, TURN_SEND_INDICATION, 0); int off = 20; 

    // XOR-PEER-ADDRESS: 指定数据目标地址
    off = append_xor_addr(buf, off, STUN_ATTR_XOR_PEER_ADDRESS, peer_addr);

    // DATA: 实际负载
    off = attr_hdr(buf, off, STUN_ATTR_DATA, (uint16_t)len) + len;

    // 确保消息体长度为 4 字节对齐（RFC 5766 Section 11.3），不足部分填充 0
    char padding[3] = {0};  // 4 字节对齐填充
    if (len & 3) {
        P_msg_set(&msgs[++num], padding, len = 4 - (len & 3));
        off += len;
    }

    // 更新消息体长度
    update_body_len(buf, off);

    return P_msg_send_to(s->sock, msgs, num+1, &t->server_addr);
}

/* ============================================================================
 * 处理 TURN 服务器响应
 *
 * 返回值:
 *   0 = 控制消息已处理
 *   1 = Data Indication（out_data/out_len/out_peer 已填充）
 *  -1 = 非 TURN 消息
 * ============================================================================ */
int p2p_turn_handle_packet(p2p_session_t *s, const uint8_t *buf, int len,
                           const struct sockaddr_in *from,
                           const uint8_t **out_data, int *out_len,
                           struct sockaddr_in *out_peer) {
    if (len < 20) return -1;

    /* 来源验证：仅处理来自 TURN 服务器的包（防止伪造） */
    if (s->turn.server_addr.sin_addr.s_addr &&
        from->sin_addr.s_addr != s->turn.server_addr.sin_addr.s_addr)
        return -1;

    turn_ctx_t *t = &s->turn;
    uint16_t type    = (buf[0] << 8) | buf[1];
    uint16_t msg_len = (buf[2] << 8) | buf[3];

    /* 安全边界：消息体不超过实际接收长度 */
    if (20 + msg_len > len) msg_len = (uint16_t)(len - 20);

    /* ----------------------------------------------------------------
     * Allocate Success Response (0x0103)
     * ---------------------------------------------------------------- */
    if (type == TURN_ALLOCATE_SUCCESS) {
        struct sockaddr_in relay = {0};
        uint32_t lifetime = 600;

        int off = 20;
        while (off + 4 <= 20 + msg_len) {
            uint16_t at = (buf[off] << 8) | buf[off+1];
            uint16_t al = (buf[off+2] << 8) | buf[off+3];
            off += 4;
            if (off + al > 20 + msg_len) break;  /* truncated attr */

            if (at == STUN_ATTR_XOR_RELAYED_ADDRESS) {
                parse_xor_addr(buf + off, al, &relay);
            }
            else if (at == STUN_ATTR_LIFETIME && al >= 4) {
                lifetime = ((uint32_t)buf[off]<<24) | ((uint32_t)buf[off+1]<<16) |
                           ((uint32_t)buf[off+2]<<8) | buf[off+3];
            }

            off += (al + 3) & ~3;
        }

        if (relay.sin_family != AF_INET) {
            print("E: %s", "TURN Allocate success but no relay address found");
            return 0;
        }

        t->relay_addr     = relay;
        t->lifetime       = lifetime;
        t->alloc_time_ms  = P_tick_ms();
        t->last_refresh_ms = t->alloc_time_ms;
        t->last_perm_ms   = t->alloc_time_ms;
        t->state          = TURN_ALLOCATED;

        print("I:", LA_F("TURN Allocated relay %s:%u (lifetime=%us)", LA_F354, 354),
              inet_ntoa(relay.sin_addr), ntohs(relay.sin_port), lifetime);

        /* 将中继地址加入本地 ICE 候选 */
        int idx = p2p_cand_push_local(s);
        if (idx < 0) {
            print("E: %s", "Push TURN relay candidate failed(OOM)");
            return -1;
        }
        
        p2p_local_candidate_entry_t *c = &s->local_cands[idx];
        c->type = P2P_CAND_RELAY;
        c->addr = relay;
        c->priority = p2p_ice_calc_priority(P2P_ICE_CAND_RELAY, 65535, 1);

        print("I:", LA_F("Gathered Relay Candidate %s:%u (priority=%u)", LA_F256, 256),
                inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), c->priority);

        if (s->turn_pending > 0) s->turn_pending--;

        if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT)
            p2p_signal_compact_trickle_turn(s);
        if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY)
            p2p_signal_relay_trickle_turn(s);

        // ICE 模式通过应用层回调接口来协商发送候选
        else if (s->signaling_mode == P2P_SIGNALING_MODE_ICE && s->cfg.on_ice_candidate) {
            char cand_str[256];
            if (p2p_ice_export_candidate_entry(c, cand_str, sizeof(cand_str)) > 0)
                s->cfg.on_ice_candidate((p2p_handle_t)s, cand_str, s->cfg.userdata);
        }

        return 0;
    }

    /* ----------------------------------------------------------------
     * Allocate Error Response (0x0113)
     *
     * ERROR-CODE 属性格式 (RFC 5389 Section 15.6):
     *   字节 0-1: 保留
     *   字节 2:   Class (百位数字, 3-6)
     *   字节 3:   Number (0-99)
     *   错误码 = Class * 100 + Number
     *
     * 401 Unauthorized: 需要认证（包含 REALM + NONCE）
     * 438 Stale Nonce: Nonce 过期，需使用新 Nonce 重试
     * ---------------------------------------------------------------- */
    if (type == TURN_ALLOCATE_ERROR) {
        int error_code = 0;
        char realm[128] = {0};
        char nonce[128] = {0};

        int off = 20;
        while (off + 4 <= 20 + msg_len) {
            uint16_t at = (buf[off] << 8) | buf[off+1];
            uint16_t al = (buf[off+2] << 8) | buf[off+3];
            off += 4;
            if (off + al > 20 + msg_len) break;  /* truncated attr */

            if (at == STUN_ATTR_ERROR_CODE && al >= 4) {
                error_code = (buf[off+2] & 0x07) * 100 + buf[off+3];
            }
            else if (at == STUN_ATTR_REALM && al > 0 && al < (int)sizeof(realm)) {
                memcpy(realm, buf + off, al);
                realm[al] = '\0';
            }
            else if (at == STUN_ATTR_NONCE && al > 0 && al < (int)sizeof(nonce)) {
                memcpy(nonce, buf + off, al);
                nonce[al] = '\0';
            }

            off += (al + 3) & ~3;
        }

        /* 401: 首次认证挑战 */
        if (error_code == 401 && t->state == TURN_ALLOCATING && realm[0] && nonce[0]) {
            print("I:", LA_F("TURN 401 Unauthorized (realm=%s), authenticating...", LA_F352, 352), realm);
            strncpy(t->realm, realm, sizeof(t->realm) - 1);
            strncpy(t->nonce, nonce, sizeof(t->nonce) - 1);
            t->has_key = false;  /* realm 变化需重新计算 key */
            allocate_auth(s);
            return 0;
        }

        /* 438: Stale Nonce — 更新 nonce 后重试 */
        if (error_code == 438 && nonce[0]) {
            print("I: %s", "TURN 438 Stale Nonce, retrying...");
            strncpy(t->nonce, nonce, sizeof(t->nonce) - 1);
            allocate_auth(s);
            return 0;
        }

        print("E:", LA_F("TURN Allocate failed with error %d", LA_F353, 353), error_code);
        t->state = TURN_FAILED;
        if (s->turn_pending > 0) s->turn_pending--;
        return 0;
    }

    /* ----------------------------------------------------------------
     * CreatePermission Success (0x0108)
     * ---------------------------------------------------------------- */
    if (type == TURN_CREATE_PERM_SUCCESS) {
        print("V: %s", "TURN CreatePermission successful");
        return 0;
    }

    /* ----------------------------------------------------------------
     * CreatePermission Error (0x0118)
     * ---------------------------------------------------------------- */
    if (type == TURN_CREATE_PERM_ERROR) {
        int error_code = 0;
        int off = 20;
        while (off + 4 <= 20 + msg_len) {
            uint16_t at = (buf[off] << 8) | buf[off+1];
            uint16_t al = (buf[off+2] << 8) | buf[off+3];
            off += 4;
            if (off + al > 20 + msg_len) break;
            if (at == STUN_ATTR_ERROR_CODE && al >= 4)
                error_code = (buf[off+2] & 0x07) * 100 + buf[off+3];
            off += (al + 3) & ~3;
        }
        print("E:", LA_F("TURN CreatePermission failed (error=%d)", LA_F355, 355), error_code);
        return 0;
    }

    /* ----------------------------------------------------------------
     * Refresh Success (0x0104)
     * ---------------------------------------------------------------- */
    if (type == TURN_REFRESH_SUCCESS) {
        uint32_t lifetime = 600;
        int off = 20;
        while (off + 4 <= 20 + msg_len) {
            uint16_t at = (buf[off] << 8) | buf[off+1];
            uint16_t al = (buf[off+2] << 8) | buf[off+3];
            off += 4;
            if (off + al > 20 + msg_len) break;
            if (at == STUN_ATTR_LIFETIME && al >= 4) {
                lifetime = ((uint32_t)buf[off]<<24) | ((uint32_t)buf[off+1]<<16) |
                           ((uint32_t)buf[off+2]<<8) | buf[off+3];
            }
            off += (al + 3) & ~3;
        }
        t->lifetime = lifetime;
        t->last_refresh_ms = P_tick_ms();
        print("V:", LA_F("TURN Refresh ok (lifetime=%us)", LA_F359, 359), lifetime);
        return 0;
    }

    /* ----------------------------------------------------------------
     * Refresh Error (0x0114)
     * ---------------------------------------------------------------- */
    if (type == TURN_REFRESH_ERROR) {
        int error_code = 0;
        char nonce[128] = {0};
        int off = 20;
        while (off + 4 <= 20 + msg_len) {
            uint16_t at = (buf[off] << 8) | buf[off+1];
            uint16_t al = (buf[off+2] << 8) | buf[off+3];
            off += 4;
            if (off + al > 20 + msg_len) break;
            if (at == STUN_ATTR_ERROR_CODE && al >= 4)
                error_code = (buf[off+2] & 0x07) * 100 + buf[off+3];
            else if (at == STUN_ATTR_NONCE && al > 0 && al < (int)sizeof(nonce)) {
                memcpy(nonce, buf + off, al);
                nonce[al] = '\0';
            }
            off += (al + 3) & ~3;
        }
        /* 438: 更新 nonce 后重试 Refresh */
        if (error_code == 438 && nonce[0]) {
            strncpy(t->nonce, nonce, sizeof(t->nonce) - 1);
            turn_refresh(s);
            return 0;
        }
        print("E:", LA_F("TURN Refresh failed (error=%d)", LA_F358, 358), error_code);
        return 0;
    }

    /* ----------------------------------------------------------------
     * Data Indication (0x0017) — 中继数据接收
     *
     * 当对端向我方的中继地址发送数据时，TURN 服务器将数据
     * 封装为 Data Indication 转发给我方。
     *
     * 包含的属性:
     *   - XOR-PEER-ADDRESS (0x0012): 数据的原始发送方地址
     *   - DATA (0x0013): 原始数据内容
     * ---------------------------------------------------------------- */
    if (type == TURN_DATA_INDICATION) {
        struct sockaddr_in peer = {0};
        const uint8_t *data = NULL;
        int data_len = 0;

        int off = 20;
        while (off + 4 <= 20 + msg_len) {
            uint16_t at = (buf[off] << 8) | buf[off+1];
            uint16_t al = (buf[off+2] << 8) | buf[off+3];
            off += 4;
            if (off + al > 20 + msg_len) break;  /* truncated attr */

            if (at == STUN_ATTR_XOR_PEER_ADDRESS) {
                parse_xor_addr(buf + off, al, &peer);
            }
            else if (at == STUN_ATTR_DATA) {
                data = buf + off;
                data_len = al;
            }

            off += (al + 3) & ~3;
        }

        if (data && data_len > 0 && peer.sin_family == AF_INET) {
            print("V:", LA_F("TURN Data Indication from %s:%u (%d bytes)", LA_F357, 357),
                  inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), data_len);

            if (out_data)  *out_data = data;
            if (out_len)   *out_len  = data_len;
            if (out_peer)  *out_peer = peer;
            return 1;   /* 通知调用方处理内部数据 */
        }
        return 0;
    }

    return -1;  /* 非 TURN 消息 */
}

/* ============================================================================
 * CreatePermission Request（带认证）
 *
 * 必须为对端 IP 创建权限，TURN 服务器才会中继来自该 IP 的数据。
 * 权限有效期 5 分钟（RFC 5766 Section 8），需定期刷新。
 *
 * 消息结构:
 *   [STUN Header (20)]
 *   [XOR-PEER-ADDRESS (12)]
 *   [USERNAME + REALM + NONCE (variable)]
 *   [MESSAGE-INTEGRITY (24)]
 *   [FINGERPRINT (8)]
 * ============================================================================ */
static int turn_create_permission(p2p_session_t *s, const struct sockaddr_in *peer_addr) {
    turn_ctx_t *t = &s->turn;
    if (t->state != TURN_ALLOCATED || !t->has_key) return -1;
    if (!s->cfg.turn_user) return -1;

    /* 去重：同一 IP 不重复发送（端口无关，RFC 5766 Section 9.1） */
    if (has_permission(t, peer_addr)) return 0;

    print("V:", LA_F("TURN CreatePermission for %s", LA_F356, 356),
          inet_ntoa(((struct sockaddr_in *)peer_addr)->sin_addr));

    uint8_t buf[768];
    write_stun_hdr(buf, TURN_CREATE_PERM_REQUEST, 0);

    int off = 20;

    /* XOR-PEER-ADDRESS */
    off = append_xor_addr(buf, off, STUN_ATTR_XOR_PEER_ADDRESS, peer_addr);

    /* 认证 */
    off = append_auth_attrs(buf, off, t, s->cfg.turn_user);
    off = append_integrity(buf, off, t->key);

    int ret = p2p_udp_send_to(s, &t->server_addr, buf, off);
    if (ret > 0 && t->perm_count < TURN_MAX_PERMISSIONS) {
        t->perms[t->perm_count] = *peer_addr;
        t->perm_count++;
    }
    return ret;
}

/* ============================================================================
 * 周期状态机推进
 *
 * 由 p2p_update() 周期调用，负责:
 *   1. Refresh 续期（在 lifetime 到期前 60s 触发）
 *   2. 权限同步（为新到达的远端候选创建 CreatePermission）
 * ============================================================================ */
void p2p_turn_tick(p2p_session_t *s, uint64_t now_ms) {
    turn_ctx_t *t = &s->turn;
    if (t->state != TURN_ALLOCATED) return;

    /* ---- Refresh 续期 ---- */
    uint64_t elapsed_s = tick_diff(now_ms, t->last_refresh_ms) / 1000;
    uint32_t margin = t->lifetime > TURN_REFRESH_MARGIN_S ? TURN_REFRESH_MARGIN_S : t->lifetime / 2;
    if (elapsed_s + margin >= t->lifetime) {
        turn_refresh(s);
    }

    /* ---- 权限刷新：权限 5 分钟过期，每 4 分钟重新创建全部权限 ---- */
    if (t->perm_count > 0 && tick_diff(now_ms, t->last_perm_ms) / 1000 >= TURN_PERM_REFRESH_S) {
        t->perm_count = 0;
        t->perm_cand_synced = 0;
        t->last_perm_ms = now_ms;
    }

    /* ---- 权限同步：为新的远端候选创建 Permission ---- */
    while (t->perm_cand_synced < s->remote_cand_cnt) {
        const struct sockaddr_in *addr = &s->remote_cands[t->perm_cand_synced].addr;
        turn_create_permission(s, addr);
        t->perm_cand_synced++;
    }
}
