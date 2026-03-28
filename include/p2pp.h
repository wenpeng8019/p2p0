/*
 * P2P 信令协议定义
 *
 * 统一定义客户端和服务器使用的协议格式，包括：
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
#define P2P_PEER_ID_MAX  32

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
    uint8_t             type;                       // 候选类型 (0=Host 1=Srflx 2=Relay 3=Prflx)
    p2p_sockaddr_t      addr;                       // 候选地址（18B）
    uint32_t            priority;                   // 候选优先级 htonl（RFC 8445: 32-bit）
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
#define P2P_MSG_DATA_MAX  (P2P_MAX_PAYLOAD - 11)    /* MSG RPC data upper bound: relay path needs [session_id(8)+sid(2)+msg(1)] */

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

/*
 * P2P_PKT_PUNCH / PUNCH_ACK 协议（即时应答设计）
 *
 * ============================================================================
 * PUNCH (0x01) — 连接探测/打洞/保活包
 * ============================================================================
 *   包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 *   负载: 无
 *   发送: 每 500ms 定时向所有候选路径并发发送（打洞阶段），或向活动路径发送（保活阶段）
 *   接收: 立即回复 PUNCH_ACK，更新路径可达性，设置 rx_confirmed
 *
 * ============================================================================
 * PUNCH_ACK (0x02) — PUNCH 的即时确认包
 * ============================================================================
 *   包头: [type=0x02 | flags=0 | seq=回传对方的 PUNCH seq(2B)]
 *   负载: 无
 *   发送: 收到 PUNCH 后立即回复，从同一源地址发出（确保回程走相同路径）
 *   接收: 通过 seq 匹配发送记录计算 per-path RTT，设置 rx_confirmed + tx_confirmed
 *
 * ============================================================================
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
 * ============================================================================
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
 * ============================================================================
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

/*
 * P2P_PKT_CONN / CONN_ACK 协议（连接建立三次握手的最后一次）
 *
 * ============================================================================
 * CONN (0x03) — 连接就绪包
 * ============================================================================
 *   包头: [type=0x03 | flags=0 | seq=发送方序列号(2B)]
 *   负载: 无
 *   发送: 收到第一个 REACH 后定期发送，直到收到 CONN_ACK 或数据包
 *   接收: 立即回复 CONN_ACK，状态机转换为 CONNECTED，允许数据传输
 *
 * ============================================================================
 * CONN_ACK (0x04) — 连接就绪确认包
 * ============================================================================
 *   包头: [type=0x04 | flags=0 | seq=回传对方的 CONN seq(2B)]
 *   负载: 无
 *   发送: 收到 CONN 后立即回复
 *   接收: 停止发送 CONN，状态机转换为 CONNECTED
 *
 * ============================================================================
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
 * ============================================================================
 * 交互示例
 * ============================================================================
 *
 *   时间轴（ms）      Alice                                 Bob
 *   ────────────────────────────────────────────────────────────────────
 *   t=0              PUNCH(seq=1) ─────────────────────────→
 *
 *   t=10                                                    收到 PUNCH(seq=1)
 *                                  ←───────────────────── REACH(seq=1)
 *
 *   t=20             收到 REACH(seq=1)
 *                    [双向连通确认]
 *                    CONN(seq=100) ────────────────────────→
 *
 *   t=30                                                    收到 CONN(seq=100)
 *                                                           [状态 → CONNECTED]
 *                                  ←───────────────────── CONN_ACK(seq=100)
 *
 *   t=40             收到 CONN_ACK(seq=100)
 *                    [停止 CONN 重传]
 *                    [状态 → CONNECTED]
 *                    DATA(seq=1) ──────────────────────────→ (开始数据传输)
 */
#define P2P_PKT_CONN            0x03        // 连接就绪包（三次握手最后一次）
#define P2P_PKT_CONN_ACK        0x04        // 连接就绪确认包

/*
 * 数据传输 (peer-to-peer)
 *
 * DATA/ACK/CRYPTO/REACH flags 字段:
 *   P2P_DATA_FLAG_SESSION (0x01): 携带 session_id（8字节）
 *
 * 格式 (flags & 0x01 == 0，上层协议自带会话隔离如 SCTP/DTLS):
 *   DATA:   [hdr(4)][data(N)]
 *   ACK:    [hdr(4)][ack_seq(2)][sack(4)]
 *   CRYPTO: [hdr(4)][crypto_data(N)]
 *   REACH:  [hdr(4)][target_addr(6)]  (直连 P2P 路径)
 *
 * 格式 (flags & 0x01 == 1，裸可靠层或中继路径):
 *   DATA:   [hdr(4)][session_id(8)][data(N)]
 *   ACK:    [hdr(4)][session_id(8)][ack_seq(2)][sack(4)]
 *   CRYPTO: [hdr(4)][session_id(8)][crypto_data(N)]
 *   REACH:  [hdr(4)][session_id(8)][target_addr(6)]  (服务器中转)
 *
 * session_id 用于:
 *   1. 会话隔离: 过滤旧会话重传的包（解决重连污染问题）
 *   2. 服务器路由: 中继路径时服务器通过 session_id 查找目标对端
 */
