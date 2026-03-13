#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "P2P"

#include "p2p_internal.h"
#include "p2p_probe.h"
#include "p2p_thread.h"
#include "p2p_udp.h"

///////////////////////////////////////////////////////////////////////////////
// 日志系统
///////////////////////////////////////////////////////////////////////////////

static p2p_log_level_t g_log_level = P2P_LOG_LEVEL_INFO;
static p2p_log_callback_t g_log_callback = NULL;

void p2p_set_log_level(p2p_log_level_t level) {
    g_log_level = level;
}

p2p_log_level_t p2p_get_log_level(void) {
    return g_log_level;
}

void p2p_set_log_output(p2p_log_callback_t cb) {
    g_log_callback = cb;
}

///////////////////////////////////////////////////////////////////////////////

/* ---- 信令重发配置 ---- */
#define SIGNAL_RESEND_INTERVAL_MS           5000   /* 信令重发间隔 (5秒，relay/compact/等) */
#define SIGNAL_RESEND_INTERVAL_PUBSUB_MS    15000  /* PUBSUB 重发间隔 (15秒，给对方充足时间回复) */
#define SIGNAL_MAX_RESEND_COUNT             12     /* 最大重发次数 (共约60秒) */

/* ---- 锁辅助函数（单线程模式下为空操作） ---- */
#ifdef P2P_THREADED
#define LOCK(s)   do { if ((s)->cfg.threaded) P_mutex_lock(&(s)->mtx); } while(0)
#define UNLOCK(s) do { if ((s)->cfg.threaded) P_mutex_unlock(&(s)->mtx); } while(0)
#else
#define LOCK(s)   ((void)0)
#define UNLOCK(s) ((void)0)
#endif

// 解析主机名
static ret_t resolve_host(const char *host, uint16_t port, struct sockaddr_in *out) {

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

    int prev_state = s->state;
    s->state = P2P_STATE_CLOSED;

    // NAT 层 FIN（仅在已连接状态，重复发送提高 UDP 可靠性）
    if (prev_state == P2P_STATE_CONNECTED || prev_state == P2P_STATE_RELAY) {

        print("V: %s", LA_S("Sending FIN packet to peer before closing", LA_S91, 91));
        for (int i = 0; i < 3; i++) {
            if (i) P_usleep(5 * 1000); // 5ms 间隔重发
            nat_send_fin(s);
        }

        // 触发回调
        if (s->cfg.on_disconnected) s->cfg.on_disconnected(s, s->cfg.userdata);
    }

    s->nat.state = NAT_CLOSED;
    
    s->remote_cand_cnt = 0;
    s->remote_ice_done = false;
    s->turn_pending = 0;

    p2p_turn_reset(s);
    p2p_ice_reset(&s->ice_ctx);

    // COMPACT 信令模式：取消与对方在服务器上的注册，UNREGISTER
    // + COMPACT 信令的 unregister 等价于 disconnect & logout
    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {

        if (s->sig_compact_ctx.state != SIGNAL_COMPACT_INIT) {
            print("I: %s", LA_S("Sending UNREGISTER packet to COMPACT signaling server", LA_S92, 92));
            p2p_signal_compact_disconnect(s);
        }
    } 
    // RELAY 信令模式：
    else if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY) {

        // todo 目前无 peer-level disconnect 协议，仅依赖 NAT FIN 
    }
}

/*
 * 被动关闭 — 对端断开时的状态转换和回调
 */
static void peer_disconnect(p2p_session_t *s) {

    // 信令信令层的 FIN 信号会统一转换为 NAT 层的 FIN 信号
    assert(s->nat.state == NAT_CLOSED);

    assert(s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY ||
           s->state == P2P_STATE_CLOSING);

    print("I: %s", LA_S("Received FIN packet, connection closed", LA_S88, 88));

    s->state = P2P_STATE_CLOSED;

    s->remote_cand_cnt = 0;
    s->remote_ice_done = false;
    s->turn_pending = 0;

    p2p_turn_reset(s);
    p2p_ice_reset(&s->ice_ctx);

    // 停止信道外 NAT 探测（回到 READY 状态）
    if (s->probe_ctx.state != P2P_PROBE_STATE_NO_SUPPORT
        && s->probe_ctx.state != P2P_PROBE_STATE_OFFLINE) {
        s->probe_ctx.state = P2P_PROBE_STATE_READY;
    }

    if (s->cfg.on_disconnected) s->cfg.on_disconnected(s, s->cfg.userdata);
}

///////////////////////////////////////////////////////////////////////////////

