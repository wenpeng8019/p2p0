
#include "p2p_internal.h"
#include "p2p_udp.h"
#include "p2p_signal_protocol.h"
#include <ifaddrs.h>
#include <net/if.h>

///////////////////////////////////////////////////////////////////////////////

/* ---- 信令重发配置 ---- */
#define SIGNAL_RESEND_INTERVAL_MS  5000   /* 信令重发间隔 (5秒) */
#define SIGNAL_MAX_RESEND_COUNT    12     /* 最大重发次数 (共约60秒) */

/* ---- 锁辅助函数（单线程模式下为空操作） ---- */

#ifdef P2P_THREADED
#define LOCK(s)   do { if ((s)->cfg.threaded) pthread_mutex_lock(&(s)->mtx); } while(0)
#define UNLOCK(s) do { if ((s)->cfg.threaded) pthread_mutex_unlock(&(s)->mtx); } while(0)
#else
#define LOCK(s)   ((void)0)
#define UNLOCK(s) ((void)0)
#endif

// 解析主机名
static int resolve_host(const char *host, uint16_t port, struct sockaddr_in *out) {

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
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

p2p_session_t *p2p_create(const p2p_config_t *cfg) {
    if (!cfg) return NULL;

    p2p_session_t *s = (p2p_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->cfg = *cfg;
    if (s->cfg.update_interval_ms <= 0)
        s->cfg.update_interval_ms = 10;
    s->state = P2P_STATE_IDLE;
    s->path = P2P_PATH_NONE;

    // 创建 UDP 套接字
    s->sock = udp_create_socket(cfg->bind_port);
    if (s->sock < 0) { free(s); return NULL; }

    // 初始化链路层（基于 NAT 穿透的 P2P）
    nat_init(&s->nat);
    
    // 初始化路由层（检测是否处于同一子网）
    route_init(&s->route);

    // 初始化传输层（支持：DTLS/OpenSSL/SCTP/PseudoTCP/Reliable）
    // + 如果配置中启动了多个传输模块，则优先级为
    //   DTLS > OpenSSL > SCTP > PseudoTCP > Reliable
    if (cfg->use_dtls) {
#ifdef WITH_DTLS
        s->trans = &p2p_trans_dtls;
#else
        printf("[WARN] DTLS (MbedTLS) requested but library not linked!\n");
        s->trans = &p2p_trans_reliable;
#endif
    } 
    else if (cfg->use_openssl) {
#ifdef WITH_OPENSSL
        s->trans = &p2p_trans_openssl;
#else
        printf("[WARN] OpenSSL requested but library not linked!\n");
        s->trans = &p2p_trans_reliable;
#endif
    }
    else if (cfg->use_sctp) {
#ifdef WITH_SCTP
        s->trans = &p2p_trans_sctp;
#else
        printf("[WARN] SCTP (usrsctp) requested but library not linked!\n");
        s->trans = &p2p_trans_reliable;
#endif
    }
    else if (cfg->use_pseudotcp) {
        s->trans = &p2p_trans_pseudotcp;
    } else {
        s->trans = &p2p_trans_reliable;
    }

    // 执行传输模块的初始化处理
    if (s->trans && s->trans->init) {
        s->trans->init(s);
    }

    // 初始化数据流层
    stream_init(&s->stream, cfg->nagle);

    // 初始化信令服务模式
    s->signaling_mode = cfg->signaling_mode;
    s->signal_sent = 0;
    
    memset(&s->sig_relay_ctx, 0, sizeof(s->sig_relay_ctx));
    s->sig_relay_ctx.fd = -1;
    s->sig_relay_ctx.state = SIGNAL_DISCONNECTED;
    
    memset(&s->sig_pubsub_ctx, 0, sizeof(s->sig_pubsub_ctx));
    
    // ICE 模式：信令服务器连接在首次执行 p2p_connect 时建立
    // PUBSUB 模式：角色在 p2p_connect 时根据 remote_peer_id 决定

    // 获取本地所有有效的网络地址
    route_detect_local(&s->route);

    s->last_update = time_ms();

#ifdef P2P_THREADED
    if (cfg->threaded) {
        if (p2p_thread_start(s) < 0) {
            close(s->sock);
            free(s);
            return NULL;
        }
    }
#endif

    return s;
}

void p2p_destroy(p2p_session_t *s) {
    if (!s) return;

#ifdef P2P_THREADED
    if (s->cfg.threaded)
        p2p_thread_stop(s);
#endif

    // 关闭信令连接
    p2p_signal_relay_close(&s->sig_relay_ctx);

    if (s->sock >= 0) {

        // 发送 FIN 包，断开连接
        if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY)
            udp_send_packet(s->sock, &s->active_addr,
                            P2P_PKT_FIN, 0, 0, NULL, 0);
        close(s->sock);
    }

    free(s);
}

int p2p_connect(p2p_session_t *s, const char *remote_peer_id) {

    if (!s) return -1;
    if (s->state != P2P_STATE_IDLE) return -1;

    LOCK(s);

    // 保存目标 peer_id（信令模式已在 create 时设置）
    // SIMPLE 模式必须指定 remote_peer_id
    if (s->signaling_mode == P2P_SIGNALING_MODE_SIMPLE && !remote_peer_id) {
        printf("[ERROR] SIMPLE mode requires explicit remote_peer_id\n");
        s->state = P2P_STATE_ERROR;
        UNLOCK(s);
        return -1;
    }
    
    if (remote_peer_id) {
        strncpy(s->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
        s->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    } else {
        s->remote_peer_id[0] = '\0';  // 空字符串表示接受任意连接
    }
    s->signal_sent = 0;
    s->cands_pending_send = 0;

    switch (s->signaling_mode) {

    case P2P_SIGNALING_MODE_SIMPLE: {
        // 简单无状态信令：UDP 协议，无需登录，只注册 peer_id 映射
        struct sockaddr_in server_addr;
        if (!s->cfg.server_host ||
            resolve_host(s->cfg.server_host, s->cfg.server_port, &server_addr) < 0) {
            s->state = P2P_STATE_ERROR;
            UNLOCK(s);
            return -1;
        }

        s->state = P2P_STATE_REGISTERING;
        
        // 初始化 SIMPLE 信令上下文
        signal_simple_init(&s->sig_simple_ctx);
        
        // 收集 Host 候选地址（SIMPLE 模式下只收集本地网卡地址，存入 session）
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == 0) {
            for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
                if (ifa->ifa_flags & IFF_LOOPBACK) continue;
                if (s->local_cand_cnt >= P2P_MAX_CANDIDATES) break;
                
                struct sockaddr_in host_addr;
                memcpy(&host_addr, ifa->ifa_addr, sizeof(host_addr));
                
                // 获取本地绑定端口
                struct sockaddr_in loc;
                socklen_t loclen = sizeof(loc);
                getsockname(s->sock, (struct sockaddr *)&loc, &loclen);
                host_addr.sin_port = loc.sin_port;
                
                // 直接添加到 session 的本地候选列表
                s->local_cands[s->local_cand_cnt].type = P2P_CAND_HOST;
                s->local_cands[s->local_cand_cnt].addr = host_addr;
                s->local_cand_cnt++;
                
                printf("[SIMPLE] Added Host candidate: %s:%d\n",
                       inet_ntoa(host_addr.sin_addr), ntohs(host_addr.sin_port));
            }
            freeifaddrs(ifaddr);
        }
        
        // 初始化打洞上下文
        nat_init(&s->nat);
        
        // 开始信令注册
        signal_simple_start(s, s->cfg.peer_id, remote_peer_id, &server_addr,
                            s->cfg.verbose_nat_punch);
        
        printf("[CONNECT] SIMPLE mode: registering <%s → %s> with %d candidates\n",
               s->cfg.peer_id, remote_peer_id, s->local_cand_cnt);
        break;
    }

    case P2P_SIGNALING_MODE_ICE: {
        // ICE 有状态信令：TCP 长连接，自动登录，交换候选者
        
        if (!s->cfg.server_host) {
            printf("[ERROR] ICE mode requires server_host configuration\n");
            s->state = P2P_STATE_ERROR;
            UNLOCK(s);
            return -1;
        }
        
        // 首次连接：自动连接信令服务器（单例模式，只执行一次）
        if (s->sig_relay_ctx.state != SIGNAL_CONNECTED) {
            if (p2p_signal_relay_connect(&s->sig_relay_ctx, s->cfg.server_host, s->cfg.server_port, s->cfg.peer_id) < 0) {
                printf("[ERROR] Failed to connect to signaling server\n");
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }
        }

        s->state = P2P_STATE_REGISTERING;
        
        // 收集 ICE 候选者（同步收集 Host 候选地址）
        if (s->cfg.use_ice) {
            p2p_ice_gather_candidates(s);
        }
        
        // 如果指定了 remote_peer_id，立即发送初始 offer
        if (remote_peer_id && s->local_cand_cnt > 0) {
            p2p_signaling_payload_t payload = {0};
            strncpy(payload.sender, s->cfg.peer_id, 31);
            strncpy(payload.target, remote_peer_id, 31);
            payload.candidate_count = s->local_cand_cnt;
            memcpy(payload.candidates, s->local_cands, 
                   sizeof(p2p_candidate_t) * s->local_cand_cnt);
            
            uint8_t buf[2048];
            int n = p2p_signal_pack(&payload, buf, sizeof(buf));
            if (n > 0) {
                p2p_signal_relay_send_connect(&s->sig_relay_ctx, remote_peer_id, buf, n);
                s->signal_sent = 1;
                printf("[CONNECT] ICE mode: sent initial offer with %d candidates to %s\n", 
                       s->local_cand_cnt, remote_peer_id);
            }
        } else if (!remote_peer_id) {
            // 被动模式：等待任意对等方的 offer
            printf("[CONNECT] ICE mode: waiting for incoming offer from any peer...\n");
        }
        
        // 注意：后续 Srflx 候选者（STUN 响应）会在 p2p_update 中增量发送
        break;
    }

    case P2P_SIGNALING_MODE_PUBSUB: {
        // PUBSUB 模式：使用 Gist 作为信令中介，角色由 remote_peer_id 决定
        
        if (!s->cfg.gh_token || !s->cfg.gist_id) {
            printf("[ERROR] PUBSUB mode requires gh_token and gist_id configuration\n");
            s->state = P2P_STATE_ERROR;
            UNLOCK(s);
            return -1;
        }

        s->state = P2P_STATE_REGISTERING;
        
        // 收集候选者（同步收集 Host 候选地址，异步请求 STUN 获取 Srflx）
        if (s->cfg.use_ice) {
            p2p_ice_gather_candidates(s);
        }
        
        // 根据 remote_peer_id 是否为 NULL 决定角色
        if (remote_peer_id) {
            // PUB 角色（发布者）：主动发起
            p2p_signal_pubsub_init(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_PUB,
                                   s->cfg.gh_token, s->cfg.gist_id);
            // PUB 模式必须等待 STUN 响应（获取公网地址）后才能发布 offer
            // 因为 Gist 无法看到客户端的公网地址（不像信令服务器能看到 TCP 连接地址）
            // offer 发布将在 p2p_update 中收到 Srflx 候选地址后自动完成
            printf("[CONNECT] PUBSUB mode (PUB): gathering candidates, waiting for STUN response before publishing...\n");
        } else {
            // SUB 角色（订阅者）：被动等待
            p2p_signal_pubsub_init(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_SUB,
                                   s->cfg.gh_token, s->cfg.gist_id);
            // SUB 模式：被动等待 offer，收到后自动回复
            printf("[CONNECT] PUBSUB mode (SUB): waiting for offer from any peer...\n");
        }
        break;
    }

    default:
        printf("[ERROR] Unknown signaling mode: %d\n", s->signaling_mode);
        s->state = P2P_STATE_ERROR;
        UNLOCK(s);
        return -1;
    }

    UNLOCK(s);
    return 0;
}

