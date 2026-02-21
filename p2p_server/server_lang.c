/*
 * p2p_server 多语言实现
 */
#include "server_lang.h"
#include <stddef.h>

/* 当前语言 */
static p2p_language_t current_language = P2P_LANG_EN;

/* 英文词表 */
static const char* messages_en[MSG_SERVER_COUNT] = {
    [MSG_SERVER_USAGE]           = "Usage: %s [port] [probe_port] [relay]",
    [MSG_SERVER_PARAMS]          = "Parameters:",
    [MSG_SERVER_PARAM_PORT]      = "  port         Signaling server listen port (default: 8888)",
    [MSG_SERVER_PARAM_PORT_TCP]  = "               - TCP: RELAY mode signaling (stateful/long connection)",
    [MSG_SERVER_PARAM_PORT_UDP]  = "               - UDP: COMPACT mode signaling (stateless)",
    [MSG_SERVER_PARAM_PROBE]     = "  probe_port   NAT type detection port (default: 0=disabled)",
    [MSG_SERVER_PARAM_PROBE_DESC]= "               Used to detect symmetric NAT (port consistency)",
    [MSG_SERVER_PARAM_RELAY]     = "  relay        Enable data relay support (COMPACT mode fallback)",
    [MSG_SERVER_EXAMPLES]        = "Examples:",
    [MSG_SERVER_EXAMPLE_DEFAULT] = "  %s                    # Default config (port 8888, no probe, no relay)",
    [MSG_SERVER_EXAMPLE_PORT]    = "  %s 9000               # Listen on port 9000",
    [MSG_SERVER_EXAMPLE_PROBE]   = "  %s 9000 9001          # Listen 9000, probe port 9001",
    [MSG_SERVER_EXAMPLE_RELAY]   = "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay",
    [MSG_SERVER_ERR_INVALID_PORT]= "Error: Invalid port number '%s' (range: 1-65535)",
    [MSG_SERVER_ERR_INVALID_PROBE]="Error: Invalid probe port '%s' (range: 0-65535)",
    [MSG_SERVER_ERR_UNKNOWN_OPT] = "Error: Unknown option '%s' (expected: 'relay')",
    [MSG_SERVER_ERR_TOO_MANY]    = "Error: Too many arguments",
    [MSG_SERVER_STARTING]        = "[SERVER] Starting P2P signal server on port %d",
    [MSG_SERVER_NAT_PROBE]       = "[SERVER] NAT probe: %s (port %d)",
    [MSG_SERVER_RELAY_SUPPORT]   = "[SERVER] Relay support: %s",
    [MSG_SERVER_ENABLED]         = "enabled",
    [MSG_SERVER_DISABLED]        = "disabled",
    [MSG_SERVER_PROBE_BIND_FAILED] = "[SERVER] NAT probe disabled (bind failed)",
    [MSG_SERVER_PROBE_LISTENING] = "[SERVER] NAT probe socket listening on port %d",
    [MSG_SERVER_LISTENING]       = "P2P Signaling Server listening on port %d (TCP + UDP)...",
    [MSG_SERVER_SHUTDOWN_SIGNAL] = "[SERVER] Received shutdown signal, exiting gracefully...",
    [MSG_SERVER_SHUTTING_DOWN]   = "[SERVER] Shutting down...",
    [MSG_SERVER_GOODBYE]         = "[SERVER] Goodbye!",
    
    /* TCP/RELAY 日志消息 */
    [MSG_TCP_PEER_DISCONNECTED]   = "[TCP] Peer %s disconnected\n",
    [MSG_TCP_INVALID_MAGIC]       = "[TCP] Invalid magic from peer\n",
    [MSG_TCP_PEER_LOGIN]          = "[TCP] Peer '%s' logged in\n",
    [MSG_TCP_MERGED_PENDING]      = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",
    [MSG_TCP_FLUSHING_PENDING]    = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",
    [MSG_TCP_FORWARDED_OFFER]     = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",
    [MSG_TCP_PENDING_FLUSHED]     = "[TCP] All pending candidates flushed to '%s'\n",
    [MSG_TCP_STORAGE_FULL_FLUSH]  = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",
    [MSG_TCP_SENT_EMPTY_OFFER]    = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",
    [MSG_TCP_STORAGE_FULL_FLUSHED]= "[TCP] Storage full indication flushed to '%s'\n",
    [MSG_TCP_RECV_TARGET_FAILED]  = "[TCP] Failed to receive target name from %s\n",
    [MSG_TCP_PAYLOAD_TOO_LARGE]   = "[TCP] Payload too large (%u bytes) from %s\n",
    [MSG_TCP_RECV_PAYLOAD_FAILED] = "[TCP] Failed to receive payload from %s\n",
    [MSG_TCP_RELAYING]            = "[TCP] Relaying %s from %s to %s (%u bytes)\n",
    [MSG_TCP_SENT_WITH_CANDS]     = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",
    [MSG_TCP_TARGET_OFFLINE]      = "[TCP] Target %s offline, caching candidates...\n",
    [MSG_TCP_NEW_SENDER_REPLACE]  = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",
    [MSG_TCP_STORAGE_FULL_DROP]   = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",
    [MSG_TCP_STORAGE_INTENT_NOTED]= "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",
    [MSG_TCP_CACHED_FULL]         = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",
    [MSG_TCP_CACHED_PARTIAL]      = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",
    [MSG_TCP_CANNOT_ALLOC_SLOT]   = "[TCP] Cannot allocate slot for offline user '%s'\n",
    [MSG_TCP_SEND_ACK_FAILED]     = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",
    [MSG_TCP_SENT_CONNECT_ACK]    = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",
    [MSG_TCP_LIST_TRUNCATED]      = "[TCP] User list truncated (too many users)\n",
    [MSG_TCP_UNKNOWN_MSG_TYPE]    = "[TCP] Unknown message type %d from %s\n",
    [MSG_TCP_CLIENT_TIMEOUT]      = "[TCP] Client '%s' timed out (no activity for %ld seconds)\n",
    [MSG_TCP_NEW_CONNECTION]      = "[TCP] New connection from %s:%d\n",
    [MSG_TCP_MAX_PEERS]           = "[TCP] Max peers reached, rejecting connection\n",
    
    /* UDP/COMPACT 日志消息 */
    [MSG_UDP_REGISTER]            = "[UDP] REGISTER from %s: local='%s', remote='%s', candidates=%d\n",
    [MSG_UDP_CANDIDATE_INFO]      = "      [%d] type=%d, %s:%d\n",
    [MSG_UDP_REGISTER_ACK_ERROR]  = "[UDP] REGISTER_ACK to %s: error (no slot available)\n",
    [MSG_UDP_REGISTER_ACK_OK]     = "[UDP] REGISTER_ACK to %s: ok, peer_online=%d, max_cands=%d, relay=%s, public=%s:%d, probe_port=%d\n",
    [MSG_UDP_SENT_PEER_INFO]      = "[UDP] Sent PEER_INFO(seq=1, base=0) to %s (local='%s') with %d candidates%s\n",
    [MSG_UDP_SENT_PEER_INFO_ADDR] = "[UDP] Sent PEER_INFO(seq=1, base=0) to %s:%d (local='%s') with %d candidates%s\n",
    [MSG_UDP_TARGET_NOT_FOUND]    = "[UDP] Target pair (%s → %s) not found (waiting for peer registration)\n",
    [MSG_UDP_UNREGISTER]          = "[UDP] UNREGISTER: releasing slot for '%s' -> '%s'\n",
    [MSG_UDP_PAIR_TIMEOUT]        = "[UDP] Peer pair (%s → %s) timed out\n",
    
    /* DEBUG 和 PROBE 日志 */
    [MSG_DEBUG_RECEIVED_BYTES]    = "[DEBUG] Received %d bytes: magic=0x%08X, type=%d, length=%d (expected magic=0x%08X)\n",
    [MSG_PROBE_ACK]               = "[PROBE] NAT_PROBE_ACK -> %s:%d (req_id=%u, mapped=%s:%d)\n",
};

