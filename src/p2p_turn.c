
#include "p2p_internal.h"
#include "p2p_udp.h"

/* ----------------------------------------------------------------------------
 * TURN 消息类型定义（RFC 5766）
 * ---------------------------------------------------------------------------- */

/* Allocate 方法 (Method = 0x003) */
#define TURN_ALLOCATE_REQUEST   0x0003  /* Class=Request(00)   -> 0x0003 */
#define TURN_ALLOCATE_SUCCESS   0x0103  /* Class=Success(10)   -> 0x0103 */
#define TURN_ALLOCATE_ERROR     0x0113  /* Class=Error(11)     -> 0x0113 */

/* Send 方法 (Method = 0x006) - 仅 Indication */
#define TURN_SEND_INDICATION    0x0016  /* Class=Indication(01) -> 0x0016 */

/* Data 方法 (Method = 0x007) - 仅 Indication */
#define TURN_DATA_INDICATION    0x0017  /* Class=Indication(01) -> 0x0017 */

/* ----------------------------------------------------------------------------
 * TURN/STUN 属性类型定义
 * ---------------------------------------------------------------------------- */

#define STUN_ATTR_MAPPED_ADDRESS       0x0001  /* 映射地址（旧版）*/
#define STUN_ATTR_USERNAME             0x0006  /* 用户名 */
#define STUN_ATTR_MESSAGE_INTEGRITY    0x0008  /* 消息完整性 HMAC-SHA1 */
#define STUN_ATTR_ERROR_CODE           0x0009  /* 错误码 */
#define STUN_ATTR_UNKNOWN_ATTRIBUTES   0x000A  /* 未知属性列表 */
#define STUN_ATTR_LIFETIME             0x000D  /* 分配生存时间（秒）*/
#define STUN_ATTR_REALM                0x0014  /* 认证域 */
#define STUN_ATTR_NONCE                0x0015  /* 随机数 */
#define STUN_ATTR_XOR_RELAYED_ADDRESS  0x0016  /* XOR 中继地址 */
#define STUN_ATTR_REQUESTED_TRANSPORT  0x0019  /* 请求传输协议 */
#define STUN_ATTR_XOR_MAPPED_ADDRESS   0x0020  /* XOR 映射地址 */
#define STUN_ATTR_FINGERPRINT          0x8028  /* CRC32 指纹 */

/* 传输协议号（IANA） */
#define TRANSPORT_UDP  17
#define TRANSPORT_TCP  6

/* ----------------------------------------------------------------------------
 * 发起 TURN Allocate 请求
 *
 * 向 TURN 服务器请求分配一个中继地址。成功后服务器会返回：
 *   - XOR-RELAYED-ADDRESS: 分配给客户端的中继地址
 *   - LIFETIME: 分配的有效期（秒）
 *
 * 参数:
 *   s - P2P 会话指针
 *
 * 返回值:
 *   成功返回发送的字节数，失败返回 -1
 *
 * 注意:
 *   - 完整实现需要处理 401 Unauthorized 并进行认证
 *   - 需要定期发送 Refresh 请求续期
 * ---------------------------------------------------------------------------- */
int p2p_turn_allocate(p2p_session_t *s) {
    if (!s->cfg.turn_server) return -1;
    
    printf("[TURN] Sending Allocate Request to %s:%d\n", 
           s->cfg.turn_server, s->cfg.turn_port ? s->cfg.turn_port : 3478);
    
    uint8_t buf[256];

    /*
     * 构建 STUN 头部（20 字节）
     * +------------------+------------------+
     * | Type (2 bytes)   | Length (2 bytes) |
     * +------------------+------------------+
     * |         Magic Cookie (4 bytes)      |
     * +-------------------------------------+
     * |                                     |
     * |     Transaction ID (12 bytes)       |
     * |                                     |
     * +-------------------------------------+
     */
    stun_hdr_t h;
    h.type = htons(TURN_ALLOCATE_REQUEST);   /* 0x0003 = Allocate Request */
    h.length = htons(4);                     /* 属性总长度: 4 字节 */
    h.magic = htonl(STUN_MAGIC);             /* 0x2112A442 */
    
    /* 生成随机 Transaction ID（用于匹配响应） */
    for (int i = 0; i < 12; i++) {
        h.tsx_id[i] = (uint8_t)rand();
    }
    
    memcpy(buf, &h, sizeof(h));  /* 复制头部到缓冲区 */

    /*
     * REQUESTED-TRANSPORT 属性（8 字节）
     * +------------------+------------------+
     * | Type=0x0019      | Length=4         |
     * +------------------+------------------+
     * | Protocol | RFFU (Reserved)          |
     * +------------------+------------------+
     *
     * Protocol: 17 = UDP (IANA 协议号)
     * RFFU: 保留字段，必须为 0
     */
    uint16_t attr_type = htons(STUN_ATTR_REQUESTED_TRANSPORT);  /* 0x0019 */
    uint16_t attr_len = htons(4);                               /* 属性值长度 */
    uint32_t transport = htonl(TRANSPORT_UDP << 24);            /* Protocol=17 在高字节 */
    
    memcpy(buf + 20, &attr_type, 2);   /* 偏移 20: 属性类型 */
    memcpy(buf + 22, &attr_len, 2);    /* 偏移 22: 属性长度 */
    memcpy(buf + 24, &transport, 4);   /* 偏移 24: 属性值 */
    
    /* 
     * 最终消息结构:
     * [0-19]  STUN Header (20 bytes)
     * [20-27] REQUESTED-TRANSPORT Attribute (8 bytes)
     * Total: 28 bytes
     */

    /* 解析 TURN 服务器地址 */
    struct sockaddr_in turn_addr;
    memset(&turn_addr, 0, sizeof(turn_addr));
    turn_addr.sin_family = AF_INET;
    turn_addr.sin_port = htons(s->cfg.turn_port ? s->cfg.turn_port : 3478);
    
    struct hostent *he = gethostbyname(s->cfg.turn_server);
    if (!he) {
        printf("[TURN] Failed to resolve TURN server: %s\n", s->cfg.turn_server);
        return -1;
    }
    memcpy(&turn_addr.sin_addr, he->h_addr_list[0], he->h_length);

    return udp_send_to(s->sock, &turn_addr, buf, 28);
}

