#ifndef P2P0_SIMPLE_H
#define P2P0_SIMPLE_H

#include "p2p0.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SIMPLE protocol: UDP-based signaling for direct peer discovery */

#define P2P0_SIMPLE_MAX_PEERS 32
#define P2P0_SIMPLE_MSG_SIZE 512

/* Message types */
typedef enum {
    P2P0_SIMPLE_MSG_HELLO = 1,
    P2P0_SIMPLE_MSG_PEER_INFO,
    P2P0_SIMPLE_MSG_CONNECT_REQ,
    P2P0_SIMPLE_MSG_CONNECT_ACK,
    P2P0_SIMPLE_MSG_PING,
    P2P0_SIMPLE_MSG_PONG
} p2p0_simple_msg_type_t;

/* SIMPLE message structure */
typedef struct {
    uint8_t type;
    uint8_t version;
    uint16_t length;
    char peer_id[64];
    char data[P2P0_SIMPLE_MSG_SIZE - 68];
} p2p0_simple_msg_t;

/* SIMPLE signaling context */
typedef struct {
    char server_address[128];
    uint16_t server_port;
    int signaling_fd;
    char peer_id[64];
} p2p0_simple_ctx_t;

/**
 * Initialize SIMPLE protocol
 * @param ctx P2P context
 * @param server_address Signaling server address
 * @param server_port Signaling server port
 * @param peer_id Unique peer identifier
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_simple_init(p2p0_ctx_t *ctx, const char *server_address, 
                     uint16_t server_port, const char *peer_id);

/**
 * Register peer with signaling server
 * @param ctx P2P context
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_simple_register(p2p0_ctx_t *ctx);

/**
 * Request peer information from signaling server
 * @param ctx P2P context
 * @param peer_id Remote peer identifier
 * @param peer Output peer information
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_simple_get_peer(p2p0_ctx_t *ctx, const char *peer_id, p2p0_peer_t *peer);

/**
 * Establish P2P connection using SIMPLE protocol
 * @param ctx P2P context
 * @param peer_id Remote peer identifier
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_simple_connect(p2p0_ctx_t *ctx, const char *peer_id);

/**
 * Cleanup SIMPLE protocol resources
 * @param ctx P2P context
 */
void p2p0_simple_cleanup(p2p0_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* P2P0_SIMPLE_H */
