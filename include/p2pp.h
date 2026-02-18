/*
 * P2P 信令协议定义
 *
 * 统一定义客户端和服务器使用的协议格式，包括：
 * - COMPACT 模式 (UDP): 轻量级 NAT 穿透
 * - RELAY 模式 (TCP): 完整 ICE/SDP 交换
 */

#ifndef P2PP_H
#define P2PP_H

#include <stdint.h>
#include <netinet/in.h>
#include <p2p.h>

/* ============================================================================
 * NAT UDP 包定义
 * ============================================================================
 * 
 * P2P/SIMPLE 通用包头 (4 bytes) 
 *
 * 包类型编号规划:
 *   0x01-0x7F: P2P 基础协议
 *     0x01-0x0F: 打洞和安全协议
 *     0x10-0x1F: 保活协议
 *     0x20-0x2F: 数据传输协议
 *     0x30-0x3F: 路由探测协议
 *   0x80-0xFF: COMPACT 信令协议（本节）
*/
typedef struct {
    uint8_t             type;               // 包类型（0x01-0x7F: P2P协议, 0x80-0xFF: 信令协议）
    uint8_t             flags;              // 标志位（具体含义由 type 决定，见各协议定义）
    uint16_t            seq;                // 序列号（网络字节序，用于可靠传输/去重）
} p2p_packet_hdr_t;

/*
 * flags 字段说明：
 * - 对于 P2P_PKT_DATA: 可能包含分片标志、优先级等
 * - 对于 SIG_PKT_PEER_INFO: 0x01 = SIG_PEER_INFO_FIN（候选列表发送完毕）
 * - 对于其他包类型: 预留，置 0
 * - 具体含义由各协议类型自行定义
 */

static inline void p2p_pkt_hdr_encode(uint8_t *buf, uint8_t type, uint8_t flags, uint16_t seq) {
    buf[0] = type;
    buf[1] = flags;
    buf[2] = (uint8_t)(seq >> 8);
    buf[3] = (uint8_t)(seq & 0xFF);
}

static inline void p2p_pkt_hdr_decode(const uint8_t *buf, p2p_packet_hdr_t *hdr) {
    hdr->type  = buf[0];
    hdr->flags = buf[1];
    hdr->seq   = ((uint16_t)buf[2] << 8) | buf[3];
}

/* ============================================================================
 * NAT 链路 P2P 协议（UDP）
 * ============================================================================
 *
 * 用于对等节点间直接通信的基础协议，所有包共享 4 字节头部。
 * 编号范围：0x01-0x7F
 */

/* 打洞协议 (NAT 穿透) */
#define P2P_PKT_PUNCH           0x01        // NAT 打洞包
#define P2P_PKT_PUNCH_ACK       0x02        // NAT 打洞确认

/* 安全协议 */
#define P2P_PKT_AUTH            0x03        // 安全握手包

/* 保活协议 */
#define P2P_PKT_PING            0x10        // 心跳请求
#define P2P_PKT_PONG            0x11        // 心跳响应

/* 数据传输 (peer-to-peer) */
#define P2P_PKT_DATA            0x20        // 数据包
#define P2P_PKT_ACK             0x21        // 确认包
#define P2P_PKT_FIN             0x22        // 结束包 (无需应答)

/* 路由探测 (同一子网) */
#define P2P_PKT_ROUTE_PROBE     0x30        // 路由探测包
#define P2P_PKT_ROUTE_PROBE_ACK 0x31        // 路由探测确认

/* ============================================================================
 * COMPACT 模式信令服务协议 (UDP)
 * ============================================================================
 *
 * 基于 P2P 协议的扩展，用于客户端与信令服务器通信。
 * 复用 P2P 协议的 4 字节包头: [type: u8 | flags: u8 | seq: u16]
 *
 * 候选列表同步流程:
 * 1. 客户端发送 REGISTER（含 UDP 包可容纳的最大候选列表）
 * 2. 服务器回复 REGISTER_ACK（告知远程缓存的候选数量限制、公网地址、探测端口、是否支持中继）
 * 3. 服务器配置：若支持 NAT 探测，则在 REGISTER_ACK 中设置 probe_port > 0；客户端收到后可发送 NAT_PROBE
 * 4. 双方上线后，服务器向双方发送 PEER_INFO(seq=1)，包含服务器缓存的对端候选
 * 5. 双方通过 PEER_INFO(seq=2,3,...)继续向对方同步剩余候选列表
 * 6. 每个 PEER_INFO 需要 PEER_INFO_ACK 确认，未确认则重发
 * 7. 如果 P2P 打洞失败且服务器支持中继，可通过 RELAY_DATA 转发数据
 *
 * 注：REGISTER 仅在注册阶段发送，收到 REGISTER_ACK 后停止（直到重连）
 */

