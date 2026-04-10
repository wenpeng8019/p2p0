/*
 * COMPACT 模式信令（UDP，单步注册 + 公网地址探测）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 基于 UDP 的轻量信令协议，通过服务器交换候选地址，完成 P2P 打洞。
 *
 * 与 RELAY（TCP 两阶段分离）相比，COMPACT 采用 UDP 无状态设计：
 *   - 无独立的 ONLINE 稳定态，ONLINE 兼含候选注册（embedded candidates）
 *   - SYNC0 取代 RELAY 的第二阶段请求，同样返回 session_id 和对端在线状态
 *   - AUTH_KEY（ONLINE_ACK 中分配）取代 TCP 长连接作为 client↔server 认证令牌
 *
 * 协议消息列表：
 *   - ONLINE:        客户端上线（注册本端 ID + 初始候选地址，获取 auth_key）
 *   - ONLINE_ACK:    服务器确认，返回 auth_key、公网地址、服务器能力
 *   - SYNC0:         建立与对端的会话（获取 session_id）
 *   - SYNC0_ACK:     服务器确认会话，返回 session_id 和对端在线状态
 *   - ALIVE:         保活心跳（维持 UDP 配对槽位）
 *   - SYNC:          双向候选传输（Client→Server 上传，Server→Client 下发）
 *   - SYNC_ACK:      候选接收确认（可靠传输控制）
 *   - OFFLINE:    主动下线，释放配对槽位，服务器通知对端 FIN
 *   - FIN:           服务器通知本端：对端已离线
 *   - NAT_PROBE:     NAT 类型探测（发往服务器探测端口，可选）
 *   - NAT_PROBE_ACK: 返回探测端口观察到的映射地址
 *
 * 协议详细格式参见 p2pp.h（COMPACT 模式信令协议节）。
 *
 * ============================================================================
 * 实例 ID 机制 (instance_id)
 * ============================================================================
 *
 * 每次调用 online() 时生成新的 32 位随机数 instance_id（参考 RTP SSRC）。
 * 服务器处理逻辑：
 *   - 相同 (peer_id, instance_id) → 重传，幂等处理
 *   - 相同 peer_id 但不同 instance_id → 客户端重启，重置旧会话，通知对端下线
 *
 * ============================================================================
 * 状态机（两阶段设计）
 * ============================================================================
 *
 *   阶段1: online() / offline()
 *   ┌────────────────────────────────────────────────────────────────────────────┐
 *   │  INIT ──→ WAIT_ONLINE_ACK ──→ ONLINE          ← disconnect() 回退到此     │
 *   │                           ↘ (connect() 已调用，ONLINE_ACK 直接到阶段2)    │
 *   └────────────────────────────────────────────────────────────────────────────┘
 *                              ↓ connect()（ONLINE 状态立即发 SYNC0）
 *   阶段2: connect() / disconnect()
 *   ┌──────────────────────────────────────────────────────────────────────────┐
 *   │  WAIT_SYNC0_ACK ──→ WAIT_PEER ──→ SYNCING ──→ READY                     │
 *   └──────────────────────────────────────────────────────────────────────────┘
 *
 *   - INIT:            未启动
 *   - WAIT_ONLINE_ACK: 已发送 ONLINE，等待 ONLINE_ACK（获取 auth_key）
 *   - ONLINE:          已收到 ONLINE_ACK（auth_key 有效），等待 connect() 触发 SYNC0
 *   - WAIT_SYNC0_ACK:  已发送 SYNC0，等待 SYNC0_ACK（获取 session_id）
 *   - WAIT_PEER:       已分配 session_id，等待对端上线（服务器下发 SYNC seq=0）
 *   - SYNCING:         候选同步中（接收/发送 SYNC）
 *   - READY:           候选同步完成，开始 P2P 打洞
 *
 * connect() 可先于 ONLINE_ACK 调用（仅存储 remote_peer_id），
 * 收到 ONLINE_ACK 后自动触发 SYNC0（懒触发，与 RELAY 模式一致）。
 *
 * ============================================================================
 * NAT 类型探测（可选）
 * ============================================================================
 *
 * 利用主端口和探测端口的两次映射观察，参考 RFC 5780 单 IP 简化模式：
 *   - ONLINE → ONLINE_ACK：主端口返回 Mapped_Addr1 + probe_port
 *   - NAT_PROBE → NAT_PROBE_ACK：探测端口返回 Mapped_Addr2（使用相同本地端口）
 *
 * 三分类判断：
 *   - Local_IP == Mapped_IP1          → OPEN（无 NAT）
 *   - Mapped_Port1 == Mapped_Port2    → CONE（端口一致性 NAT，含三种 Cone 子类型）
 *   - Mapped_Port1 != Mapped_Port2    → SYMMETRIC（端口随机 NAT）
 *   - probe_port == 0                 → UNDETECTABLE（服务器不支持探测）
 *
 * 探测在 WAIT_SYNC0_ACK 期间后台进行，超时不影响打洞主流程。
 *
 * ============================================================================
 * Trickle 候选支持
 * ============================================================================
 *
 * 候选列表分批上传（SYNC seq 1..16 窗口），支持：
 *   - HOST/公网候选随 ONLINE 首批嵌入上传（embedded candidates）
 *   - STUN/TURN 候选后续 trickle 补发
 *   - count=0 + FIN flag 标识发送结束
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责打包和发送。
 */