/* ----------------------------------------------------------------------------
 * 处理来自 TURN 服务器的响应
 *
 * 解析 TURN 服务器的响应，主要处理:
 *   - Allocate Success Response: 提取中继地址加入 ICE 候选
 *   - Data Indication: 提取中继的应用数据
 *
 * 参数:
 *   s    - P2P 会话指针
 *   buf  - 接收到的数据包
 *   len  - 数据包长度
 *   from - 发送方地址（TURN 服务器）
 * ---------------------------------------------------------------------------- */
void p2p_turn_handle_packet(p2p_session_t *s, const uint8_t *buf, int len, 
                            const struct sockaddr_in *from) {
    (void)from;  /* 暂未使用 */
    
    /* STUN 头部最小 20 字节 */
    if (len < 20) return;
    
    /* 解析消息类型（大端序） */
    uint16_t type = (buf[0] << 8) | buf[1];
    
    /* 处理 Allocate Success Response */
    if (type == TURN_ALLOCATE_SUCCESS) {
        printf("[TURN] Allocation successful!\n");
        
        /*
         * 解析 RELAYED-ADDRESS / XOR-RELAYED-ADDRESS 属性
         * 
         * 属性格式:
         * +------------------+------------------+
         * | Reserved (1 byte)| Family (1 byte)  |
         * +------------------+------------------+
         * |           Port (2 bytes)            |
         * +-------------------------------------+
         * |         Address (4 bytes IPv4)      |
         * +-------------------------------------+
         *
         * Family: 0x01 = IPv4, 0x02 = IPv6
         * 
         * 注意: XOR 版本需要对 Port/Address 进行 XOR 解码
         *       Port XOR 0x2112 (Magic Cookie 高 16 位)
         *       Address XOR 0x2112A442 (完整 Magic Cookie)
         */
        struct sockaddr_in relay_addr;
        memset(&relay_addr, 0, sizeof(relay_addr));
        
        /* 获取消息体长度 */
        int msg_len = (buf[2] << 8) | buf[3];
        
        /* 遍历属性列表 */
        int offset = 20;  /* 跳过 20 字节头部 */
        while (offset + 4 <= 20 + msg_len) {
            /* 解析属性头部 */
            uint16_t attr_type = (buf[offset] << 8) | buf[offset + 1];
            uint16_t attr_len = (buf[offset + 2] << 8) | buf[offset + 3];
            offset += 4;  /* 跳过属性头部 */
            
            /* 
             * 查找 XOR-RELAYED-ADDRESS (0x0016) 或 RELAYED-ADDRESS
             * 注意: RFC 5766 使用 0x0016 作为 XOR-RELAYED-ADDRESS
             *       部分旧实现可能使用未 XOR 的版本
             */
            if (attr_type == STUN_ATTR_XOR_RELAYED_ADDRESS) {
                /* 验证属性长度和地址族 */
                if (attr_len >= 8 && buf[offset + 1] == 0x01) {  /* 0x01 = IPv4 */
                    relay_addr.sin_family = AF_INET;
                    
                    /* 
                     * 提取端口和地址（此处假设未 XOR，简化实现）
                     * 完整实现需要:
                     *   port = X-Port XOR 0x2112
                     *   addr = X-Address XOR 0x2112A442
                     */
                    memcpy(&relay_addr.sin_port, buf + offset + 2, 2);
                    memcpy(&relay_addr.sin_addr, buf + offset + 4, 4);
                    
                    /* 将中继地址加入本地 ICE 候选列表 */
                    if (s->local_cand_cnt < P2P_MAX_CANDIDATES) {
                        p2p_candidate_t *c = &s->local_cands[s->local_cand_cnt++];
                        
                        c->type = P2P_CAND_RELAY;   /* 候选类型: 中继 */
                        c->addr = relay_addr;
                        /* RFC 5245: Relay 候选优先级使用标准公式计算 */
                        c->priority = p2p_ice_calc_priority(P2P_CAND_RELAY, 65535, 1);

                        printf("[ICE] Gathered Relay Candidate: %s:%u (priority=%u)\n", 
                               inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), c->priority);
                        
                        /* Trickle ICE（涓流模式）: 候选收集后立即发送给对端 */
                        p2p_ice_send_local_candidate(s, c);
                    }
                    
                    /*
                     * Trickle ICE 模式说明:
                     * 在涓流模式下，候选地址收集到一个就发送一个，
                     * 不需要等待所有候选（Host/Srflx/Relay）全部收集完成。
                     * 因此这里不设置 GATHERING_DONE 状态。
                     * 
                     * 如果是 Full ICE 模式，需要等所有候选收集完成后
                     * 再设置 s->ice_state = P2P_ICE_STATE_GATHERING_DONE;
                     */
                    break;
                }
            }
            
            /* 跳到下一个属性（4 字节对齐） */
            offset += (attr_len + 3) & ~3;
        }
    }
    
    /* TODO: 处理 TURN_DATA_INDICATION - 中继数据接收 */
    /* TODO: 处理 Allocate Error Response - 401 需要认证 */
}
