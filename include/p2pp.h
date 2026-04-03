/*
 * P2P & SIGNALING 协议定义
 *
 * 统一定义客户端和服务器使用的协议格式，包括：
 * - NAT UDP 链路协议: 连接探测、状态维护、数据传输等基础协议
 * - COMPACT 模式 (UDP): 轻量级 NAT 穿透
 * - RELAY 模式 (TCP): 完整 ICE/SDP 交换
 */
#ifndef P2PP_H
#define P2PP_H
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include <stdint.h>
#include <string.h>

/* #pragma pack(push/pop) 受 MSVC / GCC / Clang 三大编译器支持，无需平台宏 */
#pragma pack(push, 1)

/* Peer ID 最大长度 */
#define P2P_PEER_ID_MAX     32

/* Session ID 字节长度 */
#define P2P_SESS_ID_PSZ     (sizeof(uint64_t))

/* ============================================================================
 * 可交换的候选地址定义
 * ============================================================================
 * 
 * 采用平台无关的地址序列化格式 IPv4-mapped IPv6 统一编码（18 字节）：
 *   IPv4 地址 a.b.c.d 存储为 ::ffff:a.b.c.d（前 10 字节 0，字节 10-11 为 0xFF）
 *   IPv6 地址直接存储 16 字节
 *
 * 判断方式：ip[0..11] == {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF} 则为 IPv4，
 *          实际 IPv4 地址在 ip[12..15]。
 */

/*
 * 转换函数见 p2p_common.h：sockaddr_to_p2p_wire() / sockaddr_from_p2p_wire()
 */
typedef struct {
    uint16_t            port;                       // 端口（网络字节序）
    uint8_t             ip[16];                     // IPv4-mapped IPv6 地址
} p2p_sockaddr_t;

/* IPv4-mapped 前缀: ::ffff:0:0 */
static const uint8_t P2P_IPV4_MAPPED_PREFIX[12] = {
    0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF
};

static inline int p2p_sockaddr_is_ipv4(const p2p_sockaddr_t *w) {
    return memcmp(w->ip, P2P_IPV4_MAPPED_PREFIX, 12) == 0;
}

/* 读取 IPv4 地址（网络字节序），调用前应先用 p2p_sockaddr_is_ipv4 判断 */
static inline uint32_t p2p_sockaddr_ipv4(const p2p_sockaddr_t *w) {
    uint32_t v; memcpy(&v, &w->ip[12], 4); return v;
}

/*
 * ICE 候选地址序列化格式（23 字节，在 pack(1) 块内 sizeof == 23 在所有平台保证）
 *
 * 用于信令协议网络传输，各字段均以大端字节序存储。
 * 内部会话代码使用 p2p_local_candidate_entry_t（含 struct sockaddr_in），定义见 p2p_ice.h。
 * 转换函数：pack_candidate() / unpack_candidate()，见 p2p_internal.h
 *
 * base_addr 仅 ICE 本地诊断使用（RFC 8445 raddr/rport），不参与线协议，
 * 保留在 p2p_local_candidate_entry_t 中。
 *
 * 内存布局：
 *   ┌──────────┬────────────────────┬──────────┐
 *   │ type(1B) │    addr (18B)      │ prio(4B) │
 *   └──────────┴────────────────────┴──────────┘
 */
typedef struct {
    uint8_t             type;               // 候选类型 (0=Host 1=Srflx 2=Relay 3=Prflx)
    p2p_sockaddr_t      addr;               // 候选地址（18B）
    uint32_t            priority;           // 候选优先级 htonl（RFC 8445: 32-bit）
} p2p_candidate_t;

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
 *     0x30-0x3F: 会话控制和路由探测协议
 *   0x80-0xFF: COMPACT 信令协议（本节）
*/

/* 安全的 P2P UDP 负载 */
#define P2P_MTU         1200              
#define P2P_HDR_SIZE    4                           /* 包头大小 */
#define P2P_MAX_PAYLOAD (P2P_MTU - P2P_HDR_SIZE)    /* 1196 */
#define P2P_MSG_DATA_MAX  (P2P_MAX_PAYLOAD - 11)    /* MSG RPC data upper bound: relay path needs [session_id(P2P_SESS_ID_PSZ)+sid(2)+msg(1)] */

typedef struct {
    uint8_t             type;               // 包类型（0x01-0x7F: P2P协议, 0x80-0xFF: 信令协议）
    uint8_t             flags;              // 标志位（具体含义由 type 决定，见各协议定义）
    uint16_t            seq;                // 序列号（网络字节序，用于可靠传输/去重）
} p2p_packet_hdr_t;

/*
 * flags 字段说明：
 * - 对于 P2P_PKT_DATA: 可能包含分片标志、优先级等
 * - 对于 SIG_PKT_SYNC: 0x01 = SIG_SYNC_FLAG_FIN（候选列表发送完毕）
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

/*
 * ============================================================================
 * P2P_PKT_PUNCH / PUNCH_ACK 协议（即时应答设计）
 * ============================================================================
 *
 * PUNCH (0x01) — 连接探测/打洞/保活包
 *   包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 *   负载: 无
 *   发送: 每 500ms 定时向所有候选路径并发发送（打洞阶段），或向活动路径发送（保活阶段）
 *   接收: 立即回复 PUNCH_ACK，更新路径可达性，设置 rx_confirmed
 *
 * PUNCH_ACK (0x02) — PUNCH 的即时确认包
 *   包头: [type=0x02 | flags=0 | seq=回传对方的 PUNCH seq(2B)]
 *   负载: 无
 *   发送: 收到 PUNCH 后立即回复，从同一源地址发出（确保回程走相同路径）
 *   接收: 通过 seq 匹配发送记录计算 per-path RTT，设置 rx_confirmed + tx_confirmed
 *
 * 协议机制
 * ============================================================================
 *
 * 定时发送策略：
 *   双方各自按固定间隔（PUNCH_INTERVAL_MS = 500ms）定时发送 PUNCH 到所有候选
 *   或已建连后的活动路径，不依赖对方的包才发送，完全独立触发。
 *
 * 即时回复机制：
 *   收到 PUNCH 后立即回复 PUNCH_ACK，PUNCH_ACK.seq = PUNCH.seq。
 *   由于 PUNCH_ACK 从收到包的同一地址回复，保证测量的是同一条路径的 RTT。
 *
 * 双向连通判定：
 *   rx_confirmed: 收到 PUNCH 或 PUNCH_ACK → 证明 peer→me 方向通
 *   tx_confirmed: 收到 PUNCH_ACK → 证明 me→peer 方向通（对方收到了我的 PUNCH）
 *   rx_confirmed && tx_confirmed → 状态转换为 NAT_CONNECTED
 *
 * per-path RTT 测量：
 *   发送: 每次发送 PUNCH 时记录 {seq, path_idx, send_time} 到 pending_packets
 *   接收: 收到 PUNCH_ACK(seq=N) 时，从 pending_packets 匹配 seq，计算
 *        RTT = now - send_time，更新对应 path 的 RTT/loss/jitter 统计
 *   精度: 由于 PUNCH_ACK 从同一地址回复，RTT 精确对应该路径（非混合路径）
 *
 * 交互示例
 * ============================================================================
 *
 *   时间轴（ms）      Alice                                 Bob
 *   ────────────────────────────────────────────────────────────────────
 *   t=0              PUNCH(seq=1) ─────────────────────────→
 *                    [记录 pending: seq=1, path=0, t=0]
 *
 *   t=10                                                    收到 PUNCH(seq=1)
 *                                                           [rx_confirmed=true]
 *                                  ←───────────────────── PUNCH_ACK(seq=1)
 *
 *   t=20             收到 PUNCH_ACK(seq=1)
 *                    [匹配 pending: RTT = 20ms, path=0]
 *                    [tx_confirmed=true]
 *                    [NAT_CONNECTED]
 *
 *   t=500            PUNCH(seq=2) ─────────────────────────→ (下一轮定时)
 *                    [记录 pending: seq=2, path=0, t=500]
 *
 *   t=510                                                   收到 PUNCH(seq=2)
 *                                  ←───────────────────── PUNCH_ACK(seq=2)
 *
 *   t=520            收到 PUNCH_ACK(seq=2)
 *                    [匹配 pending: RTT = 20ms, path=0]
 *
 * 注：Bob 也会独立定时发送 PUNCH（未在上图中显示），Alice 同样会立即回复 PUNCH_ACK。
 *
 * 收发分离协议
 * ============================================================================
 *
 * PUNCH/PUNCH_ACK 携带 target_addr 实现收发路径分离：
 *
 * PUNCH 格式:
 *   包头: [type=0x01 | flags=0 | seq(2B)]
 *   负载: [target_addr(4B) | target_port(2B)]  // 我发往哪个地址
 *
 * PUNCH_ACK 格式:
 *   包头: [type=0x02 | flags=0 | seq=echo_seq(2B)]
 *   负载: [target_addr(4B) | target_port(2B)]  // echo 回 PUNCH 中的目标地址
 *
 * 收发分离语义：
 *   - PUNCH 携带 target: 告知对方"我是发往哪个地址的"
 *   - REACH echo target: 告知发送方"你的包到达了这个地址"
 *   - 发送方收到 REACH 后，标记该 target 对应的路径为 writable
 *   - 解决单向防火墙：即使收到包的端口不能回复，也能通过其他可写路径回复 REACH
 */
