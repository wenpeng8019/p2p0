#include "../include/p2p0_pubsub.h"
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

/*
 * NOTE: This is a demonstration implementation of the PUBSUB protocol concept.
 * 
 * IMPORTANT LIMITATIONS:
 * - This implementation uses plain HTTP, NOT HTTPS
 * - GitHub API requires HTTPS for actual use
 * - For production use, you must implement TLS/SSL support (e.g., using OpenSSL, mbedTLS)
 * - This code serves as a conceptual example of the serverless signaling approach
 * 
 * To make this production-ready:
 * 1. Add TLS/SSL library (breaks zero-dependency promise for this protocol)
 * 2. Or use system TLS (platform-specific)
 * 3. Or use curl/libcurl as HTTP client
 * 
 * For demonstration purposes, this shows the protocol flow and message format.
 */

/* HTTP helper functions for GitHub API (DEMONSTRATION ONLY - requires HTTPS) */
static int http_request(const char *host, const char *port, const char *method,
                       const char *path, const char *token, const char *body,
                       char *response, size_t response_size) {
    struct sockaddr_in server_addr;
    int sockfd;

    /* Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return P2P0_ERROR_SOCKET;
    }

    /* Connect to server */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return P2P0_ERROR_CONNECT;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return P2P0_ERROR_CONNECT;
    }

    /* Build HTTP request */
    char request[4096];
    int request_len;

    if (body && strlen(body) > 0) {
        request_len = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: api.github.com\r\n"
            "User-Agent: p2p0/1.0\r\n"
            "Accept: application/vnd.github.v3+json\r\n"
            "%s%s"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s",
            method, path,
            token ? "Authorization: Bearer " : "",
            token ? token : "",
            strlen(body), body);
    } else {
        request_len = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: api.github.com\r\n"
            "User-Agent: p2p0/1.0\r\n"
            "Accept: application/vnd.github.v3+json\r\n"
            "%s%s"
            "\r\n",
            method, path,
            token ? "Authorization: Bearer " : "",
            token ? token : "");
    }

    /* Send request */
    int total_sent = 0;
    while (total_sent < request_len) {
        int sent = send(sockfd, request + total_sent, request_len - total_sent, 0);
        if (sent < 0) {
            close(sockfd);
            return P2P0_ERROR;
        }
        total_sent += sent;
    }

    /* Receive response */
    int total_received = 0;
    int received;
    while ((received = recv(sockfd, response + total_received,
                           response_size - total_received - 1, 0)) > 0) {
        total_received += received;
        if ((size_t)total_received >= response_size - 1) break;
    }
    response[total_received] = '\0';

    close(sockfd);
    return total_received > 0 ? P2P0_OK : P2P0_ERROR;
}

/* Initialize PUBSUB protocol */
int p2p0_pubsub_init(p2p0_ctx_t *ctx, const char *gist_id,
                     const char *github_token, const char *peer_id) {
    if (!ctx || !gist_id || !peer_id) {
        return P2P0_ERROR_INVALID_ARG;
    }

    /* Allocate PUBSUB context */
    p2p0_pubsub_ctx_t *pubsub_ctx = (p2p0_pubsub_ctx_t *)malloc(sizeof(p2p0_pubsub_ctx_t));
    if (!pubsub_ctx) {
        return P2P0_ERROR;
    }

    memset(pubsub_ctx, 0, sizeof(p2p0_pubsub_ctx_t));
    snprintf(pubsub_ctx->gist_id, sizeof(pubsub_ctx->gist_id), "%s", gist_id);
    snprintf(pubsub_ctx->peer_id, sizeof(pubsub_ctx->peer_id), "%s", peer_id);
    pubsub_ctx->poll_interval = P2P0_PUBSUB_POLL_INTERVAL;

    if (github_token) {
        snprintf(pubsub_ctx->github_token, sizeof(pubsub_ctx->github_token), "%s", github_token);
    }

    snprintf(pubsub_ctx->api_url, sizeof(pubsub_ctx->api_url),
             "/gists/%s", gist_id);

    ctx->protocol_data = pubsub_ctx;
    ctx->state = P2P0_STATE_SIGNALING;

    return P2P0_OK;
}

