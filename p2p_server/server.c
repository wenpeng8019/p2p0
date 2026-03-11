/*
 * P2P 信令服务器
 *
 * 支持两种信令模式：
 *
 * 1. COMPACT 模式 (UDP)
 *    - 对应客户端 p2p_signal_compact 模块
 *    - 无状态信令，基于 UDP 数据包交换
 *    - 紧凑集成：信令交换 + NAT端口检测 + 候选交换 + 数据中继，一个完整的 P2P 统一协议实现
 *
 * 2. RELAY 模式 (TCP)
 *    - 对应客户端 p2p_signal_relay 模块
 *    - 有状态信令，基于 TCP 长连接
 *    - 支持在线状态查询、以及基本数据中转功能，用于支持 ICE/STUN/TURN 协议架构实现的信令服务器
 *
 * 日志原则：
 * - 调试级别：printf 输出协议包详细信息（Send/Received XXX pkt...）
 * - 正式级别：printf 输出带前缀的日志（[UDP]/[TCP] + V/I/W/E:）
 * 
 * 对于收包处理：
 *   > 收到包时，调试打印包的详细信息
 *   > 处理过程中出现错误，输出 W/E 级日志
 *   > 成功处理后，输出 V 级日志说明结果
 * 
 * 对于发包操作：
 *   > 发送前，调试打印包的详细信息（目标地址、包类型、关键字段等）
 *   > 发送失败，输出 W/E 级日志
 *   > 发送成功，输出 V 级日志说明发送结果
 * 
 * 对于状态管理：
 *   > 状态变更、重要操作时，输出 I 级日志
 *   > 超时、清理等异常情况，输出 W 级日志
 */

#include <stdc.h>
#include <signal.h>    /* signal() */

#include <p2p.h>
#include <p2pp.h>
#include "../src/p2p_common.h"
#include "LANG.h"
#include "uthash.h"

#define DEFAULT_PORT                9333

// cleanup 过期配对/客户端的时间间隔（秒）
#define CLEANUP_INTERVAL            10

// 允许最大同时在线客户端数量
#define MAX_PEERS                   128

// 命令行参数定义
ARGS_I(false, port,       'p', "port",       "Signaling server listen port (TCP+UDP)");
ARGS_I(false, probe_port, 'P', "probe-port", "NAT type detection port (0=disabled)");
ARGS_B(false, relay,      'r', "relay",      "Enable data relay support (COMPACT mode fallback)");
ARGS_B(false, cn,         0,   "cn",         "Use Chinese language");

// 允许最大候选队列缓存数量
/* + 服务器为每个用户提供的候选缓存能力
 |   32 个候选可容纳大多数网络环境的完整候选集合，实际场景通常：20-30 个候选，32 提供充足余量
 | + 内存占用：COMPACT 模式 32×7字节=224B/用户，RELAY 模式 32×32字节=1KB/用户
*/
#define MAX_CANDIDATES              32

// COMPACT 模式配对超时时间（秒）
// 客户端在 REGISTERED 状态每 20 秒发一次 keepalive REGISTER，此值取 3 倍间隔
#define COMPACT_PAIR_TIMEOUT        90

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT        60

#define COMPACT_RETRY_INTERVAL      1
// COMPACT 模式 PEER_INFO 重传参数
#define PEER_INFO0_RETRY_INTERVAL   2       // 重传间隔（秒）
#define PEER_INFO0_MAX_RETRY        5       // 最大重传次数

// COMPACT 模式 MSG RPC 重传参数
#define MSG_REQ_RETRY_INTERVAL      1       // MSG_REQ 转发重传间隔（秒）
#define MSG_REQ_MAX_RETRY           5       // MSG_REQ 最大重传次数
#define MSG_RESP_RETRY_INTERVAL     1       // MSG_RESP 转发重传间隔（秒）
#define MSG_RESP_MAX_RETRY          10      // MSG_RESP 最大重传次数（比 REQ 更多，确保 A 端收到）

// RELAY 模式客户端（TCP 长连接）
typedef struct relay_client {
    bool                    valid;                              // 客户端是否有效（无效意味着未分配或已回收）
    char                    name[P2P_PEER_ID_MAX];              // 客户端名称（登录时提供）
    sock_t                  fd;                                 // 客户端 tcp 套接口描述符
    uint64_t                last_active;                        // 最后活跃时间（毫秒，用于检测死连接）
    
    // ===== 在线连接跟踪（用于判断 OFFER/FORWARD） =====
    char                    current_peer[P2P_PEER_ID_MAX];      // 当前正在连接的对端（空字符串表示无活动连接）
    
    // ===== 离线候选缓存（仅支持单一发送者） =====
    // 注意：客户端仅支持一对一连接，不支持多方同时连接
    // 如果新发送者发起连接，会覆盖旧发送者的缓存
    // 场景4: 0 < pending_count < MAX_CANDIDATES (有候选缓存)
    // 场景5: pending_count == MAX_CANDIDATES (缓存满，发送空 OFFER)
    // 场景6: MAX_CANDIDATES == 0 (服务器不支持缓存，当前不存在此场景)
    char                    pending_sender[P2P_PEER_ID_MAX];    // 发送者名称（空字符串表示无连接请求）
    int                     pending_count;                      // 候选数量
    p2p_candidate_t         pending_candidates[MAX_CANDIDATES]; // 候选列表（网络格式，直接收发，无需 sockaddr 转换）
} relay_client_t;

// COMPACT 模式配对记录（UDP 无状态）
/* 注意：COMPACT 模式采用"配对缓存"机制：
 *   A 注册 (local=alice, remote=bob, candidates=[...])
 *   B 注册 (local=bob, remote=alice, candidates=[...])
 *   服务器检测到双向匹配后，同时向 A 和 B 发送对方的候选列表
 */
typedef struct compact_pair {
    bool                    valid;                              // 记录是否有效（无效意味着未分配或已回收）
    uint32_t                instance_id;                        // 客户端本次 connect() 的实例 ID（每次客户重启就变，区分重传 vs 新会话）
    uint64_t                session_id;                         // 会话 ID（服务器分配，唯一标识一个配对，64位随机数）
    char                    local_peer_id[P2P_PEER_ID_MAX];     // 本端 ID
    char                    remote_peer_id[P2P_PEER_ID_MAX];    // 目标对端 ID
    struct compact_pair*    peer;                               // 指向配对的对端。(void*)-1 表示对端已断开

    uint64_t                last_active;                        // 最后活跃时间（毫秒）

    struct sockaddr_in      addr;                               // 公网地址（UDP 源地址）
    uint8_t                 addr_notify_seq;                    // 发给对端的地址变更通知序号（base_index，1..255 循环）

    p2p_compact_candidate_t candidates[MAX_CANDIDATES];         // 候选列表
    int                     candidate_count;                    // 候选数量

    // PEER_INFO(seq=0) 可靠传输（首包 + 地址变更通知）
    int                     info0_acked;                        // 是否已收到首包 ACK，-1 表示未收到确认但已放弃
    uint8_t                 info0_base_index;                   // 当前待确认 seq=0 的 base_index（0=首包，!=0 地址变更通知）
    int                     info0_retry;                        // 当前待确认 seq=0 重传次数
    uint64_t                info0_sent_time;                    // 当前待确认 seq=0 最近发送时间（毫秒）
    struct compact_pair*    next_info0_pending;                 // 待确认链表指针

    // MSG RPC - 出站请求（本端发起 REQ，Server→对端 转发，等待对端响应）
    bool                    req_pending;                        // 是否有发起的请求正在转发中
    uint16_t                req_sid;                            // 请求序列号
    uint8_t                 req_msg;                            // 请求消息类型
    uint8_t                 req_data[P2P_MSG_DATA_MAX];         // RPC 数据缓冲区（REQ阶段存请求，RESP阶段复用存响应）
    int                     req_data_len;                       // RPC 数据长度（REQ/RESP 复用）
    uint64_t                req_sent_time;                      // MSG_REQ 最后转发给对端的时间（毫秒）
    int                     req_retry;                          // MSG_REQ 转发给对端的重传次数
    struct compact_pair*    next_req_pending;                   // REQ 待确认链表指针
    
    // MSG RPC - 入站响应转发（对端响应到达，缓存并可靠转发给本端）
    bool                    resp_pending;                       // 是否有待确认的响应转发
    uint16_t                resp_sid;                           // 响应序列号（= 对应请求的 sid）
    uint8_t                 resp_flags;                         // 响应 flags（0=正常, PEER_OFFLINE, TIMEOUT）
    uint8_t                 resp_msg;                           // 响应消息类型
    uint64_t                resp_sent_time;                     // MSG_RESP 最后转发给本端的时间（毫秒）
    int                     resp_retry;                         // MSG_RESP 转发给本端的重传次数
    struct compact_pair*    next_resp_pending;                  // RESP 待确认链表指针
    
    // uthash handles（支持多种索引方式）
    UT_hash_handle          hh;                                 // 按 session_id 索引（主索引，必须命名为 hh）
    UT_hash_handle          hh_peer;                            // 按 peer_key (local+remote) 索引（辅助索引）
} compact_pair_t;

#define PEER_ONLINE(p)      ((p)->peer && (p)->peer != (compact_pair_t*)(void*)-1)  // 判断对端是否在线（peer 指针为 (void*)-1 表示已断开）
#define PEER_OF(p)          (PEER_ONLINE(p) ? (p)->peer : NULL)

static relay_client_t       g_relay_clients[MAX_PEERS];
static compact_pair_t       g_compact_pairs[MAX_PEERS];

// uthash 哈希表（支持两种索引方式）
static compact_pair_t*      g_pairs_by_session = NULL;         // 按 session_id 索引
static compact_pair_t*      g_pairs_by_peer = NULL;            // 按 (local_peer_id, remote_peer_id) 索引

// PEER_INFO(seq=0) 待确认链表（仅包含已发送首包但未收到 ACK 的配对）
static compact_pair_t*      g_info0_pending_head = NULL;
static compact_pair_t*      g_info0_pending_rear = NULL;

// MSG RPC 待确认链表（分别跟踪 REQ 转发阶段和 RESP 转发阶段）
static compact_pair_t*      g_req_pending_head = NULL;
static compact_pair_t*      g_req_pending_rear = NULL;
static compact_pair_t*      g_resp_pending_head = NULL;
static compact_pair_t*      g_resp_pending_rear = NULL;

// 服务器配置（运行时参数）
static int                  g_probe_port = 0;                   // compact 模式 NAT 探测端口（0=不支持探测）
static bool                 g_relay_enabled = false;            // compact 模式是否支持中继功能
static bool                 g_msg_enabled = true;               // compact 模式是否支持 MSG RPC（默认启用）

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t g_running = 1;

///////////////////////////////////////////////////////////////////////////////

