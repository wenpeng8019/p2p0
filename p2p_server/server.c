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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdbool.h>
#include <inttypes.h>  /* PRIu64 */
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
    time_t                  last_active;                        // 最后活跃时间（用于检测死连接）
    
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
    struct sockaddr_in      addr;                               // 公网地址（UDP 源地址）
    p2p_compact_candidate_t candidates[MAX_CANDIDATES];         // 候选列表
    int                     candidate_count;                    // 候选数量
    struct compact_pair*    peer;                               // 指向配对的对端。(void*)-1 表示对端已断开
    time_t                  last_active;                        // 最后活跃时间
    
    // PEER_INFO(seq=0) 可靠传输（首包 + 地址变更通知）
    int                     info0_acked;                        // 是否已收到首包 ACK，-1 表示未收到确认但已放弃
    uint8_t                 addr_notify_seq;                    // 发给对端的地址变更通知序号（base_index，1..255 循环）
    uint8_t                 pending_base_index;                 // 当前待确认 seq=0 的 base_index（0=首包，!=0 地址变更通知）
    int                     pending_retry;                      // 当前待确认 seq=0 重传次数
    time_t                  pending_sent_time;                  // 当前待确认 seq=0 最近发送时间
    struct compact_pair*    next_pending;                       // 待确认链表指针
    
    // MSG RPC（A→Server→B 可靠中转）
    bool                    msg_pending;                        // 是否有待转发的消息（A 发起的请求等待 B 响应）
    uint16_t                msg_sid;                            // 消息序列号
    uint8_t                 msg;                                // 消息类型
    uint8_t                 msg_data[P2P_MSG_DATA_MAX];         // 消息数据缓冲区
    int                     msg_data_len;                       // 消息数据长度
    time_t                  msg_req_sent_time;                  // MSG_REQ 最后转发给 B 的时间
    int                     msg_req_retry;                      // MSG_REQ 转发给 B 的重传次数
    time_t                  msg_resp_sent_time;                 // MSG_RESP 最后转发给 A 的时间
    int                     msg_resp_retry;                     // MSG_RESP 转发给 A 的重传次数
    struct compact_pair*    next_msg_pending;                   // MSG 待确认链表指针
    
    // uthash handles（支持多种索引方式）
    UT_hash_handle          hh;                                 // 按 session_id 索引（主索引，必须命名为 hh）
    UT_hash_handle          hh_peer;                            // 按 peer_key (local+remote) 索引（辅助索引）
} compact_pair_t;

static relay_client_t       g_relay_clients[MAX_PEERS];
static compact_pair_t       g_compact_pairs[MAX_PEERS];

// uthash 哈希表（支持两种索引方式）
static compact_pair_t*      g_pairs_by_session = NULL;         // 按 session_id 索引
static compact_pair_t*      g_pairs_by_peer = NULL;            // 按 (local_peer_id, remote_peer_id) 索引

// PEER_INFO 待确认链表（仅包含已发送首包但未收到 ACK 的配对）
static compact_pair_t*      g_pending_connecting_head = NULL;
static compact_pair_t*      g_pending_connecting_rear = NULL;

// MSG RPC 待确认链表（包含待转发或已转发但未收到确认的消息）
static compact_pair_t*      g_msg_pending_head = NULL;
static compact_pair_t*      g_msg_pending_rear = NULL;

// 服务器配置（运行时参数）
static int                  g_probe_port = 0;                   // compact 模式 NAT 探测端口（0=不支持探测）
static bool                 g_relay_enabled = false;            // compact 模式是否支持中继功能
static bool                 g_msg_enabled = true;               // compact 模式是否支持 MSG RPC（默认启用）

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t g_running = 1;

///////////////////////////////////////////////////////////////////////////////

// 信号处理函数
#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            printf("\n%s\n", LA_S("[SERVER] Received shutdown signal, exiting gracefully...", LA_S8, 11));
            fflush(stdout);
            g_running = 0;
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n%s\n", LA_S("[SERVER] Received shutdown signal, exiting gracefully...", LA_S8, 11));
        fflush(stdout);
        g_running = 0;
    }
}
#endif

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

