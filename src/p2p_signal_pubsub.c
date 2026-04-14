/*
 * PUBSUB (发布板) 信令实现
 *
 * 基于 GitHub Gist 的去中心化信令交换。
 * 每方拥有独立的 Gist 发布板，互相轮询对方发布板获取候选。
 *
 * 参见 p2p_signal_pubsub.h 了解架构设计。
 */

#define MOD_TAG "PUBSUB"

#include "p2p_internal.h"
#include "p2p_http.h"
#include "p2p_crypto.h"
#include "p2p_ice.h"

/* 日志标签（对齐 RELAY/COMPACT 风格）*/
static const char *TASK_PUBLISH = "PUBLISH";
static const char *TASK_POLL    = "POLL";

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/*
 * 从 auth_key 派生 DES 密钥（8 字节）
 *
 * 注：简化实现，生产环境应使用 PBKDF2 或 HKDF
 */
static void derive_key(const char *auth_key, uint8_t key_out[8]) {
    memset(key_out, 0, 8);
    if (auth_key && auth_key[0]) {
        size_t len = strlen(auth_key);
        for (size_t i = 0; i < 8 && i < len; i++)
            key_out[i] = (uint8_t)auth_key[i];
    } else {
        memset(key_out, 0xAA, 8);
    }
}

/*
 * 将本端候选列表导出为 SDP → DES 加密 → Base64 编码
 *
 * @param s       会话（提供 local_cands）
 * @param key     DES 密钥
 * @param out     输出 Base64 字符串
 * @param out_sz  输出缓冲区大小
 * @return        编码后长度，<=0 失败
 */
static int encode_candidates(struct p2p_session *s, const uint8_t key[8],
                              char *out, int out_sz) {
    int cnt = s->local_cand_cnt;
    if (cnt <= 0) return 0;

    /* 导出为 SDP 文本（仅候选行）*/
    char sdp_buf[4096];
    int sdp_len = p2p_ice_export_sdp(s->local_cands, cnt, sdp_buf, (int)sizeof(sdp_buf),
                                      true, NULL, NULL, NULL);
    if (sdp_len <= 0) return -1;

    /* DES 加密（需 8 字节对齐）*/
    int padded = (sdp_len + 7) & ~7;
    uint8_t *raw = (uint8_t *)calloc(1, (size_t)padded);
    if (!raw) return -1;
    memcpy(raw, sdp_buf, (size_t)sdp_len);

    uint8_t *enc = (uint8_t *)malloc((size_t)padded);
    if (!enc) { free(raw); return -1; }

    p2p_des_encrypt(key, raw, (size_t)padded, enc);
    free(raw);

    /* Base64 编码 */
    int b64_len = p2p_base64_encode(enc, (size_t)padded, out, (size_t)out_sz);
    free(enc);

    return b64_len;
}

/*
 * Base64 解码 → DES 解密 → SDP 解析候选列表并注入会话
 *
 * @param s       会话（注入 remote_cands）
 * @param key     DES 密钥
 * @param b64     Base64 编码的密文
 * @return        新增的候选数量
 */