#define P2P_PKT_DATA            0x20        // 数据包
#define P2P_PKT_ACK             0x21        // 确认包
#define P2P_PKT_CRYPTO          0x22        // DTLS 加密包（握手/密文数据）

/* P2P_PKT_DATA/ACK/CRYPTO/REACH flags 标志位 */
#define P2P_DATA_FLAG_SESSION   0x01        // 携带 session_id（8字节），用于会话隔离/中继路由

/* 会话控制 */
#define P2P_PKT_FIN             0x30        // 结束包（无需应答）

/* ============================================================================
 * COMPACT 模式信令服务协议 (UDP)
 * ============================================================================
 *
 * 基于 P2P 协议的扩展，用于客户端与信令服务器通信。
 * 复用 P2P 协议的 4 字节包头: [type: u8 | flags: u8 | seq: u16]
 *
 * 候选列表序列化同步机制:
 *
 * 由于 UDP 包大小限制，候选列表需要分批传输。通过序列化的 PEER_INFO 包完成可靠同步：
 *
 *   1. 注册阶段（仅发送一次）：
 *      - 客户端发送 REGISTER（含 instance_id 与 UDP 包可容纳的最大候选列表）
 *      - 服务器回复 REGISTER_ACK（告知缓存能力 max_candidates）
 *        · max_candidates = 0: 不支持缓存
 *        · max_candidates > 0: 支持缓存，最大缓存数量
 *      - 收到 ACK 后停止 REGISTER，进入 REGISTERED 状态
 *
 *   2. 候选同步阶段（序列化 + 确认）：
 *      - 双方上线后，服务器发送 PEER_INFO(seq=0)，包含缓存的对端候选
 *      - 客户端收到后发送 PEER_INFO_ACK（携带 session_id）确认
 *      - 客户端通过 PEER_INFO(seq=1,2,3,...) 继续同步剩余候选（携带 session_id）
 *      - 对端通过 PEER_INFO_ACK 确认，未确认则重发
 *      - 允许乱序：seq>0 可能先于 seq=0 到达，接收端按 seq 位图去重并最终收敛
 *
 *   3. 离线缓存流程：
 *
 *      Alice (在线)           Server                    Bob (离线)
 *        |                       |                          |
 *        |--- REGISTER --------->|                          |
 *        |<-- REGISTER_ACK ------|  (peer_online=0, max=5)
 *        |   [进入 REGISTERED]   |                          |
 *        |                       |  (缓存 Alice 的候选)      |
 *        |    ... Bob 上线 ...                              |
 *        |                       |<-- REGISTER ------------|
 *        |                       |--- REGISTER_ACK -------->|  (peer_online=1, max=5)
 *        |<-- PEER_INFO(seq=0) --|--- PEER_INFO(seq=0) --->|  (包含缓存的 5 个候选 + session_id)
 *        |--- PEER_INFO_ACK ----->|<-- PEER_INFO_ACK -------|  (携带 session_id)
 *        |                       |                          |
 *        |<=============== P2P PEER_INFO 序列化同步 ========>|  (所有包携带 session_id)
 *        |--- PEER_INFO(seq=1, base=5) ----------------->  |  (从第 6 个候选开始)
 *        |<-- PEER_INFO_ACK(seq=1) ----------------------  |
 *        |--- PEER_INFO(seq=2, base=10) ---------------->  |
 *        |<-- PEER_INFO_ACK(seq=2) ----------------------  |
 *        |--- PEER_INFO(seq=3, count=0, FIN) ----------->  |  (结束标识)
 *        |<-- PEER_INFO_ACK(seq=3) ----------------------  |
 *
 * 注：REGISTER 仅在注册阶段发送，收到 REGISTER_ACK 后停止（直到重连）
 */

/* COMPACT 信令协议 (客户端 <-> 信令服务器) - 0x80-0x9F */
#define SIG_PKT_REGISTER        0x80        // 注册到信令服务器（含本地候选列表与 instance_id）
#define SIG_PKT_REGISTER_ACK    0x81        // 注册确认（告知 session_id、本端缓存能力、公网地址、探测端口、中继支持）
#define SIG_PKT_UNREGISTER      0x82        // 主动注销：客户端关闭时通知服务器立即释放配对槽位
                                            // 【服务端可选实现】服务端不处理此包时，自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制
#define SIG_PKT_PEER_INFO       0x83        // 候选列表同步包（序列化传输）
#define SIG_PKT_PEER_INFO_ACK   0x84        // 候选列表确认（确认指定序列号）
#define SIG_PKT_PEER_OFF        0x85        // 服务器下行通知：对端已离线/断开

#define SIG_PKT_ALIVE           0x86        // 保活包（可选，客户端定期发送以维持注册状态）
#define SIG_PKT_ALIVE_ACK       0x87        // 保活确认（服务器回复以确认注册状态）

