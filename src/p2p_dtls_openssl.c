
#define MOD_TAG "OPENSSL"

#include "p2p_internal.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

// OpenSSL DTLS 上下文
typedef struct {
    SSL_CTX *ctx;
    SSL     *ssl;
    BIO     *read_bio;
    BIO     *write_bio;
    int      handshake_done;
} p2p_openssl_ctx_t;

static unsigned int psk_client_cb(SSL *ssl, const char *hint, char *identity,
                                unsigned int max_identity_len, unsigned char *psk,
                                unsigned int max_psk_len) {
    p2p_session_t *s = SSL_get_ex_data(ssl, 0);
    if (!s || !s->cfg.auth_key) return 0;
    
    strncpy(identity, "p2p_id", max_identity_len);
    unsigned int len = strlen(s->cfg.auth_key);
    if (len > max_psk_len) len = max_psk_len;
    memcpy(psk, s->cfg.auth_key, len);
    return len;
}

static unsigned int psk_server_cb(SSL *ssl, const char *identity, unsigned char *psk,
                                unsigned int max_psk_len) {
    p2p_session_t *s = SSL_get_ex_data(ssl, 0);
    if (!s || !s->cfg.auth_key) return 0;
    
    unsigned int len = strlen(s->cfg.auth_key);
    if (len > max_psk_len) len = max_psk_len;
    memcpy(psk, s->cfg.auth_key, len);
    return len;
}

/* 刷新 write_bio: 通过 p2p_send_dtls_record 发送 DTLS 记录 */
static void openssl_flush_bio(p2p_session_t *s, BIO *write_bio) {
    char enc_buf[P2P_MTU + 64];
    int enc_len;
    while ((enc_len = BIO_read(write_bio, enc_buf, sizeof(enc_buf))) > 0) {
        p2p_send_dtls_record(s, &s->active_addr, enc_buf, enc_len);
    }
}

static int openssl_init(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = calloc(1, sizeof(p2p_openssl_ctx_t));
    if (!os) {
        print("E:", LA_F("Failed to allocate OpenSSL context", LA_F238, 238));
        return -1;
    }
    s->dtls_data = os;

    os->ctx = SSL_CTX_new(DTLS_method());
    if (!os->ctx) {
        print("E:", LA_F("SSL_CTX_new failed", LA_F321, 321));
        free(os); s->dtls_data = NULL;
        return -1;
    }
    SSL_CTX_set_verify(os->ctx, SSL_VERIFY_NONE, NULL);

    if (s->cfg.auth_key) {
        SSL_CTX_set_psk_client_callback(os->ctx, psk_client_cb);
        SSL_CTX_set_psk_server_callback(os->ctx, psk_server_cb);
    }

    os->ssl = SSL_new(os->ctx);
    if (!os->ssl) {
        print("E:", LA_F("SSL_new failed", LA_F322, 322));
        SSL_CTX_free(os->ctx); free(os); s->dtls_data = NULL;
        return -1;
    }
    SSL_set_ex_data(os->ssl, 0, s);
    
    os->read_bio = BIO_new(BIO_s_mem());
    os->write_bio = BIO_new(BIO_s_mem());
    if (!os->read_bio || !os->write_bio) {
        print("E:", LA_F("BIO_new failed", LA_F206, 206));
        if (os->read_bio) BIO_free(os->read_bio);
        if (os->write_bio) BIO_free(os->write_bio);
        SSL_free(os->ssl); SSL_CTX_free(os->ctx);
        free(os); s->dtls_data = NULL;
        return -1;
    }
    BIO_set_mem_eof_return(os->read_bio, -1);
    BIO_set_mem_eof_return(os->write_bio, -1);

    SSL_set_bio(os->ssl, os->read_bio, os->write_bio);
    
    /* 角色：由 dtls_role 决定（0=自动，1=server，2=client）
     * 自动模式下按 peer_id 字典序：ID 较大者为 server，较小者为 client
     * 被动监听方（remote_peer_id 为空）始终为 server */
    int is_server;
    if (s->cfg.dtls_role == 1)      is_server = 1;
    else if (s->cfg.dtls_role == 2) is_server = 0;
    else /* auto */                 is_server = (s->remote_peer_id[0] == '\0')
                                              || strcmp(s->local_peer_id, s->remote_peer_id) > 0;
    print("I:", LA_F("[OpenSSL] DTLS role: %s (mode=%s)", LA_F376, 376),
          is_server ? "server" : "client",
          s->cfg.dtls_role == 0 ? "auto" : "forced");

    if (is_server) {
        SSL_set_accept_state(os->ssl);
    } else {
        SSL_set_connect_state(os->ssl);
    }

    return 0;
}

static void openssl_tick(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->dtls_data;
    if (!os || !os->ssl) return;

    if (!os->handshake_done) {
        int ret = SSL_do_handshake(os->ssl);
        if (ret == 1) {
            os->handshake_done = 1;
            print("I:", LA_F("[OpenSSL] DTLS handshake completed", LA_F375, 375));
        }
    }
    openssl_flush_bio(s, os->write_bio);
}

static int openssl_is_ready(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->dtls_data;
    return os && os->handshake_done;
}

/*
 * 加密并发送: 打包内层 [type|flags|seq|payload] → SSL_write → flush
 */
static int openssl_encrypt_send(p2p_session_t *s, const struct sockaddr_in *addr,
                                uint8_t type, uint8_t flags, uint16_t seq,
                                const void *payload, int payload_len) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->dtls_data;
    if (!os || !os->handshake_done) return 0;

    uint8_t plain[P2P_HDR_SIZE + P2P_MAX_PAYLOAD];
    p2p_pkt_hdr_encode(plain, type, flags, seq);
    if (payload_len > 0)
        memcpy(plain + P2P_HDR_SIZE, payload, payload_len);
    int plain_len = P2P_HDR_SIZE + payload_len;

    int ret = SSL_write(os->ssl, plain, plain_len);
    if (ret <= 0) return -1;

    /* flush 密文到网络 */
    char enc_buf[P2P_MTU + 64];
    int enc_len;
    while ((enc_len = BIO_read(os->write_bio, enc_buf, sizeof(enc_buf))) > 0) {
        p2p_send_dtls_record(s, addr, enc_buf, enc_len);
    }
    return payload_len;
}

/*
 * 解密: 注入 DTLS 记录 → SSL_read / 握手推进
 */
static int openssl_decrypt_recv(p2p_session_t *s, const uint8_t *in, int in_len,
                                uint8_t *out, int out_cap) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->dtls_data;
    if (!os) return -1;

    BIO_write(os->read_bio, in, in_len);

    if (!os->handshake_done) {
        int ret = SSL_do_handshake(os->ssl);
        if (ret == 1) {
            os->handshake_done = 1;
            print("I:", LA_F("[OpenSSL] DTLS handshake completed", LA_F375, 375));
        }
        openssl_flush_bio(s, os->write_bio);
        return 0;
    }

    int ret = SSL_read(os->ssl, out, out_cap);
    return (ret > 0) ? ret : 0;
}

static void openssl_close(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->dtls_data;
    if (!os) return;
    if (os->ssl) SSL_free(os->ssl);
    if (os->ctx) SSL_CTX_free(os->ctx);
    free(os);
    s->dtls_data = NULL;
}

const p2p_dtls_ops_t p2p_dtls_openssl = {
    .name         = "DTLS-OpenSSL",
    .init         = openssl_init,
    .close        = openssl_close,
    .tick         = openssl_tick,
    .is_ready     = openssl_is_ready,
    .encrypt_send = openssl_encrypt_send,
    .decrypt_recv = openssl_decrypt_recv,
};
