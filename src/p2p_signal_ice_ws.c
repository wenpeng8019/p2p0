/*
 * p2p_signal_ice_ws.c — ICE 模式 WebSocket 信令实现
 *
 * 通过 WebSocket 连接信令服务器交换 SDP 和 ICE 候选。
 * 协议详见 p2p_signal_ice_ws.h
 */

#ifdef WITH_WS

#include "p2p_signal_ice_ws.h"
#include "p2p_internal.h"
#include "p2p_ice.h"
#include "ws_client.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef MOD_TAG
#undef MOD_TAG
#endif
#define MOD_TAG "ICE_WS"

/* SDP 交换间隔（毫秒）：trickle 候选增量同步 */
#define ICE_WS_SYNC_INTERVAL_MS     500

/* WS 连接超时（毫秒）*/
#define ICE_WS_CONNECT_TIMEOUT_MS   10000

/* ============================================================================
 * WS 回调
 * ============================================================================ */

static void on_ws_open(ws_client_t *c, void *user_data) {
    struct p2p_instance *inst = (struct p2p_instance *)user_data;
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    (void)c;

    /* 发送注册消息: "REG <peer_id> <instance_id>" */
    char reg[16 + P2P_PEER_ID_MAX];
    snprintf(reg, sizeof(reg), "REG %s %u", inst->local_peer_id, ctx->instance_id);
    ws_client_send_text(c, reg);

    ctx->state = ICE_WS_REGISTERING;
    print("I:", "[WS] connected, registering as '%s' (inst=%u)\n",
          inst->local_peer_id, ctx->instance_id);
}

/* 处理来自服务器的 SYNC：找到对应 session 并导入候选 */
static void on_ws_sync_from_peer(struct p2p_instance *inst,
                                const char *from_peer_id,
                                const char *payload) {
    char _ab[INET6_ADDRSTRLEN];

    /* 找到与 from_peer_id 匹配的 session */
    struct p2p_session *s = NULL;
    for (s = inst->sessions_head; s; s = s->next) {
        if (strcmp(s->remote_peer_id, from_peer_id) == 0) break;
    }
    if (!s) {
        print("V:", "[WS] SYNC from unknown peer '%s', ignored\n", from_peer_id);
        return;
    }

    if (strncmp(payload, "SDP\n", 4) == 0) {
        const char *sdp_text = payload + 4;

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
            print("W:", "[WS] SDP from '%s': import failed or empty\n", from_peer_id);
            return;
        }

        int added = 0;
        for (int i = 0; i < parsed; i++) {
            if (p2p_find_remote_candidate_by_addr(s, &tmp_cands[i].addr) >= 0) continue;

            /* IPv4/IPv6 过滤 */
            if ((tmp_cands[i].addr.family == AF_INET  && inst->cfg.test_ice_ipv4_off) ||
                (tmp_cands[i].addr.family == AF_INET6 && inst->cfg.test_ice_ipv6_off))
                continue;

            int idx = p2p_cand_push_remote(s);
            if (idx < 0) break;
            s->remote_cands[idx] = tmp_cands[i];

            p2p_remote_candidate_entry_t *c = &s->remote_cands[idx];
            print("I:", "[WS] REMOTE: %s cand[%d]<%s:%d> from '%s'\n",
                  c->type == P2P_CAND_HOST ? "host" : c->type == P2P_CAND_SRFLX ? "srflx" : "relay",
                  idx, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr),
                  from_peer_id);
            added++;
        }
        if (added > 0) {
            print("I:", "[WS] received %d candidates from '%s'\n", added, from_peer_id);
        }
    }
    else if (strncmp(payload, "ICE\n", 4) == 0) {
        /* Trickle ICE 单个候选 */
        const char *cand_line = payload + 4;

        /* 包装为 SDP 格式以复用 import 逻辑 */
        char sdp_wrap[512];
        snprintf(sdp_wrap, sizeof(sdp_wrap), "a=%s\r\n", cand_line);

        p2p_remote_candidate_entry_t tmp_cand[1];
        int parsed = p2p_ice_import_sdp(sdp_wrap, tmp_cand, 1);
        if (parsed == 1 && p2p_find_remote_candidate_by_addr(s, &tmp_cand[0].addr) < 0) {
            if (!((tmp_cand[0].addr.family == AF_INET  && inst->cfg.test_ice_ipv4_off) ||
                  (tmp_cand[0].addr.family == AF_INET6 && inst->cfg.test_ice_ipv6_off))) {
                int idx = p2p_cand_push_remote(s);
                if (idx >= 0) {
                    s->remote_cands[idx] = tmp_cand[0];
                    print("I:", "[WS] trickle cand[%d]<%s:%d> from '%s'\n",
                          idx, sockAddr_str(&tmp_cand[0].addr, _ab, sizeof(_ab)),
                          sockAddr_port(&tmp_cand[0].addr), from_peer_id);
                }
            }
        }
    }
    else if (strcmp(payload, "ICE_DONE") == 0) {
        s->remote_cand_done = true;
        print("I:", "[WS] remote '%s' candidates done\n", from_peer_id);
    }
}

