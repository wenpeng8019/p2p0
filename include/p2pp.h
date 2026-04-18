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
#define P2P_SESS_ID_PSZ     (sizeof(uint32_t))

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
    buf += 2;
    nwrite_s(buf, seq);
}

static inline void p2p_pkt_hdr_decode(const uint8_t *buf, p2p_packet_hdr_t *hdr) {
    hdr->type  = buf[0];
    hdr->flags = buf[1];
    buf += 2;
    nread_s(&hdr->seq, buf);
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
 * 当 flags & P2P_FLAG_SESSION 时，所有包在 hdr(4) 之后前置 session_id(P2P_SESS_ID_PSZ)，
 * 详见下方 P2P_FLAG_SESSION 说明。
 */
#define P2P_PKT_DATA            0x20        // 数据包
#define P2P_PKT_ACK             0x21        // 确认包
#define P2P_PKT_CRYPTO          0x22        // DTLS 加密包（握手/密文数据）

#define P2P_PKT_ACK_PSZ             6u                          // ack_seq(2) + sack(4)（无 session_id）
#define P2P_PKT_ACK_SESSION_PSZ     (P2P_SESS_ID_PSZ + 6u)      // session_id(P2P_SESS_ID_PSZ) + ack_seq(2) + sack(4)

/* 
 * P2P_FLAG_SESSION / SIG_FLAG_RELAY 说明：
 *
 *   P2P_FLAG_SESSION (0x01): 包头后携带 session_id(P2P_SESS_ID_PSZ)
 *     - 多会话模式下（p2p_config_t.multi_session=true）由发送方设置，接收方据此路由到正确 session
 *     - 信令服务器中转（relay）路径上同时设置此位（接收方用于会话隔离验证）
 *     适用包类型: PUNCH / DATA / ACK / CRYPTO / REACH / CONN / CONN_ACK / FIN
 *
 *   SIG_FLAG_RELAY (0x02): 此包经信令服务器中转（非 P2P 直连）
 *     - 由信令中转接口（signaling_relay_fn）在发送时自动设置
 *     - 信令中转包同时设置 P2P_FLAG_SESSION（携带 session_id 供服务器路由）
 *     - 接收方在 REACH 处理时需清除此标志，再传给 NAT 层
 *     适用包类型: PUNCH / DATA / ACK / CRYPTO / REACH / CONN / CONN_ACK / FIN（中转版本）
 *
 *   未设任何标志（直连单会话）:
 *     PUNCH:    [hdr(4)][target_addr(6)]
 *     DATA:     [hdr(4)][data(N)]
 *     ACK:      [hdr(4)][ack_seq(2)][sack(4)]
 *     CRYPTO:   [hdr(4)][crypto_data(N)]
 *     REACH:    [hdr(4)][target_addr(6)]
 *     CONN:     [hdr(4)]
 *     CONN_ACK: [hdr(4)]
 *     FIN:      [hdr(4)]
 *
 *   P2P_FLAG_SESSION 已设（多会话直连 或 信令中转）:
 *     PUNCH:    [hdr(4)][session_id(P2P_SESS_ID_PSZ)][target_addr(6)]
 *     DATA:     [hdr(4)][session_id(P2P_SESS_ID_PSZ)][data(N)]
 *     ACK:      [hdr(4)][session_id(P2P_SESS_ID_PSZ)][ack_seq(2)][sack(4)]
 *     CRYPTO:   [hdr(4)][session_id(P2P_SESS_ID_PSZ)][crypto_data(N)]
 *     REACH:    [hdr(4)][session_id(P2P_SESS_ID_PSZ)][target_addr(6)]
 *     CONN:     [hdr(4)][session_id(P2P_SESS_ID_PSZ)]
 *     CONN_ACK: [hdr(4)][session_id(P2P_SESS_ID_PSZ)]
 *     FIN:      [hdr(4)][session_id(P2P_SESS_ID_PSZ)]
 *
 *   session_id 用于:
 *     1. 多会话派发: 接收方使用 session_id 路由到对应 p2p_session（multi_session 模式）
 *     2. 会话隔离: 过滤旧会话重传的包（解决重连污染问题）
 *     3. 服务器路由: 信令中转时服务器通过 session_id 查找目标对端
 */
#define P2P_FLAG_SESSION            0x01    // 携带 session_id（紧跟包头），用于多会话派发/会话隔离/中继路由
#define SIG_FLAG_RELAY              0x02    // 经信令服务器中转（非直连），同时携带 P2P_FLAG_SESSION

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

#define SIG_PKT_SYNC0           0x85        // 首次候选同步（双向）：client→server 提交首批候选；server→client 下发对端候选（详见 COMPACT 模式协议详细说明）
#define SIG_PKT_SYNC0_ACK       0x86        // 首批候选确认（server→client）：[session_id(P2P_SESS_ID_PSZ)][online(1)]，session_id = 对端配对会话 ID
#define SIG_PKT_SYNC            0x87        // 候选列表同步包（序列化传输）
#define SIG_PKT_SYNC_ACK        0x88        // 候选列表确认（确认指定序列号）
#define SIG_PKT_FIN             0x89        // 对端已离线/断开

/* SYNC 标志位（p2p_packet_hdr_t.flags） */
#define SIG_SYNC_FLAG_FIN           0x01    // 候选列表发送完毕

/* MSG RPC 包类型（服务器可选实现，详见协议详细说明节） */
#define SIG_PKT_MSG_REQ         0x90        // MSG 请求：A→Server；Server→B relay（flags=SIG_FLAG_RELAY）
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
/* SIG_FLAG_RELAY (0x02) 复用为 MSG_REQ/MSG_RESP relay 标志：标识此包是 Server→B/A 的中转包 */

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
 #define SIG_PKT_ONLINE_PSZ          (P2P_PEER_ID_MAX + sizeof(uint32_t))                               // peer_id(32) + instance_id(4)
/* ONLINE_ACK:
 *   payload: [instance_id(4)][auth_key(SIG_AUTH_KEY_PSZ)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
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
 #define SIG_PKT_ONLINE_ACK_PSZ      (sizeof(uint32_t) + SIG_AUTH_KEY_PSZ + 1u + 4u + 2u + 2u)          // instance_id(4) + auth_key(SIG_AUTH_KEY_PSZ) + max_cands(1) + ip(4) + port(2) + probe(2)
/* OFFLINE:
 *   payload: [auth_key(SIG_AUTH_KEY_PSZ)]
 *   包头: type=0x82, flags=0, seq=0
 *   - auth_key: 来自 ONLINE_ACK 的客户端-服务器认证令牌（network byte order），服务器用于 O(1) 查找并释放配对槽位
 *   客户端主动断开时发送，请求服务器立即释放配对槽位
 *   服务器收到后会向对端发送 FIN 通知
 */
 #define SIG_PKT_OFFLINE_PSZ         SIG_AUTH_KEY_PSZ                                                   // auth_key(SIG_AUTH_KEY_PSZ)
/* ALIVE:
 *   payload: [auth_key(SIG_AUTH_KEY_PSZ)]
 *   包头: type=0x83, flags=0, seq=0
 *   - auth_key: 客户端-服务器认证令牌（来自 ONLINE_ACK），用于服务器识别并更新槽位活跃时间
 *   用于客户端在 ONLINE/READY 状态定期发送，保持服务器槽位活跃
 */
 #define SIG_PKT_ALIVE_PSZ           (SIG_AUTH_KEY_PSZ)                                                 // auth_key(SIG_AUTH_KEY_PSZ)
/* ALIVE_ACK:
 *   payload: 空（仅包头）
 *   包头: type=0x84, flags=0, seq=0
 *   服务器回复确认，表示槽位仍然有效
 */
 #define SIG_PKT_ALIVE_ACK_PSZ       0u                                                                 // 无 payload
/* SYNC0（双向首次 sync，两方向 payload 格式不同，由各端角色区分处理）:
 *
 * 方向 1: client → server（建立和对端连接，并提交首批同步候选）
 *   payload: [auth_key(SIG_AUTH_KEY_PSZ)][remote_peer_id(P2P_PEER_ID_MAX)][candidate_count(1)][candidates(N*23)]
 *   - auth_key: 来自 ONLINE_ACK 的客户端-服务器认证令牌（network byte order）
 *   - remote_peer_id: 目标对端 ID（32字节，不足补零）
 *   - candidate_count: 首批候选数量（最多 max_candidates 个）
 *   - candidates: 首批候选地址列表（每 23 字节，p2p_candidate_t 格式）
 *   客户端在收到 ONLINE_ACK 后立即发送，同时完成：
 *     1. 提交首批候选供服务器缓存
 *     2. 指定 remote_peer_id，建立与对端的配对关系
 *
 * 方向 2: server → client（对端已配对后触发，下发对端候选地址）
 *   payload: [remote_peer_id(P2P_PEER_ID_MAX)][session_id(P2P_SESS_ID_PSZ)][0x00(1)][candidate_count(1)][candidates(N*23)]
 *   - remote_peer_id: 对端 ID（32字节，不足补零），用于客户端多会话派发定位目标 session
 *   - session_id: 对端配对会话 ID（network byte order，由服务器在配对成功时分配）
 *   - 0x00: 保留字节（固定为 0，供 unpack_remote_candidates 识别为初始推送）
 *   - candidate_count: 对端候选数量（首个必须是服务器观察到的对端公网地址 srflx）
 *   - candidates: 对端候选地址列表
 *   客户端收到后以 SIG_PKT_SYNC0_ACK（client→server 方向）确认
 */
#define SIG_PKT_SYNC0_PSZ(n)        (SIG_AUTH_KEY_PSZ + P2P_PEER_ID_MAX + 1u + (n)*sizeof(p2p_candidate_t)) // client→server: auth_key(SIG_AUTH_KEY_PSZ) + peer_id(32) + count(1) + cands(n*23)
#define SIG_PKT_SYNC0_S2C_PSZ(n)    (P2P_PEER_ID_MAX + P2P_SESS_ID_PSZ + 2u + (n)*sizeof(p2p_candidate_t)) // server→client: remote_peer_id(32) + session_id(P2P_SESS_ID_PSZ) + reserved(1) + count(1) + cands(n*23)
/* SYNC0_ACK（双向，两端 payload 格式不同）:
 *
 * 方向 1: server → client（对 client SYNC0 的回复）
 *   payload: [remote_peer_id(P2P_PEER_ID_MAX)][session_id(P2P_SESS_ID_PSZ)][online(1)]
 *   - remote_peer_id: 对端 ID（32字节，不足补零），用于客户端多会话派发定位目标 session
 *   - session_id: 对端配对会话 ID（network byte order, 64-bit），标识 client↔peer 会话
 *     · 语义不同于 auth_key（auth_key 标识 client↔server）
 *     · 用于后续所有 SYNC/SYNC_ACK/FIN/DATA relay/MSG 包的身份验证
 *   - online: 1=对端已上线（已有对端配对），0=对端尚未上线
 *   服务器收到 client SYNC0 后回复，通知客户端候选已缓存以及对端是否已上线
 *
 * 方向 2: client → server（对 server SYNC0_ACK 的二次回复）
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   客户端收到 server 的 SYNC0_ACK 后，再次回复 SYNC0_ACK。
 *   而服务器在收到二次确认前，会确保不会将对端的 SYNC0 包提前转发过来，以确保 session id 的先后一致性。
 *
 * 方向 3: client → server（对 server SYNC0 的回复）
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   - session_id: 来自 server SYNC0 中的会话 ID，用于服务器确认对应配对
 *   客户端收到 server SYNC0（首次对端候选推送）后立即回复
 */
#define SIG_PKT_SYNC0_ACK_PSZ       (P2P_PEER_ID_MAX + P2P_SESS_ID_PSZ + 1u)                             // server→client: remote_peer_id(32) + session_id(P2P_SESS_ID_PSZ) + online(1)
#define SIG_PKT_SYNC0_ACK_C2S_PSZ   (P2P_SESS_ID_PSZ)                                                   // client→server: session_id(P2P_SESS_ID_PSZ)
/* SYNC:
 *   payload: [session_id(P2P_SESS_ID_PSZ)][notify_seq_or_base(1)][candidate_count(1)][candidates(N*23)]
 *   包头: type=0x87, flags=见下, seq=序列号
 *   - session_id: 会话 ID（网络字节序，64位，来自 SYNC0_ACK）
 *   - 字段[P2P_SESS_ID_PSZ]: 语义随 seq 分两种（内容不同，位置相同）
 *       seq=0: 循环通知序号 notify_seq（1..255 循环），接收端据此排重
 *       seq>0: 候选起始索引 base_index（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（配合 FIN 标志）
 *   - seq=0: 服务器发送，对端公网地址变更通知（candidate_count 必须为 1）
 *       * 客户端收到后以 SYNC_ACK(seq=0) 确认
 *       * 接收端按 notify_seq 循环序比较新旧，旧通知可忽略但仍需 ACK
 *       * 服务器首次对端候选推送使用独立 opcode SIG_PKT_SYNC0（server→client 方向）
 *   - seq>0: 客户端发送，base_index 递增，继续同步剩余候选
 *   - flags: 包头的 flags 字段可设置 SIG_SYNC_FLAG_FIN (0x01) 表示候选列表发送完毕
 *   - seq 窗口: 0..16（1..16 为客户端候选批次，0 为地址变更通知）
 *   - 乱序处理: 允许 seq>0 先于 SYNC0 到达；接收端按序号位图去重，重复包仅 ACK 不重复入表
 */
#define SIG_PKT_SYNC_PSZ(n)         (P2P_SESS_ID_PSZ + 2u + (n)*sizeof(p2p_candidate_t))                // session_id(P2P_SESS_ID_PSZ) + base(1) + count(1) + cands(n*23)
/* SYNC_ACK:
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   包头: type=0x88, flags=0, seq=确认的序列号
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - seq=0: 客户端→服务器，确认地址变更通知（SYNC seq=0）
 *       注：服务器 SYNC0（server→client 首次候逳推送）由 SIG_PKT_SYNC0_ACK 确认，不使用此类型
 *   - seq>0: 服务器→客户端，确认客户端发送的 SYNC(seq>0) 候选批次；或客户端→服务器 relay 转发
 *   - seq 窗口: 0..16
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
 *   2. 候选同步阶段（三次握手 + 序列化确认）：
 *      - 客户端收到 ONLINE_ACK 后立即发送 SYNC0（含 auth_key + remote_peer_id + 首批候选）
 *      - 服务器回复 SYNC0_ACK（含 session_id + online），online=1 表示对端已上线
 *      - 客户端收到 SYNC0_ACK 后，回复 SYNC0_ACK（二次确认，含 session_id）
 *      - 服务器在收到二次确认前，不会将对端的 SYNC0 转发过来（确保 session_id 先建立）
 *      - 双方上线且均已二次确认后，服务器向双方发送 SYNC0，包含缓存的对端候选
 *      - 客户端收到 SYNC0 后发送 SYNC0_ACK（携带 session_id）确认
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
 *        |--- SYNC0 ------------>|  (auth_key + 首批候选)   |
 *        |<-- SYNC0_ACK ---------|  (session_id, online=0)  |
 *        |--- SYNC0_ACK -------->|  (二次确认, session_id)  |
 *        |   [进入 ONLINE]       |  (缓存 Alice 的候选)     |
 *        |    ... Bob 上线 ...                              |
 *        |                       |<-- ONLINE ---------------|
 *        |                       |--- ONLINE_ACK ---------->|  (auth_key + capabilities)
 *        |                       |<-- SYNC0 ----------------|  (auth_key + 首批候选)
 *        |                       |--- SYNC0_ACK ----------->|  (session_id, online=1)
 *        |                       |<-- SYNC0_ACK ------------|  (二次确认, session_id)
 *        |<-- SYNC0 -------------|--- SYNC0 --------------->|  (缓存候选 + session_id)
 *        |--- SYNC0_ACK -------->|<-- SYNC0_ACK ------------|  (携带 session_id)
 *        |                       |                          |
 *        |<=============== P2P SYNC 序列化同步 ============>|  (所有包携带 session_id)
 *        |----------- SYNC(seq=1, base=5) ----------------->|  (从第 6 个候选开始)
 *        |<---------- SYNC_ACK(seq=1) ----------------------|
 *        |----------- SYNC(seq=2, base=10) ---------------->|
 *        |<---------- SYNC_ACK(seq=2) ----------------------|
 *        |----------- SYNC(seq=3, count=0, FIN) ----------->|  (结束标识)
 *        |<---------- SYNC_ACK(seq=3) ----------------------|
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
 *   A                      Server                            B
 *   │                         │                              │
 *   ├── MSG_REQ(sid) ───────►│  A 重发直到收到 REQ_ACK      │
 *   │   [session_id][sid]     │  Server 查找 A 的会话槽      │
 *   │   [msg][data]           │  msg=消息类型                │
 *   │                         │                              │
 *   │◄── MSG_REQ_ACK ────────┤  status=0 成功/1 B不在线     │
 *   │   [session_id][sid]     │  A 停止重发                  │
 *   │   [status]              │                              │
 *   │                         ├────── MSG_REQ ─────────────►│  Server 重发直到收到 RESP
 *   │                         │   [session_id][sid]          │  (relay 标志位=1)
 *   │                         │   [msg][data]                │  B 循环比较 sid：
 *   │                         │                              │  sid>last_sid → 执行新请求
 *   │                         │                              │  sid≤last_sid → 忽略旧请求
 *   │                         │◄────── MSG_RESP ────────────┤  B 定时重发直到收到 ACK
 *   │                         │   [session_id][sid]          │  Server 收到后停止向 B 发 REQ
 *   │                         │   [code][data]               │  code=响应码
 *   │                         │                              │
 *   │                         ├────── MSG_RESP_ACK ────────►│  Server 每次收到 RESP 都回 ACK
 *   │                         │   [session_id][sid]          │  B 收到 ACK 后停止重发
 *   │                         │                              │
 *   │◄── MSG_RESP ───────────┤  Server 转发第一次 RESP 给 A │
 *   │   [session_id][sid]     │  后续重发直到收到 A 的 ACK   │
 *   │   [code][data]          │  (flags 可能标识特殊错误)    │
 *   │                         │                              │
 *   ├── MSG_RESP_ACK ───────►│  流程完成                    │
 *   │   [session_id][sid]     │                              │
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
    P2P_RLY_ONLINE,                         // 上线: Client -> Server / Server -> Client（双向，上下行 payload 不同）
    P2P_RLY_ALIVE,                          // 心跳: Client -> Server / Server -> Client（双向，空 payload）

    /* 会话同步 */
    P2P_RLY_SYNC0,                          // 首次同步: 三态 (Client -> Server 发起建会; Server -> Client 建会应答; Server -> Client 转发对端首批候选)
    P2P_RLY_SYNC,                           // 后续同步: 三态 (Client -> Server 上传; Server -> Client 下发; Server -> Client 确认处理数量)
    P2P_RLY_FIN,                            // 会话结束: Client -> Server / Server -> Client

    /* P2P 数据中继（打洞失败降级） */
    P2P_RLY_PACKET,                         // 中继 P2P 数据包: Client <-> Server <-> Client (内层 P2P hdr 区分类型)

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
#define P2P_RLY_ERR(c)              (0x80+c)            // 错误码基数，code >= 0x80 表示错误
#define P2P_RLY_ERR_INTERNAL        P2P_RLY_ERR(0)      // 服务器内部错误。此时应该断开和服务器的连接，等待重连恢复
#define P2P_RLY_ERR_PROTOCOL        P2P_RLY_ERR(1)      // 协议错误（未登录/非法状态）
#define P2P_RLY_ERR_NOT_ONLINE      P2P_RLY_ERR(2)      // 未完成 ONLINE 登录
#define P2P_RLY_ERR_PEER_OFFLINE    P2P_RLY_ERR(3)      // 对端未连接（session 存在但 peer 为空）
#define P2P_RLY_ERR_BUSY            P2P_RLY_ERR(4)      // 会话忙（前一个转发尚未完成）
#define P2P_RLY_ERR_TIMEOUT         P2P_RLY_ERR(5)      // 服务器转发请求超时

/* RELAY ONLINE 下行功能标志 */
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
 *   payload: [type(1)][status_code(1)][[session_id(P2P_SESS_ID_PSZ)]|remote_peer_id(P2P_PEER_ID_MAX)][status_msg(N)]
 *   - type: 请求的 p2p_relay_type_t 类型（例如 P2P_RLY_SYNC0），用于指示哪个请求出错
 *   - status_code: 见 P2P_RLY_CODE_* 定义
 *   - session_id: 会话 ID，对于会话相关的请求（如 SYNC）存在时携带，用于客户端识别对应会话；对于非会话请求（如 ONLINE）则不携带
 *                 注意：P2P_RLY_SYNC0 请求尚未建立会话，因此返回的 STATUS 不携带 session_id，但会携带 remote_peer_id 以指示哪个对端的连接请求出错
 *   - status_msg: 可选的状态描述文本（UTF-8 编码）
 */
#define P2P_RLY_STATUS_PSZ(s, n)    (2 + ((s)==2 ? P2P_SESS_ID_PSZ : ((s) == 1 ? P2P_PEER_ID_MAX : 0)) + (n)) // type(1) + status_code(1) + route_key + status_msg(N)
 /* P2P_RLY_ONLINE（双向，上下行负载格式不同）:
 *
 *   上行 Client -> Server:
 *     payload: [name(32)][instance_id(4)]
 *     - name: 本地 peer 名称，定长 32 字节，0 填充
 *     - instance_id: 客户端每次 online() 生成的 32 位随机数（网络字节序）
 *
 *   下行 Server -> Client:
 *     payload: [features(1)][candidate_sync_max(1)]
 *     - features: 0x01=RELAY, 0x02=MSG
 *     - candidate_sync_max: 单包最大候选数（0=客户端用默认）
 */
#define P2P_RLY_ONLINE_PSZ          (P2P_PEER_ID_MAX + sizeof(uint32_t))
#define P2P_RLY_ONLINE_S2C_PSZ      2u
 /* P2P_RLY_ALIVE（双向）:
 *   payload: 空（仅包头）
 *   - Client -> Server: 心跳保活
 *   - Server -> Client: 心跳确认
 */
#define P2P_RLY_ALIVE_PSZ           0u
/* P2P_RLY_SYNC0（三态，上下行负载格式不同）:
 * + 注意一个细节，SYNC0 的候选肯定不带有 FIN 标识，也就是 SYN0 肯定不是最后一个 SYNC 包
 *
 *   状态 1: Client -> Server（发起会话创建，并提交首批候选）
 *     payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
 *     - target_name: 目标 peer 名称，定长 32 字节，0 填充
 *     - candidate_count: 本端首批候选数量（可以为 0）
 *     - candidates: N 个 p2p_candidate_t（每个 23 字节）
 *
 *   状态 2: Server -> Client（会话创建应答，对方不在线，立即返回）
 *     payload: [target_name(32)][session_id(P2P_SESS_ID_PSZ)][0xFF(1)]
 *     - target_name: 目标 peer 名称，定长 32 字节，0 填充
 *     - session_id: 64 位会话 ID（网络字节序）
 *     - 0xFF(1): 常量，表示对端离线
 *
 *   状态 3: Server -> Client（转发对端同步数据，也就是首批候选集合，同时表示对端在线/或上线）
 *     payload: [source_name(32)][session_id(P2P_SESS_ID_PSZ)][candidate_count(1)][candidates(N*23)]
 *     - 格式同 P2P_RLY_SYNC，但不包括 fin_marker 字段
 *     - candidate_count < 0xFF，0xFF 保留给状态 2 的离线标识
 */
#define P2P_RLY_SYNC0_PSZ(n)            (P2P_PEER_ID_MAX + 1u + (n)*sizeof(p2p_candidate_t))
#define P2P_RLY_SYNC0_S2C_PSZ(n)        (P2P_SESS_ID_PSZ + P2P_RLY_SYNC0_PSZ(n))
#define P2P_RLY_IS_SYNC0_OFFLINE(p)     (((uint8_t*)(p))[P2P_PEER_ID_MAX + P2P_SESS_ID_PSZ] == 0xFF) 
/* P2P_RLY_SYNC（三态，上下行负载格式不同）:
 *
 *   状态 1: Client -> Server（上传本端后续候选）
 *     payload: [session_id(P2P_SESS_ID_PSZ)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *     - session_id: 64 位会话 ID（网络字节序）
 *     - candidate_count: 本包候选数量
 *     - candidates: N 个 p2p_candidate_t（每个 23 字节）
 *     - fin_marker: 可选 1 字节；存在且为 0xFF 表示 FIN（本端候选发送完成）
 *
 *   状态 2: Server -> Client（确认候选处理数量）
 *     payload: [session_id(P2P_SESS_ID_PSZ)][confirmed_count(1)]
 *     - session_id: 64 位会话 ID（网络字节序）
 *     - confirmed_count: 实际确认处理的候选数（转发或缓存），0=全部完成（仅 FIN 后）
 *     ! 下行的 SYNC 并非是上行 SYNC 的应答。例如，上行 2 个 SYNC，返回 1 SYNC，confirmed_count 是上行两个 SYNC 中累计确认的候选数量
 * 
 *   状态 3: Server -> Client（下发/转发对端候选）
 *     payload: [session_id(P2P_SESS_ID_PSZ)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *     - 与状态 1 同格式，session_id 由服务器重写为接收方本地会话 ID
 */
#define P2P_RLY_SYNC_PSZ(n, mk)         (P2P_SESS_ID_PSZ + 1u + (n)*sizeof(p2p_candidate_t) + ((mk) ? 1u : 0u))
#define P2P_RLY_IS_SYNC_CONFIRM(hdr)    (((p2p_relay_hdr_t*)(hdr))->size == P2P_RLY_SYNC_PSZ(0, 0))
/* P2P_RLY_FIN:
 *   payload: [session_id(P2P_SESS_ID_PSZ)]
 *   - session_id: 要结束的会话 ID（网络字节序）
*/
#define P2P_RLY_FIN_PSZ             (P2P_SESS_ID_PSZ)

/* P2P_RLY_PACKET:
 *   所有 TCP relay 数据包 payload 统一格式: [session_id(P2P_SESS_ID_PSZ)][P2P hdr(4)][data]
 *   P2P hdr = [type(1)][flags(1)][seq(2)]，内层 type 区分实际包类型
 *   (DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK 等均通过 P2P_RLY_PACKET 隧道传输)
 *
 *   说明：
 *   - session_id 用于会话隔离与服务器路由（转发到配对会话）。
 *   - 服务器零拷贝转发，仅重写 session_id，不解析内层 P2P hdr。
 */
#define P2P_RLY_PACKET_PSZ(n)       (P2P_SESS_ID_PSZ + P2P_HDR_SIZE + (n))

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
 *   - session_id: 发送方的会话 ID（来自初次下行 SYNC0 建会应答；服务器转发时重写为接收方的 session_id）
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
 *   ├── TCP Connect ───────────►│
 *   │                            │
 *   ├── ONLINE ────────────────►│  [name][instance_id]
 *   │                            │
 *   │◄── ONLINE ────────────────┤  [features][candidate_sync_max]
 *   │   (features: RELAY|MSG)    │  告知服务器功能与候选批量上限
 *   │                            │
 *   [进入 ONLINE 状态]           │
 *   │                            │
 *   ├── ALIVE ─────────────────►│  (每 20 秒心跳)
 *   │                            │
 *   │◄── ALIVE ─────────────────┤  (空 payload 确认)
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
 *   │◄── SYNC0 ─────────────────┤  [target][sid][state=0x00]
 *   │   (状态 2: 建会应答)       │
 *   │                            │
 *   │◄── SYNC ──────────────────┤  [sid][fwd=5]
 *   │ (状态 3: 首批候选处理确认) │  返回首批候选处理数
 *   │                            │
 *   [收到 session_id，可继续上传] 
 *
 * 3. 后续会话同步（对端在线，实时转发）
 * ============================================================================
 *
 *   Client A                     Server                         Client B
 *   │                               │                              │
 *   ├───── SYNC0 ─────────────────►│  [target=B][cnt=5]           │
 *   │                               │  B 在线，分配 sid=123        │
 *   │◄──── SYNC0 ──────────────────┤ [target=B][sid=123][state=0x00]│
 *   │   (状态 2: 建会应答)          │                              │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=5]           │
 *   │                               │   (A 的 5 个候选)            │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=5]            │
 *   │   (状态 3: SYNC0 首批候选确认)│                              │
 *   │                               │                              │
 *   ├───── SYNC ──────────────────►│  [sid=123][cnt=5]            │
 *   │   (上传剩余 5 个候选)         │  立即转发给 B                │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=5]           │
 *   │                               │   (A 的 5 个候选)            │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=5]            │
 *   │   (状态 3: 已转发 5 个)       │  缓冲区有空间才回            │
 *   │                               │                              │
 *   ├───── SYNC ──────────────────►│  [sid=123][cnt=3]            │
 *   │   (再上传 3 个)               │                              │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=3]           │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=3]            │
 *   │                               │                              │
 *   ├───── SYNC ──────────────────►│  [sid=123][cnt=0][fin=0xFF]  │
 *   │   (上传完成，FIN 标记)        │                              │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=0][fin=0xFF] │
 *   │                               │   (A 候选传输完成)           │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=0]            │
 *   │   (状态 3: fwd=0 表示全部完成)│  FIN 确认                    │
 *   │                               │                              │
 *   │<======================== P2P ICE 打洞 ======================>│
 *
 * 4. 后续会话同步（对端离线，缓存后推送）
 * ============================================================================
 *
 *   Client A (在线)           Server                         Client B (离线)
 *   │                               │                              │
 *   ├──── ONLINE ─────────────────►│                              │
 *   │◄──── ONLINE ─────────────────┤                              │
 *   │                               │                              │
 *   ├───── SYNC0 ─────────────────►│  [target=B][cnt=5]           │
 *   │                               │  B 离线                      │
 *   │◄──── SYNC0 ──────────────────┤ [target=B][sid=123][state=0xFF]|
 *   │ (状态 2: 建会应答，B 当前离线)│                              │
 *   │                               │                              │
 *   ├───── SYNC ──────────────────►│  [sid=123][cnt=5]            │
 *   │   (上传 5 个候选)             │  尝试缓存                    │
 *   │                               │  [缓冲区空间有限]            │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=3]            │
 *   │   (状态 3: 仅缓存了 3 个)     │  有空间才回确认              │
 *   │                               │                              │
 *   ├───── SYNC ──────────────────►│  [sid=123][cnt=2]            │
 *   │   (从第 4 个重传)             │  继续缓存                    │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=2]            │
 *   │   (状态 3: 再缓存 2 个，空间满)│  暂不确认，等空间           │
 *   │   [buffer full, no confirm]   │                              │
 *   │                               │                              │
 *   [等待对端上线...]               │                              │
 *   │                               │                              │
 *   │    ... B 上线 ...             │                              │
 *   │                               │◄────── ONLINE ──────────────┤
 *   │                               ├────── ONLINE ──────────────►│
 *   │                               │                              │
 *   │                               │◄────── SYNC0 ───────────────┤  [target=A][cnt=3]
 *   │                               ├────── SYNC0 ───────────────►│  [target=A][sid=456][state=0x00]
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=3]           │
 *   │                               │   (推送 A 其余候选)          │
 *   │                               │                              │
 *   │                               ├───────── SYNC ─────────────►│
 *   │                               │   [sid=456][cnt=0][fin=0xFF] │
 *   │                               │   (A 候选推送完成)           │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=5]            │
 *   │ (状态 3: 对端上线后补回首批)  │  有空间才回确认              │
 *   │                               │                              │
 *   ├───── SYNC ──────────────────►│  [sid=123][cnt=2]            │
 *   │   (继续上传剩余候选)          │  B 在线，实时转发            │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=2]           │
 *   │                               │                              │
 *   ├──────── SYNC ───────────────►│  [sid=123][cnt=0][fin=0xFF]  │
 *   │   (上传完成，FIN 标记)        │                              │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │   [sid=456][cnt=0][fin=0xFF] │
 *   │                               │                              │
 *   │<======================== P2P ICE 打洞 ======================>│
 *
 *
 * 5. P2P_RLY_PACKET - P2P 包中继
 * ============================================================================
 *
 * 功能：P2P 打洞失败时，通过服务器转发 P2P 包（降级方案）
 *
 * Client → Server:
 *   P2P_RLY_PACKET: [session_id(P2P_SESS_ID_PSZ)][P2P hdr(4)][payload(N)]
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
 *   ├── RLY_REQ ───────────►│                        │
 *   │  [ses_id_A][sid][msg]  │                        │
 *   │  [data]                │                        │
 *   │                        ├── RLY_REQ ───────────►│
 *   │                        │  [ses_id_B][sid][msg]  │
 *   │                        │  [data]                │
 *   │                        │                        │
 *   │                        │◄── RLY_RESP ──────────┤
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