/* COMPACT 信令协议 (客户端 <-> 信令服务器) - 0x80-0x9F */
#define SIG_PKT_REGISTER        0x80        // 注册到信令服务器（含本地候选列表）
#define SIG_PKT_REGISTER_ACK    0x81        // 注册确认（告知缓存能力、公网地址、探测端口、中继支持）
#define SIG_PKT_PEER_INFO       0x82        // 候选列表同步包（序列化传输）
#define SIG_PKT_PEER_INFO_ACK   0x83        // 候选列表确认（确认指定序列号）
#define SIG_PKT_NAT_PROBE       0x84        // NAT 类型探测请求（发往探测端口）
#define SIG_PKT_NAT_PROBE_ACK   0x85        // NAT 类型探测响应（返回第二次映射地址）

/* COMPACT 服务器中继扩展协议 - 0xA0-0xBF */
#define P2P_PKT_RELAY_DATA      0xA0        // 中继服务器转发的数据（P2P 打洞失败后的降级方案）
#define P2P_PKT_RELAY_ACK       0xA1        // 中继服务器转发的 ACK 确认包

/* REGISTER_ACK status 码 */
#define SIG_REGACK_PEER_OFFLINE  0          // 成功，对端离线
#define SIG_REGACK_PEER_ONLINE   1          // 成功，对端在线

/* REGISTER_ACK 标志位（p2p_packet_hdr_t.flags） */
#define SIG_REGACK_FLAG_RELAY    0x01       // 服务器支持中继功能

/* PEER_INFO 标志位（p2p_packet_hdr_t.flags） */
#define SIG_PEER_INFO_FIN        0x01       // 候选列表发送完毕

/*
 * COMPACT 模式精简候选结构 (7 bytes)
 * 用于 UDP 信令传输，省略了 priority 和 base_addr。
 * 
 * 布局: [type: 1B][ip: 4B][port: 2B]
 */
typedef struct {
    uint8_t             type;               // 候选类型 (0=Host, 1=Srflx, 2=Relay, 3=Prflx)
    uint32_t            ip;                 // IP 地址（网络字节序）
    uint16_t            port;               // 端口（网络字节序）
} __attribute__((packed)) p2p_compact_candidate_t;

/*
 * COMPACT 模式消息格式（以下均为 payload 部分，前面需加 4 字节包头）:
 *
 * REGISTER:
 *   payload: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *   包头: type=0x80, flags=0, seq=0
 *
 * REGISTER_ACK:
 *   payload: [status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
 *   包头: type=0x81, flags=见下, seq=0
 *   - status: 0=对端离线, 1=对端在线, >=2=错误码
 *   - max_candidates: 服务器为该对端缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器主端口观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: 包头的 flags 字段可设置 SIG_REGACK_FLAG_RELAY (0x01) 表示服务器支持中继
 *   总大小: 4(包头) + 10(payload) = 14 字节
 *
 * PEER_INFO:
 *   payload: [base_index(1)][candidate_count(1)][candidates(N*7)]
 *   包头: type=0x82, flags=见下, seq=序列号
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（配合 FIN 标志）
 *   - seq=1: 服务器发送，base_index=0，包含缓存的对端候选
 *   - seq>1: 客户端发送，base_index 递增，继续同步剩余候选
 *   - flags: 包头的 flags 字段可设置 SIG_PEER_INFO_FIN (0x01) 表示候选列表发送完毕
 *
 * NAT_PROBE:
 *   payload: [request_id(2)][reserved(2)]
 *   包头: type=0x84, flags=0, seq=0
 *   - request_id: 请求标识符，用于匹配响应
 *
 * NAT_PROBE_ACK:
 *   payload: [request_id(2)][probe_ip(4)][probe_port(2)]
 *   包头: type=0x85, flags=0, seq=0
 *   - request_id: 对应的请求标识符
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *
 * PEER_INFO_ACK:
 *   payload: [ack_seq(2)][reserved(2)]
 *   包头: type=0x83, flags=0, seq=0
 *   - ack_seq: 确认的 PEER_INFO 序列号
 *
 * RELAY_DATA:
 *   payload: [target_peer_id(32)][data_len(2)][data(N)]
 *   包头: type=0xA0, flags=0, seq=数据序列号
 *   - target_peer_id: 目标对端 ID
 *   - data_len: 数据长度
 *   - data: 实际数据内容
 *   用于在 P2P 打洞失败后，通过服务器中继转发数据
 */

