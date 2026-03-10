/*
 * SCTP 传输层实现（基于 usrsctp 用户态库）
 *
 * ============================================================================
 * SCTP (Stream Control Transmission Protocol) 协议概述
 * ============================================================================
 *
 * SCTP 是一种面向消息的可靠传输协议，定义于 RFC 4960。
 * 在 WebRTC 中，SCTP 用于实现 DataChannel（数据通道）。
 *
 * SCTP 相比 TCP 的优势：
 * ┌────────────────────┬─────────────────────┬─────────────────────┐
 * │ 特性               │ TCP                 │ SCTP                │
 * ├────────────────────┼─────────────────────┼─────────────────────┤
 * │ 传输单位           │ 字节流              │ 消息（保留边界）    │
 * │ 多流支持           │ 单流                │ 多流独立传输        │
 * │ 队头阻塞           │ 有                  │ 无（流间独立）      │
 * │ 有序/无序          │ 仅有序              │ 可配置              │
 * │ 可靠/不可靠        │ 仅可靠              │ 可配置              │
 * │ 多宿主（Multihoming）│ 不支持            │ 支持                │
 * └────────────────────┴─────────────────────┴─────────────────────┘
 *
 * ============================================================================
 * SCTP 数据包格式
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Source Port Number        |     Destination Port Number   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Verification Tag                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Checksum (CRC32c)                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Chunk #1                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           ...                                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Chunk #N                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 公共头部（12 字节）：
 *   - Source Port (2字节):      源端口号
 *   - Destination Port (2字节): 目标端口号
 *   - Verification Tag (4字节): 验证标签（防止盲攻击）
 *   - Checksum (4字节):         CRC32c 校验和
 *
 * ============================================================================
 * SCTP Chunk 格式
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Chunk Type  |  Chunk Flags  |         Chunk Length          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Chunk Value ...                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 常见 Chunk 类型：
 *   0x00 - DATA:          用户数据
 *   0x01 - INIT:          建立关联请求
 *   0x02 - INIT ACK:      建立关联确认
 *   0x03 - SACK:          选择性确认
 *   0x04 - HEARTBEAT:     心跳
 *   0x05 - HEARTBEAT ACK: 心跳确认
 *   0x06 - ABORT:         中止关联
 *   0x07 - SHUTDOWN:      关闭关联
 *   0x0E - FORWARD TSN:   前向TSN（部分可靠扩展）
 *
 * ============================================================================
 * DATA Chunk 格式（用户数据）
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Type = 0    | Reserved|U|B|E|         Length                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                              TSN                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Stream Identifier        |   Stream Sequence Number      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Payload Protocol Identifier                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         User Data ...                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 标志位：
 *   - U (Unordered): 1=无序传输，0=有序传输
 *   - B (Beginning): 1=分片消息的开始
 *   - E (Ending):    1=分片消息的结束
 *
 * 字段说明：
 *   - TSN：传输序列号（用于可靠性确认）
 *   - Stream Identifier：流标识符（多流复用）
 *   - Stream Sequence Number：流内序列号（有序传输用）
 *   - PPID：协议标识符（WebRTC DataChannel 使用特定值）
 *
 * ============================================================================
 * WebRTC DataChannel 中的 SCTP
 * ============================================================================
 *
 *  应用层数据
 *      ↓
 *  ┌─────────────────┐
 *  │  SCTP (usrsctp) │  ← 用户态 SCTP 实现
 *  └─────────────────┘
 *      ↓
 *  ┌─────────────────┐
 *  │     DTLS        │  ← 加密传输
 *  └─────────────────┘
 *      ↓
 *  ┌─────────────────┐
 *  │   ICE / UDP     │  ← NAT 穿透
 *  └─────────────────┘
 *
 * usrsctp 是用户态 SCTP 库，不依赖内核支持。
 * 本模块将 usrsctp 输出的数据包封装到 UDP 中传输。
 *
 * ============================================================================
 * 本实现说明
 * ============================================================================
 *
 * 实现说明：
 *   - 使用 AF_CONN 虚拟地址族，SCTP 数据包封装在 UDP 中传输
 *   - 单线程模式使用 usrsctp_init_nothreads + usrsctp_handle_timers
 *   - 多线程模式使用 usrsctp_init（usrsctp 内部管理定时器线程）
 *   - 接收使用 upcall 回调，将数据写入 stream.recv_ring
 */

#define MOD_TAG "SCTP"