// 处理 RELAY 模式信令（TCP 长连接，对应 p2p_signal_relay 模块）
static void handle_relay_signaling(int idx) {

    p2p_relay_hdr_t hdr;
    sock_t fd = g_relay_clients[idx].fd;

    // 更新最后活跃时间（收到任何数据都表示连接活跃）
    g_relay_clients[idx].last_active = time(NULL);

    size_t n = recv(fd, (char *)&hdr, sizeof(hdr), 0);
    if (n <= 0) {
        printf(LA_F("[TCP] V: Peer '%s' disconnected\n", LA_F40, 50), g_relay_clients[idx].name);
        fflush(stdout);

        P_sock_close(fd);
        g_relay_clients[idx].valid = false;
        g_relay_clients[idx].current_peer[0] = '\0';
        return;
    }

    // 调试打印协议包信息
    printf("Received TCP pkt from peer '%s': magic=0x%08X, type=%d, length=%d (expect magic=0x%08X)\n",
           g_relay_clients[idx].name, hdr.magic, hdr.type, hdr.length, P2P_RLY_MAGIC);

    if (hdr.magic != P2P_RLY_MAGIC) {
        printf(LA_F("[TCP] E: Invalid magic from peer '%s'\n", LA_F20, 13), g_relay_clients[idx].name);
        fflush(stdout);

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

        printf(LA_F("[TCP] I: Peer '%s' logged in\n", LA_F25, 51), g_relay_clients[idx].name);
        fflush(stdout);

        // 发送登录确认
        const char* ACK_PROTO = "LOGIN_ACK";

        p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_LOGIN_ACK, 0};
        send(fd, (const char *)&ack, sizeof(ack), 0);

        printf(LA_F("[TCP] V: %s sent to '%s'\n", LA_F39, 280), ACK_PROTO, g_relay_clients[idx].name);
        fflush(stdout);

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

                printf(LA_F("[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n", LA_F26, 46),
                       g_relay_clients[k].pending_count, g_relay_clients[k].pending_sender, login.name);
                fflush(stdout);

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
            
            printf(LA_F("[TCP] Flushing %d pending candidates from '%s' to '%s'...\n", LA_F24, 45), 
                   client->pending_count, sender, client->name);
            fflush(stdout);
            
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
            
            printf(LA_F("[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n", LA_F14, 35),
                   sender, client->pending_count, (int)n);
            fflush(stdout);
            
            // 清空缓存
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(LA_F("[TCP] All pending candidates flushed to '%s'\n", LA_F16, 37), client->name);
            fflush(stdout);
        }
        // 检查是否缓存已满（场景5：发送空 OFFER，让对端反向连接）
        else if (g_relay_clients[idx].pending_count == MAX_CANDIDATES && g_relay_clients[idx].pending_sender[0] != '\0') {
            relay_client_t *client = &g_relay_clients[idx];
            const char *sender = client->pending_sender;
            
            printf(LA_F("[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n", LA_F36, 58),
                   sender, client->name);
            fflush(stdout);
            
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
            
            printf(LA_F("[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n", LA_F15, 36), sender);
            fflush(stdout);
            
            // 清空缓存满标识
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(LA_F("[TCP] Storage full indication flushed to '%s'\n", LA_F34, 56), client->name);
            fflush(stdout);
        }
    }
    // 信令转发：P2P_RLY_CONNECT → P2P_RLY_OFFER，P2P_RLY_ANSWER → P2P_RLY_FORWARD
    else if (hdr.type == P2P_RLY_CONNECT || hdr.type == P2P_RLY_ANSWER) {

        // 接收目标对端名称
        char target_name[P2P_PEER_ID_MAX];
        if (recv(fd, target_name, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
            printf(LA_F("[TCP] Failed to receive target name from %s\n", LA_F22, 43), g_relay_clients[idx].name);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        // 接收信令负载数据
        uint32_t payload_len = hdr.length - P2P_PEER_ID_MAX;
        if (payload_len > 65536) {  // 防止过大的负载
            printf(LA_F("[TCP] Payload too large (%u bytes) from %s\n", LA_F29, 49), payload_len, g_relay_clients[idx].name);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        uint8_t *payload = malloc(payload_len);
        if (recv(fd, (char *)payload, payload_len, 0) != (int)payload_len) {
            printf(LA_F("[TCP] Failed to receive payload from %s\n", LA_F21, 42), g_relay_clients[idx].name);
            free(payload);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }

        printf(LA_F("[TCP] Relaying %s from %s to %s (%u bytes)\n", LA_F30, 52), 
               hdr.type == P2P_RLY_CONNECT ? "CONNECT" : "ANSWER",
               g_relay_clients[idx].name, target_name, payload_len);
        fflush(stdout);

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
                printf(LA_F("[TCP] Sent %s with %d candidates to '%s' (from '%s')\n", LA_F31, 53),
                       is_first_offer ? "OFFER" : "FORWARD", candidates_in_payload, 
                       target_name, g_relay_clients[idx].name);
                fflush(stdout);
                break;
            }
        }
        
        if (!found) {
            /* 目标离线：缓存候选 */
            printf(LA_F("[TCP] Target %s offline, caching candidates...\n", LA_F37, 59), target_name);
            
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
                        g_relay_clients[i].last_active = time(NULL);
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
                        printf(LA_F("[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n", LA_F28, 48),
                               g_relay_clients[idx].name, target->pending_sender, target->pending_count);
                    }
                    target->pending_count = 0;
                    strncpy(target->pending_sender, g_relay_clients[idx].name, P2P_PEER_ID_MAX);
                }
                
                for (int i = 0; i < candidates_in_payload; i++) {
                    if (target->pending_count >= MAX_CANDIDATES) {
                        ack_status = 2;  /* 缓存已满 */
                        printf(LA_F("[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n", LA_F33, 55),
                               target_name, candidates_acked, candidates_in_payload - candidates_acked);
                        
                        // 缓存已满时，pending_sender 本身就表示连接意图（不需要额外字段）
                        // 此时 pending_count 保持为 MAX_CANDIDATES，pending_sender 已经记录了发送者
                        printf(LA_F("[TCP] Storage full, connection intent from '%s' to '%s' noted\n", LA_F35, 57),
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
                        printf(LA_F("[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n", LA_F18, 39),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    } else {
                        ack_status = 1;  /* 已缓存，还有剩余空间 */
                        printf(LA_F("[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n", LA_F17, 38),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    }
                    fflush(stdout);
                }
            } else {
                ack_status = 2;  /* 无法分配槽位 */
                candidates_acked = 0;
                printf(LA_F("[TCP] Cannot allocate slot for offline user '%s'\n", LA_F19, 40), target_name);
                fflush(stdout);
            }
        }

        /* 仅对 P2P_RLY_CONNECT 发送 ACK（P2P_RLY_ANSWER 不需要确认） */
        if (hdr.type == P2P_RLY_CONNECT) {
            p2p_relay_hdr_t ack_hdr = {P2P_RLY_MAGIC, P2P_RLY_CONNECT_ACK, sizeof(p2p_relay_connect_ack_t)};
            p2p_relay_connect_ack_t ack_payload = {ack_status, candidates_acked, {0, 0}};
            size_t sent1 = send(fd, (const char *)&ack_hdr, sizeof(ack_hdr), 0);
            size_t sent2 = send(fd, (const char *)&ack_payload, sizeof(ack_payload), 0);
            if (sent1 != sizeof(ack_hdr) || sent2 != sizeof(ack_payload)) {
                printf(LA_F("[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n", LA_F23, 44),
                       g_relay_clients[idx].name, (int)sent1, (int)sent2);
            } else {
                printf(LA_F("[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n", LA_F32, 54), 
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
                    printf("%s", LA_S("[TCP] User list truncated (too many users)\n", LA_S11, 15));
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
        printf(LA_F("[TCP] Unknown message type %d from %s\n", LA_F38, 60), hdr.type, g_relay_clients[idx].name);
    }
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_relay_clients[i].valid || 
            (now - g_relay_clients[i].last_active) <= RELAY_CLIENT_TIMEOUT) continue;

        printf(LA_F("[TCP] W: Client '%s' timeout (inactive for %ld seconds)\n", LA_F41, 41), 
               g_relay_clients[i].name, (long)(now - g_relay_clients[i].last_active));
        fflush(stdout);
        
        P_sock_close(g_relay_clients[i].fd);
        g_relay_clients[i].fd = P_INVALID_SOCKET;
        g_relay_clients[i].current_peer[0] = '\0';
        g_relay_clients[i].valid = false;
    }
}

//-----------------------------------------------------------------------------

// 从待确认链表移除
static void remove_compact_pending(compact_pair_t *pair) {

    if (!g_pending_connecting_head || !pair->next_pending) return;

    // 如果是头节点
    if (g_pending_connecting_head == pair) {
        g_pending_connecting_head = pair->next_pending;
        pair->next_pending = NULL;
        if (g_pending_connecting_head == (void*)-1) {
            g_pending_connecting_head = NULL;
            g_pending_connecting_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_pending_connecting_head;
    while (prev->next_pending != pair) {
        if (prev->next_pending == (void*)-1) return;  // 没有找到
        prev = prev->next_pending;
    }

    prev->next_pending = pair->next_pending;
    
    // 如果移除的是尾节点，更新尾指针
    if (pair->next_pending == (void*)-1) {
        g_pending_connecting_rear = prev;
    }
    
    pair->next_pending = NULL;
}

// 发送 PEER_INFO(seq=0)（base_index=0 首包；base_index!=0 地址变更通知）
static void send_peer_info_seq0(sock_t udp_fd, compact_pair_t *pair, uint8_t base_index) {
    const char* PROTO = "PEER_INFO";

    assert(pair && pair->peer && pair->peer != (compact_pair_t*)(void*)-1);

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
    fflush(stdout);
}

static void enqueue_compact_pending(compact_pair_t *pair, uint8_t base_index, time_t now) {

    if (pair->next_pending) {
        remove_compact_pending(pair);
    }

    pair->pending_base_index = base_index;
    pair->pending_retry = 0;
    pair->pending_sent_time = now;

    pair->next_pending = (compact_pair_t*)(void*)-1;
    if (g_pending_connecting_rear) {
        g_pending_connecting_rear->next_pending = pair;
        g_pending_connecting_rear = pair;
    } else {
        g_pending_connecting_head = pair;
        g_pending_connecting_rear = pair;
    }
}

// 检查并重传未确认的 PEER_INFO 包
static void retry_compact_pending(sock_t udp_fd, time_t now) {

    if (!g_pending_connecting_head) return;

    for(;;) {

        // 队列按时间排序，一旦遇到未超时的节点，后面都不会超时
        if (now - g_pending_connecting_head->pending_sent_time < PEER_INFO0_RETRY_INTERVAL) {
            return;
        }

        // 将第一项移除
        compact_pair_t *q = g_pending_connecting_head;
        g_pending_connecting_head = q->next_pending;

        // 检查是否超过最大重传次数
        if (q->pending_retry >= PEER_INFO0_MAX_RETRY) {
            
            // 超过最大重传次数，从链表移除（放弃）
            printf(LA_F("[UDP] W: PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n", LA_F81, 66),
                   q->local_peer_id, q->remote_peer_id, q->pending_retry);
            fflush(stdout);

            q->next_pending = NULL;
            if (q->pending_base_index == 0) {
                q->info0_acked = -1;  // 首包失败：标记为已放弃
            }

            // 如果这是最后一项
            if (g_pending_connecting_head == (void*)-1) {
                g_pending_connecting_head = NULL;
                g_pending_connecting_rear = NULL;
                return;
            }
        }
        // 重传
        else { assert(q->peer && q->peer != (compact_pair_t*)(void*)-1);

            send_peer_info_seq0(udp_fd, q, q->pending_base_index);

            // 更新时间和重传次数
            q->pending_retry++;
            q->pending_sent_time = now;

            // 如果这是最后一项
            if (g_pending_connecting_head == (void*)-1) {
                g_pending_connecting_head = q;
            }
            // 重新加到队尾（因为时间更新了，按时间排序）
            else {
                q->next_pending = (compact_pair_t*)(void*)-1;
                g_pending_connecting_rear->next_pending = q;
                g_pending_connecting_rear = q;
            }

            printf(LA_F("[UDP] V: PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%" PRIu64 ")\n", 0),
                   q->local_peer_id, q->remote_peer_id,
                   q->pending_retry, PEER_INFO0_MAX_RETRY, q->session_id);
            fflush(stdout);

            if (g_pending_connecting_head == q) return;
        }
    }
}

//-----------------------------------------------------------------------------
// MSG RPC 链表管理和处理函数

// 从 MSG 待确认链表移除
static void remove_msg_pending(compact_pair_t *pair) {

    if (!g_msg_pending_head || !pair->next_msg_pending) return;

    // 如果是头节点
    if (g_msg_pending_head == pair) {
        g_msg_pending_head = pair->next_msg_pending;
        pair->next_msg_pending = NULL;
        if (g_msg_pending_head == (void*)-1) {
            g_msg_pending_head = NULL;
            g_msg_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_msg_pending_head;
    while (prev->next_msg_pending != pair) {
        if (prev->next_msg_pending == (void*)-1) return;  // 没有找到
        prev = prev->next_msg_pending;
    }

    prev->next_msg_pending = pair->next_msg_pending;
    
    // 如果移除的是尾节点，更新尾指针
    if (pair->next_msg_pending == (void*)-1) {
        g_msg_pending_rear = prev;
    }
    
    pair->next_msg_pending = NULL;
}

// 将配对加入 MSG 待确认链表
static void enqueue_msg_pending(compact_pair_t *pair, time_t now) {

    if (pair->next_msg_pending) {
        remove_msg_pending(pair);
    }

    pair->msg_req_sent_time = now;
    pair->msg_req_retry = 0;
    pair->msg_resp_sent_time = 0;
    pair->msg_resp_retry = 0;

    pair->next_msg_pending = (compact_pair_t*)(void*)-1;
    if (g_msg_pending_rear) {
        g_msg_pending_rear->next_msg_pending = pair;
        g_msg_pending_rear = pair;
    } else {
        g_msg_pending_head = pair;
        g_msg_pending_rear = pair;
    }
}

// 发送 MSG_REQ 给 B 端（Server→B relay）
// 协议格式: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=SIG_MSG_FLAG_RELAY
// msg=消息类型，msg=0 时 B 端自动 echo
static void send_msg_req_to_peer(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_REQ";

    assert(pair && pair->peer && pair->peer != (compact_pair_t*)(void*)-1);
    assert(pair->msg_pending);

    uint8_t pkt[4 + 8 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_REQ;
    hdr->flags = SIG_MSG_FLAG_RELAY;  // 标识这是 Server→B 的 relay 包
    hdr->seq = 0;

    int n = 4;
    nwrite_ll(pkt + n, pair->peer->session_id); n += 8;  // B 端的 session_id
    nwrite_s(pkt + n, pair->msg_sid); n += 2;
    pkt[n++] = pair->msg;
    if (pair->msg_data_len > 0) {
        memcpy(pkt + n, pair->msg_data, pair->msg_data_len);
        n += pair->msg_data_len;
    }

    // 调试打印协议包信息
    printf("Send %s pkt to %s:%d, seq=0, flags=0x%02x, len=%d, sid=%u, msg=%u, session_id=%" PRIu64 "\n",
           PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port),
           hdr->flags, n, pair->msg_sid, pair->msg, pair->peer->session_id);

    sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

    printf("[UDP] V: %s sent to '%s' (relay to B), sid=%u, msg=%u, retries=%d (ses_id=%" PRIu64 ")\n",
           PROTO, pair->peer->local_peer_id, pair->msg_sid, pair->msg, pair->msg_req_retry, pair->peer->session_id);
    fflush(stdout);
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
    fflush(stdout);
}

// 发送 MSG_RESP 给 A 端（Server→A）
// 协议格式: [sid(2)][code(1)][data(N)]（正常响应，code=响应码）
//          [sid(2)]（错误响应，flags 中标识错误类型）
// flags: 0=正常, SIG_MSG_FLAG_PEER_OFFLINE=对端离线, SIG_MSG_FLAG_TIMEOUT=转发超时
static void send_msg_resp_to_requester(sock_t udp_fd, compact_pair_t *pair, uint8_t flags,
                                       uint8_t msg, const uint8_t *data, int len) {
    const char* PROTO = "MSG_RESP";

    assert(pair && pair->msg_pending);

    uint8_t pkt[4 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP;
    hdr->flags = flags;
    hdr->seq = 0;

    int n = 4;
    nwrite_s(pkt + n, pair->msg_sid); n += 2;
    
    // 如果是正常响应，包含 msg 和 data
    if (!(flags & (SIG_MSG_FLAG_PEER_OFFLINE | SIG_MSG_FLAG_TIMEOUT))) {
        pkt[n++] = msg;
        if (len > 0 && data) {
            memcpy(pkt + n, data, len);
            n += len;
        }
    }

    // 调试打印协议包信息
    printf("Send %s pkt to %s:%d, seq=0, flags=0x%02x, len=%d, sid=%u\n",
           PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port),
           flags, n, pair->msg_sid);

    sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));

    printf("[UDP] V: %s sent to '%s', sid=%u, flags=0x%02x, retries=%d (ses_id=%" PRIu64 ")\n",
           PROTO, pair->local_peer_id, pair->msg_sid, flags, pair->msg_resp_retry, pair->session_id);
    fflush(stdout);
}

// 检查并重传 MSG 包
static void retry_msg_pending(sock_t udp_fd, time_t now) {

    if (!g_msg_pending_head) return;

    compact_pair_t *curr = g_msg_pending_head;
    while (curr != (compact_pair_t*)(void*)-1) {
        compact_pair_t *next = curr->next_msg_pending;

        // 检查 MSG_REQ 重传（等待 B 端响应）
        if (curr->msg_resp_sent_time == 0 && curr->msg_req_sent_time > 0) {
            
            if (now - curr->msg_req_sent_time >= MSG_REQ_RETRY_INTERVAL) {
                
                // 检查对端是否离线
                if (!curr->peer || curr->peer == (compact_pair_t*)(void*)-1) {
                    printf("[UDP] W: MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%" PRIu64 ")\n",
                           curr->local_peer_id, curr->msg_sid, curr->session_id);
                    fflush(stdout);
                    
                    // 发送 PEER_OFFLINE 错误给 A
                    send_msg_resp_to_requester(udp_fd, curr, SIG_MSG_FLAG_PEER_OFFLINE, 0, NULL, 0);
                    curr->msg_resp_sent_time = now;
                    curr->msg_resp_retry = 0;
                }
                // 超过最大重传次数，发送超时错误
                else if (curr->msg_req_retry >= MSG_REQ_MAX_RETRY) {
                    printf("[UDP] W: MSG_REQ timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%" PRIu64 ")\n",
                           curr->msg_req_retry, curr->local_peer_id, curr->msg_sid, curr->session_id);
                    fflush(stdout);
                    
                    // 发送 TIMEOUT 错误给 A
                    send_msg_resp_to_requester(udp_fd, curr, SIG_MSG_FLAG_TIMEOUT, 0, NULL, 0);
                    curr->msg_resp_sent_time = now;
                    curr->msg_resp_retry = 0;
                }
                // 重传 MSG_REQ 给 B
                else {
                    send_msg_req_to_peer(udp_fd, curr);
                    curr->msg_req_sent_time = now;
                    curr->msg_req_retry++;
                }
            }
        }
        // 检查 MSG_RESP 重传（等待 A 端确认）
        else if (curr->msg_resp_sent_time > 0) {
            
            if (now - curr->msg_resp_sent_time >= MSG_RESP_RETRY_INTERVAL) {
                
                // 超过最大重传次数，放弃
                if (curr->msg_resp_retry >= MSG_RESP_MAX_RETRY) {
                    printf("[UDP] W: MSG_RESP gave up after %d retries, sid=%u (ses_id=%" PRIu64 ")\n",
                           curr->msg_resp_retry, curr->msg_sid, curr->session_id);
                    fflush(stdout);
                    
                    // 清理状态
                    remove_msg_pending(curr);
                    curr->msg_pending = false;
                    curr->msg_sid = 0;
                    curr->msg_data_len = 0;
                }
                // MSG_RESP 需要缓存 B 的响应才能重传，这里简化处理为直接放弃
                // 实际生产环境应该缓存响应数据以支持重传
                else {
                    // 简化：MSG_RESP 不支持重传，直接放弃
                    remove_msg_pending(curr);
                    curr->msg_pending = false;
                    curr->msg_sid = 0;
                    curr->msg_data_len = 0;
                }
            }
        }

        curr = next;
    }
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
    if (hdr->type == SIG_PKT_REGISTER) {
        const char* PROTO = "REGISTER";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len <= P2P_PEER_ID_MAX * 2 + 4) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F44, 64), PROTO, payload_len);
            fflush(stdout);
            return;
        }

        // 解析 local_peer_id 和 remote_peer_id
        char local_peer_id[P2P_PEER_ID_MAX + 1] = {0}, remote_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        memcpy(local_peer_id, payload, P2P_PEER_ID_MAX); local_peer_id[P2P_PEER_ID_MAX] = '\0';
        memcpy(remote_peer_id, payload + P2P_PEER_ID_MAX, P2P_PEER_ID_MAX); remote_peer_id[P2P_PEER_ID_MAX] = '\0';

        // 解析instance_id（4 字节大端序）
        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX * 2);
        if (instance_id == 0) {
            printf(LA_F("[UDP] E: %s: invalid instance_id=0 from %s\n", LA_F46, 64), PROTO, from_str);
            fflush(stdout);
            return;
        }

        // 解析候选列表（新格式）
        int candidate_count = 0;
        p2p_compact_candidate_t candidates[MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));

        // 读取候选数量（1 字节，紧跟在 instance_id 之后）
        size_t cand_offset = P2P_PEER_ID_MAX * 2 + 4;
        candidate_count = payload[cand_offset];
        if (candidate_count > MAX_CANDIDATES) {
            candidate_count = MAX_CANDIDATES;
        }
        cand_offset++;

        // 解析候选列表 (7 字节: type + ip + port)
        for (int i = 0; i < candidate_count && cand_offset + sizeof(p2p_compact_candidate_t) <= payload_len; i++) {
            candidates[i].type = payload[cand_offset];
            memcpy(&candidates[i].ip, payload + cand_offset + 1, sizeof(uint32_t));
            memcpy(&candidates[i].port, payload + cand_offset + 5, sizeof(uint16_t));
            cand_offset += sizeof(p2p_compact_candidate_t);
        }

        printf(LA_F("[UDP] V: %s: accepted, local='%s', remote='%s', inst_id=%u, cands=%d\n", LA_F65, 73),
               PROTO, local_peer_id, remote_peer_id, instance_id, candidate_count);
        fflush(stdout);
        
        // 查找本端槽位：直接用 hash 查找（O(1)），payload 前 64 字节是 [local_peer_id(32)][remote_peer_id(32)]
        compact_pair_t *existing = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, existing);
        int local_idx = existing ? (int)(existing - g_compact_pairs) : -1;  
        
        // 如果配对不存在，分配一个空位
        if (local_idx == -1) {
            for (int i = 0; i < MAX_PEERS; i++) {
                if (!g_compact_pairs[i].valid) { 
                    g_compact_pairs[i].valid = true;
                    local_idx = i;
                    
                    // 注意：session_id 在首次匹配成功时才分配，此时为 0
                    g_compact_pairs[i].instance_id = instance_id;
                    g_compact_pairs[i].session_id = 0;
                    
                    strncpy(g_compact_pairs[i].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
                    strncpy(g_compact_pairs[i].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
                    g_compact_pairs[i].peer = NULL;
                    g_compact_pairs[i].info0_acked = false;
                    g_compact_pairs[i].addr_notify_seq = 0;
                    g_compact_pairs[i].pending_base_index = 0;
                    g_compact_pairs[i].pending_sent_time = 0;
                    g_compact_pairs[i].pending_retry = 0;
                    g_compact_pairs[i].next_pending = NULL;
                    
                    // MSG RPC 初始化
                    g_compact_pairs[i].msg_pending = false;
                    g_compact_pairs[i].msg_sid = 0;
                    g_compact_pairs[i].msg_data_len = 0;
                    g_compact_pairs[i].next_msg_pending = NULL;
                    
                    // 添加到 peer_key 索引（session_id 索引在首次匹配时添加）
                    HASH_ADD(hh_peer, g_pairs_by_peer, local_peer_id, P2P_PEER_ID_MAX * 2, &g_compact_pairs[i]);
                    break;
                }
            }
        }
        // 无法分配槽位，发送错误 ACK
        if (local_idx < 0) {
            const char* ACK_PROTO = "REGISTER_ACK";

            uint8_t ack_response[14];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = SIG_PKT_REGISTER_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            ack_response[4] = 2;                /* status = error */
            memset(ack_response + 5, 0, 9);     /* 其余字段全部置 0 */

            // 调试打印协议包信息
            printf("Send %s pkt to %s, seq=0, flags=0, status=2(error), len=14\n",
                   ACK_PROTO, from_str);

            sendto(udp_fd, (const char *)ack_response, 14, 0, (struct sockaddr *)from, sizeof(*from));

            printf(LA_F("[UDP] E: %s sent, status=error (no slot available)\n", LA_F42, 74), ACK_PROTO);
            fflush(stdout);
            return;
        }

        compact_pair_t *local = &g_compact_pairs[local_idx];

        // 检测 instance_id：不同则客户端重启，必须重置旧会话
        if (local->instance_id != 0 && local->instance_id != instance_id) {
            printf(LA_F("[UDP] I: %s from '%s': new instance(old=%u new=%u), resetting session\n", LA_F52, 75),
                   PROTO, local_peer_id, local->instance_id, instance_id);
            fflush(stdout);

            // 通知对端下线（如果对端在线且有 session_id）
            if (local->peer != NULL && local->peer != (compact_pair_t*)(void*)-1
                    && local->peer->session_id != 0) {
                const char* OFF_PROTO = "PEER_OFF";

                uint8_t notify[4 + 8];
                p2p_packet_hdr_t *nh = (p2p_packet_hdr_t *)notify;
                nh->type = SIG_PKT_PEER_OFF; nh->flags = 0; nh->seq = htons(0);
                nwrite_ll(notify + 4, local->peer->session_id);

                // 调试打印协议包信息
                printf("Send %s pkt to %s:%d, seq=0, flags=0, len=12, session_id=%" PRIu64 "\n",
                       OFF_PROTO, inet_ntoa(local->peer->addr.sin_addr), ntohs(local->peer->addr.sin_port),
                       local->peer->session_id);

                sendto(udp_fd, (const char *)notify, sizeof(notify), 0,
                       (struct sockaddr *)&local->peer->addr, sizeof(local->peer->addr));

                printf(LA_F("[UDP] V: %s sent to '%s' (ses_id=%" PRIu64 ") [reregister]\n", 0),
                       OFF_PROTO, local->peer->local_peer_id, local->peer->session_id);
                fflush(stdout);

                local->peer->peer = (compact_pair_t*)(void*)-1;
            }

            // 从待确认链表移除
            if (local->next_pending) remove_compact_pending(local);

            // 从 session_id 索引移除
            if (local->session_id != 0) {
                HASH_DELETE(hh, g_pairs_by_session, local);
            }

            // 重置状态（保留 valid=true 和 hh_peer 不变）
            local->instance_id = instance_id;
            local->session_id = 0;
            local->peer = NULL;
            local->info0_acked = false;
            local->addr_notify_seq = 0;
            local->pending_base_index = 0;
            local->pending_retry = 0;
            local->pending_sent_time = 0;
        }
        local->instance_id = instance_id;

        // 检测地址是否变化，并记录最新地址
        bool addr_changed = memcmp(&local->addr, from, sizeof(*from)) != 0;
        local->addr = *from;

        // 记录本端的候选列表
        local->candidate_count = candidate_count;
        if (candidate_count) {
            memcpy(local->candidates, candidates, sizeof(p2p_compact_candidate_t) * candidate_count);
        }

        // 查找反向配对：构造反向 peer_key [remote_peer_id][local_peer_id]
        char reverse_key[P2P_PEER_ID_MAX * 2];
        memcpy(reverse_key, remote_peer_id, P2P_PEER_ID_MAX);
        memcpy(reverse_key + P2P_PEER_ID_MAX, local_peer_id, P2P_PEER_ID_MAX);
        compact_pair_t* remote = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, reverse_key, P2P_PEER_ID_MAX * 2, remote);
        int remote_idx = remote ? (int)(remote - g_compact_pairs) : -1;

        // 如果之前是（自己之前是已连接过的）对端断开连接，重新连接时重置对端状态
        if (local->peer == (compact_pair_t*)(void*)-1) {
            local->peer = NULL;
        }

        local->last_active = time(NULL);

        // 构造并发送 REGISTER_ACK
        // 格式: [hdr(4)][status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 14字节
        {
            const char* ACK_PROTO = "REGISTER_ACK";

            uint8_t ack_response[14];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = SIG_PKT_REGISTER_ACK;
            ack_hdr->flags = 0;
            if (g_relay_enabled) ack_hdr->flags |= SIG_REGACK_FLAG_RELAY;
            if (g_msg_enabled) ack_hdr->flags |= SIG_REGACK_FLAG_MSG;
            ack_hdr->seq = 0;
            
            /* status: 0=成功/对端离线, 1=成功/对端在线 */
            ack_response[4] = (remote_idx >= 0) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE;
            /* max_candidates */
            ack_response[5] = MAX_CANDIDATES;    /* 服务器缓存能力（0=不支持） */
            
            /* 填入客户端的公网地址（服务器主端口观察到的 UDP 源地址）*/
            memcpy(ack_response + 6, &from->sin_addr.s_addr, 4);    /* public_ip */
            memcpy(ack_response + 10, &from->sin_port, 2);          /* public_port */

            /* NAT 探测端口（根据配置填入，0=不支持探测）*/
            uint16_t probe_port_net = htons(g_probe_port);
            memcpy(ack_response + 12, &probe_port_net, 2);          /* probe_port */

            // 调试打印协议包信息
            printf("Send %s pkt to %s, seq=0, flags=0x%02x, status=%u, len=14\n",
                   ACK_PROTO, from_str, ack_hdr->flags, ack_response[4]);
            
            sendto(udp_fd, (const char *)ack_response, 14, 0, (struct sockaddr *)from, sizeof(*from));

            // register ack 无需提供确认重发机制，因为客户端会在收到 ACK 前一直重试注册请求
            printf(LA_F("[UDP] V: %s sent, status=%s, max_cands=%d, relay=%s, public=%s:%d, probe=%d\n", LA_F63, 75),
                   ACK_PROTO,
                   (remote_idx >= 0) ? "peer_online" : "peer_offline",
                   MAX_CANDIDATES,
                   g_relay_enabled ? "yes" : "no",
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port), g_probe_port);
            fflush(stdout);
        }

        if (remote_idx >= 0) {

            compact_pair_t *remote = &g_compact_pairs[remote_idx];

            // 首次匹配成功
            if (local->peer == NULL || remote->peer == NULL) {

                // 建立双向关联
                local->peer = remote; remote->peer = local;

                // 为双方生成 session_id 并添加到 session_id 索引（peer_key 索引已在 REGISTER 时添加）
                if (local->session_id == 0) {
                    local->session_id = generate_session_id();
                    HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), local);
                    printf(LA_F("[UDP] I: Assigned session_id=%" PRIu64 " for '%s' -> '%s'\n", 0),
                           local->session_id, local->local_peer_id, local->remote_peer_id);
                    fflush(stdout);
                }
                if (remote->session_id == 0) {
                    remote->session_id = generate_session_id();
                    HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), remote);
                    printf(LA_F("[UDP] I: Assigned session_id=%" PRIu64 " for '%s' -> '%s'\n", 0),
                           remote->session_id, remote->local_peer_id, remote->remote_peer_id);
                    fflush(stdout);
                }

                local->info0_acked = false;
                local->addr_notify_seq = 0;
                local->pending_base_index = 0;
                local->pending_retry = 0;
                local->pending_sent_time = 0;
                remote->info0_acked = false;
                remote->addr_notify_seq = 0;
                remote->pending_base_index = 0;
                remote->pending_retry = 0;
                remote->pending_sent_time = 0;

                // 向双方发送服务器维护的首个 PEER_INFO(seq=0, base_index=0)
                send_peer_info_seq0(udp_fd, local, 0);
                enqueue_compact_pending(local, 0, local->last_active);

                send_peer_info_seq0(udp_fd, remote, 0);
                enqueue_compact_pending(remote, 0, local->last_active);

                printf(LA_F("[UDP] I: Pairing complete: '%s'(%d cands) <-> '%s'(%d cands)\n", LA_F55, 68),
                       local_peer_id, remote->candidate_count,
                       remote_peer_id, local->candidate_count);
                fflush(stdout);
            }
            else { assert(local->peer == remote && remote->peer == local);

                // 如果公网地址发生变化，通知对端最新地址（seq=0, base_index!=0）
                if (addr_changed) {

                    // 只有对方确认收到服务器发送的首个 PEER_INFO 包后，才通知对方地址变化
                    // + 首个 PEER_INFO 包（可能）含有候选地址信息，所以必须确保对方收到
                    if (remote->info0_acked) {

                        remote->addr_notify_seq = (uint8_t)(remote->addr_notify_seq + 1);
                        if (remote->addr_notify_seq == 0) remote->addr_notify_seq = 1;
                        send_peer_info_seq0(udp_fd, remote, remote->addr_notify_seq);
                        enqueue_compact_pending(remote, remote->addr_notify_seq, local->last_active);

                        printf(LA_F("[UDP] I: Address changed for '%s', notifying '%s' (ses_id=%" PRIu64 ")\n", LA_F67, 84),
                               local_peer_id, remote_peer_id, remote->session_id);
                        fflush(stdout);
                    }
                }
            }
        } else {
            printf(LA_F("[UDP] V: %s: waiting for peer '%s' to register\n", LA_F69, 85), PROTO, remote_peer_id);
            fflush(stdout);
        }
    }

    // SIG_PKT_UNREGISTER: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端主动断开时发送，请求服务器立即释放配对槽位。
    // 【服务端可选实现】如果不处理此包类型，客户端自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制。
    else if (hdr->type == SIG_PKT_UNREGISTER) {
        const char* PROTO = "UNREGISTER";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < P2P_PEER_ID_MAX * 2) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F44, 64), PROTO, payload_len);
            fflush(stdout);
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

            printf(LA_F("[UDP] V: %s: accepted, releasing slot for '%s' -> '%s'\n", LA_F66, 86),
                   PROTO, local_peer_id, remote_peer_id);
            fflush(stdout);

            // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
            if (pair->peer != NULL && pair->peer != (compact_pair_t*)(void*)-1 && pair->peer->session_id != 0) {
                const char* OFF_PROTO = "PEER_OFF";

                uint8_t notify[4 + P2P_PEER_ID_MAX];  // 包头 + session_id
                p2p_packet_hdr_t *notify_hdr = (p2p_packet_hdr_t *)notify;
                notify_hdr->type = SIG_PKT_PEER_OFF;
                notify_hdr->flags = 0;
                notify_hdr->seq = htons(0);
                
                nwrite_ll(notify + 4, pair->peer->session_id);

                // 调试打印协议包信息
                printf("Send %s pkt to %s:%d, seq=0, flags=0, len=12, session_id=%" PRIu64 "\n",
                       OFF_PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port),
                       pair->peer->session_id);
                
                sendto(udp_fd, (const char *)notify, 4 + P2P_PEER_ID_MAX, 0, (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                printf(LA_F("[UDP] V: %s sent to '%s' (ses_id=%" PRIu64 ") [unregister]\n", 0),
                       OFF_PROTO, pair->peer->local_peer_id, pair->peer->session_id);
                fflush(stdout);
                
                // 标记对端槽位已断开
                pair->peer->peer = (compact_pair_t*)(void*)-1;
            }
            
            // 从待确认链表移除
            if (pair->next_pending) {
                remove_compact_pending(pair);
            }

            // 从 MSG 链表移除
            if (pair->next_msg_pending) {
                remove_msg_pending(pair);
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
            pair->pending_base_index = 0;
            pair->pending_retry = 0;
            pair->pending_sent_time = 0;
            pair->msg_pending = false;
            pair->msg_sid = 0;
            pair->msg_data_len = 0;
        }
    }
    // SIG_PKT_ALIVE: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端定期发送以保持槽位活跃，更新 last_active 时间
    else if (hdr->type == SIG_PKT_ALIVE) {
        const char* PROTO = "ALIVE";

        if (payload_len < P2P_PEER_ID_MAX * 2) return;

        // 直接用 hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, pair);
        
        if (pair && pair->valid) {
            pair->last_active = time(NULL);

            // 发送 ALIVE_ACK（仅包头，无 payload）
            const char* ACK_PROTO = "ALIVE_ACK";

            uint8_t ack[4];
            p2p_pkt_hdr_encode(ack, SIG_PKT_ALIVE_ACK, 0, 0);

            // 调试打印协议包信息
            printf("Send %s pkt to %s, seq=0, flags=0, len=4\n", ACK_PROTO, from_str);

            sendto(udp_fd, (const char *)ack, 4, 0, (struct sockaddr *)from, sizeof(*from));

            printf(LA_F("[UDP] V: %s received and %s sent for '%s'\n", LA_F59, 290), PROTO, ACK_PROTO, pair->local_peer_id);
            fflush(stdout);
        }
    }

    // SIG_PKT_PEER_INFO_ACK: ACK 确认收到 PEER_INFO 包
    // 格式: [hdr(4)][session_id(8)]，确认序号使用 hdr->seq
    else if (hdr->type == SIG_PKT_PEER_INFO_ACK) {
        const char* PROTO = "PEER_INFO_ACK";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < P2P_PEER_ID_MAX) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F44, 64), PROTO, payload_len);
            fflush(stdout);
            return;
        }

        uint64_t session_id = nget_ll(payload);
        uint16_t ack_seq = ntohs(hdr->seq);
        if (ack_seq > 16) {
            printf(LA_F("[UDP] E: %s: invalid seq=%u\n", LA_F48, 63), PROTO, ack_seq);
            fflush(stdout);
            return;
        }

        // ack_seq=0 的 ACK 是对服务器发送的首个 PEER_INFO 的确认，服务器需要处理
        if (ack_seq == 0) {
            // 通过 session_id 查找对应的配对记录
            compact_pair_t *pair = NULL;
            HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
            if (pair && pair->valid) {
                // 标记为已确认，从待确认链表移除
                if (!pair->info0_acked && pair->pending_base_index == 0) {
                    pair->info0_acked = true;

                    printf(LA_F("[UDP] V: %s: confirmed '%s', retries=%d (ses_id=%" PRIu64 ")\n", 0),
                           PROTO, pair->local_peer_id, pair->pending_retry, session_id);
                }

                // 从待确认链表移除（首包和地址变更通知都走 seq=0 ACK）
                if (pair->next_pending) {
                    remove_compact_pending(pair);
                }

                pair->pending_base_index = 0;
                pair->pending_retry = 0;
                pair->pending_sent_time = 0;
            }
            else printf(LA_F("[UDP] W: %s for unknown ses_id=%" PRIu64 "\n", 0), PROTO, session_id);
            fflush(stdout);
        }
        // ack_seq≠0 的 ACK 是客户端之间的确认，服务器只负责 relay 转发
        else {
            compact_pair_t *pair = NULL;
            HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
            if (pair && pair->valid && pair->peer && pair->peer != (compact_pair_t*)(void*)-1) {
                // 转发给对方
                sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                       (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                printf(LA_F("[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                       PROTO, ack_seq, pair->local_peer_id, pair->remote_peer_id, session_id);
            }
            else printf(LA_F("[UDP] W: Cannot relay %s: ses_id=%" PRIu64 " (peer unavailable)\n", 0), PROTO, session_id);
            fflush(stdout);
        }
    }
    // SIG_PKT_PEER_INFO/P2P_PKT_RELAY_DATA/P2P_PKT_RELAY_ACK: relay 转发给对方
    // 格式：所有包都包含 session_id(4) 在 payload 开头
    else if (hdr->type == SIG_PKT_PEER_INFO ||
             hdr->type == P2P_PKT_RELAY_DATA || hdr->type == P2P_PKT_RELAY_ACK) {

        const char* PROTO = (hdr->type == SIG_PKT_PEER_INFO) ? "PEER_INFO" :
                           (hdr->type == P2P_PKT_RELAY_DATA) ? "RELAY_DATA" : "RELAY_ACK";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        // SIG_PKT_PEER_INFO 特殊处理：seq=0 是服务器维护的包，不应该出现在这里
        if (hdr->type == SIG_PKT_PEER_INFO && hdr->seq == 0) {
            printf(LA_F("[UDP] E: %s seq=0 from client %s (server-only, dropped)\n", LA_F43, 67), PROTO, from_str);
            fflush(stdout);
            return;
        }

        // 所有需要 relay 的包格式都是 [session_id(8)][...]
        if (payload_len < P2P_PEER_ID_MAX) {
            printf(LA_F("[UDP] E: Relay %s: bad payload(len=%zu)\n", LA_F49, 82), PROTO, payload_len);
            fflush(stdout);
            return;
        }

        uint64_t session_id = nget_ll(payload);

        // 根据 session_id 查找配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
        if (!pair || !pair->valid) {
            printf(LA_F("[UDP] W: Relay %s for unknown ses_id=%" PRIu64 " (dropped)\n", 0), PROTO, session_id);
            fflush(stdout);
            return;
        }

        // 对方不存在，丢弃
        if (!pair->peer || pair->peer == (compact_pair_t*)(void*)-1) {
            printf(LA_F("[UDP] W: Relay %s for ses_id=%" PRIu64 ": peer unavailable (dropped)\n", 0), PROTO, session_id);
            fflush(stdout);
            return;
        }

        // 转发给对方
        sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
               (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

        if (hdr->type == SIG_PKT_PEER_INFO) {
            printf(LA_F("[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else if (hdr->type == P2P_PKT_RELAY_DATA) {
            printf(LA_F("[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else {
            printf(LA_F("[UDP] V: Relay %s: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, pair->local_peer_id, pair->remote_peer_id, session_id);
        }
        fflush(stdout);
    }
    // SIG_PKT_MSG_REQ: A→Server 或 B→Server（通过 flags 区分）
    // A→Server: [target_peer_id(32)][sid(2)][msg(1)][data(N)]，flags=0，msg=消息类型
    // B→Server（不会有这种情况，B 只发 MSG_RESP）
    else if (hdr->type == SIG_PKT_MSG_REQ) {
        const char* PROTO = "MSG_REQ";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        // MSG_REQ 只能是 A→Server（flags=0）
        if (hdr->flags & SIG_MSG_FLAG_RELAY) {
            printf(LA_F("[UDP] E: %s: invalid relay flag from client\n", LA_F47, 282), PROTO);
            fflush(stdout);
            return;
        }

        if (payload_len < P2P_PEER_ID_MAX + 3) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F44, 64), PROTO, payload_len);
            fflush(stdout);
            return;
        }

        // 解析 target_peer_id、sid、msg、data
        char target_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        memcpy(target_peer_id, payload, P2P_PEER_ID_MAX);
        uint16_t sid = nget_s(payload + P2P_PEER_ID_MAX);
        uint8_t msg = payload[P2P_PEER_ID_MAX + 2];
        const uint8_t *msg_data = payload + P2P_PEER_ID_MAX + 3;
        int msg_data_len = (int)(payload_len - P2P_PEER_ID_MAX - 3);

        if (msg_data_len > P2P_MSG_DATA_MAX) {
            printf(LA_F("[UDP] E: %s: data too large (len=%d)\n", LA_F45, 281), PROTO, msg_data_len);
            fflush(stdout);
            return;
        }

        printf(LA_F("[UDP] V: %s accepted from %s, target='%s', sid=%u, msg=%u, len=%d\n", LA_F58, 289),
               PROTO, from_str, target_peer_id, sid, msg, msg_data_len);
        fflush(stdout);

        // 查找 A 端的配对记录（通过源地址反查）
        compact_pair_t *requester = NULL;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_compact_pairs[i].valid &&
                memcmp(&g_compact_pairs[i].addr, from, sizeof(*from)) == 0) {
                requester = &g_compact_pairs[i];
                break;
            }
        }

        if (!requester) {
            printf(LA_F("[UDP] W: %s: requester not found for %s\n", LA_F77, 304), PROTO, from_str);
            fflush(stdout);
            return;
        }

        // 检查 target 是否与 requester 的 remote_peer_id 匹配
        if (strncmp(requester->remote_peer_id, target_peer_id, P2P_PEER_ID_MAX) != 0) {
            printf(LA_F("[UDP] W: %s: target mismatch (expected='%s', got='%s')\n", LA_F78, 305),
                   PROTO, requester->remote_peer_id, target_peer_id);
            fflush(stdout);
            // 仍然继续处理，但使用 requester->remote_peer_id
        }

        // 检查是否有挂起的消息
        if (requester->msg_pending) {
            printf(LA_F("[UDP] W: %s: already has pending msg, rejecting sid=%u\n", LA_F74, 301), PROTO, sid);
            fflush(stdout);
            
            // 发送 REQ_ACK status=1（对端不在线，实际应该是 "busy"）
            uint8_t ack[4 + 3];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack;
            ack_hdr->type = SIG_PKT_MSG_REQ_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            nwrite_s(ack + 4, sid);
            ack[6] = 1;  // status=1（失败）

            sendto(udp_fd, (const char *)ack, 7, 0, (struct sockaddr *)from, sizeof(*from));
            return;
        }

        // 检查对端是否在线
        if (!requester->peer || requester->peer == (compact_pair_t*)(void*)-1) {
            printf(LA_F("[UDP] W: %s: peer '%s' not online, rejecting sid=%u\n", LA_F76, 303),
                   PROTO, target_peer_id, sid);
            fflush(stdout);
            
            // 发送 REQ_ACK status=1（对端不在线）
            const char* ACK_PROTO = "MSG_REQ_ACK";
            uint8_t ack[4 + 3];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack;
            ack_hdr->type = SIG_PKT_MSG_REQ_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            nwrite_s(ack + 4, sid);
            ack[6] = 1;  // status=1（对端不在线）

            printf("Send %s pkt to %s, seq=0, flags=0, len=7, sid=%u, status=1\n",
                   ACK_PROTO, from_str, sid);

            sendto(udp_fd, (const char *)ack, 7, 0, (struct sockaddr *)from, sizeof(*from));

            printf("[UDP] V: %s sent, status=peer_offline, sid=%u\n", ACK_PROTO, sid);
            fflush(stdout);
            return;
        }

        // 缓存消息
        requester->msg_pending = true;
        requester->msg_sid = sid;
        requester->msg = msg;
        requester->msg_data_len = msg_data_len;
        if (msg_data_len > 0) {
            memcpy(requester->msg_data, msg_data, msg_data_len);
        }

        // 发送 REQ_ACK status=0（成功，开始中转）
        {
            const char* ACK_PROTO = "MSG_REQ_ACK";
            uint8_t ack[4 + 3];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack;
            ack_hdr->type = SIG_PKT_MSG_REQ_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            nwrite_s(ack + 4, sid);
            ack[6] = 0;  // status=0（成功）

            printf("Send %s pkt to %s, seq=0, flags=0, len=7, sid=%u, status=0\n",
                   ACK_PROTO, from_str, sid);

            sendto(udp_fd, (const char *)ack, 7, 0, (struct sockaddr *)from, sizeof(*from));

            printf("[UDP] V: %s sent, status=success, sid=%u (ses_id=%" PRIu64 ")\n",
                   ACK_PROTO, sid, requester->session_id);
            fflush(stdout);
        }

        // 立即向 B 端转发 MSG_REQ
        time_t now = time(NULL);
        enqueue_msg_pending(requester, now);
        send_msg_req_to_peer(udp_fd, requester);

        printf(LA_F("[UDP] I: %s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->local_peer_id, requester->peer->local_peer_id,
               sid, msg, requester->session_id);
        fflush(stdout);
    }
    // SIG_PKT_MSG_RESP: B→Server（只能是 B 对 Server relay 的 MSG_REQ 的响应）
    // 格式: [session_id(8)][sid(2)][code(1)][data(N)]，code=响应码
    else if (hdr->type == SIG_PKT_MSG_RESP) {
        const char* PROTO = "MSG_RESP";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {  // session_id(8) + sid(2) + code(1) 响应码
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F44, 64), PROTO, payload_len);
            fflush(stdout);
            return;
        }

        // 解析
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);
        uint8_t resp_msg = payload[10];
        const uint8_t *resp_data = payload + 11;
        int resp_len = (int)(payload_len - 11);

        printf(LA_F("[UDP] V: %s accepted from %s, session_id=%" PRIu64 ", sid=%u, msg=%u, len=%d\n", 0),
               PROTO, from_str, session_id, sid, resp_msg, resp_len);
        fflush(stdout);

        // 根据 session_id 查找 requester（A 端）
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);
        
        if (!requester || !requester->valid) {
            printf(LA_F("[UDP] W: %s: unknown session_id=%" PRIu64 "\n", 0), PROTO, session_id);
            fflush(stdout);
            return;
        }

        // 检查是否有匹配的挂起消息
        if (!requester->msg_pending || requester->msg_sid != sid) {
            printf(LA_F("[UDP] W: %s: no matching pending msg (sid=%u, expected=%u)\n", LA_F75, 302),
                   PROTO, sid, requester->msg_sid);
            fflush(stdout);
            // 即使不匹配，也要发送 ACK（幂等，避免 B 端一直重发）
            send_msg_resp_ack_to_responder(udp_fd, from, "unknown", sid);
            return;
        }

        // 立即发送 MSG_RESP_ACK 给 B 端（让 B 停止重发）
        // 注意：是向源地址发送（from = B 端地址）
        const char *responder_peer_id = requester->peer ? requester->peer->local_peer_id : "unknown";
        send_msg_resp_ack_to_responder(udp_fd, from, responder_peer_id, sid);

        // 转发给 A 端
        send_msg_resp_to_requester(udp_fd, requester, 0, resp_msg, resp_data, resp_len);
        
        // 更新状态：等待 A 端的 RESP_ACK
        requester->msg_resp_sent_time = time(NULL);
        requester->msg_resp_retry = 0;

        printf(LA_F("[UDP] I: %s forwarded: '%s' -> '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->peer->local_peer_id, requester->local_peer_id,
               sid, requester->session_id);
        fflush(stdout);
    }
    // SIG_PKT_MSG_RESP_ACK: A→Server（A 确认收到 MSG_RESP）
    // 格式: [sid(2)]
    else if (hdr->type == SIG_PKT_MSG_RESP_ACK) {
        const char* PROTO = "MSG_RESP_ACK";

        // 调试打印协议包信息
        printf("Received %s pkt from %s, seq=%u, flags=0x%02x, len=%zu\n",
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 2) {
            printf(LA_F("[UDP] E: %s: bad payload(len=%zu)\n", LA_F44, 64), PROTO, payload_len);
            fflush(stdout);
            return;
        }

        uint16_t sid = nget_s(payload);

        printf(LA_F("[UDP] V: %s accepted from %s, sid=%u\n", LA_F57, 288), PROTO, from_str, sid);
        fflush(stdout);

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
            printf(LA_F("[UDP] W: %s: requester not found for %s\n", LA_F77, 304), PROTO, from_str);
            fflush(stdout);
            return;
        }

        // 检查是否有匹配的挂起消息
        if (!requester->msg_pending || requester->msg_sid != sid) {
            printf(LA_F("[UDP] V: %s: no matching pending msg (sid=%u)\n", LA_F68, 296), PROTO, sid);
            fflush(stdout);
            return;
        }

        // 清理状态
        remove_msg_pending(requester);
        requester->msg_pending = false;
        requester->msg_sid = 0;
        requester->msg_data_len = 0;

        printf(LA_F("[UDP] V: %s: RPC complete for '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->local_peer_id, sid, requester->session_id);
        fflush(stdout);
    }
    else {
        printf(LA_F("[UDP] W: Unknown packet type 0x%02x from %s\n", LA_F85, 87), hdr->type, from_str);
        fflush(stdout);
    }
}

// 清理过期的 COMPACT 模式配对记录
static void cleanup_compact_pairs(sock_t udp_fd) {

    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_compact_pairs[i].valid || 
            (now - g_compact_pairs[i].last_active) <= COMPACT_PAIR_TIMEOUT) continue;

        printf(LA_F("[UDP] W: Timeout for pair '%s' -> '%s' (inactive for %ld seconds)\n", LA_F84, 72), 
               g_compact_pairs[i].local_peer_id, g_compact_pairs[i].remote_peer_id,
               (long)(now - g_compact_pairs[i].last_active));
        fflush(stdout);
        
        // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
        if (g_compact_pairs[i].peer != NULL && 
            g_compact_pairs[i].peer != (compact_pair_t*)(void*)-1 && 
            g_compact_pairs[i].peer->session_id != 0) {
            const char* PROTO = "PEER_OFF";
            
            uint8_t notify[4 + P2P_PEER_ID_MAX];  // 包头 + session_id
            p2p_packet_hdr_t *notify_hdr = (p2p_packet_hdr_t *)notify;
            notify_hdr->type = SIG_PKT_PEER_OFF;
            notify_hdr->flags = 0;
            notify_hdr->seq = htons(0);
            
            nwrite_ll(notify + 4, g_compact_pairs[i].peer->session_id);

            // 调试打印协议包信息
            printf("Send %s pkt to %s:%d, seq=0, flags=0, len=12, session_id=%" PRIu64 "\n",
                   PROTO, inet_ntoa(g_compact_pairs[i].peer->addr.sin_addr),
                   ntohs(g_compact_pairs[i].peer->addr.sin_port),
                   g_compact_pairs[i].peer->session_id);
            
            sendto(udp_fd, (const char *)notify, 4 + P2P_PEER_ID_MAX, 0,
                   (struct sockaddr *)&g_compact_pairs[i].peer->addr, 
                   sizeof(g_compact_pairs[i].peer->addr));
            
            printf(LA_F("[UDP] V: %s sent to '%s' (ses_id=%" PRIu64 ") [timeout]\n", 0),
                   PROTO, g_compact_pairs[i].peer->local_peer_id, g_compact_pairs[i].peer->session_id);
            fflush(stdout);
            
            // 标记对端槽位已断开
            g_compact_pairs[i].peer->peer = (compact_pair_t*)(void*)-1;
        }
        
        // 从待确认链表移除
        if (g_compact_pairs[i].next_pending) {
            remove_compact_pending(&g_compact_pairs[i]);
        }

        // 从 MSG 链表移除
        if (g_compact_pairs[i].next_msg_pending) {
            remove_msg_pending(&g_compact_pairs[i]);
        }

        // 从哈希表删除
        if (g_compact_pairs[i].session_id != 0) {
            HASH_DELETE(hh, g_pairs_by_session, &g_compact_pairs[i]);
        }
        HASH_DELETE(hh_peer, g_pairs_by_peer, &g_compact_pairs[i]);

        g_compact_pairs[i].peer = NULL;  // 清空指针
        g_compact_pairs[i].session_id = 0;
        g_compact_pairs[i].addr_notify_seq = 0;
        g_compact_pairs[i].pending_base_index = 0;
        g_compact_pairs[i].pending_retry = 0;
        g_compact_pairs[i].pending_sent_time = 0;
        g_compact_pairs[i].msg_pending = false;
        g_compact_pairs[i].msg_sid = 0;
        g_compact_pairs[i].msg_data_len = 0;
        g_compact_pairs[i].next_msg_pending = NULL;
        g_compact_pairs[i].valid = false;
    }
}

