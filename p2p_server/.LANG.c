/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_server;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "disabled",  /* SID:1 */
    [LA_W2] = "enabled",  /* SID:2 */
    [LA_S359] = "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)",  /* SID:359 */
    [LA_S5] = "Enable data relay support (COMPACT mode fallback)",  /* SID:5 */
    [LA_S6] = "Enable MSG RPC support",  /* SID:6 */
    [LA_S7] = "NAT type detection port (0=disabled)",  /* SID:7 */
    [LA_S9] = "Shutting down...\n",  /* SID:9 */
    [LA_S10] = "Signaling server listen port (TCP+UDP)",  /* SID:10 */
    [LA_S11] = "Use Chinese language",  /* SID:11 */
    [LA_F249] = "% Client closed connection (EOF on recv)\n",  /* SID:249 */
    [LA_F251] = "% Client sent data before ONLINE_ACK completed\n",  /* SID:251 */
    [LA_F12] = "%s accepted, '%s' -> '%s', ses_id=%u\n",  /* SID:12 */
    [LA_F13] = "%s accepted, peer='%s', auth_key=%llu\n",  /* SID:13 */
    [LA_F14] = "%s accepted, seq=%u, ses_id=%u\n",  /* SID:14 */
    [LA_F15] = "%s for unknown ses_id=%u\n",  /* SID:15 */
    [LA_F16] = "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n",  /* SID:16 */
    [LA_F17] = "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n",  /* SID:17 */
    [LA_F18] = "%s from '%.*s': new instance(old=%u new=%u), resetting\n",  /* SID:18 */
    [LA_F19] = "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n",  /* SID:19 */
    [LA_F20] = "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%u)\n",  /* SID:20 */
    [LA_F21] = "%s retransmit, resend ACK, sid=%u (ses_id=%u)\n",  /* SID:21 */
    [LA_F298] = "%s: '%.*s' cleared stale peer marker, ready for re-pair\n",  /* SID:298 */
    [LA_F257] = "%s: '%s' sid=%u code=%u data_len=%d\n",  /* SID:257 */
    [LA_F294] = "%s: '%s' sid=%u msg=%u data_len=%d\n",  /* SID:294 */
    [LA_F252] = "%s: OOM for relay buffer\n",  /* SID:252 */
    [LA_F253] = "%s: OOM for zero-copy recv buffer\n",  /* SID:253 */
    [LA_F22] = "%s: RPC complete for '%s', sid=%u (ses_id=%u)\n",  /* SID:22 */
    [LA_F23] = "%s: accepted, local='%.*s', inst_id=%u\n",  /* SID:23 */
    [LA_F24] = "%s: accepted, releasing slot for '%s'\n",  /* SID:24 */
    [LA_F25] = "%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n",  /* SID:25 */
    [LA_F26] = "%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n",  /* SID:26 */
    [LA_F27] = "%s: accepted, ses_id=%u, sid=%u\n",  /* SID:27 */
    [LA_F29] = "%s: addr-notify confirmed '%s' (ses_id=%u)\n",  /* SID:29 */
    [LA_F44] = "%s: auth_key=%llu assigned for '%.*s'\n",  /* SID:44 */
    [LA_F33] = "%s: auth_key=%llu, cands=%d from %s\n",  /* SID:33 */
    [LA_F254] = "%s: bad FIN marker=0x%02x\n",  /* SID:254 */
    [LA_F255] = "%s: bad payload(cnt=%d, len=%u, expected=%u)\n",  /* SID:255 */
    [LA_F256] = "%s: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n",  /* SID:256 */
    [LA_F295] = "%s: bad payload(len=%u)\n",  /* SID:295 */
    [LA_F28] = "%s: bad payload(len=%zu)\n",  /* SID:28 */
    [LA_F350] = "%s: build_session failed for '%.*s'\n",  /* SID:350 */
    [LA_F258] = "%s: build_session failed for '%s'\n",  /* SID:258 */
    [LA_F259] = "%s: close ses_id=%u\n",  /* SID:259 */
    [LA_F340] = "%s: confirmed '%s', retries=%d (ses_id=%u)\n",  /* SID:340 */
    [LA_F30] = "%s: data too large (len=%d)\n",  /* SID:30 */
    [LA_F260] = "%s: forwarded to peer, cands=%d\n",  /* SID:260 */
    [LA_F31] = "%s: invalid auth_key=0 from %s\n",  /* SID:31 */
    [LA_F296] = "%s: invalid instance_id=0 from %s\n",  /* SID:296 */
    [LA_F32] = "%s: invalid relay flag from client\n",  /* SID:32 */
    [LA_F297] = "%s: invalid seq=%u\n",  /* SID:297 */
    [LA_F34] = "%s: invalid session_id=%u or sid=%u\n",  /* SID:34 */
    [LA_F351] = "%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n",  /* SID:351 */
    [LA_F261] = "%s: local='%s', remote='%s', side=%d, peer_online=%d, cands=%d\n",  /* SID:261 */
    [LA_F35] = "%s: no matching pending msg (sid=%u)\n",  /* SID:35 */
    [LA_F36] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:36 */
    [LA_F37] = "%s: obsolete sid=%u (current=%u), ignoring\n",  /* SID:37 */
    [LA_F38] = "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:38 */
    [LA_F352] = "%s: paired '%.*s' <-> '%.*s'\n",  /* SID:352 */
    [LA_F39] = "%s: peer '%s' not online for session_id=%u\n",  /* SID:39 */
    [LA_F40] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:40 */
    [LA_F262] = "%s: peer '%s' offline, cached cands=%d\n",  /* SID:262 */
    [LA_F280] = "%s: peer offline, sending error resp\n",  /* SID:280 */
    [LA_F41] = "%s: requester not found for ses_id=%u\n",  /* SID:41 */
    [LA_F300] = "%s: requester offline, discarding\n",  /* SID:300 */
    [LA_F288] = "%s: rpc busy (pending sid=%u)\n",  /* SID:288 */
    [LA_F301] = "%s: sid mismatch (got=%u, pending=%u), discarding\n",  /* SID:301 */
    [LA_F353] = "%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n",  /* SID:353 */
    [LA_F302] = "%s: unknown auth_key=%llu from %s\n",  /* SID:302 */
    [LA_F43] = "%s: unknown session_id=%u\n",  /* SID:43 */
    [LA_F263] = "'%s' disconnected\n",  /* SID:263 */
    [LA_F264] = "'%s' timeout (inactive for %.1f sec)\n",  /* SID:264 */
    [LA_F45] = "Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n",  /* SID:45 */
    [LA_F46] = "Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n",  /* SID:46 */
    [LA_F47] = "Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n",  /* SID:47 */
    [LA_F48] = "Addr changed for '%s', notifying '%s' (ses_id=%u)\n",  /* SID:48 */
    [LA_F49] = "Cannot relay %s: ses_id=%u (peer unavailable)\n",  /* SID:49 */
    [LA_F265] = "Client closed connection (EOF on send, reason=%s)\n",  /* SID:265 */
    [LA_F250] = "% Client disconnected (not yet logged in)\n",  /* SID:250 */
    [LA_F266] = "Duplicate session create blocked: '%s' -> '%s'\n",  /* SID:266 */
    [LA_F50] = "% Goodbye!\n",  /* SID:50 */
    [LA_F51] = "Invalid port number %d (range: 1-65535)\n",  /* SID:51 */
    [LA_F52] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:52 */
    [LA_F53] = "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n",  /* SID:53 */
    [LA_F54] = "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n",  /* SID:54 */
    [LA_F55] = "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n",  /* SID:55 */
    [LA_F56] = "MSG_RESP gave up after %d retries, sid=%u (ses_id=%u)\n",  /* SID:56 */
    [LA_F57] = "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n",  /* SID:57 */
    [LA_F58] = "% NAT probe disabled (bind failed)\n",  /* SID:58 */
    [LA_F59] = "NAT probe socket listening on port %d\n",  /* SID:59 */
    [LA_F60] = "NAT probe: %s (port %d)\n",  /* SID:60 */
    [LA_F267] = "ONLINE: '%s' came online (inst=%u)\n",  /* SID:267 */
    [LA_F268] = "ONLINE: '%s' new instance (old=%u, new=%u), destroying old\n",  /* SID:268 */
    [LA_F269] = "ONLINE: '%s' reconnected (inst=%u), migrating fd\n",  /* SID:269 */
    [LA_F270] = "ONLINE: bad payload(len=%u, expected=%u)\n",  /* SID:270 */
    [LA_F271] = "ONLINE: duplicate from '%s'\n",  /* SID:271 */
    [LA_F272] = "ONLINE_ACK sent to '%s'\n",  /* SID:272 */
    [LA_F61] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:61 */
    [LA_F303] = "% RPC_ERR: OOM\n",  /* SID:303 */
    [LA_F65] = "Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n",  /* SID:65 */
    [LA_F66] = "Relay support: %s\n",  /* SID:66 */
    [LA_F62] = "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n",  /* SID:62 */
    [LA_F63] = "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:63 */
    [LA_F64] = "SYNC0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n",  /* SID:64 */
    [LA_F273] = "SYNC0_ACK queue busy for '%s', drop\n",  /* SID:273 */
    [LA_F274] = "SYNC_ACK queue busy for '%s', drop\n",  /* SID:274 */
    [LA_F70] = "Send %s: auth_key=%llu, peer='%s'\n",  /* SID:70 */
    [LA_F67] = "Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n",  /* SID:67 */
    [LA_F338] = "Send %s: cands=%d, ses_id=%u, peer='%s'\n",  /* SID:338 */
    [LA_F68] = "Send %s: mapped=%s:%d\n",  /* SID:68 */
    [LA_F75] = "Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n",  /* SID:75 */
    [LA_F69] = "Send %s: peer='%s', reason=%s, ses_id=%u\n",  /* SID:69 */
    [LA_F76] = "Send %s: rejected (no slot available)\n",  /* SID:76 */
    [LA_F74] = "Send %s: ses_id=%u, peer=%s\n",  /* SID:74 */
    [LA_F71] = "Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s'\n",  /* SID:71 */
    [LA_F72] = "Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n",  /* SID:72 */
    [LA_F73] = "Send %s: ses_id=%u, sid=%u, peer='%s'\n",  /* SID:73 */
    [LA_F304] = "Send %s: ses_id=%u, sid=%u, status=%u\n",  /* SID:304 */
    [LA_F77] = "Starting P2P signal server on port %d\n",  /* SID:77 */
    [LA_F78] = "Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n",  /* SID:78 */
    [LA_F79] = "Unknown packet type 0x%02x from %s\n",  /* SID:79 */
    [LA_F80] = "[Relay] %s for ses_id=%u: peer unavailable (dropped)\n",  /* SID:80 */
    [LA_F81] = "[Relay] %s for unknown ses_id=%u (dropped)\n",  /* SID:81 */
    [LA_F82] = "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n",  /* SID:82 */
    [LA_F83] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:83 */
    [LA_F84] = "[Relay] %s: '%s' -> '%s' (ses_id=%u)\n",  /* SID:84 */
    [LA_F85] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:85 */
    [LA_F86] = "[Relay] %s: missing SESSION flag, dropped\n",  /* SID:86 */
    [LA_F213] = "% [TCP] Failed to set client socket to non-blocking mode\n",  /* SID:213 */
    [LA_F99] = "% [TCP] Max peers reached, rejecting connection\n",  /* SID:99 */
    [LA_F101] = "[TCP] New connection from %s:%d\n",  /* SID:101 */
    [LA_F217] = "% [TCP] OOM: cannot allocate recv buffer for new client\n",  /* SID:217 */
    [LA_F179] = "[UDP] %s recv from %s, len=%zu\n",  /* SID:179 */
    [LA_F341] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:341 */
    [LA_F117] = "[UDP] %s send to %s failed(%d)\n",  /* SID:117 */
    [LA_F180] = "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n",  /* SID:180 */
    [LA_F181] = "[UDP] %s send to %s, seq=0, flags=0, len=%d\n",  /* SID:181 */
    [LA_F182] = "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n",  /* SID:182 */
    [LA_F121] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:121 */
    [LA_F122] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:122 */
    [LA_F123] = "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n",  /* SID:123 */
    [LA_F275] = "bad payload len %u (type=%u)\n",  /* SID:275 */
    [LA_F276] = "bad payload len %u\n",  /* SID:276 */
    [LA_F124] = "% net init failed\n",  /* SID:124 */
    [LA_F125] = "probe UDP bind failed(%d)\n",  /* SID:125 */
    [LA_F277] = "recv() failed: errno=%d\n",  /* SID:277 */
    [LA_F126] = "select failed(%d)\n",  /* SID:126 */
    [LA_F278] = "send(%s) failed: errno=%d\n",  /* SID:278 */
    [LA_F305] = "ses_id=%u busy (pending relay)\n",  /* SID:305 */
    [LA_F306] = "ses_id=%u peer not connected (type=%u)\n",  /* SID:306 */
    [LA_F281] = "type=%u rejected: client not logged in\n",  /* SID:281 */
    [LA_F282] = "unknown ses_id=%u (type=%u)\n",  /* SID:282 */
    [LA_F283] = "unsupported type=%u (ses_id=%u)\n",  /* SID:283 */
    [LA_S8] = "Received shutdown signal, exiting gracefully...",  /* SID:8 disabled */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_server_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
