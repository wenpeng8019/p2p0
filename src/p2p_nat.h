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

#include <stdc.h>
#include <p2pp.h>

/* 前向声明 */
struct p2p_session;

/* 打洞状态 */
enum {
    NAT_INIT = 0,       // 初始化状态（从未连接过）
    NAT_PUNCHING,       // 打洞中。即首次执行 nat_punch 时，也就是支持 Trickle 模式
    NAT_CONNECTED,      // 已连接，即首次收到对方的 P2P_PKT_PUNCH 包。注意，可能存在从 NAT_RELAY 变为 NAT_CONNECTED 可能，例如 P2P_PKT_PUNCH 回复的非常慢
    NAT_RELAY,          // 中继模式，即打洞超时失败。注意，此时如果执行 nat_punch，则会重新变为 NAT_PUNCHING 状态
    NAT_DISCONNECTED,   // 连接超时断开（曾经连接过，但现在断开了，可尝试恢复）
    NAT_CLOSED          // 收到 FIN 包主动断开（连接彻底终止）
};

/* 打洞上下文（精简版，候选列表在 session 中） */
typedef struct {
    int                 state;              /* 打洞状态 */
    struct sockaddr_in  peer_addr;          /* 成功连接的对端地址 */
    uint64_t            last_send_time;     /* 上次发送时间 */
    uint64_t            last_recv_time;     /* 上次接收时间 */
    uint64_t            punch_start;        /* 打洞开始时间 */
    uint16_t            punch_seq;          /* 本地 PUNCH 包序列号（自增） */
    uint16_t            last_peer_seq;      /* 上次收到对方的 seq（捎带式 echo，用于双向连通确认） */
} nat_ctx_t;

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n);

/*
 * NAT 打洞（统一接口，支持批量启动和单候选追加）
 *
 * @param s        会话对象
 * @param idx      目标候选索引（-1=批量启动所有候选，>=0=单个候选打洞）
 * @return         0=成功，-1=失败（无候选）
 *
 * 用法：
 *   - nat_punch(s, -1)      批量启动所有 remote_cands 的打洞
 *   - nat_punch(s, idx)     向单个候选追加打洞（Trickle ICE）
 *
 * 语义：
 *   - 批量模式（idx==-1）：进入 PUNCHING 状态，向所有候选并发打洞
 *   - 单候选模式（idx>=0）：追加打洞，若当前是 RELAY 状态则自动重启
 *   - 根据候选的 last_punch_send_ms 自动管理打洞时序
 */
ret_t nat_punch(struct p2p_session *s, int idx);

/*
 * 处理 PUNCH 包（NAT 打洞、保活）
 *
 * @param s        会话对象
 * @param hdr      包头（包含 type, flags, seq）
 * @param payload  负载数据
 * @param len      负载长度
 * @param from     来源地址
 * @return         E_NONE
 */
ret_t nat_on_punch(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                   const uint8_t *payload, int len, const struct sockaddr_in *from);

/*
 * 处理 FIN 包（连接断开）
 *
 * @param s        会话对象
 * @param from     来源地址
 * @return         E_NONE
 */
ret_t nat_on_fin(struct p2p_session *s, const struct sockaddr_in *from);

/*
 * 周期调用，发送打洞包和心跳
 *
 * @param s    会话对象
 * @return     0 正常，-1 连接超时断开
 */
void nat_tick(struct p2p_session *s, uint64_t now_ms);

#endif /* P2P_NAT_H */
