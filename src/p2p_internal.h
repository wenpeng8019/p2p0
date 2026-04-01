
/*
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本头文件定义了 P2P 库的内部数据结构和函数声明，包括：
 *   - p2p_session: 会话主结构体，包含所有连接状态
 *   - reliable_t:  可靠传输层，实现 ARQ (自动重传请求)
 *   - 各模块上下文的嵌入（NAT、路由、流、传输）
 *
 * 本文件仅供库内部使用，不对外暴露。
 *
 * ============================================================================
 * 会话状态机
 * ============================================================================
 *
 *  P2P_STATE_INIT ──→ P2P_STATE_DETECTING ──→ P2P_STATE_SIGNALING
 *                           │                        │
 *                           ↓                        ↓
 *                    (NAT 类型检测)          (交换候选地址)
 *                                                    │
 *                                                    ↓
 *                                          P2P_STATE_CONNECTING
 *                                                    │
 *                         ┌──────────────────────────┼───────────────┐
 *                         ↓                          ↓               ↓
 *                   P2P_PATH_DIRECT          P2P_PATH_RELAY    P2P_PATH_TCP
 *                   (直连成功)               (中继模式)        (TCP 穿透)
 *                         │                          │               │
 *                         └──────────────────────────┴───────────────┘
 *                                                    ↓
 *                                          P2P_STATE_CONNECTED
 *
 * ============================================================================
 * 协议栈层次
 * ============================================================================
 *
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │                     Application Layer                          │
 *   │                   (p2p_send / p2p_recv)                        │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │                      Stream Layer                              │
 *   │              (流分片、重组、应用数据封装)                        │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │                    Reliable Layer                              │
 *   │              (ARQ 重传、序列号、确认机制)                        │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │                    Transport Layer                             │
 *   │           (DTLS/SCTP/PseudoTCP/Simple 可选)                    │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │                      NAT Layer                                 │
 *   │           (STUN 绑定、ICE 候选、打洞协调)                       │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │                      UDP / TCP                                 │
 *   │                   (底层 Socket I/O)                            │
 *   └─────────────────────────────────────────────────────────────────┘
 */

#ifndef P2P_INTERNAL_H
#define P2P_INTERNAL_H
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma ide diagnostic ignored "UnreachableCallsOfFunction"

#include "predefine.h"
#include <p2p.h>

#include "p2p_common.h"         /* pack/unpack_signaling_payload_hdr（服务端也可包含此头） */
#include "LANG.h"               /* 多语言支持 */

#include "p2p_nat.h"            /* NAT 穿透与类型检测 */
#include "p2p_route.h"          /* 路由表管理 */
#include "p2p_stream.h"         /* 流式数据传输 */
#include "p2p_transport.h"      /* 传输层抽象接口 */
#include "p2p_dtls.h"          /* DTLS 加密层接口 */
#include "p2p_udp.h"            /* UDP 传输层常量 (P2P_MTU, P2P_MAX_PAYLOAD) */
#include "p2p_stun.h"           /* STUN 协议实现 */
#include "p2p_crypto.h"         /* 加密功能 */
#include "p2p_ice.h"            /* ICE 协议 */
#include "p2p_turn.h"           /* TURN 中继 */
#include "p2p_tcp_punch.h"      /* TCP 打洞 */
#include "p2p_signal_relay.h"   /* 中继模式信令 */
#include "p2p_signal_pubsub.h"  /* 发布/订阅模式信令 */
#include "p2p_signal_compact.h" /* COMPACT 模式信令 */
#include "p2p_path_manager.h"   /* 多路径管理器 */
#include "p2p_probe.h"          /* 信道外可达性探测 */

///////////////////////////////////////////////////////////////////////////////

/* ============================================================================
 * p2p_session: P2P 会话主结构体
 * ============================================================================
 *
 * 包含一个 P2P 连接的全部状态信息：
 *   - 配置与状态
 *   - Socket 资源
 *   - NAT 检测结果
 *   - ICE 候选地址
 *   - 信令上下文
 *   - 各传输层实例
 *   - 拥塞控制状态
 *   - 线程同步（可选）
 *
 * 生命周期：p2p_init() → p2p_connect() → p2p_send/recv() → p2p_close()
 */