// 生成安全的随机 session_id（64位，加密安全，防止跨会话注入攻击）
static uint64_t generate_session_id(void) {
    uint64_t id = P_rand64();  // 使用 stdc.h 统一封装的加密安全随机数

    // 冲突检测（虽然概率极低：1/2^64 ≈ 5.4×10^-20）
    compact_pair_t *existing = NULL;
    HASH_FIND(hh, g_pairs_by_session, &id, sizeof(uint64_t), existing);
    if (existing) {
        // 递归重试
        return generate_session_id();
    }
    
    return id;
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "RELAY"

// 处理 RELAY 模式信令（TCP 长连接，对应 p2p_signal_relay 模块）
static void handle_relay_signaling(int idx) {

    p2p_relay_hdr_t hdr;
    sock_t fd = g_relay_clients[idx].fd;

    // 更新最后活跃时间（收到任何数据都表示连接活跃）
    g_relay_clients[idx].last_active = P_tick_ms();

    size_t n = recv(fd, (char *)&hdr, sizeof(hdr), 0);
    if (n <= 0) {
        printf(LA_F("[TCP] V: Peer '%s' disconnected\n", LA_F50, 50), g_relay_clients[idx].name);

        P_sock_close(fd);
        g_relay_clients[idx].valid = false;
        g_relay_clients[idx].current_peer[0] = '\0';
        return;
    }

    // 调试打印协议包信息
    printf("Received TCP pkt from peer '%s': magic=0x%08X, type=%d, length=%d (expect magic=0x%08X)\n",
           g_relay_clients[idx].name, hdr.magic, hdr.type, hdr.length, P2P_RLY_MAGIC);

    if (hdr.magic != P2P_RLY_MAGIC) {
        printf(LA_F("[TCP] E: Invalid magic from peer '%s'\n", LA_F13, 13), g_relay_clients[idx].name);

        P_sock_close(fd);
        g_relay_clients[idx].valid = false;
        g_relay_clients[idx].current_peer[0] = '\0';
        return;
    }

    // 用户请求登录
    if (hdr.type == P2P_RLY_LOGIN) {
        const char* PROTO = "LOGIN";

        p2p_relay_login_t login;
        recv(fd, (char *)&login, sizeof(login), 0);
        strncpy(g_relay_clients[idx].name, login.name, P2P_PEER_ID_MAX);
        g_relay_clients[idx].valid = true;
        g_relay_clients[idx].current_peer[0] = '\0';  // 初始化为无连接状态

        printf(LA_F("[TCP] I: Peer '%s' logged in\n", LA_F51, 51), g_relay_clients[idx].name);

        // 发送登录确认
        const char* ACK_PROTO = "LOGIN_ACK";

        p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_LOGIN_ACK, 0};
        send(fd, (const char *)&ack, sizeof(ack), 0);

        printf(LA_F("[TCP] V: %s sent to '%s'\n", LA_F280, 280), ACK_PROTO, g_relay_clients[idx].name);

        /* Merge pending candidates from any offline slot with same name.
         * This happens when the active peer sent candidates before this peer
         * connected (offline slot was created to cache them). */
        for (int k = 0; k < MAX_PEERS; k++) {
            if (k == idx) continue;
            if (g_relay_clients[k].valid &&
                g_relay_clients[k].fd == P_INVALID_SOCKET &&
                strcmp(g_relay_clients[k].name, login.name) == 0 &&
                g_relay_clients[k].pending_count > 0) {

                /* Copy pending candidates from offline slot into online slot */
                g_relay_clients[idx].pending_count = g_relay_clients[k].pending_count;
                memcpy(g_relay_clients[idx].pending_candidates,
                       g_relay_clients[k].pending_candidates,
                       g_relay_clients[k].pending_count * sizeof(p2p_candidate_t));
                strncpy(g_relay_clients[idx].pending_sender,
                        g_relay_clients[k].pending_sender, P2P_PEER_ID_MAX);

                printf(LA_F("[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n", LA_F46, 46),
                       g_relay_clients[k].pending_count, g_relay_clients[k].pending_sender, login.name);

                /* Release the offline slot */
                g_relay_clients[k].valid = false;
                g_relay_clients[k].current_peer[0] = '\0';
                g_relay_clients[k].pending_count = 0;
                g_relay_clients[k].pending_sender[0] = '\0';
                break;
            }
        }
        
        // 检查是否有待转发的缓存候选（场景4：部分缓存）
        if (g_relay_clients[idx].pending_count > 0 && g_relay_clients[idx].pending_count < MAX_CANDIDATES) {
            relay_client_t *client = &g_relay_clients[idx];
            const char *sender = client->pending_sender;
            
            printf(LA_F("[TCP] Flushing %d pending candidates from '%s' to '%s'...\n", LA_F45, 45), 
                   client->pending_count, sender, client->name);
            
            // 构建 OFFER 包（含 hdr + count × candidate）
            uint8_t offer_buf[2048];
            n = pack_signaling_payload_hdr(
                sender,                    // sender
                client->name,              // target
                0,                         // timestamp
                0,                         // delay_trigger
                client->pending_count,     // candidate_count
                offer_buf
            );
            
            // 打包候选
            for (int j = 0; j < client->pending_count; j++) {
                memcpy(offer_buf + n, &client->pending_candidates[j], sizeof(p2p_candidate_t));
                n += sizeof(p2p_candidate_t);
            }
            
            // 发送 OFFER
            p2p_relay_hdr_t relay_hdr = {
                P2P_RLY_MAGIC,
                P2P_RLY_OFFER,
                (uint32_t)(P2P_PEER_ID_MAX + n)
            };
            
            send(fd, (const char *)&relay_hdr, sizeof(relay_hdr), 0);
            send(fd, sender, P2P_PEER_ID_MAX, 0);
            send(fd, (const char *)offer_buf, n, 0);
            
            printf(LA_F("[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n", LA_F35, 35),
                   sender, client->pending_count, (int)n);
            
            // 清空缓存
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(LA_F("[TCP] All pending candidates flushed to '%s'\n", LA_F37, 37), client->name);
        }
        // 检查是否缓存已满（场景5：发送空 OFFER，让对端反向连接）
        else if (g_relay_clients[idx].pending_count == MAX_CANDIDATES && g_relay_clients[idx].pending_sender[0] != '\0') {
            relay_client_t *client = &g_relay_clients[idx];
            const char *sender = client->pending_sender;
            
            printf(LA_F("[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n", LA_F58, 58),
                   sender, client->name);
            
            // 构建空 OFFER（candidate_count=0）
            uint8_t offer_buf[76];  // 仅包含 hdr，无候选数据
            n = pack_signaling_payload_hdr(
                sender,                    // sender
                client->name,              // target
                0,                         // timestamp
                0,                         // delay_trigger
                0,                         // candidate_count（空）
                offer_buf
            );
            
            // 发送空 OFFER
            p2p_relay_hdr_t relay_hdr = {
                P2P_RLY_MAGIC,
                P2P_RLY_OFFER,
                (uint32_t)(P2P_PEER_ID_MAX + n)
            };
            
            send(fd, (const char *)&relay_hdr, sizeof(relay_hdr), 0);
            send(fd, sender, P2P_PEER_ID_MAX, 0);
            send(fd, (const char *)offer_buf, n, 0);
            
            printf(LA_F("[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n", LA_F36, 36), sender);
            
            // 清空缓存满标识
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(LA_F("[TCP] Storage full indication flushed to '%s'\n", LA_F56, 56), client->name);
        }
    }
    // 信令转发：P2P_RLY_CONNECT → P2P_RLY_OFFER，P2P_RLY_ANSWER → P2P_RLY_FORWARD
    else if (hdr.type == P2P_RLY_CONNECT || hdr.type == P2P_RLY_ANSWER) {

        // 接收目标对端名称
        char target_name[P2P_PEER_ID_MAX];
        if (recv(fd, target_name, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
            printf(LA_F("[TCP] Failed to receive target name from %s\n", LA_F43, 43), g_relay_clients[idx].name);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        // 接收信令负载数据
        uint32_t payload_len = hdr.length - P2P_PEER_ID_MAX;
        if (payload_len > 65536) {  // 防止过大的负载
            printf(LA_F("[TCP] Payload too large (%u bytes) from %s\n", LA_F49, 49), payload_len, g_relay_clients[idx].name);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        uint8_t *payload = malloc(payload_len);
        if (recv(fd, (char *)payload, payload_len, 0) != (int)payload_len) {
            printf(LA_F("[TCP] Failed to receive payload from %s\n", LA_F42, 42), g_relay_clients[idx].name);
            free(payload);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }

        printf(LA_F("[TCP] Relaying %s from %s to %s (%u bytes)\n", LA_F52, 52), 
               hdr.type == P2P_RLY_CONNECT ? "CONNECT" : "ANSWER",
               g_relay_clients[idx].name, target_name, payload_len);

        /* 解析负载头部以获取候选数量 */
        p2p_signaling_payload_hdr_t payload_hdr;
        int candidates_in_payload = 0;
        if (payload_len >= 76 && unpack_signaling_payload_hdr(&payload_hdr, payload) == 0) {
            candidates_in_payload = payload_hdr.candidate_count;
        }

        // 查找目标客户端
        bool found = false;
        uint8_t ack_status = 0;
        uint8_t candidates_acked = 0;

        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != P_INVALID_SOCKET &&
                strcmp(g_relay_clients[i].name, target_name) == 0) {
                
                /* 目标在线：检查是否第一次连接（OFFER）或后续更新（FORWARD） */
                bool is_first_offer = (g_relay_clients[i].current_peer[0] == '\0' || 
                                       strcmp(g_relay_clients[i].current_peer, g_relay_clients[idx].name) != 0);
                
                uint8_t relay_type = is_first_offer ? P2P_RLY_OFFER : P2P_RLY_FORWARD;
                
                // 第一次连接时，记录当前连接的对端
                if (is_first_offer) {
                    strncpy(g_relay_clients[i].current_peer, g_relay_clients[idx].name, P2P_PEER_ID_MAX - 1);
                    g_relay_clients[i].current_peer[P2P_PEER_ID_MAX - 1] = '\0';
                }
                
                p2p_relay_hdr_t relay_hdr = {P2P_RLY_MAGIC, relay_type,
                                              (uint32_t)(P2P_PEER_ID_MAX + payload_len)};
                send(g_relay_clients[i].fd, (const char *)&relay_hdr, sizeof(relay_hdr), 0);
                send(g_relay_clients[i].fd, g_relay_clients[idx].name, P2P_PEER_ID_MAX, 0);
                send(g_relay_clients[i].fd, (const char *)payload, payload_len, 0);
                
                found = true;
                ack_status = 0;  /* 成功转发 */
                candidates_acked = candidates_in_payload;
                printf(LA_F("[TCP] Sent %s with %d candidates to '%s' (from '%s')\n", LA_F53, 53),
                       is_first_offer ? "OFFER" : "FORWARD", candidates_in_payload, 
                       target_name, g_relay_clients[idx].name);
                break;
            }
        }
        
        if (!found) {
            /* 目标离线：缓存候选 */
            printf(LA_F("[TCP] Target %s offline, caching candidates...\n", LA_F59, 59), target_name);
            
            // 查找或创建目标用户槽位
            int target_idx = -1;
            for (int i = 0; i < MAX_PEERS; i++) {
                if (g_relay_clients[i].valid && strcmp(g_relay_clients[i].name, target_name) == 0) {
                    target_idx = i;
                    break;
                }
            }
            
            // 如果目标从未登录，分配离线槽位
            if (target_idx == -1) {
                for (int i = 0; i < MAX_PEERS; i++) {
                    if (!g_relay_clients[i].valid) {
                        target_idx = i;
                        g_relay_clients[i].valid = true;
                        g_relay_clients[i].fd = P_INVALID_SOCKET;  // offline marker
                        strncpy(g_relay_clients[i].name, target_name, P2P_PEER_ID_MAX);
                        g_relay_clients[i].pending_count = 0;
                        g_relay_clients[i].pending_sender[0] = '\0';  // 空发送者（也表示无意图）
                        g_relay_clients[i].last_active = P_tick_ms();
                        break;
                    }
                }
            }
            
            // 逐个解析并缓存候选
            if (target_idx >= 0) {
                relay_client_t *target = &g_relay_clients[target_idx];
                const uint8_t *cand_data = payload + 76;  // 候选数据起始位置
                
                // 检查是否是新发送者（覆盖旧缓存）
                bool new_sender = (target->pending_count == 0 || 
                                   strcmp(target->pending_sender, g_relay_clients[idx].name) != 0);
                if (new_sender) {
                    if (target->pending_count > 0) {
                        printf(LA_F("[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n", LA_F48, 48),
                               g_relay_clients[idx].name, target->pending_sender, target->pending_count);
                    }
                    target->pending_count = 0;
                    strncpy(target->pending_sender, g_relay_clients[idx].name, P2P_PEER_ID_MAX);
                }
                
                for (int i = 0; i < candidates_in_payload; i++) {
                    if (target->pending_count >= MAX_CANDIDATES) {
                        ack_status = 2;  /* 缓存已满 */
                        printf(LA_F("[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n", LA_F55, 55),
                               target_name, candidates_acked, candidates_in_payload - candidates_acked);
                        
                        // 缓存已满时，pending_sender 本身就表示连接意图（不需要额外字段）
                        // 此时 pending_count 保持为 MAX_CANDIDATES，pending_sender 已经记录了发送者
                        printf(LA_F("[TCP] Storage full, connection intent from '%s' to '%s' noted\n", LA_F57, 57),
                               g_relay_clients[idx].name, target_name);
                        break;
                    }
                    
                    // 反序列化候选（网络格式直接存储，无需 sockaddr_in 转换）
                    p2p_candidate_t cand;
                    memcpy(&cand, cand_data + i * sizeof(p2p_candidate_t), sizeof(p2p_candidate_t));
                    // 缓存候选
                    target->pending_candidates[target->pending_count] = cand;
                    target->pending_count++;
                    candidates_acked++;
                }
                
                // 检查缓存后是否已满
                if (candidates_acked > 0) {
                    if (target->pending_count >= MAX_CANDIDATES) {
                        ack_status = 2;  /* 缓存后已满（包括刚好满的情况） */
                        printf(LA_F("[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n", LA_F39, 39),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    } else {
                        ack_status = 1;  /* 已缓存，还有剩余空间 */
                        printf(LA_F("[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n", LA_F38, 38),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    }
                }
            } else {
                ack_status = 2;  /* 无法分配槽位 */
                candidates_acked = 0;
                printf(LA_F("[TCP] Cannot allocate slot for offline user '%s'\n", LA_F40, 40), target_name);
            }
        }

        /* 仅对 P2P_RLY_CONNECT 发送 ACK（P2P_RLY_ANSWER 不需要确认） */
        if (hdr.type == P2P_RLY_CONNECT) {
            p2p_relay_hdr_t ack_hdr = {P2P_RLY_MAGIC, P2P_RLY_CONNECT_ACK, sizeof(p2p_relay_connect_ack_t)};
            p2p_relay_connect_ack_t ack_payload = {ack_status, candidates_acked, {0, 0}};
            size_t sent1 = send(fd, (const char *)&ack_hdr, sizeof(ack_hdr), 0);
            size_t sent2 = send(fd, (const char *)&ack_payload, sizeof(ack_payload), 0);
            if (sent1 != sizeof(ack_hdr) || sent2 != sizeof(ack_payload)) {
                printf(LA_F("[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n", LA_F44, 44),
                       g_relay_clients[idx].name, (int)sent1, (int)sent2);
            } else {
                printf(LA_F("[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n", LA_F54, 54), 
                       g_relay_clients[idx].name, ack_status, candidates_acked);
            }
        }

        free(payload);
    }
    // 获取在线用户列表
    else if (hdr.type == P2P_RLY_LIST) {

        // 构造在线用户列表（逗号分隔）
        char list_buf[1024] = {0};
        size_t offset = 0;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != fd) {
                size_t remaining = sizeof(list_buf) - offset;
                if (remaining < P2P_PEER_ID_MAX + 2) {  // 确保有足够空间
                    printf("%s", LA_S("[TCP] User list truncated (too many users)\n", LA_S15, 15));
                    break;
                }
                n = snprintf(list_buf + offset, remaining, "%s,", g_relay_clients[i].name);
                if (n >= remaining) {  // 检查是否被截断
                    break;
                }
                offset += n;
            }
        }
        
        p2p_relay_hdr_t res_hdr = {P2P_RLY_MAGIC, P2P_RLY_LIST_RES, (uint32_t)offset};
        send(fd, (const char *)&res_hdr, sizeof(res_hdr), 0);
        if (offset > 0) {
            send(fd, list_buf, offset, 0);
        }
    }
    // 处理心跳
    else if (hdr.type == P2P_RLY_HEARTBEAT) {
        // 心跳的作用：
        // 1. 检测死连接（对方崩溃、网络断开等 TCP 无法检测的情况）
        // 2. 保持 NAT 映射（防止中间设备超时关闭）
        // 3. last_active 已在函数开头更新，这里无需额外处理
        
        // 可选：回复心跳响应（让客户端也能检测服务器状态）
        // p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_HEARTBEAT, 0};
        // send(fd, &ack, sizeof(ack), 0);
    }
    // 未知消息类型
    else {
        printf(LA_F("[TCP] Unknown message type %d from %s\n", LA_F60, 60), hdr.type, g_relay_clients[idx].name);
    }
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {
    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_relay_clients[i].valid || 
            (now - g_relay_clients[i].last_active) <= RELAY_CLIENT_TIMEOUT * 1000) continue;

        printf(LA_F("[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n", LA_F41, 41), 
               g_relay_clients[i].name, (now - g_relay_clients[i].last_active) / 1000.0);
        
        P_sock_close(g_relay_clients[i].fd);
        g_relay_clients[i].fd = P_INVALID_SOCKET;
        g_relay_clients[i].current_peer[0] = '\0';
        g_relay_clients[i].valid = false;
    }
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "COMPACT"

// 发送 REGISTER_ACK: [hdr(4)][status(1)][session_id(8)][instance_id(4)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 26字节
// status: SIG_REGACK_PEER_OFFLINE(0) / SIG_REGACK_PEER_ONLINE(1) / 2(error)
static void send_register_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, uint8_t status, uint64_t session_id, uint32_t instance_id) {
    uint8_t ack[26];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_REGISTER_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    if (status <= 1) {
        if (g_relay_enabled) hdr->flags |= SIG_REGACK_FLAG_RELAY;
        if (g_msg_enabled)   hdr->flags |= SIG_REGACK_FLAG_MSG;
        ack[4] = status;
        nwrite_ll(ack + 5, session_id);
        nwrite_l(ack + 13, instance_id);
        ack[17] = MAX_CANDIDATES;
        memcpy(ack + 18, &to->sin_addr.s_addr, 4);
        memcpy(ack + 22, &to->sin_port, 2);
        uint16_t probe = htons(g_probe_port);
        memcpy(ack + 24, &probe, 2);
    } else {
        ack[4] = status;
        memset(ack + 5, 0, 21);
    }

    printf("Send REGISTER_ACK pkt to %s, seq=0, flags=0x%02x, status=%u, len=26, ses_id=%" PRIu64 ", inst_id=%u\n",
           to_str, hdr->flags, ack[4], session_id, instance_id);
    sendto(udp_fd, (const char *)ack, 26, 0, (const struct sockaddr *)to, sizeof(*to));

    if (status <= 1) {
         printf(LA_F("[UDP] V: REGISTER_ACK sent, status=%s, max_cands=%d, relay=%s, public=%s:%d, probe=%d, ses_id=%" PRIu64 ", inst_id=%u\n", 0),
               status ? "peer_online" : "peer_offline",
               MAX_CANDIDATES,
               g_relay_enabled ? "yes" : "no",
             inet_ntoa(to->sin_addr), ntohs(to->sin_port), g_probe_port, session_id, instance_id);
    } else {
        printf(LA_F("[UDP] E: REGISTER_ACK sent, status=error (no slot available)\n", LA_F74, 74));
    }
}