static int decode_and_inject_candidates(struct p2p_session *s, const uint8_t key[8],
                                         const char *b64) {
    /* Base64 解码 */
    uint8_t enc_buf[4096];
    size_t enc_len = (size_t)p2p_base64_decode(b64, strlen(b64), enc_buf, sizeof(enc_buf));
    if (enc_len <= 0) {
        print("W:", LA_F("Base64 decode failed", LA_F262, 262));
        return 0;
    }

    /* DES 解密 */
    uint8_t *dec = (uint8_t *)calloc(1, enc_len + 1);  /* +1 for NUL */
    if (!dec) return 0;
    p2p_des_decrypt(key, enc_buf, enc_len, dec);
    dec[enc_len] = '\0';  /* 确保 NUL 终止（SDP 是文本）*/

    /* SDP 解析 */
    p2p_remote_candidate_entry_t tmp_cands[32];
    int parsed = p2p_ice_import_sdp((const char *)dec, tmp_cands, 32);
    free(dec);

    if (parsed <= 0) {
        print("W:", LA_F("SDP import failed or empty", LA_F262, 262));
        return 0;
    }

    int added = 0;
    for (int i = 0; i < parsed; i++) {
        /* 检查重复 */
        if (p2p_find_remote_candidate_by_addr(s, &tmp_cands[i].addr) >= 0) continue;

        int idx = p2p_cand_push_remote(s);
        if (idx < 0) break;

        s->remote_cands[idx] = tmp_cands[i];
        p2p_remote_candidate_entry_t *c = &s->remote_cands[idx];

        print("I:", LA_F("SDP REMOTE: %s cand[%d]<%s:%d> accepted", LA_F208, 208),
              c->type == P2P_CAND_HOST ? "host" : c->type == P2P_CAND_SRFLX ? "srflx" : "relay",
              idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
        added++;
    }

    return added;
}

/*
 * 从 Gist API 响应中提取 p2p_signal.json 的 content 字段
 *
 * GitHub API 响应格式：
 *   { "files": { "p2p_signal.json": { "content": "..." } } }
 *
 * @param resp     API 响应体
 * @param out      输出缓冲区
 * @param out_sz   缓冲区大小
 * @return         0=成功，-1=未找到
 */
static int extract_gist_content(const char *resp, char *out, int out_sz) {
    const char *file_key = "\"p2p_signal.json\"";
    const char *file_sec = strstr(resp, file_key);
    if (!file_sec) return -1;

    const char *cnt_key = strstr(file_sec, "\"content\"");
    if (!cnt_key) return -1;

    const char *cs = strchr(cnt_key + 9, '\"');
    if (!cs) return -1;
    cs++;  /* skip opening quote */

    /* 复制并还原 JSON 转义 */
    char *dst = out;
    const char *end = out + out_sz - 1;
    const char *p = cs;
    while (*p && dst < end) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n':  *dst++ = '\n'; p++; break;
                case '\\': *dst++ = '\\'; p++; break;
                case '"':  *dst++ = '"';  p++; break;
                case '/':  *dst++ = '/';  p++; break;
                case 'r':  *dst++ = '\r'; p++; break;
                case 't':  *dst++ = '\t'; p++; break;
                default:   *dst++ = '\\'; *dst++ = *p++; break;
            }
        } else if (*p == '"') {
            break;  /* end of JSON string */
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';
    return 0;
}

/*
 * 写入本端发布板（PATCH Gist）
 *
 * @param ctx   实例上下文
 * @param b64   Base64 编码的密文
 * @return      0=成功
 */
static int write_gist(p2p_signal_pubsub_ctx_t *ctx, const char *gist_id, const char *content) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/gists/%s", gist_id);

    size_t content_len = strlen(content);
    size_t body_sz = content_len + 128;
    char *body = (char *)malloc(body_sz);
    if (!body) return -1;

    snprintf(body, body_sz,
             "{\"files\":{\"p2p_signal.json\":{\"content\":\"%s\"}}}", content);

    int ret = p2p_http_patch(url, ctx->auth_token, body);
    free(body);
    return ret;
}

/*
 * 读取对端发布板（GET Gist）
 *
 * @param ctx       实例上下文
 * @param gist_id   对端 Gist ID
 * @param out       输出内容缓冲区
 * @param out_sz    缓冲区大小
 * @return          0=成功且有内容，-1=失败或无内容
 */
static int poll_from_gist(p2p_signal_pubsub_ctx_t *ctx, const char *gist_id,
                           char *out, int out_sz) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/gists/%s", gist_id);

    char *resp = (char *)calloc(1, 32768);
    if (!resp) return -1;

    int got = p2p_http_get(url, ctx->auth_token, resp, 32768);
    if (got <= 0) {
        free(resp);
        return -1;
    }

    int ret = extract_gist_content(resp, out, out_sz);
    free(resp);

    /* 内容过短视为空 */
    if (ret == 0 && (int)strlen(out) < 10) return -1;
    return ret;
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

void p2p_signal_pubsub_init(p2p_signal_pubsub_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIG_PUBSUB_INIT;
}

ret_t p2p_signal_pubsub_online(struct p2p_instance *inst, const char *local_peer_id,
                                const char *token, const char *gist_id) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;

    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(ctx->auth_token, token, sizeof(ctx->auth_token) - 1);
    strncpy(ctx->local_gist_id, gist_id, sizeof(ctx->local_gist_id) - 1);

    if (inst->cfg.auth_key)
        strncpy(ctx->auth_key, inst->cfg.auth_key, sizeof(ctx->auth_key) - 1);

    ctx->state = SIG_PUBSUB_ONLINE;

    print("I:", LA_F("ONLINE: local_gist=%s peer=%s", LA_F320, 320), gist_id, local_peer_id);
    return E_NONE;
}

