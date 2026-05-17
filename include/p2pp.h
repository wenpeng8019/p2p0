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
#define P2P_SESS_ID_SZ      (sizeof(uint32_t))

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

/* 安全的 P2P UDP 负载
 * + 一般各类网关 MTU 约为 1500 字节，扣除 IPv4|IPv6/UDP 头部（20|40+8=48 字节），剩余 1452 字节
 *   再保留一些给各种网络穿透（VPN）等协议封装的包头空间，1200 字节是一个比较安全的选择（RFC 7914 推荐值）
 */
#define P2P_MTU         (1200)
#define P2P_HDR_SIZE    4                           /* 包头大小 */
#define P2P_MAX_PAYLOAD (P2P_MTU - P2P_HDR_SIZE)    /* 1196 */
#define P2P_MSG_DATA_MAX  (P2P_MAX_PAYLOAD - 11)    /* MSG RPC data upper bound: relay path needs [session_id(P2P_SESS_ID_SZ)+sid(2)+msg(1)] */

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
 *                    |                                      |
 *   t=0              | PUNCH(seq=1) ─────────────────────→ |
 *                    | [记录 pending: seq=1, path=0, t=0]   |
 *
 *   t=10             |                   收到 PUNCH(seq=1)  |
 *                    |                 [rx_confirmed=true]  |
 *                    | ←──────────────── PUNCH_ACK(seq=1)  |
 *
 *   t=20             | 收到 PUNCH_ACK(seq=1)                |
 *                    | [匹配 pending: RTT = 20ms, path=0]   |
 *                    | [tx_confirmed=true]                  |
 *                    | [NAT_CONNECTED]                      |
 *
 *   t=500            | PUNCH(seq=2) ─────────────────────→ | (下一轮定时)
 *                    | [记录 pending: seq=2, path=0, t=500] |
 *
 *   t=510            |                    收到 PUNCH(seq=2) |
 *                    | ←──────────────── PUNCH_ACK(seq=2)  |
 *
 *   t=520            | 收到 PUNCH_ACK(seq=2)                |
 *                    | [匹配 pending: RTT = 20ms, path=0]   |
 *                    |                                      |
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
 *                    |                                     |
 *   t=0              | PUNCH(seq=1) ────────────────────→ |
 *
 *   t=10             |                   收到 PUNCH(seq=1) |
 *                    | ←──────────────────── REACH(seq=1) |
 *
 *   t=20             | 收到 REACH(seq=1)                   |
 *                    | [双向连通确认]                      |
 *                    | CONN(seq=100) ───────────────────→ |
 *
 *   t=30             |                  收到 CONN(seq=100) |
 *                    |                 [状态 → CONNECTED] |
 *                    | ←─────────────── CONN_ACK(seq=100) |
 *
 *   t=40             | 收到 CONN_ACK(seq=100)              |
 *                    | [停止 CONN 重传]                    |
 *                    | [状态 → CONNECTED]                 |
 *                    | DATA(seq=1) ─────────────────────→ | (开始数据传输)
 *                    |                                     |
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
 * 当 flags & P2P_FLAG_SESSION 时，所有包在 hdr(4) 之后前置 session_id(P2P_SESS_ID_SZ)，
 * 详见下方 P2P_FLAG_SESSION 说明。
 */
#define P2P_PKT_DATA            0x20        // 数据包
#define P2P_PKT_ACK             0x21        // 确认包
#define P2P_PKT_CRYPTO          0x22        // DTLS 加密包（握手/密文数据）

#define P2P_PKT_ACK_PSZ             6u                          // ack_seq(2) + sack(4)（无 session_id）
#define P2P_PKT_ACK_SESSION_PSZ     (P2P_SESS_ID_SZ + 6u)      // session_id(P2P_SESS_ID_SZ) + ack_seq(2) + sack(4)

/* 
 * P2P_FLAG_SESSION / SIG_FLAG_RELAY 说明：
 *
 *   P2P_FLAG_SESSION (0x01): 包头后携带 session_id(P2P_SESS_ID_SZ)
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
 *     PUNCH:    [hdr(4)][session_id(P2P_SESS_ID_SZ)][target_addr(6)]
 *     DATA:     [hdr(4)][session_id(P2P_SESS_ID_SZ)][data(N)]
 *     ACK:      [hdr(4)][session_id(P2P_SESS_ID_SZ)][ack_seq(2)][sack(4)]
 *     CRYPTO:   [hdr(4)][session_id(P2P_SESS_ID_SZ)][crypto_data(N)]
 *     REACH:    [hdr(4)][session_id(P2P_SESS_ID_SZ)][target_addr(6)]
 *     CONN:     [hdr(4)][session_id(P2P_SESS_ID_SZ)]
 *     CONN_ACK: [hdr(4)][session_id(P2P_SESS_ID_SZ)]
 *     FIN:      [hdr(4)][session_id(P2P_SESS_ID_SZ)]
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
#define SIG_PKT_REG             0x80        // 上线（登录）到信令服务器
#define SIG_PKT_REG_ACK         0x81        // 上线确认（告知 auth_key、本端缓存能力、公网地址、探测端口、中继支持）
#define SIG_PKT_OFF             0x82        // 主动注销：客户端关闭时通知服务器立即释放配对槽位
                                            // 【服务端可选实现】服务端不处理此包时，自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制
#define SIG_PKT_ALV             0x83        // 保活包（可选，客户端定期发送以维持注册状态）
#define SIG_PKT_ALV_ACK         0x84        // 保活确认（服务器回复以确认注册状态）

#define SIG_PKT_SYN0            0x85        // 首次候选同步（双向）：client→server 提交首批候选；server→client 下发对端候选（详见 COMPACT 模式协议详细说明）
#define SIG_PKT_SYN0_ACK        0x86        // 首批候选确认（server→client）：[session_id(P2P_SESS_ID_SZ)][online(1)]，session_id = 对端配对会话 ID
#define SIG_PKT_SYNC            0x87        // 候选列表同步包（序列化传输）
#define SIG_PKT_SYNC_ACK        0x88        // 候选列表确认（确认指定序列号）
#define SIG_PKT_FIN             0x89        // 对端已离线/断开

/* SYNC 标志位（p2p_packet_hdr_t.flags） */
#define SIG_SYNC_FLAG_FIN           0x01    // 候选列表发送完毕

/* RPC 包类型（服务器可选实现，详见协议详细说明节） */
#define SIG_PKT_REQ             0x90        // RPC 请求：A→Server；Server→B relay（flags=SIG_FLAG_RELAY）
#define SIG_PKT_REQ_ACK         0x91        // RPC 请求确认：Server→A（已缓存并开始中转，或失败状态）
#define SIG_PKT_RSP             0x92        // RPC 应答：B→Server；Server→A relay
#define SIG_PKT_RSP_ACK         0x93        // RPC 应答确认：Server→B；A→Server

/* NAT 探测（服务器可选实现） */
#define SIG_PKT_NAT             0xA0        // NAT 类型探测请求（发往探测端口）
#define SIG_PKT_NAT_ACK         0xA1        // NAT 类型探测响应（返回第二次映射地址）

/* REG_ACK 标志位（p2p_packet_hdr_t.flags） */
#define SIG_REG_FLAG_RELAY          0x01    // 服务器支持数据中继功能（P2P 打洞失败降级）
#define SIG_REG_FLAG_MSG            0x02    // 服务器支持 MSG RPC 机制（可可靠中转请求-应答）

/* RPC 包标志位（p2p_packet_hdr_t.flags） */
/* SIG_FLAG_RELAY (0x02) 复用为 REQ/RSP relay 标志：标识此包是 Server→B/A 的中转包 */

/* RSP 包标志位 - 用于标识服务器特殊错误（而非对端返回的正常响应） */
#define SIG_RPC_FLAG_PEER_OFF       0x02    // B端在 REQ_ACK 之后离线（等待响应期间离线）
#define SIG_RPC_FLAG_TIMEOUT        0x04    // 服务器向B端转发请求超时

#define SIG_AUTH_KEY_PSZ            (sizeof(uint64_t))          // auth_key 大小（8 字节）

/* ============================================================================
 * COMPACT 模式协议详细说明
 * ============================================================================
 *
 * REG:
 *   payload: [local_peer_id(32)][instance_id(4)]
 *   包头: type=0x80, flags=0, seq=0
 *   - REG 仅建立客户端与服务器的关系，不携带 remote_peer_id 和候选地址
 *   - instance_id: 本次 connect() 的实例 ID（网络字节序，32位，必须非 0）
 *   - 语义:
 *       * instance_id 相同: 视为 REG 重传（例如客户端未收到 REG_ACK）
 *       * instance_id 不同: 视为同一 local_peer_id 的新实例（客户端重启/重连），服务端重置旧状态
 *   总大小: 4(包头) + 36(payload) = 40 字节
 */
 #define SIG_PKT_REG_PSZ            (P2P_PEER_ID_MAX + sizeof(uint32_t))                               // peer_id(32) + instance_id(4)
/* REG_ACK:
 *   payload: [instance_id(4)][auth_key(SIG_AUTH_KEY_PSZ)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
 *   包头: type=0x81, flags=见下, seq=0
 *   - auth_key: 客户端-服务器认证令牌（network byte order, 64-bit），用于后续 SYN0 和 ALV 包的身份验证
 *     · auth_key=0 表示服务器拒绝登录（无可用槽位），客户端应停止重试
 *     · 与 session_id（对端配对会话 ID）语义不同：auth_key 标识 client↔server 关系，session_id 标识 client↔peer 关系
 *   - instance_id: 回显客户端 REG 中的 instance_id（网络字节序，32位）
 *     客户端收到后应比较 instance_id 与当前实例是否一致，不一致则丢弃此 ACK
 *   - max_candidates: 服务器为该对端缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器主端口观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: 包头的 flags 字段可设置：
 *       SIG_ONACK_FLAG_RELAY (0x01) 表示服务器支持中继
 *       SIG_ONACK_FLAG_MSG (0x02) 表示服务器支持 MSG RPC 机制
 *   总大小: 4(包头) + 21(payload) = 25 字节
 */
 #define SIG_PKT_REG_ACK_PSZ        (sizeof(uint32_t) + SIG_AUTH_KEY_PSZ + 1u + 4u + 2u + 2u)          // instance_id(4) + auth_key(SIG_AUTH_KEY_PSZ) + max_cands(1) + ip(4) + port(2) + probe(2)
/* OFF:
 *   payload: [auth_key(SIG_AUTH_KEY_PSZ)]
 *   包头: type=0x82, flags=0, seq=0
 *   - auth_key: 来自 REG_ACK 的客户端-服务器认证令牌（network byte order），服务器用于 O(1) 查找并释放配对槽位
 *   客户端主动断开时发送，请求服务器立即释放配对槽位
 *   服务器收到后会向对端发送 FIN 通知
 */
 #define SIG_PKT_OFF_PSZ            SIG_AUTH_KEY_PSZ                                                   // auth_key(SIG_AUTH_KEY_PSZ)
