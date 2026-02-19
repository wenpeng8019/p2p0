
#include "p2p_internal.h"
#include "p2p_udp.h"
#include "p2p_log.h"
#include "p2p_lang.h"
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

static int openssl_init(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = calloc(1, sizeof(p2p_openssl_ctx_t));
    s->transport_data = os;

    os->ctx = SSL_CTX_new(DTLS_method());
    SSL_CTX_set_verify(os->ctx, SSL_VERIFY_NONE, NULL);

    /* Support PSK */
    if (s->cfg.auth_key) {
        SSL_CTX_set_psk_client_callback(os->ctx, psk_client_cb);
        SSL_CTX_set_psk_server_callback(os->ctx, psk_server_cb);
    }

    os->ssl = SSL_new(os->ctx);
    SSL_set_ex_data(os->ssl, 0, s);
    
    /* 使用 Memory BIO 以便手动注入/提取包 */
    os->read_bio = BIO_new(BIO_s_mem());
    os->write_bio = BIO_new(BIO_s_mem());
    BIO_set_mem_eof_return(os->read_bio, -1);
    BIO_set_mem_eof_return(os->write_bio, -1);

    SSL_set_bio(os->ssl, os->read_bio, os->write_bio);
    
    if (s->cfg.dtls_server) {
        SSL_set_accept_state(os->ssl);
    } else {
        SSL_set_connect_state(os->ssl);
    }

    return 0;
}

static int openssl_send(p2p_session_t *s, const void *buf, int len) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->transport_data;
    if (!os || !os->ssl) return -1;

    int ret = SSL_write(os->ssl, buf, len);
    if (ret <= 0) return 0;

    /* 检查是否有加密包需要从 write_bio 发出 (即写往底层 UDP) */
    char enc_buf[P2P_MTU];
    int enc_len;
    while ((enc_len = BIO_read(os->write_bio, enc_buf, sizeof(enc_buf))) > 0) {
        udp_send_packet(s->sock, &s->active_addr, P2P_PKT_DATA, 0, 0, enc_buf, enc_len);
    }

    return ret;
}

static void openssl_tick(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->transport_data;
    if (!os || !os->ssl) return;

    /* 驱动握手 */
    if (!os->handshake_done) {
        int ret = SSL_do_handshake(os->ssl);
        if (ret == 1) {
            os->handshake_done = 1;
            P2P_LOG_INFO("openssl", "%s", MSG(MSG_OPENSSL_HANDSHAKE_DONE));
        }
    }

    /* 检查是否有加密包需要从 write_bio 发出 (例如握手包或重传) */
    char enc_buf[P2P_MTU];
    int enc_len;
    while ((enc_len = BIO_read(os->write_bio, enc_buf, sizeof(enc_buf))) > 0) {
        udp_send_packet(s->sock, &s->active_addr, P2P_PKT_DATA, 0, 0, enc_buf, enc_len);
    }
}

static int openssl_is_ready(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->transport_data;
    return os && os->handshake_done;
}

static void openssl_on_packet(struct p2p_session *s, uint8_t type, const uint8_t *payload, int len, const struct sockaddr_in *from) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->transport_data;
    if (!os || type != P2P_PKT_DATA) return;
    (void)from;

    /* 注入底层收到的加密包到 read_bio */
    BIO_write(os->read_bio, payload, len);

    if (os->handshake_done) {
        uint8_t app_buf[P2P_MTU];
        int ret = SSL_read(os->ssl, app_buf, sizeof(app_buf));
        if (ret > 0) {
            ring_write(&s->stream.recv_ring, app_buf, ret);
        }
    } else {
        openssl_tick(s);
    }
}

static void openssl_close(p2p_session_t *s) {
    p2p_openssl_ctx_t *os = (p2p_openssl_ctx_t *)s->transport_data;
    if (!os) return;
    if (os->ssl) SSL_free(os->ssl);
    if (os->ctx) SSL_CTX_free(os->ctx);
    free(os);
    s->transport_data = NULL;
}

const p2p_transport_ops_t p2p_trans_openssl = {
    .name      = "DTLS-OpenSSL",
    .init      = openssl_init,
    .tick      = openssl_tick,
    .send_data = openssl_send,
    .on_packet = openssl_on_packet,
    .is_ready  = openssl_is_ready,
    .close     = openssl_close
};
