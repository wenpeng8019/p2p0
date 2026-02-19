/*
 * 多语言支持实现
 *
 * 编译选项：
 *   -DP2P_ENABLE_CHINESE  启用中文词表（默认禁用）
 */

#include "p2p_lang.h"

/* 当前语言设置（默认英文） */
static p2p_language_t current_language = P2P_LANG_EN;

/* 英文词表 */
static const char* messages_en[MSG_COUNT] = {
    /* 通用: 错误信息 */
    [MSG_ERROR_BIND]            = "Bind failed",
    [MSG_ERROR_TIMEOUT]         = "Timeout",
    [MSG_ERROR_NO_MEMORY]       = "Out of memory",

    /* P2P 主模块相关 */
    [MSG_P2P_PUBSUB_REQUIRES_AUTH]  = "PUBSUB mode requires gh_token and gist_id",
    [MSG_P2P_RELAY_REQUIRES_SERVER] = "RELAY/COMPACT mode requires server_host",
    [MSG_P2P_INVALID_MODE]          = "Invalid signaling mode in configuration",
    [MSG_P2P_UDP_SOCKET_FAILED]     = "Failed to create UDP socket on port",
    [MSG_P2P_DTLS_NOT_LINKED]       = "DTLS (MbedTLS) requested but library not linked",
    [MSG_P2P_OPENSSL_NOT_LINKED]    = "OpenSSL requested but library not linked",
    [MSG_P2P_SCTP_NOT_LINKED]       = "SCTP (usrsctp) requested but library not linked",
    [MSG_P2P_COMPACT_NEEDS_PEER_ID] = "COMPACT mode requires explicit remote_peer_id",
    [MSG_P2P_COMPACT_HOST_CAND]     = "Added Host candidate",
    [MSG_P2P_COMPACT_REGISTERING]   = "COMPACT: registering",
    [MSG_P2P_WITH_N_CANDS]          = "with candidates",
    [MSG_P2P_RELAY_SERVER_FAILED]   = "Failed to connect to signaling server",
    [MSG_P2P_RELAY_OFFER_SENT]      = "RELAY: sent initial offer with",
    [MSG_P2P_RELAY_WAITING]         = "RELAY: waiting for incoming offer from any peer",
    [MSG_P2P_PUBSUB_PUB_GATHERING]  = "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",
    [MSG_P2P_PUBSUB_SUB_WAITING]    = "PUBSUB (SUB): waiting for offer from any peer",
    [MSG_P2P_UNKNOWN_MODE]          = "Unknown signaling mode",
    [MSG_P2P_AUTH_OK]               = "Authenticated successfully",
    [MSG_P2P_AUTH_FAIL]             = "Authentication failed",
    [MSG_P2P_UNKNOWN_PKT]           = "Received unknown packet type",
    [MSG_P2P_SAME_SUBNET_PROBE]     = "Same subnet detected, sent ROUTE_PROBE to",
    [MSG_P2P_SAME_SUBNET_DISABLED]  = "Same subnet detected but LAN shortcut disabled",
    [MSG_P2P_NAT_FAIL_RELAY]        = "NAT punch failed, using server relay",
    [MSG_P2P_NAT_FAIL_NO_RELAY]     = "NAT punch failed, server has no relay support",
    [MSG_P2P_NAT_FAIL_NO_TURN]      = "NAT punch failed, no TURN server configured",
    [MSG_P2P_CANDS_SENT_FWD]        = "Sent candidates, forwarded",
    [MSG_P2P_CANDS_SENT_CACHED]     = "Sent candidates (cached, peer offline)",
    [MSG_P2P_SERVER_FULL_WAIT]      = "Server storage full, waiting for peer to come online",
    [MSG_P2P_CANDS_SEND_FAILED]     = "Failed to send candidates, will retry",
    [MSG_P2P_OFFER_PUBLISHED]       = "Published",
    [MSG_P2P_OFFER_RESENT]          = "Resent",
    [MSG_P2P_OFFER_WITH_CANDS]      = "offer with",
    
    /* NAT 打洞相关 */
    [MSG_NAT_PUNCH_ERROR_NO_CAND] = "ERROR: No remote candidates to punch",
    [MSG_NAT_PUNCH_START]       = "START: Punching to",
    [MSG_NAT_PUNCH_CANDIDATES]  = "candidates",
    [MSG_NAT_PUNCH_RECEIVED]    = "PUNCH: Received from",
    [MSG_NAT_PUNCH_ACK_RECEIVED] = "PUNCH_ACK: Received from",
    [MSG_NAT_PUNCH_SUCCESS]     = "SUCCESS: Hole punched! Connected to",
    [MSG_NAT_PUNCH_ATTEMPTS]    = "Attempts:",
    [MSG_NAT_PUNCH_TIME]        = "Time:",
    [MSG_NAT_PUNCH_TIMEOUT]     = "TIMEOUT: Punch failed after",
    [MSG_NAT_PUNCH_SWITCH_RELAY] = "attempts, switching to RELAY",
    [MSG_NAT_PUNCH_PUNCHING]    = "PUNCHING: Attempt",
    [MSG_NAT_PUNCH_TO]          = "to",
    [MSG_NAT_PUNCH_CONN_LOST]   = "TIMEOUT: Connection lost",
    [MSG_NAT_PUNCH_NO_PONG]     = "no pong for",
    
    /* NAT 检测相关 */
    [MSG_NAT_DETECTION_START]   = "Starting NAT detection",
    [MSG_NAT_DETECTION_COMPLETED] = "Detection completed",

    /* NAT 类型名称 */
    [MSG_NAT_TYPE_DETECTING]       = "Detecting...",
    [MSG_NAT_TYPE_TIMEOUT]         = "Timeout (no response)",
    [MSG_NAT_TYPE_UNKNOWN]         = "Unknown",
    [MSG_NAT_TYPE_OPEN]            = "Open Internet (No NAT)",
    [MSG_NAT_TYPE_FULL_CONE]       = "Full Cone NAT",
    [MSG_NAT_TYPE_RESTRICTED]      = "Restricted Cone NAT",
    [MSG_NAT_TYPE_PORT_RESTRICTED] = "Port Restricted Cone NAT",
    [MSG_NAT_TYPE_SYMMETRIC]       = "Symmetric NAT (port-random)",
    [MSG_NAT_TYPE_BLOCKED]         = "UDP Blocked (STUN unreachable)",
    [MSG_NAT_TYPE_UNSUPPORTED]     = "Unsupported (no STUN/probe configured)",

    /* TCP 打洞相关 */
    [MSG_TCP_SIMULTANEOUS_OPEN]  = "Attempting Simultaneous Open to",
    [MSG_TCP_FALLBACK_PORT]      = "port busy, trying random port",
    [MSG_TCP_BOUND_TO]           = "Bound to",
    
    /* ROUTE 模块相关 */
    [MSG_ROUTE_DETECT_START]    = "Detecting local network addresses",
    [MSG_ROUTE_DETECT_DONE]     = "Local address detection done",
    [MSG_ROUTE_ADDRS]           = "address(es)",
    [MSG_ROUTE_SAME_SUBNET]     = "Peer is on the same subnet as",
    [MSG_ROUTE_VIA]             = "via local",
    [MSG_ROUTE_DIFF_SUBNET]     = "Peer is on a different subnet",
    [MSG_ROUTE_PROBE_SENT]      = "Sent route probe to",
    [MSG_ROUTE_PROBE_RECV]      = "Received route probe from",
    [MSG_ROUTE_PROBE_ACK_SENT]  = "sending ACK",
    [MSG_ROUTE_LAN_CONFIRMED]   = "LAN peer confirmed",

    /* COMPACT 信令相关 */
    [MSG_COMPACT_RECEIVED_FIN]  = "Received FIN",
    [MSG_COMPACT_TOTAL_CANDIDATES] = "total candidates",
    [MSG_COMPACT_REGISTERING]   = "Registering",
    [MSG_COMPACT_WITH_SERVER]   = "with server",
    [MSG_COMPACT_SERVER_ERROR]  = "Server error",
    [MSG_COMPACT_CACHE]         = "cache",
    [MSG_COMPACT_RELAY]         = "relay",
    [MSG_COMPACT_ALREADY_READY] = "Already READY, ignoring delayed REGISTER_ACK",
    [MSG_COMPACT_ENTERED_REGISTERED] = "Entered REGISTERED state",
    [MSG_COMPACT_PEER_ONLINE]   = "Peer online, waiting for PEER_INFO(seq=1)",
    [MSG_COMPACT_PEER_OFFLINE]  = "Peer offline, waiting for peer to come online",
    [MSG_COMPACT_BASE]          = "base",
    [MSG_COMPACT_ENTERED_READY] = "Entered READY state, starting NAT punch and candidate sync",
    [MSG_COMPACT_MAX_ATTEMPTS]  = "Max register attempts reached",
    [MSG_COMPACT_ATTEMPT]       = "Attempt",
    [MSG_COMPACT_WITH]          = "with",
    [MSG_COMPACT_TOTAL_SENT]    = "total sent",

    /* COMPACT 模式 NAT 探测相关 */
    [MSG_COMPACT_NAT_PROBE_SENT]    = "NAT probe sent to",
    [MSG_COMPACT_NAT_PROBE_RETRY]   = "NAT probe retry",
    [MSG_COMPACT_NAT_PROBE_TIMEOUT] = "NAT probe timeout, type unknown",
    [MSG_COMPACT_NAT_OPEN]          = "Open Internet (No NAT)",
    [MSG_COMPACT_NAT_CONE]          = "Cone NAT (port-consistent)",
    [MSG_COMPACT_NAT_SYMMETRIC]     = "Symmetric NAT (port-random)",
    
    /* RELAY 信令相关 */
    [MSG_RELAY_CONNECT_ACK]     = "Received ACK",
    [MSG_RELAY_ANSWER_SENT]     = "Sent ANSWER",
    [MSG_RELAY_FORWARD_RECEIVED] = "Received FORWARD",
    [MSG_RELAY_CONNECTED_TO]    = "Connected to server",
    [MSG_RELAY_AS]              = "as",
    [MSG_RELAY_SEND_HEADER_FAILED] = "Failed to send header",
    [MSG_RELAY_SEND_TARGET_FAILED] = "Failed to send target name",
    [MSG_RELAY_SEND_PAYLOAD_FAILED] = "Failed to send payload",
    [MSG_RELAY_SENT_CONNECT]    = "Sent connect",
    [MSG_RELAY_REQUEST]         = "request to",
    [MSG_RELAY_BYTES]           = "bytes",
    [MSG_RELAY_SENT_ANSWER]     = "Sent answer to",
    [MSG_RELAY_WAITING_PEER]    = "Waiting for peer",
    [MSG_RELAY_TIMED_OUT]       = "timed out",
    [MSG_RELAY_GIVING_UP]       = "giving up",
    [MSG_RELAY_CONNECTION_CLOSED] = "Connection closed by server",
    [MSG_RELAY_RECV_ERROR]      = "recv error",
    [MSG_RELAY_INVALID_MAGIC]   = "Invalid magic",
    [MSG_RELAY_EXPECTED]        = "expected",
    [MSG_RELAY_RESETTING]       = "resetting",
    [MSG_RELAY_ALLOC_ACK_FAILED] = "Failed to allocate ACK payload buffer",
    [MSG_RELAY_CONN_CLOSED_SENDER] = "Connection closed while reading sender",
    [MSG_RELAY_WHILE_READING_SENDER] = "while reading sender",
    [MSG_RELAY_ALLOC_FAILED]    = "Failed to allocate",
    [MSG_RELAY_CONN_CLOSED_PAYLOAD] = "Connection closed while reading payload",
    [MSG_RELAY_WHILE_READING_PAYLOAD] = "while reading payload",
    [MSG_RELAY_RECEIVED_ACK]    = "Received ACK",
    [MSG_RELAY_PEER_ONLINE]     = "Peer online",
    [MSG_RELAY_FORWARDED]       = "forwarded",
    [MSG_RELAY_PEER_OFFLINE]    = "Peer offline",
    [MSG_RELAY_CACHED]          = "cached",
    [MSG_RELAY_STORAGE_FULL]    = "Storage full",
    [MSG_RELAY_WAITING_PEER_ONLINE] = "waiting for peer to come online",
    [MSG_RELAY_UNKNOWN_ACK_STATUS] = "Unknown ACK status",
    [MSG_RELAY_PASSIVE_LEARNED] = "Passive peer learned remote ID",
    [MSG_RELAY_FROM_MSG]        = "from",
    [MSG_RELAY_PEER]            = "Peer",
    [MSG_RELAY_IS_NOW_ONLINE]   = "is now online",
    [MSG_RELAY_RECEIVED]        = "received",
    [MSG_RELAY_RESUMING]        = "resuming",
    [MSG_RELAY_RECEIVED_SIGNAL] = "Received signal from",
    [MSG_RELAY_ALLOC_DISCARD_FAILED] = "Failed to allocate discard buffer, closing connection",
    [MSG_RELAY_CONN_CLOSED_DISCARD] = "Connection closed while discarding",
    [MSG_RELAY_WHILE_DISCARDING] = "while discarding",
    [MSG_RELAY_DISCARDED]       = "Discarded",
    [MSG_RELAY_PAYLOAD_OF_TYPE] = "payload of message type",
    [MSG_RELAY_INVALID_STATE]   = "Invalid read state",
    
    /* PUBSUB 信令相关 */
    [MSG_PUBSUB_NO_AUTH_KEY]    = "No auth_key provided, using default key (insecure)",
    [MSG_PUBSUB_INVALID_CHANNEL] = "Invalid channel_id format (security risk)",
    [MSG_PUBSUB_INITIALIZED]    = "Initialized:",
    [MSG_PUBSUB_ROLE_PUB]       = "PUB",
    [MSG_PUBSUB_ROLE_SUB]       = "SUB",
    [MSG_PUBSUB_BASE64_FAILED]  = "Base64 decode failed",
    [MSG_PUBSUB_RECEIVED_SIGNAL] = "Received valid signal from",
    [MSG_PUBSUB_DESERIALIZE_FAILED] = "Signal payload deserialization failed",
    [MSG_PUBSUB_CHANNEL_VALIDATION_FAILED] = "Channel ID validation failed",
    [MSG_PUBSUB_ANSWER_PRESENT] = "Answer already present, skipping offer re-publish",
    [MSG_PUBSUB_UPDATING_GIST]  = "Updating Gist field",
    [MSG_PUBSUB_GET_FAILED]     = "Gist GET failed",
    [MSG_PUBSUB_FIELD]          = "Field",
    [MSG_PUBSUB_FIELD_EMPTY]    = "is empty or too short",
    [MSG_PUBSUB_PROCESSING]     = "Processing",
    [MSG_PUBSUB_ROLE]           = "role",
    [MSG_PUBSUB_RECEIVED_REMOTE_CAND] = "Received remote candidate",
    [MSG_PUBSUB_TYPE]           = "type",
    [MSG_PUBSUB_ADDRESS]        = "address",
    [MSG_PUBSUB_AUTO_SEND_ANSWER] = "Auto-send answer",

    /* ICE 相关 */
    [MSG_ICE_GATHERED_SRFLX]    = "Gathered Srflx Candidate",
    [MSG_ICE_GATHERED_RELAY]    = "Gathered Relay Candidate",
    [MSG_ICE_NOMINATION_SUCCESS] = "Nomination successful! Using",
    [MSG_ICE_REMOTE_CANDIDATE_ADDED] = "Added Remote Candidate",
    [MSG_ICE_CONNECTIVITY_CHECK] = "Performing connectivity check",
    [MSG_ICE_FORMED_CHECKLIST]  = "Formed check list with",
    [MSG_ICE_CANDIDATE_PAIRS]   = "candidate pairs",
    [MSG_ICE_AND]               = "and",
    [MSG_ICE_MORE_PAIRS]        = "more pairs",
    [MSG_ICE_ERROR_NON_RELAY]   = "Error: p2p_ice_send_local_candidate called in non-RELAY mode",
    [MSG_ICE_TRICKLE_TCP_NOT_CONNECTED] = "[Trickle] TCP not connected, skipping single candidate send",
    [MSG_ICE_TRICKLE_TCP_FAILED] = "[Trickle] TCP send failed",
    [MSG_ICE_WILL_RETRY]        = "will be retried by p2p_update()",
    [MSG_ICE_TRICKLE_SENT]      = "[Trickle] Sent",
    [MSG_ICE_ONE_CANDIDATE]     = "1 candidate to",
    [MSG_ICE_ONLINE]            = "online",
    [MSG_ICE_YES]               = "yes",
    [MSG_ICE_NO_CACHED]         = "no (cached)",
    [MSG_ICE_GATHERED]          = "Gathered",
    [MSG_ICE_HOST_CANDIDATE]    = "Host Candidate",
    [MSG_ICE_REQUESTED]         = "Requested",
    [MSG_ICE_SRFLX_CANDIDATE]   = "Srflx Candidate",
    [MSG_ICE_FROM]              = "from",
    [MSG_ICE_RELAY_CANDIDATE]   = "Relay Candidate",
    [MSG_ICE_RECEIVED_REMOTE]   = "Received New Remote Candidate",
    [MSG_ICE_USING]             = "Using",
    [MSG_ICE_PATH]              = "path",
    [MSG_ICE_SENT_ANSWER]       = "Sent answer to",
    [MSG_ICE_AUTH_SENT]         = "Sent authentication request to peer",
    
    /* STUN 相关 */
    [MSG_STUN_TEST]             = "Test",
    [MSG_STUN_MAPPED_ADDRESS]   = "Mapped address",
    [MSG_STUN_PRIORITY]         = "priority",
    [MSG_STUN_SRFLX_ADD_FAILED] = "Cannot add Srflx candidate: local_cand_cnt >= P2P_MAX_CANDIDATES",
    [MSG_STUN_SUCCESS]          = "Success",
    [MSG_STUN_NEED]             = "need",
    [MSG_STUN_RESOLVE_FAILED]   = "Failed to resolve",
    [MSG_STUN_SERVER]           = "STUN server",
    [MSG_STUN_SENDING]          = "Sending",
    [MSG_STUN_TO]               = "to",
    [MSG_STUN_LEN]              = "len",
    [MSG_STUN_REQUEST_FAILED]   = "Failed to build STUN request",
    
    /* TURN 相关 */
    [MSG_TURN_SENDING_ALLOC]    = "Sending Allocate Request to",
    [MSG_TURN_RESOLVE_FAILED]   = "Failed to resolve TURN server:",
    [MSG_TURN_ALLOC_SUCCESS]    = "Allocation successful!",
    
    /* ARQ 可靠传输相关 */
    [MSG_RELIABLE_INIT]         = "Reliable transport initialized",
    [MSG_RELIABLE_WINDOW_FULL]  = "Send window full, dropping packet",
    [MSG_RELIABLE_PKT_TOO_LARGE] = "Packet too large",
    [MSG_RELIABLE_PKT_QUEUED]   = "Packet queued",
    [MSG_RELIABLE_OUT_OF_WINDOW] = "Out-of-window packet discarded",
    [MSG_RELIABLE_DATA_STORED]  = "Data stored in recv buffer",
    [MSG_RELIABLE_RTT_UPDATE]   = "RTT updated",
    [MSG_RELIABLE_ACK_PROCESSED] = "ACK processed",

    /* PseudoTCP 传输层 */
    [MSG_PSEUDOTCP_CONGESTION]  = "[PseudoTCP] congestion detected, new ssthresh: %u, cwnd: %u",
    
    /* DTLS/MbedTLS 传输层 */
    [MSG_DTLS_SETUP_FAIL]       = "[DTLS] ssl_setup failed: -0x%x",
    [MSG_DTLS_HANDSHAKE_DONE]   = "[DTLS] Handshake complete",
    [MSG_DTLS_HANDSHAKE_FAIL]   = "[DTLS] Handshake failed: %s (-0x%04x)",

    /* DTLS/OpenSSL 传输层 */
    [MSG_OPENSSL_HANDSHAKE_DONE] = "[OpenSSL] DTLS handshake completed",

    /* SCTP 传输层 */
    [MSG_SCTP_INIT]             = "[SCTP] usrsctp wrapper initialized (skeleton)",
    [MSG_SCTP_SEND]             = "[SCTP] sending %d bytes",
    [MSG_SCTP_RECV]             = "[SCTP] received encapsulated packet, length %d",
};