/* ALV:
 *   payload: [auth_key(SIG_AUTH_KEY_PSZ)]
 *   包头: type=0x83, flags=0, seq=0
 *   - auth_key: 客户端-服务器认证令牌（来自 REG_ACK），用于服务器识别并更新槽位活跃时间
 *   用于客户端在 REG/READY 状态定期发送，保持服务器槽位活跃
 */
 #define SIG_PKT_ALV_PSZ            SIG_AUTH_KEY_PSZ                                                   // auth_key(SIG_AUTH_KEY_PSZ)
/* AALV_ACK:
 *   payload: 空（仅包头）
 *   包头: type=0x84, flags=0, seq=0
 *   服务器回复确认，表示槽位仍然有效
 */
 #define SIG_PKT_ALV_ACK_PSZ       0u                                                                 // 无 payload
/* SYN0（双向首次 sync，两方向 payload 格式不同，由各端角色区分处理）:
 *
 * 方向 1: client → server（建立和对端连接，并提交首批同步候选）
 *   payload: [auth_key(SIG_AUTH_KEY_PSZ)][remote_peer_id(P2P_PEER_ID_MAX)][candidate_count(1)][candidates(N*23)]
 *   - auth_key: 来自 REG_ACK 的客户端-服务器认证令牌（network byte order）
 *   - remote_peer_id: 目标对端 ID（32字节，不足补零）
 *   - candidate_count: 首批候选数量（最多 max_candidates 个）
 *   - candidates: 首批候选地址列表（每 23 字节，p2p_candidate_t 格式）
 *   客户端在收到 REG_ACK 后立即发送，同时完成：
 *     1. 提交首批候选供服务器缓存
 *     2. 指定 remote_peer_id，建立与对端的配对关系
 *
 * 方向 2: server → client（对端已配对后触发，下发对端候选地址）
 *   payload: [remote_peer_id(P2P_PEER_ID_MAX)][session_id(P2P_SESS_ID_SZ)][0x00(1)][candidate_count(1)][candidates(N*23)]
 *   - remote_peer_id: 对端 ID（32字节，不足补零），用于客户端多会话派发定位目标 session
 *   - session_id: 对端配对会话 ID（network byte order，由服务器在配对成功时分配）
 *   - 0x00: 保留字节（固定为 0，供 unpack_remote_candidates 识别为初始推送）
 *   - candidate_count: 对端候选数量（首个必须是服务器观察到的对端公网地址 srflx）
 *   - candidates: 对端候选地址列表
 *   客户端收到后以 SIG_PKT_SYN0_ACK（client→server 方向）确认
 */
#define SIG_PKT_SYN0_PSZ(n)         (SIG_AUTH_KEY_PSZ + P2P_PEER_ID_MAX + 1u + (n)*sizeof(p2p_candidate_t)) // client→server: auth_key(SIG_AUTH_KEY_PSZ) + peer_id(32) + count(1) + cands(n*23)
#define SIG_PKT_SYN0_S2C_PSZ(n)     (P2P_PEER_ID_MAX + P2P_SESS_ID_SZ + 2u + (n)*sizeof(p2p_candidate_t)) // server→client: remote_peer_id(32) + session_id(P2P_SESS_ID_SZ) + reserved(1) + count(1) + cands(n*23)
/* SYN0_ACK（双向，两端 payload 格式不同）:
 *
 * 方向 1: server → client（对 client SYN0 的回复）
 *   payload: [remote_peer_id(P2P_PEER_ID_MAX)][session_id(P2P_SESS_ID_SZ)][online(1)]
 *   - remote_peer_id: 对端 ID（32字节，不足补零），用于客户端多会话派发定位目标 session
 *   - session_id: 对端配对会话 ID（network byte order, 64-bit），标识 client↔peer 会话
 *     · 语义不同于 auth_key（auth_key 标识 client↔server）
 *     · 用于后续所有 SYNC/SYNC_ACK/FIN/DATA relay/MSG 包的身份验证
 *   - online: 1=对端已上线（已有对端配对），0=对端尚未上线
 *   服务器收到 client SYN0 后回复，通知客户端候选已缓存以及对端是否已上线
 *
 * 方向 2: client → server（对 server SYN0_ACK 的二次回复）
 *   payload: [session_id(P2P_SESS_ID_SZ)]
 *   客户端收到 server 的 SYN0_ACK 后，再次回复 SYN0_ACK。
 *   而服务器在收到二次确认前，会确保不会将对端的 SYN0 包提前转发过来，以确保 session id 的先后一致性。
 *
 * 方向 3: client → server（对 server SYN0 的回复）
 *   payload: [session_id(P2P_SESS_ID_SZ)]
 *   - session_id: 来自 server SYN0 中的会话 ID，用于服务器确认对应配对
 *   客户端收到 server SYN0（首次对端候选推送）后立即回复
 */
#define SIG_PKT_SYN0_ACK_PSZ        (P2P_PEER_ID_MAX + P2P_SESS_ID_SZ + 1u)                            // server→client: remote_peer_id(32) + session_id(P2P_SESS_ID_SZ) + online(1)
#define SIG_PKT_SYN0_ACK_C2S_PSZ    (P2P_SESS_ID_SZ)                                                   // client→server: session_id(P2P_SESS_ID_SZ)
/* SYNC:
 *   payload: [session_id(P2P_SESS_ID_SZ)][notify_seq_or_base(1)][candidate_count(1)][candidates(N*23)]
 *   包头: type=0x87, flags=见下, seq=序列号
 *   - session_id: 会话 ID（网络字节序，64位，来自 SYN0_ACK）
 *   - 字段[P2P_SESS_ID_SZ]: 语义随 seq 分两种（内容不同，位置相同）
 *       seq=0: 循环通知序号 notify_seq（1..255 循环），接收端据此排重
 *       seq>0: 候选起始索引 base_index（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（配合 FIN 标志）
 *   - seq=0: 服务器发送，对端公网地址变更通知（candidate_count 必须为 1）
 *       * 客户端收到后以 SYNC_ACK(seq=0) 确认
 *       * 接收端按 notify_seq 循环序比较新旧，旧通知可忽略但仍需 ACK
 *       * 服务器首次对端候选推送使用独立 opcode SIG_PKT_SYN0（server→client 方向）
 *   - seq>0: 客户端发送，base_index 递增，继续同步剩余候选
 *   - flags: 包头的 flags 字段可设置 SIG_SYNC_FLAG_FIN (0x01) 表示候选列表发送完毕
 *   - seq 窗口: 0..16（1..16 为客户端候选批次，0 为地址变更通知）
 *   - 乱序处理: 允许 seq>0 先于 SYN0 到达；接收端按序号位图去重，重复包仅 ACK 不重复入表
 */
#define SIG_PKT_SYNC_PSZ(n)         (P2P_SESS_ID_SZ + 2u + (n)*sizeof(p2p_candidate_t))                // session_id(P2P_SESS_ID_SZ) + base(1) + count(1) + cands(n*23)
/* SYNC_ACK:
 *   payload: [session_id(P2P_SESS_ID_SZ)]
 *   包头: type=0x88, flags=0, seq=确认的序列号
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - seq=0: 客户端→服务器，确认地址变更通知（SYNC seq=0）
 *       注：服务器 SYN0（server→client 首次候逳推送）由 SIG_PKT_SYN0_ACK 确认，不使用此类型
 *   - seq>0: 服务器→客户端，确认客户端发送的 SYNC(seq>0) 候选批次；或客户端→服务器 relay 转发
 *   - seq 窗口: 0..16
 */
#define SIG_PKT_SYNC_ACK_PSZ        (P2P_SESS_ID_SZ)                                                   // session_id(P2P_SESS_ID_SZ)
/* FIN:
 *   payload: [session_id(P2P_SESS_ID_SZ)]
 *   包头: type=0x89, flags=0, seq=0
 *   服务器下行通知：对端已离线/断开连接
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 *   客户端收到此包后应停止该会话的所有传输和重传
 */
#define SIG_PKT_FIN_PSZ             (P2P_SESS_ID_SZ)                                                   // session_id(P2P_SESS_ID_SZ)

/* REQ (A → Server):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x90, flags=0, seq=0
 *   - session_id: A 的会话 ID（来自 SYN0_ACK）
 *   - sid: A 生成的 16 位序列号（每次 connect() 范围内唯一，用于匹配应答）
 *   - msg: 应用层消息 ID（协议层透传，由应用自定义）
 *   - A 重发此包直到收到 REQ_ACK
 *
 * REQ (Server → B, relay):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x90, flags=SIG_MSG_FLAG_RELAY(0x01), seq=0
 *   - session_id: A 的会话 ID（B 用此字段构造 RSP）
 *   - Server 重发此包直到收到 RSP
 */
#define SIG_PKT_REQ_MIN_PSZ         (P2P_SESS_ID_SZ + 3u)                                               // session_id(P2P_SESS_ID_SZ) + sid(2) + msg(1)
/* REQ_ACK (Server → A):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][status(1)]
 *   包头: type=0x91, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 A 端验证响应合法性）
 *   - sid: 对应的 REQ 序列号
 *   - status: 0=已缓存并开始向 B 中转；1=目标 B 不在线
 *   - A 收到此包后停止重发
 */
#define SIG_PKT_REQ_ACK_PSZ         (P2P_SESS_ID_SZ + 3u)                                               // session_id(P2P_SESS_ID_SZ) + sid(2) + status(1)
/* RSP (B → Server):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][data(N)]
 *   包头: type=0x92, flags=0, seq=0
 *   - session_id: 从 REQ relay 中取得的 A 的会话 ID
 *   - B 重发此包直到收到 Server → B 的 RSP_ACK
 * 
 * RSP (Server → A, relay):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][data(N)]
 *   包头: type=0x92, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 A 端验证响应合法性）
 *   - sid: 对应的 REQ 序列号
 *   - code: 响应码
 *   - data: 响应数据
 *   - Server 重发此包直到收到 A → Server 的 RSP_ACK
 */
#define SIG_PKT_RSP_MIN_PSZ         (P2P_SESS_ID_SZ + 3u)                                           // session_id(P2P_SESS_ID_SZ) + sid(2) + code(1)
/* RSP_ACK (Server → B):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)]
 *   包头: type=0x93, flags=0, seq=0
 *   - session_id: B 的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 对应的 REQ 序列号
 *   - Server 确认收到 B 的 RSP，B 停止重发
 *
 * RSP_ACK (A → Server):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)]
 *   包头: type=0x93, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 对应的 REQ 序列号
 *   - A 收到 Server 转发的 RSP 后发送，流程完成
 */