#define P2P_PKT_PUNCH           0x01        // 连接探测包（打洞/保活），负载: target_addr(6B)
#define P2P_PKT_REACH           0x02        // PUNCH 到达确认包，负载: echo target_addr(6B)

#define P2P_PKT_PUNCH_PSZ           6u      // target_addr(4) + target_port(2)
#define P2P_PKT_REACH_PSZ           6u      // echo target_addr(4) + target_port(2)

/*
 * ============================================================================
 * P2P_PKT_CONN / CONN_ACK 协议（连接建立三次握手的最后一次）
 * ============================================================================
 *
 * CONN (0x03) — 连接就绪包
 *   包头: [type=0x03 | flags=0 | seq=发送方序列号(2B)]
 *   负载: 无
 *   发送: 收到第一个 REACH 后定期发送，直到收到 CONN_ACK 或数据包
 *   接收: 立即回复 CONN_ACK，状态机转换为 CONNECTED，允许数据传输
 *
 * CONN_ACK (0x04) — 连接就绪确认包
 *   包头: [type=0x04 | flags=0 | seq=回传对方的 CONN seq(2B)]
 *   负载: 无
 *   发送: 收到 CONN 后立即回复
 *   接收: 停止发送 CONN，状态机转换为 CONNECTED
 *
 * 协议机制
 * ============================================================================
 *
 * 三次握手确保双方同步进入 CONNECTED 状态：
 *   1. PUNCH → REACH: NAT 打洞确认（双向连通）
 *   2. CONN → CONN_ACK: 数据传输就绪确认（避免丢失首包）
 *
 * 状态转换：
 *   - 发送方: 收到 REACH → 定期发送 CONN → 收到 CONN_ACK → CONNECTED
 *   - 接收方: 收到 CONN → 回复 CONN_ACK → CONNECTED
 *
 * 超时处理：
 *   - 收到任何数据包（DATA/CRYPTO）也可停止 CONN 重传
 *   - 防止握手包丢失导致单方等待
 *
 * 交互示例
 * ============================================================================
 *
 *   时间轴（ms）      Alice                                 Bob
 *   ────────────────────────────────────────────────────────────────────
 *   t=0              PUNCH(seq=1) ─────────────────────────→
 *
 *   t=10                                                    收到 PUNCH(seq=1)
 *                                  ←─────────────────────   REACH(seq=1)
 *
 *   t=20             收到 REACH(seq=1)
 *                    [双向连通确认]
 *                    CONN(seq=100) ────────────────────────→
 *
 *   t=30                                                    收到 CONN(seq=100)
 *                                                           [状态 → CONNECTED]
 *                                  ←─────────────────────   CONN_ACK(seq=100)
 *
 *   t=40             收到 CONN_ACK(seq=100)
 *                    [停止 CONN 重传]
 *                    [状态 → CONNECTED]
 *                    DATA(seq=1) ──────────────────────────→ (开始数据传输)
 */
#define P2P_PKT_CONN            0x03        // 连接就绪包（三次握手最后一次）
#define P2P_PKT_CONN_ACK        0x04        // 连接就绪确认包

#define P2P_PKT_CONN_PSZ            0u      // 无 payload
#define P2P_PKT_CONN_ACK_PSZ        0u      // 无 payload

/*
 * ============================================================================
 * P2P_PKT_FIN 协议（单向连接终止通知）
 * ============================================================================
 *
 * FIN (0x05) — 连接终止包
 *   包头: [type=0x05 | flags=0 | seq=0]
 *   负载: 无
 *   发送: 本端主动关闭连接时向对端发送，无需等待应答
 *   接收: 立即停止向该路径发送任何包，关闭本端连接状态
 *
 * 协议语义
 * ============================================================================
 *
 * FIN 是单向通知，不要求对端回复，也不重传：
 *   - 发送方发出 FIN 后即认为连接已关闭，不再等待任何响应
 *   - 接收方收到 FIN 后应立即终止该路径的所有传输
 *   - 若 FIN 在网络中丢失，对端的 PUNCH 超时机制最终也会触发连接关闭
 */
#define P2P_PKT_FIN             0x05        // 连接终止包（单向通知，无需应答）

#define P2P_PKT_FIN_PSZ             0u      // 无 payload

/*
 * ============================================================================
 * 数据传输 (peer-to-peer)
 * ============================================================================
 *
 * DATA:   [hdr(4)][data(N)]                // 数据包，负载为应用数据
 * ACK:    [hdr(4)][ack_seq(2)][sack(4)]    // 累积确认 + 选择性确认位图
 * CRYPTO: [hdr(4)][crypto_data(N)]         // DTLS 握手或加密数据
 *
 * 当 flags & P2P_RELAY_FLAG_SESSION 时，所有包在 hdr(4) 之后前置 session_id(P2P_SESS_ID_PSZ)，
 * 详见下方 P2P_RELAY_FLAG_SESSION 说明。
 */
#define P2P_PKT_DATA            0x20        // 数据包
#define P2P_PKT_ACK             0x21        // 确认包
#define P2P_PKT_CRYPTO          0x22        // DTLS 加密包（握手/密文数据）

#define P2P_PKT_ACK_PSZ             6u                          // ack_seq(2) + sack(4)（无 session_id）
#define P2P_PKT_ACK_SESSION_PSZ     (P2P_SESS_ID_PSZ + 6u)      // session_id(P2P_SESS_ID_PSZ) + ack_seq(2) + sack(4)

