#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "P2P"

#include "p2p_internal.h"
#include "p2p_probe.h"
#include "p2p_thread.h"
#include "p2p_udp.h"
#include "LANG.cn.h"

/* ---- 信令重发配置 ---- */
#define SIGNAL_MAX_RESEND_COUNT             12     /* 最大重发次数 (共约60秒) */
#define SIGNAL_RESEND_INTERVAL_MS           5000   /* 信令重发间隔 (5秒，relay/compact/等) */
#define SIGNAL_RESEND_INTERVAL_PUBSUB_MS    15000  /* PUBSUB 重发间隔 (15秒，给对方充足时间回复) */

/* ---- 路径管理配置 ---- */
#define PATH_RESELECT_INTERVAL_MS           5000   /* 路径重选间隔 (5秒检查一次是否有更优路径) */


///////////////////////////////////////////////////////////////////////////////
// 日志系统
///////////////////////////////////////////////////////////////////////////////

p2p_log_callback_t   p2p_log_callback = (p2p_log_callback_t)-1;
#ifndef NDEBUG
p2p_log_level_t      p2p_log_level = P2P_LOG_LEVEL_DEBUG;
#else
p2p_log_level_t      p2p_log_level = P2P_LOG_LEVEL_INFO;
#endif
bool                 p2p_log_pre_tag = false;

#ifndef NDEBUG
uint16_t             p2p_instrument_base = 0;
#endif

///////////////////////////////////////////////////////////////////////////////

#ifdef P2P_THREADED
#define LOCK(s)   do { if ((s)->cfg.threaded) P_mutex_lock(&(s)->mtx); } while(0)
#define UNLOCK(s) do { if ((s)->cfg.threaded) P_mutex_unlock(&(s)->mtx); } while(0)
#else
#define LOCK(s)   ((void)0)
#define UNLOCK(s) ((void)0)
#endif

// 解析主机名
static inline ret_t resolve_host(const char *host, uint16_t port, struct sockaddr_in *out) {

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;

    memcpy(out, res->ai_addr, sizeof(*out));
    freeaddrinfo(res);
    return E_NONE;
}

static inline void gather_local_candidates(p2p_session_t *s) {

    s->local_cand_cnt = 0;

    /* ======================== 1. 收集 Host 候选 ======================== */
    /*
     * Host Candidate 是本地网卡的 IP 地址。
     * 在同一局域网内的对端可以直接使用此地址通信。
     */
    if (!s->cfg.test_ice_host_off) {

        /* 获取本地端口 */
        struct sockaddr_in loc; socklen_t len = sizeof(loc);
        getsockname(s->sock, (struct sockaddr *)&loc, &len);

        int host_index = 0;  /* 用于区分多个 Host 候选的本地偏好值 */

        for (int i = 0; i < s->route.addr_count; i++) {
            int idx = p2p_cand_push_local(s);
            if (idx < 0) {
                print("E:", LA_S("Push local cand<%s:%d> failed(OOM)\n", LA_S39, 39),
                      inet_ntoa(s->route.local_addrs[i].sin_addr), ntohs(s->route.local_addrs[i].sin_port));
                return;
            }

            p2p_local_candidate_entry_t *c = &s->local_cands[idx];
            c->type = P2P_CAND_HOST;
            c->addr = s->route.local_addrs[i];
            c->addr.sin_port = loc.sin_port;  // 使用实际绑定端口

            uint16_t local_pref = (uint16_t)(65535 - host_index++);
            c->priority = p2p_ice_calc_priority(P2P_ICE_CAND_HOST, local_pref, 1);

            print("I:", LA_F("Gathered Host candidate: %s:%d (priority=0x%08x)", LA_F203, 203),
                  inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), c->priority);
        }
    }
    else print("I:", LA_F("Skipping Host Candidate gathering (disabled)", LA_F334, 334));

    /* ======================== 2. 收集 Srflx 候选 ======================== */
    /*
     * Server Reflexive Candidate 是通过 STUN 服务器发现的公网地址。
     * 用于穿透 NAT，让位于不同 NAT 后的对端能够通信。
     */
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT
        && !s->cfg.test_ice_srflx_off && s->cfg.stun_server) {

        uint8_t stun_buf[256];
        int slen = p2p_stun_build_binding_request(stun_buf, sizeof(stun_buf), NULL, NULL, NULL);
        if (slen > 0) {

            /* 解析 STUN 服务器地址并发送请求 */
            struct sockaddr_in stun_addr;
            memset(&stun_addr, 0, sizeof(stun_addr));
            stun_addr.sin_family = AF_INET;
            stun_addr.sin_port = htons(s->cfg.stun_port ? s->cfg.stun_port : 3478);
            struct hostent *he = gethostbyname(s->cfg.stun_server);
            if (he) {
                memcpy(&stun_addr.sin_addr, he->h_addr_list[0], he->h_length);
                p2p_udp_send_to(s, &stun_addr, stun_buf, slen);
                print("I:", LA_F("Requested Srflx Candidate from %s", LA_F316, 316), s->cfg.stun_server);
            }
        }
    }

    /* ======================== 3. 收集 Relay 候选 ======================== */
    /*
     * Relay Candidate 是通过 TURN 服务器分配的中继地址。
     * 当直连和 STUN 穿透都失败时，使用中继作为最后的备选。
     */
    if (!s->cfg.test_ice_relay_off && s->cfg.turn_server) {
        if (p2p_turn_allocate(s) == 0) {
            print("I:", LA_F("Requested Relay Candidate from TURN %s", LA_F314, 314), s->cfg.turn_server);
        }
    }

    /* ======================== 4. TCP 候选（可选） ======================== */
    /*
     * RFC 6544 扩展了 ICE 以支持 TCP 候选。
     * 目前仅预留接口，未完全实现。
     */
    if (s->cfg.enable_tcp) {
        for (int i = 0; i < s->local_cand_cnt; i++) {
            if (s->local_cands[i].type == P2P_CAND_HOST) {
                /* TODO: 建立 TCP 监听端口 */
            }
        }
    }
}

/*
 * 统一状态转换 — 更新状态并触发回调
 *
 * 所有 P2P 连接状态转换都应通过此函数进行，确保：
 *   1. 状态变化被统一记录
 *   2. on_state 回调被触发
 *
 * 返回：true = 状态已变化，false = 状态未变（old == new）
 */
static inline bool p2p_set_state(p2p_session_t *s, p2p_state_t new_state) {
    
    p2p_state_t old_state = s->state;
    if (old_state == new_state) return false;
    
    s->state = new_state;
    
    // 触发状态回调
    if (s->cfg.on_state) {
        s->cfg.on_state(s, old_state, new_state, s->cfg.userdata);
    }
    
    return true;
}

/*
 * NAT_CONNECTED 同步转换为 P2P_STATE_CONNECTED
 * 
 * NAT 模块在进入 NAT_CONNECTED 状态时同步调用此函数，触发：
 *   - 路径选择和切换
 *   - P2P 状态转换
 *   - on_connected 事件回调
 * 
 * 目的：确保应用层的 on_connected 总是在数据包处理之前触发（数据层会话一致性契约）。
 * 
 * 参数:
 *   s       - P2P 会话
 *   now_ms  - 当前时间戳（毫秒）
 * 
 * 使用场景：
 *   - NAT 模块收到 CONN/CONN_ACK/DATA 后进入 NAT_CONNECTED 状态
 *   - NAT 握手完成（双向 REACH 确认后收到 CONN_ACK）
 */
