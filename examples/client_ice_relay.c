#include "../include/p2p0.h"
#include "../include/p2p0_ice_relay.h"
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
    printf("Usage: %s <mode> <session_id> [server_address] [server_port]\n", prog);
    printf("  mode: 'offer' or 'answer'\n");
    printf("  session_id: Unique session identifier\n");
    printf("  server_address: Signaling server address (default: 127.0.0.1)\n");
    printf("  server_port: Signaling server port (default: 9001)\n");
    printf("\nExample:\n");
    printf("  Terminal 1: %s offer session123\n", prog);
    printf("  Terminal 2: %s answer session123\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *session_id = argv[2];
    const char *server_address = argc > 3 ? argv[3] : "127.0.0.1";
    int server_port = argc > 4 ? atoi(argv[4]) : 9001;

    printf("P2P0 ICE-RELAY Protocol Client\n");
    printf("Mode: %s, Session ID: %s\n", mode, session_id);
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

    /* Initialize ICE-RELAY protocol */
    if (p2p0_ice_relay_init(&ctx, server_address, server_port, session_id) != P2P0_OK) {
        fprintf(stderr, "Failed to initialize ICE-RELAY protocol\n");
        p2p0_close(&ctx);
        return 1;
    }

    /* Add local candidate */
    printf("Adding ICE candidate...\n");
    if (p2p0_ice_relay_add_candidate(&ctx, ctx.local_peer.address,
                                     ctx.local_peer.port, 100) != P2P0_OK) {
        fprintf(stderr, "Failed to add ICE candidate\n");
        p2p0_ice_relay_cleanup(&ctx);
        p2p0_close(&ctx);
        return 1;
    }

    if (strcmp(mode, "offer") == 0) {
        /* Offer mode: send offer and wait for answer */
        printf("Sending offer...\n");
        if (p2p0_ice_relay_send_offer(&ctx) != P2P0_OK) {
            fprintf(stderr, "Failed to send offer\n");
            p2p0_ice_relay_cleanup(&ctx);
            p2p0_close(&ctx);
            return 1;
        }

        printf("Waiting for answer...\n");
        if (p2p0_ice_relay_receive_answer(&ctx) != P2P0_OK) {
            fprintf(stderr, "Failed to receive answer\n");
            p2p0_ice_relay_cleanup(&ctx);
            p2p0_close(&ctx);
            return 1;
        }

        printf("Received answer! Establishing connection...\n");

    } else if (strcmp(mode, "answer") == 0) {
        /* Answer mode: wait for offer and send answer */
        printf("Waiting for offer...\n");
        
        /* For simplicity, this example shows the basic flow */
        printf("This is a simplified example.\n");
        printf("In production, you would wait for the offer and send an answer.\n");

    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        print_usage(argv[0]);
        p2p0_ice_relay_cleanup(&ctx);
        p2p0_close(&ctx);
        return 1;
    }

    /* Connect using ICE-RELAY */
    printf("Connecting...\n");
    if (p2p0_ice_relay_connect(&ctx) == P2P0_OK) {
        printf("P2P connection established!\n");

        /* Send test message */
        const char *message = "Hello via ICE-RELAY!";
        printf("Sending: %s\n", message);
        p2p0_send(&ctx, message, strlen(message));
    }

    /* Cleanup */
    p2p0_ice_relay_cleanup(&ctx);
    p2p0_close(&ctx);

    return 0;
}