/* 
 * P2P_RELAY_FLAG_SESSION 说明：
 *
 *   适用包类型: DATA / ACK / CRYPTO / REACH / CONN / CONN_ACK
 *
 *   flags & P2P_RELAY_FLAG_SESSION == 0（直连 P2P，上层协议自带会话隔离如 SCTP/DTLS）:
 *     DATA:     [hdr(4)][data(N)]
 *     ACK:      [hdr(4)][ack_seq(2)][sack(4)]
 *     CRYPTO:   [hdr(4)][crypto_data(N)]
 *     REACH:    [hdr(4)][target_addr(6)]
 *     CONN:     [hdr(4)]
 *     CONN_ACK: [hdr(4)]
 *
 *   flags & P2P_RELAY_FLAG_SESSION == 1（裸可靠层或中继路径，需显式会话标识）:
 *     DATA:     [hdr(4)][session_id(P2P_SESS_ID_PSZ)][data(N)]
 *     ACK:      [hdr(4)][session_id(P2P_SESS_ID_PSZ)][ack_seq(2)][sack(4)]
 *     CRYPTO:   [hdr(4)][session_id(P2P_SESS_ID_PSZ)][crypto_data(N)]
 *     REACH:    [hdr(4)][session_id(P2P_SESS_ID_PSZ)][target_addr(6)]
 *     CONN:     [hdr(4)][session_id(P2P_SESS_ID_PSZ)]
 *     CONN_ACK: [hdr(4)][session_id(P2P_SESS_ID_PSZ)]
 *
 *   session_id 用于:
 *     1. 会话隔离: 过滤旧会话重传的包（解决重连污染问题）
 *     2. 服务器路由: 中继路径时服务器通过 session_id 查找目标对端
 */
#define P2P_RELAY_FLAG_SESSION      0x01    // 携带 session_id（8字节，紧跟包头），用于会话隔离/中继路由

/* NAT 链路 payload 大小常量（不含 4 字节包头） */

/* ============================================================================
 * COMPACT 模式信令服务协议 (UDP)
 * ============================================================================
 *
 * 基于 P2P 协议的扩展，用于客户端与信令服务器通信。
 * 复用 P2P 协议的 4 字节包头: [type: u8 | flags: u8 | seq: u16]
 * 详细包格式及处理流程见本节末尾"COMPACT 模式协议详细说明"部分。
 */

/* COMPACT 信令协议 (客户端 <-> 信令服务器) - 0x80-0x9F */
#define SIG_PKT_ONLINE          0x80        // 上线（登录）到信令服务器
#define SIG_PKT_ONLINE_ACK      0x81        // 上线确认（告知 auth_key、本端缓存能力、公网地址、探测端口、中继支持）
#define SIG_PKT_OFFLINE         0x82        // 主动注销：客户端关闭时通知服务器立即释放配对槽位
                                            // 【服务端可选实现】服务端不处理此包时，自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制
#define SIG_PKT_ALIVE           0x83        // 保活包（可选，客户端定期发送以维持注册状态）
#define SIG_PKT_ALIVE_ACK       0x84        // 保活确认（服务器回复以确认注册状态）

#define SIG_PKT_SYNC0           0x85        // 指定对端 + 首批候选提交（client→server）：[auth_key(8)][remote_peer_id(32)][candidate_count(1)][candidates(N*23)]
#define SIG_PKT_SYNC0_ACK       0x86        // 首批候选确认（server→client）：[session_id(P2P_SESS_ID_PSZ)][online(1)]，session_id = 对端配对会话 ID
#define SIG_PKT_SYNC            0x87        // 候选列表同步包（序列化传输）
#define SIG_PKT_SYNC_ACK        0x88        // 候选列表确认（确认指定序列号）
#define SIG_PKT_FIN             0x89        // 对端已离线/断开

/* SYNC 标志位（p2p_packet_hdr_t.flags） */
#define SIG_SYNC_FLAG_FIN           0x01    // 候选列表发送完毕

/* MSG RPC 包类型（服务器可选实现，详见协议详细说明节） */
#define SIG_PKT_MSG_REQ         0x90        // MSG 请求：A→Server；Server→B relay（flags=SIG_MSG_FLAG_RELAY）
#define SIG_PKT_MSG_REQ_ACK     0x91        // MSG 请求确认：Server→A（已缓存并开始中转，或失败状态）
#define SIG_PKT_MSG_RESP        0x92        // MSG 应答：B→Server；Server→A relay
#define SIG_PKT_MSG_RESP_ACK    0x93        // MSG 应答确认：Server→B；A→Server

/* NAT 探测（服务器可选实现） */
#define SIG_PKT_NAT_PROBE       0xA0        // NAT 类型探测请求（发往探测端口）
#define SIG_PKT_NAT_PROBE_ACK   0xA1        // NAT 类型探测响应（返回第二次映射地址）

/* ONLINE_ACK 标志位（p2p_packet_hdr_t.flags） */
#define SIG_ONACK_FLAG_RELAY        0x01    // 服务器支持数据中继功能（P2P 打洞失败降级）
#define SIG_ONACK_FLAG_MSG          0x02    // 服务器支持 MSG RPC 机制（可可靠中转请求-应答）

/* MSG 包标志位（p2p_packet_hdr_t.flags） */
#define SIG_MSG_FLAG_RELAY          0x01    // 标识此 MSG_REQ 是 Server→B 的中转包（而非 A→Server 的原始请求）

/* MSG_RESP 包标志位 - 用于标识服务器特殊错误（而非对端返回的正常响应） */
#define SIG_MSG_FLAG_PEER_OFFLINE   0x02    // B端在 REQ_ACK 之后离线（等待响应期间离线）
#define SIG_MSG_FLAG_TIMEOUT        0x04    // 服务器向B端转发请求超时

#define SIG_AUTH_KEY_PSZ            (sizeof(uint64_t))          // auth_key 大小（8 字节）

/* ============================================================================
 * COMPACT 模式协议详细说明
 * ============================================================================
 *
 * ONLINE:
 *   payload: [local_peer_id(32)][instance_id(4)]
 *   包头: type=0x80, flags=0, seq=0
 *   - ONLINE 仅建立客户端与服务器的关系，不携带 remote_peer_id 和候选地址
 *   - instance_id: 本次 connect() 的实例 ID（网络字节序，32位，必须非 0）
 *   - 语义:
 *       * instance_id 相同: 视为 ONLINE 重传（例如客户端未收到 ONLINE_ACK）
 *       * instance_id 不同: 视为同一 local_peer_id 的新实例（客户端重启/重连），服务端重置旧状态
 *   总大小: 4(包头) + 36(payload) = 40 字节
 */
 #define SIG_PKT_ONLINE_PSZ          (P2P_PEER_ID_MAX + sizeof(uint32_t))                                // peer_id(32) + instance_id(4)
