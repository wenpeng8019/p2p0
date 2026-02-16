/*
 * ICE 协议实现（RFC 5245 / RFC 8445）
 *
 * ============================================================================
 * 模块功能
 * ============================================================================
 *
 * 实现 ICE (Interactive Connectivity Establishment) 协议的核心流程：
 *
 *   1. 候选地址收集（Gathering）
 *      - Host Candidate: 本地网卡地址
 *      - Server Reflexive: STUN 反射地址
 *      - Relay: TURN 中继地址
 *
 *   2. 候选交换（Trickle ICE）
 *      - 增量发送/接收候选地址
 *      - 无需等待全部收集完成
 *
 *   3. 连通性检查（Connectivity Check）
 *      - 生成候选对检查列表
 *      - 按优先级顺序发送探测包
 *      - 选择首个成功的路径
 *
 *   4. 提名（Nomination）
 *      - 连通性检查成功后提名该路径
 *      - 切换到 COMPLETED 状态
 */

#include "p2p_internal.h"
#include "p2p_signal_protocol.h"
#include "p2p_udp.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>

/* ============================================================================
 * 优先级计算（RFC 5245 Section 4.1.2）
 * ============================================================================
 *
 * 候选优先级公式：
 *   priority = (2^24) * type_preference +
 *              (2^8)  * local_preference +
 *              (2^0)  * (256 - component_id)
 *
 * 类型偏好值（RFC 5245 Section 4.1.2.2）：
 *   - Host:  126 （本地直连，最优先）
 *   - Prflx: 110 （对端反射，次优先）
 *   - Srflx: 100 （服务器反射，中等）
 *   - Relay:   0 （中继，最后选择）
 *
 * 本地偏好值用于区分同类型的多个候选（如多网卡）：
 *   - 通常设为 65535 减去网卡索引
 *   - 或基于网卡速度/稳定性排序
 *
 * 组件 ID：
 *   - RTP = 1
 *   - RTCP = 2（如果使用 RTCP-mux 则只有 RTP）
 */

/* 类型偏好值常量 */
#define ICE_TYPE_PREF_HOST   126
#define ICE_TYPE_PREF_PRFLX  110
#define ICE_TYPE_PREF_SRFLX  100
#define ICE_TYPE_PREF_RELAY    0

/*
 * 计算候选优先级
 *
 * @param type        候选类型
 * @param local_pref  本地偏好值（0-65535）
 * @param component   组件 ID（通常为 1）
 * @return            32 位优先级值
 */
uint32_t p2p_ice_calc_priority(p2p_cand_type_t type, uint16_t local_pref, uint8_t component) {
    uint32_t type_pref;
    
    /* 根据候选类型确定类型偏好值 */
    switch (type) {
        case P2P_CAND_HOST:
            type_pref = ICE_TYPE_PREF_HOST;
            break;
        case P2P_CAND_PRFLX:
            type_pref = ICE_TYPE_PREF_PRFLX;
            break;
        case P2P_CAND_SRFLX:
            type_pref = ICE_TYPE_PREF_SRFLX;
            break;
        case P2P_CAND_RELAY:
            type_pref = ICE_TYPE_PREF_RELAY;
            break;
        default:
            type_pref = 0;
            break;
    }
    
    /*
     * 优先级计算：
     *   priority = (2^24) * type_pref + (2^8) * local_pref + (256 - component)
     *
     * 示例（Host, local_pref=65535, component=1）：
     *   = 16777216 * 126 + 256 * 65535 + 255
     *   = 2113929216 + 16776960 + 255
     *   = 2130706431
     */
    return ((uint32_t)type_pref << 24) + 
           ((uint32_t)local_pref << 8) + 
           (256 - component);
}

/* ============================================================================
 * 候选对优先级计算（RFC 5245 Section 5.7.2）
 * ============================================================================
 *
 * 候选对优先级用于确定连通性检查的顺序。
 *
 * 公式：
 *   pair_priority = 2^32 * MIN(G, D) + 2 * MAX(G, D) + (G > D ? 1 : 0)
 *
 * 其中：
 *   - G = controlling 端候选的优先级
 *   - D = controlled 端候选的优先级
 *
 * 这个公式确保：
 *   1. 高优先级候选对先被检查
 *   2. controlling 端优先时有轻微优势（+1）
 */

