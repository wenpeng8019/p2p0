
#include "p2p_internal.h"
#include "p2p_udp.h"
#include "p2p_log.h"
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

p2p_session_t* p2p_create(const p2p_config_t *cfg) {
    if (!cfg) return NULL;

    if (cfg->signaling_mode == P2P_SIGNALING_MODE_PUBSUB) {

        if (!cfg->gh_token || !cfg->gist_id) {
            P2P_LOG_ERROR("P2P", "PUBSUB mode requires gh_token and gist_id configuration");
            return NULL;
        }
    }
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT || cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) {

        if (!cfg->server_host) {
            P2P_LOG_ERROR("P2P", "RELAY mode requires server_host configuration");
            return NULL;
        }
    }
    else {
        P2P_LOG_ERROR("P2P", "Invalid signaling mode specified in configuration");
        return NULL;
    }


    if (!cfg->bind_port) {
        P2P_LOG_ERROR("P2P", "bind_port must be specified in configuration");
        return NULL;
    }

    // 创建 UDP 套接字
    int sock = udp_create_socket(cfg->bind_port);
    if (sock < 0) {
        P2P_LOG_ERROR("P2P", "Failed to create UDP socket on port %d", cfg->bind_port);
        return NULL;
    }

    p2p_session_t *s = (p2p_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    // 初始化信令服务模式
    s->signaling_mode = cfg->signaling_mode;
    s->signal_sent = false;
    if (cfg->signaling_mode == P2P_SIGNALING_MODE_COMPACT) p2p_signal_compact_init(&s->sig_compact_ctx);
    else if (cfg->signaling_mode == P2P_SIGNALING_MODE_RELAY) p2p_signal_relay_init(&s->sig_relay_ctx);
    else if (p2p_signal_pubsub_init(&s->sig_pubsub_ctx, cfg->gh_token, cfg->gist_id) != 0) {
        free(s); close(sock);
        return NULL;
    }

    s->cfg = *cfg;
    if (s->cfg.update_interval_ms <= 0) s->cfg.update_interval_ms = 10;
    s->state = P2P_STATE_IDLE;
    s->path = P2P_PATH_NONE;

    // 初始化链路层（基于 NAT 穿透的 P2P）
    nat_init(&s->nat);
    
    // 初始化路由层（检测是否处于同一子网）
    route_init(&s->route);

    // 初始化基础传输层（reliable ARQ）
    reliable_init(&s->reliable);

    // 初始化基础传输层（reliable）
    reliable_init(&s->reliable);

    // 初始化传输层（可选的高级传输模块）
    // 注：reliable 是基础传输层，始终存在于 s->reliable
    //     这里的 s->trans 只用于高级传输模块（DTLS/OpenSSL/SCTP/PseudoTCP）
    s->trans = NULL;  // 默认无高级传输层
    
    if (cfg->use_dtls) {
#ifdef WITH_DTLS
        s->trans = &p2p_trans_dtls;
#else
        P2P_LOG_WARN("p2p", "DTLS (MbedTLS) requested but library not linked!\n");
#endif
    } 
    else if (cfg->use_openssl) {
#ifdef WITH_OPENSSL
        s->trans = &p2p_trans_openssl;
#else
        P2P_LOG_WARN("p2p", "OpenSSL requested but library not linked!\n");
#endif
    }
    else if (cfg->use_sctp) {
#ifdef WITH_SCTP
        s->trans = &p2p_trans_sctp;
#else
        P2P_LOG_WARN("p2p", "SCTP (usrsctp) requested but library not linked!\n");
#endif
    }
    else if (cfg->use_pseudotcp) {
        s->trans = &p2p_trans_pseudotcp;
    }

    // 执行传输模块的初始化处理
    if (s->trans && s->trans->init) s->trans->init(s);

    // 初始化数据流层
    stream_init(&s->stream, cfg->nagle);

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

    // RELAY 模式：关闭 TCP 长连接
    if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY) {

        p2p_signal_relay_close(&s->sig_relay_ctx);
    }

    //  COMPACT 模式的 server UDP 套接口和 NAT p2p 是同一个
    if (s->sock >= 0) {

        // 发送 FIN 包，断开连接
        if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY)
            udp_send_packet(s->sock, &s->active_addr, P2P_PKT_FIN, 0, 0, NULL, 0);
        close(s->sock);
    }

    free(s);
}

