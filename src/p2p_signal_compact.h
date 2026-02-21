/*
 * COMPACT 模式信令（UDP, 缓存配对机制 + 公网地址探测）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 实现简单的 UDP 信令协议，用于交换对端地址信息，包括双方的公网地址：
 *   - REGISTER:      向服务器注册自己的 ID 和初始候选地址
 *   - REGISTER_ACK:  服务器确认，返回对端状态和缓存能力
 *   - PEER_INFO:     序列化候选同步包（服务器首发 seq=0 并分配 session_id，后续 P2P 传输）
 *   - PEER_INFO_ACK: 候选接收确认，用于可靠传输控制
 *   - NAT_PROBE:     NAT 类型探测请求（可选，发往服务器探测端口）
 *   - NAT_PROBE_ACK: NAT 探测响应，返回第二次映射地址
 *   - RELAY_DATA:    中继数据转发（P2P 打洞失败时的降级方案）
 *   - RELAY_ACK:     中继数据确认
 *
 * ============================================================================
 * 候选列表序列化同步机制
 * ============================================================================
 *
 * 由于 UDP 包大小限制，候选列表需要分批传输。本实现通过序列化的
 * PEER_INFO 包完成可靠同步：
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
 *      - 客户端收到后发送 PEER_INFO_ACK（携带 session_id） 确认
 *      - 客户端通过 PEER_INFO(seq=1,2,3,...) 继续同步剩余候选（携带 session_id）
 *      - 对端通过 PEER_INFO_ACK 确认，未确认则重发
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
 * ============================================================================
 * 状态机
 * ============================================================================
 *
 *   IDLE ──→ REGISTERING ──→ REGISTERED ──→ READY
 *                     │                           │
 *                     └───────────────────────────┘
 *                         (收到 PEER_INFO seq=0)
 *
 *   - IDLE:        未启动
 *   - REGISTERING: 已发送 REGISTER，等待 REGISTER_ACK
 *   - REGISTERED:  已收到 ACK，等待服务器 PEER_INFO(seq=0)（首次分配 session_id）
 *   - READY:       已收到 PEER_INFO 和 session_id，开始打洞并继续同步剩余候选
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责序列化和发送。
 */

#ifndef P2P_SIGNAL_COMPACT_H
#define P2P_SIGNAL_COMPACT_H

#include <p2p.h>

/* 前向声明 */
struct p2p_session;