#ifndef P2P_SIGNAL_COMPACT_H
#define P2P_SIGNAL_COMPACT_H
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include "predefine.h"
#include <p2p.h>

/* 前向声明 */
struct p2p_session;
struct p2p_instance;

/* SYNC flags */
#define SIG_SYNC_FLAG_FIN  0x01     /* 候选列表发送完毕 */

/*
 * COMPACT 模式候选类型枚举
 * 与 ICE 的 p2p_ice_cand_type_t 数值对齐，用于 COMPACT 模式内部解析
 */
/* COMPACT 候选类型直接使用 p2p_common.h 中的 p2p_cand_type_t，无需独立定义 */

/* 信令状态 */
typedef enum {
    SIG_COMPACT_INIT = 0,                                   /* 未启动 */
    SIG_COMPACT_ERROR,                                      /* 错误状态，需重启 */
    SIG_COMPACT_WAIT_ONLINE_ACK,                            /* 已发送 ONLINE，等待 ONLINE_ACK */
    SIG_COMPACT_ONLINE,                                     /* 已上线, 收到 ONLINE_ACK */
} p2p_compact_st;

typedef struct {

    p2p_compact_st      state;                              /* 信令状态 */
    struct sockaddr_in  server_addr;                        /* 信令服务器地址 */
    uint64_t            last_send_time;                     /* 上次发送时间 */
    uint64_t            last_recv_time;                     /* 上次收到时间 */
    int                 sig_attempts;                       /* ONLINE 总共尝试次数 */
    int                 sig_sessions;                       /* 正在使用信令服务器的会话（sync0/sync），该值不为 0 则无需 keep-alive */

    /* 和服务器的会话 */
    char                local_peer_id[P2P_PEER_ID_MAX];     /* 本端 ID */
    uint32_t            instance_id;                        /* 本次 connect() 生成的随机实例 ID（非零，参考 RTP SSRC）*/
    uint64_t            auth_key;                           /* 客户端-服务器认证令牌（64位，0=尚未分配），在 ONLINE_ACK 中获得，用于 SYNC0/ALIVE */

    /* REGISTER_ACK 返回的信息 */
    uint8_t             max_candidates;                     /* 服务器允许缓存的最大候选数量 */
    bool                feature_relay;                      /* 服务器是否支持中继 */
    bool                feature_msg;                        /* 服务器是否支持 RPC */
    struct sockaddr_in  public_addr;                        /* 本端的公网地址（服务器主端口探测到的）*/
    uint16_t            probe_port;                         /* NAT 探测端口（0=不支持探测）*/

    /* NAT 类型探测（可选功能，仅当 probe_port > 0 时启用）*/
    struct sockaddr_in  probe_addr;                         /* 服务器探测端口观察到的映射地址 */
    uint64_t            nat_probe_send_time;                /* NAT_PROBE 最后发送时间（独立于 SYNC 重传定时器）*/
    int16_t             nat_probe_retries;                  /* 失败重试次数（不包括首次执行）*/
    uint8_t             nat_is_port_consistent;             /* NAT 是否端口一致性（1=是，0=否）*/

} p2p_compact_ctx_t;