/* Publish peer information to Gist */
int p2p0_pubsub_publish(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_pubsub_ctx_t *pubsub_ctx = (p2p0_pubsub_ctx_t *)ctx->protocol_data;

    /* Format peer data as JSON */
    char body[2048];
    snprintf(body, sizeof(body),
             "{\"files\":{\"%s.json\":{\"content\":\"{\\\"address\\\":\\\"%s\\\",\\\"port\\\":%u}\"}}}",
             pubsub_ctx->peer_id,
             ctx->local_peer.address,
             ctx->local_peer.port);

    /* Send PATCH request to update Gist */
    char response[8192];
    return http_request("140.82.114.6", "443", "PATCH",
                       pubsub_ctx->api_url,
                       pubsub_ctx->github_token,
                       body, response, sizeof(response));
}

/* Subscribe and wait for peer information from Gist */
int p2p0_pubsub_subscribe(p2p0_ctx_t *ctx, const char *peer_id, p2p0_peer_t *peer) {
    if (!ctx || !ctx->protocol_data || !peer_id || !peer) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_pubsub_ctx_t *pubsub_ctx = (p2p0_pubsub_ctx_t *)ctx->protocol_data;

    /* Poll Gist for peer information */
    int max_attempts = 30;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        char response[8192];
        int result = http_request("140.82.114.6", "443", "GET",
                                 pubsub_ctx->api_url,
                                 pubsub_ctx->github_token,
                                 NULL, response, sizeof(response));

        if (result == P2P0_OK) {
            /* Simple JSON parsing - look for peer_id.json file */
            char search_pattern[128];
            snprintf(search_pattern, sizeof(search_pattern), "\"%s.json\"", peer_id);

            char *file_pos = strstr(response, search_pattern);
            if (file_pos) {
                /* Find content field */
                char *content_pos = strstr(file_pos, "\"content\":\"");
                if (content_pos) {
                    content_pos += 11; /* Skip "content":" */
                    char address[128];
                    unsigned int port;

                    /* 
                     * Parse JSON content with escaped quotes
                     * Expected format: {\"address\":\"1.2.3.4\",\"port\":5000}
                     * 
                     * Note: This uses a complex sscanf pattern with escape sequences.
                     * For production code, consider using a proper JSON parser library.
                     * The pattern matches: {\\"address\\":\\"<address>\\",\\"port\\":<port>}
                     */
                    if (sscanf(content_pos, "{\\\"address\\\":\\\"%127[^\\]\\\",\\\"port\\\":%u}",
                               address, &port) == 2) {
                        snprintf(peer->address, sizeof(peer->address), "%s", address);
                        peer->port = (uint16_t)port;
                        return P2P0_OK;
                    }
                }
            }
        }

        /* Wait before next poll */
        #ifdef _WIN32
        Sleep(pubsub_ctx->poll_interval * 1000);
        #else
        sleep(pubsub_ctx->poll_interval);
        #endif
    }

    return P2P0_ERROR_TIMEOUT;
}

/* Establish P2P connection using PUBSUB protocol */
int p2p0_pubsub_connect(p2p0_ctx_t *ctx, const char *peer_id) {
    if (!ctx || !peer_id) {
        return P2P0_ERROR_INVALID_ARG;
    }

    p2p0_peer_t peer;
    int result = p2p0_pubsub_subscribe(ctx, peer_id, &peer);
    if (result != P2P0_OK) {
        return result;
    }

    /* Connect to remote peer */
    return p2p0_connect(ctx, peer.address, peer.port);
}

/* Set poll interval for checking Gist updates */
void p2p0_pubsub_set_poll_interval(p2p0_ctx_t *ctx, int interval) {
    if (!ctx || !ctx->protocol_data) {
        return;
    }

    p2p0_pubsub_ctx_t *pubsub_ctx = (p2p0_pubsub_ctx_t *)ctx->protocol_data;
    pubsub_ctx->poll_interval = interval;
}

/* Cleanup PUBSUB protocol resources */
void p2p0_pubsub_cleanup(p2p0_ctx_t *ctx) {
    if (!ctx || !ctx->protocol_data) {
        return;
    }

    free(ctx->protocol_data);
    ctx->protocol_data = NULL;
}
