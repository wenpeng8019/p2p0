//
// Created by 温朋 on 2026/4/18.
//
#define MOD_TAG "COMPACT"

#include "p2p_compact.h"

ARGS(relay);
ARGS(msg);
ARGS(probe_port);


// COMPACT 模式 SYNC 重传参数
#define SYN0_RETRY_INTERVAL_MS      2000    // 重传间隔（毫秒）
#define SYN0_MAX_RETRY              5       // 最大重传次数

static compact_client_t*            g_clients_by_auth = NULL;

// SYNC(seq=0) 待确认超时队列
static timeout_queue_t              g_sync0_pending_q;

// MSG RPC 待确认超时队列
static timeout_queue_t              g_rpc_pending_q;

static client_ctx_t                 g_ctx;

///////////////////////////////////////////////////////////////////////////////

// 发送 REG_ACK: [hdr(4)][instance_id(4)][auth_key(SIG_AUTH_KEY_PSZ)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 25字节
// auth_key=0 表示服务器拒绝（无可用槽位）
static void compact_send_reg_ack(client_t* client, const struct sockaddr_in *to,
                                 uint64_t auth_key, uint32_t instance_id) {
    const char* PROTO = "REG_ACK";

    uint8_t ack[sizeof(p2p_packet_hdr_t) + SIG_PKT_REG_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_REG_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    if (auth_key != 0) {
        if (ARGS_relay.i64)    hdr->flags |= SIG_REG_FLAG_RELAY;
        if (ARGS_msg.i64)      hdr->flags |= SIG_REG_FLAG_MSG;

        int ofz = sizeof(p2p_packet_hdr_t);
        nwrite_l(ack + ofz, instance_id); ofz += (int)sizeof(instance_id);
        nwrite_ll(ack + ofz, auth_key); ofz += (int)sizeof(auth_key);
        ack[ofz++] = MAX_CANDIDATES;
        memcpy(ack + ofz, &to->sin_addr.s_addr, 4); ofz += 4;
        memcpy(ack + ofz, &to->sin_port, 2); ofz += 2;
        uint16_t probe = htons((uint16_t)ARGS_probe_port.i64);
        memcpy(ack + ofz, &probe, 2); ofz += 2;

        print("V:", LA_F("%s:%d send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%" PRIu64 ", inst_id=%u\n", LA_F152, 152),
              inet_ntoa(to->sin_addr), ntohs(to->sin_port), PROTO, MAX_CANDIDATES,
              ARGS_relay.i64 ? "yes" : "no", ARGS_msg.i64 ? "yes" : "no",
              inet_ntoa(to->sin_addr), ntohs(to->sin_port),
              (int)ARGS_probe_port.i64, auth_key, instance_id);
    } else {
        memset(ack + sizeof(p2p_packet_hdr_t), 0, SIG_PKT_REG_ACK_PSZ);
        print("V:", LA_F("%s:%d send %s: rejected (no slot available)\n", LA_F153, 153), 
              inet_ntoa(to->sin_addr), ntohs(to->sin_port), PROTO);
    }

    udp_send(client->fd, ack, (int)sizeof(ack), to, PROTO);
}

// 发送 SYN0_ACK: [hdr(4)][remote_peer_id(32)][session_id(4)][online(1)]
static void compact_send_syn0_ack(compact_session_t *s, const char *remote_peer_id, uint8_t online) {
    const char* PROTO = "SYN0_ACK";

    uint8_t ack[sizeof(p2p_packet_hdr_t) + SIG_PKT_SYN0_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_SYN0_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    memcpy(ack + ofz, remote_peer_id, P2P_PEER_ID_MAX); ofz += P2P_PEER_ID_MAX;
    nwrite_l(ack + ofz, s->base.session_id); ofz += P2P_SESS_ID_SZ;
    ack[ofz++] = online;

    print("V:", LA_F("%s send %s: ses_id=%u, peer=%s\n", LA_F67, 67),
          CLIENT(s)->local_peer_id, PROTO, s->base.session_id, online ? "online" : "offline");

    udp_send(s->base.client->fd, ack, ofz, &COMPACT_CLIENT(s)->addr, PROTO);
}

// 发送 FIN 通知给 session
static void compact_session_send_fin(compact_session_t *s, const char *reason) {
    const char* PROTO = "FIN";

    compact_client_t *client = COMPACT_CLIENT(s);
    uint8_t pkt[sizeof(p2p_packet_hdr_t) + SIG_PKT_FIN_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_FIN; hdr->flags = 0; hdr->seq = htons(0);

    nwrite_l(pkt + sizeof(p2p_packet_hdr_t), s->base.session_id);

    print("V:", LA_F("%s send %s: reason=%s, ses_id=%u\n", LA_F66, 66),
          client->base.local_peer_id, PROTO, reason, s->base.session_id);


    udp_send(s->base.client->fd, pkt, (int)sizeof(pkt), &client->addr, PROTO);
}

// 发送首次对端候选推送（base_index=0）或地址变更通知（base_index != 0 为循环通知序号）
// base_index=0: SIG_PKT_SYN0，payload: [session_id(4)][0x00(1)][cand_cnt(1)][candidates]
// base_index!=0: SIG_PKT_SYNC（seq=0），payload: [session_id(4)][notify_seq(1)][1][candidate]
static void compact_session_send_syn0(compact_session_t *c, uint8_t base_index) {
    const char* PROTO = base_index == 0 ? "SYN0" : "SYNC";

    assert(c && PEER_ONLINE(c));

    compact_client_t *client    = COMPACT_CLIENT(c);
    compact_session_t *peer     = (compact_session_t*)c->base.peer;
    compact_client_t  *peer_cli = COMPACT_CLIENT(peer);

    uint8_t pkt[sizeof(p2p_packet_hdr_t) + P2P_PEER_ID_MAX + P2P_SESS_ID_SZ + 2 + MAX_CANDIDATES * sizeof(p2p_candidate_t)];
    p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)pkt;
    resp_hdr->flags = 0;
    resp_hdr->seq = htons(0);

    int ofz = sizeof(p2p_packet_hdr_t);
    memcpy(pkt + ofz, peer_cli->base.local_peer_id, P2P_PEER_ID_MAX); ofz += P2P_PEER_ID_MAX;
    nwrite_l(pkt + ofz, c->base.session_id); ofz += P2P_SESS_ID_SZ;

    int cand_cnt;

    if (base_index == 0) {

        resp_hdr->type = SIG_PKT_SYN0;

        cand_cnt = 1 + peer->candidate_count;
        pkt[ofz++] = 0;
        pkt[ofz++] = (uint8_t)cand_cnt;

        // 第一个候选：对端的公网地址
        p2p_candidate_t wire_cand;
        wire_cand.type = 1; // srflx
        sockaddr_to_p2p_wire(&peer_cli->addr, &wire_cand.addr);
        wire_cand.priority = 0;
        memcpy(pkt + ofz, &wire_cand, sizeof(p2p_candidate_t));
        ofz += sizeof(p2p_candidate_t);

        for (int i = 0; i < peer->candidate_count; i++) {
            memcpy(pkt + ofz, &peer->candidates[i], sizeof(p2p_candidate_t));
            ofz += sizeof(p2p_candidate_t);
        }

        print("V:", LA_F("%s send %s: cands=%d, ses_id=%u\n", LA_F65, 65),
              client->base.local_peer_id, PROTO, cand_cnt, c->base.session_id);
    }
    else {

        resp_hdr->type = SIG_PKT_SYNC;

        cand_cnt = 1;
        pkt[ofz++] = base_index;
        pkt[ofz++] = 1;

        p2p_candidate_t wire_cand2;
        wire_cand2.type = 1; // srflx
        sockaddr_to_p2p_wire(&peer_cli->addr, &wire_cand2.addr);
        wire_cand2.priority = 0;
        memcpy(pkt + ofz, &wire_cand2, sizeof(p2p_candidate_t));
        ofz += sizeof(p2p_candidate_t);

        print("V:", LA_F("%s send %s: base_index=%u, cands=%d, ses_id=%u\n", LA_F64, 64),
              client->base.local_peer_id, PROTO, base_index, cand_cnt, c->base.session_id);
    }

    udp_send(client->base.fd, pkt, ofz, &client->addr, PROTO);
}

// 发送 REQ_ACK
static void compact_session_send_req_ack(compact_session_t *sender, uint16_t sid, uint8_t status) {
    const char* PROTO = "REQ_ACK";

    uint8_t ack[sizeof(p2p_packet_hdr_t) + SIG_PKT_REQ_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)ack;
    hdr->type = SIG_PKT_REQ_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(ack + ofz, sender->base.session_id); ofz += P2P_SESS_ID_SZ;
    nwrite_s(ack + ofz, sid); ofz += 2;
    ack[ofz++] = status;

    print("V:", LA_F("%s send %s: ses_id=%u, sid=%u, status=%u\n", LA_F70, 70),
          CLIENT(sender)->local_peer_id, PROTO, sender->base.session_id, sid, status);

    udp_send(sender->base.client->fd, ack, ofz, &COMPACT_CLIENT(sender)->addr, PROTO);
}

// 发送 REQ 给对端（Server→对端 relay）
static void compact_session_send_req_to_peer(compact_session_t *sender) {
    const char* PROTO = "REQ";

    assert(sender && PEER_ONLINE(sender));
    assert(!sender->rpc_responding);

    compact_session_t *peer   = (compact_session_t*)sender->base.peer;
    compact_client_t  *peer_c = COMPACT_CLIENT(peer);

    uint8_t pkt[sizeof(p2p_packet_hdr_t) + P2P_SESS_ID_SZ + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_REQ;
    hdr->flags = SIG_FLAG_RELAY;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, peer->base.session_id); ofz += P2P_SESS_ID_SZ;
    nwrite_s(pkt + ofz, sender->rpc_last_sid); ofz += 2;
    pkt[ofz++] = sender->rpc_code;
    if (sender->rpc_data_len > 0) {
        memcpy(pkt + ofz, sender->rpc_data, sender->rpc_data_len);
        ofz += sender->rpc_data_len;
    }

    print("V:", LA_F("%s send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, retries=%d\n", LA_F69, 69),
          peer_c->base.local_peer_id, PROTO, peer->base.session_id, sender->rpc_last_sid, sender->rpc_code, sender->rpc_data_len,
          sender->rpc_retry);

    udp_send(peer_c->base.fd, pkt, ofz, &peer_c->addr, PROTO);
}

// 发送 RSP_ACK 给 B 端（Server→B）
static void compact_session_send_resp_ack(compact_session_t *responder, uint16_t sid) {
    const char* PROTO = "RSP_ACK";

    uint8_t pkt[sizeof(p2p_packet_hdr_t) + SIG_PKT_RSP_ACK_PSZ];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_RSP_ACK;
    hdr->flags = 0;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, responder->base.session_id); ofz += P2P_SESS_ID_SZ;
    nwrite_s(pkt + ofz, sid); ofz += 2;

    print("V:", LA_F("%s send %s: ses_id=%u, sid=%u\n", LA_F71, 71),
          COMPACT_PEER(responder)->base.client->local_peer_id, PROTO, responder->base.session_id, sid);

    udp_send(responder->base.client->fd, pkt, ofz, &COMPACT_CLIENT(responder)->addr, PROTO);
}

// 发送 RSP 给请求方（Server→A）
static void compact_send_msg_resp_to_requester(compact_session_t *cs) {
    const char* PROTO = "RSP";

    assert(cs && cs->rpc_responding);

    compact_client_t *client = COMPACT_CLIENT(cs);
    uint8_t pkt[sizeof(p2p_packet_hdr_t) + P2P_SESS_ID_SZ + 2 + 1 + P2P_MSG_DATA_MAX];
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)pkt;
    hdr->type = SIG_PKT_RSP;
    hdr->flags = cs->rpc_flags;
    hdr->seq = 0;

    int ofz = sizeof(p2p_packet_hdr_t);
    nwrite_l(pkt + ofz, cs->base.session_id); ofz += P2P_SESS_ID_SZ;
    nwrite_s(pkt + ofz, cs->rpc_last_sid); ofz += 2;

    if (!(cs->rpc_flags & (SIG_RPC_FLAG_PEER_OFF | SIG_RPC_FLAG_TIMEOUT))) {
        pkt[ofz++] = cs->rpc_code;
        if (cs->rpc_data_len > 0) {
            memcpy(pkt + ofz, cs->rpc_data, cs->rpc_data_len);
            ofz += cs->rpc_data_len;
        }
    }

    print("V:", LA_F("%s send %s: ses_id=%u, sid=%u, flags=0x%02x, code=%u, data_len=%d, retries=%d\n", LA_F68, 68),
          client->base.local_peer_id, PROTO, cs->base.session_id, cs->rpc_last_sid, cs->rpc_flags, cs->rpc_code, cs->rpc_data_len, cs->rpc_retry);

    udp_send(client->base.fd, pkt, ofz, &client->addr, PROTO);
}

