
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
#include "p2p_signal_protocol.h" /* 信令协议序列化 */

/* ============================================================================
 * 流层分片常量
 * ============================================================================ */

/* DATA 子包头 (5 bytes, 作为负载数据的一部分) */
#define P2P_FRAG_FIRST    0x01
#define P2P_FRAG_LAST     0x02
#define P2P_FRAG_WHOLE    0x03              /* FIRST | LAST */

typedef struct {
    uint32_t stream_offset;                 /* 网络字节序 */
    uint8_t  frag_flags;
} p2p_data_hdr_t;

#define P2P_DATA_HDR_SIZE   5
#define P2P_STREAM_PAYLOAD  (P2P_MAX_PAYLOAD - P2P_DATA_HDR_SIZE)  /* 1191 */

/* ============================================================================
 * 可靠传输层 (Reliable Transport Layer)
 * ============================================================================
 *
 * 基于 ARQ (Automatic Repeat reQuest) 的可靠传输机制。
 *
 * 核心概念：
 *   - 滑动窗口：限制未确认数据包数量
 *   - 序列号：标识数据包顺序
 *   - RTO：重传超时，基于 RTT 动态计算
 *
 * 滑动窗口示意：
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 10│ ... │
 *   └─────────────────────────────────────────────────────────────┘
 *     ↑               ↑                   ↑
 *   已确认          send_base           send_seq
 *   (可释放)        (最小未确认)         (下一个发送)
 *
 *                     |←── 发送窗口 ──→|
 *
 * RTO 计算（RFC 6298）：
 *   SRTT    = (1-α) * SRTT + α * RTT_sample    (α = 1/8)
 *   RTTVAR  = (1-β) * RTTVAR + β * |SRTT - RTT_sample|  (β = 1/4)
 *   RTO     = SRTT + max(G, 4 * RTTVAR)        (G = 时钟粒度)
 */

#define RELIABLE_WINDOW   32      /* 滑动窗口大小（最大未确认数据包数） */
#define RELIABLE_RTO_INIT 200     /* 初始 RTO (毫秒) */
#define RELIABLE_RTO_MAX  2000    /* 最大 RTO (毫秒) */

/*
 * retx_entry_t: 重传队列条目
 *
 * 存储待确认数据包的完整信息，用于超时重传。
 */
typedef struct {
    uint8_t  data[P2P_MAX_PAYLOAD];  /* 数据包内容 */
    int      len;                     /* 数据包长度 */
    uint16_t seq;                     /* 序列号 */
    uint64_t send_time;               /* 发送时间戳 (毫秒) */
    int      retx_count;              /* 已重传次数 */
    int      acked;                   /* 是否已确认 (1=已确认) */
} retx_entry_t;

/*
 * reliable_s: 可靠传输层状态
 *
 * 实现基于序列号的可靠传输，包含发送端和接收端状态。
 */
struct reliable_s {
    /* ======================== 发送端状态 ======================== */
    uint16_t     send_seq;                              /* 下一个待分配的序列号 */
    uint16_t     send_base;                             /* 最小未确认的序列号 */
    retx_entry_t send_buf[RELIABLE_WINDOW];             /* 发送缓冲区（环形） */
    int          send_count;                            /* 缓冲区中待确认数据包数 */

    /* ======================== 接收端状态 ======================== */
    uint16_t     recv_base;                             /* 下一个期望的序列号 */
    uint8_t      recv_bitmap[RELIABLE_WINDOW];          /* 接收位图（标记已收到的包） */
    uint8_t      recv_data[RELIABLE_WINDOW][P2P_MAX_PAYLOAD]; /* 乱序数据缓存 */
    int          recv_lens[RELIABLE_WINDOW];            /* 各槽位数据长度 */

    /* ======================== RTT 估计 ======================== */
    int          srtt;                                  /* 平滑 RTT (毫秒) */
    int          rttvar;                                /* RTT 方差 */
    int          rto;                                   /* 当前重传超时 (毫秒) */
};
typedef struct reliable_s reliable_t;

