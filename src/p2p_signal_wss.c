/*
 * WSS 模式信令实现（WebSocket 文本协议）
 *
 * ============================================================================
 * 设计理念：完全对齐 RELAY
 * ============================================================================
 *
 * 两阶段分离：
 *   阶段1 (REG):  建立"客户端-服务器"的基础连接
 *   阶段2 (SYN0): 建立"我-对方"的会话
 *
 * 与 RELAY 的核心区别：
 *   - 传输: WebSocket 文本帧 vs TCP 二进制流
 *   - 分帧: WS 自带消息边界 vs RELAY 需要 size 头处理粘包
 *   - 协议: 文本命令（如 "REG alice 12345"）vs 二进制类型码
 *   - 数据中继: WS 二进制帧 vs RELAY 二进制包封装
 *
 * 协议详见 p2pp.h（WSS 模式协议）
 */

#ifdef WITH_WS

#define MOD_TAG "WSS"

#include "p2p_signal_wss.h"
#include "p2p_internal.h"
#include "p2p_ice.h"
#include "ws_client.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TASK_REG                    "REG"
#define TASK_TOUCH                  "TOUCH"
#define TASK_SYNC                   "SYNC"
#define TASK_SYNC_REMOTE            "SYNC REMOTE"
#define TASK_PKT                    "PKT"
#define TASK_RPC                    "RPC"

/* 一个 SYNC 包所承载的候选数量（单位）*/
#define SYNC_CAND_UNIT \
    (P2P_WSS_MAX_CANDS_PER_SYNC < inst->sig_ctx.wss.sync_max / (int)sizeof(p2p_candidate_t) \
     ? P2P_WSS_MAX_CANDS_PER_SYNC \
     : inst->sig_ctx.wss.sync_max / (int)sizeof(p2p_candidate_t))

///////////////////////////////////////////////////////////////////////////////
// 辅助函数
///////////////////////////////////////////////////////////////////////////////

/*
 * 将 session_id 格式化为 8 位 16 进制字符串
 * 示例: 0x1A2B3C4D → "1A2B3C4D"
 */
static void format_session_id(uint32_t session_id, char *out) {
    snprintf(out, 9, "%08X", session_id);
}

/*
 * 从 8 位 16 进制字符串解析 session_id
 */
static uint32_t parse_session_id(const char *hex) {
    return (uint32_t)strtoul(hex, NULL, 16);
}

/*
 * 从 2 位 16 进制字符串解析 sid
 */
static uint8_t parse_sid(const char *hex) {
    return (uint8_t)strtoul(hex, NULL, 16);
}

/*
 * 发送 WebSocket 文本消息
 */
static ret_t ws_send_text(p2p_wss_ctx_t *ctx, const char *msg, const char *proto) {
    if (!ctx->ws || ctx->state < SIG_WSS_CONNECTING) {
        print("E:", LA_F("[W] %s send failed: WS not ready\n", LA_F469, 469), proto);
        return E_NONE_CONTEXT;  /* 不具备可运行的状态 */
    }

    if (ws_client_send_text((ws_client_t *)ctx->ws, msg) != 0) {
        print("E:", LA_F("[W] %s send failed\n", LA_F469, 469), proto);
        return E_UNKNOWN;
    }

    ctx->last_send_time = P_tick_ms();
    print("V:", LA_F("[W] %s sent: %s\n", LA_F469, 469), proto, msg);
    return E_NONE;
}

/*
 * 发送 WebSocket 二进制消息
 */