#define SIG_PKT_RSP_ACK_PSZ         (P2P_SESS_ID_SZ + 2u)                                               // session_id(P2P_SESS_ID_SZ) + sid(2)
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
#define SIG_PKT_NAT_ACK_PSZ         6u                                                                  // probe_ip(4) + probe_port(2)
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
 *      - 客户端发送 REG（含 local_peer_id 与 instance_id）
 *      - 服务器回复 REG_ACK（告知 auth_key、max_candidates、公网地址）
 *        · auth_key=0: 服务器拒绝登录（无可用槽位），客户端停止重试
 *        · auth_key≠0: 登录成功，用于后续 SYN0/ALV 身份验证
 *      - 收到 REG_ACK 后停止 REG 重发，进入 REG 状态
 *
 *   2. 候选同步阶段（三次握手 + 序列化确认）：
 *      - 客户端收到 REG_ACK 后立即发送 SYN0（含 auth_key + remote_peer_id + 首批候选）
 *      - 服务器回复 SYN0_ACK（含 session_id + online），online=1 表示对端已上线
 *      - 客户端收到 SYN0_ACK 后，回复 SYN0_ACK（二次确认，含 session_id）
 *      - 服务器在收到二次确认前，不会将对端的 SYN0 转发过来（确保 session_id 先建立）
 *      - 双方上线且均已二次确认后，服务器向双方发送 SYN0，包含缓存的对端候选
 *      - 客户端收到 SYN0 后发送 SYN0_ACK（携带 session_id）确认
 *      - 客户端通过 SYNC(seq=1,2,3,...) 继续同步剩余候选（携带 session_id）
 *      - 对端通过 SYNC_ACK 确认，未确认则重发
 *      - 允许乱序：seq>0 可能先于 seq=0 到达；接收端按序号位图去重，重复包仅 ACK 不重复入表
 *
 *   3. 离线缓存流程：
 *
 *      Alice (在线)           Server                    Bob (离线)
 *        |                       |                          |
 *        |--- REG -------------->|                          |
 *        |<-- REG_ACK -----------|  (auth_key + capabilities)
 *        |--- SYN0 ------------->|  (auth_key + 首批候选)   |
 *        |<-- SYN0_ACK ----------|  (session_id, online=0)  |
 *        |--- SYN0_ACK --------->|  (二次确认, session_id)  |
 *        |   [进入 REG]          |  (缓存 Alice 的候选)     |
 *        |    ... Bob 上线 ...                              |
 *        |                       |<-- REG ------------------|
 *        |                       |--- REG_ACK ------------->|  (auth_key + capabilities)
 *        |                       |<-- SYN0 -----------------|  (auth_key + 首批候选)
 *        |                       |--- SYN0_ACK ------------>|  (session_id, online=1)
 *        |                       |<-- SYN0_ACK -------------|  (二次确认, session_id)
 *        |<-- SYN0 --------------|--- SYN0 ---------------->|  (缓存候选 + session_id)
 *        |--- SYN0_ACK --------->|<-- SYN0_ACK -------------|  (携带 session_id)
 *        |                       |                          |
 *        |<=============== P2P SYNC 序列化同步 ============>|  (所有包携带 session_id)
 *        |----------- SYNC(seq=1, base=5) ----------------->|  (从第 6 个候选开始)
 *        |<---------- SYNC_ACK(seq=1) ----------------------|
 *        |----------- SYNC(seq=2, base=10) ---------------->|
 *        |<---------- SYNC_ACK(seq=2) ----------------------|
 *        |----------- SYNC(seq=3, count=0, FIN) ----------->|  (结束标识)
 *        |<---------- SYNC_ACK(seq=3) ----------------------|
 *
 * 注：REG 仅在上线阶段发送，收到 REG_ACK 后停止（直到重连）；SYN0 在收到 REG_ACK 后发送
 *
 * 2. MSG RPC 机制（服务器可选实现）
 * ============================================================================
 *
 * 通过服务器中转，实现对端间可信赖的一次请求-应答交互（类似 RPC）。
 * 服务器是否支持由 REG_ACK.flags 中 SIG_ONACK_FLAG_MSG (0x02) 位标识。
 *
 * msg 特殊值（请求消息类型）：
 *   - msg=0: Echo 测试，B端自动回复相同数据，无需应用层介入
 *   - msg>0: 应用层自定义消息类型，需要应用层在 on_request 回调中处理并调用 p2p_response()
 *
 * 错误处理：
 *   1. REQ_ACK 阶段 B 不在线：Server → A 返回 status=1，A 调用 on_response(len=-1, code=原始请求msg)
 *   2. B 在等待响应期间离线：Server → A 发送 RSP(flags=SIG_MSG_FLAG_PEER_OFF)，
 *      A 调用 on_response(len=-1, code=P2P_RPC_ERR_PEER_OFF)
 *   3. Server 转发请求超时：Server → A 发送 RSP(flags=SIG_MSG_FLAG_TIMEOUT)，
 *      A 调用 on_response(len=-1, code=P2P_RPC_ERR_TIMEOUT)
 *
 * 流程（7 次传输）：
 *
 *   A                      Server                            B
 *   │                         │                              │
 *   ├── REQ(sid) ───────────►│  A 重发直到收到 REQ_ACK      │
 *   │   [session_id][sid]     │  Server 查找 A 的会话槽      │
 *   │   [msg][data]           │  msg=消息类型                │
 *   │                         │                              │
 *   │◄── REQ_ACK ────────────┤  status=0 成功/1 B不在线     │
 *   │   [session_id][sid]     │  A 停止重发                  │
 *   │   [status]              │                              │
 *   │                         ├────── REQ ─────────────────►│  Server 重发直到收到 RSP
 *   │                         │   [session_id][sid]          │  (relay 标志位=1)
 *   │                         │   [msg][data]                │  B 循环比较 sid：
 *   │                         │                              │  sid>last_sid → 执行新请求
 *   │                         │                              │  sid≤last_sid → 忽略旧请求
 *   │                         │◄──── RSP ───────────────────┤  B 定时重发直到收到 ACK
 *   │                         │   [session_id][sid]          │  Server 收到后停止向 B 发 REQ
 *   │                         │   [code][data]               │  code=响应码
 *   │                         │                              │
 *   │                         ├────── RSP_ACK ─────────────►│  Server 每次收到 RSP 都回 ACK
 *   │                         │   [session_id][sid]          │  B 收到 ACK 后停止重发
 *   │                         │                              │
 *   │◄── RSP ────────────────┤  Server 转发第一次 RSP 给 A  │
 *   │   [session_id][sid]     │  后续重发直到收到 A 的 ACK   │
 *   │   [code][data]          │  (flags 可能标识特殊错误)    │
 *   │                         │                              │
 *   ├── RSP_ACK ────────────►│  流程完成                    │
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
    P2P_RLY_STA = 0,                        // 状态包（仅服务器发送，包含请求类型 + 状态码）

    /* 在线管理 */
    P2P_RLY_REG,                            // 上线: Client -> Server / Server -> Client（双向，上下行 payload 不同）
    P2P_RLY_OFF,                            //
    P2P_RLY_ALV,                            // 心跳: Client -> Server / Server -> Client（双向，空 payload）

    /* 会话同步 */
    P2P_RLY_SYN0,                           // 首次同步: 三态 (Client -> Server 发起创建会话; Server -> Client 会话创建应答; Server -> Client 转发对端 SYN0 请求)
    P2P_RLY_SYNC,                           // 后续同步: 双向对等、四态 (Client -> Server 上传; Server -> Client 应答确认; Server -> Client 下发; Server -> Client 应答确认)
    P2P_RLY_FIN,                            // 会话结束: Client -> Server / Server -> Client

    /* P2P 数据中继（打洞失败降级） */
    P2P_RLY_PKT,                            // 中继 P2P 数据包: Client <-> Server <-> Client (内层 P2P hdr 区分类型)

    /* 消息 RPC（服务器中转的请求-应答机制） */
    P2P_RLY_REQ,                            // 请求: Client -> Server / Server -> Client (双向)
    P2P_RLY_RSP,                            // 响应: Client -> Server / Server -> Client (双向)
} p2p_relay_type_t;

/* RELAY 模式包头 (3 bytes) */
typedef struct {
    uint8_t             type;
    uint16_t            size;
} p2p_relay_hdr_t;

#define P2P_RLY_CODE_READY          0                   // 请求操作成功，服务器就绪，客户端可继续后续操作
                                                        // + 作为 TCP 协议，都是基于 REQ/RSP 的请求应答模式，也就是每个请求，都会对应一个应答码
                                                        //   READY 表示请求成功。此外，它也代表客户端可以继续请求后续操作，
                                                        //   对应的，如果服务器想让客户端延迟后续操作，可以通过延迟返回该状态码来实现。
#define P2P_RLY_ERR(c)              (0x80+(uint8_t)(c)) // 错误码基数，code >= 0x80 表示错误
#define P2P_RLY_ERR_DISCONNECTED    P2P_RLY_ERR(0)      // 网络 I/O 错误（连接异常/读写失败）
#define P2P_RLY_ERR_IO              P2P_RLY_ERR(1)      // 网络 I/O 错误（连接异常/读写失败）
#define P2P_RLY_ERR_OVERFLOW        P2P_RLY_ERR(2)      // 请求协议包数据过大（size 超出限制）
#define P2P_RLY_ERR_INTERNAL        P2P_RLY_ERR(3)      // 服务器内部错误。此时应该断开和服务器的连接，等待重连恢复
#define P2P_RLY_ERR_PROTOCOL        P2P_RLY_ERR(4)      // 协议错误（未登录/非法状态）
#define P2P_RLY_ERR_PEER_OFF        P2P_RLY_ERR(5)      // 对端未完成 REG（未登录）或已 OFF（离线）
#define P2P_RLY_ERR_UNREACHABLE     P2P_RLY_ERR(6)      // 对方暂时不可达（对方已经 REG 但可能网络闪断或异常）
#define P2P_RLY_ERR_INVALID         P2P_RLY_ERR(7)      // 无效的参数或操作
#define P2P_RLY_ERR_TIMEOUT         P2P_RLY_ERR(8)      // 服务器转发请求超时
#define P2P_RLY_ERR_BUSY            P2P_RLY_ERR(9)      // 会话忙（前一个转发尚未完成）

/* RELAY REG 下行功能标志 */
#define P2P_RLY_FEATURE_RELAY       0x01    // 支持数据包中继
#define P2P_RLY_FEATURE_MSG         0x02    // 支持 MSG RPC 机制
#define P2P_RLY_SYNC_FIN_MARKER     0xFF    // SYNC 负载尾部 FIN 标记字节

