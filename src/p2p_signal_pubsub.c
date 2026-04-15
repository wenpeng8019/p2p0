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
 * 写入发布板指定文件（PATCH Gist）
 *
 * @param ctx      实例上下文
 * @param gist_id  Gist ID
 * @param filename 文件名（peer_id）
 * @param content  写入内容
 * @return         0=成功
 */
static int gist_write(p2p_signal_pubsub_ctx_t *ctx, const char *gist_id, const char *filename, const char *content) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/gists/%s", gist_id);

    size_t content_len = strlen(content);
    size_t fname_len = strlen(filename);
    size_t body_sz = content_len + fname_len + 128;
    char *body = (char *)malloc(body_sz);
    if (!body) return -1;

    snprintf(body, body_sz,
             "{\"files\":{\"%.64s\":{\"content\":\"%s\"}}}", filename, content);

    int ret = p2p_http_patch(url, ctx->auth_token, body);
    free(body);
    return ret;
}

/*
 * 读取发布板指定文件（GET Gist）
 *
 * @param ctx       实例上下文
 * @param gist_id   Gist ID
 * @param filename  文件名（peer_id）
 * @param out       输出内容缓冲区
 * @param out_sz    缓冲区大小
 * @return          0=成功且有内容，-1=失败或无内容
 */