static ret_t ws_send_binary(p2p_wss_ctx_t *ctx, const void *data, size_t len, const char *proto) {
    if (!ctx->ws || ctx->state < SIG_WSS_CONNECTING) {
        print("E:", LA_F("[W] %s send failed: WS not ready\n", LA_F469, 469), proto);
        return E_NONE_CONTEXT;  /* 不具备可运行的状态 */
    }

    if (ws_client_send_binary((ws_client_t *)ctx->ws, (const uint8_t *)data, len) != 0) {
        print("E:", LA_F("[W] %s send failed\n", LA_F469, 469), proto);
        return E_UNKNOWN;
    }

    ctx->last_send_time = P_tick_ms();
    print("V:", LA_F("[W] %s sent (%zu bytes)\n", LA_F469, 469), proto, len);
    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// 协议发送函数
///////////////////////////////////////////////////////////////////////////////

/*
 * 发送 REG 消息
 *
 * 格式: "REG <peer_id> <instance_id>\n"
 */
static void send_reg(struct p2p_instance *inst, uint64_t now) {
    const char *PROTO = "REG";
    (void)now;

    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;

    char msg[256];
    snprintf(msg, sizeof(msg), P2P_WSS_CMD_REG_FMT,
             ctx->local_peer_id, ctx->instance_id);

    if (ws_send_text(ctx, msg, PROTO) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, name='%s' inst=%u\n", LA_F60, 60),
          PROTO, ctx->local_peer_id, ctx->instance_id);
}

/*
 * 发送 OFF 消息
 *
 * 格式: "OFF\n"
 */
static void send_off(struct p2p_instance *inst) {
    const char *PROTO = "OFF";

    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    ws_send_text(ctx, P2P_WSS_CMD_OFF_MSG, PROTO);

    print("V:", LA_F("%s sent\n", LA_F65, 65), PROTO);
}

/*
 * 发送 SYN0 消息
 *
 * 格式: "SYN0 <remote_peer_id>\n"
 *       "SYN0 <remote_peer_id>\n<payload>\n"  (可选预缓存负载)
 */
static void send_syn0(struct p2p_instance *inst, struct p2p_session *s, uint64_t now) {
    const char *PROTO = "SYN0";
    (void)now;

    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    /* SYN0 不携带候选，所有候选通过 SYNC 发送并等待 confirm（与 relay 模式一致）*/
    char msg[256];
    snprintf(msg, sizeof(msg), "SYN0 %s\n", sess_ctx->remote_peer_id);

    if (ws_send_text(ctx, msg, PROTO) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent (no candidates) to '%s'\n", LA_F60, 60),
          PROTO, sess_ctx->remote_peer_id);
}

/*
 * 发送 SYNC 消息（候选批次）
 *
 * 格式: "SYNC <session_id_hex> <sid_hex>\n<payload>\n"
 */
static void send_sync(struct p2p_instance *inst, struct p2p_session *s, uint64_t now) {
    const char *PROTO = "SYNC";
    (void)now;

    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    /* 计算待发送候选 */
    int base = sess_ctx->candidate_syncing_base;
    int total = s->local_cand_cnt;
    if (base >= total) return;  /* 无新候选 */

    int count = (total - base) > SYNC_CAND_UNIT ? SYNC_CAND_UNIT : (total - base);

    /* 构造 ICE 候选文本 */
    char payload[4096];
    int payload_len = p2p_ice_export_sdp(&s->local_cands[base], count,
                                          payload, (int)sizeof(payload),
                                          true, NULL, NULL, NULL);
    if (payload_len <= 0) {
        print("E:", LA_F("%s: export ICE candidates failed\n", LA_F119, 119), PROTO);
        return;
    }

    /* sid 递增（循环 1..255，跳过 0）*/
    sess_ctx->sync_sid++;
    if (sess_ctx->sync_sid == 0) sess_ctx->sync_sid = 1;

    /* 构造完整消息 */
    char session_id_hex[9];
    format_session_id(s->id, session_id_hex);

    char msg[5120];
    snprintf(msg, sizeof(msg), "SYNC %s %02X\nICE\n%s",
             session_id_hex, sess_ctx->sync_sid, payload);

    if (ws_send_text(ctx, msg, PROTO) != E_NONE) {
        return;
    }

    /* 更新同步状态：设置 syncing_base，但不更新 synced_count（等待 confirm）*/
    sess_ctx->candidate_syncing_base = base + count;

    print("V:", LA_F("%s sent sid=%02X, cands[%d..%d] to ses_id=%u\n", LA_F469, 469),
          PROTO, sess_ctx->sync_sid, base, base + count - 1, s->id);
}

/*
 * 发送候选发送完成标记（空 payload）
 *
 * 格式: "SYNC <session_id_hex> <sid_hex>\n\n" （空行标记，类似 HTTP）
 */
static void send_ice_done(struct p2p_instance *inst, struct p2p_session *s) {
    const char *PROTO = "SYNC(DONE)";

    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    /* sid 递增 */
    sess_ctx->sync_sid++;
    if (sess_ctx->sync_sid == 0) sess_ctx->sync_sid = 1;

    char session_id_hex[9];
    format_session_id(s->id, session_id_hex);

    char msg[256];
    snprintf(msg, sizeof(msg), "SYNC %s %02X\n\n",
             session_id_hex, sess_ctx->sync_sid);

    if (ws_send_text(ctx, msg, PROTO) != E_NONE) {
        return;
    }

    sess_ctx->state = SIG_WSS_SESS_READY;
    print("V:", LA_F("%s sent, ses_id=%u\n", LA_F469, 469), PROTO, s->id);
}

/*
 * 发送 FIN 消息
 *
 * 格式: "FIN <session_id_hex>\n"
 */
static void send_fin(struct p2p_session *s) {
    const char *PROTO = "FIN";

    char session_id_hex[9];
    format_session_id(s->id, session_id_hex);

    char msg[64];
    snprintf(msg, sizeof(msg), "FIN %s\n", session_id_hex);

    if (ws_send_text(&s->inst->sig_ctx.wss, msg, PROTO) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, ses_id=%u\n", LA_F67, 67), PROTO, s->id);
}

///////////////////////////////////////////////////////////////////////////////
// 协议接收处理函数
///////////////////////////////////////////////////////////////////////////////

/*
 * 解析远程 ICE 候选并追加到 session
 */
static void unpack_remote_candidates_from_sdp(struct p2p_session *s,
                                               const char *sdp_text) {
    char _ab[INET6_ADDRSTRLEN];

    /* 提取 ICE 凭证 */
    const char *p;
    if ((p = strstr(sdp_text, "a=ice-ufrag:")) != NULL) {
        p += 12;
        int i = 0;
        while (*p && *p != '\r' && *p != '\n' && i < (int)sizeof(s->ice_remote_ufrag) - 1)
            s->ice_remote_ufrag[i++] = *p++;
        s->ice_remote_ufrag[i] = '\0';
    }
    if ((p = strstr(sdp_text, "a=ice-pwd:")) != NULL) {
        p += 10;
        int i = 0;
        while (*p && *p != '\r' && *p != '\n' && i < (int)sizeof(s->ice_remote_pwd) - 1)
            s->ice_remote_pwd[i++] = *p++;
        s->ice_remote_pwd[i] = '\0';
    }

    /* 解析候选 */
    p2p_remote_candidate_entry_t tmp_cands[32];
    int parsed = p2p_ice_import_sdp(sdp_text, tmp_cands, 32);
    if (parsed <= 0) {
        print("W:", LA_F("%s: import SDP failed or empty\n", LA_F134, 134), TASK_SYNC_REMOTE);
        return;
    }

    int added = 0;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    for (int i = 0; i < parsed; i++) {
        /* 检查重复 */
        if (p2p_find_remote_candidate_by_addr(s, &tmp_cands[i].addr) >= 0) {
            continue;
        }

        /* IPv4/IPv6 过滤 */
        if ((tmp_cands[i].addr.family == AF_INET  && s->inst->cfg.test_ice_ipv4_off) ||
            (tmp_cands[i].addr.family == AF_INET6 && s->inst->cfg.test_ice_ipv6_off)) {
            continue;
        }

        /* 候选类型过滤 */
        bool opt_off = false;
        if (tmp_cands[i].type == P2P_CAND_HOST && s->inst->cfg.test_ice_host_off) {
            opt_off = true;
        } else if (tmp_cands[i].type == P2P_CAND_SRFLX && s->inst->cfg.test_ice_srflx_off) {
            opt_off = true;
        } else if (tmp_cands[i].type == P2P_CAND_RELAY && s->inst->cfg.test_ice_relay_off) {
            opt_off = true;
        }
        if (opt_off) {
            continue;
        }

        int idx = p2p_cand_push_remote(s);
        if (idx < 0) break;

        s->remote_cands[idx] = tmp_cands[i];
        added++;

        p2p_remote_candidate_entry_t *c = &s->remote_cands[idx];
        print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F222, 222),
              TASK_SYNC_REMOTE,
              c->type == P2P_CAND_HOST ? "host" : c->type == P2P_CAND_SRFLX ? "srflx" : "relay",
              idx, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr));

        /* 启动打洞 */
        if (sess_ctx->state >= SIG_WSS_SESS_SYNCING && nat_punch(s, idx) != E_NONE) {
            print("E:", LA_F("%s: punch remote cand[%d] failed\n", LA_F199, 199),
                  TASK_SYNC_REMOTE, idx);
        }
    }

    if (added > 0) {
        print("I:", LA_F("%s: added %d remote candidates\n", LA_F222, 222),
              TASK_SYNC_REMOTE, added);
    }
}

/*
 * 处理 REG OK 响应
 *
 * 格式: "REG OK <sync_max> <features>\n"
 */