void p2p_connected(p2p_session_t *s, uint64_t now_ms) {
    
    // 仅在 PUNCHING/REGISTERING/REGISTERED 状态下转换
    // NAT 可能从 LOST → CONNECTED，但 p2p 状态可能是 RELAY，不在此处理
    if (s->state < P2P_STATE_REGISTERING || s->state > P2P_STATE_PUNCHING) {
        return;
    }

    // 选择最佳路径
    int best_path = path_manager_select_best_path(s);
    if (best_path < -1) {  // -2=无可用路径
        print("E:", LA_F("NAT connected but no available path in path manager", LA_F440, 440));
        return;
    }

    if (best_path != s->active_path)
        p2p_set_active_path(s, best_path);

    path_manager_switch_reset(s, now_ms);
    
    // P2P 状态与 NAT 状态同步（NAT_RELAY ↔ P2P_STATE_RELAY）
    if (s->nat.state == NAT_RELAY) {
        print("I:", LA_F("State: → RELAY, path[%d]", LA_F348, 348), best_path);
        p2p_set_state(s, P2P_STATE_RELAY);
    } else {
        print("I:", LA_F("State: → CONNECTED, path[%d]", LA_F343, 343), best_path);
        p2p_set_state(s, P2P_STATE_CONNECTED);
    }
}

/*
 * 主动断开 — 通过 NAT 层和信令层双通道通知对端
 *
 * NAT FIN:  UDP 直连（不可靠，重复发送提高成功率）
 * 信令 FIN: 通过信令服务器转发（COMPACT: UNREGISTER → PEER_OFF）
 *
 * 由 p2p_close 调用；p2p_destroy 作为兜底也会调用
 */
static void disconnect(p2p_session_t *s) {

    assert(s->state != P2P_STATE_CLOSED);

    p2p_state_t old_state = s->state;
    p2p_session_reset(s, true);  // 这会设置 s->state = P2P_STATE_CLOSED

    // NAT 层 FIN（仅在已连接状态，重复发送提高 UDP 可靠性）
    if (old_state >= P2P_STATE_LOST) {

        print("V:", LA_F("Sending FIN packet to peer before closing", LA_F326, 326));

        nat_send_fin(s);

        // 触发回调
        if (s->cfg.on_state) s->cfg.on_state(s, old_state, P2P_STATE_CLOSED, s->cfg.userdata);
    }

    // COMPACT 信令模式：取消与对方在服务器上的注册，UNREGISTER
    // + COMPACT 信令的 unregister 等价于 disconnect & logout
    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {

        if (s->sig_compact_ctx.state != SIGNAL_COMPACT_INIT) {
            print("I:", LA_F("Sending UNREGISTER packet to COMPACT signaling server", LA_F328, 328));
            p2p_signal_compact_disconnect(s);
        }
    } 
    // RELAY 信令模式：
    else if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY) {

        p2p_signal_relay_disconnect(s);
    }
}

/*
 * 被动关闭 — 对端断开时的状态转换和回调
 */
static void peer_disconnect(p2p_session_t *s) {

    // 信令信令层的 FIN 信号会统一转换为 NAT 层的 FIN 信号
    assert(s->nat.state == NAT_CLOSED);

    assert(s->state >= P2P_STATE_LOST);

    print("I:", LA_F("connection closed by peer", LA_F403, 403));

    p2p_state_t old_state = s->state;
    p2p_probe_state_t prev_probe_state = s->probe_ctx.state;
    p2p_session_reset(s, true);  // 这会设置 s->state = P2P_STATE_CLOSED

    // 信道外探测基于信令服务器 RPC 转发，而对方断开连接，不影响自己和服务器的注册状态
    if (prev_probe_state != P2P_PROBE_STATE_OFFLINE) {
        s->probe_ctx.state = prev_probe_state == P2P_PROBE_STATE_NO_SUPPORT
                ? P2P_PROBE_STATE_NO_SUPPORT
                : P2P_PROBE_STATE_READY;
    }

    // 触发状态回调
    if (s->cfg.on_state) s->cfg.on_state(s, old_state, P2P_STATE_CLOSED, s->cfg.userdata);
}

///////////////////////////////////////////////////////////////////////////////

