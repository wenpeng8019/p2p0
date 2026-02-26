/*
 * PubSub (发布-订阅) 信令适配器实现
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本模块实现基于 GitHub Gist 的 P2P 信令交换机制。
 * 使用发布-订阅模式，双方通过共享的 Gist 文件交换 ICE 候选地址。
 *
 * 工作原理：
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │                        GitHub Gist (p2p_signal.json)                   │
 * │                                                                         │
 * │    {                                                                    │
 * │      "offer": "<加密的 Publisher ICE 候选>",                           │
 * │      "answer": "<加密的 Subscriber ICE 候选>"                          │
 * │    }                                                                    │
 * └─────────────────────────────────────────────────────────────────────────┘
 *           ▲                                           ▲
 *           │ 写入 offer                                │ 写入 answer
 *           │ 读取 answer                               │ 读取 offer
 *           │                                           │
 *    ┌──────┴──────┐                             ┌──────┴──────┐
 *    │  Publisher  │                             │  Subscriber │
 *    │   (PUB)     │                             │    (SUB)    │
 *    └─────────────┘                             └─────────────┘
 *
 * 通信流程：
 *   1. PUB 将自己的 ICE 候选写入 "offer" 字段
 *   2. SUB 轮询 Gist，读取 "offer" 字段
 *   3. SUB 收到 offer 后，将自己的 ICE 候选写入 "answer" 字段
 *   4. PUB 轮询 Gist，读取 "answer" 字段
 *   5. 双方获取对方候选后，ICE 连接检查开始
 *
 * ============================================================================
 * 安全说明
 * ============================================================================
 *
 * - 本实现使用 DES 加密（仅用于演示，生产环境应使用 AES-256）
 * - 使用 p2p_http 模块发起 HTTPS 请求（跨平台，无 shell 中间层）
 * - 需要配置 auth_key 作为加密密钥
 * - GitHub Token 需要 gist 读写权限
 *
 * ============================================================================
 * 数据格式
 * ============================================================================
 *
 * 加密流程：
 *   原始数据 → DES 加密 → Base64 编码 → JSON 存储
 *
 * p2p_signaling_payload_t 结构：
 *   - sender[32]:     发送方 local_peer_id
 *   - target[32]:     目标方 local_peer_id
 *   - candidate_count: 候选数量
 *   - candidates[8]:  ICE 候选数组
 */

#include "p2p_internal.h"
#include "p2p_http.h"
#include "p2p_crypto_extra.h"
#include <ctype.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

/*
 * ============================================================================
 * 从 auth_key 派生加密密钥
 * ============================================================================
 *
 * DES 需要 8 字节密钥，从用户提供的 auth_key 中提取。
 *
 * 注意：这是简化实现，生产环境应使用 PBKDF2 或 HKDF 进行密钥派生。
 *
 * @param auth_key  用户提供的认证密钥
 * @param key_out   输出：8 字节 DES 密钥
 * @param key_len   密钥长度（应为 8）
 */
static void derive_key(const char *auth_key, uint8_t key_out[8]) {
    const size_t key_len = 8;
    memset(key_out, 0, key_len);
    if (auth_key[0]) {
        size_t auth_len = strlen(auth_key);
        for (size_t i = 0; i < key_len && i < auth_len; i++) {
            key_out[i] = auth_key[i];
        }
    } else {
        /* 未提供密钥时使用默认值（不安全，仅用于测试） */
        P2P_LOG_WARN("SIGNAL_PUBSUB", "%s", LA_S("No auth_key provided, using default key (insecure)", LA_S37));
        memset(key_out, 0xAA, key_len);
    }
}

/*
 * ============================================================================
 * 验证字符串的 Shell 安全性
 * ============================================================================
 *
 * 检查字符串是否只包含安全字符，防止命令注入攻击。
 * 只允许：字母、数字、连字符、下划线、点号
 *
 * @param str  要验证的字符串
 * @return     1=安全，0=不安全
 */
static int is_safe_string(const char *str) {
    if (!str) return 0;
    for (const char *p = str; *p; p++) {
        if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '.') {
            return 0;
        }
    }
    return 1;
}

