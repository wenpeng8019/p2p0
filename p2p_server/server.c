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

// 命令行参数定义
ARGS_I(false, port,       'p', "port",       "Signaling server listen port (TCP+UDP)");
ARGS_I(false, probe_port, 'P', "probe-port", "NAT type detection port (0=disabled)");
ARGS_B(false, relay,      'r', "relay",      "Enable data relay support (COMPACT mode fallback)");
ARGS_B(false, msg,        'm', "msg",        "Enable MSG RPC support");
ARGS_B(false, cn,         0,   "cn",         "Use Chinese language");

#define DEFAULT_PORT                    9333

// cleanup 过期配对/客户端的时间间隔（秒）
#define CLEANUP_INTERVAL                10

// 允许最大同时在线客户端数量
#define MAX_PEERS                       128

// 允许最大候选队列缓存数量
/* + 服务器为每个用户提供的候选缓存能力
 |   32 个候选可容纳大多数网络环境的完整候选集合，实际场景通常：20-30 个候选，32 提供充足余量
 | + 内存占用：COMPACT 模式 32×7字节=224B/用户，RELAY 模式 32×32字节=1KB/用户
*/
#define MAX_CANDIDATES                  32

// COMPACT 模式配对超时时间（秒）
// 客户端在 REGISTERED 状态每 20 秒发一次 keepalive REGISTER，此值取 3 倍间隔
#define COMPACT_PAIR_TIMEOUT            90

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT            60

#define COMPACT_RETRY_INTERVAL_MS       1000    // COMPACT 模式重传检查间隔（毫秒）
// COMPACT 模式 PEER_INFO 重传参数
#define PEER_INFO0_RETRY_INTERVAL_MS    2000    // 重传间隔（毫秒）
#define PEER_INFO0_MAX_RETRY            5       // 最大重传次数

// COMPACT 模式 MSG RPC 重传参数
#define MSG_RPC_RETRY_INTERVAL_MS       1000    // MSG RPC 统一重传间隔（毫秒）
#define MSG_REQ_MAX_RETRY               5       // MSG_REQ 最大重传次数
#define MSG_RESP_MAX_RETRY              10      // MSG_RESP 最大重传次数（比 REQ 更多，确保 A 端收到）

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

// RPC 状态枚举
enum {
    RPC_STATE_IDLE = 0,         // 空闲状态
    RPC_STATE_REQ_PENDING,      // REQ 阶段：等待对端响应
    RPC_STATE_RESP_PENDING,     // RESP 阶段：等待请求方确认
};

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

    // MSG RPC（请求-响应机制，共用字段存储两个阶段的数据）
    uint16_t                rpc_sid;                            // 最后一次完成或正在执行的 RPC 序列号（0=未使用）
    int                     rpc_state;                          // RPC 状态（IDLE/REQ_PENDING/RESP_PENDING）
    uint8_t                 rpc_code;                           // RPC 消息类型/响应码（REQ阶段=消息类型，RESP阶段=响应码）
    uint8_t                 rpc_flags;                          // RPC flags（RESP 阶段使用：PEER_OFFLINE/TIMEOUT）
    uint8_t                 rpc_data[P2P_MSG_DATA_MAX];         // RPC 数据缓冲区
    int                     rpc_data_len;                       // RPC 数据长度
    uint64_t                rpc_sent_time;                      // 最后发送时间（毫秒）
    int                     rpc_retry;                          // 重传次数
    struct compact_pair*    next_rpc_pending;                   // RPC 待确认链表指针
    
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

// MSG RPC 待确认链表（统一管理 REQ 和 RESP 阶段，通过 rpc_state 区分）
static compact_pair_t*      g_rpc_pending_head = NULL;
static compact_pair_t*      g_rpc_pending_rear = NULL;

// 全局运行状态标志（用于信号处理）
static volatile sig_atomic_t g_running = 1;

///////////////////////////////////////////////////////////////////////////////