p2p_handle_t
p2p_create(const char *local_peer_id, const p2p_config_t *cfg) {

    P_check(cfg && local_peer_id, return NULL;)

    ret_t ret;

    static int s_initialized = 0;
    if (!s_initialized) { s_initialized = 1;

        // 初始化语言环境（多语言支持）
        LA_init();

        // 初始化随机数生成器（线程安全，幂等）
        P_rand_init();

        // 初始化网络子系统（Windows 需要，POSIX 是 no-op）
        if ((ret = P_net_init()) != E_NONE) {
            print("E:", LA_F("Initialize network subsystem failed(%d)", LA_F261, 261), ret);
            return NULL;
        }

        instrument_listen(p2p_instrument, NULL);
    }

    if (cfg->signaling_mode == P2P_SIGNALING_MODE_PUBSUB) {
        if (!cfg->gh_token || !cfg->gist_id) {
            print("E:", LA_F("PUBSUB mode requires gh_token and gist_id", LA_F282, 282));
            return NULL;
        }
    }
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT || cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) {
        if (!cfg->server_host) {
            print("E:", LA_F("RELAY/COMPACT mode requires server_host", LA_F297, 297));
            return NULL;
        }
    } else {
        print("E:", LA_F("Invalid signaling mode in configuration", LA_F266, 266));
        return NULL;
    }

    p2p_session_t *s = (p2p_session_t*)calloc(1, sizeof(*s));
    if (!s) {
        print("E:", LA_F("Failed to allocate memory for session", LA_F241, 241));
        return NULL;
    }

    // 创建 UDP 套接字，port 0 为有效值，表示由操作系统分配随机端口
    print("I:", LA_F("Open P2P UDP socket on port %d", LA_F273, 273), cfg->bind_port);
    if ((ret = p2p_udp_open(s, cfg->bind_port)) != E_NONE) {
        print("E:", LA_F("Open P2P UDP socket on port %d failed(%d)", LA_F274, 274), cfg->bind_port, ret);
        free(s);
        return NULL;
    }

    // 初始化信令服务模式
    print("I:", LA_F("Initialize signaling mode: %d", LA_F262, 262), (int)cfg->signaling_mode);
    if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT) p2p_signal_compact_init(&s->sig_compact_ctx);
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) p2p_signal_relay_init(&s->sig_relay_ctx);
    else if (s->cfg.signaling_mode == P2P_SIGNALING_MODE_ICE) s->cfg.use_ice = true;    /* ICE 模式隐含 use_ice=true */
    else if ((ret = p2p_signal_pubsub_init(&s->sig_pubsub_ctx, cfg->gh_token, cfg->gist_id)) != E_NONE) {
        print("E:", LA_F("Initialize PUBSUB signaling context failed(%d)", LA_F260, 260), ret);
        p2p_udp_close(s); free(s);
        return NULL;
    } else if (cfg->auth_key) {
        strncpy(s->sig_pubsub_ctx.auth_key, cfg->auth_key, sizeof(s->sig_pubsub_ctx.auth_key) - 1);
    }

    // 如果没有配置 STUN 服务器，并且未使用 COMPACT 模式，则无法进行 NAT 类型检测
    s->nat_type = P2P_NAT_UNKNOWN;
    if (!s->cfg.stun_server && cfg->signaling_mode != P2P_SIGNALING_MODE_COMPACT) {
        s->nat_type = P2P_NAT_UNDETECTABLE;
    }

    // 初始化路由层（用于检测是否处于同一子网）
    route_init(&s->route);

    // 获取本地所有有效的网络地址
    if ((ret = route_detect_local(&s->route)) < 0) {
        print("E:", LA_F("Detect local network interfaces failed(%d)", LA_F230, 230), ret);
        p2p_udp_close(s); free(s);
        return NULL;
    }

    // 初始化候选地址数组（动态分配，初始容量 8）
    const int initial_cand_cap = 8;
    s->local_cands  = (p2p_local_candidate_entry_t *)calloc(initial_cand_cap, sizeof(p2p_local_candidate_entry_t));
    s->remote_cands = (p2p_remote_candidate_entry_t *)calloc(initial_cand_cap, sizeof(p2p_remote_candidate_entry_t));
    if (!s->local_cands || !s->remote_cands) {
        print("E:", LA_F("Failed to allocate memory for candidate lists", LA_F240, 240));
        if (s->local_cands) free(s->local_cands);
        if (s->remote_cands) free(s->remote_cands);
        p2p_udp_close(s); free(s);
        return NULL;
    }
    s->local_cand_cap = s->remote_cand_cap = initial_cand_cap;

    s->cfg = *cfg;
    if (s->cfg.update_interval_ms <= 0) s->cfg.update_interval_ms = 10;
    
    // 初始化本端身份标识
    memset(s->local_peer_id, 0, sizeof(s->local_peer_id));
    strncpy(s->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);

    s->tcp_sock = P_INVALID_SOCKET;

    // 初始化虚拟链路层（基于 NAT 穿透的 P2P）
    nat_init(&s->nat);

    // 初始化路径管理器（多路径并行支持）
    p2p_path_strategy_t strategy = (p2p_path_strategy_t)(cfg->path_strategy);
    if (strategy < P2P_PATH_STRATEGY_CONNECTION_FIRST || strategy > P2P_PATH_STRATEGY_HYBRID) {
        strategy = P2P_PATH_STRATEGY_CONNECTION_FIRST; // 默认：直连优先
    }
    path_manager_init(s, strategy);
    print("I:", LA_F("Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)", LA_F286, 286), strategy);

    // 初始化基础传输层（reliable ARQ）
    reliable_init(s);

    // 初始化传输层（可靠性模块，与加密层正交）
    s->trans = NULL;
    
    if (cfg->use_sctp) {
#ifdef WITH_SCTP
        print("I:", LA_F("SCTP (usrsctp) enabled as transport layer", LA_F289, 289));
        s->trans = &p2p_trans_sctp;
#else
        print("W:", LA_F("SCTP (usrsctp) requested but library not linked", LA_F318, 318));
#endif
    }
    else if (cfg->use_pseudotcp) {
        print("I:", LA_F("PseudoTCP enabled as transport layer", LA_F293, 293));
        s->trans = &p2p_trans_pseudotcp;
    }
    else print("I:", LA_F("No advanced transport layer enabled, using simple reliable layer", LA_F270, 270));

    // 执行传输模块的初始化处理
    if (s->trans && s->trans->init) {
        if (s->trans->init(s) != 0) {
            print("E:", LA_F("Transport layer '%s' init failed, falling back to simple reliable", LA_F367, 367), s->trans->name);
            s->trans = NULL;
            s->trans_data = NULL;
        }
    }

    // 初始化加密层（与传输层正交，DTLS 后端选择）
    s->dtls = NULL;
    s->dtls_data = NULL;
    
    if (cfg->dtls_backend == 1) {
#ifdef WITH_DTLS
        print("I:", LA_F("DTLS (MbedTLS) enabled as encryption layer", LA_F196, 196));
        s->dtls = &p2p_dtls_mbedtls;
#else
        print("W:", LA_F("DTLS (MbedTLS) requested but library not linked", LA_F227, 227));
#endif
    }
    else if (cfg->dtls_backend == 2) {
#ifdef WITH_OPENSSL
        print("I:", LA_F("OpenSSL DTLS enabled as encryption layer", LA_F243, 243));
        s->dtls = &p2p_dtls_openssl;
#else
        print("W:", LA_F("OpenSSL requested but library not linked", LA_F275, 275));
#endif
    }

    // 注：DTLS init 延迟到 p2p_connect()，因为自动角色需要知道 remote_peer_id

    // 初始化数据流层
    stream_init(&s->stream, cfg->nagle);

    // 初始化探测上下文（初始状态为 OFFLINE）
    probe_init(&s->probe_ctx);

    //----------------------------

    p2p_turn_init(&s->turn);

    s->signaling_mode = cfg->signaling_mode;
    s->state = P2P_STATE_INIT;
    s->path_type = P2P_PATH_NONE;
    s->active_path = PATH_IDX_NONE;

    // 收集本地候选地址（Host/TURN）
    gather_local_candidates(s);

    //----------------------------

    s->last_update = P_tick_ms();

#ifdef P2P_THREADED
    if (cfg->threaded) {
        print("I:", LA_F("Starting internal thread", LA_F337, 337));
        if ((ret = p2p_thread_start(s)) != E_NONE) {
            print("E:", LA_F("Start internal thread failed(%d)", LA_F336, 336), ret);
            P_sock_close(s->sock);
            free(s->local_cands); free(s->remote_cands); free(s);
            return NULL;
        }
    }
#endif

    return (p2p_handle_t)s;
}

void
p2p_destroy(p2p_handle_t hdl) {
    if (!hdl) return;

    p2p_session_t *s = (p2p_session_t*)hdl;

#ifdef P2P_THREADED
    if (s->cfg.threaded) {
        print("I:", LA_F("Stopping internal thread", LA_F349, 349));
        p2p_thread_stop(s);
    }
#endif

    // 重置信道外 NAT 探测
    probe_reset(s);

    // 兜底：如果没有调用过 p2p_close，执行 disconnect
    if (s->state != P2P_STATE_CLOSED) {
        disconnect(s);
    }

    //  COMPACT 模式的 server UDP 套接口和 NAT p2p 是同一个
    if (s->sock != P_INVALID_SOCKET) {

        // 关闭加密层
        if (s->dtls && s->dtls->close) {
            s->dtls->close(s);
            s->dtls = NULL;
        }

        // 关闭高级传输层（在 socket 关闭前，以便发送 close_notify 等协议包）
        if (s->trans && s->trans->close) {
            s->trans->close(s);
            s->trans = NULL;
        }

        print("I:", LA_F("Close P2P UDP socket", LA_F215, 215));
        P_sock_close(s->sock);
    }

    // RELAY 信令模式：关闭 TCP 长连接（断开和服务器的连接, logout）
    if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY
        && s->sig_relay_ctx.state != SIGNAL_RELAY_INIT) {

        print("I:", LA_F("Closing TCP connection to RELAY signaling server", LA_F216, 216));
        p2p_signal_relay_offline(s);
    }

    free(s->local_cands);
    free(s->remote_cands);
    free(s);
}

///////////////////////////////////////////////////////////////////////////////

