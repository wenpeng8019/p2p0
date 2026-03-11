/*
 * Auto-generated language strings
 */

#include ".LANG.h"

/* 字符串表 */
const char* lang_en[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 */
    [LA_W2] = "enabled",  /* SID:2 */
    [LA_S3] = "               - TCP: RELAY mode signaling (stateful/long connection)",  /* SID:3 */
    [LA_S4] = "               - UDP: COMPACT mode signaling (stateless)",  /* SID:4 */
    [LA_S5] = "               Used to detect symmetric NAT (port consistency)",  /* SID:5 */
    [LA_S6] = "  port         Signaling server listen port (default: 8888)",  /* SID:6 */
    [LA_S7] = "  probe_port   NAT type detection port (default: 0=disabled)",  /* SID:7 */
    [LA_S8] = "  relay        Enable data relay support (COMPACT mode fallback)",  /* SID:8 */
    [LA_S9] = "[SERVER] Goodbye!",  /* SID:9 */
    [LA_S10] = "[SERVER] NAT probe disabled (bind failed)",  /* SID:10 */
    [LA_S11] = "[SERVER] Received shutdown signal, exiting gracefully...",  /* SID:11 */
    [LA_S12] = "[SERVER] Shutting down...",  /* SID:12 */
    [LA_S14] = "[TCP] Max peers reached, rejecting connection\n",  /* SID:14 */
    [LA_S15] = "[TCP] User list truncated (too many users)\n",  /* SID:15 */
    [LA_S16] = "Error: Too many arguments",  /* SID:16 */
    [LA_S17] = "Examples:",  /* SID:17 */
    [LA_S18] = "Parameters:",  /* SID:18 */
    [LA_F20] = "  %s                    # Default config (port 8888, no probe, no relay)",  /* SID:20 */
    [LA_F21] = "  %s 9000               # Listen on port 9000",  /* SID:21 */
    [LA_F22] = "  %s 9000 9001          # Listen 9000, probe port 9001",  /* SID:22 */
    [LA_F23] = "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay",  /* SID:23 */
    [LA_F24] = "Error: Invalid port number '%s' (range: 1-65535)",  /* SID:24 */
    [LA_F25] = "Error: Invalid probe port '%s' (range: 0-65535)",  /* SID:25 */
    [LA_F26] = "Error: Unknown option '%s' (expected: 'relay')",  /* SID:26 */
    [LA_F27] = "P2P Signaling Server listening on port %d (TCP + UDP)...",  /* SID:27 */
    [LA_F28] = "Usage: %s [port] [probe_port] [relay]",  /* SID:28 */
    [LA_F30] = "[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n",  /* SID:30 */
    [LA_F31] = "[SERVER] NAT probe socket listening on port %d",  /* SID:31 */
    [LA_F32] = "[SERVER] NAT probe: %s (port %d)",  /* SID:32 */
    [LA_F33] = "[SERVER] Relay support: %s",  /* SID:33 */
    [LA_F34] = "[SERVER] Starting P2P signal server on port %d",  /* SID:34 */
    [LA_F35] = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",  /* SID:35 */
    [LA_F36] = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",  /* SID:36 */
    [LA_F37] = "[TCP] All pending candidates flushed to '%s'\n",  /* SID:37 */
    [LA_F38] = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",  /* SID:38 */
    [LA_F39] = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",  /* SID:39 */
    [LA_F40] = "[TCP] Cannot allocate slot for offline user '%s'\n",  /* SID:40 */
    [LA_F13] = "[TCP] E: Invalid magic from peer '%s'\n",  /* SID:13 */
    [LA_F42] = "[TCP] Failed to receive payload from %s\n",  /* SID:42 */
    [LA_F43] = "[TCP] Failed to receive target name from %s\n",  /* SID:43 */
    [LA_F44] = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",  /* SID:44 */
    [LA_F45] = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",  /* SID:45 */
    [LA_F51] = "[TCP] I: Peer '%s' logged in\n",  /* SID:51 */
    [LA_F46] = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",  /* SID:46 */
    [LA_F47] = "[TCP] New connection from %s:%d\n",  /* SID:47 */
    [LA_F48] = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",  /* SID:48 */
    [LA_F49] = "[TCP] Payload too large (%u bytes) from %s\n",  /* SID:49 */
    [LA_F52] = "[TCP] Relaying %s from %s to %s (%u bytes)\n",  /* SID:52 */
    [LA_F53] = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",  /* SID:53 */
    [LA_F54] = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",  /* SID:54 */
    [LA_F55] = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",  /* SID:55 */
    [LA_F56] = "[TCP] Storage full indication flushed to '%s'\n",  /* SID:56 */
    [LA_F57] = "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",  /* SID:57 */
    [LA_F58] = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",  /* SID:58 */
    [LA_F59] = "[TCP] Target %s offline, caching candidates...\n",  /* SID:59 */
    [LA_F60] = "[TCP] Unknown message type %d from %s\n",  /* SID:60 */
    [LA_F280] = "[TCP] V: %s sent to '%s'\n",  /* SID:280 */
    [LA_F50] = "[TCP] V: Peer '%s' disconnected\n",  /* SID:50 */
    [LA_F41] = "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n",  /* SID:41 */
    [LA_F67] = "[UDP] E: %s seq=0 from client %s (server-only, dropped)\n",  /* SID:67 */
    [LA_F64] = "[UDP] E: %s: bad payload(len=%zu)\n",  /* SID:64 */
    [LA_F281] = "[UDP] E: %s: data too large (len=%d)\n",  /* SID:281 */
    [LA_F64] = "[UDP] E: %s: invalid instance_id=0 from %s\n",  /* SID:64 */
    [LA_F282] = "[UDP] E: %s: invalid relay flag from client\n",  /* SID:282 */
    [LA_F63] = "[UDP] E: %s: invalid seq=%u\n",  /* SID:63 */
    [LA_F74] = "[UDP] E: REGISTER_ACK sent, status=error (no slot available)\n",  /* SID:74 */
    [LA_F82] = "[UDP] E: Relay %s: bad payload(len=%zu)\n",  /* SID:82 */
    [LA_F75] = "[UDP] I: %s from '%.*s': new instance(old=%u new=%u), resetting session\n",  /* SID:75 */
    [LA_F68] = "[UDP] I: Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n",  /* SID:68 */
    [LA_F288] = "[UDP] V: %s accepted from %s, sid=%u\n",  /* SID:288 */
    [LA_F73] = "[UDP] V: %s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n",  /* SID:73 */
    [LA_F86] = "[UDP] V: %s: accepted, releasing slot for '%s' -> '%s'\n",  /* SID:86 */
    [LA_F296] = "[UDP] V: %s: no matching pending msg (sid=%u)\n",  /* SID:296 */
    [LA_F85] = "[UDP] V: %s: waiting for peer '%.*s' to register\n",  /* SID:85 */
    [LA_F301] = "[UDP] W: %s: already has pending msg, rejecting sid=%u\n",  /* SID:301 */
    [LA_F302] = "[UDP] W: %s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:302 */
    [LA_F303] = "[UDP] W: %s: peer '%s' not online, rejecting sid=%u\n",  /* SID:303 */
    [LA_F304] = "[UDP] W: %s: requester not found for %s\n",  /* SID:304 */
    [LA_F66] = "[UDP] W: PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:66 */
    [LA_F72] = "[UDP] W: Timeout for pair '%s' -> '%s' (inactive for %.1f seconds)\n",  /* SID:72 */
    [LA_F87] = "[UDP] W: Unknown packet type 0x%02x from %s\n",  /* SID:87 */
};