typedef struct p2p_session {
    /* ======================== 配置与状态 ======================== */
    p2p_config_t                    cfg;                // 用户配置（STUN 服务器、模式等）
    p2p_state_t                     state;              // 连接状态 P2P_STATE_*

    /* ======================== Socket 资源 ======================== */
    sock_t                          sock;               // UDP 套接字描述符
    sock_t                          tcp_sock;           // TCP 套接字（打洞/回退用）

    /* ======================== 活跃路径 ======================== */
    int                             active_path;        // 当前活跃路径索引 (-2=无, -1=SIGNALING, >=0=候选)
    struct sockaddr_in              active_addr;        // 当前通信目标地址（与 active_path 一致）
    p2p_path_type_t                 path_type;          // 连接路径 P2P_PATH_*

    /* ======================== 信令转发路径 ======================== */
    /*
     * SIGNALING 转发（索引 -1）：通过信令服务器转发数据，非标准降级方案。
     * 与 remote_cands[i] 平级，stats 作为统一的路径统计基本对象。
     */
    struct {
        bool                    active;                 // 是否启用
        struct sockaddr_in      addr;                   // 信令服务器地址
        path_stats_t            stats;                  // 统计信息
    } signaling;

    /* ======================== p2p 链路 ======================== */

    nat_ctx_t                       nat;                // NAT 穿透上下文
    reliable_t                      reliable;           // 可靠传输层状态
    stream_t                        stream;             // 流传输层状态
    path_manager_t                  path_mgr;           // 路径管理器（多路径并行支持）
    p2p_probe_ctx_t                 probe_ctx;          // 探测上下文

    bool                            rx_confirmed;       // peer→me 已确认（收到对端包）
    bool                            tx_confirmed;       // me→peer 已确认（对端可收到我的包）

    /* ======================== 传输层实例 ======================== */
    /*
     * 可插拔传输层 + 加密层（正交组合）：
     *   传输层: simple(reliable) / pseudotcp / sctp  — 管可靠性
     *   加密层: disabled / mbedtls / openssl         — 管加密
     */
    const p2p_trans_ops_t*          trans;              // 传输层操作函数表（reliable/pseudotcp/sctp）
    void*                           trans_data;         // 传输层私有数据
    const p2p_dtls_ops_t*           dtls;               // DTLS 加密层操作函数表（与传输层正交）
    void*                           dtls_data;          // DTLS 加密层私有数据（SSL 上下文等）

    /* ======================== 候选队列 ======================== */
    p2p_local_candidate_entry_t*    local_cands;        // 本地候选地址（动态分配）
    int                             local_cand_cnt;     // 本地候选数量
    int                             local_cand_cap;     // 本地候选容量
    p2p_remote_candidate_entry_t*   remote_cands;       // 远端候选地址（动态分配，含运行时状态）
    int                             remote_cand_cnt;    // 远端候选数量
    int                             remote_cand_cap;    // 远端候选容量
    uint16_t                        remote_host_cnt;    // 对端 host 类型候选数量
    uint16_t                        remote_srflx_cnt;   // 对端 srflx 类型候选数量
    uint16_t                        remote_relay_cnt;   // 对端 relay 类型候选数量
    bool                            remote_cand_done;   // 远端候选是否同步完成（由信令层设置，NAT 层判断超时用）
    int                             stun_pending;       // STUN/Srflx 候选待响应计数（一次性本地候选收集）
    int                             turn_pending;       // TURN Allocate 待响应计数（本轮连接中的动态 Relay 候选）
    int                             turn_base;          // local_cands 中首个 TURN 候选索引（-1 表示当前无 TURN 候选）

    /* ===== 信令上下文/ICE（Interactive Connectivity Establishment）交换 ===== */
    /*
     * 信令模块负责在两个对等体之间交换连接信息（候选地址、密钥等）。
     * 支持三种模式：
     *   - sig_compact_ctx: COMPACT模式，UDP 无状态信令
     *   - sig_relay_ctx:  ICE模式，TCP 中继信令
     *   - sig_pubsub_ctx: PUBSUB模式，通过 GitHub Gist
     */
    char                            local_peer_id[P2P_PEER_ID_MAX];  // 本端身份标识
    char                            remote_peer_id[P2P_PEER_ID_MAX]; // 目标对等体 ID
    p2p_signaling_t                 signaling_mode;     // 信令模式
    p2p_signal_compact_ctx_t        sig_compact_ctx;    // COMPACT 模式信令上下文
    p2p_signal_relay_ctx_t          sig_relay_ctx;      // RELAY 模式信令上下文
    p2p_signal_pubsub_ctx_t         sig_pubsub_ctx;     // PUB/SUB 模式信令上下文

    /* ======================== 中继服务 ======================== */
    turn_ctx_t                      turn;               // TURN 中继上下文

    /*
     * 通过信令服务器中转发送数据包（通用接口）
     *
     * 各信令模式（compact/relay/pubsub）在 p2p_create 时赋值具体实现。
     * 自动处理 session_id 封装和服务器路由。
     *
     * 参数：
     *   type: P2P 包类型（REACH/DATA/ACK/CRYPTO 等）
     *   flags: 原始 flags（接口内部会自动添加 P2P_DATA_FLAG_SESSION）
     *   seq: 序列号
     *   payload: 原始负载（不含 session_id）
     *   payload_len: 原始负载长度
     *
     * 返回：E_NONE=成功，负值=错误码
     */
    ret_t (*signaling_relay_fn)(struct p2p_session *s,
                                uint8_t type, uint8_t flags, uint16_t seq,
                                const void *payload, uint16_t payload_len);

    /* ======================== NAT 检测 ======================== */
    int                             nat_type;           // NAT 类型，即 p2p_nat_type() 返回值，也就是支持负值状态
    int                             det_step;           // 当前检测步骤 det_step_t
    uint64_t                        det_last_send;      // 上次发送检测包时间
    int                             det_retries;        // 当前步骤重试次数

    /* ======================== PseudoTCP 拥塞控制 ======================== */
    /*
     * 类 TCP 拥塞控制算法 (AIMD)：
     *   - cwnd:     拥塞窗口，控制发送速率
     *   - ssthresh: 慢启动阈值
     *   - dup_acks: 重复 ACK 计数（触发快速重传）
     *   - sack:     选择确认位图
     */
    struct {
        uint32_t                cwnd;                   // 拥塞窗口 (字节/包数)
        uint32_t                ssthresh;               // 慢启动阈值
        uint32_t                dup_acks;               // 重复 ACK 计数（>=3 触发快速重传）
        uint32_t                sack;                   // SACK 位图
        uint64_t                last_ack;               // 上次收到 ACK 的时间戳
        int                     cc_state;               // 拥塞控制状态 TCP_STATE_*
        float                   loss_rate;              // EWMA 丢包率估计（0.0-1.0）
    } tcp;

    /* ======================== 定时器 ======================== */
    uint64_t                        last_update;        // 上次调用 p2p_update() 的时间

#ifdef P2P_THREADED
    /* ======================== 多线程支持 ======================== */
    /*
     * 启用 P2P_THREADED 时，会话在独立线程中运行。
     * 需要互斥锁保护共享状态。
     * 类型由 stdc 模块定义（pthread_t / HANDLE）。
     */
    thd_t                           thread;             // 工作线程
    P_mutex_t                       mtx;                // 互斥锁
    int                             thread_running;     // 线程是否运行中
    int                             quit;               // 退出标志
#endif
} p2p_session_t;