// 生成安全的随机 session_id（64位，加密安全，防止跨会话注入攻击）
static uint64_t generate_session_id(void) {
    uint64_t id;
    compact_pair_t *existing;
    int attempts = 0;
    
    // 使用循环代替递归，避免极端情况下的栈溢出
    do {
        id = P_rand64();  // 使用 stdc.h 统一封装的加密安全随机数
        HASH_FIND(hh, g_pairs_by_session, &id, sizeof(uint64_t), existing);
        
        // 安全限制：虽然冲突概率极低（1/2^64），但在极端情况下提供保护
        if (++attempts > 1000) {
            print("F:", "Cannot generate unique session_id after 1000 attempts\n");
            exit(1);
        }
    } while (existing);
    
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
        printf(LA_F("[TCP] V: Peer '%s' disconnected\n", LA_F65, 65), g_relay_clients[idx].name);

        P_sock_close(fd);
        g_relay_clients[idx].valid = false;
        g_relay_clients[idx].current_peer[0] = '\0';
        return;
    }

    // 调试打印协议包信息
    printf("Received TCP pkt from peer '%s': magic=0x%08X, type=%d, length=%d (expect magic=0x%08X)\n",
           g_relay_clients[idx].name, hdr.magic, hdr.type, hdr.length, P2P_RLY_MAGIC);

    if (hdr.magic != P2P_RLY_MAGIC) {
        printf(LA_F("[TCP] E: Invalid magic from peer '%s'\n", LA_F45, 45), g_relay_clients[idx].name);

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

        printf(LA_F("[TCP] I: Peer '%s' logged in\n", LA_F50, 50), g_relay_clients[idx].name);

        // 发送登录确认
        const char* ACK_PROTO = "LOGIN_ACK";

        p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_LOGIN_ACK, 0};
        send(fd, (const char *)&ack, sizeof(ack), 0);

        printf(LA_F("[TCP] V: %s sent to '%s'\n", LA_F64, 64), ACK_PROTO, g_relay_clients[idx].name);

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

                printf(LA_F("[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n", LA_F51, 51),
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
            
            printf(LA_F("[TCP] Flushing %d pending candidates from '%s' to '%s'...\n", LA_F49, 49), 
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
            
            printf(LA_F("[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n", LA_F39, 39),
                   sender, client->pending_count, (int)n);
            
            // 清空缓存
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(LA_F("[TCP] All pending candidates flushed to '%s'\n", LA_F41, 41), client->name);
        }
        // 检查是否缓存已满（场景5：发送空 OFFER，让对端反向连接）
        else if (g_relay_clients[idx].pending_count == MAX_CANDIDATES && g_relay_clients[idx].pending_sender[0] != '\0') {
            relay_client_t *client = &g_relay_clients[idx];
            const char *sender = client->pending_sender;
            
            printf(LA_F("[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n", LA_F61, 61),
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
            
            printf(LA_F("[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n", LA_F40, 40), sender);
            
            // 清空缓存满标识
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(LA_F("[TCP] Storage full indication flushed to '%s'\n", LA_F59, 59), client->name);
        }
    }
    // 信令转发：P2P_RLY_CONNECT → P2P_RLY_OFFER，P2P_RLY_ANSWER → P2P_RLY_FORWARD
    else if (hdr.type == P2P_RLY_CONNECT || hdr.type == P2P_RLY_ANSWER) {

        // 接收目标对端名称
        char target_name[P2P_PEER_ID_MAX];
        if (recv(fd, target_name, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
            printf(LA_F("[TCP] Failed to receive target name from %s\n", LA_F47, 47), g_relay_clients[idx].name);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        // 接收信令负载数据
        uint32_t payload_len = hdr.length - P2P_PEER_ID_MAX;
        if (payload_len > 65536) {  // 防止过大的负载
            printf(LA_F("[TCP] Payload too large (%u bytes) from %s\n", LA_F54, 54), payload_len, g_relay_clients[idx].name);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        uint8_t *payload = malloc(payload_len);
        if (recv(fd, (char *)payload, payload_len, 0) != (int)payload_len) {
            printf(LA_F("[TCP] Failed to receive payload from %s\n", LA_F46, 46), g_relay_clients[idx].name);
            free(payload);
            P_sock_close(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }

        printf(LA_F("[TCP] Relaying %s from %s to %s (%u bytes)\n", LA_F55, 55), 
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
                printf(LA_F("[TCP] Sent %s with %d candidates to '%s' (from '%s')\n", LA_F56, 56),
                       is_first_offer ? "OFFER" : "FORWARD", candidates_in_payload, 
                       target_name, g_relay_clients[idx].name);
                break;
            }
        }
        
        if (!found) {
            /* 目标离线：缓存候选 */
            printf(LA_F("[TCP] Target %s offline, caching candidates...\n", LA_F62, 62), target_name);
            
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
                        printf(LA_F("[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n", LA_F53, 53),
                               g_relay_clients[idx].name, target->pending_sender, target->pending_count);
                    }
                    target->pending_count = 0;
                    strncpy(target->pending_sender, g_relay_clients[idx].name, P2P_PEER_ID_MAX);
                }
                
                for (int i = 0; i < candidates_in_payload; i++) {
                    if (target->pending_count >= MAX_CANDIDATES) {
                        ack_status = 2;  /* 缓存已满 */
                        printf(LA_F("[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n", LA_F58, 58),
                               target_name, candidates_acked, candidates_in_payload - candidates_acked);
                        
                        // 缓存已满时，pending_sender 本身就表示连接意图（不需要额外字段）
                        // 此时 pending_count 保持为 MAX_CANDIDATES，pending_sender 已经记录了发送者
                        printf(LA_F("[TCP] Storage full, connection intent from '%s' to '%s' noted\n", LA_F60, 60),
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
                        printf(LA_F("[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n", LA_F43, 43),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    } else {
                        ack_status = 1;  /* 已缓存，还有剩余空间 */
                        printf(LA_F("[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n", LA_F42, 42),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    }
                }
            } else {
                ack_status = 2;  /* 无法分配槽位 */
                candidates_acked = 0;
                printf(LA_F("[TCP] Cannot allocate slot for offline user '%s'\n", LA_F44, 44), target_name);
            }
        }

        /* 仅对 P2P_RLY_CONNECT 发送 ACK（P2P_RLY_ANSWER 不需要确认） */
        if (hdr.type == P2P_RLY_CONNECT) {
            p2p_relay_hdr_t ack_hdr = {P2P_RLY_MAGIC, P2P_RLY_CONNECT_ACK, sizeof(p2p_relay_connect_ack_t)};
            p2p_relay_connect_ack_t ack_payload = {ack_status, candidates_acked, {0, 0}};
            size_t sent1 = send(fd, (const char *)&ack_hdr, sizeof(ack_hdr), 0);
            size_t sent2 = send(fd, (const char *)&ack_payload, sizeof(ack_payload), 0);
            if (sent1 != sizeof(ack_hdr) || sent2 != sizeof(ack_payload)) {
                printf(LA_F("[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n", LA_F48, 48),
                       g_relay_clients[idx].name, (int)sent1, (int)sent2);
            } else {
                printf(LA_F("[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n", LA_F57, 57), 
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
                    printf("%s", LA_S("[TCP] User list truncated (too many users)\n", LA_S4, 4));
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
        printf(LA_F("[TCP] Unknown message type %d from %s\n", LA_F63, 63), hdr.type, g_relay_clients[idx].name);
    }
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {
    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_relay_clients[i].valid || 
            (now - g_relay_clients[i].last_active) <= RELAY_CLIENT_TIMEOUT * 1000) continue;

        printf(LA_F("[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n", LA_F66, 66), 
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
    const char* PROTO = "REGISTER_ACK";

    uint8_t ack[26];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_REGISTER_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    if (status <= 1) {
        if (ARGS_relay.i64)    hdr->flags |= SIG_REGACK_FLAG_RELAY;
        if (ARGS_msg.i64)      hdr->flags |= SIG_REGACK_FLAG_MSG;
        ack[4] = status;
        nwrite_ll(ack + 5, session_id);
        nwrite_l(ack + 13, instance_id);
        ack[17] = MAX_CANDIDATES;
        memcpy(ack + 18, &to->sin_addr.s_addr, 4);
        memcpy(ack + 22, &to->sin_port, 2);
        uint16_t probe = htons((uint16_t)ARGS_probe_port.i64);
        memcpy(ack + 24, &probe, 2);

        print("V:", LA_F("Send %s: status=%s, max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, ses_id=%" PRIu64 ", inst_id=%u\n", 0, 0),
              PROTO, status ? "peer_online" : "peer_offline", MAX_CANDIDATES,
              ARGS_relay.i64 ? "yes" : "no", ARGS_msg.i64 ? "yes" : "no",
              inet_ntoa(to->sin_addr), ntohs(to->sin_port), 
              (int)ARGS_probe_port.i64, session_id, instance_id);
    } else {
        
        ack[4] = status;
        memset(ack + 5, 0, 21);
        print("V:", LA_F("Send %s: status=error (no slot available)\n", LA_F33, 33), PROTO);
    }

    ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (const struct sockaddr *)to, sizeof(*to));
    if (n != sizeof(ack))
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F68, 68), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n", LA_F71, 71), PROTO, to_str, hdr->flags, (int)n);     
}

// 发送 PEER_OFF 通知: [hdr(4)][session_id(8)] = 12字节
// 并标记对端已断开 (peer->peer = -1)
static void send_peer_off(sock_t udp_fd, compact_pair_t *peer, const char *reason) {
    const char* PROTO = "PEER_OFF";

    uint8_t pkt[4 + 8];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_PEER_OFF; hdr->flags = 0; hdr->seq = htons(0);
    nwrite_ll(pkt + 4, peer->session_id);

    print("V:", LA_F("Send %s: peer='%s', reason=%s, ses_id=%" PRIu64 "\n", 0),
          PROTO, peer->local_peer_id, reason, peer->session_id);

    ssize_t n = sendto(udp_fd, (const char *)pkt, sizeof(pkt), 0,
           (struct sockaddr *)&peer->addr, sizeof(peer->addr));
    if (n != (ssize_t)sizeof(pkt))
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F72, 72),
              PROTO, inet_ntoa(peer->addr.sin_addr), ntohs(peer->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F73, 73),
               PROTO, inet_ntoa(peer->addr.sin_addr), ntohs(peer->addr.sin_port), (int)n);

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

    print("V:", LA_F("Send %s: base_index=%u, cands=%d, ses_id=%" PRIu64 ", peer='%s'\n", 0),
          PROTO, base_index, cand_cnt, pair->session_id, pair->local_peer_id);

    ssize_t n = sendto(udp_fd, (const char *)pkt, resp_len, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));
    if (n != resp_len)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F72, 72),
              PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F73, 73),
               PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port), (int)n);
}