ret_t p2p_signal_pubsub_offline(struct p2p_instance *inst) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    ctx->state = SIG_PUBSUB_INIT;
    print("I:", LA_F("OFFLINE", LA_F268, 268));
    return E_NONE;
}

//-----------------------------------------------------------------------------

ret_t p2p_signal_pubsub_connect(struct p2p_session *s, const char *remote_peer_id) {
    p2p_signal_pubsub_ctx_t *ctx = &s->inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    memset(sess, 0, sizeof(*sess));

    if (ctx->state < SIG_PUBSUB_ONLINE) {
        sess->state = SIG_PUBSUB_SESS_WAIT_ONLINE;
        print("I:", LA_F("CONNECT: instance not online yet, queued", 0, 0));
        return E_NONE;
    }

    /* 启动 NAT 打洞（STUN 后台运行）*/
    nat_punch(s, -1);

    if (remote_peer_id && remote_peer_id[0]) {
        /* PUB 模式：知道对方的 gist_id，主动发起 */
        sess->is_pub = true;
        strncpy(sess->remote_gist_id, remote_peer_id, sizeof(sess->remote_gist_id) - 1);
        sess->state = SIG_PUBSUB_SESS_OFFERING;
        print("I:", LA_F("CONNECT PUB: target=%s", 0, 0), remote_peer_id);
    } else {
        /* SUB 模式：不知道对方，等待 offer */
        sess->is_pub = false;
        sess->state = SIG_PUBSUB_SESS_WAIT_OFFER;
        print("I:", LA_F("CONNECT SUB: waiting for offer in mailbox (gist=%s)", 0, 0), ctx->local_gist_id);
    }

    return E_NONE;
}

ret_t p2p_signal_pubsub_disconnect(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;
    memset(sess, 0, sizeof(*sess));
    print("I:", LA_F("DISCONNECT", LA_F268, 268));
    return E_NONE;
}

//-----------------------------------------------------------------------------

void p2p_signal_pubsub_stun_ready(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    /* PUBSUB 不依赖 WAIT_STUN 状态，但日志记录 STUN 就绪 */
    if (sess->state == SIG_PUBSUB_SESS_SYNCING || sess->state == SIG_PUBSUB_SESS_OFFERING) {
        print("I:", LA_F("%s: STUN ready (state=%d)", LA_F475, 475), TASK_PUBLISH, sess->state);
    }
}

void p2p_signal_pubsub_trickle_candidate(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    /* 有新候选时，标记需要重新发布 */
    if (sess->state == SIG_PUBSUB_SESS_SYNCING &&
        s->local_cand_cnt > sess->local_published_cnt) {
        sess->local_published = false;
    }
}

//-----------------------------------------------------------------------------

/*
 * 阶段 2：信令拉取
 *
 *   SUB/WAIT_OFFER: 轮询自己的 Gist（5s），等待 PUB 投递 offer
 *   PUB/OFFERING:   轮询 SUB 的 Gist（1s），等待 SUB 用候选替换 offer
 *   SYNCING:        轮询对端的 Gist（1s），获取对端候选
 */