#include "p2p_internal.h"
#include <usrsctp.h>

#ifndef htonl
#include <arpa/inet.h>
#endif

/* SCTP 端口（双方使用相同端口，AF_CONN 下无实际意义） */
#define SCTP_LOCAL_PORT  5000

/* PPID: 二进制数据（区分 WebRTC DataChannel 的文本/二进制/控制） */
#define PPID_BINARY  htonl(53)

/* usrsctp 全局初始化引用计数（多个 session 共享同一个 usrsctp 实例） */
static int g_sctp_ref_count = 0;

/*
 * SCTP 上下文结构
 */
typedef struct {
    struct socket *sock;        /* usrsctp socket 句柄 */
    int            state;       /* 0=未连接, 1=连接中, 2=已连接 */
    uint64_t       last_tick;   /* 上次 handle_timers 时间戳 (ms) */
} p2p_sctp_ctx_t;

/* ============================================================================
 * 出站回调：usrsctp → UDP
 * ============================================================================ */
static int p2p_sctp_out(void *addr, void *buffer, size_t length, uint8_t tos, uint8_t set_df) {
    p2p_session_t *s = (p2p_session_t *)addr;
    (void)tos; (void)set_df;

    if (!s || length > P2P_MTU) return -1;

    p2p_send_packet(s, &s->active_addr, P2P_PKT_DATA, 0, 0, buffer, (int)length);
    return 0;
}

/* ============================================================================
 * Upcall 回调：usrsctp 有数据可读时调用
 * ============================================================================ */
