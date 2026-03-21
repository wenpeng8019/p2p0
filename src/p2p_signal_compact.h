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
 *   - DATA/ACK/CRYPTO + P2P_DATA_FLAG_SESSION: 会话隔离和中继转发
 *
 * 候选列表序列化同步机制详见 p2pp.h（COMPACT 模式信令服务协议节）。
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
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include "predefine.h"
#include <p2p.h>

/* 前向声明 */
struct p2p_session;

/* ============================================================================
 * COMPACT 模式消息格式
 * ============================================================================
 *
 * 候选地址使用 p2p_candidate_t（定义在 p2pp.h），每个 23 字节。
 *
 * REGISTER:
 *   [local_peer_id(32)][remote_peer_id(32)][instance_id(4)][candidate_count(1)][candidates(N*7)]
 *   - instance_id: 客户端每次调用 connect() 时生成的 32 位随机数（参考 RTP SSRC）
 *     · 服务器用于区分"同一 peer_key 的重新注册"和"正常重传"：
 *       instance_id 相同 → 重传，幂等处理；
 *       instance_id 不同 → 客户端重启，服务器重置旧会话（清除 session_id、通知对端下线）
 *     · 初始值 0 不合法（服务器忽略），客户端必须保证非零
 *   注意：candidate_count 仅表示本次 REGISTER 包中的候选数量（受 UDP MTU 限制），
 *   不代表总候选数。即使服务器缓存能力足够，客户端也必须通过后续 PEER_INFO
 *   序列化传输剩余候选，并发送 FIN 包明确结束，否则对端无法判断是否还有更多候选。
 *
 * REGISTER_ACK:
 *   [status(1)][session_id(8)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)]
 *   status: 0=成功/对端离线, 1=成功/对端在线, >=2=错误码
 *   session_id: 本端会话 ID（网络字节序，64位，注册成功后立即分配）
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
 *      5) 若 probe_port == 0，结论为 P2P_NAT_UNDETECTABLE（无法探测）
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
 *    - DATA/ACK/CRYPTO + P2P_DATA_FLAG_SESSION：会话隔离和中继转发
 *
 * 4. 服务器索引：
 *    - 双索引机制：session_id（O(1)查找）+ peer_key（local_id+remote_id）
 *    - 通过 session_id 快速定位转发目标（无需遍历）
 *
 * PEER_INFO_ACK:
 *   [session_id(8)]
 *   包头: type=0x85, flags=0, seq=确认的 PEER_INFO 序列号
 *   - session_id: 会话 ID（网络字节序，64位，与对应的 PEER_INFO 一致）
 *   - seq: 确认的 PEER_INFO 序列号（seq=0 表示确认服务器下发的 PEER_INFO(seq=0)）
 *   - seq 窗口: 0..16（客户端接收端仅接受 1..16）
 *
 * PEER_INFO(seq=0) 的两种语义：
 *   - base_index=0: 服务器下发的首个候选列表（candidate_count>=1）
 *   - base_index!=0: 对端公网地址变更通知（candidate_count=1）
 *     - base_index 作为 8 位循环通知序号（1..255 循环）
 *     - 接收端按循环序比较，仅应用更新的通知；旧通知可忽略但仍需 ACK
 *
 * DATA/ACK/CRYPTO + P2P_DATA_FLAG_SESSION（会话隔离和中继转发）:
 *   [session_id(8)][payload(N)]
 *   包头: type=0x20/0x21/0x22, flags=0x01, seq=数据序列号
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - payload: 原始数据/ACK/DTLS 内容
 *   用于：
 *     1. 会话隔离：过滤旧会话重传的包（解决重连污染问题）
 *     2. 中继转发：服务器通过 session_id 查找目标对端
 */

/* PEER_INFO flags */
#define SIG_PEER_INFO_FIN  0x01     /* 候选列表发送完毕 */

/*
 * COMPACT 模式候选类型枚举
 * 与 ICE 的 p2p_ice_cand_type_t 数值对齐，用于 COMPACT 模式内部解析
 */
/* COMPACT 候选类型直接使用 p2p_common.h 中的 p2p_cand_type_t，无需独立定义 */

