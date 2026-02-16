#include "../include/p2p0_simple.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

/* Initialize SIMPLE protocol */
int p2p0_simple_init(p2p0_ctx_t *ctx, const char *server_address,
                     uint16_t server_port, const char *peer_id) {
    if (!ctx || !server_address || !peer_id) {
        return P2P0_ERROR_INVALID_ARG;
    }

    /* Allocate SIMPLE context */
    p2p0_simple_ctx_t *simple_ctx = (p2p0_simple_ctx_t *)malloc(sizeof(p2p0_simple_ctx_t));
    if (!simple_ctx) {
        return P2P0_ERROR;
    }

    memset(simple_ctx, 0, sizeof(p2p0_simple_ctx_t));
    snprintf(simple_ctx->server_address, sizeof(simple_ctx->server_address), "%s", server_address);
    simple_ctx->server_port = server_port;
    snprintf(simple_ctx->peer_id, sizeof(simple_ctx->peer_id), "%s", peer_id);

    /* Create signaling socket */
    simple_ctx->signaling_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (simple_ctx->signaling_fd < 0) {
        free(simple_ctx);
        return P2P0_ERROR_SOCKET;
    }

    ctx->protocol_data = simple_ctx;
    ctx->state = P2P0_STATE_SIGNALING;

    return P2P0_OK;
}

/* Register peer with signaling server */
int p2p0_simple_register(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_simple_ctx_t *simple_ctx = (p2p0_simple_ctx_t *)ctx->protocol_data;
    p2p0_simple_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P0_SIMPLE_MSG_HELLO;
    msg.version = 1;
    msg.length = sizeof(msg);
    snprintf(msg.peer_id, sizeof(msg.peer_id), "%s", simple_ctx->peer_id);

    /* Format peer information */
    snprintf(msg.data, sizeof(msg.data), "%s:%u",
             ctx->local_peer.address, ctx->local_peer.port);

    /* Send registration message to server */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(simple_ctx->server_port);

    if (inet_pton(AF_INET, simple_ctx->server_address, &server_addr.sin_addr) <= 0) {
        return P2P0_ERROR_CONNECT;
    }

    int sent = sendto(simple_ctx->signaling_fd, &msg, sizeof(msg), 0,
                      (struct sockaddr *)&server_addr, sizeof(server_addr));

    return sent > 0 ? P2P0_OK : P2P0_ERROR;
}

/* Request peer information from signaling server */
int p2p0_simple_get_peer(p2p0_ctx_t *ctx, const char *peer_id, p2p0_peer_t *peer) {
    if (!ctx || !ctx->protocol_data || !peer_id || !peer) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_simple_ctx_t *simple_ctx = (p2p0_simple_ctx_t *)ctx->protocol_data;
    p2p0_simple_msg_t msg;

    /* Send peer info request */
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P0_SIMPLE_MSG_PEER_INFO;
    msg.version = 1;
    msg.length = sizeof(msg);
    snprintf(msg.peer_id, sizeof(msg.peer_id), "%s", peer_id);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(simple_ctx->server_port);

    if (inet_pton(AF_INET, simple_ctx->server_address, &server_addr.sin_addr) <= 0) {
        return P2P0_ERROR_CONNECT;
    }

    int sent = sendto(simple_ctx->signaling_fd, &msg, sizeof(msg), 0,
                      (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        return P2P0_ERROR;
    }

    /* Receive response */
    socklen_t addr_len = sizeof(server_addr);
    int received = recvfrom(simple_ctx->signaling_fd, &msg, sizeof(msg), 0,
                           (struct sockaddr *)&server_addr, &addr_len);

    if (received < 0 || msg.type != P2P0_SIMPLE_MSG_PEER_INFO) {
        return P2P0_ERROR;
    }

    /* Parse peer information */
    char address[128];
    unsigned int port;
    if (sscanf(msg.data, "%127[^:]:%u", address, &port) == 2) {
        snprintf(peer->address, sizeof(peer->address), "%s", address);
        peer->port = (uint16_t)port;
        return P2P0_OK;
    }

    return P2P0_ERROR;
}

/* Establish P2P connection using SIMPLE protocol */
int p2p0_simple_connect(p2p0_ctx_t *ctx, const char *peer_id) {
    if (!ctx || !peer_id) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_peer_t peer;
    int result = p2p0_simple_get_peer(ctx, peer_id, &peer);
    if (result != P2P0_OK) {
        return result;
    }

    /* Connect to remote peer */
    return p2p0_connect(ctx, peer.address, peer.port);
}

/* Cleanup SIMPLE protocol resources */
void p2p0_simple_cleanup(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return;
    }

    p2p0_simple_ctx_t *simple_ctx = (p2p0_simple_ctx_t *)ctx->protocol_data;

    if (simple_ctx->signaling_fd >= 0) {
        close(simple_ctx->signaling_fd);
        simple_ctx->signaling_fd = -1;
    }

    free(simple_ctx);
    ctx->protocol_data = NULL;
}