/* ONLINE_ACK:
 *   payload: [instance_id(4)][auth_key(8)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
 *   包头: type=0x81, flags=见下, seq=0
 *   - auth_key: 客户端-服务器认证令牌（network byte order, 64-bit），用于后续 SYNC0 和 ALIVE 包的身份验证
 *     · auth_key=0 表示服务器拒绝登录（无可用槽位），客户端应停止重试
 *     · 与 session_id（对端配对会话 ID）语义不同：auth_key 标识 client↔server 关系，session_id 标识 client↔peer 关系
 *   - instance_id: 回显客户端 ONLINE 中的 instance_id（网络字节序，32位）
 *     客户端收到后应比较 instance_id 与当前实例是否一致，不一致则丢弃此 ACK
 *   - max_candidates: 服务器为该对端缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器主端口观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: 包头的 flags 字段可设置：
 *       SIG_ONACK_FLAG_RELAY (0x01) 表示服务器支持中继
 *       SIG_ONACK_FLAG_MSG (0x02) 表示服务器支持 MSG RPC 机制
 *   总大小: 4(包头) + 21(payload) = 25 字节
 */
 #define SIG_PKT_ONLINE_ACK_PSZ      (sizeof(uint32_t) + SIG_AUTH_KEY_PSZ + 1u + 4u + 2u + 2u)          // instance_id(4) + auth_key(8) + max_cands(1) + ip(4) + port(2) + probe(2)
/* OFFLINE:
 *   payload: [auth_key(8)]
 *   包头: type=0x82, flags=0, seq=0
 *   - auth_key: 来自 ONLINE_ACK 的客户端-服务器认证令牌（network byte order），服务器用于 O(1) 查找并释放配对槽位
 *   客户端主动断开时发送，请求服务器立即释放配对槽位
 *   服务器收到后会向对端发送 FIN 通知
 */
 #define SIG_PKT_OFFLINE_PSZ         SIG_AUTH_KEY_PSZ                                                    // auth_key(8)
/* ALIVE:
 *   payload: [auth_key(8)]
 *   包头: type=0x83, flags=0, seq=0
 *   - auth_key: 客户端-服务器认证令牌（来自 ONLINE_ACK），用于服务器识别并更新槽位活跃时间
 *   用于客户端在 ONLINE/READY 状态定期发送，保持服务器槽位活跃
 */
 #define SIG_PKT_ALIVE_PSZ           (P2P_SESS_ID_PSZ)                                                  // auth_key(8)
/* ALIVE_ACK:
 *   payload: 空（仅包头）
 *   包头: type=0x84, flags=0, seq=0
 *   服务器回复确认，表示槽位仍然有效
 */
 #define SIG_PKT_ALIVE_ACK_PSZ       0u                                                                 // 无 payload
/* SYNC0:
 *   payload: [auth_key(8)][remote_peer_id(32)][candidate_count(1)][candidates(N*23)]
 *   包头: type=0x85, flags=0, seq=0
 *   - auth_key: 来自 ONLINE_ACK 的客户端-服务器认证令牌（network byte order），服务器用于识别客户端身份
 *   - remote_peer_id: 目标对端 ID（32字节，不足补零）
 *   - candidate_count: 首批候选数量（最多 max_candidates 个）
 *   - candidates: 首批候选地址列表（每 23 字节，p2p_candidate_t 格式）
 *   客户端在收到 ONLINE_ACK 后立即发送，同时完成：
 *     1. 提交首批候选供服务器缓存
 *     2. 指定 remote_peer_id，建立与对端的配对关系
 */
#define SIG_PKT_SYNC0_PSZ(n)        (sizeof(uint64_t) + P2P_PEER_ID_MAX + 1u + (n)*sizeof(p2p_candidate_t))  // auth_key(8) + peer_id(32) + count(1) + cands(n*23)
/* SYNC0_ACK:
 *   payload: [session_id(P2P_SESS_ID_PSZ)][online(1)]
 *   包头: type=0x86, flags=0, seq=0
 *   - session_id: 对端配对会话 ID（network byte order, 64-bit），标识 client↔peer 会话
 *     · 语义不同于 auth_key（auth_key 标识 client↔server）
 *     · 用于后续所有 SYNC/SYNC_ACK/FIN/DATA relay/MSG 包的身份验证
 *   - online: 1=对端已上线（已有对端配对），0=对端尚未上线
 *   服务器收到 SYNC0 后回复，通知客户端候选已缓存以及对端是否已上线
 */
#define SIG_PKT_SYNC0_ACK_PSZ       (P2P_SESS_ID_PSZ + 1u)                                              // session_id(P2P_SESS_ID_PSZ) + online(1)
/* SYNC:
 *   payload: [session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)][candidates(N*23)]
 *   包头: type=0x87, flags=见下, seq=序列号
 *   - session_id: 会话 ID（网络字节序，64位，来自 SYNC0_ACK）
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（配合 FIN 标志）
 *   - seq=0: 服务器发送，base_index=0，包含缓存的对端候选
 *   - seq=0 且 base_index!=0: 地址变更通知（candidate_count=1）
 *       * base_index 作为 8 位循环通知序号（1..255 循环）
 *       * 接收端按循环序比较新旧，旧通知可忽略但仍需 ACK
 *   - seq>0: 客户端发送，base_index 递增，继续同步剩余候选，使用 ONLINE_ACK 中的 session_id
 *   - flags: 包头的 flags 字段可设置 SIG_SYNC_FLAG_FIN (0x01) 表示候选列表发送完毕
 *   - seq 窗口: 0..16（0 为服务器首包，1..16 为后续候选批次）
 *   - 乱序处理: 允许 seq>0 先于 seq=0 到达；接收端按序号位图去重，重复包仅 ACK 不重复入表
 */
#define SIG_PKT_SYNC_PSZ(n)         (P2P_SESS_ID_PSZ + 2u + (n)*sizeof(p2p_candidate_t))                // session_id(P2P_SESS_ID_PSZ) + base(1) + count(1) + cands(n*23)
/* SYNC_ACK:
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   包头: type=0x88, flags=0, seq=确认的 SYNC 序列号
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - seq: 确认的 SYNC 序列号（0 表示确认服务器下发的 SYNC(seq=0)）
 *   - seq 窗口: 0..16（客户端接收端仅接受 1..16）
 */
#define SIG_PKT_SYNC_ACK_PSZ        (P2P_SESS_ID_PSZ)                                                   // session_id(P2P_SESS_ID_PSZ)
/* FIN:
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   包头: type=0x89, flags=0, seq=0
 *   服务器下行通知：对端已离线/断开连接
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 *   客户端收到此包后应停止该会话的所有传输和重传
 */
#define SIG_PKT_FIN_PSZ             (P2P_SESS_ID_PSZ)                                                   // session_id(P2P_SESS_ID_PSZ)

/* MSG_REQ (A → Server):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x90, flags=0, seq=0
 *   - session_id: A 的会话 ID（来自 SYNC0_ACK）
 *   - sid: A 生成的 16 位序列号（每次 connect() 范围内唯一，用于匹配应答）
 *   - msg: 应用层消息 ID（协议层透传，由应用自定义）
 *   - A 重发此包直到收到 MSG_REQ_ACK
 *
 * MSG_REQ (Server → B, relay):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x90, flags=SIG_MSG_FLAG_RELAY(0x01), seq=0
 *   - session_id: A 的会话 ID（B 用此字段构造 MSG_RESP）
 *   - Server 重发此包直到收到 MSG_RESP
 */
#define SIG_PKT_MSG_REQ_MIN_PSZ     (P2P_SESS_ID_PSZ + 3u)                                              // session_id(P2P_SESS_ID_PSZ) + sid(2) + msg(1)
/* MSG_REQ_ACK (Server → A):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][status(1)]
 *   包头: type=0x91, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 A 端验证响应合法性）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - status: 0=已缓存并开始向 B 中转；1=目标 B 不在线
 *   - A 收到此包后停止重发
 */
