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
    LA_S10,  /* "[TCP] Invalid magic from peer\n"  [server.c] */
    LA_S11,  /* "[TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_S12,  /* "[TCP] User list truncated (too many users)\n"  [server.c] */
    LA_S13,  /* "Error: Too many arguments"  [server.c] */
    LA_S14,  /* "Examples:"  [server.c] */
    LA_S15,  /* "Parameters:"  [server.c] */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "      [%d] type=%d, %s:%d\n" (%d,%d,%s,%d)  [server.c] */
    LA_F1,  /* "  %s                    # Default config (port 8888, no probe, no relay)" (%s)  [server.c] */
    LA_F2,  /* "  %s 9000               # Listen on port 9000" (%s)  [server.c] */
    LA_F3,  /* "  %s 9000 9001          # Listen 9000, probe port 9001" (%s)  [server.c] */
    LA_F4,  /* "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay" (%s)  [server.c] */
    LA_F5,  /* "Error: Invalid port number '%s' (range: 1-65535)" (%s)  [server.c] */
    LA_F6,  /* "Error: Invalid probe port '%s' (range: 0-65535)" (%s)  [server.c] */
    LA_F7,  /* "Error: Unknown option '%s' (expected: 'relay')" (%s)  [server.c] */
    LA_F8,  /* "P2P Signaling Server listening on port %d (TCP + UDP)..." (%d)  [server.c] */
    LA_F9,  /* "Usage: %s [port] [probe_port] [relay]" (%s)  [server.c] */
    LA_F10,  /* "[DEBUG] Received %d bytes: magic=0x%08X, type=%d, length=%d (expected magic=0x%08X)\n" (%d,%d,%d)  [server.c] */
    LA_F11,  /* "[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n" (%s,%d,%u,%s,%d)  [server.c] */
    LA_F12,  /* "[SERVER] NAT probe socket listening on port %d" (%d)  [server.c] */
    LA_F13,  /* "[SERVER] NAT probe: %s (port %d)" (%s,%d)  [server.c] */
    LA_F14,  /* "[SERVER] Relay support: %s" (%s)  [server.c] */
    LA_F15,  /* "[SERVER] Starting P2P signal server on port %d" (%d)  [server.c] */
    LA_F16,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F17,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F18,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F19,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F20,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F21,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F22,  /* "[TCP] Client '%s' timed out (no activity for %ld seconds)\n" (%s,%l)  [server.c] */
    LA_F23,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F24,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F25,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F26,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F27,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F28,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F29,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F30,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F31,  /* "[TCP] Peer %s disconnected\n" (%s)  [server.c] */
    LA_F32,  /* "[TCP] Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F33,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F34,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F35,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F36,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F37,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F38,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F39,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F40,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F41,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    LA_F42,  /* "[UDP] Assigned session_id=%llu for %s -> %s\n" (%l,%s,%s)  [server.c] */
    LA_F43,  /* "[UDP] Cannot relay PEER_INFO_ACK: sid=%llu (peer unavailable)\n" (%l)  [server.c] */
    LA_F44,  /* "[UDP] Invalid PEER_INFO_ACK from %s (size %zu)\n" (%s)  [server.c] */
    LA_F45,  /* "[UDP] Invalid REGISTER from %s (payload too short)\n" (%s)  [server.c] */
    LA_F46,  /* "[UDP] Invalid UNREGISTER from %s (payload too short)\n" (%s)  [server.c] */
    LA_F47,  /* "[UDP] PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F48,  /* "[UDP] PEER_INFO seq=0 from client %s (server-only, dropped)\n" (%s)  [server.c] */
    LA_F49,  /* "[UDP] PEER_INFO(seq=0) bilateral: %s(%d cands) <-> %s(%d cands)\n" (%s,%d,%s,%d)  [server.c] */
    LA_F50,  /* "[UDP] PEER_INFO_ACK for unknown sid=%llu from %s\n" (%l,%s)  [server.c] */
    LA_F51,  /* "[UDP] PEER_INFO_ACK(seq=0) confirmed: sid=%llu (%s <-> %s, %d retransmits)\n" (%l,%s,%s,%d)  [server.c] */
    LA_F52,  /* "[UDP] PEER_OFF sent to %s (sid=%llu)%s\n" (%s,%l,%s)  [server.c] */
    LA_F53,  /* "[UDP] Peer pair (%s → %s) timed out\n" (%s,%s)  [server.c] */
    LA_F54,  /* "[UDP] REGISTER from %s: local='%s', remote='%s', candidates=%d\n" (%s,%s,%s,%d)  [server.c] */
    LA_F55,  /* "[UDP] REGISTER_ACK to %s: error (no slot available)\n" (%s)  [server.c] */
    LA_F56,  /* "[UDP] REGISTER_ACK to %s: ok, peer_online=%d, max_cands=%d, relay=%s, public=%s:%d, probe_port=%d\n" (%s,%d,%d,%s,%s,%d,%d)  [server.c] */
    LA_F57,  /* "[UDP] Relay 0x%02x for sid=%llu: peer unavailable (dropped)\n" (%l)  [server.c] */
    LA_F58,  /* "[UDP] Relay 0x%02x for unknown sid=%llu from %s (dropped)\n" (%l,%s)  [server.c] */
    LA_F59,  /* "[UDP] Relay ACK: sid=%llu (%s -> %s)\n" (%l,%s,%s)  [server.c] */
    LA_F60,  /* "[UDP] Relay DATA seq=%u: sid=%llu (%s -> %s)\n" (%u,%l,%s,%s)  [server.c] */
    LA_F61,  /* "[UDP] Relay PEER_INFO seq=%u: sid=%llu (%s -> %s)\n" (%u,%l,%s,%s)  [server.c] */
    LA_F62,  /* "[UDP] Relay PEER_INFO_ACK seq=%u: sid=%llu (%s -> %s)\n" (%u,%l,%s,%s)  [server.c] */
    LA_F63,  /* "[UDP] Relay packet too short: type=0x%02x from %s (size %zu)\n" (%s)  [server.c] */
    LA_F64,  /* "[UDP] Retransmit PEER_INFO (sid=%llu): %s <-> %s (attempt %d/%d)\n" (%l,%s,%s,%d,%d)  [server.c] */
    LA_F65,  /* "[UDP] Sent PEER_INFO(seq=0) to %s:%d (peer='%s') with %d cands%s\n" (%s,%d,%s,%d,%s)  [server.c] */
    LA_F66,  /* "[UDP] Target pair (%s → %s) not found (waiting for peer registration)\n" (%s,%s)  [server.c] */
    LA_F67,  /* "[UDP] UNREGISTER: releasing slot for '%s' -> '%s'\n" (%s,%s)  [server.c] */
    LA_F68,  /* "[UDP] Unknown signaling packet type %d from %s\n" (%d,%s)  [server.c] */

    LA_NUM = 87
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