int
p2p_connect(p2p_handle_t hdl, const char *remote_peer_id) {

    P_check(hdl, return -1;)

    p2p_session_t *s = (p2p_session_t*)hdl;
    P_check(s->state == P2P_STATE_INIT, return -1;)

    if (remote_peer_id && !*remote_peer_id) remote_peer_id = NULL;

    // COMPACT 模式必须指定 remote_peer_id
    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT && !remote_peer_id) {
        print("E:", LA_F("COMPACT mode requires explicit remote_peer_id", LA_F211, 211));
        s->state = P2P_STATE_ERROR;
        return -1;
    }

    LOCK(s);

    // 设置对方 id
    if (remote_peer_id) {
        memset(s->remote_peer_id, 0, sizeof(s->remote_peer_id));
        strncpy(s->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    } else s->remote_peer_id[0] = '\0';

    // 初始化 DTLS 加密层（需要 remote_peer_id 以确定自动角色）
    if (s->dtls && s->dtls->init) {
        if (s->dtls->init(s) != 0) {
            print("E:", LA_F("Crypto layer '%s' init failed, continuing without encryption", LA_F226, 226), s->dtls->name);
            s->dtls = NULL;
            s->dtls_data = NULL;
        }
    }

    ret_t ret;
    switch (s->signaling_mode) {

        // 对于 COMPACT 模式
        case P2P_SIGNALING_MODE_COMPACT: {

            // 解析信令服务器地址
            assert(s->cfg.server_host);         // p2p_create 成功会确保这个条件
            struct sockaddr_in server_addr;
            if ((ret = resolve_host(s->cfg.server_host, s->cfg.server_port, &server_addr)) != E_NONE) {
                print("E:", LA_F("Resolve COMPACT signaling server address: %s:%d failed(%d)", LA_F317, 317),
                             s->cfg.server_host, s->cfg.server_port, ret);
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }

            s->state = P2P_STATE_REGISTERING;   // 进入注册阶段

            // 注册（连接）到 COMPACT 信令服务器
            print("I:", LA_F("Register to COMPACT signaling server at %s:%d", LA_F312, 312),
                         inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

            if ((ret = p2p_signal_compact_connect(s, s->local_peer_id, remote_peer_id, &server_addr)) != E_NONE) {
                print("E:", LA_F("Connect to COMPACT signaling server failed(%d)", LA_F217, 217), ret);
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }

            break;
        }

        // 对于 RELAY 模式
        case P2P_SIGNALING_MODE_RELAY: {

            assert(s->cfg.server_host);

            // 首次连接：自动登录信令服务器（只执行一次）
            if (s->sig_relay_ctx.state == SIGNAL_RELAY_INIT) {

                print("I:", LA_F("Connecting to RELAY signaling server at %s:%d", LA_F220, 220),
                             s->cfg.server_host, s->cfg.server_port);

                struct sockaddr_in server_addr;
                memset(&server_addr, 0, sizeof(server_addr));
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(s->cfg.server_port);
                inet_pton(AF_INET, s->cfg.server_host, &server_addr.sin_addr);

                if ((ret = p2p_signal_relay_online(s, s->local_peer_id, &server_addr)) != E_NONE) {
                    print("E:", LA_F("Connect to RELAY signaling server failed(%d)", LA_F218, 218), ret);
                    s->state = P2P_STATE_ERROR;
                    UNLOCK(s);
                    return -1;
                }
            }

            // 等待上线
            if (s->sig_relay_ctx.state < SIGNAL_RELAY_ONLINE) {
                print("I:", LA_F("Waiting for RELAY server ONLINE_ACK", LA_F535, 535));
                UNLOCK(s);
                return 0;
            }

            s->state = P2P_STATE_REGISTERING;

            // 如果指定了 remote_peer_id，立即发送初始 offer
            if (remote_peer_id && s->local_cand_cnt > 0) {

                // RELAY 模式：发送 CONNECT 请求建立会话
                print("I:", LA_F("Starting RELAY session with %s", LA_F332, 332), remote_peer_id);
                if ((ret = p2p_signal_relay_connect(s, remote_peer_id)) != E_NONE) {
                    print("E:", LA_F("Start RELAY session failed(%d)", LA_F323, 323), ret);
                }
            }
            // 被动模式：等待任意对等方的 offer
            else if (!remote_peer_id) {

                print("I: %s", LA_W("Waiting for incoming offer from any peer", LA_W21, 21));
            }

            // 注意：后续 Srflx 候选者（STUN 响应）会在 p2p_update 中增量发送
            break;
        }

        // 对于 PUBSUB 模式
        case P2P_SIGNALING_MODE_PUBSUB: {

            assert(s->cfg.gh_token && s->cfg.gist_id);

            s->state = P2P_STATE_REGISTERING;

            // 如果指定了 remote_peer_id
            if (remote_peer_id) {

                // 说明是 PUB 角色（发布者），主动发起连接
                p2p_signal_pubsub_set_role(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_PUB);

                // PUB 模式必须等待 STUN 响应（获取公网地址）后才能发布 offer
                // + PUB/SUB 不支持 Trickle ICE 模式，所以必须等候选收集完成后一次性发送
                print("I:", LA_F("PUBSUB (PUB): gathering candidates, waiting for STUN before publishing", LA_F280, 280));
            }
            else {

                // SUB 角色（订阅者）：被动等待连接
                p2p_signal_pubsub_set_role(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_SUB);

                // SUB 模式：被动等待 offer，收到后自动回复
                print("I:", LA_F("PUBSUB (SUB): waiting for offer from any peer", LA_F281, 281));
            }
            break;
        }

        // 对于 ICE 模式（应用层自定义信令 + ICE 标准协议）
        case P2P_SIGNALING_MODE_ICE: {

            s->state = P2P_STATE_PUNCHING;

            nat_punch(s, -1);

            // 应用层自行负责信令通道：将收集到的候选发给对端，并通过 p2p_import_ice_sdp() 导入对端候选
            print("I: %s", "ICE mode: starting NAT punching, waiting for application to exchange candidates and SDP via custom signaling");
            break;
        }

        default:
            print("E:", LA_F("Unknown signaling mode: %d", LA_F371, 371), s->signaling_mode);
            s->state = P2P_STATE_ERROR;
            UNLOCK(s);
            return -1;
    }

    UNLOCK(s);
    return 0;
}

void
p2p_close(p2p_handle_t hdl) {

    if (!hdl) return;

    p2p_session_t *s = (p2p_session_t*)hdl;

    LOCK(s);

    if (s->state == P2P_STATE_INIT || s->state == P2P_STATE_CLOSED) {
        UNLOCK(s);
        return;
    }

    // 主动断开（NAT FIN + 信令层 disconnect）
    disconnect(s);
    
    UNLOCK(s);
}

/*
 * 主更新循环 — 驱动所有状态机。
 * > 在单线程模式下，应用程序调用此函数
 * > 在多线程模式下，内部线程在锁下调用此函数
 * 
 * ============================================================================
 * 执行流程说明（按逻辑顺序组织）
 * ============================================================================
 *
 * 阶段 1: 远程数据输入（被动接收）
 *   - 从 UDP socket 接收数据包
 *   - 分发到各个协议处理器（STUN/TURN/P2P）
 *   - 更新本地状态（NAT 打洞确认、数据接收等）
 * 
 * 阶段 2: 信令服务维护（主动拉取）
 *   - COMPACT: 维护与 UDP 信令服务器的注册状态
 *   - RELAY: 维护 TCP 长连接，接收对方候选
 *   - PUBSUB: 轮询 GitHub Gist，检查对方发布的 offer/answer
 * 
 * 阶段 3: ICE 候选收集
 *   - 向 STUN 服务器发送探测请求
 *   - 向 TURN 服务器发送分配请求
 *   - 收集本地反射地址（Srflx）和中继地址（Relay）
 * 
 * 阶段 4: NAT 层维护
 *   - 向对端候选地址发送 PUNCH 包（NAT 打洞）
 *   - 维护已建立连接的保活（定期发送心跳）
 *   - 检测连接超时（转 NAT_LOST 状态）
 *   - LAN 路径升级（同子网直连优化）
 * 
 * 阶段 5: 统一状态机（集中处理所有状态转换）
 *   - REGISTERING → PUNCHING: 开始 NAT 打洞
 *   - PUNCHING/REGISTERING → CONNECTED: NAT 穿透成功
 *   - PUNCHING → RELAY: NAT 打洞失败，降级中继
 *   - CONNECTED → RELAY: NAT 连接超时，降级中继
 * 
 * 阶段 6: 数据传输
 *   - 应用层数据 → 传输层（DTLS/SCTP/Reliable）
 *   - 传输层 tick（重传、拥塞控制）
 *   - 传输层 → 应用层接收缓冲区
 * 
 * 阶段 7: 信令输出（主动推送）
 *   - RELAY: Trickle ICE 增量发送候选（含断点续传）
 *   - PUBSUB: 发布 offer（PUB 角色）
 *
 * 阶段 8: 连接监控与维护
 *  - 监控连接质量（RTT、丢包率等）
 *  - 根据策略动态切换路径（直连/中继）
 *  - 连接异常处理（重试、降级、断开）
 *
 * 阶段 9: NAT 类型检测
 *   - 后台定期运行 STUN 探测
 *   - 检测 NAT 类型（Full Cone / Symmetric 等）
 * 
 * ============================================================================
 * 设计原则
 * ============================================================================
 * 
 * 1. **远程输入优先**：先处理网络接收的数据包，确保及时响应
 * 2. **拉取在前，推送在后**：信令拉取在 ICE/NAT 前获取对端信息，
 *    信令推送在状态机后确保发送最新状态
 * 3. **状态机集中化**：所有 P2P 连接状态转换集中在阶段 5，便于维护
 * 4. **数据传输独立**：仅在已连接状态下执行，与信令/NAT 解耦
 */
int
p2p_update(p2p_handle_t hdl) {

    if (!hdl) return -1;

    p2p_session_t *s = (p2p_session_t*)hdl;

    uint8_t buf[P2P_MTU + 16]; struct sockaddr_in from; uint8_t* pkt; int n;

    uint64_t now_ms = P_tick_ms();

    /* ========================================================================
     * 阶段 1：远程数据输入（被动接收所有网络数据包）
     * ======================================================================== */
    while ((n = p2p_udp_recv_from(s, &from, buf, sizeof(buf))) > 0) { pkt = buf;

        // --------------------
        // STUN/TURN 协议包
        // --------------------
        // 注：TURN 是 STUN 的扩展，共享相同的包格式和 Magic Cookie (0x2112A442)
        //     两个 handler 内部会根据消息类型（Method）分别过滤处理
        if (n >= 20 && pkt[0] < 2) { // STUN type 0x00xx or 0x01xx
            uint8_t* ptr = pkt + 4; /* [4-7]:magic */
            if (nget_l(ptr) == STUN_MAGIC) {

                uint16_t type = nget_s(pkt); /* [0-1]:type */
                printf(LA_F("Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d", LA_F300, 300),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port), type, n);
                
                // 如果使用 ICE 机制进行打洞
                if (s->cfg.use_ice && p2p_stun_has_ice_attrs(pkt, n)) {
                    nat_on_stun_packet(s, &from, now_ms, type, pkt, n);
                    continue;
                }
                
                // STUN 模块处理（NAT 检测 / Srflx 地址探测）
                if (p2p_stun_is_binding_response(type, pkt, n)) {
                    p2p_stun_handle_packet(s, &from, type, pkt, n);
                    continue;
                }

                // TURN 响应处理（Allocate/Refresh/CreatePermission/Data Indication）
                const uint8_t *inner_data = NULL; int inner_len = 0; struct sockaddr_in inner_peer = {0};
                int turn_ret = p2p_turn_handle_packet(s, &from, type, pkt, n,
                                                      &inner_data, &inner_len, &inner_peer);
                if (turn_ret == 1 && inner_data && inner_len >= P2P_HDR_SIZE) {

                    // Data Indication: 解包为内层完整 P2P 包，转换为 P2P 协议继续执行下面主派发循环
                    pkt = (uint8_t*)inner_data; n = inner_len; from = inner_peer;
                    goto dispatch_p2p;
                }

                continue;
            }
        }

        // --------------------
        // P2P 协议（见 p2pp.h）
        // --------------------

        dispatch_p2p:
        if (n < P2P_HDR_SIZE) continue;

        // 获取 header
        p2p_packet_hdr_t hdr;
        p2p_pkt_hdr_decode(pkt, &hdr);

        // 获取 payload
        const uint8_t *payload = pkt + P2P_HDR_SIZE;
        int payload_len = n - P2P_HDR_SIZE;
        uint8_t crypto_dec_buf[P2P_HDR_SIZE + P2P_MAX_PAYLOAD];  /* 解密输出缓冲区 */

        switch (hdr.type) {

            // --------------------
            // 加密层
            // --------------------

            case P2P_PKT_CRYPTO: {

                // flags & P2P_DATA_FLAG_SESSION 表示携带 session_id
                if (hdr.flags & P2P_DATA_FLAG_SESSION) {
                    if (!p2p_signal_compact_relay_validation(s, &payload, &payload_len, "CRYPTO")) break;
                }
                if (!s->dtls) break;

                // 解密密文包
                int dec_len = s->dtls->decrypt_recv(s, payload, payload_len, crypto_dec_buf, sizeof(crypto_dec_buf));
                if (dec_len >= P2P_HDR_SIZE) {

                    // unpack 解密后的内层 P2P 包头和负载
                    hdr.type = crypto_dec_buf[0];
                    hdr.flags = crypto_dec_buf[1];
                    hdr.seq = (uint16_t)((crypto_dec_buf[2] << 8) | crypto_dec_buf[3]);
                    payload = crypto_dec_buf + P2P_HDR_SIZE;
                    payload_len = dec_len - P2P_HDR_SIZE;
                    if (hdr.type == P2P_PKT_DATA) goto handle_data;
                    if (hdr.type == P2P_PKT_ACK) goto handle_ack;
                }
                break;
            }

            // --------------------
            // 数据传输（P2P 直连 或 服务器中继）
            // --------------------

            /*
             * 协议：P2P_PKT_DATA (0x20)
             * 包头: [type=0x20 | flags=见下 | seq=序列号(2B)]
             * 负载 (flags & 0x01 == 0): [data(N)]
             * 负载 (flags & 0x01 == 1): [session_id(8)][data(N)]
             * 说明：P2P 数据包，由 reliable 层或高级传输层处理
             */
            case P2P_PKT_DATA:
                // flags & P2P_DATA_FLAG_SESSION 表示携带 session_id
                if (hdr.flags & P2P_DATA_FLAG_SESSION) {
                    if (!p2p_signal_compact_relay_validation(s, &payload, &payload_len, "DATA")) break;
                }
            handle_data:

                nat_on_data(s, &from, now_ms, hdr.seq, P2P_HDR_SIZE + payload_len);

                // 高级传输层（DTLS/SCTP/PseudoTCP）有自己的解包逻辑
                if (s->trans && s->trans->on_packet)
                    s->trans->on_packet(s, payload, payload_len);
                // 基础 reliable 层处理
                else if (payload_len > 0)
                    reliable_on_data(s, hdr.seq, payload, payload_len);
                
                break;

            /*
             * 协议：P2P_PKT_ACK (0x21)
             * 包头: [type=0x21 | flags=见下 | seq=序列号(2B)]
             * 负载 (flags & 0x01 == 0): [ack_seq(2B) | sack(4B)]
             * 负载 (flags & 0x01 == 1): [session_id(8)][ack_seq(2B) | sack(4B)]
             * 说明：ACK 仅基础 reliable 层使用，DTLS/SCTP 有自己的确认机制
             */
            case P2P_PKT_ACK:
                /* flags & P2P_DATA_FLAG_SESSION 表示携带 session_id */
                if (hdr.flags & P2P_DATA_FLAG_SESSION) {
                    if (!p2p_signal_compact_relay_validation(s, &payload, &payload_len, "ACK")) break;
                }
            handle_ack: {

                // 协议不匹配：有独立封装的传输层不应该收到 P2P_PKT_ACK
                // + 注：有 on_packet 的传输层（如 SCTP）会将自己的所有协议数据（含内部 ACK）
                //   都封装在 P2P_PKT_DATA payload 中，类似 P2P_PKT_CRYPTO 将内层包封装为
                //   CRYPTO 包。这种独立协议封装的传输层不使用 P2P_PKT_ACK
                if (s->trans && s->trans->on_packet) {
                    print("E:", LA_F("ACK: protocol mismatch, trans=%s has on_packet but received P2P_PKT_ACK", LA_F200, 200),
                          s->trans->name);
                    break;
                }

                // ACK 包至少要有 ack_seq(2B) + sack(4B) 
                if (payload_len < 6) {
                    print("E:", LA_F("ACK: invalid payload length %d, expected at least 6", LA_F199, 199), payload_len);
                    break;
                }

                uint16_t ack_seq = nget_s(payload); 
                uint32_t sack; nread_l(&sack, payload + 2);

                nat_on_data_ack(s, &from, now_ms, ack_seq, sack);

                // reliable 内部会更新 RTT
                int old_srtt = s->reliable.srtt;
                reliable_on_ack(s, ack_seq, sack, now_ms);
                
                // 这里检测变化后同步到路径管理器
                if (s->reliable.srtt != old_srtt && s->reliable.srtt > 0 && s->active_path >= -1) {
                    path_manager_on_data_rtt(s, s->active_path, (uint32_t)s->reliable.srtt);
                }
                break;
            }
            
            // --------------------
            // NAT 链路层（打洞、保活、断开）
            // --------------------

            // NAT 打洞/保活包
            case P2P_PKT_PUNCH:
                nat_on_punch(s, &hdr, payload, payload_len, &from, now_ms);
                break;
            case P2P_PKT_REACH:
                // 中转版本（通过信令服务器）：验证 session_id 后传给 NAT 层
                if (hdr.flags & P2P_DATA_FLAG_SESSION) {
                    if (p2p_signal_compact_relay_validation(s, &payload, &payload_len, "REACH_RELAYED")) {
                        // session_id 验证成功，清除 RELAYED 标志后传给 NAT 层
                        hdr.flags = 0;
                        nat_on_reach(s, &hdr, payload, payload_len, &from, now_ms);
                    }
                }
                // 直连版本（P2P 路径）：直接传给 NAT 层
                else nat_on_reach(s, &hdr, payload, payload_len, &from, now_ms);
                break;
            case P2P_PKT_CONN:
                nat_on_conn(s, &hdr, &from, now_ms);
                break;
            case P2P_PKT_CONN_ACK:
                nat_on_conn_ack(s, &hdr, &from, now_ms);
                break;
            case P2P_PKT_FIN:
                nat_on_fin(s, &from);
                break;

            // --------------------
            // COMPACT 模式的信令包（REGISTER_ACK、PEER_INFO、PEER_INFO_ACK 等）
            // --------------------

            case SIG_PKT_REGISTER_ACK:
                compact_on_register_ack(s, hdr.seq, hdr.flags, payload, payload_len, &from);
                break;
            case SIG_PKT_ALIVE_ACK:
                compact_on_alive_ack(s, &from);
                break;
            case SIG_PKT_PEER_INFO:
                compact_on_peer_info(s, hdr.seq, hdr.flags, payload, payload_len, &from);
                break;
            case SIG_PKT_PEER_INFO_ACK:
                compact_on_peer_info_ack(s, hdr.seq, payload, payload_len, &from);
                break;
            case SIG_PKT_PEER_OFF:
                compact_on_peer_off(s, payload, payload_len, &from);
                break;
            case SIG_PKT_NAT_PROBE_ACK:
                compact_on_nat_probe_ack(s, hdr.seq, payload, payload_len, &from);
                break;
            case SIG_PKT_MSG_REQ:
                compact_on_request(s, hdr.flags, payload, payload_len, &from);
                break;
            case SIG_PKT_MSG_REQ_ACK:
                compact_on_request_ack(s, payload, payload_len, &from);
                break;
            case SIG_PKT_MSG_RESP:
                compact_on_response(s, hdr.flags, payload, payload_len, &from);
                break;
            case SIG_PKT_MSG_RESP_ACK:
                compact_on_response_ack(s, payload, payload_len, &from);
                break;
            default:
                print("V:", LA_F("Received UNKNOWN pkt type: 0x%02X", LA_F302, 302), hdr.type);
                printf(LA_F("Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d", LA_F301, 301),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port), hdr.type, hdr.seq, payload_len);
                break;
        }

    } // while ((n = p2p_udp_recv_from(s->sock, &from, pkt, sizeof(pkt))) > 0)

    /* ========================================================================
     * 阶段 2：信令服务维护（主动拉取远端候选地址）
     * ======================================================================== */

    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {

        // 信令层维护（注册、等待、候选同步）+ MSG 超时重传
        // 注意：MSG 机制在所有非 INIT 状态下都需要处理超时重传
        if (s->sig_compact_ctx.state == SIGNAL_COMPACT_REGISTERING ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_REGISTERED ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_ICE ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_READY) {
            p2p_signal_compact_tick_recv(s);
        }
    }
    else if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY) {

        p2p_signal_relay_tick_recv(s);
    }
    else { assert(s->signaling_mode == P2P_SIGNALING_MODE_PUBSUB);
        
        p2p_signal_pubsub_tick_recv(&s->sig_pubsub_ctx, s);
    }

    /* ========================================================================
     * 阶段 3：TURN 定时维护（Refresh 续期、权限同步）
     * ======================================================================== */

    p2p_turn_tick(s, now_ms);

    /* ========================================================================
     * 阶段 4：NAT 层维护（打洞、保活）
     * ======================================================================== */

    if (s->nat.state != NAT_INIT) {
        nat_tick(s, now_ms);
    }

    /* ========================================================================
     * 阶段 5：统一状态机（集中处理所有 P2P 连接状态转换）
     * ======================================================================== */

    // 转换：REGISTERING/REGISTERED → PUNCHING（开始打洞）
    if ((s->state == P2P_STATE_REGISTERING || s->state == P2P_STATE_REGISTERED)
        && (s->nat.state == NAT_PUNCHING || s->nat.state == NAT_CONNECTING)) {

        print("I:", LA_F("State: → PUNCHING", LA_F345, 345));
        p2p_set_state(s, P2P_STATE_PUNCHING);
    }

    // NAT_CONNECTED 状态转换已由 nat 模块通过 p2p_connected() 同步触发
    // + 同步转换保证了数据包处理总是在 on_connected 事件之后发生（数据层会话一致性契约）
    // + 此处不再需要异步协同（NAT_CONNECTED → P2P_STATE_CONNECTED）

    // NAT_RELAY 状态转换已由 nat 模块通过 p2p_connected() 同步触发
    // relay 候选也需要通过 NAT 层 connect 握手，已统一处理

    // 转换：PUNCHING → ERROR（NAT 打洞超时且无中继服务）
    if ((s->state == P2P_STATE_PUNCHING || s->state == P2P_STATE_REGISTERING || s->state == P2P_STATE_REGISTERED)
        && s->nat.state == NAT_CLOSED) {

        print("E:", LA_F("State: → ERROR (punch timeout, no relay available)", LA_F344, 344));
        p2p_set_state(s, P2P_STATE_ERROR);
        // NAT_CLOSED 表示打洞失败且无信令中转服务，连接已不可恢复
    }

    // NAT 重新连接后恢复路径（NAT_RELAY → NAT_CONNECTED）
    if (s->state == P2P_STATE_RELAY
        && s->nat.state == NAT_CONNECTED) {
        
        // 标记中继路径为降级（但不移除，保留作为备份）
        path_manager_set_path_state(s, PATH_IDX_SIGNALING, PATH_STATE_DEGRADED);
        
        // 重新选择最佳路径（PUNCH 应当优先）
        int best_path = path_manager_select_best_path(s);
        if (best_path >= -1) {  // -1=SIGNALING, >=0=候选
            p2p_set_active_path(s, best_path);
            path_manager_switch_reset(s, now_ms);
            print("I:", LA_F("State: RELAY → CONNECTED, path=PUNCH[%d]", LA_F342, 342), best_path);
            p2p_set_state(s, P2P_STATE_CONNECTED);
        } else {
            print("W:", LA_F("RELAY recovery: NAT connected but no path available", LA_F441, 441));
        }
    }

    // 转换：CONNECTED/RELAY → LOST（NAT 连接丢失）
    // NAT_LOST 表示所有数据通道都断了（PUNCH/TURN/SIGNALING），等待恢复
    if ((s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY)
        && s->nat.state == NAT_LOST) {
        
        // 标记当前活跃路径为失效
        path_manager_set_path_state(s, s->active_path, PATH_STATE_FAILED);
        
        // 清除活跃路径
        p2p_set_active_path(s, PATH_IDX_NONE);
        
        print("W:", LA_F("State: → LOST (all paths failed)", LA_F338, 338));
        p2p_set_state(s, P2P_STATE_LOST);
    }

    // 转换：LOST → CONNECTED（NAT 连接恢复）
    if (s->state == P2P_STATE_LOST
        && (s->nat.state == NAT_CONNECTED || s->nat.state == NAT_RELAY)) {
        
        int best_path = path_manager_select_best_path(s);
        if (best_path >= -1) {
            // 状态机立即切换（不走防抖）
            p2p_set_active_path(s, best_path);
            path_manager_switch_reset(s, now_ms);
            print("I:", LA_F("State: LOST → CONNECTED, path=PUNCH[%d]", LA_F341, 341), best_path);
            p2p_set_state(s, P2P_STATE_CONNECTED);
        } else {
            // 无可用路径，保持 LOST 状态
            print("W:", LA_F("LOST recovery: NAT connected but no path available", LA_F430, 430));
        }
    }

    // 转换：CONNECTED/RELAY/LOST → CLOSED
    // + NAT FIN 和信令 PEER_OFF 都归一化为 NAT_CLOSED，统一在此处理
    if (s->state >= P2P_STATE_LOST
        && s->nat.state == NAT_CLOSED) {
        peer_disconnect(s);
    }

    /* ========================================================================
     * 阶段 6：数据传输（应用层数据收发）
     * ======================================================================== */

    // 发送数据：数据流层 → 传输层 flush 写入
    if (s->state > P2P_STATE_LOST) {
        
        // 如果使用高级传输层（DTLS、SCTP、PseudoTCP）
        if (s->trans && s->trans->send_data) {

            // 传输层就绪检查：DTLS 握手完成前不 drain 数据，避免丢失
            int ready = !s->trans->is_ready || s->trans->is_ready(s);
            if (ready) {
                // 由高级传输模块自行处理流的数据
                n = ring_read(&s->stream.send_ring, pkt, sizeof(pkt));
                if (n > 0) {
                    int sent = s->trans->send_data(s, pkt, n);
                    if (sent > 0) {
                        s->stream.pending_bytes -= n;
                        s->stream.send_offset += n;
                    } else {
                        // send_data 失败，数据已从 ring 消费，记录丢失
                        print("W:", LA_F("transport send_data failed, %d bytes dropped", LA_F416, 416), n);
                    }
                }
            }
        }
        // 使用基础 reliable 层
        else stream_flush_to_reliable(s);
    }

    // 如果使用了高级传输层（如 DTLS/SCTP/PseudoTCP）
    if (s->trans) {

        // 传输模块周期 tick（重传，拥塞控制等）
        if (s->trans->tick) {
            if (s->state > P2P_STATE_LOST) {
                s->trans->tick(s);
            }
        }

        // 将高级传输层统计同步到路径管理器（Group 2: 数据层 RTT + 丢包率）
        if (s->trans->get_stats && s->active_path >= -1) {
            uint32_t rtt_ms = 0;
            float loss_rate = 0.0f;
            
            if (s->trans->get_stats(s, &rtt_ms, &loss_rate) == 0) {

                if (rtt_ms > 0)
                    path_manager_on_data_rtt(s, s->active_path, rtt_ms);

                // 上报数据层丢包率（之前被忽略，导致路径质量评估缺少数据层信息）
                if (loss_rate > 0.0f)
                    path_manager_on_data_loss_rate(s, s->active_path, loss_rate);
            }
        }
    }
    // reliable 周期 tick：发送/重传数据包 + 发 ACK
    // 注：仅在无高级传输层时调用。PseudoTCP 虽然 on_packet==NULL（复用 reliable 收包），
    //     但它有自己的 tick（cwnd 控制），不能再调 reliable_tick，否则双重发送且绕过拥塞控制。
    else reliable_tick(s);

    // 接收数据：传输层 → 数据流层
    // 注：DTLS/SCTP 直接写入 stream.recv_ring，不需要此步骤
    //     只有基础 reliable 层需要从 reliable 缓冲区读取
    if (!s->trans || !s->trans->on_packet) {
        stream_feed_from_reliable(s);
    }

    // 加密层周期 tick（DTLS 握手推进、重传定时器）
    if (s->dtls && s->dtls->tick) {
        s->dtls->tick(s);
    }
    
    /* ========================================================================
     * 阶段 7：信令输出（主动推送本地候选地址）
     * ======================================================================== */

    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {

        // ICE 状态时发送候选，READY 状态发送新候选（如果有）
        if (s->sig_compact_ctx.state == SIGNAL_COMPACT_ICE ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_READY) {
            p2p_signal_compact_tick_send(s);
        }
    }
    else if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY) {

        // Trickle ICE 增量发送候选（含断点续传）
        p2p_signal_relay_tick_send(s);
    }
    else { assert(s->signaling_mode == P2P_SIGNALING_MODE_PUBSUB);

        // 发布 offer（PUB 角色）
        p2p_signal_pubsub_tick_send(&s->sig_pubsub_ctx, s);
    }

    /* ========================================================================
     * 阶段 8：路径管理器维护（健康检查与路径选择）
     * ======================================================================== */

    // LOST 状态下无活跃路径（active_path = -2），恢复由 NAT 层驱动，无需路径管理器维护
    if (s->state > P2P_STATE_LOST) { assert(s->active_path >= PATH_IDX_SIGNALING && s->path_type != P2P_PATH_NONE);

        // 路径健康检查（检测超时、失效路径）
        path_manager_tick(s, now_ms);
        
        // 周期性路径重选（检查是否有更优路径）
        if (tick_diff(now_ms, s->path_mgr.last_reselect_ms) > PATH_RESELECT_INTERVAL_MS) { 
            s->path_mgr.last_reselect_ms = now_ms;

            int best_path = path_manager_select_best_path(s);        
            if (best_path >= PATH_IDX_SIGNALING && best_path != s->active_path) { 
                
                // 如果找到更优路径
                path_stats_t *new_stats = p2p_get_path_stats(s, best_path);
                path_stats_t *old_stats = p2p_get_path_stats(s, s->active_path);
                
                // 判断是否值得切换（性能提升显著）
                // 根据当前活跃路径类型查抽切换阈值
                const path_threshold_config_t *thr = &s->path_mgr.thresholds[s->path_type];
                if ((new_stats->rtt_ms < old_stats->rtt_ms - thr->rtt_threshold_ms) ||  // RTT 显著改善
                    (old_stats->loss_rate > thr->loss_threshold) ||                     // 当前路径丢包严重
                    (s->path_mgr.strategy == P2P_PATH_STRATEGY_CONNECTION_FIRST
                     && new_stats->cost_score < old_stats->cost_score)                  // 直连优先模式：更低成本
                    ) {

                    /* 执行路径切换（含防抖、历史记录） */
                    int ret = path_manager_switch_path(s, best_path, "periodic reselect", now_ms);
                    if (ret == 0) {
                        print("I:", LA_F("Path switched to better route (idx=%d)", LA_F288, 288), best_path);
                    } else if (ret > 0) {
                        print("V:", LA_F("Path switch debounced, waiting for stability", LA_F287, 287));
                    }
                }
            }
            // 如果没有可用路径，那当前路径肯定是最后一个路径，且状态应该是 PATH_STATE_FAILED
            else assert(best_path == s->active_path || p2p_get_path_stats(s, s->active_path)->state == PATH_STATE_FAILED);
        }
    } else assert(s->state != P2P_STATE_LOST || (s->active_path < PATH_IDX_SIGNALING && s->path_type == P2P_PATH_NONE));
    
    /* ========================================================================
     * 阶段 9：NAT 类型检测（后台定期运行 STUN 探测）
     * ======================================================================== */

    if (s->cfg.stun_server) p2p_stun_nat_detect_tick(s, now_ms);
    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT)
        p2p_signal_compact_nat_detect_tick(s);

    s->last_update = now_ms;
    return 0;
}