/*
 * 计算候选对优先级
 *
 * @param controlling_prio  controlling 端候选优先级
 * @param controlled_prio   controlled 端候选优先级
 * @param is_controlling    本端是否为 controlling 角色
 * @return                  64 位候选对优先级
 */
uint64_t p2p_ice_calc_pair_priority(uint32_t controlling_prio, uint32_t controlled_prio, int is_controlling) {
    uint64_t g, d;              /* g = controlling, d = controlled */
    uint64_t min_val, max_val;
    
    if (is_controlling) {
        /* 本端是 controlling，本地候选是 G */
        g = controlling_prio;
        d = controlled_prio;
    } else {
        /* 本端是 controlled，远端候选是 G */
        g = controlled_prio;
        d = controlling_prio;
    }
    
    /* 计算 MIN 和 MAX */
    min_val = (g < d) ? g : d;
    max_val = (g > d) ? g : d;
    
    /*
     * pair_priority = 2^32 * MIN(G, D) + 2 * MAX(G, D) + (G > D ? 1 : 0)
     *
     * 使用 64 位整数避免溢出：
     *   2^32 * 2130706431 (max priority) 需要 63 位
     */
    return (min_val << 32) + (max_val << 1) + (g > d ? 1 : 0);
}

/* ============================================================================
 * 检查列表生成与排序（RFC 5245 Section 5.7）
 * ============================================================================
 *
 * 检查列表（Check List）是所有候选对的有序列表，按优先级降序排列。
 *
 * 生成步骤：
 *   1. 将每个本地候选与每个远端候选配对
 *   2. 计算每个候选对的优先级
 *   3. 按优先级降序排序
 *   4. 剪枝（去除冗余候选对）
 *
 * 初始状态：
 *   - 第一个候选对设为 Waiting
 *   - 其余候选对设为 Frozen（等待前一个完成）
 */

/*
 * pair_compare: 候选对比较函数（用于 qsort）
 *
 * 按优先级降序排列（优先级高的在前）
 */
static int pair_compare(const void *a, const void *b) {
    const p2p_candidate_pair_t *pa = (const p2p_candidate_pair_t *)a;
    const p2p_candidate_pair_t *pb = (const p2p_candidate_pair_t *)b;
    
    /* 降序排列：优先级高的在前 */
    if (pa->pair_priority > pb->pair_priority) return -1;
    if (pa->pair_priority < pb->pair_priority) return 1;
    return 0;
}

/*
 * 生成候选对检查列表
 *
 * 将本地候选和远端候选组合成候选对，计算优先级并排序。
 *
 * @param pairs          输出：候选对数组
 * @param max_pairs      数组最大容量
 * @param local_cands    本地候选数组
 * @param local_cnt      本地候选数量
 * @param remote_cands   远端候选数组
 * @param remote_cnt     远端候选数量
 * @param is_controlling 本端是否为 controlling 角色
 * @return               生成的候选对数量
 */
int p2p_ice_form_check_list(
    p2p_candidate_pair_t *pairs, int max_pairs,
    const p2p_candidate_t *local_cands, int local_cnt,
    const p2p_candidate_t *remote_cands, int remote_cnt,
    int is_controlling
) {
    int pair_cnt = 0;
    
    /* 遍历所有本地和远端候选的组合 */
    for (int i = 0; i < local_cnt && pair_cnt < max_pairs; i++) {
        for (int j = 0; j < remote_cnt && pair_cnt < max_pairs; j++) {
            p2p_candidate_pair_t *p = &pairs[pair_cnt];
            
            /* 复制候选信息 */
            p->local = local_cands[i];
            p->remote = remote_cands[j];
            
            /* 计算候选对优先级 */
            if (is_controlling) {
                /* 本端是 controlling：本地候选是 G，远端候选是 D */
                p->pair_priority = p2p_ice_calc_pair_priority(
                    local_cands[i].priority,
                    remote_cands[j].priority,
                    1
                );
            } else {
                /* 本端是 controlled：远端候选是 G，本地候选是 D */
                p->pair_priority = p2p_ice_calc_pair_priority(
                    remote_cands[j].priority,
                    local_cands[i].priority,
                    0
                );
            }
            
            /* 初始状态设为 Frozen */
            p->state = P2P_PAIR_FROZEN;
            p->nominated = 0;
            p->last_check_time = 0;
            p->check_count = 0;
            
            pair_cnt++;
        }
    }
    
    /* 按优先级降序排序 */
    if (pair_cnt > 1) {
        qsort(pairs, pair_cnt, sizeof(p2p_candidate_pair_t), pair_compare);
    }
    
    /* 第一个候选对设为 Waiting（可以开始检查） */
    if (pair_cnt > 0) {
        pairs[0].state = P2P_PAIR_WAITING;
    }
    
    printf("[ICE] Formed check list with %d candidate pairs:\n", pair_cnt);
    for (int i = 0; i < pair_cnt && i < 5; i++) {  /* 只打印前 5 个 */
        printf("  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx\n",
               i,
               inet_ntoa(pairs[i].local.addr.sin_addr),
               ntohs(pairs[i].local.addr.sin_port),
               inet_ntoa(pairs[i].remote.addr.sin_addr),
               ntohs(pairs[i].remote.addr.sin_port),
               (unsigned long long)pairs[i].pair_priority);
    }
    if (pair_cnt > 5) {
        printf("  ... and %d more pairs\n", pair_cnt - 5);
    }
    
    return pair_cnt;
}