p2p_handle_t
p2p_create(const char *local_peer_id, const p2p_config_t *cfg) {

    P_check(cfg && local_peer_id, return NULL;)

    ret_t ret;

    static int s_initialized = 0;
    if (!s_initialized) {

        LA_init();

#if P_WIN
        // Initialize Winsock on Windows (no-op on POSIX)
        if ((ret = P_net_init()) != E_NONE) {
            print("E:", LA_F("Initialize network subsystem failed(%d)", LA_S33, 228), ret);
            return NULL;
        }
#endif
        // Initialize random number generator (thread-safe, idempotent)
        P_rand_init();
        s_initialized = 1;
    }

    if (cfg->signaling_mode == P2P_SIGNALING_MODE_PUBSUB) {
        if (!cfg->gh_token || !cfg->gist_id) {
            print("E: %s", LA_S("PUBSUB mode requires gh_token and gist_id", LA_S87, 87));
            return NULL;
        }
    }
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT || cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) {
        if (!cfg->server_host) {
            print("E: %s", LA_S("RELAY/COMPACT mode requires server_host", LA_S89, 89));
            return NULL;
        }
    } else {
        print("E: %s", LA_S("Invalid signaling mode in configuration", LA_S71, 71));
        return NULL;
    }

    // 创建 UDP 套接字，port 0 为有效值，表示由操作系统分配随机端口
    print("I:", LA_F("Open P2P UDP socket on port %d", LA_F241, 241), cfg->bind_port);
    sock_t sock = udp_open_socket(cfg->bind_port);
    if (sock == P_INVALID_SOCKET) {
        print("E:", LA_F("Open P2P UDP socket on port %d failed(%d)", LA_F242, 242), cfg->bind_port, P_sock_errno());
        return NULL;
    }

    p2p_session_t *s = (p2p_session_t*)calloc(1, sizeof(*s));
    if (!s) {
        print("E: %s", LA_S("Failed to allocate memory for session", LA_S59, 59));
        P_sock_close(sock);
        return NULL;
    }

    // 初始化信令服务模式
    print("I:", LA_F("Initialize signaling mode: %d", LA_F234, 234), (int)cfg->signaling_mode);
    if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT) p2p_signal_compact_init(&s->sig_compact_ctx);
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) p2p_signal_relay_init(&s->sig_relay_ctx);
    else if ((ret = p2p_signal_pubsub_init(&s->sig_pubsub_ctx, cfg->gh_token, cfg->gist_id)) != E_NONE) {
        print("E:", LA_F("Initialize PUBSUB signaling context failed(%d)", LA_F233, 233), ret);
        free(s); P_sock_close(sock);
        return NULL;
    } else if (cfg->auth_key) {
        strncpy(s->sig_pubsub_ctx.auth_key, cfg->auth_key, sizeof(s->sig_pubsub_ctx.auth_key) - 1); // todo 用 strdup
    }

    // 初始化路由层（用于检测是否处于同一子网）
    route_init(&s->route);

    // 获取本地所有有效的网络地址
    if ((ret = route_detect_local(&s->route)) < 0) {
        print("E:", LA_F("Detect local network interfaces failed(%d)", LA_F217, 217), ret);
        free(s); P_sock_close(sock);
        return NULL;
    }

    // 初始化候选地址数组（动态分配，初始容量 8）
    const int initial_cand_cap = 8;
    s->local_cands  = (p2p_candidate_entry_t *)calloc(initial_cand_cap, sizeof(p2p_candidate_entry_t));
    s->remote_cands = (p2p_remote_candidate_entry_t *)calloc(initial_cand_cap, sizeof(p2p_remote_candidate_entry_t));
    if (!s->local_cands || !s->remote_cands) {
        print("E: %s", LA_S("Failed to allocate memory for candidate lists", LA_S58, 58));
        if (s->local_cands) free(s->local_cands);
        if (s->remote_cands) free(s->remote_cands);
        free(s); P_sock_close(sock);
        return NULL;
    }
    s->local_cand_cap = s->remote_cand_cap = initial_cand_cap;

    s->cfg = *cfg;
    if (s->cfg.update_interval_ms <= 0) s->cfg.update_interval_ms = 10;
    
    // 初始化本端身份标识
    memset(s->local_peer_id, 0, sizeof(s->local_peer_id));
    strncpy(s->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);

    s->sock = sock;
    s->tcp_sock = P_INVALID_SOCKET;

    // 初始化虚拟链路层（基于 NAT 穿透的 P2P）
    nat_init(&s->nat);

    // 初始化基础传输层（reliable ARQ）
    reliable_init(&s->reliable);
    s->reliable.session = s;  // 设置回溯指针

    // 初始化路径管理器（多路径并行支持）
    p2p_path_strategy_t strategy = (p2p_path_strategy_t)(cfg->path_strategy);
    if (strategy < P2P_PATH_STRATEGY_CONNECTION_FIRST || strategy > P2P_PATH_STRATEGY_HYBRID) {
        strategy = P2P_PATH_STRATEGY_CONNECTION_FIRST; // 默认：直连优先
    }
    path_manager_init(s, strategy);
    print("I:", LA_F("Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)", LA_F249, 249), strategy);

    // 初始化传输层（可靠性模块，与加密层正交）
    s->trans = NULL;
    
    if (cfg->use_sctp) {
#ifdef WITH_SCTP
        print("I: %s", LA_S("SCTP (usrsctp) enabled as transport layer", LA_S90, 90));
        s->trans = &p2p_trans_sctp;
#else
        print("W: %s", LA_S("SCTP (usrsctp) requested but library not linked", LA_S59, 85));
#endif
    }
    else if (cfg->use_pseudotcp) {
        print("I: %s", LA_S("PseudoTCP enabled as transport layer", LA_S84, 84));
        s->trans = &p2p_trans_pseudotcp;
    }
    else print("I: %s", LA_S("No advanced transport layer enabled, using simple reliable layer", LA_S74, 74));

    // 执行传输模块的初始化处理
    if (s->trans && s->trans->init) {
        if (s->trans->init(s) != 0) {
            print("E:", LA_F("Transport layer '%s' init failed, falling back to simple reliable", LA_F311, 311), s->trans->name);
            s->trans = NULL;
            s->trans_data = NULL;
        }
    }

    // 初始化加密层（与传输层正交，DTLS 后端选择）
    s->dtls = NULL;
    s->dtls_data = NULL;
    
    if (cfg->dtls_backend == 1) {
#ifdef WITH_DTLS
        print("I: %s", LA_S("DTLS (MbedTLS) enabled as encryption layer", LA_S53, 53));
        s->dtls = &p2p_dtls_mbedtls;
#else
        print("W: %s", LA_S("DTLS (MbedTLS) requested but library not linked", LA_S51, 51));
#endif
    }
    else if (cfg->dtls_backend == 2) {
#ifdef WITH_OPENSSL
        print("I: %s", LA_S("OpenSSL DTLS enabled as encryption layer", 0, 0));
        s->dtls = &p2p_dtls_openssl;
#else
        print("W: %s", LA_S("OpenSSL requested but library not linked", LA_S76, 76));
#endif
    }

    // 注：DTLS init 延迟到 p2p_connect()，因为自动角色需要知道 remote_peer_id

    // 初始化数据流层
    stream_init(&s->stream, cfg->nagle);

    // 初始化探测上下文（初始状态为 OFFLINE）
    probe_init(&s->probe_ctx);

    //----------------------------

    p2p_ice_init(&s->ice_ctx);

    p2p_turn_init(&s->turn);

    s->signaling_mode = cfg->signaling_mode;
    s->state = P2P_STATE_INIT;
    s->path = P2P_PATH_NONE;
    s->nat_type = P2P_NAT_UNKNOWN;

    //----------------------------

    s->last_update = P_tick_ms();

