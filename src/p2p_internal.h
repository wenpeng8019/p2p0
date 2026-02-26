
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
#pragma clang diagnostic ignored "-Wunused-function"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma ide diagnostic ignored "UnreachableCallsOfFunction"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>           /* PRIu64 */

#include <p2p.h>

#include "p2p_common.h"         /* pack/unpack_signaling_payload_hdr（服务端也可包含此头） */
#include "LANG.h"               /* 多语言支持 */

#include "p2p_nat.h"            /* NAT 穿透与类型检测 */
#include "p2p_route.h"          /* 路由表管理 */
#include "p2p_stream.h"         /* 流式数据传输 */
#include "p2p_transport.h"      /* 传输层抽象接口 */
#include "p2p_udp.h"            /* UDP 传输层常量 (P2P_MTU, P2P_MAX_PAYLOAD) */
#include "p2p_stun.h"           /* STUN 协议实现 */
#include "p2p_crypto.h"         /* 加密功能 */
#include "p2p_ice.h"            /* ICE 协议 */
#include "p2p_turn.h"           /* TURN 中继 */
#include "p2p_tcp_punch.h"      /* TCP 打洞 */
#include "p2p_signal_relay.h"   /* 中继模式信令 */
#include "p2p_signal_pubsub.h"  /* 发布/订阅模式信令 */
#include "p2p_signal_compact.h" /* COMPACT 模式信令 */
#include "p2p_log.h"

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
    p2p_config_t                cfg;                // 用户配置（STUN 服务器、模式等）
    p2p_state_t                 state;              // 连接状态 P2P_STATE_*
    int                         path;               // 连接路径 P2P_PATH_*

    /* ======================== Socket 资源 ======================== */
    p2p_socket_t                sock;               // UDP 套接字描述符
    p2p_socket_t                tcp_sock;           // TCP 套接字（打洞/回退用）
    struct sockaddr_in          active_addr;        // 当前通信目标地址

    /* ======================== NAT 检测 ======================== */
    int                         nat_type;           // NAT 检测结果（p2p_get_nat_type() 返回值，同时支持负值状态）
    int                         det_step;           // 当前检测步骤 det_step_t
    uint64_t                    det_last_send;      // 上次发送检测包时间
    int                         det_retries;        // 当前步骤重试次数

    /* ======================== ICE 状态 ======================== */
    p2p_ice_state_t             ice_state;          // ICE 协商状态
    p2p_candidate_entry_t*      local_cands;        // 本地候选地址（动态分配）
    int                         local_cand_cnt;     // 本地候选数量
    int                         local_cand_cap;     // 本地候选容量
    p2p_remote_candidate_entry_t* remote_cands;     // 远端候选地址（动态分配，含运行时状态）
    int                         remote_cand_cnt;    // 远端候选数量
    int                         remote_cand_cap;    // 远端候选容量
    uint64_t                    ice_check_last_ms;  // 上次连通性检查时间
    int                         ice_check_count;    // 已发送检查轮数

    /* ======================== 信令上下文 ======================== */
    /*
     * 信令模块负责在两个对等体之间交换连接信息（候选地址、密钥等）。
     * 支持三种模式：
     *   - sig_compact_ctx: COMPACT模式，UDP 无状态信令
     *   - sig_relay_ctx:  ICE模式，TCP 中继信令
     *   - sig_pubsub_ctx: PUBSUB模式，通过 GitHub Gist
     */
    char                        local_peer_id[P2P_PEER_ID_MAX];  // 本端身份标识
    char                        remote_peer_id[P2P_PEER_ID_MAX]; // 目标对等体 ID
    p2p_signal_compact_ctx_t    sig_compact_ctx;    // COMPACT 模式信令上下文
    p2p_signal_relay_ctx_t      sig_relay_ctx;      // RELAY 模式信令上下文
    p2p_signal_pubsub_ctx_t     sig_pubsub_ctx;     // PUB/SUB 模式信令上下文
    int                         signaling_mode;     // 信令模式 P2P_CONNECT_MODE_*
    bool                        signal_sent;        // 是否已发送初始信令
    uint64_t                    last_signal_time;   // 上次发送信令的时间戳 (ms)
    int                         last_cand_cnt_sent; // 上次发送时的候选数量
    bool                        cands_pending_send; // 有待发送的候选（TCP 发送失败时置 1）

    /* ======================== 传输层实例 ======================== */
    nat_ctx_t                   nat;                // NAT 穿透上下文
    route_ctx_t                 route;              // 路由表上下文
    reliable_t                  reliable;           // 可靠传输层状态
    stream_t                    stream;             // 流传输层状态

    /* ======================== 模块化传输 ======================== */
    /*
     * 可插拔传输层架构，支持多种实现：
     *   - simple:    无加密直接传输
     *   - mbedtls:   DTLS 加密 (MbedTLS)
     *   - sctp:      SCTP 协议 (usrsctp)
     *   - pseudotcp: 模拟 TCP 拥塞控制
     */
    const p2p_transport_ops_t*  trans;              // 传输层操作函数表
    void*                       transport_data;     // 传输层私有数据

    /* ======================== PseudoTCP 拥塞控制 ======================== */
    /*
     * 类 TCP 拥塞控制算法 (AIMD)：
     *   - cwnd:     拥塞窗口，控制发送速率
     *   - ssthresh: 慢启动阈值
     *   - dup_acks: 重复 ACK 计数（触发快速重传）
     *   - sack:     选择确认位图
     */
    struct {
        uint32_t                cwnd;               // 拥塞窗口 (字节/包数)
        uint32_t                ssthresh;           // 慢启动阈值
        uint32_t                dup_acks;           // 重复 ACK 计数（>=3 触发快速重传）
        uint32_t                sack;               // SACK 位图
        uint64_t                last_ack;           // 上次收到 ACK 的时间戳
        int                     cc_state;           // 拥塞控制状态 TCP_STATE_*
    } tcp;

    /* ======================== 定时器 ======================== */
    uint64_t                    last_update;        // 上次调用 p2p_update() 的时间

