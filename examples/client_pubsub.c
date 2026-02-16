#include "../include/p2p0.h"
#include "../include/p2p0_pubsub.h"
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
    printf("Usage: %s <mode> <peer_id> <gist_id> [github_token]\n", prog);
    printf("  mode: 'publish' or 'subscribe'\n");
    printf("  peer_id: Your unique peer identifier\n");
    printf("  gist_id: GitHub Gist ID for signaling\n");
    printf("  github_token: GitHub API token (optional for subscribe)\n");
    printf("\nExample:\n");
    printf("  Terminal 1: %s publish peer1 abc123def456 ghp_token\n", prog);
    printf("  Terminal 2: %s subscribe peer2 abc123def456\n", prog);
    printf("\nNote: This example demonstrates the PUBSUB protocol concept.\n");
    printf("For actual GitHub API usage, proper HTTPS support is required.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *peer_id = argv[2];
    const char *gist_id = argv[3];
    const char *github_token = argc > 4 ? argv[4] : NULL;

    printf("P2P0 PUBSUB Protocol Client (GitHub Gist Signaling)\n");
    printf("Mode: %s, Peer ID: %s\n", mode, peer_id);
    printf("Gist ID: %s\n\n", gist_id);

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

    /* Initialize PUBSUB protocol */
    if (p2p0_pubsub_init(&ctx, gist_id, github_token, peer_id) != P2P0_OK) {
        fprintf(stderr, "Failed to initialize PUBSUB protocol\n");
        p2p0_close(&ctx);
        return 1;
    }

    if (strcmp(mode, "publish") == 0) {
        /* Publish mode: publish peer info to Gist */
        printf("Publishing peer information to Gist...\n");
        
        if (!github_token) {
            fprintf(stderr, "Error: GitHub token is required for publishing\n");
            p2p0_pubsub_cleanup(&ctx);
            p2p0_close(&ctx);
            return 1;
        }

        printf("\nNote: This example demonstrates the concept.\n");
        printf("For actual GitHub Gist updates, you would need:\n");
        printf("  1. HTTPS support (TLS/SSL)\n");
        printf("  2. Valid GitHub token with gist permissions\n");
        printf("  3. Proper error handling\n\n");

        printf("Conceptually, the library would:\n");
        printf("  - Update Gist file '%s.json' with your peer info\n", peer_id);
        printf("  - Content: {\"address\":\"%s\",\"port\":%u}\n",
               ctx.local_peer.address, ctx.local_peer.port);

        printf("\nListening for P2P messages...\n");
        char buffer[1024];
        while (1) {
            int received = p2p0_recv(&ctx, buffer, sizeof(buffer) - 1);
            if (received > 0) {
                buffer[received] = '\0';
                printf("Received: %s\n", buffer);
            }
            sleep(1);
        }

    } else if (strcmp(mode, "subscribe") == 0) {
        /* Subscribe mode: lookup peer from Gist */
        printf("Looking up 'peer1' from Gist...\n");

        printf("\nNote: This example demonstrates the concept.\n");
        printf("For actual GitHub Gist reading, you would need:\n");
        printf("  1. HTTPS support (TLS/SSL)\n");
        printf("  2. JSON parsing\n");
        printf("  3. Proper polling mechanism\n\n");

        printf("Conceptually, the library would:\n");
        printf("  - Poll Gist for file 'peer1.json'\n");
        printf("  - Parse JSON to get peer address and port\n");
        printf("  - Establish P2P connection\n\n");

        printf("For demonstration, simulating peer lookup...\n");
        printf("In real usage, p2p0_pubsub_subscribe() would:\n");
        printf("  - Return peer address and port from Gist\n");
        printf("  - Then call p2p0_connect() to establish connection\n");

    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        print_usage(argv[0]);
        p2p0_pubsub_cleanup(&ctx);
        p2p0_close(&ctx);
        return 1;
    }

    /* Cleanup */
    p2p0_pubsub_cleanup(&ctx);
    p2p0_close(&ctx);

    return 0;
}
