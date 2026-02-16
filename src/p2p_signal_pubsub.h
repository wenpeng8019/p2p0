
#ifndef P2P_SIGNAL_PUBSUB_H
#define P2P_SIGNAL_PUBSUB_H

#include <stdint.h>
#include <netinet/in.h>

struct p2p_session;

typedef enum {
    P2P_SIGNAL_ROLE_PUB = 0, /* Publisher: 发起端 (控制器) */
    P2P_SIGNAL_ROLE_SUB      /* Subscriber: 订阅端 (受控端) */
} p2p_signal_role_t;

typedef struct {
    p2p_signal_role_t role;
    char backend_url[256]; /* e.g., GitHub Gist API URL */
    char auth_token[128];
    char channel_id[128];  /* e.g., Gist ID */
    char etag[128];
    uint64_t last_poll;
    int answered;          /* SUB: already sent answer */
} p2p_signal_pubsub_ctx_t;

int  p2p_signal_pubsub_init(p2p_signal_pubsub_ctx_t *ctx, p2p_signal_role_t role, const char *token, const char *channel_id);
void p2p_signal_pubsub_tick(p2p_signal_pubsub_ctx_t *ctx, struct p2p_session *s);
int  p2p_signal_pubsub_send(p2p_signal_pubsub_ctx_t *ctx, const char *target_name, const void *data, int len);

#endif /* P2P_SIGNAL_PUBSUB_H */
