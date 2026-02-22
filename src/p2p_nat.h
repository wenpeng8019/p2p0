/*
 * NAT 穿透（纯打洞逻辑）
 *
 * 本模块只负责 NAT 打洞的核心逻辑：
 *   - PUNCH/PUNCH_ACK 交换
 *   - PING/PONG 心跳保活
 *   - 打洞状态管理
 *
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

#ifndef P2P_NAT_H
#define P2P_NAT_H

#include <p2p.h>
#include "p2p_platform.h"   /* cross-platform socket headers */

/* 前向声明 */
struct p2p_session;

/* 打洞状态 */
enum {
    NAT_INIT = 0,       /* 未启动 */
    NAT_PUNCHING,       /* 打洞中 */
    NAT_CONNECTED,      /* 已连接 */
    NAT_RELAY           /* 中继模式 */
};

/* 打洞上下文（精简版，候选列表在 session 中） */
typedef struct {
    int              state;                                   /* 打洞状态 */
    struct sockaddr_in peer_addr;                             /* 成功连接的对端地址 */
    uint64_t         last_send_time;                          /* 上次发送时间 */
    uint64_t         last_recv_time;                          /* 上次接收时间 */
    int              punch_attempts;                          /* 打洞尝试次数 */
    uint64_t         punch_start;                             /* 打洞开始时间 */
    int              verbose;                                 /* 是否输出详细日志 */
} nat_ctx_t;

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n);

/*
 * 开始打洞（从 session 的 remote_cands[] 读取目标）
 *
 * @param s       会话对象（包含远端候选列表）
 * @param verbose 是否输出详细日志
 * @return        0 成功，-1 无候选地址
 */
int nat_start_punch(struct p2p_session *s, int verbose);

/*
 * 处理打洞相关数据包 (PUNCH/PUNCH_ACK/PING/PONG)
 *
 * @param s       会话对象
 * @param type    包类型
 * @param payload 负载数据（可能为空）
 * @param len     负载长度
 * @param from    发送方地址
 * @return        0 正常，-1 错误
 */
int nat_on_packet(struct p2p_session *s, uint8_t type, const uint8_t *payload, int len,
                  const struct sockaddr_in *from);

/*
 * 周期调用，发送打洞包和心跳
 *
 * @param s    会话对象
 * @return     0 正常，-1 连接超时断开
 */
int nat_tick(struct p2p_session *s);

#endif /* P2P_NAT_H */