///////////////////////////////////////////////////////////////////////////////

static void print_usage(const char *prog) {
    printf(LA_F("Usage: %s [port] [probe_port] [relay]", LA_F8, 28), prog);
    printf("\n\n");
    printf("%s\n", LA_S("Parameters:", LA_S14, 18));
    printf("%s\n", LA_S("  port         Signaling server listen port (default: 8888)", LA_S3, 6));
    printf("%s\n", LA_S("               - TCP: RELAY mode signaling (stateful/long connection)", LA_S0, 3));
    printf("%s\n", LA_S("               - UDP: COMPACT mode signaling (stateless)", LA_S1, 4));
    printf("%s\n", LA_S("  probe_port   NAT type detection port (default: 0=disabled)", LA_S4, 7));
    printf("%s\n", LA_S("               Used to detect symmetric NAT (port consistency)", LA_S2, 5));
    printf("%s\n", LA_S("  relay        Enable data relay support (COMPACT mode fallback)", LA_S5, 8));
    printf("\n%s\n", LA_S("Examples:", LA_S13, 17));
    printf(LA_F("  %s                    # Default config (port 8888, no probe, no relay)", LA_F0, 20), prog);
    printf("\n");
    printf(LA_F("  %s 9000               # Listen on port 9000", LA_F1, 21), prog);
    printf("\n");
    printf(LA_F("  %s 9000 9001          # Listen 9000, probe port 9001", LA_F2, 22), prog);
    printf("\n");
    printf(LA_F("  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay", LA_F3, 23), prog);
    printf("\n\n");
}