/* ============================================================================
 * COMPACT 模式消息格式
 * ============================================================================
 *
 * 候选地址使用 p2p_compact_candidate_t（定义在 p2pp.h），每个 7 字节。
 *
 * REGISTER:
 *   [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *   注意：candidate_count 仅表示本次 REGISTER 包中的候选数量（受 UDP MTU 限制），
 *   不代表总候选数。即使服务器缓存能力足够，客户端也必须通过后续 PEER_INFO
 *   序列化传输剩余候选，并发送 FIN 包明确结束，否则对端无法判断是否还有更多候选。
 *
 * REGISTER_ACK:
 *   [status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
 *   status: 0=成功/对端离线, 1=成功/对端在线, >=2=错误码
 *   max_candidates: 服务器为对端缓存的最大候选数量（0=不支持缓存）
 *   public_ip/port: 客户端的公网地址（服务器主端口观察到的 UDP 源地址）
 *   probe_port: NAT 探测端口号（0=不支持探测，>0=探测端口）
 *
 * PEER_INFO (seq 字段在包头 hdr.seq):
 *   [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
 *   - session_id: 会话 ID（网络字节序，64位）
 *     · seq=0: 服务器发送，session_id 由服务器生成（首次分配）
 *     · seq>0: 客户端发送，session_id 使用服务器分配的值
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（FIN）
 *   - seq=0: 服务器发送，base_index=0，包含缓存的候选，首次分配 session_id
 *   - seq>0: 客户端/服务器继续同步剩余候选，使用已分配的 session_id
 *   - flags: 可包含 FIN 标志（0x01）表示候选列表发送完毕
 *
 * NAT_PROBE (客户端 → 服务器探测端口):
 *   payload: 空（无需额外字段）
 *   包头: type=0x86, flags=0, seq=客户端分配的请求号
 *   - seq 字段可用于匹配响应，客户端可递增或随机分配
 *   - 客户端收到 REGISTER_ACK 后，若 probe_port > 0，则向该端口发送此包
 *
 * NAT_PROBE_ACK (服务器探测端口 → 客户端):
 *   [probe_ip(4)][probe_port(2)]
 *   包头: type=0x87, flags=0, seq=对应的 NAT_PROBE 请求 seq
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *   - seq: 复制请求包的 seq，用于客户端匹配响应
 *
 * UNREGISTER (客户端 → 服务器):
 *   [local_peer_id(32)][remote_peer_id(32)]
 *   包头: type=0x88, flags=0, seq=0
 *   客户端主动断开时发送，请求服务器立即释放配对槽位
 *   服务器收到后会向对端发送 PEER_OFF 通知
 *
 * PEER_OFF (服务器 → 客户端，下行通知):
 *   [session_id(8)]
 *   包头: type=0x89, flags=0, seq=0
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 *   服务器通知客户端：对端已主动断开或离线
 *   客户端收到后应停止该会话的所有传输和重传，清理相关资源
 *
 * ============================================================================
 * NAT 类型探测方案
 * ============================================================================
 *
 * 利用 REGISTER 和 NAT_PROBE 两次通讯，参考 STUN RFC 5389/5780，探测 NAT 类型：
 *
 * 1. 服务器配置：
 *    - 主端口（如 3478）：处理 REGISTER/PEER_INFO 等主要协议
 *    - 探测端口（如 3479）：仅处理 NAT_PROBE，可选功能
 *    - 若不配置探测端口，则 REGISTER_ACK 中 probe_port = 0
 *
 * 2. 第一次通讯（REGISTER → REGISTER_ACK，主端口）：
 *    - 客户端从本地地址 Local_IP:Local_Port 向主端口发送 REGISTER
 *    - 服务器主端口观察到源地址 Mapped_IP1:Mapped_Port1
 *    - REGISTER_ACK 返回：
 *      · public_ip/public_port = Mapped_IP1:Mapped_Port1（主端口映射）
 *      · probe_port = 3479（探测端口号，0 表示不支持）
 *
 * 3. 第二次通讯（NAT_PROBE → NAT_PROBE_ACK，探测端口）：
 *    - 客户端收到 REGISTER_ACK 后，若 probe_port > 0，则：
 *      · 向 Server_IP:probe_port 发送 NAT_PROBE（使用相同的本地端口）
 *      · 类似 STUN 的 Change Port Request
 *    - 服务器探测端口观察到源地址 Mapped_IP2:Mapped_Port2
 *    - NAT_PROBE_ACK 返回 probe_ip/probe_port = Mapped_IP2:Mapped_Port2
 *
 * 4. NAT 类型判断（三分类模型）：
 *
 *    ┌─────────────────────────────────────────────────────────────────────┐
 *    │ 条件                                    │ 结论                       │
 *    ├─────────────────────────────────────────────────────────────────────┤
 *    │ Local_IP == Mapped_IP1                  │ OPEN（无 NAT，公网直连）   │
 *    │ Mapped_Port1 == Mapped_Port2            │ CONE（端口一致性 NAT）     │
 *    │ Mapped_Port1 != Mapped_Port2            │ SYMMETRIC（端口随机 NAT）  │
 *    └─────────────────────────────────────────────────────────────────────┘
 *
 *    OPEN：
 *      - 本地 IP 与服务器观察到的源 IP 一致，无地址转换
 *      - P2P 直连无障碍
 *
 *    CONE（端口一致性 NAT）：
 *      - 同一本地端口对不同目标（主端口/探测端口）映射外部端口相同
 *      - 对应 RFC 3489 中的 Full Cone / Restricted Cone / Port Restricted Cone
 *      - 由于 COMPACT 使用单 IP 服务器，无法通过 CHANGE-IP 测试区分三种子类型
 *      - P2P 打洞成功率高（约 80–95%，取决于对端类型）
 *      - 报告为 P2P_NAT_FULL_CONE（最乐观估计，打洞策略相同）
 *
 *    SYMMETRIC（端口随机 NAT）：
 *      - 同一本地端口对不同目标映射的外部端口不同
 *      - 对应 RFC 3489 中的 Symmetric NAT
 *      - P2P 打洞成功率低（需端口预测或依赖中继）
 *      - 报告为 P2P_NAT_SYMMETRIC
 *
 * 5. 与 RFC 3489 全量检测的对比：
 *
 *    RFC 3489 完整流程需要服务器拥有两个独立 IP（双 IP 服务器）：
 *      Test I:   获取 Mapped_Addr
 *      Test II:  CHANGE-IP + CHANGE-PORT → 识别 Full Cone
 *      Test III: CHANGE-PORT only       → 区分 Restricted / Port Restricted
 *
 *    COMPACT 是单 IP 服务器模式，CHANGE-IP 测试天然不可能，因此：
 *
 *    ┌───────────────────┬───────────────┬───────────────────────────────────┐
 *    │ RFC 3489 类型      │ COMPACT 结论  │ 原因                               │
 *    ├───────────────────┼───────────────┼───────────────────────────────────┤
 *    │ Open (No NAT)     │ OPEN          │ ✅ 可精确识别（本地 IP = 映射 IP） │
 *    │ Full Cone         │ CONE          │ ✅ 端口一致性可判断               │
 *    │ Restricted Cone   │ CONE          │ ⚠️ 无法与 Full Cone 区分          │
 *    │ Port Restricted   │ CONE          │ ⚠️ 无法与 Full Cone 区分          │
 *    │ Symmetric         │ SYMMETRIC     │ ✅ 端口随机性可判断               │
 *    │ UDP Blocked       │ TIMEOUT       │ ✅ 注册/探测超时推断              │
 *    └───────────────────┴───────────────┴───────────────────────────────────┘
 *
 *    三种 Cone 子类型在 P2P 打洞策略上并无本质差异（都需要对端先打洞），
 *    故此处合并为 CONE 已满足实际需求。
 *
 * 6. 实现要点：
 *    - 服务器配置：主端口必选，探测端口可选（建议与主端口同 IP 不同端口）
 *    - 客户端流程：
 *      1) 发送 REGISTER 到主端口，获得 Mapped_Addr1 + probe_port
 *      2) 若 probe_port > 0，发送 NAT_PROBE 到探测端口（使用包头 seq 匹配响应）
 *      3) 收到 NAT_PROBE_ACK，获得 Mapped_Addr2
 *      4) 比较 Mapped_Port1 vs Mapped_Port2，写入 nat_detected_result
 *      5) 若 probe_port == 0，结论为 P2P_NAT_UNSUPPORTED（无法探测）
 *    - 整个探测在 REGISTERED 状态完成（等待 PEER_INFO 期间，不阻塞主流程）
 *    - 探测失败（超时）不影响 P2P 打洞，降级为普通打洞策略
 *
 * 7. 优化建议：
 *    - 探测结果可缓存（如 5 分钟），避免每次连接都重新探测
 *    - NAT_PROBE 可设置超时（如 500ms × 3 次重试），快速失败
 *    - 探测端口可与主端口同进程监听（通过 SO_REUSEPORT 或 select 多路复用）
 *
 * ============================================================================
 * 会话标识机制（session_id）
 * ============================================================================
 *
 * 为了支持同一对端的多个并发连接，服务器在双方首次配对成功时为每个方向分配
 * 唯一的 64 位会话 ID（session_id）：
 *
 * 1. 分配时机：
 *    - 服务器检测到双方都注册成功（REGISTER 都到达）
 *    - 服务器向双方发送 PEER_INFO(seq=0) 时，首次分配 session_id
 *    - 每个方向独立分配（A→B 和 B→A 的 session_id 不同）
 *
 * 2. session_id 生成策略：
 *    - 64 位加密安全随机数（使用 arc4random/rand_s/urandom）
 *    - 冲突概率：1/2^64 ≈ 5.4×10^-20（几乎不可能）
 *    - 安全性：无法预测，防止跨会话注入攻击
 *
 * 3. 使用场景：
 *    - PEER_INFO(seq>0)：客户端继续同步候选时，携带 session_id
 *    - PEER_INFO_ACK：客户端确认收到 PEER_INFO，携带 session_id
 *    - RELAY_DATA：P2P 打洞失败后，通过服务器中继转发，用 session_id 查找目标
 *    - RELAY_ACK：中继数据确认，携带 session_id
 *
 * 4. 服务器索引：
 *    - 双索引机制：session_id（O(1)查找）+ peer_key（local_id+remote_id）
 *    - 通过 session_id 快速定位转发目标（无需遍历）
 *
 * PEER_INFO_ACK:
 *   [session_id(8)][ack_seq(2)]
 *   - session_id: 会话 ID（网络字节序，64位，与对应的 PEER_INFO 一致）
 *   - ack_seq: 确认的 PEER_INFO 序列号（网络字节序）
 *
 * RELAY_DATA（P2P 打洞失败后的中继转发）:
 *   [session_id(8)][data_len(2)][data(N)]
 *   包头: type=0xA0, flags=0, seq=数据序列号
 *   - session_id: 会话 ID（网络字节序，64位，用于服务器查找目标对端）
 *   - data_len: 数据长度（网络字节序）
 *   - data: 实际数据内容
 *   用于在 P2P 打洞失败后，通过服务器中继转发数据
 *
 * RELAY_ACK:
 *   [session_id(8)][ack_seq(2)]
 *   包头: type=0xA1, flags=0, seq=0
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - ack_seq: 确认的 RELAY_DATA 序列号（网络字节序）
 */

