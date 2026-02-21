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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <inttypes.h>  /* PRIu64 */

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET server_socket_t;
  #define SERVER_INVALID_SOCKET INVALID_SOCKET

  #define server_close_socket(s) closesocket(s)

  // 64-bit network byte order conversion
  static inline uint64_t htonll(uint64_t x) {
    return ((uint64_t)htonl((uint32_t)x) << 32) | htonl((uint32_t)(x >> 32));
  }
  static inline uint64_t ntohll(uint64_t x) {
    return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
  }
#else
  #include <unistd.h>
  #include <signal.h>       /* POSIX 信号处理 */
  #include <errno.h>        /* errno 用于 select EINTR 检查 */
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  typedef int server_socket_t;
  #define SERVER_INVALID_SOCKET (-1)
  #define server_close_socket(s) close(s)
  
  // 64-bit network byte order conversion
  #if defined(__linux__)
    #include <endian.h>
    #define htonll(x) htobe64(x)
    #define ntohll(x) be64toh(x)
  #elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    #include <libkern/OSByteOrder.h>
    // macOS already defines htonll/ntohll in <sys/_endian.h>, no need to redefine
    #ifndef htonll
      #define htonll(x) OSSwapHostToBigInt64(x)
    #endif
    #ifndef ntohll
      #define ntohll(x) OSSwapBigToHostInt64(x)
    #endif
  // Generic fallback
  #else
    static inline uint64_t htonll(uint64_t x) {
      #if __BYTE_ORDER == __LITTLE_ENDIAN
        return ((uint64_t)htonl((uint32_t)x) << 32) | htonl((uint32_t)(x >> 32));
      #else
        return x;
      #endif
    }
    static inline uint64_t ntohll(uint64_t x) {
      #if __BYTE_ORDER == __LITTLE_ENDIAN
        return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
      #else
        return x;
      #endif
    }
  #endif
#endif

#include <p2p.h>
#include <p2pp.h>
#include "../src/p2p_internal.h"
#include "server_lang.h"
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

// RELAY 模式客户端（TCP 长连接）
typedef struct relay_client {
    bool                    valid;                              // 客户端是否有效（无效意味着未分配或已回收）
    char                    name[P2P_PEER_ID_MAX];              // 客户端名称（登录时提供）
    server_socket_t         fd;                                 // 客户端 tcp 套接口描述符
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
    uint64_t                session_id;                         // 会话 ID（服务器分配，唯一标识一个配对，64位随机数）
    char                    local_peer_id[P2P_PEER_ID_MAX];     // 本端 ID
    char                    remote_peer_id[P2P_PEER_ID_MAX];    // 目标对端 ID
    struct sockaddr_in      addr;                               // 公网地址（UDP 源地址）
    p2p_compact_candidate_t candidates[MAX_CANDIDATES];         // 候选列表
    int                     candidate_count;                    // 候选数量
    struct compact_pair*    peer;                               // 指向配对的对端。(void*)-1 表示对端已断开
    time_t                  last_active;                        // 最后活跃时间
    
    // PEER_INFO 首包可靠传输
    int                     info0_acked;                        // 是否已收到首包 ACK，-1 表示未收到确认，但达到了最大重传次数，已放弃重传
    int                     info0_retry;                        // 重传次数
    time_t                  info0_sent_time;                    // 首包发送时间（用于重传）
    struct compact_pair*    next_pending;                       // 待确认链表指针
    
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

// 服务器配置（运行时参数）
static int                  g_probe_port = 0;                   // compact 模式 NAT 探测端口（0=不支持探测）
static bool                 g_relay_enabled = false;            // compact 模式是否支持中继功能

// 随机数源（用于生成安全的 session_id）
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
static FILE*                g_urandom_fp = NULL;                // /dev/urandom 文件句柄（Linux）
#endif

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
            printf("\n%s\n", server_msg(MSG_SERVER_SHUTDOWN_SIGNAL));
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
        printf("\n%s\n", server_msg(MSG_SERVER_SHUTDOWN_SIGNAL));
        fflush(stdout);
        g_running = 0;
    }
}
#endif