static void on_ws_message(ws_client_t *c, ws_msg_type_t type,
                          const uint8_t *data, size_t len, void *user_data) {
    struct p2p_instance *inst = (struct p2p_instance *)user_data;
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    (void)c;

    if (type != WS_MSG_TEXT || len == 0) return;

    /* NUL 终止 */
    char *txt = (char *)malloc(len + 1);
    if (!txt) return;
    memcpy(txt, data, len);
    txt[len] = '\0';

    if (strncmp(txt, "REG OK", 6) == 0) {
        /* "REG OK <sync_max> <features>" */
        int features = 0;
        if (txt[6] == ' ') {
            char *sp = strchr(txt + 7, ' ');
            if (sp) features = (int)strtol(sp + 1, NULL, 10);
        }
        ctx->feature_relay = (features & P2P_RLY_FEATURE_RELAY) != 0;
        ctx->feature_msg   = (features & P2P_RLY_FEATURE_MSG) != 0;
        ctx->state = ICE_WS_REG;
        print("I:", "[WS] registered OK (relay=%s, msg=%s)\n",
              ctx->feature_relay ? "yes" : "no", ctx->feature_msg ? "yes" : "no");
    }
    else if (strncmp(txt, "REG FAIL", 8) == 0) {
        /* legacy compatibility: newer servers close the WS with close reason instead */
        ctx->state = ICE_WS_ERROR;
        print("E:", "[WS] registration failed: %s\n", txt + 9);
    }
    else if (strncmp(txt, "SYNC ", 5) == 0) {
        /* SYNC <from_peer_id>\n<payload> */
        char *from = txt + 5;
        char *nl = strchr(from, '\n');
        if (nl) {
            *nl = '\0';
            on_ws_sync_from_peer(inst, from, nl + 1);
        }
    }
    else if (strncmp(txt, "FIN ", 4) == 0) {
        uint32_t session_id = (uint32_t)strtoul(txt + 4, NULL, 10);
        print("I:", "[WS] FIN received (session_id=%u)\n", session_id);
        /* TODO: find session by id, trigger peer_disconnect */
        (void)session_id;
    }

    free(txt);
}

static void on_ws_close(ws_client_t *c, uint16_t status_code, const char *reason,
                        void *user_data) {
    struct p2p_instance *inst = (struct p2p_instance *)user_data;
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    (void)c;

    print("W:", "[WS] connection closed (code=%d, reason=%s)\n",
          status_code, reason ? reason : "");
    ctx->state = ICE_WS_INIT;
}

/* ============================================================================
 * 信令发送辅助
 * ============================================================================ */

/* 发送 SYNC 消息到服务器 */
static int ws_sync(struct p2p_instance *inst, const char *to_peer_id,
                  const char *payload) {
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    if (ctx->state != ICE_WS_REG || !ctx->ws) return -1;

    size_t to_len  = strlen(to_peer_id);
    size_t pay_len = strlen(payload);
    size_t msg_len = 5 + to_len + 1 + pay_len;  /* "SYNC " + to + '\n' + payload */
    char *msg = (char *)malloc(msg_len + 1);
    if (!msg) return -1;
    snprintf(msg, msg_len + 1, "SYNC %s\n%s", to_peer_id, payload);

    int ret = ws_client_send_text((ws_client_t *)ctx->ws, msg);
    free(msg);
    return ret;
}

