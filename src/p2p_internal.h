
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>           /* PRIu64 */

#include <p2p.h>
#include <p2pp.h>               /* 协议定义 */

#include "p2p_platform.h"       /* 跨平台兼容层（socket / 线程 / 时钟 / sleep） */
#include "p2p_lang.h"           /* 多语言消息 / p2p_nat_type_str 依赖 */

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
    p2p_candidate_entry_t      *local_cands;        // 本地候选地址（动态分配）
    int                         local_cand_cnt;     // 本地候选数量
    int                         local_cand_cap;     // 本地候选容量
    p2p_candidate_entry_t      *remote_cands;       // 远端候选地址（动态分配）
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
    p2p_signal_compact_ctx_t    sig_compact_ctx;    // COMPACT 模式信令上下文
    p2p_signal_relay_ctx_t      sig_relay_ctx;      // RELAY 模式信令上下文
    p2p_signal_pubsub_ctx_t     sig_pubsub_ctx;     // PUB/SUB 模式信令上下文
    int                         signaling_mode;     // 信令模式 P2P_CONNECT_MODE_*
    char                        remote_peer_id[P2P_PEER_ID_MAX]; // 目标对等体 ID
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
 * NAT 类型转可读字符串（支持多语言）
 *
 * 覆盖所有负值（检测中/超时）和 p2p_nat_type_t 枚举值。
 * 通常传入 s->cfg.language 以匹配当前会话语言配置。
 */
static inline const char* p2p_nat_type_str(int type, p2p_language_t lang) {
    switch (type) {
        case P2P_NAT_DETECTING:        return p2p_msg_lang(MSG_NAT_TYPE_DETECTING,       lang);
        case P2P_NAT_TIMEOUT:          return p2p_msg_lang(MSG_NAT_TYPE_TIMEOUT,         lang);
        case P2P_NAT_UNKNOWN:          return p2p_msg_lang(MSG_NAT_TYPE_UNKNOWN,         lang);
        case P2P_NAT_OPEN:             return p2p_msg_lang(MSG_NAT_TYPE_OPEN,            lang);
        case P2P_NAT_FULL_CONE:        return p2p_msg_lang(MSG_NAT_TYPE_FULL_CONE,       lang);
        case P2P_NAT_RESTRICTED:       return p2p_msg_lang(MSG_NAT_TYPE_RESTRICTED,      lang);
        case P2P_NAT_PORT_RESTRICTED:  return p2p_msg_lang(MSG_NAT_TYPE_PORT_RESTRICTED, lang);
        case P2P_NAT_SYMMETRIC:        return p2p_msg_lang(MSG_NAT_TYPE_SYMMETRIC,       lang);
        case P2P_NAT_BLOCKED:          return p2p_msg_lang(MSG_NAT_TYPE_BLOCKED,         lang);
        case P2P_NAT_UNSUPPORTED:      return p2p_msg_lang(MSG_NAT_TYPE_UNSUPPORTED,     lang);
        default:                       return p2p_msg_lang(MSG_NAT_TYPE_UNKNOWN,         lang);
    }
}

/* ============================================================================
 * 内联工具函数
 * ============================================================================ */

/*
 * time_ms: 获取当前时间戳（毫秒）
 *
 * 使用 gettimeofday() 获取高精度时间，用于：
 *   - RTO 计时
 *   - RTT 测量
 *   - 心跳间隔
 *
 * @return 自 1970-01-01 00:00:00 UTC 以来的毫秒数
 */
static inline uint64_t time_ms(void) {
    return p2p_time_ms();
}

/*
 * seq_diff: 计算序列号差值（处理回绕）
 *
 * 使用有符号 16 位差值处理序列号回绕问题。
 *
 * 示例：
 *   seq_diff(5, 3)     = 2   (正常情况)
 *   seq_diff(3, 5)     = -2  (正常情况)
 *   seq_diff(1, 65535) = 2   (回绕情况：1 比 65535 新)
 *   seq_diff(65535, 1) = -2  (回绕情况：65535 比 1 旧)
 *
 * @param a  序列号 A
 * @param b  序列号 B
 * @return   a - b（考虑回绕）
 */
static inline int16_t seq_diff(uint16_t a, uint16_t b) {
    return (int16_t)(a - b);
}

/*
 * struct sockaddr_in → p2p_sockaddr_t
 *
 * sockaddr_in 各字段含义：
 *   sin_family : 主机字节序（uint16_t）
 *   sin_port   : 已是网络字节序（uint16_t，big-endian）
 *   sin_addr   : 已是网络字节序（uint32_t，big-endian）
 *
 * 统一用 htonl 写入 uint32_t，使得任意主机字序下字节流相同。
 * 注：sin_port 本身已是大端，(uint32_t) 零扩展后再 htonl，与 p2p_wire_to_sockaddr 的
 *     ntohl 对称，round-trip 正确。
 */
static inline void p2p_sockaddr_to_wire(const struct sockaddr_in *s, p2p_sockaddr_t *w) {
    w->family = htonl((uint32_t)s->sin_family);
    w->port   = htonl((uint32_t)s->sin_port);
    w->ip     = s->sin_addr.s_addr;     /* 已是网络字节序，直接存储 */
}

/*
 * p2p_sockaddr_t → struct sockaddr_in
 *
 * 自动清零 sin_zero[8] 填充字段（某些系统要求填充字段为 0）。
 * 跨平台安全：macOS 额外的 sin_len 字段也被 memset 清零。
 */