/*
 * ============================================================================
 * 初始化 PubSub 信令上下文
 * ============================================================================
 *
 * @param ctx        信令上下文
 * @param role       角色（PUB=发布者，SUB=订阅者）
 * @param token      GitHub Personal Access Token
 * @param channel_id Gist ID（作为信令通道）
 * @return           0=成功，-1=失败
 */
int p2p_signal_pubsub_init(p2p_signal_pubsub_ctx_t *ctx, const char *token, const char *channel_id) {

    memset(ctx, 0, sizeof(*ctx));
    ctx->role = P2P_SIGNAL_ROLE_UNKNOWN;
    strncpy(ctx->auth_token, token, sizeof(ctx->auth_token) - 1);
    strncpy(ctx->channel_id, channel_id, sizeof(ctx->channel_id) - 1);
    
    /* 安全验证：防止命令注入 */
    if (!is_safe_string(channel_id)) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "%s", LA_S("Invalid channel_id format (security risk)", LA_S33));
        return -1;
    }
    
    P2P_LOG_INFO("SIGNAL_PUBSUB", "%s %s", LA_W("Initialized:", LA_W45), channel_id);
    return 0;
}

void p2p_signal_pubsub_set_role(p2p_signal_pubsub_ctx_t *ctx, p2p_signal_role_t role) {

    ctx->role = role;
    P2P_LOG_INFO("SIGNAL_PUBSUB", "%s %s", LA_W("Initialized:", LA_W45), 
                 role == P2P_SIGNAL_ROLE_PUB ? LA_W("PUB", LA_W82) : LA_W("SUB", LA_W123));
}

/*
 * ============================================================================
 * 处理接收到的信令数据
 * ============================================================================
 *
 * 处理流程：
 *   1. Base64 解码
 *   2. DES 解密
 *   3. 反序列化 p2p_signaling_payload_t
 *   4. 提取远端 ICE 候选
 *   5. 如果是 SUB 角色，自动发送 answer
 *
 * @param ctx      信令上下文
 * @param s        P2P 会话
 * @param b64_data Base64 编码的加密数据
 */
