/*
 * Auto-generated language strings
 */

#include ".LANG.h"

/* 字符串表 */
const char* lang_en[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 */
    [LA_W2] = "enabled",  /* SID:2 */
    [LA_S14] = "[TCP] Max peers reached, rejecting connection\n",  /* SID:14 */
    [LA_S15] = "[TCP] User list truncated (too many users)\n",  /* SID:15 */
    [LA_S9] = "Goodbye!\n",  /* SID:9 */
    [LA_S10] = "NAT probe disabled (bind failed)\n",  /* SID:10 */
    [LA_S11] = "Received shutdown signal, exiting gracefully...",  /* SID:11 */
    [LA_S12] = "Shutting down...\n",  /* SID:12 */
    [LA_F75] = "%s from '%.*s': new instance(old=%u new=%u), resetting session\n",  /* SID:75 */
    [LA_F73] = "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n",  /* SID:73 */
    [LA_F86] = "%s: accepted, releasing slot for '%s' -> '%s'\n",  /* SID:86 */
    [LA_F64] = "%s: bad payload(len=%zu)\n",  /* SID:64 */
    [LA_F281] = "%s: data too large (len=%d)\n",  /* SID:281 */
    [LA_F64] = "%s: invalid instance_id=0 from %s\n",  /* SID:64 */
    [LA_F282] = "%s: invalid relay flag from client\n",  /* SID:282 */
    [LA_F63] = "%s: invalid seq=%u\n",  /* SID:63 */
    [LA_F296] = "%s: no matching pending msg (sid=%u)\n",  /* SID:296 */
    [LA_F302] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:302 */
    [LA_F303] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:303 */
    [LA_F85] = "%s: waiting for peer '%.*s' to register\n",  /* SID:85 */
    [LA_F328] = "Invalid port number %d (range: 1-65535)\n",  /* SID:328 */
    [LA_F329] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:329 */
    [LA_F31] = "NAT probe socket listening on port %d\n",  /* SID:31 */
    [LA_F32] = "NAT probe: %s (port %d)\n",  /* SID:32 */
    [LA_F27] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:27 */
    [LA_F66] = "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:66 */
    [LA_F68] = "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n",  /* SID:68 */
    [LA_F33] = "Relay support: %s\n",  /* SID:33 */
    [LA_F34] = "Starting P2P signal server on port %d\n",  /* SID:34 */
    [LA_F72] = "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n",  /* SID:72 */
    [LA_F87] = "Unknown packet type 0x%02x from %s\n",  /* SID:87 */
    [LA_F67] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:67 */
    [LA_F82] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:82 */
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
    [LA_F330] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:330 */
    [LA_F331] = "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n",  /* SID:331 */
    [LA_F332] = "[UDP] %s send to %s, seq=0, flags=0, len=%d\n",  /* SID:332 */
    [LA_F333] = "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n",  /* SID:333 */
    [LA_F334] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:334 */
    [LA_F335] = "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n",  /* SID:335 */
    [LA_F336] = "probe UDP bind failed(%d)\n",  /* SID:336 */
    [LA_F337] = "select failed(%d)\n",  /* SID:337 */
};
