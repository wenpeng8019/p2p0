//
// Created by 温朋 on 2026/4/18.
//
#define MOD_TAG "COMPACT"

#include "p2p_compact.h"

ARGS(relay);
ARGS(msg);
ARGS(probe_port);


// COMPACT 模式 SYNC 重传参数
#define SYN0_RETRY_INTERVAL_MS         2000    // 重传间隔（毫秒）
#define SYN0_MAX_RETRY                 5       // 最大重传次数

static compact_client_t*            g_clients_by_auth = NULL;

// SYNC(seq=0) 待确认链表（仅包含已发送首包但未收到 ACK 的配对）
static compact_session_t*           g_sync0_pending_head = NULL;
static compact_session_t*           g_sync0_pending_rear = NULL;

// MSG RPC 待确认链表（统一管理 REQ 和 RSP 阶段，通过 rpc_responding 区分）
static compact_session_t*           g_rpc_pending_head = NULL;
static compact_session_t*           g_rpc_pending_rear = NULL;

///////////////////////////////////////////////////////////////////////////////

// 从待确认链表移除
static void compact_pending_remove_syn0(compact_session_t *cs) {

    if (!g_sync0_pending_head || !cs->sync0_pending_next) return;

    if (g_sync0_pending_head == cs) {
        g_sync0_pending_head = cs->sync0_pending_next;
        cs->sync0_pending_next = NULL;
        if (g_sync0_pending_head == (compact_session_t*)(void*)-1) {
            g_sync0_pending_head = NULL;
            g_sync0_pending_rear = NULL;
        }
        return;
    }

    compact_session_t *prev = g_sync0_pending_head;
    while (prev->sync0_pending_next != cs) {
        if (prev->sync0_pending_next == (compact_session_t*)(void*)-1) return;
        prev = prev->sync0_pending_next;
    }

    prev->sync0_pending_next = cs->sync0_pending_next;

    if (cs->sync0_pending_next == (compact_session_t*)(void*)-1) {
        g_sync0_pending_rear = prev;
    }

    cs->sync0_pending_next = NULL;
}

// 将 session 加入 SYNC(seq=0) 待确认链表
static void compact_pending_enqueue_syn0(compact_session_t *cs, uint8_t base_index, uint64_t now) {

    if (cs->sync0_pending_next) {
        compact_pending_remove_syn0(cs);
    }

    cs->sync0_base_index = base_index;
    cs->sync0_retry = 0;
    cs->sync0_sent_time = now;

    cs->sync0_pending_next = (compact_session_t*)(void*)-1;
    if (g_sync0_pending_rear) {
        g_sync0_pending_rear->sync0_pending_next = cs;
        g_sync0_pending_rear = cs;
    } else {
        g_sync0_pending_head = cs;
        g_sync0_pending_rear = cs;
    }
}

// 从 RPC 待确认链表移除
static void compact_pending_remove_rpc(compact_session_t *cs) {

    if (!g_rpc_pending_head || !cs->rpc_pending_next) return;

    if (g_rpc_pending_head == cs) {
        g_rpc_pending_head = cs->rpc_pending_next;
        cs->rpc_pending_next = NULL;
        if (g_rpc_pending_head == (compact_session_t*)(void*)-1) {
            g_rpc_pending_head = NULL;
            g_rpc_pending_rear = NULL;
        }
        return;
    }

    compact_session_t *prev = g_rpc_pending_head;
    while (prev->rpc_pending_next != cs) {
        if (prev->rpc_pending_next == (compact_session_t*)(void*)-1) return;
        prev = prev->rpc_pending_next;
    }

    prev->rpc_pending_next = cs->rpc_pending_next;
    if (cs->rpc_pending_next == (compact_session_t*)(void*)-1) {
        g_rpc_pending_rear = prev;
    }
    cs->rpc_pending_next = NULL;
}

// 将 session 加入 RPC 待确认链表
static void compact_pending_enqueue_rpc(compact_session_t *cs) {

    cs->rpc_pending_next = (compact_session_t*)(void*)-1;
    if (g_rpc_pending_rear) {
        g_rpc_pending_rear->rpc_pending_next = cs;
        g_rpc_pending_rear = cs;
    } else {
        g_rpc_pending_head = cs;
        g_rpc_pending_rear = cs;
    }
}

//-----------------------------------------------------------------------------

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

        print("V:", LA_F("Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%" PRIu64 ", inst_id=%u\n", LA_F112, 112),
              PROTO, MAX_CANDIDATES,
              ARGS_relay.i64 ? "yes" : "no", ARGS_msg.i64 ? "yes" : "no",
              inet_ntoa(to->sin_addr), ntohs(to->sin_port),
              (int)ARGS_probe_port.i64, auth_key, instance_id);
    } else {
        memset(ack + sizeof(p2p_packet_hdr_t), 0, SIG_PKT_REG_ACK_PSZ);
        print("V:", LA_F("Send %s: rejected (no slot available)\n", LA_F114, 114), PROTO);
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

    print("V:", LA_F("Send %s: ses_id=%u, peer=%s\n", LA_F115, 115),
          PROTO, s->base.session_id, online ? "online" : "offline");

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

    print("V:", LA_F("Send %s: peer='%s', reason=%s, ses_id=%u\n", LA_F113, 113),
          PROTO, client->base.local_peer_id, reason, s->base.session_id);


    udp_send(s->base.client->fd, pkt, (int)sizeof(pkt), &client->addr, PROTO);
}

