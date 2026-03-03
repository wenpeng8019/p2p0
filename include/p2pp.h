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
#include <p2p.h>

/* #pragma pack(push/pop) 受 MSVC / GCC / Clang 三大编译器支持，无需平台宏 */

/* Peer ID 最大长度 */
#define P2P_PEER_ID_MAX  32

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

/* 安全的 P2P UDP 负载 */
#define P2P_MTU         1200              
#define P2P_HDR_SIZE    4                           /* 包头大小 */
#define P2P_MAX_PAYLOAD (P2P_MTU - P2P_HDR_SIZE)    /* 1196 */
#define P2P_MSG_DATA_MAX  (P2P_MAX_PAYLOAD - P2P_PEER_ID_MAX - 3)  /* MSG_REQ data: 1196-32-3=1161 */

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
 * P2P_PKT_PUNCH 协议（对称捎带式设计）
 *
 * 包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 * 负载: [echo_seq(2B, 网络字节序)]
 *   - echo_seq: 上次收到对方的 seq（初始为 0，即 last_peer_seq）
 *   - 无"探测包"/"确认包"之分，所有 PUNCH 格式完全相同
 *
 * 机制：
 *   双方各自按固定间隔定时发送 PUNCH，payload 中持续携带
 *   "上次收到对方的 seq"（捎带式 echo），无需立刻回复。
 *
 * 示例（A、B 各自定时发）：
 *   A → PUNCH(seq=1, echo=0) → B     // A 还没收到 B 的 seq
 *   B → PUNCH(seq=1, echo=0) → A     // B 还没收到 A 的 seq
 *   A → PUNCH(seq=2, echo=1) → B     // A 收到了 B 的 seq=1，捎带
 *   B → PUNCH(seq=2, echo=1) → A     // B 收到了 A 的 seq=1，捎带
 *
 * 双向连通判定：
 *   收到任意 PUNCH                    → peer→me 方向确认（入方向通）
 *   收到 PUNCH 中 echo_seq != 0       → me→peer 方向确认（出方向通，因为对方
 *                                       已收到我们至少一个包并将其 seq 回显）
 *   两个条件都满足                     → 真正双向连通 → NAT_CONNECTED
 *
 * 注：echo_seq == 0 仅在对方尚未收到我们任何包时出现（初始值）。
 *     因此 echo_seq != 0 足以证明出方向至少有一包到达，无需精确匹配最后发的 seq。
 *
 * 与即时 ACK 方案对比：
 *   即时 ACK：收到探测包后立刻额外发一个确认包（~2× 包量）
 *   捎带式：确认信息在下次定时包中捎带（包量减少 ~50%）
 *   代价：连通确认延迟增加最多一个 PUNCH_INTERVAL_MS（500ms）
 */
#define P2P_PKT_PUNCH           0x01        // 连接探测包（用于打洞和保活，带 echo_seq 负载）

/* 数据传输 (peer-to-peer) */
#define P2P_PKT_DATA            0x20        // 数据包
#define P2P_PKT_ACK             0x21        // 确认包
#define P2P_PKT_FIN             0x22        // 结束包 (无需应答)

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
 *      - 客户端发送 REGISTER（含 UDP 包可容纳的最大候选列表）
 *      - 服务器回复 REGISTER_ACK（告知缓存能力 max_candidates）
 *        · max_candidates = 0: 不支持缓存
 *        · max_candidates > 0: 支持缓存，最大缓存数量
 *      - 收到 ACK 后停止 REGISTER，进入 REGISTERED 状态
 *
 *   2. 候选同步阶段（序列化 + 确认 + session_id 分配）：
 *      - 双方上线后，服务器发送 PEER_INFO(seq=0)，包含缓存的对端候选，**首次分配 session_id**
 *      - 客户端收到后发送 PEER_INFO_ACK（携带 session_id）确认
 *      - 客户端通过 PEER_INFO(seq=1,2,3,...) 继续同步剩余候选（携带 session_id）
 *      - 对端通过 PEER_INFO_ACK 确认，未确认则重发
 *      - 允许乱序：seq>0 可能先于 seq=0 到达，接收端按 seq 位图去重并最终收敛
 *
 *   3. 离线缓存流程（含 session_id 分配）：
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
#define SIG_PKT_REGISTER        0x80        // 注册到信令服务器（含本地候选列表）
#define SIG_PKT_REGISTER_ACK    0x81        // 注册确认（告知缓存能力、公网地址、探测端口、中继支持）
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
 * msg 特殊值：
 *   - msg=0: Echo 测试，B端自动回复相同数据，无需应用层介入
 *   - msg>0: 应用层自定义消息 ID，需要应用层在 on_msg_req 回调中处理并调用 p2p_response()
 *
 * 流程（6 次传输）：
 *
 *   A                      Server                      B
 *   │                         │                        │
 *   ├── MSG_REQ(sid) ────────►│  A 重发直到收到 REQ_ACK  │
 *   │   [target_id][sid]      │  Server 查找 B 的注册槽  │
 *   │   [msg][data]           │                        │
 *   │                         │                        │
 *   │◄── MSG_REQ_ACK ─────────┤  status=0 成功/1 B不在线 │
 *   │   [sid][status]         │  A 停止重发              │
 *   │                         │                         │
 *   │                         ├── MSG_REQ ─────────────►│  Server 重发直到收到 RES
 *   │                         │   [session_id][sid]     │  (relay 标志位=1)
 *   │                         │   [msg][data]           │  msg=0 时自动 echo
 *   │                         │                         │
 *   │                         │◄── MSG_RESP ────────────┤  B 的应答同时作为对 relay 的 ACK
 *   │                         │   [session_id][sid]     │  Server 停止重发
 *   │                         │   [msg][data]           │
 *   │                         │                         │
 *   │◄── MSG_RESP ────────────┤  Server 重发直到收到 RES_ACK
 *   │   [sid][msg]            │                         │
 *   │   [data]                │                         │
 *   │                         │                         │
 *   ├── MSG_RESP_ACK ────────►│  流程完成                │
 *   │   [sid]                 │                         │
 */
