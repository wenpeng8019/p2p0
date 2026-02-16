# Examples and Usage Guide

This document provides detailed examples of using the P2P0 library with all three signaling protocols.

## Table of Contents
1. [SIMPLE Protocol Examples](#simple-protocol-examples)
2. [ICE-RELAY Protocol Examples](#ice-relay-protocol-examples)
3. [PUBSUB Protocol Examples](#pubsub-protocol-examples)
4. [Building Custom Applications](#building-custom-applications)

---

## SIMPLE Protocol Examples

The SIMPLE protocol uses UDP for fast, lightweight signaling. Best for LAN environments.

### Starting the Signaling Server

```bash
# Start on default port 9000
./bin/server_simple

# Start on custom port
./bin/server_simple 8888
```

### Example 1: Simple Echo Server

**Terminal 1 - Start the signaling server:**
```bash
./bin/server_simple 9000
```

**Terminal 2 - Start listening peer (echo server):**
```bash
./bin/client_simple listen peer1 127.0.0.1 9000
```
This peer will:
- Register with the signaling server as "peer1"
- Listen for incoming P2P messages
- Echo back any received messages

**Terminal 3 - Start connecting peer (echo client):**
```bash
./bin/client_simple connect peer2 127.0.0.1 9000
```
This peer will:
- Register with the signaling server as "peer2"
- Look up "peer1" from the signaling server
- Send test messages and receive echoed responses

### Example 2: Custom SIMPLE Application

Create a file `my_p2p_app.c`:

```c
#include "p2p0.h"
#include "p2p0_simple.h"
#include <stdio.h>
#include <string.h>

int main() {
    p2p0_ctx_t ctx;
    
    // Initialize
    p2p0_init(&ctx);
    p2p0_create_socket(&ctx, 0);
    
    printf("My address: %s:%u\n", 
           ctx.local_peer.address, ctx.local_peer.port);
    
    // Connect to signaling server
    p2p0_simple_init(&ctx, "127.0.0.1", 9000, "my_app");
    p2p0_simple_register(&ctx);
    
    // Connect to another peer
    p2p0_simple_connect(&ctx, "other_peer");
    
    // Send message
    const char *msg = "Hello, P2P world!";
    p2p0_send(&ctx, msg, strlen(msg));
    
    // Receive response
    char buffer[1024];
    int received = p2p0_recv(&ctx, buffer, sizeof(buffer));
    if (received > 0) {
        buffer[received] = '\0';
        printf("Received: %s\n", buffer);
    }
    
    // Cleanup
    p2p0_simple_cleanup(&ctx);
    p2p0_close(&ctx);
    
    return 0;
}
```

Compile:
```bash
gcc -I./include my_p2p_app.c build/libp2p0.a -o my_p2p_app
```

---

## ICE-RELAY Protocol Examples

The ICE-RELAY protocol uses TCP with support for ICE candidates and relay functionality.

### Starting the ICE-RELAY Server

```bash
# Start on default port 9001
./bin/server_ice_relay

# Start on custom port
./bin/server_ice_relay 8889
```

### Example 1: Basic ICE Connection

**Terminal 1 - Start the relay server:**
```bash
./bin/server_ice_relay 9001
```

**Terminal 2 - Peer A (offer):**
```bash
./bin/client_ice_relay offer session123 127.0.0.1 9001
```

**Terminal 3 - Peer B (answer):**
```bash
./bin/client_ice_relay answer session123 127.0.0.1 9001
```

### Example 2: Custom ICE Application

```c
#include "p2p0.h"
#include "p2p0_ice_relay.h"
#include <stdio.h>

int main() {
    p2p0_ctx_t ctx;
    
    // Initialize
    p2p0_init(&ctx);
    p2p0_create_socket(&ctx, 0);
    
    // Initialize ICE-RELAY protocol
    p2p0_ice_relay_init(&ctx, "relay.example.com", 9001, "my-session");
    
    // Add local candidates
    p2p0_ice_relay_add_candidate(&ctx, ctx.local_peer.address, 
                                 ctx.local_peer.port, 100);
    
    // Send offer
    p2p0_ice_relay_send_offer(&ctx);
    
    // Receive answer
    p2p0_ice_relay_receive_answer(&ctx);
    
    // Connect to best candidate
    p2p0_ice_relay_connect(&ctx);
    
    // Now you can use p2p0_send/recv
    
    // Cleanup
    p2p0_ice_relay_cleanup(&ctx);
    p2p0_close(&ctx);
    
    return 0;
}
```

---

## PUBSUB Protocol Examples

The PUBSUB protocol uses GitHub Gist for serverless signaling.

### Prerequisites

1. Create a GitHub account if you don't have one
2. Generate a Personal Access Token (PAT):
   - Go to GitHub Settings → Developer settings → Personal access tokens
   - Generate new token with `gist` scope
3. Create a new Gist (public or private)
4. Note the Gist ID from the URL: `https://gist.github.com/username/{gist_id}`

### Example 1: Basic Gist Signaling

**Note:** This example demonstrates the protocol concept. For actual use, HTTPS/TLS support is required.

**Terminal 1 - Publisher:**
```bash
./bin/client_pubsub publish peer1 abc123def456 ghp_yourTokenHere
```

**Terminal 2 - Subscriber:**
```bash
./bin/client_pubsub subscribe peer2 abc123def456
```

### Example 2: Production-Ready PUBSUB

For production use, you would need to implement HTTPS. Here's a conceptual example using a hypothetical TLS-enabled version:

```c
#include "p2p0.h"
#include "p2p0_pubsub.h"

int main() {
    p2p0_ctx_t ctx;
    
    // Initialize
    p2p0_init(&ctx);
    p2p0_create_socket(&ctx, 0);
    
    // Initialize PUBSUB with your Gist
    p2p0_pubsub_init(&ctx, 
                     "abc123def456",              // Gist ID
                     "ghp_yourTokenHere",         // GitHub token
                     "my-peer-id");               // Your peer ID
    
    // Publish your info to Gist
    p2p0_pubsub_publish(&ctx);
    
    // Wait for and connect to another peer
    p2p0_pubsub_connect(&ctx, "other-peer-id");
    
    // Use P2P connection
    p2p0_send(&ctx, "Hello", 5);
    
    // Cleanup
    p2p0_pubsub_cleanup(&ctx);
    p2p0_close(&ctx);
    
    return 0;
}
```

---

## Building Custom Applications

### Project Structure

```
my-p2p-project/
├── Makefile
├── src/
│   └── my_app.c
└── p2p0/               # Copy or submodule
    ├── include/
    ├── src/
    └── build/
```

### Makefile Template

```makefile
# My P2P Application Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./p2p0/include
LDFLAGS = 

# Link against P2P0 library
LIBS = ./p2p0/build/libp2p0.a

my_app: src/my_app.c $(LIBS)
	$(CC) $(CFLAGS) $< $(LIBS) -o $@ $(LDFLAGS)

$(LIBS):
	cd p2p0 && make

clean:
	rm -f my_app

.PHONY: clean
```

### Application Template

```c
#include "p2p0.h"
#include "p2p0_simple.h"  // or p2p0_ice_relay.h or p2p0_pubsub.h
#include <stdio.h>
#include <string.h>
#include <signal.h>

static int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <peer_id>\n", argv[0]);
        return 1;
    }
    
    const char *peer_id = argv[1];
    
    // Setup signal handling
    signal(SIGINT, signal_handler);
    
    // Initialize P2P
    p2p0_ctx_t ctx;
    if (p2p0_init(&ctx) != P2P0_OK) {
        fprintf(stderr, "Failed to initialize\n");
        return 1;
    }
    
    // Create socket
    if (p2p0_create_socket(&ctx, 0) != P2P0_OK) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }
    
    printf("Local address: %s:%u\n", 
           ctx.local_peer.address, ctx.local_peer.port);
    
    // Initialize protocol (SIMPLE example)
    if (p2p0_simple_init(&ctx, "127.0.0.1", 9000, peer_id) != P2P0_OK) {
        fprintf(stderr, "Failed to initialize protocol\n");
        p2p0_close(&ctx);
        return 1;
    }
    
    // Register with server
    if (p2p0_simple_register(&ctx) != P2P0_OK) {
        fprintf(stderr, "Failed to register\n");
        p2p0_simple_cleanup(&ctx);
        p2p0_close(&ctx);
        return 1;
    }
    
    printf("Registered as %s\n", peer_id);
    printf("Ready for P2P communication\n");
    
    // Main application loop
    char buffer[1024];
    while (running) {
        int received = p2p0_recv(&ctx, buffer, sizeof(buffer) - 1);
        if (received > 0) {
            buffer[received] = '\0';
            printf("Received: %s\n", buffer);
            
            // Process message and send response
            // ...
        }
    }
    
    // Cleanup
    printf("\nShutting down...\n");
    p2p0_simple_cleanup(&ctx);
    p2p0_close(&ctx);
    
    return 0;
}
```

---

## Advanced Topics

### Non-blocking I/O

To use non-blocking sockets:

```c
#include <fcntl.h>

// After creating socket
int flags = fcntl(ctx.sockfd, F_GETFL, 0);
fcntl(ctx.sockfd, F_SETFL, flags | O_NONBLOCK);
```

### Timeouts

Set receive timeout:

```c
#include <sys/time.h>

struct timeval tv;
tv.tv_sec = 5;  // 5 seconds
tv.tv_usec = 0;
setsockopt(ctx.sockfd, SOL_SOCKET, SO_RCVTIMEO, 
           (const char*)&tv, sizeof(tv));
```

### Error Handling

Always check return values:

```c
int result = p2p0_send(&ctx, data, len);
if (result < 0) {
    fprintf(stderr, "Send failed: %s\n", p2p0_strerror(result));
    // Handle error
}
```

---

## Common Issues and Solutions

### Issue: "Address already in use"
**Solution:** Port is still in use. Wait a moment or use different port.

### Issue: "Connection refused"
**Solution:** Make sure signaling server is running first.

### Issue: Peer not found
**Solution:** Ensure the peer has registered with the server and is still active.

### Issue: Firewall blocks connection
**Solution:** 
- Open required ports in firewall
- Use ICE-RELAY protocol for better NAT traversal
- Check firewall logs

---

## Performance Tips

1. **Buffer Sizing:** Use appropriate buffer sizes for your use case
2. **Keep-Alive:** Send periodic keep-alive messages to maintain connections
3. **Batching:** Batch small messages together to reduce overhead
4. **Protocol Choice:** Choose protocol based on your network environment

---

## Next Steps

1. Study the source code in `src/` and `examples/`
2. Read the [Protocol Specifications](PROTOCOLS.md)
3. Consult the [API Documentation](API.md)
4. Build your own P2P application!

For more help, please open an issue on GitHub.
