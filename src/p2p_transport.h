#ifndef P2P_TRANSPORT_H
#define P2P_TRANSPORT_H

#include <stdint.h>
#include "p2p_udp.h"

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
typedef struct reliable {
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
} reliable_t;

/* ------------------------------ p2p_trans_reliable.c ------------------------------ */
/*
 * 可靠传输层：实现 ARQ 重传机制
 */

/* 初始化 reliable 模块 */
void reliable_init(reliable_t *r);

/* 发送数据包（加入发送缓冲区等待确认） */
int  reliable_send_pkt(reliable_t *r, const uint8_t *data, int len);

/* 接收已确认的顺序数据包 */
int  reliable_recv_pkt(reliable_t *r, uint8_t *buf, int *out_len);

/* 处理收到的数据包（乱序缓存） */
int  reliable_on_data(reliable_t *r, uint16_t seq, const uint8_t *payload, int len);

/* 处理收到的 ACK（释放已确认数据包） */
int  reliable_on_ack(reliable_t *r, uint16_t ack_seq, uint32_t sack_bits);

/* 定时 ACK 处理（检查超时重传） */
void reliable_tick_ack(reliable_t *r, int sock, const struct sockaddr_in *addr, int is_relay_mode);

/* 周期 tick：发送队列中尚未发出的包、重传超时包、发送 ACK */
void reliable_tick(reliable_t *r, int sock, const struct sockaddr_in *addr, int is_relay_mode);

/* 查询发送窗口剩余空间 */
int  reliable_window_avail(const reliable_t *r);

/* ------------------------------ p2p_trans_pseudotcp.c ------------------------------ */
/*
 * PseudoTCP：类 TCP 拥塞控制
 */

/* ACK 回调（更新拥塞窗口） */
void p2p_pseudotcp_on_ack(struct p2p_session *s, uint16_t ack_seq);


/* ============================================================================
 * 传输层函数声明
 * ============================================================================
 *
 * 这些函数由各传输层模块实现，供上层调用。
 */

// 前置声明
struct p2p_session;
struct sockaddr_in;

// 传输层操作接口 (vtable)
typedef struct p2p_transport_ops {
    const char *name;

    /* 初始化传输模块 */
    int (*init)(struct p2p_session *s);

    /* 关闭传输模块 */
    void (*close)(struct p2p_session *s);

    /* 发送应用层数据 */
    int (*send_data)(struct p2p_session *s, const void *buf, int len);

    /* 周期性驱动逻辑 */
    void (*tick)(struct p2p_session *s);

    /* 处理来自底层的 UDP 包 */
    void (*on_packet)(struct p2p_session *s, uint8_t type, const uint8_t *payload, int len, const struct sockaddr_in *from);
    int  (*is_ready)(struct p2p_session *s);
} p2p_transport_ops_t;

// 已知的传输层具体实现
// 注：reliable 作为基础传输层，直接调用 reliable_* 函数，无需 VTable
extern const p2p_transport_ops_t p2p_trans_pseudotcp; /* 拥塞控制 */
extern const p2p_transport_ops_t p2p_trans_dtls;      /* DTLS (MbedTLS) */
extern const p2p_transport_ops_t p2p_trans_openssl;   /* DTLS (OpenSSL) */
extern const p2p_transport_ops_t p2p_trans_sctp;      /* SCTP (usrsctp) */

#endif /* P2P_TRANSPORT_H */