// 发送首次对端候选推送（base_index=0）或地址变更通知（base_index != 0 为循环通知序号）
// base_index=0: SIG_PKT_SYN0，payload: [session_id(4)][0x00(1)][cand_cnt(1)][candidates]
// base_index!=0: SIG_PKT_SYNC（seq=0），payload: [session_id(4)][notify_seq(1)][1][candidate]
static void compact_session_send_syn0(compact_session_t *c, uint8_t base_index) {
    const char* PROTO = base_index == 0 ? "SYN0" : "SYNC";

    assert(c && PEER_ONLINE(&c->base));

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

        print("V:", LA_F("Send %s: cands=%d, ses_id=%u, peer='%s'\n", LA_F110, 110),
              PROTO, cand_cnt, c->base.session_id, client->base.local_peer_id);
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

        print("V:", LA_F("Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n", LA_F109, 109),
              PROTO, base_index, cand_cnt, c->base.session_id, client->base.local_peer_id);
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

    print("V:", LA_F("Send %s: ses_id=%u, sid=%u, status=%u\n", LA_F119, 119),
          PROTO, sender->base.session_id, sid, status);

    udp_send(sender->base.client->fd, ack, ofz, &COMPACT_CLIENT(sender)->addr, PROTO);
}

// 发送 REQ 给对端（Server→对端 relay）
static void compact_session_send_req_to_peer(compact_session_t *sender) {
    const char* PROTO = "REQ";

    assert(sender && PEER_ONLINE(&sender->base));
    assert(sender->rpc_pending_next && !sender->rpc_responding);

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

    print("V:", LA_F("Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s', retries=%d\n", LA_F116, 116),
          PROTO, peer->base.session_id, sender->rpc_last_sid, sender->rpc_code, sender->rpc_data_len,
          peer_c->base.local_peer_id, sender->rpc_retry);

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

    print("V:", LA_F("Send %s: ses_id=%u, sid=%u, peer='%s'\n", LA_F118, 118),
          PROTO, responder->base.session_id, sid, COMPACT_PEER(responder)->base.client->local_peer_id);

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

    print("V:", LA_F("Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d, retries=%d\n", LA_F117, 117),
          PROTO, cs->base.session_id, cs->rpc_last_sid, client->base.local_peer_id, cs->rpc_flags, cs->rpc_code, cs->rpc_data_len, cs->rpc_retry);

    udp_send(client->base.fd, pkt, ofz, &client->addr, PROTO);
}

///////////////////////////////////////////////////////////////////////////////

client_ctx_t*
compact_init(void) {

}

bool
compact_init_client(compact_client_t* c, struct sockaddr_in *from) {

    c->addr = *from;

    // 生成 auth_key 并加入哈希表
    int n = 0;
    do { c->auth_key = P_rand64();
        if (++n > 32) return false;
    } while (!c->auth_key);
    HASH_ADD(hh, g_clients_by_auth, auth_key, sizeof(uint64_t), c);
    return true;
}

// 释放 session
static void compact_free_session(session_t *s) {

    compact_session_t *cs = (compact_session_t*)s;
    if (cs->sync0_pending_next) compact_pending_remove_syn0(cs);
    if (cs->rpc_pending_next)   compact_pending_remove_rpc(cs);

    // 通知对端断开，并标记对端 peer 指针为 -1
    if (PEER_ONLINE(&cs->base)) {
        compact_session_t *peer = COMPACT_PEER(cs);

        peer->addr_notify_seq = 0;
        peer->candidate_count = 0;

        if (peer->sync0_pending_next) compact_pending_remove_syn0(peer);
        peer->sync0_acked = 0;
        peer->sync0_sent_time = 0;
        peer->sync0_retry = 0;
        peer->sync0_base_index = 0;

        if (peer->rpc_pending_next) compact_pending_remove_rpc(peer);
        peer->rpc_last_sid = 0;
        peer->rpc_sent_time = 0;
        peer->rpc_retry = 0;
        peer->rpc_responding = false;

        compact_session_send_fin(peer, "peer_disconnect");
    }

    free_session_base(&cs->base);
}

// 释放 client
void compact_free_client(client_t *c) {
    compact_client_t *cc = (compact_client_t*)c;

    // 从 auth 哈希表移除
    if (cc->auth_key) {
        HASH_DELETE(hh, g_clients_by_auth, cc);
        cc->auth_key = 0;
    }

    while (cc->base.sessions) compact_free_session(cc->base.sessions);
    free_client_base(&cc->base);
}

client_ctx_t g_compact_client_ctx = { compact_free_client, NULL };


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
    compact_pending_enqueue_rpc(requester);
    compact_send_msg_resp_to_requester(requester);
}


///////////////////////////////////////////////////////////////////////////////

static bool check_addr_change(compact_client_t *client, const struct sockaddr_in *from);