/*
 * NAT 类型转可读字符串
 *
 * 覆盖所有负值（检测中/超时）和 p2p_nat_type_t 枚举值。
 * 语言由全局 lang_init() / lang_load_fp() 控制。
 */
static inline const char* p2p_nat_type_str(int type) {
    switch (type) {
        case P2P_NAT_DETECTING:         return LA_W("Detecting...", LA_W3, 3);
        case P2P_NAT_TIMEOUT:           return LA_W("Timeout (no response)", LA_W17, 17);
        case P2P_NAT_UNKNOWN:           return LA_W("Unknown", LA_W20, 20);
        case P2P_NAT_OPEN:              return LA_W("Open Internet (No NAT)", LA_W6, 6);
        case P2P_NAT_FULL_CONE:         return LA_W("Full Cone NAT", LA_W4, 4);
        case P2P_NAT_RESTRICTED:        return LA_W("Restricted Cone NAT", LA_W13, 13);
        case P2P_NAT_PORT_RESTRICTED:   return LA_W("Port Restricted Cone NAT", LA_W7, 7);
        case P2P_NAT_SYMMETRIC:         return LA_W("Symmetric NAT (port-random)", LA_W16, 16);
        case P2P_NAT_BLOCKED:           return LA_W("UDP Blocked (STUN unreachable)", LA_W18, 18);
        case P2P_NAT_UNDETECTABLE:       return LA_W("Undetectable (no STUN/probe configured)", LA_W19, 19);
        default:                        return LA_W("Unknown", LA_W20, 20);
    }
}