/* 信令状态 */
typedef enum {
    SIGNAL_COMPACT_INIT = 0,        /* 未启动 */
    SIGNAL_COMPACT_REGISTERING,     /* 等待 REGISTER_ACK */
    SIGNAL_COMPACT_REGISTERED,      /* 已注册，等待 PEER_INFO(seq=0) */
    SIGNAL_COMPACT_ICE,             /* 向对方发送后续候选队列、和 FIN */
    SIGNAL_COMPACT_READY            /* 如果已经完成向对方发送包括 FIN 在内的所有候选队列包，并得到确认 */
} p2p_signal_compact_state_t;

/* COMPACT 信令上下文 */
typedef struct {
    p2p_signal_compact_state_t state;                       /* 信令状态 */
    struct sockaddr_in  server_addr;                        /* 信令服务器地址 */
    char                local_peer_id[P2P_PEER_ID_MAX];     /* 本端 ID */
    char                remote_peer_id[P2P_PEER_ID_MAX];    /* 对端 ID */
    uint64_t            last_send_time;                     /* 上次发送时间 */
    uint64_t            last_recv_time;                     /* 上次收到时间 */

    /* REGISTER 重发控制（仅 REGISTERING 状态） */
    uint32_t            instance_id;                        /* 本次 connect() 生成的随机实例 ID（非零，参考 RTP SSRC）*/
    int                 register_attempts;                  /* REGISTER 总共尝试次数 */

    /* 和对方的会话 */
    uint64_t            session_id;                         /* 会话 ID（64位，0=尚未分配），在 REGISTER_ACK 中首次获得 */
    bool                peer_online;                        /* 对端是否在线；REGISTER_ACK 和 PEER_INFO 都会导致 online 为 true */

    /* REGISTER_ACK 返回的信息 */
    int                 candidates_cached;                  /* 提交到服务器缓存的本地候选队列数量 */
    struct sockaddr_in  public_addr;                        /* 本端的公网地址（服务器主端口探测到的）*/
    bool                relay_support;                      /* 服务器是否支持中继 */
    bool                msg_support;                        /* 服务器是否支持 RPC */
    uint16_t            probe_port;                         /* NAT 探测端口（0=不支持探测）*/

    /* PEER_INFO 序列化同步控制 */
    uint16_t            candidates_mask;                    /* 后续候选队列 seq 窗口 mask，用于全部完成确认，同时意味着最多发 16 个包 */
    uint16_t            candidates_acked;                   /* 后续候选队列对方确认的窗口 */
    uint16_t            trickle_idx_base;                   /* trickle 候选队列在 local_cands 中的起始索引 */
    uint8_t             trickle_seq_base;                   /* trickle 候选队列包的窗口 seq base（首批/trickle 分界） */
    uint8_t             trickle_seq_next;                   /* 下一个 trickle 包的 seq（累加目标，flush 后自增） */
    uint8_t             trickle_queue[17];                  /* trickle 候选队列包队列，记录每个包所携带的候选队列数量（1-based，索引 1-16） */
    uint64_t            trickle_last_pack_time;             /* 上次打包发送 trickle 包的时间，用于累积批发送 trickle 候选队列 */
    bool                remote_candidates_0;                /* 是否已收到 PEER_INFO seq=0（首批候选），该包由信令服务器维护 */
    uint16_t            remote_candidates_mask;             /* 对端候选队列 seq 窗口 mask，用于判断是否收到过某个 seq 的 PEER_INFO 包 */
    uint16_t            remote_candidates_done;             /* 对端候选队列 seq 窗口完成 mask，表示已收到过且确认过的 seq（即对端已应用） */
    uint8_t             remote_addr_notify_seq;             /* 最近一次已应用的地址变更通知序号（base_index，1..255） */

    /* NAT 类型探测（可选功能，仅当 probe_port > 0 时启用）*/
    struct sockaddr_in  probe_addr;                         /* 服务器探测端口观察到的映射地址 */
    uint64_t            nat_probe_send_time;                /* NAT_PROBE 最后发送时间（独立于 PEER_INFO 重传定时器）*/
    int16_t             nat_probe_retries;                  /* 失败重试次数（不包括首次执行）*/
    uint8_t             nat_is_port_consistent;             /* NAT 是否端口一致性（1=是，0=否）*/

    /* MSG RPC（A 端：发送请求） */
    uint8_t             req_state;                          /* 0=空闲 1=等待 REQ_ACK 2=等待 RESP */
    uint16_t            req_sid;                            /* 当前挂起的 rpc 序列号（0=无挂起）*/
    uint8_t             req_msg;                            /* 挂起请求的消息 ID */
    uint8_t             req_data[P2P_MSG_DATA_MAX];         /* 挂起请求的数据缓冲区 */
    int                 req_data_len;                       /* 挂起请求的数据长度 */
    uint64_t            req_send_time;                      /* MSG_REQ 最后发送时间 */
    int                 req_retries;                        /* 失败重试次数（不包括首次执行）*/

    /* MSG RPC（B 端：接收中转的请求，等待用户回应） */
    uint8_t             resp_state;                         /* 0=空闲 1=等待 RESP_ACK */
    uint16_t            resp_sid;                           /* 待回应的 rpc 序列号（0=无）*/
    uint64_t            resp_session_id;                    /* 待回应的 rpc 所属 session id */
    uint8_t             resp_code;                          /* 缓存的响应码 */
    uint8_t             resp_data[P2P_MSG_DATA_MAX];        /* 缓存的响应数据 */
    int                 resp_data_len;                      /* 缓存的响应长度 */
    uint64_t            resp_send_time;                     /* MSG_RESP 最后发送时间 */
    int                 resp_retries;                       /* 失败重试次数（不包括首次执行）*/

    uint16_t            rpc_last_sid;                       /* 最后完成的 sid（用于判断新旧请求，支持循环）*/

} p2p_signal_compact_ctx_t;

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_signal_compact_ctx_t *ctx);

