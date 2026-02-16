#include "../include/p2p0_ice_relay.h"
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

/* Initialize ICE-RELAY protocol */
int p2p0_ice_relay_init(p2p0_ctx_t *ctx, const char *server_address,
                        uint16_t server_port, const char *session_id) {
    if (!ctx || !server_address || !session_id) {
        return P2P0_ERROR_INVALID_ARG;
    }

    /* Allocate ICE-RELAY context */
    p2p0_ice_relay_ctx_t *ice_ctx = (p2p0_ice_relay_ctx_t *)malloc(sizeof(p2p0_ice_relay_ctx_t));
    if (!ice_ctx) {
        return P2P0_ERROR;
    }

    memset(ice_ctx, 0, sizeof(p2p0_ice_relay_ctx_t));
    snprintf(ice_ctx->server_address, sizeof(ice_ctx->server_address), "%s", server_address);
    ice_ctx->server_port = server_port;
    snprintf(ice_ctx->session_id, sizeof(ice_ctx->session_id), "%s", session_id);

    /* Create TCP signaling socket */
    ice_ctx->signaling_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ice_ctx->signaling_fd < 0) {
        free(ice_ctx);
        return P2P0_ERROR_SOCKET;
    }

    /* Connect to signaling server */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_address, &server_addr.sin_addr) <= 0) {
        close(ice_ctx->signaling_fd);
        free(ice_ctx);
        return P2P0_ERROR_CONNECT;
    }

    if (connect(ice_ctx->signaling_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(ice_ctx->signaling_fd);
        free(ice_ctx);
        return P2P0_ERROR_CONNECT;
    }

    ctx->protocol_data = ice_ctx;
    ctx->state = P2P0_STATE_SIGNALING;

    return P2P0_OK;
}

/* Add ICE candidate */
int p2p0_ice_relay_add_candidate(p2p0_ctx_t *ctx, const char *address,
                                  uint16_t port, uint8_t priority) {
    if (!ctx || !ctx->protocol_data || !address) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_ice_relay_ctx_t *ice_ctx = (p2p0_ice_relay_ctx_t *)ctx->protocol_data;

    if (ice_ctx->num_local_candidates >= P2P0_ICE_RELAY_MAX_CANDIDATES) {
        return P2P0_ERROR;
    }

    int idx = ice_ctx->num_local_candidates;
    snprintf(ice_ctx->local_candidates[idx].address,
             sizeof(ice_ctx->local_candidates[idx].address), "%s", address);
    ice_ctx->local_candidates[idx].port = port;
    ice_ctx->local_candidates[idx].priority = priority;
    ice_ctx->num_local_candidates++;

    return P2P0_OK;
}

/* Send offer to remote peer */
int p2p0_ice_relay_send_offer(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_ice_relay_ctx_t *ice_ctx = (p2p0_ice_relay_ctx_t *)ctx->protocol_data;
    p2p0_ice_relay_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P0_ICE_RELAY_MSG_OFFER;
    msg.version = 1;
    msg.length = sizeof(msg);
    snprintf(msg.session_id, sizeof(msg.session_id), "%s", ice_ctx->session_id);

    /* Format candidates */
    char *data_ptr = msg.data;
    int remaining = sizeof(msg.data);
    for (int i = 0; i < ice_ctx->num_local_candidates; i++) {
        int written = snprintf(data_ptr, remaining, "%s:%u:%u;",
                              ice_ctx->local_candidates[i].address,
                              ice_ctx->local_candidates[i].port,
                              ice_ctx->local_candidates[i].priority);
        if (written >= remaining) break;
        data_ptr += written;
        remaining -= written;
    }

    /* Send offer */
    int sent = send(ice_ctx->signaling_fd, (const char *)&msg, sizeof(msg), 0);
    return sent > 0 ? P2P0_OK : P2P0_ERROR;
}

/* Receive and process answer from remote peer */
int p2p0_ice_relay_receive_answer(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_ice_relay_ctx_t *ice_ctx = (p2p0_ice_relay_ctx_t *)ctx->protocol_data;
    p2p0_ice_relay_msg_t msg;

    /* Receive answer */
    int received = recv(ice_ctx->signaling_fd, (char *)&msg, sizeof(msg), 0);
    if (received < 0 || msg.type != P2P0_ICE_RELAY_MSG_ANSWER) {
        return P2P0_ERROR;
    }

    /* Parse remote candidates */
    char *token = strtok(msg.data, ";");
    ice_ctx->num_remote_candidates = 0;

    while (token && ice_ctx->num_remote_candidates < P2P0_ICE_RELAY_MAX_CANDIDATES) {
        char address[128];
        unsigned int port, priority;

        if (sscanf(token, "%127[^:]:%u:%u", address, &port, &priority) == 3) {
            int idx = ice_ctx->num_remote_candidates;
            snprintf(ice_ctx->remote_candidates[idx].address,
                    sizeof(ice_ctx->remote_candidates[idx].address), "%s", address);
            ice_ctx->remote_candidates[idx].port = (uint16_t)port;
            ice_ctx->remote_candidates[idx].priority = (uint8_t)priority;
            ice_ctx->num_remote_candidates++;
        }

        token = strtok(NULL, ";");
    }

    return ice_ctx->num_remote_candidates > 0 ? P2P0_OK : P2P0_ERROR;
}

/* Establish P2P connection using ICE-RELAY protocol */
int p2p0_ice_relay_connect(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_ice_relay_ctx_t *ice_ctx = (p2p0_ice_relay_ctx_t *)ctx->protocol_data;

    /* Try to connect to best candidate */
    if (ice_ctx->num_remote_candidates == 0) {
        return P2P0_ERROR;
    }

    /* Use highest priority candidate */
    int best_idx = 0;
    for (int i = 1; i < ice_ctx->num_remote_candidates; i++) {
        if (ice_ctx->remote_candidates[i].priority >
            ice_ctx->remote_candidates[best_idx].priority) {
            best_idx = i;
        }
    }

    /* Connect to remote peer */
    return p2p0_connect(ctx,
                       ice_ctx->remote_candidates[best_idx].address,
                       ice_ctx->remote_candidates[best_idx].port);
}

/* Cleanup ICE-RELAY protocol resources */
void p2p0_ice_relay_cleanup(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return;
    }

    p2p0_ice_relay_ctx_t *ice_ctx = (p2p0_ice_relay_ctx_t *)ctx->protocol_data;

    if (ice_ctx->signaling_fd >= 0) {
        close(ice_ctx->signaling_fd);
        ice_ctx->signaling_fd = -1;
    }

    free(ice_ctx);
    ctx->protocol_data = NULL;
}