static inline const char* p2p_path_type_str(int type) {
    switch (type) {
        case P2P_PATH_NONE:             return "NONE";
        case P2P_PATH_LAN:              return "LAN";
        case P2P_PATH_PUNCH:            return "PUNCH";
        case P2P_PATH_RELAY:            return "RELAY";
        case P2P_PATH_SIGNALING:        return "SIGNALING";
        default:                        return "UNKNOWN";
    }
}

/* ============================================================================
 * 内部候选地址定义（前移以供 p2p_session_reset 使用）
 * ============================================================================ */

/* 
 * 候选地址类型
 */
typedef enum {
    P2P_CAND_HOST  = 0,                         // 本地网卡地址（Host Candidate）
    P2P_CAND_SRFLX,                             // Server 反射地址（Server Reflexive Candidate）
    P2P_CAND_RELAY,                             // Server 中继地址（Relayed Candidate）
    P2P_CAND_PRFLX                              // 对端反射地址（Peer Reflexive Candidate）
} p2p_cand_type_t;

static inline const char* p2p_candidate_type_str(p2p_cand_type_t type) {
    switch (type) {
        case P2P_CAND_HOST:             return "Host";
        case P2P_CAND_SRFLX:            return "Srflx";
        case P2P_CAND_PRFLX:            return "Prflx";
        case P2P_CAND_RELAY:            return "Relay";
        default:                        return "Unknown";
    }
}

/*
 * 重置 session 连接状态
 *
 * @param closing  true = 连接断开（释放 TURN 分配，NAT 已 CLOSED）
 *                 false = 重连准备（保留 TURN，重置 NAT）
 *
 * 清除：
 *   - 远端候选列表
 *   - ICE 状态
 *   - 路径管理器运行时状态
 *   - 活跃路径
 *   - SIGNALING 统计（保留 active 和 addr）
 */
static inline void p2p_session_reset(p2p_session_t *s, bool closing) {
    
    // 清除远端候选
    s->remote_cand_cnt = 0;
    s->remote_host_cnt = 0;
    s->remote_srflx_cnt = 0;
    s->remote_relay_cnt = 0;
    s->remote_cand_done = false;

    // 重置活跃路径
    s->path_type = P2P_PATH_NONE;
    s->active_path = PATH_IDX_NONE;
    memset(&s->active_addr, 0, sizeof(s->active_addr));
    
    // 重置 SIGNALING 中转地址的统计
    if (s->signaling.active)
        path_stats_init(&s->signaling.stats, 5);
    
    // 重置路径管理器
    path_manager_reset(s);

    // 重置探测状态（它依赖信令服务器的 rpc，即基于 session 的数据状态）
    // + 所以 session 关闭或重置，之前的探测也就无效了
    probe_reset(s);

    // 重置可靠传输层（序列号、窗口、重试计数等）
    // + 对端重连时使用新的序列号起点，旧的状态会导致消息被误判为重复或乱序
    reliable_init(s);

    // 如果是关闭连接（而非重置）
    if (closing) {

        // 清理本地 TURN 候选（TURN 候选随连接失效而失效）
        if (s->turn_base >= 0) { assert(s->turn_base < s->local_cand_cnt);

            // 优先从后向前 pop 掉 TURN 候选（多次连接后，TURN 候选都会位于 local_cands 队列的末尾）
            while (s->local_cand_cnt > s->turn_base) {
                if (s->local_cands[s->local_cand_cnt - 1].type != P2P_CAND_RELAY) break;
                s->local_cand_cnt--;
            }

            // 如果 TURN 候选并非连续地位于末尾（首次连接可能和 STUN 候选交错）
            // + 紧缩 turn_base..end 范围内的非 TURN 候选，覆盖掉前面的 TURN 候选
            if (s->local_cand_cnt != s->turn_base) {
                int j = s->turn_base;
                for (int i = s->turn_base; i < s->local_cand_cnt; i++) {
                    if (s->local_cands[i].type != P2P_CAND_RELAY)
                        s->local_cands[j++] = s->local_cands[i];
                }
                s->local_cand_cnt = j;
            }

            s->turn_base = -1;
        }
        s->turn_pending = 0;            // 关闭当前 TURN 候选收集
        p2p_turn_reset(s);
        s->nat.state = NAT_CLOSED;      // 标记 NAT 已关闭
        s->state = P2P_STATE_CLOSED;    // 更新状态为已关闭
    }
    // 重置 NAT
    else nat_reset(&s->nat);
}