/*
 * MSG：服务器缓存+可靠中转的 RPC 机制（服务器可选实现）
 *
 * 通过服务器中转，实现对端间可信赖的一次请求-应答交互（类似 RPC）。
 * 服务器是否支持由 REGISTER_ACK.flags 中 SIG_REGACK_FLAG_MSG (0x02) 位标识。
 *
 * msg 特殊值（请求消息类型）：
 *   - msg=0: Echo 测试，B端自动回复相同数据，无需应用层介入
 *   - msg>0: 应用层自定义消息类型，需要应用层在 on_request 回调中处理并调用 p2p_response()
 *
 * 错误处理：
 *   1. REQ_ACK 阶段 B 不在线：Server → A 返回 status=1，A 调用 on_response(len=-1, code=原始请求msg)
 *   2. B 在等待响应期间离线：Server → A 发送 MSG_RESP(flags=SIG_MSG_FLAG_PEER_OFFLINE)，
 *      A 调用 on_response(len=-1, code=SIG_MSG_ERR_PEER_OFFLINE)
 *   3. Server 转发请求超时：Server → A 发送 MSG_RESP(flags=SIG_MSG_FLAG_TIMEOUT)，
 *      A 调用 on_response(len=-1, code=SIG_MSG_ERR_TIMEOUT)
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
 */
#define SIG_PKT_MSG_REQ         0x88        // MSG 请求：A→Server（[session_id][sid][msg][data]）；Server→B relay（含 session_id，flags=SIG_MSG_FLAG_RELAY）
#define SIG_PKT_MSG_REQ_ACK     0x89        // MSG 请求确认：Server→A（已缓存并开始中转，或失败状态）
#define SIG_PKT_MSG_RESP        0x8A        // MSG 应答：B→Server（应答内容）；Server→A relay（转发 B 的应答）
#define SIG_PKT_MSG_RESP_ACK    0x8B        // MSG 应答确认：Server→B（B 停止重发）；A→Server（A 停止重发）
 
#define SIG_PKT_NAT_PROBE       0x8C        // NAT 类型探测请求（发往探测端口）
#define SIG_PKT_NAT_PROBE_ACK   0x8D        // NAT 类型探测响应（返回第二次映射地址）


/* REGISTER_ACK status 码 */
#define SIG_REGACK_PEER_OFFLINE 0           // 成功，对端离线
#define SIG_REGACK_PEER_ONLINE  1           // 成功，对端在线

/* REGISTER_ACK 标志位（p2p_packet_hdr_t.flags） */
#define SIG_REGACK_FLAG_RELAY   0x01        // 服务器支持数据中继功能（P2P 打洞失败降级）
#define SIG_REGACK_FLAG_MSG     0x02        // 服务器支持 MSG RPC 机制（可可靠中转请求-应答）

/* MSG 包标志位（p2p_packet_hdr_t.flags） */
#define SIG_MSG_FLAG_RELAY      0x01        // 标识此 MSG_REQ 是 Server→B 的中转包（而非 A→Server 的原始请求）

/* MSG_RESP 包标志位 - 用于标识服务器特殊错误（而非对端返回的正常响应） */
#define SIG_MSG_FLAG_PEER_OFFLINE   0x02    // B端在 REQ_ACK 之后离线（等待响应期间离线）
#define SIG_MSG_FLAG_TIMEOUT        0x04    // 服务器向B端转发请求超时

/* MSG_RESP 错误码（当 on_response 回调的 len=-1 时，msg 字段表示错误类型） */
#define SIG_MSG_ERR_PEER_OFFLINE    0xFE    // B端在等待响应期间离线（区别于 REQ_ACK 时已知离线）
#define SIG_MSG_ERR_TIMEOUT         0xFF    // 服务器转发请求超时

/* PEER_INFO 标志位（p2p_packet_hdr_t.flags） */
#define SIG_PEER_INFO_FIN           0x01    // 候选列表发送完毕

