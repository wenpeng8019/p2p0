/*
 * Auto-generated language strings
 */

#include ".LANG.h"

/* 字符串表 */
const char* lang_en[LA_NUM] = {
    [LA_W0] = "disabled",  /* SID:1 */
    [LA_W1] = "enabled",  /* SID:2 */
    [LA_S0] = "               - TCP: RELAY mode signaling (stateful/long connection)",  /* SID:3 */
    [LA_S1] = "               - UDP: COMPACT mode signaling (stateless)",  /* SID:4 */
    [LA_S2] = "               Used to detect symmetric NAT (port consistency)",  /* SID:5 */
    [LA_S3] = "  port         Signaling server listen port (default: 8888)",  /* SID:6 */
    [LA_S4] = "  probe_port   NAT type detection port (default: 0=disabled)",  /* SID:7 */
    [LA_S5] = "  relay        Enable data relay support (COMPACT mode fallback)",  /* SID:8 */
    [LA_S6] = "[SERVER] Goodbye!",  /* SID:9 */
    [LA_S7] = "[SERVER] NAT probe disabled (bind failed)",  /* SID:10 */
    [LA_S8] = "[SERVER] Received shutdown signal, exiting gracefully...",  /* SID:11 */
    [LA_S9] = "[SERVER] Shutting down...",  /* SID:12 */
    [LA_S10] = "[TCP] Max peers reached, rejecting connection\n",  /* SID:14 */
    [LA_S11] = "[TCP] User list truncated (too many users)\n",  /* SID:15 */
    [LA_S12] = "Error: Too many arguments",  /* SID:16 */
    [LA_S13] = "Examples:",  /* SID:17 */
    [LA_S14] = "Parameters:",  /* SID:18 */
    [LA_F0] = "  %s                    # Default config (port 8888, no probe, no relay)",  /* SID:20 */
    [LA_F1] = "  %s 9000               # Listen on port 9000",  /* SID:21 */
    [LA_F2] = "  %s 9000 9001          # Listen 9000, probe port 9001",  /* SID:22 */
    [LA_F3] = "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay",  /* SID:23 */
    [LA_F4] = "Error: Invalid port number '%s' (range: 1-65535)",  /* SID:24 */
    [LA_F5] = "Error: Invalid probe port '%s' (range: 0-65535)",  /* SID:25 */
    [LA_F6] = "Error: Unknown option '%s' (expected: 'relay')",  /* SID:26 */
    [LA_F7] = "P2P Signaling Server listening on port %d (TCP + UDP)...",  /* SID:27 */
    [LA_F8] = "Usage: %s [port] [probe_port] [relay]",  /* SID:28 */
    [LA_F9] = "[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n",  /* SID:30 */
    [LA_F10] = "[SERVER] NAT probe socket listening on port %d",  /* SID:31 */
    [LA_F11] = "[SERVER] NAT probe: %s (port %d)",  /* SID:32 */
    [LA_F12] = "[SERVER] Relay support: %s",  /* SID:33 */
    [LA_F13] = "[SERVER] Starting P2P signal server on port %d",  /* SID:34 */
    [LA_F14] = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",  /* SID:35 */
    [LA_F15] = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",  /* SID:36 */
    [LA_F16] = "[TCP] All pending candidates flushed to '%s'\n",  /* SID:37 */
    [LA_F17] = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",  /* SID:38 */
    [LA_F18] = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",  /* SID:39 */
    [LA_F19] = "[TCP] Cannot allocate slot for offline user '%s'\n",  /* SID:40 */
    [LA_F20] = "[TCP] E: Invalid magic from peer '%s'\n",  /* SID:13 */
    [LA_F21] = "[TCP] Failed to receive payload from %s\n",  /* SID:42 */
    [LA_F22] = "[TCP] Failed to receive target name from %s\n",  /* SID:43 */
    [LA_F23] = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",  /* SID:44 */
    [LA_F24] = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",  /* SID:45 */
    [LA_F25] = "[TCP] I: Peer '%s' logged in\n",  /* SID:51 */
    [LA_F26] = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",  /* SID:46 */
    [LA_F27] = "[TCP] New connection from %s:%d\n",  /* SID:47 */
    [LA_F28] = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",  /* SID:48 */
    [LA_F29] = "[TCP] Payload too large (%u bytes) from %s\n",  /* SID:49 */
    [LA_F30] = "[TCP] Relaying %s from %s to %s (%u bytes)\n",  /* SID:52 */
    [LA_F31] = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",  /* SID:53 */
    [LA_F32] = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",  /* SID:54 */
    [LA_F33] = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",  /* SID:55 */
    [LA_F34] = "[TCP] Storage full indication flushed to '%s'\n",  /* SID:56 */
    [LA_F35] = "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",  /* SID:57 */
    [LA_F36] = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",  /* SID:58 */
    [LA_F37] = "[TCP] Target %s offline, caching candidates...\n",  /* SID:59 */
    [LA_F38] = "[TCP] Unknown message type %d from %s\n",  /* SID:60 */
    [LA_F39] = "[TCP] V: %s sent to '%s'\n",  /* SID:280 */
    [LA_F40] = "[TCP] V: Peer '%s' disconnected\n",  /* SID:50 */
    [LA_F41] = "[TCP] W: Client '%s' timeout (inactive for %ld seconds)\n",  /* SID:41 */
    [LA_F42] = "[UDP] E: %s sent, status=error (no slot available)\n",  /* SID:74 */
    [LA_F43] = "[UDP] E: %s seq=0 from client %s (server-only, dropped)\n",  /* SID:67 */
    [LA_F44] = "[UDP] E: %s: bad payload(len=%zu)\n",  /* SID:64 */
    [LA_F45] = "[UDP] E: %s: data too large (len=%d)\n",  /* SID:281 */
    [LA_F46] = "[UDP] E: %s: invalid instance_id=0 from %s\n",  /* SID:64 */
    [LA_F47] = "[UDP] E: %s: invalid relay flag from client\n",  /* SID:282 */
    [LA_F48] = "[UDP] E: %s: invalid seq=%u\n",  /* SID:63 */
    [LA_F49] = "[UDP] E: Relay %s: bad payload(len=%zu)\n",  /* SID:82 */
    [LA_F50] = "[UDP] I: %s forwarded: '%s' -> '%s', sid=%u (ses_id=%llu)\n",  /* SID:310 */
    [LA_F51] = "[UDP] I: %s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%llu)\n",  /* SID:311 */
    [LA_F52] = "[UDP] I: %s from '%s': new instance(old=%u new=%u), resetting session\n",  /* SID:75 */
    [LA_F53] = "[UDP] I: Address changed for '%s', notifying '%s' (ses_id=%llu)\n",  /* SID:312 */
    [LA_F54] = "[UDP] I: Assigned session_id=%llu for '%s' -> '%s'\n",  /* SID:313 */
    [LA_F55] = "[UDP] I: Pairing complete: '%s'(%d cands) <-> '%s'(%d cands)\n",  /* SID:68 */
    [LA_F56] = "[UDP] V: %s accepted from %s, session_id=%llu, sid=%u, msg=%u, len=%d\n",  /* SID:314 */
    [LA_F57] = "[UDP] V: %s accepted from %s, sid=%u\n",  /* SID:288 */
    [LA_F58] = "[UDP] V: %s accepted from %s, target='%s', sid=%u, msg=%u, len=%d\n",  /* SID:289 */
    [LA_F59] = "[UDP] V: %s received and %s sent for '%s'\n",  /* SID:290 */
    [LA_F60] = "[UDP] V: %s sent to '%s' (ses_id=%llu) [reregister]\n",  /* SID:315 */
    [LA_F61] = "[UDP] V: %s sent to '%s' (ses_id=%llu) [timeout]\n",  /* SID:316 */
    [LA_F62] = "[UDP] V: %s sent to '%s' (ses_id=%llu) [unregister]\n",  /* SID:317 */
    [LA_F63] = "[UDP] V: %s sent, status=%s, max_cands=%d, relay=%s, public=%s:%d, probe=%d\n",  /* SID:75 */
    [LA_F64] = "[UDP] V: %s: RPC complete for '%s', sid=%u (ses_id=%llu)\n",  /* SID:318 */
    [LA_F65] = "[UDP] V: %s: accepted, local='%s', remote='%s', inst_id=%u, cands=%d\n",  /* SID:73 */
    [LA_F66] = "[UDP] V: %s: accepted, releasing slot for '%s' -> '%s'\n",  /* SID:86 */
    [LA_F67] = "[UDP] V: %s: confirmed '%s', retries=%d (ses_id=%llu)\n",  /* SID:319 */
    [LA_F68] = "[UDP] V: %s: no matching pending msg (sid=%u)\n",  /* SID:296 */
    [LA_F69] = "[UDP] V: %s: waiting for peer '%s' to register\n",  /* SID:85 */
    [LA_F70] = "[UDP] V: PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%llu)\n",  /* SID:320 */
    [LA_F71] = "[UDP] V: Relay %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n",  /* SID:321 */
    [LA_F72] = "[UDP] V: Relay %s: '%s' -> '%s' (ses_id=%llu)\n",  /* SID:322 */
    [LA_F73] = "[UDP] W: %s for unknown ses_id=%llu\n",  /* SID:323 */
    [LA_F74] = "[UDP] W: %s: already has pending msg, rejecting sid=%u\n",  /* SID:301 */
    [LA_F75] = "[UDP] W: %s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:302 */
    [LA_F76] = "[UDP] W: %s: peer '%s' not online, rejecting sid=%u\n",  /* SID:303 */
    [LA_F77] = "[UDP] W: %s: requester not found for %s\n",  /* SID:304 */
    [LA_F78] = "[UDP] W: %s: target mismatch (expected='%s', got='%s')\n",  /* SID:305 */
    [LA_F79] = "[UDP] W: %s: unknown session_id=%llu\n",  /* SID:324 */
    [LA_F80] = "[UDP] W: Cannot relay %s: ses_id=%llu (peer unavailable)\n",  /* SID:325 */
    [LA_F81] = "[UDP] W: PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:66 */
    [LA_F82] = "[UDP] W: Relay %s for ses_id=%llu: peer unavailable (dropped)\n",  /* SID:326 */
    [LA_F83] = "[UDP] W: Relay %s for unknown ses_id=%llu (dropped)\n",  /* SID:327 */
    [LA_F84] = "[UDP] W: Timeout for pair '%s' -> '%s' (inactive for %ld seconds)\n",  /* SID:72 */
    [LA_F85] = "[UDP] W: Unknown packet type 0x%02x from %s\n",  /* SID:87 */
};
