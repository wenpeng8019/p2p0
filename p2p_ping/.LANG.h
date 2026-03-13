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
    LA_S10,  /* "--- Connected ---"  [p2p_ping.c] */
    LA_S11,  /* "--- Peer disconnected ---"  [p2p_ping.c] */
    LA_S12,  /* "[EVENT] Connection closed"  [p2p_ping.c] */
    LA_S13,  /* "Auto-echo received messages back to sender"  [p2p_ping.c] */
    LA_S14,  /* "Disable LAN shortcut (force NAT punch test)"  [p2p_ping.c] */
    LA_S15,  /* "Enable DTLS (MbedTLS)"  [p2p_ping.c] */
    LA_S16,  /* "Enable DTLS (OpenSSL)"  [p2p_ping.c] */
    LA_S17,  /* "Enable PseudoTCP"  [p2p_ping.c] */
    LA_S18,  /* "GitHub Gist ID for Public Signaling"  [p2p_ping.c] */
    LA_S19,  /* "GitHub Token for Public Signaling"  [p2p_ping.c] */
    LA_S20,  /* "Log level (0-5)"  [p2p_ping.c] */
    LA_S21,  /* "Signaling server IP[:PORT]"  [p2p_ping.c] */
    LA_S22,  /* "Skip host candidates"  [p2p_ping.c] */
    LA_S23,  /* "Target Peer Name (if specified: active role)"  [p2p_ping.c] */
    LA_S24,  /* "Test PUNCH/PUNCH_ACK state machine over LAN"  [p2p_ping.c] */
    LA_S25,  /* "TURN password"  [p2p_ping.c] */
    LA_S26,  /* "TURN server address"  [p2p_ping.c] */
    LA_S27,  /* "TURN username"  [p2p_ping.c] */
    LA_S28,  /* "Use Chinese language"  [p2p_ping.c] */
    LA_S29,  /* "Use COMPACT mode (UDP signaling, default is ICE/TCP)"  [p2p_ping.c] */
    LA_S30,  /* "Your Peer Name"  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F31,  /* "% === P2P Ping Diagnostic Tool ===\n"  [p2p_ping.c] */
    LA_F32,  /* "% Failed to create sessions\n"  [p2p_ping.c] */
    LA_F33,  /* "% Failed to initialize connection\n"  [p2p_ping.c] */
    LA_F34,  /* "% No signaling mode.\nUse --server or --github\n"  [p2p_ping.c] */
    LA_F35,  /* "Running in %s mode (connecting to %s)..." (%s,%s)  [p2p_ping.c] */
    LA_F36,  /* "Running in %s mode (waiting for connection)..." (%s)  [p2p_ping.c] */
    LA_F37,  /* "% [Chat] Echo mode enabled: received messages will be echoed back.\n"  [p2p_ping.c] */
    LA_F38,  /* "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n"  [p2p_ping.c] */
    LA_F39,  /* "[STATE] %s (%d) -> %s (%d)" (%s,%d,%s,%d)  [p2p_ping.c] */
    LA_F40,  /* "% [TEST] LAN punch mode: PUNCH/PUNCH_ACK over Host candidates (nat_start_punch)\n"  [p2p_ping.c] */
    LA_F41,  /* "% [TEST] LAN shortcut disabled - forcing NAT punch\n"  [p2p_ping.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F31

/* 字符串表 */
extern const char* lang_en[LA_NUM];

/* 语言实例 ID（多实例支持） */
#define LA_RID lang_rid
extern int lang_rid;

#endif /* LANG_H__ */
