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

    /* 从 Gist API 响应中提取 p2p_signal.json 的 content 字段 */
    int ret = -1;
    const char *file_sec = strstr(resp, "\"p2p_signal.json\"");
    if (file_sec) {
        const char *cnt_key = strstr(file_sec, "\"content\"");
        if (cnt_key) {
            const char *cs = strchr(cnt_key + 9, '\"');
            if (cs) {
                cs++;  /* skip opening quote */
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
                        break;
                    } else {
                        *dst++ = *p++;
                    }
                }
                *dst = '\0';
                ret = 0;
            }
        }
    }
    free(resp);

    /* 内容过短视为空 */
    if (ret == 0 && (int)strlen(out) < 10) return -1;
    return ret;
}

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
 * Base64 解码 → DES 解密 → SDP 解析候选列表并注入会话
 *
 * @param s       会话（注入 remote_cands）
 * @param key     DES 密钥
 * @param b64     Base64 编码的密文
 * @return        新增的候选数量
 */
static int unpack_remote_candidates(struct p2p_session *s, const uint8_t key[8], const char *b64) {
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

/* ============================================================================
 * 协议操作函数（静态）— 每个函数对应一个协议操作项
 *
 * 传输层：PATCH/GET https://api.github.com/gists/{gist_id}
 * 存储文件：p2p_signal.json
 * 内容格式：纯文本字符串（非 JSON），三种互斥协议：
 *
 * 心跳协议（SUB → 自己的 Gist）：
 *   "ONLINE:" timestamp(ascii_decimal) ":" peer_id(string)
 *   - timestamp: Unix 时间戳（秒），time(NULL) 的十进制文本
 *   - peer_id:   本端 peer ID（local_peer_id）
 *   - 示例: "ONLINE:1713100800:alice"
 *
 * 邀约协议（PUB → SUB 的 Gist）：
 *   "OFFER:" pub_gist_id(hex_string, 32) ":" peer_id(string)
 *   - pub_gist_id: PUB 方的 Gist ID（GitHub Gist 的 32 位十六进制 ID）
 *   - peer_id:     PUB 方的 peer ID（local_peer_id）
 *   - 示例: "OFFER:a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4:bob"
 *
 * 候选协议（双方 → 自己的 Gist）：
 *   base64( des_ecb( sdp_candidates ) )
 *   - sdp_candidates: p2p_ice_export_sdp() 输出的 "a=candidate:..." 文本
 *   - des_ecb: DES ECB 模式，密钥 = derive_key(auth_key) → 8 字节
 *              明文按 8 字节块对齐（补零）
 *   - base64: 标准 Base64 编码
 *
 * 操作矩阵：
 *   函数                 角色   HTTP          Gist    写入内容
 *   sync0_sub()          SUB    PATCH local   自己    "ONLINE:<ts>:<peer_id>"
 *   poll_offer()         SUB    GET local     自己    检测 "OFFER:*"
 *   sync0_offer()        PUB    GET+PATCH     对方    "OFFER:<gist_id>:<peer_id>"
 *   poll_answer()        PUB    GET remote    对方    检测候选/ONLINE/OFFER
 *   sync_candidates()    双方   PATCH local   自己    Base64(DES(SDP))
 *   poll_candidates()    双方   GET remote    对方    解码候选
 * ============================================================================ */

/*
 * SUB: 写入心跳到自己的 Gist
 *
 * PATCH local_gist
 * 内容: "ONLINE:" timestamp ":" peer_id
 *   - timestamp: time(NULL) 的十进制文本（秒）
 *   - peer_id:   ctx->local_peer_id
 *
 * connect() 时立即调用，tick_send 每 P2P_PUBSUB_HEARTBEAT_SEC 秒刷新。
 */
static void sync0_sub(struct p2p_instance *inst, struct p2p_session *s, uint64_t now) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    char ts_str[128];
    snprintf(ts_str, sizeof(ts_str), "ONLINE:%lld:%s", (long long)time(NULL), ctx->local_peer_id);

    print("I:", LA_F("%s: writing heartbeat (gist=%s) %s", LA_F423, 423),
          TASK_PUBLISH, ctx->local_gist_id, ts_str);

    if (write_gist(ctx, ctx->local_gist_id, ts_str) == 0) {
        sess->last_sub = now;
        print("I:", LA_F("%s: heartbeat written", LA_F260, 260), TASK_PUBLISH);
    } else {
        print("W:", LA_F("%s: heartbeat write failed", LA_F439, 439), TASK_PUBLISH);
    }
}