// 生成安全的随机 session_id（64位，加密安全，防止跨会话注入攻击）
static uint64_t generate_session_id(void) {
    uint64_t id;
    
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    // macOS/BSD: 使用 arc4random() 生成 64 位
    id = ((uint64_t)arc4random() << 32) | arc4random();
    if (id == 0) id = 1;  // 0 保留为无效值
    
#elif defined(_WIN32)
    // Windows: 使用 rand_s 生成 64 位（加密安全）
    #if defined(_MSC_VER) && _MSC_VER >= 1400
        uint32_t high, low;
        while (rand_s(&high) != 0 || rand_s(&low) != 0) {
            // rand_s 失败，重试
        }
        id = ((uint64_t)high << 32) | low;
        if (id == 0) id = 1;
    #else
        // 降级方案：使用 rand()（不推荐用于生产环境）
        id = ((uint64_t)rand() << 48) ^ ((uint64_t)rand() << 32) ^ 
             ((uint64_t)rand() << 16) ^ (uint64_t)rand();
        if (id == 0) id = 1;
    #endif
    
#else
    // Linux: 使用全局 /dev/urandom 文件句柄读取 64 位
    if (g_urandom_fp && fread(&id, sizeof(id), 1, g_urandom_fp) == 1) {
        if (id == 0) id = 1;  // 0 保留为无效值
    } else {
        // /dev/urandom 不可用或读取失败，使用降级方案
        id = ((uint64_t)time(NULL) << 32) ^ ((uint64_t)getpid() << 16) ^ (uint64_t)clock();
        if (id == 0) id = 1;
    }
#endif

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
    server_socket_t fd = g_relay_clients[idx].fd;

    // 更新最后活跃时间（收到任何数据都表示连接活跃）
    g_relay_clients[idx].last_active = time(NULL);

    size_t n = recv(fd, (char *)&hdr, sizeof(hdr), 0);
    if (n <= 0) {
        printf(server_msg(MSG_TCP_PEER_DISCONNECTED), g_relay_clients[idx].name);
        server_close_socket(fd);
        g_relay_clients[idx].valid = false;
        g_relay_clients[idx].current_peer[0] = '\0';
        return;
    }

    // Debug: print received bytes
    printf(server_msg(MSG_DEBUG_RECEIVED_BYTES),
           (int)n, hdr.magic, hdr.type, hdr.length, P2P_RLY_MAGIC);

    if (hdr.magic != P2P_RLY_MAGIC) {
        printf("%s", server_msg(MSG_TCP_INVALID_MAGIC));
        server_close_socket(fd);
        g_relay_clients[idx].valid = false;
        g_relay_clients[idx].current_peer[0] = '\0';
        return;
    }

    // 用户请求登录
    if (hdr.type == P2P_RLY_LOGIN) {
        p2p_relay_login_t login;
        recv(fd, (char *)&login, sizeof(login), 0);
        strncpy(g_relay_clients[idx].name, login.name, P2P_PEER_ID_MAX);
        g_relay_clients[idx].valid = true;
        g_relay_clients[idx].current_peer[0] = '\0';  // 初始化为无连接状态
        printf(server_msg(MSG_TCP_PEER_LOGIN), g_relay_clients[idx].name);
        fflush(stdout);

        // 发送登录确认
        p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_LOGIN_ACK, 0};
        send(fd, (const char *)&ack, sizeof(ack), 0);

        /* Merge pending candidates from any offline slot with same name.
         * This happens when the active peer sent candidates before this peer
         * connected (offline slot was created to cache them). */
        for (int k = 0; k < MAX_PEERS; k++) {
            if (k == idx) continue;
            if (g_relay_clients[k].valid &&
                g_relay_clients[k].fd == SERVER_INVALID_SOCKET &&
                strcmp(g_relay_clients[k].name, login.name) == 0 &&
                g_relay_clients[k].pending_count > 0) {

                /* Copy pending candidates from offline slot into online slot */
                g_relay_clients[idx].pending_count = g_relay_clients[k].pending_count;
                memcpy(g_relay_clients[idx].pending_candidates,
                       g_relay_clients[k].pending_candidates,
                       g_relay_clients[k].pending_count * sizeof(p2p_candidate_t));
                strncpy(g_relay_clients[idx].pending_sender,
                        g_relay_clients[k].pending_sender, P2P_PEER_ID_MAX);

                printf(server_msg(MSG_TCP_MERGED_PENDING),
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
            
            printf(server_msg(MSG_TCP_FLUSHING_PENDING), 
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
            
            printf(server_msg(MSG_TCP_FORWARDED_OFFER),
                   sender, client->pending_count, (int)n);
            fflush(stdout);
            
            // 清空缓存
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(server_msg(MSG_TCP_PENDING_FLUSHED), client->name);
            fflush(stdout);
        }
        // 检查是否缓存已满（场景5：发送空 OFFER，让对端反向连接）
        else if (g_relay_clients[idx].pending_count == MAX_CANDIDATES && g_relay_clients[idx].pending_sender[0] != '\0') {
            relay_client_t *client = &g_relay_clients[idx];
            const char *sender = client->pending_sender;
            
            printf(server_msg(MSG_TCP_STORAGE_FULL_FLUSH),
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
            
            printf(server_msg(MSG_TCP_SENT_EMPTY_OFFER), sender);
            fflush(stdout);
            
            // 清空缓存满标识
            client->pending_count = 0;
            client->pending_sender[0] = '\0';
            printf(server_msg(MSG_TCP_STORAGE_FULL_FLUSHED), client->name);
            fflush(stdout);
        }
    }
    // 信令转发：P2P_RLY_CONNECT → P2P_RLY_OFFER，P2P_RLY_ANSWER → P2P_RLY_FORWARD
    else if (hdr.type == P2P_RLY_CONNECT || hdr.type == P2P_RLY_ANSWER) {

        // 接收目标对端名称
        char target_name[P2P_PEER_ID_MAX];
        if (recv(fd, target_name, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
            printf(server_msg(MSG_TCP_RECV_TARGET_FAILED), g_relay_clients[idx].name);
            server_close_socket(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        // 接收信令负载数据
        uint32_t payload_len = hdr.length - P2P_PEER_ID_MAX;
        if (payload_len > 65536) {  // 防止过大的负载
            printf(server_msg(MSG_TCP_PAYLOAD_TOO_LARGE), payload_len, g_relay_clients[idx].name);
            server_close_socket(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }
        
        uint8_t *payload = malloc(payload_len);
        if (recv(fd, (char *)payload, payload_len, 0) != (int)payload_len) {
            printf(server_msg(MSG_TCP_RECV_PAYLOAD_FAILED), g_relay_clients[idx].name);
            free(payload);
            server_close_socket(fd);
            g_relay_clients[idx].valid = false;
            g_relay_clients[idx].current_peer[0] = '\0';
            return;
        }

        printf(server_msg(MSG_TCP_RELAYING), 
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
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != SERVER_INVALID_SOCKET &&
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
                printf(server_msg(MSG_TCP_SENT_WITH_CANDS),
                       is_first_offer ? "OFFER" : "FORWARD", candidates_in_payload, 
                       target_name, g_relay_clients[idx].name);
                fflush(stdout);
                break;
            }
        }
        
        if (!found) {
            /* 目标离线：缓存候选 */
            printf(server_msg(MSG_TCP_TARGET_OFFLINE), target_name);
            
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
                        g_relay_clients[i].fd = SERVER_INVALID_SOCKET;  // offline marker
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
                        printf(server_msg(MSG_TCP_NEW_SENDER_REPLACE),
                               g_relay_clients[idx].name, target->pending_sender, target->pending_count);
                    }
                    target->pending_count = 0;
                    strncpy(target->pending_sender, g_relay_clients[idx].name, P2P_PEER_ID_MAX);
                }
                
                for (int i = 0; i < candidates_in_payload; i++) {
                    if (target->pending_count >= MAX_CANDIDATES) {
                        ack_status = 2;  /* 缓存已满 */
                        printf(server_msg(MSG_TCP_STORAGE_FULL_DROP),
                               target_name, candidates_acked, candidates_in_payload - candidates_acked);
                        
                        // 缓存已满时，pending_sender 本身就表示连接意图（不需要额外字段）
                        // 此时 pending_count 保持为 MAX_CANDIDATES，pending_sender 已经记录了发送者
                        printf(server_msg(MSG_TCP_STORAGE_INTENT_NOTED),
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
                        printf(server_msg(MSG_TCP_CACHED_FULL),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    } else {
                        ack_status = 1;  /* 已缓存，还有剩余空间 */
                        printf(server_msg(MSG_TCP_CACHED_PARTIAL),
                               candidates_acked, target_name, target->pending_count, MAX_CANDIDATES);
                    }
                    fflush(stdout);
                }
            } else {
                ack_status = 2;  /* 无法分配槽位 */
                candidates_acked = 0;
                printf(server_msg(MSG_TCP_CANNOT_ALLOC_SLOT), target_name);
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
                printf(server_msg(MSG_TCP_SEND_ACK_FAILED),
                       g_relay_clients[idx].name, (int)sent1, (int)sent2);
            } else {
                printf(server_msg(MSG_TCP_SENT_CONNECT_ACK), 
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
                    printf("%s", server_msg(MSG_TCP_LIST_TRUNCATED));
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
        printf(server_msg(MSG_TCP_UNKNOWN_MSG_TYPE), hdr.type, g_relay_clients[idx].name);
    }
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_relay_clients[i].valid || 
            (now - g_relay_clients[i].last_active) <= RELAY_CLIENT_TIMEOUT) continue;

        printf(server_msg(MSG_TCP_CLIENT_TIMEOUT), 
               g_relay_clients[i].name, (long)(now - g_relay_clients[i].last_active));
        
        server_close_socket(g_relay_clients[i].fd);
        g_relay_clients[i].fd = SERVER_INVALID_SOCKET;
        g_relay_clients[i].current_peer[0] = '\0';
        g_relay_clients[i].valid = false;
    }
}

// 检查并重传未确认的 PEER_INFO 包
static void retry_compact_pending(server_socket_t udp_fd, time_t now) {

    for(;;) {

        // 队列按时间排序，一旦遇到未超时的节点，后面都不会超时
        if (now - g_pending_connecting_head->info0_sent_time < PEER_INFO0_RETRY_INTERVAL) {
            return;
        }

        // 将第一项移除
        compact_pair_t *q = g_pending_connecting_head;
        g_pending_connecting_head = q->next_pending;

        // 检查是否超过最大重传次数
        if (q->info0_retry >= PEER_INFO0_MAX_RETRY) {
            
            // 超过最大重传次数，从链表移除（放弃）
            printf(server_msg(MSG_UDP_PEER_INFO_RETRANSMIT_FAIL),
                   q->local_peer_id, q->remote_peer_id, q->info0_retry);

            q->next_pending = NULL;
            q->info0_acked = -1;  // 标记为已放弃

            // 如果这是最后一项
            if (g_pending_connecting_head == (void*)-1) {
                g_pending_connecting_head = NULL;
                g_pending_connecting_rear = NULL;
                return;
            }
        }
        // 重传
        else { assert(q->peer && q->peer != (compact_pair_t*)(void*)-1);

            // 构造 SIG_PKT_PEER_INFO 响应（序列化传输首包 seq=0）
            // 格式: [hdr(4)][session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
            uint8_t response[4 + 8 + 2 + MAX_CANDIDATES * 7];
            p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)response;
            resp_hdr->type = SIG_PKT_PEER_INFO;
            resp_hdr->flags = 0;
            resp_hdr->seq = htons(0);  // seq=0 表示服务器发送的首包

            // session_id (网络字节序)
            uint64_t session_id_net = htonll(q->session_id);
            memcpy(response + 4, &session_id_net, 8);

            response[12] = 0;  // base_index = 0 (从第一个候选开始)
            response[13] = (uint8_t)q->peer->candidate_count;
            int resp_len = 14;
            for (int i = 0; i < q->peer->candidate_count; i++) {
                response[resp_len] = q->peer->candidates[i].type;
                memcpy(response + resp_len + 1, &q->peer->candidates[i].ip, 4);
                memcpy(response + resp_len + 5, &q->peer->candidates[i].port, 2);
                resp_len += 7;
            }

            sendto(udp_fd, (const char *)response, resp_len, 0, (struct sockaddr *)&q->addr, sizeof(q->addr));

            // 更新时间和重传次数
            q->info0_retry++;
            q->info0_sent_time = now;

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

            printf(server_msg(MSG_UDP_PEER_INFO_RETRANSMIT),
                   q->session_id, q->local_peer_id, q->remote_peer_id,
                   q->info0_retry, PEER_INFO0_MAX_RETRY);
            fflush(stdout);

            if (g_pending_connecting_head == q) return;
        }
    }
}

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

// 处理 COMPACT 模式信令（UDP 无状态，对应 p2p_signal_compact 模块）
static void handle_compact_signaling(server_socket_t udp_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {
    
    if (len < 4) return;  // 至少需要包头
    
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4; size_t payload_len = len - 4;
    
    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // SIG_PKT_REGISTER: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
    if (hdr->type == SIG_PKT_REGISTER) {

        if (payload_len <= P2P_PEER_ID_MAX * 2) {
            printf(server_msg(MSG_UDP_REGISTER_INVALID), from_str);
            return;
        }

        // 解析 local_peer_id 和 remote_peer_id
        char local_peer_id[P2P_PEER_ID_MAX + 1] = {0}, remote_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        memcpy(local_peer_id, payload, P2P_PEER_ID_MAX); local_peer_id[P2P_PEER_ID_MAX] = '\0';     // 确保字符串以 \0 结尾
        memcpy(remote_peer_id, payload + P2P_PEER_ID_MAX, P2P_PEER_ID_MAX); remote_peer_id[P2P_PEER_ID_MAX] = '\0';

        // 解析候选列表（新格式）
        int candidate_count = 0;
        p2p_compact_candidate_t candidates[MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));

        // 读取候选数量（1 字节，紧跟在两个 peer_id 之后）
        size_t cand_offset = P2P_PEER_ID_MAX * 2;
        candidate_count = payload[cand_offset];
        if (candidate_count > MAX_CANDIDATES) {
            candidate_count = MAX_CANDIDATES;
        }
        cand_offset++;

        // 解析候选列表 (7 字节: type + ip + port)
        for (int i = 0; i < candidate_count && cand_offset + 7 <= payload_len; i++) {
            candidates[i].type = payload[cand_offset];
            memcpy(&candidates[i].ip, payload + cand_offset + 1, sizeof(uint32_t));
            memcpy(&candidates[i].port, payload + cand_offset + 5, sizeof(uint16_t));
            cand_offset += 7;
        }

        printf(server_msg(MSG_UDP_REGISTER), from_str, local_peer_id, remote_peer_id, candidate_count);
        for (int i = 0; i < candidate_count; i++) {
            struct in_addr ip; ip.s_addr = candidates[i].ip;
            printf(server_msg(MSG_UDP_CANDIDATE_INFO), i, candidates[i].type,
                   inet_ntoa(ip), ntohs(candidates[i].port));
        }
        fflush(stdout);
        
        // 查找本端槽位：直接用 hash 查找（O(1)），payload 前 64 字节是 [local_peer_id(32)][remote_peer_id(32)]
        compact_pair_t *existing = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, existing);
        
        int local_idx = -1;
        if (existing) {
            local_idx = (int)(existing - g_compact_pairs);  // 计算数组下标
        }
        
        // 如果配对不存在，分配一个空位
        if (local_idx == -1) {
            for (int i = 0; i < MAX_PEERS; i++) {
                if (!g_compact_pairs[i].valid) { 
                    g_compact_pairs[i].valid = true;
                    local_idx = i;
                    
                    // 注意：session_id 在首次匹配成功时才分配，此时为 0
                    g_compact_pairs[i].session_id = 0;
                    
                    strncpy(g_compact_pairs[i].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
                    strncpy(g_compact_pairs[i].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
                    g_compact_pairs[i].peer = NULL;
                    g_compact_pairs[i].info0_acked = false;
                    g_compact_pairs[i].info0_sent_time = 0;
                    g_compact_pairs[i].info0_retry = 0;
                    g_compact_pairs[i].next_pending = NULL;
                    
                    // 添加到 peer_key 索引（session_id 索引在首次匹配时添加）
                    HASH_ADD(hh_peer, g_pairs_by_peer, local_peer_id, P2P_PEER_ID_MAX * 2, &g_compact_pairs[i]);
                    break;
                }
            }
        }
        // 无法分配槽位，发送错误 ACK
        if (local_idx < 0) {

            uint8_t ack_response[14];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = SIG_PKT_REGISTER_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            ack_response[4] = 2;                /* status = error */
            memset(ack_response + 5, 0, 9);     /* 其余字段全部置 0 */

            sendto(udp_fd, (const char *)ack_response, 14, 0, (struct sockaddr *)from, sizeof(*from));

            printf(server_msg(MSG_UDP_REGISTER_ACK_ERROR), from_str);
            fflush(stdout);
            return;
        }

        compact_pair_t *local = &g_compact_pairs[local_idx];

        // 检测地址是否变化
        bool addr_changed = false;
        if (local->valid) {
            addr_changed = memcmp(&local->addr, from, sizeof(*from)) != 0;
            local->addr = *from;
        }

        // 记录本端的候选列表
        local->candidate_count = candidate_count;
        if (candidate_count) {
            memcpy(local->candidates, candidates, sizeof(p2p_compact_candidate_t) * candidate_count);
        }

        // 查找反向配对
        int remote_idx = -1;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_compact_pairs[i].valid &&
                strcmp(g_compact_pairs[i].local_peer_id, remote_peer_id) == 0 &&
                strcmp(g_compact_pairs[i].remote_peer_id, local_peer_id) == 0) {
                remote_idx = i;
                break;
            }
        }

        // 如果之前是（自己之前是已连接过的）对端断开连接，重新连接时重置对端状态
        if (local->peer == (compact_pair_t*)(void*)-1) {
            local->peer = NULL;
        }

        local->last_active = time(NULL);

        // 构造并发送 REGISTER_ACK
        // 格式: [hdr(4)][status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 14字节
        {
            uint8_t ack_response[14];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = SIG_PKT_REGISTER_ACK;
            ack_hdr->flags = g_relay_enabled ? SIG_REGACK_FLAG_RELAY : 0;
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
            
            sendto(udp_fd, (const char *)ack_response, 14, 0, (struct sockaddr *)from, sizeof(*from));

            // register ack 无需提供确认重发机制，因为客户端会在收到 ACK 前一直重试注册请求
            printf(server_msg(MSG_UDP_REGISTER_ACK_OK),
                   from_str, (remote_idx >= 0) ? 1 : 0, MAX_CANDIDATES,
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
                    printf(server_msg(MSG_UDP_SESSION_ASSIGNED),
                           local->session_id, local->local_peer_id, local->remote_peer_id);
                    fflush(stdout);
                }
                if (remote->session_id == 0) {
                    remote->session_id = generate_session_id();
                    HASH_ADD(hh, g_pairs_by_session, session_id, sizeof(uint64_t), remote);
                    printf(server_msg(MSG_UDP_SESSION_ASSIGNED),
                           remote->session_id, remote->local_peer_id, remote->remote_peer_id);
                    fflush(stdout);
                }

                // 构造 SIG_PKT_PEER_INFO 响应（序列化传输首包 seq=0）
                // 格式: [hdr(4)][session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
                uint8_t response[4 + 8 + 2 + MAX_CANDIDATES * 7];
                p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)response;
                resp_hdr->type = SIG_PKT_PEER_INFO;
                resp_hdr->flags = 0;
                resp_hdr->seq = htons(0);  /* seq=0 表示服务器发送的首包 */

                //-------------

                // 向当前请求方发送对方的候选列表（包含 local 的 session_id）
                uint64_t session_id_net = htonll(local->session_id);
                memcpy(response + 4, &session_id_net, 8);
                
                response[12] = 0;  /* base_index = 0 (从第一个候选开始) */
                response[13] = (uint8_t)remote->candidate_count;
                int resp_len = 14;
                for (int i = 0; i < remote->candidate_count; i++) {
                    response[resp_len] = remote->candidates[i].type;
                    memcpy(response + resp_len + 1, &remote->candidates[i].ip, 4);
                    memcpy(response + resp_len + 5, &remote->candidates[i].port, 2);
                    resp_len += 7;
                }

                sendto(udp_fd, (const char *)response, resp_len, 0, (struct sockaddr *)from, sizeof(*from));

                // 添加到待确认队列的尾部
                local->info0_sent_time = local->last_active;
                local->next_pending = (void*)-1;
                if (g_pending_connecting_rear) {
                    g_pending_connecting_rear->next_pending = local;
                    g_pending_connecting_rear = local;
                } else {
                    g_pending_connecting_head = local;
                    g_pending_connecting_rear = local;
                }

                //-------------

                // 向对端发送当前请求方的候选列表（包含 remote 的 session_id）
                session_id_net = htonll(remote->session_id);
                memcpy(response + 4, &session_id_net, 8);
                
                response[12] = 0;  /* base_index = 0 (从第一个候选开始) */
                response[13] = (uint8_t)local->candidate_count;
                resp_len = 14;
                for (int i = 0; i < local->candidate_count; i++) {
                    response[resp_len] = local->candidates[i].type;
                    memcpy(response + resp_len + 1, &local->candidates[i].ip, 4);
                    memcpy(response + resp_len + 5, &local->candidates[i].port, 2);
                    resp_len += 7;
                }

                sendto(udp_fd, (const char *)response, resp_len, 0, (struct sockaddr *)&remote->addr, sizeof(remote->addr));

                // 添加到待确认队列
                remote->info0_sent_time = local->last_active;
                remote->next_pending = (void*)-1;
                if (g_pending_connecting_rear) {
                    g_pending_connecting_rear->next_pending = remote;
                    g_pending_connecting_rear = remote;
                } else {
                    g_pending_connecting_head = remote;
                    g_pending_connecting_rear = remote;
                }

                //-------------

                printf(server_msg(MSG_UDP_SENT_PEER_INFO),
                       local_peer_id, remote->candidate_count,
                       remote_peer_id, local->candidate_count);
                fflush(stdout);
            }
            else { assert(local->peer == remote && remote->peer == local);

                // 如果公网地址发生变化，todo 如何处理?
                if (addr_changed) {

                    // 只有对方确认收到服务器发送的首个 PEER_INFO 包后，才通知对方地址变化
                    // + 否则多个 info0 包，对方无法知道哪个是最新的地址
                    if (remote->info0_acked) {

                        printf(server_msg(MSG_UDP_SENT_PEER_INFO_ADDR),
                               inet_ntoa(remote->addr.sin_addr), ntohs(remote->addr.sin_port),
                               remote_peer_id, local->candidate_count, " [ADDR_CHANGED]");
                        fflush(stdout);
                    }
                }
            }
        } else {
            printf(server_msg(MSG_UDP_TARGET_NOT_FOUND), remote_peer_id, local_peer_id);
            fflush(stdout);
        }
    }

    // SIG_PKT_UNREGISTER: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端主动断开时发送，请求服务器立即释放配对槽位。
    // 【服务端可选实现】如果不处理此包类型，客户端自动降级为 COMPACT_PAIR_TIMEOUT 超时清除机制。
    else if (hdr->type == SIG_PKT_UNREGISTER) {

        if (payload_len < P2P_PEER_ID_MAX * 2) {
            printf(server_msg(MSG_UDP_UNREGISTER_INVALID), from_str);
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

            printf(server_msg(MSG_UDP_UNREGISTER), local_peer_id, remote_peer_id);
            fflush(stdout);

            // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
            if (pair->peer != NULL && pair->peer != (compact_pair_t*)(void*)-1 && pair->peer->session_id != 0) {

                uint8_t notify[4 + 8];  // 包头 + session_id
                p2p_packet_hdr_t *notify_hdr = (p2p_packet_hdr_t *)notify;
                notify_hdr->type = SIG_PKT_PEER_OFF;
                notify_hdr->flags = 0;
                notify_hdr->seq = htons(0);
                
                uint64_t peer_session_id_net = htonll(pair->peer->session_id);
                memcpy(notify + 4, &peer_session_id_net, 8);
                
                sendto(udp_fd, (const char *)notify, 12, 0, (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                printf(server_msg(MSG_UDP_PEER_OFF_SENT), pair->peer->local_peer_id, pair->peer->session_id, " [unregister]");
                fflush(stdout);
                
                // 标记对端槽位已断开
                pair->peer->peer = (compact_pair_t*)(void*)-1;
            }
            
            // 从待确认链表移除
            if (pair->next_pending) {
                remove_compact_pending(pair);
            }

            // 从哈希表删除
            if (pair->session_id != 0) {
                HASH_DELETE(hh, g_pairs_by_session, pair);
            }
            HASH_DELETE(hh_peer, g_pairs_by_peer, pair);

            pair->valid = false;
            pair->session_id = 0;
            pair->peer = NULL;
        }
    }
    // SIG_PKT_ALIVE: [local_peer_id(32)][remote_peer_id(32)]
    // 客户端定期发送以保持槽位活跃，更新 last_active 时间
    else if (hdr->type == SIG_PKT_ALIVE) {

        if (payload_len < P2P_PEER_ID_MAX * 2) return;

        // 直接用 hash 查找（O(1)）
        compact_pair_t *pair = NULL;
        HASH_FIND(hh_peer, g_pairs_by_peer, payload, P2P_PEER_ID_MAX * 2, pair);
        
        if (pair && pair->valid) {
            pair->last_active = time(NULL);

            // 发送 ALIVE_ACK（仅包头，无 payload）
            uint8_t ack[4];
            p2p_pkt_hdr_encode(ack, SIG_PKT_ALIVE_ACK, 0, 0);
            sendto(udp_fd, (const char *)ack, 4, 0, (struct sockaddr *)from, sizeof(*from));
        }
    }

    // SIG_PKT_PEER_INFO_ACK: ACK 确认收到 PEER_INFO 包
    // 格式: [hdr(4)][session_id(8)][ack_seq(2)]
    else if (hdr->type == SIG_PKT_PEER_INFO_ACK) {

        if (payload_len < 10) {
            printf(server_msg(MSG_UDP_PEER_INFO_ACK_INVALID), from_str, payload_len);
            fflush(stdout);
            return;
        }

        uint64_t session_id = ntohll(*(uint64_t*)payload);
        uint16_t ack_seq = ntohs(*(uint16_t*)(payload + 8));

        // ack_seq=0 的 ACK 是对服务器发送的首个 PEER_INFO 的确认，服务器需要处理
        if (ack_seq == 0) {
            // 通过 session_id 查找对应的配对记录
            compact_pair_t *pair = NULL;
            HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
            if (pair && pair->valid) {
                // 标记为已确认，从待确认链表移除
                if (!pair->info0_acked) {
                    pair->info0_acked = true;

                    // 从待确认链表移除
                    if (pair->next_pending) {
                        remove_compact_pending(pair);
                    }
                    
                    printf(server_msg(MSG_UDP_PEER_INFO_ACK_CONFIRMED),
                           session_id, pair->local_peer_id, pair->remote_peer_id, pair->info0_retry);
                }
            }
            else printf(server_msg(MSG_UDP_PEER_INFO_ACK_UNKNOWN), session_id, from_str);
        }
        // ack_seq≠0 的 ACK 是客户端之间的确认，服务器只负责 relay 转发
        else {
            compact_pair_t *pair = NULL;
            HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
            if (pair && pair->valid && pair->peer && pair->peer != (compact_pair_t*)(void*)-1) {
                // 转发给对方
                sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                       (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));
                
                printf(server_msg(MSG_UDP_PEER_INFO_ACK_RELAYED),
                       ack_seq, session_id, pair->local_peer_id, pair->remote_peer_id);
            }
            else printf(server_msg(MSG_UDP_PEER_INFO_ACK_RELAY_FAIL), session_id);
        }
        fflush(stdout);
    }
    // SIG_PKT_PEER_INFO/P2P_PKT_RELAY_DATA/P2P_PKT_RELAY_ACK: relay 转发给对方
    // 格式：所有包都包含 session_id(4) 在 payload 开头
    else if (hdr->type == SIG_PKT_PEER_INFO ||
             hdr->type == P2P_PKT_RELAY_DATA || hdr->type == P2P_PKT_RELAY_ACK) {

        // SIG_PKT_PEER_INFO 特殊处理：seq=0 是服务器维护的包，不应该出现在这里
        if (hdr->type == SIG_PKT_PEER_INFO && hdr->seq == 0) {
            printf(server_msg(MSG_UDP_RELAY_INVALID_SRC), from_str);
            fflush(stdout);
            return;
        }

        // 所有需要 relay 的包格式都是 [session_id(8)][...]
        if (payload_len < 8) {
            printf(server_msg(MSG_UDP_RELAY_PKT_INVALID), hdr->type, from_str, payload_len);
            fflush(stdout);
            return;
        }

        uint64_t session_id = ntohll(*(uint64_t*)payload);

        // 根据 session_id 查找配对记录
        compact_pair_t *pair = NULL;
        HASH_FIND(hh, g_pairs_by_session, &session_id, sizeof(uint64_t), pair);
        if (!pair || !pair->valid) {
            printf(server_msg(MSG_UDP_RELAY_UNKNOWN_SESSION), hdr->type, session_id, from_str);
            fflush(stdout);
            return;
        }

        // 对方不存在，丢弃
        if (!pair->peer || pair->peer == (compact_pair_t*)(void*)-1) {
            printf(server_msg(MSG_UDP_RELAY_NO_PEER), hdr->type, session_id);
            fflush(stdout);
            return;
        }

        // 转发给对方
        sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
               (struct sockaddr *)&pair->peer->addr, sizeof(pair->peer->addr));

        if (hdr->type == SIG_PKT_PEER_INFO) {
            printf(server_msg(MSG_UDP_RELAY_PEER_INFO),
                   ntohs(hdr->seq), session_id, pair->local_peer_id, pair->remote_peer_id);
        } else if (hdr->type == P2P_PKT_RELAY_DATA) {
            printf(server_msg(MSG_UDP_RELAY_DATA),
                   ntohs(hdr->seq), session_id, pair->local_peer_id, pair->remote_peer_id);
        } else {
            printf(server_msg(MSG_UDP_RELAY_ACK),
                   session_id, pair->local_peer_id, pair->remote_peer_id);
        }
        fflush(stdout);
    }
    else {
        printf(server_msg(MSG_UDP_UNKNOWN_SIG), from_str, hdr->type);
        fflush(stdout);
    }
}

// 清理过期的 COMPACT 模式配对记录
static void cleanup_compact_pairs(server_socket_t udp_fd) {

    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_compact_pairs[i].valid || 
            (now - g_compact_pairs[i].last_active) <= COMPACT_PAIR_TIMEOUT) continue;

        printf(server_msg(MSG_UDP_PAIR_TIMEOUT), 
               g_compact_pairs[i].local_peer_id, g_compact_pairs[i].remote_peer_id);
        
        // 向对端发送 PEER_OFF 通知（如果对端在线且有 session_id）
        if (g_compact_pairs[i].peer != NULL && 
            g_compact_pairs[i].peer != (compact_pair_t*)(void*)-1 && 
            g_compact_pairs[i].peer->session_id != 0) {
            
            uint8_t notify[4 + 8];  // 包头 + session_id
            p2p_packet_hdr_t *notify_hdr = (p2p_packet_hdr_t *)notify;
            notify_hdr->type = SIG_PKT_PEER_OFF;
            notify_hdr->flags = 0;
            notify_hdr->seq = htons(0);
            
            uint64_t peer_session_id_net = htonll(g_compact_pairs[i].peer->session_id);
            memcpy(notify + 4, &peer_session_id_net, 8);
            
            sendto(udp_fd, (const char *)notify, 12, 0,
                   (struct sockaddr *)&g_compact_pairs[i].peer->addr, 
                   sizeof(g_compact_pairs[i].peer->addr));
            
            printf(server_msg(MSG_UDP_PEER_OFF_SENT),
                   g_compact_pairs[i].peer->local_peer_id, g_compact_pairs[i].peer->session_id, " [timeout]");
            fflush(stdout);
            
            // 标记对端槽位已断开
            g_compact_pairs[i].peer->peer = (compact_pair_t*)(void*)-1;
        }
        
        // 从待确认链表移除
        if (g_compact_pairs[i].next_pending) {
            remove_compact_pending(&g_compact_pairs[i]);
        }

        // 从哈希表删除
        if (g_compact_pairs[i].session_id != 0) {
            HASH_DELETE(hh, g_pairs_by_session, &g_compact_pairs[i]);
        }
        HASH_DELETE(hh_peer, g_pairs_by_peer, &g_compact_pairs[i]);

        g_compact_pairs[i].peer = NULL;  // 清空指针
        g_compact_pairs[i].session_id = 0;
        g_compact_pairs[i].valid = false;
    }
}

