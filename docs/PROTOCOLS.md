# Protocol Specifications

This document describes the three signaling protocols supported by P2P0.

## SIMPLE Protocol (UDP-based)

### Overview
SIMPLE is a lightweight UDP-based signaling protocol designed for fast peer discovery and connection establishment.

### Architecture
```
┌─────────┐         ┌──────────────┐         ┌─────────┐
│ Peer A  │◄────────►│   Signaling  │◄────────►│ Peer B  │
│         │   UDP    │    Server    │   UDP    │         │
└─────────┘          └──────────────┘          └─────────┘
     │                                               │
     └───────────────────────────────────────────────┘
                    P2P Connection (UDP)
```

### Message Format
```c
struct p2p0_simple_msg {
    uint8_t type;        // Message type
    uint8_t version;     // Protocol version
    uint16_t length;     // Message length
    char peer_id[64];    // Peer identifier
    char data[444];      // Payload data
};
```

### Message Types

| Type | Value | Description |
|------|-------|-------------|
| `HELLO` | 1 | Register peer with server |
| `PEER_INFO` | 2 | Request/response for peer information |
| `CONNECT_REQ` | 3 | Connection request |
| `CONNECT_ACK` | 4 | Connection acknowledgment |
| `PING` | 5 | Keep-alive ping |
| `PONG` | 6 | Keep-alive response |

### Connection Flow

```
Peer A                  Server                  Peer B
  |                       |                       |
  |---HELLO-------------->|                       |
  |<--ACK----------------|                       |
  |                       |<------HELLO-----------|
  |                       |-------ACK------------>|
  |                       |                       |
  |---PEER_INFO(B)------->|                       |
  |<--PEER_INFO(B data)---|                       |
  |                       |                       |
  |<===========P2P Connection Established========>|
  |                       |                       |
```

### Advantages
- Low latency (UDP)
- Simple implementation
- Good for LAN environments

### Limitations
- Requires signaling server
- UDP may be blocked by firewalls
- No built-in reliability

---

## ICE-RELAY Protocol (TCP-based)

### Overview
ICE-RELAY is a TCP-based signaling protocol with support for ICE candidate exchange and relay functionality for NAT traversal.

### Architecture
```
┌─────────┐         ┌──────────────┐         ┌─────────┐
│ Peer A  │◄────────►│  ICE-RELAY   │◄────────►│ Peer B  │
│         │   TCP    │    Server    │   TCP    │         │
└─────────┘          └──────────────┘          └─────────┘
     │                     │  │                      │
     │    Direct P2P       │  │  Relay (if needed)   │
     └─────────────────────┘  └──────────────────────┘
```

### Message Format
```c
struct p2p0_ice_relay_msg {
    uint8_t type;           // Message type
    uint8_t version;        // Protocol version
    uint16_t length;        // Message length
    char session_id[64];    // Session identifier
    char data[956];         // Payload data
};
```

### Message Types

| Type | Value | Description |
|------|-------|-------------|
| `OFFER` | 1 | SDP offer with ICE candidates |
| `ANSWER` | 2 | SDP answer with ICE candidates |
| `CANDIDATE` | 3 | Additional ICE candidate |
| `RELAY_REQ` | 4 | Request relay connection |
| `RELAY_DATA` | 5 | Relayed data packet |

### ICE Candidate Format
```
address:port:priority
```

Example:
```
192.168.1.100:5000:100
```

### Connection Flow

```
Peer A                  Server                  Peer B
  |                       |                       |
  |---OFFER(candidates)-->|                       |
  |                       |---OFFER-------------->|
  |                       |<--ANSWER(candidates)--|
  |<--ANSWER--------------|                       |
  |                       |                       |
  |<======Try Direct P2P Connection=============>|
  |                       |                       |
  [If direct fails]       |                       |
  |---RELAY_REQ---------->|<------RELAY_REQ-------|
  |                       |                       |
  |<===========Relayed Connection===============>|
  |                       |                       |
```