/* ============================================================================
 * RELAY 模式协议详细定义说明
 * ============================================================================
 *
 * 三种服务类型的数据完整性维护策略：
 *
 *   SYNC（会话候选同步，SYN0 + SYNC + FIN）：
 *     服务器提供可靠的双向同步中转缓存服务。客户端通过请求/应答方式与服务器
 *     逐步维护同步数据的完整性（每包均有确认，未确认的包服务器持续缓存）。
 *     服务器在整个会话期间保持并维护已同步缓存的完整性，断网重连后自动重发
 *     未确认的数据，确保数据交互不丢失。
 *
 *   PKT（P2P 数据中继，PKT）：
 *     服务器提供单纯的数据中继服务，不做任何可靠性保证。应答确认、超时重发
 *     等可靠性机制完全由双方客户端自行实现；服务器仅负责将收到的数据包原样
 *     转发给对端，不缓存、不重传。
 *
 *   RPC（消息请求-应答，REQ/RSP）：
 *     服务器提供实时数据交互中转服务，要求双方必须同时在线且可达，否则服务器
 *     立即回复失败（ERR_PEER_OFF / ERR_UNREACHABLE）或超时（ERR_TIMEOUT）。
 *     网络中断时，服务器会自动中止并清除正在执行的请求，向发起方回复错误。
 *
 * 所有消息：[p2p_relay_hdr_t: 3B][payload: N bytes]
 *
 * P2P_RLY_STA:
 *   payload: [type(1)][status_code(1)][[session_id(P2P_SESS_ID_SZ)]|remote_peer_id(P2P_PEER_ID_MAX)][status_msg(N)]
 *   - type: 请求的 p2p_relay_type_t 类型（例如 P2P_RLY_SYN0），用于指示哪个请求出错
 *   - status_code: 见 P2P_RLY_CODE_* 定义
 *   - session_id: 会话 ID，对于会话相关的请求（如 SYNC）存在时携带，用于客户端识别对应会话；对于非会话请求（如 REG）则不携带
 *                 注意：P2P_RLY_SYN0 请求尚未建立会话，因此返回的 STATUS 不携带 session_id，但会携带 remote_peer_id 以指示哪个对端的连接请求出错
 *   - status_msg: 可选的状态描述文本（UTF-8 编码）
 */
#define P2P_RLY_STA_PSZ(s, n)        (2 + ((s)==2 ? P2P_SESS_ID_SZ : ((s) == 1 ? P2P_PEER_ID_MAX : 0)) + (n)) // type(1) + status_code(1) + route_key + status_msg(N)
/* P2P_RLY_REG（双向，上下行负载格式不同）:
 *
 *   上行 Client -> Server:
 *     payload: [name(32)][instance_id(4)]
 *     - name: 本地 peer 名称，定长 32 字节，0 填充
 *     - instance_id: 客户端每次 register() 生成的 32 位随机数（网络字节序）
 *
 *   下行 Server -> Client:
 *     payload: [features(1)][candidate_sync_max(1)]
 *     - features: 0x01=RELAY, 0x02=MSG
 *     - candidate_sync_max: 单包最大候选数（0=客户端用默认）
*/
#define P2P_RLY_REG_PSZ                 (P2P_PEER_ID_MAX + sizeof(uint32_t))
#define P2P_RLY_REG_S2C_PSZ             2u
 /* P2P_RLY_ALV（双向）:
 *   payload: 空（仅包头）
 *   - Client -> Server: 心跳保活
 *   - Server -> Client: 心跳确认
 */
#define P2P_RLY_ALV_PSZ                 0u
/* P2P_RLY_SYN0 — 创建/恢复会话（客户端 → 服务器 & 服务器 → 客户端）:
 * + SYN0 可视作 sid=0 的首个 sync 包；sid 固定为 0，因此在线协议中省略传输。
 * + 后续真实 SYNC 的 sid 从 1 起始，循环递增。
 * + 注意一个细节，SYN0 的候选不带 FIN 标识，也就是 SYN0 不是最后一个 SYNC 包。
 *   这也同时意味着，sync0 之后至少还会有一个 SYNC 包（即使没有候选，也会有一个 count=0 的空 SYNC 作为结束标识）
 *
 * Client -> Server 格式:
 *   payload: [target_name(32)][candidate_count(1)][candidates(N*23)]
 *   - target_name: 目标 peer 名称，定长 32 字节，0 填充
 *   - candidate_count: 本端首批候选数量（可以为 0）
 *   - candidates: N 个 p2p_candidate_t（每个 23 字节）
 *   - 语义: 发起/恢复会话，并提交 sid=0 的初始化候选
 *
 * Server -> Client 语义（统一看作 SYN0 的 S2C 三态）:
 *
 *   1. 离线态:
 *      payload: [target_name(32)][session_id(P2P_SESS_ID_SZ)][0xFF(1)]
 *      - target_name: 目标 peer 名称，定长 32 字节，0 填充
 *      - session_id: 会话 ID（网络字节序）
 *      - 0xFF(1): 常量，表示对端离线
 *      - 语义: 会话已建立，但对端当前未在线；本端 sid=0 候选已缓存，等待对端上线
 *
 *   2. 在线态:
 *      payload: [source_name(32)][session_id(P2P_SESS_ID_SZ)][candidate_count(1)][candidates(N*23)]
 *      - source_name: 对端 peer 名称，定长 32 字节，0 填充
 *      - session_id: 会话 ID（网络字节序）
 *      - candidate_count: 对端 sid=0 初始化候选数量
 *      - candidates: 对端首批候选地址列表
 *      - 语义: 会话建立完成，对端已在线；SYN0 自身就承载 sid=0 的初始化 payload
 *      - 这可以有两种到达时机，但语义相同：
 *        a. 对端本来就在线：作为本次建会的直接应答返回
 *        b. 对端当时离线：先返回离线态；待对端后续上线后，再补发一个在线态 SYN0
 *
 *   3. 错误态:
 *      - 通过 P2P_RLY_STA + P2P_RLY_SYN0 返回错误，而不是单独的 SYN0 负载态
 *
 * SYN0 隐式 ACK 机制：
 *   服务器向对端转发/下发在线态 SYN0 后，对端无独立的 SYN0 ACK 消息。
 *   协议规定：对端收到 SYN0 后，必然随后发送首个 SYNC 上行包（sid=1）；该首个 SYNC 即视为隐式 SYN0 ACK。
 *   服务器收到对端首个 SYNC 上行时，释放对应的 SYN0 转发缓冲区（sync_peer_sent）。
 */
#define P2P_RLY_SYN0_PSZ(n)             (P2P_PEER_ID_MAX + 1u + (n)*sizeof(p2p_candidate_t))
#define P2P_RLY_SYN0_S2C_PSZ(n)         (P2P_SESS_ID_SZ + P2P_RLY_SYN0_PSZ(n))
#define P2P_RLY_IS_SYN0_PEER_OFF(p)     (((uint8_t*)(p))[P2P_PEER_ID_MAX + P2P_SESS_ID_SZ] == 0xFF)
/* P2P_RLY_SYNC — 会话数据同步（客户端 ↔ 服务器 ↔ 客户端）:
 * + SYN0 负责 sid=0 的初始化数据；本节 SYNC 只描述 sid>=1 的后续同步。
 *
 * Client -> Server / Server -> Client 数据格式:
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(1)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *   - session_id: 会话 ID（网络字节序）
 *   - sid: 本包序列号（从 1 起始循环递增；0 保留给 SYN0）
 *   - candidate_count: 本包候选数量
 *   - candidates: N 个 p2p_candidate_t（每个 23 字节）
 *   - fin_marker: 可选 1 字节；存在且为 0xFF 表示 FIN（本端候选发送完成）
 *   - Server -> Client 下发时，session_id 会被重写为接收方本地会话 ID；sid 透传用于去重
 *
 * Server -> Client confirm 格式:
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(1)]
 *   - session_id: 会话 ID（网络字节序）
 *   - sid: 回显对应上行 SYNC 的序列号
 *   - 语义: 一个 confirm 表示该 sid 对应批次已全部转发到对端；客户端收到后可立即发送下一批
 */
#define P2P_RLY_SYNC_CONFIRM_PSZ        (P2P_SESS_ID_SZ + 1u)
#define P2P_RLY_SYNC_PSZ(n, mk)         (P2P_SESS_ID_SZ + 2u + (n)*sizeof(p2p_candidate_t) + ((mk) ? 1u : 0u))
#define P2P_RLY_IS_SYNC_CONFIRM(hdr)    (((p2p_relay_hdr_t*)(hdr))->size == P2P_RLY_SYNC_CONFIRM_PSZ)
/* P2P_RLY_FIN:
 *   payload: [session_id(P2P_SESS_ID_SZ)]
 *   - session_id: 要结束的会话 ID（网络字节序）
*/
#define P2P_RLY_FIN_PSZ                 (P2P_SESS_ID_SZ)

/* P2P_RLY_PKT:
 *   所有 TCP relay 数据包 payload 统一格式: [session_id(P2P_SESS_ID_SZ)][P2P hdr(4)][data]
 *   P2P hdr = [type(1)][flags(1)][seq(2)]，内层 type 区分实际包类型
 *   (DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK 等均通过 P2P_RLY_PKT 隧道传输)
 *
 *   说明：
 *   - session_id 用于会话隔离与服务器路由（转发到配对会话）。
 *   - 服务器零拷贝转发，仅重写 session_id，不解析内层 P2P hdr。
 */
#define P2P_RLY_PKT_PSZ(n)              (P2P_SESS_ID_SZ + P2P_HDR_SIZE + (n))

/* P2P_RLY_REQ / P2P_RLY_RSP 最小负载长度（session_id + sid + msg/code = 11 字节） */
#define P2P_RLY_RPC_MIN_PSZ             (P2P_SESS_ID_SZ + 3)