// 缓存响应数据并从 REQ 阶段转换到 RSP 阶段
static void compact_transition_to_resp_pending(compact_session_t *requester, uint64_t now,
                                               uint8_t flags, uint8_t code, const uint8_t *data, int len) {

    requester->rpc_responding = true;
    requester->rpc_flags = flags;
    requester->rpc_code = code;
    requester->rpc_data_len = 0;
    if (len > 0 && data) {
        memcpy(requester->rpc_data, data, len);
        requester->rpc_data_len = len;
    }
    requester->rpc_sent_time = now;
    requester->rpc_retry = 0;
    TQ_ADD(&g_rpc_pending_q, requester, requester->rpc_sent_time);
    compact_send_msg_resp_to_requester(requester);
}

///////////////////////////////////////////////////////////////////////////////

static bool check_addr_change(compact_client_t *client, const struct sockaddr_in *from);

// SIG_PKT_SYN0: [auth_key(SIG_AUTH_KEY_PSZ)][remote_peer_id(32)][candidate_count(1)][candidates(N*sizeof(p2p_candidate_t))]
// + 客户端请求连接对方
static void compact_handle_syn0(struct sockaddr_in *from, uint8_t *payload, uint16_t payload_len) {
    const char* PROTO = "SYN0";

    if (payload_len < SIG_PKT_SYN0_PSZ(0)) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    uint64_t auth_key = 0;
    nread_ll(&auth_key, payload);
    if (auth_key == 0) {
        print("E:", LA_F("%s:%d %s: invalid auth_key=0\n", LA_F143, 143),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO);
        return;
    }

    compact_client_t *local_client = NULL;
    HASH_FIND(hh, g_clients_by_auth, &auth_key, sizeof(uint64_t), local_client);
    if (!local_client) {
        print("W:", LA_F("%s:%d %s: unknown auth_key=%" PRIu64 "\n", LA_F148, 148),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, auth_key);
        return;
    }

    const char *remote_peer_id = (const char *)(payload + sizeof(uint64_t));

    // 解析候选列表
    int candidate_count = (uint8_t)payload[sizeof(uint64_t) + P2P_PEER_ID_MAX];
    if (candidate_count > (int)MAX_CANDIDATES) candidate_count = MAX_CANDIDATES;
    p2p_candidate_t candidates[MAX_CANDIDATES];
    memset(candidates, 0, sizeof(candidates));
    size_t cand_offset = sizeof(uint64_t) + P2P_PEER_ID_MAX + 1;
    for (int i = 0; i < candidate_count && cand_offset + sizeof(p2p_candidate_t) <= payload_len; i++) {
        memcpy(&candidates[i], payload + cand_offset, sizeof(p2p_candidate_t));
        cand_offset += sizeof(p2p_candidate_t);
    }

    local_client->base.last_active = P_tick_ms();
    check_addr_change(local_client, from);

    compact_session_t *local_s = NULL, *remote_s = NULL;
    ret_t side = pair_session(&local_client->base, remote_peer_id,
                             (session_t**)&local_s, (session_t**)&remote_s,
                             sizeof(compact_session_t), NULL);

    // UDP 允许可靠性重传，所以 E_DUPLICATE 不视为错误
    if (side == E_DUPLICATE) {

        if (local_s->candidate_count != candidate_count ||
            memcmp(local_s->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count) != 0) {
            print("E:", LA_F("%s %s: duplicate SYN0 with different candidates\n", LA_F33, 33),
                   local_client->base.local_peer_id, PROTO);
        }
        // （重复）回复 SYN0_ACK 应答
        else compact_send_syn0_ack(local_s, remote_peer_id, remote_s != NULL);

        return;
    }
    if (side < E_NONE) {
        print("E:", LA_F("%s %s: build session failed\n", LA_F30, 30),
              local_client->base.local_peer_id, PROTO);
        return;
    }
    assert(local_s);

    // 更新候选列表
    local_s->candidate_count = candidate_count;
    if (candidate_count) {
        memcpy(local_s->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count);
    }

    print("V:", LA_F("%s %s: auth_key=%" PRIu64 ", cands=%d\n", LA_F29, 29),
           local_client->base.local_peer_id, PROTO, auth_key, candidate_count);

    // 如果对端在线
    if (remote_s) {

        // 执行配对
        local_s->base.peer = (session_t*)remote_s; remote_s->base.peer = (session_t*)local_s;
        print("I:", LA_F("%s %s: paired with '%.*s'\n", LA_F35, 35),
               local_client->base.local_peer_id, PROTO,
               P2P_PEER_ID_MAX, remote_peer_id);

        // 如果对端已二次确认 SYN0_ACK，则触发 SYN0 推送
        if (remote_s->sync0_acked == 1 && !TQ_INQ(&g_sync0_pending_q, remote_s)) {
            compact_session_send_syn0(remote_s, 0);
            remote_s->sync0_base_index = 0;
            remote_s->sync0_retry = 0;
            remote_s->sync0_sent_time = local_client->base.last_active;
            TQ_ADD(&g_sync0_pending_q, remote_s, remote_s->sync0_sent_time);
        }
    }

    // 回复 SYN0_ACK 并加入（一阶段）待确认队列（等待客户端二次确认）
    compact_send_syn0_ack(local_s, remote_peer_id, remote_s != NULL);
    assert(local_s->sync0_acked == 0 && !TQ_INQ(&g_sync0_pending_q, local_s));
    local_s->sync0_base_index = 0;
    local_s->sync0_retry = 0;
    local_s->sync0_sent_time = local_client->base.last_active;
    TQ_ADD(&g_sync0_pending_q, local_s, local_s->sync0_sent_time);
}

