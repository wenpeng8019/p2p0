#ifndef P2P0_H
#define P2P0_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version information */
#define P2P0_VERSION_MAJOR 1
#define P2P0_VERSION_MINOR 0
#define P2P0_VERSION_PATCH 0

/* Error codes */
#define P2P0_OK 0
#define P2P0_ERROR -1
#define P2P0_ERROR_SOCKET -2
#define P2P0_ERROR_BIND -3
#define P2P0_ERROR_CONNECT -4
#define P2P0_ERROR_TIMEOUT -5
#define P2P0_ERROR_INVALID_ARG -6

/* Connection states */
typedef enum {
    P2P0_STATE_INIT,
    P2P0_STATE_SIGNALING,
    P2P0_STATE_CONNECTED,
    P2P0_STATE_DISCONNECTED,
    P2P0_STATE_ERROR
} p2p0_state_t;

/* Peer information */
typedef struct {
    char address[128];
    uint16_t port;
} p2p0_peer_t;

/* P2P connection context */
typedef struct {
    p2p0_state_t state;
    int sockfd;
    p2p0_peer_t local_peer;
    p2p0_peer_t remote_peer;
    void *protocol_data;
} p2p0_ctx_t;

/* Core API functions */

/**
 * Initialize a P2P context
 * @param ctx Pointer to context structure
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_init(p2p0_ctx_t *ctx);

/**
 * Create a socket and bind to local address
 * @param ctx P2P context
 * @param port Local port (0 for automatic)
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_create_socket(p2p0_ctx_t *ctx, uint16_t port);

/**
 * Connect to a remote peer
 * @param ctx P2P context
 * @param remote_address Remote peer address
 * @param remote_port Remote peer port
 * @return P2P0_OK on success, error code on failure
 */
int p2p0_connect(p2p0_ctx_t *ctx, const char *remote_address, uint16_t remote_port);

/**
 * Send data to connected peer
 * @param ctx P2P context
 * @param data Data buffer
 * @param len Data length
 * @return Number of bytes sent, or error code on failure
 */
int p2p0_send(p2p0_ctx_t *ctx, const void *data, size_t len);

/**
 * Receive data from connected peer
 * @param ctx P2P context
 * @param buffer Receive buffer
 * @param len Buffer length
 * @return Number of bytes received, or error code on failure
 */
int p2p0_recv(p2p0_ctx_t *ctx, void *buffer, size_t len);

/**
 * Close P2P connection and cleanup
 * @param ctx P2P context
 */
void p2p0_close(p2p0_ctx_t *ctx);

/**
 * Get error message for error code
 * @param error_code Error code
 * @return Error message string
 */
const char *p2p0_strerror(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* P2P0_H */