// 发送 PEER_OFF 通知: [hdr(4)][session_id(8)] = 12字节
// 并标记对端已断开 (peer->peer = -1)
static void send_peer_off(sock_t udp_fd, compact_pair_t *peer, const char *reason) {
    uint8_t pkt[4 + 8];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_PEER_OFF; hdr->flags = 0; hdr->seq = htons(0);
    nwrite_ll(pkt + 4, peer->session_id);

    printf("Send PEER_OFF pkt to %s:%d, seq=0, flags=0, len=12, session_id=%" PRIu64 "\n",
           inet_ntoa(peer->addr.sin_addr), ntohs(peer->addr.sin_port), peer->session_id);
    sendto(udp_fd, (const char *)pkt, sizeof(pkt), 0,
           (struct sockaddr *)&peer->addr, sizeof(peer->addr));

    printf("[UDP] V: PEER_OFF sent to '%s' (ses_id=%" PRIu64 ") [%s]\n",
           peer->local_peer_id, peer->session_id, reason);

    peer->peer = (compact_pair_t*)(void*)-1;
}

// 发送 PEER_INFO(seq=0)（base_index=0 首包；base_index!=0 地址变更通知）
static void send_peer_info_seq0(sock_t udp_fd, compact_pair_t *pair, uint8_t base_index) {
    const char* PROTO = "PEER_INFO";

    assert(pair && PEER_ONLINE(pair));

    uint8_t pkt[4 + P2P_PEER_ID_MAX + 2 + MAX_CANDIDATES * sizeof(p2p_compact_candidate_t)];  // 包头 + session_id + base_index + candidates
    p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)pkt;
    resp_hdr->type = SIG_PKT_PEER_INFO;
    resp_hdr->flags = 0;
    resp_hdr->seq = htons(0);

    nwrite_ll(pkt + 4, pair->session_id);
    pkt[12] = base_index;

    int resp_len = 14;
    int cand_cnt = 0;
    if (base_index == 0) {

        // 首包：候选数量 = 对端公网地址(1) + 对端注册的候选列表(candidate_count)
        cand_cnt = 1 + pair->peer->candidate_count;
        pkt[13] = (uint8_t)cand_cnt;
        
        // 第一个候选：对端的公网地址（服务器观察到的 UDP 源地址）
        pkt[resp_len] = 1; // srflx
        memcpy(pkt + resp_len + 1, &pair->peer->addr.sin_addr.s_addr, 4);
        memcpy(pkt + resp_len + 5, &pair->peer->addr.sin_port, 2);
        resp_len += sizeof(p2p_compact_candidate_t);
        
        // 后续候选：对端注册时提供的候选列表
        for (int i = 0; i < pair->peer->candidate_count; i++) {
            pkt[resp_len] = pair->peer->candidates[i].type;
            memcpy(pkt + resp_len + 1, &pair->peer->candidates[i].ip, 4);
            memcpy(pkt + resp_len + 5, &pair->peer->candidates[i].port, 2);
            resp_len += sizeof(p2p_compact_candidate_t);
        }
    }
    else {

        // 地址变更通知：仅发送 1 个公网候选地址（来自对端当前 UDP 源地址）
        cand_cnt = 1;
        pkt[13] = 1;
        pkt[resp_len] = 1; // srflx
        memcpy(pkt + resp_len + 1, &pair->peer->addr.sin_addr.s_addr, 4);
        memcpy(pkt + resp_len + 5, &pair->peer->addr.sin_port, 2);
        resp_len += sizeof(p2p_compact_candidate_t);
    }

    // 调试打印协议包信息
    printf("Send %s pkt to %s:%d, seq=0, flags=0, base_index=%u, cands=%d, len=%d, session_id=%" PRIu64 "\n",
           PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port),
           base_index, cand_cnt, resp_len, pair->session_id);

    sendto(udp_fd, (const char *)pkt, resp_len, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));

    printf("[UDP] V: %s sent to '%s', base_index=%u, cands=%d (ses_id=%" PRIu64 ")\n",
           PROTO, pair->local_peer_id, base_index, cand_cnt, pair->session_id);
}

