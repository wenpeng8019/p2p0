#include "../include/p2p0.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
#endif

/* Initialize a P2P context */
int p2p0_init(p2p0_ctx_t *ctx) {
    if (!ctx) {
        return P2P0_ERROR_INVALID_ARG;
    }

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return P2P0_ERROR_SOCKET;
    }
#endif

    memset(ctx, 0, sizeof(p2p0_ctx_t));
    ctx->state = P2P0_STATE_INIT;
    ctx->sockfd = -1;

    return P2P0_OK;
}

/* Create a socket and bind to local address */
int p2p0_create_socket(p2p0_ctx_t *ctx, uint16_t port) {
    if (!ctx) {
        return P2P0_ERROR_INVALID_ARG;
    }

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    /* Create UDP socket */
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) {
        return P2P0_ERROR_SOCKET;
    }

    /* Bind to local address */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(ctx->sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ctx->sockfd);
        ctx->sockfd = -1;
        return P2P0_ERROR_BIND;
    }

    /* Get actual bound address */
    if (getsockname(ctx->sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
        snprintf(ctx->local_peer.address, sizeof(ctx->local_peer.address),
                 "%s", inet_ntoa(addr.sin_addr));
        ctx->local_peer.port = ntohs(addr.sin_port);
    }

    return P2P0_OK;
}

/* Connect to a remote peer */
int p2p0_connect(p2p0_ctx_t *ctx, const char *remote_address, uint16_t remote_port) {
    if (!ctx || !remote_address) {
        return P2P0_ERROR_INVALID_ARG;
    }

    /* Store remote peer information */
    snprintf(ctx->remote_peer.address, sizeof(ctx->remote_peer.address),
             "%s", remote_address);
    ctx->remote_peer.port = remote_port;

    ctx->state = P2P0_STATE_CONNECTED;
    return P2P0_OK;
}

/* Send data to connected peer */
int p2p0_send(p2p0_ctx_t *ctx, const void *data, size_t len) {
    if (!ctx || !data || ctx->sockfd < 0) {
        return P2P0_ERROR_INVALID_ARG;
    }

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(ctx->remote_peer.port);

    if (inet_pton(AF_INET, ctx->remote_peer.address, &remote_addr.sin_addr) <= 0) {
        return P2P0_ERROR_CONNECT;
    }

    int sent = sendto(ctx->sockfd, data, len, 0,
                      (struct sockaddr *)&remote_addr, sizeof(remote_addr));

    return sent > 0 ? sent : P2P0_ERROR;
}

/* Receive data from connected peer */
int p2p0_recv(p2p0_ctx_t *ctx, void *buffer, size_t len) {
    if (!ctx || !buffer || ctx->sockfd < 0) {
        return P2P0_ERROR_INVALID_ARG;
    }

    struct sockaddr_in remote_addr;
    socklen_t addr_len = sizeof(remote_addr);

    int received = recvfrom(ctx->sockfd, buffer, len, 0,
                           (struct sockaddr *)&remote_addr, &addr_len);

    return received >= 0 ? received : P2P0_ERROR;
}

/* Close P2P connection and cleanup */
void p2p0_close(p2p0_ctx_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->sockfd >= 0) {
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }

    ctx->state = P2P0_STATE_DISCONNECTED;

#ifdef _WIN32
    WSACleanup();
#endif
}

/* Get error message for error code */
const char *p2p0_strerror(int error_code) {
    switch (error_code) {
        case P2P0_OK:
            return "Success";
        case P2P0_ERROR:
            return "General error";
        case P2P0_ERROR_SOCKET:
            return "Socket error";
        case P2P0_ERROR_BIND:
            return "Bind error";
        case P2P0_ERROR_CONNECT:
            return "Connection error";
        case P2P0_ERROR_TIMEOUT:
            return "Timeout error";
        case P2P0_ERROR_INVALID_ARG:
            return "Invalid argument";
        default:
            return "Unknown error";
    }
}