/* ============================================================================
 * RELAY 模式协议 (TCP)
 * ============================================================================
 *
 * 包头: [magic: 4B][type: 1B][length: 4B]
 * magic = 0x50325030 ("P2P0")
 */

#define P2P_RLY_MAGIC 0x50325030            // "P2P0"

/* RELAY 模式消息类型 */
typedef enum {
    P2P_RLY_LOGIN = 1,                      // 登录请求: Client -> Server
    P2P_RLY_LOGIN_ACK,                      // 登录确认: Server -> Client
    P2P_RLY_LIST,                           // 查询在线用户: Client -> Server
    P2P_RLY_LIST_RES,                       // 在线用户列表: Server -> Client
    P2P_RLY_CONNECT,                        // 发起连接: Client -> Server (携带候选)
    P2P_RLY_OFFER,                          // 转发连接请求: Server -> Target（含缓存候选）
    P2P_RLY_ANSWER,                         // 返回应答: Target -> Server
    P2P_RLY_FORWARD,                        // 转发应答: Server -> Client
    P2P_RLY_HEARTBEAT,                      // 心跳: Client -> Server
    P2P_RLY_CONNECT_ACK                     // 连接确认: Server -> Client
} p2p_relay_type_t;

/* RELAY 模式包头 (9 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint32_t            magic;
    uint8_t             type;
    uint32_t            length;
} p2p_relay_hdr_t;

/* RELAY 模式登录消息 */
typedef struct {
    char                name[P2P_PEER_ID_MAX];
} p2p_relay_login_t;

/*
 * RELAY 模式连接确认 (P2P_RLY_CONNECT_ACK)
 *
 * status:
 *   0 = 成功转发给目标（对端在线）
 *   1 = 已缓存且有剩余空间（对端离线，可以继续发送候选）
 *   2 = 缓存已满（对端离线，停止发送，等待对端上线）
 *
 * candidates_acked: 服务器已确认的候选数量（本次 CONNECT 中的候选）
 *                   - status=0（在线）: 全部转发，等于发送数量
 *                   - status=1（离线）: 实际缓存数量，缓存后仍有剩余空间
 *                   - status=2（已满）: 实际缓存数量（可能为 0 或 >0，缓存满）
 *
 * 客户端逻辑：
 *   - status=0: 继续 Trickle ICE（对端在线，实时转发）
 *   - status=1: 继续 Trickle ICE（对端离线，但服务器还能缓存）
 *   - status=2: 停止发送，进入等待状态（等待收到 FORWARD）
 */
typedef struct {
    uint8_t             status;
    uint8_t             candidates_acked;   // 本次确认的候选数量
    uint8_t             reserved[2];
} p2p_relay_connect_ack_t;

/*
 * ICE 候选地址结构（32 字节）
 *
 * 用于 RELAY 模式信令传输，包含完整的候选信息。
 * type 字段：0=Host, 1=Srflx, 2=Relay, 3=Prflx（见 RFC 5245）
 */
typedef struct {
    int                 type;               // 候选类型（0-3）
    struct sockaddr_in  addr;               // 传输地址（16B）
    struct sockaddr_in  base_addr;          // 基础地址（16B）
    uint32_t            priority;           // 候选优先级
} p2p_candidate_t;

/*
 * RELAY 模式信令负载头部
 *
 * 用于封装 ICE 候选交换的元数据。
 * 序列化格式（76字节）：[sender:32B][target:32B][timestamp:4B][delay_trigger:4B][count:4B]
 */
typedef struct {
    char                sender[32];         // 发送方 peer_id
    char                target[32];         // 目标方 peer_id
    uint32_t            timestamp;          // 时间戳（用于排序和去重）
    uint32_t            delay_trigger;      // 延迟触发打洞（毫秒）
    int                 candidate_count;    // ICE 候选数量
} p2p_signaling_payload_hdr_t;

#pragma pack(pop)

#endif /* P2PP_H */
