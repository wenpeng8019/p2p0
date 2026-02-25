/*
 * Auto-generated language IDs
 * Generated: 2026-02-25 18:55:53
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
    LA_W0,  /* "CLOSED" */
    LA_W1,  /* "CLOSING" */
    LA_W2,  /* "CONNECTED" */
    LA_W3,  /* "ERROR" */
    LA_W4,  /* "INIT" */
    LA_W5,  /* "PUNCHING" */
    LA_W6,  /* "REGISTERING" */
    LA_W7,  /* "RELAY" */
    LA_W8,  /* "UNKNOWN" */

    /* Strings (LA_S) */
    LA_S0,  /* "  --cn              Use Chinese language" */
    LA_S1,  /* "  --compact         Use COMPACT mode (UDP signaling, default is ICE/TCP)" */
    LA_S2,  /* "  --disable-lan     Disable LAN shortcut (force NAT punch test)" */
    LA_S3,  /* "  --dtls            Enable DTLS (MbedTLS)" */
    LA_S4,  /* "  --echo             Auto-echo received messages back to sender" */
    LA_S5,  /* "  --gist ID         GitHub Gist ID for Public Signaling" */
    LA_S6,  /* "  --github TOKEN    GitHub Token for Public Signaling" */
    LA_S7,  /* "  --lan-punch       Test PUNCH/PUNCH_ACK state machine over LAN (skips STUN/TURN, uses nat_start_punch)" */
    LA_S8,  /* "  --name NAME       Your Peer Name" */
    LA_S9,  /* "  --openssl         Enable DTLS (OpenSSL)" */
    LA_S10,  /* "  --pseudo          Enable PseudoTCP" */
    LA_S11,  /* "  --server IP       Standard Signaling Server IP" */
    LA_S12,  /* "  --to TARGET       Target Peer Name (if specified: active role; if omitted: passive role)" */
    LA_S13,  /* "  --verbose-punch   Enable verbose NAT punch logging" */
    LA_S14,  /* "--- Connected ---" */
    LA_S15,  /* "--- Peer disconnected ---" */
    LA_S16,  /* "[Chat] Echo mode enabled: received messages will be echoed back." */
    LA_S17,  /* "[Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit." */
    LA_S18,  /* "[EVENT] Connection closed" */
    LA_S19,  /* "[TEST] LAN punch mode: PUNCH/PUNCH_ACK over Host candidates (nat_start_punch)" */
    LA_S20,  /* "[TEST] LAN shortcut disabled - forcing NAT punch" */
    LA_S21,  /* "=== P2P Ping Diagnostic Tool ===" */
    LA_S22,  /* "Error: No connection mode specified." */
    LA_S23,  /* "Failed to create session" */
    LA_S24,  /* "Failed to initialize connection" */
    LA_S25,  /* "Options:" */
    LA_S26,  /* "Use one of: --server or --github" */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "[STATE] %s (%d) -> %s (%d)" (%s,%d,%s,%d) */
    LA_F1,  /* "Running in %s mode (connecting to %s)..." (%s,%s) */
    LA_F2,  /* "Running in %s mode (waiting for connection)..." (%s) */
    LA_F3,  /* "Usage: %s [options]" (%s) */

    LA_NUM = 40
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
