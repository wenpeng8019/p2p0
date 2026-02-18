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
 * - 使用 system() 调用 curl（存在命令注入风险，生产环境应使用 libcurl）
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

#include "p2p_signal_pubsub.h"
#include "p2p_crypto_extra.h"
#include "p2p_internal.h"
#include "p2p_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <openssl/des.h>
#include <openssl/evp.h>

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
static void derive_key(const char *auth_key, uint8_t *key_out, size_t key_len) {
    memset(key_out, 0, key_len);
    if (auth_key && auth_key[0]) {
        size_t auth_len = strlen(auth_key);
        for (size_t i = 0; i < key_len && i < auth_len; i++) {
            key_out[i] = auth_key[i];
        }
    } else {
        /* 未提供密钥时使用默认值（不安全，仅用于测试） */
        P2P_LOG_WARN("SIGNAL_PUBSUB", "No auth_key provided, using default key (insecure)");
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
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "Invalid channel_id format (security risk)");
        return -1;
    }
    
    P2P_LOG_INFO("SIGNAL_PUBSUB", "Initialized: channel=%s", channel_id);
    return 0;
}

void p2p_signal_pubsub_set_role(p2p_signal_pubsub_ctx_t *ctx, p2p_signal_role_t role) {

    ctx->role = role;
    P2P_LOG_INFO("SIGNAL_PUBSUB", "Initialized: role=%s", role == P2P_SIGNAL_ROLE_PUB ? "PUB" : "SUB");
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
    derive_key(s->cfg.auth_key, key, sizeof(key));
    
    /* Base64 解码 */
    uint8_t enc_buf[1024];
    int enc_len = p2p_base64_decode(b64_data, strlen(b64_data), enc_buf, sizeof(enc_buf));
    if (enc_len <= 0) {
        P2P_LOG_WARN("SIGNAL_PUBSUB", "Base64 decode failed");
        return;
    }
    
    /* 分配解密缓冲区 */
    uint8_t *dec_buf = malloc(enc_len);
    if (!dec_buf) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "Memory allocation failed");
        return;
    }
    
    /* DES 解密 */
    p2p_des_decrypt(key, enc_buf, enc_len, dec_buf);
    
    /* 反序列化信令数据 */
    p2p_signaling_payload_hdr_t payload;
    if (enc_len >= 76 && unpack_signaling_payload_hdr(&payload, dec_buf) == 0 &&
        enc_len >= (size_t)(76 + payload.candidate_count * 32)) {
        P2P_LOG_INFO("SIGNAL_PUBSUB", "Received valid signal from '%s'", payload.sender);
        
        /* 添加远端 ICE 候选 */
        for (int i = 0; i < payload.candidate_count && i < 8; i++) {
            if (s->remote_cand_cnt < P2P_MAX_CANDIDATES) {
                p2p_candidate_t c;
                unpack_candidate(&c, dec_buf + sizeof(p2p_signaling_payload_hdr_t) + i * sizeof(p2p_candidate_t));
                s->remote_cands[s->remote_cand_cnt++] = c;
                printf("[ICE] 收到远端候选: 类型=%d, 地址=%s:%d\n",
                       c.type,
                       inet_ntoa(c.addr.sin_addr),
                       ntohs(c.addr.sin_port));
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
                s->cfg.local_peer_id,
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
                printf("[SIGNALING] 自动发送 answer（包含 %d 个候选）给 %s\n", 
                       s->local_cand_cnt, payload.sender);
            }
        }
    } else {
        P2P_LOG_WARN("SIGNAL_PUBSUB", "Signal payload deserialization failed");
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
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "Channel ID validation failed");
        return -1;
    }
    
    /* 派生加密密钥 */
    uint8_t key[8];
    derive_key(NULL, key, sizeof(key));  /* TODO: 传入实际的 auth_key */
    
    /* DES 加密需要 8 字节对齐 */
    int padded_len = (len + 7) & ~7;
    uint8_t *padded_data = calloc(1, padded_len);
    if (!padded_data) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "Memory allocation failed");
        return -1;
    }
    memcpy(padded_data, data, len);
    
    /* DES 加密 */
    uint8_t *enc_data = malloc(padded_len);
    if (!enc_data) {
        free(padded_data);
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "Memory allocation failed");
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
     * 读取现有 Gist 内容
     * 需要保留另一个字段的值（PUB 保留 answer，SUB 保留 offer）
     */
    char cmd_read[2048];
    snprintf(cmd_read, sizeof(cmd_read), 
        "curl -s -H \"Authorization: token %s\" "
        "https://api.github.com/gists/%s > .gist_read.json",
        ctx->auth_token, ctx->channel_id);
    system(cmd_read);
    
    /* 解析现有内容 */
    char existing_offer[4096] = {0};
    char existing_answer[4096] = {0};
    
    FILE *rf = fopen(".gist_read.json", "r");
    if (rf) {
        char rbuf[32768];
        size_t total = 0;
        size_t n;
        while ((n = fread(rbuf + total, 1, sizeof(rbuf) - total - 1, rf)) > 0) {
            total += n;
        }
        fclose(rf);
        rbuf[total] = '\0';
        
        /* 提取现有 "offer" 字段 */
        char *offer_start = strstr(rbuf, "\"offer\"");
        if (offer_start) {
            char *val_start = strchr(offer_start, '\"');
            if (val_start) {
                val_start = strchr(val_start + 1, '\"');
                if (val_start) {
                    val_start++;
                    char *val_end = val_start;
                    while (*val_end && *val_end != '\"') val_end++;
                    size_t len = val_end - val_start;
                    if (len > 0 && len < sizeof(existing_offer)) {
                        memcpy(existing_offer, val_start, len);
                        existing_offer[len] = '\0';
                    }
                }
            }
        }
        
        /* 提取现有 "answer" 字段 */
        char *answer_start = strstr(rbuf, "\"answer\"");
        if (answer_start) {
            char *val_start = strchr(answer_start, '\"');
            if (val_start) {
                val_start = strchr(val_start + 1, '\"');
                if (val_start) {
                    val_start++;
                    char *val_end = val_start;
                    while (*val_end && *val_end != '\"') val_end++;
                    size_t len = val_end - val_start;
                    if (len > 0 && len < sizeof(existing_answer)) {
                        memcpy(existing_answer, val_start, len);
                        existing_answer[len] = '\0';
                    }
                }
            }
        }
    }
    
    /*
     * 构建 JSON 内容
     * 保留另一个字段的值，只更新自己的字段
     */
    const char *offer_value = (ctx->role == P2P_SIGNAL_ROLE_PUB) ? escaped_b64 : existing_offer;
    const char *answer_value = (ctx->role == P2P_SIGNAL_ROLE_SUB) ? escaped_b64 : existing_answer;
    
    char json_content[8192];
    snprintf(json_content, sizeof(json_content),
        "{\\\"offer\\\":\\\"%s\\\",\\\"answer\\\":\\\"%s\\\"}",
        offer_value, answer_value);
    
    /*
     * PATCH 请求更新 Gist
     *
     * GitHub Gist API：
     *   PATCH /gists/{gist_id}
     *   Body: {"files": {"filename": {"content": "new content"}}}
     */
    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
        "curl -s -X PATCH -H \"Authorization: token %s\" "
        "-H \"Content-Type: application/json\" "
        "-d '{\"files\":{\"p2p_signal.json\":{\"content\":\"%s\"}}}' "
        "https://api.github.com/gists/%s > /dev/null",
        ctx->auth_token, json_content, ctx->channel_id);
    
    P2P_LOG_INFO("SIGNAL_PUBSUB", "Updating Gist field '%s'...", field_name);
    int ret = system(cmd);
    
    free(padded_data);
    free(enc_data);
    
    return (ret == 0) ? 0 : -1;
}