static void process_payload(p2p_signal_pubsub_ctx_t *ctx, struct p2p_session *s, const char *b64_data) {
    /* 派生解密密钥 */
    uint8_t key[8];
    derive_key(ctx->auth_key, key);
    
    /* Base64 解码 */
    uint8_t enc_buf[1024];
    size_t enc_len = p2p_base64_decode(b64_data, strlen(b64_data), enc_buf, sizeof(enc_buf));
    if (enc_len <= 0) {
        P2P_LOG_WARN("SIGNAL_PUBSUB", "%s", LA_S("Base64 decode failed", LA_S8));
        return;
    }
    
    /* 分配解密缓冲区 */
    uint8_t *dec_buf = malloc(enc_len);
    if (!dec_buf) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "%s", LA_S("Out of memory", LA_S41));
        return;
    }
    
    /* DES 解密 */
    p2p_des_decrypt(key, enc_buf, enc_len, dec_buf);
    
    /* 反序列化信令数据 */
    p2p_signaling_payload_hdr_t payload;
    if (enc_len >= sizeof(p2p_signaling_payload_hdr_t) && unpack_signaling_payload_hdr(&payload, dec_buf) == 0 &&
        enc_len >= (size_t)(sizeof(p2p_signaling_payload_hdr_t) + payload.candidate_count * sizeof(p2p_candidate_t))) {
        P2P_LOG_INFO("SIGNAL_PUBSUB", "%s '%s'", LA_W("Received valid signal from", LA_W95), payload.sender);
        
        /* SUB 收到首个 offer（或发送者改变），重置 ICE 避免残留旧连接状态 */
        if (ctx->role == P2P_SIGNAL_ROLE_SUB && !ctx->answered) {
            if (s->remote_cand_cnt > 0 || s->ice_state != P2P_ICE_STATE_INIT) {
                P2P_LOG_DEBUG("SIGNAL_PUBSUB", "[DEBUG] First offer, resetting ICE and clearing %d stale candidates", s->remote_cand_cnt);
                s->remote_cand_cnt = 0;
                s->ice_state = P2P_ICE_STATE_GATHERING_DONE;
                s->ice_check_count = 0;
                s->ice_check_last_ms = 0;
            }
        }
        
        /* 添加远端 ICE 候选（步长 = sizeof(p2p_candidate_t) = 32）*/
        for (int i = 0; i < payload.candidate_count; i++) {
             p2p_remote_candidate_entry_t *c = p2p_cand_push_remote(s);
            if (!c) break;  /* OOM */
             unpack_candidate(&c->cand, dec_buf + sizeof(p2p_signaling_payload_hdr_t) + i * sizeof(p2p_candidate_t));
             c->last_punch_send_ms = 0;
            P2P_LOG_INFO("ICE", "%s: %s=%d, %s=%s:%d",
                 LA_W("Received remote candidate", LA_W91), LA_S("type", LA_S60), c->cand.type,
                 LA_W("address", LA_W5), inet_ntoa(c->cand.addr.sin_addr), ntohs(c->cand.addr.sin_port));
            
            /* Trickle ICE：如果 ICE 已在 CHECKING 状态，立即向新候选发送探测包 */
            if (s->ice_state == P2P_ICE_STATE_CHECKING) {

                P2P_LOG_DEBUG("ICE", "[Trickle] Immediately probing new candidate %s:%d",
                              inet_ntoa(c->cand.addr.sin_addr), ntohs(c->cand.addr.sin_port));
                nat_punch(s, &c->cand.addr);
            }
        }
        
        /*
         * SUB 角色收到 offer 后自动发送 answer
         * 只发送一次（answered 标志防止重复）
         */
        if (ctx->role == P2P_SIGNAL_ROLE_SUB && !ctx->answered) {
            ctx->answered = 1;
            
            /* 保存远端 local_peer_id */
            strncpy(s->remote_peer_id, payload.sender, sizeof(s->remote_peer_id) - 1);
            
            /* 构建并发送 answer */
            uint8_t buf[2048];
            int n = pack_signaling_payload_hdr(
                s->local_peer_id,
                payload.sender,
                0,  /* timestamp */
                0,  /* delay_trigger */
                s->local_cand_cnt,
                buf
            );
            for (int i = 0; i < s->local_cand_cnt; i++) {
                n += pack_candidate(&s->local_cands[i], buf + n);
            }
            if (n > 0) {
                p2p_signal_pubsub_send(ctx, payload.sender, buf, n);
                P2P_LOG_INFO("SIGNALING", "%s (%s %d %s) %s %s",
                       LA_W("Auto-send answer", LA_W14), LA_S("with", LA_S65), s->local_cand_cnt,
                       LA_W("candidates", LA_W20), LA_S("total sent", LA_S59), payload.sender);
            }
        }
    } else {
        P2P_LOG_WARN("SIGNAL_PUBSUB", "%s", LA_S("Signal payload deserialization failed", LA_S54));
    }
    
    free(dec_buf);
}

/*
 * ============================================================================
 * 发送信令数据到 Gist
 * ============================================================================
 *
 * 发送流程：
 *   1. DES 加密原始数据
 *   2. Base64 编码
 *   3. JSON 转义特殊字符
 *   4. 读取现有 Gist 内容（保留另一个字段）
 *   5. PATCH 更新 Gist
 *
 * 字段写入规则：
 *   - PUB 角色写入 "offer" 字段
 *   - SUB 角色写入 "answer" 字段
 *
 * @param ctx         信令上下文
 * @param target_name 目标 local_peer_id（暂未使用）
 * @param data        要发送的数据
 * @param len         数据长度
 * @return            0=成功，-1=失败
 */