/*
 * SUB/WAIT_OFFER: 轮询自己的 Gist→ 检测 "OFFER:" 前缀
 *
 * GET local_gist
 * 匹配: "OFFER:" pub_gist_id(hex_string, 32) ":" peer_id(string)
 *   - 提取 pub_gist_id → remote_gist_id
 *   - 提取 peer_id     → remote_peer_id
 *   - → SYNCING
 */
static bool poll_offer(struct p2p_instance *inst, struct p2p_session *s) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    char content[4096];
    if (poll_from_gist(ctx, ctx->local_gist_id, content, (int)sizeof(content)) != 0) {
        print("V:", LA_F("%s: mailbox empty, waiting", LA_F142, 142), TASK_POLL);
        return false;
    }

    /* 检查是否是 offer: "OFFER:<gist_id>:<peer_id>" */
    if (strncmp(content, "OFFER:", 6) == 0) {
        const char *pub_gist_id = content + 6;
        /* 分离 gist_id 和 peer_id */
        const char *colon = strchr(pub_gist_id, ':');
        if (colon) {
            size_t gid_len = (size_t)(colon - pub_gist_id);
            if (gid_len >= sizeof(sess->remote_gist_id)) gid_len = sizeof(sess->remote_gist_id) - 1;
            memcpy(sess->remote_gist_id, pub_gist_id, gid_len);
            sess->remote_gist_id[gid_len] = '\0';
            strncpy(sess->remote_peer_id, colon + 1, sizeof(sess->remote_peer_id) - 1);
            sess->remote_peer_id[sizeof(sess->remote_peer_id) - 1] = '\0';
        } else {
            strncpy(sess->remote_gist_id, pub_gist_id, sizeof(sess->remote_gist_id) - 1);
            sess->remote_gist_id[sizeof(sess->remote_gist_id) - 1] = '\0';
        }
        sess->state = SIG_PUBSUB_SESS_SYNCING;
        nat_punch(s, -1);
        print("I:", LA_F("%s: received offer from %s (peer=%s) → SYNCING", LA_F351, 351),
              TASK_POLL, sess->remote_gist_id, sess->remote_peer_id);
        return true;
    }
    return false;
}

/*
 * PUB: 检测 SUB 在线 + 投递 offer 到 SUB 的 Gist
 *
 * GET remote_gist  → 读取心跳检查 SUB 是否在线
 * PATCH remote_gist → 写入 offer
 * 内容: "OFFER:" local_gist_id ":" peer_id
 *   - local_gist_id: PUB 方的 Gist ID，告知 SUB 到哪里轮询候选
 *   - peer_id:       ctx->local_peer_id
 *
 * connect() 时立即调用（resend=false）。
 * poll_answer() 检测到 ONLINE 竞争时立即调用（resend=true，跳过心跳检测）。
 */
static void sync0_offer(struct p2p_instance *inst, struct p2p_session *s, bool resend) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    /* 首次发送时读取 SUB 的 Gist，检查心跳时间戳；resend 时跳过 */
    if (!resend) {
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
    }  /* !resend */

    /* 构造 offer: "OFFER:<local_gist_id>:<peer_id>" */
    char offer[256];
    snprintf(offer, sizeof(offer), "OFFER:%s:%s", ctx->local_gist_id, ctx->local_peer_id);

    if (write_gist(ctx, sess->remote_gist_id, offer) == 0) {
        sess->offer_sent = 1;  /* 已写入，待确认 */
        print("I:", LA_F("%s: offer %s (my gist=%s)", LA_F260, 260),
              TASK_PUBLISH, resend ? "resent" : "sent", ctx->local_gist_id);
    } else {
        print("W:", LA_F("%s: send offer failed", LA_F439, 439), TASK_PUBLISH);
    }
}


/*
 * GET remote_gist
 * offer_sent==1 时：确认 offer 是否仍在
 *   "OFFER:*"  → 确认成功，offer_sent=2
 *   "ONLINE:*" → 心跳覆盖，立即 sync0_offer(resend=true)
 *   其他        → SUB 已响应候选（快速路径）
 * offer_sent==2 时：等待 SUB 候选响应
 *   "OFFER:*"  → SUB 尚未响应
 *   其他        → 解码候选 → SYNCING
 */