// SIG_PKT_SYN0: [auth_key(SIG_AUTH_KEY_PSZ)][remote_peer_id(32)][candidate_count(1)][candidates(N*sizeof(p2p_candidate_t))]
// + 客户端请求连接对方
static void compact_handle_syn0(struct sockaddr_in *from, uint8_t *payload, size_t payload_len) {
    const char* PROTO = "SYN0";

    if (payload_len < SIG_PKT_SYN0_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
        return;
    }

    uint64_t auth_key = 0;
    nread_ll(&auth_key, payload);
    if (auth_key == 0) {
        char from_str[64];
        snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        print("E:", LA_F("%s: invalid auth_key=0 from %s\n", LA_F49, 49), PROTO, from_str);
        return;
    }

    compact_client_t *local_client = NULL;
    HASH_FIND(hh, g_clients_by_auth, &auth_key, sizeof(uint64_t), local_client);
    if (!local_client) {
        char from_str[64];
        snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        print("W:", LA_F("%s: unknown auth_key=%" PRIu64 " from %s\n", LA_F70, 70), PROTO, auth_key, from_str);
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
                             (session_t**)&local_s, (session_t**)&remote_s, sizeof(compact_session_t));

    // UDP 允许可靠性重传，所以 E_DUPLICATE 不视为错误
    if (side == E_DUPLICATE) {

        if (local_s->candidate_count != candidate_count ||
            memcmp(local_s->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count) != 0) {
            print("E:", LA_F("%s: duplicate SYN0 with different candidates from '%s'\n", LA_F159, 159),
                   PROTO, local_client->base.local_peer_id);
        }
        // （重复）回复 SYN0_ACK 应答
        else compact_send_syn0_ack(local_s, remote_peer_id, remote_s != NULL);

        return;
    }
    if (side < E_NONE) {
        print("E:", LA_F("%s: build_session failed for '%.*s'\n", LA_F43, 43), PROTO, P2P_PEER_ID_MAX,
              local_client->base.local_peer_id);
        return;
    }
    assert(local_s);

    // 更新候选列表
    local_s->candidate_count = candidate_count;
    if (candidate_count) {
        memcpy(local_s->candidates, candidates, sizeof(p2p_candidate_t) * candidate_count);
    }

    {
        char from_str[64];
        snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        print("V:", LA_F("%s: auth_key=%" PRIu64 ", cands=%d from %s\n", LA_F154, 154),
               PROTO, auth_key, candidate_count, from_str);
    }

    // 如果对端在线
    if (remote_s) {

        // 执行配对
        local_s->base.peer = (session_t*)remote_s; remote_s->base.peer = (session_t*)local_s;
        print("I:", LA_F("%s: paired '%.*s' <-> '%.*s'\n", LA_F60, 60),
               PROTO, P2P_PEER_ID_MAX, local_client->base.local_peer_id,
               P2P_PEER_ID_MAX, remote_peer_id);

        // 如果对端已二次确认 SYN0_ACK，则触发 SYN0 推送
        if (remote_s->sync0_acked == 1 && !remote_s->sync0_pending_next) {
            compact_session_send_syn0(remote_s, 0);
            compact_pending_enqueue_syn0(remote_s, 0, local_client->base.last_active);
        }
    }

    // 回复 SYN0_ACK 并加入（一阶段）待确认队列（等待客户端二次确认）
    compact_send_syn0_ack(local_s, remote_peer_id, remote_s != NULL);
    assert(local_s->sync0_acked == 0 && !local_s->sync0_pending_next);
    compact_pending_enqueue_syn0(local_s, 0, local_client->base.last_active);
}