#define SIG_PKT_MSG_REQ_ACK_PSZ     (P2P_SESS_ID_PSZ + 3u)                                              // session_id(P2P_SESS_ID_PSZ) + sid(2) + status(1)
/* MSG_RESP (B → Server):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][code(1)][data(N)]
 *   包头: type=0x92, flags=0, seq=0
 *   - session_id: 从 MSG_REQ relay 中取得的 A 的会话 ID
 *   - B 重发此包直到收到 Server → B 的 MSG_RESP_ACK
 * 
 * MSG_RESP (Server → A, relay):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][code(1)][data(N)]
 *   包头: type=0x92, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 A 端验证响应合法性）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - code: 响应码
 *   - data: 响应数据
 *   - Server 重发此包直到收到 A → Server 的 MSG_RESP_ACK
 */
#define SIG_PKT_MSG_RESP_MIN_PSZ    (P2P_SESS_ID_PSZ + 3u)                                              // session_id(P2P_SESS_ID_PSZ) + sid(2) + code(1)
/* MSG_RESP_ACK (Server → B):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)]
 *   包头: type=0x93, flags=0, seq=0
 *   - session_id: B 的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - Server 确认收到 B 的 MSG_RESP，B 停止重发
 *
 * MSG_RESP_ACK (A → Server):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)]
 *   包头: type=0x93, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - A 收到 Server 转发的 MSG_RESP 后发送，流程完成
 */
#define SIG_PKT_MSG_RESP_ACK_PSZ    (P2P_SESS_ID_PSZ + 2u)                                              // session_id(P2P_SESS_ID_PSZ) + sid(2)
/* NAT_PROBE:
 *   payload: 空（无需额外字段）
 *   包头: type=0xA0, flags=0, seq=客户端分配的请求号
 *   - seq 可用于匹配响应，客户端可递增或随机分配
 */

#define SIG_PKT_NAT_PROBE_PSZ       0u                                                                  // 无 payload（仅包头 seq 用于匹配）
/* NAT_PROBE_ACK:
 *   payload: [probe_ip(4)][probe_port(2)]
 *   包头: type=0xA1, flags=0, seq=对应的 NAT_PROBE 请求 seq
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *   - seq: 复制请求包的 seq，用于客户端匹配响应
 */
#define SIG_PKT_NAT_PROBE_ACK_PSZ   6u                                                                  // probe_ip(4) + probe_port(2)
/* 
 * ============================================================================
 * 协议流程详解
 * ============================================================================
 * 
 * 1. 上线与候选同步流程
 * ============================================================================
 *
 * 由于 UDP 包大小限制，候选列表需要分批传输。通过序列化的 SYNC 包完成可靠同步：
 *
 *   1. 上线阶段：
 *      - 客户端发送 ONLINE（含 local_peer_id 与 instance_id）
 *      - 服务器回复 ONLINE_ACK（告知 auth_key、max_candidates、公网地址）
 *        · auth_key=0: 服务器拒绝登录（无可用槽位），客户端停止重试
 *        · auth_key≠0: 登录成功，用于后续 SYNC0/ALIVE 身份验证
 *      - 收到 ONLINE_ACK 后停止 ONLINE 重发，进入 ONLINE 状态
 *
 *   2. 候选同步阶段（序列化 + 确认）：
 *      - 客户端收到 ONLINE_ACK 后立即发送 SYNC0（含 auth_key + remote_peer_id + 首批候选）
 *      - 服务器回复 SYNC0_ACK（含 session_id + online），online=1 表示对端已上线
 *      - 双方上线后，服务器发送 SYNC(seq=0)，包含缓存的对端候选
 *      - 客户端收到后发送 SYNC_ACK（携带 session_id）确认
 *      - 客户端通过 SYNC(seq=1,2,3,...) 继续同步剩余候选（携带 session_id）
 *      - 对端通过 SYNC_ACK 确认，未确认则重发
 *      - 允许乱序：seq>0 可能先于 seq=0 到达，接收端按 seq 位图去重并最终收敛
 *
 *   3. 离线缓存流程：
 *
 *      Alice (在线)           Server                    Bob (离线)
 *        |                       |                          |
 *        |--- ONLINE ----------->|                          |
 *        |<-- ONLINE_ACK --------|  (auth_key + capabilities)
 *        |--- SYNC0 ------------>|  (auth_key + 首批候选)    |
 *        |<-- SYNC0_ACK ---------|  (session_id, online=0)   |
 *        |   [进入 ONLINE]       |  (缓存 Alice 的候选)      |
 *        |    ... Bob 上线 ...                              |
 *        |                       |<-- ONLINE --------------|
 *        |                       |--- ONLINE_ACK ---------->|  (auth_key + capabilities)
 *        |                       |<-- SYNC0 ---------------|  (auth_key + 首批候选)
 *        |                       |--- SYNC0_ACK ----------->|  (session_id, online=1)
 *        |<-- SYNC(seq=0) -------|--- SYNC(seq=0) -------->|  (缓存候选 + session_id)
 *        |--- SYNC_ACK --------->|<-- SYNC_ACK ------------|  (携带 session_id)
 *        |                       |                          |
 *        |<=============== P2P SYNC 序列化同步 ========>|  (所有包携带 session_id)
 *        |--- SYNC(seq=1, base=5) ----------------->  |  (从第 6 个候选开始)
 *        |<-- SYNC_ACK(seq=1) ----------------------  |
 *        |--- SYNC(seq=2, base=10) ---------------->  |
 *        |<-- SYNC_ACK(seq=2) ----------------------  |
 *        |--- SYNC(seq=3, count=0, FIN) ----------->  |  (结束标识)
 *        |<-- SYNC_ACK(seq=3) ----------------------  |
 *
 * 注：ONLINE 仅在上线阶段发送，收到 ONLINE_ACK 后停止（直到重连）；SYNC0 在收到 ONLINE_ACK 后发送
 *
 * 2. MSG RPC 机制（服务器可选实现）
 * ============================================================================
 *
 * 通过服务器中转，实现对端间可信赖的一次请求-应答交互（类似 RPC）。
 * 服务器是否支持由 ONLINE_ACK.flags 中 SIG_ONACK_FLAG_MSG (0x02) 位标识。
 *
 * msg 特殊值（请求消息类型）：
 *   - msg=0: Echo 测试，B端自动回复相同数据，无需应用层介入
 *   - msg>0: 应用层自定义消息类型，需要应用层在 on_request 回调中处理并调用 p2p_response()
 *
 * 错误处理：
 *   1. REQ_ACK 阶段 B 不在线：Server → A 返回 status=1，A 调用 on_response(len=-1, code=原始请求msg)
 *   2. B 在等待响应期间离线：Server → A 发送 MSG_RESP(flags=SIG_MSG_FLAG_PEER_OFFLINE)，
 *      A 调用 on_response(len=-1, code=P2P_MSG_ERR_PEER_OFFLINE)
 *   3. Server 转发请求超时：Server → A 发送 MSG_RESP(flags=SIG_MSG_FLAG_TIMEOUT)，
 *      A 调用 on_response(len=-1, code=P2P_MSG_ERR_TIMEOUT)
 *
 * 流程（7 次传输）：
 *
 *   A                      Server                      B
 *   │                         │                        │
 *   ├── MSG_REQ(sid) ────────►│  A 重发直到收到 REQ_ACK  │
 *   │   [session_id][sid]     │  Server 查找 A 的会话槽  │
 *   │   [msg][data]           │  msg=消息类型            │
 *   │                         │                        │
 *   │◄── MSG_REQ_ACK ─────────┤  status=0 成功/1 B不在线 │
 *   │   [session_id][sid]     │  A 停止重发              │
 *   │   [status]              │                         │
 *   │                         ├── MSG_REQ ─────────────►│  Server 重发直到收到 RESP
 *   │                         │   [session_id][sid]     │  (relay 标志位=1)
 *   │                         │   [msg][data]           │  B 循环比较 sid：
 *   │                         │                         │  sid>last_sid → 执行新请求
 *   │                         │                         │  sid≤last_sid → 忽略旧请求
 *   │                         │◄── MSG_RESP ────────────┤  B 定时重发直到收到 ACK
 *   │                         │   [session_id][sid]     │  Server 收到后停止向 B 发 REQ
 *   │                         │   [code][data]          │  code=响应码
 *   │                         │                         │
 *   │                         ├── MSG_RESP_ACK ────────►│  Server 每次收到 RESP 都回 ACK
 *   │                         │   [session_id][sid]     │  B 收到 ACK 后停止重发
 *   │                         │                         │
 *   │◄── MSG_RESP ────────────┤  Server 转发第一次 RESP 给 A
 *   │   [session_id][sid]     │  后续重发直到收到 A 的 ACK
 *   │   [code][data]          │  (flags 可能标识特殊错误) │
 *   │                         │                         │
 *   ├── MSG_RESP_ACK ────────►│  流程完成                │
 *   │   [session_id][sid]     │                         │
 * 
 */