typedef enum {
    SIG_COMPACT_SESS_SUSPENDED = 0,                         /* 挂起的 session，连接过程超时挂起。报错逻辑统一在 p2p_compact_ctx_t 中处理 */
    SIG_COMPACT_SESS_WAIT_SYNCABLE,                         /* 执行 connect() 创建了 session，但信令服务还未完成在线登录；或等待 stun 完成候选地址收集 */
    SIG_COMPACT_SESS_WAIT_SYNC0_ACK,                        /* 已发送 SYNC0，等待 SYNC0_ACK */
    SIG_COMPACT_SESS_WAIT_PEER,                             /* 已收到 SYNC0_ACK（获得 session_id）但 online=0，等待 PEER SYNC */
    SIG_COMPACT_SESS_SYNCING,                               /* 收到 PEER SYNC0 或 SYNC0_ACK online=1，向对方同步后续候选队列和 FIN */
    SIG_COMPACT_SESS_READY                                  /* 已完成向对方发送包括 FIN 在内的所有候选队列包，并得到确认 */
} p2p_compact_sess_st;

/* COMPACT 信令上下文 */
typedef struct {

    p2p_compact_sess_st state;                              /* 会话状态 */
    uint64_t            sync_send_time;                     /* 上次 SYNC0/SYNC 发送时间（用于重传控制）*/
    int                 sync_attempts;                      /* SYNC0/SYNC 总共尝试次数 */

    char                remote_peer_id[P2P_PEER_ID_MAX];    /* 对端 ID */

    /* 候选同步管理 */
    int                 candidates_cached;                  /* 提交到服务器缓存的本地候选队列数量（ONLINE_ACK 中 max_candidates 限制）*/
    uint16_t            candidates_mask;                    /* 后续候选队列 seq 窗口 mask，用于全部完成确认，同时意味着最多发 16 个包 */
    uint16_t            candidates_acked;                   /* 后续候选队列对方确认的窗口 */
    uint16_t            trickle_idx_base;                   /* trickle 候选队列在 local_cands 中的起始索引 */
    uint8_t             trickle_seq_base;                   /* trickle 候选队列包的窗口 seq base（首批/trickle 分界） */
    uint8_t             trickle_seq_next;                   /* 下一个 trickle 包的 seq（累加目标，flush 后自增） */
    uint8_t             trickle_queue[17];                  /* trickle 候选队列包队列，记录每个包所携带的候选队列数量（1-based，索引 1-16） */
    uint64_t            trickle_last_pack_time;             /* 上次打包发送 trickle 包的时间，用于累积批发送 trickle 候选队列 */
    bool                remote_candidates_0;                /* 是否已收到 SYNC seq=0（首批候选），该包由信令服务器维护 */
    uint16_t            remote_candidates_mask;             /* 对端候选队列 seq 窗口 mask，用于判断是否收到过某个 seq 的 SYNC 包 */
    uint16_t            remote_candidates_done;             /* 对端候选队列 seq 窗口完成 mask，表示已收到过且确认过的 seq（即对端已应用） */
    uint8_t             remote_addr_notify_seq;             /* 最近一次已应用的地址变更通知序号（1..255，0=从未收到）*/

    /* MSG RPC 上下文管理 */
    uint16_t            rpc_last_sid;                       /* 最后完成的 sid（用于判断新旧请求，支持循环）*/

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

} p2p_compact_session_t;

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_compact_ctx_t *ctx);

/*
 * 信令（首次）变为可 sync 状态
 */
void p2p_signal_compact_syncable(struct p2p_instance *inst, p2p_compact_ctx_t *ctx);