/* P2P_RLY_REQ / P2P_RLY_RSP — 基于会话的 MSG RPC
 *
 * 与 COMPACT 模式的 MSG RPC 对应，但基于 TCP 可靠传输，无需应用层重传。
 * 使用 session_id 路由（与 SYNC/DATA 一致），服务器零拷贝转发时仅重写 session_id。
 *
 * P2P_RLY_REQ (双向，A→Server, Server→B):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
 *   - session_id: 发送方的会话 ID（来自初次下行 SYN0 建会应答；服务器转发时重写为接收方的 session_id）
 *   - sid: 序列号（非零，循环递增）
 *   - msg: 消息类型（0=echo 自动回复，>0=应用自定义）
 *   - data: 请求数据（最大 P2P_MSG_DATA_MAX 字节）
 *
 * P2P_RLY_RSP (双向，B→Server, Server→A):
 *   payload: [session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][data(N)]
 *   - session_id: 本端会话 ID（服务器转发时重写为请求方的 session_id）
 *   - sid: 对应请求的序列号
 *   - code: 响应码（0=成功，应用自定义；0xFE/0xFF=错误，见下）
 *   - data: 响应数据
 *
 * 特殊错误码（服务器生成的错误响应，on_response 回调 len=-1）：
 *   0xFF (P2P_RPC_ERR_PEER_OFF): 对端在等待响应期间离线
 *   0xFE (P2P_RPC_ERR_TIMEOUT):      服务器转发超时
 *
 * 流控：使用 rpc_pending 通道（独立于 SYNC/DATA 的 peer_send），
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
 *   ├──── TCP Connect ─────────►│
 *   │                            │
 *   ├──── REG ─────────────────►│  [name][instance_id]
 *   │                            │
 *   │◄── REG ───────────────────┤  [features][candidate_sync_max]
 *   │   (features: RELAY|MSG)    │  告知服务器功能与候选批量上限
 *   │                            │
 *   [进入 REG 状态]              │
 *   │                            │
 *   ├──── ALV ─────────────────►│  (每 20 秒心跳)
 *   │                            │
 *   │◄── ALV ───────────────────┤  (空 payload 确认)
 *   │                            │
 *
 * 2. 初始化会话同步（SYN0 = sid=0 初始化包）
 * ============================================================================
 *
 *   Client A                  Server
 *   │                            │
 *   ├──── SYN0 ────────────────►│  [target][cnt=5][cands]
 *   │                            │  查找 B 的状态
 *   │                            │  分配 session_id
 *   │◄── SYN0 ──────────────────┤  [peer=B][session_id][cnt=5]
 *   │   (online 态 SYN0，携带 sid=0 初始化候选) │
 *   │                            │
 *   [收到 session_id 与 sid=0 候选，可继续上传 sid>=1] 
 *
 * 3. 后续会话同步（对端在线，实时转发）
 * ============================================================================
 *
 *   Client A                     Server                         Client B
 *   │                               │                              │
 *   ├────── SYN0 ─────────────────►│  [target=B][cnt=5]           │
 *   │                               │  B 在线，分配 sid=123        │
 *   │◄──── SYN0 ───────────────────┤ [peer=B][session_id=123][cnt=5]│
 *   │   (online 态 SYN0，携带 sid=0 初始化候选) │                  │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=5]            │
 *   │   (上传剩余 5 个候选)         │  立即转发给 B                │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │  [sid=456][cnt=5]            │
 *   │                               │   (A 的 5 个候选)            │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=5]            │
 *   │   (状态 3: 已转发 5 个)       │  缓冲区有空间才回            │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=3]            │
 *   │   (再上传 3 个)               │  B 在线，实时转发            │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │  [sid=456][cnt=3]            │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=0][fin=0xFF]  │
 *   │   (上传完成，FIN 标记)        │                              │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │  [sid=456][cnt=0][fin=0xFF]  │
 *   │                               │                              │
 *   │<======================== P2P ICE 打洞 ======================>│
 *
 * 4. 后续会话同步（对端离线，缓存后推送）
 * ============================================================================
 *
 *   Client A (在线)           Server                         Client B (离线)
 *   │                               │                              │
 *   ├────── REG ──────────────────►│                              │
 *   │◄──── REG ────────────────────┤                              │
 *   │                               │                              │
 *   ├────── SYN0 ─────────────────►│  [target=B][cnt=5]           │
 *   │                               │  B 离线                      │
 *   │◄──── SYN0 ───────────────────┤ [peer=B][session_id=123][offline]|
 *   │ (offline 态 SYN0，B 当前离线) │                              │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=5]            │
 *   │   (上传 5 个候选)             │  尝试缓存                    │
 *   │                               │  [缓冲区空间有限]            │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=3]            │
 *   │   (confirm: 仅缓存/转发了 3 个)│  有空间才回确认             │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=2]            │
 *   │   (从第 4 个重传)             │  继续缓存                    │
 *   │                               │                              │
 *   │◄──── SYNC ───────────────────┤  [sid=123][fwd=2]            │
 *   │ (confirm: 再缓存 2 个，空间满) │  暂不确认，等空间           │
 *   │   [buffer full, no confirm]   │                              │
 *   │                               │                              │
 *   [等待对端上线...]               │                              │
 *   │                               │                              │
 *   │    ... B 上线 ...             │                              │
 *   │                               │◄────── REG ─────────────────┤
 *   │                               ├──────── REG ───────────────►│
 *   │                               │                              │
 *   │                               │◄────── SYN0 ────────────────┤  [target=A][cnt=3]
 *   │                               ├──────── SYN0 ──────────────►│  [peer=A][session_id=456][cnt=3]
 *   │                               │ (online 态 SYN0，携带 sid=0 初始化候选) │
 *   │                               │                              │
 *   │◄──── SYN0 ───────────────────┤  [peer=B][session_id=123][cnt=5] │
 *   │ (对端上线后补发 online 态 SYN0，携带缓存的 sid=0 候选) │      │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=2]            │
 *   │   (继续上传剩余候选)          │  B 在线，实时转发            │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │  [sid=456][cnt=2]            │
 *   │                               │                              │
 *   ├────── SYNC ─────────────────►│  [sid=123][cnt=0][fin=0xFF]  │
 *   │   (上传完成，FIN 标记)        │                              │
 *   │                               │                              │
 *   │                               ├────── SYNC ────────────────►│
 *   │                               │  [sid=456][cnt=0][fin=0xFF]  │
 *   │                               │                              │
 *   │<======================== P2P ICE 打洞 ======================>│
 *
 *
 * 5. P2P_RLY_PKT - P2P 包中继
 * ============================================================================
 *
 * 功能：P2P 打洞失败时，通过服务器转发 P2P 包（降级方案）
 *
 * Client → Server:
 *   P2P_RLY_PKT: [session_id(P2P_SESS_ID_SZ)][P2P hdr(4)][payload(N)]
 *   - 内层 P2P hdr.type 区分: DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK
 *   - 服务器零拷贝转发，仅重写 session_id。
 *
 * Server → Target:
 *   - 按目标侧 session_id 重写后原样转发。
 *
 * 6. REQ/RSP 机制 - RPC 请求-应答
 * ============================================================================
 *
 * 功能：通过服务器中转实现可靠的请求-应答机制（TCP 传输，无需 ACK/重传）
 *
 * msg 特殊值：
 *   - msg=0: Echo 测试，B端自动回复相同数据，无需应用层介入
 *   - msg>0: 应用层自定义消息类型，需 on_request 回调处理
 *
 * 流控：使用 rpc_pending 通道（独立于 SYNC/DATA 的 peer_send），
 *       每个方向同时最多一个 RPC 消息在传输中。
 *
 * 错误处理（服务器生成错误 RSP 返回给 A）：
 *   - 对端离线: code=0xFE (P2P_RPC_ERR_PEER_OFF)
 *   - 转发超时: code=0xFF (P2P_RPC_ERR_TIMEOUT)
 *
 * 流程（4 步）：
 *
 *   A (requester)          Server               B (responder)
 *   │                        │                        │
 *   ├─── RLY_REQ ──────────►│                        │
 *   │  [ses_id_A][sid][msg]  │                        │
 *   │  [data]                │                        │
 *   │                        ├──── RLY_REQ ─────────►│
 *   │                        │  [ses_id_B][sid][msg]  │
 *   │                        │  [data]                │
 *   │                        │                        │
 *   │                        │◄── RLY_RSP ───────────┤
 *   │                        │  [ses_id_B][sid][code] │
 *   │                        │  [data]                │
 *   │◄── RLY_RSP ───────────┤                        │
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
 * WSS 模式协议 (WebSocket, 纯文本帧)
 * ============================================================================
 *
 * 基于 WebSocket 的 ICE 信令通道，为 P2P_SIGNALING_MODE_ICE 模式提供
 * ICE 候选交换，并支持按 peer_id 的 SDP 文本转发。每条 WS text frame 承载一条消息，纯文本格式。
 *
 * 与 COMPACT/RELAY 的二进制协议不同，WSS 采用可读文本协议，
 * 便于调试和跨语言集成（如浏览器 JavaScript 客户端）。
 *
 * 传输层：WebSocket (RFC 6455)，text frame (opcode=0x1)
 * 编码：UTF-8 纯文本
 * 分帧：每条 WS text frame = 一条完整消息，无粘包问题
 * 可靠性：WebSocket over TCP，保证有序可靠传输，无需应用层重传
 *
 * 服务端维护 client_t / session_t 体系，提供会话感知的信令服务：
 * - 断线重连保留会话状态（类似 RELAY fd 迁移）
 * - 对端上线主动通知（统一 SYN0 推送，应答与推送同格式）
 * - 离线客户端超时清理（WSS_CLIENT_TIMEOUT_S = 60s）
 *
 * 消息格式通用规则：
 * - 第一个空格前为命令关键字（大写）
 * - 空格分隔字段
 * - 每条消息以 '\n' 结尾（作为消息结束标识；接收方可将 msg[len-1] 置 '\0' 后按 C 字符串处理）
 * - SYNC/SYN0/SDP 消息中第一个 '\n' 分隔头部字段与 payload
 * - peer_id: UTF-8 字符串，最长 P2P_PEER_ID_MAX-1 字节（不含 NUL）
 * - session_id: 8 位 16 进制 ASCII 表示（如 1A2B3C4D，4 字节，零拷贝友好）
*/

/* WSS 消息类型前缀（纯文本匹配，非二进制编码）
 * 2026-05-17: WSS SYNC/CONFIRM 协议重设计，完全对齐 relay：
 *   0. SYN0 可视作 sid=0 的首个 sync 包；sid=0 固定，因此在 SYN0 线协议中省略传输。
 *      后续真实 SYNC 的 sid_hex 从 01 起始，循环递增。
 *      注意：SYN0 自身不携带结束语义，因此不是最后一个 SYNC 包。
 *      这也意味着，在 sid=0 的 SYN0 之后，至少还会有一个 sid_hex>=01 的真实 SYNC 包；
 *      即使没有更多候选，也应由后续 SYNC 显式给出结束语义（例如发送 ICE_DONE）。
 *   1. 每个 SYNC 包都独立确认（confirm），不再归并累计字节量，无 confirm <bytes> 语法。
 *   2. <session_id> 字段统一为 8 位 16 进制（4 字节，零拷贝友好，便于 relay/wss 统一转发）。
 *   3. SYNC/CONFIRM 均带 sid_hex 字段，格式与 relay 完全一致，支持安全的同步中断重连。
 *   4. 文本协议示例：
 *        "SYNC <session_id_hex> <sid_hex>\n<payload>\n"
 *        "SYNC <session_id_hex> <sid_hex> confirm\n"
 *        "SYNC <session_id_hex> <sid_hex> busy\n"
 *      其中 session_id_hex 固定 8 位 16 进制（如 1A2B3C4D），sid_hex 固定 2 位 16 进制（如 AF）。
 *   5. 兼容说明：原 confirm <bytes> 语法废弃，所有确认均为一包一 confirm。
 */
#define P2P_WSS_CMD_REG         "REG "          /* + <peer_id> <instance_id>\n */                                      // 注册身份
#define P2P_WSS_CMD_OFF         "OFF"           /* OFF\n */                                                            // 主动下线（立即释放资源）
#define P2P_WSS_CMD_SDP         "SDP "          /* + <remote_peer_id>\n<sdp> */                                        // 基于 peer_id 的 SDP 文本转发（不依赖会话）
#define P2P_WSS_CMD_SYN0        "SYN0 "         /* + <remote_peer_id>\n  或  <remote_peer_id>\n<payload>\n */          // 创建/恢复会话（可选预缓存负载）
#define P2P_WSS_CMD_SYNC        "SYNC "         /* + <session_id_hex> <sid_hex>\n<payload>\n */                        // 同步数据 (C2S & S2C)
#define P2P_WSS_CMD_FIN         "FIN "          /* + <session_id_hex>\n */                                             // 会话结束 (C2S & S2C)

