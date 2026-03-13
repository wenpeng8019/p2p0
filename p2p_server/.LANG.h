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
    LA_W1,  /* "disabled"  [server.c] */
    LA_W2,  /* "enabled"  [server.c] */

    /* Strings (LA_S) */
    LA_S3,  /* "[TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_S4,  /* "[TCP] User list truncated (too many users)\n"  [server.c] */
    LA_S5,  /* "Goodbye!\n"  [server.c] */
    LA_S6,  /* "NAT probe disabled (bind failed)\n"  [server.c] */
    LA_S7,  /* "net init failed\n"  [server.c] */
    LA_S8,  /* "Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S9,  /* "Shutting down...\n"  [server.c] */

    /* Formats (LA_F) */
    LA_F10,  /* "%s from '%.*s': new instance(old=%u new=%u), resetting session\n" (%s,%u,%u)  [server.c] */
    LA_F11,  /* "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n" (%s,%u,%d)  [server.c] */
    LA_F12,  /* "%s: accepted, releasing slot for '%s' -> '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F13,  /* "%s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F14,  /* "%s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F15,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F16,  /* "%s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F17,  /* "%s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F18,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F19,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F20,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F21,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F22,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F23,  /* "%s: waiting for peer '%.*s' to register\n" (%s)  [server.c] */
    LA_F24,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F25,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F26,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F27,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F28,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F29,  /* "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F30,  /* "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n" (%d,%d)  [server.c] */
    LA_F31,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F32,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F33,  /* "Send %s: status=error (no slot available)\n" (%s)  [server.c] */
    LA_F34,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F35,  /* "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n" (%s,%s)  [server.c] */
    LA_F36,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
    LA_F37,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F38,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F39,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F40,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F41,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F42,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F43,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F44,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F45,  /* "[TCP] E: Invalid magic from peer '%s'\n" (%s)  [server.c] */
    LA_F46,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F47,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F48,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F49,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F50,  /* "[TCP] I: Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F51,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F52,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F53,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F54,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F55,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F56,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F57,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F58,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F59,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F60,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F61,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F62,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F63,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    LA_F64,  /* "[TCP] V: %s sent to '%s'\n" (%s,%s)  [server.c] */
    LA_F65,  /* "[TCP] V: Peer '%s' disconnected\n" (%s)  [server.c] */
    LA_F66,  /* "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n" (%s)  [server.c] */
    LA_F67,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c] */
    LA_F68,  /* "[UDP] %s send to %s failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F69,  /* "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n" (%s,%s,%u,%d)  [server.c] */
    LA_F70,  /* "[UDP] %s send to %s, seq=0, flags=0, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F71,  /* "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F72,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [server.c] */
    LA_F73,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F74,  /* "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n" (%s,%s,%d,%d,%d)  [server.c] */
    LA_F75,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F76,  /* "select failed(%d)\n" (%d)  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F10

#endif /* LANG_H__ */