/* ============================================================================
 * WS_ICE 模式协议 (WebSocket, 纯文本帧)
 * ============================================================================
 *
 * 基于 WebSocket 的 ICE 信令通道，为 P2P_SIGNALING_MODE_ICE 模式提供
 * ICE 候选交换。每条 WS text frame 承载一条消息，纯文本格式。
 *
 * 与 COMPACT/RELAY 的二进制协议不同，WS_ICE 采用可读文本协议，
 * 便于调试和跨语言集成（如浏览器 JavaScript 客户端）。
 *
 * 传输层：WebSocket (RFC 6455)，text frame (opcode=0x1)
 * 编码：UTF-8 纯文本
 * 分帧：每条 WS text frame = 一条完整消息，无粘包问题
 * 可靠性：WebSocket over TCP，保证有序可靠传输，无需应用层重传
 *
 * 服务端维护 client_t / session_t 体系，提供会话感知的信令服务：
 * - 断线重连保留会话状态（类似 RELAY fd 迁移）
 * - 对端上线主动通知（统一 SYNC0 推送，应答与推送同格式）
 * - 离线客户端超时清理（WS_ICE_CLIENT_TIMEOUT_S = 60s）
 *
 * 消息格式通用规则：
 * - 第一个空格前为命令关键字（大写）
 * - 空格分隔字段
 * - SYNC 消息中第一个 '\n' 分隔 session_id 与 payload
 * - peer_id: UTF-8 字符串，最长 P2P_PEER_ID_MAX-1 字节（不含 NUL）
 * - session_id: uint32 十进制 ASCII 表示
 */

 /* WS_ICE 消息类型前缀（纯文本匹配，非二进制编码） */