#define SIG_PKT_MSG_REQ         0x88        // MSG 请求：A→Server（含目标 peer_id + payload）；Server→B relay（含 session_id，flags=SIG_MSG_FLAG_RELAY）
#define SIG_PKT_MSG_REQ_ACK     0x89        // MSG 请求确认：Server→A（已缓存并开始中转，或失败状态）
#define SIG_PKT_MSG_RESP        0x8A        // MSG 应答：B→Server（应答内容，兼作对 relay 的 ACK）；Server→A relay（转发 B 的应答）
#define SIG_PKT_MSG_RESP_ACK    0x8B        // MSG 应答确认：A→Server（已收到应答，流程完成）
 
#define SIG_PKT_NAT_PROBE       0x8C        // NAT 类型探测请求（发往探测端口）
#define SIG_PKT_NAT_PROBE_ACK   0x8D        // NAT 类型探测响应（返回第二次映射地址）


/* COMPACT 服务器中继扩展协议 - 0xA0-0xBF */
#define P2P_PKT_RELAY_DATA      0xA0        // 中继服务器转发的数据（P2P 打洞失败后的降级方案）
#define P2P_PKT_RELAY_ACK       0xA1        // 中继服务器转发的 ACK 确认包

/* REGISTER_ACK status 码 */
#define SIG_REGACK_PEER_OFFLINE 0           // 成功，对端离线
#define SIG_REGACK_PEER_ONLINE  1           // 成功，对端在线

/* REGISTER_ACK 标志位（p2p_packet_hdr_t.flags） */
#define SIG_REGACK_FLAG_RELAY   0x01        // 服务器支持数据中继功能（P2P 打洞失败降级）
#define SIG_REGACK_FLAG_MSG     0x02        // 服务器支持 MSG RPC 机制（可可靠中转请求-应答）

/* MSG 包标志位（p2p_packet_hdr_t.flags） */
#define SIG_MSG_FLAG_RELAY      0x01        // 标识此 MSG_REQ 是 Server→B 的中转包（而非 A→Server 的原始请求）

/* PEER_INFO 标志位（p2p_packet_hdr_t.flags） */
#define SIG_PEER_INFO_FIN       0x01        // 候选列表发送完毕

/*
 * COMPACT 模式精简候选结构 (7 bytes)
 * 用于 UDP 信令传输，省略了 priority 和 base_addr。
 * 
 * 布局: [type: 1B][ip: 4B][port: 2B]
 */