/* 发送 SDP（含 candidates + ufrag/pwd）给对端 */
static void ws_send_sdp(struct p2p_instance *inst, struct p2p_session *s) {
    char sdp_buf[4096];
    int sdp_len = p2p_ice_export_sdp(s->local_cands, s->local_cand_cnt,
                                      sdp_buf, (int)sizeof(sdp_buf),
                                      true, NULL, NULL, NULL);
    if (sdp_len <= 0) return;

    /* 追加 ICE 凭证 */
    if (s->ice_ufrag[0]) {
        sdp_len += snprintf(sdp_buf + sdp_len, (int)sizeof(sdp_buf) - sdp_len,
                            "a=ice-ufrag:%s\r\na=ice-pwd:%s\r\n",
                            s->ice_ufrag, s->ice_pwd);
    }

    /* 构造 payload: "SDP\n<sdp_text>" */
    size_t payload_len = 4 + (size_t)sdp_len;
    char *payload = (char *)malloc(payload_len + 1);
    if (!payload) return;
    snprintf(payload, payload_len + 1, "SDP\n%s", sdp_buf);

    if (ws_sync(inst, s->remote_peer_id, payload) == 0) {
        p2p_ice_ws_session_t *sess = &s->sig_sess.ice_ws;
        sess->candidate_synced = s->local_cand_cnt;
        sess->last_sync = P_tick_ms();
        print("I:", "[WS] sent %d candidates to '%s'\n",
              s->local_cand_cnt, s->remote_peer_id);
    }
    free(payload);
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

void p2p_signal_ice_ws_init(struct p2p_instance *inst) {
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    memset(ctx, 0, sizeof(*ctx));

    /* 每次 init 生成新 instance_id（区分重连 vs 重启） */
    uint32_t rid = 0;
    while (rid == 0) rid = P_rand32();
    ctx->instance_id = rid;

    ws_client_cfg_t cfg = {0};
    cfg.on_open    = on_ws_open;
    cfg.on_message = on_ws_message;
    cfg.on_close   = on_ws_close;
    cfg.user_data  = inst;

    ws_client_t *c = ws_client_create(&cfg);
    if (!c) {
        print("E:", "[WS] failed to create ws_client\n");
        ctx->state = ICE_WS_ERROR;
        return;
    }
    ctx->ws = c;

    /* 发起连接 */
    if (ws_client_connect(c, inst->cfg.server_host, inst->cfg.server_port, "/") != 0) {
        print("E:", "[WS] connect to %s:%d failed\n",
              inst->cfg.server_host, inst->cfg.server_port);
        ws_client_destroy(c);
        ctx->ws = NULL;
        ctx->state = ICE_WS_ERROR;
        return;
    }

    ctx->state = ICE_WS_CONNECTING;
    ctx->connect_time = P_tick_ms();
    print("I:", "[WS] connecting to %s:%d\n",
          inst->cfg.server_host, inst->cfg.server_port);
}

void p2p_signal_ice_ws_destroy(struct p2p_instance *inst) {
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    if (ctx->ws) {
        ws_client_destroy((ws_client_t *)ctx->ws);
        ctx->ws = NULL;
    }
    ctx->state = ICE_WS_INIT;
}

int p2p_signal_ice_ws_connect(struct p2p_session *s, const char *remote_peer_id) {
    (void)remote_peer_id;  /* remote_peer_id 已在 p2p_connect 中存储到 s->remote_peer_id */
    p2p_ice_ws_session_t *sess = &s->sig_sess.ice_ws;
    memset(sess, 0, sizeof(*sess));
    sess->state = ICE_WS_SESS_IDLE;
    return 0;
}

void p2p_signal_ice_ws_tick(struct p2p_instance *inst, uint64_t now_ms) {
    p2p_ice_ws_ctx_t *ctx = &inst->sig_ctx.ice_ws;
    if (!ctx->ws) return;

    /* 驱动 WS 状态机 */
    ws_client_update((ws_client_t *)ctx->ws);

    /* 连接超时检查 */
    if (ctx->state == ICE_WS_CONNECTING || ctx->state == ICE_WS_REGISTERING) {
        if (tick_diff(now_ms, ctx->connect_time) > ICE_WS_CONNECT_TIMEOUT_MS) {
            print("E:", "[WS] connect/register timeout\n");
            ctx->state = ICE_WS_ERROR;
            return;
        }
    }

    /* 未在线则不处理会话级信令 */
    if (ctx->state != ICE_WS_REG) return;

    /* 遍历会话，同步候选 */
    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        if (s->state == P2P_STATE_CLOSED || s->state == P2P_STATE_ERROR) continue;

        p2p_ice_ws_session_t *sess = &s->sig_sess.ice_ws;

        /* 有新候选需要同步 */
        if (s->local_cand_cnt > sess->candidate_synced) {
            if (tick_diff(now_ms, sess->last_sync) >= ICE_WS_SYNC_INTERVAL_MS) {
                ws_send_sdp(inst, s);

                /* 候选收集完毕后发 ICE_DONE */
                if (!P2P_CAND_PENDING(inst) && sess->state != ICE_WS_SESS_READY) {
                    ws_sync(inst, s->remote_peer_id, "ICE_DONE");
                    sess->state = ICE_WS_SESS_READY;
                    print("I:", "[WS] sent ICE_DONE to '%s'\n", s->remote_peer_id);
                }
            }
        }
        else if (!P2P_CAND_PENDING(inst) && sess->state < ICE_WS_SESS_READY) {
            /* 没有新候选但收集已完成——发 ICE_DONE */
            if (sess->candidate_synced > 0) {
                ws_sync(inst, s->remote_peer_id, "ICE_DONE");
                sess->state = ICE_WS_SESS_READY;
                print("I:", "[WS] sent ICE_DONE to '%s'\n", s->remote_peer_id);
            }
        }
    }
}

#endif /* WITH_WS */