#define P2P_WS_ICE_CMD_REG          "REG "          /* + <peer_id> <instance_id> */          // 注册身份
#define P2P_WS_ICE_CMD_OFF          "OFF"                                                   // 主动下线（立即释放资源）
#define P2P_WS_ICE_CMD_SYNC0        "SYNC0 "        /* + <remote_peer_id>[\n<payload>] */   // 创建/恢复会话（可选预缓存负载）
#define P2P_WS_ICE_CMD_SYNC         "SYNC "         /* + <session_id>\n<payload> */         // 同步数据 (C2S & S2C)
#define P2P_WS_ICE_CMD_FIN          "FIN "          /* + <session_id> */                    // 会话结束 (C2S & S2C)

#define P2P_WS_ICE_RSP_REG_OK       "REG OK "       /* + <sync_max> <features> */           // 注册成功，sync_max=预缓存负载上限，features=功能位掩码
#define P2P_WS_ICE_RSP_REG_FAIL     "REG FAIL "     /* + <reason> */
#define P2P_WS_ICE_RSP_SYNC0        "SYNC0 "        /* + <peer_id> <session_id> online[\n<payload>]|offline|confirm|busy */
#define P2P_WS_ICE_RSP_SYNC0_FAIL   "SYNC0 FAIL "   /* + <reason> */
#define P2P_WS_ICE_RSP_SYNC         "SYNC "         /* + <session_id> confirm <bytes>|busy  (S2C 响应) */

