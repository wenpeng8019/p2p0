/*
 * Auto-generated language IDs
 *
 * DO NOT EDIT - Regenerate with: ./i18n/i18n.sh
 */

#ifndef LANG_H__
#define LANG_H__

#ifndef LA_PREDEFINED
#   define LA_PREDEFINED -1
#endif

enum {
    LA_PRED = LA_PREDEFINED,  /* 基础 ID，后续 ID 从此开始递增 */

    /* Words (LA_W) */
    LA_W1,  /* "CLOSED"  [p2p_ping.c] */
    LA_W2,  /* "CLOSING"  [p2p_ping.c] */
    LA_W3,  /* "CONNECTED"  [p2p_ping.c] */
    LA_W4,  /* "ERROR"  [p2p_ping.c] */
    LA_W5,  /* "INIT"  [p2p_ping.c] */
    LA_W6,  /* "PUNCHING"  [p2p_ping.c] */
    LA_W7,  /* "REGISTERING"  [p2p_ping.c] */
    LA_W8,  /* "RELAY"  [p2p_ping.c] */
    LA_W9,  /* "UNKNOWN"  [p2p_ping.c] */

    /* Strings (LA_S) */
    LA_S10,  /* "  --cn              Use Chinese language"  [p2p_ping.c] */
    LA_S11,  /* "  --compact         Use COMPACT mode (UDP signaling, default is ICE/TCP)"  [p2p_ping.c] */
    LA_S12,  /* "  --disable-lan     Disable LAN shortcut (force NAT punch test)"  [p2p_ping.c] */
    LA_S13,  /* "  --dtls            Enable DTLS (MbedTLS)"  [p2p_ping.c] */
    LA_S14,  /* "  --echo             Auto-echo received messages back to sender"  [p2p_ping.c] */
    LA_S15,  /* "  --gist ID         GitHub Gist ID for Public Signaling"  [p2p_ping.c] */
    LA_S16,  /* "  --github TOKEN    GitHub Token for Public Signaling"  [p2p_ping.c] */
    LA_S17,  /* "  --lan-punch       Test PUNCH/PUNCH_ACK state machine over LAN (skips STUN/TURN, uses nat_start_punch)"  [p2p_ping.c] */
    LA_S18,  /* "  --name NAME       Your Peer Name"  [p2p_ping.c] */
    LA_S19,  /* "  --openssl         Enable DTLS (OpenSSL)"  [p2p_ping.c] */
    LA_S20,  /* "  --pseudo          Enable PseudoTCP"  [p2p_ping.c] */
    LA_S21,  /* "  --server IP       Standard Signaling Server IP"  [p2p_ping.c] */
    LA_S22,  /* "  --to TARGET       Target Peer Name (if specified: active role; if omitted: passive role)"  [p2p_ping.c] */
    LA_S23,  /* "  --verbose-punch   Enable verbose NAT punch logging"  [p2p_ping.c] */
    LA_S24,  /* "--- Connected ---"  [p2p_ping.c] */
    LA_S25,  /* "--- Peer disconnected ---"  [p2p_ping.c] */
    LA_S26,  /* "=== P2P Ping Diagnostic Tool ==="  [p2p_ping.c] */
    LA_S27,  /* "[Chat] Echo mode enabled: received messages will be echoed back."  [p2p_ping.c] */
    LA_S28,  /* "[Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit."  [p2p_ping.c] */
    LA_S29,  /* "[EVENT] Connection closed"  [p2p_ping.c] */
    LA_S30,  /* "[TEST] LAN punch mode: PUNCH/PUNCH_ACK over Host candidates (nat_start_punch)"  [p2p_ping.c] */
    LA_S31,  /* "[TEST] LAN shortcut disabled - forcing NAT punch"  [p2p_ping.c] */
    LA_S32,  /* "Error: No connection mode specified."  [p2p_ping.c] */
    LA_S33,  /* "Failed to create session"  [p2p_ping.c] */
    LA_S34,  /* "Failed to initialize connection"  [p2p_ping.c] */
    LA_S35,  /* "Options:"  [p2p_ping.c] */
    LA_S36,  /* "Use one of: --server or --github"  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F37,  /* "Running in %s mode (connecting to %s)..." (%s,%s)  [p2p_ping.c] */
    LA_F38,  /* "Running in %s mode (waiting for connection)..." (%s)  [p2p_ping.c] */
    LA_F39,  /* "Usage: %s [options]" (%s)  [p2p_ping.c] */
    LA_F40,  /* "[STATE] %s (%d) -> %s (%d)" (%s,%d,%s,%d)  [p2p_ping.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F37

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