// 发送 MSG_REQ_ACK: [hdr(4)][sid(2)][status(1)] = 7字节
// status: 0=成功, 1=失败
static void send_msg_req_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, uint16_t sid, uint8_t status) {
    uint8_t ack[4 + 3];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_MSG_REQ_ACK;
    hdr->flags = 0;
    hdr->seq = 0;
    nwrite_s(ack + 4, sid);
    ack[6] = status;

    printf("Send MSG_REQ_ACK pkt to %s, seq=0, flags=0, len=7, sid=%u, status=%u\n",
           to_str, sid, status);
    sendto(udp_fd, (const char *)ack, 7, 0, (const struct sockaddr *)to, sizeof(*to));
}

// 发送 MSG_REQ 给对端（Server→对端 relay）
// 协议格式: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=SIG_MSG_FLAG_RELAY
// msg=消息类型，msg=0 时对端自动 echo
static void send_msg_req_to_peer(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_REQ";

    assert(pair && PEER_ONLINE(pair));
    assert(pair->req_pending);

    uint8_t pkt[4 + 8 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_REQ;
    hdr->flags = SIG_MSG_FLAG_RELAY;  // 标识这是 Server→对端 的 relay 包
    hdr->seq = 0;

    int n = 4;
    nwrite_ll(pkt + n, pair->peer->session_id); n += 8;  // 对端的 session_id
    nwrite_s(pkt + n, pair->req_sid); n += 2;
    pkt[n++] = pair->req_msg;
    if (pair->req_data_len > 0) {
        memcpy(pkt + n, pair->req_data, pair->req_data_len);
        n += pair->req_data_len;
    }

    // 调试打印协议包信息
    printf("Send %s pkt to %s:%d, seq=0, flags=0x%02x, len=%d, sid=%u, msg=%u, session_id=%" PRIu64 "\n",
           PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port),
           hdr->flags, n, pair->req_sid, pair->req_msg, pair->peer->session_id);

    sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

    printf("[UDP] V: %s sent to '%s' (relay), sid=%u, msg=%u, retries=%d (ses_id=%" PRIu64 ")\n",
           PROTO, pair->peer->local_peer_id, pair->req_sid, pair->req_msg, pair->req_retry, pair->peer->session_id);
}

// 发送 MSG_RESP_ACK 给 B 端（Server→B）
// 协议格式: [sid(2)]
// 说明: 确认收到 B 端的 MSG_RESP，让 B 停止重发
static void send_msg_resp_ack_to_responder(sock_t udp_fd, const struct sockaddr_in *addr, 
                                           const char *peer_id, uint16_t sid) {
    const char* PROTO = "MSG_RESP_ACK";

    uint8_t pkt[4 + 2];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    nwrite_s(pkt + 4, sid);

    // 调试打印协议包信息
    printf("Send %s pkt to %s:%d, seq=0, flags=0, len=2, sid=%u\n",
           PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), sid);

    sendto(udp_fd, (const char *)pkt, 6, 0, (struct sockaddr *)addr, sizeof(*addr));

    printf("[UDP] V: %s sent to '%s', sid=%u\n", PROTO, peer_id, sid);
}

// 发送 MSG_RESP 给请求方（Server→请求方，从缓存的 resp_* 字段构建）
// 协议格式: [sid(2)][code(1)][data(N)]（正常响应，code=响应码）
//          [sid(2)]（错误响应，flags 中标识错误类型）
static void send_msg_resp_to_requester(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_RESP";

    assert(pair && pair->resp_pending);

    uint8_t pkt[4 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP;
    hdr->flags = pair->resp_flags;
    hdr->seq = 0;

    int n = 4;
    nwrite_s(pkt + n, pair->resp_sid); n += 2;
    
    // 如果是正常响应，包含 msg 和 data
    if (!(pair->resp_flags & (SIG_MSG_FLAG_PEER_OFFLINE | SIG_MSG_FLAG_TIMEOUT))) {
        pkt[n++] = pair->resp_msg;
        if (pair->req_data_len > 0) {
            memcpy(pkt + n, pair->req_data, pair->req_data_len);
            n += pair->req_data_len;
        }
    }

    // 调试打印协议包信息
    printf("Send %s pkt to %s:%d, seq=0, flags=0x%02x, len=%d, sid=%u\n",
           PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port),
           pair->resp_flags, n, pair->resp_sid);

    sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));

    printf("[UDP] V: %s sent to '%s', sid=%u, flags=0x%02x, retries=%d (ses_id=%" PRIu64 ")\n",
           PROTO, pair->local_peer_id, pair->resp_sid, pair->resp_flags, pair->resp_retry, pair->session_id);
}

//-----------------------------------------------------------------------------