///////////////////////////////////////////////////////////////////////////////

static void print_usage(const char *prog) {
    printf(server_msg(MSG_SERVER_USAGE), prog);
    printf("\n\n");
    printf("%s\n", server_msg(MSG_SERVER_PARAMS));
    printf("%s\n", server_msg(MSG_SERVER_PARAM_PORT));
    printf("%s\n", server_msg(MSG_SERVER_PARAM_PORT_TCP));
    printf("%s\n", server_msg(MSG_SERVER_PARAM_PORT_UDP));
    printf("%s\n", server_msg(MSG_SERVER_PARAM_PROBE));
    printf("%s\n", server_msg(MSG_SERVER_PARAM_PROBE_DESC));
    printf("%s\n", server_msg(MSG_SERVER_PARAM_RELAY));
    printf("\n%s\n", server_msg(MSG_SERVER_EXAMPLES));
    printf(server_msg(MSG_SERVER_EXAMPLE_DEFAULT), prog);
    printf("\n");
    printf(server_msg(MSG_SERVER_EXAMPLE_PORT), prog);
    printf("\n");
    printf(server_msg(MSG_SERVER_EXAMPLE_PROBE), prog);
    printf("\n");
    printf(server_msg(MSG_SERVER_EXAMPLE_RELAY), prog);
    printf("\n\n");
}

