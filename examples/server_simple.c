#include "../include/p2p0_simple.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    #include <signal.h>
#endif

/* Peer registry */
typedef struct {
    char peer_id[64];
    char address[128];
    uint16_t port;
    int active;
} peer_entry_t;

static peer_entry_t peer_registry[P2P0_SIMPLE_MAX_PEERS];
static int running = 1;

#ifndef _WIN32
static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}
#endif

/* Find peer in registry */
static peer_entry_t *find_peer(const char *peer_id) {
    for (int i = 0; i < P2P0_SIMPLE_MAX_PEERS; i++) {
        if (peer_registry[i].active &&
            strcmp(peer_registry[i].peer_id, peer_id) == 0) {
            return &peer_registry[i];
        }
    }
    return NULL;
}

/* Add or update peer in registry */
static int register_peer(const char *peer_id, const char *address, uint16_t port) {
    peer_entry_t *peer = find_peer(peer_id);

    if (peer) {
        /* Update existing peer */
        snprintf(peer->address, sizeof(peer->address), "%s", address);
        peer->port = port;
        return 0;
    }

    /* Add new peer */
    for (int i = 0; i < P2P0_SIMPLE_MAX_PEERS; i++) {
        if (!peer_registry[i].active) {
            snprintf(peer_registry[i].peer_id, sizeof(peer_registry[i].peer_id), "%s", peer_id);
            snprintf(peer_registry[i].address, sizeof(peer_registry[i].address), "%s", address);
            peer_registry[i].port = port;
            peer_registry[i].active = 1;
            return 0;
        }
    }

    return -1; /* Registry full */
}

int main(int argc, char *argv[]) {
    int port = 9000;
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    p2p0_simple_msg_t msg;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("SIMPLE Signaling Server v1.0\n");
    printf("Starting on port %d...\n", port);

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    /* Initialize peer registry */
    memset(peer_registry, 0, sizeof(peer_registry));

    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    /* Bind to port */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Failed to bind to port %d\n", port);
        close(sockfd);
        return 1;
    }

    printf("Server listening on 0.0.0.0:%d\n", port);

    /* Main server loop */
    while (running) {
        client_addr_len = sizeof(client_addr);
        int received = recvfrom(sockfd, (char *)&msg, sizeof(msg), 0,
                               (struct sockaddr *)&client_addr, &client_addr_len);

        if (received < 0) {
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        printf("Received message from %s:%u, type=%u, peer_id=%s\n",
               client_ip, ntohs(client_addr.sin_port), msg.type, msg.peer_id);

        switch (msg.type) {
            case P2P0_SIMPLE_MSG_HELLO: {
                /* Register peer */
                char address[128];
                unsigned int peer_port;

                if (sscanf(msg.data, "%127[^:]:%u", address, &peer_port) == 2) {
                    register_peer(msg.peer_id, address, peer_port);
                    printf("Registered peer %s at %s:%u\n", msg.peer_id, address, peer_port);

                    /* Send acknowledgment */
                    p2p0_simple_msg_t response;
                    memset(&response, 0, sizeof(response));
                    response.type = P2P0_SIMPLE_MSG_HELLO;
                    response.version = 1;
                    response.length = sizeof(response);
                    snprintf(response.data, sizeof(response.data), "OK");

                    sendto(sockfd, (const char *)&response, sizeof(response), 0,
                          (struct sockaddr *)&client_addr, client_addr_len);
                }
                break;
            }

            case P2P0_SIMPLE_MSG_PEER_INFO: {
                /* Lookup peer */
                peer_entry_t *peer = find_peer(msg.peer_id);

                p2p0_simple_msg_t response;
                memset(&response, 0, sizeof(response));
                response.type = P2P0_SIMPLE_MSG_PEER_INFO;
                response.version = 1;
                response.length = sizeof(response);

                if (peer) {
                    snprintf(response.data, sizeof(response.data), "%s:%u",
                            peer->address, peer->port);
                    printf("Sent peer info for %s: %s\n", msg.peer_id, response.data);
                } else {
                    snprintf(response.data, sizeof(response.data), "NOT_FOUND");
                    printf("Peer %s not found\n", msg.peer_id);
                }

                sendto(sockfd, (const char *)&response, sizeof(response), 0,
                      (struct sockaddr *)&client_addr, client_addr_len);
                break;
            }

            case P2P0_SIMPLE_MSG_PING: {
                /* Respond to ping */
                p2p0_simple_msg_t response;
                memset(&response, 0, sizeof(response));
                response.type = P2P0_SIMPLE_MSG_PONG;
                response.version = 1;
                response.length = sizeof(response);

                sendto(sockfd, (const char *)&response, sizeof(response), 0,
                      (struct sockaddr *)&client_addr, client_addr_len);
                break;
            }
        }
    }

    printf("\nShutting down server...\n");
    close(sockfd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