// 从待确认链表移除
static void remove_info0_pending(compact_pair_t *pair) {

    if (!g_info0_pending_head || !pair->next_info0_pending) return;

    // 如果是头节点
    if (g_info0_pending_head == pair) {
        g_info0_pending_head = pair->next_info0_pending;
        pair->next_info0_pending = NULL;
        if (g_info0_pending_head == (void*)-1) {
            g_info0_pending_head = NULL;
            g_info0_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_info0_pending_head;
    while (prev->next_info0_pending != pair) {
        if (prev->next_info0_pending == (void*)-1) return;  // 没有找到
        prev = prev->next_info0_pending;
    }

    prev->next_info0_pending = pair->next_info0_pending;
    
    // 如果移除的是尾节点，更新尾指针
    if (pair->next_info0_pending == (void*)-1) {
        g_info0_pending_rear = prev;
    }
    
    pair->next_info0_pending = NULL;
}

// 将配对加入 PEER_INFO(seq=0) 待确认链表
static void enqueue_info0_pending(compact_pair_t *pair, uint8_t base_index, uint64_t now) {

    if (pair->next_info0_pending) {
        remove_info0_pending(pair);
    }

    pair->info0_base_index = base_index;
    pair->info0_retry = 0;
    pair->info0_sent_time = now;

    pair->next_info0_pending = (compact_pair_t*)(void*)-1;
    if (g_info0_pending_rear) {
        g_info0_pending_rear->next_info0_pending = pair;
        g_info0_pending_rear = pair;
    } else {
        g_info0_pending_head = pair;
        g_info0_pending_rear = pair;
    }
}

// 检查并重传未确认的 PEER_INFO 包
static void retry_info0_pending(sock_t udp_fd, uint64_t now) {

    if (!g_info0_pending_head) return;

    for(;;) {

        // 队列按时间排序，一旦遇到未超时的节点，后面都不会超时
        if (now - g_info0_pending_head->info0_sent_time < PEER_INFO0_RETRY_INTERVAL * 1000) {
            return;
        }

        // 将第一项移除
        compact_pair_t *q = g_info0_pending_head;
        g_info0_pending_head = q->next_info0_pending;

        // 检查是否超过最大重传次数
        if (q->info0_retry >= PEER_INFO0_MAX_RETRY) {
            
            // 超过最大重传次数，从链表移除（放弃）
            printf(LA_F("[UDP] W: PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n", LA_F66, 66),
                   q->local_peer_id, q->remote_peer_id, q->info0_retry);

            q->next_info0_pending = NULL;
            if (q->info0_base_index == 0) {
                q->info0_acked = -1;  // 首包失败：标记为已放弃
            }

            // 如果这是最后一项
            if (g_info0_pending_head == (void*)-1) {
                g_info0_pending_head = NULL;
                g_info0_pending_rear = NULL;
                return;
            }
        }
        // 重传
        else { assert(PEER_ONLINE(q));

            send_peer_info_seq0(udp_fd, q, q->info0_base_index);

            // 更新时间和重传次数
            q->info0_retry++;
            q->info0_sent_time = now;

            // 如果这是最后一项
            if (g_info0_pending_head == (void*)-1) {
                g_info0_pending_head = q;
            }
            // 重新加到队尾（因为时间更新了，按时间排序）
            else {
                q->next_info0_pending = (compact_pair_t*)(void*)-1;
                g_info0_pending_rear->next_info0_pending = q;
                g_info0_pending_rear = q;
            }

            printf(LA_F("[UDP] V: PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%" PRIu64 ")\n", 0),
                   q->local_peer_id, q->remote_peer_id,
                   q->info0_retry, PEER_INFO0_MAX_RETRY, q->session_id);

            if (g_info0_pending_head == q) return;
        }
    }
}

//-----------------------------------------------------------------------------
// MSG RPC 链表管理和处理函数

// 缓存响应数据到 pair->resp_* 并从 REQ 链表移到 RESP 链表
static void transition_to_resp_pending(sock_t udp_fd, compact_pair_t *pair, uint64_t now,
                                       uint8_t flags, uint8_t msg, const uint8_t *data, int len);

// 从 REQ 待确认链表移除
static void remove_req_pending(compact_pair_t *pair) {

    if (!g_req_pending_head || !pair->next_req_pending) return;

    if (g_req_pending_head == pair) {
        g_req_pending_head = pair->next_req_pending;
        pair->next_req_pending = NULL;
        if (g_req_pending_head == (void*)-1) {
            g_req_pending_head = NULL;
            g_req_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_req_pending_head;
    while (prev->next_req_pending != pair) {
        if (prev->next_req_pending == (void*)-1) return;
        prev = prev->next_req_pending;
    }

    prev->next_req_pending = pair->next_req_pending;
    if (pair->next_req_pending == (void*)-1) {
        g_req_pending_rear = prev;
    }
    pair->next_req_pending = NULL;
}

// 将配对加入 REQ 待确认链表
static void enqueue_req_pending(compact_pair_t *pair, uint64_t now) {

    if (pair->next_req_pending) {
        remove_req_pending(pair);
    }

    pair->req_sent_time = now;
    pair->req_retry = 0;

    pair->next_req_pending = (compact_pair_t*)(void*)-1;
    if (g_req_pending_rear) {
        g_req_pending_rear->next_req_pending = pair;
        g_req_pending_rear = pair;
    } else {
        g_req_pending_head = pair;
        g_req_pending_rear = pair;
    }
}

// 检查并重传 MSG_REQ（等待对端响应阶段）
static void retry_req_pending(sock_t udp_fd, uint64_t now) {

    if (!g_req_pending_head) return;

    compact_pair_t *curr = g_req_pending_head;
    while (curr != (compact_pair_t*)(void*)-1) {
        compact_pair_t *next = curr->next_req_pending;

        if (now - curr->req_sent_time >= MSG_REQ_RETRY_INTERVAL * 1000) {
            
            // 检查对端是否离线
            if (!curr->peer || curr->peer == (compact_pair_t*)(void*)-1) {
                printf("[UDP] W: MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%" PRIu64 ")\n",
                       curr->local_peer_id, curr->req_sid, curr->session_id);
                
                transition_to_resp_pending(udp_fd, curr, now, SIG_MSG_FLAG_PEER_OFFLINE, 0, NULL, 0);
            }
            // 超过最大重传次数，发送超时错误
            else if (curr->req_retry >= MSG_REQ_MAX_RETRY) {
                printf("[UDP] W: MSG_REQ timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%" PRIu64 ")\n",
                       curr->req_retry, curr->local_peer_id, curr->req_sid, curr->session_id);
                
                transition_to_resp_pending(udp_fd, curr, now, SIG_MSG_FLAG_TIMEOUT, 0, NULL, 0);
            }
            // 重传 MSG_REQ 给对端
            else {
                send_msg_req_to_peer(udp_fd, curr);
                curr->req_sent_time = now;
                curr->req_retry++;
            }
        }

        curr = next;
    }
}


// 从 RESP 待确认链表移除
static void remove_resp_pending(compact_pair_t *pair) {

    if (!g_resp_pending_head || !pair->next_resp_pending) return;

    if (g_resp_pending_head == pair) {
        g_resp_pending_head = pair->next_resp_pending;
        pair->next_resp_pending = NULL;
        if (g_resp_pending_head == (void*)-1) {
            g_resp_pending_head = NULL;
            g_resp_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_resp_pending_head;
    while (prev->next_resp_pending != pair) {
        if (prev->next_resp_pending == (void*)-1) return;
        prev = prev->next_resp_pending;
    }

    prev->next_resp_pending = pair->next_resp_pending;
    if (pair->next_resp_pending == (void*)-1) {
        g_resp_pending_rear = prev;
    }
    pair->next_resp_pending = NULL;
}

// 将配对加入 RESP 待确认链表
static void enqueue_resp_pending(compact_pair_t *pair, uint64_t now) {

    if (pair->next_resp_pending) {
        remove_resp_pending(pair);
    }

    pair->resp_sent_time = now;
    pair->resp_retry = 0;

    pair->next_resp_pending = (compact_pair_t*)(void*)-1;
    if (g_resp_pending_rear) {
        g_resp_pending_rear->next_resp_pending = pair;
        g_resp_pending_rear = pair;
    } else {
        g_resp_pending_head = pair;
        g_resp_pending_rear = pair;
    }
}

// 检查并重传 MSG_RESP（等待请求方确认阶段，从缓存数据重传）
static void retry_resp_pending(sock_t udp_fd, uint64_t now) {

    if (!g_resp_pending_head) return;

    compact_pair_t *curr = g_resp_pending_head;
    while (curr != (compact_pair_t*)(void*)-1) {
        compact_pair_t *next = curr->next_resp_pending;

        if (now - curr->resp_sent_time >= MSG_RESP_RETRY_INTERVAL * 1000) {
            
            // 超过最大重传次数，放弃
            if (curr->resp_retry >= MSG_RESP_MAX_RETRY) {
                printf("[UDP] W: MSG_RESP gave up after %d retries, sid=%u (ses_id=%" PRIu64 ")\n",
                       curr->resp_retry, curr->resp_sid, curr->session_id);
                
                remove_resp_pending(curr);
                curr->resp_pending = false;
            }
            // 从缓存重传 MSG_RESP
            else {
                curr->resp_retry++;
                send_msg_resp_to_requester(udp_fd, curr);
                curr->resp_sent_time = now;
            }
        }

        curr = next;
    }
}


// 缓存响应数据到 pair->resp_* 并从 REQ 链表移到 RESP 链表
static void transition_to_resp_pending(sock_t udp_fd, compact_pair_t *pair, uint64_t now,
                                       uint8_t flags, uint8_t msg, const uint8_t *data, int len) {
    // 从 REQ 链表移除
    remove_req_pending(pair);
    pair->req_pending = false;

    // 缓存响应
    pair->resp_pending = true;
    pair->resp_sid = pair->req_sid;
    pair->resp_flags = flags;
    pair->resp_msg = msg;
    pair->req_data_len = 0;
    if (len > 0 && data) {
        memcpy(pair->req_data, data, len);
        pair->req_data_len = len;
    }

    // 发送并加入 RESP 链表
    enqueue_resp_pending(pair, now);
    send_msg_resp_to_requester(udp_fd, pair);
}

//-----------------------------------------------------------------------------

// 检测地址变更并通知对端（任何包都可能触发 NAT 重绑定检测）
// 返回 true 表示地址发生了变化
static bool check_addr_change(sock_t udp_fd, compact_pair_t *pair, const struct sockaddr_in *from) {

    if (memcmp(&pair->addr, from, sizeof(*from)) == 0) return false;

    // 更新为最新地址
    pair->addr = *from;

    // 如果对方在线，通知对方
    if (PEER_ONLINE(pair)) {

        // 首包已确认，立即发送地址变更通知
        if (pair->peer->info0_acked) {

            pair->peer->addr_notify_seq = (uint8_t)(pair->peer->addr_notify_seq + 1);
            if (pair->peer->addr_notify_seq == 0) pair->peer->addr_notify_seq = 1;

            send_peer_info_seq0(udp_fd, pair->peer, pair->peer->addr_notify_seq);
            enqueue_info0_pending(pair->peer, pair->peer->addr_notify_seq, P_tick_ms());

            printf("[UDP] I: Address changed for '%s', notifying '%s' (ses_id=%" PRIu64 ")\n",
                   pair->local_peer_id, pair->peer->local_peer_id, pair->peer->session_id);
        }
        // 首包未确认（正在同步中），延期发送地址变更通知（等 ACK 后再发）
        // + 设置 addr_notify_seq = 1 标记有延期通知
        else {

            if (pair->peer->addr_notify_seq == 0) pair->peer->addr_notify_seq = 1;
            
            printf("[UDP] I: Address changed for '%s', defer notification until first ACK (ses_id=%" PRIu64 ")\n",
                   pair->local_peer_id, pair->peer->session_id);
        }
    }

    return true;
}

//-----------------------------------------------------------------------------

// 处理 COMPACT 模式信令（UDP 无状态，对应 p2p_signal_compact 模块）
static void handle_compact_signaling(sock_t udp_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {
    
    if (len < 4) return;  // 至少需要包头
    
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4; size_t payload_len = len - 4;
    
    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // SIG_PKT_REGISTER: [local_peer_id(32)][remote_peer_id(32)][instance_id(4)][candidate_count(1)][candidates(N*7)]
    switch (hdr->type) {
    case SIG_PKT_REGISTER: { const char* PROTO = "REGISTER";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len <= P2P_PEER_ID_MAX * 2 + 4) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F64, 64), PROTO, payload_len);
            return;
        }

        // 解析 instance_id
        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX * 2);
        if (instance_id == 0) {
            printf(LA_F("[UDP] E: %s: invalid instance_id=0 from %s\n", LA_F64, 64), PROTO, from_str);
            return;
        }

        // 直接以 local_peer_id + remote_peer_id 作为 key 检索
        compact_pair_t *existing = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, existing);

        // 如果重复注册（客户端未收到 ACK 前的重传）
        if (existing && existing->instance_id == instance_id) {

            check_addr_change(udp_fd, existing, from);
            existing->last_active = P_tick_ms();

            send_register_ack(udp_fd, from, from_str, 
                              PEER_ONLINE(existing) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE,
                              existing->session_id, instance_id);
            return;
        }

        // 首次注册 或 instance_id 变更

        // payload 直接指向 peer_key：[local_peer_id(32)][remote_peer_id(32)]
        const char *local_peer_id = (const char *)payload;
        const char *remote_peer_id = (const char *)(payload + P2P_PEER_ID_MAX);

        // 解析候选列表
        int candidate_count = 0;
        p2p_compact_candidate_t candidates[MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));
        size_t cand_offset = P2P_PEER_ID_MAX * 2 + 4;
        candidate_count = payload[cand_offset];
        if (candidate_count > MAX_CANDIDATES) {
            candidate_count = MAX_CANDIDATES;
        }
        cand_offset++;
        for (int i = 0; i < candidate_count && cand_offset + sizeof(p2p_compact_candidate_t) <= payload_len; i++) {
            candidates[i].type = payload[cand_offset];
            memcpy(&candidates[i].ip, payload + cand_offset + 1, sizeof(uint32_t));
            memcpy(&candidates[i].port, payload + cand_offset + 5, sizeof(uint16_t));
            cand_offset += sizeof(p2p_compact_candidate_t);
        }

        printf(LA_F("[UDP] V: %s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n", LA_F73, 73),
               PROTO, P2P_PEER_ID_MAX, local_peer_id, P2P_PEER_ID_MAX, remote_peer_id, instance_id, candidate_count);

        // 如果配对不存在（首次注册），分配一个空位
        int i;
        if (existing) i = (int)(existing - g_compact_pairs);
        else {
            for (i = 0; i < MAX_PEERS; i++) {
                if (!g_compact_pairs[i].valid) { 
                    g_compact_pairs[i].valid = true;
                    
                    // 在首次注册时分配 session_id（后续 MSG_REQ 直接使用）
                    g_compact_pairs[i].instance_id = instance_id;
                    g_compact_pairs[i].session_id = generate_session_id();
                    
                    memcpy(g_compact_pairs[i].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
                    memcpy(g_compact_pairs[i].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
                    g_compact_pairs[i].peer = NULL;
                    g_compact_pairs[i].info0_acked = false;
                    g_compact_pairs[i].addr_notify_seq = 0;
                    g_compact_pairs[i].info0_base_index = 0;
                    g_compact_pairs[i].info0_sent_time = 0;
                    g_compact_pairs[i].info0_retry = 0;
                    g_compact_pairs[i].next_info0_pending = NULL;
                    
                    // MSG RPC 初始化
                    g_compact_pairs[i].req_pending = false;
                    g_compact_pairs[i].req_sid = 0;
                    g_compact_pairs[i].req_data_len = 0;
                    g_compact_pairs[i].next_req_pending = NULL;
                    g_compact_pairs[i].resp_pending = false;
                    g_compact_pairs[i].resp_sid = 0;
                    g_compact_pairs[i].next_resp_pending = NULL;
                    
                    // 添加到 peer_key/session 索引
                    HASH_ADD(hh_peer, g_pairs_by_peer, local_peer_id, P2P_PEER_ID_MAX * 2, &g_compact_pairs[i]);
                    HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), &g_compact_pairs[i]);
                    break;
                }
            }
            // 无法分配槽位，发送错误 ACK
            if (i == MAX_PEERS) {
                send_register_ack(udp_fd, from, from_str, 2, 0, instance_id);
                return;
            }
        }
        compact_pair_t *local = &g_compact_pairs[i];

        // 如果是 instance_id 变更（客户端重启），必须重置旧会话
        if (existing) {

            printf(LA_F("[UDP] I: %s from '%.*s': new instance(old=%u new=%u), resetting session\n", LA_F75, 75),
                   PROTO, P2P_PEER_ID_MAX, local_peer_id, local->instance_id, instance_id);

            // 通知对端下线（如果对端在线且有 session_id）
            if (PEER_ONLINE(local)
                && local->peer->session_id != 0) {

                send_peer_off(udp_fd, local->peer, "reregister");
            }

            // 从待确认链表移除
            if (local->next_info0_pending) remove_info0_pending(local);

            // 从 session_id 索引移除
            if (local->session_id != 0) {
                HASH_DELETE(hh, g_pairs_by_session, local);
            }

            // 重置状态（保留 valid=true 和 hh_peer 不变）
            local->instance_id = instance_id;
            local->session_id = generate_session_id();
            HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), local);
            local->peer = NULL;
            local->info0_acked = false;
            local->addr_notify_seq = 0;
            local->info0_base_index = 0;
            local->info0_retry = 0;
            local->info0_sent_time = 0;
        }
        local->instance_id = instance_id;
        local->addr = *from;

        // 记录本端的候选列表
        local->candidate_count = candidate_count;
        if (candidate_count) {
            memcpy(local->candidates, candidates, sizeof(p2p_compact_candidate_t) * candidate_count);
        }

        // 查找反向配对：构造反向 peer_key [remote_peer_id][local_peer_id]
        char reverse_key[P2P_PEER_ID_MAX * 2];
        memcpy(reverse_key, payload + P2P_PEER_ID_MAX, P2P_PEER_ID_MAX);
        memcpy(reverse_key + P2P_PEER_ID_MAX, payload, P2P_PEER_ID_MAX);
        compact_pair_t* remote = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, reverse_key, P2P_PEER_ID_MAX * 2, remote);
        int remote_idx = remote ? (int)(remote - g_compact_pairs) : -1;

        // 如果之前是（自己之前是已连接过的）对端断开连接，重新连接时重置对端状态
        if (local->peer == (compact_pair_t*)(void*)-1) local->peer = NULL;

        local->last_active = P_tick_ms();

        // 发送 REGISTER_ACK（无需确认重发，客户端会在收到 ACK 前一直重试注册请求）
        {
            uint8_t status = (remote_idx >= 0) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE;
            send_register_ack(udp_fd, from, from_str, status, local->session_id, instance_id);
        }

        if (remote_idx >= 0) {

            compact_pair_t *remote = &g_compact_pairs[remote_idx];

            // 首次匹配成功
            if (local->peer == NULL || remote->peer == NULL) {

                // 建立双向关联
                local->peer = remote; remote->peer = local;

                // session_id 已在 REGISTER 阶段分配
                assert(local->session_id != 0 && remote->session_id != 0);

                local->info0_acked = false;
                local->addr_notify_seq = 0;
                local->info0_base_index = 0;
                local->info0_retry = 0;
                local->info0_sent_time = 0;
                remote->info0_acked = false;
                remote->addr_notify_seq = 0;
                remote->info0_base_index = 0;
                remote->info0_retry = 0;
                remote->info0_sent_time = 0;

                // 向双方发送服务器维护的首个 PEER_INFO(seq=0, base_index=0)
                send_peer_info_seq0(udp_fd, local, 0);
                enqueue_info0_pending(local, 0, local->last_active);

                send_peer_info_seq0(udp_fd, remote, 0);
                enqueue_info0_pending(remote, 0, local->last_active);

                printf(LA_F("[UDP] I: Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n", LA_F68, 68),
                       P2P_PEER_ID_MAX, local_peer_id, remote->candidate_count,
                       P2P_PEER_ID_MAX, remote_peer_id, local->candidate_count);
            }
            // 地址变更通知已在 check_addr_change() 中统一处理
            else assert(local->peer == remote && remote->peer == local);

        } else printf(LA_F("[UDP] V: %s: waiting for peer '%.*s' to register\n", LA_F85, 85), PROTO, P2P_PEER_ID_MAX, remote_peer_id);
    } break;

    // SIG_PKT_UNREGISTER: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端主动断开时发送，请求服务器立即释放配对槽位。
    // 【服务端可选实现】如果不处理此包类型，客户端自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制。
    case SIG_PKT_UNREGISTER: { const char* PROTO = "UNREGISTER";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < P2P_PEER_ID_MAX * 2) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F64, 64), PROTO, payload_len);
            return;
        }

        // 直接用 hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, pair);
        
        if (pair && pair->valid) {
            char local_peer_id[P2P_PEER_ID_MAX + 1] = {0};
            char remote_peer_id[P2P_PEER_ID_MAX + 1] = {0};
            memcpy(local_peer_id, pair->local_peer_id, P2P_PEER_ID_MAX);
            memcpy(remote_peer_id, pair->remote_peer_id, P2P_PEER_ID_MAX);

            printf(LA_F("[UDP] V: %s: accepted, releasing slot for '%s' -> '%s'\n", LA_F86, 86),
                   PROTO, local_peer_id, remote_peer_id);

            // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
            if (PEER_ONLINE(pair) && pair->peer->session_id != 0) {
                send_peer_off(udp_fd, pair->peer, "unregister");
            }
            
            // 从待确认链表移除
            if (pair->next_info0_pending) {
                remove_info0_pending(pair);
            }

            // 从 MSG 链表移除
            if (pair->next_req_pending) {
                remove_req_pending(pair);
            }
            if (pair->next_resp_pending) {
                remove_resp_pending(pair);
            }

            // 从哈希表删除
            if (pair->session_id != 0) {
                HASH_DELETE(hh, g_pairs_by_session, pair);
            }
            HASH_DELETE(hh_peer, g_pairs_by_peer, pair);

            pair->valid = false;
            pair->session_id = 0;
            pair->peer = NULL;
            pair->addr_notify_seq = 0;
            pair->info0_base_index = 0;
            pair->info0_retry = 0;
            pair->info0_sent_time = 0;
            pair->req_pending = false;
            pair->req_sid = 0;
            pair->req_data_len = 0;
            pair->resp_pending = false;
            pair->resp_sid = 0;
        }
    } break;

    // SIG_PKT_ALIVE: [session_id(8)]
    // 客户端定期发送以保持槽位活跃，更新 last_active 时间
    case SIG_PKT_ALIVE: { const char* PROTO = "ALIVE";

        if (payload_len < 8) return;

        uint64_t session_id = nget_ll(payload);

        // 直接用 session_id hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);        
        if (pair && pair->valid) {

            pair->last_active = P_tick_ms();
            check_addr_change(udp_fd, pair, from);

            // 回复 ACK
            {   const char* ACK_PROTO = "ALIVE_ACK";

                uint8_t ack[4];
                p2p_pkt_hdr_encode(ack, SIG_PKT_ALIVE_ACK, 0, 0);

                // 调试打印协议包信息
                printf("Send %s pkt to %s, seq=0, flags=0, len=4\n", ACK_PROTO, from_str);

                sendto(udp_fd, (const char *)ack, 4, 0, (struct sockaddr *)from, sizeof(*from));

                printf(LA_F("[UDP] V: %s received and %s sent for '%s' (ses_id=%" PRIu64 ")\n", 0),
                       PROTO, ACK_PROTO, pair->local_peer_id, session_id);
            }
        }
    } break;

    // SIG_PKT_PEER_INFO_ACK: ACK 确认收到 PEER_INFO 包
    // 格式: [hdr(4)][session_id(8)]，确认序号使用 hdr->seq
    case SIG_PKT_PEER_INFO_ACK: { const char* PROTO = "PEER_INFO_ACK";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < P2P_PEER_ID_MAX) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F64, 64), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);
        uint16_t ack_seq = ntohs(hdr->seq);
        if (ack_seq > 16) {
            printf(LA_F("[UDP] E: %s: invalid seq=%u\n", LA_F63, 63), PROTO, ack_seq);
            return;
        }

        // ack_seq=0 的 ACK 是对服务器发送的首个 PEER_INFO 的确认，服务器需要处理
        if (ack_seq == 0) {
            // 通过 session_id 查找对应的配对记录
            compact_pair_t *pair = NULL;
            HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
            if (pair && pair->valid) {
                check_addr_change(udp_fd, pair, from);

                // 标记为已确认，从待确认链表移除
                bool is_first_ack = (!pair->info0_acked && pair->info0_base_index == 0);
                if (is_first_ack) {
                    pair->info0_acked = true;

                    printf(LA_F("[UDP] V: %s: confirmed '%s', retries=%d (ses_id=%" PRIu64 ")\n", 0),
                           PROTO, pair->local_peer_id, pair->info0_retry, session_id);
                }

                // 从待确认链表移除（首包和地址变更通知都走 seq=0 ACK）
                if (pair->next_info0_pending) {
                    remove_info0_pending(pair);
                }

                pair->info0_base_index = 0;
                pair->info0_retry = 0;
                pair->info0_sent_time = 0;

                // 如果首包 ACK 且有延期的地址变更通知（addr_notify_seq != 0），则立即发送地址变更通知
                if (is_first_ack && pair->addr_notify_seq != 0) {

                    send_peer_info_seq0(udp_fd, pair, pair->addr_notify_seq);
                    enqueue_info0_pending(pair, pair->addr_notify_seq, P_tick_ms());

                    printf("[UDP] I: Sending deferred address change notification to '%s' (ses_id=%" PRIu64 ")\n",
                           pair->local_peer_id, session_id);
                }
            }
            else printf(LA_F("[UDP] W: %s for unknown ses_id=%" PRIu64 "\n", 0), PROTO, session_id);
        }
        // ack_seq≠0 的 ACK 是客户端之间的确认，服务器只负责 relay 转发
        else {
            compact_pair_t *pair = NULL;
            HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
            if (pair && pair->valid && PEER_ONLINE(pair)) {
                check_addr_change(udp_fd, pair, from);

                // 转发给对方
                sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                       (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                printf(LA_F("[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                       PROTO, ack_seq, pair->local_peer_id, pair->remote_peer_id, session_id);
            }
            else printf(LA_F("[UDP] W: Cannot relay %s: ses_id=%" PRIu64 " (peer unavailable)\n", 0), PROTO, session_id);
        }

    } break;

    // SIG_PKT_PEER_INFO/P2P_PKT_RELAY_DATA/P2P_PKT_RELAY_ACK/P2P_PKT_RELAY_CRYPTO: relay 转发给对方
    // 格式：所有包都包含 session_id(4) 在 payload 开头
    case SIG_PKT_PEER_INFO:
    case P2P_PKT_RELAY_DATA:
    case P2P_PKT_RELAY_ACK:
    case P2P_PKT_RELAY_CRYPTO: {
        const char* PROTO = (hdr->type == SIG_PKT_PEER_INFO) ? "PEER_INFO" :
                           (hdr->type == P2P_PKT_RELAY_DATA) ? "RELAY_DATA" :
                           (hdr->type == P2P_PKT_RELAY_ACK) ? "RELAY_ACK" : "RELAY_CRYPTO";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        // SIG_PKT_PEER_INFO 特殊处理：seq=0 是服务器维护的包，不应该出现在这里
        if (hdr->type == SIG_PKT_PEER_INFO && hdr->seq == 0) {
            printf(LA_F("[UDP] E: %s seq=0 from client %s (server-only, dropped)\n", LA_F67, 67), PROTO, from_str);
            return;
        }

        // 所有需要 relay 的包格式都是 [session_id(8)][...]
        if (payload_len < P2P_PEER_ID_MAX) {
            printf(LA_F("[UDP] E: Relay %s: bad payload(len=%zu)\n", LA_F82, 82), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);

        // 根据 session_id 查找配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
        if (!pair || !pair->valid) {
            printf(LA_F("[UDP] W: Relay %s for unknown ses_id=%" PRIu64 " (dropped)\n", 0), PROTO, session_id);
            return;
        }

        check_addr_change(udp_fd, pair, from);

        // 对方不存在，丢弃
        if (!pair->peer || pair->peer == (compact_pair_t*)(void*)-1) {
            printf(LA_F("[UDP] W: Relay %s for ses_id=%" PRIu64 ": peer unavailable (dropped)\n", 0), PROTO, session_id);
            return;
        }

        // 转发给对方
        sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
               (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

        if (hdr->type == SIG_PKT_PEER_INFO) {
            printf(LA_F("[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else if (hdr->type == P2P_PKT_RELAY_DATA || hdr->type == P2P_PKT_RELAY_CRYPTO) {
            printf(LA_F("[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else {
            printf(LA_F("[UDP] V: Relay %s: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, pair->local_peer_id, pair->remote_peer_id, session_id);
        }
    } break;

    // SIG_PKT_MSG_REQ: A→Server 或 B→Server（通过 flags 区分）
    // A→Server: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=0，msg=消息类型
    // B→Server（不会有这种情况，B 只发 MSG_RESP）
    case SIG_PKT_MSG_REQ: { const char* PROTO = "MSG_REQ";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F64, 64), PROTO, payload_len);
            return;
        }

        // MSG_REQ 只能是 A→Server（flags=0）
        if (hdr->flags & SIG_MSG_FLAG_RELAY) {
            printf(LA_F("[UDP] E: %s: invalid relay flag from client\n", LA_F282, 282), PROTO);
            return;
        }

        int msg_data_len = (int)(payload_len - 11);
        if (msg_data_len > P2P_MSG_DATA_MAX) {
            printf(LA_F("[UDP] E: %s: data too large (len=%d)\n", LA_F281, 281), PROTO, msg_data_len);
            return;
        }

        // 解析 session_id、sid、msg、data
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);
        uint8_t msg = payload[10];
        const uint8_t *msg_data = payload + 11;

        printf(LA_F("[UDP] V: %s accepted from %s, ses_id=%" PRIu64 ", sid=%u, msg=%u, len=%d\n", 0),
             PROTO, from_str, session_id, sid, msg, msg_data_len);

        // 查找 A 端的配对记录（通过 session_id 快速索引）
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);

        if (!requester || !requester->valid) {
            printf(LA_F("[UDP] W: %s: requester not found for ses_id=%" PRIu64 "\n", 0), PROTO, session_id);
            return;
        }

        // local_id 定位后，统一进行地址变更检测（NAT 重绑定）
        check_addr_change(udp_fd, requester, from);

        // 检查是否有挂起的请求
        if (requester->req_pending || requester->resp_pending) {
            printf(LA_F("[UDP] W: %s: already has pending msg, rejecting sid=%u\n", LA_F301, 301), PROTO, sid);
            send_msg_req_ack(udp_fd, from, from_str, sid, 1);
            return;
        }

        // 检查对端是否在线
        if (!requester->peer || requester->peer == (compact_pair_t*)(void*)-1) {
            printf(LA_F("[UDP] W: %s: peer '%s' not online, rejecting sid=%u\n", LA_F303, 303),
                   PROTO, requester->remote_peer_id, sid);
            send_msg_req_ack(udp_fd, from, from_str, sid, 1);
            printf("[UDP] V: MSG_REQ_ACK sent, status=peer_offline, sid=%u\n", sid);
            return;
        }

        // 缓存请求
        requester->req_pending = true;
        requester->req_sid = sid;
        requester->req_msg = msg;
        requester->req_data_len = msg_data_len;
        if (msg_data_len > 0) {
            memcpy(requester->req_data, msg_data, msg_data_len);
        }

        // 发送 REQ_ACK status=0（成功，开始中转）
        {
            send_msg_req_ack(udp_fd, from, from_str, sid, 0);
            printf("[UDP] V: MSG_REQ_ACK sent, status=success, sid=%u (ses_id=%" PRIu64 ")\n",
                   sid, requester->session_id);
        }

        // 立即向对端转发 MSG_REQ
        uint64_t now = P_tick_ms();
        enqueue_req_pending(requester, now);
        send_msg_req_to_peer(udp_fd, requester);

        printf(LA_F("[UDP] I: %s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->local_peer_id, requester->peer->local_peer_id,
               sid, msg, requester->session_id);
    } break;

    // SIG_PKT_MSG_RESP: B→Server（只能是 B 对 Server relay 的 MSG_REQ 的响应）
    // 格式: [session_id(8)][sid(2)][code(1)][data(N)]，code=响应码
    case SIG_PKT_MSG_RESP: { const char* PROTO = "MSG_RESP";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {  // session_id(8) + sid(2) + code(1) 响应码
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F64, 64), PROTO, payload_len);
            return;
        }

        // 解析
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);
        uint8_t resp_msg = payload[10];
        const uint8_t *resp_data = payload + 11;
        int resp_len = (int)(payload_len - 11);

        if (resp_len > P2P_MSG_DATA_MAX) {
            printf(LA_F("[UDP] E: %s: data too large (len=%d)\n", LA_F281, 281), PROTO, resp_len);
            return;
        }

        printf(LA_F("[UDP] V: %s accepted from %s, session_id=%" PRIu64 ", sid=%u, msg=%u, len=%d\n", 0),
               PROTO, from_str, session_id, sid, resp_msg, resp_len);

        // 根据 session_id 查找 requester（A 端）
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);
        
        if (!requester || !requester->valid) {
            printf(LA_F("[UDP] W: %s: unknown session_id=%" PRIu64 "\n", 0), PROTO, session_id);
            return;
        }

        // MSG_RESP 来自响应方（B），检查 B 的地址变更
        if (PEER_ONLINE(requester)) {
            check_addr_change(udp_fd, requester->peer, from);
        }

        // 检查是否有匹配的挂起请求（REQ 阶段）
        if (!requester->req_pending || requester->req_sid != sid) {
            // 可能是 RESP 阶段的重复响应（已收到过一次，正在等 A 的 ACK）
            if (requester->resp_pending && requester->resp_sid == sid) {
                // 幂等回复 ACK
                send_msg_resp_ack_to_responder(udp_fd, from,
                    requester->peer ? requester->peer->local_peer_id : "unknown", sid);
                return;
            }
            printf(LA_F("[UDP] W: %s: no matching pending msg (sid=%u, expected=%u)\n", LA_F302, 302),
                   PROTO, sid, requester->req_sid);
            // 即使不匹配，也要发送 ACK（幂等，避免对端一直重发）
            send_msg_resp_ack_to_responder(udp_fd, from, "unknown", sid);
            return;
        }

        // 立即发送 MSG_RESP_ACK 给响应方（让对端停止重发）
        const char *responder_peer_id = requester->peer ? requester->peer->local_peer_id : "unknown";
        send_msg_resp_ack_to_responder(udp_fd, from, responder_peer_id, sid);

        // 缓存响应数据并从 REQ 链表移到 RESP 链表，同时发送给请求方
        uint64_t now = P_tick_ms();
        transition_to_resp_pending(udp_fd, requester, now, 0, resp_msg, resp_data, resp_len);

        printf(LA_F("[UDP] I: %s forwarded: '%s' -> '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, responder_peer_id, requester->local_peer_id,
               sid, requester->session_id);
    } break;

    // SIG_PKT_MSG_RESP_ACK: A→Server（A 确认收到 MSG_RESP）
    // 格式: [sid(2)]
    case SIG_PKT_MSG_RESP_ACK: { const char* PROTO = "MSG_RESP_ACK";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 2) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F64, 64), PROTO, payload_len);
            return;
        }

        uint16_t sid = nget_s(payload);

        printf(LA_F("[UDP] V: %s accepted from %s, sid=%u\n", LA_F288, 288), PROTO, from_str, sid);

        // 通过源地址查找 requester
        compact_pair_t *requester = NULL;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_compact_pairs[i].valid &&
                memcmp(&g_compact_pairs[i].addr, from, sizeof(*from)) == 0) {
                requester = &g_compact_pairs[i];
                break;
            }
        }

        if (!requester) {
            printf(LA_F("[UDP] W: %s: requester not found for %s\n", LA_F304, 304), PROTO, from_str);
            return;
        }

        // 检查是否有匹配的挂起响应转发
        if (!requester->resp_pending || requester->resp_sid != sid) {
            printf(LA_F("[UDP] V: %s: no matching pending msg (sid=%u)\n", LA_F296, 296), PROTO, sid);
            return;
        }

        // 清理状态
        remove_resp_pending(requester);
        requester->resp_pending = false;

        printf(LA_F("[UDP] V: %s: RPC complete for '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->local_peer_id, sid, requester->session_id);
    } break;
    
    default:
        printf(LA_F("[UDP] W: Unknown packet type 0x%02x from %s\n", LA_F87, 87), hdr->type, from_str);
        break;
    } // switch
}