static void handle_reg_ok(struct p2p_instance *inst, const char *msg) {
    const char *PROTO = "REG_OK";

    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;

    int sync_max = 0, features = 0;
    if (sscanf(msg + strlen(P2P_WSS_RSP_REG_OK), "%d %d", &sync_max, &features) != 2) {
        print("E:", LA_F("%s: parse failed\n", LA_F208, 208), PROTO);
        ctx->state = SIG_WSS_ERROR;
        return;
    }

    ctx->sync_max = sync_max;
    ctx->feature_relay = (features & P2P_RLY_FEATURE_RELAY) != 0;
    ctx->feature_msg   = (features & P2P_RLY_FEATURE_MSG) != 0;
    ctx->state = SIG_WSS_REG;

    print("I:", LA_F("%s: sync_max=%d, relay=%s, msg=%s\n", LA_F208, 208),
          PROTO, sync_max,
          ctx->feature_relay ? "yes" : "no",
          ctx->feature_msg ? "yes" : "no");

    /* 如果服务器支持数据中继，启用信令转发路径 */
    if (ctx->feature_relay) {
        inst->signaling_relay_fn = p2p_signal_wss_pkt;
        path_manager_enable_signaling(inst, &ctx->server_addr);
        print("I:", LA_F("%s: SIGNALING path enabled (server supports relay)\n", LA_F208, 208), PROTO);
    }

    /* 触发所有等待 REG 的会话 */
    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;
        if (sess_ctx->state == SIG_WSS_SESS_WAIT_REG) {
            sess_ctx->state = SIG_WSS_SESS_WAIT_SYN0_ACK;
            send_syn0(inst, s, P_tick_ms());
        }
    }
}

/*
 * 处理 SYN0 响应（服务器 → 客户端）
 *
 * 格式: "SYN0 <peer_id> <session_id_hex> online\n[<payload>\n]"
 *       "SYN0 <peer_id> <session_id_hex> offline\n"
 */
static void handle_syn0_from_server(struct p2p_instance *inst, const char *msg) {
    const char *PROTO = "SYN0";

    char peer_id[P2P_PEER_ID_MAX];
    char session_id_hex[9];
    char status[16];

    if (sscanf(msg + strlen(P2P_WSS_RSP_SYN0), "%31s %8s %15s",
               peer_id, session_id_hex, status) != 3) {
        print("E:", LA_F("%s: parse failed\n", LA_F208, 208), PROTO);
        return;
    }

    uint32_t session_id = parse_session_id(session_id_hex);
    bool online = (strcmp(status, "online") == 0);

    /* 查找会话 */
    struct p2p_session *s = NULL;
    for (s = inst->sessions_head; s; s = s->next) {
        if (strcmp(s->remote_peer_id, peer_id) == 0) break;
    }
    if (!s) {
        print("W:", LA_F("%s: session not found for peer '%s'\n", LA_F223, 223),
              PROTO, peer_id);
        return;
    }

    /* 分配 session_id */
    if (s->id == 0) {
        s->id = session_id;
        print("I:", LA_F("%s: assigned session_id=%08X for peer '%s'\n", LA_F208, 208),
              PROTO, session_id, peer_id);
    }

    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (online) {
        /* 对端在线，可能携带候选负载 */
        const char *payload_start = strstr(msg, "\n");
        bool remote_done = false;  /* 本次收到完成标记 */
        if (payload_start) {
            payload_start++;  /* 跳过 '\n' */
            if (strncmp(payload_start, "SDP\n", 4) == 0) {
                unpack_remote_candidates_from_sdp(s, payload_start + 4);
            } else if (strncmp(payload_start, "ICE\n", 4) == 0) {
                /* ICE 批量候选（与 SDP 格式相同）*/
                unpack_remote_candidates_from_sdp(s, payload_start + 4);
            } else if (*payload_start == '\n' || *payload_start == '\0') {
                /* 空 payload 表示候选发送完成 */
                s->remote_cand_done = true;
                remote_done = true;
                print("I:", LA_F("%s: remote '%s' candidates done (empty payload)\n", LA_F222, 222),
                      PROTO, peer_id);
            }
        }

        /* 重置候选同步状态，触发重新发送（包括完成标记）*/
        sess_ctx->candidate_syncing_base = 0;
        sess_ctx->candidate_synced_count = 0;
        /* 只在未收到完成标记时重置 remote_cand_done，等待后续 SYNC 消息携带完成标记 */
        if (!remote_done) {
            s->remote_cand_done = false;
        }

        /* 首次确认双方在线，启动 NAT 打洞（对齐 RELAY）*/
        nat_punch(s, -1/* all candidates */);

        uint64_t now = P_tick_ms();

        /* 如果需要等待 STUN 收集完成再同步 */
        if (P2P_SESSION_WAITING_STUN(s)) {
            sess_ctx->state = SIG_WSS_SESS_WAIT_STUN;
            print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", LA_F239, 239),
                  TASK_TOUCH, "WAIT_STUN", "sync0", LA_S("waiting stun pending", LA_S30, 30));
        }
        /* 否则直接进入 SYNCING 状态，开始上传候选 */
        else {
            sess_ctx->state = SIG_WSS_SESS_SYNCING;
            print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", LA_F239, 239),
                  TASK_TOUCH, "SYNCING", "sync0", LA_S("sync candidates", LA_S28, 28));

            /* 收到对端 SYN0 隐含确认本端首批候选已转发（对齐 RELAY handle_syn0_ack line 675）*/
            sess_ctx->candidate_synced_count = sess_ctx->candidate_syncing_base;

            /* SYN0 携带的候选可能尚未被 SYNC_ACK 确认 */
            if (sess_ctx->candidate_syncing_base < s->local_cand_cnt || !P2P_CAND_PENDING(inst))
                send_sync(inst, s, now);
            else { sess_ctx->trickle_last_time = now; }
        }
    } else {
        /* 对端离线 */
        sess_ctx->state = SIG_WSS_SESS_WAIT_PEER;
        print("I:", LA_F("%s: peer '%s' offline, waiting\n", LA_F208, 208),
              PROTO, peer_id);
    }
}

/*
 * 处理 SYNC 消息（服务器 → 客户端）
 *
 * 格式: "SYNC <session_id_hex> <sid_hex>\n<payload>\n"
 *       "SYNC <session_id_hex> <sid_hex> confirm\n"
 */