/* PEER_INFO flags */
#define SIG_PEER_INFO_FIN  0x01     /* 候选列表发送完毕 */

/* 信令状态 */
enum {
    SIGNAL_COMPACT_IDLE = 0,         /* 未启动 */
    SIGNAL_COMPACT_REGISTERING,      /* 等待 REGISTER_ACK */
    SIGNAL_COMPACT_REGISTERED,      /* 已注册，等待 PEER_INFO(seq=0) */
    SIGNAL_COMPACT_READY             /* 已收到 PEER_INFO，开始打洞并同步剩余候选 */
};

/* COMPACT 信令上下文 */
typedef struct {
    int                 state;                              /* 信令状态 */
    struct sockaddr_in  server_addr;                        /* 信令服务器地址 */
    char                local_peer_id[P2P_PEER_ID_MAX];     /* 本端 ID */
    char                remote_peer_id[P2P_PEER_ID_MAX];    /* 对端 ID */
    uint64_t            last_send_time;                     /* 上次发送时间 */
    int                 verbose;                            /* 是否输出详细日志 */
    
    /* REGISTER_ACK 返回的信息 */
    uint8_t             peer_online;                        /* 对端是否在线 */
    uint8_t             max_remote_candidates;              /* 服务器为对端缓存的最大候选数（0=不支持） */
    uint8_t             relay_support;                      /* 服务器是否支持中继（0=不支持, 1=支持）*/
    struct sockaddr_in  public_addr;                        /* 本端的公网地址（服务器主端口探测到的）*/
    uint16_t            probe_port;                         /* NAT 探测端口（0=不支持探测）*/
    
    /* 会话标识（服务器在首次 PEER_INFO(seq=0) 时分配） */
    uint64_t            session_id;                         /* 会话 ID（64位，0=尚未分配）*/
    
    /* NAT 类型探测（可选功能，仅当 probe_port > 0 时启用）*/
    struct sockaddr_in  probe_addr;                         /* 服务器探测端口观察到的映射地址 */
    uint8_t             nat_type_detected;                  /* NAT 类型是否已探测 */
    uint8_t             nat_is_port_consistent;             /* NAT 是否端口一致性（1=是，0=否）*/
    uint16_t            nat_probe_request_seq;              /* NAT_PROBE 请求序列号（使用包头 seq 字段）*/
    uint8_t             nat_probe_retries;                  /* NAT_PROBE 已发次数（0=尚未发送，最多 3 次）*/
    int                 nat_detected_result;                /* NAT 类型探测结论（p2p_nat_type_t，0=未探测）*/
    uint64_t            nat_probe_send_time;                /* NAT_PROBE 最后发送时间（独立于 PEER_INFO 重传定时器）*/
    
    /* REGISTER 重发控制（仅 REGISTERING 状态） */
    int                 register_attempts;                  /* REGISTER 重发次数 */
    
    /* PEER_INFO 序列化同步控制 */
    int                 candidates_per_packet;              /* 每个 PEER_INFO 包的候选数量 */
    int                 candidates_sent;                    /* 已发送的候选总数（base_index） */
    uint16_t            next_send_seq;                      /* 下一个要发送的 PEER_INFO 序列号 */
    uint16_t            last_acked_seq;                     /* 对方已确认的最大序列号 */
    uint8_t             remote_recv_complete;               /* 对端已接收完所有候选 */
    uint16_t            last_recv_seq;                      /* 已收到的最大 PEER_INFO 序列号 */
    uint8_t             local_send_complete;                /* 本端已发送完所有候选 */
} p2p_signal_compact_ctx_t;

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_signal_compact_ctx_t *ctx);

