/*
 * RELAY 模式信令（TCP，服务器中转 ICE 候选）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 实现基于 TCP 的信令协议，通过服务器中转交换 ICE 候选信息。
 *
 * 与 COMPACT 模式的核心区别：
 *
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ COMPACT 模式：一步式配对注册（UDP 无状态设计）                        │
 * │                                                                      │
 * │   REGISTER(local_id, remote_id, candidates)                         │
 * │       ↓                                                              │
 * │   服务器建立 pair(A↔B)，等待双方注册成对                             │
 * │       ↓                                                              │
 * │   REGISTER_ACK(peer_online?, session_id, server_features)           │
 * │                                                                      │
 * │   特点：REGISTER 一次完成三方关系（自己-服务器-对方）                 │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ RELAY 模式：两阶段分离设计（TCP 有状态设计）                          │
 * │                                                                      │
 * │   阶段1: ONLINE(my_name, instance_id)                                │
 * │       ↓                                                              │
 * │   ONLINE_ACK(server_features)  ← 仅建立"客户端-服务器"连接           │
 * │                                                                      │
 * │   阶段2: CONNECT(target_name)                                        │
 * │       ↓                                                              │
 * │   CONNECT_ACK(peer_status, session_id)  ← 建立"我-对方"会话          │
 * │                                                                      │
 * │   特点1：分离设计，ONLINE 后可发起多个 CONNECT，支持并发会话         │
 * │   特点2：instance_id 机制允许 TCP 重连但保持数据上下文（会话可恢复） │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * 协议消息列表：
 *   - ONLINE:        客户端上线（建立客户端-服务器连接）
 *   - ONLINE_ACK:    服务器确认上线，返回服务器能力标志
 *   - ALIVE:         客户端心跳保活（维持 TCP 长连接）
 *   - CONNECT:       请求建立与对端的会话（申请连接对方）
 *   - CONNECT_ACK:   服务器确认会话，返回 session_id 和对端在线状态
 *   - DISCONNECT:    主动断开与对端会话（按 session_id）
 *   - PEER_INFO:     双向候选传输（Client→Server 上传，Server→Client 下发）
 *   - PEER_INFO_ACK: 服务器确认候选，返回转发/缓存状态
 *   - DATA:          P2P 失败后的数据包中继（可选）
 *   - ACK:           P2P 失败后的确认包中继（可选）
 *   - CRYPTO:        P2P 失败后的加密包中继（可选）
 *
 * 协议详细格式参见 p2pp.h（RELAY 模式信令协议）。
 *
 * ============================================================================
 * 实例 ID 机制 (instance_id)
 * ============================================================================
 *
 * 每次调用 online() 时，客户端生成新的 32 位随机数 instance_id（参考 RTP SSRC）。
 * instance_id 与 name 一起在 ONLINE 消息中发送给服务器，用于标识客户端连接实例。
 *
 * 服务器处理逻辑：
 *   - 相同的 (name, instance_id) → 重传，幂等处理
 *   - 相同 name 但不同 instance_id → 客户端重连，服务器可以选择：
 *     · 保持数据上下文（允许会话恢复，不立即清理 session_id）
 *     · 重置数据上下文（清除旧会话，通知对端下线）
 *
 * 这种机制允许：
 *   1. TCP 连接断开后，客户端可以重新 online() 并恢复之前的会话状态
 *   2. 服务器可以根据 instance_id 区分"重传"和"重连"，避免误判
 *   3. 支持更灵活的断线重连策略，提高网络不稳定时的可用性
 *
 * ============================================================================
 * 状态机（两阶段设计）
 * ============================================================================
 *
 *   阶段1: 客户端上线（建立客户端-服务器连接）
 *   ┌────────────────────────────────────────────────────┐
 *   │  INIT ──→ ONLINE_ING ──→ WAIT_ONLINE_ACK ──→ ONLINE │
 *   └────────────────────────────────────────────────────┘
 *                                    ↓
 *   阶段2: 建立会话（申请连接对方，进行候选交换）
 *   ┌────────────────────────────────────────────────────┐
 *   │  ONLINE ──→ WAIT_CONNECT_ACK ──→ WAIT_PEER ──→ EXCHANGING ──→ READY │
 *   └────────────────────────────────────────────────────┘
 *
 *   - INIT:              未启动
 *   - ONLINE_ING:        TCP 连接建立中
 *   - WAIT_ONLINE_ACK:   已发送 ONLINE，等待 ONLINE_ACK
 *   - ONLINE:            已上线，可以发起多个 CONNECT（核心状态）
 *   - WAIT_CONNECT_ACK:  已发送 CONNECT，等待 CONNECT_ACK（分配 session_id）
 *   - WAIT_PEER:         已分配 session_id，但对端离线，等待首个 PEER_INFO
 *   - EXCHANGING:        候选交换中（上传/接收 PEER_INFO）
 *   - READY:             候选交换完成，可以开始 P2P 打洞
 *
 * 注意：ONLINE 状态是稳定状态，可以在此状态下发起多个 CONNECT，
 *       从而支持与多个对端并发建立会话（每个会话有独立的 session_id）。
 *
 * ============================================================================
 * TCP 粘包处理
 * ============================================================================
 *
 * TCP 是流式协议，需要状态机解包：
 *
 *   1. RECV_HEADER:  读取包头（sizeof(p2p_relay_hdr_t) 字节）
 *   2. RECV_PAYLOAD: 根据包头的 payload_len 读取负载
 *   3. 循环处理直到 EAGAIN
 *
 * 接收缓冲区：
 *   - hdr_buf[sizeof(p2p_relay_hdr_t)]: 包头缓冲区
 *   - payload:        动态分配的负载缓冲区
 *   - offset:         当前读取偏移
 *   - expected:       期待的数据长度
 *
 * ============================================================================
 * Trickle ICE 支持
 * ============================================================================
 *
 * 候选列表分批上传，支持：
 *   - 初始候选（HOST）立即上传
 *   - TURN 候选后续 trickle 上传
 *   - count=0 发送 FIN 标识结束
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责打包和发送。
 */