/* ============================================================================
 * ICE 状态机处理
 * ============================================================================ */

/*
 * ICE 状态机周期处理
 *
 * 在 GATHERING 或 CHECKING 状态下，定期向所有远端候选发送连通性检查包。
 *
 * 检查间隔：500ms（RFC 5245 建议 Ta = 20ms * N，这里简化为固定值）
 */
void p2p_ice_tick(p2p_session_t *s) {
    if (s->ice_state == P2P_ICE_STATE_IDLE) return;
    
    /* 如果已收集候选并有远端候选，开始连通性检查 */
    if (s->ice_state == P2P_ICE_STATE_GATHERING || s->ice_state == P2P_ICE_STATE_CHECKING) {
        if (s->remote_cand_cnt > 0 && s->ice_state != P2P_ICE_STATE_COMPLETED) {

            /* 标记状态为 CHECKING */
            s->ice_state = P2P_ICE_STATE_CHECKING;

            /* 定时发送连通性检查（间隔 500ms） */
            static uint64_t last_check = 0;
            uint64_t now = time_ms();
            if (now - last_check >= 500) {

                /* 向所有远端候选发送探测包 */
                for (int i = 0; i < s->remote_cand_cnt; i++) { 
                    p2p_candidate_t *c = &s->remote_cands[i];

                    /* 
                     * 发送连通性检查包
                     * RFC 5245 要求使用 STUN Binding Request
                     * 这里简化为自定义的 P2P_PKT_PUNCH
                     */
                    udp_send_packet(s->sock, &c->addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
                    printf("[ICE] Sending connectivity check to Candidate %d: %s:%d\n", 
                           i, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
                }

                last_check = now;
            }
        }
    }
}

/* ============================================================================
 * Trickle ICE 支持
 * ============================================================================ */

/*
 * 发送单个本地候选（Trickle ICE）
 *
 * Trickle ICE（RFC 8838）允许增量发送候选地址，无需等待全部收集完成。
 * 这可以减少连接建立延迟。
 *
 * 候选载荷格式：
 *   [ type: 1B | ip: 4B | port: 2B ]
 */
void p2p_ice_send_local_candidate(p2p_session_t *s, p2p_candidate_t *c) {

    uint8_t payload[16];
    int offset = 0;
    
    /* 编码候选类型 */
    payload[offset++] = (uint8_t)c->type;
    
    /* 编码 IP 和端口（网络字节序） */
    memcpy(payload + offset, &c->addr.sin_addr.s_addr, 4);
    memcpy(payload + offset + 4, &c->addr.sin_port, 2);
    offset += 6;

    /* 发送到信令服务器 */
    udp_send_packet(s->sock, &s->nat.server_addr, P2P_PKT_ICE_CANDIDATES, 0, 0, payload, offset);
    printf("[ICE] [Trickle] Sent Candidate %d to peer\n", c->type);
}

/* ============================================================================
 * 候选地址收集
 * ============================================================================ */

/*
 * 收集本地候选地址
 *
 * 收集三种类型的候选地址：
 *   1. Host Candidate:   本地网卡地址（通过 getifaddrs()）
 *   2. Srflx Candidate:  STUN 反射地址（通过 STUN Binding Request）
 *   3. Relay Candidate:  TURN 中继地址（通过 TURN Allocate）
 *
 * 收集完成后进入 GATHERING 状态，等待 STUN/TURN 响应后完成。
 */
int p2p_ice_gather_candidates(p2p_session_t *s) {

    s->local_cand_cnt = 0;

    /* ======================== 1. 收集 Host 候选 ======================== */
    /*
     * Host Candidate 是本地网卡的 IP 地址。
     * 在同一局域网内的对端可以直接使用此地址通信。
     */
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {

        int host_index = 0;  /* 用于区分多个 Host 候选的本地偏好值 */
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            /* 只处理 IPv4 地址，跳过回环接口 */
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            if (s->local_cand_cnt < P2P_MAX_CANDIDATES) {
                p2p_candidate_t *c = &s->local_cands[s->local_cand_cnt++];

                c->type = P2P_CAND_HOST;
                
                /* 
                 * 使用 RFC 5245 优先级计算公式
                 * local_pref 递减以区分多个网卡（第一个网卡优先级最高）
                 */
                uint16_t local_pref = 65535 - host_index;
                c->priority = p2p_ice_calc_priority(P2P_CAND_HOST, local_pref, 1);
                host_index++;

                memcpy(&c->addr, ifa->ifa_addr, sizeof(struct sockaddr_in));

                /* 使用 socket 绑定的端口 */
                struct sockaddr_in loc;
                socklen_t loclen = sizeof(loc);
                getsockname(s->sock, (struct sockaddr *)&loc, &loclen);
                c->addr.sin_port = loc.sin_port;
                
                printf("[ICE] Gathered Host Candidate: %s:%d (priority=0x%08x)\n", 
                       inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), c->priority);
                
                /* 候选已添加到 local_cands，将在 p2p_update() 中定期批量发送 */
                printf("[ICE] [Trickle] Candidate %d queued for batch sending\n", s->local_cand_cnt);
            }
        }
        freeifaddrs(ifaddr);
    }

    /* ======================== 2. 收集 Srflx 候选 ======================== */
    /*
     * Server Reflexive Candidate 是通过 STUN 服务器发现的公网地址。
     * 用于穿透 NAT，让位于不同 NAT 后的对端能够通信。
     */
    if (s->cfg.stun_server) {

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
                udp_send_to(s->sock, &stun_addr, stun_buf, slen);
                printf("[ICE] Requested Srflx Candidate from %s\n", s->cfg.stun_server);
            }
        }
    }

    /* ======================== 3. 收集 Relay 候选 ======================== */
    /*
     * Relay Candidate 是通过 TURN 服务器分配的中继地址。
     * 当直连和 STUN 穿透都失败时，使用中继作为最后的备选。
     */
    if (s->cfg.turn_server) {
        if (p2p_turn_allocate(s) == 0) {
            printf("[ICE] Requested Relay Candidate from %s\n", s->cfg.turn_server);
        }
    }

    /* ======================== 4. TCP 候选（可选） ======================== */
    /*
     * RFC 6544 扩展了 ICE 以支持 TCP 候选。
     * 目前仅预留接口，未完全实现。
     */
    if (s->cfg.enable_tcp) {
        for (int i = 0; i < s->local_cand_cnt; i++) {
            if (s->local_cands[i].type == P2P_CAND_HOST && s->local_cand_cnt < P2P_MAX_CANDIDATES) {
                /* TODO: 建立 TCP 监听端口 */
            }
        }
    }

    /* 进入 GATHERING 状态 */
    s->ice_state = P2P_ICE_STATE_GATHERING;

    return 0;
}