#ifdef P2P_ENABLE_CHINESE
/* 中文词表（仅当启用中文支持时编译） */
static const char* messages_zh[MSG_COUNT] = {
    /* 错误信息 */
    [MSG_ERROR_BIND]            = "绑定失败",
    [MSG_ERROR_TIMEOUT]         = "超时",
    [MSG_ERROR_NO_MEMORY]       = "内存不足",
    
    /* P2P 主模块相关 */
    [MSG_P2P_PUBSUB_REQUIRES_AUTH]  = "PUBSUB 模式需要 gh_token 和 gist_id",
    [MSG_P2P_RELAY_REQUIRES_SERVER] = "RELAY/COMPACT 模式需要 server_host",
    [MSG_P2P_INVALID_MODE]          = "配置中指定了无效的信令模式",
    [MSG_P2P_UDP_SOCKET_FAILED]     = "创建 UDP 套接字失败，端口",
    [MSG_P2P_DTLS_NOT_LINKED]       = "请求 DTLS (MbedTLS) 但库未連接",
    [MSG_P2P_OPENSSL_NOT_LINKED]    = "请求 OpenSSL 但库未連接",
    [MSG_P2P_SCTP_NOT_LINKED]       = "请求 SCTP (usrsctp) 但库未連接",
    [MSG_P2P_COMPACT_NEEDS_PEER_ID] = "COMPACT 模式需要显式指定 remote_peer_id",
    [MSG_P2P_COMPACT_HOST_CAND]     = "已添加本地候选",
    [MSG_P2P_COMPACT_REGISTERING]   = "COMPACT: 注册中",
    [MSG_P2P_WITH_N_CANDS]          = "个候选",
    [MSG_P2P_RELAY_SERVER_FAILED]   = "连接信令服务器失败",
    [MSG_P2P_RELAY_OFFER_SENT]      = "RELAY: 已发送初始 offer，包含",
    [MSG_P2P_RELAY_WAITING]         = "RELAY: 等待任意对端的 offer",
    [MSG_P2P_PUBSUB_PUB_GATHERING]  = "PUBSUB (PUB): 收集候选中，等待 STUN 响应后再发布",
    [MSG_P2P_PUBSUB_SUB_WAITING]    = "PUBSUB (SUB): 等待任意对端的 offer",
    [MSG_P2P_UNKNOWN_MODE]          = "未知信令模式",
    [MSG_P2P_AUTH_OK]               = "认证成功",
    [MSG_P2P_AUTH_FAIL]             = "认证失败",
    [MSG_P2P_UNKNOWN_PKT]           = "收到未知包类型",
    [MSG_P2P_SAME_SUBNET_PROBE]     = "检测到同一子网，已发送 ROUTE_PROBE 到",
    [MSG_P2P_SAME_SUBNET_DISABLED]  = "检测到同一子网但 LAN 捷径已禁用",
    [MSG_P2P_NAT_FAIL_RELAY]        = "NAT 打洞失败，使用服务器中继",
    [MSG_P2P_NAT_FAIL_NO_RELAY]     = "NAT 打洞失败，服务器不支持中继",
    [MSG_P2P_NAT_FAIL_NO_TURN]      = "NAT 打洞失败，未配置 TURN 服务器",
    [MSG_P2P_CANDS_SENT_FWD]        = "已发送候选，已转发",
    [MSG_P2P_CANDS_SENT_CACHED]     = "已发送候选（已缓存，对端离线）",
    [MSG_P2P_SERVER_FULL_WAIT]      = "服务器存储已满，等待对端上线",
    [MSG_P2P_CANDS_SEND_FAILED]     = "发送候选失败，将重试",
    [MSG_P2P_OFFER_PUBLISHED]       = "已发布",
    [MSG_P2P_OFFER_RESENT]          = "重新发送",
    [MSG_P2P_OFFER_WITH_CANDS]      = "offer 包含",
    
    /* NAT 打洞相关 */
    [MSG_NAT_PUNCH_ERROR_NO_CAND] = "错误: 没有远端候选可以打洞",
    [MSG_NAT_PUNCH_START]       = "开始: 打洞到",
    [MSG_NAT_PUNCH_CANDIDATES]  = "个候选",
    [MSG_NAT_PUNCH_RECEIVED]    = "打洞: 收到来自",
    [MSG_NAT_PUNCH_ACK_RECEIVED] = "打洞应答: 收到来自",
    [MSG_NAT_PUNCH_SUCCESS]     = "成功: 打洞成功！连接到",
    [MSG_NAT_PUNCH_ATTEMPTS]    = "尝试次数:",
    [MSG_NAT_PUNCH_TIME]        = "耗时:",
    [MSG_NAT_PUNCH_TIMEOUT]     = "超时: 打洞失败，尝试次数",
    [MSG_NAT_PUNCH_SWITCH_RELAY] = "次，切换到 RELAY",
    [MSG_NAT_PUNCH_PUNCHING]    = "打洞中: 尝试",
    [MSG_NAT_PUNCH_TO]          = "，目标",
    [MSG_NAT_PUNCH_CONN_LOST]   = "超时: 连接丢失",
    [MSG_NAT_PUNCH_NO_PONG]     = "无 pong 超过",
    
    /* NAT 检测相关 */
    [MSG_NAT_DETECTION_START]   = "开始 NAT 检测",
    [MSG_NAT_DETECTION_COMPLETED] = "检测完成",

    /* NAT 类型名称 */
    [MSG_NAT_TYPE_DETECTING]       = "检测中...",
    [MSG_NAT_TYPE_TIMEOUT]         = "超时（无响应）",
    [MSG_NAT_TYPE_UNKNOWN]         = "未知",
    [MSG_NAT_TYPE_OPEN]            = "无 NAT（公网直连）",
    [MSG_NAT_TYPE_FULL_CONE]       = "完全锥形 NAT",
    [MSG_NAT_TYPE_RESTRICTED]      = "受限锥形 NAT",
    [MSG_NAT_TYPE_PORT_RESTRICTED] = "端口受限锥形 NAT",
    [MSG_NAT_TYPE_SYMMETRIC]       = "对称型 NAT（端口随机）",
    [MSG_NAT_TYPE_BLOCKED]         = "UDP 不可达",
    [MSG_NAT_TYPE_UNSUPPORTED]     = "不支持（未配置 STUN/探测端口）",

    /* TCP 打洞相关 */
    [MSG_TCP_SIMULTANEOUS_OPEN]  = "尝试 TCP 同时发起到",
    [MSG_TCP_FALLBACK_PORT]      = "端口已占用，改用随机端口",
    [MSG_TCP_BOUND_TO]           = "已绑定到",
    
    /* ROUTE 模块相关 */
    [MSG_ROUTE_DETECT_START]    = "检测本地网络地址中",
    [MSG_ROUTE_DETECT_DONE]     = "本地地址检测完成",
    [MSG_ROUTE_ADDRS]           = "个地址",
    [MSG_ROUTE_SAME_SUBNET]     = "对端与本机处于同一子网",
    [MSG_ROUTE_VIA]             = "通过本地接口",
    [MSG_ROUTE_DIFF_SUBNET]     = "对端处于不同子网",
    [MSG_ROUTE_PROBE_SENT]      = "已发送路由探测到",
    [MSG_ROUTE_PROBE_RECV]      = "收到路由探测来自",
    [MSG_ROUTE_PROBE_ACK_SENT]  = "已回复 ACK",
    [MSG_ROUTE_LAN_CONFIRMED]   = "内网对端已确认",
    
    /* COMPACT 信令相关 */
    [MSG_COMPACT_RECEIVED_FIN]  = "收到 FIN",
    [MSG_COMPACT_TOTAL_CANDIDATES] = "总候选数",
    [MSG_COMPACT_REGISTERING]   = "正在注册",
    [MSG_COMPACT_WITH_SERVER]   = "到服务器",
    [MSG_COMPACT_SERVER_ERROR]  = "服务器错误",
    [MSG_COMPACT_CACHE]         = "缓存",
    [MSG_COMPACT_RELAY]         = "中继",
    [MSG_COMPACT_ALREADY_READY] = "已处于 READY 状态，忽略延迟的 REGISTER_ACK",
    [MSG_COMPACT_ENTERED_REGISTERED] = "进入 REGISTERED 状态",
    [MSG_COMPACT_PEER_ONLINE]   = "对端在线，等待 PEER_INFO(seq=1)",
    [MSG_COMPACT_PEER_OFFLINE]  = "对端离线，等待对端上线",
    [MSG_COMPACT_BASE]          = "基准索引",
    [MSG_COMPACT_ENTERED_READY] = "进入 READY 状态，开始 NAT 打洞和候选同步",
    [MSG_COMPACT_MAX_ATTEMPTS]  = "达到最大注册尝试次数",
    [MSG_COMPACT_ATTEMPT]       = "尝试",
    [MSG_COMPACT_WITH]          = "带",
    [MSG_COMPACT_TOTAL_SENT]    = "总发送",

    /* COMPACT 模式 NAT 探测相关 */
    [MSG_COMPACT_NAT_PROBE_SENT]    = "NAT 探测已发送至",
    [MSG_COMPACT_NAT_PROBE_RETRY]   = "NAT 探测重试",
    [MSG_COMPACT_NAT_PROBE_TIMEOUT] = "NAT 探测超时，无法确定类型",
    [MSG_COMPACT_NAT_OPEN]          = "无 NAT（公网直连）",
    [MSG_COMPACT_NAT_CONE]          = "锥形 NAT（端口一致）",
    [MSG_COMPACT_NAT_SYMMETRIC]     = "对称型 NAT（端口随机）",
    
    /* RELAY 信令相关 */
    [MSG_RELAY_CONNECT_ACK]     = "收到 ACK",
    [MSG_RELAY_ANSWER_SENT]     = "发送 ANSWER",
    [MSG_RELAY_FORWARD_RECEIVED] = "收到 FORWARD",
    [MSG_RELAY_CONNECTED_TO]    = "已连接到服务器",
    [MSG_RELAY_AS]              = "作为",
    [MSG_RELAY_SEND_HEADER_FAILED] = "发送消息头失败",
    [MSG_RELAY_SEND_TARGET_FAILED] = "发送目标名称失败",
    [MSG_RELAY_SEND_PAYLOAD_FAILED] = "发送负载失败",
    [MSG_RELAY_SENT_CONNECT]    = "已发送连接",
    [MSG_RELAY_REQUEST]         = "请求到",
    [MSG_RELAY_BYTES]           = "字节",
    [MSG_RELAY_SENT_ANSWER]     = "已发送应答到",
    [MSG_RELAY_WAITING_PEER]    = "等待对端",
    [MSG_RELAY_TIMED_OUT]       = "超时",
    [MSG_RELAY_GIVING_UP]       = "放弃",
    [MSG_RELAY_CONNECTION_CLOSED] = "服务器关闭连接",
    [MSG_RELAY_RECV_ERROR]      = "接收错误",
    [MSG_RELAY_INVALID_MAGIC]   = "无效的魔数",
    [MSG_RELAY_EXPECTED]        = "期望",
    [MSG_RELAY_RESETTING]       = "重置中",
    [MSG_RELAY_ALLOC_ACK_FAILED] = "分配 ACK 负载缓冲区失败",
    [MSG_RELAY_CONN_CLOSED_SENDER] = "读取发送者时连接关闭",
    [MSG_RELAY_WHILE_READING_SENDER] = "读取发送者时",
    [MSG_RELAY_ALLOC_FAILED]    = "分配失败",
    [MSG_RELAY_CONN_CLOSED_PAYLOAD] = "读取负载时连接关闭",
    [MSG_RELAY_WHILE_READING_PAYLOAD] = "读取负载时",
    [MSG_RELAY_RECEIVED_ACK]    = "收到 ACK",
    [MSG_RELAY_PEER_ONLINE]     = "对端在线",
    [MSG_RELAY_FORWARDED]       = "已转发",
    [MSG_RELAY_PEER_OFFLINE]    = "对端离线",
    [MSG_RELAY_CACHED]          = "已缓存",
    [MSG_RELAY_STORAGE_FULL]    = "存储已满",
    [MSG_RELAY_WAITING_PEER_ONLINE] = "等待对端上线",
    [MSG_RELAY_UNKNOWN_ACK_STATUS] = "未知 ACK 状态",
    [MSG_RELAY_PASSIVE_LEARNED] = "被动端学习到远端 ID",
    [MSG_RELAY_FROM_MSG]        = "来自",
    [MSG_RELAY_PEER]            = "对端",
    [MSG_RELAY_IS_NOW_ONLINE]   = "现在在线",
    [MSG_RELAY_RECEIVED]        = "已接收",
    [MSG_RELAY_RESUMING]        = "恢复中",
    [MSG_RELAY_RECEIVED_SIGNAL] = "收到信令来自",
    [MSG_RELAY_ALLOC_DISCARD_FAILED] = "分配丢弃缓冲区失败，关闭连接",
    [MSG_RELAY_CONN_CLOSED_DISCARD] = "丢弃时连接关闭",
    [MSG_RELAY_WHILE_DISCARDING] = "丢弃时",
    [MSG_RELAY_DISCARDED]       = "已丢弃",
    [MSG_RELAY_PAYLOAD_OF_TYPE] = "消息类型的负载",
    [MSG_RELAY_INVALID_STATE]   = "无效的读取状态",
    
    /* PUBSUB 信令相关 */
    [MSG_PUBSUB_NO_AUTH_KEY]    = "未提供 auth_key，使用默认密钥（不安全）",
    [MSG_PUBSUB_INVALID_CHANNEL] = "channel_id 格式无效（安全风险）",
    [MSG_PUBSUB_INITIALIZED]    = "已初始化:",
    [MSG_PUBSUB_ROLE_PUB]       = "发布者",
    [MSG_PUBSUB_ROLE_SUB]       = "订阅者",
    [MSG_PUBSUB_BASE64_FAILED]  = "Base64 解码失败",
    [MSG_PUBSUB_RECEIVED_SIGNAL] = "收到有效信令来自",
    [MSG_PUBSUB_DESERIALIZE_FAILED] = "信令载荷反序列化失败",
    [MSG_PUBSUB_CHANNEL_VALIDATION_FAILED] = "Channel ID 验证失败",
    [MSG_PUBSUB_ANSWER_PRESENT] = "Answer 已存在，跳过 offer 重新发布",
    [MSG_PUBSUB_UPDATING_GIST]  = "更新 Gist 字段",
    [MSG_PUBSUB_GET_FAILED]     = "Gist GET 失败",
    [MSG_PUBSUB_FIELD]          = "字段",
    [MSG_PUBSUB_FIELD_EMPTY]    = "为空或太短",
    [MSG_PUBSUB_PROCESSING]     = "正在处理",
    [MSG_PUBSUB_ROLE]           = "角色",
    [MSG_PUBSUB_RECEIVED_REMOTE_CAND] = "收到远端候选",
    [MSG_PUBSUB_TYPE]           = "类型",
    [MSG_PUBSUB_ADDRESS]        = "地址",
    [MSG_PUBSUB_AUTO_SEND_ANSWER] = "自动发送 answer",
    
    /* ICE 相关 */
    [MSG_ICE_GATHERED_SRFLX]    = "收集到服务器反射候选",
    [MSG_ICE_GATHERED_RELAY]    = "收集到中继候选",
    [MSG_ICE_NOMINATION_SUCCESS] = "协商成功！使用",
    [MSG_ICE_REMOTE_CANDIDATE_ADDED] = "添加远端候选",
    [MSG_ICE_CONNECTIVITY_CHECK] = "执行连通性检查",
    [MSG_ICE_FORMED_CHECKLIST]  = "生成检查列表，包含",
    [MSG_ICE_CANDIDATE_PAIRS]   = "个候选对",
    [MSG_ICE_AND]               = "以及",
    [MSG_ICE_MORE_PAIRS]        = "个候选对",
    [MSG_ICE_ERROR_NON_RELAY]   = "错误: 在非 RELAY 模式下调用了 p2p_ice_send_local_candidate",
    [MSG_ICE_TRICKLE_TCP_NOT_CONNECTED] = "[Trickle] TCP 未连接，跳过单个候选发送",
    [MSG_ICE_TRICKLE_TCP_FAILED] = "[Trickle] TCP 发送失败",
    [MSG_ICE_WILL_RETRY]        = "将在 p2p_update() 中重试",
    [MSG_ICE_TRICKLE_SENT]      = "[Trickle] 已发送",
    [MSG_ICE_ONE_CANDIDATE]     = "1 个候选到",
    [MSG_ICE_ONLINE]            = "在线",
    [MSG_ICE_YES]               = "是",
    [MSG_ICE_NO_CACHED]         = "否（已缓存）",
    [MSG_ICE_GATHERED]          = "收集到",
    [MSG_ICE_HOST_CANDIDATE]    = "本地候选",
    [MSG_ICE_REQUESTED]         = "请求",
    [MSG_ICE_SRFLX_CANDIDATE]   = "Srflx 候选",
    [MSG_ICE_FROM]              = "从",
    [MSG_ICE_RELAY_CANDIDATE]   = "中继候选",
    [MSG_ICE_RECEIVED_REMOTE]   = "收到新的远端候选",
    [MSG_ICE_USING]             = "使用",
    [MSG_ICE_PATH]              = "路径",
    [MSG_ICE_SENT_ANSWER]       = "已发送应答到",
    [MSG_ICE_AUTH_SENT]         = "已发送认证请求到对端",
    
    /* STUN 相关 */
    [MSG_STUN_TEST]             = "测试",
    [MSG_STUN_MAPPED_ADDRESS]   = "映射地址",
    [MSG_STUN_PRIORITY]         = "优先级",
    [MSG_STUN_SRFLX_ADD_FAILED] = "无法添加 Srflx 候选：local_cand_cnt >= P2P_MAX_CANDIDATES",
    [MSG_STUN_SUCCESS]          = "成功",
    [MSG_STUN_NEED]             = "需要",
    [MSG_STUN_RESOLVE_FAILED]   = "解析失败",
    [MSG_STUN_SERVER]           = "STUN 服务器",
    [MSG_STUN_SENDING]          = "发送",
    [MSG_STUN_TO]               = "到",
    [MSG_STUN_LEN]              = "长度",
    [MSG_STUN_REQUEST_FAILED]   = "构建 STUN 请求失败",
    
    /* TURN 相关 */
    [MSG_TURN_SENDING_ALLOC]    = "发送分配请求到",
    [MSG_TURN_RESOLVE_FAILED]   = "解析 TURN 服务器失败:",
    [MSG_TURN_ALLOC_SUCCESS]    = "分配成功！",
    
    /* ARQ 可靠传输相关 */
    [MSG_RELIABLE_INIT]         = "可靠传输层已初始化",
    [MSG_RELIABLE_WINDOW_FULL]  = "发送窗口已满，丢弃数据包",
    [MSG_RELIABLE_PKT_TOO_LARGE] = "数据包过大",
    [MSG_RELIABLE_PKT_QUEUED]   = "数据包已入队",
    [MSG_RELIABLE_OUT_OF_WINDOW] = "超出窗口的数据包已丢弃",
    [MSG_RELIABLE_DATA_STORED]  = "数据已存入接收缓冲区",
    [MSG_RELIABLE_RTT_UPDATE]   = "RTT 更新",
    [MSG_RELIABLE_ACK_PROCESSED] = "ACK 已处理",

    /* PseudoTCP */
    [MSG_PSEUDOTCP_CONGESTION]  = "[PseudoTCP] 检测到拥塞，新 ssthresh: %u, cwnd: %u",

    /* DTLS/MbedTLS */
    [MSG_DTLS_SETUP_FAIL]       = "[DTLS] ssl_setup 失败: -0x%x",
    [MSG_DTLS_HANDSHAKE_DONE]   = "[DTLS] 握手成功",
    [MSG_DTLS_HANDSHAKE_FAIL]   = "[DTLS] 握手失败: %s (-0x%04x)",

    /* DTLS/OpenSSL */
    [MSG_OPENSSL_HANDSHAKE_DONE] = "[OpenSSL] DTLS 握手完成",

    /* SCTP */
    [MSG_SCTP_INIT]             = "[SCTP] usrsctp 封装初始化（骨架实现）",
    [MSG_SCTP_SEND]             = "[SCTP] 发送 %d 字节数据",
    [MSG_SCTP_RECV]             = "[SCTP] 收到封装数据包，长度 %d",
};
#endif /* P2P_ENABLE_CHINESE */