/* ============================================================================
 * RELAY 模式协议 (TCP)
 * ============================================================================
 *
 * 包头: [type: 1B][size: 2B]
 */

/* RELAY 模式消息类型 */
typedef enum {
    P2P_RLY_STATUS = 0,                      // 状态包（仅服务器发送，包含请求类型 + 状态码）

    /* 在线管理 */
    P2P_RLY_ONLINE,                         // 上线请求: Client -> Server
    P2P_RLY_ONLINE_ACK,                     // 上线确认: Server -> Client (含服务器功能标志)
    P2P_RLY_ALIVE,                          // 心跳: Client -> Server

    /* 会话同步 */
    P2P_RLY_SYNC0,                          // 首次同步: 双向 (Client -> Server 带 target + 首批候选, Server -> Client 转发对端首批候选)
    P2P_RLY_SYNC0_ACK,                      // 首次同步应答: Server -> Client (session_id + online，立即返回)
    P2P_RLY_SYNC,                           // 后续同步: 双向 (Client -> Server 上传, Server -> Client 下发)
    P2P_RLY_SYNC_ACK,                       // 同步确认: Server -> Client (确认候选处理数量，confirmed_count == 0 可表示 FIN 完成)
    P2P_RLY_FIN,                            // 会话结束: Client -> Server / Server -> Client

    /* P2P 数据中继（打洞失败降级） */
    P2P_RLY_DATA,                           // 中继 P2P 数据包: Client <-> Server <-> Client (内层 P2P hdr 区分类型)

    /* 消息 RPC（服务器中转的请求-应答机制） */
    P2P_RLY_REQ,                            // 请求: Client -> Server / Server -> Client (双向)
    P2P_RLY_RESP,                           // 响应: Client -> Server / Server -> Client (双向)
} p2p_relay_type_t;

/* RELAY 模式包头 (3 bytes) */
typedef struct {
    uint8_t             type;
    uint16_t            size;
} p2p_relay_hdr_t;

#define P2P_RLY_CODE_READY           0                  // 服务器就绪，客户端可继续后续操作
#define P2P_RLY_ERR(c)              (100+c)             // 错误码基数，code >= 100 表示错误
#define P2P_RLY_ERR_PROTOCOL        P2P_RLY_ERR(0)      // 协议错误（未登录/非法状态）
#define P2P_RLY_ERR_INTERNAL        P2P_RLY_ERR(1)      // 服务器内部错误
#define P2P_RLY_ERR_NOT_ONLINE      P2P_RLY_ERR(2)      // 未完成 ONLINE 登录
#define P2P_RLY_ERR_PEER_OFFLINE    P2P_RLY_ERR(3)      // 对端未连接（session 存在但 peer 为空）
#define P2P_RLY_ERR_BUSY            P2P_RLY_ERR(4)      // 会话忙（前一个转发尚未完成）
#define P2P_RLY_ERR_TIMEOUT         P2P_RLY_ERR(5)      // 服务器转发请求超时

/* RELAY 上线确认功能标志 */
#define P2P_RLY_FEATURE_RELAY       0x01    // 支持数据包中继
#define P2P_RLY_FEATURE_MSG         0x02    // 支持 MSG RPC 机制
#define P2P_RLY_SYNC_FIN_MARKER     0xFF    // SYNC 负载尾部 FIN 标记字节

/* ============================================================================
 * RELAY 模式协议详细定义说明
 * ============================================================================
 *
 * 所有消息：[p2p_relay_hdr_t: 3B][payload: N bytes]
 *
 * P2P_RLY_STATUS:
 *   payload: [type(1)][status_code(1)][status_msg(N)]
 *   - type: 请求的 p2p_relay_type_t 类型（例如 P2P_RLY_SYNC0），用于指示哪个请求出错
 *   - status_code: 见 P2P_RLY_CODE_* 定义
 *   - status_msg: 可选的状态描述文本（UTF-8 编码）
 */
#define P2P_RLY_STATUS_PSZ          2
 /* P2P_RLY_ONLINE:
 *   payload: [name(32)][instance_id(4)]
 *   - name: 本地 peer 名称，定长 32 字节，0 填充
 *   - instance_id: 客户端每次 online() 生成的 32 位随机数（网络字节序）
 */
#define P2P_RLY_ONLINE_PSZ          (P2P_PEER_ID_MAX + sizeof(uint32_t))
 /* P2P_RLY_ONLINE_ACK:
 *   payload: [features(1)][candidate_sync_max(1)]
 *   - features: 0x01=RELAY, 0x02=MSG
 *   - candidate_sync_max: 单包最大候选数（0=客户端用默认）
 */
#define P2P_RLY_ONLINE_ACK_PSZ      2
 /* P2P_RLY_SYNC0 (双向，上下行负载格式不同):
 *
 *   上行 Client -> Server:
 *     payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
 *     - target_name: 目标 peer 名称，定长 32 字节，0 填充
 *     - candidate_count: 本端首批候选数量（可以为 0）
 *     - candidates: N 个 p2p_candidate_t（每个 23 字节）
 *
 *   下行 Server -> Client（转发对端的首批候选）:
 *     payload: [session_id(P2P_SESS_ID_PSZ)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *     - 格式同 P2P_RLY_SYNC，表示对端重新发起了连接
 *     - 接收端应据此重置会话状态（session_id 变化时强制 p2p_session_reset）
 */