/*
 * ============================================================================
 * 周期性轮询 Gist
 * ============================================================================
 *
 * 轮询策略：
 *   - SUB 角色：每 5 秒轮询一次（快速检测 offer）
 *   - PUB 角色：每 10 秒轮询一次（等待 answer）
 *
 * 使用 raw URL 而非 API 以获取更好的缓存行为。
 *
 * @param ctx  信令上下文
 * @param s    P2P 会话
 */
void p2p_signal_pubsub_tick(p2p_signal_pubsub_ctx_t *ctx, struct p2p_session *s) {

    /* 根据角色设置不同的轮询间隔 */
    int poll_interval;
    if (ctx->role == P2P_SIGNAL_ROLE_PUB) poll_interval = 10000;
    else if (ctx->role == P2P_SIGNAL_ROLE_SUB) poll_interval = 5000;
    else return;

    uint64_t now = time_ms();
    uint64_t diff = now - ctx->last_poll;
    if (diff < (uint64_t)poll_interval) return;
    ctx->last_poll = now;

    /* 安全验证 */
    if (!is_safe_string(ctx->channel_id)) {
        P2P_LOG_ERROR("SIGNAL_PUBSUB", "Channel ID validation failed");
        return;
    }

    /*
     * 使用 curl 获取 Gist 原始内容
     *
     * 使用 raw URL 的优势：
     *   - 响应体直接是文件内容（非 JSON API 格式）
     *   - 更小的响应体
     *   - 添加时间戳参数绕过 CDN 缓存
     *
     * TODO: 生产环境应使用 libcurl 替代 system()
     */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -s -H \"Authorization: token %s\" -H \"Cache-Control: no-cache\" "
             "\"https://gist.githubusercontent.com/raw/%s/p2p_signal.json?t=%llu\" > .gist_resp.json",
             ctx->auth_token, ctx->channel_id, (unsigned long long)now);

    int ret = system(cmd);
    if (ret != 0) {
        P2P_LOG_DEBUG("SIGNAL_PUBSUB", "curl command failed");
        return;
    }

    /*
     * 解析 JSON：
     *   - SUB 读取 "offer" 字段
     *   - PUB 读取 "answer" 字段
     */
    const char *target_field = (ctx->role == P2P_SIGNAL_ROLE_SUB) ? "\"offer\"" : "\"answer\"";

    FILE *f = fopen(".gist_resp.json", "r");
    if (!f) {
        P2P_LOG_DEBUG("SIGNAL_PUBSUB", "Cannot open response file");
        return;
    }

    /* 读取整个文件 */
    char buffer[16384] = {0};
    size_t total = 0;
    size_t n;
    while ((n = fread(buffer + total, 1, sizeof(buffer) - total - 1, f)) > 0) {
        total += n;
    }
    fclose(f);

    /* 查找目标字段 */
    char *field_start = strstr(buffer, target_field);
    if (!field_start) {
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
        P2P_LOG_DEBUG("SIGNAL_PUBSUB", "Field %s is empty or too short", target_field);
        return;
    }

    /* 提取字段值 */
    size_t value_len = value_end - value_start;
    if (value_len > sizeof(buffer) - 1) value_len = sizeof(buffer) - 1;

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
        P2P_LOG_INFO("SIGNAL_PUBSUB", "Processing %s (role=%s)",
                     target_field, ctx->role == P2P_SIGNAL_ROLE_PUB ? "PUB" : "SUB");
        process_payload(ctx, s, content);
    }
}