/* SYNC payload 子类型前缀（应用层约定，服务器透传） */
#define P2P_WS_ICE_PAY_ICE          "ICE\n"         /* + <candidate_line> */
#define P2P_WS_ICE_PAY_ICE_DONE     "ICE_DONE"

/* WS_ICE 二进制帧类型（WebSocket binary frame, opcode=0x2）
 *
 * 帧格式: [type(1)][session_id(4)][payload(N)]
 *   - type: 见下方 P2P_WS_ICE_BIN_* 定义
 *   - session_id: uint32 网络字节序，路由键
 *   - payload: 类型相关数据（服务器仅重写 session_id，透传 payload）
 *
 * 与 RELAY 模式的区别:
 *   - 无需 relay_hdr.size（WebSocket 帧自带长度）
 *   - 无需 STATUS 应答（WebSocket 可靠传输，PACKET 无需 ACK/流控）
 *   - RPC 错误统一用伪造 RESP 返回（peer_offline / timeout）
 */
#define P2P_WS_ICE_BIN_PACKET      0x01    /* [type][ses_id][p2p_hdr(4)][data(N)] */        // 中继 P2P 数据包:
#define P2P_WS_ICE_BIN_REQ         0x02    /* [type][ses_id][sid(2)][msg(1)][data(N)] */    // RPC 请求
#define P2P_WS_ICE_BIN_RESP        0x03    /* [type][ses_id][sid(2)][code(1)][data(N)] */   // RPC 响应