#define P2P_RLY_SYNC0_PSZ(n)        (P2P_PEER_ID_MAX + 1u + (n)*sizeof(p2p_candidate_t))
 /* P2P_RLY_SYNC0_ACK:
 *   payload: [session_id(P2P_SESS_ID_PSZ)][online(1)]
 *   - session_id: 64 位会话 ID（网络字节序）
 *   - online: bool，0=对端离线，1=对端在线
 *   - 该 ACK 对 SYNC0 请求立即返回，仅用于告知会话建立结果
 *   - 若 SYNC0 携带 candidate_count>0，服务器会在候选已处理后，再额外返回一个 SYNC_ACK
 */
#define P2P_RLY_SYNC0_ACK_PSZ       (P2P_SESS_ID_PSZ + 1u)
/* P2P_RLY_SYNC:
 *   payload: [session_id(P2P_SESS_ID_PSZ)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *   - session_id: 64 位会话 ID（网络字节序）
 *   - candidate_count: 本包候选数量
 *   - candidates: N 个 p2p_candidate_t（每个 23 字节）
 *   - fin_marker: 可选 1 字节；存在且为 0xFF 表示 FIN（本端候选发送完成）
*/
#define P2P_RLY_SYNC_PSZ(n, mk)     (P2P_SESS_ID_PSZ + 1u + (n)*sizeof(p2p_candidate_t) + ((mk) ? 1u : 0u))
/* P2P_RLY_SYNC_ACK:
 *   payload: [session_id(P2P_SESS_ID_PSZ)][confirmed_count(1)]
 *   - session_id: 64 位会话 ID（网络字节序）
 *   - confirmed_count: 实际确认处理的候选数（转发或缓存），0=全部完成（仅 FIN 后）
*/
#define P2P_RLY_SYNC_ACK_PSZ        (P2P_SESS_ID_PSZ + 1u)
/* P2P_RLY_FIN:
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   - session_id: 要结束的会话 ID（网络字节序）
*/
#define P2P_RLY_FIN_PSZ             (P2P_SESS_ID_PSZ)

/* P2P_RLY_DATA:
 *   所有 TCP relay 数据包 payload 统一格式: [session_id(P2P_SESS_ID_PSZ)][P2P hdr(4)][data]
 *   P2P hdr = [type(1)][flags(1)][seq(2)]，内层 type 区分实际包类型
 *   (DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK 等均通过 P2P_RLY_DATA 隧道传输)
 *
 *   说明：
 *   - session_id 用于会话隔离与服务器路由（转发到配对会话）。
 *   - 服务器零拷贝转发，仅重写 session_id，不解析内层 P2P hdr。
 */
#define P2P_RLY_DATA_PSZ(n)         (P2P_SESS_ID_PSZ + P2P_HDR_SIZE + (n))

/* P2P_RLY_REQ / P2P_RLY_RESP 最小负载长度（session_id + sid + msg/code = 11 字节） */
#define P2P_RLY_REQ_MIN_PSZ         (P2P_SESS_ID_PSZ + 3)
#define P2P_RLY_RESP_MIN_PSZ        (P2P_SESS_ID_PSZ + 3)