/* ============================================================================
 * 远端候选处理
 * ============================================================================ */

/*
 * 处理信令载荷中的候选地址
 *
 * 解析对端通过信令发送的候选地址列表，添加到远端候选集合中。
 * 重复的候选会被过滤。
 */
void p2p_ice_handle_signaling_payload(p2p_session_t *s, const p2p_signaling_payload_t *p) {

    printf("[ICE] Handling signaling payload from '%s' (%d candidates)\n", 
           p->sender, p->candidate_count);
    
    for (int i = 0; i < p->candidate_count; i++) {
        const p2p_candidate_t *caddr = &p->candidates[i];
        
        /* 排重检查：避免添加重复候选 */
        int exists = 0;
        for (int j = 0; j < s->remote_cand_cnt; j++) {
            if (s->remote_cands[j].addr.sin_addr.s_addr == caddr->addr.sin_addr.s_addr &&
                s->remote_cands[j].addr.sin_port == caddr->addr.sin_port) {
                exists = 1;
                break;
            }
        }

        if (!exists && s->remote_cand_cnt < P2P_MAX_CANDIDATES) {
            s->remote_cands[s->remote_cand_cnt++] = *caddr;
            printf("[ICE] Added Remote Candidate: %d -> %s:%d\n", 
                   caddr->type, inet_ntoa(caddr->addr.sin_addr), ntohs(caddr->addr.sin_port));
        }
    }
}