#define P2P_WS_ICE_BIN_HDR_SIZE    (1u + P2P_SESS_ID_PSZ)                       /* type(1) + session_id(4) = 5 */
#define P2P_WS_ICE_BIN_PACKET_MIN  (P2P_WS_ICE_BIN_HDR_SIZE + P2P_HDR_SIZE)     /* 9: 最小 PACKET 帧 */
#define P2P_WS_ICE_BIN_REQ_MIN     (P2P_WS_ICE_BIN_HDR_SIZE + 3u)               /* 8: type+ses_id+sid+msg */
#define P2P_WS_ICE_BIN_RESP_MIN    (P2P_WS_ICE_BIN_HDR_SIZE + 3u)               /* 8: type+ses_id+sid+code */

/* ============================================================================
 * WS_ICE 协议详细定义说明
 * ============================================================================
 *
 * ────────────────────────────────────────────────────────────────────────────
 * REG — 身份注册（客户端 → 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "REG <peer_id> <instance_id>"
 *   - peer_id: 本端身份标识，UTF-8 字符串，最长 P2P_PEER_ID_MAX-1 字节
 *   - instance_id: 客户端实例 ID（uint32 十进制 ASCII），每次 online() 生成新随机数
 *                  用于服务器区分网络重连（保留会话）和客户端重启（销毁旧会话）
 *
 * 功能: 注册本端身份，建立 peer_id → WS 连接的映射。
 *       类似 RELAY P2P_RLY_ONLINE / COMPACT SIG_PKT_ONLINE。
 *
 * 服务端处理:
 *   1. peer_id 为空 / instance_id 无效 → 返回 "REG FAIL ..."
 *   2. peer_id + instance_id 均匹配（同一 cid）→ 幂等，返回 "REG OK <sync_max>"
 *   3. peer_id 匹配 + instance_id 相同（不同 cid，网络重连）:
 *      - 踢掉旧 WS 连接（ws_server_disconnect, code=1000）
 *      - 复用 ws_ice_client_t，更新 cid，保留所有会话
 *      - 遍历已配对会话，向所有在线对端推送 "SYNC0 <peer_id> <peer_session_id> online"
 *      - 向本端推送所有在线对端的 "SYNC0 <remote_peer_id> <session_id> online"
 *      - 返回 "REG OK <sync_max>"
 *   4. peer_id 匹配 + instance_id 不同（客户端重启）:
 *      - 销毁旧 client 及其所有会话（通知对端 FIN）
 *      - 创建新 ws_ice_client_t
 *      - 返回 "REG OK <sync_max>"
 *   5. 同一 cid 曾注册其他 peer_id → 清除旧 client 及其会话
 *   6. 全新注册 → 创建 ws_ice_client_t，返回 "REG OK <sync_max>"
 *
 * 响应:
 *   "REG OK <sync_max> <features>" — 注册成功
 *     - sync_max: SYNC0 预缓存负载字节上限（不含 NUL）
 *     - features: 服务器功能位掩码（十进制），与 RELAY 一致:
 *         0x01 = P2P_RLY_FEATURE_RELAY（支持数据包中继）
 *         0x02 = P2P_RLY_FEATURE_MSG（支持 MSG RPC 机制）
 *   "REG FAIL <reason>"       — 注册失败
 *     reason: "empty peer_id"      — peer_id 为空
 *             "invalid instance_id" — instance_id 缺失或为 0
 *             "OOM"                — 内存分配失败
 *
 * 示例:
 *   → "REG alice_device_01 3827401956"
 *   ← "REG OK 2048 3"
 *
 * ────────────────────────────────────────────────────────────────────────────
 * OFF — 主动下线（客户端 → 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "OFF"
 *
 * 功能: 主动注销身份并立即释放服务器资源（client + 所有会话）。
 *       与 REG 配对使用。相比直接断开 WS 连接，OFF 无需等待超时回收。
 *
 * 服务端处理:
 *   1. 已注册 → 调用 ws_ice_invalidate_client(do_free=true)
 *      - 遍历所有会话，通知在线对端 "FIN <peer_session_id>"
 *      - 释放所有会话和 client 结构
 *   2. 未注册 → 静默忽略
 *
 * 无响应（服务器不回复）。
 *
 * 示例:
 *   → "OFF
 *
 * ────────────────────────────────────────────────────────────────────────────
 * OFF — 主动下线（客户端 → 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "OFF"
 *
 * 功能: 主动注销身份并立即释放服务器资源（client + 所有会话）。
 *       与 REG 配对使用。相比直接断开 WS 连接，OFF 无需等待超时回收。
 *
 * 服务端处理:
 *   1. 已注册 → 调用 ws_ice_invalidate_client(do_free=true)
 *      - 遍历所有会话，通知在线对端 "FIN <peer_session_id>"
 *      - 释放所有会话和 client 结构
 *   2. 未注册 → 静默忽略
 *
 * 无响应（服务器不回复）。
 *
 * 示例:
 *   → "OFF"
 *
 * ────────────────────────────────────────────────────────────────────────────
 * SYNC0 — 创建/恢复会话（客户端 → 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "SYNC0 <remote_peer_id>"
 *       "SYNC0 <remote_peer_id>\n<payload>"   （可选携带预缓存负载）
 *   - remote_peer_id: 目标对端 peer_id
 *   - payload: 可选，预缓存的 ICE 候选数据（对端离线时服务器缓存，上线后转发）
 *     最大长度由 REG OK 返回的 sync_max 决定（不含 NUL）
 *
 * 功能: 创建与指定对端的信令会话，建立 session_pair_t 双向配对。
 *       类似 RELAY P2P_RLY_SYNC0 / COMPACT SIG_PKT_SYNC0。
 *       可选携带首批 ICE 候选作为预缓存负载（类似 RELAY SYNC0 的 peer_pending）。
 *
 * 前置条件: 发送方必须已 REG 注册，否则返回 "REG FAIL not registered"
 *
 * 服务端处理:
 *   1. remote_peer_id 为空 → 返回 "SYNC0 FAIL empty peer_id"
 *   2. 已有到该对端的活跃会话（session_pair 中已存在）:
 *      a. 对端标记已死亡（peer == -1）→ 清除标记，尝试重新配对
 *      b. 追加本端预缓存负载（超出 sync_max 则返回 busy）
 *      c. 对端在线 → 交换双方缓存 + 发送 confirm:
 *         - 向本端发送 "SYNC0 <remote_peer_id> <session_id> online\n<对端缓存>"
 *         - 向对端发送 "SYNC0 <local_peer_id> <peer_session_id> online\n<本端缓存>"
 *         - 向本端发送 "SYNC0 <remote_peer_id> <session_id> confirm <bytes>"
 *         - 向对端发送 "SYNC0 <local_peer_id> <peer_session_id> confirm <bytes>"
 *      d. 对端离线 → 返回 "SYNC0 <remote_peer_id> <session_id> offline"
 *      e. payload 超出缓存可用空间 → 追加返回 "SYNC0 ... busy"
 *   3. 无已有会话 → 调用 build_session() 创建:
 *      a. 创建 ws_ice_session_t，分配 session_id
 *      b. 查找 session_pair（双向 key），已有则配对
 *      c. 对端已创建会话且未死亡 → 双向配对（peer 指针互指）
 *      d. 追加本端 payload（超出 sync_max 则 busy）
 *      e. 若配对成功且对端在线 → 交换双方缓存 + 发送 confirm（同 2c）
 *      f. 否则返回 "SYNC0 <remote_peer_id> <session_id> offline"
 *   4. build_session 失败 → 返回 "SYNC0 FAIL internal"
 *
 * 响应 (S2C 与推送统一格式):
 *   "SYNC0 <remote_peer_id> <session_id> online"
 *     — 会话已建立，对端已注册且 WS 连接有效
 *     — session_id: uint32 十进制（由 generate_session_id() 分配）
 *
 *   "SYNC0 <remote_peer_id> <session_id> online\n<payload>"
 *     — 对端在线且对端有预缓存负载，\n 后为对端的缓存内容
 *
 *   "SYNC0 <remote_peer_id> <session_id> offline"
 *     — 会话已建立，对端未注册或 WS 已断开
 *     — 本端 payload 已缓存，会话保留在服务端，对端上线时自动转发
 *
 *   "SYNC0 <remote_peer_id> <session_id> confirm <confirmed_bytes>"
 *     — 预缓存负载已转发至对端，confirmed_bytes 为转发字节数（不含 NUL）
 *     — 客户端可据此计算剩余缓存空间: remaining = sync_max - (sent - confirmed)
 *
 *   "SYNC0 <remote_peer_id> <session_id> busy"
 *     — 本次 payload 超出服务器缓存可用空间（sync_max），未被接受
 *     — 客户端应等待 confirm（缓存空间释放）后重试
 *
 *   "SYNC0 FAIL <reason>"
 *     — 会话创建失败
 *     reason: "empty peer_id"   — remote_peer_id 为空
 *             "internal"        — build_session 内部错误（OOM / 重复创建）
 *             "not registered"  — 发送方未 REG（返回 "REG FAIL not registered"）
 *
 * 示例:
 *   → "SYNC0 bob_device_02"
 *   ← "SYNC0 bob_device_02 42 offline"
 *
 *   → "SYNC0 bob_device_02\nICE\na=candidate:1 1 udp ..."
 *   ← "SYNC0 bob_device_02 42 offline"              (负载已缓存)
 *
 *   (bob 随后也发 SYNC0)
 *   → "SYNC0 alice_device_01\nICE\na=candidate:2 1 udp ..."      (bob 发送)
 *   ← "SYNC0 alice_device_01 43 online\nICE\na=candidate:1 1 udp ..."  (bob 收到，携带 alice 的缓存)
 *   ← "SYNC0 alice_device_01 43 confirm 52"                            (bob 收到，缓存已转发)
 *   ← "SYNC0 bob_device_02 42 online\nICE\na=candidate:2 1 udp ..."    (alice 收到，携带 bob 的缓存)
 *   ← "SYNC0 bob_device_02 42 confirm 48"                              (alice 收到，缓存已转发)
 *
 *   → "SYNC0 bob_device_02\nICE\n..."       (alice 再次追加，缓存已满)
 *   ← "SYNC0 bob_device_02 42 busy"         (超出 sync_max)
 *
 * ────────────────────────────────────────────────────────────────────────────
 * SYNC0 推送 — 对端上线通知（服务器 → 客户端，S2C 方向）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "SYNC0 <peer_id> <session_id> online"
 *       "SYNC0 <peer_id> <session_id> online\n<payload>"   （携带对端预缓存负载）
 *   - peer_id: 对端 peer_id
 *   - session_id: 接收方自己的 session_id（uint32 十进制）
 *   - payload: 可选，对端此前 SYNC0 中缓存的负载（存在时紧跟 \n 后）
 *
 * 功能: 通知客户端某个已配对对端上线，应发起/重新发起 ICE 候选交换。
 *       与 RELAY 一致：SYNC0 在 C2S 方向为会话请求（无 session_id），
 *       在 S2C 方向为应答/推送（服务器插入 session_id）。
 *       应答与推送统一格式，客户端无需区分，统一处理即可。
 *       若对端此前 SYNC0 时携带了 payload 且尚未被消费，则一并转发。
 *
 * 触发时机（服务器主动推送，无需客户端请求）:
 *   1. 对端 ONLINE 注册/重连成功 → 遍历其所有已配对会话，
 *      向每个在线对端推送 "SYNC0 <reconnected_peer_id> <my_session_id> online[\n<cached>]"，
 *      同时向重连方推送所有在线对端的 "SYNC0 <peer_id> <my_session_id> online[\n<cached>]"
 *      若任一方有预缓存负载被转发，则向原缓存方追加发送 "SYNC0 ... confirm <bytes>"
 *   2. 本端 SYNC0 创建会话时对端已在线 → 双向交换缓存 + confirm
 *
 * 客户端处理:
 *   收到 "SYNC0 <peer_id> <session_id> online[\n<payload>]" 后应：
 *   1. 根据 session_id 查找/创建本地 session
 *   2. 若有 \n<payload>，先处理对端预缓存的 ICE 候选
 *   3. 通过 SYNC 发送 ICE 候选发起候选交换
 *   4. 若此前已有候选交换（重连场景），应重新收集并发送
 *
 * 示例:
 *   ← "SYNC0 bob_device_02 42 online"
 *   (客户端随后发起 ICE 候选交换)
 *   → "SYNC 42\nICE\na=candidate:..."
 *
 *   ← "SYNC0 bob_device_02 42 online\nICE\na=candidate:2 1 udp ..."
 *   (客户端先处理 bob 的预缓存候选，再发送自己的候选)
 *
 * ────────────────────────────────────────────────────────────────────────────
 * SYNC — 同步数据交换（双向：客户端 ↔ 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "SYNC <session_id>\n<payload>"
 *   - session_id: 发送方的 session_id（由 SYNC0 分配，uint32 十进制）
 *   - payload: 任意文本（可含换行符），服务器透传不解析
 *
 * 功能: 通过会话路由向对端同步 ICE 候选。
 *       类似 RELAY P2P_RLY_SYNC：服务器按 session_id 查找配对会话，
 *       将 session_id 重写为对端的 session_id 后原样投递。
 *       因为服务端自身维护了 session，不需要交换 SDP，只需交换 ICE 候选行。
 *       SYNC 与 SYNC0 共享 pending_sync 缓存空间（受 sync_max 限制）。
 *
 * 前置条件: 发送方必须已 ONLINE 注册，且已通过 SYNC0 建立会话
 *
 * 服务端处理:
 *   1. 发送方未注册 → 返回 "REG FAIL not registered"
 *   2. session_id 无效（查不到或不属于该 client）→ 静默丢弃
 *   3. 对端在线 → 立即转发:
 *      - 重写 session_id 为对端的 session_id
 *      - 构造 "SYNC <peer_session_id>\n<payload>" 发送给对端
 *      - 向发送方返回 "SYNC <session_id> confirm <bytes>"（bytes = payload 字节数）
 *   4. 对端离线 → 追加缓存到 pending_sync（与 SYNC0 共享缓存空间）:
 *      - 缓存成功 → 静默（对端上线后通过 SYNC0 online 转发 + confirm）
 *      - 缓存已满（超出 sync_max）→ 返回 "SYNC <session_id> busy"
 *
 * 响应 (S2C):
 *   "SYNC <session_id>\n<payload>"
 *     — 对端同步数据（session_id 已重写为接收方的 session_id）
 *
 *   "SYNC <session_id> confirm <confirmed_bytes>"
 *     — 同步数据已转发至对端，confirmed_bytes 为转发字节数（不含 NUL）
 *     — 客户端可据此计算剩余缓存空间: remaining = sync_max - (sent - confirmed)
 *
 *   "SYNC <session_id> busy"
 *     — 服务器同步缓存空间不足（超出 sync_max），payload 未被接受
 *     — 客户端应等待 confirm（缓存空间释放）后重试
 *
 * payload 子类型（应用层约定，服务器透传）:
 *
 *   "ICE\n<candidate_line>"
 *     ICE 候选行：单个候选（逐条上报，类似 RELAY SYNC trickle）。
 *     candidate_line 格式:
 *       a=candidate:<foundation> <component> <transport> <priority> <addr> <port> typ <type> [raddr <raddr> rport <rport>]
 *
 *   "ICE_DONE"
 *     候选收集完成通知（所有候选均已通过 ICE trickle 发送）。
 *
 * 示例（alice sid=42, bob sid=43）:
 *   → "SYNC 42\nICE\na=candidate:1 1 udp 2130706431 192.168.1.100 12345 typ host"
 *   ← "SYNC 42 confirm 67"
 *   (bob 收到 →) "SYNC 43\nICE\na=candidate:1 1 udp 2130706431 192.168.1.100 12345 typ host"
 *
 *   → "SYNC 42\nICE_DONE"
 *   ← "SYNC 42 confirm 8"
 *   (bob 收到 →) "SYNC 43\nICE_DONE"
 *
 *   → "SYNC 42\nICE\n..."    (bob 离线，缓存到 pending_sync)
 *   (无 confirm，对端上线后通过 SYNC0 online 转发并触发 SYNC0 confirm)
 *
 * ────────────────────────────────────────────────────────────────────────────
 * FIN — 会话结束（双向：C2S + S2C）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "FIN <session_id>"
 *   - session_id: 要结束的会话 session_id
 *
 * 功能: 通知对方某个会话结束。与 COMPACT SIG_PKT_FIN / RELAY P2P_RLY_FIN 对齐。
 *
 * C2S（客户端 → 服务器，主动断开会话）:
 *   1. 客户端发送 "FIN <session_id>" 主动断开指定会话
 *   2. 服务器释放该会话，并向对端转发 FIN（携带对端自己的 session_id）
 *
 * S2C（服务器 → 客户端，对端断连通知）:
 *   1. 对端 WS 连接断开（网络中断/主动关闭）
 *   2. 对端发送 FIN 主动断开（服务器转发）
 *   3. 对端会话被超时清理（ws_ice_free_session）
 *
 * 客户端处理:
 *   收到 FIN 后应：
 *   1. 根据 session_id 找到本地 session
 *   2. 标记对端已断开，暂停向其发送 SYNC
 *   3. 等待后续 SYNC0 推送再恢复交换
 *   4. 不需要销毁本地 session（服务端可能保留等待重连）
 *
 * 示例:
 *   → "FIN 42"                        (C2S: 客户端主动断开)
 *   ← "FIN 42"                        (S2C: 对端断连通知)
 *
 * ════════════════════════════════════════════════════════════════════════════
 * 二进制帧协议（WebSocket binary frame, opcode=0x2）
 * ════════════════════════════════════════════════════════════════════════════
 *
 * 以下消息使用 WebSocket 二进制帧传输，与上方文本帧信令共享同一 WS 连接。
 * 二进制帧用于 P2P 数据中继（打洞失败降级）和 MSG RPC（服务器中转请求-应答），
 * 对应 RELAY 模式的 P2P_RLY_PACKET / P2P_RLY_REQ / P2P_RLY_RESP。
 *
 * 公共帧格式: [type(1)][session_id(4)][payload(N)]
 *   - type: 消息类型（P2P_WS_ICE_BIN_*）
 *   - session_id: uint32 网络字节序，用于会话路由
 *   - payload: 类型相关数据
 *
 * 与 RELAY 的区别:
 *   - 无 relay_hdr.size — WebSocket 帧自带长度
 *   - 无 STATUS 应答 — WebSocket 可靠传输，PACKET 无需 ACK/流控
 *   - RPC 错误统一使用服务器生成的伪 RESP 返回
 *
 * ────────────────────────────────────────────────────────────────────────────
 * PACKET — P2P 数据包中继（双向：客户端 ↔ 服务器 ↔ 客户端）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 帧格式: [0x01][session_id(4)][p2p_hdr(4)][data(N)]
 *   - session_id: 发送方的 session_id（来自 SYNC0 建会应答）
 *   - p2p_hdr: P2P 协议头 [type(1)][flags(1)][seq(2)]
 *   - data: P2P 协议数据
 *
 * 功能: P2P 打洞失败时，通过服务器中继转发 P2P 数据包（降级方案）。
 *       对应 RELAY P2P_RLY_PACKET。
 *
 * 服务端处理:
 *   1. 查找 session_id 对应的会话
 *   2. 对端在线 → 重写 session_id 为对端的 session_id，原样转发
 *   3. 对端离线 → 静默丢弃（实时数据不缓存）
 *   服务器不解析内层 p2p_hdr，仅做路由转发。
 *
 * 最小帧长度: P2P_WS_ICE_BIN_PACKET_MIN = 9 字节
 *
 * ────────────────────────────────────────────────────────────────────────────
 * REQ — RPC 请求（双向：A → 服务器 → B）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 帧格式: [0x02][session_id(4)][sid(2)][msg(1)][data(N)]
 *   - session_id: 发送方的 session_id
 *   - sid: 序列号（非零，循环递增，用于请求-响应匹配）
 *   - msg: 消息类型（0=echo 自动回复，>0=应用自定义）
 *   - data: 请求数据（最大 P2P_MSG_DATA_MAX 字节）
 *
 * 功能: 通过服务器中转实现可靠的请求-应答机制。
 *       对应 RELAY P2P_RLY_REQ。
 *
 * 服务端处理:
 *   1. 对端离线 → 生成伪 RESP [0x03][ses_id][sid][code=0xFF]
 *   2. RPC 忙（前一个 REQ 尚未收到 RESP）→ 生成伪 RESP [code=0xFE]
 *   3. 正常 → 重写 session_id，转发给对端
 *      记录 rpc_pending_sid，等待 RESP 解锁
 *
 * 超时: 服务器每秒检查 RPC 待确认链表，
 *       超过 MSG_REQ_MAX_RETRY × MSG_RPC_RETRY_INTERVAL_MS 未收到 RESP
 *       → 生成伪 RESP [code=0xFE (P2P_MSG_ERR_TIMEOUT)]
 *
 * 最小帧长度: P2P_WS_ICE_BIN_REQ_MIN = 8 字节
 *
 * ────────────────────────────────────────────────────────────────────────────
 * RESP — RPC 响应（双向：B → 服务器 → A）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 帧格式: [0x03][session_id(4)][sid(2)][code(1)][data(N)]
 *   - session_id: 响应方的 session_id
 *   - sid: 对应请求的序列号
 *   - code: 响应码（0=成功，应用自定义；服务器错误见下）
 *   - data: 响应数据
 *
 * 功能: 将 RPC 响应通过服务器转发回请求方。
 *       对应 RELAY P2P_RLY_RESP。
 *
 * 服务端处理:
 *   1. 请求方离线 → 静默丢弃
 *   2. sid 与请求方 rpc_pending_sid 不匹配 → 静默丢弃
 *   3. 正常 → 重写 session_id 为请求方的 session_id，转发
 *      解锁请求方 rpc_pending_sid（RPC 生命周期完成）
 *
 * 服务器生成的错误 RESP（客户端 on_response 回调 len=-1）:
 *   code=0xFF (P2P_MSG_ERR_PEER_OFFLINE): 对端在等待响应期间离线或会话销毁
 *   code=0xFE (P2P_MSG_ERR_TIMEOUT):      服务器转发超时或 RPC 通道忙
 *
 * 最小帧长度: P2P_WS_ICE_BIN_RESP_MIN = 8 字节
 */

