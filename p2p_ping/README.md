# p2p_ping

A diagnostic utility for the P2P library. It supports multiple signaling modes and role-based communication.

## Usage

### Standard Signaling Server
```bash
# Terminal A (Subscriber)
./p2p_ping --name alice --server 127.0.0.1

# Terminal B (Publisher)
./p2p_ping --name bob --server 127.0.0.1 --to alice
```

### Public Channel (GitHub Gist)
```bash
# Terminal A (Subscriber)
./p2p_ping --name alice --github <TOKEN> --gist <GIST_ID>

# Terminal B (Publisher)
./p2p_ping --name bob --github <TOKEN> --gist <GIST_ID> --to alice
```

### Local Loopback (Testing)
```bash
# Terminal A
./p2p_ping --loopback 9002

# Terminal B (connects to A)
./p2p_ping --loopback 9001
```

## Features
- Detailed state transition logging: `[STATE] IDLE (0) -> CONNECTED (3)`
- Support for DTLS and PseudoTCP.
- PUB/SUB role differentiation for public channels.
