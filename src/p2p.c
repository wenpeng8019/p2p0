#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "P2P"

#include "p2p_internal.h"
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

///////////////////////////////////////////////////////////////////////////////

p2p_handle_t
p2p_create(const char *local_peer_id, const p2p_config_t *cfg) {

    P_check(cfg && local_peer_id, return NULL;)

    ret_t ret;

    static int net_initialized = 0;
    if (!net_initialized) {
#if P_WIN
        // Initialize Winsock on Windows (no-op on POSIX)
        if ((ret = P_net_init()) != E_NONE) {
            print("E:", LA_F("Initialize network subsystem failed(%d)", LA_S33, 228), ret);
            return NULL;
        }
#endif
        net_initialized = 1;
    }

    if (cfg->signaling_mode == P2P_SIGNALING_MODE_PUBSUB) {
        if (!cfg->gh_token || !cfg->gist_id) {
            print("E: %s", LA_S("PUBSUB mode requires gh_token and gist_id", LA_S64, 195));
            return NULL;
        }
    }
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT || cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) {
        if (!cfg->server_host) {
            print("E: %s", LA_S("RELAY/COMPACT mode requires server_host", LA_S73, 199));
            return NULL;
        }
    } else {
        print("E: %s", LA_S("Invalid signaling mode in configuration", LA_S38, 185));
        return NULL;
    }

    // 创建 UDP 套接字，port 0 为有效值，表示由操作系统分配随机端口
    print("I:", LA_F("Open P2P UDP socket on port %d", LA_F94, 232), cfg->bind_port);
    sock_t sock = udp_open_socket(cfg->bind_port);
    if (sock == P_INVALID_SOCKET) {
        print("E:", LA_F("Open P2P UDP socket on port %d failed(%d)", LA_F95, 33), cfg->bind_port, P_sock_errno());
        return NULL;
    }

    p2p_session_t *s = (p2p_session_t*)calloc(1, sizeof(*s));
    if (!s) {
        print("E: %s", LA_S("Failed to allocate memory for session", LA_S26, 225));
        P_sock_close(sock);
        return NULL;
    }

    // 初始化信令服务模式
    print("I:", LA_F("Initialize signaling mode: %d", LA_F78, 243), (int)cfg->signaling_mode);
    if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT) p2p_signal_compact_init(&s->sig_compact_ctx);
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) p2p_signal_relay_init(&s->sig_relay_ctx);
    else if ((ret = p2p_signal_pubsub_init(&s->sig_pubsub_ctx, cfg->gh_token, cfg->gist_id)) != E_NONE) {
        print("E:", LA_F("Initialize PUBSUB signaling context failed(%d)", LA_F77, 229), ret);
        free(s); P_sock_close(sock);
        return NULL;
    } else if (cfg->auth_key) {
        strncpy(s->sig_pubsub_ctx.auth_key, cfg->auth_key, sizeof(s->sig_pubsub_ctx.auth_key) - 1); // todo 用 strdup
    }

    // 初始化路由层（用于检测是否处于同一子网）
    route_init(&s->route);

    // 获取本地所有有效的网络地址
    if ((ret = route_detect_local(&s->route)) < 0) {
        print("E:", LA_F("Detect local network interfaces failed(%d)", LA_F65, 227), ret);
        free(s); P_sock_close(sock);
        return NULL;
    }

    // 初始化候选地址数组（动态分配，初始容量 8）
    const int initial_cand_cap = 8;
    s->local_cands  = (p2p_candidate_entry_t *)calloc(initial_cand_cap, sizeof(p2p_candidate_entry_t));
    s->remote_cands = (p2p_remote_candidate_entry_t *)calloc(initial_cand_cap, sizeof(p2p_remote_candidate_entry_t));
    if (!s->local_cands || !s->remote_cands) {
        print("E: %s", LA_S("Failed to allocate memory for candidate lists", LA_S25, 224));
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

    // 初始化路径管理器（多路径并行支持）
    p2p_path_strategy_t strategy = (p2p_path_strategy_t)(cfg->path_strategy);
    if (strategy < P2P_PATH_STRATEGY_CONNECTION_FIRST || strategy > P2P_PATH_STRATEGY_HYBRID) {
        strategy = P2P_PATH_STRATEGY_CONNECTION_FIRST; // 默认：直连优先
    }
    path_manager_init(&s->path_mgr, strategy);
    print("I:", LA_F("Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)", LA_F100, 232), strategy);

    // 初始化传输层（可选的高级传输模块）
    // + 这里的 s->trans 只用于高级传输模块（DTLS/OpenSSL/SCTP/PseudoTCP）
    s->trans = NULL;
    
    if (cfg->use_dtls) {
#ifdef WITH_DTLS
        print("I: %s", LA_S("DTLS (MbedTLS) enabled as transport layer", 0, 0));
        s->trans = &p2p_trans_dtls;
#else
        print("W: %s", LA_S("DTLS (MbedTLS) requested but library not linked", LA_S21, 170));
#endif
    } 
    else if (cfg->use_openssl) {
#ifdef WITH_OPENSSL
        print("I: %s", LA_S("OpenSSL DTLS enabled as transport layer", 0, 0));
        s->trans = &p2p_trans_openssl;
#else
        print("W: %s", LA_S("OpenSSL requested but library not linked", LA_S53, 191));
#endif
    }
    else if (cfg->use_sctp) {
#ifdef WITH_SCTP
        print("I: %s", LA_S("SCTP (usrsctp) enabled as transport layer", 0, 0));
        s->trans = &p2p_trans_sctp;
#else
        print("W: %s", LA_S("SCTP (usrsctp) requested but library not linked", LA_S78, 202));
#endif
    }
    else if (cfg->use_pseudotcp) {
        print("I: %s", LA_S("PseudoTCP enabled as transport layer", LA_S61, 233));
        s->trans = &p2p_trans_pseudotcp;
    }
    else print("I: %s", LA_S("No advanced transport layer enabled, using simple reliable layer", LA_S49, 231));

    // 执行传输模块的初始化处理
    if (s->trans && s->trans->init) s->trans->init(s);

    // 初始化数据流层
    stream_init(&s->stream, cfg->nagle);

    //----------------------------

    s->signaling_mode = cfg->signaling_mode;
    s->signal_sent = false;

    s->state = P2P_STATE_INIT;
    s->path = P2P_PATH_NONE;

    s->nat_type = P2P_NAT_UNKNOWN;
    s->ice_state = P2P_ICE_STATE_INIT;

    P_clock _clk; P_clock_now(&_clk);
    s->last_update = clock_ms(_clk);

    //----------------------------

#ifdef P2P_THREADED
    if (cfg->threaded) {
        print("I: %s", LA_S("Starting internal thread", LA_S87, 237));
        if ((ret = p2p_thread_start(s)) != E_NONE) {
            print("E:", LA_F("Start internal thread failed(%d)", LA_F148, 570), ret);
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
        print("I: %s", LA_S("Stopping internal thread", LA_S88, 238));
        p2p_thread_stop(s);
    }
#endif

    //  COMPACT 模式的 server UDP 套接口和 NAT p2p 是同一个
    if (s->sock != P_INVALID_SOCKET) {

        // 发送 FIN 包，断开 P2P 连接
        if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {

            print("I: %s", LA_S("Sending FIN packet to peer", LA_S80, 234));
            udp_send_packet(s->sock, &s->active_addr, P2P_PKT_FIN, 0, 0, NULL, 0);
        }

        // COMPACT 模式：主动通知信令服务器释放配对槽位
        // 不管当前处于哪个状态（连接前/连接后/中继中）均发送；
        // 服务器收到后即刻释放槽位。如果服务器未实现此包类型的处理，
        // 将自动降级为 90 秒超时清除机制。
        if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT &&
            s->sig_compact_ctx.state != SIGNAL_COMPACT_INIT) {

            print("I: %s", LA_S("Sending UNREGISTER packet to COMPACT signaling server", LA_S82, 236));

            uint8_t unreg[P2P_PEER_ID_MAX * 2];
            memset(unreg, 0, sizeof(unreg));
            memcpy(unreg,                   s->sig_compact_ctx.local_peer_id,  P2P_PEER_ID_MAX);
            memcpy(unreg + P2P_PEER_ID_MAX, s->sig_compact_ctx.remote_peer_id, P2P_PEER_ID_MAX);
            udp_send_packet(s->sock, &s->sig_compact_ctx.server_addr,
                            SIG_PKT_UNREGISTER, 0, 0, unreg, sizeof(unreg));

            printf(LA_F("Sent UNREGISTER Pkt for local_peer_id=%s, remote_peer_id=%s", LA_F145, 240),
                          s->sig_compact_ctx.local_peer_id, s->sig_compact_ctx.remote_peer_id);
        }

        print("I: %s", LA_S("Close P2P UDP socket", LA_S12, 222));
        P_sock_close(s->sock);
    }

    // RELAY 模式：关闭 TCP 长连接
    if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY
        && s->sig_relay_ctx.state != SIGNAL_DISCONNECTED) {

        print("I: %s", LA_S("Closing TCP connection to RELAY signaling server", LA_S13, 223));
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
        print("E: %s", LA_S("COMPACT mode requires explicit remote_peer_id", LA_S14, 163));
        s->state = P2P_STATE_ERROR;
        return -1;
    }

    LOCK(s);

    // 设置对方 id
    if (remote_peer_id) {
        memset(s->remote_peer_id, 0, sizeof(s->remote_peer_id));
        strncpy(s->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    } else s->remote_peer_id[0] = '\0';

    // 初始化相关状态
    s->signal_sent = false;
    s->cands_pending_send = false;

    ret_t ret;
    switch (s->signaling_mode) {

        // 对于 COMPACT 模式
        case P2P_SIGNALING_MODE_COMPACT: {

            // 解析信令服务器地址
            assert(s->cfg.server_host);         // p2p_create 成功会确保这个条件
            struct sockaddr_in server_addr;
            if ((ret = resolve_host(s->cfg.server_host, s->cfg.server_port, &server_addr)) != E_NONE) {
                print("E:", LA_F("Resolve COMPACT signaling server address: %s:%d failed(%d)", LA_F136, 230),
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
                    if (idx < 0) break;
                    p2p_candidate_entry_t *c = &s->local_cands[idx];

                    c->type = P2P_ICE_CAND_HOST;
                    c->addr = s->route.local_addrs[i];
                    c->addr.sin_port = loc.sin_port;  // 使用实际绑定端口
                    print("I:", LA_F("Append Host candidate: %s:%d", LA_F59, 4),
                                 inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
                }
            }
            else print("I: %s", LA_S("Skipping local Host candidates on --public-only", LA_S85, 121));

            // 注册（连接）到 COMPACT 信令服务器
            print("I:", LA_F("Register to COMPACT signaling server at %s:%d", LA_F132, 244),
                         inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

            if ((ret = p2p_signal_compact_connect(s, s->local_peer_id, remote_peer_id, &server_addr)) != E_NONE) {
                print("E:", LA_F("Connect to COMPACT signaling server failed(%d)", LA_F62, 226), ret);
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
            if (s->sig_relay_ctx.state != SIGNAL_CONNECTED) {

                print("I:", LA_F("Connecting to RELAY signaling server at %s:%d", LA_F64, 239),
                             s->cfg.server_host, s->cfg.server_port);

                if ((ret = p2p_signal_relay_login(&s->sig_relay_ctx, s->cfg.server_host, s->cfg.server_port, s->local_peer_id)) != E_NONE) {
                    print("E:", LA_F("Connect to RELAY signaling server failed(%d)", LA_F63, 176), ret);
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

                print("I:", LA_F("Sent initial offer(%d) to %s)", LA_F146, 245), n, remote_peer_id);
                if ((ret = p2p_signal_relay_send_connect(&s->sig_relay_ctx, remote_peer_id, buf, n)) != E_NONE) {
                    print("E:", LA_F("Send offer to RELAY signaling server failed(%d)", LA_F141, 568), ret);
                    // todo
                }
                s->signal_sent = true;
            }
            // 被动模式：等待任意对等方的 offer
            else if (!remote_peer_id) {

                print("I: %s", LA_W("Waiting for incoming offer from any peer", LA_W110, 100));
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
                print("I: %s", LA_S("PUBSUB (PUB): gathering candidates, waiting for STUN before publishing", LA_S62, 85));
            }
            else {

                // SUB 角色（订阅者）：被动等待连接
                p2p_signal_pubsub_set_role(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_SUB);

                // SUB 模式：被动等待 offer，收到后自动回复
                print("I: %s", LA_S("PUBSUB (SUB): waiting for offer from any peer", LA_S63, 86));
            }
            break;
        }

        default:
            print("E:", LA_F("Unknown signaling mode: %d", LA_F155, 140), s->signaling_mode);
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

    // 发送 FIN 包通知对端断开连接（仅在已连接状态）
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {

        print("V: %s", LA_S("Sending FIN packet to peer before closing", LA_S81, 235));
        udp_send_packet(s->sock, &s->active_addr, P2P_PKT_FIN, 0, 0, NULL, 0);
    }

    // 标记连接状态为 CLOSING，等待对端确认（收到 FIN 包）后转为 CLOSED
    int prev_state = s->state;
    s->state = P2P_STATE_CLOSING;
    
    // 触发回调（仅在从连接状态转换时）
    if ((prev_state == P2P_STATE_CONNECTED || prev_state == P2P_STATE_RELAY)
        && s->cfg.on_disconnected) s->cfg.on_disconnected(s, s->cfg.userdata);
    
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
 *   - 检测连接超时（转 NAT_DISCONNECTED 状态）
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
                printf(LA_F("Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d", LA_F128, 141),
                    inet_ntoa(from.sin_addr), ntohs(from.sin_port), msg_type, n);
                p2p_stun_handle_packet(s, buf, n, &from);  // 处理 Binding Response
                p2p_turn_handle_packet(s, buf, n, &from);  // 处理 Allocate Success 等
                continue;
            }
        }

        // --------------------
        // P2P 协议（见 p2pp.h）
        // --------------------

        if (n < P2P_HDR_SIZE) continue;

        // 获取 header
        p2p_packet_hdr_t hdr;
        p2p_pkt_hdr_decode(buf, &hdr);

        // 获取 payload
        const uint8_t *payload = buf + P2P_HDR_SIZE;
        int payload_len = n - P2P_HDR_SIZE;

        switch (hdr.type) {

            // --------------------
            // 数据传输（P2P 直连 或 服务器中继）
            // --------------------

            case P2P_PKT_RELAY_DATA:
            case P2P_PKT_RELAY_ACK:
                if (compact_on_relay_packet(s, hdr.type, &payload, &payload_len, &from) != 0)
                    break;
                if (hdr.type == P2P_PKT_RELAY_DATA) goto handle_data;
                else goto handle_ack;

            /*
             * 协议：P2P_PKT_DATA (0x20)
             * 包头: [type=0x20 | flags=0 | seq=序列号(2B)]
             * 负载: [data(N)]
             * 说明：P2P 数据包，由 reliable 层或高级传输层处理
             */
            case P2P_PKT_DATA:
                printf(LA_F("Received DATA pkt from %s:%d, seq=%u, len=%d", LA_F112, 143),
                    inet_ntoa(from.sin_addr), ntohs(from.sin_port), hdr.seq, payload_len);
            handle_data:

                // 记录数据包接收（更新路径活跃时间）
                if (s->path_mgr.active_path >= 0) {
                    path_manager_on_packet_recv(&s->path_mgr, s->path_mgr.active_path, now_ms);
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
                uint16_t ack_seq; nread_s(&ack_seq, payload);
                uint32_t sack; nread_l(&sack, payload + 2);
                if (hdr.type == P2P_PKT_ACK) {
                    printf(LA_F("Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x", LA_F110, 142),
                        inet_ntoa(from.sin_addr), ntohs(from.sin_port), ack_seq, sack);
                }

                // 只有使用基础 reliable 层时才处理（无高级传输层或高级传输层无 on_packet）
                if ((!s->trans || !s->trans->on_packet) && payload_len >= 6) {
                    
                    // 记录 ACK 前的 RTT 值
                    int old_srtt = s->reliable.srtt;
                    
                    // 处理 ACK（reliable 层会更新 RTT）
                    reliable_on_ack(&s->reliable, ack_seq, sack);
                    
                    // 如果 RTT 更新了，同步到路径管理器
                    if (s->reliable.srtt != old_srtt && s->reliable.srtt > 0) {
                        if (s->path_mgr.active_path >= 0) {
                            path_manager_update_metrics(&s->path_mgr, s->path_mgr.active_path, (uint32_t)s->reliable.srtt, true);
                            path_manager_update_quality(&s->path_mgr, s->path_mgr.active_path);
                        }
                    }
                }
                break;
            }
            // --------------------
            // NAT 链路层（打洞、保活、断开）
            // --------------------

            case P2P_PKT_PUNCH:
                nat_on_punch(s, &hdr, payload, payload_len, &from);
                break;
            case P2P_PKT_FIN:
                nat_on_fin(s, &from);
                break;

            // --------------------
            // 路由探测包，即处于同一个子网内的双方的探测和确认
            // --------------------

            case P2P_PKT_ROUTE_PROBE:
                route_on_probe(&s->route, &from, s->sock);
                break;
            case P2P_PKT_ROUTE_PROBE_ACK:
                route_on_probe_ack(&s->route, &from);
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
                compact_on_msg_req(s, hdr.flags, payload, payload_len, &from);
                break;
            case SIG_PKT_MSG_REQ_ACK:
                compact_on_msg_req_ack(s, payload, payload_len, &from);
                break;
            case SIG_PKT_MSG_RES:
                compact_on_msg_res(s, payload, payload_len, &from);
                break;
            default:
                print("V:", LA_F("Received UNKNOWN pkt type: 0x%02X", LA_F130, 95), hdr.type);
                printf(LA_F("Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d", LA_F129, 150),
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port), hdr.type, hdr.seq, payload_len);
                break;
        }

    } // while ((n = udp_recv_from(s->sock, &from, buf, sizeof(buf))) > 0)

    /* ========================================================================
     * 阶段 2：信令服务维护（主动拉取远端候选地址）
     * ======================================================================== */

    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {

        // 信令层维护（注册、等待、候选同步）
        if (s->sig_compact_ctx.state == SIGNAL_COMPACT_REGISTERING ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_REGISTERED ||
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

    /* ========================================================================
     * 阶段 4：NAT 层维护（打洞、保活）
     * ======================================================================== */

    if (s->nat.state != NAT_INIT) {
        nat_tick(s, now_ms);
    }

    // 升级到 LAN 路径（如果确认同一子网）
    if (s->route.lan_confirmed && s->path != P2P_PATH_LAN) {
        print("I: %s", LA_S("Same subnet confirmed, switching to LAN path", LA_S77, 624));
        
        // 添加 LAN 路径到路径管理器
        int lan_idx = path_manager_add_or_update_path(&s->path_mgr, P2P_PATH_LAN, &s->route.lan_peer_addr);
        if (lan_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, lan_idx, PATH_STATE_ACTIVE);
            print("I:", LA_F("Added LAN path to path manager, idx=%d", LA_F56, 232), lan_idx);
            
            // 重新选择最佳路径（LAN 应当优先）
            int best_path = path_manager_select_best_path(&s->path_mgr);
            if (best_path >= 0 && best_path == lan_idx) {
                s->path = P2P_PATH_LAN;
                s->active_addr = s->route.lan_peer_addr;
                s->path_mgr.active_path = best_path;
                print("I: %s", LA_S("Switched to LAN path", LA_S90, 624));
            }
        } else {
            // 降级：路径管理器失败，使用传统方式
            s->path = P2P_PATH_LAN;
            s->active_addr = s->route.lan_peer_addr;
        }
    }

    /* ========================================================================
     * 阶段 5：统一状态机（集中处理所有 P2P 连接状态转换）
     * ======================================================================== */

    // 转换：REGISTERING → PUNCHING（开始打洞）
    if (s->state == P2P_STATE_REGISTERING && s->nat.state == NAT_PUNCHING) {
        print("I: %s", LA_S("P2P punching in progress ...", LA_S58, 623));
        s->state = P2P_STATE_PUNCHING;
    }

    // 转换：PUNCHING/REGISTERING → CONNECTED（NAT 穿透成功）
    // + 注意，NAT 变为 CONNECTED 状态时，P2P 可能还未进入 PUNCHING 状态
    //   因为，NAT 的 CONNECTED，是收到对方发过来的包，说明自己的端口对对方是开放的了
    //   但 PUNCHING 状态是向对方端口发送打洞包，也就是检测对方端口是否可写入。
    //   所以，对方可能先于自己获得对方（也就是自己）的候选地址，并先完成 NAT 穿透（对方发包过来），此时自己还未开始打洞（PUNCHING）
    if ((s->state == P2P_STATE_PUNCHING || s->state == P2P_STATE_REGISTERING) &&
        s->nat.state == NAT_CONNECTED) {

        print("I: %s", LA_S("P2P connection established", LA_S56, 621));
        s->state = P2P_STATE_CONNECTED;
        
        // 添加 PUNCH 路径到路径管理器
        int path_idx = path_manager_add_or_update_path(&s->path_mgr, P2P_PATH_PUNCH, &s->nat.peer_addr);
        if (path_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, path_idx, PATH_STATE_ACTIVE);
            print("I:", LA_F("Added PUNCH path to path manager, idx=%d", LA_F57, 232), path_idx);
        }
        
        // 选择最佳路径
        int best_path = path_manager_select_best_path(&s->path_mgr);
        if (best_path >= 0) {
            path_info_t *pi = path_manager_get_active_path(&s->path_mgr);
            if (pi) {
                s->path = pi->type;
                s->active_addr = pi->addr;
                s->path_mgr.active_path = best_path;
                print("I:", LA_F("Selected path: %s (idx=%d)", LA_F139, 232),
                       path_type_str(pi->type), best_path);
            }
        } else {
            // 降级：路径管理器无可用路径，使用传统方式
            s->path = P2P_PATH_PUNCH;
            s->active_addr = s->nat.peer_addr;
        }

        // 触发连接建立回调
        if (s->cfg.on_connected) {
            s->cfg.on_connected(s, s->cfg.userdata);
        }

        // 查找远端 Host 候选（用于同子网检测）
        struct sockaddr_in *peer_priv = NULL;
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            if (s->remote_cands[i].cand.type == P2P_ICE_CAND_HOST) {
                peer_priv = &s->remote_cands[i].cand.addr;
                break;
            }
        }

        // 如果对方内网 IP 和自己属于同一个子网段内
        if (peer_priv && route_check_same_subnet(&s->route, peer_priv)) {

            if (!s->cfg.disable_lan_shortcut) {

                // 发送 ROUTE_PROBE，确认后将切换到 LAN 路径
                struct sockaddr_in priv = *peer_priv;
                priv.sin_port = s->nat.peer_addr.sin_port;
                route_send_probe(&s->route, s->sock, &priv, 0);

                print("I:", LA_F("Same subnet detecting, sent ROUTE_PROBE to %s:%d", LA_F138, 109),
                       inet_ntoa(priv.sin_addr), ntohs(priv.sin_port));
            }
            // 同子网但 disable_lan_shortcut=true：跳过升级，仅打印日志
            else print("V: %s", LA_S("Skipping LAN shortcut on --disable-lan-shortcut", LA_S84, 122));
        }
    }

    // NAT 重新连接后恢复路径（NAT_RELAY → NAT_CONNECTED）
    if (s->nat.state == NAT_CONNECTED && s->state == P2P_STATE_RELAY) {
        print("I: %s", LA_S("NAT connection recovered, upgrading from RELAY to CONNECTED", LA_S46, 620));
        
        // 更新 PUNCH 路径状态
        int punch_idx = path_manager_find_path(&s->path_mgr, P2P_PATH_PUNCH);
        if (punch_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, punch_idx, PATH_STATE_ACTIVE);
            path_manager_add_or_update_path(&s->path_mgr, P2P_PATH_PUNCH, &s->nat.peer_addr);
        } else {
            punch_idx = path_manager_add_or_update_path(&s->path_mgr, P2P_PATH_PUNCH, &s->nat.peer_addr);
            if (punch_idx >= 0) {
                path_manager_set_path_state(&s->path_mgr, punch_idx, PATH_STATE_ACTIVE);
            }
        }
        
        // 标记中继路径为降级（但不移除，保留作为备份）
        int relay_idx = path_manager_find_path(&s->path_mgr, P2P_PATH_RELAY);
        if (relay_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, relay_idx, PATH_STATE_DEGRADED);
        }
        
        // 重新选择最佳路径（PUNCH 应当优先）
        int best_path = path_manager_select_best_path(&s->path_mgr);
        if (best_path >= 0) {
            path_info_t *pi = &s->path_mgr.paths[best_path];
            s->path = pi->type;
            s->active_addr = pi->addr;
            s->path_mgr.active_path = best_path;
            s->state = P2P_STATE_CONNECTED; // 恢复为 CONNECTED 状态
            print("I:", LA_F("Path recovered: switched to %s", LA_F101, 232), path_type_str(pi->type));
        }
    }

    // 转换：PUNCHING → RELAY（NAT 打洞失败，添加中继路径）
    if (s->state == P2P_STATE_PUNCHING && s->nat.state == NAT_RELAY) {

        // 打洞失败：标记 PUNCH 路径为失效
        int punch_idx = path_manager_find_path(&s->path_mgr, P2P_PATH_PUNCH);
        if (punch_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, punch_idx, PATH_STATE_FAILED);
        }
        
        // 添加中继路径
        print("I: %s", LA_S("P2P punch failed, adding relay path", LA_S57, 622));
        
        struct sockaddr_in relay_addr;
        bool relay_available = false;
        
        if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {
            if (s->sig_compact_ctx.relay_support) {
                relay_addr = s->sig_compact_ctx.server_addr;
                relay_available = true;
                print("I: %s", LA_W("NAT punch failed, using COMPACT server relay", LA_W50, 62));
            } else {
                print("W: %s", LA_W("NAT punch failed, server has no relay support", LA_W49, 61));
            }
        } else {
            print("W: %s", LA_W("NAT punch failed, no TURN server configured", LA_W48, 60));
        }
        
        if (relay_available) {
            int relay_idx = path_manager_add_or_update_path(&s->path_mgr, P2P_PATH_RELAY, &relay_addr);
            if (relay_idx >= 0) {
                path_manager_set_path_state(&s->path_mgr, relay_idx, PATH_STATE_ACTIVE);
                print("I:", LA_F("Added RELAY path to path manager, idx=%d", LA_F58, 232), relay_idx);
            }
        }
        
        // 选择最佳可用路径
        int best_path = path_manager_select_best_path(&s->path_mgr);
        if (best_path >= 0) {
            path_info_t *pi = &s->path_mgr.paths[best_path];
            s->path = pi->type;
            s->active_addr = pi->addr;
            s->path_mgr.active_path = best_path;
            s->state = P2P_STATE_RELAY;
            print("I:", LA_F("Using path: %s", LA_F157, 232), path_type_str(pi->type));
        } else {
            // 无可用路径：降级到传统方式
            s->state = P2P_STATE_RELAY;
            s->path = P2P_PATH_RELAY;
            s->active_addr = relay_available ? relay_addr : s->nat.peer_addr;
        }
    }

    // 转换：CONNECTED/RELAY → CLOSED（收到 FIN 包主动断开）
    if (s->nat.state == NAT_CLOSED && 
        (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY)) {
        print("I: %s", LA_S("Received FIN packet, connection closed", LA_S67, 621));
        s->state = P2P_STATE_CLOSED;
        if (s->cfg.on_disconnected) s->cfg.on_disconnected(s, s->cfg.userdata);
    }

    // 转换：CONNECTED → RELAY（NAT 连接超时断开，降级到中继模式）
    if (s->nat.state == NAT_DISCONNECTED && s->state == P2P_STATE_CONNECTED) {
        print("W: %s", LA_S("NAT connection timeout, downgrading to relay mode", LA_S47, 620));
        
        // 标记 PUNCH 路径为失效
        int punch_idx = path_manager_find_path(&s->path_mgr, P2P_PATH_PUNCH);
        if (punch_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, punch_idx, PATH_STATE_FAILED);
        }
        
        // 标记 LAN 路径为失效（如果存在）
        int lan_idx = path_manager_find_path(&s->path_mgr, P2P_PATH_LAN);
        if (lan_idx >= 0) {
            path_manager_set_path_state(&s->path_mgr, lan_idx, PATH_STATE_FAILED);
        }
        
        // 尝试切换到中继路径
        int best_path = path_manager_select_best_path(&s->path_mgr);
        if (best_path >= 0) {
            path_info_t *pi = &s->path_mgr.paths[best_path];
            s->path = pi->type;
            s->active_addr = pi->addr;
            s->path_mgr.active_path = best_path;
            s->state = P2P_STATE_RELAY;
            print("I:", LA_F("Switched to backup path: %s", LA_F150, 232), path_type_str(pi->type));
        } else {
            // 无备用路径：NAT 层会继续尝试恢复
            s->state = P2P_STATE_RELAY;
            s->nat.state = NAT_RELAY;
        }
    }

    /* ========================================================================
     * 阶段 6：数据传输（应用层数据收发）
     * ======================================================================== */

    // 发送数据：数据流层 → 传输层 flush 写入
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
        
        // 如果使用高级传输层（DTLS、SCTP、PseudoTCP）
        if (s->trans && s->trans->send_data) {
            // 由高级传输模块自行处理流的数据
            n = ring_read(&s->stream.send_ring, buf, sizeof(buf));
            if (n > 0) {
                s->trans->send_data(s, buf, n);
                s->stream.pending_bytes -= n;
                s->stream.send_offset += n;
            }
        } 
        // 使用基础 reliable 层
        else stream_flush_to_reliable(&s->stream, &s->reliable);
    }

    // reliable 周期 tick：发送/重传数据包 + 发 ACK
    if (!s->trans || !s->trans->on_packet) {
        int is_relay = (s->state == P2P_STATE_RELAY);
        reliable_tick(&s->reliable, s->sock, &s->active_addr, is_relay);
    }

    // 传输模块周期 tick（重传，拥塞控制等）
    if (s->trans && s->trans->tick) {
        if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
            s->trans->tick(s);
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

        // 健康检查
        path_manager_health_check(&s->path_mgr, now_ms);
        
        // 周期性路径重选（每5秒检查一次是否有更优路径）
        static uint64_t last_path_reselect = 0;
        if (now_ms - last_path_reselect > 5000) {
            last_path_reselect = now_ms;
            
            int current_path = s->path_mgr.active_path;
            int best_path = path_manager_select_best_path(&s->path_mgr);
            
            // 如果找到更优路径且不在冷却期内
            if (best_path >= 0 && best_path != current_path &&
                now_ms - s->path_mgr.last_switch_time > s->path_mgr.path_switch_cooldown_ms) {
                
                path_info_t *new_pi = &s->path_mgr.paths[best_path];
                path_info_t *old_pi = (current_path >= 0) ? &s->path_mgr.paths[current_path] : NULL;
                
                // 判断是否值得切换（性能提升显著）
                bool should_switch = false;
                if (!old_pi) {
                    should_switch = true; // 当前无路径，立即切换
                } else if (new_pi->rtt_ms + s->path_mgr.switch_rtt_threshold_ms < old_pi->rtt_ms) {
                    should_switch = true; // RTT 显著改善
                } else if (old_pi->loss_rate > s->path_mgr.switch_loss_threshold) {
                    should_switch = true; // 当前路径丢包严重
                } else if (s->path_mgr.strategy == P2P_PATH_STRATEGY_CONNECTION_FIRST &&
                           new_pi->cost_score < old_pi->cost_score) {
                    should_switch = true; // 直连优先模式：更低成本
                }
                
                if (should_switch) {
                    s->path = new_pi->type;
                    s->active_addr = new_pi->addr;
                    s->path_mgr.active_path = best_path;
                    s->path_mgr.last_switch_time = now_ms;
                    s->path_mgr.total_path_switches++;
                    
                    print("I:", LA_F("Path switched: %s -> %s (RTT: %u -> %u ms)", LA_F102, 232),
                           old_pi ? path_type_str(old_pi->type) : "NONE",
                           path_type_str(new_pi->type),
                           old_pi ? old_pi->rtt_ms : 9999,
                           new_pi->rtt_ms);
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
p2p_msg_send(p2p_handle_t hdl, uint8_t msg_type, const void *data, int len) {

    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) return -1;

    LOCK(s);
    int ret = p2p_signal_compact_msg_send(s, msg_type, data, len);
    UNLOCK(s);
    return ret;
}

int
p2p_msg_reply(p2p_handle_t hdl, uint16_t req_id,
              uint8_t msg_type, const void *data, int len) {

    if (!hdl) return -1;
    p2p_session_t *s = (p2p_session_t*)hdl;
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) return -1;

    LOCK(s);
    int ret = p2p_signal_compact_msg_reply(s, req_id, msg_type, data, len);
    UNLOCK(s);
    return ret;
}

///////////////////////////////////////////////////////////////////////////////

#pragma clang diagnostic pop