/* 中文词表 */
static const char* messages_zh[MSG_SERVER_COUNT] = {
    [MSG_SERVER_USAGE]           = "用法: %s [端口] [探测端口] [relay]",
    [MSG_SERVER_PARAMS]          = "参数:",
    [MSG_SERVER_PARAM_PORT]      = "  端口         信令服务器监听端口 (默认: 8888)",
    [MSG_SERVER_PARAM_PORT_TCP]  = "               - TCP: RELAY模式信令 (有状态/长连接)",
    [MSG_SERVER_PARAM_PORT_UDP]  = "               - UDP: COMPACT模式信令 (无状态)",
    [MSG_SERVER_PARAM_PROBE]     = "  探测端口     NAT类型探测端口 (默认: 0=禁用)",
    [MSG_SERVER_PARAM_PROBE_DESC]= "               用于检测对称NAT (端口一致性)",
    [MSG_SERVER_PARAM_RELAY]     = "  relay        启用数据中继支持 (COMPACT模式降级方案)",
    [MSG_SERVER_EXAMPLES]        = "示例:",
    [MSG_SERVER_EXAMPLE_DEFAULT] = "  %s                    # 默认配置 (端口8888, 无探测, 无中继)",
    [MSG_SERVER_EXAMPLE_PORT]    = "  %s 9000               # 监听9000端口",
    [MSG_SERVER_EXAMPLE_PROBE]   = "  %s 9000 9001          # 监听9000, 探测端口9001",
    [MSG_SERVER_EXAMPLE_RELAY]   = "  %s 9000 9001 relay    # 监听9000, 探测9001, 启用中继",
    [MSG_SERVER_ERR_INVALID_PORT]= "错误: 无效的端口号 '%s' (范围: 1-65535)",
    [MSG_SERVER_ERR_INVALID_PROBE]="错误: 无效的探测端口 '%s' (范围: 0-65535)",
    [MSG_SERVER_ERR_UNKNOWN_OPT] = "错误: 未知选项 '%s' (预期: 'relay')",
    [MSG_SERVER_ERR_TOO_MANY]    = "错误: 参数过多",
    [MSG_SERVER_STARTING]        = "[服务器] 正在启动 P2P 信令服务器，端口 %d",
    [MSG_SERVER_NAT_PROBE]       = "[服务器] NAT 探测: %s (端口 %d)",
    [MSG_SERVER_RELAY_SUPPORT]   = "[服务器] 中继支持: %s",
    [MSG_SERVER_ENABLED]         = "已启用",
    [MSG_SERVER_DISABLED]        = "已禁用",
    [MSG_SERVER_PROBE_BIND_FAILED] = "[服务器] NAT 探测已禁用（绑定失败）",
    [MSG_SERVER_PROBE_LISTENING] = "[服务器] NAT 探测端口监听于端口 %d",
    [MSG_SERVER_LISTENING]       = "P2P 信令服务器监听于端口 %d (TCP + UDP)...",
    [MSG_SERVER_SHUTDOWN_SIGNAL] = "[服务器] 收到关闭信号，正在优雅退出...",
    [MSG_SERVER_SHUTTING_DOWN]   = "[服务器] 正在关闭...",
    [MSG_SERVER_GOODBYE]         = "[服务器] 再见！",
    
    /* TCP/RELAY 日志消息 */
    [MSG_TCP_PEER_DISCONNECTED]   = "[TCP] 对端 %s 已断开连接\n",
    [MSG_TCP_INVALID_MAGIC]       = "[TCP] 对端发送无效的magic标识\n",
    [MSG_TCP_PEER_LOGIN]          = "[TCP] 对端 '%s' 已登录\n",
    [MSG_TCP_MERGED_PENDING]      = "[TCP] 从离线槽位合并 %d 个待转发候选 (发送者='%s') 到在线槽位 '%s'\n",
    [MSG_TCP_FLUSHING_PENDING]    = "[TCP] 正在转发 %d 个待发候选从 '%s' 到 '%s'...\n",
    [MSG_TCP_FORWARDED_OFFER]     = "[TCP]   → 已转发 OFFER 从 '%s' (%d 个候选, %d 字节)\n",
    [MSG_TCP_PENDING_FLUSHED]     = "[TCP] 所有待发候选已转发到 '%s'\n",
    [MSG_TCP_STORAGE_FULL_FLUSH]  = "[TCP] 缓存已满，正在转发连接意图从 '%s' 到 '%s' (发送空OFFER)...\n",
    [MSG_TCP_SENT_EMPTY_OFFER]    = "[TCP]   → 已发送空 OFFER 从 '%s' (缓存已满，反向连接)\n",
    [MSG_TCP_STORAGE_FULL_FLUSHED]= "[TCP] 缓存满标识已转发到 '%s'\n",
    [MSG_TCP_RECV_TARGET_FAILED]  = "[TCP] 接收目标名称失败，来自 %s\n",
    [MSG_TCP_PAYLOAD_TOO_LARGE]   = "[TCP] 负载过大 (%u 字节)，来自 %s\n",
    [MSG_TCP_RECV_PAYLOAD_FAILED] = "[TCP] 接收负载失败，来自 %s\n",
    [MSG_TCP_RELAYING]            = "[TCP] 正在转发 %s 从 %s 到 %s (%u 字节)\n",
    [MSG_TCP_SENT_WITH_CANDS]     = "[TCP] 已发送 %s 含 %d 个候选到 '%s' (来自 '%s')\n",
    [MSG_TCP_TARGET_OFFLINE]      = "[TCP] 目标 %s 离线，正在缓存候选...\n",
    [MSG_TCP_NEW_SENDER_REPLACE]  = "[TCP] 新发送者 '%s' 替换旧发送者 '%s' (丢弃 %d 个旧候选)\n",
    [MSG_TCP_STORAGE_FULL_DROP]   = "[TCP] 缓存已满，目标 '%s' (已缓存=%d, 已丢弃=%d)\n",
    [MSG_TCP_STORAGE_INTENT_NOTED]= "[TCP] 缓存已满，已记录连接意图从 '%s' 到 '%s'\n",
    [MSG_TCP_CACHED_FULL]         = "[TCP] 已为离线用户 '%s' 缓存 %d 个候选，缓存现已满 (%d/%d)\n",
    [MSG_TCP_CACHED_PARTIAL]      = "[TCP] 已为离线用户 '%s' 缓存 %d 个候选 (总计=%d/%d)\n",
    [MSG_TCP_CANNOT_ALLOC_SLOT]   = "[TCP] 无法为离线用户 '%s' 分配槽位\n",
    [MSG_TCP_SEND_ACK_FAILED]     = "[TCP] 发送 CONNECT_ACK 到 %s 失败 (sent_hdr=%d, sent_payload=%d)\n",
    [MSG_TCP_SENT_CONNECT_ACK]    = "[TCP] 已发送 CONNECT_ACK 到 %s (status=%d, candidates_acked=%d)\n",
    [MSG_TCP_LIST_TRUNCATED]      = "[TCP] 用户列表已截断（用户过多）\n",
    [MSG_TCP_UNKNOWN_MSG_TYPE]    = "[TCP] 未知消息类型 %d，来自 %s\n",
    [MSG_TCP_CLIENT_TIMEOUT]      = "[TCP] 客户端 '%s' 超时 (无活动 %ld 秒)\n",
    [MSG_TCP_NEW_CONNECTION]      = "[TCP] 新连接来自 %s:%d\n",
    [MSG_TCP_MAX_PEERS]           = "[TCP] 已达到最大连接数，拒绝连接\n",
    
    /* UDP/COMPACT 日志消息 */
    [MSG_UDP_REGISTER]            = "[UDP] 收到 REGISTER 从 %s: local='%s', remote='%s', candidates=%d\n",
    [MSG_UDP_CANDIDATE_INFO]      = "      [%d] type=%d, %s:%d\n",
    [MSG_UDP_REGISTER_ACK_ERROR]  = "[UDP] REGISTER_ACK 到 %s: 错误 (无可用槽位)\n",
    [MSG_UDP_REGISTER_ACK_OK]     = "[UDP] REGISTER_ACK 到 %s: ok, peer_online=%d, max_cands=%d, relay=%s, public=%s:%d, probe_port=%d\n",
    [MSG_UDP_SENT_PEER_INFO]      = "[UDP] 已发送 PEER_INFO(seq=1, base=0) 到 %s (local='%s') 含 %d 个候选%s\n",
    [MSG_UDP_SENT_PEER_INFO_ADDR] = "[UDP] 已发送 PEER_INFO(seq=1, base=0) 到 %s:%d (local='%s') 含 %d 个候选%s\n",
    [MSG_UDP_TARGET_NOT_FOUND]    = "[UDP] 目标配对 (%s → %s) 未找到 (等待对端注册)\n",
    [MSG_UDP_UNREGISTER]          = "[UDP] UNREGISTER: 释放槽位 '%s' -> '%s'\n",
    [MSG_UDP_PAIR_TIMEOUT]        = "[UDP] 对端配对 (%s → %s) 超时\n",
    
    /* DEBUG 和 PROBE 日志 */
    [MSG_DEBUG_RECEIVED_BYTES]    = "[DEBUG] 接收 %d 字节: magic=0x%08X, type=%d, length=%d (期望magic=0x%08X)\n",
    [MSG_PROBE_ACK]               = "[PROBE] NAT_PROBE_ACK -> %s:%d (req_id=%u, mapped=%s:%d)\n",
};

/* 设置当前语言 */
void server_set_language(p2p_language_t lang) {
    current_language = lang;
}

/* 获取消息字符串 */
const char* server_msg(server_msg_id_t msg_id) {
    if (msg_id >= MSG_SERVER_COUNT) return "";
    
#ifdef P2P_ENABLE_CHINESE
    if (current_language == P2P_LANG_ZH) {
        return messages_zh[msg_id] ? messages_zh[msg_id] : "";
    }
#endif
    
    return messages_en[msg_id] ? messages_en[msg_id] : "";
}