/*
 * COMPACT 模式消息格式（以下均为 payload 部分，前面需加 4 字节包头）:
 *
 * 候选地址统一使用 p2p_candidate_t（23 字节），COMPACT 和 RELAY 模式共享同一线格式。
 *
 * REGISTER:
 *   payload: [local_peer_id(32)][remote_peer_id(32)][instance_id(4)][candidate_count(1)][candidates(N*23)]
 *   包头: type=0x80, flags=0, seq=0
 *   - instance_id: 本次 connect() 的实例 ID（网络字节序，32位，必须非 0）
 *   - 语义:
 *       * instance_id 相同: 视为 REGISTER 重传（例如客户端未收到 REGISTER_ACK）
 *       * instance_id 不同: 视为同一 peer_key 的新实例（客户端重启/重连），服务端重置旧会话状态
 *
 * REGISTER_ACK:
 *   payload: [status(1)][session_id(8)][instance_id(4)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
 *   包头: type=0x81, flags=见下, seq=0
 *   - status: 0=对端离线, 1=对端在线, >=2=错误码
 *   - session_id: 本端会话 ID（网络字节序，64位，注册成功后立即分配）
 *   - instance_id: 回显客户端 REGISTER 中的 instance_id（网络字节序，32位）
 *     客户端收到后应比较 instance_id 与当前实例是否一致，不一致则丢弃此 ACK
 *   - max_candidates: 服务器为该对端缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器主端口观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: 包头的 flags 字段可设置：
 *       SIG_REGACK_FLAG_RELAY (0x01) 表示服务器支持中继
 *       SIG_REGACK_FLAG_MSG (0x02) 表示服务器支持 MSG RPC 机制
 *   总大小: 4(包头) + 22(payload) = 26 字节
 *
 * UNREGISTER:
 *   payload: [local_peer_id(32)][remote_peer_id(32)]
 *   包头: type=0x88, flags=0, seq=0
 *   客户端主动断开时发送，请求服务器立即释放配对槽位
 *   服务器收到后会向对端发送 PEER_OFF 通知
 *
 * ALIVE:
 *   payload: [session_id(8)]
 *   包头: type=0x86, flags=0, seq=0
 *   - session_id: 本端会话 ID（网络字节序，64位，来自 REGISTER_ACK）
 *   用于客户端在 REGISTERED 状态定期发送，保持服务器槽位活跃
 *
 * ALIVE_ACK:
 *   payload: 空（仅包头）
 *   包头: type=0x87, flags=0, seq=0
 *   服务器回复确认，表示槽位仍然有效
 *
 * PEER_OFF:
 *   payload: [session_id(8)]
 *   包头: type=0x89, flags=0, seq=0
 *   服务器下行通知：对端已离线/断开连接
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 *   客户端收到此包后应停止该会话的所有传输和重传
 *
 * PEER_INFO:
 *   payload: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*23)]
 *   包头: type=0x84, flags=见下, seq=序列号
 *   - session_id: 会话 ID（网络字节序，64位，来自 REGISTER_ACK）
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（配合 FIN 标志）
 *   - seq=0: 服务器发送，base_index=0，包含缓存的对端候选
 *   - seq=0 且 base_index!=0: 地址变更通知（candidate_count=1）
 *       * base_index 作为 8 位循环通知序号（1..255 循环）
 *       * 接收端按循环序比较新旧，旧通知可忽略但仍需 ACK
 *   - seq>0: 客户端发送，base_index 递增，继续同步剩余候选，使用 REGISTER_ACK 中的 session_id
 *   - flags: 包头的 flags 字段可设置 SIG_PEER_INFO_FIN (0x01) 表示候选列表发送完毕
 *   - seq 窗口: 0..16（0 为服务器首包，1..16 为后续候选批次）
 *   - 乱序处理: 允许 seq>0 先于 seq=0 到达；接收端按序号位图去重，重复包仅 ACK 不重复入表
 *
 * PEER_INFO_ACK:
 *   payload: [session_id(8)]
 *   包头: type=0x85, flags=0, seq=确认的 PEER_INFO 序列号
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - seq: 确认的 PEER_INFO 序列号（0 表示确认服务器下发的 PEER_INFO(seq=0)）
 *   - seq 窗口: 0..16（客户端接收端仅接受 1..16）
 *
 * NAT_PROBE:
 *   payload: 空（无需额外字段）
 *   包头: type=0x86, flags=0, seq=客户端分配的请求号
 *   - seq 可用于匹配响应，客户端可递增或随机分配
 *
 * NAT_PROBE_ACK:
 *   payload: [probe_ip(4)][probe_port(2)]
 *   包头: type=0x87, flags=0, seq=对应的 NAT_PROBE 请求 seq
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *   - seq: 复制请求包的 seq，用于客户端匹配响应
 *
 * P2P_PKT_DATA (flags & P2P_DATA_FLAG_SESSION):
 *   payload: [session_id(8)][data(N)]
 *   包头: type=0x20, flags=0x01, seq=数据序列号
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - data: 实际数据内容
 *   用于裸可靠层的会话隔离，或通过服务器中继转发数据
 *
 * P2P_PKT_ACK (flags & P2P_DATA_FLAG_SESSION):
 *   payload: [session_id(8)][ack_seq(2)][sack(4)]
 *   包头: type=0x21, flags=0x01, seq=0
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - ack_seq: 累积确认序列号（网络字节序）
 *   - sack: 选择性确认位图（网络字节序）
 *
 * P2P_PKT_CRYPTO (flags & P2P_DATA_FLAG_SESSION):
 *   payload: [session_id(8)][crypto_data(N)]
 *   包头: type=0x22, flags=0x01, seq=0
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - crypto_data: DTLS 握手或加密数据
 *   用于中继路径的 DTLS 握手和加密数据转发
 * 
 * MSG_REQ (A → Server):
 *   payload: [session_id(8)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x88, flags=0, seq=0
 *   - session_id: A 的会话 ID（来自 REGISTER_ACK）
 *   - sid: A 生成的 16 位序列号（每次 connect() 范围内唯一，用于匹配应答）
 *   - msg: 应用层消息 ID（协议层透传，由应用自定义）
 *   - A 重发此包直到收到 MSG_REQ_ACK
 *
 * MSG_REQ (Server → B, relay):
 *   payload: [session_id(8)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x88, flags=SIG_MSG_FLAG_RELAY(0x01), seq=0
 *   - session_id: A 的会话 ID（B 用此字段构造 MSG_RESP）
 *   - Server 重发此包直到收到 MSG_RESP
 *
 * MSG_REQ_ACK (Server → A):
 *   payload: [session_id(8)][sid(2)][status(1)]
 *   包头: type=0x89, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 A 端验证响应合法性）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - status: 0=已缓存并开始向 B 中转；1=目标 B 不在线
 *   - A 收到此包后停止重发
 *
 * MSG_RESP (B → Server):
 *   payload: [session_id(8)][sid(2)][code(1)][data(N)]
 *   包头: type=0x8A, flags=0, seq=0
 *   - session_id: 从 MSG_REQ relay 中取得的 A 的会话 ID
 *   - B 重发此包直到收到 Server → B 的 MSG_RESP_ACK
 *
 * MSG_RESP_ACK (Server → B):
 *   payload: [session_id(8)][sid(2)]
 *   包头: type=0x8B, flags=0, seq=0
 *   - session_id: B 的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - Server 确认收到 B 的 MSG_RESP，B 停止重发
 *
 * MSG_RESP (Server → A, relay):
 *   payload: [session_id(8)][sid(2)][code(1)][data(N)]
 *   包头: type=0x8A, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 A 端验证响应合法性）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - code: 响应码
 *   - data: 响应数据
 *   - Server 重发此包直到收到 A → Server 的 MSG_RESP_ACK
 *
 * MSG_RESP_ACK (A → Server):
 *   payload: [session_id(8)][sid(2)]
 *   包头: type=0x8B, flags=0, seq=0
 *   - session_id: A 的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 对应的 MSG_REQ 序列号
 *   - A 收到 Server 转发的 MSG_RESP 后发送，流程完成
 */