static inline void p2p_wire_to_sockaddr(const p2p_sockaddr_t *w, struct sockaddr_in *s) {
    memset(s, 0, sizeof(*s));
    s->sin_family      = (sa_family_t)ntohl(w->family);
    s->sin_port        = (in_port_t)  ntohl(w->port);
    s->sin_addr.s_addr = w->ip;
}

/* ============================================================================
 * 信令协议序列化函数
 * ============================================================================
 *
 * p2p_signaling_payload_hdr_t 定义在 p2pp.h 中
 * 这里仅提供序列化/反序列化的静态内联实现
 */

/*
 * pack_signaling_payload_hdr: 序列化信令负载头部到字节流
 *
 * 格式（76 字节）：[sender:32B][target:32B][timestamp:4B][delay_trigger:4B][count:4B]
 *
 * @param sender          发送方 peer_id
 * @param target          目标方 peer_id
 * @param timestamp       时间戳
 * @param delay_trigger   延迟触发（毫秒）
 * @param candidate_count 候选数量
 * @param buf             输出缓冲区（至少 76 字节）
 * @return                写入的字节数（76）
 */
static inline int pack_signaling_payload_hdr(
    const char *sender,
    const char *target,
    uint32_t timestamp,
    uint32_t delay_trigger,
    int candidate_count,
    uint8_t *buf
) {
    int n = 0, m;
    
    /* sender (32字节) */
    m = sizeof(((p2p_signaling_payload_hdr_t*)0)->sender) - 1;
    strncpy((char*)buf, sender, m); 
    buf[m] = '\0';
    n = m + 1;
    
    /* target (32字节) */
    m = sizeof(((p2p_signaling_payload_hdr_t*)0)->target) - 1;
    strncpy((char*)(buf + n), target, m); 
    buf[n + m] = '\0';
    n += m + 1;
    
    /* timestamp, delay_trigger, candidate_count */
    *(uint32_t*)&buf[n] = htonl(timestamp); n += 4;
    *(uint32_t*)&buf[n] = htonl(delay_trigger); n += 4;
    *(uint32_t*)&buf[n] = htonl((uint32_t)candidate_count); n += 4;
    
    return n;  /* 76 */
}

/*
 * unpack_signaling_payload_hdr: 从字节流反序列化信令负载头部
 *
 * 格式（76 字节）：[sender:32B][target:32B][timestamp:4B][delay_trigger:4B][count:4B]
 *
 * @param p    输出：信令负载结构（仅写入头部字段）
 * @param buf  输入缓冲区（至少 76 字节）
 * @return     0=成功，-1=失败
 */
static inline int unpack_signaling_payload_hdr(p2p_signaling_payload_hdr_t *p, const uint8_t *buf) {
    int n = 0;
    
    /* sender, target */
    memcpy(p->sender, buf + n, 32); n += 32;
    memcpy(p->target, buf + n, 32); n += 32;
    
    /* timestamp, delay_trigger, candidate_count */
    p->timestamp = ntohl(*(uint32_t*)&buf[n]); n += 4;
    p->delay_trigger = ntohl(*(uint32_t*)&buf[n]); n += 4;
    p->candidate_count = (int)ntohl(*(uint32_t*)&buf[n]); n += 4;
    
    /* 验证候选数量（防止恶意/畸形包，候选使用动态分配无固定上限）*/
    if (p->candidate_count < 0 || p->candidate_count > 200) return -1;
    
    return 0;
}

/*
 * pack_candidate: p2p_candidate_entry_t（内部平台格式）→ 网络字节流
 *
 * 格式（32 字节）：[type:4B][addr:12B][base_addr:12B][priority:4B]
 * 通过中间 p2p_candidate_t 完成转换，该类型在 pack(1) 块内定义，sizeof == 32。
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

static inline p2p_candidate_entry_t *p2p_cand_push_remote(p2p_session_t *s) {
    if (s->remote_cand_cnt >= s->remote_cand_cap) {
        int nc = s->remote_cand_cap > 0 ? s->remote_cand_cap * 2 : 8;
        p2p_candidate_entry_t *p = (p2p_candidate_entry_t *)realloc(
            s->remote_cands, nc * sizeof(p2p_candidate_entry_t));
        if (!p) return NULL;
        s->remote_cands    = p;
        s->remote_cand_cap = nc;
    }
    return &s->remote_cands[s->remote_cand_cnt++];
}

/* 确保 remote_cands 能按索引存放 need 个槽位（idx 随机访问时使用）。
 * 新分配空间清零，返回 0 成功，-1 OOM。 */
static inline int p2p_remote_cands_reserve(p2p_session_t *s, int need) {
    if (need <= s->remote_cand_cap) return 0;
    int nc = s->remote_cand_cap > 0 ? s->remote_cand_cap : 8;
    while (nc < need) nc *= 2;
    p2p_candidate_entry_t *p = (p2p_candidate_entry_t *)realloc(
        s->remote_cands, nc * sizeof(p2p_candidate_entry_t));
    if (!p) return -1;
    memset(p + s->remote_cand_cap, 0,
           (nc - s->remote_cand_cap) * sizeof(p2p_candidate_entry_t));
    s->remote_cands    = p;
    s->remote_cand_cap = nc;
    return 0;
}

#pragma clang diagnostic pop
#endif /* P2P_INTERNAL_H */