static void handle_sync_from_server(struct p2p_instance *inst, const char *msg) {
    const char *PROTO = "SYNC";

    char session_id_hex[9];
    char sid_hex[3];
    char action[32] = {0};

    /* 尝试解析 "SYNC <session_id> <sid> confirm|busy" */
    int parsed = sscanf(msg + strlen("SYNC "), "%8s %2s %31s",
                        session_id_hex, sid_hex, action);
    if (parsed < 2) {
        print("E:", LA_F("%s: parse header failed\n", LA_F208, 208), PROTO);
        return;
    }

    uint32_t session_id = parse_session_id(session_id_hex);
    uint8_t sid = parse_sid(sid_hex);

    /* 查找会话 */
    struct p2p_session *s = inst->sessions_head;
    for (; s; s = s->next) {
        if (s->id == session_id) break;
    }
    if (!s) {
        print("W:", LA_F("%s: session %08X not found\n", LA_F223, 223),
              PROTO, session_id);
        return;
    }

    /* 处理 confirm/busy */
    if (parsed >= 3 && action[0]) {
        if (strcmp(action, "confirm") == 0) {
            print("V:", LA_F("%s: sid=%02X confirmed\n", LA_F469, 469), PROTO, sid);
            /* 服务器确认收到我们的 SYNC，更新 synced_count */
            p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;
            sess_ctx->sync_sid_confirmed = sid;
            sess_ctx->candidate_synced_count = sess_ctx->candidate_syncing_base;
            print("V:", LA_F("%s: synced=%d base=%d\n", LA_F469, 469), 
                  PROTO, sess_ctx->candidate_synced_count, sess_ctx->candidate_syncing_base);
            return;
        } else if (strcmp(action, "busy") == 0) {
            print("W:", LA_F("%s: sid=%02X busy\n", LA_F223, 223), PROTO, sid);
            /* 服务器缓存满，稍后重试 */
            return;
        }
    }

    /* 解析负载（候选数据）*/
    const char *payload_start = strstr(msg, "\n");
    if (!payload_start) {
        print("E:", LA_F("%s: no payload\n", LA_F208, 208), PROTO);
        return;
    }
    payload_start++;  /* 跳过 '\n' */

    /* 空 payload（空行标记）表示候选发送完成 */
    if (*payload_start == '\n' || *payload_start == '\0') {
        s->remote_cand_done = true;
        print("I:", LA_F("%s: remote candidates done (empty payload)\n", LA_F222, 222), PROTO);
    }
    else if (strncmp(payload_start, "SDP\n", 4) == 0) {
        unpack_remote_candidates_from_sdp(s, payload_start + 4);
    } else if (strncmp(payload_start, "ICE\n", 4) == 0) {
        /* ICE 批量候选（与 SDP 格式相同）*/
        unpack_remote_candidates_from_sdp(s, payload_start + 4);
    }

    print("V:", LA_F("%s: received sid=%02X from ses_id=%08X\n", LA_F469, 469),
          PROTO, sid, session_id);

    /* 发送 confirm 消息给服务器，释放服务器端队列 */
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    char confirm_buf[64];
    int confirm_len = snprintf(confirm_buf, sizeof(confirm_buf),
                               "SYNC %08X %02X confirm\n",
                               session_id, sid);
    if (confirm_len > 0 && confirm_len < (int)sizeof(confirm_buf)) {
        ws_send_text(ctx, confirm_buf, "SYNC_CONFIRM");
        print("V:", LA_F("%s: sent confirm for sid=%02X\n", LA_F469, 469), PROTO, sid);
    }
}

/*
 * 处理 FIN 消息
 *
 * 格式: "FIN <session_id_hex>\n"
 */
static void handle_fin(struct p2p_instance *inst, const char *msg) {
    const char *PROTO = "FIN";

    char session_id_hex[9];
    if (sscanf(msg + strlen("FIN "), "%8s", session_id_hex) != 1) {
        print("E:", LA_F("%s: parse failed\n", LA_F208, 208), PROTO);
        return;
    }

    uint32_t session_id = parse_session_id(session_id_hex);
    
    /* 调试：打印接收到的原始数据 */
    print("V:", LA_F("%s: received hex='%s', parsed_id=%08X\n", LA_F240, 240),
          PROTO, session_id_hex, session_id);
    
    struct p2p_session *s = inst->sessions_head;
    for (; s; s = s->next) {
        if (s->id == session_id) break;
    }
    if (!s) {
        print("W:", LA_F("%s: session %08X not found\n", LA_F223, 223),
              PROTO, session_id);
        return;
    }

    print("I:", LA_F("%s: peer disconnected, ses_id=%08X\n", LA_F208, 208),
          PROTO, session_id);

    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    // 清理会话状态，回到 WAIT_PEER 被动等待（对端主动断开，不自动重连）
    sess_ctx->state = SIG_WSS_SESS_WAIT_PEER;
    // 注意：不在这里设置 s->id = 0，避免后续 peer_disconnect 发送 FIN 时 session_id 为 0

    // 重置候选同步状态
    sess_ctx->candidate_synced_count = 0;
    s->remote_cand_done = false;

    // 如果还在打洞阶段
    if (s->state < P2P_STATE_LOST) {
        s->state = P2P_STATE_LOST;
    }

    // 触发 NAT 层断开，让 p2p_update 走正常的 peer_disconnect 路径
    // WSS FIN 经可靠传输，等同于 NAT FIN
    if (s->nat.state > NAT_CLOSED) {
        s->nat.state = NAT_CLOSED;
    }
}

/*
 * 处理实例级 STATUS（对齐 RELAY handle_status）
 *
 * 用于客户端级别的错误反馈（REG, OFF 等）
 */
static void handle_status(struct p2p_instance *inst, const char *req_type, 
                         uint8_t code, const char *msg) {
    const char *PROTO = "STATUS";

    if (msg && msg[0])
        print("E:", LA_F("%s: req_type=%s code=%u msg=%s\n", LA_F225, 225),
              PROTO, req_type, (unsigned)code, msg);
    else
        print("E:", LA_F("%s: req_type=%s code=%u\n", LA_F226, 226),
              PROTO, req_type, (unsigned)code);

    /* 客户端级错误：关闭 WebSocket 连接 */
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    if (ctx->ws) {
        ws_client_close(ctx->ws, 1000);  /* 1000 = 正常关闭 */
        ctx->ws = NULL;
    }
    ctx->state = SIG_WSS_ERROR;
}

/*
 * 处理会话级 STATUS（对齐 RELAY handle_session_status）
 *
 * 用于会话级别的流控和错误反馈
 */