/* ============================================================================
 * 内部候选地址定义（续）
 * ============================================================================ */

/*
 * ICE 候选地址（内部类型，使用平台原生 struct sockaddr_in）
 */
struct p2p_local_candidate_entry {
    p2p_cand_type_t    type;                    // 候选类型
    uint32_t           priority;                // 候选优先级
    struct sockaddr_in addr;                    // 传输地址（平台原生 16B）
    struct sockaddr_in base_addr;               // 基础地址（平台原生 16B）
};

/*
 * 远端候选地址
 *
 * 与 p2p_local_candidate_entry_t 解耦；共享 type/priority/addr 字段布局，
 * 但不包含 base_addr，额外运行时状态仅用于本地调度，不参与线协议。
 */
struct p2p_remote_candidate_entry {
    p2p_cand_type_t    type;                    // 候选类型
    uint32_t           priority;                // 候选优先级
    struct sockaddr_in addr;                    // 传输地址（平台原生 16B）

    /* 收发分离状态 */
    uint64_t           last_punch_send_ms;      // 最近一次发送 PUNCH 的时间
    path_stats_t       stats;                   // 路径统计信息
};

/*
 * 收发分离状态工具函数
 *
 * readable: 路径可读（收到过来自该地址的包）
 * writable: 路径可写（双向连通，可发送数据）
 */
static inline bool p2p_remote_candidate_readable(const p2p_remote_candidate_entry_t *c) {
    return c->stats.last_recv_ms != 0;
}

static inline bool p2p_remote_candidate_writable(const p2p_remote_candidate_entry_t *c) {
    return path_is_selectable(c->stats.state);
}

/* ============================================================================
 * 候选地址序列化 / 反序列化
 * ============================================================================ */

/*
 * pack_candidate: p2p_local_candidate_entry_t（内部平台格式）→ 网络字节流
 *
 * 格式（23 字节）：[type:1B][addr:18B][priority:4B]
 */
static inline int pack_candidate(const p2p_local_candidate_entry_t *c, uint8_t *buf) {
    p2p_candidate_t* w = (p2p_candidate_t*)buf;
    w->type     = (uint8_t)c->type;
    sockaddr_to_p2p_wire(&c->addr, &w->addr);
    w->priority = htonl(c->priority);
    return (int)sizeof(p2p_candidate_t);  /* 23 */
}

/*
 * unpack_candidate: 网络字节流 → p2p_local_candidate_entry_t（内部平台格式）
 */
static inline int unpack_candidate(p2p_remote_candidate_entry_t *c, const uint8_t *buf) {
    const p2p_candidate_t* w = (p2p_candidate_t*)buf;
    c->type     = (int)w->type;
    sockaddr_from_p2p_wire(&c->addr, &w->addr);
    c->priority = ntohl(w->priority);

    c->last_punch_send_ms = 0;
    path_stats_init(&c->stats, 0);  /* 初始化路径统计默认值（rtt_min=9999 等） */

    return (int)sizeof(p2p_candidate_t);  /* 23 */
}

/* ============================================================================
 * 动态候选数组辅助函数
 *
 * 向 local_cands / remote_cands 追加新候选，容量不足时自动翻倍扩容
 * 返回新候选槽位索引，或负值错误码（E_OUT_OF_MEMORY）
 * ============================================================================ */