#define P2P_WSS_RSP_REG_OK      "REG OK "       /* + <sync_max> <features>\n */           // 注册成功，sync_max=预缓存负载上限，features=功能位掩码
#define P2P_WSS_RSP_REG_FAIL    "REG FAIL "     /* + <reason>\n */
#define P2P_WSS_RSP_SDP_OK      "SDP OK "       /* + <remote_peer_id>\n */
#define P2P_WSS_RSP_SDP_FAIL    "SDP FAIL "     /* + <remote_peer_id> <reason>\n */
#define P2P_WSS_RSP_SYN0        "SYN0 "         /* + <peer_id> <session_id_hex> online\n[<payload>\n]|offline\n */
#define P2P_WSS_RSP_SYN0_FAIL   "SYN0 FAIL "    /* + <reason>\n */
#define P2P_WSS_RSP_SYNC        "SYNC "         /* + <session_id_hex> <sid_hex> confirm\n|busy\n  (S2C 响应) */

/* SYNC payload 子类型前缀（应用层约定，服务器透传） */
#define P2P_WSS_PAY_ICE          "ICE\n"        /* + <candidate_line> */
#define P2P_WSS_PAY_ICE_DONE     "ICE_DONE"

/* WSS 二进制帧类型（WebSocket binary frame, opcode=0x2）
 *
 * 帧格式: [type(1)][session_id(P2P_SESS_ID_SZ)][payload(N)]
 *   - type: 见下方 P2P_WSS_BIN_* 定义
 *   - session_id: uint32 网络字节序，路由键
 *   - payload: 类型相关数据（服务器仅重写 session_id，透传 payload）
 *
 * 与 RELAY 模式的区别:
 *   - 无需 relay_hdr.size（WebSocket 帧自带长度）
 *   - 无需 STATUS 应答（WebSocket 可靠传输，PACKET 无需 ACK/流控）
 *   - RPC 错误统一用伪造 RSP 返回（peer_offline / timeout）
 */
#define P2P_WSS_BIN_PKT        0x01    /* [type][ses_id][p2p_hdr(4)][data(N)] */        // 中继 P2P 数据包:
#define P2P_WSS_BIN_REQ        0x02    /* [type][ses_id][sid(2)][msg(1)][data(N)] */    // RPC 请求
#define P2P_WSS_BIN_RSP        0x03    /* [type][ses_id][sid(2)][code(1)][data(N)] */   // RPC 响应

/* ============================================================================
 * WSS 协议详细定义说明
 * ============================================================================
 *
 * ────────────────────────────────────────────────────────────────────────────
 * REG — 身份注册（客户端 → 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "REG <peer_id> <instance_id>\n"
 *   - peer_id: 本端身份标识，UTF-8 字符串，最长 P2P_PEER_ID_MAX-1 字节
 *   - instance_id: 客户端实例 ID（uint32 十进制 ASCII），每次 online() 生成新随机数
 *                  用于服务器区分网络重连（保留会话）和客户端重启（销毁旧会话）
 *
 * 功能: 注册本端身份，建立 peer_id → WS 连接的映射。
 *       类似 RELAY P2P_RLY_REG / COMPACT SIG_PKT_REG。
 *
 * 服务端处理:
 *   1. peer_id 为空 / instance_id 无效 → 返回 "REG FAIL ...\n"
 *   2. peer_id + instance_id 均匹配（同一 cid）→ 幂等，返回 "REG OK <sync_max>\n"
 *   3. peer_id 匹配 + instance_id 相同（不同 cid，网络重连）:
 *      - 踢掉旧 WS 连接（ws_server_disconnect, code=1000）
 *      - 复用 wss_client_t，更新 cid，保留所有会话
 *      - 遍历已配对会话，向所有在线对端推送 "SYN0 <peer_id> <peer_session_id_hex> online\n"
 *      - 向本端推送所有在线对端的 "SYN0 <remote_peer_id> <session_id_hex> online\n"
 *      - 返回 "REG OK <sync_max>\n"
 *   4. peer_id 匹配 + instance_id 不同（客户端重启）:
 *      - 销毁旧 client 及其所有会话（通知对端 FIN）
 *      - 创建新 wss_client_t
 *      - 返回 "REG OK <sync_max>\n"
 *   5. 同一 cid 曾注册其他 peer_id → 清除旧 client 及其会话
 *   6. 全新注册 → 创建 wss_client_t，返回 "REG OK <sync_max>\n"
 *
 * 响应:
 *   "REG OK <sync_max> <features>\n" — 注册成功
 *     - sync_max: SYN0 预缓存负载字节上限（不含 NUL）
 *     - features: 服务器功能位掩码（十进制），与 RELAY 一致:
 *         0x01 = P2P_RLY_FEATURE_RELAY（支持数据包中继）
 *         0x02 = P2P_RLY_FEATURE_MSG（支持 MSG RPC 机制）
 *   "REG FAIL <reason>\n"      — 注册失败
 *     reason: "empty peer_id"      — peer_id 为空
 *             "invalid instance_id" — instance_id 缺失或为 0
 *             "OOM"                — 内存分配失败
 *
 * 示例:
 *   → "REG alice_device_01 3827401956\n"
 *   ← "REG OK 2048 3\n"
*/
#define P2P_WSS_CMD_REG_SZ          (sizeof(P2P_WSS_CMD_REG) - 1u)      /* "REG " */
#define P2P_WSS_CMD_REG_FMT         P2P_WSS_CMD_REG "%s %u\n"           /* "REG <peer_id> <instance_id>\n" */

#define P2P_WSS_RSP_REG_OK_SZ       (sizeof(P2P_WSS_RSP_REG_OK) - 1u)   /* "REG OK " */
#define P2P_WSS_RSP_REG_OK_FMT      P2P_WSS_RSP_REG_OK "%u %u\n"        /* "REG OK <sync_max> <features>\n" */
#define P2P_WSS_RSP_REG_FAIL_SZ     (sizeof(P2P_WSS_RSP_REG_FAIL) - 1u) /* "REG FAIL " */
#define P2P_WSS_RSP_REG_FAIL_FMT    P2P_WSS_RSP_REG_FAIL "%s\n"         /* "REG FAIL <reason>\n" */
/* ────────────────────────────────────────────────────────────────────────────
 * OFF — 主动下线（客户端 → 服务器）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "OFF\n"
 *
 * 功能: 主动注销身份并立即释放服务器资源（client + 所有会话）。
 *       与 REG 配对使用。相比直接断开 WS 连接，OFF 无需等待超时回收。
 *
 * 服务端处理:
 *   1. 已注册 → 调用 wss_invalidate_client(do_free=true)
 *      - 遍历所有会话，通知在线对端 "FIN <peer_session_id>\n"
 *      - 释放所有会话和 client 结构
 *   2. 未注册 → 静默忽略
 *
 * 无响应（服务器不回复）。
 *
 * 示例:
 *   → "OFF\n"
 */
#define P2P_WSS_CMD_OFF_MSG         P2P_WSS_CMD_OFF "\n"                 /* "OFF\n" */
/* ────────────────────────────────────────────────────────────────────────────
 * SDP — 基于 peer_id 的文本转发（双向：客户端 → 服务器 → 客户端）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "SDP <remote_peer_id>\n<sdp>"
 *   - remote_peer_id: 目标对端 peer_id
 *   - sdp: 完整 SDP 文本（可包含多行），位于首个 '\n' 之后直到帧末尾
 *
 * 功能: 按 peer_id 直接路由并转发 SDP 文本，不依赖双方是否已建会，不依赖 session_id。
 *       与 SYNC 的区别：SYNC 走 session_id 路由并具备缓存/confirm 语义；
 *       SDP 仅做在线即时转发并返回一次成功/失败结果（类似 RPC ACK）。
 *
 * 前置条件: 发送方必须已 REG 注册。
 *
 * 服务端处理:
 *   1. 未 REG → 返回 "REG FAIL not registered\n"
 *   2. remote_peer_id 为空 → 返回 "SDP FAIL <remote_peer_id> empty peer id\n"
 *   3. sdp 为空 → 返回 "SDP FAIL <remote_peer_id> empty sdp\n"
 *   4. 对端不存在/未注册/离线 → 返回 "SDP FAIL <remote_peer_id> peer offline\n"
 *   5. 对端在线 → 转发给对端并返回 "SDP OK <remote_peer_id>\n"
 *
 * 转发格式（S2C）:
 *   "SDP <source_peer_id>\n<sdp>"
 *
 * 响应（回给发送方）:
 *   "SDP OK <remote_peer_id>\n"
 *   "SDP FAIL <remote_peer_id> <reason>\n"
 *
 * 示例:
 *   → "SDP bob_device_02\nv=0\no=- 123 2 IN IP4 127.0.0.1\n..."
 *   ← "SDP OK bob_device_02\n"
 *
 *   (bob 收到)
 *   ← "SDP alice_device_01\nv=0\no=- 123 2 IN IP4 127.0.0.1\n..."
 */
#define P2P_WSS_CMD_SDP_SZ              (sizeof(P2P_WSS_CMD_SDP) - 1u)              /* "SDP " */
#define P2P_WSS_CMD_SDP_FMT             P2P_WSS_CMD_SDP "%s\n"                      /* "SDP <remote_peer_id>\n<sdp>" 头部 */