static void handle_session_status(struct p2p_session *s, const char *req_type,
                                  uint8_t code, const char *msg) {
    const char *PROTO = "STATUS";
    struct p2p_instance *inst = s->inst;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    const char *lvl = (code == P2P_CODE_READY) ? "V:" : "W:";
    if (msg && msg[0])
        print(lvl, LA_F("%s: sess_id=%u req_type=%s code=%u msg=%s\n", LA_F237, 237),
              PROTO, s->id, req_type, (unsigned)code, msg);
    else
        print(lvl, LA_F("%s: sess_id=%u req_type=%s code=%u\n", LA_F238, 238),
              PROTO, s->id, req_type, (unsigned)code);

    /* 会话忙：服务器转发缓冲区满，稍后重试 */
    if (code == P2P_ERR_BUSY) {
        if (strcmp(req_type, "SYNC") == 0 || strcmp(req_type, "SYN0") == 0) {
            /* 重置 trickle 时间，稍后重试 */
            if (sess_ctx->trickle_last_time) 
                sess_ctx->trickle_last_time = P_tick_ms();
            print("V:", LA_F("%s: sync busy, will retry\n", LA_F249, 249), PROTO);
        }
        else if (strcmp(req_type, "PKT") == 0) {
            /* relay 流控：保持等待，延迟重试 */
            sess_ctx->awaiting_relay_ready = true;
            print("V:", LA_F("%s: relay busy, will retry\n", LA_F218, 218), PROTO);
        }
    }
    /* 服务就绪：解除对应流控 */
    else if (code == P2P_CODE_READY) {
        if (strcmp(req_type, "PKT") == 0) {
            sess_ctx->awaiting_relay_ready = false;
            print("V:", LA_F("%s: relay ready, flow control released\n", LA_F219, 219), PROTO);
        }
    }
    /* 对端离线 */
    else if (code == P2P_ERR_PEER_OFF) {
        print("W:", LA_F("%s: peer offline\n", LA_F188, 188), PROTO);

        p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
        if (ctx->state >= SIG_WSS_REG) {
            sess_ctx->state = SIG_WSS_SESS_WAIT_PEER;
            s->state = P2P_STATE_WAITING;
            print("I:", LA_F("[ST:%s] peer went offline, waiting for reconnect\n", LA_F488, 488), 
                  "WAIT_PEER");
        }
    }
    /* NOT_REG / PROTOCOL / INTERNAL / UNKNOWN → 致命错误 */
    else {
        print("E:", LA_F("%s: fatal error code=%u, entering ERROR state\n", LA_F143, 143),
              PROTO, (unsigned)code);

        /* 会话级致命错误：关闭连接 */
        p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
        if (ctx->ws) {
            ws_client_close(ctx->ws, 1000);  /* 1000 = 正常关闭 */
            ctx->ws = NULL;
        }
        ctx->state = SIG_WSS_ERROR;
    }
}

/*
 * 处理 STA 消息（统一状态应答）
 *
 * 格式: "STA <session_id_hex> <req_type> <status_hex>\n"
 */
static void handle_sta(struct p2p_instance *inst, const char *msg) {
    const char *PROTO = "STA";

    char session_id_hex[9];
    char req_type[16];
    int status_hex;

    /* 解析 "STA <session_id_hex> <req_type> <status_hex>" */
    if (sscanf(msg + strlen("STA "), "%8s %15s %x", 
               session_id_hex, req_type, &status_hex) != 3) {
        print("E:", LA_F("%s: parse failed\n", LA_F208, 208), PROTO);
        return;
    }

    uint32_t session_id = parse_session_id(session_id_hex);
    uint8_t code = (uint8_t)status_hex;

    /* session_id=00000000 表示实例级错误 */
    if (session_id == 0) {
        handle_status(inst, req_type, code, NULL);
        return;
    }

    /* 查找会话 */
    struct p2p_session *s = NULL;
    for (s = inst->sessions_head; s; s = s->next) {
        if (s->id == session_id) break;
    }
    if (!s) {
        print("W:", LA_F("%s: session %08X not found (req_type=%s)\n", LA_F223, 223),
              PROTO, session_id, req_type);
        return;
    }

    /* 处理会话级状态 */
    handle_session_status(s, req_type, code, NULL);
}

///////////////////////////////////////////////////////////////////////////////
// WebSocket 回调
///////////////////////////////////////////////////////////////////////////////

static void on_ws_open(ws_client_t *c, void *user_data) {
    struct p2p_instance *inst = (struct p2p_instance *)user_data;
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    (void)c;

    print("I:", "[W] WebSocket connected\n");

    /* 发送 REG */
    ctx->state = SIG_WSS_WAIT_REG_ACK;
    send_reg(inst, P_tick_ms());
}