p2p_state_t
p2p_state(const p2p_handle_t hdl) {
    return hdl ? ((p2p_session_t*)hdl)->state : P2P_STATE_ERROR;
}

int
p2p_nat_type(const p2p_handle_t hdl) {
    return hdl ? ((p2p_session_t*)hdl)->nat_type : P2P_NAT_UNKNOWN;
}

p2p_probe_state_t
p2p_probe(p2p_handle_t hdl) {
    if (!hdl) return P2P_PROBE_STATE_OFFLINE;
    p2p_session_t *s = (p2p_session_t*)hdl;

    // 已关闭：信令通道已断，返回 OFFLINE
    if (s->state == P2P_STATE_CLOSED) return P2P_PROBE_STATE_OFFLINE;

    // 已直连时无需探测
    if (s->state == P2P_STATE_CONNECTED) return P2P_PROBE_STATE_CONNECTED;

    // 直接返回统一状态
    return s->probe_ctx.state;
}

///////////////////////////////////////////////////////////////////////////////

int
p2p_path(const p2p_handle_t hdl) {
    return hdl ? ((p2p_session_t*)hdl)->path_type : P2P_PATH_NONE;
}

bool
p2p_is_ready(p2p_handle_t hdl) {
    if (!hdl) return false;

    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->state < P2P_STATE_LOST) return false;
    if (s->trans && s->trans->is_ready) {
        return s->trans->is_ready(s);
    }
    return true; // 默认准备好，如果不需要传输特定检查
}