// 清理过期的 COMPACT 模式配对记录
static void cleanup_compact_pairs(sock_t udp_fd) {

    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_compact_pairs[i].valid || 
            (now - g_compact_pairs[i].last_active) <= COMPACT_PAIR_TIMEOUT * 1000) continue;

        printf(LA_F("[UDP] W: Timeout for pair '%s' -> '%s' (inactive for %.1f seconds)\n", LA_F72, 72), 
               g_compact_pairs[i].local_peer_id, g_compact_pairs[i].remote_peer_id,
               (now - g_compact_pairs[i].last_active) / 1000.0);
        
        // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
        if (PEER_ONLINE(&g_compact_pairs[i]) && 
            g_compact_pairs[i].peer->session_id != 0) {
            send_peer_off(udp_fd, g_compact_pairs[i].peer, "timeout");
        }
        
        // 从待确认链表移除
        if (g_compact_pairs[i].next_info0_pending) {
            remove_info0_pending(&g_compact_pairs[i]);
        }

        // 从 MSG 链表移除
        if (g_compact_pairs[i].next_req_pending) {
            remove_req_pending(&g_compact_pairs[i]);
        }
        if (g_compact_pairs[i].next_resp_pending) {
            remove_resp_pending(&g_compact_pairs[i]);
        }

        // 从哈希表删除
        if (g_compact_pairs[i].session_id != 0) {
            HASH_DELETE(hh, g_pairs_by_session, &g_compact_pairs[i]);
        }
        HASH_DELETE(hh_peer, g_pairs_by_peer, &g_compact_pairs[i]);

        g_compact_pairs[i].peer = NULL;  // 清空指针
        g_compact_pairs[i].session_id = 0;
        g_compact_pairs[i].addr_notify_seq = 0;
        g_compact_pairs[i].info0_base_index = 0;
        g_compact_pairs[i].info0_retry = 0;
        g_compact_pairs[i].info0_sent_time = 0;
        g_compact_pairs[i].req_pending = false;
        g_compact_pairs[i].req_sid = 0;
        g_compact_pairs[i].req_data_len = 0;
        g_compact_pairs[i].next_req_pending = NULL;
        g_compact_pairs[i].resp_pending = false;
        g_compact_pairs[i].resp_sid = 0;
        g_compact_pairs[i].next_resp_pending = NULL;
        g_compact_pairs[i].valid = false;
    }
}