int main(int argc, char *argv[]) {

    lang_init();  /* 注册默认英文字符串表 */

    // 中文语言支持（需要在--help检查之前处理）
    bool help = false; int opt = 0;
    for (int i = 1; i < argc;) {

        if (strcmp(argv[i], "--cn") == 0) {
            /* Chinese: load lang.zh if present next to the binary */
            FILE *fp = fopen("lang.zh", "r");
            if (fp) { lang_load_fp(fp); fclose(fp); }

            // 移除--cn参数（为了最终的位置参数调整位置
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
            argc--;
            continue;
        }

        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) help = true;
        else if (*argv[i] ==  '-' && !opt) opt = i;
        ++i;
    }
    if (opt) {
        fprintf(stderr, LA_F("Error: Unknown option '%s' (expected: 'relay')", LA_F6, 26), argv[1]);
        fprintf(stderr, "\n\n");
        print_usage(argv[0]);
        return -1;
    }
    // 显示帮助
    if (help) {
        print_usage(argv[0]);
        return 0;
    }

    // 解析参数

    // 第二个位置参数：监听端口（TCP 和 UDP 共用，默认 DEFAULT_PORT）
    int port = DEFAULT_PORT;
    if (argc > 1) {
        char *p = NULL;
        long val = strtol(argv[1], &p, 10);
        if (p == argv[1] || *p != '\0' || val <= 0 || val > 65535) {
            fprintf(stderr, LA_F("Error: Invalid port number '%s' (range: 1-65535)", LA_F4, 24), argv[1]);
            fprintf(stderr, "\n\n");
            print_usage(argv[0]);
            return 1;
        }
        port = (int)val;
    }

    // 第三个位置参数：NAT 探测端口（独立端口，默认禁用）
    if (argc > 2) {
        char *p = NULL;
        long val = strtol(argv[2], &p, 10);
        if (p == argv[2] || *p != '\0' || val < 0 || val > 65535) {
            fprintf(stderr, LA_F("Error: Invalid probe port '%s' (range: 0-65535)", LA_F5, 25), argv[2]);
            fprintf(stderr, "\n\n");
            print_usage(argv[0]);
            return 1;
        }
        g_probe_port = (int)val;
    }

    // 第四个位置参数：是否启用数据 Relay（中继）模式
    if (argc > 3) {
        if (strcmp(argv[3], "relay") != 0) {
            fprintf(stderr, LA_F("Error: Unknown option '%s' (expected: 'relay')", LA_F6, 26), argv[3]);
            fprintf(stderr, "\n\n");
            print_usage(argv[0]);
            return 1;
        }
        g_relay_enabled = true;
    }
    
    if (argc > 4) {
        fprintf(stderr, "%s\n\n", LA_S("Error: Too many arguments", LA_S12, 16));
        print_usage(argv[0]);
        return 1;
    }
    
    // 初始化随机数生成器（用于生成安全的 session_id）
    P_rand_init();

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "%s", LA_S("[SERVER] WSAStartup failed\n", 0));
        return 1;
    }