/* 设置当前语言 */
void p2p_set_language(p2p_language_t lang) {
#ifdef P2P_ENABLE_CHINESE
    if (lang == P2P_LANG_EN || lang == P2P_LANG_ZH) {
        current_language = lang;
    }
#else
    /* 未启用中文支持，强制使用英文 */
    (void)lang;
    current_language = P2P_LANG_EN;
#endif
}

/* 获取当前语言 */
p2p_language_t p2p_get_language(void) {
    return current_language;
}

/* 获取消息文本（显式指定语言）*/
const char* p2p_msg_lang(p2p_msg_id_t id, p2p_language_t lang) {
    if (id >= MSG_COUNT) return "Invalid message ID";
#ifdef P2P_ENABLE_CHINESE
    if (lang == P2P_LANG_ZH)
        return messages_zh[id] ? messages_zh[id] : messages_en[id];
#else
    (void)lang;
#endif
    return messages_en[id] ? messages_en[id] : "Unknown message";
}

/* 获取消息文本 */
const char* p2p_msg(p2p_msg_id_t id) {
    if (id >= MSG_COUNT) {
        return "Invalid message ID";
    }
    
#ifdef P2P_ENABLE_CHINESE
    switch (current_language) {
        case P2P_LANG_ZH:
            return messages_zh[id] ? messages_zh[id] : messages_en[id];
        case P2P_LANG_EN:
        default:
            return messages_en[id] ? messages_en[id] : "Unknown message";
    }
#else
    /* 未启用中文支持，直接返回英文 */
    return messages_en[id] ? messages_en[id] : "Unknown message";
#endif
}
