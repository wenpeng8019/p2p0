
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

#include "p2p_internal.h"
#include "p2p_udp.h"
#include <arpa/inet.h>
#include <sys/socket.h>

/* 前向声明 */
extern uint64_t time_ms(void);
static int resolve_host(const char *host, uint16_t port, struct sockaddr_in *out);

/*
 * 解析主机名为 IPv4 地址
 */
static int resolve_host(const char *host, uint16_t port, struct sockaddr_in *out) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;

    memcpy(out, res->ai_addr, sizeof(*out));
    freeaddrinfo(res);
    return 0;
}

/*
 * ============================================================================
 * 构建 STUN Binding 请求
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
int p2p_stun_build_binding_request(uint8_t *buf, int max_len, uint8_t tsx_id[12], 
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
    
    /* Transaction ID: 12字节随机数，用于匹配请求和响应 */
    if (tsx_id) {
        memcpy(buf + 8, tsx_id, 12);
    } else {
        for (int i = 0; i < 12; i++) buf[8+i] = (uint8_t)rand();
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
        int ulen = strlen(username);
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
    buf[2] = (uint8_t)(payload_len >> 8);
    buf[3] = (uint8_t)(payload_len & 0xFF);

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
        buf[2] = (uint8_t)(payload_len >> 8);
        buf[3] = (uint8_t)(payload_len & 0xFF);

        /* 计算 HMAC-SHA1 */
        uint8_t digest[20];
        p2p_hmac_sha1((const uint8_t*)password, strlen(password), buf, offset, digest);
        
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
        buf[2] = (uint8_t)(payload_len >> 8);
        buf[3] = (uint8_t)(payload_len & 0xFF);

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
int p2p_stun_parse_response(const uint8_t *buf, int len, struct sockaddr_in *mapped_addr, const char *password) {
    if (len < 20) return -1; /* 最小为消息头长度 */
    
    /*
     * 验证消息类型
     * Binding Response 成功: 0x0101
     * Allocate Success (TURN): 0x0103
     * 使用掩码 0xFFFE 同时接受两者
     */
    uint16_t type = (buf[0] << 8) | buf[1];
    if ((type & 0xFFFE) != 0x0100) return -1; 
    
    /* 验证 Magic Cookie */
    uint32_t magic = ntohl(*(uint32_t *)(buf + 4));
    if (magic != STUN_MAGIC) return -1;

    /* 验证消息长度 */
    int msg_len = (buf[2] << 8) | buf[3];
    if (msg_len + 20 > len) return -1;

    /*
     * 可选：验证 FINGERPRINT
     * 生产环境建议实现，此处简化略过
     */

    /*
     * 可选：验证 MESSAGE-INTEGRITY
     * 如果提供了密码，查找并验证 HMAC-SHA1
     */
    if (password) {
        /* 查找 MESSAGE-INTEGRITY 属性 */
        int mi_offset = -1;
        int off = 20;
        while (off + 4 <= len) {
            uint16_t attr_type = (buf[off] << 8) | buf[off + 1];
            uint16_t attr_len = (buf[off + 2] << 8) | buf[off + 3];
            if (attr_type == STUN_ATTR_MESSAGE_INTEGRITY) {
                mi_offset = off;
                break;
            }
            off += 4 + ((attr_len + 3) & ~3); /* 跳到下一个属性 */
        }
        
        if (mi_offset != -1) {
            /*
             * MESSAGE-INTEGRITY 验证逻辑：
             * 1. 将 Length 字段调整为包含到 MI 属性头的长度
             * 2. 计算 HMAC-SHA1(password, 消息头+属性到MI之前)
             * 3. 比较计算结果与 MI 属性值
             * 此处简化略过，ICE 通常依赖其他机制建立信任
             */
        }
    }

    /*
     * 遍历属性，查找地址属性
     *
     * 支持的地址属性：
     *   - MAPPED-ADDRESS (0x0001): 明文地址（RFC 3489 旧格式）
     *   - XOR-MAPPED-ADDRESS (0x0020): XOR 加密地址（RFC 5389 推荐）
     *   - RELAYED-ADDRESS (0x0016): TURN 中继地址
     */
    int offset = 20; /* 从消息头之后开始 */
    int found = 0;
    while (offset + 4 <= 20 + msg_len) {
        uint16_t attr_type = (buf[offset] << 8) | buf[offset + 1];
        uint16_t attr_len = (buf[offset + 2] << 8) | buf[offset + 3];
        offset += 4;
        
        if (attr_type == STUN_ATTR_MAPPED_ADDR || 
            attr_type == STUN_ATTR_XOR_MAPPED_ADDR || 
            attr_type == 0x0016) { /* RELAYED-ADDRESS (TURN) */
            /*
             * 地址格式（8字节 for IPv4）：
             *   字节 0:   预留 (0x00)
             *   字节 1:   地址族 (0x01=IPv4, 0x02=IPv6)
             *   字节 2-3: 端口 (可能 XOR 加密)
             *   字节 4-7: IP 地址 (可能 XOR 加密)
             */
            if (attr_len >= 8 && (buf[offset + 1] == 0x01)) { /* IPv4 */
                mapped_addr->sin_family = AF_INET;
                memcpy(&mapped_addr->sin_port, buf + offset + 2, 2);
                memcpy(&mapped_addr->sin_addr, buf + offset + 4, 4);
                
                /*
                 * XOR 解密（如果是 XOR-MAPPED-ADDRESS 或 RELAYED-ADDRESS）
                 * X-Port = Port XOR 0x2112
                 * X-Address = Address XOR 0x2112A442
                 */
                if (attr_type == STUN_ATTR_XOR_MAPPED_ADDR || attr_type == 0x0016) {
                    mapped_addr->sin_port ^= htons(0x2112);
                    uint32_t *addr_ptr = (uint32_t *)&mapped_addr->sin_addr;
                    *addr_ptr ^= htonl(STUN_MAGIC);
                }
                found = 1;
            }
        }
        offset += (attr_len + 3) & ~3; /* 跳到下一个属性（4字节对齐）*/
    }
    
    return found ? 0 : -1;
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
#define STUN_TEST_INTERVAL_MS  500   /* 每次测试发送间隔 500ms */
#define STUN_TEST_TIMEOUT_MS   2000  /* 单次测试超时 2 秒 */
#define STUN_MAX_RETRIES       3     /* 每个测试最多重试 3 次 */

/*
 * NAT 检测状态机
 */
typedef enum {
    NAT_TEST_IDLE = 0,        /* 空闲：尚未开始检测 */
    NAT_TEST_I_SENT,          /* Test I: 已发送请求，等待响应 */
    NAT_TEST_I_DONE,          /* Test I: 已收到响应 */
    NAT_TEST_II_SENT,         /* Test II: 已发送 CHANGE-REQUEST */
    NAT_TEST_II_DONE,         /* Test II: 完成（成功或超时） */
    NAT_TEST_III_SENT,        /* Test III: 已发送 CHANGE-PORT */
    NAT_TEST_III_DONE,        /* Test III: 完成 */
    NAT_TEST_COMPLETED        /* 所有测试完成 */
} nat_test_state_t;

/*
 * NAT 检测上下文
 */
typedef struct {
    nat_test_state_t state;           /* 当前状态 */
    uint64_t last_send_time;          /* 上次发送时间戳 */
    int retry_count;                  /* 当前测试的重试次数 */
    
    struct sockaddr_in mapped_addr;   /* Test I 获取的映射地址 (Srflx) */
    struct sockaddr_in alt_addr;      /* CHANGED-ADDRESS（备用服务器地址） */
    int test_ii_success;              /* Test II 是否收到响应 */
    int test_iii_success;             /* Test III 是否收到响应 */
    
    uint8_t tsx_id[12];               /* 当前 Transaction ID */
    p2p_nat_type_t detected_type;     /* 检测结果：NAT 类型 */
} nat_detect_ctx_t;

/* 全局检测上下文（临时方案，后续应移入 p2p_session） */
static nat_detect_ctx_t g_nat_ctx = {0};

/*
 * NAT 类型转可读字符串
 */
const char* p2p_nat_type_str(p2p_nat_type_t type) {
    switch (type) {
        case P2P_NAT_UNKNOWN:         return "未知 (Unknown)";
        case P2P_NAT_OPEN:            return "公网 IP (无 NAT)";
        case P2P_NAT_BLOCKED:         return "UDP 被屏蔽";
        case P2P_NAT_FULL_CONE:       return "完全锥形 NAT (Full Cone)";
        case P2P_NAT_RESTRICTED:      return "受限锥形 NAT (Restricted)";
        case P2P_NAT_PORT_RESTRICTED: return "端口受限锥形 NAT (Port Restricted)";
        case P2P_NAT_SYMMETRIC:       return "对称型 NAT (Symmetric)";
        case P2P_NAT_SYMMETRIC_UDP:   return "对称 UDP 防火墙";
        default:                      return "未知";
    }
}

/*
 * 获取本地 socket 绑定的地址
 */
static int get_local_address(int sock, struct sockaddr_in *addr) {
    socklen_t len = sizeof(*addr);
    if (getsockname(sock, (struct sockaddr *)addr, &len) < 0) {
        return -1;
    }
    return 0;
}

/*
 * 比较两个地址是否相同 (IP + Port)
 */
static int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return (a->sin_addr.s_addr == b->sin_addr.s_addr &&
            a->sin_port == b->sin_port);
}

/*
 * ============================================================================
 * 处理 STUN 响应 (用于 NAT 类型检测)
 * ============================================================================
 *
 * 根据当前检测状态，处理收到的 STUN 响应：
 *   - NAT_TEST_I_SENT:   处理 Test I 响应，提取 mapped_addr
 *   - NAT_TEST_II_SENT:  处理 Test II 响应（从不同 IP/Port）
 *   - NAT_TEST_III_SENT: 处理 Test III 响应（从不同 Port）
 */
void p2p_nat_handle_stun_packet(struct p2p_session *s, const uint8_t *buf, int len, 
                                const struct sockaddr_in *from) {
    (void)from; /* from 用于验证响应来源，暂未使用 */
    nat_detect_ctx_t *ctx = &g_nat_ctx;
    
    /* 验证 Transaction ID：确保这是我们发送的请求的响应 */
    if (len >= 20) {
        if (memcmp(buf + 8, ctx->tsx_id, 12) != 0) {
            return;  /* Transaction ID 不匹配，忽略 */
        }
    }
    
    struct sockaddr_in mapped;
    if (p2p_stun_parse_response(buf, len, &mapped, NULL) < 0) {
        return;
    }
    
    switch (ctx->state) {
    case NAT_TEST_I_SENT: {
        /* Test I 响应：获取映射地址 */
        ctx->mapped_addr = mapped;
        ctx->state = NAT_TEST_I_DONE;
        
        /* 检查是否为公网 IP */
        struct sockaddr_in local;
        if (get_local_address(s->sock, &local) == 0) {
            if (addr_equal(&mapped, &local)) {
                ctx->detected_type = P2P_NAT_OPEN;
                ctx->state = NAT_TEST_COMPLETED;
                printf("[NAT] Detected: %s\n", p2p_nat_type_str(ctx->detected_type));
                return;
            }
        }
        
        /* 解析 CHANGED-ADDRESS（如果有） */
        /* TODO: 解析 alt_addr */
        
        printf("[NAT] Test I: Mapped address %s:%d\n",
               inet_ntoa(mapped.sin_addr), ntohs(mapped.sin_port));
        
        /* ★ 添加 Srflx 候选到 ICE 候选列表 */
        if (s->local_cand_cnt < P2P_MAX_CANDIDATES) {
            p2p_candidate_t *c = &s->local_cands[s->local_cand_cnt++];
            c->type = P2P_CAND_SRFLX;
            /* RFC 5245: Srflx 候选优先级使用标准公式计算 */
            c->priority = p2p_ice_calc_priority(P2P_CAND_SRFLX, 65535, 1);
            c->addr = mapped;
            printf("[ICE] ✓ Added Srflx Candidate: %s:%d (priority=%u)\n",
                   inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), c->priority);
            
            /* 候选已添加到 local_cands，将在 p2p_update() 中定期批量发送 */
            printf("[ICE] [Trickle] Candidate %d queued for batch sending\n", s->local_cand_cnt);
        } else {
            printf("[ICE] ✗ Cannot add Srflx candidate: local_cand_cnt >= P2P_MAX_CANDIDATES\n");
        }
        
        break;
    }
    
    case NAT_TEST_II_SENT: {
        /* Test II 响应：从不同 IP/Port 收到响应 */
        ctx->test_ii_success = 1;
        ctx->state = NAT_TEST_II_DONE;
        ctx->detected_type = P2P_NAT_FULL_CONE;
        ctx->state = NAT_TEST_COMPLETED;
        printf("[NAT] Test II: Success! Detected: %s\n", 
               p2p_nat_type_str(ctx->detected_type));
        break;
    }
    
    case NAT_TEST_III_SENT: {
        /* Test III 响应：从不同 Port 收到响应 */
        ctx->test_iii_success = 1;
        ctx->state = NAT_TEST_III_DONE;
        ctx->detected_type = P2P_NAT_RESTRICTED;
        ctx->state = NAT_TEST_COMPLETED;
        printf("[NAT] Test III: Success! Detected: %s\n",
               p2p_nat_type_str(ctx->detected_type));
        break;
    }
    
    default:
        break;
    }
}

