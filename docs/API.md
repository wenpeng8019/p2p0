# API Documentation

## Core P2P0 API

### Data Structures

#### `p2p0_ctx_t`
Main P2P context structure containing connection state and configuration.

```c
typedef struct {
    p2p0_state_t state;      // Connection state
    int sockfd;              // Socket file descriptor
    p2p0_peer_t local_peer;  // Local peer information
    p2p0_peer_t remote_peer; // Remote peer information
    void *protocol_data;     // Protocol-specific data
} p2p0_ctx_t;
```

#### `p2p0_peer_t`
Peer information structure.

```c
typedef struct {
    char address[128];  // IP address
    uint16_t port;      // Port number
} p2p0_peer_t;
```

#### `p2p0_state_t`
Connection state enumeration.

```c
typedef enum {
    P2P0_STATE_INIT,         // Initial state
    P2P0_STATE_SIGNALING,    // Signaling in progress
    P2P0_STATE_CONNECTED,    // Connected to peer
    P2P0_STATE_DISCONNECTED, // Disconnected
    P2P0_STATE_ERROR         // Error state
} p2p0_state_t;
```

### Functions

#### `p2p0_init`
```c
int p2p0_init(p2p0_ctx_t *ctx);
```
Initialize a P2P context. Must be called before any other operations.

**Parameters:**
- `ctx`: Pointer to context structure to initialize

**Returns:**
- `P2P0_OK` on success
- `P2P0_ERROR_INVALID_ARG` if ctx is NULL
- `P2P0_ERROR_SOCKET` on socket initialization failure

**Example:**
```c
p2p0_ctx_t ctx;
if (p2p0_init(&ctx) != P2P0_OK) {
    fprintf(stderr, "Failed to initialize P2P context\n");
    return -1;
}
```

#### `p2p0_create_socket`
```c
int p2p0_create_socket(p2p0_ctx_t *ctx, uint16_t port);
```
Create a UDP socket and bind to local address.

**Parameters:**
- `ctx`: P2P context
- `port`: Local port to bind (0 for automatic assignment)

**Returns:**
- `P2P0_OK` on success
- `P2P0_ERROR_INVALID_ARG` if ctx is NULL
- `P2P0_ERROR_SOCKET` on socket creation failure
- `P2P0_ERROR_BIND` on bind failure

**Example:**
```c
// Bind to automatic port
if (p2p0_create_socket(&ctx, 0) != P2P0_OK) {
    fprintf(stderr, "Failed to create socket\n");
    return -1;
}
printf("Bound to %s:%u\n", ctx.local_peer.address, ctx.local_peer.port);
```

#### `p2p0_connect`
```c
int p2p0_connect(p2p0_ctx_t *ctx, const char *remote_address, uint16_t remote_port);
```
Connect to a remote peer.

**Parameters:**
- `ctx`: P2P context
- `remote_address`: Remote peer IP address
- `remote_port`: Remote peer port

**Returns:**
- `P2P0_OK` on success
- `P2P0_ERROR_INVALID_ARG` if parameters are invalid

**Example:**
```c
if (p2p0_connect(&ctx, "192.168.1.100", 5000) != P2P0_OK) {
    fprintf(stderr, "Failed to connect to peer\n");
    return -1;
}
```

#### `p2p0_send`
```c
int p2p0_send(p2p0_ctx_t *ctx, const void *data, size_t len);
```
Send data to connected peer.

**Parameters:**
- `ctx`: P2P context
- `data`: Data buffer to send
- `len`: Length of data in bytes

**Returns:**
- Number of bytes sent on success
- `P2P0_ERROR` or `P2P0_ERROR_INVALID_ARG` on failure

**Example:**
```c
const char *message = "Hello, peer!";
int sent = p2p0_send(&ctx, message, strlen(message));
if (sent < 0) {
    fprintf(stderr, "Failed to send data\n");
}
```

#### `p2p0_recv`
```c
int p2p0_recv(p2p0_ctx_t *ctx, void *buffer, size_t len);
```
Receive data from peer.

**Parameters:**
- `ctx`: P2P context
- `buffer`: Buffer to store received data
- `len`: Size of buffer in bytes