/*
 * 开始信令交换（发送 REGISTER）
 *
 * @param s             会话对象（包含候选列表）
 * @param local_peer_id 本端 ID
 * @param remote_peer_id 对端 ID
 * @param server        服务器地址
 * @param verbose       是否输出详细日志
 * @return              0 成功，-1 失败
 */
int p2p_signal_compact_start(struct p2p_session *s, const char *local_peer_id, const char *remote_peer_id,
                             const struct sockaddr_in *server, int verbose);

/*
 * 周期调用，处理重发和候选同步
 *
 * - REGISTERING 状态：重发 REGISTER（直到收到 ACK）
 * - READY 状态：序列化发送剩余候选（PEER_INFO seq>1），处理重传
 *
 * @param s   会话对象
 * @return    0 正常，-1 错误
 */
int p2p_signal_compact_tick(struct p2p_session *s);

/*
 * 处理收到的信令包
 *
 * 支持的包类型：
 * - REGISTER_ACK:   服务器确认，提取对端状态、缓存能力、公网地址、探测端口
 * - PEER_INFO:      对端候选列表（序列化传输）
 * - PEER_INFO_ACK:  对端确认（停止重传对应序列号的包）
 * - NAT_PROBE_ACK:  NAT 探测响应（包含探测端口观察到的映射地址）
 *
 * @param s       会话对象
 * @param type    包类型
 * @param seq     包序列号（从包头提取）
 * @param flags   包标志位（从包头提取）
 * @param payload 负载数据
 * @param len     负载长度
 * @param from    发送方地址
 * @return        0 成功处理，-1 解析失败，1 未处理
 */
int p2p_signal_compact_on_packet(struct p2p_session *s, uint8_t type, uint16_t seq, uint8_t flags,
                                 const uint8_t *payload, int len,
                                 const struct sockaddr_in *from);

/*
 * 根据 COMPACT 信令/探测状态推导并写入当前 NAT 检测结果
 * 每次 p2p_update() tick 时调用，用于同步 s->nat_type
 */
void p2p_signal_compact_nat_detect_tick(struct p2p_session *s);

#endif /* P2P_SIGNAL_COMPACT_H */