void p2p_signal_pubsub_tick_recv(struct p2p_instance *inst, uint64_t now) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    if (ctx->state < SIG_PUBSUB_ONLINE) return;

    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

        /* —— SUB/WAIT_OFFER: 轮询自己的 Gist 等待 offer —— */
        if (sess->state == SIG_PUBSUB_SESS_WAIT_OFFER) {
            if (!sess->heartbeat_sent) continue;  /* 先等 tick_send 写入心跳 */
            if (tick_diff(now, sess->last_poll) < P2P_PUBSUB_POLL_MAILBOX_MS) continue;
            sess->last_poll = now;

            char content[4096];
            if (poll_from_gist(ctx, ctx->local_gist_id, content, (int)sizeof(content)) != 0) {
                print("V:", LA_F("%s: mailbox empty, waiting", LA_F142, 142), TASK_POLL);
                continue;
            }

            /* 检查是否是 offer */
            if (strncmp(content, "OFFER:", 6) == 0) {
                const char *pub_gist_id = content + 6;
                strncpy(sess->remote_gist_id, pub_gist_id, sizeof(sess->remote_gist_id) - 1);
                sess->state = SIG_PUBSUB_SESS_SYNCING;
                print("I:", LA_F("%s: received offer from %s → SYNCING", LA_F351, 351),
                      TASK_POLL, pub_gist_id);
            }
            continue;
        }

        /* —— PUB/OFFERING: 轮询 SUB 的 Gist 等待候选响应 —— */
        if (sess->state == SIG_PUBSUB_SESS_OFFERING) {
            if (!sess->offer_sent) continue;  /* 先等 tick_send 投递 offer */
            if (tick_diff(now, sess->last_poll) < P2P_PUBSUB_POLL_SYNC_MS) continue;
            sess->last_poll = now;

            char content[4096];
            if (poll_from_gist(ctx, sess->remote_gist_id, content, (int)sizeof(content)) != 0) {
                print("V:", LA_F("%s: SUB gist empty, waiting", LA_F142, 142), TASK_POLL);
                continue;
            }

            /* 仍是 offer → SUB 还没响应 */
            if (strncmp(content, "OFFER:", 6) == 0) continue;

            /*
             * SUB 心跳覆盖了 offer → 并发写入竞争
             * 重置 offer_sent，让 tick_send 重新投递
             */
            if (strncmp(content, "ONLINE:", 7) == 0) {
                print("W:", LA_F("%s: offer overwritten by SUB heartbeat, will resend", LA_F351, 351),
                      TASK_POLL);
                sess->offer_sent = false;
                continue;
            }

            /* 不是 offer 也不是心跳 → SUB 已响应,内容是候选数据 */
            int added = decode_and_inject_candidates(s, key, content);
            if (added > 0) {
                print("I:", LA_F("%s: SUB responded with %d candidates", LA_F351, 351),
                      TASK_POLL, added);
                sess->remote_received = true;
                s->remote_cand_done = true;
                nat_punch(s, -1);
                sess->state = SIG_PUBSUB_SESS_SYNCING;
                print("I:", LA_F("%s: → SYNCING", LA_F475, 475), TASK_POLL);
            }
            continue;
        }

        /* —— SYNCING: 轮询对端 Gist 获取候选 —— */
        if (sess->state == SIG_PUBSUB_SESS_SYNCING && !sess->remote_received) {
            if (tick_diff(now, sess->last_poll) < P2P_PUBSUB_POLL_SYNC_MS) continue;
            sess->last_poll = now;

            /* SUB 轮询 PUB 的 Gist（remote_gist_id）*/
            char content[4096];
            if (poll_from_gist(ctx, sess->remote_gist_id, content, (int)sizeof(content)) != 0) {
                print("V:", LA_F("%s: GET %s — empty or failed", LA_F142, 142), TASK_POLL, sess->remote_gist_id);
                continue;
            }

            /* 跳过 offer 内容（可能是 PUB 还没发布候选）*/
            if (strncmp(content, "OFFER:", 6) == 0) continue;

            int added = decode_and_inject_candidates(s, key, content);
            if (added > 0) {
                print("I:", LA_F("%s: received %d candidates from %s", LA_F351, 351),
                      TASK_POLL, added, sess->remote_gist_id);

                sess->remote_received = true;
                s->remote_cand_done = true;
                nat_punch(s, -1);

                if (sess->local_published) {
                    sess->state = SIG_PUBSUB_SESS_READY;
                    print("I:", LA_F("%s: → READY", LA_F475, 475), TASK_POLL);
                }
            }
        }
    }
}

/*
 * 阶段 8：信令推送
 *
 *   SUB/WAIT_OFFER: 清空信箱（PATCH 自己的 Gist 为空）
 *   PUB/OFFERING:   投递 offer 到 SUB 的 Gist
 *   SYNCING:        发布本端候选到自己的 Gist
 */