// 发送 MSG_REQ_ACK: [hdr(4)][session_id(8)][sid(2)][status(1)] = 15字节
// status: 0=成功, 1=失败
static void send_msg_req_ack(sock_t udp_fd, const struct sockaddr_in *to, const char *to_str, 
                             uint64_t session_id, uint16_t sid, uint8_t status) {
    const char* PROTO = "MSG_REQ_ACK";

    uint8_t ack[4 + 8 + 2 + 1];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_MSG_REQ_ACK;
    hdr->flags = 0;
    hdr->seq = 0;
    nwrite_ll(ack + 4, session_id);
    nwrite_s(ack + 12, sid);
    ack[14] = status;

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, status=%u\n", 0),
          PROTO, session_id, sid, status);

    ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (const struct sockaddr *)to, sizeof(*to));
    if (n != (ssize_t)sizeof(ack))
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F68, 68), PROTO, to_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F70, 70), PROTO, to_str, (int)n);
}

// 发送 MSG_REQ 给对端（Server→对端 relay）
// 协议格式: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=SIG_MSG_FLAG_RELAY
// msg=消息类型，msg=0 时对端自动 echo
static void send_msg_req_to_peer(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_REQ";

    assert(pair && PEER_ONLINE(pair));
    assert(pair->rpc_state == RPC_STATE_REQ_PENDING);

    uint8_t pkt[4 + 8 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_REQ;
    hdr->flags = SIG_MSG_FLAG_RELAY;  // 标识这是 Server→对端 的 relay 包
    hdr->seq = 0;

    int n = 4;
    nwrite_ll(pkt + n, pair->peer->session_id); n += 8;  // 对端的 session_id
    nwrite_s(pkt + n, pair->rpc_sid); n += 2;
    pkt[n++] = pair->rpc_code;
    if (pair->rpc_data_len > 0) {
        memcpy(pkt + n, pair->rpc_data, pair->rpc_data_len);
        n += pair->rpc_data_len;
    }

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, msg=%u, data_len=%d, peer='%s'\n", 0),
          PROTO, pair->peer->session_id, pair->rpc_sid, pair->rpc_code, pair->rpc_data_len,
          pair->peer->local_peer_id);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
    if (sent != n)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F72, 72),
              PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n", LA_F74, 74),
               PROTO, inet_ntoa(pair->peer->addr.sin_addr), ntohs(pair->peer->addr.sin_port),
               hdr->flags, (int)sent, pair->rpc_retry);
}

// 发送 MSG_RESP_ACK 给 B 端（Server→B）
// 协议格式: [session_id(8)][sid(2)]
// 说明: 确认收到 B 端的 MSG_RESP，让 B 停止重发
static void send_msg_resp_ack_to_responder(sock_t udp_fd, const struct sockaddr_in *addr, 
                                           const char *peer_id, uint64_t session_id, uint16_t sid) {
    const char* PROTO = "MSG_RESP_ACK";

    uint8_t pkt[4 + 8 + 2];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    nwrite_ll(pkt + 4, session_id);
    nwrite_s(pkt + 12, sid);

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, peer='%s'\n", 0),
          PROTO, session_id, sid, peer_id);

    ssize_t n = sendto(udp_fd, (const char *)pkt, sizeof(pkt), 0, (struct sockaddr *)addr, sizeof(*addr));
    if (n != (ssize_t)sizeof(pkt))
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F72, 72),
              PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F73, 73),
               PROTO, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), (int)n);
}