/* ============================================================================
 * RELAY 模式协议 (TCP)
 * ============================================================================
 *
 * 包头: [type: 1B][size: 2B]
 */

/* RELAY 模式消息类型 */
typedef enum {
    /* 会话管理 */
    P2P_RLY_ONLINE = 1,                     // 上线请求: Client -> Server
    P2P_RLY_ONLINE_ACK,                     // 上线确认: Server -> Client (含服务器功能标志)
    P2P_RLY_ALIVE,                          // 心跳: Client -> Server

    /* ICE 候选交换 */
    P2P_RLY_CONNECT,                        // 连接请求: Client -> Server (指定目标 peer)
    P2P_RLY_CONNECT_ACK,                    // 连接应答: Server -> Client (对端状态 + session_id)
    P2P_RLY_DISCONNECT,                     // 断开请求: Client -> Server / Server -> Client
    P2P_RLY_PEER_INFO,                      // 候选传输: 双向 (Client -> Server 上传, Server -> Client 下发)
    P2P_RLY_PEER_INFO_ACK,                  // 候选确认: Server -> Client (已转发给对端)

    /* P2P 数据中继（打洞失败降级） */
    P2P_RLY_DATA,                           // 中继 P2P 数据包: Client <-> Server <-> Client
    P2P_RLY_ACK,                            // 中继 P2P 确认包: Client <-> Server <-> Client
    P2P_RLY_CRYPTO,                         // 中继 P2P 加密包: Client <-> Server <-> Client

    /* 消息 RPC（服务器中转的请求-应答机制） */
    P2P_RLY_REQ,                            // 请求: Client -> Server / Server -> Client (双向)
    P2P_RLY_REQ_ACK,                        // 请求确认: Server -> Client
    P2P_RLY_RESP,                           // 响应: Client -> Server / Server -> Client (双向)
    P2P_RLY_RESP_ACK                        // 响应确认: Server -> Client
} p2p_relay_type_t;

/* RELAY 响应错误码（当 on_response 回调 len=-1 时，code 字段表示错误类型） */
#define P2P_RLY_ERR_PEER_OFFLINE    0xFE    // 目标在等待响应期间离线
#define P2P_RLY_ERR_TIMEOUT         0xFF    // 服务器转发请求超时

/* RELAY 模式包头 (3 bytes) */
typedef struct {
    uint8_t             type;
    uint16_t            size;
} p2p_relay_hdr_t;

/* RELAY 模式上线消息 (P2P_RLY_ONLINE) */
typedef struct {
    char                name[P2P_PEER_ID_MAX];          // 本地 peer 名称
    uint32_t            instance_id;                    // 实例 ID（客户端每次 online() 生成随机数）
} p2p_relay_online_t;

/* RELAY 上线确认功能标志 */
#define P2P_RLY_FEATURE_RELAY       0x01    // 支持数据包中继
#define P2P_RLY_FEATURE_MSG         0x02    // 支持 MSG RPC 机制

/* RELAY 模式上线确认 (P2P_RLY_ONLINE_ACK) */
typedef struct {
    uint8_t             features;                       // 服务器支持的功能标志
    uint8_t             candidate_relay_max;            // 服务器允许的单包最大候选数（0=客户端使用默认值）
} p2p_relay_online_ack_t;

/* RELAY 请求头（不含 data） */
typedef struct {
    char                target_name[P2P_PEER_ID_MAX];   // 目标 peer 名称（A→Server）或发送者名称（Server→B）
    uint16_t            sid;                             // 请求序列号（网络字节序）
    uint8_t             msg;                             // 消息类型（0=Echo, >0=自定义）
} p2p_relay_req_hdr_t;