/*
 * 信令服务周期维护（拉取阶段）— 注册重试、保活
 * 
 * 处理各信令状态下的维护任务：
 * - WAIT_ONLINE_ACK：定期重发 ONLINE（获取 auth_key）
 * - WAIT_SYNC0_ACK：定期重发 SYNC0（获取 session_id）
 * - ONLINE/WAIT_PEER/SYNCING/READY：发送 ALIVE keepalive 保持服务器槽位
 * 
 * 在 p2p_update() 的阶段 2（信令拉取）中调用。
 * 
 * @param s   P2P 会话
 */
void p2p_signal_compact_tick_recv(struct p2p_instance *inst, uint64_t now);

/*
 * 信令输出（推送阶段）— 向对端发送候选地址
 * 
 * 处理 SYNCING/READY 状态下向对端推送本地候选：
 * - SYNCING：定期重发剩余候选和 FIN，直到对端全部确认（进入 READY）
 * - READY：发送新收集到的候选（如果有）
 * 
 * 在 p2p_update() 的阶段 7（信令推送）中调用。
 * 
 * @param s   P2P 会话
 */
void p2p_signal_compact_tick_send(struct p2p_instance *inst, uint64_t now);

/*
 * 根据 COMPACT 信令/探测状态推导并写入当前 NAT 检测结果
 * 每次 p2p_update() tick 时调用，用于同步 s->nat_type
 */
void p2p_signal_compact_nat_detect_tick(struct p2p_instance *inst, uint64_t now);

//-----------------------------------------------------------------------------

/*
 * 阶段1：客户端上线（发送 ONLINE，建立 client↔server 关系，获取 auth_key）
 *
 * @param s             会话对象
 * @param local_peer_id 本端 ID
 * @param server        服务器地址
 * @return              E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_compact_online(struct p2p_instance *inst, const char *local_peer_id,
                                const struct sockaddr_in *server);

/*
 * 阶段1：客户端下线（发送 OFFLINE，清理服务器上的全部状态，回到 INIT）
 *
 * @param s 会话对象
 * @return  E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_compact_offline(struct p2p_instance *inst);

//-----------------------------------------------------------------------------

/*
 * 阶段2：建立与对端的会话（发送 SYNC0，建立 client↔peer 关系，获取 session_id）
 *
 * 若尚未收到 ONLINE_ACK（WAIT_ONLINE_ACK 状态），将在收到后自动触发 SYNC0。
 * 前提：必须已经调用过 online()。
 *
 * @param s              会话对象
 * @param remote_peer_id 目标对端 ID
 * @return               E_NONE=成功，E_NONE_CONTEXT=未上线，E_BUSY=已连接其他对端，其他=错误码
 */
ret_t p2p_signal_compact_connect(struct p2p_session *s, const char *remote_peer_id);

/*
 * 阶段2：断开与对端的会话（发送 OFFLINE，清理 peer 会话，回到 ONLINE 等待下次 connect()）
 *
 * @param s 会话对象
 * @return  E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_compact_disconnect(struct p2p_session *s);

/*
 * 本地候选异步补发入口（支持 STUN/TURN）
 *
 * 若首批候选已发送，则将新增本地候选按 trickle 方式补发给对端
 * 若首批候选还未发送，则不做任何处理，此时新增候选会默认作为首批候选发送
 */
void p2p_signal_compact_trickle_candidate(struct p2p_session *s);

/*
 * 通过 COMPACT 信令中转发送 REACH 包（NAT 打洞冷启动握手）
 *
 * @param s       会话对象
 * @param type    P2P 包类型
 * @param flags   flags 标志（接口内部会添加 P2P_FLAG_SESSION）
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

void p2p_signal_compact_proto(struct p2p_instance *inst, uint8_t type, uint8_t flags, uint16_t seq,
                              uint8_t *payload, int payload_len, uint64_t now);


///////////////////////////////////////////////////////////////////////////////

#pragma clang diagnostic pop
#endif /* P2P_SIGNAL_COMPACT_H */
