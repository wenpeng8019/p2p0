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
 * 当前为骨架实现，完整功能需要：
 *   1. 链接 usrsctp 库
 *   2. 初始化 usrsctp 并注册输出回调
 *   3. 创建 SCTP socket 并建立关联
 *   4. 处理接收和发送
 */

#include "p2p_internal.h"
#include "p2p_udp.h"

/*
 * SCTP 上下文结构
 * 
 * 在完整实现中应包含：
 *   - struct socket *sctp_sock: usrsctp socket 句柄
 *   - 关联状态、流配置等
 */
typedef struct {
    int state;  /* 连接状态：0=未连接，1=连接中，2=已连接 */
    /* struct socket *sctp_sock; */  /* usrsctp socket（需要 usrsctp.h） */
} p2p_sctp_ctx_t;

/*
 * ============================================================================
 * SCTP 出站数据包回调
 * ============================================================================
 *
 * usrsctp 通过此回调发送数据包。
 * 我们将 SCTP 数据包封装到 UDP 中传输。
 *
 * 数据流向：
 *   usrsctp 内部 → p2p_sctp_out() → UDP 发送
 *
 * @param addr    用户上下文（p2p_session 指针）
 * @param buffer  SCTP 数据包
 * @param length  数据包长度
 * @param tos     服务类型（DSCP，通常忽略）
 * @param set_df  Don't Fragment 标志
 * @return        0=成功
 */
static int p2p_sctp_out(void *addr, void *buffer, size_t length, uint8_t tos, uint8_t set_df) {
    p2p_session_t *s = (p2p_session_t *)addr;
    (void)tos; (void)set_df;
    
    /*
     * SCTP 已内置可靠性，因此直接封装为 P2P_PKT_DATA
     * 不需要上层再做 ARQ 处理
     */
    udp_send_packet(s->sock, &s->active_addr, P2P_PKT_DATA, 0, 0, buffer, (int)length);
    return 0;
}

/*
 * ============================================================================
 * 初始化 SCTP 传输层
 * ============================================================================
 *
 * 完整实现步骤：
 *
 *   1. usrsctp_init(0, p2p_sctp_out, NULL)
 *      初始化 usrsctp 库，注册出站回调
 *      第一个参数为本地 SCTP 端口（0=自动分配）
 *
 *   2. usrsctp_register_address(s)
 *      注册会话地址，用于回调时识别数据归属
 *
 *   3. usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, ...)
 *      创建 SCTP socket
 *      AF_CONN 表示使用连接地址族（非真实网络地址）
 *
 *   4. usrsctp_setsockopt(...)
 *      配置 socket 选项：
 *        - SCTP_NODELAY: 禁用 Nagle 算法
 *        - SCTP_RECVRCVINFO: 接收消息元信息
 *        - SCTP_ENABLE_STREAM_RESET: 启用流重置
 *
 *   5. usrsctp_bind() / usrsctp_connect() 或 usrsctp_listen() / usrsctp_accept()
 *      建立 SCTP 关联（类似 TCP 连接）
 *
 * @param s  P2P 会话
 * @return   0=成功，-1=失败
 */
static int sctp_init(p2p_session_t *s) {
    (void)s;
    printf("[SCTP] 初始化 usrsctp 封装（骨架实现）\n");
    
    /*
     * 完整实现示例：
     *
     * usrsctp_init(0, p2p_sctp_out, NULL);
     * usrsctp_sysctl_set_sctp_blackhole(2);  // 不回复 ABORT
     * usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);
     * 
     * usrsctp_register_address(s);
     * 
     * struct socket *sock = usrsctp_socket(
     *     AF_CONN, SOCK_STREAM, IPPROTO_SCTP,
     *     sctp_receive_cb,  // 接收回调
     *     NULL, 0, s);
     * 
     * if (!sock) return -1;
     * 
     * s->sctp_ctx = sock;
     */
    
    return 0;
}