int main(int argc, char *argv[]) {

    // 中文语言支持（需要在--help检查之前处理）
    bool help = false; int opt = 0;
    for (int i = 1; i < argc;) {

        if (strcmp(argv[i], "--cn") == 0) {
            server_set_language(P2P_LANG_ZH);

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
        fprintf(stderr, server_msg(MSG_SERVER_ERR_UNKNOWN_OPT), argv[1]);
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
            fprintf(stderr, server_msg(MSG_SERVER_ERR_INVALID_PORT), argv[1]);
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
            fprintf(stderr, server_msg(MSG_SERVER_ERR_INVALID_PROBE), argv[2]);
            fprintf(stderr, "\n\n");
            print_usage(argv[0]);
            return 1;
        }
        g_probe_port = (int)val;
    }

    // 第四个位置参数：是否启用数据 Relay（中继）模式
    if (argc > 3) {
        if (strcmp(argv[3], "relay") != 0) {
            fprintf(stderr, server_msg(MSG_SERVER_ERR_UNKNOWN_OPT), argv[3]);
            fprintf(stderr, "\n\n");
            print_usage(argv[0]);
            return 1;
        }
        g_relay_enabled = true;
    }
    
    if (argc > 4) {
        fprintf(stderr, "%s\n\n", server_msg(MSG_SERVER_ERR_TOO_MANY));
        print_usage(argv[0]);
        return 1;
    }
    
    // 初始化随机数源（用于生成安全的 session_id）
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    // Linux: 打开 /dev/urandom 文件句柄（保持打开，避免重复 open/close）
    g_urandom_fp = fopen("/dev/urandom", "rb");
    if (!g_urandom_fp) {
        fprintf(stderr, "%s", server_msg(MSG_SERVER_URANDOM_WARN));
    }
#endif

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "%s", server_msg(MSG_SERVER_WINSOCK_ERR));
        return 1;
    }
