/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_server;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 */
    [LA_W2] = "enabled",  /* SID:2 */
    [LA_S3] = "[TCP] User list truncated (too many users)\n",  /* SID:3 */
    [LA_S4] = "Received shutdown signal, exiting gracefully...",  /* SID:4 */
    [LA_S5] = "Shutting down...\n",  /* SID:5 */
    [LA_F6] = "%s from '%.*s': new instance(old=%u new=%u), resetting session\n",  /* SID:6 */
    [LA_F7] = "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n",  /* SID:7 */
    [LA_F8] = "%s: accepted, releasing slot for '%s' -> '%s'\n",  /* SID:8 */
    [LA_F9] = "%s: bad payload(len=%zu)\n",  /* SID:9 */
    [LA_F10] = "%s: data too large (len=%d)\n",  /* SID:10 */
    [LA_F11] = "%s: invalid instance_id=0 from %s\n",  /* SID:11 */
    [LA_F12] = "%s: invalid relay flag from client\n",  /* SID:12 */
    [LA_F13] = "%s: invalid seq=%u\n",  /* SID:13 */
    [LA_F14] = "%s: no matching pending msg (sid=%u)\n",  /* SID:14 */
    [LA_F15] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:15 */
    [LA_F16] = "%s: obsolete sid=%u (current=%u), ignoring\n",  /* SID:16 */
    [LA_F17] = "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:17 */
    [LA_F18] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:18 */
    [LA_F19] = "%s: waiting for peer '%.*s' to register\n",  /* SID:19 */
    [LA_F20] = "% Goodbye!\n",  /* SID:20 */
    [LA_F21] = "Invalid port number %d (range: 1-65535)\n",  /* SID:21 */
    [LA_F22] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:22 */
    [LA_F23] = "% NAT probe disabled (bind failed)\n",  /* SID:23 */
    [LA_F24] = "NAT probe socket listening on port %d\n",  /* SID:24 */
    [LA_F25] = "NAT probe: %s (port %d)\n",  /* SID:25 */
    [LA_F26] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:26 */
    [LA_F27] = "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:27 */
    [LA_F28] = "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n",  /* SID:28 */
    [LA_F29] = "Relay support: %s\n",  /* SID:29 */
    [LA_F30] = "Send %s: mapped=%s:%d\n",  /* SID:30 */
    [LA_F31] = "Send %s: status=error (no slot available)\n",  /* SID:31 */
    [LA_F32] = "Starting P2P signal server on port %d\n",  /* SID:32 */
    [LA_F33] = "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n",  /* SID:33 */
    [LA_F34] = "Unknown packet type 0x%02x from %s\n",  /* SID:34 */
    [LA_F35] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:35 */
    [LA_F36] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:36 */
    [LA_F37] = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",  /* SID:37 */
    [LA_F38] = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",  /* SID:38 */
    [LA_F39] = "[TCP] All pending candidates flushed to '%s'\n",  /* SID:39 */
    [LA_F40] = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",  /* SID:40 */
    [LA_F41] = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",  /* SID:41 */
    [LA_F42] = "[TCP] Cannot allocate slot for offline user '%s'\n",  /* SID:42 */
    [LA_F43] = "[TCP] E: Invalid magic from peer '%s'\n",  /* SID:43 */
    [LA_F44] = "[TCP] Failed to receive payload from %s\n",  /* SID:44 */
    [LA_F45] = "[TCP] Failed to receive target name from %s\n",  /* SID:45 */
    [LA_F46] = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",  /* SID:46 */
    [LA_F47] = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",  /* SID:47 */
    [LA_F48] = "[TCP] I: Peer '%s' logged in\n",  /* SID:48 */
    [LA_F49] = "% [TCP] Max peers reached, rejecting connection\n",  /* SID:49 */
    [LA_F50] = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",  /* SID:50 */
    [LA_F51] = "[TCP] New connection from %s:%d\n",  /* SID:51 */
    [LA_F52] = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",  /* SID:52 */
    [LA_F53] = "[TCP] Payload too large (%u bytes) from %s\n",  /* SID:53 */
    [LA_F54] = "[TCP] Relaying %s from %s to %s (%u bytes)\n",  /* SID:54 */
    [LA_F55] = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",  /* SID:55 */
    [LA_F56] = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",  /* SID:56 */
    [LA_F57] = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",  /* SID:57 */
    [LA_F58] = "[TCP] Storage full indication flushed to '%s'\n",  /* SID:58 */
    [LA_F59] = "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",  /* SID:59 */
    [LA_F60] = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",  /* SID:60 */
    [LA_F61] = "[TCP] Target %s offline, caching candidates...\n",  /* SID:61 */
    [LA_F62] = "[TCP] Unknown message type %d from %s\n",  /* SID:62 */
    [LA_F63] = "[TCP] V: %s sent to '%s'\n",  /* SID:63 */
    [LA_F64] = "[TCP] V: Peer '%s' disconnected\n",  /* SID:64 */
    [LA_F65] = "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n",  /* SID:65 */
    [LA_F66] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:66 */
    [LA_F67] = "[UDP] %s send to %s failed(%d)\n",  /* SID:67 */
    [LA_F68] = "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n",  /* SID:68 */
    [LA_F69] = "[UDP] %s send to %s, seq=0, flags=0, len=%d\n",  /* SID:69 */
    [LA_F70] = "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n",  /* SID:70 */
    [LA_F71] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:71 */
    [LA_F72] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:72 */
    [LA_F73] = "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n",  /* SID:73 */
    [LA_F74] = "% net init failed\n",  /* SID:74 */
    [LA_F75] = "probe UDP bind failed(%d)\n",  /* SID:75 */
    [LA_F76] = "select failed(%d)\n",  /* SID:76 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_server_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
