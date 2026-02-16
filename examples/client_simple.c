#include "../include/p2p0.h"
#include "../include/p2p0_simple.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep((x) * 1000)
#else
    #include <unistd.h>
#endif

void print_usage(const char *prog) {
    printf("Usage: %s <mode> <peer_id> [server_address] [server_port]\n", prog);
    printf("  mode: 'listen' or 'connect'\n");
    printf("  peer_id: Your unique peer identifier\n");
    printf("  server_address: Signaling server address (default: 127.0.0.1)\n");
    printf("  server_port: Signaling server port (default: 9000)\n");
    printf("\nExample:\n");
    printf("  Terminal 1: %s listen peer1\n", prog);
    printf("  Terminal 2: %s connect peer2\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *peer_id = argv[2];
    const char *server_address = argc > 3 ? argv[3] : "127.0.0.1";
    int server_port = argc > 4 ? atoi(argv[4]) : 9000;

    printf("P2P0 SIMPLE Protocol Client\n");
    printf("Mode: %s, Peer ID: %s\n", mode, peer_id);
    printf("Signaling Server: %s:%d\n\n", server_address, server_port);

    /* Initialize P2P context */
    p2p0_ctx_t ctx;
    if (p2p0_init(&ctx) != P2P0_OK) {
        fprintf(stderr, "Failed to initialize P2P context\n");
        return 1;
    }

    /* Create socket */
    if (p2p0_create_socket(&ctx, 0) != P2P0_OK) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    printf("Local endpoint: %s:%u\n", ctx.local_peer.address, ctx.local_peer.port);

    /* Initialize SIMPLE protocol */
    if (p2p0_simple_init(&ctx, server_address, server_port, peer_id) != P2P0_OK) {
        fprintf(stderr, "Failed to initialize SIMPLE protocol\n");
        p2p0_close(&ctx);
        return 1;
    }

    /* Register with signaling server */
    printf("Registering with signaling server...\n");
    if (p2p0_simple_register(&ctx) != P2P0_OK) {
        fprintf(stderr, "Failed to register with signaling server\n");
        p2p0_simple_cleanup(&ctx);
        p2p0_close(&ctx);
        return 1;
    }

    printf("Successfully registered!\n\n");

    if (strcmp(mode, "listen") == 0) {
        /* Listen mode: wait for messages */
        printf("Listening for P2P messages...\n");
        printf("Start another client with 'connect' mode to establish connection.\n\n");

        char buffer[1024];
        while (1) {
            int received = p2p0_recv(&ctx, buffer, sizeof(buffer) - 1);
            if (received > 0) {
                buffer[received] = '\0';
                printf("Received: %s\n", buffer);

                /* Echo back */
                const char *reply = "Message received!";
                p2p0_send(&ctx, reply, strlen(reply));
            }
            sleep(1);
        }
    } else if (strcmp(mode, "connect") == 0) {
        /* Connect mode: connect to peer1 */
        printf("Looking up peer 'peer1'...\n");

        p2p0_peer_t peer;
        if (p2p0_simple_get_peer(&ctx, "peer1", &peer) != P2P0_OK) {
            fprintf(stderr, "Failed to get peer information\n");
            printf("Make sure peer1 is running in listen mode!\n");
            p2p0_simple_cleanup(&ctx);
            p2p0_close(&ctx);
            return 1;
        }

        printf("Found peer1 at %s:%u\n", peer.address, peer.port);

        /* Connect to peer */
        if (p2p0_connect(&ctx, peer.address, peer.port) != P2P0_OK) {
            fprintf(stderr, "Failed to connect to peer\n");
            p2p0_simple_cleanup(&ctx);
            p2p0_close(&ctx);
            return 1;
        }

        printf("Connected! Sending test messages...\n\n");

        /* Send test messages */
        for (int i = 0; i < 5; i++) {
            char message[128];
            snprintf(message, sizeof(message), "Hello from %s, message #%d", peer_id, i + 1);

            printf("Sending: %s\n", message);
            p2p0_send(&ctx, message, strlen(message));

            /* Wait for response */
            char buffer[1024];
            int received = p2p0_recv(&ctx, buffer, sizeof(buffer) - 1);
            if (received > 0) {
                buffer[received] = '\0';
                printf("Received: %s\n\n", buffer);
            }

            sleep(2);
        }

        printf("Test completed!\n");
    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        print_usage(argv[0]);
        p2p0_simple_cleanup(&ctx);
        p2p0_close(&ctx);
        return 1;
    }

    /* Cleanup */
    p2p0_simple_cleanup(&ctx);
    p2p0_close(&ctx);

    return 0;
}