// SIG_PKT_SYN0_ACK（client→server）
// + 方向 2：客户端二次确认收到 SYN0_ACK（session_id 已建立）
// + 方向 3：客户端确认收到服务器 SYN0，停止可靠性重传机制
static void compact_handle_syn0_ack(struct sockaddr_in *from, uint8_t *payload, size_t payload_len) {
    const char* PROTO = "SYN0_ACK";

    if (payload_len < SIG_PKT_SYN0_ACK_C2S_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    compact_session_t *s = (compact_session_t*)find_session(session_id);

    if (s) {
        check_addr_change(COMPACT_CLIENT(s), from);

        // 方向 2：客户端对服务器返回的 SYN0_ACK 的（二次）确认。此时服务器尚未推送（来自对端的）SYN0
        if (s->sync0_acked == 0) {

            s->sync0_acked = 1;

            // 从 SYN0_ACK 待确认队列中移除
            if (s->sync0_pending_next) {
                compact_pending_remove_syn0(s);
            }
            s->sync0_retry = 0;
            s->sync0_sent_time = 0;

            print("V:", LA_F("%s: 2nd-ack confirmed '%s' (ses_id=%u)\n", LA_F26, 26),
                   PROTO, COMPACT_CLIENT(s)->base.local_peer_id, session_id);

            // 二次确认后，若已配对则触发 SYN0 推送
            if (PEER_ONLINE(&s->base) && !s->sync0_pending_next) {
                compact_session_send_syn0(s, 0);
                compact_pending_enqueue_syn0(s, 0, P_tick_ms());
            }
        }
        else {

            // 方向 3：客户端确认了（服务器转发的）来自对端 SYN0
            if (s->sync0_acked == 1) { s->sync0_acked = 2;

                print("V:", LA_F("%s: confirmed '%s', retries=%d (ses_id=%u)\n", LA_F46, 46),
                       PROTO, COMPACT_CLIENT(s)->base.local_peer_id, s->sync0_retry, session_id);
            }

            if (s->sync0_pending_next) {
                compact_pending_remove_syn0(s);
            }

            s->sync0_base_index = 0;
            s->sync0_retry = 0;
            s->sync0_sent_time = 0;

            // 有延期的地址变更通知，立即发送
            if (s->addr_notify_seq != 0) {

                compact_session_send_syn0(s, s->addr_notify_seq);
                compact_pending_enqueue_syn0(s, s->addr_notify_seq, P_tick_ms());

                print("I:", LA_F("Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n", LA_F76, 76),
                      COMPACT_CLIENT(s)->base.local_peer_id, COMPACT_CLIENT(s)->base.local_peer_id,
                      s->base.peer->session_id);
            }
        }
    }
    else print("W:", LA_F("%s for unknown ses_id=%u\n", LA_F16, 16), PROTO, session_id);
}

static void compact_handle_sync_ack(sock_t udp_fd, uint8_t *buf, size_t len,
                                     struct sockaddr_in *from, p2p_packet_hdr_t *hdr,
                                     uint8_t *payload, size_t payload_len) {
    const char* PROTO = "SYNC_ACK";

    if (payload_len < SIG_PKT_SYNC_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    uint16_t ack_seq = ntohs(hdr->seq);
    if (ack_seq > 16) {
        print("E:", LA_F("%s: invalid seq=%u\n", LA_F52, 52), PROTO, ack_seq);
        return;
    }

    compact_session_t *c = (compact_session_t*)find_session(session_id);

    print("V:", LA_F("%s accepted, seq=%u, ses_id=%u\n", LA_F15, 15),
          PROTO, ack_seq, session_id);

    // 如果是客户端确认收到 addr change SYNC 包，则停止可靠性重传机制
    if (ack_seq == 0) {

        if (c) {
            check_addr_change(COMPACT_CLIENT(c), from);

            if (c->sync0_pending_next) {
                compact_pending_remove_syn0(c);
            }

            c->sync0_base_index = 0;
            c->sync0_retry = 0;
            c->sync0_sent_time = 0;

            print("V:", LA_F("%s: addr-notify confirmed '%s' (ses_id=%u)\n", LA_F35, 35),
                   PROTO, COMPACT_CLIENT(c)->base.local_peer_id, session_id);
        }
        else print("W:", LA_F("%s for unknown ses_id=%u\n", LA_F16, 16), PROTO, session_id);
    }
    // ack_seq≠0 的 ACK 是客户端之间的确认，服务器负责 relay 转发
    else {

        if (c && PEER_ONLINE(&c->base)) {
            check_addr_change(COMPACT_CLIENT(c), from);

            nwrite_l((uint8_t *)payload, c->base.peer->session_id);

            compact_client_t* peer_client = COMPACT_CLIENT(COMPACT_PEER(c));
            sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
                   (struct sockaddr *)&peer_client->addr, sizeof(peer_client->addr));

            print("V:", LA_F("Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n", LA_F101, 101),
                   PROTO, ack_seq, COMPACT_CLIENT(c)->base.local_peer_id,
                   c->base.peer->client->local_peer_id, session_id);
        }
        else print("W:", LA_F("Cannot relay %s: ses_id=%u (peer unavailable)\n", LA_F78, 78), PROTO, session_id);
    }
}

// SIG_PKT_SYNC + relay 数据包（P2P_PKT_DATA / ACK / CRYPTO / CONN / CONN_ACK / REACH）
static void compact_handle_relay(sock_t udp_fd, uint8_t *buf, size_t len,
                                  struct sockaddr_in *from, p2p_packet_hdr_t *hdr,
                                  uint8_t *payload, size_t payload_len) {
    const char* PROTO = (hdr->type == P2P_PKT_DATA) ? "RELAY-DATA" :
                       (hdr->type == P2P_PKT_ACK) ? "RELAY-ACK" :
                       (hdr->type == P2P_PKT_CRYPTO) ? "RELAY-CRYPTO" :
                       (hdr->type == SIG_PKT_SYNC) ? "SYNC" :
                       (hdr->type == P2P_PKT_CONN) ? "RELAY-CONN" :
                       (hdr->type == P2P_PKT_CONN_ACK) ? "RELAY-CONN_ACK" : "RELAY-REACH";

    if (payload_len < (int)P2P_SESS_ID_SZ) {
        print("E:", LA_F("[Relay] %s: bad payload(len=%zu)\n", LA_F128, 128), PROTO, payload_len);
        return;
    }

    if (hdr->type == SIG_PKT_SYNC && hdr->seq == 0) {
        char from_str[64];
        snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        print("E:", LA_F("[Relay] %s seq=0 from client %s (server-only, dropped)\n", LA_F126, 126), PROTO, from_str);
        return;
    }

    uint32_t session_id = nget_l(payload);

    compact_session_t *c = (compact_session_t*)find_session(session_id);
    if (!c) {
        print("W:", LA_F("[Relay] %s for unknown ses_id=%u (dropped)\n", LA_F124, 124), PROTO, session_id);
        return;
    }

    if (!PEER_ONLINE(&c->base)) {
        print("W:", LA_F("[Relay] %s for ses_id=%u: peer unavailable (dropped)\n", LA_F123, 123), PROTO, session_id);
        return;
    }

    check_addr_change(COMPACT_CLIENT(c), from);

    print("V:", LA_F("%s accepted, '%s' -> '%s', ses_id=%u\n", LA_F13, 13),
          PROTO, COMPACT_CLIENT(c)->base.local_peer_id, c->base.peer->client->local_peer_id, session_id);

    nwrite_l((uint8_t *)payload, c->base.peer->session_id);

    compact_client_t* peer_client = COMPACT_CLIENT(COMPACT_PEER(c));
    sendto(udp_fd, (const char *)buf, 4 + payload_len, 0,
           (struct sockaddr *)&peer_client->addr, sizeof(peer_client->addr));

    if (hdr->type == SIG_PKT_SYNC || hdr->type == P2P_PKT_REACH ||
        hdr->type == P2P_PKT_DATA || hdr->type == P2P_PKT_CRYPTO) {
        print("V:", LA_F("[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n", LA_F125, 125),
               PROTO, ntohs(hdr->seq), COMPACT_CLIENT(c)->base.local_peer_id,
               c->base.peer->client->local_peer_id, session_id);
    } else {
        print("V:", LA_F("[Relay] %s: '%s' -> '%s' (ses_id=%u)\n", LA_F127, 127),
               PROTO, COMPACT_CLIENT(c)->base.local_peer_id, c->base.peer->client->local_peer_id, session_id);
    }
}

// SIG_PKT_REQ: A→Server
static void compact_handle_req(struct sockaddr_in *from, p2p_packet_hdr_t *hdr,
                                uint8_t *payload, size_t payload_len) {
    const char* PROTO = "REQ";

    if (payload_len < SIG_PKT_REQ_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
        return;
    }

    if (hdr->flags & SIG_FLAG_RELAY) {
        print("E:", LA_F("%s: invalid relay flag from client\n", LA_F161, 161), PROTO);
        return;
    }

    int msg_data_len = (int)(payload_len - SIG_PKT_REQ_MIN_PSZ);
    if (msg_data_len > P2P_MSG_DATA_MAX) {
        print("E:", LA_F("%s: data too large (len=%d)\n", LA_F47, 47), PROTO, msg_data_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + 4);

    if (session_id == 0 || sid == 0) {
        print("E:", LA_F("%s: invalid session_id=%u or sid=%u\n", LA_F53, 53),
               PROTO, session_id, sid);
        return;
    }

    uint8_t msg = payload[6];
    const uint8_t *msg_data = payload + 7;

    compact_session_t *requester = (compact_session_t*)find_session(session_id);
    if (!requester) {
        print("W:", LA_F("%s: requester not found for ses_id=%u\n", LA_F65, 65), PROTO, session_id);
        return;
    }

    print("V:", LA_F("%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n", LA_F33, 33),
           PROTO, session_id, sid, msg, msg_data_len);

    if (!PEER_ONLINE(&requester->base)) {

        const char *peer_id = PEER_VALID(requester->base.peer)
            ? requester->base.peer->client->local_peer_id : "N/A";
        print("W:", LA_F("%s: peer '%s' not online, rejecting sid=%u\n", LA_F62, 62),
               PROTO, peer_id, sid);

        compact_session_send_req_ack(requester, sid, 1);
        return;
    }

    check_addr_change(COMPACT_CLIENT(requester), from);

    if (requester->rpc_pending_next) {

        if (sid == requester->rpc_last_sid) {

            if (!requester->rpc_responding) {

                compact_session_send_req_ack(requester, sid, 0);

                print("V:", LA_F("%s retransmit, resend ACK, sid=%u (ses_id=%u)\n", LA_F22, 22),
                      PROTO, sid, requester->base.session_id);
            }
            else {
                print("V:", LA_F("%s retransmit during RSP phase, ignoring, sid=%u (ses_id=%u)\n", LA_F21, 21),
                      PROTO, sid, requester->base.session_id);
            }
            return;
        }

        if (!uint16_circle_newer(sid, requester->rpc_last_sid)) {
            print("V:", LA_F("%s: obsolete sid=%u (current=%u), ignoring\n", LA_F58, 58),
                  PROTO, sid, requester->rpc_last_sid);
            return;
        }

        print("I:", LA_F("%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n", LA_F20, 20),
              PROTO, sid, requester->rpc_last_sid, requester->rpc_responding, requester->base.session_id);

        compact_pending_remove_rpc(requester);
    }

    if (requester->rpc_last_sid != 0 && !uint16_circle_newer(sid, requester->rpc_last_sid)) {
        print("V:", LA_F("%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n", LA_F59, 59),
              PROTO, sid, requester->rpc_last_sid);
        return;
    }

    requester->rpc_last_sid = sid;
    requester->rpc_responding = false;
    requester->rpc_code = msg;
    requester->rpc_data_len = msg_data_len;
    if (msg_data_len > 0) memcpy(requester->rpc_data, msg_data, msg_data_len);

    compact_session_send_req_ack(requester, sid, 0);

    requester->rpc_sent_time = P_tick_ms();
    requester->rpc_retry = 0;
    compact_pending_enqueue_rpc(requester);
    compact_session_send_req_to_peer(requester);

    print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n", LA_F18, 18),
           PROTO, COMPACT_CLIENT(requester)->base.local_peer_id,
           COMPACT_CLIENT(COMPACT_PEER(requester))->base.local_peer_id,
           sid, msg, requester->base.session_id);
}

// SIG_PKT_RSP: B→Server
static void compact_handle_rsp(struct sockaddr_in *from, uint8_t *payload, size_t payload_len) {
    const char* PROTO = "RSP";

    if (payload_len < SIG_PKT_RSP_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
        return;
    }

    int resp_len = (int)(payload_len - SIG_PKT_RSP_MIN_PSZ);
    if (resp_len > P2P_MSG_DATA_MAX) {
        print("E:", LA_F("%s: data too large (len=%d)\n", LA_F47, 47), PROTO, resp_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + 4);
    if (session_id == 0 || sid == 0) {
        print("E:", LA_F("%s: invalid session_id=%u or sid=%u\n", LA_F53, 53),
              PROTO, session_id, sid);
        return;
    }

    compact_session_t *responder = (compact_session_t*)find_session(session_id);
    if (!responder) {
        print("W:", LA_F("%s: unknown session_id=%u\n", LA_F71, 71), PROTO, session_id);
        return;
    }

    if (!PEER_ONLINE(&responder->base)) {
        print("W:", LA_F("%s: peer '%s' not online for session_id=%u\n", LA_F61, 61),
              PROTO, responder->base.peer->client->local_peer_id, session_id);
        return;
    }

    uint8_t resp_code = payload[6];
    const uint8_t *resp_data = payload + 7;

    print("V:", LA_F("%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n", LA_F32, 32),
          PROTO, session_id, sid, resp_code, resp_len);

    check_addr_change(COMPACT_CLIENT(responder), from);

    compact_session_send_resp_ack(responder, sid);

    compact_session_t *requester = COMPACT_PEER(responder);

    if (!requester->rpc_pending_next || requester->rpc_responding || requester->rpc_last_sid != sid) {
        print("W:", LA_F("%s: no matching pending msg (sid=%u, expected=%u)\n", LA_F57, 57),
              PROTO, sid, requester->rpc_last_sid);
        return;
    }

    compact_pending_remove_rpc(requester);
    compact_transition_to_resp_pending(requester, P_tick_ms(), 0, resp_code, resp_data, resp_len);

    print("I:", LA_F("%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n", LA_F17, 17),
          PROTO, COMPACT_CLIENT(responder)->base.local_peer_id,
          COMPACT_CLIENT(requester)->base.local_peer_id,
          sid, requester->base.session_id);
}

// SIG_PKT_RSP_ACK: A→Server（A 确认收到 RSP）
static void compact_handle_rsp_ack(uint8_t *payload, size_t payload_len) {
    const char* PROTO = "RSP_ACK";

    if (payload_len < SIG_PKT_RSP_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + 4);

    if (session_id == 0 || sid == 0) {
        print("E:", LA_F("%s: invalid session_id=%u or sid=%u\n", LA_F53, 53),
               PROTO, session_id, sid);
        return;
    }

    print("V:", LA_F("%s: accepted, ses_id=%u, sid=%u\n", LA_F34, 34),
           PROTO, session_id, sid);

    compact_session_t *requester = (compact_session_t*)find_session(session_id);
    if (!requester) {
        print("W:", LA_F("%s: unknown session_id=%u\n", LA_F71, 71), PROTO, session_id);
        return;
    }

    if (!requester->rpc_responding || requester->rpc_last_sid != sid) {
        print("V:", LA_F("%s: no matching pending msg (sid=%u)\n", LA_F56, 56), PROTO, sid);
        return;
    }

    compact_pending_remove_rpc(requester);
    requester->rpc_responding = false;
    requester->rpc_retry = 0;

    print("I:", LA_F("%s: RPC complete for '%s', sid=%u (ses_id=%u)\n", LA_F29, 29),
           PROTO, COMPACT_CLIENT(requester)->base.local_peer_id, sid, requester->base.session_id);
}

///////////////////////////////////////////////////////////////////////////////

// 检测地址变更并通知对端（所有已配对 session 均会发出通知）
static bool check_addr_change(compact_client_t *client, const struct sockaddr_in *from) {

    if (memcmp(&client->addr, from, sizeof(*from)) == 0) return false;

    client->addr = *from;

    for (session_t *sbase = client->base.sessions; sbase; sbase = sbase->next) {
        compact_session_t *cs = (compact_session_t*)sbase;
        if (!PEER_ONLINE(&cs->base)) continue;

        compact_session_t *peer = COMPACT_PEER(cs);

        // 如果本端已收到过来自对端的 SYN0
        if (peer->sync0_acked > 1) {
            peer->addr_notify_seq = (uint8_t)(peer->addr_notify_seq + 1);
            if (peer->addr_notify_seq == 0) peer->addr_notify_seq = 1;

            compact_session_send_syn0(peer, peer->addr_notify_seq);
            compact_pending_enqueue_syn0(peer, peer->addr_notify_seq, P_tick_ms());

            print("I:", LA_F("Addr changed for '%s', notifying '%s' (ses_id=%u)\n", LA_F77, 77),
                  client->base.local_peer_id, COMPACT_CLIENT(peer)->base.local_peer_id, peer->base.session_id);
        }
        else if (peer->sync0_acked >= 0) {
            if (peer->addr_notify_seq == 0) peer->addr_notify_seq = 1;

            print("I:", LA_F("Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n", LA_F75, 75),
                  client->base.local_peer_id, peer->base.session_id);
        }
        else {
            print("W:", LA_F("Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n", LA_F74, 74),
                   client->base.local_peer_id, peer->base.session_id);
        }
    }

    return true;
}

// 处理 COMPACT 模式信令（UDP 无状态，对应 p2p_signal_compact 模块）
void compact_handle_signaling(sock_t udp_fd, uint8_t *buf, size_t len, struct sockaddr_in *from) {

    if (len < 4) return;

    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4; size_t payload_len = len - 4;

    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    switch (hdr->type) {
    case SIG_PKT_REG: { const char* PROTO = "REG";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F135, 135),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_REG_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
            return;
        }

        uint32_t instance_id = 0;
        nread_l(&instance_id, payload + P2P_PEER_ID_MAX);
        if (instance_id == 0) {
            print("E:", LA_F("%s: invalid instance_id=0 from %s\n", LA_F50, 50), PROTO, from_str);
            return;
        }

        const char *local_peer_id = (const char *)payload;
        print("V:", LA_F("%s: accepted, local='%.*s', inst_id=%u\n", LA_F30, 30),
               PROTO, P2P_PEER_ID_MAX, local_peer_id, instance_id);

        client_t *client = find_client(local_peer_id);
        if (client) {

            //（协议/instance_id 相同）可靠性重传机制，幂等响应
            if (resident_client(client, PROTO_COMPACT, instance_id, NULL)) {

                check_addr_change((compact_client_t*)client, from);
                compact_send_reg_ack(client, from, ((compact_client_t*)client)->auth_key, instance_id);
                return;
            }
        }
        else {

            client = alloc_client(PROTO_COMPACT, udp_fd);
            if (!client) {
                print("E:", LA_F("%s: alloc client for '%.*s' failed\n", LA_F51, 51), PROTO, P2P_PEER_ID_MAX, local_peer_id);
                return;
            }

            client->instance_id = instance_id;
        }

        // 初始化客户端槽位
        memcpy(client->local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
        compact_init_client((compact_client_t*)client, from);
        identify_client(client);

        compact_send_reg_ack(client, from, ((compact_client_t*)client)->auth_key, instance_id);
        print("V:", LA_F("%s: auth_key=%" PRIu64 " assigned for '%.*s'\n", LA_F36, 36),
               PROTO, ((compact_client_t*)client)->auth_key, P2P_PEER_ID_MAX, local_peer_id);
    } break;

    // SIG_PKT_OFF: [auth_key(SIG_AUTH_KEY_PSZ)]
    case SIG_PKT_OFF: { const char* PROTO = "OFF";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F135, 135),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < (int)SIG_PKT_OFF_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
            return;
        }

        uint64_t off_auth_key = 0;
        nread_ll(&off_auth_key, payload);
        if (off_auth_key == 0) {
            print("E:", LA_F("%s: invalid auth_key=0 from %s\n", LA_F49, 49), PROTO, from_str);
            return;
        }

        compact_client_t *off_client = NULL;
        HASH_FIND(hh, g_clients_by_auth, &off_auth_key, sizeof(uint64_t), off_client);

        if (off_client) {
            print("V:", LA_F("%s: accepted, releasing slot for '%s'\n", LA_F31, 31),
                   PROTO, off_client->base.local_peer_id);
            compact_free_client((client_t*)off_client);
        }
    } break;

    // SIG_PKT_ALIVE: [auth_key(SIG_AUTH_KEY_PSZ)]
    case SIG_PKT_ALV: { const char* PROTO = "ALIVE";

        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F135, 135),
               PROTO, from_str, ntohs(hdr->seq), hdr->flags, len);

        if (payload_len < SIG_PKT_ALV_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%zu)\n", LA_F42, 42), PROTO, payload_len);
            return;
        }

        uint64_t alive_auth_key = nget_ll(payload);

        compact_client_t *alive_client = NULL;
        HASH_FIND(hh, g_clients_by_auth, &alive_auth_key, sizeof(uint64_t), alive_client);
        if (alive_client) {

            print("V:", LA_F("%s accepted, peer='%s', auth_key=%" PRIu64 "\n", LA_F14, 14),
                  PROTO, alive_client->base.local_peer_id, alive_auth_key);

            alive_client->base.last_active = P_tick_ms();
            check_addr_change(alive_client, from);

            {   const char* ACK_PROTO = "ALIVE_ACK";

                uint8_t ack[4];
                p2p_pkt_hdr_encode(ack, SIG_PKT_ALV_ACK, 0, 0);

                print("V:", LA_F("Send %s: auth_key=%" PRIu64 ", peer='%s'\n", LA_F108, 108),
                      ACK_PROTO, alive_auth_key, alive_client->base.local_peer_id);

                udp_send(alive_client->base.fd, ack, (int)sizeof(ack), from, ACK_PROTO);
            }
        } else {
            print("W:", LA_F("%s: unknown auth_key=%" PRIu64 " from %s\n", LA_F70, 70), PROTO, alive_auth_key, from_str);
        }
    } break;

    case SIG_PKT_SYN0:
        printf(LA_F("[UDP] SYN0 recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F189, 189),
               from_str, ntohs(hdr->seq), hdr->flags, len);
        compact_handle_syn0(from, payload, payload_len);
        break;

    case SIG_PKT_SYN0_ACK:
        printf(LA_F("[UDP] SYN0_ACK recv from %s, len=%zu\n", LA_F134, 134),
               from_str, len);
        compact_handle_syn0_ack(from, payload, payload_len);
        break;

    case SIG_PKT_SYNC_ACK:
        printf(LA_F("[UDP] SYNC_ACK recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F190, 190),
               from_str, ntohs(hdr->seq), hdr->flags, len);
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
            print("E:", LA_F("[Relay] %s: missing SESSION flag, dropped\n", LA_F129, 129),
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
        printf(LA_F("[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F135, 135),
               _rp, from_str, ntohs(hdr->seq), hdr->flags, len);
        compact_handle_relay(udp_fd, buf, len, from, hdr, payload, payload_len);
    } break;

    case SIG_PKT_REQ:
        printf(LA_F("[UDP] REQ recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F186, 186),
               from_str, ntohs(hdr->seq), hdr->flags, len);
        compact_handle_req(from, hdr, payload, payload_len);
        break;

    case SIG_PKT_RSP:
        printf(LA_F("[UDP] RSP recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F187, 187),
               from_str, ntohs(hdr->seq), hdr->flags, len);
        compact_handle_rsp(from, payload, payload_len);
        break;

    case SIG_PKT_RSP_ACK:
        printf(LA_F("[UDP] RSP_ACK recv from %s, seq=%u, flags=0x%02x, len=%zu\n", LA_F188, 188),
               from_str, ntohs(hdr->seq), hdr->flags, len);
        compact_handle_rsp_ack(payload, payload_len);
        break;

    default:
        print("W:", LA_F("Unknown packet type 0x%02x from %s\n", LA_F122, 122), hdr->type, from_str);
        break;
    } // switch
}

// 检查并重传 RPC（统一处理 REQ 和 RSP 阶段）
void compact_retry_pending(sock_t udp_fd, uint64_t now) { (void)udp_fd;

    while (g_sync0_pending_head) {

        if (tick_diff(now, g_sync0_pending_head->sync0_sent_time) < SYN0_RETRY_INTERVAL_MS) {
            return;
        }

        compact_session_t *q = g_sync0_pending_head;
        g_sync0_pending_head = q->sync0_pending_next;

        if (q->sync0_retry >= SYN0_MAX_RETRY) {

            const char *remote_id = PEER_ONLINE(&q->base) ? q->base.peer->client->local_peer_id
                                    : (q->base.pair->sessions[0] == &q->base ? q->base.pair->peer_id[1]
                                                                              : q->base.pair->peer_id[0]);
            print("W:", LA_F("SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n", LA_F104, 104),
                   q->base.client->local_peer_id, remote_id, q->sync0_retry);

            q->sync0_pending_next = NULL;
            if (q->sync0_base_index == 0) {
                q->sync0_acked = -1/* 超时停止 */;
            }

            if (g_sync0_pending_head == (compact_session_t*)(void*)-1) {
                g_sync0_pending_head = NULL;
                g_sync0_pending_rear = NULL;
                return;
            }
        }
        else {

            // 状态 0：重传 SYN0_ACK，等待客户端二次确认
            if (q->sync0_acked == 0) {
                // 从 pair 获取 remote_peer_id（避免 peer 为 NULL 时的解引用）
                const char *remote_id = PEER_ONLINE(&q->base) ? q->base.peer->client->local_peer_id
                                        : (q->base.pair->sessions[0] == &q->base ? q->base.pair->peer_id[1]
                                                                                  : q->base.pair->peer_id[0]);
                compact_send_syn0_ack(q, remote_id, PEER_ONLINE(&q->base));
            }
            // 状态 1：重传 SYN0，等待客户端确认收到该（来自对端的）SYN0
            else {

                // 如果对端已经不在线了，就不必重传了
                if (!PEER_ONLINE(&q->base)) {
                    q->sync0_pending_next = NULL;
                    if (g_sync0_pending_head == (compact_session_t*)(void*)-1) {
                        g_sync0_pending_head = NULL;
                        g_sync0_pending_rear = NULL;
                        return; // sync0_pending 链表已空，直接返回
                    }
                    continue;
                }
                compact_session_send_syn0(q, q->sync0_base_index);
            }

            q->sync0_retry++;
            q->sync0_sent_time = now;

            if (g_sync0_pending_head == (compact_session_t*)(void*)-1) {
                g_sync0_pending_head = q;
            } else {
                q->sync0_pending_next = (compact_session_t*)(void*)-1;
                g_sync0_pending_rear->sync0_pending_next = q;
                g_sync0_pending_rear = q;
            }

            print("V:", LA_F("SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n", LA_F103, 103),
                   COMPACT_CLIENT(q)->base.local_peer_id,
                   PEER_ONLINE(&q->base) ? q->base.peer->client->local_peer_id
                                         : (q->base.pair->sessions[0] == &q->base ? q->base.pair->peer_id[1]
                                                                                   : q->base.pair->peer_id[0]),
                   q->sync0_retry, SYN0_MAX_RETRY, q->base.session_id);

            if (g_sync0_pending_head == q) return;
        }
    }

    while (g_rpc_pending_head) {

        if (tick_diff(now, g_rpc_pending_head->rpc_sent_time) < RPC_RETRY_INTERVAL_MS) {
            return;
        }

        compact_session_t *q = g_rpc_pending_head;
        g_rpc_pending_head = q->rpc_pending_next;
        if (g_rpc_pending_head == (compact_session_t*)(void*)-1) {
            g_rpc_pending_head = NULL;
            g_rpc_pending_rear = NULL;
        }

        if (!q->rpc_responding) {

            if (!PEER_ONLINE(&q->base)) {
                print("W:", LA_F("REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n", LA_F86, 86),
                      COMPACT_CLIENT(q)->base.local_peer_id, q->rpc_last_sid, q->base.session_id);

                compact_transition_to_resp_pending(q, now, SIG_RPC_FLAG_PEER_OFF, 0, NULL, 0);
            }
            else if (q->rpc_retry >= REQ_MAX_RETRY) {
                print("W:", LA_F("REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n", LA_F85, 85),
                      q->rpc_retry, COMPACT_CLIENT(q)->base.local_peer_id, q->rpc_last_sid, q->base.session_id);

                compact_transition_to_resp_pending(q, now, SIG_RPC_FLAG_TIMEOUT, 0, NULL, 0);
            }
            else {
                compact_session_send_req_to_peer(q);
                q->rpc_retry++;
                q->rpc_sent_time = now;
                compact_pending_enqueue_rpc(q);

                print("V:", LA_F("REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n", LA_F87, 87),
                      COMPACT_CLIENT(q)->base.local_peer_id, COMPACT_CLIENT(q->base.peer)->base.local_peer_id,
                      q->rpc_last_sid, q->rpc_retry, REQ_MAX_RETRY, q->base.session_id);

                if (g_rpc_pending_head == q) return;
            }
        }
        else {

            if (q->rpc_retry >= RSP_MAX_RETRY) {
                print("W:", LA_F("RSP gave up after %d retries, sid=%u (ses_id=%u)\n", LA_F88, 88),
                      q->rpc_retry, q->rpc_last_sid, q->base.session_id);

                q->rpc_pending_next = NULL;
                q->rpc_responding = false;
                q->rpc_retry = 0;
            }
            else {
                q->rpc_retry++;
                compact_send_msg_resp_to_requester(q);
                q->rpc_sent_time = now;
                compact_pending_enqueue_rpc(q);

                print("V:", LA_F("RSP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n", LA_F89, 89),
                      COMPACT_CLIENT(q)->base.local_peer_id, q->rpc_last_sid, q->rpc_retry, RSP_MAX_RETRY, q->base.session_id);

                if (g_rpc_pending_head == q) return;
            }
        }

        if (!g_rpc_pending_head) return;
    }
}

///////////////////////////////////////////////////////////////////////////////
