/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 new */
    [LA_W2] = "enabled",  /* SID:2 new */
    [LA_S3] = "[TCP] User list truncated (too many users)\n",  /* SID:3 new */
    [LA_S131] = "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)",  /* SID:131 new */
    [LA_S5] = "Enable data relay support (COMPACT mode fallback)",  /* SID:5 new */
    [LA_S6] = "Enable MSG RPC support",  /* SID:6 new */
    [LA_S7] = "NAT type detection port (0=disabled)",  /* SID:7 new */
    [LA_S8] = "Received shutdown signal, exiting gracefully...",  /* SID:8 new */
    [LA_S9] = "Shutting down...\n",  /* SID:9 new */
    [LA_S10] = "Signaling server listen port (TCP+UDP)",  /* SID:10 new */
    [LA_S11] = "Use Chinese language",  /* SID:11 new */
    [LA_F12] = "%s accepted, '%s' -> '%s', ses_id=%llu\n",  /* SID:12 new */
    [LA_F13] = "%s accepted, peer='%s', ses_id=%llu\n",  /* SID:13 new */
    [LA_F14] = "%s accepted, seq=%u, ses_id=%llu\n",  /* SID:14 new */
    [LA_F15] = "%s for unknown ses_id=%llu\n",  /* SID:15 new */
    [LA_F16] = "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%llu)\n",  /* SID:16 new */
    [LA_F17] = "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%llu)\n",  /* SID:17 new */
    [LA_F18] = "%s from '%.*s': new instance(old=%u new=%u), resetting session\n",  /* SID:18 new */
    [LA_F19] = "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%llu)\n",  /* SID:19 new */
    [LA_F20] = "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%llu)\n",  /* SID:20 new */
    [LA_F21] = "%s retransmit, resend ACK, sid=%u (ses_id=%llu)\n",  /* SID:21 new */
    [LA_F22] = "%s: RPC complete for '%s', sid=%u (ses_id=%llu)\n",  /* SID:22 new */
    [LA_F23] = "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n",  /* SID:23 new */
    [LA_F24] = "%s: accepted, releasing slot for '%s' -> '%s'\n",  /* SID:24 new */
    [LA_F25] = "%s: accepted, ses_id=%llu, sid=%u, code=%u, len=%d\n",  /* SID:25 new */
    [LA_F26] = "%s: accepted, ses_id=%llu, sid=%u, msg=%u, len=%d\n",  /* SID:26 new */
    [LA_F27] = "%s: accepted, ses_id=%llu, sid=%u\n",  /* SID:27 new */
    [LA_F28] = "%s: bad payload(len=%zu)\n",  /* SID:28 new */
    [LA_F29] = "%s: confirmed '%s', retries=%d (ses_id=%llu)\n",  /* SID:29 new */
    [LA_F30] = "%s: data too large (len=%d)\n",  /* SID:30 new */
    [LA_F31] = "%s: invalid instance_id=0 from %s\n",  /* SID:31 new */
    [LA_F32] = "%s: invalid relay flag from client\n",  /* SID:32 new */
    [LA_F33] = "%s: invalid seq=%u\n",  /* SID:33 new */
    [LA_F34] = "%s: invalid session_id=%llu or sid=%u\n",  /* SID:34 new */
    [LA_F35] = "%s: no matching pending msg (sid=%u)\n",  /* SID:35 new */
    [LA_F36] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:36 new */
    [LA_F37] = "%s: obsolete sid=%u (current=%u), ignoring\n",  /* SID:37 new */
    [LA_F38] = "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:38 new */
    [LA_F39] = "%s: peer '%s' not online for session_id=%llu\n",  /* SID:39 new */
    [LA_F40] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:40 new */
    [LA_F41] = "%s: requester not found for ses_id=%llu\n",  /* SID:41 new */
    [LA_F42] = "%s: reset '%.*s'(disconnected) session for re-pairing\n",  /* SID:42 new */
    [LA_F43] = "%s: unknown session_id=%llu\n",  /* SID:43 new */
    [LA_F44] = "%s: waiting for peer '%.*s' to register\n",  /* SID:44 new */
    [LA_F45] = "Addr changed for '%s', but first info packet was abandoned (ses_id=%llu)\n",  /* SID:45 new */
    [LA_F46] = "Addr changed for '%s', defer notification until first ACK (ses_id=%llu)\n",  /* SID:46 new */
    [LA_F47] = "Addr changed for '%s', deferred notifying '%s' (ses_id=%llu)\n",  /* SID:47 new */
    [LA_F48] = "Addr changed for '%s', notifying '%s' (ses_id=%llu)\n",  /* SID:48 new */
    [LA_F49] = "Cannot relay %s: ses_id=%llu (peer unavailable)\n",  /* SID:49 new */
    [LA_F50] = "% Goodbye!\n",  /* SID:50 new */
    [LA_F51] = "Invalid port number %d (range: 1-65535)\n",  /* SID:51 new */
    [LA_F52] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:52 new */
    [LA_F53] = "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%llu)\n",  /* SID:53 new */
    [LA_F54] = "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%llu)\n",  /* SID:54 new */
    [LA_F55] = "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%llu)\n",  /* SID:55 new */
    [LA_F56] = "MSG_RESP gave up after %d retries, sid=%u (ses_id=%llu)\n",  /* SID:56 new */
    [LA_F57] = "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%llu)\n",  /* SID:57 new */
    [LA_F58] = "% NAT probe disabled (bind failed)\n",  /* SID:58 new */
    [LA_F59] = "NAT probe socket listening on port %d\n",  /* SID:59 new */
    [LA_F60] = "NAT probe: %s (port %d)\n",  /* SID:60 new */
    [LA_F61] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:61 new */
    [LA_F62] = "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%llu)\n",  /* SID:62 new */
    [LA_F63] = "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:63 new */
    [LA_F64] = "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n",  /* SID:64 new */
    [LA_F65] = "Relay %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n",  /* SID:65 new */
    [LA_F66] = "Relay support: %s\n",  /* SID:66 new */
    [LA_F67] = "Send %s: base_index=%u, cands=%d, ses_id=%llu, peer='%s'\n",  /* SID:67 new */
    [LA_F68] = "Send %s: mapped=%s:%d\n",  /* SID:68 new */
    [LA_F69] = "Send %s: peer='%s', reason=%s, ses_id=%llu\n",  /* SID:69 new */
    [LA_F70] = "Send %s: ses_id=%llu, peer='%s'\n",  /* SID:70 new */
    [LA_F71] = "Send %s: ses_id=%llu, sid=%u, msg=%u, data_len=%d, peer='%s'\n",  /* SID:71 new */
    [LA_F72] = "Send %s: ses_id=%llu, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n",  /* SID:72 new */
    [LA_F73] = "Send %s: ses_id=%llu, sid=%u, peer='%s'\n",  /* SID:73 new */
    [LA_F74] = "Send %s: ses_id=%llu, sid=%u, status=%u\n",  /* SID:74 new */
    [LA_F75] = "Send %s: status=%s, max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, ses_id=%llu, inst_id=%u\n",  /* SID:75 new */
    [LA_F76] = "Send %s: status=error (no slot available)\n",  /* SID:76 new */
    [LA_F77] = "Starting P2P signal server on port %d\n",  /* SID:77 new */
    [LA_F78] = "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n",  /* SID:78 new */
    [LA_F79] = "Unknown packet type 0x%02x from %s\n",  /* SID:79 new */
    [LA_F80] = "[Relay] %s for ses_id=%llu: peer unavailable (dropped)\n",  /* SID:80 new */
    [LA_F81] = "[Relay] %s for unknown ses_id=%llu (dropped)\n",  /* SID:81 new */
    [LA_F82] = "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n",  /* SID:82 new */
    [LA_F83] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:83 new */
    [LA_F84] = "[Relay] %s: '%s' -> '%s' (ses_id=%llu)\n",  /* SID:84 new */
    [LA_F85] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:85 new */
    [LA_F86] = "[Relay] %s: missing SESSION flag, dropped\n",  /* SID:86 new */
    [LA_F87] = "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n",  /* SID:87 new */
    [LA_F88] = "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n",  /* SID:88 new */
    [LA_F89] = "[TCP] All pending candidates flushed to '%s'\n",  /* SID:89 new */
    [LA_F90] = "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n",  /* SID:90 new */
    [LA_F91] = "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n",  /* SID:91 new */
    [LA_F92] = "[TCP] Cannot allocate slot for offline user '%s'\n",  /* SID:92 new */
    [LA_F93] = "[TCP] E: Invalid magic from peer '%s'\n",  /* SID:93 new */
    [LA_F94] = "[TCP] Failed to receive payload from %s\n",  /* SID:94 new */
    [LA_F95] = "[TCP] Failed to receive target name from %s\n",  /* SID:95 new */
    [LA_F96] = "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n",  /* SID:96 new */
    [LA_F97] = "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n",  /* SID:97 new */
    [LA_F98] = "[TCP] I: Peer '%s' logged in\n",  /* SID:98 new */
    [LA_F99] = "% [TCP] Max peers reached, rejecting connection\n",  /* SID:99 new */
    [LA_F100] = "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n",  /* SID:100 new */
    [LA_F101] = "[TCP] New connection from %s:%d\n",  /* SID:101 new */
    [LA_F102] = "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n",  /* SID:102 new */
    [LA_F103] = "[TCP] Payload too large (%u bytes) from %s\n",  /* SID:103 new */
    [LA_F104] = "[TCP] Relaying %s from %s to %s (%u bytes)\n",  /* SID:104 new */
    [LA_F105] = "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n",  /* SID:105 new */
    [LA_F106] = "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n",  /* SID:106 new */
    [LA_F107] = "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n",  /* SID:107 new */
    [LA_F108] = "[TCP] Storage full indication flushed to '%s'\n",  /* SID:108 new */
    [LA_F109] = "[TCP] Storage full, connection intent from '%s' to '%s' noted\n",  /* SID:109 new */
    [LA_F110] = "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n",  /* SID:110 new */
    [LA_F111] = "[TCP] Target %s offline, caching candidates...\n",  /* SID:111 new */
    [LA_F112] = "[TCP] Unknown message type %d from %s\n",  /* SID:112 new */
    [LA_F113] = "[TCP] V: %s sent to '%s'\n",  /* SID:113 new */
    [LA_F114] = "[TCP] V: Peer '%s' disconnected\n",  /* SID:114 new */
    [LA_F115] = "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n",  /* SID:115 new */
    [LA_F116] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:116 new */
    [LA_F117] = "[UDP] %s send to %s failed(%d)\n",  /* SID:117 new */
    [LA_F118] = "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n",  /* SID:118 new */
    [LA_F119] = "[UDP] %s send to %s, seq=0, flags=0, len=%d\n",  /* SID:119 new */
    [LA_F120] = "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n",  /* SID:120 new */
    [LA_F121] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:121 new */
    [LA_F122] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:122 new */
    [LA_F123] = "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n",  /* SID:123 new */
    [LA_F124] = "% net init failed\n",  /* SID:124 new */
    [LA_F125] = "probe UDP bind failed(%d)\n",  /* SID:125 new */
    [LA_F126] = "select failed(%d)\n",  /* SID:126 new */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