static inline ret_t p2p_cand_push_local(p2p_session_t *s) {
    if (s->local_cand_cnt >= s->local_cand_cap) {
        int nc = s->local_cand_cap > 0 ? s->local_cand_cap * 2 : 8;
        p2p_local_candidate_entry_t *p = (p2p_local_candidate_entry_t *)realloc(s->local_cands, nc * sizeof(p2p_local_candidate_entry_t));
        if (!p) return E_OUT_OF_MEMORY;
        s->local_cands    = p;
        s->local_cand_cap = nc;
    }
    return s->local_cand_cnt++;
}

static inline ret_t p2p_cand_push_remote(p2p_session_t *s) {
    if (s->remote_cand_cnt >= s->remote_cand_cap) {
        int nc = s->remote_cand_cap > 0 ? s->remote_cand_cap * 2 : 8;
        p2p_remote_candidate_entry_t *p = (p2p_remote_candidate_entry_t *)realloc(s->remote_cands, nc * sizeof(p2p_remote_candidate_entry_t));
        if (!p) return E_OUT_OF_MEMORY;
        if (nc > s->remote_cand_cap) {
            memset(p + s->remote_cand_cap, 0, (nc - s->remote_cand_cap) * sizeof(p2p_remote_candidate_entry_t));
        }
        s->remote_cands    = p;
        s->remote_cand_cap = nc;
    }
    return s->remote_cand_cnt++;
}

static inline int p2p_find_remote_candidate_by_addr(const p2p_session_t *s, const struct sockaddr_in *addr) {
    if (!s || !addr) return -1;
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (sockaddr_equal(&s->remote_cands[i].addr, addr)) return i;
    }
    return -1;
}

/*
 * 为 remote_cands 保留目标（need）个槽位。新分配空间会被置 NULL
 * 返回 E_NONE，分配失败返回 E_OUT_OF_MEMORY
*/
static inline ret_t p2p_remote_cands_reserve(p2p_session_t *s, int need) {
    if (need <= s->remote_cand_cap) return E_NONE;
    int nc = s->remote_cand_cap > 0 ? s->remote_cand_cap : 8;
    while (nc < need) nc *= 2;
    p2p_remote_candidate_entry_t *p = (p2p_remote_candidate_entry_t *)realloc(s->remote_cands, nc * sizeof(p2p_remote_candidate_entry_t));
    if (!p) {
        print("E:", LA_F("Failed to realloc memory for remote candidates (capacity: %d)", LA_F243, 243), nc);
        return E_OUT_OF_MEMORY;
    }
    memset(p + s->remote_cand_cap, 0, (nc - s->remote_cand_cap) * sizeof(p2p_remote_candidate_entry_t));
    s->remote_cands    = p;
    s->remote_cand_cap = nc;
    return E_NONE;
}

/* ============================================================================
 * 路径统计与地址访问
 *
 * 统一访问 API：根据路径索引获取统计信息或地址
 *   -1=SIGNALING(s->signaling), >=0=候选(s->remote_cands[idx])
 *
 * 注意：这些函数需要 p2p_remote_candidate_entry 完整定义，故必须放在结构体之后
 * ============================================================================ */

 static inline void p2p_set_active_path(p2p_session_t *s, int path_idx) {
    if (path_idx == PATH_IDX_SIGNALING) {
        if (!s->signaling.active) return;
        s->active_path = path_idx;
        s->active_addr = s->signaling.addr;
        s->path_type = P2P_PATH_SIGNALING;
    } else if (path_idx >= 0 && path_idx < s->remote_cand_cnt) {
        p2p_remote_candidate_entry_t *e = &s->remote_cands[path_idx];
        s->active_path = path_idx;
        s->active_addr = e->addr;
        if (e->type == P2P_CAND_RELAY) s->path_type = P2P_PATH_RELAY;
        else if (e->stats.is_lan) s->path_type = P2P_PATH_LAN;
        else s->path_type = P2P_PATH_PUNCH;
    }
    else {
        s->active_path = PATH_IDX_NONE;
        s->active_addr = (struct sockaddr_in){0};
        s->path_type = P2P_PATH_NONE;
    }
}

