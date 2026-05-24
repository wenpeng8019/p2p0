/*
 * p2p_signal_wss — WSS 模式 WebSocket 信令
 *
 * 基于 WebSocket 的信令协议，完全对齐 RELAY 模式的两阶段设计：
 *   阶段1: REG  — 建立"客户端-服务器"基础连接
 *   阶段2: SYN0 — 建立"我-对方"的会话
 *
 * 与 RELAY 的区别：
 *   - 传输层: WebSocket (文本帧) vs TCP (二进制)
 *   - 分帧: WS 自带边界 vs RELAY 需要 size 头
 *   - 协议: 文本命令 vs 二进制类型码
 *   - 数据中继: 二进制帧 vs 二进制包封装
 *
 * 协议详见 p2pp.h（WSS 模式协议）
 */

#ifndef P2P_SIGNAL_WSS_H
#define P2P_SIGNAL_WSS_H

#ifdef WITH_WS

#include <stdint.h>
#include <stdbool.h>
#include "../stdc/stdc.h"  /* ret_t, err_t, 错误码 */
#include "../include/p2pp.h"  /* P2P_PEER_ID_MAX 等常量 */

/* 前向声明 */
struct p2p_instance;
struct p2p_session;

/* ============================================================================
 * WSS 模式参数配置
 * ============================================================================ */

#define P2P_WSS_HEARTBEAT_INTERVAL_MS     20000       /* 心跳间隔（毫秒）*/
#define P2P_WSS_ACK_TIMEOUT_MS            5000        /* ACK 响应超时（毫秒）*/
#define P2P_WSS_TRICKLE_BATCH_MS          1000        /* Trickle 攒批窗口（毫秒）*/
#define P2P_WSS_MAX_CANDS_PER_SYNC        10          /* 每个 SYNC 最大候选数 */

/* ============================================================================
 * 实例级上下文（嵌入 sig_ctx union）
 * ============================================================================
 *
 * 对应 RELAY 的 p2p_relay_ctx_t，管理与 WSS 服务器的连接状态
 */

typedef enum {
    SIG_WSS_INIT = 0,                                 /* 未启动 */
    SIG_WSS_ERROR,                                    /* 错误状态 */
    SIG_WSS_CONNECTING,                               /* WS 连接建立中 */
    SIG_WSS_WAIT_REG_ACK,                             /* 等待 REG OK */
    SIG_WSS_REG,                                      /* 已上线，可发起会话 */
} p2p_wss_st;

typedef struct {
    /* 基础状态 */
    p2p_wss_st          state;                        /* 信令状态 */
    void               *ws;                           /* ws_client_t*（避免暴露 ws_client.h） */
    uint64_t            connect_time;                 /* 连接发起时间 */
    uint64_t            last_send_time;               /* 上次发送时间 */
    uint64_t            last_recv_time;               /* 上次接收时间 */
    int                 trickle_sessions;             /* 当前正在进行 trickle sync 的会话数量 */

    /* 身份标识 */
    char                local_peer_id[P2P_PEER_ID_MAX+1]; /* 本端名称 */
    uint32_t            instance_id;                  /* 本次 reg() 生成的实例 ID */

    /* 服务器能力（REG OK 返回）*/
    int                 sync_max;                     /* SYN0 预缓存负载字节上限 */
    bool                feature_relay;                /* 支持数据包中继 */
    bool                feature_msg;                  /* 支持 RPC 机制 */
} p2p_wss_ctx_t;

/* ============================================================================
 * 会话级上下文（嵌入 sig_sess union）
 * ============================================================================
 *
 * 对应 RELAY 的 p2p_relay_session_t，管理与对端的会话状态
 */

typedef enum {
    SIG_WSS_SESS_IDLE = 0,                            /* 未启动 */
    SIG_WSS_SESS_WAIT_REG,                            /* 等待实例级 REG 完成 */
    SIG_WSS_SESS_WAIT_SYN0_ACK,                       /* 等待 SYN0 响应 */
    SIG_WSS_SESS_WAIT_PEER,                           /* 对端离线，等待对端上线 */
    SIG_WSS_SESS_SYNCING,                             /* 候选同步中 */
    SIG_WSS_SESS_READY,                               /* 候选同步完成 */
    SIG_WSS_SESS_SUSPENDED,                           /* 会话已挂起（disconnect 后）*/
} p2p_wss_sess_state_t;