#ifndef P2P_SIGNAL_RELAY_H
#define P2P_SIGNAL_RELAY_H
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include "predefine.h"
#include <p2pp.h>

/* 前向声明 */
struct p2p_session;

/* ============================================================================
 * RELAY 模式参数配置
 * ============================================================================ */

#define P2P_RELAY_HEARTBEAT_INTERVAL_MS     20000   /* 心跳间隔（毫秒）*/
#define P2P_RELAY_ACK_TIMEOUT_MS            5000    /* ACK 响应超时（毫秒）*/
#define P2P_RELAY_TRICKLE_BATCH_MS          1000    /* Trickle 攒批窗口（毫秒）*/
#define P2P_RELAY_MAX_CANDS_PER_PACKET      10      /* 每包最大候选数 */

/* ============================================================================
 * RELAY 信令状态
 * ============================================================================ */

typedef enum {
    SIGNAL_RELAY_INIT = 0,          /* 未启动 */
    SIGNAL_RELAY_ERROR,              /* 错误状态 */
    SIGNAL_RELAY_ONLINE_ING,        /* TCP 连接建立中 */
    SIGNAL_RELAY_WAIT_ONLINE_ACK,   /* 等待 ONLINE_ACK */
    SIGNAL_RELAY_ONLINE,            /* 已上线 */
    SIGNAL_RELAY_WAIT_CONNECT_ACK,  /* 等待 CONNECT_ACK */
    SIGNAL_RELAY_WAIT_PEER,         /* 已分配会话，等待对端上线 */
    SIGNAL_RELAY_EXCHANGING,        /* 候选交换中 */
    SIGNAL_RELAY_READY              /* 交换完成，对端候选接收完成 */
} relay_state_t;

/* ============================================================================
 * TCP 接收状态机
 * ============================================================================ */

typedef enum {
    RECV_STATE_HEADER = 0,          /* 读取包头（sizeof(p2p_relay_hdr_t) 字节）*/
    RECV_STATE_PAYLOAD              /* 读取负载（变长）*/
} relay_recv_state_t;

/* ============================================================================
 * 发送 chunk（内存块 + 队列 + 回收机制）
 * ============================================================================ */

/* 发送 chunk（固定大小的内存块）*/
typedef struct p2p_send_chunk {
    uint8_t data[P2P_MTU];              /* 数据缓冲区 (1200 字节) */
    int len;                            /* 有效数据长度 */
    struct p2p_send_chunk *next;        /* 链表指针（用于队列和回收池）*/
} p2p_send_chunk_t;

/* ============================================================================
 * RELAY 信令上下文
 * ============================================================================ */

