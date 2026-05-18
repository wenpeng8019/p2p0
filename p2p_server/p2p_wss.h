//
// Created by 温朋 on 2026/4/19.
//
/*
 * WS ICE 信令：通过 WebSocket 为 P2P_SIGNALING_MODE_ICE 提供 SDP/ICE 交换
 *
 * 基于 client_t / session_t 体系，提供会话感知的信令服务。
 * 当对端上线时主动通知，触发 SDP 交换。
 *
 * 协议（纯文本帧，每条 WS text frame 一条消息）：
 *
 *   客户端 → 服务器：
 *     REG <peer_id>                 注册身份（类似 RELAY REG）
 *     OFF                           主动下线，立即释放服务器资源（与 REG 配对）
 *     SDP <remote_peer_id>\n<sdp>   按 peer_id 转发 SDP 文本（不依赖 session）
 *     SYN0 <remote_peer_id>        创建/恢复会话（类似 RELAY SYN0）
 *     SYN0 <remote_peer_id>\n<payload>  创建会话 + 携带预缓存负载
 *     SYNC <session_id_hex> <sid_hex>\n<payload>  按 session_id 路由同步数据（类似 RELAY SYNC）
 *     FIN <session_id>              主动断开会话（类似 RELAY FIN）
 *
 *   服务器 → 客户端：
 *     REG OK <sync_max>             注册成功（sync_max = 预缓存负载上限）
 *     注册失败不再返回文本协议；服务器直接发送 WS close，具体细节放在 close reason 中
 *     SDP OK <remote_peer_id>       SDP 转发成功
 *     SDP FAIL <remote_peer_id> <reason>  SDP 转发失败
 *     SYN0 <peer_id> <session_id_hex> online|offline  应答/推送
 *     SYN0 FAIL <reason>           参数非法或 payload 超限
 *     SYN0 FAIL <reason>           会话创建失败
 *     SYNC <session_id_hex> <sid_hex>\n<line>     对端同步数据（流式，每行一帧）
 *     SYNC <session_id_hex> <sid_hex>\n\n         fin mark，本批次传输结束
 *     SYNC <session_id_hex> <sid_hex> confirm  同步数据逐包确认（发给发送方）
 *     SYNC <session_id_hex> <sid_hex> busy  同步缓存空间不足
 *     STA <session_id_hex> <req_type> <status>  统一状态应答（PKT flow-control / 会话级错误）
 *     FIN <session_id>               会话结束通知 (C2S & S2C)
 *
 *   二进制帧（WebSocket binary frame, 用于 P2P 数据中继和 MSG RPC）:
 *   帧格式: [type(1)][session_id(4)][payload(N)]，服务器重写 session_id 并透传
 *     PACKET (0x01): [0x01][ses_id(4)][p2p_hdr(4)][data(N)]  中继 P2P 包
 *     REQ    (0x02): [0x02][ses_id(4)][sid(2)][msg(1)][data(N)]  RPC 请求
 *     RSP   (0x03): [0x03][ses_id(4)][sid(2)][code(1)][data(N)] RPC 响应
 *   错误（服务器生成伪 RSP / STA）:
 *     code=0xFF: 对端离线 (P2P_RPC_ERR_PEER_OFF)
 *     code=0xFE: 转发超时 (P2P_RPC_ERR_TIMEOUT)
 */

#ifndef P2P_P2P_WSS_H
#define P2P_P2P_WSS_H

#include "common.h"
#include "custom_ws.h"

#define WSS_MAX_PAYLOAD             P2P_MAX_PAYLOAD /* max_payload_len for custom_tcp / REG OK sync_max */

/* SYNC/PKT 发送队列深度（与 relay 对齐） */
#define WSS_PEER_Q_MAX              2u

typedef struct wss_session {
    session_t                       base;
    CUSTOM_TCP_SESSION

    uint8_t                         last_sid;               /* 最后一个已分配/接收的 SYNC sid；0 保留给 SYN0 */

    /* SYNC 发送队列（对标 relay sync_peer_send，含 ACK_PENDING 机制） */
    buf16_item_t*                   sync_peer_slots[WSS_PEER_Q_MAX];    /* 循环队列的 slots 数组（指针） */
    buffer_round_t                  sync_peer_send;                     /* 发送队列（循环缓冲）
                                                                        * 队头 FRONT 的发送状态由 refer 判定：
                                                                        *   refer=NULL             → 待发但未启动（对端暂不可达，或 sid=0 payload 待组装）
                                                                        *   refer=session          → TCP 写入中
                                                                        *   refer=ITEM_REF_ACK_PENDING → 等待应用层 ACK */

    /* PKT 发送队列（对标 relay pkt_peer_send） */
    buf16_item_t*                   pkt_peer_slots[WSS_PEER_Q_MAX];     /* 循环队列的 slots 数组（指针） */
    buffer_round_t                  pkt_peer_send;                      /* 发送队列（循环缓冲） */

    /* MSG RPC 状态（与 relay_session_t 一致，独立于 SYNC 通道） */
    uint16_t                        rpc_pending_sid;        /* 0=空闲, 非零=等待 RSP 的 REQ sid */
    uint64_t                        rpc_sent_time;          /* REQ 转发时间戳（用于超时检测） */
    struct wss_session*             rpc_pending_prev;       /* RPC 超时队列前驱指针（用于 timeout_queue_t） */
    struct wss_session*             rpc_pending_next;       /* RPC 超时队列后继指针（用于 timeout_queue_t） */
} wss_session_t;

typedef struct wss_client {
    client_t                        base;
    TCP_CLIENT
    CUSTOM_TCP_CLIENT
    CUSTOM_WS_CLIENT

    UT_hash_handle                  hh;          /* g_wss_clients，按 base.local_peer_id 索引 */
} wss_client_t;

#define WSS_IO_FLAG_CLOSING         CW_IO_FLAG_CLOSING

cw_client_ctx_t*
wss_init(void);

void
retry_wss_pending(uint64_t now);

#endif //P2P_P2P_WSS_H