/* stream_t: 流传输层状态（定义在 p2p_stream.h） */
typedef struct stream_s stream_t;

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
    p2p_config_t            cfg;                /* 用户配置（STUN 服务器、模式等） */
    int                     state;              /* 连接状态 P2P_STATE_* */
    int                     path;               /* 连接路径 P2P_PATH_* */

    /* ======================== Socket 资源 ======================== */
    int                     sock;               /* UDP 套接字描述符 */
    int                     tcp_sock;           /* TCP 套接字（打洞/回退用） */
    struct sockaddr_in      active_addr;        /* 当前通信目标地址 */

    /* ======================== NAT 检测 ======================== */
    p2p_stun_nat_type_t     nat_type;           /* 本地 NAT 类型 */
    int                     det_step;           /* 当前检测步骤 det_step_t */
    uint64_t                det_last_send;      /* 上次发送检测包时间 */
    int                     det_retries;        /* 当前步骤重试次数 */

    /* ======================== ICE 状态 ======================== */
    p2p_ice_state_t         ice_state;          /* ICE 协商状态 */
    p2p_candidate_t         local_cands[P2P_MAX_CANDIDATES];   /* 本地候选地址 */
    int                     local_cand_cnt;     /* 本地候选数量 */
    p2p_candidate_t         remote_cands[P2P_MAX_CANDIDATES];  /* 远端候选地址 */
    int                     remote_cand_cnt;    /* 远端候选数量 */

    /* ======================== 信令上下文 ======================== */
    /*
     * 信令模块负责在两个对等体之间交换连接信息（候选地址、密钥等）。
     * 支持三种模式：
     *   - sig_compact_ctx: COMPACT模式，UDP 无状态信令
     *   - sig_relay_ctx:  ICE模式，TCP 中继信令
     *   - sig_pubsub_ctx: PUBSUB模式，通过 GitHub Gist
     */
    signal_compact_ctx_t     sig_compact_ctx;     /* COMPACT 模式信令上下文 */
    p2p_signal_relay_ctx_t  sig_relay_ctx;      /* RELAY 模式信令上下文 */
    p2p_signal_pubsub_ctx_t sig_pubsub_ctx;     /* PUB/SUB 模式信令上下文 */
    int                     signaling_mode;     /* 信令模式 P2P_CONNECT_MODE_* */
    char                    remote_peer_id[P2P_PEER_ID_MAX]; /* 目标对等体 ID */
    int                     signal_sent;        /* 是否已发送初始信令 */
    uint64_t                last_signal_time;   /* 上次发送信令的时间戳 (ms) */
    int                     last_cand_cnt_sent; /* 上次发送时的候选数量 */
    int                     cands_pending_send; /* 有待发送的候选（TCP 发送失败时置 1） */

    /* ======================== 传输层实例 ======================== */
    nat_ctx_t               nat;                /* NAT 穿透上下文 */
    route_ctx_t             route;              /* 路由表上下文 */
    reliable_t              reliable;           /* 可靠传输层状态 */
    stream_t                stream;             /* 流传输层状态 */

    /* ======================== 模块化传输 ======================== */
    /*
     * 可插拔传输层架构，支持多种实现：
     *   - simple:    无加密直接传输
     *   - mbedtls:   DTLS 加密 (MbedTLS)
     *   - sctp:      SCTP 协议 (usrsctp)
     *   - pseudotcp: 模拟 TCP 拥塞控制
     */
    const p2p_transport_ops_t* trans;           /* 传输层操作函数表 */
    void*                   transport_data;     /* 传输层私有数据 */

    /* ======================== PseudoTCP 拥塞控制 ======================== */
    /*
     * 类 TCP 拥塞控制算法 (AIMD)：
     *   - cwnd:     拥塞窗口，控制发送速率
     *   - ssthresh: 慢启动阈值
     *   - dup_acks: 重复 ACK 计数（触发快速重传）
     *   - sack:     选择确认位图
     */
    struct {
        uint32_t cwnd;      /* 拥塞窗口 (字节/包数) */
        uint32_t ssthresh;  /* 慢启动阈值 */
        uint32_t dup_acks;  /* 重复 ACK 计数（>=3 触发快速重传） */
        uint32_t sack;      /* SACK 位图 */
        uint64_t last_ack;  /* 上次收到 ACK 的时间戳 */
        int      cc_state;  /* 拥塞控制状态 TCP_STATE_* */
    } tcp;

    /* ======================== 定时器 ======================== */
    uint64_t            last_update;            /* 上次调用 p2p_update() 的时间 */

#ifdef P2P_THREADED
    /* ======================== 多线程支持 ======================== */
    /*
     * 启用 P2P_THREADED 时，会话在独立线程中运行。
     * 需要互斥锁保护共享状态。
     */
    pthread_t           thread;                 /* 工作线程 */
    pthread_mutex_t     mtx;                    /* 互斥锁 */
    int                 thread_running;         /* 线程是否运行中 */
    int                 quit;                   /* 退出标志 */
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

/* ============================================================================
 * 传输层函数声明
 * ============================================================================
 *
 * 这些函数由各传输层模块实现，供上层调用。
 */

/* ------------------------------ p2p_trans_reliable.c ------------------------------ */
/*
 * 可靠传输层：实现 ARQ 重传机制
 */

/* 发送数据包（加入发送缓冲区等待确认） */
int  reliable_send_pkt(reliable_t *r, const uint8_t *data, int len);

/* 接收已确认的顺序数据包 */
int  reliable_recv_pkt(reliable_t *r, uint8_t *buf, int *out_len);

/* 处理收到的数据包（乱序缓存） */
int  reliable_on_data(reliable_t *r, uint16_t seq, const uint8_t *payload, int len);

/* 处理收到的 ACK（释放已确认数据包） */
int  reliable_on_ack(reliable_t *r, uint16_t ack_seq, uint32_t sack_bits);

/* 定时 ACK 处理（检查超时重传） */
void reliable_tick_ack(reliable_t *r, int sock, const struct sockaddr_in *addr);

/* 查询发送窗口剩余空间 */
int  reliable_window_avail(const reliable_t *r);

/* ------------------------------ p2p_trans_pseudotcp.c ------------------------------ */
/*
 * PseudoTCP：类 TCP 拥塞控制
 */

/* ACK 回调（更新拥塞窗口） */
void p2p_pseudotcp_on_ack(struct p2p_session *s, uint16_t ack_seq);

#endif /* P2P_INTERNAL_H */