/* ────────────────────────────────────────────────────────────────────────────
 * 协议流程详解
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 1. 上线流程（建立 WebSocket 连接）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Client                        Server
 *   │                                │
 *   ├── WebSocket Connect ─────────►│  HTTP Upgrade → WS
 *   │                                │
 *   ├── "REG alice" ───────────────►│  创建 ws_ice_client_t
 *   │                                │  peer_id="alice", cid=N
 *   │◄── "REG OK <sync_max>" ───────┤
 *   │                                │
 *   [进入 ONLINE 状态]               │
 *   │                                │
 *   [WebSocket 自身的 PING/PONG]     │  (保活由 WS 协议层处理)
 *
 * 2. 会话建立（首方离线等待）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                          Bob (离线)
 *   │                                │                                │
 *   ├── "SYNC0 bob\nICE\n..." ────►│  build_session("alice","bob")  │
 *   │                                │  pair 创建，side=0             │
 *   │                                │  remote_s=NULL (bob 未注册)    │
 *   │                                │  缓存 alice 的 payload          │
 *   │◄── "SYNC0 bob 42 offline" ────┤                                │
 *   │                                │                                │
 *   [等待 SYNC0 bob 42 online]       │                                │
 *
 * 3. 会话配对（对端上线 + 双向通知）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                             Bob
 *   │                                │                                │
 *   │                                │◄── "REG bob" ─────────────────┤
 *   │                                │  创建 ws_ice_client_t          │
 *   │                                ├── "REG OK <sync_max>" ───────►│
 *   │                                │                                │
 *   │                                │◄── "SYNC0 alice\nICE\n..." ───┤
 *   │                                │  build_session("bob","alice")  │
 *   │                                │  pair 找到，side=1             │
 *   │                                │  remote_s=alice's session      │
 *   │                                │  → 双向配对 + 交换缓存         │
 *   │                                │                                │
 *   │◄─ "SYNC0 bob 42 online\n    ──┤  (推送给 alice，携带 bob 的缓存)
 *   │    ICE\n..."                    │                                │
 *   │                                ├─ "SYNC0 alice 43 online\n ──►│  (应答给 bob，携带 alice 的缓存)
 *   │                                │    ICE\n..."                   │
 *   │◄─ "SYNC0 bob 42 confirm 52" ──┤                                │  (alice 的缓存已转发)
 *   │                                ├─ "SYNC0 alice 43 confirm 48"►│  (bob 的缓存已转发)
 *   │                                │                                │
 *   [处理 bob 的缓存候选]            │ [处理 alice 的缓存候选]
 *
 * 4. 同步交换 ICE 候选（alice sid=42, bob sid=43）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                             Bob
 *   │                                │                                │
 *   ├──── "SYNC 42\nICE\n..." ─────►│  sid=42 → 配对 → bob sid=43  │
 *   │◄──── "SYNC 42 confirm 67" ────┤                                │
 *   │                                ├──── "SYNC 43\nICE\n..." ─────►│
 *   │                                │                                │
 *   │                                │◄──── "SYNC 43\nICE\n..." ─────┤
 *   │                                ├──── "SYNC 43 confirm 67" ────►│
 *   │◄──── "SYNC 42\nICE\n..." ─────┤ sid=43 → 配对 → alice sid=42 │
 *   │                                │                                │
 *   ├──── "SYNC 42\nICE_DONE" ─────►│                                │
 *   │◄──── "SYNC 42 confirm 8" ─────┤                                │
 *   │                                ├──── "SYNC 43\nICE_DONE" ─────►│
 *   │                                │                                │
 *   │                                │◄──── "SYNC 43\nICE_DONE" ─────┤
 *   │                                ├──── "SYNC 43 confirm 8" ─────►│
 *   │◄──── "SYNC 42\nICE_DONE" ─────┤                                │
 *   │                                │                                │
 *   │◄════════════════════════ P2P ICE 打洞 ═══════════════════════►│
 *
 * 5. 断线重连（会话保留 + 自动恢复）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                             Bob
 *   │                                │                                │
 *   │                                │  Bob WS 断开                   ╳
 *   │                                │  bob.cid = -1 (标记离线)       │
 *   │                                │  bob 会话保留                  │
 *   │◄── "FIN 42" ──────────────────┤                                │
 *   │                                │                                │
 *   [暂停向 bob 发送 SYNC]           │     ... 网络恢复 ...           │
 *   │                                │                                │
 *   │                                │◄── WebSocket Connect ─────────┤
 *   │                                │◄── "REG bob" ─────────────────┤
 *   │                                │  复用 ws_ice_client_t          │
 *   │                                │  更新 cid，保留会话            │
 *   │                                ├── "REG OK <sync_max>" ────────►│
 *   │                                │                                │
 *   │                                │  遍历 bob 的已配对会话         │
 *   │                                │  转发预缓存负载 + confirm      │
 *   │◄── "SYNC0 bob 42 online       ┤                                │
 *   │     [\n<bob 的缓存>]"          │                                │
 *   │                                ├── "SYNC0 alice 43 online ────►│
 *   │                                │    [\n<alice 的缓存>]"         │
 *   │◄── "SYNC0 bob 42 confirm N" ──┤  (alice 的缓存已转发，如有)    │
 *   │                                ├── "SYNC0 alice 43 confirm M"►│  (bob 的缓存已转发，如有)
 *   │                                │                                │
 *       [重新发起 ICE 候选交换]      │    [重新发起 ICE 候选交换]
 *   ├── "SYNC 42\nICE\n..." ─────►│  ...
 *
 * 6. 超时清理（离线过久，释放资源）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Server (每 CLEANUP_INTERVAL_S 秒执行一次):
 *   │
 *   ├── 遍历 g_ws_ice_clients
 *   │   └── 对每个离线 client (cid == -1):
 *   │       └── 若 now - last_active > WS_ICE_CLIENT_TIMEOUT_S (60s):
 *   │           ├── 释放所有会话 (ws_ice_free_session)
 *   │           │   └── 每个有配对的会话 → 通知对端 "FIN <peer_session_id>"
 *   │           │       并标记对端 peer 指针为 -1
 *   │           └── 移除 client (HASH_DELETE + free)
 */

#pragma pack(pop)
#pragma ide diagnostic pop
#pragma clang diagnostic pop
#endif /* P2PP_H */