#endif

    // 打印服务器配置信息
    printf(server_msg(MSG_SERVER_STARTING), port);
    printf("\n");
    printf(server_msg(MSG_SERVER_NAT_PROBE), 
           g_probe_port > 0 ? server_msg(MSG_SERVER_ENABLED) : server_msg(MSG_SERVER_DISABLED), 
           g_probe_port);
    printf("\n");
    printf(server_msg(MSG_SERVER_RELAY_SUPPORT), 
           g_relay_enabled ? server_msg(MSG_SERVER_ENABLED) : server_msg(MSG_SERVER_DISABLED));
    printf("\n");
    fflush(stdout);

    // 注册信号处理
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        fprintf(stderr, "%s", server_msg(MSG_SERVER_WIN_CTRL_HANDLER_ERR));
    }
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    // 创建 TCP 监听套接字（用于 Relay 信令模式）
    server_socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == SERVER_INVALID_SOCKET) {
        perror("TCP socket");
        return 1;
    }
    opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // 创建 UDP 套接字（用于 COMPACT 信令模式）
    server_socket_t udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == SERVER_INVALID_SOCKET) {
        perror("UDP socket");
        return 1;
    }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // 创建 NAT 探测 UDP 套接字（可选，仅当配置了 probe_port 时）
    server_socket_t probe_fd = SERVER_INVALID_SOCKET;
    if (g_probe_port > 0) {
        probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (probe_fd == SERVER_INVALID_SOCKET) {
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
    if (probe_fd != SERVER_INVALID_SOCKET) {
        struct sockaddr_in probe_addr = {0};
        probe_addr.sin_family = AF_INET;
        probe_addr.sin_addr.s_addr = INADDR_ANY;
        probe_addr.sin_port = htons((unsigned short)g_probe_port);
        if (bind(probe_fd, (struct sockaddr *)&probe_addr, sizeof(probe_addr)) < 0) {
            perror("probe UDP bind");
            server_close_socket(probe_fd);
            probe_fd = SERVER_INVALID_SOCKET;
            g_probe_port = 0;  /* 绑定失败，禁用探测功能 */
            printf("%s\n", server_msg(MSG_SERVER_PROBE_BIND_FAILED));
        } else {
            printf(server_msg(MSG_SERVER_PROBE_LISTENING), g_probe_port);
            printf("\n");
        }
        fflush(stdout);
    }

    // 启动 TCP 监听（用于 Relay 模式与客户端连接）
    listen(listen_fd, 10);
    printf(server_msg(MSG_SERVER_LISTENING), port);
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

        // 设置要监听的套接口 fd
        // + TCP listen + TCP clients + UDP + probe UDP + 客户端...
        // + max_fd 必须是所有监听套接字中数值最大的那个（Windows 不使用此值，但 POSIX 需要正确设置）
        int max_fd = 0;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        FD_SET(udp_fd, &read_fds);
        if (probe_fd != SERVER_INVALID_SOCKET) FD_SET(probe_fd, &read_fds);
#ifndef _WIN32
        max_fd = (int)((listen_fd > udp_fd) ? listen_fd : udp_fd);
        if (probe_fd != SERVER_INVALID_SOCKET && (int)probe_fd > max_fd) max_fd = (int)probe_fd;
#endif
        // 添加有效的 TCP 客户端套接字到监听集合中
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != SERVER_INVALID_SOCKET) {
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
            server_socket_t client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            
            int i = 0;
            for (i = 0; i < MAX_PEERS; i++) {

                // 查找一个空闲槽位来存储这个新的连接
                if (!g_relay_clients[i].valid) { g_relay_clients[i].valid = true;
                    g_relay_clients[i].fd = client_fd;
                    g_relay_clients[i].last_active = time(NULL);
                    g_relay_clients[i].pending_count = 0;
                    g_relay_clients[i].pending_sender[0] = '\0';
                    strncpy(g_relay_clients[i].name, "unknown", P2P_PEER_ID_MAX);
                    printf(server_msg(MSG_TCP_NEW_CONNECTION), 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    fflush(stdout);
                    break;
                }
            }
            if (i == MAX_PEERS) {
                printf("%s", server_msg(MSG_TCP_MAX_PEERS));
                server_close_socket(client_fd);
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
        if (probe_fd != SERVER_INVALID_SOCKET && FD_ISSET(probe_fd, &read_fds)) {

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

                printf(server_msg(MSG_PROBE_ACK),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port),
                       req_seq,
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                fflush(stdout);
            }
        }

        // 处理 Relay 模式的信令交互（TCP 连接），包括连接请求、候选交换、在线列表查询等
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid &&
                g_relay_clients[i].fd != SERVER_INVALID_SOCKET &&
                FD_ISSET(g_relay_clients[i].fd, &read_fds)) {
                handle_relay_signaling(i);
            }
        }

    } // while (g_running)

    // 清理资源
    printf("\n%s\n", server_msg(MSG_SERVER_SHUTTING_DOWN));
    
    // 关闭所有客户端连接
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_relay_clients[i].valid && g_relay_clients[i].fd != SERVER_INVALID_SOCKET) {
            server_close_socket(g_relay_clients[i].fd);
        }
    }
    
    // 关闭监听套接字
    server_close_socket(listen_fd);
    server_close_socket(udp_fd);
    if (probe_fd != SERVER_INVALID_SOCKET) server_close_socket(probe_fd);
    
#ifdef _WIN32
    WSACleanup();
#endif

    // 关闭随机数源
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    if (g_urandom_fp) {
        fclose(g_urandom_fp);
    }
#endif
    
    printf("%s\n", server_msg(MSG_SERVER_GOODBYE));
    return 0;
}
