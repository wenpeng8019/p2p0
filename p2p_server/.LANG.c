/*
 * Auto-generated language strings
 */

#include ".LANG.h"

/* 字符串表 */
const char* lang_en[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 */
    [LA_W2] = "enabled",  /* SID:2 */
    [LA_S3] = "[TCP] Max peers reached, rejecting connection\n",  /* SID:3 */
    [LA_S4] = "[TCP] User list truncated (too many users)\n",  /* SID:4 */
    [LA_S5] = "Goodbye!\n",  /* SID:5 */
    [LA_S6] = "NAT probe disabled (bind failed)\n",  /* SID:6 */
    [LA_S7] = "net init failed\n",  /* SID:7 */
    [LA_S8] = "Received shutdown signal, exiting gracefully...",  /* SID:8 */
    [LA_S9] = "Shutting down...\n",  /* SID:9 */
    [LA_F10] = "%s from '%.*s': new instance(old=%u new=%u), resetting session\n",  /* SID:10 */
    [LA_F11] = "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n",  /* SID:11 */
    [LA_F12] = "%s: accepted, releasing slot for '%s' -> '%s'\n",  /* SID:12 */
    [LA_F13] = "%s: bad payload(len=%zu)\n",  /* SID:13 */
    [LA_F14] = "%s: data too large (len=%d)\n",  /* SID:14 */
    [LA_F15] = "%s: invalid instance_id=0 from %s\n",  /* SID:15 */
    [LA_F16] = "%s: invalid relay flag from client\n",  /* SID:16 */
    [LA_F17] = "%s: invalid seq=%u\n",  /* SID:17 */
    [LA_F18] = "%s: no matching pending msg (sid=%u)\n",  /* SID:18 */
    [LA_F19] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:19 */
    [LA_F20] = "%s: obsolete sid=%u (current=%u), ignoring\n",  /* SID:20 */
    [LA_F21] = "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:21 */
    [LA_F22] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:22 */
    [LA_F23] = "%s: waiting for peer '%.*s' to register\n",  /* SID:23 */
    [LA_F24] = "Invalid port number %d (range: 1-65535)\n",  /* SID:24 */
    [LA_F25] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:25 */
    [LA_F26] = "NAT probe socket listening on port %d\n",  /* SID:26 */
    [LA_F27] = "NAT probe: %s (port %d)\n",  /* SID:27 */
    [LA_F28] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:28 */
    [LA_F29] = "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:29 */
    [LA_F30] = "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n",  /* SID:30 */
    [LA_F31] = "Relay support: %s\n",  /* SID:31 */
    [LA_F32] = "Send %s: mapped=%s:%d\n",  /* SID:32 */
    [LA_F33] = "Send %s: status=error (no slot available)\n",  /* SID:33 */
    [LA_F34] = "Starting P2P signal server on port %d\n",  /* SID:34 */
    [LA_F35] = "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n",  /* SID:35 */
    [LA_F36] = "Unknown packet type 0x%02x from %s\n",  /* SID:36 */
    [LA_F37] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:37 */
    [LA_F38] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:38 */
    [LA_F39] = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",  /* SID:39 */
    [LA_F40] = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",  /* SID:40 */
    [LA_F41] = "[TCP] All pending candidates flushed to '%s'\n",  /* SID:41 */
    [LA_F42] = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",  /* SID:42 */
    [LA_F43] = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",  /* SID:43 */
    [LA_F44] = "[TCP] Cannot allocate slot for offline user '%s'\n",  /* SID:44 */
    [LA_F45] = "[TCP] E: Invalid magic from peer '%s'\n",  /* SID:45 */
    [LA_F46] = "[TCP] Failed to receive payload from %s\n",  /* SID:46 */
    [LA_F47] = "[TCP] Failed to receive target name from %s\n",  /* SID:47 */
    [LA_F48] = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",  /* SID:48 */
    [LA_F49] = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",  /* SID:49 */
    [LA_F50] = "[TCP] I: Peer '%s' logged in\n",  /* SID:50 */
    [LA_F51] = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",  /* SID:51 */
    [LA_F52] = "[TCP] New connection from %s:%d\n",  /* SID:52 */
    [LA_F53] = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",  /* SID:53 */
    [LA_F54] = "[TCP] Payload too large (%u bytes) from %s\n",  /* SID:54 */
    [LA_F55] = "[TCP] Relaying %s from %s to %s (%u bytes)\n",  /* SID:55 */
    [LA_F56] = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",  /* SID:56 */
    [LA_F57] = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",  /* SID:57 */
    [LA_F58] = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",  /* SID:58 */
    [LA_F59] = "[TCP] Storage full indication flushed to '%s'\n",  /* SID:59 */
    [LA_F60] = "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",  /* SID:60 */
    [LA_F61] = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",  /* SID:61 */
    [LA_F62] = "[TCP] Target %s offline, caching candidates...\n",  /* SID:62 */
    [LA_F63] = "[TCP] Unknown message type %d from %s\n",  /* SID:63 */
    [LA_F64] = "[TCP] V: %s sent to '%s'\n",  /* SID:64 */
    [LA_F65] = "[TCP] V: Peer '%s' disconnected\n",  /* SID:65 */
    [LA_F66] = "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n",  /* SID:66 */
    [LA_F67] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:67 */
    [LA_F68] = "[UDP] %s send to %s failed(%d)\n",  /* SID:68 */
    [LA_F69] = "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n",  /* SID:69 */
    [LA_F70] = "[UDP] %s send to %s, seq=0, flags=0, len=%d\n",  /* SID:70 */
    [LA_F71] = "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n",  /* SID:71 */
    [LA_F72] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:72 */
    [LA_F73] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:73 */
    [LA_F74] = "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n",  /* SID:74 */
    [LA_F75] = "probe UDP bind failed(%d)\n",  /* SID:75 */
    [LA_F76] = "select failed(%d)\n",  /* SID:76 */
};