/*
 * 信令服务周期维护（拉取阶段）— 注册重试、保活
 * 
 * 处理 REGISTERING/REGISTERED/READY 状态下的信令维护：
 * - REGISTERING：定期重发 REGISTER 包
 * - REGISTERED/READY：定期发送 keepalive 保持槽位
 * 
 * 在 p2p_update() 的阶段 2（信令拉取）中调用。
 * 
 * @param s   P2P 会话
 * @return    0 正常，-1 错误
 */
void p2p_signal_compact_tick_recv(struct p2p_session *s);

/*
 * 信令输出（推送阶段）— 向对端发送候选地址
 * 
 * 处理 ICE/READY 状态下向对端推送本地候选：
 * - ICE：定期重发剩余候选和 FIN
 * - READY：发送新收集到的候选（如果有）
 * 
 * 在 p2p_update() 的阶段 7（信令推送）中调用。
 * 
 * @param s   P2P 会话
 * @return    0 正常，-1 错误
 */
void p2p_signal_compact_tick_send(struct p2p_session *s);

/*
 * 根据 COMPACT 信令/探测状态推导并写入当前 NAT 检测结果
 * 每次 p2p_update() tick 时调用，用于同步 s->nat_type
 */
void p2p_signal_compact_nat_detect_tick(struct p2p_session *s);

//-----------------------------------------------------------------------------

/*
 * 开始信令交换（发送 REGISTER）
 *
 * @param s             会话对象（包含候选列表）
 * @param local_peer_id 本端 ID
 * @param remote_peer_id 对端 ID
 * @param server        服务器地址
 * @param verbose       是否输出详细日志
 * @return              =0 成功，!=0 错误码
 */
ret_t p2p_signal_compact_connect(struct p2p_session *s, const char *local_peer_id, const char *remote_peer_id,
                               const struct sockaddr_in *server);

/*
 * 结束信令交换（发送 UNREGISTER）
 *
 * @param s 会话对象
 * @return =0 成功，!=0 错误码
 */
ret_t p2p_signal_compact_disconnect(struct p2p_session *s);

/*
 * TURN Allocate 完成后，Trickle 发送中继候选到对端
 * 在已发窗口后追加 PEER_INFO + FIN，完成 COMPACT 模式的异步候选交换
 */
void p2p_signal_compact_trickle_turn(struct p2p_session *s);