///////////////////////////////////////////////////////////////////////////////

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "p2p0d"

// 信号处理函数
#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            printf("\n%s\n", LA_S("[SERVER] Received shutdown signal, exiting gracefully...", LA_S11, 11));
            g_running = 0;
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n%s\n", LA_S("[SERVER] Received shutdown signal, exiting gracefully...", LA_S11, 11));
        g_running = 0;
    }
}
#endif

int main(int argc, char *argv[]) {

    lang_init();  /* 注册默认英文字符串表 */

    // 设置命令行帮助信息
    ARGS_usage(NULL,
        "Description:\n"
        "  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n"
        "  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n"
        "  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n"
        "\n"
        "Examples:\n"
        "  $0                              # Default: port 9333, no probe, no relay\n"
        "  $0 -p 8888                      # Listen on port 8888\n"
        "  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n"
        "  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n"
        "  $0 --cn -p 8888                 # Chinese language\n"
        "\n"
        "Note: Run without arguments to use default configuration (port 9333)");

    // 解析命令行参数（如果无参数，使用默认配置不显示帮助）
    if (argc > 1) {
        ARGS_parse(argc, argv,
            &ARGS_DEF_port,
            &ARGS_DEF_probe_port,
            &ARGS_DEF_relay,
            &ARGS_DEF_cn,
            NULL);

        // 处理中文语言
        if (ARGS_cn.i64) {
            FILE *fp = fopen("lang.zh", "r");
            if (fp) { lang_load_fp(fp); fclose(fp); }
        }
    }

    // 获取参数值（设置默认值）
    int port = ARGS_port.i64 ? (int)ARGS_port.i64 : DEFAULT_PORT;
    g_probe_port = (int)ARGS_probe_port.i64;  // 默认 0
    g_relay_enabled = (bool)ARGS_relay.i64;

    // 验证端口范围
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number %d (range: 1-65535)\n\n", port);
        ARGS_print(argv[0]);
        return 1;
    }
    if (g_probe_port < 0 || g_probe_port > 65535) {
        fprintf(stderr, "Error: Invalid probe port %d (range: 0-65535)\n\n", g_probe_port);
        ARGS_print(argv[0]);
        return 1;
    }
    
    if (P_net_init() != E_NONE) {
        fprintf(stderr, "%s", LA_S("[SERVER] WSAStartup failed\n", 0));
        return 1;
    }

    // 初始化随机数生成器（用于生成安全的 session_id）
    P_rand_init();

    // 打印服务器配置信息
    printf(LA_F("[SERVER] Starting P2P signal server on port %d", LA_F34, 34), port);
    printf("\n");
    printf(LA_F("[SERVER] NAT probe: %s (port %d)", LA_F32, 32), 
           g_probe_port > 0 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1), 
           g_probe_port);
    printf("\n");
    printf(LA_F("[SERVER] Relay support: %s", LA_F33, 33), 
           g_relay_enabled ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1));
    printf("\n");

    // 注册信号处理
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        fprintf(stderr, "%s", LA_S("[SERVER] Failed to set console ctrl handler\n", 0));
    }
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    // 创建 TCP 监听套接字（用于 Relay 信令模式）
    sock_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == P_INVALID_SOCKET) {
        perror("TCP socket");
        return 1;
    }
    int sockopt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));

    // 创建 UDP 套接字（用于 COMPACT 信令模式）
    sock_t udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == P_INVALID_SOCKET) {
        perror("UDP socket");
        return 1;
    }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));

    // 创建 NAT 探测 UDP 套接字（可选，仅当配置了 probe_port 时）
    sock_t probe_fd = P_INVALID_SOCKET;
    if (g_probe_port > 0) {
        probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (probe_fd == P_INVALID_SOCKET) {
            perror("probe UDP socket");
            return 1;
        }
        setsockopt(probe_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));
    }

    // 绑定监听端口（TCP 和 UDP 同一端口）
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
       if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
       
        perror("TCP bind");
        return 1;
    }
    if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("UDP bind");
        return 1;
    }

    // 绑定 NAT 探测端口（独立端口，客户端用同一本地端口发包，服务器在此处看到不同映射地址）
    if (probe_fd != P_INVALID_SOCKET) {
        struct sockaddr_in probe_addr = {0};
        probe_addr.sin_family = AF_INET;
        probe_addr.sin_addr.s_addr = INADDR_ANY;
        probe_addr.sin_port = htons((unsigned short)g_probe_port);
        if (bind(probe_fd, (struct sockaddr *)&probe_addr, sizeof(probe_addr)) < 0) {
            perror("probe UDP bind");
            P_sock_close(probe_fd);
            probe_fd = P_INVALID_SOCKET;
            g_probe_port = 0;  /* 绑定失败，禁用探测功能 */
            printf("%s\n", LA_S("[SERVER] NAT probe disabled (bind failed)", LA_S10, 10));
        } else {
            printf(LA_F("[SERVER] NAT probe socket listening on port %d", LA_F31, 31), g_probe_port);
            printf("\n");
        }
    }

    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd, 10);
    printf(LA_F("P2P Signaling Server listening on port %d (TCP + UDP)...", LA_F27, 27), port);
    printf("\n");

    // 主循环
    fd_set read_fds;
    uint64_t last_cleanup = P_tick_ms(), last_compact_retry_check = last_cleanup;
    while (g_running) {

        uint64_t now = P_tick_ms();

        // 周期清理过期的 COMPACT 配对记录和 Relay 客户端连接
        if (now - last_cleanup >= CLEANUP_INTERVAL * 1000) {
            cleanup_compact_pairs(udp_fd);
            cleanup_relay_clients();
            last_cleanup = now;
        }
        
        // 检查并重传未确认的 PEER_INFO 包（每秒检查一次）
        if (g_info0_pending_head && (now - last_compact_retry_check) >= COMPACT_RETRY_INTERVAL * 1000) {
            retry_info0_pending(udp_fd, now);
            last_compact_retry_check = now;
        }

        // 检查并重传 MSG RPC 包（每秒检查一次）
        if ((g_req_pending_head || g_resp_pending_head) && (now - last_compact_retry_check) >= MSG_REQ_RETRY_INTERVAL * 1000) {
            retry_req_pending(udp_fd, now);
            retry_resp_pending(udp_fd, now);
        }

        // 设置要监听的套接口 fd
        // + TCP listen + TCP clients + UDP + probe UDP + 客户端...
        // + max_fd 必须是所有监听套接字中数值最大的那个（Windows 不使用此值，但 POSIX 需要正确设置）
        int max_fd = 0;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        FD_SET(udp_fd, &read_fds);
        if (probe_fd != P_INVALID_SOCKET) FD_SET(probe_fd, &read_fds);
#ifndef _WIN32
        max_fd = (int)((listen_fd > udp_fd) ? listen_fd : udp_fd);
        if (probe_fd != P_INVALID_SOCKET && (int)probe_fd > max_fd) max_fd = (int)probe_fd;
#endif
        // 添加有效的 TCP 客户端套接字到监听集合中
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != P_INVALID_SOCKET) {
                FD_SET(g_relay_clients[i].fd, &read_fds);
#ifndef _WIN32
                if ((int)g_relay_clients[i].fd > max_fd) max_fd = (int)g_relay_clients[i].fd;
#endif
            }
        }

        // 等待套接口数据（超时1秒，用于周期性清理）
        struct timeval tv = {1, 0};
        int sel_ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (sel_ret < 0) {
            if (P_sock_is_interrupted()) continue;  // 被信号打断，继续循环
            perror("select");
            break;
        }

        //-------------------------------

        // 如果存在新的 TCP 连接请求，accept 并将其添加到客户端列表中
        if (FD_ISSET(listen_fd, &read_fds)) {

            struct sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
            sock_t client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            
            int i = 0;
            for (i = 0; i < MAX_PEERS; i++) {

                // 查找一个空闲槽位来存储这个新的连接
                if (!g_relay_clients[i].valid) { g_relay_clients[i].valid = true;
                    g_relay_clients[i].fd = client_fd;
                    g_relay_clients[i].last_active = P_tick_ms();
                    g_relay_clients[i].pending_count = 0;
                    g_relay_clients[i].pending_sender[0] = '\0';
                    strncpy(g_relay_clients[i].name, "unknown", P2P_PEER_ID_MAX);
                    printf(LA_F("[TCP] New connection from %s:%d\n", LA_F47, 47), 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    break;
                }
            }
            if (i == MAX_PEERS) {
                printf("%s", LA_S("[TCP] Max peers reached, rejecting connection\n", LA_S14, 14));
                P_sock_close(client_fd);
            }
        }
        
        // UDP 监听端口收到数据包（COMPACT 模式的信令交互）
        if (FD_ISSET(udp_fd, &read_fds)) {

            uint8_t buf[P2P_MTU]; struct sockaddr_in from; socklen_t from_len = sizeof(from);
            size_t n = recvfrom(udp_fd, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
            if (n > 0) {
                handle_compact_signaling(udp_fd, buf, n, &from);
            }
        }

        // NAT 探测 UDP 收到数据包（也是 COMPACT 模式的信令交互）
        if (probe_fd != P_INVALID_SOCKET && FD_ISSET(probe_fd, &read_fds)) {

            uint8_t buf[64]; struct sockaddr_in from; socklen_t from_len = sizeof(from);
            size_t n = recvfrom(probe_fd, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);

            // NAT_PROBE: [hdr(4)] = 4 bytes
            if (n >= 4 && buf[0] == SIG_PKT_NAT_PROBE) {

                uint16_t req_seq = ((uint16_t)buf[2] << 8) | buf[3];  // 从包头读取 seq

                // 构造应答包（NAT_PROBE_ACK）
                // [hdr(4)][probe_ip(4)][probe_port(2)] = 10 bytes
                buf[0] = SIG_PKT_NAT_PROBE_ACK;
                buf[1] = 0;                                     /* flags */
                buf[2] = (uint8_t)(req_seq >> 8);               /* seq hi (复制请求的 seq) */
                buf[3] = (uint8_t)(req_seq & 0xFF);             /* seq lo */
                memcpy(buf + 4, &from.sin_addr.s_addr, 4);      /* probe_ip   */
                memcpy(buf + 8, &from.sin_port, 2);             /* probe_port */
                sendto(probe_fd, (const char *)buf, 10, 0, (struct sockaddr *)&from, sizeof(from));

                printf(LA_F("[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n", LA_F30, 30),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port),
                       req_seq,
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            }
        }

        // 处理 Relay 模式的信令交互（TCP 连接），包括连接请求、候选交换、在线列表查询等
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid &&
                g_relay_clients[i].fd != P_INVALID_SOCKET &&
                FD_ISSET(g_relay_clients[i].fd, &read_fds)) {
                handle_relay_signaling(i);
            }
        }

    } // while (g_running)

    // 清理资源
    printf("\n%s\n", LA_S("[SERVER] Shutting down...", LA_S12, 12));
    
    // 关闭所有客户端连接
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_relay_clients[i].valid && g_relay_clients[i].fd != P_INVALID_SOCKET) {
            P_sock_close(g_relay_clients[i].fd);
        }
    }
    
    // 关闭监听套接字
    P_sock_close(listen_fd);
    P_sock_close(udp_fd);
    if (probe_fd != P_INVALID_SOCKET) P_sock_close(probe_fd);
    
    P_net_cleanup();

    printf("%s\n", LA_S("[SERVER] Goodbye!", LA_S9, 9));
    return 0;
}