#pragma pack(push, 1)
typedef struct {
    uint8_t             type;               // 候选类型 (由客户端内部定义，见 p2p_signal_compact.h::p2p_compact_cand_type_t)
    uint32_t            ip;                 // IP 地址（网络字节序）
    uint16_t            port;               // 端口（网络字节序）
} p2p_compact_candidate_t;
#pragma pack(pop)

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
 * ALIVE:
 *   payload: [local_peer_id(32)][remote_peer_id(32)]
 *   包头: type=0x82, flags=0, seq=0
 *   用于客户端在 REGISTERED 状态定期发送，保持服务器槽位活跃
 *
 * ALIVE_ACK:
 *   payload: 空（仅包头）
 *   包头: type=0x83, flags=0, seq=0
 *   服务器回复确认，表示槽位仍然有效
 *
 * PEER_INFO:
 *   payload: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
 *   包头: type=0x84, flags=见下, seq=序列号
 *   - session_id: 会话 ID（网络字节序，64位，服务器在 seq=0 时分配，客户端在 seq>0 时使用收到的值）
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（配合 FIN 标志）
 *   - seq=0: 服务器发送，base_index=0，包含缓存的对端候选，**首次分配 session_id**
 *   - seq=0 且 base_index!=0: 地址变更通知（candidate_count=1）
 *       * base_index 作为 8 位循环通知序号（1..255 循环）
 *       * 接收端按循环序比较新旧，旧通知可忽略但仍需 ACK
 *   - seq>0: 客户端发送，base_index 递增，继续同步剩余候选，使用服务器分配的 session_id
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
 * MSG_REQ (A → Server):
 *   payload: [target_peer_id(32)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x88, flags=0, seq=0
 *   - target_peer_id: 目标对端 peer_id（必须已在服务器注册且在线）
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
 *   payload: [sid(2)][status(1)]
 *   包头: type=0x89, flags=0, seq=0
 *   - sid: 对应的 MSG_REQ 序列号
 *   - status: 0=已缓存并开始向 B 中转；1=目标 B 不在线
 *   - A 收到此包后停止重发
 *
 * MSG_RESP (B → Server):
 *   payload: [session_id(8)][sid(2)][msg(1)][data(N)]
 *   包头: type=0x8A, flags=0, seq=0
 *   - session_id: 从 MSG_REQ relay 中取得的 A 的会话 ID
 *   - B 的应答同时兼作对 Server relay 的 ACK（Server 收到后停止向 B 重发）
 *
 * MSG_RESP (Server → A, relay):
 *   payload: [sid(2)][msg(1)][data(N)]
 *   包头: type=0x8A, flags=0, seq=0
 *   - Server 重发此包直到收到 MSG_RESP_ACK
 *
 * MSG_RESP_ACK (A → Server):
 *   payload: [sid(2)]
 *   包头: type=0x8B, flags=0, seq=0
 *   - A 收到 MSG_RESP 后发送，流程完成
 *
 * UNREGISTER:
 *   payload: [local_peer_id(32)][remote_peer_id(32)]
 *   包头: type=0x88, flags=0, seq=0
 *   客户端主动断开时发送，请求服务器立即释放配对槽位
 *   服务器收到后会向对端发送 PEER_OFF 通知
 *
 * PEER_OFF:
 *   payload: [session_id(8)]
 *   包头: type=0x89, flags=0, seq=0
 *   服务器下行通知：对端已离线/断开连接
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 *   客户端收到此包后应停止该会话的所有传输和重传
 *
 * RELAY_DATA:
 *   payload: [session_id(8)][data(N)]
 *   包头: type=0xA0, flags=0, seq=数据序列号
 *   - session_id: 会话 ID（网络字节序，64位，用于服务器查找目标对端）
 *   - data: 实际数据内容（UDP 保留消息边界，无需 data_len 字段）
 *   用于在 P2P 打洞失败后，通过服务器中继转发数据
 *
 * RELAY_ACK:
 *   payload: [session_id(8)][ack_seq(2)][sack(4)]
 *   包头: type=0xA1, flags=0, seq=0
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - ack_seq: 累积确认序列号（网络字节序）
 *   - sack: 选择性确认位图（网络字节序）
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

#pragma pack(push, 1)

/* RELAY 模式包头 (9 bytes) */
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
    uint8_t             candidates_acked;           // 本次确认的候选数量
    uint8_t             reserved[2];
} p2p_relay_connect_ack_t;

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

/*
 * 平台无关的 IPv4 地址序列化格式（12 字节）
 *
 * struct sockaddr_in 在不同平台上布局不同，不可直接用于网络传输：
 *   Linux / Windows : sin_family(2B) + sin_port(2B) + sin_addr(4B) + sin_zero[8] = 16B
 *   macOS / BSD     : sin_len(1B) + sin_family(1B) + sin_port(2B) + sin_addr(4B) + sin_zero[8] = 16B
 *
 * 本结构统一序列化为 3×uint32_t（大端），与平台无关。
 * 转换函数见 p2p_internal.h：sockaddr_to_p2p_wire() / sockaddr_from_p2p_wire()
 */
typedef struct {
    uint32_t            family;                     // 地址族   htonl(sin_family)
    uint32_t            port;                       // 端口     htonl((uint32_t)sin_port)
    uint32_t            ip;                         // IPv4     sin_addr.s_addr
} p2p_sockaddr_t;

/*
 * ICE 候选地址序列化格式（32 字节，在 pack(1) 块内 sizeof == 32 在所有平台保证）
 *
 * 用于信令协议网络传输，各字段均以大端字节序存储。
 * 内部会话代码使用 p2p_candidate_entry_t（含 struct sockaddr_in），定义见 p2p_ice.h。
 * 转换函数：pack_candidate() / unpack_candidate()，见 p2p_internal.h
 *
 * 内存布局：
 *   ┌──────────┬────────────────────┬────────────────────┬──────────┐
 *   │ type(4B) │    addr (12B)      │  base_addr (12B)   │ prio(4B) │
 *   └──────────┴────────────────────┴────────────────────┴──────────┘
 */
typedef struct {
    uint32_t            type;                       // 候选类型 htonl(0=Host 1=Srflx 2=Relay 3=Prflx)
    p2p_sockaddr_t      addr;                       // 传输地址（12B）
    p2p_sockaddr_t      base_addr;                  // 基础地址（12B）
    uint32_t            priority;                   // 候选优先级 htonl
} p2p_candidate_t;

#pragma pack(pop)

#endif /* P2PP_H */
