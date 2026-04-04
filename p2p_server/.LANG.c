/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_server;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 */
    [LA_W2] = "enabled",  /* SID:2 */
    [LA_S3] = "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)",  /* SID:3 */
    [LA_S4] = "Enable data relay support (COMPACT mode fallback)",  /* SID:4 */
    [LA_S5] = "Enable MSG RPC support",  /* SID:5 */
    [LA_S6] = "NAT type detection port (0=disabled)",  /* SID:6 */
    [LA_S7] = "Received shutdown signal, exiting gracefully...",  /* SID:7 */
    [LA_S8] = "Shutting down...\n",  /* SID:8 */
    [LA_S9] = "Signaling server listen port (TCP+UDP)",  /* SID:9 */
    [LA_S10] = "Use Chinese language",  /* SID:10 */
    [LA_F11] = "% Client closed connection (EOF on recv)\n",  /* SID:11 */
    [LA_F12] = "% Client sent data before ONLINE_ACK completed\n",  /* SID:12 */
    [LA_F13] = "%s accepted, '%s' -> '%s', ses_id=%u\n",  /* SID:13 */
    [LA_F14] = "%s accepted, peer='%s', auth_key=%llu\n",  /* SID:14 */
    [LA_F15] = "%s accepted, seq=%u, ses_id=%u\n",  /* SID:15 */
    [LA_F16] = "%s for unknown ses_id=%u\n",  /* SID:16 */
    [LA_F17] = "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n",  /* SID:17 */
    [LA_F18] = "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n",  /* SID:18 */
    [LA_F19] = "%s from '%.*s': new instance(old=%u new=%u), resetting\n",  /* SID:19 */
    [LA_F20] = "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n",  /* SID:20 */
    [LA_F21] = "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%u)\n",  /* SID:21 */
    [LA_F22] = "%s retransmit, resend ACK, sid=%u (ses_id=%u)\n",  /* SID:22 */
    [LA_F23] = "%s: '%.*s' cleared stale peer marker, ready for re-pair\n",  /* SID:23 */
    [LA_F24] = "%s: '%s' sid=%u code=%u data_len=%d\n",  /* SID:24 */
    [LA_F25] = "%s: '%s' sid=%u msg=%u data_len=%d\n",  /* SID:25 */
    [LA_F26] = "%s: OOM for relay buffer\n",  /* SID:26 */
    [LA_F27] = "%s: OOM for zero-copy recv buffer\n",  /* SID:27 */
    [LA_F28] = "%s: RPC complete for '%s', sid=%u (ses_id=%u)\n",  /* SID:28 */
    [LA_F29] = "%s: accepted, local='%.*s', inst_id=%u\n",  /* SID:29 */
    [LA_F30] = "%s: accepted, releasing slot for '%s'\n",  /* SID:30 */
    [LA_F31] = "%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n",  /* SID:31 */
    [LA_F32] = "%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n",  /* SID:32 */
    [LA_F33] = "%s: accepted, ses_id=%u, sid=%u\n",  /* SID:33 */
    [LA_F34] = "%s: addr-notify confirmed '%s' (ses_id=%u)\n",  /* SID:34 */
    [LA_F35] = "%s: auth_key=%llu assigned for '%.*s'\n",  /* SID:35 */
    [LA_F36] = "%s: auth_key=%llu, cands=%d from %s\n",  /* SID:36 */
    [LA_F37] = "%s: bad FIN marker=0x%02x\n",  /* SID:37 */
    [LA_F38] = "%s: bad payload(cnt=%d, len=%u, expected=%u)\n",  /* SID:38 */
    [LA_F39] = "%s: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n",  /* SID:39 */
    [LA_F40] = "%s: bad payload(len=%u)\n",  /* SID:40 */
    [LA_F41] = "%s: bad payload(len=%zu)\n",  /* SID:41 */
    [LA_F42] = "%s: build_session failed for '%.*s'\n",  /* SID:42 */
    [LA_F43] = "%s: build_session failed for '%s'\n",  /* SID:43 */
    [LA_F44] = "%s: close ses_id=%u\n",  /* SID:44 */
    [LA_F45] = "%s: confirmed '%s', retries=%d (ses_id=%u)\n",  /* SID:45 */
    [LA_F46] = "%s: data too large (len=%d)\n",  /* SID:46 */
    [LA_F47] = "%s: forwarded to peer, cands=%d\n",  /* SID:47 */
    [LA_F48] = "%s: invalid auth_key=0 from %s\n",  /* SID:48 */
    [LA_F49] = "%s: invalid instance_id=0 from %s\n",  /* SID:49 */
    [LA_F50] = "%s: invalid relay flag from client\n",  /* SID:50 */
    [LA_F51] = "%s: invalid seq=%u\n",  /* SID:51 */
    [LA_F52] = "%s: invalid session_id=%u or sid=%u\n",  /* SID:52 */
    [LA_F53] = "%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n",  /* SID:53 */
    [LA_F54] = "%s: local='%s', remote='%s', side=%d, peer_online=%d, cands=%d\n",  /* SID:54 */
    [LA_F55] = "%s: no matching pending msg (sid=%u)\n",  /* SID:55 */
    [LA_F56] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:56 */
    [LA_F57] = "%s: obsolete sid=%u (current=%u), ignoring\n",  /* SID:57 */
    [LA_F58] = "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:58 */
    [LA_F59] = "%s: paired '%.*s' <-> '%.*s'\n",  /* SID:59 */
    [LA_F60] = "%s: peer '%s' not online for session_id=%u\n",  /* SID:60 */
    [LA_F61] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:61 */
    [LA_F62] = "%s: peer '%s' offline, cached cands=%d\n",  /* SID:62 */
    [LA_F63] = "%s: peer offline, sending error resp\n",  /* SID:63 */
    [LA_F64] = "%s: requester not found for ses_id=%u\n",  /* SID:64 */
    [LA_F65] = "%s: requester offline, discarding\n",  /* SID:65 */
    [LA_F66] = "%s: rpc busy (pending sid=%u)\n",  /* SID:66 */
    [LA_F67] = "%s: sid mismatch (got=%u, pending=%u), discarding\n",  /* SID:67 */
    [LA_F68] = "%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n",  /* SID:68 */
    [LA_F69] = "%s: unknown auth_key=%llu from %s\n",  /* SID:69 */
    [LA_F70] = "%s: unknown session_id=%u\n",  /* SID:70 */
    [LA_F71] = "'%s' disconnected\n",  /* SID:71 */
    [LA_F72] = "'%s' timeout (inactive for %.1f sec)\n",  /* SID:72 */
    [LA_F73] = "Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n",  /* SID:73 */
    [LA_F74] = "Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n",  /* SID:74 */
    [LA_F75] = "Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n",  /* SID:75 */
    [LA_F76] = "Addr changed for '%s', notifying '%s' (ses_id=%u)\n",  /* SID:76 */
    [LA_F77] = "Cannot relay %s: ses_id=%u (peer unavailable)\n",  /* SID:77 */
    [LA_F78] = "Client closed connection (EOF on send, reason=%s)\n",  /* SID:78 */
    [LA_F79] = "% Client disconnected (not yet logged in)\n",  /* SID:79 */
    [LA_F80] = "Duplicate session create blocked: '%s' -> '%s'\n",  /* SID:80 */
    [LA_F81] = "% Goodbye!\n",  /* SID:81 */
    [LA_F82] = "Invalid port number %d (range: 1-65535)\n",  /* SID:82 */
    [LA_F83] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:83 */
    [LA_F84] = "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n",  /* SID:84 */
    [LA_F85] = "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n",  /* SID:85 */
    [LA_F86] = "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n",  /* SID:86 */
    [LA_F87] = "MSG_RESP gave up after %d retries, sid=%u (ses_id=%u)\n",  /* SID:87 */
    [LA_F88] = "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n",  /* SID:88 */
    [LA_F89] = "% NAT probe disabled (bind failed)\n",  /* SID:89 */
    [LA_F90] = "NAT probe socket listening on port %d\n",  /* SID:90 */
    [LA_F91] = "NAT probe: %s (port %d)\n",  /* SID:91 */
    [LA_F92] = "ONLINE: '%s' came online (inst=%u)\n",  /* SID:92 */
    [LA_F93] = "ONLINE: '%s' new instance (old=%u, new=%u), destroying old\n",  /* SID:93 */
    [LA_F94] = "ONLINE: '%s' reconnected (inst=%u), migrating fd\n",  /* SID:94 */
    [LA_F95] = "ONLINE: bad payload(len=%u, expected=%u)\n",  /* SID:95 */
    [LA_F96] = "ONLINE: duplicate from '%s'\n",  /* SID:96 */
    [LA_F97] = "ONLINE_ACK sent to '%s'\n",  /* SID:97 */
    [LA_F98] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:98 */
    [LA_F99] = "% RPC_ERR: OOM\n",  /* SID:99 */
    [LA_F100] = "Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n",  /* SID:100 */
    [LA_F101] = "Relay support: %s\n",  /* SID:101 */
    [LA_F102] = "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n",  /* SID:102 */
    [LA_F103] = "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:103 */
    [LA_F104] = "SYNC0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n",  /* SID:104 */
    [LA_F105] = "SYNC0_ACK queue busy for '%s', drop\n",  /* SID:105 */
    [LA_F106] = "SYNC_ACK queue busy for '%s', drop\n",  /* SID:106 */
    [LA_F107] = "Send %s: auth_key=%llu, peer='%s'\n",  /* SID:107 */
    [LA_F108] = "Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n",  /* SID:108 */
    [LA_F109] = "Send %s: cands=%d, ses_id=%u, peer='%s'\n",  /* SID:109 */
    [LA_F110] = "Send %s: mapped=%s:%d\n",  /* SID:110 */
    [LA_F111] = "Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n",  /* SID:111 */
    [LA_F112] = "Send %s: peer='%s', reason=%s, ses_id=%u\n",  /* SID:112 */
    [LA_F113] = "Send %s: rejected (no slot available)\n",  /* SID:113 */
    [LA_F114] = "Send %s: ses_id=%u, peer=%s\n",  /* SID:114 */
    [LA_F115] = "Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s'\n",  /* SID:115 */
    [LA_F116] = "Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n",  /* SID:116 */
    [LA_F117] = "Send %s: ses_id=%u, sid=%u, peer='%s'\n",  /* SID:117 */
    [LA_F118] = "Send %s: ses_id=%u, sid=%u, status=%u\n",  /* SID:118 */
    [LA_F119] = "Starting P2P signal server on port %d\n",  /* SID:119 */
    [LA_F120] = "Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n",  /* SID:120 */
    [LA_F121] = "Unknown packet type 0x%02x from %s\n",  /* SID:121 */
    [LA_F122] = "[Relay] %s for ses_id=%u: peer unavailable (dropped)\n",  /* SID:122 */
    [LA_F123] = "[Relay] %s for unknown ses_id=%u (dropped)\n",  /* SID:123 */
    [LA_F124] = "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n",  /* SID:124 */
    [LA_F125] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:125 */
    [LA_F126] = "[Relay] %s: '%s' -> '%s' (ses_id=%u)\n",  /* SID:126 */
    [LA_F127] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:127 */
    [LA_F128] = "[Relay] %s: missing SESSION flag, dropped\n",  /* SID:128 */
    [LA_F129] = "% [TCP] Failed to set client socket to non-blocking mode\n",  /* SID:129 */
    [LA_F130] = "% [TCP] Max peers reached, rejecting connection\n",  /* SID:130 */
    [LA_F131] = "[TCP] New connection from %s:%d\n",  /* SID:131 */
    [LA_F132] = "% [TCP] OOM: cannot allocate recv buffer for new client\n",  /* SID:132 */
    [LA_F133] = "[UDP] %s recv from %s, len=%zu\n",  /* SID:133 */
    [LA_F134] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:134 */
    [LA_F135] = "[UDP] %s send to %s failed(%d)\n",  /* SID:135 */
    [LA_F136] = "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n",  /* SID:136 */
    [LA_F137] = "[UDP] %s send to %s, seq=0, flags=0, len=%d\n",  /* SID:137 */
    [LA_F138] = "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n",  /* SID:138 */
    [LA_F139] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:139 */
    [LA_F140] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:140 */
    [LA_F141] = "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n",  /* SID:141 */
    [LA_F142] = "bad payload len %u (type=%u)\n",  /* SID:142 */
    [LA_F143] = "bad payload len %u\n",  /* SID:143 */
    [LA_F144] = "% net init failed\n",  /* SID:144 */
    [LA_F145] = "probe UDP bind failed(%d)\n",  /* SID:145 */
    [LA_F146] = "recv() failed: errno=%d\n",  /* SID:146 */
    [LA_F147] = "select failed(%d)\n",  /* SID:147 */
    [LA_F148] = "send(%s) failed: errno=%d\n",  /* SID:148 */
    [LA_F149] = "ses_id=%u busy (pending relay)\n",  /* SID:149 */
    [LA_F150] = "ses_id=%u peer not connected (type=%u)\n",  /* SID:150 */
    [LA_F151] = "type=%u rejected: client not logged in\n",  /* SID:151 */
    [LA_F152] = "unknown ses_id=%u (type=%u)\n",  /* SID:152 */
    [LA_F153] = "unsupported type=%u (ses_id=%u)\n",  /* SID:153 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_server_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