### Advantages
- Reliable (TCP)
- NAT traversal support
- Fallback relay mechanism
- Good for public internet

### Limitations
- Requires TCP signaling server
- Higher latency than UDP
- More complex implementation

---

## PUBSUB Protocol (GitHub Gist-based)

### Overview
PUBSUB is an innovative serverless signaling protocol that uses GitHub Gist as a message broker for peer discovery.

### Architecture
```
┌─────────┐         ┌──────────────┐         ┌─────────┐
│ Peer A  │◄────────►│ GitHub Gist  │◄────────►│ Peer B  │
│         │  HTTPS   │    (JSON)    │  HTTPS   │         │
└─────────┘          └──────────────┘          └─────────┘
     │                                               │
     └───────────────────────────────────────────────┘
                    P2P Connection (UDP)
```

### Gist Structure
Each peer creates a JSON file in the Gist with their peer ID as the filename:

```json
{
  "peer1.json": {
    "address": "203.0.113.1",
    "port": 5000
  },
  "peer2.json": {
    "address": "198.51.100.1",
    "port": 5001
  }
}
```

### GitHub API Usage

**Publish peer information (PATCH request):**
```
PATCH /gists/{gist_id}
Authorization: Bearer {token}
Content-Type: application/json

{
  "files": {
    "peer1.json": {
      "content": "{\"address\":\"203.0.113.1\",\"port\":5000}"
    }
  }
}
```

**Subscribe to peer information (GET request):**
```
GET /gists/{gist_id}
Authorization: Bearer {token}
```

### Connection Flow

```
Peer A                  GitHub Gist              Peer B
  |                       |                       |
  |---Publish A info----->|                       |
  |                       |<---Subscribe to A-----|
  |                       |---A info------------->|
  |<---Subscribe to B-----|<------Publish B info--|
  |<--B info--------------|                       |
  |                       |                       |
  |<===========P2P Connection Established========>|
  |                       |                       |
```

### Polling Strategy
- Default poll interval: 2 seconds
- Exponential backoff on repeated failures
- Maximum 30 attempts (60 seconds total)

### Advantages
- **Zero server deployment**
- No infrastructure to maintain
- Built-in persistence
- Web-based monitoring (via GitHub UI)
- Free for public and private Gists

### Limitations
- Requires GitHub account and token
- Higher latency (polling-based)
- GitHub API rate limits (5000 req/hour authenticated)
- Not suitable for real-time applications
- Requires HTTPS/TLS support

### Security Considerations
- Keep GitHub token secure
- Use private Gists for sensitive applications
- Rotate tokens periodically
- Consider token scope limitations

---

## Protocol Comparison

| Feature | SIMPLE | ICE-RELAY | PUBSUB |
|---------|--------|-----------|--------|
| Transport | UDP | TCP | HTTPS |
| Latency | Low | Medium | High |
| Server Required | Yes (UDP) | Yes (TCP) | No (GitHub) |
| NAT Traversal | Limited | Good | N/A |
| Reliability | Low | High | Medium |
| Setup Complexity | Simple | Medium | Simple |
| Best For | LAN | Internet | Demos |

## Choosing a Protocol

### Use SIMPLE when:
- Operating in LAN environment
- Low latency is critical
- Simple setup is preferred
- UDP is not blocked

### Use ICE-RELAY when:
- Operating over public internet
- Need NAT traversal
- Reliability is important
- Can deploy TCP server

### Use PUBSUB when:
- No server infrastructure available
- Latency is not critical
- Small number of peers
- Demonstration or testing

---

## Future Extensions

### Planned Features
- [ ] STUN/TURN integration for ICE-RELAY
- [ ] WebSocket signaling option
- [ ] DHT-based peer discovery
- [ ] End-to-end encryption
- [ ] IPv6 support

### Community Contributions Welcome
Feel free to propose new protocols or enhancements!