// 发送 MSG_RESP 给请求方（Server→A，从缓存的 rpc_* 字段构建）
// 协议格式: [session_id(8)][sid(2)][msg(1)][data(N)]（正常响应，msg=响应码）
//          [session_id(8)][sid(2)]（错误响应，flags 中标识错误类型）
// 说明: 包含 session_id 用于 A 端验证响应合法性
static void send_msg_resp_to_requester(sock_t udp_fd, compact_pair_t *pair) {
    const char* PROTO = "MSG_RESP";

    assert(pair && pair->rpc_state == RPC_STATE_RESP_PENDING);

    uint8_t pkt[4 + 8 + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_MSG_RESP;
    hdr->flags = pair->rpc_flags;
    hdr->seq = 0;

    int n = 4;
    nwrite_ll(pkt + n, pair->session_id); n += 8;
    nwrite_s(pkt + n, pair->rpc_sid); n += 2;
    
    // 如果是正常响应，包含 msg(响应码) 和 data（复用 rpc_code 和 rpc_data 字段）
    if (!(pair->rpc_flags & (SIG_MSG_FLAG_PEER_OFFLINE | SIG_MSG_FLAG_TIMEOUT))) {
        pkt[n++] = pair->rpc_code;
        if (pair->rpc_data_len > 0) {
            memcpy(pkt + n, pair->rpc_data, pair->rpc_data_len);
            n += pair->rpc_data_len;
        }
    }

    print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n", 0),
          PROTO, pair->session_id, pair->rpc_sid, pair->local_peer_id, pair->rpc_flags, pair->rpc_code, pair->rpc_data_len);

    ssize_t sent = sendto(udp_fd, (const char *)pkt, n, 0, (struct sockaddr *)&pair->addr, sizeof(pair->addr));
    if (sent != n)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F72, 72),
                PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port), P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n", LA_F74, 74),
                PROTO, inet_ntoa(pair->addr.sin_addr), ntohs(pair->addr.sin_port),
                pair->rpc_flags, (int)sent, pair->rpc_retry);
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
        if (now - g_info0_pending_head->info0_sent_time < PEER_INFO0_RETRY_INTERVAL_MS) {
            return;
        }

        // 将第一项移除
        compact_pair_t *q = g_info0_pending_head;
        g_info0_pending_head = q->next_info0_pending;

        // 检查是否超过最大重传次数
        if (q->info0_retry >= PEER_INFO0_MAX_RETRY) {
            
            // 超过最大重传次数，从链表移除（放弃）
            print("W:", LA_F("PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n", LA_F29, 29),
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
        else {
            // 检查对端是否仍在线（可能在 pending 期间断开）
            if (!PEER_ONLINE(q)) {
                // 对端已断开，从链表移除
                q->next_info0_pending = NULL;
                if (g_info0_pending_head == (void*)-1) {
                    g_info0_pending_head = NULL;
                    g_info0_pending_rear = NULL;
                    return;
                }
                continue;  // 处理下一个
            }

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

            print("V:", LA_F("PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%" PRIu64 ")\n", 0),
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

// 从 RPC 待确认链表移除
static void remove_rpc_pending(compact_pair_t *pair) {

    if (!g_rpc_pending_head || !pair->next_rpc_pending) return;

    if (g_rpc_pending_head == pair) {
        g_rpc_pending_head = pair->next_rpc_pending;
        pair->next_rpc_pending = NULL;
        if (g_rpc_pending_head == (void*)-1) {
            g_rpc_pending_head = NULL;
            g_rpc_pending_rear = NULL;
        }
        return;
    }

    compact_pair_t *prev = g_rpc_pending_head;
    while (prev->next_rpc_pending != pair) {
        if (prev->next_rpc_pending == (void*)-1) return;
        prev = prev->next_rpc_pending;
    }

    prev->next_rpc_pending = pair->next_rpc_pending;
    if (pair->next_rpc_pending == (void*)-1) {
        g_rpc_pending_rear = prev;
    }
    pair->next_rpc_pending = NULL;
}

// 将配对加入 RPC 待确认链表
static void enqueue_rpc_pending(compact_pair_t *pair, uint64_t now) {

    if (pair->next_rpc_pending) {
        remove_rpc_pending(pair);
    }

    pair->rpc_sent_time = now;
    pair->rpc_retry = 0;

    pair->next_rpc_pending = (compact_pair_t*)(void*)-1;
    if (g_rpc_pending_rear) {
        g_rpc_pending_rear->next_rpc_pending = pair;
        g_rpc_pending_rear = pair;
    } else {
        g_rpc_pending_head = pair;
        g_rpc_pending_rear = pair;
    }
}

// 检查并重传 RPC（统一处理 REQ 和 RESP 阶段的超时重传）
// 队列按发送时间排序（出队-重传-入队），未超时即短路返回
static void retry_rpc_pending(sock_t udp_fd, uint64_t now) {

    if (!g_rpc_pending_head) return;

    for (;;) {

        // 队列按时间排序，一旦遇到未超时的节点，后面都不会超时
        if (now - g_rpc_pending_head->rpc_sent_time < MSG_RPC_RETRY_INTERVAL_MS) {
            return;
        }

        // 将第一项从队列头部移除
        compact_pair_t *q = g_rpc_pending_head;
        g_rpc_pending_head = q->next_rpc_pending;
        q->next_rpc_pending = NULL;

        // REQ 阶段：等待对端响应
        if (q->rpc_state == RPC_STATE_REQ_PENDING) {

            // 检查对端是否离线
            if (!PEER_ONLINE(q)) {
                print("W:", LA_F("MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
                      q->local_peer_id, q->rpc_sid, q->session_id);

                // 已从链表移除（next_rpc_pending=NULL），transition 内部的 remove 是 no-op
                if (g_rpc_pending_head == (void*)-1) {
                    g_rpc_pending_head = NULL;
                    g_rpc_pending_rear = NULL;
                }
                transition_to_resp_pending(udp_fd, q, now, SIG_MSG_FLAG_PEER_OFFLINE, 0, NULL, 0);
            }
            // 超过最大重传次数，发送超时错误
            else if (q->rpc_retry >= MSG_REQ_MAX_RETRY) {
                print("W:", LA_F("MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
                      q->rpc_retry, q->local_peer_id, q->rpc_sid, q->session_id);

                if (g_rpc_pending_head == (void*)-1) {
                    g_rpc_pending_head = NULL;
                    g_rpc_pending_rear = NULL;
                }
                transition_to_resp_pending(udp_fd, q, now, SIG_MSG_FLAG_TIMEOUT, 0, NULL, 0);
            }
            // 重传 MSG_REQ 给对端
            else {
                send_msg_req_to_peer(udp_fd, q);
                q->rpc_retry++;
                q->rpc_sent_time = now;

                // 重新加入队尾（保持时间排序）
                q->next_rpc_pending = (compact_pair_t*)(void*)-1;
                if (g_rpc_pending_head == (void*)-1) {
                    g_rpc_pending_head = q;
                    g_rpc_pending_rear = q;
                } else {
                    g_rpc_pending_rear->next_rpc_pending = q;
                    g_rpc_pending_rear = q;
                }
                print("V:", LA_F("MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%" PRIu64 ")\n", 0),
                      q->local_peer_id, q->peer->local_peer_id,
                      q->rpc_sid, q->rpc_retry, MSG_REQ_MAX_RETRY, q->session_id);

                if (g_rpc_pending_head == q) return;
            }
        }
        // RESP 阶段：等待请求方确认
        else if (q->rpc_state == RPC_STATE_RESP_PENDING) {

            // 超过最大重传次数，放弃
            if (q->rpc_retry >= MSG_RESP_MAX_RETRY) {
                print("W:", LA_F("MSG_RESP gave up after %d retries, sid=%u (ses_id=%" PRIu64 ")\n", 0),
                      q->rpc_retry, q->rpc_sid, q->session_id);

                if (g_rpc_pending_head == (void*)-1) {
                    g_rpc_pending_head = NULL;
                    g_rpc_pending_rear = NULL;
                }
                q->rpc_state = RPC_STATE_IDLE;
            }
            // 从缓存重传 MSG_RESP
            else {
                q->rpc_retry++;
                send_msg_resp_to_requester(udp_fd, q);
                q->rpc_sent_time = now;

                // 重新加入队尾（保持时间排序）
                q->next_rpc_pending = (compact_pair_t*)(void*)-1;
                if (g_rpc_pending_head == (void*)-1) {
                    g_rpc_pending_head = q;
                    g_rpc_pending_rear = q;
                } else {
                    g_rpc_pending_rear->next_rpc_pending = q;
                    g_rpc_pending_rear = q;
                }
                print("V:", LA_F("MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%" PRIu64 ")\n", 0),
                      q->local_peer_id, q->rpc_sid, q->rpc_retry, MSG_RESP_MAX_RETRY, q->session_id);

                if (g_rpc_pending_head == q) return;
            }
        }
        // 意外状态：从链表移除
        else {
            if (g_rpc_pending_head == (void*)-1) {
                g_rpc_pending_head = NULL;
                g_rpc_pending_rear = NULL;
            }
        }

        // 队列已空
        if (!g_rpc_pending_head) return;
    }
}


// 缓存响应数据到 pair->rpc_* 并从 REQ 阶段转换到 RESP 阶段
static void transition_to_resp_pending(sock_t udp_fd, compact_pair_t *requester, uint64_t now,
                                       uint8_t flags, uint8_t code, const uint8_t *data, int len) {
    // 从 RPC 链表移除（REQ 阶段）
    remove_rpc_pending(requester);

    // 状态转换为 RESP：直接复用 rpc_code 和 rpc_data 字段存储响应
    requester->rpc_state = RPC_STATE_RESP_PENDING;
    // rpc_sid 保持不变（RESP 的 sid 等于对应 REQ 的 sid）
    requester->rpc_flags = flags;
    
    // 响应的 code 和 data 直接保存到 rpc_code 和 rpc_data（复用）
    requester->rpc_code = code;
    requester->rpc_data_len = 0;
    if (len > 0 && data) {
        memcpy(requester->rpc_data, data, len);
        requester->rpc_data_len = len;
    }

    // 发送并加入 RPC 链表（RESP 阶段）
    enqueue_rpc_pending(requester, now);
    send_msg_resp_to_requester(udp_fd, requester);
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

        // 首包已成功确认（info0_acked == 1），立即发送地址变更通知
        if (pair->peer->info0_acked == 1) {

            pair->peer->addr_notify_seq = (uint8_t)(pair->peer->addr_notify_seq + 1);
            if (pair->peer->addr_notify_seq == 0) pair->peer->addr_notify_seq = 1;

            send_peer_info_seq0(udp_fd, pair->peer, pair->peer->addr_notify_seq);
            enqueue_info0_pending(pair->peer, pair->peer->addr_notify_seq, P_tick_ms());

            print("I:", LA_F("Addr changed for '%s', notifying '%s' (ses_id=%" PRIu64 ")\n", 0),
                  pair->local_peer_id, pair->peer->local_peer_id, pair->peer->session_id);
        }
        // 首包未确认（info0_acked == 0）或已放弃（info0_acked == -1）
        else if (pair->peer->info0_acked == 0) {
            // 正在同步中，延期发送地址变更通知（等 ACK 后再发）
            // 设置 addr_notify_seq = 1 标记有延期通知
            if (pair->peer->addr_notify_seq == 0) pair->peer->addr_notify_seq = 1;
            
            print("I:", LA_F("Addr changed for '%s', defer notification until first ACK (ses_id=%" PRIu64 ")\n", 0),
                  pair->local_peer_id, pair->peer->session_id);
        }
        // info0_acked == -1：首包已放弃，不再发送任何通知
        else print("W:", LA_F("Addr changed for '%s', but first info packet was abandoned (ses_id=%" PRIu64 ")\n", 0),
                   pair->local_peer_id, pair->peer->session_id);
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

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len <= P2P_PEER_ID_MAX * 2 + 4) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
            return;
        }

        // 解析 instance_id
        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX * 2);
        if (instance_id == 0) {
            print("E:", LA_F("%s: invalid instance_id=0 from %s\n", LA_F15, 15), PROTO, from_str);
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

        print("V:", LA_F("%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n", LA_F11, 11),
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
                    g_compact_pairs[i].rpc_state = RPC_STATE_IDLE;
                    g_compact_pairs[i].rpc_sid = 0;
                    g_compact_pairs[i].rpc_data_len = 0;
                    g_compact_pairs[i].next_rpc_pending = NULL;
                    
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

            print("I:", LA_F("%s from '%.*s': new instance(old=%u new=%u), resetting session\n", LA_F10, 10),
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
            
            // 清理 RPC pending 状态（可能有旧会话的 RPC 正在重传）
            if (local->next_rpc_pending) {
                remove_rpc_pending(local);
            }
            local->rpc_state = RPC_STATE_IDLE;
            local->rpc_sid = 0;
            local->rpc_data_len = 0;
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
        send_register_ack(udp_fd, from, from_str, 
                          (remote_idx >= 0) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE,
                          local->session_id, instance_id);

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

                print("I:", LA_F("Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n", LA_F30, 30),
                       P2P_PEER_ID_MAX, local_peer_id, remote->candidate_count,
                       P2P_PEER_ID_MAX, remote_peer_id, local->candidate_count);
            }
            // 地址变更通知已在 check_addr_change() 中统一处理
            else assert(local->peer == remote && remote->peer == local);

        } else print("V:", LA_F("%s: waiting for peer '%.*s' to register\n", LA_F23, 23), PROTO, P2P_PEER_ID_MAX, remote_peer_id);
    } break;

    // SIG_PKT_UNREGISTER: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端主动断开时发送，请求服务器立即释放配对槽位。
    // 【服务端可选实现】如果不处理此包类型，客户端自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制。
    case SIG_PKT_UNREGISTER: { const char* PROTO = "UNREGISTER";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < P2P_PEER_ID_MAX * 2) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
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

            print("V:", LA_F("%s: accepted, releasing slot for '%s' -> '%s'\n", LA_F12, 12),
                   PROTO, local_peer_id, remote_peer_id);

            // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
            if (PEER_ONLINE(pair) && pair->peer->session_id != 0) {
                send_peer_off(udp_fd, pair->peer, "unregister");
            }
            
            // 从待确认链表移除
            if (pair->next_info0_pending) {
                remove_info0_pending(pair);
            }

            // 从 MSG RPC 链表移除
            if (pair->next_rpc_pending) {
                remove_rpc_pending(pair);
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
            pair->rpc_state = RPC_STATE_IDLE;
            pair->rpc_sid = 0;
            pair->rpc_data_len = 0;  // 添加数据长度清零
            pair->next_rpc_pending = NULL;
        }
    } break;

    // SIG_PKT_ALIVE: [session_id(8)]
    // 客户端定期发送以保持槽位活跃，更新 last_active 时间
    case SIG_PKT_ALIVE: { const char* PROTO = "ALIVE";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 8) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);

        // 直接用 session_id hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);        
        if (pair && pair->valid) {

            print("V:", LA_F("%s accepted, peer='%s', ses_id=%" PRIu64 "\n", 0),
                  PROTO, pair->local_peer_id, session_id);

            pair->last_active = P_tick_ms();
            check_addr_change(udp_fd, pair, from);

            // 回复 ACK
            {   const char* ACK_PROTO = "ALIVE_ACK";

                uint8_t ack[4];
                p2p_pkt_hdr_encode(ack, SIG_PKT_ALIVE_ACK, 0, 0);

                print("V:", LA_F("Send %s: ses_id=%" PRIu64 ", peer='%s'\n", 0),
                      ACK_PROTO, session_id, pair->local_peer_id);

                ssize_t n = sendto(udp_fd, (const char *)ack, sizeof(ack), 0, (struct sockaddr *)from, sizeof(*from));
                if (n != (ssize_t)sizeof(ack))
                    print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F68, 68), ACK_PROTO, from_str, P_sock_errno());
                else
                    printf(LA_F("[UDP] %s send to %s, seq=0, flags=0, len=%d\n", LA_F70, 70), ACK_PROTO, from_str, (int)n);
            }
        }
    } break;

    // SIG_PKT_PEER_INFO_ACK: ACK 确认收到 PEER_INFO 包
    // 格式: [hdr(4)][session_id(8)]，确认序号使用 hdr->seq
    case SIG_PKT_PEER_INFO_ACK: { const char* PROTO = "PEER_INFO_ACK";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 8) {  // session_id(8)
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);
        uint16_t ack_seq = ntohs(hdr->seq);
        if (ack_seq > 16) {
            print("E:", LA_F("%s: invalid seq=%u\n", LA_F17, 17), PROTO, ack_seq);
            return;
        }

        // 通过 session_id 查找对应的配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);

        print("V:", LA_F("%s accepted, seq=%u, ses_id=%" PRIu64 "\n", 0),
              PROTO, ack_seq, session_id);

        // ack_seq=0 的 ACK 是对服务器发送的首个 PEER_INFO 的确认，服务器需要处理
        if (ack_seq == 0) {

            if (pair && pair->valid) {
                check_addr_change(udp_fd, pair, from);

                // 标记为已确认，从待确认链表移除
                bool is_first_ack = (!pair->info0_acked && pair->info0_base_index == 0);
                if (is_first_ack) {
                    pair->info0_acked = true;

                    print("V:", LA_F("%s: confirmed '%s', retries=%d (ses_id=%" PRIu64 ")\n", 0),
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

                    print("I:", LA_F("Addr changed for '%s', deferred notifying '%s' (ses_id=%" PRIu64 ")\n", 0),
                          pair->peer->local_peer_id, pair->local_peer_id, pair->peer->session_id);
                }
            }
            else print("W:", LA_F("%s for unknown ses_id=%" PRIu64 "\n", 0), PROTO, session_id);
        }
        // ack_seq≠0 的 ACK 是客户端之间的确认，服务器只负责 relay 转发
        else {
            
            if (pair && pair->valid && PEER_ONLINE(pair)) {
                check_addr_change(udp_fd, pair, from);

                // 转发给对方
                sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                       (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                print("V:", LA_F("Relay %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                       PROTO, ack_seq, pair->local_peer_id, pair->remote_peer_id, session_id);
            }
            else print("W:", LA_F("Cannot relay %s: ses_id=%" PRIu64 " (peer unavailable)\n", 0), PROTO, session_id);
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

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        // 所有需要 relay 的包格式都是 [session_id(8)][...]
        if (payload_len < 8) {  // session_id 8 bytes
            print("E:", LA_F("[Relay] %s: bad payload(len=%zu)\n", LA_F38, 38), PROTO, payload_len);
            return;
        }

        // SIG_PKT_PEER_INFO 特殊处理：seq=0 是服务器维护的包，不应该出现在这里
        if (hdr->type == SIG_PKT_PEER_INFO && hdr->seq == 0) {
            print("E:", LA_F("[Relay] %s seq=0 from client %s (server-only, dropped)\n", LA_F37, 37), PROTO, from_str);
            return;
        }

        uint64_t session_id = nget_ll(payload);

        // 根据 session_id 查找配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
        if (!pair || !pair->valid) {
            print("W:", LA_F("[Relay] %s for unknown ses_id=%" PRIu64 " (dropped)\n", 0), PROTO, session_id);
            return;
        }

        // 对方不存在，丢弃
        if (!PEER_ONLINE(pair)) {
            print("W:", LA_F("[Relay] %s for ses_id=%" PRIu64 ": peer unavailable (dropped)\n", 0), PROTO, session_id);
            return;
        }

        check_addr_change(udp_fd, pair, from);

        print("V:", LA_F("%s accepted, '%s' -> '%s', ses_id=%" PRIu64 "\n", 0),
              PROTO, pair->local_peer_id, pair->remote_peer_id, session_id);

        // 转发给对方
        sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
               (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

        if (hdr->type == SIG_PKT_PEER_INFO) {
            print("V:", LA_F("[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else if (hdr->type == P2P_PKT_RELAY_DATA || hdr->type == P2P_PKT_RELAY_CRYPTO) {
            print("V:", LA_F("[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, ntohs(hdr->seq), pair->local_peer_id, pair->remote_peer_id, session_id);
        } else {
            print("V:", LA_F("[Relay] %s: '%s' -> '%s' (ses_id=%" PRIu64 ")\n", 0),
                   PROTO, pair->local_peer_id, pair->remote_peer_id, session_id);
        }
    } break;

    // SIG_PKT_MSG_REQ: A→Server 或 B→Server（通过 flags 区分）
    // A→Server: [session_id(8)][sid(2)][msg(1)][data(N)]，flags=0，msg=消息类型
    // B→Server（不会有这种情况，B 只发 MSG_RESP）
    case SIG_PKT_MSG_REQ: { const char* PROTO = "MSG_REQ";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
            return;
        }

        // MSG_REQ 只能是 A→Server（flags=0）
        if (hdr->flags & SIG_MSG_FLAG_RELAY) {
            print("E:", LA_F("%s: invalid relay flag from client\n", LA_F16, 16), PROTO);
            return;
        }

        int msg_data_len = (int)(payload_len - 11);
        if (msg_data_len > P2P_MSG_DATA_MAX) {
            print("E:", LA_F("%s: data too large (len=%d)\n", LA_F14, 14), PROTO, msg_data_len);
            return;
        }

        // 解析验证 session_id 和 sid 不为 0
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);

        // 验证 session_id 和 sid 不为 0
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu64 " or sid=%u\n", 0, 0), 
                   PROTO, session_id, sid);
            return;
        }

        // 解析 msg、data
        uint8_t msg = payload[10];
        const uint8_t *msg_data = payload + 11;

        // 查找 A 端的配对记录（通过 session_id 快速索引）
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);
        if (!requester || !requester->valid) {
            print("W:", LA_F("%s: requester not found for ses_id=%" PRIu64 "\n", 0), PROTO, session_id);
            return;
        }

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 ", sid=%u, msg=%u, len=%d\n", 0),
               PROTO, session_id, sid, msg, msg_data_len);

        // 先检查对端是否在线（如果对端下线，pending RPC 会在 UNREGISTER/超时时清理）
        if (!PEER_ONLINE(requester)) {

            print("W:", LA_F("%s: peer '%s' not online, rejecting sid=%u\n", LA_F22, 22),
                   PROTO, requester->remote_peer_id, sid);

            send_msg_req_ack(udp_fd, from, from_str, session_id, sid, 1);
            return;
        }

        // 统一进行地址变更检测（NAT 重绑定）
        check_addr_change(udp_fd, requester, from);

        // 检查是否有挂起的 RPC（REQ 或 RESP 阶段）
        if (requester->rpc_state != RPC_STATE_IDLE) {

            // 如果是重传同一个 sid（客户端未收到 ACK 前重传）→ 幂等处理
            if (sid == requester->rpc_sid) {

                // REQ 阶段：立即回复成功 ACK
                if (requester->rpc_state == RPC_STATE_REQ_PENDING) {

                    send_msg_req_ack(udp_fd, from, from_str, requester->session_id, sid, 0);

                    print("V:", LA_F("%s retransmit, resend ACK, sid=%u (ses_id=%" PRIu64 ")\n", 0),
                          PROTO, sid, requester->session_id);
                }
                // RESP 阶段：客户端重传旧请求，忽略（等待 RESP_ACK）
                else {
                    print("V:", LA_F("%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%" PRIu64 ")\n", 0),
                          PROTO, sid, requester->session_id);
                }
                return;
            }

            // 如果 sid 过时
            if (!uint16_circle_newer(sid, requester->rpc_sid)) {
                print("V:", LA_F("%s: obsolete sid=%u (current=%u), ignoring\n", LA_F20, 20),
                      PROTO, sid, requester->rpc_sid);
                return;
            }

            // 如果是新的 sid（客户端中断旧请求发起新请求）→ 取消旧的，接受新请求
            print("I:", LA_F("%s new sid=%u > pending sid=%u (state=%d), canceling old RPC (ses_id=%" PRIu64 ")\n", 0),
                  PROTO, sid, requester->rpc_sid, requester->rpc_state, requester->session_id);
            
            // 清理旧的 RPC 状态（无论是 REQ 还是 RESP）
            remove_rpc_pending(requester);
            requester->rpc_state = RPC_STATE_IDLE;
            
            // 继续处理新请求（下面的代码）
        }

        // IDLE 状态下，验证新 sid 比最后完成的 rpc_sid 更新（rpc_sid=0 时无须检查）
        if (requester->rpc_sid != 0 && !uint16_circle_newer(sid, requester->rpc_sid)) {
            print("V:", LA_F("%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n", LA_F21, 21),
                  PROTO, sid, requester->rpc_sid);
            return;
        }

        // 缓存请求
        requester->rpc_sid = sid;
        requester->rpc_state = RPC_STATE_REQ_PENDING;
        requester->rpc_code = msg;
        requester->rpc_data_len = msg_data_len;
        if (msg_data_len > 0) memcpy(requester->rpc_data, msg_data, msg_data_len);

        // 发送 REQ_ACK status=0（成功，开始中转）
        send_msg_req_ack(udp_fd, from, from_str, requester->session_id, sid, 0);

        // 立即向对端转发 MSG_REQ
        enqueue_rpc_pending(requester, P_tick_ms());
        send_msg_req_to_peer(udp_fd, requester);

        print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->local_peer_id, requester->peer->local_peer_id,
               sid, msg, requester->session_id);
    } break;

    // SIG_PKT_MSG_RESP: B→Server（只能是 B 对 Server relay 的 MSG_REQ 的响应）
    // 格式: [session_id(8)][sid(2)][code(1)][data(N)]，code=响应码
    case SIG_PKT_MSG_RESP: { const char* PROTO = "MSG_RESP";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 11) {  // session_id(8) + sid(2) + code(1) 响应码
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
            return;
        }

        int resp_len = (int)(payload_len - 11);
        if (resp_len > P2P_MSG_DATA_MAX) {
            print("E:", LA_F("%s: data too large (len=%d)\n", LA_F14, 14), PROTO, resp_len);
            return;
        }

        // 解析验证 session_id 和 sid 不为 0
        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu64 " or sid=%u\n", 0, 0), 
                  PROTO, session_id, sid);
            return;
        }
        
        // 根据 session_id 查找 responder（B 端，响应方）
        compact_pair_t *responder = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), responder);        
        if (!responder || !responder->valid) {
            print("W:", LA_F("%s: unknown session_id=%" PRIu64 "\n", 0), PROTO, session_id);
            return;
        }

        if (!PEER_ONLINE(responder)) {
            print("W:", LA_F("%s: peer '%s' not online for session_id=%" PRIu64 "\n", 0, 0),
                  PROTO, responder->remote_peer_id, session_id);
            return;
        }

        // 解析 code、data
        uint8_t resp_code = payload[10];
        const uint8_t *resp_data = payload + 11;

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 ", sid=%u, code=%u, len=%d\n", 0),
              PROTO, session_id, sid, resp_code, resp_len);

        // MSG_RESP 来自响应方（B），检查 B 的地址变更
        check_addr_change(udp_fd, responder, from);

        // 可靠重发机制：无条件立即发送 MSG_RESP_ACK 给响应方（让对端停止重发）
        send_msg_resp_ack_to_responder(udp_fd, from, responder->local_peer_id, responder->session_id, sid);

        // peer 是 requester（A）
        compact_pair_t *requester = responder->peer;

        // 检查是否有匹配的挂起请求（REQ 阶段）
        if (requester->rpc_state != RPC_STATE_REQ_PENDING || requester->rpc_sid != sid) {
            print("W:", LA_F("%s: no matching pending msg (sid=%u, expected=%u)\n", LA_F19, 19),
                  PROTO, sid, requester->rpc_sid);
            return;
        }

        // 缓存响应数据并从 REQ 链表移到 RESP 链表，同时发送给请求方
        transition_to_resp_pending(udp_fd, requester, P_tick_ms(), 0, resp_code, resp_data, resp_len);

        print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
              PROTO, responder->local_peer_id, requester->local_peer_id,
              sid, requester->session_id);
    } break;

    // SIG_PKT_MSG_RESP_ACK: A→Server（A 确认收到 MSG_RESP）
    // 格式: [session_id(8)][sid(2)]
    case SIG_PKT_MSG_RESP_ACK: { const char* PROTO = "MSG_RESP_ACK";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < 10) {  // session_id(8) + sid(2)
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F13, 13), PROTO, payload_len);
            return;
        }

        uint64_t session_id = nget_ll(payload);
        uint16_t sid = nget_s(payload + 8);

        // 验证 session_id 和 sid 不为 0
        if (session_id == 0 || sid == 0) {
            print("E:", LA_F("%s: invalid session_id=%" PRIu64 " or sid=%u\n", 0, 0), 
                   PROTO, session_id, sid);
            return;
        }

        print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 ", sid=%u\n", 0),
               PROTO, session_id, sid);

        // 根据 session_id 查找 requester
        compact_pair_t *requester = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), requester);
        if (!requester || !requester->valid) {
            print("W:", LA_F("%s: unknown session_id=%" PRIu64 "\n", 0), PROTO, session_id);
            return;
        }

        // 检查是否有匹配的挂起响应转发
        if (requester->rpc_state != RPC_STATE_RESP_PENDING || requester->rpc_sid != sid) {
            print("V:", LA_F("%s: no matching pending msg (sid=%u)\n", LA_F18, 18), PROTO, sid);
            return;
        }

        // 清理状态
        remove_rpc_pending(requester);
        requester->rpc_state = RPC_STATE_IDLE;

        print("I:", LA_F("%s: RPC complete for '%s', sid=%u (ses_id=%" PRIu64 ")\n", 0),
               PROTO, requester->local_peer_id, sid, requester->session_id);
    } break;
    
    default:
        print("W:", LA_F("Unknown packet type 0x%02x from %s\n", LA_F36, 36), hdr->type, from_str);
        break;
    } // switch
}

