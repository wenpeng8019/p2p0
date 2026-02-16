#include "../include/p2p0_ice_relay.h"
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

#define MAX_SESSIONS 32
#define MAX_CLIENTS 64

/* Session info */
typedef struct {
    char session_id[64];
    int client_fds[2];
    int num_clients;
    int active;
} session_t;

static session_t sessions[MAX_SESSIONS];
static int running = 1;

#ifndef _WIN32
static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}
#endif

/* Find or create session */
static session_t *find_session(const char *session_id, int create) {
    session_t *empty_slot = NULL;

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && strcmp(sessions[i].session_id, session_id) == 0) {
            return &sessions[i];
        }
        if (!sessions[i].active && !empty_slot) {
            empty_slot = &sessions[i];
        }
    }

    if (create && empty_slot) {
        snprintf(empty_slot->session_id, sizeof(empty_slot->session_id), "%s", session_id);
        empty_slot->num_clients = 0;
        empty_slot->active = 1;
        return empty_slot;
    }

    return NULL;
}

/* 
 * Handle client connection
 * 
 * NOTE: This is a simple synchronous implementation for demonstration.
 * LIMITATIONS:
 * - Handles clients sequentially in the main thread
 * - One slow/unresponsive client blocks all others
 * - No timeouts on blocking recv() operations
 * 
 * For production use, consider:
 * - Multi-threading or async I/O (select/poll/epoll)
 * - Setting socket timeouts with SO_RCVTIMEO
 * - Connection pooling and rate limiting
 */
static void handle_client(int client_fd) {
    p2p0_ice_relay_msg_t msg;
    int received = recv(client_fd, (char *)&msg, sizeof(msg), 0);

    if (received < (int)sizeof(msg)) {
        close(client_fd);
        return;
    }

    printf("Received message type=%u, session_id=%s\n", msg.type, msg.session_id);

    session_t *session = find_session(msg.session_id, 1);
    if (!session) {
        printf("Session registry full\n");
        close(client_fd);
        return;
    }

    /* Add client to session */
    if (session->num_clients < 2) {
        session->client_fds[session->num_clients] = client_fd;
        session->num_clients++;
        printf("Client joined session %s (%d/2)\n", msg.session_id, session->num_clients);

        /* If both clients connected, relay messages */
        if (session->num_clients == 2) {
            printf("Session %s: Both clients connected, starting relay\n", msg.session_id);

            /* Forward offer to second client */
            if (msg.type == P2P0_ICE_RELAY_MSG_OFFER) {
                send(session->client_fds[1], (const char *)&msg, sizeof(msg), 0);

                /* Wait for answer from second client */
                p2p0_ice_relay_msg_t answer;
                received = recv(session->client_fds[1], (char *)&answer, sizeof(answer), 0);
                if (received > 0 && answer.type == P2P0_ICE_RELAY_MSG_ANSWER) {
                    /* Forward answer to first client */
                    send(session->client_fds[0], (const char *)&answer, sizeof(answer), 0);
                    printf("Session %s: Completed signaling exchange\n", msg.session_id);
                }
            }
        }
    } else {
        printf("Session %s already has 2 clients\n", msg.session_id);
        close(client_fd);
    }
}

int main(int argc, char *argv[]) {
    int port = 9001;
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("ICE-RELAY Signaling Server v1.0\n");
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

    /* Initialize sessions */
    memset(sessions, 0, sizeof(sessions));

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    /* Set socket options */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    /* Bind to port */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Failed to bind to port %d\n", port);
        close(server_fd);
        return 1;
    }

    /* Listen for connections */
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        fprintf(stderr, "Failed to listen\n");
        close(server_fd);
        return 1;
    }

    printf("Server listening on 0.0.0.0:%d\n", port);

    /* Main server loop */
    while (running) {
        client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd < 0) {
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Client connected from %s:%u\n", client_ip, ntohs(client_addr.sin_port));

        /* Handle client in same thread (simple implementation) */
        handle_client(client_fd);
    }

    printf("\nShutting down server...\n");
    close(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