/*
 * 通过 COMPACT 信令中转发送 REACH 包（NAT 打洞冷启动握手）
 *
 * @param s       会话对象
 * @param type    P2P 包类型
 * @param flags   flags 标志（接口内部会添加 P2P_DATA_FLAG_SESSION）
 * @param seq     序列号
 * @param payload 原始负载
 * @param payload_len 负载长度
 * @return        0=成功，!=0 错误码
 */
ret_t p2p_signal_compact_relay(struct p2p_session *s,
                               uint8_t type, uint8_t flags, uint16_t seq,
                               const void *payload, uint16_t payload_len);

/*
 * COMPACT 信令会话隔离验证（防止旧会话重传包污染新会话）
 * 
 * 验证 session_id 并跳过头部，返回 true 表示验证成功。
 * 成功后 payload 指针和 len 会被更新，指向去掉 session_id 后的数据。
 */
bool p2p_signal_compact_relay_validation(struct p2p_session *s,
                                         const uint8_t **payload, int *len,
                                         const char *proto_name);

/*
 * 通过信令代理服务向对端发送 MSG 请求（A 端）
 *
 * @param s     会话对象
 * @param msg   应用层消息类型（1 字节，应用自定义）
 * @param data  请求数据（最多 P2P_MSG_DATA_MAX 字节）
 * @param len   数据长度
 * @return      0=已加入发送队列，-1=失败（不支持/已有挂起/参数错误/未注册）
 */
ret_t p2p_signal_compact_request(struct p2p_session *s,
                                 uint8_t msg, const void *data, int len);

/*
 * 回复对端的 MSG 请求（B 端）。
 *
 * @param s     会话对象
 * @param code  应用层消息类型（1 字节，应用自定义）
 * @param data  回复数据（最多 P2P_MSG_DATA_MAX 字节）
 * @param len   数据长度
 * @return      0=已加入发送队列，-1=失败（参数错误/无挂起请求）
 */
ret_t p2p_signal_compact_response(struct p2p_session *s,
                                  uint8_t code, const void *data, int len);

//-----------------------------------------------------------------------------

/*
 * 处理收到的信令包（独立接口）
 * 
 * 以下函数分别处理不同类型的 COMPACT 信令包，消除包类型派发层。
 */

/* 处理 REGISTER_ACK（服务器注册确认） */
void compact_on_register_ack(struct p2p_session *s, uint16_t seq, uint8_t flags,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from);

/* 处理 ALIVE_ACK（保活确认） */
void compact_on_alive_ack(struct p2p_session *s, const struct sockaddr_in *from);

/* 处理 PEER_INFO（对端候选信息） */
void compact_on_peer_info(struct p2p_session *s, uint16_t seq, uint8_t flags,
                          const uint8_t *payload, int len,
                          const struct sockaddr_in *from);

/* 处理 PEER_INFO_ACK（对端候选确认） */
void compact_on_peer_info_ack(struct p2p_session *s, uint16_t seq,
                               const uint8_t *payload, int len,
                               const struct sockaddr_in *from);

/* 处理 PEER_OFF（对端离线通知） */
void compact_on_peer_off(struct p2p_session *s, const uint8_t *payload, int len,
                         const struct sockaddr_in *from);

/* 处理 MSG_REQ（可能是 A→Server 原始请求，也可能是 Server→B relay）*/
void compact_on_request(struct p2p_session *s, uint8_t flags,
                        const uint8_t *payload, int len,
                        const struct sockaddr_in *from);

/* 处理 MSG_REQ_ACK（Server→A，确认已缓存并开始中转）*/
void compact_on_request_ack(struct p2p_session *s,
                            const uint8_t *payload, int len,
                            const struct sockaddr_in *from);

/* 处理 MSG_RESP（Server→A relay B 的应答）*/
void compact_on_response(struct p2p_session *s, uint8_t flags,
                         const uint8_t *payload, int len,
                         const struct sockaddr_in *from);

/* 处理 MSG_RESP_ACK（Server 对 B 端响应的确认） */
void compact_on_response_ack(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from);

void compact_on_nat_probe_ack(struct p2p_session *s, uint16_t seq,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from);

///////////////////////////////////////////////////////////////////////////////

#pragma clang diagnostic pop
#endif /* P2P_SIGNAL_COMPACT_H */