// 清理过期的 COMPACT 模式配对记录
static void cleanup_compact_pairs(sock_t udp_fd) {

    uint64_t now = P_tick_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_compact_pairs[i].valid || 
            (now - g_compact_pairs[i].last_active) <= COMPACT_PAIR_TIMEOUT * 1000) continue;

        print("W:", LA_F("Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n", LA_F35, 35), 
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

        // 从 MSG RPC 链表移除
        if (g_compact_pairs[i].next_rpc_pending) {
            remove_rpc_pending(&g_compact_pairs[i]);
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
        g_compact_pairs[i].rpc_sid = 0;
        g_compact_pairs[i].rpc_state = RPC_STATE_IDLE;
        g_compact_pairs[i].rpc_data_len = 0;
        g_compact_pairs[i].next_rpc_pending = NULL;
        g_compact_pairs[i].valid = false;
    }
}

// 处理 NAT 探测请求
static void handle_probe(sock_t probe_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {

    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    // NAT_PROBE: [hdr(4)] = 4 bytes
    if (len < 4 || buf[0] != SIG_PKT_NAT_PROBE) return;
    const char* PROTO = "NAT_PROBE";

    uint16_t req_seq = ((uint16_t)buf[2] << 8) | buf[3];

    printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F67, 67),
           PROTO, from_str, req_seq, buf[1], len);

    // 构造应答包（NAT_PROBE_ACK）
    // [hdr(4)][probe_ip(4)][probe_port(2)] = 10 bytes
    const char* PROTO_ACK = "NAT_PROBE_ACK";
    buf[0] = SIG_PKT_NAT_PROBE_ACK;
    buf[1] = 0;                                     /* flags */
    buf[2] = (uint8_t)(req_seq >> 8);               /* seq hi (复制请求的 seq) */
    buf[3] = (uint8_t)(req_seq & 0xFF);             /* seq lo */
    memcpy(buf + 4, &from->sin_addr.s_addr, 4);     /* probe_ip   */
    memcpy(buf + 8, &from->sin_port, 2);            /* probe_port */

    print("V:", LA_F("Send %s: mapped=%s:%d\n", LA_F32, 32),
          PROTO_ACK, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    ssize_t n = sendto(probe_fd, (const char *)buf, 10, 0, (struct sockaddr *)from, sizeof(*from));
    if (n != 10)
        print("E:", LA_F("[UDP] %s send to %s failed(%d)\n", LA_F68, 68), PROTO_ACK, from_str, P_sock_errno());
    else
        printf(LA_F("[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n", LA_F69, 69),
               PROTO_ACK, from_str, req_seq, (int)n);
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
            print("I: \n%s\n", LA_S("Received shutdown signal, exiting gracefully...", LA_S8, 8));
            g_running = 0;
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        print("I: \n%s\n", LA_S("Received shutdown signal, exiting gracefully...", LA_S8, 8));
        g_running = 0;
    }
}
#endif