// SIG_PKT_SYN0_ACK（client→server）
// + 方向 2：客户端二次确认收到 SYN0_ACK（session_id 已建立）
// + 方向 3：客户端确认收到服务器 SYN0，停止可靠性重传机制
// NOLINTNEXTLINE(readability-non-const-parameter)
static void compact_handle_syn0_ack(struct sockaddr_in *from, uint8_t *payload, uint16_t payload_len) {
    const char* PROTO = "SYN0_ACK";

    if (payload_len < SIG_PKT_SYN0_ACK_C2S_PSZ) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    compact_session_t *session = (compact_session_t*)find_session(session_id);

    if (session) {
        check_addr_change(COMPACT_CLIENT(session), from);

        // 方向 2：客户端对服务器返回的 SYN0_ACK 的（二次）确认。此时服务器尚未推送（来自对端的）SYN0
        if (session->sync0_acked == 0) {

            session->sync0_acked = 1;

            // 从 SYN0_ACK 待确认队列中移除
            if (TQ_INQ(&g_sync0_pending_q, session)) TQ_RM(&g_sync0_pending_q, session);
            session->sync0_retry = 0;
            session->sync0_sent_time = 0;

            print("V:", LA_F("%s %s: 2nd-ack confirmed (ses_id=%u)\n", LA_F24, 24),
                   CLIENT(session)->local_peer_id, PROTO, session_id);

            // 二次确认后，若已配对则触发 SYN0 推送
            if (PEER_ONLINE(session) && !TQ_INQ(&g_sync0_pending_q, session)) {
                compact_session_send_syn0(session, 0);
                session->sync0_base_index = 0;
                session->sync0_retry = 0;
                session->sync0_sent_time = P_tick_ms();
                TQ_ADD(&g_sync0_pending_q, session, session->sync0_sent_time);
            }
        }
        else {

            // 方向 3：客户端确认了（服务器转发的）来自对端 SYN0
            if (session->sync0_acked == 1) { session->sync0_acked = 2;

                print("V:", LA_F("%s %s: confirmed, retries=%d (ses_id=%u)\n", LA_F32, 32),
                       CLIENT(session)->local_peer_id, PROTO, session->sync0_retry, session_id);
            }

            if (TQ_INQ(&g_sync0_pending_q, session)) TQ_RM(&g_sync0_pending_q, session);

            session->sync0_base_index = 0;
            session->sync0_retry = 0;
            session->sync0_sent_time = 0;

            // 有延期的地址变更通知，立即发送
            if (session->addr_notify_seq != 0) {

                compact_session_send_syn0(session, session->addr_notify_seq);
                if (TQ_INQ(&g_sync0_pending_q, session)) TQ_RM(&g_sync0_pending_q, session);
                session->sync0_base_index = session->addr_notify_seq;
                session->sync0_retry = 0;
                session->sync0_sent_time = P_tick_ms();
                TQ_ADD(&g_sync0_pending_q, session, session->sync0_sent_time);

                print("I:", LA_F("%s addr changed, deferred notifying (ses_id=%u)\n", LA_F60, 60),
                      CLIENT(session)->local_peer_id, session->base.peer->session_id);
            }
        }
    }
    else print("W:", LA_F("%s for unknown ses_id=%u\n", LA_F62, 62), PROTO, session_id);
}