/*
 * ============================================================================
 * 发送数据
 * ============================================================================
 *
 * 通过 SCTP 发送应用层数据。
 *
 * 完整实现应调用 usrsctp_sendv()：
 *
 *   struct sctp_sendv_spa spa;
 *   memset(&spa, 0, sizeof(spa));
 *   spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
 *   spa.sendv_sndinfo.snd_ppid = htonl(PPID);    // 协议标识
 *   spa.sendv_sndinfo.snd_sid = stream_id;       // 流 ID
 *   spa.sendv_sndinfo.snd_flags = SCTP_EOR;      // 消息结束标志
 *
 *   usrsctp_sendv(sock, buf, len, NULL, 0, &spa, sizeof(spa),
 *                 SCTP_SENDV_SPA, 0);
 *
 * PPID (Payload Protocol Identifier) 常见值：
 *   - 50: WebRTC String
 *   - 51: WebRTC Binary
 *   - 53: WebRTC String Empty
 *   - 54: WebRTC Binary Empty
 *
 * @param s    P2P 会话
 * @param buf  要发送的数据
 * @param len  数据长度
 * @return     发送的字节数，-1=失败
 */
static int sctp_send(p2p_session_t *s, const void *buf, int len) {
    (void)s; (void)buf;
    printf("[SCTP] 发送 %d 字节数据\n", len);
    
    /* 
     * 完整实现：
     * return usrsctp_sendv(s->sctp_sock, buf, len, NULL, 0, 
     *                      &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
     */
    
    return len;
}

/*
 * ============================================================================
 * 周期性处理
 * ============================================================================
 *
 * usrsctp 内部使用线程处理定时器，通常不需要外部 tick。
 *
 * 但在单线程模式下，可能需要：
 *   - 手动触发超时检查
 *   - 处理心跳
 *   - 检查关联状态
 */
static void sctp_tick(p2p_session_t *s) {
    (void)s;
    /*
     * 单线程模式下可能需要：
     * usrsctp_handle_timers();
     */
}

/*
 * ============================================================================
 * 处理接收的数据包
 * ============================================================================
 *
 * 接收 UDP 层传入的 SCTP 数据包，交给 usrsctp 处理。
 *
 * 数据流向：
 *   UDP 接收 → sctp_on_packet() → usrsctp_conninput() → usrsctp 内部处理
 *                                                            ↓
 *                                               sctp_receive_cb() 回调应用层
 *
 * @param s       P2P 会话
 * @param type    数据包类型
 * @param payload 数据负载（SCTP 数据包）
 * @param len     负载长度
 * @param from    发送方地址
 */
static void sctp_on_packet(struct p2p_session *s, uint8_t type, const uint8_t *payload, int len, const struct sockaddr_in *from) {
    if (type != P2P_PKT_DATA) return;
    (void)from; (void)s; (void)payload;
    
    printf("[SCTP] 收到封装的 SCTP 数据包，长度 %d\n", len);
    
    /*
     * 完整实现：
     * usrsctp_conninput(s, payload, len, 0);
     *
     * usrsctp 内部会：
     * 1. 解析 SCTP 包头
     * 2. 验证 Verification Tag 和校验和
     * 3. 处理各种 Chunk（DATA、SACK、HEARTBEAT 等）
     * 4. 对于 DATA Chunk，触发接收回调
     */
}

/*
 * ============================================================================
 * 传输层操作表
 * ============================================================================
 *
 * 注册为 P2P 传输层实现，可通过配置选择使用。
 *
 * 与其他传输层对比：
 *   - p2p_trans_simple:    无可靠性，最低延迟
 *   - p2p_trans_reliable:  ARQ 可靠性，简单实现
 *   - p2p_trans_pseudotcp: TCP 风格拥塞控制
 *   - p2p_trans_sctp:      SCTP 多流可靠/不可靠混合
 */
const p2p_transport_ops_t p2p_trans_sctp = {
    .name = "SCTP-usrsctp",   /* 传输层名称 */
    .init = sctp_init,        /* 初始化 */
    .send_data = sctp_send,   /* 发送数据 */
    .tick = sctp_tick,        /* 周期处理 */
    .on_packet = sctp_on_packet  /* 接收处理 */
};