/*
 * 处理 Trickle ICE 候选
 *
 * 接收对端增量发送的候选地址。
 *
 * 载荷格式：[ type: 1B | ip: 4B | port: 2B ] × N
 */
void p2p_ice_on_remote_candidates(p2p_session_t *s, const uint8_t *payload, int len) {
    int offset = 0;

    /* 循环解析所有候选 */
    while (offset + 7 <= len && s->remote_cand_cnt < P2P_MAX_CANDIDATES) {
        p2p_cand_type_t ctype = (p2p_cand_type_t)payload[offset++];

        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sin_family = AF_INET;
        memcpy(&caddr.sin_addr.s_addr, payload + offset, 4);
        memcpy(&caddr.sin_port, payload + offset + 4, 2);
        offset += 6;

        /* 排重检测 */
        int exists = 0;
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            if (s->remote_cands[i].addr.sin_addr.s_addr == caddr.sin_addr.s_addr &&
                s->remote_cands[i].addr.sin_port == caddr.sin_port) {
                exists = 1;
                break;
            }
        }

        if (!exists && s->remote_cand_cnt < P2P_MAX_CANDIDATES) {
            p2p_candidate_t *c = &s->remote_cands[s->remote_cand_cnt++];
            c->type = ctype;
            c->addr = caddr;
            printf("[ICE] Received New Remote Candidate: %d -> %s:%d\n", 
                   c->type, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
        }
    }
}

/* ============================================================================
 * 连通性检查成功处理
 * ============================================================================ */

/*
 * 连通性检查成功回调
 *
 * 当收到对端的响应时调用此函数。
 * 成功后：
 *   1. 设置活动地址（用于后续数据传输）
 *   2. 状态转为 COMPLETED
 *   3. 触发连接成功回调
 *   4. 如果是被动方，发送 answer
 *   5. 如需认证，发送 AUTH 包
 */
void p2p_ice_on_check_success(p2p_session_t *s, const struct sockaddr_in *from) {
    if (s->ice_state == P2P_ICE_STATE_COMPLETED) return;

    /* 查找对应的远端候选 */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (s->remote_cands[i].addr.sin_addr.s_addr == from->sin_addr.s_addr &&
            s->remote_cands[i].addr.sin_port == from->sin_port) {

            printf("[ICE] Nomination successful! Using path %s:%d\n", 
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            
            /* 设置活动地址 */
            s->active_addr = *from;

            /* 更新状态 */
            s->ice_state = P2P_ICE_STATE_COMPLETED;
            s->state = P2P_STATE_CONNECTED;
            s->path = P2P_PATH_PUNCH; 

            /* 触发连接建立回调 */
            if (s->cfg.on_connected) {
                s->cfg.on_connected(s, s->cfg.userdata);
            }

            /* 被动方：发送 answer 给主动方 */
            if (s->sig_relay_ctx.incoming_peer_name[0] != '\0') {
                p2p_signaling_payload_t answer;
                memset(&answer, 0, sizeof(answer));
                strncpy(answer.sender, s->sig_relay_ctx.my_name, P2P_MAX_NAME);
                answer.candidate_count = s->local_cand_cnt < P2P_MAX_CANDIDATES ? s->local_cand_cnt : P2P_MAX_CANDIDATES;
                for (int i = 0; i < answer.candidate_count; i++) {
                    answer.candidates[i] = s->local_cands[i];
                }
                
                uint8_t answer_buf[512];
                int answer_len = p2p_signal_pack(&answer, answer_buf, sizeof(answer_buf));
                if (answer_len > 0) {
                    p2p_signal_relay_reply_connect(&s->sig_relay_ctx, s->sig_relay_ctx.incoming_peer_name, answer_buf, answer_len);
                    printf("[ICE] Sent answer to '%s'\n", s->sig_relay_ctx.incoming_peer_name);
                }
            }

            /* 认证握手 */
            if (s->cfg.auth_key) {
                udp_send_packet(s->sock, from, P2P_PKT_AUTH, 0, 0, s->cfg.auth_key, strlen(s->cfg.auth_key));
                printf("[AUTH] Sent authentication request to peer\n");
            }

            break;
        }
    }
}
