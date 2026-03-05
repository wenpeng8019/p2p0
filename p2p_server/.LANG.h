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
    LA_W0,  /* "disabled"  [server.c] */
    LA_W1,  /* "enabled"  [server.c] */

    /* Strings (LA_S) */
    LA_S0,  /* "               - TCP: RELAY mode signaling (stateful/long connection)"  [server.c] */
    LA_S1,  /* "               - UDP: COMPACT mode signaling (stateless)"  [server.c] */
    LA_S2,  /* "               Used to detect symmetric NAT (port consistency)"  [server.c] */
    LA_S3,  /* "  port         Signaling server listen port (default: 8888)"  [server.c] */
    LA_S4,  /* "  probe_port   NAT type detection port (default: 0=disabled)"  [server.c] */
    LA_S5,  /* "  relay        Enable data relay support (COMPACT mode fallback)"  [server.c] */
    LA_S6,  /* "[SERVER] Goodbye!"  [server.c] */
    LA_S7,  /* "[SERVER] NAT probe disabled (bind failed)"  [server.c] */
    LA_S8,  /* "[SERVER] Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S9,  /* "[SERVER] Shutting down..."  [server.c] */
    LA_S10,  /* "[TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_S11,  /* "[TCP] User list truncated (too many users)\n"  [server.c] */
    LA_S12,  /* "Error: Too many arguments"  [server.c] */
    LA_S13,  /* "Examples:"  [server.c] */
    LA_S14,  /* "Parameters:"  [server.c] */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "  %s                    # Default config (port 8888, no probe, no relay)" (%s)  [server.c] */
    LA_F1,  /* "  %s 9000               # Listen on port 9000" (%s)  [server.c] */
    LA_F2,  /* "  %s 9000 9001          # Listen 9000, probe port 9001" (%s)  [server.c] */
    LA_F3,  /* "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay" (%s)  [server.c] */
    LA_F4,  /* "Error: Invalid port number '%s' (range: 1-65535)" (%s)  [server.c] */
    LA_F5,  /* "Error: Invalid probe port '%s' (range: 0-65535)" (%s)  [server.c] */
    LA_F6,  /* "Error: Unknown option '%s' (expected: 'relay')" (%s)  [server.c] */
    LA_F7,  /* "P2P Signaling Server listening on port %d (TCP + UDP)..." (%d)  [server.c] */
    LA_F8,  /* "Usage: %s [port] [probe_port] [relay]" (%s)  [server.c] */
    LA_F9,  /* "[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n" (%s,%d,%u,%s,%d)  [server.c] */
    LA_F10,  /* "[SERVER] NAT probe socket listening on port %d" (%d)  [server.c] */
    LA_F11,  /* "[SERVER] NAT probe: %s (port %d)" (%s,%d)  [server.c] */
    LA_F12,  /* "[SERVER] Relay support: %s" (%s)  [server.c] */
    LA_F13,  /* "[SERVER] Starting P2P signal server on port %d" (%d)  [server.c] */
    LA_F14,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F15,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F16,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F17,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F18,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F19,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F20,  /* "[TCP] E: Invalid magic from peer '%s'\n" (%s)  [server.c] */
    LA_F21,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F22,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F23,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F24,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F25,  /* "[TCP] I: Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F26,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F27,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F28,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F29,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F30,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F31,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F32,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F33,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F34,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F35,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F36,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F37,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F38,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    LA_F39,  /* "[TCP] V: %s sent to '%s'\n" (%s,%s)  [server.c] */
    LA_F40,  /* "[TCP] V: Peer '%s' disconnected\n" (%s)  [server.c] */
    LA_F41,  /* "[TCP] W: Client '%s' timeout (inactive for %ld seconds)\n" (%s,%l)  [server.c] */
    LA_F42,  /* "[UDP] E: %s sent, status=error (no slot available)\n" (%s)  [server.c] */
    LA_F43,  /* "[UDP] E: %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F44,  /* "[UDP] E: %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F45,  /* "[UDP] E: %s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F46,  /* "[UDP] E: %s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F47,  /* "[UDP] E: %s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F48,  /* "[UDP] E: %s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F49,  /* "[UDP] E: Relay %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F50,  /* "[UDP] I: %s forwarded: '%s' -> '%s', sid=%u (ses_id=%llu)\n" (%s,%s,%s,%u,%l)  [server.c] */
    LA_F51,  /* "[UDP] I: %s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%llu)\n" (%s,%s,%s,%u,%u,%l)  [server.c] */
    LA_F52,  /* "[UDP] I: %s from '%s': new instance(old=%u new=%u), resetting session\n" (%s,%s,%u,%u)  [server.c] */
    LA_F53,  /* "[UDP] I: Address changed for '%s', notifying '%s' (ses_id=%llu)\n" (%s,%s,%l)  [server.c] */
    LA_F54,  /* "[UDP] I: Assigned session_id=%llu for '%s' -> '%s'\n" (%l,%s,%s)  [server.c] */
    LA_F55,  /* "[UDP] I: Pairing complete: '%s'(%d cands) <-> '%s'(%d cands)\n" (%s,%d,%s,%d)  [server.c] */
    LA_F56,  /* "[UDP] V: %s accepted from %s, session_id=%llu, sid=%u, msg=%u, len=%d\n" (%s,%s,%l,%u,%u,%d)  [server.c] */
    LA_F57,  /* "[UDP] V: %s accepted from %s, sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F58,  /* "[UDP] V: %s accepted from %s, target='%s', sid=%u, msg=%u, len=%d\n" (%s,%s,%s,%u,%u,%d)  [server.c] */
    LA_F59,  /* "[UDP] V: %s received and %s sent for '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F60,  /* "[UDP] V: %s sent to '%s' (ses_id=%llu) [reregister]\n" (%s,%s,%l)  [server.c] */
    LA_F61,  /* "[UDP] V: %s sent to '%s' (ses_id=%llu) [timeout]\n" (%s,%s,%l)  [server.c] */
    LA_F62,  /* "[UDP] V: %s sent to '%s' (ses_id=%llu) [unregister]\n" (%s,%s,%l)  [server.c] */
    LA_F63,  /* "[UDP] V: %s sent, status=%s, max_cands=%d, relay=%s, public=%s:%d, probe=%d\n" (%s,%s,%d,%s,%s,%d,%d)  [server.c] */
    LA_F64,  /* "[UDP] V: %s: RPC complete for '%s', sid=%u (ses_id=%llu)\n" (%s,%s,%u,%l)  [server.c] */
    LA_F65,  /* "[UDP] V: %s: accepted, local='%s', remote='%s', inst_id=%u, cands=%d\n" (%s,%s,%s,%u,%d)  [server.c] */
    LA_F66,  /* "[UDP] V: %s: accepted, releasing slot for '%s' -> '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F67,  /* "[UDP] V: %s: confirmed '%s', retries=%d (ses_id=%llu)\n" (%s,%s,%d,%l)  [server.c] */
    LA_F68,  /* "[UDP] V: %s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F69,  /* "[UDP] V: %s: waiting for peer '%s' to register\n" (%s,%s)  [server.c] */
    LA_F70,  /* "[UDP] V: PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%llu)\n" (%s,%s,%d,%d,%l)  [server.c] */
    LA_F71,  /* "[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n" (%s,%u,%s,%s,%l)  [server.c] */
    LA_F72,  /* "[UDP] V: Relay %s: '%s' -> '%s' (ses_id=%llu)\n" (%s,%s,%s,%l)  [server.c] */
    LA_F73,  /* "[UDP] W: %s for unknown ses_id=%llu\n" (%s,%l)  [server.c] */
    LA_F74,  /* "[UDP] W: %s: already has pending msg, rejecting sid=%u\n" (%s,%u)  [server.c] */
    LA_F75,  /* "[UDP] W: %s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F76,  /* "[UDP] W: %s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F77,  /* "[UDP] W: %s: requester not found for %s\n" (%s,%s)  [server.c] */
    LA_F78,  /* "[UDP] W: %s: target mismatch (expected='%s', got='%s')\n" (%s,%s,%s)  [server.c] */
    LA_F79,  /* "[UDP] W: %s: unknown session_id=%llu\n" (%s,%l)  [server.c] */
    LA_F80,  /* "[UDP] W: Cannot relay %s: ses_id=%llu (peer unavailable)\n" (%s,%l)  [server.c] */
    LA_F81,  /* "[UDP] W: PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F82,  /* "[UDP] W: Relay %s for ses_id=%llu: peer unavailable (dropped)\n" (%s,%l)  [server.c] */
    LA_F83,  /* "[UDP] W: Relay %s for unknown ses_id=%llu (dropped)\n" (%s,%l)  [server.c] */
    LA_F84,  /* "[UDP] W: Timeout for pair '%s' -> '%s' (inactive for %ld seconds)\n" (%s,%s,%l)  [server.c] */
    LA_F85,  /* "[UDP] W: Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */

    LA_NUM = 103
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
