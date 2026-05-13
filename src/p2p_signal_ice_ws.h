/*
 * p2p_signal_ice_ws — ICE 模式 WebSocket 信令
 *
 * 当 P2P_SIGNALING_MODE_ICE 且配置了 server_host 时，
 * 通过 WebSocket 连接信令服务器进行 SDP/候选交换。
 *
 * 协议（与 server.c WSS section 对应）：
 *
 *   客户端 → 服务器：
 *     REG <peer_id> <instance_id>  注册身份（instance_id 区分重连/重启）
 *     OFF                          主动下线
 *     SYNC0 <remote_peer_id>      创建/恢复会话
 *     SYNC <to_peer_id>\n<payload> 同步数据给指定 peer
 *     FIN <session_id>            主动断开会话
 *
 *   服务器 → 客户端：
 *     REG OK <sync_max>           注册成功
 *     REG FAIL <reason>           注册失败
 *     SYNC0 <peer_id> <session_id> online|offline  会话应答/对端上线推送
 *     SYNC <from_peer_id>\n<payload> 来自其他 peer 的同步数据
 *     FIN <session_id>            对端断连通知
 *
 * payload 格式（纯文本，应用层约定）：
 *     SDP\n<sdp_text>             SDP offer/answer（含 ice-ufrag/ice-pwd + candidates）
 *     ICE\n<candidate_line>       Trickle ICE 候选（单行 WebRTC 格式）
 *     ICE_DONE                    候选收集完成
 */

#ifndef P2P_SIGNAL_ICE_WS_H
#define P2P_SIGNAL_ICE_WS_H

#ifdef WITH_WS

#include <stdint.h>
#include <stdbool.h>

/* 前向声明 */
struct p2p_instance;
struct p2p_session;

/* ============================================================================
 * 实例级上下文（嵌入 sig_ctx union）
 * ============================================================================ */

typedef enum {
    ICE_WS_INIT = 0,        /* 未连接 */
    ICE_WS_CONNECTING,      /* WS 握手中 */
    ICE_WS_REGISTERING,     /* 已连接，等待 REG_ACK */
    ICE_WS_REG,             /* 注册成功，可收发 */
    ICE_WS_ERROR,           /* 错误 */
} p2p_ice_ws_state_t;

typedef struct {
    void               *ws;             /* ws_client_t*（避免暴露 ws_client.h） */
    p2p_ice_ws_state_t  state;
    uint64_t            connect_time;   /* 连接发起时间 */
    uint32_t            instance_id;    /* 实例 ID（每次 online 生成新随机数） */
    bool                feature_relay;  /* 服务器支持数据包中继 */
    bool                feature_msg;    /* 服务器支持 MSG RPC */
} p2p_ice_ws_ctx_t;

/* ============================================================================
 * 会话级上下文（嵌入 sig_sess union）
 * ============================================================================ */

typedef enum {
    ICE_WS_SESS_IDLE = 0,      /* 未发送 */
    ICE_WS_SESS_OFFERING,      /* 已发送 SDP offer */
    ICE_WS_SESS_ANSWERING,     /* 已发送 SDP answer */
    ICE_WS_SESS_SYNCING,       /* 候选同步中（trickle） */
    ICE_WS_SESS_READY,         /* 候选同步完成 */
} p2p_ice_ws_sess_state_t;

typedef struct {
    p2p_ice_ws_sess_state_t state;
    int                     candidate_synced;   /* 已同步的本地候选数 */
    uint64_t                last_sync;          /* 上次同步时间 */
} p2p_ice_ws_session_t;

/* ============================================================================
 * API
 * ============================================================================ */

/* 实例级：初始化/销毁 WS 连接 */
void p2p_signal_ice_ws_init(struct p2p_instance *inst);
void p2p_signal_ice_ws_destroy(struct p2p_instance *inst);

/* 会话级：发起信令交换 */
int  p2p_signal_ice_ws_connect(struct p2p_session *s, const char *remote_peer_id);

/* 信令 tick（在 p2p_update 阶段 2 调用） */
void p2p_signal_ice_ws_tick(struct p2p_instance *inst, uint64_t now_ms);

#endif /* WITH_WS */
#endif /* P2P_SIGNAL_ICE_WS_H */
