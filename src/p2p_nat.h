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
#include "../stdc/stdc.h"   /* cross-platform utilities */

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
    int                 state;              /* 打洞状态 */
    struct sockaddr_in  peer_addr;          /* 成功连接的对端地址 */
    uint64_t            last_send_time;     /* 上次发送时间 */
    uint64_t            last_recv_time;     /* 上次接收时间 */
    uint64_t            punch_start;        /* 打洞开始时间 */
} nat_ctx_t;

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n);

/*
 * NAT 打洞（统一接口，支持批量启动和单候选追加）
 *
 * @param s        会话对象
 * @param addr     目标地址（NULL=批量启动所有候选，非NULL=单个候选打洞）
 * @param verbose  详细日志开关（仅批量启动时有效）
 * @return         0=成功，-1=失败（无候选）
 *
 * 用法：
 *   - nat_punch(s, NULL, verbose)  批量启动所有 remote_cands 的打洞
 *   - nat_punch(s, &addr, 0)       向单个候选追加打洞（Trickle ICE）
 *
 * 语义：
 *   - 批量模式（addr==NULL）：进入 PUNCHING 状态，向所有候选并发打洞
 *   - 单候选模式（addr!=NULL）：追加打洞，若当前是 RELAY 状态则自动重启
 *   - 根据候选的 last_punch_send_ms 自动管理打洞时序
 */
int nat_punch(struct p2p_session *s, const struct sockaddr_in *addr);

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