static int gist_poll(p2p_signal_pubsub_ctx_t *ctx, const char *gist_id, const char *filename, char *out, int out_sz) {

    snprintf(out, out_sz, "https://api.github.com/gists/%s", gist_id);

    static char resp[32768];
    int got = p2p_http_get(out, ctx->auth_token, resp, (int)sizeof(resp));
    if (got <= 0) {
        return -1;
    }

    /* 从 Gist API 响应中提取指定文件的 content 字段 */
    int ret = -1;
    char file_key[256];
    snprintf(file_key, sizeof(file_key), "\"%.64s\"", filename);
    const char *file_sec = strstr(resp, file_key);
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
 * 存储文件：以 peer_id 为文件名（每个 peer 对应 Gist 中的一个文件）
 * 地址模型：<gist_id>/<peer_id> 唯一标识一个信箱
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
 *   version(ascii_decimal) ":" base64( des_ecb( sdp_candidates ) )
 *   - version: 1,2,3... = trickle 增量版本；0 = 全部发送完成
 *   - sdp_candidates: p2p_ice_export_sdp() 输出的 "a=candidate:..." 文本
 *   - des_ecb: DES ECB 模式，密钥 = derive_key(auth_key) → 8 字节
 *              明文按 8 字节块对齐（补零）
 *   - base64: 标准 Base64 编码
 *   - 示例: "2:ABCDef..." (第 2 次 trickle)
 *   - 示例: "0:ABCDef..." (全部完成)
 *
 * 操作矩阵：
 *   函数                 角色   HTTP          文件(peer_id)   写入内容
 *   sync0_sub()          SUB    PATCH local   local_peer      "ONLINE:<ts>:<peer_id>"
 *   poll_offer()         SUB    GET local     local_peer      检测 "OFFER:*"
 *   sync0_offer()        PUB    GET+PATCH     remote_peer     "OFFER:<gist_id>:<peer_id>"
 *   poll_answer()        PUB    GET remote    remote_peer     检测候选/ONLINE/OFFER
 *   sync_candidates()    双方   PATCH local   local_peer      "<ver>:" Base64(DES(SDP))
 *   poll_candidates()    双方   GET remote    remote_peer     解码候选
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

    if (gist_write(ctx, ctx->local_gist_id, ctx->local_peer_id, ts_str) == 0) {
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
    if (gist_poll(ctx, ctx->local_gist_id, ctx->local_peer_id, content, (int)sizeof(content)) != 0) {
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
        nat_punch(s, -1);
        if (P2P_SESSION_WAITING_STUN(s)) {
            sess->state = SIG_PUBSUB_SESS_WAIT_STUN;
            print("I:", LA_F("%s: received offer from %s (peer=%s) → WAIT_STUN", LA_F351, 351),
                  TASK_POLL, sess->remote_gist_id, sess->remote_peer_id);
        } else {
            sess->state = SIG_PUBSUB_SESS_SYNCING;
            print("I:", LA_F("%s: received offer from %s (peer=%s) → SYNCING", LA_F351, 351),
                  TASK_POLL, sess->remote_gist_id, sess->remote_peer_id);
        }
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
        if (gist_poll(ctx, sess->remote_gist_id, sess->remote_peer_id, probe, (int)sizeof(probe)) == 0) {
            if (strncmp(probe, "ONLINE:", 7) == 0) {
                time_t sub_ts = (time_t)strtoll(probe + 7, NULL, 10);
                time_t now_sec = time(NULL);
                long long age = (long long)(now_sec - sub_ts);
                if (age > P2P_PUBSUB_HEARTBEAT_SEC) {
                    print("W:", LA_F("%s: SUB heartbeat stale (%llds ago, threshold %ds), may be offline",
                        LA_F351, 351), TASK_POLL, age, P2P_PUBSUB_HEARTBEAT_SEC);
                } else {
                    print("I:", LA_F("%s: SUB online (heartbeat %llds ago), early nat_punch", LA_F351, 351),
                        TASK_POLL, age);
                    nat_punch(s, -1);  /* 提前启动 STUN 收集，nat_punch 幂等（state>=PUNCHING 时跳过）*/
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

    if (gist_write(ctx, sess->remote_gist_id, sess->remote_peer_id, offer) == 0) {
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
    if (gist_poll(ctx, sess->remote_gist_id, sess->remote_peer_id, content, (int)sizeof(content)) != 0) {
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

    /* SUB 已响应，内容是候选数据: "<ver>:<b64>" */
    char *colon = strchr(content, ':');
    if (!colon) return;
    int ver = atoi(content);
    sess->remote_sync_ver = ver;

    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    int added = unpack_remote_candidates(s, key, colon + 1);
    if (added > 0) {
        print("I:", LA_F("%s: SUB responded with %d candidates (ver=%d)", LA_F351, 351),
              TASK_POLL, added, ver);
        if (ver == 0) {
            s->remote_cand_done = true;
        }
        nat_punch(s, -1);
        if (P2P_SESSION_WAITING_STUN(s)) {
            sess->state = SIG_PUBSUB_SESS_WAIT_STUN;
            print("I:", LA_F("%s: → WAIT_STUN", LA_F475, 475), TASK_POLL);
        } else {
            sess->state = SIG_PUBSUB_SESS_SYNCING;
            print("I:", LA_F("%s: → SYNCING", LA_F475, 475), TASK_POLL);
        }
    }
}

/*
 * SYNCING: 发布本端候选到自己的 Gist
 *
 * PATCH local_gist
 * 内容: version ":" base64( des_ecb( sdp_text ) )
 *   - version:  >=1 trickle 增量, 0=全部完成
 *   - sdp_text: p2p_ice_export_sdp(candidates_only=true) 输出的 "a=candidate:..." 文本
 *   - des_ecb:  密钥 derive_key(auth_key)，明文按 8 字节块对齐（补零）
 *   - base64:   标准 Base64 编码
 *
 * tick_send: 有新候选时 trickle 发布（ver>=1），候选收集完毕时发终版（ver=0）。
 * 本端发布 ver=0 成功 → READY（与 relay/compact 一致，只关注本端同步完成）
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

    /* 版本号: >=1 trickle 增量, 0=全部完成 */
    bool final = !P2P_CAND_PENDING(inst);
    int ver = final ? 0 : ++sess->local_sync_ver;

    char payload[4200];
    snprintf(payload, sizeof(payload), "%d:%s", ver, b64);

    print("I:", LA_F("%s: publishing %d candidates (ver=%d) to local gist", LA_F423, 423),
          TASK_PUBLISH, s->local_cand_cnt, ver);

    if (gist_write(ctx, ctx->local_gist_id, ctx->local_peer_id, payload) == 0) {
        sess->candidate_synced_count = s->local_cand_cnt;
        print("I:", LA_F("%s: published %d candidates (ver=%d)", LA_F260, 260),
              TASK_PUBLISH, s->local_cand_cnt, ver);

        if (final) {
            sess->local_sync_ver = 0;
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
 *   "OFFER:*"     → 跳过（对端尚未发布候选）
 *   "<ver>:<b64>" → 解码候选，ver==0 表示对端全部完成
 */
static void poll_candidates(struct p2p_instance *inst, struct p2p_session *s) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    char content[4096];
    if (gist_poll(ctx, sess->remote_gist_id, sess->remote_peer_id, content, (int)sizeof(content)) != 0) {
        print("V:", LA_F("%s: GET %s — empty or failed", LA_F142, 142), TASK_POLL, sess->remote_gist_id);
        return;
    }

    /* 跳过 offer 内容（可能是 PUB 还没发布候选）*/
    if (strncmp(content, "OFFER:", 6) == 0) return;

    /* 解析版本号: "<ver>:<b64>" */
    char *colon = strchr(content, ':');
    if (!colon) return;
    int ver = atoi(content);
    if (ver == sess->remote_sync_ver) return;  /* 同版本已处理 */
    sess->remote_sync_ver = ver;

    uint8_t key[8];
    derive_key(ctx->auth_key, key);

    int added = unpack_remote_candidates(s, key, colon + 1);
    if (added > 0) {
        print("I:", LA_F("%s: received %d candidates (ver=%d) from %s", LA_F351, 351),
              TASK_POLL, added, ver, sess->remote_gist_id);
    }

    /* ver==0: 对端全部候选发送完成 */
    if (ver == 0) {
        s->remote_cand_done = true;
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
    sess->candidate_synced_count = 0;
    sess->last_poll           = 0;
    sess->last_sync           = 0;
    sess->remote_sync_ver     = -1;
    sess->last_sub            = 0;
    sess->offer_sent          = 0;
    sess->local_sync_ver      = 0;
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

ret_t p2p_signal_pubsub_online(struct p2p_instance *inst, const char *local_peer_id,
                                const char *token, const char *gist_id) {
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;

    /* peer_id 用作 Gist 文件名，不允许含 '/' */
    if (local_peer_id && strchr(local_peer_id, '/')) {
        print("E:", LA_F("ONLINE: peer_id cannot contain '/' (got \"%s\")", 0, 0), local_peer_id);
        return E_INVALID;
    }

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

ret_t p2p_signal_pubsub_connect(struct p2p_session *s, const char *remote_addr) {
    p2p_signal_pubsub_ctx_t *ctx = &s->inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    if (ctx->state < SIG_PUBSUB_ONLINE) {
        print("W:", LA_F("CONNECT: instance not online yet", 0, 0));
        return E_NONE_CONTEXT;
    }

    /* 幂等：已有活跃会话 → 相同 target 成功，不同则忙 */
    if (sess->remote_gist_id[0]) {
        if (remote_addr && remote_addr[0]) {
            /* 重构 target 地址并比较 */
            const char *slash = strchr(remote_addr, '/');
            const char *cmp_gist = slash ? remote_addr : ctx->local_gist_id;
            size_t cmp_gist_len = slash ? (size_t)(slash - remote_addr) : strlen(ctx->local_gist_id);
            const char *cmp_peer = slash ? slash + 1 : remote_addr;
            if (strncmp(sess->remote_gist_id, cmp_gist, cmp_gist_len) == 0
                && sess->remote_gist_id[cmp_gist_len] == '\0'
                && strcmp(sess->remote_peer_id, cmp_peer) == 0)
                return E_NONE;
        }
        return E_BUSY;
    }

    sess->remote_sync_ver = -1;

    // SUB 模式：不知道对方，等待 offer
    if (!remote_addr || !remote_addr[0]) {
        sess->is_pub = false;
        sess->state = SIG_PUBSUB_SESS_WAIT_OFFER;
        print("I:", LA_F("CONNECT SUB: waiting for offer (gist=%s/%s)", 0, 0),
              ctx->local_gist_id, ctx->local_peer_id);
        sync0_sub(s->inst, s, P_tick_ms());
    }
    // PUB 模式：解析对方地址，主动发起
    else {
        sess->is_pub = true;

        /* 解析 remote_addr: "gist_id/peer_id" 或 "peer_id"（同 gist） */
        const char *slash = strchr(remote_addr, '/');
        if (slash) {
            size_t gid_len = (size_t)(slash - remote_addr);
            if (gid_len >= sizeof(sess->remote_gist_id)) gid_len = sizeof(sess->remote_gist_id) - 1;
            memcpy(sess->remote_gist_id, remote_addr, gid_len);
            sess->remote_gist_id[gid_len] = '\0';
            strncpy(sess->remote_peer_id, slash + 1, sizeof(sess->remote_peer_id) - 1);
            sess->remote_peer_id[sizeof(sess->remote_peer_id) - 1] = '\0';
        } else {
            /* 无 "/"：同 gist 模式，remote_gist = local_gist */
            strncpy(sess->remote_gist_id, ctx->local_gist_id, sizeof(sess->remote_gist_id) - 1);
            sess->remote_gist_id[sizeof(sess->remote_gist_id) - 1] = '\0';
            strncpy(sess->remote_peer_id, remote_addr, sizeof(sess->remote_peer_id) - 1);
            sess->remote_peer_id[sizeof(sess->remote_peer_id) - 1] = '\0';
        }

        sess->state = SIG_PUBSUB_SESS_OFFERING;
        print("I:", LA_F("CONNECT PUB: target=%s/%s", 0, 0),
              sess->remote_gist_id, sess->remote_peer_id);
        sync0_offer(s->inst, s, false);
    }

    return E_NONE;
}

void p2p_signal_pubsub_disconnect(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;
    if (!sess->remote_gist_id[0]) return;

    sess->state             = SIG_PUBSUB_SESS_IDLE;
    sess->is_pub            = false;
    sess->remote_gist_id[0] = '\0';
    reset_peer(sess);

    print("I:", LA_F("DISCONNECT", LA_F268, 268));
}

//-----------------------------------------------------------------------------

void p2p_signal_pubsub_stun_ready(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    if (sess->state != SIG_PUBSUB_SESS_WAIT_STUN) return;

    sess->state = SIG_PUBSUB_SESS_SYNCING;
    print("I:", LA_F("%s: STUN ready → SYNCING", LA_F475, 475), TASK_PUBLISH);

    /* 首次发布：有候选则立即同步，否则等 tick_send */
    if (s->local_cand_cnt > 0 || !P2P_CAND_PENDING(s->inst)) {
        sync_candidates(s->inst, s);
        sess->last_sync = P_tick_ms();
    }
}

void p2p_signal_pubsub_trickle_candidate(struct p2p_session *s) {
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;
    if (sess->state != SIG_PUBSUB_SESS_SYNCING) return;

    /* 还没进入 trickle 阶段（首次 sync 尚未执行）*/
    if (!sess->last_sync) return;

    /* 无新候选 */
    if (sess->candidate_synced_count >= s->local_cand_cnt) return;

    /* 候选收集全部完成 → 立即发终版（绕过攒批）*/
    if (!P2P_CAND_PENDING(s->inst)) {
        sync_candidates(s->inst, s);
        sess->last_sync = P_tick_ms();
        return;
    }

    /* 攒批窗口到期 → 发 trickle */
    if (tick_diff(P_tick_ms(), sess->last_sync) >= P2P_PUBSUB_TRICKLE_BATCH_MS) {
        sync_candidates(s->inst, s);
        sess->last_sync = P_tick_ms();
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

        if (sess->state >= SIG_PUBSUB_SESS_SYNCING && sess->remote_sync_ver != 0) {
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

        if (sess->state == SIG_PUBSUB_SESS_SYNCING) {

            /* 触发: 有新候选未发布 || 已发 trickle 但收集完毕需发终版(ver=0) */
            bool need_trickle = sess->candidate_synced_count < s->local_cand_cnt;
            bool need_final = !need_trickle && sess->local_sync_ver > 0 && !P2P_CAND_PENDING(inst);
            if (!need_trickle && !need_final) continue;

            /* trickle 攒批：等待窗口到期再发，final 立即发 */
            if (need_trickle && sess->last_sync &&
                tick_diff(now, sess->last_sync) < P2P_PUBSUB_TRICKLE_BATCH_MS) continue;
            sync_candidates(inst, s);
            sess->last_sync = now;
        }
    }
}

/* ============================================================================
 * 单元测试
 *
 * 构建：
 *   cd build && cmake .. && make test_pubsub
 *
 * 运行（纯本地，无需网络）：
 *   ./test/test_pubsub
 *
 * 运行（含 Gist 协议测试，需要 GitHub PAT + Gist ID）：
 *   P2P_TEST_TOKEN="ghp_xxx" P2P_TEST_GIST="<gist_id>" ./test/test_pubsub
 *
 * CTest：
 *   ctest -R test_pubsub -V
 * ============================================================================ */
#ifdef TEST_PUBSUB

#include "test_framework.h"

/* --- fake 桩 --- */

/* p2p.c 全局变量 */
p2p_log_callback_t p2p_log_callback = (p2p_log_callback_t)-1;
p2p_log_level_t    p2p_log_level    = P2P_LOG_LEVEL_DEBUG;
bool               p2p_log_pre_tag  = false;
uint16_t           p2p_instrument_base = 0;

ret_t nat_punch(struct p2p_session *s, int idx) { (void)s; (void)idx; return 0; }
void path_stats_init(path_stats_t *st, int cost_score) { (void)st; (void)cost_score; }
int p2p_stun_build_ice_check(uint8_t *buf, int max_len, uint8_t tsx_id[12],
                             const char *username, const char *password,
                             uint32_t priority, int is_controlling,
                             uint64_t tie_breaker, int use_candidate) {
    (void)buf; (void)max_len; (void)tsx_id; (void)username; (void)password;
    (void)priority; (void)is_controlling; (void)tie_breaker; (void)use_candidate;
    return 0;
}

/* --- 测试辅助 --- */

/*
 * 环境变量：
 *   P2P_TEST_TOKEN  - GitHub PAT（必须）
 *   P2P_TEST_GIST   - Gist ID（必须，单个即可，同时充当 local/remote）
 *   P2P_TEST_KEY    - DES 加密密钥（可选，默认 "testkey1"）
 *
 * 运行后可用 curl 验证：
 *   curl -s -H "Authorization: token $P2P_TEST_TOKEN" \
 *        https://api.github.com/gists/$P2P_TEST_GIST \
 *        | jq '.files.test_pub.content'
 */

static const char *env_token;
static const char *env_gist;
static const char *env_key;

static bool env_ready(void) {
    return env_token && env_gist;
}

/* 创建一组可用于测试的 instance + session（calloc 保证全零初始化） */
static void setup_with_peer(struct p2p_instance **out_inst, struct p2p_session **out_s,
                             const char *peer_id) {
    struct p2p_instance *inst = calloc(1, sizeof(*inst));
    struct p2p_session  *s    = calloc(1, sizeof(*s));
    s->inst = inst;
    inst->sessions_head = s;

    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    strncpy(ctx->auth_token, env_token, sizeof(ctx->auth_token) - 1);
    strncpy(ctx->local_gist_id, env_gist, sizeof(ctx->local_gist_id) - 1);
    strncpy(ctx->local_peer_id, peer_id, sizeof(ctx->local_peer_id) - 1);
    strncpy(ctx->auth_key, env_key ? env_key : "testkey1", sizeof(ctx->auth_key) - 1);
    ctx->state = SIG_PUBSUB_ONLINE;

    s->sig_sess.pubsub.remote_sync_ver = -1;

    *out_inst = inst;
    *out_s    = s;
}

static void setup(struct p2p_instance **out_inst, struct p2p_session **out_s) {
    setup_with_peer(out_inst, out_s, "test_pub");
}

static void teardown(struct p2p_instance *inst, struct p2p_session *s) {
    free(s->local_cands);
    free(s->remote_cands);
    free(s);
    free(inst);
}

/* --- 测试用例 --- */

TEST(init) {
    p2p_signal_pubsub_ctx_t ctx;
    p2p_signal_pubsub_init(&ctx);
    ASSERT_EQ(ctx.state, SIG_PUBSUB_INIT);
    ASSERT_EQ(ctx.local_peer_id[0], '\0');
}

/*
 * gist_roundtrip: 写入→读回 验证基础 HTTP 通道
 */
TEST(gist_roundtrip) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }
    struct p2p_instance *inst; struct p2p_session *s;
    setup(&inst, &s);
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;

    int ret = gist_write(ctx, env_gist, "test_pub", "TEST_ROUNDTRIP:hello");
    ASSERT_EQ(ret, 0);

    char buf[4096];
    ret = gist_poll(ctx, env_gist, "test_pub", buf, sizeof(buf));
    ASSERT_EQ(ret, 0);
    ASSERT(strncmp(buf, "TEST_ROUNDTRIP:hello", 20) == 0);

    teardown(inst, s);
}

/*
 * heartbeat_write: sync0_sub 写入心跳 → 读回验证 "ONLINE:<ts>:<peer>"
 */
TEST(heartbeat_write) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }
    struct p2p_instance *inst; struct p2p_session *s;
    setup(&inst, &s);
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;

    sync0_sub(inst, s, P_tick_ms());

    char buf[4096];
    int ret = gist_poll(ctx, env_gist, "test_pub", buf, sizeof(buf));
    ASSERT_EQ(ret, 0);
    ASSERT(strncmp(buf, "ONLINE:", 7) == 0);
    /* 检查 peer_id 出现在末尾 */
    ASSERT(strstr(buf, ":test_pub") != NULL);

    teardown(inst, s);
}

/*
 * offer_write: sync0_offer 写入邀约 → 读回验证 "OFFER:<gist>:<peer>"
 */
TEST(offer_write) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }
    struct p2p_instance *inst; struct p2p_session *s;
    setup(&inst, &s);
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    /* 先写入心跳，让 sync0_offer 读到有效内容（同一个 gist 充当 remote） */
    gist_write(ctx, env_gist, "remote_sub", "ONLINE:9999999999:remote_sub");

    sess->is_pub = true;
    strncpy(sess->remote_gist_id, env_gist, sizeof(sess->remote_gist_id) - 1);
    strncpy(sess->remote_peer_id, "remote_sub", sizeof(sess->remote_peer_id) - 1);
    sync0_offer(inst, s, false);

    ASSERT_EQ(sess->offer_sent, 1);

    /* 读回验证 offer 格式 */
    char buf[4096];
    int ret = gist_poll(ctx, env_gist, "remote_sub", buf, sizeof(buf));
    ASSERT_EQ(ret, 0);
    ASSERT(strncmp(buf, "OFFER:", 6) == 0);
    ASSERT(strstr(buf, env_gist) != NULL);       /* 包含本端 gist id */
    ASSERT(strstr(buf, ":test_pub") != NULL);    /* 包含 peer id */

    teardown(inst, s);
}

/*
 * offer_detect: 向 gist_a 写入 OFFER → poll_offer 检测并解析
 */
TEST(offer_detect) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }
    struct p2p_instance *inst; struct p2p_session *s;
    setup(&inst, &s);
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    sess->is_pub = false;
    sess->state = SIG_PUBSUB_SESS_WAIT_OFFER;

    /* 模拟 PUB 端写入 offer（用同一个 gist，remote_gist 写个假 ID 区分） */
    char fake_offer[256];
    snprintf(fake_offer, sizeof(fake_offer), "OFFER:%s:remote_pub", "fake_remote_gist_id_0123456789ab");
    gist_write(ctx, env_gist, "test_pub", fake_offer);

    bool found = poll_offer(inst, s);
    ASSERT(found);
    ASSERT(strcmp(sess->remote_gist_id, "fake_remote_gist_id_0123456789ab") == 0);
    ASSERT(strcmp(sess->remote_peer_id, "remote_pub") == 0);
    /* nat_punch 是 fake，不涉及 WAIT_STUN → 应进入 SYNCING */
    ASSERT_EQ(sess->state, SIG_PUBSUB_SESS_SYNCING);

    teardown(inst, s);
}

/*
 * candidate_roundtrip: sync_candidates 加密发布 → poll_candidates 解密接收
 *
 * 用两个 gist 模拟双方：
 *   A(inst1) 发布候选到 gist_a → B(inst2) 从 gist_a 读取
 */
TEST(candidate_roundtrip) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }

    /* --- A 端：发布候选 --- */
    struct p2p_instance *inst_a; struct p2p_session *s_a;
    setup(&inst_a, &s_a);
    p2p_pubsub_session_t *sess_a = &s_a->sig_sess.pubsub;
    sess_a->state = SIG_PUBSUB_SESS_SYNCING;

    /* 注入一个 host 候选 */
    s_a->local_cands = calloc(4, sizeof(p2p_local_candidate_entry_t));
    s_a->local_cand_cap = 4;
    s_a->local_cand_cnt = 1;
    s_a->local_cands[0].type = P2P_CAND_HOST;
    s_a->local_cands[0].addr.sin_family = AF_INET;
    s_a->local_cands[0].addr.sin_port = htons(12345);
    inet_pton(AF_INET, "192.168.1.100", &s_a->local_cands[0].addr.sin_addr);
    s_a->local_cands[0].priority = p2p_ice_calc_priority(P2P_ICE_CAND_HOST, 65535, 1);

    /* 标记候选收集完毕（srflx_active >= srflx_count） → final ver=0 */
    inst_a->srflx_count  = 0;
    inst_a->srflx_active = 0;
    inst_a->turn_pending = 0;

    sync_candidates(inst_a, s_a);
    ASSERT_EQ(sess_a->state, SIG_PUBSUB_SESS_READY);  /* final → READY */

    /* --- B 端：读取候选 --- */
    struct p2p_instance *inst_b; struct p2p_session *s_b;
    setup(&inst_b, &s_b);
    p2p_pubsub_session_t *sess_b = &s_b->sig_sess.pubsub;
    sess_b->state = SIG_PUBSUB_SESS_SYNCING;
    strncpy(sess_b->remote_gist_id, env_gist, sizeof(sess_b->remote_gist_id) - 1);
    strncpy(sess_b->remote_peer_id, "test_pub", sizeof(sess_b->remote_peer_id) - 1);

    /* 分配远端候选缓冲 */
    s_b->remote_cands = calloc(16, sizeof(p2p_remote_candidate_entry_t));
    s_b->remote_cand_cap = 16;

    poll_candidates(inst_b, s_b);

    ASSERT_GE(s_b->remote_cand_cnt, 1);
    ASSERT_EQ(s_b->remote_cands[0].type, P2P_CAND_HOST);
    ASSERT_EQ(ntohs(s_b->remote_cands[0].addr.sin_port), 12345);
    ASSERT(s_b->remote_cand_done);  /* ver=0 → done */

    teardown(inst_a, s_a);
    teardown(inst_b, s_b);
}

/*
 * poll_answer_heartbeat_resend: SUB 心跳覆盖 offer → poll_answer 重发
 */
TEST(poll_answer_heartbeat_resend) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }
    struct p2p_instance *inst; struct p2p_session *s;
    setup(&inst, &s);
    p2p_signal_pubsub_ctx_t *ctx = &inst->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess = &s->sig_sess.pubsub;

    sess->is_pub = true;
    sess->state = SIG_PUBSUB_SESS_OFFERING;
    sess->offer_sent = 1;
    strncpy(sess->remote_gist_id, env_gist, sizeof(sess->remote_gist_id) - 1);
    strncpy(sess->remote_peer_id, "remote_sub", sizeof(sess->remote_peer_id) - 1);

    /* 写入心跳，模拟 offer 被覆盖 */
    gist_write(ctx, env_gist, "remote_sub", "ONLINE:9999999999:remote_sub");

    poll_answer(inst, s);

    /* poll_answer 检测到 ONLINE → 重发 offer */
    ASSERT_EQ(sess->offer_sent, 1);  /* resend 后 offer_sent=1 */

    /* 验证现在又是 offer */
    char buf[4096];
    gist_poll(ctx, env_gist, "remote_sub", buf, sizeof(buf));
    ASSERT(strncmp(buf, "OFFER:", 6) == 0);

    teardown(inst, s);
}