**Returns:**
- Number of bytes received on success
- `P2P0_ERROR` or `P2P0_ERROR_INVALID_ARG` on failure

**Example:**
```c
char buffer[1024];
int received = p2p0_recv(&ctx, buffer, sizeof(buffer));
if (received > 0) {
    buffer[received] = '\0';
    printf("Received: %s\n", buffer);
}
```

#### `p2p0_close`
```c
void p2p0_close(p2p0_ctx_t *ctx);
```
Close P2P connection and cleanup resources.

**Parameters:**
- `ctx`: P2P context to cleanup

**Example:**
```c
p2p0_close(&ctx);
```

#### `p2p0_strerror`
```c
const char *p2p0_strerror(int error_code);
```
Get human-readable error message for an error code.

**Parameters:**
- `error_code`: Error code from P2P0 function

**Returns:**
- String describing the error

**Example:**
```c
int result = p2p0_init(&ctx);
if (result != P2P0_OK) {
    fprintf(stderr, "Error: %s\n", p2p0_strerror(result));
}
```

## Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `P2P0_OK` | 0 | Success |
| `P2P0_ERROR` | -1 | General error |
| `P2P0_ERROR_SOCKET` | -2 | Socket operation failed |
| `P2P0_ERROR_BIND` | -3 | Bind operation failed |
| `P2P0_ERROR_CONNECT` | -4 | Connection failed |
| `P2P0_ERROR_TIMEOUT` | -5 | Operation timed out |
| `P2P0_ERROR_INVALID_ARG` | -6 | Invalid argument |

## SIMPLE Protocol API

See [p2p0_simple.h](../include/p2p0_simple.h) for detailed documentation.

Key functions:
- `p2p0_simple_init()` - Initialize protocol
- `p2p0_simple_register()` - Register with signaling server
- `p2p0_simple_get_peer()` - Get peer information
- `p2p0_simple_connect()` - Establish connection
- `p2p0_simple_cleanup()` - Cleanup resources

## ICE-RELAY Protocol API

See [p2p0_ice_relay.h](../include/p2p0_ice_relay.h) for detailed documentation.

Key functions:
- `p2p0_ice_relay_init()` - Initialize protocol
- `p2p0_ice_relay_add_candidate()` - Add ICE candidate
- `p2p0_ice_relay_send_offer()` - Send offer
- `p2p0_ice_relay_receive_answer()` - Receive answer
- `p2p0_ice_relay_connect()` - Establish connection
- `p2p0_ice_relay_cleanup()` - Cleanup resources

## PUBSUB Protocol API

See [p2p0_pubsub.h](../include/p2p0_pubsub.h) for detailed documentation.

Key functions:
- `p2p0_pubsub_init()` - Initialize protocol
- `p2p0_pubsub_publish()` - Publish peer info to Gist
- `p2p0_pubsub_subscribe()` - Subscribe to peer info
- `p2p0_pubsub_connect()` - Establish connection
- `p2p0_pubsub_cleanup()` - Cleanup resources

## Usage Patterns

### Basic P2P Communication

```c
// Initialize
p2p0_ctx_t ctx;
p2p0_init(&ctx);
p2p0_create_socket(&ctx, 0);

// Connect to peer
p2p0_connect(&ctx, "192.168.1.100", 5000);

// Send/receive
const char *msg = "Hello";
p2p0_send(&ctx, msg, strlen(msg));

char buffer[1024];
int received = p2p0_recv(&ctx, buffer, sizeof(buffer));

// Cleanup
p2p0_close(&ctx);
```

### Using SIMPLE Protocol

```c
p2p0_ctx_t ctx;
p2p0_init(&ctx);
p2p0_create_socket(&ctx, 0);

// Initialize SIMPLE protocol
p2p0_simple_init(&ctx, "127.0.0.1", 9000, "my_peer_id");
p2p0_simple_register(&ctx);

// Connect to another peer
p2p0_simple_connect(&ctx, "other_peer_id");

// Send/receive data
p2p0_send(&ctx, "Hello", 5);

// Cleanup
p2p0_simple_cleanup(&ctx);
p2p0_close(&ctx);
```