static void poll_answer(struct p2p_instance *inst, struct p2p_session *s) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    char content[4096];
    if (poll_from_gist(ctx, sess->remote_gist_id, content, (int)sizeof(content)) != 0) {
        print("V:", LA_F("%s: SUB gist empty, waiting", LA_F142, 142), TASK_POLL);
        return;
    }

    /* SUB 心跳覆盖了 offer → 立即重发 */
    if (strncmp(content, "ONLINE:", 7) == 0) {
        print("W:", LA_F("%s: offer overwritten by SUB heartbeat, resending", LA_F351, 351),
              TASK_POLL);
        sync0_offer(inst, s, true);
        return;
    }

    /* offer 仍在 */
    if (strncmp(content, "OFFER:", 6) == 0) {
        if (sess->offer_sent == 1) {
            sess->offer_sent = 2;  /* 确认成功 */
            print("I:", LA_F("%s: offer confirmed", LA_F351, 351), TASK_POLL);
        }
        return;
    }

    /* SUB 已响应，内容是候选数据 */
    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    int added = unpack_remote_candidates(s, key, content);
    if (added > 0) {
        print("I:", LA_F("%s: SUB responded with %d candidates", LA_F351, 351),
              TASK_POLL, added);
        sess->remote_received = true;
        s->remote_cand_done = true;
        nat_punch(s, -1);
        sess->state = SIG_PUBSUB_SESS_SYNCING;
        print("I:", LA_F("%s: → SYNCING", LA_F475, 475), TASK_POLL);
    }
}

/*
 * SYNCING: 发布本端候选到自己的 Gist
 *
 * PATCH local_gist
 * 内容: base64( des_ecb( sdp_text ) )
 *   - sdp_text: p2p_ice_export_sdp(candidates_only=true) 输出的 "a=candidate:..." 文本
 *   - des_ecb:  密钥 derive_key(auth_key)，明文按 8 字节块对齐（补零）
 *   - base64:   标准 Base64 编码
 *
 * tick_send 检测 local_published=false 且候选收集完毕时调用。
 * 发布成功后若已收到对端候选 → READY
 */
static void sync_candidates(struct p2p_instance *inst, struct p2p_session *s) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    /* SDP 导出 → DES 加密 → Base64 编码 */
    int cnt = s->local_cand_cnt;
    char sdp_buf[4096];
    int sdp_len = p2p_ice_export_sdp(s->local_cands, cnt, sdp_buf, (int)sizeof(sdp_buf),
                                      true, NULL, NULL, NULL);
    if (sdp_len <= 0) return;

    int padded = (sdp_len + 7) & ~7;
    uint8_t *raw = (uint8_t *)calloc(1, (size_t)padded);
    if (!raw) return;
    memcpy(raw, sdp_buf, (size_t)sdp_len);

    uint8_t *enc = (uint8_t *)malloc((size_t)padded);
    if (!enc) { free(raw); return; }

    p2p_des_encrypt(key, raw, (size_t)padded, enc);
    free(raw);

    char b64[4096];
    int b64_len = p2p_base64_encode(enc, (size_t)padded, b64, (int)sizeof(b64));
    free(enc);
    if (b64_len <= 0) return;

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

/*
 * SYNCING: 轮询对端 Gist，解码候选
 *
 * GET remote_gist
 * 匹配:
 *   "OFFER:*" → 跳过（对端尚未发布候选）
 *   其他      → base64 → des_ecb_decrypt → sdp → inject remote_cands
 */
static void poll_candidates(struct p2p_instance *inst, struct p2p_session *s) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    char content[4096];
    if (poll_from_gist(ctx, sess->remote_gist_id, content, (int)sizeof(content)) != 0) {
        print("V:", LA_F("%s: GET %s — empty or failed", LA_F142, 142), TASK_POLL, sess->remote_gist_id);
        return;
    }

    /* 跳过 offer 内容（可能是 PUB 还没发布候选）*/
    if (strncmp(content, "OFFER:", 6) == 0) return;

    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    int added = unpack_remote_candidates(s, key, content);
    if (added > 0) {
        print("I:", LA_F("%s: received %d candidates from %s", LA_F351, 351),
              TASK_POLL, added, sess->remote_gist_id);

        sess->remote_received = true;
        s->remote_cand_done = true;

        if (sess->local_published) {
            sess->state = SIG_PUBSUB_SESS_READY;
            print("I:", LA_F("%s: → READY", LA_F475, 475), TASK_POLL);
        }
    }
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

void p2p_signal_pubsub_init(p2p_signal_pubsub_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIG_PUBSUB_INIT;
}