#endif

    // 打印服务器配置信息
    printf(LA_F("[SERVER] Starting P2P signal server on port %d", LA_F13, 34), port);
    printf("\n");
    printf(LA_F("[SERVER] NAT probe: %s (port %d)", LA_F11, 32), 
           g_probe_port > 0 ? LA_W("enabled", LA_W1, 2) : LA_W("disabled", LA_W0, 1), 
           g_probe_port);
    printf("\n");
    printf(LA_F("[SERVER] Relay support: %s", LA_F12, 33), 
           g_relay_enabled ? LA_W("enabled", LA_W1, 2) : LA_W("disabled", LA_W0, 1));
    printf("\n");
    fflush(stdout);

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
    opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // 创建 UDP 套接字（用于 COMPACT 信令模式）
    sock_t udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == P_INVALID_SOCKET) {
        perror("UDP socket");
        return 1;
    }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // 创建 NAT 探测 UDP 套接字（可选，仅当配置了 probe_port 时）
    sock_t probe_fd = P_INVALID_SOCKET;
    if (g_probe_port > 0) {
        probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (probe_fd == P_INVALID_SOCKET) {
            perror("probe UDP socket");
            return 1;
        }
        setsockopt(probe_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
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
            printf("%s\n", LA_S("[SERVER] NAT probe disabled (bind failed)", LA_S7, 10));
        } else {
            printf(LA_F("[SERVER] NAT probe socket listening on port %d", LA_F10, 31), g_probe_port);
            printf("\n");
        }
        fflush(stdout);
    }

    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd, 10);
    printf(LA_F("P2P Signaling Server listening on port %d (TCP + UDP)...", LA_F7, 27), port);
    printf("\n");
    fflush(stdout);

    // 主循环
    fd_set read_fds;
    time_t last_cleanup = time(NULL), last_compact_retry_check = last_cleanup;
    while (g_running) {

        time_t now = time(NULL);

        // 周期清理过期的 COMPACT 配对记录和 Relay 客户端连接
        if (now - last_cleanup >= CLEANUP_INTERVAL) {
            cleanup_compact_pairs(udp_fd);
            cleanup_relay_clients();
            last_cleanup = now;
        }
        
        // 检查并重传未确认的 PEER_INFO 包（每秒检查一次）
        if (g_pending_connecting_head && (now - last_compact_retry_check) >= COMPACT_RETRY_INTERVAL) {
            retry_compact_pending(udp_fd, now);
            last_compact_retry_check = now;
        }

        // 检查并重传 MSG RPC 包（每秒检查一次）
        if (g_msg_pending_head && (now - last_compact_retry_check) >= MSG_REQ_RETRY_INTERVAL) {
            retry_msg_pending(udp_fd, now);
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
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR) continue;  // 被信号打断，继续循环
#else
            if (errno == EINTR) continue;   // 被信号打断，继续循环
#endif
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
                    g_relay_clients[i].last_active = time(NULL);
                    g_relay_clients[i].pending_count = 0;
                    g_relay_clients[i].pending_sender[0] = '\0';
                    strncpy(g_relay_clients[i].name, "unknown", P2P_PEER_ID_MAX);
                    printf(LA_F("[TCP] New connection from %s:%d\n", LA_F27, 47), 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    fflush(stdout);
                    break;
                }
            }
            if (i == MAX_PEERS) {
                printf("%s", LA_S("[TCP] Max peers reached, rejecting connection\n", LA_S10, 14));
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

                printf(LA_F("[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n", LA_F9, 30),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port),
                       req_seq,
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                fflush(stdout);
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
    printf("\n%s\n", LA_S("[SERVER] Shutting down...", LA_S9, 12));
    
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
    
#ifdef _WIN32
    WSACleanup();
#endif

    printf("%s\n", LA_S("[SERVER] Goodbye!", LA_S6, 9));
    return 0;
}