static void sctp_upcall(struct socket *sock, void *arg, int flags) {
    p2p_session_t *s = (p2p_session_t *)arg;
    (void)flags;
    if (!s) return;
    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    if (!ctx) return;

    int events = usrsctp_get_events(sock);

    /* 处理可读事件 */
    if (events & SCTP_EVENT_READ) {
        for (;;) {
            uint8_t buf[P2P_MTU];
            struct sockaddr_conn from;
            socklen_t fromlen = sizeof(from);
            struct sctp_rcvinfo rcvinfo;
            socklen_t infolen = sizeof(rcvinfo);
            unsigned int infotype = 0;
            int msg_flags = 0;

            ssize_t n = usrsctp_recvv(sock, buf, sizeof(buf),
                                      (struct sockaddr *)&from, &fromlen,
                                      &rcvinfo, &infolen, &infotype, &msg_flags);
            if (n <= 0) break;

            if (msg_flags & MSG_NOTIFICATION) {
                /* 处理 SCTP 事件通知 */
                union sctp_notification *notif = (union sctp_notification *)buf;
                if ((size_t)n >= sizeof(notif->sn_header)) {
                    switch (notif->sn_header.sn_type) {
                    case SCTP_ASSOC_CHANGE: {
                        struct sctp_assoc_change *sac = &notif->sn_assoc_change;
                        if (sac->sac_state == SCTP_COMM_UP) {
                            ctx->state = 2;
                            print("I:", LA_S("[SCTP] association established", LA_S34, 34));
                        } else if (sac->sac_state == SCTP_COMM_LOST ||
                                   sac->sac_state == SCTP_SHUTDOWN_COMP ||
                                   sac->sac_state == SCTP_CANT_STR_ASSOC) {
                            ctx->state = 0;
                            print("W:", LA_F("[SCTP] association lost/shutdown (state=%u)", LA_F320, 320),
                                  sac->sac_state);
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
                continue;
            }

            /* 应用数据 → stream recv_ring */
            ring_write(&s->stream.recv_ring, buf, (int)n);
        }
    }
}

/* ============================================================================
 * 订阅 SCTP 事件通知
 * ============================================================================ */
static void sctp_subscribe_events(struct socket *sock) {
    uint16_t event_types[] = {
        SCTP_ASSOC_CHANGE,
        SCTP_PEER_ADDR_CHANGE,
        SCTP_SHUTDOWN_EVENT,
        SCTP_SEND_FAILED_EVENT,
        SCTP_SENDER_DRY_EVENT,
    };
    for (int i = 0; i < (int)(sizeof(event_types) / sizeof(event_types[0])); i++) {
        struct sctp_event event;
        memset(&event, 0, sizeof(event));
        event.se_assoc_id = SCTP_ALL_ASSOC;
        event.se_type = event_types[i];
        event.se_on = 1;
        usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT,
                           &event, sizeof(event));
    }
}

/* ============================================================================
 * 初始化 SCTP 传输层
 * ============================================================================ */
static int sctp_init(p2p_session_t *s) {
    p2p_sctp_ctx_t *ctx = calloc(1, sizeof(p2p_sctp_ctx_t));
    if (!ctx) return -1;
    s->trans_data = ctx;

    /* 全局初始化（仅首次） */
    if (g_sctp_ref_count == 0) {
#ifdef P2P_THREADED
        usrsctp_init(0, p2p_sctp_out, NULL);
#else
        usrsctp_init_nothreads(0, p2p_sctp_out, NULL);
#endif
        usrsctp_sysctl_set_sctp_blackhole(2);           /* 静默丢弃未知关联的包 */
        usrsctp_sysctl_set_sctp_no_csum_on_loopback(0); /* 始终校验 CRC32c */
        usrsctp_sysctl_set_sctp_ecn_enable(0);          /* 关闭 ECN（UDP 封装无 ECN 支持） */
    }
    g_sctp_ref_count++;

    usrsctp_register_address(s);

    /* 创建 SCTP socket（AF_CONN: 应用管理传输，非内核协议栈） */
    struct socket *sock = usrsctp_socket(
        AF_CONN, SOCK_STREAM, IPPROTO_SCTP,
        NULL,   /* receive_cb = NULL，使用 upcall 模式 */
        NULL,   /* send_cb */
        0,      /* sb_threshold */
        s       /* ulp_info */
    );
    if (!sock) {
        print("E:", LA_S("[SCTP] usrsctp_socket failed", LA_S36, 36));
        goto fail;
    }
    ctx->sock = sock;

    /* 设置 upcall（数据可读时回调） */
    usrsctp_set_upcall(sock, sctp_upcall, s);

    /* 设为非阻塞 */
    usrsctp_set_non_blocking(sock, 1);

    /* 配置 socket 选项 */
    int on = 1;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_NODELAY, &on, sizeof(on));
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_RECVRCVINFO, &on, sizeof(on));

    /* 配置初始流参数 */
    struct sctp_initmsg initmsg;
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams  = 1;     /* P2P 只需 1 条流 */
    initmsg.sinit_max_instreams = 1;
    initmsg.sinit_max_attempts  = 3;
    initmsg.sinit_max_init_timeo = 5000; /* 5 秒 */
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_INITMSG,
                       &initmsg, sizeof(initmsg));

    /* 启用流重置（WebRTC DataChannel 要求） */
    struct sctp_assoc_value av;
    memset(&av, 0, sizeof(av));
    av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ |
                     SCTP_ENABLE_RESET_ASSOC_REQ  |
                     SCTP_ENABLE_CHANGE_ASSOC_REQ;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET,
                       &av, sizeof(av));

    /* 订阅事件通知 */
    sctp_subscribe_events(sock);

    /* 构造本地地址并 bind */
    struct sockaddr_conn sconn;
    memset(&sconn, 0, sizeof(sconn));
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    sconn.sconn_len = sizeof(sconn);
#endif
    sconn.sconn_family = AF_CONN;
    sconn.sconn_port = htons(SCTP_LOCAL_PORT);
    sconn.sconn_addr = s;

    if (usrsctp_bind(sock, (struct sockaddr *)&sconn, sizeof(sconn)) < 0) {
        print("E:", LA_F("[SCTP] bind failed: %s", LA_F321, 321), strerror(errno));
        goto fail;
    }

    /* 发起连接（对端地址也用同一个 session 指针，AF_CONN 下地址仅做路由标识） */
    struct sockaddr_conn peer;
    memset(&peer, 0, sizeof(peer));
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    peer.sconn_len = sizeof(peer);
#endif
    peer.sconn_family = AF_CONN;
    peer.sconn_port = htons(SCTP_LOCAL_PORT);
    peer.sconn_addr = s;

    ctx->state = 1; /* 连接中 */
    int ret = usrsctp_connect(sock, (struct sockaddr *)&peer, sizeof(peer));
    if (ret < 0 && errno != EINPROGRESS) {
        print("E:", LA_F("[SCTP] connect failed: %s", LA_F322, 322), strerror(errno));
        goto fail;
    }

    ctx->last_tick = P_tick_ms();
    print("I:", LA_S("[SCTP] usrsctp initialized, connecting...", LA_S35, 35));
    return 0;