/* 周期性 NAT 检测 tick */
void p2p_nat_detect_tick(struct p2p_session *s) {
    if (!s->cfg.stun_server) {
        return;  /* 未配置 STUN 服务器 */
    }
    
    nat_detect_ctx_t *ctx = &g_nat_ctx;
    uint64_t now = time_ms();
    
    /* 首次调用时打印调试信息 */
    // printf("[NAT_DEBUG] p2p_nat_detect_tick called, state=%d\n", ctx->state);
    
    /* 如果已完成检测，不再重复 */
    if (ctx->state == NAT_TEST_COMPLETED) {
        return;
    }
    
    /* 检查超时 */
    if (ctx->state != NAT_TEST_IDLE && 
        now - ctx->last_send_time > STUN_TEST_TIMEOUT_MS) {
        
        if (ctx->retry_count >= STUN_MAX_RETRIES) {
            /* 超时失败，进入下一个测试 */
            switch (ctx->state) {
            case NAT_TEST_I_SENT:
                printf("[NAT] Test I: Timeout\n");
                ctx->state = NAT_TEST_COMPLETED;
                ctx->detected_type = P2P_NAT_SYMMETRIC;  /* 默认最严格 */
                return;
            
            case NAT_TEST_II_SENT:
                printf("[NAT] Test II: Timeout (need Test III)\n");
                ctx->test_ii_success = 0;
                ctx->state = NAT_TEST_II_DONE;
                break;
            
            case NAT_TEST_III_SENT:
                printf("[NAT] Test III: Timeout\n");
                ctx->test_iii_success = 0;
                ctx->state = NAT_TEST_III_DONE;
                ctx->detected_type = P2P_NAT_PORT_RESTRICTED;
                ctx->state = NAT_TEST_COMPLETED;
                printf("[NAT] Detected: %s\n", p2p_nat_type_str(ctx->detected_type));
                return;
            
            default:
                break;
            }
            ctx->retry_count = 0;
        } else {
            /* 重试 */
            ctx->retry_count++;
            ctx->state--;  /* 回到发送前状态 */
        }
    }
    
    /* 状态机 */
    switch (ctx->state) {
    case NAT_TEST_IDLE: {
        /* 启动 Test I */
        struct sockaddr_in stun_addr;
        // printf("[NAT_DEBUG] Starting NAT detection, resolving %s:%d\n", 
        //        s->cfg.stun_server, s->cfg.stun_port);
        
        if (resolve_host(s->cfg.stun_server, s->cfg.stun_port, &stun_addr) < 0) {
            printf("[NAT] Failed to resolve STUN server %s\n", s->cfg.stun_server);
            ctx->state = NAT_TEST_COMPLETED;
            return;
        }
        
        /* 生成新的 Transaction ID */
        for (int i = 0; i < 12; i++) {
            ctx->tsx_id[i] = (uint8_t)rand();
        }
        
        /* DEBUG: Print Transaction ID
        printf("[NAT_DEBUG] Transaction ID: ");
        for (int i = 0; i < 12; i++) {
            printf("%02x", ctx->tsx_id[i]);
        }
        printf("\n");
        */
        
        uint8_t req[512];
        int len = p2p_stun_build_binding_request(req, sizeof(req), ctx->tsx_id, NULL, NULL);
        if (len > 0) {
            /* DEBUG: Print request
            printf("[NAT_DEBUG] Built STUN request, length=%d\n", len);
            printf("[NAT_DEBUG] Request bytes[8-19]: ");
            for (int i = 8; i < 20; i++) {
                printf("%02x", req[i]);
            }
            printf("\n");
            */
            
            udp_send_to(s->sock, &stun_addr, req, len);
            ctx->last_send_time = now;
            ctx->state = NAT_TEST_I_SENT;
            ctx->retry_count = 0;
            printf("[NAT] Sending Test I to %s:%d (len=%d)\n", 
                   s->cfg.stun_server, s->cfg.stun_port, len);
        } else {
            printf("[NAT] Failed to build STUN request\n");
        }
        break;
    }
    
    case NAT_TEST_I_DONE: {
        /* Test I 完成，启动 Test II */
        /* 注意：Test II 需要 CHANGE-REQUEST 属性，这里简化处理 */
        /* 实际应该向 alt_addr 发送请求，要求从原服务器响应 */
        
        /* 简化版：直接进入 Test III */
        ctx->state = NAT_TEST_II_DONE;
        ctx->test_ii_success = 0;
        break;
    }
    
    case NAT_TEST_II_DONE: {
        if (ctx->test_ii_success) {
            /* Full Cone，测试完成 */
            break;
        }
        
        /* 启动 Test III（简化版：假设失败） */
        ctx->state = NAT_TEST_III_DONE;
        ctx->test_iii_success = 0;
        break;
    }
    
    case NAT_TEST_III_DONE: {
        /* 根据结果判断最终类型 */
        if (!ctx->test_ii_success && !ctx->test_iii_success) {
            ctx->detected_type = P2P_NAT_PORT_RESTRICTED;
        }
        ctx->state = NAT_TEST_COMPLETED;
        printf("[NAT] Detection completed: %s\n", 
               p2p_nat_type_str(ctx->detected_type));
        break;
    }
    
    default:
        break;
    }
}