/* RELAY 请求确认 */
typedef struct {
    uint16_t            sid;                             // 请求序列号（网络字节序）
    uint8_t             status;                          // 0=成功, 1=目标不在线, 2=错误
} p2p_relay_req_ack_t;

/* RELAY 响应头（不含 data） */
typedef struct {
    uint16_t            sid;                             // 请求序列号（网络字节序）
    uint8_t             code;                            // 响应码（0=成功，应用自定义）
} p2p_relay_resp_hdr_t;

/* RELAY 响应确认 */
typedef struct {
    uint16_t            sid;                             // 请求序列号（网络字节序）
} p2p_relay_resp_ack_t;

/*
 * RELAY 模式连接请求 (P2P_RLY_CONNECT)
 *
 * 客户端向服务器发起连接，指定目标 peer
 * 类似 COMPACT 的 REGISTER，但更简化（仅建立连接关系）
 */
typedef struct {
    char                target_name[P2P_PEER_ID_MAX];   // 目标 peer 名称
} p2p_relay_connect_t;

/*
 * RELAY 模式连接应答 (P2P_RLY_CONNECT_ACK)
 *
 * 服务器响应连接请求，告知对端状态和分配的 session_id
 *
 * status 状态码：
 *   0x00 = 对端在线
 *   0x01 = 对端离线
 *   0xFF = 错误（target 不存在等）
 */
typedef struct {
    uint8_t             status;                         // 对端状态
    uint8_t             reserved[3];
    uint64_t            session_id;                     // 分配的会话 ID（网络字节序）
} p2p_relay_connect_ack_t;

/*
 * RELAY 模式候选传输 (P2P_RLY_PEER_INFO)
 *
 * 双向协议：
 *   1. Client → Server: 上传候选（段落传输）
 *   2. Server → Client: 下发候选（中转对端上传的候选）
 *
 * 段落传输：
 *   - candidate_count > 0: 本段包含的候选数量
 *   - candidate_count = 0: 传输完成标识（FIN）
 *
 * session_id 用途：
 *   - Client → Server: 标识本次连接会话（来自 CONNECT_ACK）
 *   - Server → Client: 标识对端会话（用于识别候选来源）
 */
typedef struct {
    uint64_t            session_id;                     // 会话 ID（网络字节序）
    uint8_t             candidate_count;                // 本段候选数量（0=FIN）
    /* 后续跟随 candidate_count 个 p2p_candidate_t (每个23字节) */
} p2p_relay_peer_info_t;

/*
 * RELAY 模式候选确认 (P2P_RLY_PEER_INFO_ACK)
 *
 * Server → Client: 流控确认，告知本次实际转发（或缓存）的候选数量。
 *
 * 服务器仅在有能力转发（中转缓冲区有空间）时才发送 ACK；若缓冲区满则
 * 不回复 ACK，直到有空间处理后再一并回复。客户端在未收到 ACK 前不应
 * 发送下一批候选（流控门控）。
 *
 * forwarded_count 语义：
 *   - > 0: 本批次成功接受（转发或缓存）的候选数；
 *          若小于上传数，客户端需从该索引位置重传剩余候选
 *   - = 0: 本端所有候选已全部转发到对端（仅在本端发送 FIN 后出现）
 */
typedef struct {
    uint64_t            session_id;                     // 会话 ID（网络字节序）
    uint8_t             forwarded_count;                // 实际转发的候选数（0=全部完成，需已发 FIN）
} p2p_relay_peer_info_ack_t;