static void reset_peer(p2p_pubsub_session_t *sess) {
    sess->remote_peer_id[0]   = '\0';
    sess->local_published     = false;
    sess->local_published_cnt = 0;
    sess->last_poll           = 0;
    sess->remote_received     = false;
    sess->last_sub      = 0;
    sess->offer_sent          = 0;
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

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

ret_t p2p_signal_pubsub_connect(struct p2p_session *s, const char *remote_gist_id) {
    p2p_signal_pubsub_ctx_t *ctx = &s->inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    if (ctx->state < SIG_PUBSUB_ONLINE) {
        print("W:", LA_F("CONNECT: instance not online yet", 0, 0));
        return E_NONE_CONTEXT;
    }

    /* 幂等：相同 target 则成功，不同则忙 */
    if (sess->remote_gist_id[0]) {
        if (remote_gist_id && remote_gist_id[0])
            return strncmp(sess->remote_gist_id, remote_gist_id, sizeof(sess->remote_gist_id)) == 0 ? E_NONE : E_BUSY;
        return E_BUSY;
    }

    // SUB 模式：不知道对方，等待 offer
    if (!remote_gist_id || !remote_gist_id[0]) {
        sess->is_pub = false;
        sess->state = SIG_PUBSUB_SESS_WAIT_OFFER;
        print("I:", LA_F("CONNECT SUB: waiting for offer in mailbox (gist=%s)", 0, 0), ctx->local_gist_id);
        sync0_sub(s->inst, s, P_tick_ms());
    }
    // PUB 模式：知道对方的 gist_id，主动发起
    else {
        sess->is_pub = true;
        strncpy(sess->remote_gist_id, remote_gist_id, sizeof(sess->remote_gist_id) - 1);
        sess->state = SIG_PUBSUB_SESS_OFFERING;
        print("I:", LA_F("CONNECT PUB: target=%s", 0, 0), remote_gist_id);
        sync0_offer(s->inst, s, false);
    }

    return E_NONE;
}

ret_t p2p_signal_pubsub_disconnect(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;
    if (!sess->remote_gist_id[0]) return E_NONE;  /* 没有建立过配对 */

    sess->state             = SIG_PUBSUB_SESS_IDLE;
    sess->is_pub            = false;
    sess->remote_gist_id[0] = '\0';
    reset_peer(sess);

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

/* ============================================================================
 * tick 循环（状态机推进 + 定时调用协议操作）
 * ============================================================================ */

/*
 * 阶段 2：信令拉取（轮询对端 / 自己的 Gist）
 */
void p2p_signal_pubsub_tick_recv(struct p2p_instance *inst, uint64_t now) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    if (ctx->state < SIG_PUBSUB_ONLINE) return;

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

        // SUB 等待 offer 阶段
        if (sess->state == SIG_PUBSUB_SESS_WAIT_OFFER) {

            // 定时监测是否收到 offer
            if (tick_diff(now, sess->last_poll) < P2P_PUBSUB_POLL_MAILBOX_MS) continue;
            sess->last_poll = now;
            if (!poll_offer(inst, s)) {

                // 没有 offer，检查心跳是否需要刷新
                if (tick_diff(now, sess->last_sub) >= (uint64_t)P2P_PUBSUB_HEARTBEAT_SEC * 1000)
                    sync0_sub(inst, s, now);
            }
            continue;
        }

        // PUB 已发送 offer，等待确认或 SUB 响应
        if (sess->state == SIG_PUBSUB_SESS_OFFERING) {
            if (!sess->offer_sent) continue;
            
            // 定时监测是否收到确认或 SUB 响应
            uint64_t interval = (sess->offer_sent == 1)
                ? P2P_PUBSUB_POLL_CONFIRM_MS    /* 待确认，快速检测和 sub 的心跳写操作的竞争 */
                : P2P_PUBSUB_POLL_SYNC_MS;      /* 已确认，等待 SUB 响应 */
            if (tick_diff(now, sess->last_poll) < interval) continue;
            sess->last_poll = now;
            poll_answer(inst, s);
            continue;
        }

        if (sess->state == SIG_PUBSUB_SESS_SYNCING && !sess->remote_received) {
            if (tick_diff(now, sess->last_poll) < P2P_PUBSUB_POLL_SYNC_MS) continue;
            sess->last_poll = now;
            poll_candidates(inst, s);
        }
    }
}

/*
 * 阶段 8：信令推送（心跳 / offer / 候选发布）
 */
void p2p_signal_pubsub_tick_send(struct p2p_instance *inst, uint64_t now) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    if (ctx->state < SIG_PUBSUB_ONLINE) return;

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

        if (sess->state == SIG_PUBSUB_SESS_SYNCING && !sess->local_published) {
            if (s->local_cand_cnt == 0) continue;
            if (P2P_CAND_PENDING(inst)) continue;
            sync_candidates(inst, s);
        }
    }
}
