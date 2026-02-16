#ifndef P2P0_ICE_RELAY_H
#define P2P0_ICE_RELAY_H

#include "p2p0.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ICE-RELAY protocol: TCP-based signaling with relay support */

#define P2P0_ICE_RELAY_MAX_CANDIDATES 8
#define P2P0_ICE_RELAY_MSG_SIZE 1024

/* Message types */
typedef enum {
    P2P0_ICE_RELAY_MSG_OFFER = 1,
    P2P0_ICE_RELAY_MSG_ANSWER,
    P2P0_ICE_RELAY_MSG_CANDIDATE,
    P2P0_ICE_RELAY_MSG_RELAY_REQ,
    P2P0_ICE_RELAY_MSG_RELAY_DATA
} p2p0_ice_relay_msg_type_t;

/* ICE candidate */
typedef struct {
    char address[128];
    uint16_t port;
    uint8_t priority;
} p2p0_ice_candidate_t;

/* ICE-RELAY message structure */
typedef struct {
    uint8_t type;
    uint8_t version;
    uint16_t length;
    char session_id[64];
    char data[P2P0_ICE_RELAY_MSG_SIZE - 68];
} p2p0_ice_relay_msg_t;

/* ICE-RELAY signaling context */
typedef struct {
    char server_address[128];
    uint16_t server_port;
    int signaling_fd;
    char session_id[64];
    p2p0_ice_candidate_t local_candidates[P2P0_ICE_RELAY_MAX_CANDIDATES];
    int num_local_candidates;
    p2p0_ice_candidate_t remote_candidates[P2P0_ICE_RELAY_MAX_CANDIDATES];
    int num_remote_candidates;
    int use_relay;
} p2p0_ice_relay_ctx_t;

/**
 * Initialize ICE-RELAY protocol
 * @param ctx P2P context
 * @param server_address Signaling server address
 * @param server_port Signaling server port
 * @param session_id Unique session identifier
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_ice_relay_init(p2p0_ctx_t *ctx, const char *server_address,
                        uint16_t server_port, const char *session_id);

/**
 * Add ICE candidate
 * @param ctx P2P context
 * @param address Candidate address
 * @param port Candidate port
 * @param priority Candidate priority
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_ice_relay_add_candidate(p2p0_ctx_t *ctx, const char *address,
                                  uint16_t port, uint8_t priority);

/**
 * Send offer to remote peer
 * @param ctx P2P context
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_ice_relay_send_offer(p2p0_ctx_t *ctx);

/**
 * Receive and process answer from remote peer
 * @param ctx P2P context
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_ice_relay_receive_answer(p2p0_ctx_t *ctx);

/**
 * Establish P2P connection using ICE-RELAY protocol
 * @param ctx P2P context
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_ice_relay_connect(p2p0_ctx_t *ctx);

/**
 * Cleanup ICE-RELAY protocol resources
 * @param ctx P2P context
 */
void p2p0_ice_relay_cleanup(p2p0_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* P2P0_ICE_RELAY_H */