#ifdef P2P_THREADED
    /* ======================== 多线程支持 ======================== */
    /*
     * 启用 P2P_THREADED 时，会话在独立线程中运行。
     * 需要互斥锁保护共享状态。
     * 类型由 p2p_platform.h 定义（pthread_t / HANDLE）。
     */
    p2p_thread_t                thread;             // 工作线程
    p2p_mutex_t                 mtx;                // 互斥锁
    int                         thread_running;     // 线程是否运行中
    int                         quit;               // 退出标志
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
        case P2P_NAT_DETECTING:        return LA_W("Detecting...", LA_W25, 26);
        case P2P_NAT_TIMEOUT:          return LA_W("Timeout (no response)", LA_W131, 132);
        case P2P_NAT_UNKNOWN:          return LA_W("Unknown", LA_W137, 138);
        case P2P_NAT_OPEN:             return LA_W("Open Internet (No NAT)", LA_W65, 66);
        case P2P_NAT_FULL_CONE:        return LA_W("Full Cone NAT", LA_W40, 41);
        case P2P_NAT_RESTRICTED:       return LA_W("Restricted Cone NAT", LA_W104, 105);
        case P2P_NAT_PORT_RESTRICTED:  return LA_W("Port Restricted Cone NAT", LA_W79, 80);
        case P2P_NAT_SYMMETRIC:        return LA_W("Symmetric NAT (port-random)", LA_W126, 127);
        case P2P_NAT_BLOCKED:          return LA_W("UDP Blocked (STUN unreachable)", LA_W135, 136);
        case P2P_NAT_UNSUPPORTED:      return LA_W("Unsupported (no STUN/probe configured)", LA_W140, 141);
        default:                       return LA_W("Unknown", LA_W137, 138);
    }
}

/* ============================================================================
 * 内部候选地址定义
 * ============================================================================ */

/*
 * ICE 候选地址（内部类型，使用平台原生 struct sockaddr_in）
 *
 * 仅用于会话内部运算。网络传输使用 p2p_candidate_t（见 p2pp.h）。
 * 转换函数：pack_candidate() / unpack_candidate()，见下文。
 */
struct p2p_candidate_entry {
    int                type;                    // 候选类型（信令模式相关：RELAY/ICE=p2p_ice_cand_type_t，COMPACT=p2p_compact_cand_type_t）
    struct sockaddr_in addr;                    // 传输地址（平台原生 16B）
    struct sockaddr_in base_addr;               // 基础地址（平台原生 16B）
    uint32_t           priority;                // 候选优先级
};

/*
 * 远端候选地址（扩展类型）
 *
 * 以 p2p_candidate_entry_t 作为首成员，保持与 pack_candidate()/unpack_candidate()
 * 共享同一基础序列化字段；额外运行时状态仅用于本地调度，不参与线协议。
 */