int main(int argc, char *argv[]) {

    LA_init();  /* 注册默认英文字符串表 */

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
            &ARGS_DEF_msg,
            &ARGS_DEF_cn,
            NULL);

        // 处理中文语言
        if (ARGS_cn.i64) {
            FILE *fp = fopen("lang.zh", "r");
            if (fp) { lang_load_fp(LA_RID, fp); fclose(fp); }
        }
    }

    // 获取参数值（设置默认值）
    int port = ARGS_port.i64 ? (int)ARGS_port.i64 : DEFAULT_PORT;

    // 验证端口范围
    if (port <= 0 || port > 65535) {
        print("E:", LA_F("Invalid port number %d (range: 1-65535)\n", LA_F24, 24), port);
        ARGS_print(argv[0]);
        return 1;
    }
    if (ARGS_probe_port.i64 < 0 || ARGS_probe_port.i64 > 65535) {
        print("E:", LA_F("Invalid probe port %d (range: 0-65535)\n", LA_F25, 25), (int)ARGS_probe_port.i64);
        ARGS_print(argv[0]);
        return 1;
    }
    
    if (P_net_init() != E_NONE) {
        print("E: %s", LA_S("net init failed\n", LA_S7, 7));
        return 1;
    }

    // 初始化随机数生成器（用于生成安全的 session_id）
    P_rand_init();

    // 打印服务器配置信息
    print("I:", LA_F("Starting P2P signal server on port %d\n", LA_F34, 34), port);
    print("I:", LA_F("NAT probe: %s (port %d)\n", LA_F27, 27), 
          ARGS_probe_port.i64 > 0 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1), 
          (int)ARGS_probe_port.i64);
    print("I:", LA_F("Relay support: %s\n", LA_F31, 31), 
          ARGS_relay.i64 ? LA_W("enabled", LA_W2, 2) : LA_W("disabled", LA_W1, 1));

    // 注册信号处理
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        print("W:", "%s", LA_S("Failed to set console ctrl handler\n", 0));
    }
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    // 创建 TCP 监听套接字（用于 Relay 信令模式）
    sock_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == P_INVALID_SOCKET) {
        print("E:", "TCP socket failed(%d)\n", P_sock_errno());
        return 1;
    }
    int sockopt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));

    // 创建 UDP 套接字（用于 COMPACT 信令模式）
    sock_t udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == P_INVALID_SOCKET) {
        print("E:", "UDP socket failed(%d)\n", P_sock_errno());
        return 1;
    }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));

    // 创建 NAT 探测 UDP 套接字（可选，仅当配置了 probe_port 时）
    sock_t probe_fd = P_INVALID_SOCKET;
    if (ARGS_probe_port.i64 > 0) {
        probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (probe_fd == P_INVALID_SOCKET) {
            print("E:", "probe UDP socket failed(%d)\n", P_sock_errno());
            return 1;
        }
        setsockopt(probe_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sockopt, sizeof(sockopt));
    }

    // 绑定监听端口（TCP 和 UDP 同一端口）
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
       if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
       
        print("E:", "TCP bind failed(%d)\n", P_sock_errno());
        return 1;
    }
    if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        print("E:", "UDP bind failed(%d)\n", P_sock_errno());
        return 1;
    }

    // 绑定 NAT 探测端口（独立端口，客户端用同一本地端口发包，服务器在此处看到不同映射地址）
    if (probe_fd != P_INVALID_SOCKET) {
        struct sockaddr_in probe_addr = {0};
        probe_addr.sin_family = AF_INET;
        probe_addr.sin_addr.s_addr = INADDR_ANY;
        probe_addr.sin_port = htons((uint16_t)ARGS_probe_port.i64);
        if (bind(probe_fd, (struct sockaddr *)&probe_addr, sizeof(probe_addr)) < 0) {

            print("E:", LA_F("probe UDP bind failed(%d)\n", LA_F75, 75), P_sock_errno());
            P_sock_close(probe_fd);
            probe_fd = P_INVALID_SOCKET;
            ARGS_probe_port.i64 = 0;  /* 绑定失败，禁用探测功能 */
            print("W: %s", LA_S("NAT probe disabled (bind failed)\n", LA_S6, 6));
        } 
        else {
            print("I:", LA_F("NAT probe socket listening on port %d\n", LA_F26, 26), (int)ARGS_probe_port.i64);
        }
    }

    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd, 10);
    print("I:", LA_F("P2P Signaling Server listening on port %d (TCP + UDP)...\n", LA_F28, 28), port);

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
        
        // 检查并重传未确认的 PEER_INFO 包 + MSG RPC 包（每秒检查一次）
        if ((now - last_compact_retry_check) >= COMPACT_RETRY_INTERVAL_MS) {
            if (g_info0_pending_head) retry_info0_pending(udp_fd, now);
            if (g_rpc_pending_head)   retry_rpc_pending(udp_fd, now);
            last_compact_retry_check = now;
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
            print("E:", LA_F("select failed(%d)\n", LA_F76, 76), P_sock_errno());
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
                    print("V:", LA_F("[TCP] New connection from %s:%d\n", LA_F52, 52), 
                          inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    break;
                }
            }
            if (i == MAX_PEERS) {
                print("W: %s", LA_S("[TCP] Max peers reached, rejecting connection\n", LA_S3, 3));
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
            if (n > 0) {
                handle_probe(probe_fd, buf, n, &from);
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
    print("I: \n%s", LA_S("Shutting down...\n", LA_S9, 9));
    
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

    print("I: %s", LA_S("Goodbye!\n", LA_S5, 5));
    return 0;
}