#define P2P_WSS_RSP_SDP_OK_SZ           (sizeof(P2P_WSS_RSP_SDP_OK) - 1u)           /* "SDP OK " */
#define P2P_WSS_RSP_SDP_OK_FMT          P2P_WSS_RSP_SDP_OK "%s\n"                   /* "SDP OK <remote_peer_id>\n" */
#define P2P_WSS_RSP_SDP_FAIL_SZ         (sizeof(P2P_WSS_RSP_SDP_FAIL) - 1u)         /* "SDP FAIL " */
#define P2P_WSS_RSP_SDP_FAIL_FMT        P2P_WSS_RSP_SDP_FAIL "%s %s\n"              /* "SDP FAIL <remote_peer_id> <reason>\n" */
/* ────────────────────────────────────────────────────────────────────────────
 * SYN0 — 创建/恢复会话（客户端 → 服务器 & 服务器 → 客户端）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "SYN0 <remote_peer_id>\n"
 *       "SYN0 <remote_peer_id>\n<payload>\n"   （可选携带预缓存负载）
 *   - remote_peer_id: 目标对端 peer_id
 *   - payload: 可选，预缓存的 ICE 候选数据（对端离线时服务器缓存，上线后转发）
 *     最大长度由 REG OK 返回的 sync_max 决定（不含 NUL）
 *
 * 功能: 创建与指定对端的信令同步会话，建立 session_pair_t 双向配对。
 *       可选携带首批 ICE 候选作为预缓存负载（类似 RELAY SYN0 的 peer_send）
 *       SYN0 可视作 sid=0 的初始化同步包；后续真实 SYNC 从 sid_hex=01 开始。
 *       SYN0 自身不携带结束语义，因此不是最后一个 SYNC 包；即使没有更多候选，
 *       也应由后续真实 SYNC 明确结束本轮同步（例如发送一个 ICE_DONE）。
 *
 * 前置条件: 发送方必须已 REG 注册，否则返回 "REG FAIL not registered\n"
 *
 * 服务端处理:
 *   1. remote_peer_id 为空 → 返回 "SYN0 FAIL empty peer_id\n"
 *   2. 已有到该对端的活跃会话（session_pair 中已存在）:
 *      a. 对端标记已死亡（peer == -1）→ 清除标记，尝试重新配对
 *      b. 追加本端预缓存负载（超出 sync_max 则返回 FAIL）
 *      c. 对端在线 → 交换双方缓存
 *         - 向本端发送 "SYN0 <remote_peer_id> <session_id_hex> online\n[<payload>\n]"
 *         - 向对端发送 "SYN0 <local_peer_id> <peer_session_id_hex> online\n[<payload>\n]"
 *         - 若对端有预缓存，则直接拼接在发给本端的 SYN0 online 之后一起下发
 *         - 若本端有预缓存，则直接拼接在发给对端的 SYN0 online 之后一起下发
 *      d. 对端离线 → 返回 "SYN0 <remote_peer_id> <session_id_hex> offline\n"
 *      e. payload 超出 sync_max → 返回 "SYN0 FAIL too large\n"
 *   3. 无已有会话 → 调用 build_session() 创建:
 *      a. 创建 wss_session_t，分配 session_id
 *      b. 查找 session_pair（双向 key），已有则配对
 *      c. 对端已创建会话且未死亡 → 双向配对（peer 指针互指）
 *      d. 追加本端 payload（超出 sync_max 则 FAIL）
 *      e. 若配对成功且对端在线 → 交换双方缓存；缓存负载直接随 SYN0 online 一起下发
 *      f. 否则返回 "SYN0 <remote_peer_id> <session_id_hex> offline\n"
 *   4. build_session 失败 → 返回 "SYN0 FAIL internal\n"
 *
 * 服务器 → 客户端语义（统一看作 SYN0 的 S2C 三态）:
 *   SYN0 在 C2S 方向是 “发起/恢复会话” 的请求；
 *   在 S2C 方向统一是“应答/通知会话状态 + 返回 session_id_hex”。
 *
 *   1. "SYN0 <remote_peer_id> <session_id_hex> offline\n"
 *      — 会话已建立，但对端当前未在线
 *      — 本端 payload 已缓存；对端上线后，服务器会再发送对应的 online 通知
 *      — 因此 offline 不是与 online 并列的终态，而是“尚未 online”的中间态
 *
 *   2. "SYN0 <remote_peer_id> <session_id_hex> online\n"
 *      "SYN0 <remote_peer_id> <session_id_hex> online\n<payload>\n"
 *      — 会话建立完成，对端已在线
 *      — session_id_hex: 固定 8 位 16 进制 ASCII（如 0000002A，由 generate_session_id() 分配）
 *      — 可视作 sid=0 的首个 sync 包：若已有预缓存 payload，则直接附着在本次 SYN0 online 应答/通知内
 *      — 这可以有两种到达时机，但语义相同：
 *        a. 对端本来就在线：作为本次 SYN0 的直接应答返回
 *        b. 对端当时离线：先返回 offline；待对端后续上线后，再补发一个 online 通知
 *      — 若本包携带 payload，客户端应将其按 sid=0 的初始化数据处理；后续真实 SYNC 从 sid_hex=01 开始
 *
 *   3. "SYN0 FAIL <reason>\n"
 *      — 会话创建失败，未进入会话态
 *      — reason: "empty peer_id"   — remote_peer_id 为空
 *                "internal"        — build_session 内部错误（OOM / 重复创建）
 *                "too large"       — payload 超出 sync_max 上限，服务器直接拒绝
 *                "not registered"  — 发送方未 REG（返回 "REG FAIL not registered\n"）
 *
 * 示例:
 *   → "SYN0 bob_device_02\n"
 *   ← "SYN0 bob_device_02 0000002A offline\n"
 *
 *   → "SYN0 bob_device_02\nICE\na=candidate:1 1 udp ...\n"
 *   ← "SYN0 bob_device_02 0000002A offline\n"        (负载已缓存)
 *
 *   (bob 随后也发 SYN0)
 *   → "SYN0 alice_device_01\nICE\na=candidate:2 1 udp ...\n"      (bob 发送)
 *   ← "SYN0 alice_device_01 0000002B online\nICE\na=candidate:1 1 udp ...\n"
 *                                            (bob 收到 alice 的 sid=0 初始化 payload)
 *   ← "SYN0 bob_device_02 0000002A online\nICE\na=candidate:2 1 udp ...\n"
 *                                            (alice 收到 bob 的 sid=0 初始化 payload)
 *
 *   → "SYN0 bob_device_02\nICE\n...\n"       (alice 再次追加，payload 超出 sync_max)
 *   ← "SYN0 FAIL too large\n"

 * 客户端处理（统一适用于 direct online 应答和 deferred online 通知）:
 *   收到 "SYN0 <peer_id> <session_id_hex> online\n" 或
 *        "SYN0 <peer_id> <session_id_hex> online\n<payload>\n" 后应：
 *   1. 根据 session_id 查找/创建本地 session
 *   2. 若 SYN0 自身携带 payload，则按 sid=0 的初始化数据处理该批候选
 *   3. 通过 SYNC 发送 ICE 候选，发起或继续双方对等的候选交换
 *   4. 若此前已有候选交换（重连场景），应重新收集并发送
 *
 * 触发来源（对客户端来说无需区分）:
 *   1. 本端发送 SYN0 时对端已在线 → 直接收到 online 应答
 *   2. 本端发送 SYN0 时对端离线 → 先收到 offline；对端后续 REG/重连成功后，再收到 online 通知
 *   3. 任一方若有 SYN0 预缓存 payload，则直接附着在该次 online SYN0 内一起下发
 *
 * 示例:
 *   ← "SYN0 bob_device_02 0000002A online\n"
 *   (客户端随后发起或继续 ICE 候选交换)
 *   → "SYNC 0000002A 2A\nICE\na=candidate:...\n"
 */
#define P2P_WSS_CMD_SYN0_SZ             (sizeof(P2P_WSS_CMD_SYN0) - 1u)            /* "SYN0 " */
#define P2P_WSS_CMD_SYN0_FMT            P2P_WSS_CMD_SYN0 "%s\n"                    /* "SYN0 <remote_peer_id>\n" */

#define P2P_WSS_RSP_SYN0_SZ             (sizeof(P2P_WSS_RSP_SYN0) - 1u)            /* "SYN0 " */
#define P2P_WSS_RSP_SYN0_REG_FMT        P2P_WSS_RSP_SYN0 "%s %08X online\n"        /* "SYN0 <peer_id> <session_id_hex> online\n" */
#define P2P_WSS_RSP_SYN0_OFF_FMT        P2P_WSS_RSP_SYN0 "%s %08X offline\n"       /* "SYN0 <peer_id> <session_id_hex> offline\n" */
#define P2P_WSS_RSP_SYN0_FAIL_SZ        (sizeof(P2P_WSS_RSP_SYN0_FAIL) - 1u)       /* "SYN0 FAIL " */
#define P2P_WSS_RSP_SYN0_FAIL_FMT       P2P_WSS_RSP_SYN0_FAIL "%s\n"               /* "SYN0 FAIL <reason>\n" */

/* ────────────────────────────────────────────────────────────────────────────
 * SYNC — 会话数据同步（客户端 ↔ 服务器 ↔ 客户端）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式:
 *   → "SYNC <session_id_hex> <sid_hex>\n<payload>\n"      // 客户端/服务器 → 服务器/客户端
 *   ← "SYNC <session_id_hex> <sid_hex> confirm\n"         // 服务器/客户端 → 客户端/服务器
 *   ← "SYNC <session_id_hex> <sid_hex> busy\n"            // 仅服务器 → 客户端
 *   - session_id_hex: 8 位 16 进制（4 字节，零拷贝友好）
 *   - sid_hex: 2 位 16 进制（1 字节，完全对齐 relay，必须为16进制，如 AF）
 *   - payload: ICE 候选等同步数据
 *
 * 语义：
 *   - 每个 SYNC 包都独立确认（confirm）
 *   - confirm 必须带 sid_hex，便于断点续传和安全重连。
 *   - busy 仅由服务器返回，表示服务器缓存区满，客户端需等待后重试。
 *
 * 示例：
 *   → "SYNC 1A2B3C4D AF\nICE candidate:...\n"   // 1A2B3C4D 和 AF 都是16进制
 *   ← "SYNC 1A2B3C4D AF confirm\n"
 *   ← "SYNC 1A2B3C4D AF busy\n"
 */
#define P2P_WSS_CMD_SYNC_SZ             (sizeof(P2P_WSS_CMD_SYNC) - 1u)         /* "SYNC " */
#define P2P_WSS_CMD_SYNC_FMT            P2P_WSS_CMD_SYNC "%08X %02X\n"          /* "SYNC <session_id_hex> <sid_hex>\n" 8位16进制+2位16进制 */
#define P2P_WSS_RSP_SYNC_CONFIRM_FMT    P2P_WSS_RSP_SYNC "%08X %02X confirm\n"  /* "SYNC <session_id_hex> <sid_hex> confirm\n" */
#define P2P_WSS_RSP_SYNC_BUSY_FMT       P2P_WSS_RSP_SYNC "%08X %02X busy\n"     /* "SYNC <session_id_hex> <sid_hex> busy\n" */

/* ────────────────────────────────────────────────────────────────────────────
 * FIN — 会话结束（双向：C2S + S2C）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 格式: "FIN <session_id>\n"
 *   - session_id: 要结束的会话 session_id
 *
 * 功能: 通知对方某个会话结束。与 COMPACT SIG_PKT_FIN / RELAY P2P_RLY_FIN 对齐。
 *
 * C2S（客户端 → 服务器，主动断开会话）:
 *   1. 客户端发送 "FIN <session_id>\n" 主动断开指定会话
 *   2. 服务器释放该会话，并向对端转发 FIN（携带对端自己的 session_id）
 *
 * S2C（服务器 → 客户端，对端断连通知）:
 *   1. 对端 WS 连接断开（网络中断/主动关闭）
 *   2. 对端发送 FIN 主动断开（服务器转发）
 *   3. 对端会话被超时清理（wss_free_session）
 *
 * 客户端处理:
 *   收到 FIN 后应：
 *   1. 根据 session_id 找到本地 session
 *   2. 标记对端已断开，暂停向其发送 SYNC
 *   3. 等待后续 SYN0 推送再恢复交换
 *   4. 不需要销毁本地 session（服务端可能保留等待重连）
 *
 * 示例:
 *   → "FIN 42\n"                        (C2S: 客户端主动断开)
 *   ← "FIN 42\n"                        (S2C: 对端断连通知)
 */