/*
 * RELAY 模式信令负载头部
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

/* ============================================================================
 * RELAY 模式协议流程说明
 * ============================================================================
 *
 * 基础流程：上线 → 连接 → 候选交换 → P2P 打洞 → (降级) 数据中继
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
 * 2. 连接流程（建立会话）
 * ============================================================================
 *
 *   Client A                  Server
 *   │                            │
 *   ├── CONNECT ───────────────►│  [target_name]
 *   │                            │  查找 B 的状态
 *   │                            │
 *   │◄── CONNECT_ACK ────────────┤  [status][session_id]
 *   │   (status=0 在线/1 离线)   │  分配 session_id
 *   │                            │
 *   [收到 session_id，可以上传候选]
 *
 * 3. 候选交换流程（对端在线，实时转发）
 * ============================================================================
 *
 *   Client A                  Server                    Client B
 *   │                            │                         │
 *   ├── CONNECT ───────────────►│  [target=B]             │
 *   │                            │  B 在线                 │
 *   │◄── CONNECT_ACK ────────────┤  [status=0][sid=123]    │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=5]       │
 *   │   (上传 5 个候选)           │  立即转发给 B            │
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=5]      │
 *   │                            │   (A 的 5 个候选)        │
 *   │                            │                         │
 *   │◄── PEER_INFO_ACK ──────────┤  [sid=123][fwd=5]       │
 *   │   (已转发 5 个，ACK 解锁)   │  缓冲区有空间才回        │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=3]       │
 *   │   (再上传 3 个)             │                         │
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=3]      │
 *   │                            │                         │
 *   │◄── PEER_INFO_ACK ──────────┤  [sid=123][fwd=3]       │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=0]       │
 *   │   (上传完成，FIN)           │                         │
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=0]      │
 *   │                            │   (A 候选传输完成)       │
 *   │                            │                         │
 *   │◄── PEER_INFO_ACK ──────────┤  [sid=123][fwd=0]       │
 *   │   (fwd=0 表示全部完成)      │  FIN 确认               │
 *   │                            │                         │
 *   │<==================== P2P ICE 打洞 ====================>│
 *
 * 4. 候选交换流程（对端离线，缓存后推送）
 * ============================================================================
 *
 *   Client A (在线)           Server                    Client B (离线)
 *   │                            │                         │
 *   ├── ONLINE ─────────────────►│                         │
 *   │◄── ONLINE_ACK ─────────────┤                         │
 *   │                            │                         │
 *   ├── CONNECT ───────────────►│  [target=B]             │
 *   │                            │  B 离线                 │
 *   │◄── CONNECT_ACK ────────────┤  [status=1][sid=123]    │
 *   │   (B 离线，但可上传)        │                         │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=5]       │
 *   │   (上传 5 个候选)           │  尝试缓存               │
 *   │                            │  [缓冲区空间有限]        │
 *   │◄── PEER_INFO_ACK ──────────┤  [sid=123][fwd=3]       │
 *   │   (仅缓存了 3 个)           │  有空间才回 ACK          │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=2]       │
 *   │   (从第 4 个重传)           │  继续缓存                │
 *   │                            │                         │
 *   │◄── PEER_INFO_ACK ──────────┤  [sid=123][fwd=2]       │
 *   │   (再缓存 2 个，空间满)     │  暂不 ACK，等空间        │
 *   │   [buffer full, no ACK]    │                         │
 *   │                            │                         │
 *   [等待对端上线...]            │                         │
 *   │                            │                         │
 *   │    ... B 上线 ...          │                         │
 *   │                            │◄── ONLINE ──────────────┤
 *   │                            ├── ONLINE_ACK ──────────►│
 *   │                            │                         │
 *   │                            │◄── CONNECT ─────────────┤  [target=A]
 *   │                            ├── CONNECT_ACK ─────────►│  [status=0][sid=456]
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=8]      │
 *   │                            │   (推送 A 缓存的候选)    │
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=0]      │
 *   │                            │   (A 的候选推送完成)     │
 *   │                            │                         │
 *   │◄── PEER_INFO_ACK ──────────┤  (A 的缓存批推送完成)    │
 *   │   [sid=123][fwd=5]         │  缓冲区空间有限情况下     │
 *   │                            │  有空间才回 ACK          │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=2]       │
 *   │   (继续上传剩余候选)        │  B 在线，实时转发        │
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=2]      │
 *   │                            │                         │
 *   ├── PEER_INFO ──────────────►│  [sid=123][cnt=0]       │
 *   │   (上传完成)                │                         │
 *   │                            │                         │
 *   │                            ├── PEER_INFO ───────────►│
 *   │                            │   [sid=456][cnt=0]      │
 *   │                            │                         │
 *   │<==================== P2P ICE 打洞 ====================>│
 *
 * 5. TCP 粘包处理
 * ============================================================================
 *
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
 *
 * ============================================================================
 * 消息格式详解
 * ============================================================================
 *
 * 所有消息：[p2p_relay_hdr_t: 3B][payload: N bytes]
 *
 * P2P_RLY_ONLINE:
 *   payload: [name(32)][instance_id(4)]
 *   - name: 本地 peer 名称
 *   - instance_id: 客户端每次 online() 生成的 32 位随机数（参考 RTP SSRC）
 *     · instance_id 相同 → 重传，幂等处理
 *     · instance_id 不同 → 客户端重连，服务器保持/重置数据上下文
 *
 * P2P_RLY_ONLINE_ACK:
 *   payload: [features(1)][candidate_relay_max(1)]
 *   - features: 
 *     · 0x01 = RELAY（支持数据包中继）
 *     · 0x02 = MSG（支持 RPC 机制）
 *   - candidate_relay_max: 服务器允许的单包最大候选数（0=客户端使用默认值）
 *
 * P2P_RLY_CONNECT:
 *   payload: [target_name(32)]
 *   - target_name: 目标 peer 名称
 *
 * P2P_RLY_CONNECT_ACK:
 *   payload: [status(1)][reserved(3)][session_id(8)]
 *   - status:
 *     · 0x00 = 对端在线
 *     · 0x01 = 对端离线
 *     · 0xFF = 错误（target 不存在等）
 *   - session_id: 分配的会话 ID（网络字节序）
 *
 * P2P_RLY_DISCONNECT:
 *   payload: [session_id(8)]
 *   - session_id: 要断开的会话 ID（网络字节序）
 *   - 用途：主动通知服务器结束与对端的会话，服务器转发给对端
 *
 * P2P_RLY_PEER_INFO (双向):
 *   payload: [session_id(8)][candidate_count(1)][candidates(N*23)]
 *   - session_id: 会话 ID（网络字节序）
 *   - candidate_count > 0: 本段候选数量
 *   - candidate_count = 0: 传输完成（FIN）
 *   - 用途：
 *     · Client → Server: 上传候选
 *     · Server → Client: 下发候选（中转对端上传的候选）
 *
 * P2P_RLY_PEER_INFO_ACK:
 *   payload: [session_id(8)][forwarded_count(1)]
 *   - session_id: 会话 ID（网络字节序）
 *   - forwarded_count: 实际转发（或缓存）的候选数
 *     · > 0: 成功接受 N 个候选；若 N < 上传数，客户端从第 N+1 个重传
 *     · = 0: 所有候选已转发到对端（仅 FIN 发送后才出现）
 *   注：服务器仅在中转缓冲区有空间时才发送 ACK（flow control）
 *
 * P2P_RLY_DATA / P2P_RLY_ACK / P2P_RLY_CRYPTO - P2P 包中继
 * ============================================================================
 *
 * 功能：P2P 打洞失败时，通过服务器转发数据/确认/加密包（降级方案）
 *
 * Client → Server:
 *   payload: [target_name(32)][p2p_packet(N)]
 *   - p2p_packet: 完整 P2P 包（包头+payload）
 *   - 对应关系：
 *     · P2P_RLY_DATA   <-> P2P_PKT_DATA
 *     · P2P_RLY_ACK    <-> P2P_PKT_ACK
 *     · P2P_RLY_CRYPTO <-> P2P_PKT_CRYPTO
 *
 * Server → Target:
 *   payload: [sender_name(32)][p2p_packet(N)]
 *   - 透传原始 P2P 包
 *
 * REQ/RESP 机制 - RPC 请求-应答
 * ============================================================================
 *
 * 功能：通过服务器中转实现可靠的请求-应答机制
 *
 * msg 特殊值：
 *   - msg=0: Echo 测试，自动回复
 *   - msg>0: 应用层自定义消息
 *
 * 流程（6 步）：
 *
 *   A                      Server                      B
 *   ├── REQ ──────────────►│  [target][sid][msg][data] │
 *   │◄── REQ_ACK ───────────┤  [sid][status]            │
 *   │                       ├── REQ ───────────────────►│  [sender][sid][msg][data]
 *   │                       │◄── RESP ──────────────────┤  [sid][code][data]
 *   │                       ├── RESP_ACK ──────────────►│  [sid]
 *   │◄── RESP ──────────────┤  [sid][code][data]        │
 *
 * P2P_RLY_REQ (双向，A→Server, Server→B):
 *   payload: [target_name(32)][sid(2)][msg(1)][data(N)]  (A→Server)
 *   payload: [sender_name(32)][sid(2)][msg(1)][data(N)]  (Server→B)
 *
 * P2P_RLY_REQ_ACK:
 *   payload: [sid(2)][status(1)]
 *   - status: 0=成功, 1=目标不在线
 *
 * P2P_RLY_RESP (双向，B→Server, Server→A):
 *   payload: [sid(2)][code(1)][data(N)]
 *
 * P2P_RLY_REQ (Server → B, relay，复用相同协议类型):
 *   payload: [sender_name(32)][sid(2)][msg(1)][data(N)]
 *   - sender_name: 请求发起方名称（B 通过此识别来源）
 *   - sid: 透传 A 的 sid（B 响应时需要回传）
 *   - 注：通过 target_name/sender_name 字段语义区分方向
 *
 * P2P_RLY_REQ_ACK (Server → A):
 *   payload: [sid(2)][status(1)]
 *   - sid: 对应的请求序列号
 *   - status: 0=成功转发给 B; 1=目标 B 不在线; 2=服务器错误
 *
 * P2P_RLY_RESP (B → Server):
 *   payload: [sid(2)][code(1)][data(N)]
 *   - sid: 对应的请求序列号（来自 REQ）
 *   - code: 响应码（0=成功，应用自定义）
 *   - data: 响应数据
 *
 * P2P_RLY_RESP_ACK (Server → B):
 *   payload: [sid(2)]
 *   - sid: 对应的响应序列号（确认收到）
 *
 * P2P_RLY_RESP (Server → A, relay，复用相同协议类型):
 *   payload: [sid(2)][code(1)][data(N)]
 *   - sid: 对应的请求序列号（A 用此匹配原始请求）
 *   - code: 响应码
 *   - data: 响应数据
 *   - 注：Server→A 和 B→Server 使用相同格式，通过 TCP 连接区分
 *
 * 特殊错误码（当 on_response 回调 len=-1 时，code 字段表示错误类型）：
 *   0xFE: 目标在等待响应期间离线
 *   0xFF: 服务器转发超时
 *
 * TCP 特性优化：
 *   - 无需 session_id：TCP 连接已标识客户端
 *   - 无需重传机制：TCP 保证可靠传输
 *   - ACK 可选：主要用于流量控制和错误检测
 */


#pragma pack(pop)
#pragma ide diagnostic pop
#pragma clang diagnostic pop
#endif /* P2PP_H */