void p2p_signal_pubsub_tick_send(struct p2p_instance *inst, uint64_t now) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    if (ctx->state < SIG_PUBSUB_ONLINE) return;

    (void)now;

    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

        /* —— SUB/WAIT_OFFER: 写入心跳时间戳（首次 + 每 5 分钟刷新）—— */
        if (sess->state == SIG_PUBSUB_SESS_WAIT_OFFER) {
            bool need = !sess->heartbeat_sent ||
                        tick_diff(now, sess->last_heartbeat) >= (uint64_t)P2P_PUBSUB_HEARTBEAT_SEC * 1000;
            if (need) {
                char ts_str[64];
                snprintf(ts_str, sizeof(ts_str), "ONLINE:%lld", (long long)time(NULL));

                print("I:", LA_F("%s: writing heartbeat (gist=%s) %s", LA_F423, 423),
                      TASK_PUBLISH, ctx->local_gist_id, ts_str);

                if (write_gist(ctx, ctx->local_gist_id, ts_str) == 0) {
                    sess->heartbeat_sent = true;
                    sess->last_heartbeat = now;
                    print("I:", LA_F("%s: heartbeat written", LA_F260, 260), TASK_PUBLISH);
                } else {
                    print("W:", LA_F("%s: heartbeat write failed", LA_F439, 439), TASK_PUBLISH);
                }
            }
            continue;
        }

        /* —— PUB/OFFERING: 检测 SUB 在线状态，投递 offer 到 SUB 的 Gist —— */
        if (sess->state == SIG_PUBSUB_SESS_OFFERING && !sess->offer_sent) {
            /* 先读取 SUB 的 Gist，检查心跳时间戳 */
            char probe[4096];
            if (poll_from_gist(ctx, sess->remote_gist_id, probe, (int)sizeof(probe)) == 0) {
                if (strncmp(probe, "ONLINE:", 7) == 0) {
                    time_t sub_ts = (time_t)strtoll(probe + 7, NULL, 10);
                    time_t now_sec = time(NULL);
                    long long age = (long long)(now_sec - sub_ts);
                    if (age > P2P_PUBSUB_HEARTBEAT_SEC) {
                        print("W:", LA_F("%s: SUB heartbeat stale (%llds ago, threshold %ds), may be offline",
                              LA_F351, 351), TASK_POLL, age, P2P_PUBSUB_HEARTBEAT_SEC);
                    } else {
                        print("I:", LA_F("%s: SUB online (heartbeat %llds ago)", LA_F351, 351),
                              TASK_POLL, age);
                    }
                } else {
                    print("W:", LA_F("%s: SUB gist not ONLINE (content: %.20s...)", LA_F351, 351),
                          TASK_POLL, probe);
                }
            } else {
                print("W:", LA_F("%s: cannot read SUB gist %s", LA_F351, 351),
                      TASK_POLL, sess->remote_gist_id);
            }

            /* 构造 offer: "OFFER:<local_gist_id>" */
            char offer[256];
            snprintf(offer, sizeof(offer), "OFFER:%s", ctx->local_gist_id);

            print("I:", LA_F("%s: sending offer to %s", LA_F423, 423),
                  TASK_PUBLISH, sess->remote_gist_id);

            if (write_gist(ctx, sess->remote_gist_id, offer) == 0) {
                sess->offer_sent = true;
                print("I:", LA_F("%s: offer sent (my gist=%s)", LA_F260, 260),
                      TASK_PUBLISH, ctx->local_gist_id);
            } else {
                print("W:", LA_F("%s: send offer failed", LA_F439, 439), TASK_PUBLISH);
            }
            continue;
        }

        /* —— SYNCING: 发布本端候选到自己的 Gist —— */
        if (sess->state == SIG_PUBSUB_SESS_SYNCING && !sess->local_published) {
            if (s->local_cand_cnt == 0) continue;
            if (P2P_CAND_PENDING(inst)) continue;

            char b64[4096];
            int b64_len = encode_candidates(s, key, b64, (int)sizeof(b64));
            if (b64_len <= 0) continue;

            print("I:", LA_F("%s: publishing %d candidates to local gist", LA_F423, 423),
                  TASK_PUBLISH, s->local_cand_cnt);

            if (write_gist(ctx, ctx->local_gist_id, b64) == 0) {
                sess->local_published = true;
                sess->local_published_cnt = s->local_cand_cnt;
                print("I:", LA_F("%s: published %d candidates", LA_F260, 260),
                      TASK_PUBLISH, s->local_cand_cnt);

                if (sess->remote_received) {
                    sess->state = SIG_PUBSUB_SESS_READY;
                    print("I:", LA_F("%s: → READY", LA_F475, 475), TASK_PUBLISH);
                }
            } else {
                print("W:", LA_F("%s: PATCH failed", LA_F439, 439), TASK_PUBLISH);
            }
        }
    }
}