#define P2P_WSS_CMD_FIN_SZ              (sizeof(P2P_WSS_CMD_FIN) - 1u)          /* "FIN " */
#define P2P_WSS_CMD_FIN_FMT             P2P_WSS_CMD_FIN "%u\n"                  /* "FIN <session_id>\n" */
/* ════════════════════════════════════════════════════════════════════════════
 * 二进制帧协议（WebSocket binary frame, opcode=0x2）
 * ════════════════════════════════════════════════════════════════════════════
 *
 * 以下消息使用 WebSocket 二进制帧传输，与上方文本帧信令共享同一 WS 连接。
 * 二进制帧用于 P2P 数据中继（打洞失败降级）和 MSG RPC（服务器中转请求-应答），
 * 对应 RELAY 模式 P2P_RLY_PKT / P2P_RLY_REQ / P2P_RLY_RSP。
 *
 * 公共帧格式: [type(1)][session_id(P2P_SESS_ID_SZ)][payload(N)]
 *   - type: 消息类型（P2P_WSS_BIN_*）
 *   - session_id: uint32 网络字节序，用于会话路由
 *   - payload: 类型相关数据
 *
 * 与 RELAY 的区别:
 *   - 无 relay_hdr.size — WebSocket 帧自带长度
 *   - 无 STATUS 应答 — WebSocket 可靠传输，PACKET 无需 ACK/流控
 *   - RPC 错误统一使用服务器生成的伪 RSP 返回
 *
 * ────────────────────────────────────────────────────────────────────────────
 * PKT — P2P 数据包中继（双向：客户端 ↔ 服务器 ↔ 客户端）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 帧格式: [0x01][session_id(P2P_SESS_ID_SZ)][p2p_hdr(4)][data(N)]
 *   - session_id: 发送方的 session_id（来自 SYN0 建会应答）
 *   - p2p_hdr: P2P 协议头 [type(1)][flags(1)][seq(2)]
 *   - data: P2P 协议数据
 *
 * 功能: P2P 打洞失败时，通过服务器中继转发 P2P 数据包（降级方案）。
 *       对应 RELAY P2P_RLY_PKT。
 *
 * 服务端处理:
 *   1. 查找 session_id 对应的会话
 *   2. 对端在线 → 重写 session_id 为对端的 session_id，原样转发
 *   3. 对端离线 → 静默丢弃（实时数据不缓存）
 *   服务器不解析内层 p2p_hdr，仅做路由转发。
 *
 * 最小帧长度: P2P_WSS_BIN_PACKET_MIN = 9 字节
 */
#define P2P_WSS_BIN_PKT_MIN_SZ             (1 + P2P_SESS_ID_SZ + P2P_HDR_SIZE)
/* ────────────────────────────────────────────────────────────────────────────
 * REQ — RPC 请求（双向：A → 服务器 → B）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 帧格式: [0x02][session_id(P2P_SESS_ID_SZ)][sid(2)][msg(1)][data(N)]
 *   - session_id: 发送方的 session_id
 *   - sid: 序列号（非零，循环递增，用于请求-响应匹配）
 *   - msg: 消息类型（0=echo 自动回复，>0=应用自定义）
 *   - data: 请求数据（最大 P2P_MSG_DATA_MAX 字节）
 *
 * 功能: 通过服务器中转实现可靠的请求-应答机制。
 *       对应 RELAY P2P_RLY_REQ。
 *
 * 服务端处理:
 *   1. 对端离线 → 生成伪 RSP [0x03][ses_id][sid][code=0xFF]
 *   2. RPC 忙（前一个 REQ 尚未收到 RSP）→ 生成伪 RSP [code=0xFE]
 *   3. 正常 → 重写 session_id，转发给对端
 *      记录 rpc_pending_sid，等待 RSP 解锁
 *
 * 超时: 服务器每秒检查 RPC 待确认链表，
 *       超过 REQ_MAX_RETRY × MSG_RPC_RETRY_INTERVAL_MS 未收到 RSP
 *       → 生成伪 RSP [code=0xFE (P2P_RPC_ERR_TIMEOUT)]
 *
 * ────────────────────────────────────────────────────────────────────────────
 * RSP — RPC 响应（双向：B → 服务器 → A）
 * ────────────────────────────────────────────────────────────────────────────
 *
 * 帧格式: [0x03][session_id(P2P_SESS_ID_SZ)][sid(2)][code(1)][data(N)]
 *   - session_id: 响应方的 session_id
 *   - sid: 对应请求的序列号
 *   - code: 响应码（0=成功，应用自定义；服务器错误见下）
 *   - data: 响应数据
 *
 * 功能: 将 RPC 响应通过服务器转发回请求方。
 *       对应 RELAY P2P_RLY_RSP。
 *
 * 服务端处理:
 *   1. 请求方离线 → 静默丢弃
 *   2. sid 与请求方 rpc_pending_sid 不匹配 → 静默丢弃
 *   3. 正常 → 重写 session_id 为请求方的 session_id，转发
 *      解锁请求方 rpc_pending_sid（RPC 生命周期完成）
 *
 * 服务器生成的错误 RSP（客户端 on_response 回调 len=-1）:
 *   code=0xFF (P2P_RPC_ERR_PEER_OFF): 对端在等待响应期间离线或会话销毁
 *   code=0xFE (P2P_RPC_ERR_TIMEOUT):      服务器转发超时或 RPC 通道忙
 *
 * 最小帧长度: P2P_WSS_BIN_RSP_MIN = 8 字节
 */
#define P2P_WSS_BIN_RPC_MIN_SZ          (1 + P2P_SESS_ID_SZ + 3)

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
 *   ├── "REG alice\n" ─────────────►│  创建 wss_client_t
 *   │                                │  peer_id="alice", cid=N
 *   │◄── "REG OK <sync_max>\n" ─────┤
 *   │                                │
 *   [进入 REG 状态]                  │
 *   │                                │
 *   [WebSocket 自身的 PING/PONG]     │  (保活由 WS 协议层处理)
 *
 * 2. 会话建立（首方离线等待）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                          Bob (离线)
 *   │                                │                                │
 *   ├── "SYN0 bob\nICE\n...\n" ────►│  build_session("alice","bob")  │
 *   │                                │  pair 创建，side=0             │
 *   │                                │  remote_s=NULL (bob 未注册)    │
 *   │                                │  缓存 alice 的 payload         │
 *   │◄── "SYN0 bob 0000002A offline\n" ───┤                          │
 *   │                                │                                │
 *   [等待 SYN0 bob 0000002A online]  │                                │
 *
 * 3. 会话配对（对端上线 + 双向通知）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                             Bob
 *   │                                │                                │
 *   │                                │◄── "REG bob\n" ───────────────┤
 *   │                                │  创建 wss_client_t             │
 *   │                                ├── "REG OK <sync_max>\n" ─────►│
 *   │                                │                                │
 *   │                                │◄── "SYN0 alice\nICE\n...\n" ──┤
 *   │                                │  build_session("bob","alice")  │
 *   │                                │  pair 找到，side=1             │
 *   │                                │  remote_s=alice's session      │
 *   │                                │  → 双向配对 + 交换缓存        │
 *   │                                │                                │
 *   │◄─ "SYN0 bob 0000002A online\nICE\n...\n" ─┤  (推送给 alice, 含 bob 的 sid=0 payload)
 *   │                                ├─ "SYN0 alice 0000002B online\nICE\n...\n" ─►│
 *   │                                │   (应答给 bob, 含 alice 的 sid=0 payload)
 *   │                                │                                │
 *   [处理 bob 的 sid=0 候选]         │ [处理 alice 的 sid=0 候选]
 *
 * 4. 同步交换 ICE 候选（alice sid=42, bob sid=43）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Alice                         Server                             Bob
 *   │                                │                                │
 *   ├──── "SYNC 0000002A 2A\nICE\n...\n" ───►│ sid=2A → 配对 → bob sid=2B │
 *   │◄── "SYNC 0000002A 2A confirm\n" ────┤                                │
 *   │                                ├──── "SYNC 0000002B 2B\nICE\n...\n" ───►│
 *   │                                │                                │
 *   │                                │◄── "SYNC 0000002B 2B\nICE\n...\n" ─────┤
 *   │                                ├──── "SYNC 0000002B 2B confirm\n" ──►│
 *   │◄── "SYNC 0000002A 2A\nICE\n...\n" ─────┤ sid=2B → 配对 → alice sid=2A │
 *   │                                │                                │
 *   ├──── "SYNC 0000002A 2A\nICE_DONE\n" ───►│                        │
 *   │◄── "SYNC 0000002A 2A confirm\n" ─────┤                        │
 *   │                                ├──── "SYNC 0000002B 2B\nICE_DONE\n" ───►│
 *   │                                │                                │
 *   │                                │◄── "SYNC 0000002B 2B\nICE_DONE\n" ─────┤
 *   │                                ├──── "SYNC 0000002B 2B confirm\n" ───►│
 *   │◄── "SYNC 0000002A 2A\nICE_DONE\n" ─────┤                        │
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
 *   │◄── "FIN 42\n" ────────────────┤                                │
 *   │                                │                                │
 *   [暂停向 bob 发送 SYNC]           │     ... 网络恢复 ...           │
 *   │                                │                                │
 *   │                                │◄── WebSocket Connect ─────────┤
 *   │                                │◄── "REG bob\n" ───────────────┤
 *   │                                │  复用 wss_client_t             │
 *   │                                │  更新 cid，保留会话            │
 *   │                                ├── "REG OK <sync_max>\n" ─────►│
 *   │                                │                                │
 *   │                                │  遍历 bob 的已配对会话         │
 *   │                                │  推送 online，并把预缓存直接附着在 SYN0 内 │
 *   │◄── "SYN0 bob 0000002A online\nICE\n...\n" ─┤                 │
 *   │                                ├── "SYN0 alice 0000002B online\nICE\n...\n" ►│
 *   │                                │                                │
 *       [重新发起 ICE 候选交换]      │    [重新发起 ICE 候选交换]
 *   ├── "SYNC 0000002A 2A\nICE\n...\n" ─────►│  ...
 *
 * 6. 超时清理（离线过久，释放资源）
 * ────────────────────────────────────────────────────────────────────────────
 *
 *   Server (每 CLEANUP_INTERVAL_S 秒执行一次):
 *   │
 *   ├── 遍历 g_wss_clients
 *   │   └── 对每个离线 client (cid == -1):
 *   │       └── 若 now - last_active > WSS_CLIENT_TIMEOUT_S (60s):
 *   │           ├── 释放所有会话 (wss_free_session)
 *   │           │   └── 每个有配对的会话 → 通知对端 "FIN <peer_session_id>\n"
 *   │           │       并标记对端 peer 指针为 -1
 *   │           └── 移除 client (HASH_DELETE + free)
 */

#pragma pack(pop)
#pragma ide diagnostic pop
#pragma clang diagnostic pop
#endif /* P2PP_H */