void p2p_close(p2p_session_t *s) {
    if (!s) return;

    LOCK(s);
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
        udp_send_packet(s->sock, &s->active_addr,
                        P2P_PKT_FIN, 0, 0, NULL, 0);
    }
    
    int prev_state = s->state;
    s->state = P2P_STATE_CLOSING;
    
    // 触发断开回调（仅在从连接状态转换时）
    if ((prev_state == P2P_STATE_CONNECTED || prev_state == P2P_STATE_RELAY) &&
        s->cfg.on_disconnected) {
        s->cfg.on_disconnected(s, s->cfg.userdata);
    }
    
    UNLOCK(s);
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 主更新循环 — 驱动所有状态机。
 * 在单线程模式下，应用程序调用此函数。
 * 在多线程模式下，内部线程在锁下调用此函数。
 */
int p2p_update(p2p_session_t *s) {
    if (!s) return -1;

    uint8_t buf[P2P_MTU + 16];
    struct sockaddr_in from;
    int n;

    // 接收并分发数据包
    while ((n = udp_recv_from(s->sock, &from, buf, sizeof(buf))) > 0) {

        // --------------------
        // STUN 服务返回包 
        // --------------------

        // 简单检查：长度 >= 20 且 Magic Cookie 匹配
        if (n >= 20 && buf[0] < 2) { // STUN type 0x00xx or 0x01xx
            uint32_t magic = ntohl(*(uint32_t *)(buf + 4));
            if (magic == STUN_MAGIC) {
                p2p_stun_handle_packet(s, buf, n, &from);
                p2p_turn_handle_packet(s, buf, n, &from);
                continue;
            }
        }

        if (n < P2P_HDR_SIZE) continue;

        p2p_packet_hdr_t hdr;
        pkt_hdr_decode(buf, &hdr);

        const uint8_t *payload = buf + P2P_HDR_SIZE;
        int payload_len = n - P2P_HDR_SIZE;

        switch (hdr.type) {

        // --------------------
        // NAT / 信令包
        // --------------------
    
        case P2P_PKT_REGISTER:
            /* 忽略（服务器发给客户端的不会是 REGISTER） */
            break;

        case P2P_PKT_REGISTER_ACK:
            /* SIMPLE 模式：服务器确认注册 */
            if (s->signaling_mode == P2P_SIGNALING_MODE_SIMPLE) {
                signal_simple_on_packet(s, hdr.type, payload, payload_len, &from);
            }
            break;

        case P2P_PKT_PEER_INFO:
            /* SIMPLE 模式：信令处理 → 打洞启动 */
            if (s->signaling_mode == P2P_SIGNALING_MODE_SIMPLE) {
                if (signal_simple_on_packet(s, hdr.type, payload, payload_len, &from) == 0) {
                    /* 远端候选已写入 session 的 remote_cands[]，直接启动打洞 */
                    nat_start_punch(s, s->cfg.verbose_nat_punch);
                }
            }
            break;

        case P2P_PKT_PUNCH:
        case P2P_PKT_PUNCH_ACK:
        case P2P_PKT_PING:
        case P2P_PKT_PONG:
            nat_on_packet(s, hdr.type, payload, payload_len, &from);
            break;

        // --------------------
        // 数据传输
        // --------------------

        case P2P_PKT_DATA:
            if (s->trans && s->trans->on_packet) {
                s->trans->on_packet(s, hdr.type, payload, payload_len, &from);
            } else {
                reliable_on_data(&s->reliable, hdr.seq, payload, payload_len);
            }
            break;

        // ACK（仅 reliable/pseudotcp 使用）
        case P2P_PKT_ACK:
            // DTLS/SCTP 有自己的确认机制，不使用 P2P_PKT_ACK
            if ((!s->trans || !s->trans->on_packet) && payload_len >= 6) {
                uint16_t ack_seq = ((uint16_t)payload[0] << 8) | payload[1];
                uint32_t sack = ((uint32_t)payload[2] << 24) |
                                ((uint32_t)payload[3] << 16) |
                                ((uint32_t)payload[4] << 8) |
                                 (uint32_t)payload[5];
                reliable_on_ack(&s->reliable, ack_seq, sack);
            }
            break;

        // --------------------
        // 中继数据（来自服务器）
        // --------------------

        case P2P_PKT_RELAY_DATA:

            // 作为 DATA 处理，但来自服务器
            if (payload_len > 0)
                reliable_on_data(&s->reliable, hdr.seq, payload, payload_len);
            break;

        // --------------------
        // FIN 连接断开
        // --------------------

        case P2P_PKT_FIN:
            s->state = P2P_STATE_CLOSED;
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
        // 连接授权验证
        // --------------------

        case P2P_PKT_AUTH:
        
            // 简化版安全握手：检查 payload 是否匹配 cfg.auth_key
            if (s->cfg.auth_key && payload_len > 0) {
                if (strncmp((const char*)payload, s->cfg.auth_key, payload_len) == 0) {
                    printf("[AUTH] Authenticated successfully!\n");
                } else {
                    printf("[AUTH] Authentication failed!\n");
                    s->state = P2P_STATE_ERROR;
                }
            }
            break;

        default:
            break;
        }
    }

    // --------------------
    // 周期维护 NAT 打洞
    // --------------------

    if (s->state == P2P_STATE_REGISTERING && s->nat.state == NAT_PUNCHING) {
        s->state = P2P_STATE_PUNCHING;
    }

    if ((s->state == P2P_STATE_PUNCHING || s->state == P2P_STATE_REGISTERING) &&
        s->nat.state == NAT_CONNECTED) {
        s->state = P2P_STATE_CONNECTED;
        s->path = P2P_PATH_PUNCH;
        s->active_addr = s->nat.peer_addr;
        
        // 触发连接建立回调
        if (s->cfg.on_connected) {
            s->cfg.on_connected(s, s->cfg.userdata);
        }

        // 查找远端 Host 候选（用于同子网检测）
        struct sockaddr_in *peer_priv = NULL;
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            if (s->remote_cands[i].type == P2P_CAND_HOST) {
                peer_priv = &s->remote_cands[i].addr;
                break;
            }
        }

        // 如果对方内网 IP 和自己属于同一个子网段内（且未禁用优化）
        if (!s->cfg.disable_lan_shortcut && peer_priv &&
            route_check_same_subnet(&s->route, peer_priv)) {
            struct sockaddr_in priv = *peer_priv;
            priv.sin_port = s->nat.peer_addr.sin_port;
            route_send_probe(&s->route, s->sock, &priv, 0);
            if (s->cfg.verbose_nat_punch) {
                printf("[NAT_PUNCH] Same subnet detected, sent ROUTE_PROBE to %s:%d\n",
                       inet_ntoa(priv.sin_addr), ntohs(priv.sin_port));
                fflush(stdout);
            }
        } else if (s->cfg.disable_lan_shortcut && peer_priv &&
                   route_check_same_subnet(&s->route, peer_priv)) {
            if (s->cfg.verbose_nat_punch) {
                printf("[NAT_PUNCH] Same subnet detected but LAN shortcut disabled, forcing NAT punch\n");
                fflush(stdout);
            }
        }
    }

    if (s->state == P2P_STATE_PUNCHING &&
        s->nat.state == NAT_RELAY) {
        s->state = P2P_STATE_RELAY;
        s->path = P2P_PATH_RELAY;
        /* RELAY 模式下使用信令服务器地址 */
        if (s->signaling_mode == P2P_SIGNALING_MODE_SIMPLE) {
            s->active_addr = s->sig_simple_ctx.server_addr;
        } else {
            /* ICE 模式暂不支持 RELAY 回退 */
            s->active_addr = s->nat.peer_addr;
        }
    }

    // --------------------
    // 周期维护 ROUTE 状态机
    // --------------------

    // 升级到 LAN 路径（如果确认）
    if (s->route.lan_confirmed && s->path != P2P_PATH_LAN) {
        s->path = P2P_PATH_LAN;
        s->active_addr = s->route.lan_peer_addr;
    }

    // --------------------
    // P2P 已经连接（周期进行数据读写）
    // --------------------

    // 发送数据：数据流层 → 可靠/传输层 flush 写入
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
        
        // 如果启动了传输模块（DTLS、PseudoTCP、...）
        if (s->trans && s->trans != &p2p_trans_reliable
            && s->trans->send_data) {

            // 由传输模块自行处理流的数据
            uint8_t tmp[P2P_MTU];
            int n = ring_read(&s->stream.send_ring, tmp, sizeof(tmp));
            if (n > 0) {
                s->trans->send_data(s, tmp, n);
                s->stream.pending_bytes -= n;
                s->stream.send_offset += n;
            }
        } 
        else stream_flush_to_reliable(&s->stream, &s->reliable);
    }

    // 传输模块周期 tick（重传，拥塞控制等）
    if (s->trans && s->trans->tick) {
        if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
            s->trans->tick(s);
        }
    }

    // 接收数据：可靠/传输层 → 数据流层（仅 reliable/pseudotcp 需要）
    // DTLS/SCTP 直接写入 stream.recv_ring，不需要此步骤
    if (!s->trans || !s->trans->on_packet) {
        stream_feed_from_reliable(&s->stream, &s->reliable);
    }

    // --------------------
    // 周期维护 NAT/信令 状态机
    // --------------------

    if (s->signaling_mode == P2P_SIGNALING_MODE_SIMPLE) {
        /* SIMPLE 模式：信令和打洞分开 tick */
        if (s->sig_simple_ctx.state == SIGNAL_SIMPLE_REGISTERING ||
            s->sig_simple_ctx.state == SIGNAL_SIMPLE_REGISTERED) {
            /* 信令注册/等待阶段 */
            signal_simple_tick(s);
        }
        if (s->nat.state != NAT_IDLE) {
            /* 打洞/已连接阶段 */
            nat_tick(s);
        }
    } else {
        /* ICE/PUBSUB 模式：只调用 nat_tick 处理打洞 */
        nat_tick(s);
    }

    // --------------------
    // 周期维护 STUN 机制状态机（STUN 服务的流程）
    // --------------------

    p2p_stun_detect_tick(s);

    // --------------------
    // 周期维护 ICE 机制状态机
    // --------------------

    if (s->cfg.use_ice) {
        p2p_ice_tick(s);
    }

    // --------------------
    // 检测 NAT 断开连接（仅当使用 NAT 时）
    // --------------------

    if (s->nat.state == NAT_IDLE && s->nat.last_send_time > 0
        && (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY)) {

        s->state = P2P_STATE_ERROR;
    }

    // --------------------
    // 周期维护信令服务状态机（ICE 模式）
    // --------------------

    if (s->signaling_mode == P2P_SIGNALING_MODE_ICE) {
        p2p_signal_relay_tick(&s->sig_relay_ctx, s);
        
        // 定期重发信令（处理对方不在线或新候选收集）
        // 重发条件：
        //   1. 有候选地址 且
        //   2. 未连接成功 且
        //   3. (从未发送 或 超时 或 有新候选 或 有待发送的候选)
        if (s->local_cand_cnt > 0 && 
            s->state == P2P_STATE_REGISTERING &&
            s->remote_peer_id[0] != '\0') {
            
            uint64_t now = time_ms();
            int should_send = 0;
            
            if (!s->signal_sent) {
                // 从未发送过
                should_send = 1;
            } else if (now - s->last_signal_time >= SIGNAL_RESEND_INTERVAL_MS) {
                // 超过重发间隔
                should_send = 1;
            } else if (s->local_cand_cnt > s->last_cand_cnt_sent) {
                // 有新候选收集到（如 STUN 响应）
                should_send = 1;
            } else if (s->cands_pending_send) {
                // 有待发送的候选（之前 TCP 发送失败）
                should_send = 1;
            }
            
            if (should_send) {
                p2p_signaling_payload_t payload = {0};
                strncpy(payload.sender, s->cfg.peer_id, 31);
                strncpy(payload.target, s->remote_peer_id, 31);
                payload.candidate_count = s->local_cand_cnt;
                memcpy(payload.candidates, s->local_cands, 
                       sizeof(p2p_candidate_t) * s->local_cand_cnt);
                
                uint8_t buf[2048];
                int n = p2p_signal_pack(&payload, buf, sizeof(buf));
                if (n > 0) {
                    int ret = p2p_signal_relay_send_connect(&s->sig_relay_ctx, s->remote_peer_id, buf, n);
                    /*
                     * 返回值：
                     *   >0: 成功转发的候选数量
                     *    0: 目标不在线（已存储等待转发）
                     *   <0: 失败（-1=超时, -2=容量不足, -3=服务器错误）
                     */
                    if (ret >= 0) {
                        s->signal_sent = 1;
                        s->last_signal_time = now;
                        s->last_cand_cnt_sent = s->local_cand_cnt;
                        s->cands_pending_send = 0;  /* 清除待发送标志 */
                        printf("[SIGNALING] %s ICE candidates (%d) to %s (stored=%d)\n", 
                               s->last_cand_cnt_sent > 0 ? "Resent" : "Sent",
                               s->local_cand_cnt, s->remote_peer_id, ret);
                    } else {
                        s->cands_pending_send = 1;  /* TCP 发送失败，标记待重发 */
                        printf("[SIGNALING] Failed to send candidates (ret=%d), will retry...\n", ret);
                    }
                }
            }
        }
    }

    // --------------------
    // 周期维护信令服务状态机（PUBSUB 模式）
    // --------------------

    if (s->signaling_mode == P2P_SIGNALING_MODE_PUBSUB) {
        p2p_signal_pubsub_tick(&s->sig_pubsub_ctx, s);
        
        // PUB 角色：等待 STUN 响应后发送 offer（必须包含公网地址）
        // 定期重发直到连接成功
        if (s->sig_pubsub_ctx.role == P2P_SIGNAL_ROLE_PUB && 
            s->local_cand_cnt > 0 &&
            s->state == P2P_STATE_REGISTERING) {
            
            // 检查是否已收集到 Srflx 候选地址（公网反射地址）
            int has_srflx = 0;
            for (int i = 0; i < s->local_cand_cnt; i++) {
                if (s->local_cands[i].type == P2P_CAND_SRFLX) {
                    has_srflx = 1;
                    break;
                }
            }
            
            // 只有在收到 STUN 响应（有 Srflx）后才发布
            if (has_srflx) {
                uint64_t now = time_ms();
                int should_send = 0;
                
                if (!s->signal_sent) {
                    should_send = 1;
                } else if (now - s->last_signal_time >= SIGNAL_RESEND_INTERVAL_MS) {
                    should_send = 1;
                } else if (s->local_cand_cnt > s->last_cand_cnt_sent) {
                    should_send = 1;
                }
                
                if (should_send) {
                    p2p_signaling_payload_t payload = {0};
                    strncpy(payload.sender, s->cfg.peer_id, 31);
                    strncpy(payload.target, s->remote_peer_id, 31);
                    payload.candidate_count = s->local_cand_cnt;
                    memcpy(payload.candidates, s->local_cands, 
                           sizeof(p2p_candidate_t) * s->local_cand_cnt);
                    
                    uint8_t buf[2048];
                    int n = p2p_signal_pack(&payload, buf, sizeof(buf));
                    if (n > 0) {
                        p2p_signal_pubsub_send(&s->sig_pubsub_ctx, s->remote_peer_id, buf, n);
                        s->signal_sent = 1;
                        s->last_signal_time = now;
                        s->last_cand_cnt_sent = s->local_cand_cnt;
                        printf("[SIGNALING] %s offer with %d candidates to %s\n", 
                               s->last_cand_cnt_sent > 0 ? "Resent" : "Published",
                               s->local_cand_cnt, s->remote_peer_id);
                    }
                }
            }
        }
    }

    s->last_update = time_ms();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

int p2p_send(p2p_session_t *s, const void *buf, int len) {

    if (!s || !buf || len <= 0) return -1;
    if (s->state != P2P_STATE_CONNECTED && s->state != P2P_STATE_RELAY)
        return -1;

    LOCK(s);
    int ret = stream_write(&s->stream, buf, len);
    UNLOCK(s);
    return ret;
}

int p2p_recv(p2p_session_t *s, void *buf, int len) {
    if (!s || !buf || len <= 0) return -1;

    LOCK(s);
    int ret = stream_read(&s->stream, buf, len);
    UNLOCK(s);
    return ret;
}

int p2p_state(const p2p_session_t *s) {
    return s ? s->state : P2P_STATE_ERROR;
}

int p2p_path(const p2p_session_t *s) {
    return s ? s->path : P2P_PATH_NONE;
}

int p2p_is_ready(p2p_session_t *s) {
    if (!s) return 0;
    if (s->state != P2P_STATE_CONNECTED && s->state != P2P_STATE_RELAY) return 0;
    if (s->trans && s->trans->is_ready) {
        return s->trans->is_ready(s);
    }
    return 1; // 默认准备好，如果不需要传输特定检查
}
