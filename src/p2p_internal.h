
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

#include <p2p.h>
#include <p2pp.h>           /* 协议定义 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* 多线程支持（可选） */
#ifdef P2P_THREADED
#include <pthread.h>
#endif

#include "p2p_nat.h"           /* NAT 穿透与类型检测 */
#include "p2p_route.h"         /* 路由表管理 */
#include "p2p_stream.h"        /* 流式数据传输 */
#include "p2p_transport.h"     /* 传输层抽象接口 */
#include "p2p_udp.h"           /* UDP 传输层常量 (P2P_MTU, P2P_MAX_PAYLOAD) */
#include "p2p_stun.h"          /* STUN 协议实现 */
#include "p2p_crypto.h"        /* 加密功能 */
#include "p2p_ice.h"           /* ICE 协议 */
#include "p2p_turn.h"          /* TURN 中继 */
#include "p2p_tcp_punch.h"     /* TCP 打洞 */
#include "p2p_thread.h"        /* 线程工具 */
#include "p2p_signal_relay.h"  /* 中继模式信令 */
#include "p2p_signal_pubsub.h" /* 发布/订阅模式信令 */
#include "p2p_signal_compact.h" /* COMPACT 模式信令 */

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
struct p2p_session {
    /* ======================== 配置与状态 ======================== */
    p2p_config_t                cfg;                // 用户配置（STUN 服务器、模式等）
    int                         state;              // 连接状态 P2P_STATE_*
    int                         path;               // 连接路径 P2P_PATH_*

    /* ======================== Socket 资源 ======================== */
    int                         sock;               // UDP 套接字描述符
    int                         tcp_sock;           // TCP 套接字（打洞/回退用）
    struct sockaddr_in          active_addr;        // 当前通信目标地址

    /* ======================== NAT 检测 ======================== */
    p2p_stun_nat_type_t         nat_type;           // 本地 NAT 类型
    int                         det_step;           // 当前检测步骤 det_step_t
    uint64_t                    det_last_send;      // 上次发送检测包时间
    int                         det_retries;        // 当前步骤重试次数

    /* ======================== ICE 状态 ======================== */
    p2p_ice_state_t             ice_state;          // ICE 协商状态
    p2p_candidate_t             local_cands[P2P_MAX_CANDIDATES];   // 本地候选地址
    int                         local_cand_cnt;     // 本地候选数量
    p2p_candidate_t             remote_cands[P2P_MAX_CANDIDATES];  // 远端候选地址
    int                         remote_cand_cnt;    // 远端候选数量

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
     */
    pthread_t                   thread;             // 工作线程
    pthread_mutex_t             mtx;                // 互斥锁
    int                         thread_running;     // 线程是否运行中
    int                         quit;               // 退出标志
#endif
};

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
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
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
 * sockaddr2n: sockaddr_in 转网络字节流（序列化）
 *
 * 格式（12 字节）：[sin_family: 4B] [sin_port: 4B] [sin_addr.s_addr: 4B]
 *
 * @param buf   输出缓冲区（至少 12 字节）
 * @param addr  要序列化的地址
 * @return      写入的字节数（12）
 */
static inline int sockaddr2n(uint8_t *buf, const struct sockaddr_in *addr) {
    int n = 0;
    *(uint32_t*)&buf[n] = htonl((uint32_t)addr->sin_family); n += 4;
    *(uint32_t*)&buf[n] = htonl((uint32_t)addr->sin_port); n += 4;
    *(uint32_t*)&buf[n] = addr->sin_addr.s_addr; n += 4;
    return n;
}

/*
 * n2sockaddr: 网络字节流转 sockaddr_in（反序列化）
 *
 * 格式（12 字节）：[sin_family: 4B] [sin_port: 4B] [sin_addr.s_addr: 4B]
 *
 * 注意：会清零整个结构体（包括 sin_zero 填充字段，某些系统要求填充字段为 0）
 *
 * @param buf   输入缓冲区（至少 12 字节）
 * @param addr  输出地址结构
 * @return      读取的字节数（12）
 */
static inline int n2sockaddr(const uint8_t *buf, struct sockaddr_in *addr) {
    int n = 0;
    memset(addr, 0, sizeof(*addr));  /* 清零 sin_zero 填充字段（8字节） */
    addr->sin_family = (sa_family_t)ntohl(*(uint32_t*)&buf[n]); n += 4;
    addr->sin_port = (in_port_t)ntohl(*(uint32_t*)&buf[n]); n += 4;
    addr->sin_addr.s_addr = *(uint32_t*)&buf[n]; n += 4;
    return n;
}

/* ============================================================================
 * 信令协议数据结构
 * ============================================================================ */

/*
 * p2p_signaling_payload_hdr_t: 信令负载头部结构
 *
 * 包含 ICE 信令的元数据（不包含候选数组）。
 * 候选数据通过 pack_candidate/unpack_candidate 单独序列化。
 * 可通过 Relay 服务器或 PubSub (GitHub Gist) 传输。
 */
typedef struct p2p_signaling_payload_hdr {
    char sender[32];              /* 发送方 local_peer_id */
    char target[32];              /* 目标方 local_peer_id */
    uint32_t timestamp;           /* 时间戳（用于排序和去重） */
    uint32_t delay_trigger;       /* 延迟触发打洞（毫秒） */
    int candidate_count;          /* ICE 候选数量（0-8） */
} p2p_signaling_payload_hdr_t;

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
    
    /* 验证候选数量 */
    if (p->candidate_count < 0 || p->candidate_count > 8) return -1;
    
    return 0;
}

/*
 * pack_candidate: 序列化单个 ICE 候选到字节流
 *
 * 格式（32 字节）：[type:4B][addr:12B][base_addr:12B][priority:4B]
 *
 * @param c    ICE 候选结构
 * @param buf  输出缓冲区（至少 32 字节）
 * @return     写入的字节数（32）
 */
static inline int pack_candidate(const p2p_candidate_t *c, uint8_t *buf) {
    int n = 0;
    *(uint32_t*)&buf[n] = htonl((uint32_t)c->type); n += 4;
    n += sockaddr2n(buf + n, &c->addr);
    n += sockaddr2n(buf + n, &c->base_addr);
    *(uint32_t*)&buf[n] = htonl(c->priority); n += 4;
    return n;
}

/*
 * unpack_candidate: 从字节流反序列化单个 ICE 候选
 *
 * 格式（32 字节）：[type:4B][addr:12B][base_addr:12B][priority:4B]
 *
 * @param c    输出：ICE 候选结构
 * @param buf  输入缓冲区（至少 32 字节）
 * @return     读取的字节数（32）
 */
static inline int unpack_candidate(p2p_candidate_t *c, const uint8_t *buf) {
    int n = 0;
    c->type = (p2p_cand_type_t)ntohl(*(uint32_t*)&buf[n]); n += 4;
    n += n2sockaddr(buf + n, &c->addr);
    n += n2sockaddr(buf + n, &c->base_addr);
    c->priority = ntohl(*(uint32_t*)&buf[n]); n += 4;
    return n;
}

#endif /* P2P_INTERNAL_H */