typedef struct {
    /* 基础状态 */
    relay_state_t       state;                          /* 信令状态 */
    sock_t              sockfd;                         /* TCP socket */
    struct sockaddr_in  server_addr;                    /* 服务器地址 */
    uint64_t            last_send_time;                 /* 上次发送时间 */
    uint64_t            last_recv_time;                 /* 上次接收时间 */
    uint64_t            heartbeat_time;                 /* 上次心跳时间 */

    /* 身份标识 */
    uint32_t            instance_id;                    /* 本次 online() 生成的实例 ID（参考 RTP SSRC）*/
    char                local_peer_id[P2P_PEER_ID_MAX]; /* 本端名称 */
    char                remote_peer_id[P2P_PEER_ID_MAX];/* 目标名称 */
    bool                connected;                      /* 是否存在 connect 意图/会话（用于幂等和异步触发） */

    /* 会话管理 */
    uint64_t            session_id;                     /* 会话 ID（0=未分配）*/
    bool                peer_online;                    /* 对端是否在线 */

    /* 服务器能力（ONLINE_ACK 返回）*/
    bool                feature_relay;                  /* 支持数据包中继 */
    bool                feature_msg;                    /* 支持 RPC 机制 */
    uint8_t             candidate_relay_max;            /* 服务器允许的单包最大候选数（0=使用本地默认）*/

    /* Trickle ICE 控制 */
    uint16_t            next_candidate_index;           /* 下一个要发送的候选索引 */
    bool                local_candidates_fin;           /* FIN 已发送（本端候选上传完成）*/
    bool                remote_candidates_fin;          /* 对端候选接收完成（收到对端 FIN）*/
    bool                awaiting_peer_info_ack;         /* 等待 PEER_INFO_ACK（流控门控）*/
    bool                local_delivery_confirmed;       /* 服务器确认所有候选已转发到对端（收到 ACK=0）*/
    uint8_t             last_sent_cand_count;           /* 上批 PEER_INFO 发送的候选数（用于 ACK 对账）*/
    uint64_t            trickle_last_time;              /* 上次 trickle 发送时间（用于攒批窗口控制）*/
    uint8_t             trickle_batch_count;            /* 当前批次累积的候选数（实现攒批）*/

    /* TCP 接收状态机 */
    relay_recv_state_t  recv_state;                     /* 接收状态 */
    uint8_t             hdr_buf[sizeof(p2p_relay_hdr_t)]; /* 包头缓冲区 */
    p2p_relay_hdr_t     hdr;                            /* 解析后的包头 */
    uint8_t             payload[P2P_MAX_PAYLOAD];       /* 负载缓冲区（固定 1196 字节）*/
    uint16_t            offset;                         /* 当前读取偏移 */
    
    /* 发送队列 */
    p2p_send_chunk_t   *send_queue_head;                /* 发送队列头（也是正在发送的chunk）*/
    p2p_send_chunk_t   *send_queue_tail;                /* 发送队列尾 */
    int                 send_offset;                    /* 当前 chunk 的已发送偏移 */

    /* 发送 chunk 回收池（动态内存 + 链表）*/
    p2p_send_chunk_t   *chunk_free_list;                /* chunk 回收链表头 */
    int                 chunk_free_count;               /* chunk 回收数量 */

} p2p_signal_relay_ctx_t;

/* ============================================================================
 * RELAY 信令 API
 * ============================================================================ */

/*
 * 初始化 RELAY 信令上下文
 */
void p2p_signal_relay_init(p2p_signal_relay_ctx_t *ctx);

/*
 * 客户端上线（阶段1：建立与服务器的 TCP 连接）
 *
 * 创建 TCP socket，连接到 RELAY 服务器，并发送 ONLINE 消息。
 * 成功后进入 ONLINE 状态，可以发起多个 CONNECT 会话。
 *
 * @param s             P2P 会话
 * @param local_peer_id 本端名称
 * @param server        服务器地址
 * @return              E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_relay_online(struct p2p_session *s, const char *local_peer_id,
                              const struct sockaddr_in *server);

/*
 * 客户端下线（阶段1：断开与服务器的 TCP 连接）
 *
 * 关闭 TCP socket，清理所有资源，回到 INIT 状态。
 * 下线后需要重新调用 online() 才能使用信令服务。
 *
 * @param s   P2P 会话
 * @return    E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_relay_offline(struct p2p_session *s);


/*
 * 建立与对端的会话（阶段2：发送 CONNECT 请求）
 *
 * 向服务器请求建立与目标对端的会话，服务器分配 session_id。
 * 前提：必须已经处于 ONLINE 状态。
 *
 * @param s             P2P 会话
 * @param remote_peer_id 目标对端名称
 * @return              E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_relay_connect(struct p2p_session *s, const char *remote_peer_id);

/*
 * 断开当前会话（阶段2：发送 DISCONNECT 消息）
 *
 * 向服务器发送 DISCONNECT 消息，通知结束与对端的会话。
 * 清理会话状态后回到 ONLINE 状态，可以再次发起 CONNECT。
 * 前提：必须处于 WAIT_PEER / EXCHANGING / READY 状态。
 *
 * @param s   P2P 会话
 * @return    E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_relay_disconnect(struct p2p_session *s);

/*
 * Trickle ICE：本地候选异步补发入口（支持 STUN/TURN）
 *
 * @param s   P2P 会话
 */
void p2p_signal_relay_trickle_candidate(struct p2p_session *s);

//----------------------------------------------------------------------------

/*
 * 信令接收维护（拉取阶段）
 *
 * 处理 TCP 接收和协议解析：
 *   - 读取 TCP 数据
 *   - 状态机解包（RECV_HEADER → RECV_PAYLOAD）
 *   - 分发处理各类协议消息
 *
 * 在 p2p_update() 的阶段 2（信令拉取）中调用。
 *
 * @param s   P2P 会话
 */
void p2p_signal_relay_tick_recv(struct p2p_session *s);

/*
 * 信令发送维护（推送阶段）
 *
 * 处理信令发送和重传：
 *   - 心跳保活（ALIVE）
 *   - CONNECT 重传
 *   - 上传候选（PEER_INFO）
 *
 * 在 p2p_update() 的阶段 7（信令推送）中调用。
 *
 * @param s   P2P 会话
 */
void p2p_signal_relay_tick_send(struct p2p_session *s);

///////////////////////////////////////////////////////////////////////////////
#pragma clang diagnostic pop
#endif /* P2P_SIGNAL_RELAY_H */
