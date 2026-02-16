#ifndef P2P0_PUBSUB_H
#define P2P0_PUBSUB_H

#include "p2p0.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PUBSUB protocol: GitHub Gist-based signaling for serverless P2P */

#define P2P0_PUBSUB_GIST_ID_SIZE 64
#define P2P0_PUBSUB_TOKEN_SIZE 256
#define P2P0_PUBSUB_POLL_INTERVAL 2

/* Message types */
typedef enum {
    P2P0_PUBSUB_MSG_OFFER = 1,
    P2P0_PUBSUB_MSG_ANSWER,
    P2P0_PUBSUB_MSG_CANDIDATE,
    P2P0_PUBSUB_MSG_KEEPALIVE
} p2p0_pubsub_msg_type_t;

/* PUBSUB signaling context */
typedef struct {
    char gist_id[P2P0_PUBSUB_GIST_ID_SIZE];
    char github_token[P2P0_PUBSUB_TOKEN_SIZE];
    char peer_id[64];
    int poll_interval;
    char api_url[256];
} p2p0_pubsub_ctx_t;

/**
 * Initialize PUBSUB protocol
 * @param ctx P2P context
 * @param gist_id GitHub Gist ID for signaling
 * @param github_token GitHub API token (optional, can be NULL for read-only)
 * @param peer_id Unique peer identifier
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_pubsub_init(p2p0_ctx_t *ctx, const char *gist_id,
                     const char *github_token, const char *peer_id);

/**
 * Publish peer information to Gist
 * @param ctx P2P context
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_pubsub_publish(p2p0_ctx_t *ctx);

/**
 * Subscribe and wait for peer information from Gist
 * @param ctx P2P context
 * @param peer_id Remote peer identifier
 * @param peer Output peer information
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_pubsub_subscribe(p2p0_ctx_t *ctx, const char *peer_id, p2p0_peer_t *peer);

/**
 * Establish P2P connection using PUBSUB protocol
 * @param ctx P2P context
 * @param peer_id Remote peer identifier
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_pubsub_connect(p2p0_ctx_t *ctx, const char *peer_id);

/**
 * Set poll interval for checking Gist updates
 * @param ctx P2P context
 * @param interval Interval in seconds
 */
void p2p0_pubsub_set_poll_interval(p2p0_ctx_t *ctx, int interval);

/**
 * Cleanup PUBSUB protocol resources
 * @param ctx P2P context
 */
void p2p0_pubsub_cleanup(p2p0_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* P2P0_PUBSUB_H */