int p2p_signal_pubsub_send(p2p_signal_pubsub_ctx_t *ctx, const char *target_name, const void *data, int len) {
    (void)target_name;

    const char *field_name;

    if (ctx->role == P2P_SIGNAL_ROLE_PUB) field_name = "offer";
    else if (ctx->role == P2P_SIGNAL_ROLE_SUB) field_name = "answer";
    else return -1;

    /* 安全验证 */
    if (!is_safe_string(ctx->channel_id)) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "%s", LA_S("Channel ID validation failed", LA_S11));
        return -1;
    }
    
    /* 派生加密密钥 */
    uint8_t key[8];
    derive_key(ctx->auth_key, key);
    
    /* DES 加密需要 8 字节对齐 */
    int padded_len = (len + 7) & ~7;
    uint8_t *padded_data = calloc(1, padded_len);
    if (!padded_data) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "%s", LA_S("Out of memory", LA_S41));
        return -1;
    }
    memcpy(padded_data, data, len);
    
    /* DES 加密 */
    uint8_t *enc_data = malloc(padded_len);
    if (!enc_data) {
        free(padded_data);
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "%s", LA_S("Out of memory", LA_S41));
        return -1;
    }
    
    p2p_des_encrypt(key, padded_data, padded_len, enc_data);
    
    /* Base64 编码 */
    char b64[2048] = {0};
    p2p_base64_encode(enc_data, padded_len, b64, sizeof(b64));
    

    /*
     * JSON 转义处理
     * Base64 字符串中可能包含需要转义的字符
     */
    char escaped_b64[4096] = {0};
    char *src = b64;
    char *dst = escaped_b64;
    while (*src && (dst - escaped_b64) < (int)sizeof(escaped_b64) - 10) {
        if (*src == '\\') {
            *dst++ = '\\';
            *dst++ = '\\';
        } else if (*src == '\"') {
            *dst++ = '\\';
            *dst++ = '\"';
        } else if (*src == '\n') {
            *dst++ = '\\';
            *dst++ = 'n';
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
    
    /*
     * 读取现有 Gist 内容（保留另一个字段：PUB 保留 answer，SUB 保留 offer）
     */
    char existing_offer[4096] = {0};
    char existing_answer[4096] = {0};

    {
        char get_url[256];
        snprintf(get_url, sizeof(get_url),
                 "https://api.github.com/gists/%s", ctx->channel_id);

        char *rbuf = calloc(1, 32768);
        if (rbuf) {
            p2p_http_get(get_url, ctx->auth_token, rbuf, 32768);

            /* 同 tick 函数：找到 p2p_signal.json 节，提取 content 字段，还原转义 */
            const char *file_key = "\"p2p_signal.json\"";
            char *file_sec = strstr(rbuf, file_key);
            if (file_sec) {
                char *cnt_key = strstr(file_sec, "\"content\"");
                if (cnt_key) {
                    char *cs = strchr(cnt_key + 9, '\"');
                    if (cs) {
                        cs++;  /* skip opening quote */
                        char inner[8192] = {0};
                        char *ip = cs, *id = inner, *ie = inner + sizeof(inner) - 1;
                        while (*ip && id < ie) {
                            if (*ip == '\\') {
                                ip++;
                                if (*ip == 'n')       { *id++ = '\n'; ip++; }
                                else if (*ip == '\\') { *id++ = '\\'; ip++; }
                                else if (*ip == '"')  { *id++ = '"';  ip++; }
                                else if (*ip == '/')  { *id++ = '/';  ip++; }
                                else if (*ip == 'r')  { *id++ = '\r'; ip++; }
                                else if (*ip == 't')  { *id++ = '\t'; ip++; }
                                else { *id++ = '\\'; *id++ = *ip++; }
                            } else if (*ip == '"') {
                                break;  /* end of JSON string */
                            } else {
                                *id++ = *ip++;
                            }
                        }
                        *id = '\0';

                        /* 在内层 JSON 中提取 offer 字段 */
                        char *os = strstr(inner, "\"offer\"");
                        if (os) {
                            char *v = strchr(os + 7, '"');
                            if (v) {
                                v++;
                                char *ve = v;
                                while (*ve && *ve != '"') ve++;
                                size_t vl = ve - v;
                                if (vl > 0 && vl < sizeof(existing_offer)) {
                                    memcpy(existing_offer, v, vl);
                                    existing_offer[vl] = '\0';
                                }
                            }
                        }

                        /* 在内层 JSON 中提取 answer 字段 */
                        char *as2 = strstr(inner, "\"answer\"");
                        if (as2) {
                            char *v = strchr(as2 + 8, '"');
                            if (v) {
                                v++;
                                char *ve = v;
                                while (*ve && *ve != '"') ve++;
                                size_t vl = ve - v;
                                if (vl > 0 && vl < sizeof(existing_answer)) {
                                    memcpy(existing_answer, v, vl);
                                    existing_answer[vl] = '\0';
                                }
                            }
                        }
                    }
                }
            }
            free(rbuf);
        }
    }

    /*
     * PUB 角色：如果对方（SUB）已经写入了 answer，不再重发 offer 覆盖它
     * 直接返回成功，等待 tick 轮询时读取 answer
     */
    if (ctx->role == P2P_SIGNAL_ROLE_PUB && existing_answer[0] != '\0') {
        P2P_LOG_INFO("SIGNAL_PUBSUB", "%s", LA_S("Answer already present, skipping offer re-publish", LA_S7));
        free(padded_data);
        free(enc_data);
        return 0;
    }

    /*
     * 构建 PATCH 请求体（全部在内存中，无临时文件）
     *
     * 外层格式：{"files":{"p2p_signal.json":{"content":"<JSON 转义的内层 JSON>"}}}
     */
    {
        const char *offer_value  = (ctx->role == P2P_SIGNAL_ROLE_PUB) ? escaped_b64 : existing_offer;
        const char *answer_value = (ctx->role == P2P_SIGNAL_ROLE_SUB) ? escaped_b64 : existing_answer;
        char inner_json[8192];
        size_t inner_len;
        char *body_buf;
        const char *pi;
        char *bp;
        char patch_url[256];
        int ret;

        /* 内层 JSON：{"offer":"...","answer":"..."} */
        snprintf(inner_json, sizeof(inner_json),
            "{\"offer\":\"%s\",\"answer\":\"%s\"}",
            offer_value, answer_value);

        /* 分配外层请求体缓冲区（最坏情况每字符变两字节） */
        inner_len = strlen(inner_json);
        body_buf = (char *)malloc(inner_len * 2 + 64);
        if (!body_buf) {
            free(padded_data);
            free(enc_data);
            return -1;
        }

        bp = body_buf;
        bp += sprintf(bp, "{\"files\":{\"p2p_signal.json\":{\"content\":\"");
        for (pi = inner_json; *pi; pi++) {
            if      (*pi == '\\') { *bp++ = '\\'; *bp++ = '\\'; }
            else if (*pi == '"')  { *bp++ = '\\'; *bp++ = '"';  }
            else if (*pi == '\n') { *bp++ = '\\'; *bp++ = 'n';  }
            else if (*pi == '\r') { *bp++ = '\\'; *bp++ = 'r';  }
            else                  { *bp++ = *pi; }
        }
        bp += sprintf(bp, "\"}}}");
        *bp = '\0';

        snprintf(patch_url, sizeof(patch_url),
                 "https://api.github.com/gists/%s", ctx->channel_id);

        P2P_LOG_INFO("SIGNAL_PUBSUB", "%s '%s'...", LA_W("Updating Gist field", LA_W141), field_name);
        ret = p2p_http_patch(patch_url, ctx->auth_token, body_buf);

        free(body_buf);
        free(padded_data);
        free(enc_data);

        return ret;
    }
}

/*
 * ============================================================================
 * 周期性轮询 Gist
 * ============================================================================
 *
 * 轮询策略：
 *   - PUB 角色：每 1 秒轮询一次（尽快获取 answer，缩短建连延迟）
 *   - SUB 角色：每 5 秒轮询一次（等待 offer，无需频繁轮询）
 *
 * 使用 raw URL 而非 API 以获取更好的缓存行为。
 *
 * @param ctx  信令上下文
 * @param s    P2P 会话
 */
void p2p_signal_pubsub_tick(p2p_signal_pubsub_ctx_t *ctx, struct p2p_session *s) {

    /* 根据角色设置不同的轮询间隔 */
    int poll_interval;
    if (ctx->role == P2P_SIGNAL_ROLE_PUB) poll_interval = P2P_PUBSUB_PUB_POLL_MS;
    else if (ctx->role == P2P_SIGNAL_ROLE_SUB) poll_interval = P2P_PUBSUB_SUB_POLL_MS;
    else return;

    uint64_t now = p2p_time_ms();
    uint64_t diff = now - ctx->last_poll;
    if (diff < (uint64_t)poll_interval) return;
    ctx->last_poll = now;

    /* 安全验证 */
    if (!is_safe_string(ctx->channel_id)) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "%s", LA_S("Channel ID validation failed", LA_S11));
        return;
    }

    /*
     * 通过 p2p_http_get 获取 Gist 内容（内存直接接收，无临时文件）
     *
     * GitHub API 响应格式：
     *   { "files": { "p2p_signal.json": { "content": "{\"offer\":\"...\",\"answer\":\"\"}" } } }
     *
     * SUB 读取 "offer" 字段，PUB 读取 "answer" 字段。
     */
    const char *target_field = (ctx->role == P2P_SIGNAL_ROLE_SUB) ? "\"offer\"" : "\"answer\"";

    char get_url[256];
    snprintf(get_url, sizeof(get_url),
             "https://api.github.com/gists/%s", ctx->channel_id);

    char *buffer = calloc(1, 32768);
    if (!buffer) return;

    int got = p2p_http_get(get_url, ctx->auth_token, buffer, 32768);
    if (got <= 0) {
        P2P_LOG_DEBUG("SIGNAL_PUBSUB", "%s", LA_S("Gist GET failed", LA_S31));
        free(buffer);
        return;
    }

    /* 从 API 响应中提取 p2p_signal.json → content → 内层 JSON → 目标字段 */
    const char *content_key = "\"p2p_signal.json\"";
    char *file_section = strstr(buffer, content_key);
    if (!file_section) {
        return;  /* Gist 中没有 p2p_signal.json 文件 */
    }

    /* 在文件节中找 "content": "..." */
    char *content_start = strstr(file_section, "\"content\"");
    if (!content_start) return;
    content_start = strchr(content_start + 9, '\"');
    if (!content_start) return;
    content_start++;  /* skip opening quote */

    /* 复制并还原 JSON 转义，得到内层 JSON 字符串 */
    char inner_json[16384] = {0};
    char *src2 = content_start;
    char *dst2 = inner_json;
    char *end2 = inner_json + sizeof(inner_json) - 1;
    while (*src2 && dst2 < end2) {
        if (*src2 == '\\') {
            src2++;
            if (*src2 == 'n')       { *dst2++ = '\n'; src2++; }
            else if (*src2 == '\\') { *dst2++ = '\\'; src2++; }
            else if (*src2 == '"')  { *dst2++ = '"';  src2++; }
            else if (*src2 == '/')  { *dst2++ = '/';  src2++; }
            else if (*src2 == 'r')  { *dst2++ = '\r'; src2++; }
            else if (*src2 == 't')  { *dst2++ = '\t'; src2++; }
            else { *dst2++ = '\\'; *dst2++ = *src2++; }
        } else if (*src2 == '\"') {
            break;  /* end of outer JSON string value */
        } else {
            *dst2++ = *src2++;
        }
    }
    *dst2 = '\0';

    /* 在内层 JSON 中查找目标字段 */
    char *field_start = strstr(inner_json, target_field);
    if (!field_start) {
        free(buffer);
        return;  /* 字段不存在或为空 */
    }

    /* 提取字段值（跳过引号） */
    char *value_start = strchr(field_start + strlen(target_field), '\"');
    if (!value_start) return;
    value_start++;

    char *value_end = value_start;
    while (*value_end && *value_end != '\"') {
        value_end++;
    }

    if (value_end - value_start < 10) {
        P2P_LOG_DEBUG("SIGNAL_PUBSUB", "%s %s %s", LA_W("Field", LA_W36), target_field, LA_W("is empty or too short", LA_W48));
        return;
    }

    /* 提取字段值 */
    size_t value_len = value_end - value_start;
    if (value_len > sizeof(inner_json) - 1) value_len = sizeof(inner_json) - 1;

    char content[16384];
    memcpy(content, value_start, value_len);
    content[value_len] = '\0';

    /*
     * JSON 转义字符还原
     *   \n  → 换行符
     *   \\  → 反斜杠
     *   \"  → 双引号
     */
    char *src = content;
    char *dst = content;
    while (*src) {
        if (*src == '\\' && *(src+1) == 'n') {
            *dst++ = '\n';
            src += 2;
        } else if (*src == '\\' && *(src+1) == '\\') {
            *dst++ = '\\';
            src += 2;
        } else if (*src == '\\' && *(src+1) == '\"') {
            *dst++ = '\"';
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    /* 处理有效数据 */
    if (strlen(content) > 10) {
        P2P_LOG_INFO("SIGNAL_PUBSUB", "%s %s (%s=%s)",
                     LA_W("Processing", LA_W81), target_field, LA_W("role", LA_W105),
                     ctx->role == P2P_SIGNAL_ROLE_PUB ? LA_W("PUB", LA_W82) : LA_W("SUB", LA_W123));
        process_payload(ctx, s, content);
    }

    free(buffer);
}