static void on_ws_message(ws_client_t *c, ws_msg_type_t type,
                          const uint8_t *data, size_t len, void *user_data) {
    struct p2p_instance *inst = (struct p2p_instance *)user_data;
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    (void)c;

    ctx->last_recv_time = P_tick_ms();

    if (type == WS_MSG_TEXT) {
        /* 文本消息：控制协议 */
        char *txt = (char *)malloc(len + 1);
        if (!txt) return;
        memcpy(txt, data, len);
        txt[len] = '\0';

        /* 分发协议 */
        if (strncmp(txt, P2P_WSS_RSP_REG_OK, P2P_WSS_RSP_REG_OK_SZ) == 0) {
            handle_reg_ok(inst, txt);
        } else if (strncmp(txt, P2P_WSS_RSP_SYN0, strlen(P2P_WSS_RSP_SYN0)) == 0) {
            handle_syn0_from_server(inst, txt);
        } else if (strncmp(txt, "SYNC ", 5) == 0) {
            handle_sync_from_server(inst, txt);
        } else if (strncmp(txt, "FIN ", 4) == 0) {
            handle_fin(inst, txt);
        } else if (strncmp(txt, "STA ", 4) == 0) {
            handle_sta(inst, txt);
        } else {
            print("W:", "[W] unknown message: %s\n", txt);
        }

        free(txt);
    } else if (type == WS_MSG_BINARY) {
        /* 二进制消息：数据中继或 RPC */
        if (len < 1 + P2P_SESS_ID_SZ) {
            print("W:", "[W] binary frame too short (%zu bytes)\n", len);
            return;
        }

        uint8_t frame_type = data[0];
        uint32_t session_id = nget_l(data + 1);

        /* 查找会话 */
        struct p2p_session *s = NULL;
        for (s = inst->sessions_head; s; s = s->next) {
            if (s->id == session_id) break;
        }
        if (!s) {
            print("W:", "[W] binary frame: session %08X not found\n", session_id);
            return;
        }

        p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;
        const uint8_t *payload = data + 1 + P2P_SESS_ID_SZ;
        uint16_t payload_len = (uint16_t)(len - 1 - P2P_SESS_ID_SZ);

        switch (frame_type) {
            case P2P_WSS_BIN_PKT: {
                /* 数据包中继：[type][session_id][p2p_hdr(4)][data(N)] */
                if (payload_len < P2P_HDR_SIZE) {
                    print("W:", "[W] PKT frame too short\n");
                    return;
                }
                
                /* 解析 P2P 包头并传递给 NAT 层处理 */
                p2p_packet_hdr_t hdr;
                p2p_pkt_hdr_decode(payload, &hdr);
                
                print("V:", "[W] received PKT frame (%u bytes), inner type=%u\n", 
                      payload_len, hdr.type);
                
                sockAddr_t from;
                sockAddr_from_v4(&from, &ctx->server_addr);
                nat_proto(s, hdr.type, hdr.flags, hdr.seq, 
                         payload + P2P_HDR_SIZE, payload_len - P2P_HDR_SIZE,
                         &from, P_tick_ms());
                break;
            }

            case P2P_WSS_BIN_REQ: {
                /* RPC 请求：[type][session_id][sid(2)][msg(1)][data(N)] */
                if (payload_len < 3) {
                    print("W:", "[W] REQ frame too short\n");
                    return;
                }
                uint16_t sid = nget_s(payload);
                uint8_t msg = payload[2];
                const uint8_t *req_data = payload + 3;
                int req_len = (int)payload_len - 3;

                /* 去重：忽略正在处理的相同请求 */
                if (sess_ctx->resp_sid == sid) {
                    print("V:", "[W] duplicate request ignored (sid=%u)\n", sid);
                    return;
                }

                /* 忽略旧请求 */
                if (sess_ctx->rpc_last_sid != 0 && !uint16_circle_newer(sid, sess_ctx->rpc_last_sid)) {
                    print("V:", "[W] old request ignored (sid=%u <= last_sid=%u)\n",
                          sid, sess_ctx->rpc_last_sid);
                    return;
                }

                sess_ctx->resp_sid = sid;  /* 记录待回应的 sid */

                /* msg=0: 自动 echo 回复 */
                if (msg == 0) {
                    print("V:", "[W] msg=0: echo reply (sid=%u)\n", sid);
                    p2p_signal_wss_rsp(s, 0, req_data, req_len);
                    return;
                }

                print("V:", "[W] REQ accepted (ses_id=%u), sid=%u msg=%u\n", 
                      s->id, sid, msg);

                /* 调用 on_request 回调 */
                if (s->inst->cfg.on_request)
                    s->inst->cfg.on_request((p2p_session_t)s, sid, msg, 
                                           req_data, req_len, s->inst->cfg.userdata);
                break;
            }

            case P2P_WSS_BIN_RSP: {
                /* RPC 响应：[type][session_id][sid(2)][code(1)][data(N)] */
                if (payload_len < 3) {
                    print("W:", "[W] RSP frame too short\n");
                    return;
                }
                uint16_t sid = nget_s(payload);
                uint8_t code = payload[2];
                const uint8_t *rsp_data = payload + 3;
                int rsp_len = (int)payload_len - 3;

                /* 仅命中当前挂起请求 */
                if (!(sess_ctx->req_state == 1 && sess_ctx->req_sid == sid)) {
                    print("E:", "[W] irrelevant response (sid=%u, current sid=%u, state=%d)\n",
                          sid, sess_ctx->req_sid, (int)sess_ctx->req_state);
                    return;
                }

                /* 错误响应 */
                if (code >= P2P_RPC_ERR_PEER_OFF) {
                    if (code == P2P_RPC_ERR_PEER_OFF)
                        print("W:", "[W] peer offline (sid=%u)\n", sid);
                    else
                        print("W:", "[W] timeout (sid=%u)\n", sid);

                    sess_ctx->req_state = 0;
                    sess_ctx->req_sid   = 0;

                    if (s->inst->cfg.on_response)
                        s->inst->cfg.on_response((p2p_session_t)s, sid, code, 
                                                 NULL, -1, s->inst->cfg.userdata);
                    return;
                }

                print("V:", "[W] RSP complete (ses_id=%u), sid=%u code=%u\n", 
                      s->id, sid, code);

                sess_ctx->req_state = 0;  /* 清除等待状态 */
                sess_ctx->req_sid   = 0;

                /* 调用 on_response 回调 */
                if (s->inst->cfg.on_response)
                    s->inst->cfg.on_response((p2p_session_t)s, sid, code, 
                                            rsp_data, rsp_len, s->inst->cfg.userdata);
                break;
            }

            default:
                print("W:", "[W] unknown binary frame type: 0x%02X\n", frame_type);
                break;
        }
    }
}

