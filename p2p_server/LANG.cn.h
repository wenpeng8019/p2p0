/*
 * Auto-generated language strings
 */

#include ".LANG.h"

/* Embedded cn language table */
static const char* lang_cn[LA_NUM] = {
    [LA_W0] = "disabled",  /* SID:1 new */
    [LA_W1] = "enabled",  /* SID:2 new */
    [LA_S0] = "               - TCP: RELAY mode signaling (stateful/long connection)",  /* SID:3 new */
    [LA_S1] = "               - UDP: COMPACT mode signaling (stateless)",  /* SID:4 new */
    [LA_S2] = "               Used to detect symmetric NAT (port consistency)",  /* SID:5 new */
    [LA_S3] = "  port         Signaling server listen port (default: 8888)",  /* SID:6 new */
    [LA_S4] = "  probe_port   NAT type detection port (default: 0=disabled)",  /* SID:7 new */
    [LA_S5] = "  relay        Enable data relay support (COMPACT mode fallback)",  /* SID:8 new */
    [LA_S6] = "[SERVER] Goodbye!",  /* SID:9 new */
    [LA_S7] = "[SERVER] NAT probe disabled (bind failed)",  /* SID:10 new */
    [LA_S8] = "[SERVER] Received shutdown signal, exiting gracefully...",  /* SID:11 new */
    [LA_S9] = "[SERVER] Shutting down...",  /* SID:12 new */
    [LA_S10] = "[TCP] Invalid magic from peer\n",  /* SID:13 new */
    [LA_S11] = "[TCP] Max peers reached, rejecting connection\n",  /* SID:14 new */
    [LA_S12] = "[TCP] User list truncated (too many users)\n",  /* SID:15 new */
    [LA_S13] = "Error: Too many arguments",  /* SID:16 new */
    [LA_S14] = "Examples:",  /* SID:17 new */
    [LA_S15] = "Parameters:",  /* SID:18 new */
    [LA_F0] = "      [%d] type=%d, %s:%d\n",  /* SID:19 new */
    [LA_F1] = "  %s                    # Default config (port 8888, no probe, no relay)",  /* SID:20 new */
    [LA_F2] = "  %s 9000               # Listen on port 9000",  /* SID:21 new */
    [LA_F3] = "  %s 9000 9001          # Listen 9000, probe port 9001",  /* SID:22 new */
    [LA_F4] = "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay",  /* SID:23 new */
    [LA_F5] = "Error: Invalid port number '%s' (range: 1-65535)",  /* SID:24 new */
    [LA_F6] = "Error: Invalid probe port '%s' (range: 0-65535)",  /* SID:25 new */
    [LA_F7] = "Error: Unknown option '%s' (expected: 'relay')",  /* SID:26 new */
    [LA_F8] = "P2P Signaling Server listening on port %d (TCP + UDP)...",  /* SID:27 new */
    [LA_F9] = "Usage: %s [port] [probe_port] [relay]",  /* SID:28 new */
    [LA_F10] = "[DEBUG] Received %d bytes: magic=0x%08X, type=%d, length=%d (expected magic=0x%08X)\n",  /* SID:29 new */
    [LA_F11] = "[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n",  /* SID:30 new */
    [LA_F12] = "[SERVER] NAT probe socket listening on port %d",  /* SID:31 new */
    [LA_F13] = "[SERVER] NAT probe: %s (port %d)",  /* SID:32 new */
    [LA_F14] = "[SERVER] Relay support: %s",  /* SID:33 new */
    [LA_F15] = "[SERVER] Starting P2P signal server on port %d",  /* SID:34 new */
    [LA_F16] = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",  /* SID:35 new */
    [LA_F17] = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",  /* SID:36 new */
    [LA_F18] = "[TCP] All pending candidates flushed to '%s'\n",  /* SID:37 new */
    [LA_F19] = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",  /* SID:38 new */
    [LA_F20] = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",  /* SID:39 new */
    [LA_F21] = "[TCP] Cannot allocate slot for offline user '%s'\n",  /* SID:40 new */
    [LA_F22] = "[TCP] Client '%s' timed out (no activity for %ld seconds)\n",  /* SID:41 new */
    [LA_F23] = "[TCP] Failed to receive payload from %s\n",  /* SID:42 new */
    [LA_F24] = "[TCP] Failed to receive target name from %s\n",  /* SID:43 new */
    [LA_F25] = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",  /* SID:44 new */
    [LA_F26] = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",  /* SID:45 new */
    [LA_F27] = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",  /* SID:46 new */
    [LA_F28] = "[TCP] New connection from %s:%d\n",  /* SID:47 new */
    [LA_F29] = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",  /* SID:48 new */
    [LA_F30] = "[TCP] Payload too large (%u bytes) from %s\n",  /* SID:49 new */
    [LA_F31] = "[TCP] Peer %s disconnected\n",  /* SID:50 new */
    [LA_F32] = "[TCP] Peer '%s' logged in\n",  /* SID:51 new */
    [LA_F33] = "[TCP] Relaying %s from %s to %s (%u bytes)\n",  /* SID:52 new */
    [LA_F34] = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",  /* SID:53 new */
    [LA_F35] = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",  /* SID:54 new */
    [LA_F36] = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",  /* SID:55 new */
    [LA_F37] = "[TCP] Storage full indication flushed to '%s'\n",  /* SID:56 new */
    [LA_F38] = "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",  /* SID:57 new */
    [LA_F39] = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",  /* SID:58 new */
    [LA_F40] = "[TCP] Target %s offline, caching candidates...\n",  /* SID:59 new */
    [LA_F41] = "[TCP] Unknown message type %d from %s\n",  /* SID:60 new */
    [LA_F42] = "[UDP] Assigned session_id=%llu for %s -> %s\n",  /* SID:88 new */
    [LA_F43] = "[UDP] Cannot relay PEER_INFO_ACK: sid=%llu (peer unavailable)\n",  /* SID:89 new */
    [LA_F44] = "[UDP] Invalid PEER_INFO_ACK from %s (size %zu)\n",  /* SID:63 new */
    [LA_F45] = "[UDP] Invalid REGISTER from %s (payload too short)\n",  /* SID:64 new */
    [LA_F46] = "[UDP] Invalid UNREGISTER from %s (payload too short)\n",  /* SID:65 new */
    [LA_F47] = "[UDP] PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:66 new */
    [LA_F48] = "[UDP] PEER_INFO seq=0 from client %s (server-only, dropped)\n",  /* SID:67 new */
    [LA_F49] = "[UDP] PEER_INFO(seq=0) bilateral: %s(%d cands) <-> %s(%d cands)\n",  /* SID:68 new */
    [LA_F50] = "[UDP] PEER_INFO_ACK for unknown sid=%llu from %s\n",  /* SID:90 new */
    [LA_F51] = "[UDP] PEER_INFO_ACK(seq=0) confirmed: sid=%llu (%s <-> %s, %d retransmits)\n",  /* SID:91 new */
    [LA_F52] = "[UDP] PEER_OFF sent to %s (sid=%llu)%s\n",  /* SID:92 new */
    [LA_F53] = "[UDP] Peer pair (%s → %s) timed out\n",  /* SID:72 new */
    [LA_F54] = "[UDP] REGISTER from %s: local='%s', remote='%s', candidates=%d\n",  /* SID:73 new */
    [LA_F55] = "[UDP] REGISTER_ACK to %s: error (no slot available)\n",  /* SID:74 new */
    [LA_F56] = "[UDP] REGISTER_ACK to %s: ok, peer_online=%d, max_cands=%d, relay=%s, public=%s:%d, probe_port=%d\n",  /* SID:75 new */
    [LA_F57] = "[UDP] Relay 0x%02x for sid=%llu: peer unavailable (dropped)\n",  /* SID:93 new */
    [LA_F58] = "[UDP] Relay 0x%02x for unknown sid=%llu from %s (dropped)\n",  /* SID:94 new */
    [LA_F59] = "[UDP] Relay ACK: sid=%llu (%s -> %s)\n",  /* SID:95 new */
    [LA_F60] = "[UDP] Relay DATA seq=%u: sid=%llu (%s -> %s)\n",  /* SID:96 new */
    [LA_F61] = "[UDP] Relay PEER_INFO seq=%u: sid=%llu (%s -> %s)\n",  /* SID:97 new */
    [LA_F62] = "[UDP] Relay PEER_INFO_ACK seq=%u: sid=%llu (%s -> %s)\n",  /* SID:98 new */
    [LA_F63] = "[UDP] Relay packet too short: type=0x%02x from %s (size %zu)\n",  /* SID:82 new */
    [LA_F64] = "[UDP] Retransmit PEER_INFO (sid=%llu): %s <-> %s (attempt %d/%d)\n",  /* SID:99 new */
    [LA_F65] = "[UDP] Sent PEER_INFO(seq=0) to %s:%d (peer='%s') with %d cands%s\n",  /* SID:84 new */
    [LA_F66] = "[UDP] Target pair (%s → %s) not found (waiting for peer registration)\n",  /* SID:85 new */
    [LA_F67] = "[UDP] UNREGISTER: releasing slot for '%s' -> '%s'\n",  /* SID:86 new */
    [LA_F68] = "[UDP] Unknown signaling packet type %d from %s\n",  /* SID:87 new */
};
