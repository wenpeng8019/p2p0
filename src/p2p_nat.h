/*
 * NAT 穿透（P2P 虚拟链路层）
 *
 * 本模块负责 NAT 打洞以及链路状态的维护。
 *   - PUNCH/PUNCH_ACK 交换（打洞/保活）
 *   - 打洞状态管理
 *
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

#ifndef P2P_NAT_H
#define P2P_NAT_H

#include "predefine.h"
#include <p2pp.h>

///////////////////////////////////////////////////////////////////////////////

/* 前向声明 */
struct p2p_session;

/* 打洞状态 */
enum {
    NAT_INIT = 0,                           // 初始化状态（从未连接过）
    NAT_CLOSED,                             // 收到 FIN 包主动断开（连接彻底终止）
    NAT_PUNCHING,                           // 打洞中（已回取候选列表，正在尝试建立双向路径）
    NAT_CONNECTING,                         // 双向连通：rx + tx 同时已确认，正在发送 CONN 握手
                                            //   （rx/tx_confirmed 现已移至 p2p_session_t，所有传输路径共享）
    NAT_LOST,                               // 连接丢失：曾经连通，现超时无响应（可能恢复）
    NAT_CONNECTED,                          // 数据层已连接：收到 CONN_ACK 或 CONN，可以传输数据
    NAT_RELAY                               // 中继模式：打洞超时失败。此时仍周期发送 PUNCH 尝试重连
};

/* reaching 队列节点（NAT 内部使用，单向链表） */
typedef struct punch_reaching {
    uint16_t                   seq;         // PUNCH seq（需要 echo）
    struct sockaddr_in         target;      // PUNCH 携带的 target_addr（需要 echo）
    int                        cand_idx;    // 来源候选索引
    struct punch_reaching*     next;        // 下一个节点
} punch_reaching_t;

/* 打洞上下文 */
typedef struct {
    int                 state;              // 打洞状态
    struct sockaddr_in  peer_addr;          // 成功连接的对端地址
    uint64_t            last_recv_time;     // 最后接收时间（用于连接超时检测）

    /* 打洞相关状态 */
    uint64_t            punch_start;        // 打洞开始时间（计算打洞超时）
    uint16_t            punch_seq;          // 本地 PUNCH 包序列号（自增）
    
    /* reaching 队列（原路径处于非 writable 状态时缓存 REACH） */
    punch_reaching_t*   reaching_recycle;   
    punch_reaching_t*   reaching_head;      // 队列头指针（最早的在前）
    punch_reaching_t*   reaching_rear;      // 队列尾指针（最新的在后）
    uint64_t            last_reaching_send_ms; // 上次通过信令发送的时间

    /* CONN 握手（数据层连接确认） */
    uint64_t            conn_start_ms;      // CONN 握手开始时间（计算握手超时）
    uint64_t            last_conn_send_ms;  // 上次发送 CONN 的时间（周期发送计时器）
    bool                peer_connecting;

    /* 保活和重试计时器 */
    uint64_t            last_keepalive_send_ms; // NAT_CONNECTED: 上次发送保活包的时间
    uint64_t            last_retry_send_ms;     // NAT_RELAY: 上次发送重试打洞的时间

} nat_ctx_t;

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n);

void nat_reset(nat_ctx_t *n);

/*
 * 周期调用，发送打洞包和心跳
 *
 * @param s        会话对象
 * @param now_ms   当前时间（毫秒）
 */
void nat_tick(struct p2p_session *s, uint64_t now_ms);

//-----------------------------------------------------------------------------

/*
 * NAT 打洞
 *
 * @param s        会话对象
 * @param idx      目标候选索引（-1=批量启动所有候选，>=0=单个候选打洞）
 * @return         0=成功，!0=失败
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
 * 发送 FIN 包（主动断开 NAT 连接）
 *
 * @param s        会话对象
 * @param to       目标地址
 */
void nat_send_fin(struct p2p_session *s);

//-----------------------------------------------------------------------------

/*
 * 处理 PUNCH 包（NAT 打洞、保活）
 *
 * @param s           会话对象
 * @param hdr         包头（包含 type, flags, seq）
 * @param payload     负载数据（扩展协议时携带 target_addr）
 * @param payload_len 负载长度
 * @param from        来源地址
 */
void nat_on_punch(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                  const uint8_t *payload, int payload_len,
                  const struct sockaddr_in *from, uint64_t now);

/*
 * 处理 REACH 包（PUNCH 到达确认）
 *
 * @param s           会话对象
 * @param hdr         包头（seq = 回传的 PUNCH seq）
 * @param payload     负载数据（携带 target_addr）
 * @param payload_len 负载长度
 * @param from        来源地址
 */
void nat_on_reach(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                  const uint8_t *payload, int payload_len,
                  const struct sockaddr_in *from, uint64_t now);

/*
 * 处理 FIN 包（对方主动断开连接）
 *
 * @param s        会话对象
 * @param from     来源地址
 * @return         0=成功，!0=失败
 */
void nat_on_fin(struct p2p_session *s, const struct sockaddr_in *from);

/*
 * 处理 CONN 包（数据层连接请求）
 *
 * @param s        会话对象
 * @param hdr      包头（包含 seq）
 * @param from     来源地址
 */
void nat_on_conn(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                 const struct sockaddr_in *from, uint64_t now);

/*
 * 处理 CONN_ACK 包（数据层连接确认）
 *
 * @param s        会话对象
 * @param hdr      包头（包含 echo seq）
 * @param from     来源地址
 */
void nat_on_conn_ack(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                     const struct sockaddr_in *from, uint64_t now);

/*
 * 处理 DATA 数据包（P2P_PKT_DATA）
 *
 * @param s           会话对象
 * @param from        来源地址
 * @param now         当前时间戳
 * @param seq         数据包序列号
 * @param data_len    数据包长度（含 P2P 头）
 */
void nat_on_data(struct p2p_session *s, const struct sockaddr_in *from, 
                 uint64_t now, uint16_t seq, int data_len);

/*
 * 处理 ACK 包（P2P_PKT_ACK）
 *
 * @param s           会话对象
 * @param from        来源地址
 * @param now         当前时间戳
 * @param ack_seq     ACK 序列号
 * @param sack        SACK 位图
 */
void nat_on_data_ack(struct p2p_session *s, const struct sockaddr_in *from,
                     uint64_t now, uint16_t ack_seq, uint32_t sack);

///////////////////////////////////////////////////////////////////////////////

#endif /* P2P_NAT_H */