static void compact_handle_sync_ack(sock_t udp_fd, uint8_t *buf, size_t len,
                                     struct sockaddr_in *from, p2p_packet_hdr_t *hdr,
                                     uint8_t *payload, uint16_t payload_len) { (void)len;
    const char* PROTO = "SYNC_ACK";

    if (payload_len < SIG_PKT_SYNC_ACK_PSZ) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    uint16_t ack_seq = ntohs(hdr->seq);
    if (ack_seq > 16) {
        print("E:", LA_F("%s:%d %s: invalid seq=%u\n", LA_F145, 145),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, ack_seq);
        return;
    }

    uint32_t session_id = nget_l(payload);
    compact_session_t *session = (compact_session_t*)find_session(session_id);
    if (!session) {
        print("W:", LA_F("%s for unknown ses_id=%u\n", LA_F62, 62), PROTO, session_id);
        return;
    }

    print("V:", LA_F("%s %s accepted, seq=%u, ses_id=%u\n", LA_F20, 20),
          CLIENT(session)->local_peer_id, PROTO, ack_seq, session_id);

    // 如果是客户端确认收到 addr change SYNC 包，则停止可靠性重传机制
    if (ack_seq == 0) {
        check_addr_change(COMPACT_CLIENT(session), from);

        if (TQ_INQ(&g_sync0_pending_q, session)) TQ_RM(&g_sync0_pending_q, session);

        session->sync0_base_index = 0;
        session->sync0_retry = 0;
        session->sync0_sent_time = 0;

        print("V:", LA_F("%s %s: addr-notify confirmed (ses_id=%u)\n", LA_F28, 28),
               CLIENT(session)->local_peer_id, PROTO, session_id);
    }
    // ack_seq≠0 的 ACK 是客户端之间的确认，服务器负责 relay 转发
    else {

        if (session && PEER_ONLINE(session)) {
            check_addr_change(COMPACT_CLIENT(session), from);

            nwrite_l((uint8_t *)payload, session->base.peer->session_id);

            compact_client_t* peer_client = COMPACT_CLIENT(COMPACT_PEER(session));
            sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                   (struct sockaddr *)&peer_client->addr, sizeof(peer_client->addr));

            print("V:", LA_F("%s -> %s relay %s, seq=%u (ses_id=%u)\n", LA_F50, 50),
                   CLIENT(session)->local_peer_id, CLIENT(session->base.peer)->local_peer_id,
                   PROTO, ack_seq, session_id);
        }
        else {
            print("W:", LA_F("%s %s: relay failed (peer unavailable), seq=%u (ses_id=%u)\n", LA_F36, 36),
                   CLIENT(session)->local_peer_id, PROTO, ack_seq, session_id);
        }
    }
}