int
p2p_send(p2p_handle_t hdl, const void *buf, int len) {

    if (!hdl || !buf || len <= 0) return -1;

    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->state != P2P_STATE_CONNECTED && s->state != P2P_STATE_RELAY) return -1;

    LOCK(s);
    int ret = stream_write(&s->stream, buf, len);
    UNLOCK(s);
    return ret;
}

int
p2p_recv(p2p_handle_t hdl, void *buf, int len) {

    if (!hdl || !buf || len <= 0) return -1;

    p2p_session_t *s = (p2p_session_t*)hdl;

    LOCK(s);
    int ret = stream_read(&s->stream, buf, len);
    UNLOCK(s);
    return ret;
}

int
p2p_request(p2p_handle_t hdl, uint8_t msg, const void *data, int len) {

    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) return -1;

    LOCK(s);
    int ret = p2p_signal_compact_request(s, msg, data, len);
    UNLOCK(s);
    return ret;
}

int
p2p_response(p2p_handle_t hdl, uint8_t code, const void *data, int len) {

    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) return -1;

    LOCK(s);
    int ret = p2p_signal_compact_response(s, code, data, len);
    UNLOCK(s);
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
// ICE / SDP 公开接口
///////////////////////////////////////////////////////////////////////////////


/*
 * 获取本地候选数量
 */
