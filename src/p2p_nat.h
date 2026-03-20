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
    NAT_LOST,                               // 连接丢失：曾经连通，现超时无响应（可能恢复）
    NAT_CONNECTED,                          // 双向连通：rx + tx 同时已确认
                                            //   rx: 收到对方的 PUNCH/PUNCH_ACK（peer→me 方向通）
                                            //   tx: 收到 PUNCH_ACK（对方收到了我们的 PUNCH，me→peer 方向通）
    NAT_RELAY                               // 中继模式：打洞超时失败。此时仍周期发送 PUNCH 尝试重连
};

/* 打洞上下文 */
typedef struct {
    int                 state;              // 打洞状态
    struct sockaddr_in  peer_addr;          // 成功连接的对端地址
    uint64_t            last_send_time;     // 上次发送时间
    uint64_t            last_recv_time;     // 上次接收时间
    uint64_t            punch_start;        // 打洞开始时间
    uint16_t            punch_seq;          // 本地 PUNCH 包序列号（自增）
    bool                rx_confirmed;       // peer→me 已确认
    bool                tx_confirmed;       // me→peer 已确认
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
 * NAT 打洞（统一接口，支持批量启动和单候选追加）
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
 * @param s        会话对象
 * @param hdr      包头（包含 type, flags, seq）
 * @param from     来源地址
 */
void nat_on_punch(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                  const struct sockaddr_in *from);

/*
 * 处理 PUNCH_ACK 包（PUNCH 的即时确认）
 *
 * @param s        会话对象
 * @param hdr      包头（seq = 回传的 PUNCH seq）
 * @param from     来源地址
 */
void nat_on_punch_ack(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                     const struct sockaddr_in *from);

/*
 * 处理 FIN 包（对方主动断开连接）
 *
 * @param s        会话对象
 * @param from     来源地址
 * @return         0=成功，!0=失败
 */
void nat_on_fin(struct p2p_session *s, const struct sockaddr_in *from);

/*
 * 验证 session_id 并调整 payload/len（用于会话隔离）
 *
 * 当 DATA/ACK/CRYPTO 携带 P2P_DATA_FLAG_SESSION 标志时，
 * 调用此函数验证 session_id 是否匹配当前会话。
 *
 * 工作原理：
 *   1. 会话隔离: 防止旧会话的重传包污染新会话的 reliable 层
 *   2. 中继路由: 服务器通过 session_id 查找目标对端
 *
 * @param s          会话对象
 * @param payload    输入/输出参数：payload 指针（验证成功后跳过 session_id）
 * @param len        输入/输出参数：payload 长度（验证成功后减去 session_id 大小）
 * @param proto_name 协议名称（用于日志）
 * @return true=验证成功（payload/len 已调整），false=验证失败
 */
bool nat_validate_session(struct p2p_session *s,
                          const uint8_t **payload, int *len,
                          const char *proto_name);

///////////////////////////////////////////////////////////////////////////////

#endif /* P2P_NAT_H */