// SIG_PKT_SYNC + relay 数据包（P2P_PKT_DATA / ACK / CRYPTO / CONN / CONN_ACK / REACH）
static void compact_handle_relay(sock_t udp_fd, uint8_t *buf, size_t len,
                                  struct sockaddr_in *from, p2p_packet_hdr_t *hdr,
                                  uint8_t *payload, uint16_t payload_len) { (void)len;
    const char* PROTO = (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                       (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                       (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                       (hdr->type == SIG_PKT_SYNC) ? "SYNC" :
                       (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                       (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH";

    if (payload_len < (int)P2P_SESS_ID_SZ) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    if (hdr->type == SIG_PKT_SYNC && hdr->seq == 0) {
        print("E:", LA_F("%s:%d %s: invalid server-only seq=0, dropped\n", LA_F146, 146),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO);
        return;
    }

    uint32_t session_id = nget_l(payload);
    compact_session_t *session = (compact_session_t*)find_session(session_id);
    if (!session) {
        print("W:", LA_F("%s:%d %s: unknown ses_id=%u, dropped\n", LA_F149, 149),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id);
        return;
    }

    if (!PEER_ONLINE(session)) {
        print("W:", LA_F("%s %s: unavailable peer (sess_id=%u), dropped\n", LA_F37, 37),
              CLIENT(session)->local_peer_id, PROTO, session_id);
        return;
    }

    check_addr_change(COMPACT_CLIENT(session), from);

    print("V:", LA_F("%s -> %s %s accepted, ses_id=%u\n", LA_F40, 40),
          CLIENT(session)->local_peer_id, CLIENT(session->base.peer)->local_peer_id,
          PROTO, session_id);

    nwrite_l((uint8_t *)payload, session->base.peer->session_id);

    compact_client_t* peer_client = COMPACT_CLIENT(COMPACT_PEER(session));
    sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
           (struct sockaddr *)&peer_client->addr, sizeof(peer_client->addr));

    if (hdr->type == SIG_PKT_SYNC || hdr->type == P2P_PKT_REACH ||
        hdr->type == P2P_PKT_DATA || hdr->type == P2P_PKT_CRYPTO) {
        print("V:", LA_F("%s -> %s %s seq=%u (ses_id=%u)\n", LA_F42, 42),
               CLIENT(session)->local_peer_id, CLIENT(session->base.peer)->local_peer_id,
               PROTO, ntohs(hdr->seq), session_id);
    } else {
        print("V:", LA_F("%s -> %s %s (ses_id=%u)\n", LA_F39, 39),
              CLIENT(session)->local_peer_id, CLIENT(session->base.peer)->local_peer_id,
              PROTO, session_id);
    }
}

// SIG_PKT_REQ: A→Server
// NOLINTNEXTLINE(readability-non-const-parameter)
static void compact_handle_req(struct sockaddr_in *from, p2p_packet_hdr_t *hdr, uint8_t *payload, uint16_t payload_len) {
    const char* PROTO = "REQ";

    if (payload_len < SIG_PKT_REQ_MIN_PSZ) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    if (hdr->flags & SIG_FLAG_RELAY) {
        print("E:", LA_F("%s:%d %s: invalid relay flag\n", LA_F144, 144),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO);
        return;
    }

    int msg_data_len = (int)(payload_len - SIG_PKT_REQ_MIN_PSZ);
    if (msg_data_len > P2P_MSG_DATA_MAX) {
        print("E:", LA_F("%s:%d %s: data overflow (%d)\n", LA_F142, 142),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, msg_data_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + 4);
    if (session_id == 0 || sid == 0) {
        print("E:", LA_F("%s:%d %s: invalid sess_id=%u or sid=%u\n", LA_F147, 147),
               inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id, sid);
        return;
    }

    compact_session_t *req_sess = (compact_session_t*)find_session(session_id);
    if (!req_sess) {
        print("W:", LA_F("%s:%d %s: unknown sess_id=%u, dropped\n", LA_F150, 150),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id);
        return;
    }

    if (!PEER_ONLINE(req_sess)) {
        print("W:", LA_F("%s %s: unavailable peer (sess_id=%u), rejected\n", LA_F38, 38),
              CLIENT(req_sess)->local_peer_id, PROTO, session_id);
        compact_session_send_req_ack(req_sess, sid, 1);
        return;
    }

    uint8_t msg = payload[6]; const uint8_t *msg_data = payload + 7;
    print("V:", LA_F("%s %s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n", LA_F26, 26),
           CLIENT(req_sess)->local_peer_id, PROTO, session_id, sid, msg, msg_data_len);

    check_addr_change(COMPACT_CLIENT(req_sess), from);

    if (TQ_INQ(&g_rpc_pending_q, req_sess)) {

        // 如果和上次的 sid 相同（可靠性重传）
        if (sid == req_sess->rpc_last_sid) {

            // 还未回复
            if (!req_sess->rpc_responding) {

                compact_session_send_req_ack(req_sess, sid, 0);

                print("V:", LA_F("%s -> %s %s: retransmit & resend ACK, sid=%u (ses_id=%u)\n", LA_F48, 48),
                      CLIENT(req_sess)->local_peer_id, CLIENT(req_sess->base.peer)->local_peer_id,
                      PROTO, sid, req_sess->base.session_id);
            }
            else {
                print("V:", LA_F("%s -> %s %s: retransmit during RSP phase, ignoring, sid=%u (ses_id=%u)\n", LA_F49, 49),
                      CLIENT(req_sess)->local_peer_id, CLIENT(req_sess->base.peer)->local_peer_id,
                      PROTO, sid, req_sess->base.session_id);
            }
            return;
        }

        // 过时的 rpc
        if (!uint16_circle_newer(sid, req_sess->rpc_last_sid)) {
            print("V:", LA_F("%s -> %s %s: obsolete sid=%u (last=%u), ignoring\n", LA_F47, 47),
                  CLIENT(req_sess)->local_peer_id, CLIENT(req_sess->base.peer)->local_peer_id,
                  PROTO, sid, req_sess->rpc_last_sid);
            return;
        }

        // 发起新的 rpc，将之前未完成的丢弃
        print("I:", LA_F("%s -> %s %s: new sid=%u last=%u (responding=%d), canceling old RPC (ses_id=%u)\n", LA_F45, 45),
              CLIENT(req_sess)->local_peer_id, CLIENT(req_sess->base.peer)->local_peer_id,
              PROTO, sid, req_sess->rpc_last_sid, req_sess->rpc_responding, req_sess->base.session_id);

        if (TQ_INQ(&g_rpc_pending_q, req_sess)) TQ_RM(&g_rpc_pending_q, req_sess);
    }

    if (req_sess->rpc_last_sid != 0 && !uint16_circle_newer(sid, req_sess->rpc_last_sid)) {
        print("V:", LA_F("%s -> %s %s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n", LA_F46, 46),
              CLIENT(req_sess)->local_peer_id, CLIENT(req_sess->base.peer)->local_peer_id,
              PROTO, sid, req_sess->rpc_last_sid);
        return;
    }

    req_sess->rpc_last_sid = sid;
    req_sess->rpc_responding = false;
    req_sess->rpc_code = msg;
    req_sess->rpc_data_len = msg_data_len;
    if (msg_data_len > 0) memcpy(req_sess->rpc_data, msg_data, msg_data_len);

    compact_session_send_req_ack(req_sess, sid, 0);

    req_sess->rpc_sent_time = P_tick_ms();
    req_sess->rpc_retry = 0;
    TQ_ADD(&g_rpc_pending_q, req_sess, req_sess->rpc_sent_time);
    compact_session_send_req_to_peer(req_sess);

    print("I:", LA_F("%s -> %s %s: forwarded, sid=%u, msg=%u (ses_id=%u)\n", LA_F44, 44),
           CLIENT(req_sess)->local_peer_id, CLIENT(req_sess->base.peer)->local_peer_id, PROTO,
           sid, msg, req_sess->base.session_id);
}

// SIG_PKT_RSP: B→Server
// NOLINTNEXTLINE(readability-non-const-parameter)
static void compact_handle_rsp(struct sockaddr_in *from, uint8_t *payload, uint16_t payload_len) {
    const char* PROTO = "RSP";

    if (payload_len < SIG_PKT_RSP_MIN_PSZ) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    int resp_len = (int)(payload_len - SIG_PKT_RSP_MIN_PSZ);
    if (resp_len > P2P_MSG_DATA_MAX) {
        print("E:", LA_F("%s:%d %s: data overflow (%d)\n", LA_F142, 142),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, resp_len);
        return;
    }

    uint32_t session_id = nget_l(payload); uint16_t sid = nget_s(payload + 4);
    if (session_id == 0 || sid == 0) {
        print("E:", LA_F("%s:%d %s: invalid sess_id=%u or sid=%u\n", LA_F147, 147),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id, sid);
        return;
    }

    compact_session_t *rsp_sess = (compact_session_t*)find_session(session_id);
    if (!rsp_sess) {
        print("W:", LA_F("%s:%d %s: unknown sess_id=%u, dropped\n", LA_F150, 150),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id);
        return;
    }

    if (!PEER_ONLINE(rsp_sess)) {
        print("W:", LA_F("%s %s: unavailable peer (sess_id=%u), dropped\n", LA_F37, 37),
              CLIENT(rsp_sess)->local_peer_id, PROTO, session_id);
        return;
    }

    uint8_t resp_code = payload[6]; const uint8_t *resp_data = payload + 7;
    print("V:", LA_F("%s %s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n", LA_F25, 25),
          COMPACT_CLIENT(rsp_sess)->base.local_peer_id, PROTO, session_id, sid, resp_code, resp_len);

    check_addr_change(COMPACT_CLIENT(rsp_sess), from);

    compact_session_send_resp_ack(rsp_sess, sid);

    compact_session_t *req_sess = COMPACT_PEER(rsp_sess);

    if (!TQ_INQ(&g_rpc_pending_q, req_sess) || req_sess->rpc_responding || req_sess->rpc_last_sid != sid) {
        print("W:", LA_F("%s <- %s %s: no matching pending rpc (sid=%u, expected=%u)\n", LA_F54, 54),
              CLIENT(req_sess)->local_peer_id, CLIENT(rsp_sess)->local_peer_id,
              PROTO, sid, req_sess->rpc_last_sid);
        return;
    }

    if (TQ_INQ(&g_rpc_pending_q, req_sess)) TQ_RM(&g_rpc_pending_q, req_sess);
    compact_transition_to_resp_pending(req_sess, P_tick_ms(), 0, resp_code, resp_data, resp_len);

    print("I:", LA_F("%s <- %s %s: backward, sid=%u (ses_id=%u)\n", LA_F53, 53),
          CLIENT(rsp_sess)->local_peer_id, CLIENT(req_sess)->local_peer_id,
          PROTO, sid, req_sess->base.session_id);
}

// SIG_PKT_RSP_ACK: A→Server（A 确认收到 RSP）
// NOLINTNEXTLINE(readability-non-const-parameter)
static void compact_handle_rsp_ack(struct sockaddr_in *from, uint8_t *payload, uint16_t payload_len) {
    const char* PROTO = "RSP_ACK";

    if (payload_len < SIG_PKT_RSP_ACK_PSZ) {
        print("E:", LA_F("%s:%d %s: bad payload(%u)\n", LA_F141, 141),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, payload_len);
        return;
    }

    uint32_t session_id = nget_l(payload); uint16_t sid = nget_s(payload + 4);
    if (session_id == 0 || sid == 0) {
        print("E:", LA_F("%s:%d %s: invalid sess_id=%u or sid=%u\n", LA_F147, 147),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id, sid);
        return;
    }

    compact_session_t *req_sess = (compact_session_t*)find_session(session_id);
    if (!req_sess) {
        print("W:", LA_F("%s:%d %s: unknown sess_id=%u\n", LA_F151, 151),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), PROTO, session_id);
        return;
    }

    print("V:", LA_F("%s %s: accepted, ses_id=%u, sid=%u\n", LA_F27, 27),
          CLIENT(req_sess)->local_peer_id, PROTO, session_id, sid);

    if (!req_sess->rpc_responding || req_sess->rpc_last_sid != sid) {
        print("V:", LA_F("%s %s: no matching pending rpc (sid=%u)\n", LA_F34, 34),
              CLIENT(req_sess)->local_peer_id, PROTO, sid);
        return;
    }

    if (TQ_INQ(&g_rpc_pending_q, req_sess)) TQ_RM(&g_rpc_pending_q, req_sess);
    req_sess->rpc_responding = false;
    req_sess->rpc_retry = 0;

    print("I:", LA_F("%s %s: complete, sid=%u (ses_id=%u)\n", LA_F31, 31),
          CLIENT(req_sess)->local_peer_id, PROTO, sid, req_sess->base.session_id);
}

///////////////////////////////////////////////////////////////////////////////

// 检测地址变更并通知对端（所有已配对 session 均会发出通知）
static bool check_addr_change(compact_client_t *client, const struct sockaddr_in *from) {

    if (memcmp(&client->addr, from, sizeof(*from)) == 0) return false;

    client->addr = *from;

    for (session_t *sbase = client->base.sessions; sbase; sbase = sbase->next) {
        compact_session_t *session = (compact_session_t*)sbase;
        if (!PEER_ONLINE(session)) continue;

        compact_session_t *peer = COMPACT_PEER(session);

        // 如果本端已收到过来自对端的 SYN0
        if (peer->sync0_acked > 1) {
            peer->addr_notify_seq = (uint8_t)(peer->addr_notify_seq + 1);
            if (peer->addr_notify_seq == 0) peer->addr_notify_seq = 1;

            compact_session_send_syn0(peer, peer->addr_notify_seq);
            if (TQ_INQ(&g_sync0_pending_q, peer)) TQ_RM(&g_sync0_pending_q, peer);
            peer->sync0_base_index = peer->addr_notify_seq;
            peer->sync0_retry = 0;
            peer->sync0_sent_time = P_tick_ms();
            TQ_ADD(&g_sync0_pending_q, peer, peer->sync0_sent_time);

            print("I:", LA_F("%s addr changed, notifying '%s' (ses_id=%u)\n", LA_F61, 61),
                  client->base.local_peer_id, CLIENT(peer)->local_peer_id, peer->base.session_id);
        }
        else if (peer->sync0_acked >= 0) {
            if (peer->addr_notify_seq == 0) peer->addr_notify_seq = 1;

            print("I:", LA_F("%s addr changed, defer notification until first ACK (ses_id=%u)\n", LA_F59, 59),
                  client->base.local_peer_id, peer->base.session_id);
        }
        else {
            print("W:", LA_F("%s addr changed, but first info packet was abandoned (ses_id=%u)\n", LA_F58, 58),
                   client->base.local_peer_id, peer->base.session_id);
        }
    }

    return true;
}

// 处理 COMPACT 模式信令（UDP 无状态，对应 p2p_signal_compact 模块）
void compact_handle_signaling(sock_t udp_fd, uint8_t *buf, uint16_t len, struct sockaddr_in *from) {

    if (len < 4) return;

    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4; uint16_t payload_len = len - 4;

    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    switch (hdr->type) {
    case SIG_PKT_REG: { const char* PROTO = "REG";

        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%u\n", LA_F192, 192),
               from_str, PROTO, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_REG_PSZ) {
            print("E:", LA_F("%s %s bad payload(len=%u)\n", LA_F21, 21), from_str, PROTO, payload_len);
            return;
        }

        const char *local_peer_id = (const char *)payload;
        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX);
        if (instance_id == 0) {
            print("E:", LA_F("%.*s %s: invalid instance_id=0\n", LA_F16, 16), P2P_PEER_ID_MAX, local_peer_id, PROTO);
            return;
        }

        print("V:", LA_F("%.*s %s: accepted, inst_id=%u\n", LA_F14, 14),
               P2P_PEER_ID_MAX, local_peer_id, PROTO, instance_id);

        client_t *client = find_client(local_peer_id);
        if (client) {

            //（协议/instance_id 相同）可靠性重传机制，幂等响应
            int r = restore_client_as(client, PROTO_COMPACT, udp_fd, instance_id);
            if (r < 0) {
                print("E:", LA_F("%.*s %s: realloc client\n", LA_F17, 17), P2P_PEER_ID_MAX, local_peer_id, PROTO);
                return;
            }
            if (r) {
                check_addr_change((compact_client_t*)client, from);
                compact_send_reg_ack(client, from, ((compact_client_t*)client)->auth_key, instance_id);
                return;
            }
        }
        else {

            client = alloc_client(PROTO_COMPACT, udp_fd);
            if (!client) {
                print("E:", LA_F("%.*s %s: alloc client failed\n", LA_F15, 15), P2P_PEER_ID_MAX, local_peer_id, PROTO);
                return;
            }

            client->instance_id = instance_id;
        }

         // 初始化客户端槽位
         identify_client(client, local_peer_id);
         compact_client_t *compact_client = (compact_client_t*)client;
         compact_client->addr = *from;

         int n = 0;
         do { compact_client->auth_key = P_rand64();
             if (++n > 32) return;
         } while (!compact_client->auth_key);
         HASH_ADD(hh, g_clients_by_auth, auth_key, sizeof(uint64_t), compact_client);

        compact_send_reg_ack(client, from, compact_client->auth_key, instance_id);
        print("V:", LA_F("%.*s %s, auth_key=%" PRIu64 "\n", LA_F13, 13),
             P2P_PEER_ID_MAX, local_peer_id, PROTO, compact_client->auth_key);
    } break;

    // SIG_PKT_OFF: [auth_key(SIG_AUTH_KEY_PSZ)]
    case SIG_PKT_OFF: { const char* PROTO = "OFF";

        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%u\n", LA_F192, 192),
               from_str, PROTO, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < (int)SIG_PKT_OFF_PSZ) {
            print("E:", LA_F("%s %s bad payload(len=%u)\n", LA_F21, 21), from_str, PROTO, payload_len);
            return;
        }

        uint64_t off_auth_key = 0;
        nread_ll(&off_auth_key, payload);
        if (off_auth_key == 0) {
            print("E:", LA_F("%s %s invalid auth_key=0\n", LA_F23, 23), from_str, PROTO);
            return;
        }

        compact_client_t *off_client = NULL;
        HASH_FIND(hh, g_clients_by_auth, &off_auth_key, sizeof(uint64_t), off_client);

        if (off_client) {
            print("V:", LA_F("%s %s accepted, free slot\n", LA_F19, 19),
                   off_client->base.local_peer_id, PROTO);
            free_client((client_t*)off_client);
        }
    } break;

    // SIG_PKT_ALIVE: [auth_key(SIG_AUTH_KEY_PSZ)]
    case SIG_PKT_ALV: { const char* PROTO = "ALIVE";

        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%u\n", LA_F192, 192),
               from_str, PROTO, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_ALV_PSZ) {
            print("E:", LA_F("%s %s bad payload(len=%u)\n", LA_F21, 21), from_str, PROTO, payload_len);
            return;
        }

        uint64_t alive_auth_key = nget_ll(payload);

        compact_client_t *alive_client = NULL;
        HASH_FIND(hh, g_clients_by_auth, &alive_auth_key, sizeof(uint64_t), alive_client);
        if (alive_client) {

            print("V:", LA_F("%s %s accepted, auth_key=%" PRIu64 "\n", LA_F18, 18),
                  alive_client->base.local_peer_id, PROTO, alive_auth_key);

            alive_client->base.last_active = P_tick_ms();
            check_addr_change(alive_client, from);

            {   const char* ACK_PROTO = "ALV_ACK";

                uint8_t ack[4];
                p2p_pkt_hdr_encode(ack, SIG_PKT_ALV_ACK, 0, 0);

                print("V:", LA_F("%s send %s, auth_key=%" PRIu64 "\n", LA_F63, 63),
                      alive_client->base.local_peer_id, ACK_PROTO, alive_auth_key);

                udp_send(alive_client->base.fd, ack, (int)sizeof(ack), from, ACK_PROTO);
            }
        } else {
            print("W:", LA_F("%s %s invalid auth_key=%" PRIu64 "\n", LA_F22, 22), from_str, PROTO, alive_auth_key);
        }
    } break;

    case SIG_PKT_SYN0:
        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F193, 193),
               from_str, "SYN0", ntohs(hdr->seq), hdr->flags, len);
        compact_handle_syn0(from, payload, payload_len);
        break;

    case SIG_PKT_SYN0_ACK:
        printf(LA_F("[U] %s recv %s, len=%zu\n", LA_F191, 191),
               from_str, "SYN0_ACK", len);
        compact_handle_syn0_ack(from, payload, payload_len);
        break;

    case SIG_PKT_SYNC_ACK:
        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F193, 193),
               from_str, "SYNC_ACK", ntohs(hdr->seq), hdr->flags, len);
        compact_handle_sync_ack(udp_fd, buf, len, from, hdr, payload, payload_len);
        break;

    // SIG_PKT_SYNC + relay 数据包（P2P_PKT_DATA / ACK / CRYPTO / CONN / CONN_ACK / REACH）
    case P2P_PKT_DATA:
    case P2P_PKT_ACK:
    case P2P_PKT_CRYPTO:
    case P2P_PKT_CONN:
    case P2P_PKT_CONN_ACK:
    case P2P_PKT_REACH:
        if (!(hdr->flags & P2P_FLAG_SESSION)) {
            print("E:", LA_F("%s: missing SESSION flag, dropped\n", LA_F115, 115),
                  (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                  (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                  (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                  (hdr->type == SIG_PKT_SYNC) ? "SYNC" :
                  (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                  (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH");
            return;
        }
        /* fall through */
    case SIG_PKT_SYNC: {
        const char* _rp = (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                          (hdr->type == P2P_PKT_ACK)  ? "RELAY-ACK"  :
                          (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                          (hdr->type == SIG_PKT_SYNC) ? "SYNC" :
                          (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                          (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH";
        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F193, 193),
               from_str, _rp, ntohs(hdr->seq), hdr->flags, len);
        compact_handle_relay(udp_fd, buf, len, from, hdr, payload, payload_len);
    } break;

    case SIG_PKT_REQ:
        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F193, 193),
               from_str, "REQ", ntohs(hdr->seq), hdr->flags, len);
        compact_handle_req(from, hdr, payload, payload_len);
        break;

    case SIG_PKT_RSP:
        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F193, 193),
               from_str, "RSP", ntohs(hdr->seq), hdr->flags, len);
        compact_handle_rsp(from, payload, payload_len);
        break;

    case SIG_PKT_RSP_ACK:
        printf(LA_F("[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F193, 193),
               from_str, "RSP_ACK", ntohs(hdr->seq), hdr->flags, len);
        compact_handle_rsp_ack(from, payload, payload_len);
        break;

    default:
        print("W:", LA_F("%s Unknown pkt type 0x%02x\n", LA_F57, 57), from_str, hdr->type);
        break;
    } // switch
}

///////////////////////////////////////////////////////////////////////////////

// 停止/终止会话
static void compact_session_break(client_ctx_t *ctx, session_t *s, session_t *ps, break_mode_e break_mode) {
    (void)ctx;

    assert(PEER_ONLINE(s) && PEER_ONLINE(ps));

    if (break_mode == SESS_BREAK_STOP) return;

    compact_session_t *session = (compact_session_t*)s;
    compact_session_t *peer = (compact_session_t*)ps;

    // 通知对端断开，并标记对端 peer 指针为 -1
    peer->addr_notify_seq = 0;
    peer->candidate_count = 0;

    if (TQ_INQ(&g_sync0_pending_q, peer)) TQ_RM(&g_sync0_pending_q, peer);
    peer->sync0_acked = 0;
    peer->sync0_sent_time = 0;
    peer->sync0_retry = 0;
    peer->sync0_base_index = 0;

    if (TQ_INQ(&g_rpc_pending_q, peer)) TQ_RM(&g_rpc_pending_q, peer);
    peer->rpc_last_sid = 0;
    peer->rpc_sent_time = 0;
    peer->rpc_retry = 0;
    peer->rpc_responding = false;

    compact_session_send_fin(peer, "peer_disconnect");

    session->addr_notify_seq = 0;
    session->candidate_count = 0;
    session->sync0_acked = 0;
    session->sync0_sent_time = 0;
    session->sync0_retry = 0;
    session->sync0_base_index = 0;
    session->rpc_last_sid = 0;
    session->rpc_sent_time = 0;
    session->rpc_retry = 0;
    session->rpc_responding = false;
}

static void compact_session_close(client_ctx_t *ctx, session_t *s, bool terminate, bool clearing) {
    (void)ctx;
    (void)terminate;
    (void)clearing;

    compact_session_t *cs = (compact_session_t*)s;

    if (TQ_INQ(&g_sync0_pending_q, cs)) TQ_RM(&g_sync0_pending_q, cs);
    if (TQ_INQ(&g_rpc_pending_q, cs))   TQ_RM(&g_rpc_pending_q, cs);
}

// 释放 client
static void compact_client_free(client_ctx_t *ctx, client_t *c) { (void)ctx;
    compact_client_t *cc = (compact_client_t*)c;

    // 从 auth 哈希表移除
    if (cc->auth_key) {
        HASH_DELETE(hh, g_clients_by_auth, cc);
        cc->auth_key = 0;
    }
}

client_ctx_t*
compact_init(void) {

    g_ctx.cb_free = compact_client_free;
    g_ctx.cb_break = compact_session_break;
    g_ctx.cb_close = compact_session_close;

    TQ_INIT(&g_sync0_pending_q,
            SYN0_RETRY_INTERVAL_MS,
            offsetof(compact_session_t, sync0_pending_prev),
            offsetof(compact_session_t, sync0_pending_next),
            offsetof(compact_session_t, sync0_sent_time));

    TQ_INIT(&g_rpc_pending_q,
            RPC_RETRY_INTERVAL_MS,
            offsetof(compact_session_t, rpc_pending_prev),
            offsetof(compact_session_t, rpc_pending_next),
            offsetof(compact_session_t, rpc_sent_time));

    return &g_ctx;
}

// 检查并重传 RPC（统一处理 REQ 和 RSP 阶段）
void compact_retry_pending(sock_t udp_fd, uint64_t now) { (void)udp_fd;

    // 处理 SYNC(seq=0) 超时重传
    compact_session_t *q;
    TQ_RETRY(&g_sync0_pending_q, now, q,
        if (q->sync0_retry >= SYN0_MAX_RETRY) { assert(PEER_ONLINE(q));  // compact_session_break 机制确保队列中的 peer 总是在线

            print("W:", LA_F("%s <> %s %s gave up after %d tries, (ses_id=%u)\n", LA_F55, 55),
                   CLIENT(q)->local_peer_id, PEER(q)->client->local_peer_id, "SYN0",
                   q->sync0_retry, q->base.session_id);

            if (q->sync0_base_index == 0) {
                q->sync0_acked = -1/* 超时停止 */;
            }
            // TQ_RETRY 已自动移除
        }
        else { assert(PEER_ONLINE(q));  // compact_session_break 机制确保队列中的 peer 总是在线
            
            // 状态 0：重传 SYN0_ACK，等待客户端二次确认
            if (q->sync0_acked == 0) {
                compact_send_syn0_ack(q, q->base.peer->client->local_peer_id, true);
            }
            // 状态 1：重传 SYN0，等待客户端确认收到该（来自对端的）SYN0
            else {
                compact_session_send_syn0(q, q->sync0_base_index);
            }

            q->sync0_retry++;
            q->sync0_sent_time = now;
            TQ_ADD(&g_sync0_pending_q, q, q->sync0_sent_time); // 重新入队

            print("V:", LA_F("%s <> %s %s resent %d/%d, (ses_id=%u)\n", LA_F56, 56),
                   CLIENT(q)->local_peer_id, PEER(q)->client->local_peer_id, "SYN0",
                   q->sync0_retry, SYN0_MAX_RETRY, q->base.session_id);
        }
    )

    // 处理 RPC 超时重传
    TQ_RETRY(&g_rpc_pending_q, now, q,
        
        assert(PEER_ONLINE(q));  // compact_session_break 机制确保队列中的 peer 总是在线

        // REQ 阶段
        if (!q->rpc_responding) { 
            
            if (q->rpc_retry >= REQ_MAX_RETRY) {
                print("W:", LA_F("%s -> %s %s timeout after %d retries, sid=%u (ses_id=%u)\n", LA_F43, 43),
                      CLIENT(q)->local_peer_id, PEER(q)->client->local_peer_id, "REQ",
                      q->rpc_retry, q->rpc_last_sid, q->base.session_id);

                compact_transition_to_resp_pending(q, now, SIG_RPC_FLAG_TIMEOUT, 0, NULL, 0);
            }
            else {

                // 重传 REQ 给对端
                compact_session_send_req_to_peer(q);
                q->rpc_retry++;
                q->rpc_sent_time = now;
                TQ_ADD(&g_rpc_pending_q, q, q->rpc_sent_time); // 重新入队

                print("V:", LA_F("%s -> %s %s resent %d/%d, sid=%u (ses_id=%u)\n", LA_F41, 41),
                      CLIENT(q)->local_peer_id, PEER(q)->client->local_peer_id, "REQ",
                      q->rpc_retry, REQ_MAX_RETRY, q->rpc_last_sid, q->base.session_id);
            }
        }
        // RSP 阶段
        else { 

            if (q->rpc_retry >= RSP_MAX_RETRY) {
                print("W:", LA_F("%s <- %s %s gave up after %d retries, sid=%u (ses_id=%u)\n", LA_F51, 51),
                      CLIENT(q)->local_peer_id, PEER(q)->client->local_peer_id, "RSP",
                      q->rpc_retry, q->rpc_last_sid, q->base.session_id);

                q->rpc_responding = false;
                q->rpc_retry = 0;
                // TQ_RETRY 已自动移除
            }
            else {
                q->rpc_retry++;
                compact_send_msg_resp_to_requester(q);
                q->rpc_sent_time = now;
                TQ_ADD(&g_rpc_pending_q, q, q->rpc_sent_time); // 重新入队

                print("V:", LA_F("%s <- %s %s resent %d/%d, sid=%u (ses_id=%u)\n", LA_F52, 52),
                      CLIENT(q)->local_peer_id, PEER(q)->client->local_peer_id, "RSP",
                      q->rpc_retry, RSP_MAX_RETRY, q->rpc_last_sid, q->base.session_id);
            }
        }
    )
}

///////////////////////////////////////////////////////////////////////////////