//-----------------------------------------------------------------------------

static inline ret_t p2p_reset_path(p2p_session_t *s, int path_idx) {

    if (path_idx == PATH_IDX_SIGNALING) {
        if (!s->signaling.active) return E_INVALID;
        path_stats_init(&s->signaling.stats, 5);  /* cost_score=5 */
    } else if (path_idx >= 0 && path_idx < s->remote_cand_cnt) {
        p2p_remote_candidate_entry_t *c = &s->remote_cands[path_idx];
        c->last_punch_send_ms = 0;
        path_stats_init(&c->stats, 0);
    } else return E_INVALID;

    /* 如果重置的是活跃路径，切换到下一个最佳路径（可能是 PATH_IDX_NONE） */
    if (s->active_path == path_idx) {
        p2p_set_active_path(s, path_manager_select_best_path(s));
    }
    return E_NONE;
}

static inline p2p_path_type_t p2p_get_path_type(p2p_session_t *s, int path_idx) {
    if (path_idx == PATH_IDX_SIGNALING)
        return P2P_PATH_SIGNALING;
    if (path_idx < 0 || path_idx >= s->remote_cand_cnt)
        return P2P_PATH_NONE;
    p2p_remote_candidate_entry_t *e = &s->remote_cands[path_idx];
    if (e->type == P2P_CAND_RELAY)
        return P2P_PATH_RELAY;
    if (e->stats.is_lan)
        return P2P_PATH_LAN;
    return P2P_PATH_PUNCH;
}

static inline path_stats_t* p2p_get_path_stats(p2p_session_t *s, int path_idx) {
    if (path_idx == PATH_IDX_SIGNALING)
        return s->signaling.active ? &s->signaling.stats : NULL;
    if (path_idx >= 0 && path_idx < s->remote_cand_cnt)
        return &s->remote_cands[path_idx].stats;
    return NULL;
}

static inline const struct sockaddr_in* p2p_get_path_addr(p2p_session_t *s, int path_idx) {
    if (path_idx == PATH_IDX_SIGNALING)
        return s->signaling.active ? &s->signaling.addr : NULL;
    if (path_idx >= 0 && path_idx < s->remote_cand_cnt)
        return &s->remote_cands[path_idx].addr;
    return NULL;
}

static inline int p2p_find_path_by_addr(p2p_session_t *s, const struct sockaddr_in *addr) {
    if (s->signaling.active && sockaddr_equal(&s->signaling.addr, addr))
        return PATH_IDX_SIGNALING;
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (sockaddr_equal(&s->remote_cands[i].addr, addr)) return i;
    }
    return -2;
}

/* ============================================================================
 * p2p 连接层
 *
 * 所有传输模块（reliable / pseudotcp / sctp）通过 p2p_send_packet 发送:
 *   - 自动处理加密（如果 s->dtls 已就绪）
 *   - 自动处理中继封装（TURN / Compact 信令）
 *   - 调用者总是传"基础"包类型（DATA / ACK）
 * ============================================================================ */

void p2p_connected(struct p2p_session *s, uint64_t now_ms);

int p2p_send_packet(struct p2p_session *s, const struct sockaddr_in *addr,
                    uint8_t type, uint8_t flags, uint16_t seq,
                    const void *payload, int payload_len, uint64_t now_ms);

/* 发送原始 DTLS 记录（加密模块的握手/加密输出使用） */
void p2p_send_dtls_record(struct p2p_session *s, const struct sockaddr_in *addr,
                  const void *dtls_record, int record_len);

/* ============================================================================
 * 实际链路发送接口（UDP 传输层直接调用）
 * ============================================================================ */

ret_t p2p_udp_send_packet(p2p_session_t *s, const struct sockaddr_in *addr,
                          uint8_t type, uint8_t flags, uint16_t seq,
                          const void *payload, int payload_len);

ret_t p2p_turn_send_packet(p2p_session_t *s, const struct sockaddr_in *addr,
                           uint8_t type, uint8_t flags, uint16_t seq,
                           const void *payload, int payload_len);

///////////////////////////////////////////////////////////////////////////////
#pragma ide diagnostic pop
#pragma clang diagnostic pop
#endif /* P2P_INTERNAL_H */