int
p2p_local_candidate_count(p2p_handle_t hdl) {
    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;

    LOCK(s);
    int cnt = s->local_cand_cnt;
    UNLOCK(s);

    return cnt;
}

int
p2p_remote_candidate_count(p2p_handle_t hdl) {
    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;

    LOCK(s);
    int cnt = s->remote_cand_cnt;
    UNLOCK(s);

    return cnt;
}

/*
 * 导出多个 ICE 候选，可选生成完整 SDP
 * 包装 p2p_export_ice_sdp()，直接对外暴露
 */
int
p2p_export_ice_sdp(p2p_handle_t hdl, char *sdp_buf, int buf_size,
                   bool candidates_only,
                   const char *ice_ufrag,
                   const char *ice_pwd,
                   const char *dtls_fingerprint) {
    if (!hdl || !sdp_buf || buf_size <= 0) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;

    LOCK(s);
    int cnt = s->local_cand_cnt;
    UNLOCK(s);

    return p2p_ice_export_sdp(s->local_cands, cnt, sdp_buf, buf_size,
                                     candidates_only, ice_ufrag, ice_pwd, dtls_fingerprint);
}

/*
 * 从 SDP 文本解析远端候选并添加到会话
 */
int
p2p_import_ice_sdp(p2p_handle_t hdl, const char *sdp_text) {
    if (!hdl || !sdp_text) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;

    /* 临时缓冲区：最多解析 64 个候选 */
    p2p_remote_candidate_entry_t tmp[64];
    int parsed = p2p_ice_import_sdp(sdp_text, tmp, 64);
    if (parsed <= 0) return parsed;

    LOCK(s);
    int added = 0;
    for (int i = 0; i < parsed; i++) {
        int idx = p2p_cand_push_remote(s);
        if (idx < 0) break;
        s->remote_cands[idx] = tmp[i];
        added++;
    }
    UNLOCK(s);

    return added;
}

///////////////////////////////////////////////////////////////////////////////

#pragma clang diagnostic pop