int p2p_connect(p2p_session_t *s, const char *remote_peer_id) {

    if (!s) return -1;
    if (s->state != P2P_STATE_IDLE) return -1;

    if (remote_peer_id && !*remote_peer_id) remote_peer_id = NULL;

    // COMPACT 模式必须指定 remote_peer_id
    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT && !remote_peer_id) {
        P2P_LOG_ERROR("P2P", "SIMPLE mode requires explicit remote_peer_id\n");
        s->state = P2P_STATE_ERROR;
        return -1;
    }

    LOCK(s);

    // 设置对方 id
    if (remote_peer_id) {
        strncpy(s->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
        s->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    } else {
        s->remote_peer_id[0] = '\0';
    }

    // 初始化相关状态
    s->signal_sent = false;
    s->cands_pending_send = false;

    switch (s->signaling_mode) {

        // 对于 COMPACT 模式
        case P2P_SIGNALING_MODE_COMPACT: {

            assert(s->cfg.server_host);
            struct sockaddr_in server_addr;
            if (resolve_host(s->cfg.server_host, s->cfg.server_port, &server_addr) < 0) {
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }

            s->state = P2P_STATE_REGISTERING;

            // 获取本地绑定端口
            struct sockaddr_in loc; socklen_t len = sizeof(loc);
            getsockname(s->sock, (struct sockaddr *)&loc, &len);

            // 将 route 中的本地地址转换为候选列表
            for (int i = 0; i < s->route.addr_count && s->local_cand_cnt < P2P_MAX_CANDIDATES; i++) {

                s->local_cands[s->local_cand_cnt].type = P2P_CAND_HOST;
                s->local_cands[s->local_cand_cnt].addr = s->route.local_addrs[i];
                s->local_cands[s->local_cand_cnt].addr.sin_port = loc.sin_port;  // 使用实际绑定端口

                P2P_LOG_INFO("P2P", "[COMPACT] Added Host candidate: %s:%d\n",
                       inet_ntoa(s->local_cands[s->local_cand_cnt].addr.sin_addr),
                       ntohs(s->local_cands[s->local_cand_cnt].addr.sin_port));
                s->local_cand_cnt++;
            }

            // 开始信令注册
            p2p_signal_compact_start(s, s->cfg.local_peer_id, remote_peer_id, &server_addr, s->cfg.verbose_nat_punch);

            P2P_LOG_INFO("P2P", "[CONNECT] SIMPLE mode: registering <%s → %s> with %d candidates\n",
                         s->cfg.local_peer_id, remote_peer_id, s->local_cand_cnt);
            break;
        }

        // 对于 RELAY 模式
        case P2P_SIGNALING_MODE_RELAY: {

            assert(s->cfg.server_host);

            // 首次连接：自动登录信令服务器（只执行一次）
            if (s->sig_relay_ctx.state != SIGNAL_CONNECTED
                && p2p_signal_relay_login(&s->sig_relay_ctx, s->cfg.server_host, s->cfg.server_port,
                                          s->cfg.local_peer_id) < 0) {

                P2P_LOG_ERROR("P2P", "Failed to connect to signaling server\n");
                s->state = P2P_STATE_ERROR;
                UNLOCK(s);
                return -1;
            }

            s->state = P2P_STATE_REGISTERING;

            // 收集 ICE 候选者
            if (s->cfg.use_ice) {
                p2p_ice_gather_candidates(s);
            }

            // 如果指定了 remote_peer_id，立即发送初始 offer
            if (remote_peer_id && s->local_cand_cnt > 0) {
                uint8_t buf[2048];
                int n = pack_signaling_payload_hdr(
                    s->cfg.local_peer_id,
                    remote_peer_id,
                    0,  /* timestamp */
                    0,  /* delay_trigger */
                    s->local_cand_cnt,
                    buf
                );
                for (int i = 0; i < s->local_cand_cnt; i++) {
                    n += pack_candidate(&s->local_cands[i], buf + n);
                }
                if (n > 0) {
                    p2p_signal_relay_send_connect(&s->sig_relay_ctx, remote_peer_id, buf, n);
                    s->signal_sent = true;
                    P2P_LOG_INFO("P2P", "[CONNECT] RELAY mode: sent initial offer with %d candidates to %s\n",
                           s->local_cand_cnt, remote_peer_id);
                }
            } else if (!remote_peer_id) {
                // 被动模式：等待任意对等方的 offer
                P2P_LOG_INFO("P2P", "[CONNECT] RELAY mode: waiting for incoming offer from any peer...\n");
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
                P2P_LOG_INFO("P2P", "[CONNECT] PUBSUB mode (PUB): gathering candidates, waiting for STUN response before publishing...\n");
            }
            else {

                // SUB 角色（订阅者）：被动等待连接
                p2p_signal_pubsub_set_role(&s->sig_pubsub_ctx, P2P_SIGNAL_ROLE_SUB);

                // SUB 模式：被动等待 offer，收到后自动回复
                P2P_LOG_INFO("P2P", "[CONNECT] PUBSUB mode (SUB): waiting for offer from any peer...\n");
            }
            break;
        }

        default:
            P2P_LOG_ERROR("P2P", "Unknown signaling mode: %d\n", s->signaling_mode);
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

    // 发送 FIN 包通知对端断开连接（仅在已连接状态）
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
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

///////////////////////////////////////////////////////////////////////////////

/*
 * 主更新循环 — 驱动所有状态机。
 * > 在单线程模式下，应用程序调用此函数
 * > 在多线程模式下，内部线程在锁下调用此函数
 */
int p2p_update(p2p_session_t *s) {
    if (!s) return -1;

    uint8_t buf[P2P_MTU + 16]; struct sockaddr_in from; int n;

    // 接收并分发数据包
    while ((n = udp_recv_from(s->sock, &from, buf, sizeof(buf))) > 0) {

        // --------------------
        // STUN/TURN 协议包
        // --------------------
        // 注：TURN 是 STUN 的扩展，共享相同的包格式和 Magic Cookie (0x2112A442)
        //     两个 handler 内部会根据消息类型（Method）分别过滤处理

        if (n >= 20 && buf[0] < 2) { // STUN type 0x00xx or 0x01xx
            uint32_t magic = ntohl(*(uint32_t *)(buf + 4));
            if (magic == STUN_MAGIC) {
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
            // 打洞/保活
            // --------------------

            case P2P_PKT_PUNCH:
            case P2P_PKT_PUNCH_ACK:
            case P2P_PKT_PING:
            case P2P_PKT_PONG:
                nat_on_packet(s, hdr.type, payload, payload_len, &from);
                break;

            // --------------------
            // 数据传输（P2P 直连 或 服务器中继）
            // --------------------

            case P2P_PKT_DATA: case P2P_PKT_RELAY_DATA:

                // 高级传输层（DTLS/SCTP/PseudoTCP）有自己的解包逻辑
                if (s->trans && s->trans->on_packet)
                    s->trans->on_packet(s, hdr.type, payload, payload_len, &from);
                // 基础 reliable 层处理
                else if (payload_len > 0)
                    reliable_on_data(&s->reliable, hdr.seq, payload, payload_len);
                
                break;

            // ACK 仅基础 reliable 层使用
            // + 注：DTLS/SCTP 有自己的确认机制，不使用 P2P_PKT_ACK
            case P2P_PKT_ACK: case P2P_PKT_RELAY_ACK:

                // 只有使用基础 reliable 层时才处理（无高级传输层或高级传输层无 on_packet）
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
                        P2P_LOG_INFO("P2P", "[AUTH] Authenticated successfully!\n");
                    } else {
                        P2P_LOG_INFO("P2P", "[AUTH] Authentication failed!\n");
                        s->state = P2P_STATE_ERROR;
                    }
                }
                break;

            // --------------------
            // COMPACT 模式的信令包（REGISTER_ACK、PEER_INFO、PEER_INFO_ACK）
            // --------------------

            case SIG_PKT_REGISTER_ACK:
            case SIG_PKT_PEER_INFO:
            case SIG_PKT_PEER_INFO_ACK:
                if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) break;

                // 处理信令包
                int ret = p2p_signal_compact_on_packet(s, hdr.type, hdr.seq, hdr.flags, payload, payload_len, &from);
                
                // PEER_INFO 特殊处理：seq=1 时启动打洞（首次收到服务器转发的候选）
                if (ret == 0 && hdr.type == SIG_PKT_PEER_INFO && hdr.seq == 1) {
                    nat_start_punch(s, s->cfg.verbose_nat_punch);
                }
                break;

            default:

                P2P_LOG_WARN("P2P", "Received unknown packet type: 0x%02X\n", hdr.type);
                break;
        }

    } // while ((n = udp_recv_from(s->sock, &from, buf, sizeof(buf))) > 0)

    // --------------------
    // 维护连接状态机（未连接到已连接）
    // --------------------

    if (s->state == P2P_STATE_REGISTERING && s->nat.state == NAT_PUNCHING) {
        s->state = P2P_STATE_PUNCHING;
    }

    // 成功 NAT 穿透连接
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
                P2P_LOG_INFO("P2P", "[NAT_PUNCH] Same subnet detected, sent ROUTE_PROBE to %s:%d\n",
                       inet_ntoa(priv.sin_addr), ntohs(priv.sin_port));
                fflush(stdout);
            }
        } else if (s->cfg.disable_lan_shortcut && peer_priv &&
                   route_check_same_subnet(&s->route, peer_priv)) {
            if (s->cfg.verbose_nat_punch) {
                P2P_LOG_INFO("P2P", "[NAT_PUNCH] Same subnet detected but LAN shortcut disabled, forcing NAT punch\n");
                fflush(stdout);
            }
        }
    }

    // 降级到服务器中继
    if (s->state == P2P_STATE_PUNCHING &&
        s->nat.state == NAT_RELAY) {

        s->state = P2P_STATE_RELAY;
        s->path = P2P_PATH_RELAY;

        // 中继模式：NAT 打洞失败后的降级方案
        if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {
            // 检查服务器是否支持中继功能
            if (s->sig_compact_ctx.relay_support) {
                // COMPACT 模式：通过 UDP 信令服务器中继数据
                // 服务器会将 P2P_PKT_RELAY_DATA 转发给对端
                s->active_addr = s->sig_compact_ctx.server_addr;
                P2P_LOG_INFO("P2P", "[NAT_PUNCH] Failed, using server relay mode\n");
            } else {
                // 服务器不支持中继，继续尝试打洞
                s->active_addr = s->nat.peer_addr;
                P2P_LOG_WARN("P2P", "[NAT_PUNCH] Failed, server does not support relay. Will keep trying direct connection...\n");
            }
        } 
        else {
            // RELAY/PUBSUB 模式：目前不支持数据中继
            // 保持 peer_addr，继续尝试打洞（NAT_RELAY 状态会周期性重试）
            // TODO: 需要配置 TURN 服务器或扩展 RELAY 服务器的中继功能
            s->active_addr = s->nat.peer_addr;
            P2P_LOG_WARN("P2P", "[NAT_PUNCH] Failed, no TURN server configured. Will keep trying direct connection...\n");
        }
    }

    // --------------------
    // 周期维护 ROUTE 状态机
    // --------------------

    // 升级到 LAN 路径（如果确认同一子网）
    if (s->route.lan_confirmed && s->path != P2P_PATH_LAN) {
        s->path = P2P_PATH_LAN;
        s->active_addr = s->route.lan_peer_addr;
    }

    // --------------------
    // P2P 已经连接（周期进行数据读写）
    // --------------------

    // 发送数据：数据流层 → 传输层 flush 写入
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
        
        // 如果使用高级传输层（DTLS、SCTP、PseudoTCP）
        if (s->trans && s->trans->send_data) {
            // 由高级传输模块自行处理流的数据
            uint8_t tmp[P2P_MTU];
            int n = ring_read(&s->stream.send_ring, tmp, sizeof(tmp));
            if (n > 0) {
                s->trans->send_data(s, tmp, n);
                s->stream.pending_bytes -= n;
                s->stream.send_offset += n;
            }
        } 
        // 使用基础 reliable 层
        else stream_flush_to_reliable(&s->stream, &s->reliable);
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

    // --------------------
    // 周期维护 STUN/ICE 机制状态机
    // --------------------

    p2p_stun_detect_tick(s);

    if (s->cfg.use_ice) {
        p2p_ice_tick(s);
    }

    // --------------------
    // 周期维护 NAT/信令 状态机（按信令模式分组）
    // --------------------

    if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {
        
        // 信令层维护（注册、等待、候选同步）
        if (s->sig_compact_ctx.state == SIGNAL_COMPACT_REGISTERING ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_REGISTERED ||
            s->sig_compact_ctx.state == SIGNAL_COMPACT_READY) {
            p2p_signal_compact_tick(s);
        }
        
        // NAT 层维护（打洞、保活）
        // 注：READY 状态时两者需要同时执行
        if (s->nat.state != NAT_IDLE) {
            nat_tick(s);
        }
    }
    else if (s->signaling_mode == P2P_SIGNALING_MODE_RELAY) {
        
        // NAT 层维护
        nat_tick(s);
        
        // 信令层维护
        p2p_signal_relay_tick(&s->sig_relay_ctx, s);
        
        // 定期重发信令（处理对方不在线或新候选收集）
        // 重发条件：
        //   1. 有候选地址 且
        //   2. 未连接成功 且
        //   3. (从未发送 或 超时 或 有新候选 或 有待发送的候选) 且
        //   4. 不在等待对端上线状态（status=2 后需等待 FORWARD）
        if (s->local_cand_cnt > 0 && 
            s->state == P2P_STATE_REGISTERING &&
            s->remote_peer_id[0] != '\0') {
            
            uint64_t now = time_ms();
            int unsent_count = s->local_cand_cnt - s->sig_relay_ctx.total_candidates_acked;
            
            // 检查是否需要发送（包括断点续传）
            // 注意：waiting_for_peer=true 时，只有收到 FORWARD 清除该标志后才会继续发送
            if (unsent_count > 0 &&
                !s->sig_relay_ctx.waiting_for_peer &&                        // 不在等待对端上线状态
                (!s->signal_sent ||                                          // 从未发送过
                 (now - s->last_signal_time >= SIGNAL_RESEND_INTERVAL_MS) || // 超过重发间隔（Trickle ICE）
                 (s->local_cand_cnt > s->last_cand_cnt_sent) ||              // 有新候选收集到（如 STUN 响应）
                 s->cands_pending_send                                       // 有待发送的候选（之前 TCP 发送失败）
                )) {

                // 从 next_candidate_index 开始发送剩余候选（断点续传）
                int start_idx = s->sig_relay_ctx.next_candidate_index;
                if (start_idx >= s->local_cand_cnt) {
                    start_idx = 0;  // 重置（可能是全新的候选列表）
                }
                
                // Trickle ICE：每次最多发送 8 个候选（协议规定 1-8 个/批次）
                #define MAX_CANDIDATES_PER_BATCH 8
                int remaining = s->local_cand_cnt - start_idx;
                int batch_size = (remaining > MAX_CANDIDATES_PER_BATCH) ? MAX_CANDIDATES_PER_BATCH : remaining;
                
                uint8_t buf[2048];
                int n = pack_signaling_payload_hdr(
                    s->cfg.local_peer_id,
                    s->remote_peer_id,
                    0,  /* timestamp */
                    0,  /* delay_trigger */
                    batch_size,
                    buf
                );
                for (int i = 0; i < batch_size; i++) {
                    n += pack_candidate(&s->local_cands[start_idx + i], buf + n);
                }
                if (n > 0) {
                    int ret = p2p_signal_relay_send_connect(&s->sig_relay_ctx, s->remote_peer_id, buf, n);
                    /*
                     * 返回值：
                     *   >0: 成功转发的候选数量
                     *    0: 目标不在线（已存储等待转发）
                     *   <0: 失败（-1=超时, -2=容量不足, -3=服务器错误）
                     */
                    if (ret >= 0) {
                        s->signal_sent = true;
                        s->last_signal_time = now;
                        s->last_cand_cnt_sent = s->local_cand_cnt;
                        s->cands_pending_send = false;  /* 清除待发送标志 */
                        
                        if (ret > 0) {
                            P2P_LOG_INFO("P2P", "[SIGNALING] Sent candidates [%d-%d] to %s (forwarded=%d)\n",
                                         start_idx, start_idx + batch_size - 1, s->remote_peer_id, ret);
                        } else {
                            P2P_LOG_INFO("P2P", "[SIGNALING] Sent %d candidates to %s (cached, peer offline)\n",
                                         batch_size, s->remote_peer_id);
                        }
                    } else if (ret == -2) {
                        /* 服务器缓存满（status=2）：停止发送，等待对端上线后收到 FORWARD */
                        /* waiting_for_peer 已在 send_connect() 中设置为 true */
                        P2P_LOG_WARN("P2P", "[SIGNALING] Server storage full (status=2), waiting for peer to come online...\n");
                    } else {
                        s->cands_pending_send = true;  /* TCP 发送失败（-1/-3），标记待重发 */
                        P2P_LOG_WARN("P2P", "[SIGNALING] Failed to send candidates (ret=%d), will retry...\n", ret);
                    }
                }
            }
        }
    }
    else if (s->signaling_mode == P2P_SIGNALING_MODE_PUBSUB) {
        
        // NAT 层维护
        nat_tick(s);
        
        // 信令层维护
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
                if (!s->signal_sent ||
                    (now - s->last_signal_time >= SIGNAL_RESEND_INTERVAL_MS) ||
                    (s->local_cand_cnt > s->last_cand_cnt_sent)
                ) {

                    uint8_t buf[2048];
                    int n = pack_signaling_payload_hdr(
                        s->cfg.local_peer_id,
                        s->remote_peer_id,
                        0,  /* timestamp */
                        0,  /* delay_trigger */
                        s->local_cand_cnt,
                        buf
                    );
                    for (int i = 0; i < s->local_cand_cnt; i++) {
                        n += pack_candidate(&s->local_cands[i], buf + n);
                    }
                    if (n > 0) {
                        p2p_signal_pubsub_send(&s->sig_pubsub_ctx, s->remote_peer_id, buf, n);
                        s->signal_sent = true;
                        s->last_signal_time = now;
                        s->last_cand_cnt_sent = s->local_cand_cnt;
                        P2P_LOG_INFO("P2P", "[SIGNALING] %s offer with %d candidates to %s\n",
                                     s->last_cand_cnt_sent > 0 ? "Resent" : "Published",
                                     s->local_cand_cnt, s->remote_peer_id);
                    }
                }
            }
        }
    }

    // --------------------
    // 检测 NAT 断开连接（仅当使用 NAT 时）
    // --------------------

    if (s->nat.state == NAT_IDLE && s->nat.last_send_time > 0
        && (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY)) {

        s->state = P2P_STATE_ERROR;
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