#ifdef P2P_THREADED
    if (cfg->threaded) {
        print("I: %s", LA_S("Starting internal thread", LA_S97, 97));
        if ((ret = p2p_thread_start(s)) != E_NONE) {
            print("E:", LA_F("Start internal thread failed(%d)", LA_F295, 295), ret);
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
        print("I: %s", LA_S("Stopping internal thread", LA_S98, 98));
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

        print("I: %s", LA_S("Close P2P UDP socket", LA_S45, 45));
        P_sock_close(s->sock);
    }

    // RELAY 信令模式：关闭 TCP 长连接（断开和服务器的连接, logout）
    if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY
        && s->sig_relay_ctx.state != SIGNAL_DISCONNECTED) {

        print("I: %s", LA_S("Closing TCP connection to RELAY signaling server", LA_S46, 46));
        p2p_signal_relay_close(&s->sig_relay_ctx);
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
        print("E: %s", LA_S("COMPACT mode requires explicit remote_peer_id", LA_S47, 47));
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
            print("E:", LA_F("Crypto layer '%s' init failed, continuing without encryption", LA_F215, 215), s->dtls->name);
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
                print("E:", LA_F("Resolve COMPACT signaling server address: %s:%d failed(%d)", LA_F280, 280),
                             s->cfg.server_host, s->cfg.server_port, ret);
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }

            s->state = P2P_STATE_REGISTERING;   // 进入注册阶段

            // 获取本地绑定端口
            struct sockaddr_in loc; socklen_t len = sizeof(loc);
            getsockname(s->sock, (struct sockaddr *)&loc, &len);

            // 将 route 中的本地地址转换为候选列表（skip_host_candidates 时跳过）
            if (!s->cfg.skip_host_candidates) {
                for (int i = 0; i < s->route.addr_count; i++) {
                    int idx = p2p_cand_push_local(s);
                    if (idx < 0) {
                        print("E:", LA_S("Push local cand<%s:%d> failed(OOM)\n", LA_S352, 352),
                              inet_ntoa(s->route.local_addrs[i].sin_addr), ntohs(s->route.local_addrs[i].sin_port));
                        return idx;
                    }

                    p2p_candidate_entry_t *c = &s->local_cands[idx];
                    c->type = P2P_CAND_HOST;
                    c->addr = s->route.local_addrs[i];
                    c->addr.sin_port = loc.sin_port;  // 使用实际绑定端口
                    print("I:", LA_F("Append Host candidate: %s:%d", LA_F205, 205),
                                 inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
                }
            }
            else print("I: %s", LA_S("Skipping local Host candidates on --public-only", LA_S94, 94));

            // 注册（连接）到 COMPACT 信令服务器
            print("I:", LA_F("Register to COMPACT signaling server at %s:%d", LA_F274, 274),
                         inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

            if ((ret = p2p_signal_compact_connect(s, s->local_peer_id, remote_peer_id, &server_addr)) != E_NONE) {
                print("E:", LA_F("Connect to COMPACT signaling server failed(%d)", LA_F210, 210), ret);
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }

            // TURN：异步收集 Relay 候选（响应到达后 trickle 发送给对端）
            if (!s->cfg.lan_punch && s->cfg.turn_server) {
                if (p2p_turn_allocate(s) == 0) {
                    print("I:", LA_F("Requested Relay Candidate from TURN %s", LA_F277, 277), s->cfg.turn_server);
                }
            }

            break;
        }

        // 对于 RELAY 模式
        case P2P_SIGNALING_MODE_RELAY: {

            assert(s->cfg.server_host);

            // 首次连接：自动登录信令服务器（只执行一次）
            if (s->sig_relay_ctx.state != SIGNAL_CONNECTED) {

                print("I:", LA_F("Connecting to RELAY signaling server at %s:%d", LA_F213, 213),
                             s->cfg.server_host, s->cfg.server_port);

                if ((ret = p2p_signal_relay_login(&s->sig_relay_ctx, s->cfg.server_host, s->cfg.server_port, s->local_peer_id)) != E_NONE) {
                    print("E:", LA_F("Connect to RELAY signaling server failed(%d)", LA_F211, 211), ret);
                    s->state = P2P_STATE_ERROR;
                    UNLOCK(s);
                    return -1;
                }
            }

            s->state = P2P_STATE_REGISTERING;

            // 收集 ICE 候选者
            if (s->cfg.use_ice) {
                p2p_ice_gather_candidates(s);
            }

            // 如果指定了 remote_peer_id，立即发送初始 offer
            if (remote_peer_id && s->local_cand_cnt > 0) {

                uint8_t buf[2048]; // fixme 这个缓冲区大小需要根据实际情况调整，确保足够容纳所有候选者的序列化数据
                int n = pack_signaling_payload_hdr(s->local_peer_id, remote_peer_id,
                                                   0/* timestamp */, 0/* delay_trigger */,
                                                   s->local_cand_cnt, buf);
                for (int i = 0; i < s->local_cand_cnt; i++) {
                    n += pack_candidate(&s->local_cands[i], buf + n);
                }

                print("I:", LA_F("Sent initial offer(%d) to %s)", LA_F294, 294), n, remote_peer_id);
                if ((ret = p2p_signal_relay_send_connect(&s->sig_relay_ctx, remote_peer_id, buf, n)) != E_NONE) {
                    print("E:", LA_F("Send offer to RELAY signaling server failed(%d)", LA_F287, 287), ret);
                    // todo
                }
                s->ice_ctx.signal_sent = true;
            }
            // 被动模式：等待任意对等方的 offer
            else if (!remote_peer_id) {

                print("I: %s", LA_W("Waiting for incoming offer from any peer", LA_W24, 24));
            }

            // 注意：后续 Srflx 候选者（STUN 响应）会在 p2p_update 中增量发送
            break;
        }

        // 对于 PUBSUB 模式
        case P2P_SIGNALING_MODE_PUBSUB: {

            assert(s->cfg.gh_token && s->cfg.gist_id);

            s->state = P2P_STATE_REGISTERING;

            // 收集候选者
            if (s->cfg.use_ice) {
                p2p_ice_gather_candidates(s);
            }

            // 如果指定了 remote_peer_id
            if (remote_peer_id) {

                // 说明是 PUB 角色（发布者），主动发起连接
                p2p_signal_pubsub_set_role(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_PUB);

                // PUB 模式必须等待 STUN 响应（获取公网地址）后才能发布 offer
                // + PUB/SUB 不支持 Trickle ICE 模式，所以必须等候选收集完成后一次性发送
                print("I: %s", LA_S("PUBSUB (PUB): gathering candidates, waiting for STUN before publishing", LA_S85, 85));
            }
            else {

                // SUB 角色（订阅者）：被动等待连接
                p2p_signal_pubsub_set_role(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_SUB);

                // SUB 模式：被动等待 offer，收到后自动回复
                print("I: %s", LA_S("PUBSUB (SUB): waiting for offer from any peer", LA_S86, 86));
            }
            break;
        }

        default:
            print("E:", LA_F("Unknown signaling mode: %d", LA_F315, 315), s->signaling_mode);
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

    // 停止信道外 NAT 探测（回到 READY 状态）
    if (s->probe_ctx.state != P2P_PROBE_STATE_NO_SUPPORT
        && s->probe_ctx.state != P2P_PROBE_STATE_OFFLINE) {
        s->probe_ctx.state = P2P_PROBE_STATE_READY;
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

    uint8_t buf[P2P_MTU + 16]; struct sockaddr_in from; int n;

    P_clock _clk_now; P_clock_now(&_clk_now);
    uint64_t now_ms = clock_ms(_clk_now);

    /* ========================================================================
     * 阶段 1：远程数据输入（被动接收所有网络数据包）
     * ======================================================================== */
    while ((n = udp_recv_from(s->sock, &from, buf, sizeof(buf))) > 0) {

        // --------------------
        // STUN/TURN 协议包
        // --------------------
        // 注：TURN 是 STUN 的扩展，共享相同的包格式和 Magic Cookie (0x2112A442)
        //     两个 handler 内部会根据消息类型（Method）分别过滤处理

        if (n >= 20 && buf[0] < 2) { // STUN type 0x00xx or 0x01xx
            uint32_t magic = ntohl(*(uint32_t *)(buf + 4));
            if (magic == STUN_MAGIC) {
                uint16_t msg_type = (buf[0] << 8) | buf[1];
                printf(LA_F("Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d", LA_F262, 262),
                    inet_ntoa(from.sin_addr), ntohs(from.sin_port), msg_type, n);
                p2p_stun_handle_packet(s, buf, n, &from);  // 处理 Binding Response

                // TURN 响应处理（Allocate/Refresh/CreatePermission/Data Indication）
                const uint8_t *inner_data = NULL;
                int inner_len = 0;
                struct sockaddr_in inner_peer = {0};
                int turn_ret = p2p_turn_handle_packet(s, buf, n, &from,
                                                      &inner_data, &inner_len, &inner_peer);
                if (turn_ret == 1 && inner_data && inner_len >= P2P_HDR_SIZE) {
                    // Data Indication: 内层为完整 P2P 包，重新注入主派发循环
                    memmove(buf, inner_data, inner_len);
                    n = inner_len;
                    from = inner_peer;
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
        p2p_pkt_hdr_decode(buf, &hdr);

        // 获取 payload
        const uint8_t *payload = buf + P2P_HDR_SIZE;
        int payload_len = n - P2P_HDR_SIZE;
        uint8_t crypto_dec_buf[P2P_HDR_SIZE + P2P_MAX_PAYLOAD];  /* 解密输出缓冲区 */

        switch (hdr.type) {

            // --------------------
            // 加密层（DTLS 密文解包 → 解密 → 重新派发）
            // --------------------

            case P2P_PKT_RELAY_CRYPTO:
                if (!compact_on_relay_packet(s, hdr.type, &payload, &payload_len, &from)) break;
                /* fall through — payload 现在是 DTLS 记录 */
            case P2P_PKT_CRYPTO: {
                if (!s->dtls) break;
                int dec_len = s->dtls->decrypt_recv(s, payload, payload_len,
                                                      crypto_dec_buf, sizeof(crypto_dec_buf));
                if (dec_len >= P2P_HDR_SIZE) {
                    /* 解析内层 P2P 包头 */
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

            case P2P_PKT_RELAY_DATA:
            case P2P_PKT_RELAY_ACK:
                if (!compact_on_relay_packet(s, hdr.type, &payload, &payload_len, &from)) break;
                if (hdr.type == P2P_PKT_RELAY_DATA) goto handle_data;
                else goto handle_ack;

            /*
             * 协议：P2P_PKT_DATA (0x20)
             * 包头: [type=0x20 | flags=0 | seq=序列号(2B)]
             * 负载: [data(N)]
             * 说明：P2P 数据包，由 reliable 层或高级传输层处理
             */
            case P2P_PKT_DATA:
                printf(LA_F("Received DATA pkt from %s:%d, seq=%u, len=%d", LA_F260, 260),
                    inet_ntoa(from.sin_addr), ntohs(from.sin_port), hdr.seq, payload_len);
            handle_data:

                // 记录数据包接收（根据来源地址查找实际接收路径）
                {
                    int recv_path = path_manager_find_by_addr(s, &from);
                    if (recv_path >= -1)
                        path_manager_on_packet_recv(s, recv_path, now_ms, P2P_HDR_SIZE + payload_len);
                }

                // 高级传输层（DTLS/SCTP/PseudoTCP）有自己的解包逻辑
                if (s->trans && s->trans->on_packet)
                    s->trans->on_packet(s, hdr.type, payload, payload_len, &from);
                // 基础 reliable 层处理
                else if (payload_len > 0)
                    reliable_on_data(&s->reliable, hdr.seq, payload, payload_len);
                
                break;

            /*
             * 协议：P2P_PKT_ACK (0x21)
             * 包头: [type=0x21 | flags=0 | seq=序列号(2B)]
             * 负载: [ack_seq(2B) | sack(4B)]
             * 说明：ACK 仅基础 reliable 层使用，DTLS/SCTP 有自己的确认机制
             */
            case P2P_PKT_ACK:
            handle_ack: {
                uint16_t ack_seq = nget_s(payload);
                uint32_t sack; nread_l(&sack, payload + 2);
                if (hdr.type == P2P_PKT_ACK) {
                    printf(LA_F("Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x", LA_F259, 259),
                        inet_ntoa(from.sin_addr), ntohs(from.sin_port), ack_seq, sack);
                }

                // 只有使用基础 reliable 层时才处理（无高级传输层或高级传输层无 on_packet）
                if ((!s->trans || !s->trans->on_packet) && payload_len >= 6) {
                    
                    // 记录 ACK 前的 RTT 值
                    int old_srtt = s->reliable.srtt;
                    
                    // 处理 ACK（reliable 层会更新 RTT）
                    reliable_on_ack(&s->reliable, ack_seq, sack);
                    
                    // 如果 RTT 更新了，同步到路径管理器（Group 2: 数据层 RTT）
                    if (s->reliable.srtt != old_srtt && s->reliable.srtt > 0) {
                        if (s->path_mgr.active_path >= -1) {
                            path_manager_on_data_rtt(s, s->path_mgr.active_path, (uint32_t)s->reliable.srtt);
                        }
                    }
                }
                break;
            }
            // --------------------
            // NAT 链路层（打洞、保活、断开）
            // --------------------

            // NAT 打洞/保活包
            case P2P_PKT_PUNCH:
                nat_on_punch(s, &hdr, &from);
                break;
            case P2P_PKT_PUNCH_ACK:
                nat_on_punch_ack(s, &hdr, &from);
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
                print("V:", LA_F("Received UNKNOWN pkt type: 0x%02X", LA_F264, 264), hdr.type);
                printf(LA_F("Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d", LA_F263, 263),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port), hdr.type, hdr.seq, payload_len);
                break;
        }

    } // while ((n = udp_recv_from(s->sock, &from, buf, sizeof(buf))) > 0)

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

        p2p_signal_relay_tick_recv(&s->sig_relay_ctx, s);
    }
    else { assert(s->signaling_mode == P2P_SIGNALING_MODE_PUBSUB);
        
        p2p_signal_pubsub_tick_recv(&s->sig_pubsub_ctx, s);
    }

    /* ========================================================================
     * 阶段 3：ICE 候选收集（STUN/TURN 探测本地公网地址）
     * ======================================================================== */

    if (s->cfg.use_ice) {
        p2p_ice_tick(s, now_ms);
    }

    /* TURN 定时维护（Refresh 续期、权限同步） */
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

    // 转换：REGISTERING → PUNCHING（开始打洞）
    if (s->state == P2P_STATE_REGISTERING && s->nat.state == NAT_PUNCHING) {
        print("I: %s", LA_S("P2P punching in progress ...", LA_S81, 81));
        s->state = P2P_STATE_PUNCHING;
    }

    // 转换：PUNCHING/REGISTERING → CONNECTED（NAT 穿透成功）
    // + 注意，NAT 变为 CONNECTED 状态时，P2P 可能还未进入 PUNCHING 状态
    //   因为，NAT 的 CONNECTED，是收到对方发过来的包，说明自己的端口对对方是开放的了
    //   但 PUNCHING 状态是向对方端口发送打洞包，也就是检测对方端口是否可写入。
    //   所以，对方可能先于自己获得对方（也就是自己）的候选地址，并先完成 NAT 穿透（对方发包过来），此时自己还未开始打洞（PUNCHING）
    if ((s->state == P2P_STATE_PUNCHING || s->state == P2P_STATE_REGISTERING) &&
        s->nat.state == NAT_CONNECTED) {


        print("I: %s", LA_S("P2P connection established", LA_S79, 79));
        s->state = P2P_STATE_CONNECTED;
        
        // 选择最佳路径
        int best_path = path_manager_select_best_path(s);
        if (best_path >= -1) {  // -1=SIGNALING, >=0=候选
            const struct sockaddr_in *addr = path_manager_get_addr(s, best_path);
            if (addr) {
                s->path = P2P_PATH_PUNCH;  // 目前只有 PUNCH
                s->active_addr = *addr;
                path_manager_switch_path(s, best_path, "nat_punch_success", now_ms);
                print("I:", LA_F("Selected path: PUNCH (idx=%d)", LA_F281, 281), best_path);
            }
        } else {
            // 降级：路径管理器无可用路径，使用传统方式
            s->path = P2P_PATH_PUNCH;
            s->active_addr = s->nat.peer_addr;
            s->path_mgr.active_path = -2;  // -2=无路径管理器管理的路径
        }

        // 触发连接建立回调
        if (s->cfg.on_connected) {
            s->cfg.on_connected(s, s->cfg.userdata);
        }
    }

    // NAT 重新连接后恢复路径（NAT_RELAY → NAT_CONNECTED）
    if (s->nat.state == NAT_CONNECTED && s->state == P2P_STATE_RELAY) {
        print("I: %s", LA_S("NAT connection recovered, upgrading from RELAY to CONNECTED", LA_S72, 72));
        
        // 标记中继路径为降级（但不移除，保留作为备份）
        path_manager_set_path_state(s, PATH_IDX_SIGNALING, PATH_STATE_DEGRADED);
        
        // 重新选择最佳路径（PUNCH 应当优先）
        int best_path = path_manager_select_best_path(s);
        if (best_path >= -1) {  // -1=SIGNALING, >=0=候选
            const struct sockaddr_in *addr = path_manager_get_addr(s, best_path);
            if (addr) {
                s->path = P2P_PATH_PUNCH;
                s->active_addr = *addr;
                path_manager_switch_path(s, best_path, "nat_recovery", now_ms);
                s->state = P2P_STATE_CONNECTED; // 恢复为 CONNECTED 状态
                print("I:", LA_F("Path recovered: switched to PUNCH", LA_F250, 250));
            }
        }
    }

    // 转换：PUNCHING → RELAY（NAT 打洞失败，添加中继路径）
    if (s->state == P2P_STATE_PUNCHING && s->nat.state == NAT_RELAY) {

        // 添加中继路径
        print("I: %s", LA_S("P2P punch failed, adding relay path", LA_S80, 80));
        
        struct sockaddr_in relay_addr;
        bool relay_available = false;
        
        if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {
            if (s->sig_compact_ctx.relay_support) {
                relay_addr = s->sig_compact_ctx.server_addr;
                relay_available = true;
                print("I: %s", LA_W("NAT punch failed, using COMPACT server relay", LA_W7, 7));
            } else {
                print("W: %s", LA_W("NAT punch failed, server has no relay support", LA_W6, 6));
            }
        } else {
            print("W: %s", LA_W("NAT punch failed, no TURN server configured", LA_W5, 5));
        }
        
        // 设置 SIGNALING 路径
        if (relay_available) {
            path_manager_enable_signaling(s, &relay_addr);
            print("I: %s", LA_S("Added SIGNALING path to path manager", LA_S39, 39));
        }
        
        // 选择最佳可用路径
        int best_path = path_manager_select_best_path(s);
        if (best_path >= -1) {  // -1=SIGNALING, >=0=候选
            const struct sockaddr_in *addr = path_manager_get_addr(s, best_path);
            if (addr) {
                s->path = path_manager_get_path_type(s, best_path);
                s->active_addr = *addr;
                path_manager_switch_path(s, best_path, "nat_punch_failed", now_ms);
                s->state = P2P_STATE_RELAY;
                print("I: %s", LA_S("Using path: RELAY", LA_S102, 102));
            }
        } else {
            // 无可用路径：降级到传统方式
            s->state = P2P_STATE_RELAY;
            s->path = P2P_PATH_SIGNALING;
            s->active_addr = relay_available ? relay_addr : s->nat.peer_addr;
            s->path_mgr.active_path = -2;  // -2=无路径管理器管理的路径
        }
    }

    // 转换：CONNECTED/RELAY/CLOSING → CLOSED
    // + NAT FIN 和信令 PEER_OFF 都归一化为 NAT_CLOSED，统一在此处理
    if (s->nat.state == NAT_CLOSED
        && (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY|| 
            s->state == P2P_STATE_CLOSING)) {
        peer_disconnect(s);
    }

    // 转换：CONNECTED → RELAY（NAT 连接超时断开，降级到中继模式）
    if (s->nat.state == NAT_LOST && s->state == P2P_STATE_CONNECTED) {
        print("W: %s", LA_S("NAT connection timeout, downgrading to relay mode", LA_S73, 73));
        
        // 标记当前活跃路径为失效
        path_manager_set_path_state(s, s->path_mgr.active_path, PATH_STATE_FAILED);
        
        // 尝试切换到中继路径
        int best_path = path_manager_select_best_path(s);
        if (best_path >= -1) {  // -1=SIGNALING, >=0=候选
            const struct sockaddr_in *addr = path_manager_get_addr(s, best_path);
            if (addr) {
                s->path = path_manager_get_path_type(s, best_path);
                s->active_addr = *addr;
                path_manager_switch_path(s, best_path, "nat_timeout", now_ms);
                s->state = P2P_STATE_RELAY;
                print("I: %s", LA_S("Switched to backup path: RELAY", LA_S100, 100));
            }
        } else {
            // 无备用路径：NAT 层会继续尝试恢复
            s->state = P2P_STATE_RELAY;
            s->nat.state = NAT_RELAY;
            s->path_mgr.active_path = -2;  // -2=无路径管理器管理的路径
        }
    }

    /* ========================================================================
     * 阶段 6：数据传输（应用层数据收发）
     * ======================================================================== */

    // 发送数据：数据流层 → 传输层 flush 写入
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
        
        // 如果使用高级传输层（DTLS、SCTP、PseudoTCP）
        if (s->trans && s->trans->send_data) {
            // 传输层就绪检查：DTLS 握手完成前不 drain 数据，避免丢失
            int ready = !s->trans->is_ready || s->trans->is_ready(s);
            if (ready) {
                // 由高级传输模块自行处理流的数据
                n = ring_read(&s->stream.send_ring, buf, sizeof(buf));
                if (n > 0) {
                    int sent = s->trans->send_data(s, buf, n);
                    if (sent > 0) {
                        s->stream.pending_bytes -= n;
                        s->stream.send_offset += n;
                    } else {
                        // send_data 失败，数据已从 ring 消费，记录丢失
                        print("W:", LA_F("transport send_data failed, %d bytes dropped", LA_F343, 343), n);
                    }
                }
            }
        }
        // 使用基础 reliable 层
        else stream_flush_to_reliable(&s->stream, &s->reliable);
    }

    // reliable 周期 tick：发送/重传数据包 + 发 ACK
    // 注：仅在无高级传输层时调用。PseudoTCP 虽然 on_packet==NULL（复用 reliable 收包），
    //     但它有自己的 tick（cwnd 控制），不能再调 reliable_tick，否则双重发送且绕过拥塞控制。
    if (!s->trans) {
        reliable_tick(&s->reliable);
    }

    // 传输模块周期 tick（重传，拥塞控制等）
    if (s->trans && s->trans->tick) {
        if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
            s->trans->tick(s);
        }
    }

    // 加密层周期 tick（DTLS 握手推进、重传定时器）
    if (s->dtls && s->dtls->tick) {
        s->dtls->tick(s);
    }
    
    // [CRITICAL] 同步高级传输层统计到路径管理器（Group 2: 数据层 RTT + 丢包率）
    if (s->trans && s->trans->get_stats && s->path_mgr.active_path >= -1) {
        uint32_t rtt_ms = 0;
        float loss_rate = 0.0f;
        
        if (s->trans->get_stats(s, &rtt_ms, &loss_rate) == 0) {

            if (rtt_ms > 0)
                path_manager_on_data_rtt(s, s->path_mgr.active_path, rtt_ms);

            // 上报数据层丢包率（之前被忽略，导致路径质量评估缺少数据层信息）
            if (loss_rate > 0.0f)
                path_manager_on_data_loss_rate(s, s->path_mgr.active_path, loss_rate);
        }
    }

    // 接收数据：传输层 → 数据流层
    // 注：DTLS/SCTP 直接写入 stream.recv_ring，不需要此步骤
    //     只有基础 reliable 层需要从 reliable 缓冲区读取
    if (!s->trans || !s->trans->on_packet) {
        stream_feed_from_reliable(&s->stream, &s->reliable);
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
        p2p_signal_relay_tick_send(&s->sig_relay_ctx, s);
    }
    else { assert(s->signaling_mode == P2P_SIGNALING_MODE_PUBSUB);

        // 发布 offer（PUB 角色）
        p2p_signal_pubsub_tick_send(&s->sig_pubsub_ctx, s);
    }

    /* ========================================================================
     * 阶段 8：路径管理器维护（健康检查与路径选择）
     * ======================================================================== */

    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {

        // 路径健康检查（检测超时、失效路径）
        path_manager_tick(s, now_ms);
        
        // 检查 failover 后的状态同步：如果活跃地址与路径管理器不一致，自动同步
        if (s->path_mgr.active_path >= -1) {  // -1=SIGNALING, >=0=候选
            const struct sockaddr_in *active_addr = path_manager_get_addr(s, s->path_mgr.active_path);
            
            if (active_addr && !sockaddr_equal(&s->active_addr, active_addr)) {
                s->path = path_manager_get_path_type(s, s->path_mgr.active_path);
                s->active_addr = *active_addr;
                
                print("I:", LA_F("Synced path after failover", LA_F296, 296));
                
                // 同步 NAT 状态
                if (s->path_mgr.active_path == PATH_IDX_SIGNALING && s->nat.state != NAT_RELAY) {
                    s->nat.state = NAT_RELAY;
                } else if (s->path_mgr.active_path >= 0 && s->nat.state != NAT_CONNECTED) {
                    s->nat.state = NAT_CONNECTED;
                }
            }
        }
        
        // 周期性路径重选（每5秒检查一次是否有更优路径）
        if (now_ms - s->path_mgr.last_reselect_ms > 5000) {
            s->path_mgr.last_reselect_ms = now_ms;
            
            int current_path = s->path_mgr.active_path;
            int best_path = path_manager_select_best_path(s);
            
            // 如果找到更优路径
            if (best_path >= -1 && best_path != current_path) {
                
                path_stats_t *new_stats = path_manager_get_stats(s, best_path);
                path_stats_t *old_stats = (current_path >= -1) ? path_manager_get_stats(s, current_path) : NULL;
                
                // 判断是否值得切换（性能提升显著）
                // 根据当前活跃路径类型查抽切换阈值
                p2p_path_t cur_type = (current_path >= -1) ? p2p_path(hdl) : P2P_PATH_PUNCH;
                if (cur_type == P2P_PATH_NONE) cur_type = P2P_PATH_PUNCH;
                const path_threshold_config_t *thr = &s->path_mgr.thresholds[cur_type];

                if (!old_stats ||                                                                                   // 当前无路径，立即切换
                    (new_stats && new_stats->rtt_ms + thr->rtt_threshold_ms < old_stats->rtt_ms) ||                 // RTT 显著改善
                    (old_stats->loss_rate > thr->loss_threshold) ||                                                  // 当前路径丢包严重
                    (s->path_mgr.strategy == P2P_PATH_STRATEGY_CONNECTION_FIRST && new_stats && new_stats->cost_score < old_stats->cost_score) // 直连优先模式：更低成本
                    ) {

                    /* 执行路径切换（含防抖、历史记录） */
                    int ret = path_manager_switch_path(s, best_path, "periodic reselect", now_ms);
                    if (ret == 0) {
                        /* 切换成功：同步上层状态 */
                        const struct sockaddr_in *new_addr = path_manager_get_addr(s, best_path);
                        if (new_addr) {
                            s->path = path_manager_get_path_type(s, best_path);
                            s->active_addr = *new_addr;
                            
                            print("I:", LA_F("Path switched to better route (idx=%d)", LA_F251, 251), best_path);
                        }
                    } else if (ret == 1) {
                        print("V: %s", LA_S("Path switch debounced, waiting for stability", LA_S83, 83));
                    }
                }
            }
        }
    }

    /* ========================================================================
     * 阶段 9：NAT 类型检测（后台定期运行 STUN 探测）
     * ======================================================================== */

    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT)
        p2p_signal_compact_nat_detect_tick(s);
    else
        p2p_stun_nat_detect_tick(s);


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
    return hdl ? ((p2p_session_t*)hdl)->path : P2P_PATH_NONE;
}

bool
p2p_is_ready(p2p_handle_t hdl) {
    if (!hdl) return false;

    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->state != P2P_STATE_CONNECTED && s->state != P2P_STATE_RELAY) return false;
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
p2p_response(p2p_handle_t hdl, uint8_t msg, const void *data, int len) {

    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) return -1;

    LOCK(s);
    int ret = p2p_signal_compact_response(s, msg, data, len);
    UNLOCK(s);
    return ret;
}

///////////////////////////////////////////////////////////////////////////////

#pragma clang diagnostic pop