fail:
    if (ctx->sock) {
        usrsctp_close(ctx->sock);
        ctx->sock = NULL;
    }
    usrsctp_deregister_address(s);
    g_sctp_ref_count--;
    if (g_sctp_ref_count == 0) usrsctp_finish();
    free(ctx);
    s->trans_data = NULL;
    return -1;
}

/* ============================================================================
 * 发送数据
 * ============================================================================ */
static int sctp_send(p2p_session_t *s, const void *buf, int len) {
    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    if (!ctx || !ctx->sock || ctx->state != 2) return -1;

    struct sctp_sendv_spa spa;
    memset(&spa, 0, sizeof(spa));
    spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
    spa.sendv_sndinfo.snd_sid = 0;             /* 流 0 */
    spa.sendv_sndinfo.snd_flags = SCTP_EOR;    /* 完整消息 */
    spa.sendv_sndinfo.snd_ppid = PPID_BINARY;

    ssize_t sent = usrsctp_sendv(ctx->sock, buf, (size_t)len,
                                 NULL, 0,
                                 &spa, sizeof(spa),
                                 SCTP_SENDV_SPA, 0);
    if (sent < 0) {
        if (errno == EWOULDBLOCK) return 0; /* 发送缓冲区满，下次重试 */
        print("W:", LA_F("[SCTP] sendv failed: %s", LA_F323, 323), strerror(errno));
        return -1;
    }
    return (int)sent;
}

/* ============================================================================
 * 周期性处理
 * ============================================================================ */
static void sctp_tick(p2p_session_t *s) {
    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    if (!ctx) return;

#ifndef P2P_THREADED
    /* 单线程模式：手动驱动 usrsctp 定时器 */
    uint64_t now = P_tick_ms();
    uint32_t elapsed = (uint32_t)(now - ctx->last_tick);
    if (elapsed > 0) {
        usrsctp_handle_timers(elapsed);
        ctx->last_tick = now;
    }
#endif
}

/* ============================================================================
 * 处理入站 SCTP 数据包（UDP → usrsctp）
 * ============================================================================ */
static void sctp_on_packet(struct p2p_session *s, uint8_t type, const uint8_t *payload,
                           int len, const struct sockaddr_in *from) {
    if (type != P2P_PKT_DATA && type != P2P_PKT_RELAY_DATA) return;
    (void)from;

    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    if (!ctx) return;

    /* 将 UDP 中提取出的 SCTP 数据包送入 usrsctp 协议栈 */
    usrsctp_conninput(s, payload, (size_t)len, 0);
}

/* ============================================================================
 * 获取传输层统计
 * ============================================================================ */
static int sctp_get_stats(struct p2p_session *s, uint32_t *rtt_ms, float *loss_rate) {
    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    if (!ctx || !ctx->sock || ctx->state != 2) return -1;

    struct sctp_status status;
    memset(&status, 0, sizeof(status));
    socklen_t len = sizeof(status);
    if (usrsctp_getsockopt(ctx->sock, IPPROTO_SCTP, SCTP_STATUS,
                           &status, &len) != 0)
        return -1;

    *rtt_ms = status.sstat_primary.spinfo_srtt;
    *loss_rate = 0.0f;  /* SCTP 内部管理重传，无直接丢包率指标 */
    return (*rtt_ms > 0) ? 0 : -1;
}

/* ============================================================================
 * 关闭
 * ============================================================================ */
static void sctp_close(p2p_session_t *s) {
    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    if (!ctx) return;

    if (ctx->sock) {
        usrsctp_close(ctx->sock);
        ctx->sock = NULL;
    }
    usrsctp_deregister_address(s);

    g_sctp_ref_count--;
    if (g_sctp_ref_count == 0) {
        usrsctp_finish();
    }

    free(ctx);
    s->trans_data = NULL;
}

/* ============================================================================
 * 就绪检查
 * ============================================================================ */
static int sctp_is_ready(struct p2p_session *s) {
    p2p_sctp_ctx_t *ctx = (p2p_sctp_ctx_t *)s->trans_data;
    return ctx && ctx->state == 2;
}

/* ============================================================================
 * 传输层操作表
 * ============================================================================ */
const p2p_trans_ops_t p2p_trans_sctp = {
    .name      = "SCTP-usrsctp",
    .init      = sctp_init,
    .close     = sctp_close,
    .send_data = sctp_send,
    .tick      = sctp_tick,
    .on_packet = sctp_on_packet,
    .is_ready  = sctp_is_ready,
    .get_stats = sctp_get_stats
};
