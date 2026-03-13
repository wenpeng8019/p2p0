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
    _LA_10,
    _LA_11,
    _LA_12,
    _LA_13,
    _LA_14,
    _LA_15,
    _LA_16,
    _LA_17,
    _LA_18,
    _LA_19,
    _LA_20,
    _LA_21,
    _LA_22,
    _LA_23,

    /* Strings (LA_S) */
    LA_S24,  /* "--- Connected ---"  [p2p_ping.c] */
    LA_S25,  /* "--- Peer disconnected ---"  [p2p_ping.c] */
    LA_S26,  /* "=== P2P Ping Diagnostic Tool ==="  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F27,  /* "% [Chat] Echo mode enabled: received messages will be echoed back.\n"  [p2p_ping.c] */
    LA_F28,  /* "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n"  [p2p_ping.c] */

    /* Strings (LA_S) */
    LA_S29,  /* "[EVENT] Connection closed"  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F30,  /* "% [TEST] LAN punch mode: PUNCH/PUNCH_ACK over Host candidates (nat_start_punch)\n"  [p2p_ping.c] */
    LA_F31,  /* "% [TEST] LAN shortcut disabled - forcing NAT punch\n"  [p2p_ping.c] */
    _LA_32,
    LA_F33,  /* "% Failed to create sessions\n"  [p2p_ping.c] */
    LA_F34,  /* "% Failed to initialize connection\n"  [p2p_ping.c] */
    _LA_35,
    _LA_36,
    LA_F37,  /* "Running in %s mode (connecting to %s)..." (%s,%s)  [p2p_ping.c] */
    LA_F38,  /* "Running in %s mode (waiting for connection)..." (%s)  [p2p_ping.c] */
    _LA_39,
    LA_F40,  /* "[STATE] %s (%d) -> %s (%d)" (%s,%d,%s,%d)  [p2p_ping.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F33

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