static void on_ws_close(ws_client_t *c, uint16_t status_code, const char *reason,
                        void *user_data) {
    struct p2p_instance *inst = (struct p2p_instance *)user_data;
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;
    (void)c;

    print("W:", "[W] connection closed (code=%d, reason=%s)\n",
          status_code, reason ? reason : "");

    ctx->state = SIG_WSS_INIT;

    /* 触发所有会话的断连处理 */
    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        if (s->sig_sess.wss.state != SIG_WSS_SESS_IDLE) {
            s->sig_sess.wss.state = SIG_WSS_SESS_IDLE;
            if (s->nat.state > NAT_CLOSED) {
                s->nat.state = NAT_CLOSED;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// 公共 API 实现
///////////////////////////////////////////////////////////////////////////////

void p2p_signal_wss_init(p2p_wss_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIG_WSS_INIT;
}

ret_t p2p_signal_wss_reg(struct p2p_instance *inst, const char *local_peer_id,
                          const char *server_host, int server_port) {
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;

    if (ctx->state != SIG_WSS_INIT) {
        print("E:", "[W] already online or connecting\n");
        return E_CONFLICT;  /* 当前正在被使用，或者之前的使用没有被正确释放 */
    }

    /* 保存身份 */
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
    ctx->local_peer_id[P2P_PEER_ID_MAX] = '\0';

    /* 保存服务器地址（用于标记转发包来源） */
    memset(&ctx->server_addr, 0, sizeof(ctx->server_addr));
    ctx->server_addr.sin_family = AF_INET;
    ctx->server_addr.sin_port = htons((uint16_t)server_port);
    if (inet_pton(AF_INET, server_host, &ctx->server_addr.sin_addr) != 1) {
        print("E:", "[W] invalid server address: %s\n", server_host);
        ctx->state = SIG_WSS_ERROR;
        return E_INVALID;
    }

    /* 生成 instance_id */
    uint32_t rid = 0;
    while (rid == 0) rid = P_rand32();
    ctx->instance_id = rid;

    /* 创建 WS 客户端 */
    ws_client_cfg_t cfg = {0};
    cfg.on_open    = on_ws_open;
    cfg.on_message = on_ws_message;
    cfg.on_close   = on_ws_close;
    cfg.user_data  = inst;

    ws_client_t *c = ws_client_create(&cfg);
    if (!c) {
        print("E:", "[W] failed to create ws_client\n");
        ctx->state = SIG_WSS_ERROR;
        return E_OUT_OF_MEMORY;
    }
    ctx->ws = c;

    /* 发起连接 */
    if (ws_client_connect(c, server_host, server_port, "/") != 0) {
        print("E:", "[W] connect to %s:%d failed\n", server_host, server_port);
        ws_client_destroy(c);
        ctx->ws = NULL;
        ctx->state = SIG_WSS_ERROR;
        return E_UNKNOWN;
    }

    ctx->state = SIG_WSS_CONNECTING;
    ctx->connect_time = P_tick_ms();

    print("I:", "[W] connecting to %s:%d (peer='%s', inst=%u)\n",
          server_host, server_port, local_peer_id, ctx->instance_id);

    return E_NONE;
}

void p2p_signal_wss_off(struct p2p_instance *inst) {
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;

    if (ctx->state == SIG_WSS_REG) {
        send_off(inst);
    }

    if (ctx->ws) {
        ws_client_destroy((ws_client_t *)ctx->ws);
        ctx->ws = NULL;
    }

    ctx->state = SIG_WSS_INIT;
    print("I:", "[W] offline\n");
}

ret_t p2p_signal_wss_syn0(struct p2p_session *s, const char *remote_peer_id) {
    p2p_wss_ctx_t *ctx = &s->inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    /* 保存对端身份 */
    strncpy(sess_ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
    sess_ctx->remote_peer_id[P2P_PEER_ID_MAX] = '\0';

    /* 状态初始化 */
    sess_ctx->candidate_syncing_base = 0;
    sess_ctx->candidate_synced_count = 0;
    sess_ctx->sync_sid = 0;

    /* 已上线：立即发送 SYN0；否则等待 REG 完成后自动触发 */
    if (ctx->state == SIG_WSS_REG) {
        sess_ctx->state = SIG_WSS_SESS_WAIT_SYN0_ACK;
        send_syn0(s->inst, s, P_tick_ms());
    } else {
        sess_ctx->state = SIG_WSS_SESS_WAIT_REG;
    }

    return E_NONE;
}

ret_t p2p_signal_wss_fin(struct p2p_session *s) {
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (!sess_ctx->remote_peer_id[0]) {
        return E_NONE;  /* 没有建立过配对 */
    }

    // 如果尚未完成在线：直接取消 syn0 连接状态
    if (sess_ctx->state == SIG_WSS_SESS_WAIT_REG)
        sess_ctx->state = SIG_WSS_SESS_SUSPENDED;
    if (sess_ctx->state == SIG_WSS_SESS_SUSPENDED) {
        *sess_ctx->remote_peer_id = 0;
        return E_NONE;
    }

    // 如果正在通过信令服务器申请建立连接中
    if (sess_ctx->state < SIG_WSS_SESS_WAIT_PEER) {
        return E_BUSY;
    }

    print("I:", LA_F("[W] Disconnected, back to REG state\n", LA_F471, 471));

    // 发送 FIN 消息（主动断开）
    send_fin(s);

    // 清理 peer 会话状态
    sess_ctx->candidate_synced_count = 0;
    s->remote_cand_done = false;
    memset(sess_ctx->remote_peer_id, 0, sizeof(sess_ctx->remote_peer_id));

    // 清理会话状态
    s->id = 0;
    return E_NONE;
}

void p2p_signal_wss_stun_ready(struct p2p_session *s) {

    assert(s->inst->srflx_active >= s->inst->srflx_count);

    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (sess_ctx->state == SIG_WSS_SESS_WAIT_STUN) {

        sess_ctx->state = SIG_WSS_SESS_SYNCING;
        print("I:", LA_F("%s: stun collection ready, auto SYNC sent\n", LA_F248, 248), TASK_TOUCH);

        // 同步发送首批候选（如果有）
        assert(sess_ctx->candidate_syncing_base == 0);
        if (s->local_cand_cnt || !s->inst->turn_pending)
            send_sync(s->inst, s, P_tick_ms());
        else { sess_ctx->trickle_last_time = P_tick_ms(); s->inst->sig_ctx.wss.trickle_sessions++; }
    }
}

void p2p_signal_wss_trickle_candidate(struct p2p_session *s) {

    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (sess_ctx->state == SIG_WSS_SESS_SYNCING) {

        // 还没进入 trickle 阶段
        // + 也就是 sync_ack 首次将现有的候选全部同步完成，但又存在待收集的异步候选（如 STUN/TURN）
        if (!sess_ctx->trickle_last_time) return;

        // 检查是否有新候选
        if (sess_ctx->candidate_syncing_base >= s->local_cand_cnt) {
            assert(sess_ctx->candidate_syncing_base == s->local_cand_cnt);
            return;
        }

        // 如果上次发送后还没有收到对端的 SYNC_ACK
        if (sess_ctx->candidate_synced_count < sess_ctx->candidate_syncing_base) return;

        // 发送控制：如果已没有待收集的候选了
        if (s->inst->srflx_active >= s->inst->srflx_count && !s->inst->turn_pending) {

            sess_ctx->trickle_last_time = 0; s->inst->sig_ctx.wss.trickle_sessions--;
            send_sync(s->inst, s, P_tick_ms());
        }
        // 或已经积累了足够的候选；又或者距离上次发送已经超过攒批时间窗口了
        else if ((s->local_cand_cnt - sess_ctx->candidate_syncing_base >= s->inst->sig_ctx.wss.sync_max) ||
                 (P_tick_ms() - sess_ctx->trickle_last_time) >= P2P_WSS_TRICKLE_BATCH_MS) {

            send_sync(s->inst, s, P_tick_ms());
        }
    }
    // todo: >SIG_WSS_SESS_SYNCING 用于动态更新 srflx 地址的变更
}

void p2p_signal_wss_tick_recv(struct p2p_instance *inst, uint64_t now) {
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;

    if (ctx->state < SIG_WSS_CONNECTING || !ctx->ws) {
        return;
    }

    /* 驱动 WS 状态机 */
    ws_client_update((ws_client_t *)ctx->ws);

    /* 连接超时检查 */
    if (ctx->state == SIG_WSS_CONNECTING || ctx->state == SIG_WSS_WAIT_REG_ACK) {
        if (tick_diff(now, ctx->connect_time) > P2P_WSS_ACK_TIMEOUT_MS) {
            print("E:", "[W] connect/register timeout\n");
            ctx->state = SIG_WSS_ERROR;
            return;
        }
    }

    /* 心跳超时检查：超过 60 秒无消息认为连接断开 */
    if (ctx->state == SIG_WSS_REG) {
        if (tick_diff(now, ctx->last_recv_time) > 60000) {
            print("E:", "[W] heartbeat timeout (no message for 60s)\n");
            ctx->state = SIG_WSS_ERROR;
            return;
        }
    }
}

void p2p_signal_wss_tick_send(struct p2p_instance *inst, uint64_t now) {
    p2p_wss_ctx_t *ctx = &inst->sig_ctx.wss;

    if (ctx->state != SIG_WSS_REG) {
        return;
    }

    /* 遍历会话，同步候选 */
    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        if (s->state == P2P_STATE_CLOSED || s->state == P2P_STATE_ERROR) {
            continue;
        }

        p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

        if (sess_ctx->state < SIG_WSS_SESS_SYNCING) {
            continue;
        }

        /* 有新候选需要同步 */
        if (s->local_cand_cnt > sess_ctx->candidate_synced_count) {
            /* 流控：检查未确认的 SYNC 数量（限制为 1，避免服务器队列满）*/
            uint8_t unconfirmed = (sess_ctx->sync_sid - sess_ctx->sync_sid_confirmed) & 0xFF;
            if (unconfirmed >= 2) {
                /* 等待之前的 SYNC 被确认 */
                continue;
            }
            if (sess_ctx->trickle_last_time == 0 ||
                tick_diff(now, sess_ctx->trickle_last_time) >= P2P_WSS_TRICKLE_BATCH_MS) {
                send_sync(inst, s, now);
                sess_ctx->trickle_last_time = now;
            }
        }
        /* 候选收集完毕，发送 ICE_DONE */
        else if (!P2P_CAND_PENDING(inst) && sess_ctx->state != SIG_WSS_SESS_READY) {
            /* 检查候选是否全部同步（等待 confirm 完成后再发送 ICE_DONE）*/
            if (sess_ctx->candidate_synced_count >= s->local_cand_cnt) {
                send_ice_done(inst, s);
            }
        }
    }

    /* 心跳保活：定期发送 TOUCH 保持连接 */
    if (tick_diff(now, ctx->last_send_time) > P2P_WSS_HEARTBEAT_INTERVAL_MS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "TOUCH %s", inst->local_peer_id);
        ws_send_text(ctx, msg, "TOUCH");
    }
}

/* RPC 和数据中继功能 */
ret_t p2p_signal_wss_pkt(struct p2p_session *s,
                          uint8_t type, uint8_t flags, uint16_t seq,
                          const void *payload, uint16_t payload_len) {
    p2p_wss_ctx_t *ctx = &s->inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (ctx->state != SIG_WSS_REG || !ctx->feature_relay) {
        return E_NO_SUPPORT;  /* 服务器不支持数据中继 */
    }

    /* 流控：等待服务器 READY 确认 */
    if (sess_ctx->awaiting_relay_ready) {
        return E_BUSY;
    }

    /* 帧格式: [P2P_WSS_BIN_PKT(0x01)][session_id(4)][type(1)][flags(1)][seq(2)][payload(N)] */
    uint8_t buf[P2P_MTU];
    uint16_t total = (uint16_t)(1 + P2P_SESS_ID_SZ + P2P_HDR_SIZE + payload_len);

    if (total > sizeof(buf)) {
        return E_OUT_OF_RANGE;
    }

    buf[0] = P2P_WSS_BIN_PKT;
    nwrite_l(buf + 1, s->id);
    buf[5] = type;
    buf[6] = flags;
    nwrite_s(buf + 7, seq);
    if (payload_len > 0) {
        memcpy(buf + 9, payload, payload_len);
    }

    ret_t ret = ws_send_binary(ctx, buf, total, "PKT");
    if (ret == E_NONE) {
        /* 发送后设置流控等待标志 */
        sess_ctx->awaiting_relay_ready = true;
    }
    return ret;
}

ret_t p2p_signal_wss_req(struct p2p_session *s,
                          uint8_t msg, const void *data, int len) {
    p2p_wss_ctx_t *ctx = &s->inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (ctx->state != SIG_WSS_REG || !ctx->feature_msg) {
        return E_NO_SUPPORT;  /* 服务器不支持 RPC */
    }

    if (sess_ctx->req_state != 0) {
        return E_BUSY;  /* 已有挂起请求 */
    }

    /* 分配 sid */
    uint16_t sid = sess_ctx->rpc_last_sid + 1;
    if (sid == 0) sid = 1;  /* 跳过 0 */

    /* 帧格式: [P2P_WSS_BIN_REQ(0x02)][session_id(4)][sid(2)][msg(1)][data(N)] */
    uint8_t buf[P2P_MTU];
    uint16_t total = (uint16_t)(1 + P2P_SESS_ID_SZ + 2 + 1 + len);

    if (total > sizeof(buf)) {
        return E_OUT_OF_RANGE;
    }

    buf[0] = P2P_WSS_BIN_REQ;
    nwrite_l(buf + 1, s->id);
    nwrite_s(buf + 5, sid);
    buf[7] = msg;
    if (len > 0) {
        memcpy(buf + 8, data, (size_t)len);
    }

    ret_t ret = ws_send_binary(ctx, buf, total, "REQ");
    if (ret == E_NONE) {
        sess_ctx->req_state = 1;
        sess_ctx->req_sid = sid;
        sess_ctx->req_msg = msg;
    }

    return ret;
}

ret_t p2p_signal_wss_rsp(struct p2p_session *s,
                          uint8_t code, const void *data, int len) {
    p2p_wss_ctx_t *ctx = &s->inst->sig_ctx.wss;
    p2p_wss_session_t *sess_ctx = &s->sig_sess.wss;

    if (ctx->state != SIG_WSS_REG || !ctx->feature_msg) {
        return E_NO_SUPPORT;  /* 服务器不支持 RPC */
    }

    if (sess_ctx->resp_sid == 0) {
        return E_INVALID;  /* 没有待回应的请求 */
    }

    /* 帧格式: [P2P_WSS_BIN_RSP(0x03)][session_id(4)][sid(2)][code(1)][data(N)] */
    uint8_t buf[P2P_MTU];
    uint16_t total = (uint16_t)(1 + P2P_SESS_ID_SZ + 2 + 1 + len);

    if (total > sizeof(buf)) {
        return E_OUT_OF_RANGE;
    }

    buf[0] = P2P_WSS_BIN_RSP;
    nwrite_l(buf + 1, s->id);
    nwrite_s(buf + 5, sess_ctx->resp_sid);
    buf[7] = code;
    if (len > 0) {
        memcpy(buf + 8, data, (size_t)len);
    }

    ret_t ret = ws_send_binary(ctx, buf, total, "RSP");
    if (ret == E_NONE) {
        sess_ctx->resp_sid = 0;  /* 清除待回应标记 */
    }

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
#endif /* WITH_WS */