struct p2p_remote_candidate_entry {
    p2p_candidate_entry_t cand;                 // 可序列化基础候选字段
    uint64_t              last_punch_send_ms;   // 最近一次发送 PUNCH 的时间（调度状态）
};


/* ============================================================================
 * 候选地址序列化 / 反序列化
 * ============================================================================ */

/*
 * pack_candidate: p2p_candidate_entry_t（内部平台格式）→ 网络字节流
 *
 * 格式（32 字节）：[type:4B][addr:12B][base_addr:12B][priority:4B]
 */
static inline int pack_candidate(const p2p_candidate_entry_t *c, uint8_t *buf) {
    p2p_candidate_t w;
    w.type     = htonl((uint32_t)c->type);
    p2p_sockaddr_to_wire(&c->addr,      &w.addr);
    p2p_sockaddr_to_wire(&c->base_addr, &w.base_addr);
    w.priority = htonl(c->priority);
    memcpy(buf, &w, sizeof(w));
    return (int)sizeof(w);  /* 32 */
}

/*
 * unpack_candidate: 网络字节流 → p2p_candidate_entry_t（内部平台格式）
 */
static inline int unpack_candidate(p2p_candidate_entry_t *c, const uint8_t *buf) {
    p2p_candidate_t w;
    memcpy(&w, buf, sizeof(w));
    c->type     = (int)ntohl(w.type);
    p2p_wire_to_sockaddr(&w.addr,      &c->addr);
    p2p_wire_to_sockaddr(&w.base_addr, &c->base_addr);
    c->priority = ntohl(w.priority);
    return (int)sizeof(w);  /* 32 */
}

/* ============================================================================
 * 动态候选数组辅助函数
 *
 * 向 local_cands / remote_cands 追加新候选，容量不足时自动翻倍扩容。
 * 返回新候选槽位指针；OOM 时返回 NULL。
 * ============================================================================ */

static inline p2p_candidate_entry_t *p2p_cand_push_local(p2p_session_t *s) {
    if (s->local_cand_cnt >= s->local_cand_cap) {
        int nc = s->local_cand_cap > 0 ? s->local_cand_cap * 2 : 8;
        p2p_candidate_entry_t *p = (p2p_candidate_entry_t *)realloc(
            s->local_cands, nc * sizeof(p2p_candidate_entry_t));
        if (!p) return NULL;
        s->local_cands    = p;
        s->local_cand_cap = nc;
    }
    return &s->local_cands[s->local_cand_cnt++];
}

static inline p2p_remote_candidate_entry_t *p2p_cand_push_remote(p2p_session_t *s) {
    if (s->remote_cand_cnt >= s->remote_cand_cap) {
        int nc = s->remote_cand_cap > 0 ? s->remote_cand_cap * 2 : 8;
        p2p_remote_candidate_entry_t *p = (p2p_remote_candidate_entry_t *)realloc(
            s->remote_cands, nc * sizeof(p2p_remote_candidate_entry_t));
        if (!p) return NULL;

        if (nc > s->remote_cand_cap) {
            memset(p + s->remote_cand_cap, 0,
                   (nc - s->remote_cand_cap) * sizeof(p2p_remote_candidate_entry_t));
        }

        s->remote_cands    = p;
        s->remote_cand_cap = nc;
    }
    return &s->remote_cands[s->remote_cand_cnt++];
}

// 为 remote_cands 保留目标（need）个槽位。（新分配空间会被置 0，返回 0 成功，-1 OOM）
static inline int p2p_remote_cands_reserve(p2p_session_t *s, int need) {
    if (need <= s->remote_cand_cap) return 0;
    int nc = s->remote_cand_cap > 0 ? s->remote_cand_cap : 8;
    while (nc < need) nc *= 2;
    p2p_remote_candidate_entry_t *p = (p2p_remote_candidate_entry_t *)realloc(
        s->remote_cands, nc * sizeof(p2p_remote_candidate_entry_t));
    if (!p) return -1;

    memset(p + s->remote_cand_cap, 0,
           (nc - s->remote_cand_cap) * sizeof(p2p_remote_candidate_entry_t));

    s->remote_cands    = p;
    s->remote_cand_cap = nc;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

#pragma clang diagnostic pop
#endif /* P2P_INTERNAL_H */