typedef struct {
    p2p_wss_sess_state_t state;

    /* 对端身份 */
    char                remote_peer_id[P2P_PEER_ID_MAX+1]; /* 对端 peer_id */

    /* 候选同步状态 */
    int                 candidate_syncing_base;       /* 本地候选同步起始索引 */
    int                 candidate_synced_count;       /* 已同步的本地候选数 */
    uint8_t             sync_sid;                     /* 当前 SYNC 批次序号（循环 1..255）*/
    uint64_t            trickle_last_time;            /* 上次 trickle 攒批时间 */

    /* RPC 消息上下文（仅当 feature_msg=true 时使用）*/
    uint16_t            rpc_last_sid;                 /* 最后完成的 sid */
    uint8_t             req_state;                    /* A端: 0=空闲 1=等待 RSP */
    uint16_t            req_sid;                      /* A端: 当前挂起的 RPC 序列号 */
    uint8_t             req_msg;                      /* A端: 挂起请求的消息 ID */
    uint16_t            resp_sid;                     /* B端: 待回应的 RPC 序列号 */
} p2p_wss_session_t;

/* ============================================================================
 * WSS 信令 API
 * ============================================================================ */

/*
 * 初始化 WSS 信令上下文
 */
void p2p_signal_wss_init(p2p_wss_ctx_t *ctx);

/*
 * 信令接收维护（拉取阶段）
 *
 * 处理 WS 消息接收和协议解析。
 * 在 p2p_update() 的阶段 2（信令拉取）中调用。
 */
void p2p_signal_wss_tick_recv(struct p2p_instance *inst, uint64_t now);

/*
 * 信令发送维护（推送阶段）
 *
 * 处理信令发送和重传：
 *   - 心跳保活
 *   - SYN0 重传
 *   - 上传候选（SYNC）
 *
 * 在 p2p_update() 的阶段 7（信令推送）中调用。
 */
void p2p_signal_wss_tick_send(struct p2p_instance *inst, uint64_t now);

/*
 * 客户端上线（阶段1：建立与服务器的 WS 连接）
 *
 * 创建 WS 连接，连接到 WSS 服务器，并发送 REG 消息。
 * 成功后进入 REG 状态，可以发起多个 SYN0 会话。
 *
 * @param inst          P2P 实例
 * @param local_peer_id 本端名称
 * @param server_host   服务器地址
 * @param server_port   服务器端口
 * @return              E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_wss_reg(struct p2p_instance *inst, const char *local_peer_id,
                          const char *server_host, int server_port);

/*
 * 客户端下线（阶段1：断开与服务器的 WS 连接）
 *
 * 发送 OFF 消息并关闭 WS 连接。
 */
void p2p_signal_wss_off(struct p2p_instance *inst);

/*
 * 会话级：建立与对端的会话（阶段2：发送 SYN0 请求）
 *
 * 向服务器请求建立与目标对端的会话，服务器分配 session_id。
 * 前提：必须已经处于 REG 状态。
 *
 * @param s               P2P 会话
 * @param remote_peer_id  目标对端名称
 * @return                E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_wss_syn0(struct p2p_session *s, const char *remote_peer_id);

/*
 * 断开当前会话（阶段2：发送 FIN 消息）
 *
 * 向服务器发送 FIN 消息，通知结束与对端的会话。
 * 清理会话状态后回到 REG 状态，可以再次发起 SYN0。
 *
 * @param s   P2P 会话
 * @return    E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_wss_fin(struct p2p_session *s);

/*
 * 通过 WSS 服务器转发数据包（DATA/ACK/CRYPTO）
 *
 * 此函数作为 signaling_relay_fn 回调，由 p2p_send_packet 调用。
 * 仅当 feature_relay=true 时可用。
 *
 * @param s           P2P 会话
 * @param type        包类型（P2P_PKT_DATA / P2P_PKT_ACK / P2P_PKT_CRYPTO）
 * @param flags       包标志
 * @param seq         序列号
 * @param payload     负载数据
 * @param payload_len 负载长度
 * @return            E_NONE=成功，E_BUSY=流控等待，其他=错误码
 */
ret_t p2p_signal_wss_pkt(struct p2p_session *s,
                          uint8_t type, uint8_t flags, uint16_t seq,
                          const void *payload, uint16_t payload_len);

/*
 * 通过 WSS 服务器向对端发起 RPC 请求
 *
 * 仅当 feature_msg=true 时可用。
 *
 * @param s    P2P 会话
 * @param msg  消息类型（0=echo，>0=应用自定义）
 * @param data 请求数据
 * @param len  数据长度
 * @return     E_NONE=成功，E_BUSY=已有挂起请求，其他=错误码
 */
ret_t p2p_signal_wss_req(struct p2p_session *s,
                          uint8_t msg, const void *data, int len);

/*
 * 通过 WSS 服务器向请求方回复 RPC 响应
 *
 * 仅当 feature_msg=true 时可用。
 *
 * @param s    P2P 会话
 * @param code 响应码
 * @param data 响应数据
 * @param len  数据长度
 * @return     E_NONE=成功，其他=错误码
 */
ret_t p2p_signal_wss_rsp(struct p2p_session *s,
                          uint8_t code, const void *data, int len);

#endif /* WITH_WS */
#endif /* P2P_SIGNAL_WSS_H */