/*
 * same_gist_e2e: 同一个 Gist，不同 peer_id 的完整协议流程
 *
 * alice (SUB) 和 bob (PUB) 使用同一个 gist_id，各自写自己的文件（alice/bob）。
 * 流程：
 *   1. alice 写心跳到 gist/alice
 *   2. bob 读 gist/alice 检测心跳 → 写 offer 到 gist/alice
 *   3. alice 读 gist/alice 检测 offer → 提取 bob 地址 → SYNCING
 *   4. alice 发布候选到 gist/alice
 *   5. bob 读 gist/alice 获取候选 → SYNCING
 *   6. bob 发布候选到 gist/bob
 *   7. alice 读 gist/bob 获取候选
 *   8. 双方候选交换完毕
 */
TEST(same_gist_e2e) {
    if (!env_ready()) { printf("SKIP (no env)\n"); return; }

    /* === 创建 alice (SUB) 和 bob (PUB) === */
    struct p2p_instance *inst_a; struct p2p_session *s_a;
    setup_with_peer(&inst_a, &s_a, "alice");
    p2p_signal_pubsub_ctx_t *ctx_a = &inst_a->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess_a = &s_a->sig_sess.pubsub;

    struct p2p_instance *inst_b; struct p2p_session *s_b;
    setup_with_peer(&inst_b, &s_b, "bob");
    p2p_signal_pubsub_ctx_t *ctx_b = &inst_b->sig_ctx.pubsub;
    p2p_pubsub_session_t *sess_b = &s_b->sig_sess.pubsub;

    /* === 步骤 1: alice (SUB) 写心跳 === */
    sess_a->is_pub = false;
    sess_a->state = SIG_PUBSUB_SESS_WAIT_OFFER;
    sync0_sub(inst_a, s_a, P_tick_ms());

    /* 验证心跳写到了 gist/alice */
    char buf[4096];
    int ret = gist_poll(ctx_b, env_gist, "alice", buf, sizeof(buf));
    ASSERT_EQ(ret, 0);
    ASSERT(strncmp(buf, "ONLINE:", 7) == 0);
    ASSERT(strstr(buf, ":alice") != NULL);
    printf("  [1] alice heartbeat ok\n");

    sleep(1);

    /* === 步骤 2: bob (PUB) 读心跳 → 写 offer 到 gist/alice === */
    sess_b->is_pub = true;
    strncpy(sess_b->remote_gist_id, env_gist, sizeof(sess_b->remote_gist_id) - 1);
    strncpy(sess_b->remote_peer_id, "alice", sizeof(sess_b->remote_peer_id) - 1);
    sync0_offer(inst_b, s_b, false);
    ASSERT_EQ(sess_b->offer_sent, 1);

    /* 验证 alice 的文件被覆写为 offer */
    ret = gist_poll(ctx_a, env_gist, "alice", buf, sizeof(buf));
    ASSERT_EQ(ret, 0);
    ASSERT(strncmp(buf, "OFFER:", 6) == 0);
    ASSERT(strstr(buf, ":bob") != NULL);
    printf("  [2] bob offer written to gist/alice ok\n");

    sleep(1);

    /* === 步骤 3: alice 检测 offer → SYNCING === */
    bool found = poll_offer(inst_a, s_a);
    ASSERT(found);
    ASSERT(strcmp(sess_a->remote_gist_id, env_gist) == 0);
    ASSERT(strcmp(sess_a->remote_peer_id, "bob") == 0);
    ASSERT_EQ(sess_a->state, SIG_PUBSUB_SESS_SYNCING);
    printf("  [3] alice detected offer → SYNCING (remote=%s/%s) ok\n",
           sess_a->remote_gist_id, sess_a->remote_peer_id);

    sleep(1);

    /* === 步骤 4: alice 发布候选到 gist/alice === */
    sess_a->state = SIG_PUBSUB_SESS_SYNCING;
    s_a->local_cands = calloc(4, sizeof(p2p_local_candidate_entry_t));
    s_a->local_cand_cap = 4;
    s_a->local_cand_cnt = 1;
    s_a->local_cands[0].type = P2P_CAND_HOST;
    s_a->local_cands[0].addr.sin_family = AF_INET;
    s_a->local_cands[0].addr.sin_port = htons(10001);
    inet_pton(AF_INET, "10.0.0.1", &s_a->local_cands[0].addr.sin_addr);
    s_a->local_cands[0].priority = p2p_ice_calc_priority(P2P_ICE_CAND_HOST, 65535, 1);
    inst_a->srflx_count = 0; inst_a->srflx_active = 0; inst_a->turn_pending = 0;

    sync_candidates(inst_a, s_a);
    ASSERT_EQ(sess_a->state, SIG_PUBSUB_SESS_READY);
    printf("  [4] alice published candidates → READY ok\n");

    sleep(1);

    /* === 步骤 5: bob 读 gist/alice 获取候选 → SYNCING === */
    sess_b->state = SIG_PUBSUB_SESS_OFFERING;
    sess_b->offer_sent = 2;  /* 已确认 */

    s_b->remote_cands = calloc(16, sizeof(p2p_remote_candidate_entry_t));
    s_b->remote_cand_cap = 16;

    /* poll_answer 检测到候选数据 → SYNCING */
    poll_answer(inst_b, s_b);
    ASSERT_GE(s_b->remote_cand_cnt, 1);
    ASSERT_EQ(ntohs(s_b->remote_cands[0].addr.sin_port), 10001);
    ASSERT(sess_b->state >= SIG_PUBSUB_SESS_SYNCING);
    printf("  [5] bob received alice's candidates (%d) ok\n", s_b->remote_cand_cnt);

    sleep(1);

    /* === 步骤 6: bob 发布候选到 gist/bob === */
    sess_b->state = SIG_PUBSUB_SESS_SYNCING;
    s_b->local_cands = calloc(4, sizeof(p2p_local_candidate_entry_t));
    s_b->local_cand_cap = 4;
    s_b->local_cand_cnt = 1;
    s_b->local_cands[0].type = P2P_CAND_HOST;
    s_b->local_cands[0].addr.sin_family = AF_INET;
    s_b->local_cands[0].addr.sin_port = htons(20002);
    inet_pton(AF_INET, "10.0.0.2", &s_b->local_cands[0].addr.sin_addr);
    s_b->local_cands[0].priority = p2p_ice_calc_priority(P2P_ICE_CAND_HOST, 65535, 1);
    inst_b->srflx_count = 0; inst_b->srflx_active = 0; inst_b->turn_pending = 0;

    sync_candidates(inst_b, s_b);
    ASSERT_EQ(sess_b->state, SIG_PUBSUB_SESS_READY);
    printf("  [6] bob published candidates → READY ok\n");

    sleep(1);

    /* === 步骤 7: alice 读 gist/bob 获取候选 === */
    s_a->remote_cands = calloc(16, sizeof(p2p_remote_candidate_entry_t));
    s_a->remote_cand_cap = 16;

    poll_candidates(inst_a, s_a);
    ASSERT_GE(s_a->remote_cand_cnt, 1);
    ASSERT_EQ(ntohs(s_a->remote_cands[0].addr.sin_port), 20002);
    ASSERT(s_a->remote_cand_done);
    printf("  [7] alice received bob's candidates (%d) ok\n", s_a->remote_cand_cnt);

    /* === 验证完毕：双方候选交换成功 === */
    printf("  [OK] same gist e2e: alice(10.0.0.1:10001) ↔ bob(10.0.0.2:20002)\n");

    teardown(inst_a, s_a);
    teardown(inst_b, s_b);
}

/* --- main --- */

int main(void) {
    env_token = getenv("P2P_TEST_TOKEN");
    env_gist   = getenv("P2P_TEST_GIST");
    env_key    = getenv("P2P_TEST_KEY");

    printf("=== test_pubsub ===\n");
    if (!env_ready())
        printf("  ⚠ P2P_TEST_TOKEN / P2P_TEST_GIST not set — Gist tests will SKIP\n\n");

    RUN_TEST(init);
    RUN_TEST(gist_roundtrip);       if (env_ready()) sleep(1);
    RUN_TEST(heartbeat_write);      if (env_ready()) sleep(1);
    RUN_TEST(offer_write);          if (env_ready()) sleep(1);
    RUN_TEST(offer_detect);         if (env_ready()) sleep(1);
    RUN_TEST(candidate_roundtrip);  if (env_ready()) sleep(1);
    RUN_TEST(poll_answer_heartbeat_resend);  if (env_ready()) sleep(1);
    RUN_TEST(same_gist_e2e);

    printf("\n%d passed, %d failed\n", test_passed, test_failed);
    return test_failed > 0 ? 1 : 0;
}

#endif /* TEST_PUBSUB */