/* P2P_RLY_REQ / P2P_RLY_RESP — 基于会话的 MSG RPC
 *
 * 与 COMPACT 模式的 MSG RPC 对应，但基于 TCP 可靠传输，无需应用层重传。
 * 使用 session_id 路由（与 SYNC/DATA 一致），服务器零拷贝转发时仅重写 session_id。
 *
 * P2P_RLY_REQ (双向，A→Server, Server→B):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][msg(1)][data(N)]
 *   - session_id: 发送方的会话 ID（服务器转发时重写为接收方的 session_id）
 *   - sid: 序列号（非零，循环递增）
 *   - msg: 消息类型（0=echo 自动回复，>0=应用自定义）
 *   - data: 请求数据（最大 P2P_MSG_DATA_MAX 字节）
 *
 * P2P_RLY_RESP (双向，B→Server, Server→A):
 *   payload: [session_id(P2P_SESS_ID_PSZ)][sid(2)][code(1)][data(N)]
 *   - session_id: 本端会话 ID（服务器转发时重写为请求方的 session_id）
 *   - sid: 对应请求的序列号
 *   - code: 响应码（0=成功，应用自定义；0xFE/0xFF=错误，见下）
 *   - data: 响应数据
 *
 * 特殊错误码（服务器生成的错误响应，on_response 回调 len=-1）：
 *   0xFF (P2P_MSG_ERR_PEER_OFFLINE): 对端在等待响应期间离线
 *   0xFE (P2P_MSG_ERR_TIMEOUT): 服务器转发超时
 *
 * 流控：使用 rpc_pending 通道（独立于 SYNC/DATA 的 peer_pending），
 *      每个方向同时最多一个 RPC 消息在传输中。
 *
 * ============================================================================
 * 协议流程详解
 * ============================================================================
 *
 * 基础流程：上线 → 会话同步 → P2P 打洞 → (降级) 数据中继
 *
 * 1. 上线流程（建立 TCP 长连接）
 * ============================================================================
 *
 *   Client                    Server
 *   │                            │
 *   ├── TCP Connect ────────────►│
 *   │                            │
 *   ├── ONLINE ─────────────────►│  [my_name][target_name]
 *   │                            │
 *   │◄── ONLINE_ACK ─────────────┤  [features]
 *   │   (features: RELAY|MSG)     │  告知服务器支持的功能
 *   │                            │
 *   [进入 ONLINE 状态]            │
 *   │                            │
 *   ├── ALIVE ──────────────────►│  (每 20 秒心跳)
 *   │                            │
 *
 * 2. 初始化会话同步（建立会话 + 首批候选同步）
 * ============================================================================
 *
 *   Client A                  Server
 *   │                            │
 *   ├── SYNC0 ─────────────────►│  [target][cnt=5][cands]
 *   │                            │  查找 B 的状态
 *   │                            │  分配 session_id
 *   │◄── SYNC0_ACK ──────────────┤  [sid][online=1]
 *   │   (立即返回，建立会话)       │
 *   │                            │
 *   │◄── SYNC_ACK ───────────────┤  [sid][fwd=5]
 *   │   (仅当 SYNC0.cnt>0 才返回)  │  返回首批候选处理数
 *   │                            │
 *   [收到 session_id，可继续上传]  │
 *
 * 3. 后续会话同步（对端在线，实时转发）
 * ============================================================================
 *
 *   Client A                  Server                    Client B
 *   │                            │                         │
 *   ├── SYNC0 ─────────────────►│  [target=B][cnt=5]     │
 *   │                            │  B 在线，分配 sid=123   │
 *   │◄── SYNC0_ACK ──────────────┤  [sid=123][online=1]
 *   │   (立即返回)                │                          │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=5]      │
 *   │                            │   (A 的 5 个候选)        │
 *   │                            │                         │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=5]       │
 *   │   (SYNC0 首批候选处理确认)   │                          │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=5]       │
 *   │   (上传剩余 5 个候选)       │  立即转发给 B            │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=5]      │
 *   │                            │   (A 的 5 个候选)        │
 *   │                            │                         │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=5]       │
 *   │   (已转发 5 个，ACK 解锁)   │  缓冲区有空间才回        │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=3]       │
 *   │   (再上传 3 个)             │                         │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=3]      │
 *   │                            │                         │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=3]       │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=0][fin=0xFF] │
 *   │   (上传完成，FIN 标记)       │                          │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=0][fin=0xFF] │
 *   │                            │   (A 候选传输完成)            │
 *   │                            │                         │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=0]       │
 *   │   (fwd=0 表示全部完成)      │  FIN 确认               │
 *   │                            │                         │
 *   │<==================== P2P ICE 打洞 ====================>│
 *
 * 4. 后续会话同步（对端离线，缓存后推送）
 * ============================================================================
 *
 *   Client A (在线)           Server                    Client B (离线)
 *   │                            │                         │
 *   ├── ONLINE ─────────────────►│                         │
 *   │◄── ONLINE_ACK ─────────────┤                         │
 *   │                            │                         │
 *   ├── SYNC0 ─────────────────►│  [target=B][cnt=5]      │
 *   │                            │  B 离线                 │
 *   │◄── SYNC0_ACK ──────────────┤  [sid=123][online=0]
 *   │   (立即返回，B 当前离线)     │                          │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=5]       │
 *   │   (上传 5 个候选)           │  尝试缓存               │
 *   │                            │  [缓冲区空间有限]        │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=3]       │
 *   │   (仅缓存了 3 个)           │  有空间才回 ACK          │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=2]       │
 *   │   (从第 4 个重传)           │  继续缓存                │
 *   │                            │                         │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=2]       │
 *   │   (再缓存 2 个，空间满)     │  暂不 ACK，等空间        │
 *   │   [buffer full, no ACK]    │                         │
 *   │                            │                         │
 *   [等待对端上线...]            │                         │
 *   │                            │                         │
 *   │    ... B 上线 ...          │                         │
 *   │                            │◄── ONLINE ──────────────┤
 *   │                            ├── ONLINE_ACK ──────────►│
 *   │                            │                         │
 *   │                            │◄── SYNC0 ───────────────┤  [target=A][cnt=3]
 *   │                            ├── SYNC0_ACK ───────────►│  [sid=456][online=1]
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=3]      │
 *   │                            │   (推送 A 其余候选)      │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=0][fin=0xFF] │
 *   │                            │   (A 候选推送完成)            │
 *   │                            │                         │
 *   │◄── SYNC_ACK ───────────────┤  [sid=123][fwd=5]       │
 *   │   (对端上线后，补回 SYNC0 首批)│  有空间才回 ACK         │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=2]       │
 *   │   (继续上传剩余候选)        │  B 在线，实时转发        │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=2]      │
 *   │                            │                         │
 *   ├── SYNC ──────────────────►│  [sid=123][cnt=0][fin=0xFF] │
 *   │   (上传完成，FIN 标记)       │                          │
 *   │                            │                         │
 *   │                            ├── SYNC ────────────────►│
 *   │                            │   [sid=456][cnt=0][fin=0xFF] │
 *   │                            │                         │
 *   │<==================== P2P ICE 打洞 ====================>│
 *
 *
 * 5. P2P_RLY_DATA - P2P 包中继
 * ============================================================================
 *
 * 功能：P2P 打洞失败时，通过服务器转发 P2P 包（降级方案）
 *
 * Client → Server:
 *   P2P_RLY_DATA: [session_id(P2P_SESS_ID_PSZ)][P2P hdr(4)][payload(N)]
 *   - 内层 P2P hdr.type 区分: DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK
 *   - 服务器零拷贝转发，仅重写 session_id。
 *
 * Server → Target:
 *   - 按目标侧 session_id 重写后原样转发。
 *
 * 6. REQ/RESP 机制 - RPC 请求-应答
 * ============================================================================
 *
 * 功能：通过服务器中转实现可靠的请求-应答机制（TCP 传输，无需 ACK/重传）
 *
 * msg 特殊值：
 *   - msg=0: Echo 测试，B端自动回复相同数据，无需应用层介入
 *   - msg>0: 应用层自定义消息类型，需 on_request 回调处理
 *
 * 流控：使用 rpc_pending 通道（独立于 SYNC/DATA 的 peer_pending），
 *       每个方向同时最多一个 RPC 消息在传输中。
 *
 * 错误处理（服务器生成错误 RESP 返回给 A）：
 *   - 对端离线: code=0xFE (P2P_MSG_ERR_PEER_OFFLINE)
 *   - 转发超时: code=0xFF (P2P_MSG_ERR_TIMEOUT)
 *
 * 流程（4 步）：
 *
 *   A (requester)         Server                    B (responder)
 *   │                        │                        │
 *   ├── RLY_REQ ────────────►│                        │
 *   │  [ses_id_A][sid][msg]  │                        │
 *   │  [data]                │                        │
 *   │                        ├── RLY_REQ ────────────►│
 *   │                        │  [ses_id_B][sid][msg]  │
 *   │                        │  [data]                │
 *   │                        │                        │
 *   │                        │◄── RLY_RESP ───────────┤
 *   │                        │  [ses_id_B][sid][code] │
 *   │                        │  [data]                │
 *   │◄── RLY_RESP ──────────┤                        │
 *   │  [ses_id_A][sid][code] │                        │
 *   │  [data]                │
 *
 *
 * ============================================================================
 * TCP 特性优化
 * ============================================================================
 *   - 仍需 session_id：用于会话隔离与服务器路由到配对会话
 *   - 无需重传机制：TCP 保证可靠传输
 *   - ACK 可选：主要用于流量控制和错误检测
 *
 * TCP 粘包处理：
 * RELAY 使用 TCP 传输，必须处理粘包/半包问题：
 *
 * 接收状态机：
 *   - RECV_HEADER: 读取包头（9 字节）
 *   - RECV_PAYLOAD: 读取 payload（length 字节）
 *
 * 包头格式：[magic: 4B][type: 1B][length: 4B]
 *   - magic = 0x50325030 ("P2P0")，帧同步标识
 *   - type = 消息类型枚举
 *   - length = payload 长度（不包括包头）
 *
 * 循环读取直到 EAGAIN：
 *   ```c
 *   for (;;) {
 *       switch (state) {
 *       case RECV_HEADER:
 *           n = recv(fd, buf + offset, sizeof(hdr) - offset, 0);
 *           if (n == 0) { close(); return; }
 *           if (n < 0 && EAGAIN) return;
 *           offset += n;
 *           if (offset == sizeof(hdr)) {
 *               state = RECV_PAYLOAD;
 *               offset = 0;
 *           }
 *           break;
 *       case RECV_PAYLOAD:
 *           n = recv(fd, payload + offset, length - offset, 0);
 *           if (n < 0 && EAGAIN) return;
 *           offset += n;
 *           if (offset == length) {
 *               dispatch(type, payload);
 *               state = RECV_HEADER;
 *               offset = 0;
 *           }
 *           break;
 *       }
 *   }
 *   ```
 */


/*
 * RELAY 模式信令负载头部
 * todo: 过时定义，目前用于 pubsub 模式
 *
 * 用于封装 ICE 候选交换的元数据。
 * 序列化格式（76字节）：[sender:32B][target:32B][timestamp:4B][delay_trigger:4B][count:4B]
 */
typedef struct {
    char                sender[P2P_PEER_ID_MAX];    // 发送方 peer_id
    char                target[P2P_PEER_ID_MAX];    // 目标方 peer_id
    uint32_t            timestamp;                  // 时间戳（用于排序和去重）
    uint32_t            delay_trigger;              // 延迟触发打洞（毫秒）
    int                 candidate_count;            // ICE 候选数量
} p2p_signaling_payload_hdr_t;

#pragma pack(pop)
#pragma ide diagnostic pop
#pragma clang diagnostic pop
#endif /* P2PP_H */
