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
    [LA_S150] = "Enable WebSocket service on same TCP port",  /* SID:150 */
    [LA_S6] = "NAT type detection port (0=disabled)",  /* SID:6 */
    [LA_S7] = "Received shutdown signal, exiting gracefully...",  /* SID:7 */
    [LA_S8] = "Shutting down...\n",  /* SID:8 */
    [LA_S9] = "Signaling server listen port (TCP+UDP)",  /* SID:9 */
    [LA_S10] = "Use Chinese language",  /* SID:10 */
    [LA_S151] = "WebSocket dedicated port (also enables --ws)",  /* SID:151 */
    [LA_F13] = "%s accepted, '%s' -> '%s', ses_id=%u\n",  /* SID:13 */
    [LA_F14] = "%s accepted, peer='%s', auth_key=%llu\n",  /* SID:14 */
    [LA_F15] = "%s accepted, seq=%u, ses_id=%u\n",  /* SID:15 */
    [LA_F16] = "%s for unknown ses_id=%u\n",  /* SID:16 */
    [LA_F17] = "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n",  /* SID:17 */
    [LA_F18] = "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n",  /* SID:18 */
    [LA_F20] = "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n",  /* SID:20 */
    [LA_F21] = "%s retransmit during RSP phase, ignoring, sid=%u (ses_id=%u)\n",  /* SID:21 */
    [LA_F22] = "%s retransmit, resend ACK, sid=%u (ses_id=%u)\n",  /* SID:22 */
    [LA_F48] = "%s: %s <-> %s forward\n",  /* SID:48 */
    [LA_F93] = "%s: '%.*s' new REG (inst=%u)\n",  /* SID:93 */
    [LA_F98] = "%s: '%.*s' reconnected & reactive (inst=%u)\n",  /* SID:98 */
    [LA_F153] = "%s: '%.*s' reconnected & renew (inst=%u)\n",  /* SID:153 */
    [LA_F167] = "%s: '%s' -> '%s' (%zu bytes)\n",  /* SID:167 */
    [LA_F63] = "%s: '%s' -> '%s' created (id=%u, peer_zombie)\n",  /* SID:63 */
    [LA_F152] = "%s: '%s' <-> '%s' paired (ses=%u/%u)\n",  /* SID:152 */
    [LA_F230] = "%s: '%s' new REG (inst=%u)\n",  /* SID:230 */
    [LA_F231] = "%s: '%s' reconnected & reactive (inst=%u)\n",  /* SID:231 */
    [LA_F232] = "%s: '%s' reconnected & renew (inst=%u)\n",  /* SID:232 */
    [LA_F45] = "%s: '%s' ses_id=%u\n",  /* SID:45 */
    [LA_F24] = "%s: '%s' sid=%u code=%u data_len=%d\n",  /* SID:24 */
    [LA_F25] = "%s: '%s' sid=%u msg=%u data_len=%d\n",  /* SID:25 */
    [LA_F235] = "%s: '%s' unreachable, pending\n",  /* SID:235 */
    [LA_F72] = "%s: '%s'\n",  /* SID:72 */
    [LA_F26] = "%s: 2nd-ack confirmed '%s' (ses_id=%u)\n",  /* SID:26 */
    [LA_F37] = "%s: OOM building session '%s' -> '%s'\n",  /* SID:37 */
    [LA_F29] = "%s: RPC complete for '%s', sid=%u (ses_id=%u)\n",  /* SID:29 */
    [LA_F30] = "%s: accepted, local='%.*s', inst_id=%u\n",  /* SID:30 */
    [LA_F31] = "%s: accepted, releasing slot for '%s'\n",  /* SID:31 */
    [LA_F32] = "%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n",  /* SID:32 */
    [LA_F33] = "%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n",  /* SID:33 */
    [LA_F34] = "%s: accepted, ses_id=%u, sid=%u\n",  /* SID:34 */
    [LA_F35] = "%s: addr-notify confirmed '%s' (ses_id=%u)\n",  /* SID:35 */
    [LA_F259] = "%s: alloc buf failed(OOM)\n",  /* SID:259 */
    [LA_F28] = "%s: alloc buffer failed(OOM)\n",  /* SID:28 */
    [LA_F51] = "%s: alloc client for '%.*s' failed\n",  /* SID:51 */
    [LA_F36] = "%s: auth_key=%llu assigned for '%.*s'\n",  /* SID:36 */
    [LA_F154] = "%s: auth_key=%llu, cands=%d from %s\n",  /* SID:154 */
    [LA_F38] = "%s: bad FIN marker=0x%02x\n",  /* SID:38 */
    [LA_F155] = "%s: bad frame len=%u\n",  /* SID:155 */
    [LA_F138] = "%s: bad payload(%u)\n",  /* SID:138 */
    [LA_F39] = "%s: bad payload(cnt=%d, len=%u, expected=%u)\n",  /* SID:39 */
    [LA_F156] = "%s: bad payload(len=%u)\n",  /* SID:156 */
    [LA_F42] = "%s: bad payload(len=%zu)\n",  /* SID:42 */
    [LA_F40] = "%s: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n",  /* SID:40 */
    [LA_F205] = "%s: bad payload(sid=0)\n",  /* SID:205 */
    [LA_F44] = "%s: build session to '%s' failed(%d)\n",  /* SID:44 */
    [LA_F157] = "%s: build session to '%s' failed(OOM)",  /* SID:157 */
    [LA_F43] = "%s: build_session failed for '%.*s'\n",  /* SID:43 */
    [LA_F145] = "%s: busy (ses_id=%u), pending\n",  /* SID:145 */
    [LA_F206] = "%s: busy (ses_id=%u, sid=%u), pending\n",  /* SID:206 */
    [LA_F158] = "%s: close ses_id=%u\n",  /* SID:158 */
    [LA_F46] = "%s: confirmed '%s', retries=%d (ses_id=%u)\n",  /* SID:46 */
    [LA_F47] = "%s: data too large (len=%d)\n",  /* SID:47 */
    [LA_F207] = "%s: deprecated (ses_id=%u, sid=%u), drop\n",  /* SID:207 */
    [LA_F208] = "%s: deprecated (ses_id=%u, sid=%u, last=%u), discarding\n",  /* SID:208 */
    [LA_F209] = "%s: deprecated (ses_id=%u, sid=%u, last=%u), drop\n",  /* SID:209 */
    [LA_F210] = "%s: duplicate SYN0 (ses_id=%u), resend ACK\n",  /* SID:210 */
    [LA_F211] = "%s: duplicate SYN0 (ses_id=%u), resend response\n",  /* SID:211 */
    [LA_F159] = "%s: duplicate SYN0 with different candidates from '%s'\n",  /* SID:159 */
    [LA_F97] = "%s: duplicate from '%s'\n",  /* SID:97 */
    [LA_F160] = "%s: invalid REG format\n",  /* SID:160 */
    [LA_F49] = "%s: invalid auth_key=0 from %s\n",  /* SID:49 */
    [LA_F94] = "%s: invalid instance id\n",  /* SID:94 */
    [LA_F50] = "%s: invalid instance_id=0 from %s\n",  /* SID:50 */
    [LA_F96] = "%s: invalid peer id\n",  /* SID:96 */
    [LA_F161] = "%s: invalid relay flag from client\n",  /* SID:161 */
    [LA_F142] = "%s: invalid remote id\n",  /* SID:142 */
    [LA_F52] = "%s: invalid seq=%u\n",  /* SID:52 */
    [LA_F53] = "%s: invalid session_id=%u or sid=%u\n",  /* SID:53 */
    [LA_F212] = "%s: invalid sid=0\n",  /* SID:212 */
    [LA_F55] = "%s: local='%s', remote='%s', online=%d, cands=%d\n",  /* SID:55 */
    [LA_F162] = "%s: local='%s', remote='%s', online=%d, payload=%u\n",  /* SID:162 */
    [LA_F213] = "%s: missing payload\n",  /* SID:213 */
    [LA_F56] = "%s: no matching pending msg (sid=%u)\n",  /* SID:56 */
    [LA_F57] = "%s: no matching pending msg (sid=%u, expected=%u)\n",  /* SID:57 */
    [LA_F58] = "%s: obsolete sid=%u (current=%u), ignoring\n",  /* SID:58 */
    [LA_F59] = "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:59 */
    [LA_F60] = "%s: paired '%.*s' <-> '%.*s'\n",  /* SID:60 */
    [LA_F61] = "%s: peer '%s' not online for session_id=%u\n",  /* SID:61 */
    [LA_F62] = "%s: peer '%s' not online, rejecting sid=%u\n",  /* SID:62 */
    [LA_F163] = "%s: peer '%s' offline, pending\n",  /* SID:163 */
    [LA_F260] = "%s: peer '%s' unreachable, pending\n",  /* SID:260 */
    [LA_F64] = "%s: peer offline, sending error resp\n",  /* SID:64 */
    [LA_F214] = "%s: pkt queue full, reply busy\n",  /* SID:214 */
    [LA_F215] = "%s: prev ALV ACK still pending, skip\n",  /* SID:215 */
    [LA_F261] = "%s: realloc client for '%.*s' failed\n",  /* SID:261 */
    [LA_F147] = "%s: rejected for not reg(%.*s)\n",  /* SID:147 */
    [LA_F236] = "%s: rejected for not reg(%s)\n",  /* SID:236 */
    [LA_F237] = "%s: rejected for not reg\n",  /* SID:237 */
    [LA_F216] = "%s: request simultaneously for '%s'\n",  /* SID:216 */
    [LA_F65] = "%s: requester not found for ses_id=%u\n",  /* SID:65 */
    [LA_F67] = "%s: rpc busy (pending sid=%u)\n",  /* SID:67 */
    [LA_F165] = "%s: ses_id=%u, confirm sid=%u\n",  /* SID:165 */
    [LA_F166] = "%s: ses_id=%u, data_len=%u\n",  /* SID:166 */
    [LA_F217] = "%s: ses_id=%u, dup sid=%u, resend confirm\n",  /* SID:217 */
    [LA_F218] = "%s: ses_id=%u, peer offline, drop pkt\n",  /* SID:218 */
    [LA_F219] = "%s: ses_id=%u, peer offline, drop rsp\n",  /* SID:219 */
    [LA_F220] = "%s: ses_id=%u, peer offline, drop sid=%u\n",  /* SID:220 */
    [LA_F221] = "%s: ses_id=%u, sid=%u, cands=%d\n",  /* SID:221 */
    [LA_F68] = "%s: sid mismatch (got=%u, pending=%u), discarding\n",  /* SID:68 */
    [LA_F240] = "%s: sid=%u -> peer_sid=%u sync_sid=%u\n",  /* SID:240 */
    [LA_F168] = "%s: sid=%u -> peer_sid=%u, data_len=%u\n",  /* SID:168 */
    [LA_F262] = "%s: snprintf failed\n",  /* SID:262 */
    [LA_F263] = "%s: text frame overflow(32-bit)\n",  /* SID:263 */
    [LA_F241] = "%s: too many candidates(cnt=%d)\n",  /* SID:241 */
    [LA_F70] = "%s: unknown auth_key=%llu from %s\n",  /* SID:70 */
    [LA_F148] = "%s: unknown ses_id=%u\n",  /* SID:148 */
    [LA_F71] = "%s: unknown session_id=%u\n",  /* SID:71 */
    [LA_F222] = "%session: alloc buffer failed(OOM)\n",  /* SID:222 */
    [LA_F73] = "'%s' timeout & cleanup (inactive for %.1f sec)\n",  /* SID:73 */
    [LA_F242] = ": bad payload(%u)\n",  /* SID:242 */
    [LA_F171] = ": unknown ses_id=%u type=0x%02x from '%s'\n",  /* SID:171 */
    [LA_F74] = "Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n",  /* SID:74 */
    [LA_F75] = "Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n",  /* SID:75 */
    [LA_F76] = "Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n",  /* SID:76 */
    [LA_F77] = "Addr changed for '%s', notifying '%s' (ses_id=%u)\n",  /* SID:77 */
    [LA_F170] = "BIN 0x%02x: ses_id=%u peer not connected\n",  /* SID:170 */
    [LA_F149] = "BIN: unknown type=0x%02x from '%s'\n",  /* SID:149 */
    [LA_F78] = "Cannot relay %s: ses_id=%u (peer unavailable)\n",  /* SID:78 */
    [LA_F172] = "Client closed during protocol detection (slot %d)\n",  /* SID:172 */
    [LA_F174] = "Failed to initialize %s client\n",  /* SID:174 */
    [LA_F175] = "Failed to initialize TCP/RELAY client for slot %d\n",  /* SID:175 */
    [LA_F176] = "Failed to initialize WS/ICE client for slot %d\n",  /* SID:176 */
    [LA_F177] = "Failed to peek client data for protocol detection (slot %d), errno=%d\n",  /* SID:177 */
    [LA_F82] = "% Goodbye!\n",  /* SID:82 */
    [LA_F83] = "Invalid port number %d (range: 1-65535)\n",  /* SID:83 */
    [LA_F84] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:84 */
    [LA_F90] = "% NAT probe disabled (bind failed)\n",  /* SID:90 */
    [LA_F91] = "NAT probe socket listening on port %d\n",  /* SID:91 */
    [LA_F92] = "NAT probe: %s (port %d)\n",  /* SID:92 */
    [LA_F178] = "New %s client connected from %s:%d, assigned slot %d\n",  /* SID:178 */
    [LA_F99] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:99 */
    [LA_F85] = "REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n",  /* SID:85 */
    [LA_F86] = "REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n",  /* SID:86 */
    [LA_F87] = "REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n",  /* SID:87 */
    [LA_F88] = "RSP gave up after %d retries, sid=%u (ses_id=%u)\n",  /* SID:88 */
    [LA_F89] = "RSP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n",  /* SID:89 */
    [LA_F101] = "Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n",  /* SID:101 */
    [LA_F102] = "Relay support: %s\n",  /* SID:102 */
    [LA_F103] = "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n",  /* SID:103 */
    [LA_F104] = "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n",  /* SID:104 */
    [LA_F108] = "Send %s: auth_key=%llu, peer='%s'\n",  /* SID:108 */
    [LA_F109] = "Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n",  /* SID:109 */
    [LA_F110] = "Send %s: cands=%d, ses_id=%u, peer='%s'\n",  /* SID:110 */
    [LA_F111] = "Send %s: mapped=%s:%d\n",  /* SID:111 */
    [LA_F112] = "Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n",  /* SID:112 */
    [LA_F113] = "Send %s: peer='%s', reason=%s, ses_id=%u\n",  /* SID:113 */
    [LA_F114] = "Send %s: rejected (no slot available)\n",  /* SID:114 */
    [LA_F115] = "Send %s: ses_id=%u, peer=%s\n",  /* SID:115 */
    [LA_F116] = "Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s', retries=%d\n",  /* SID:116 */
    [LA_F117] = "Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d, retries=%d\n",  /* SID:117 */
    [LA_F118] = "Send %s: ses_id=%u, sid=%u, peer='%s'\n",  /* SID:118 */
    [LA_F119] = "Send %s: ses_id=%u, sid=%u, status=%u\n",  /* SID:119 */
    [LA_F120] = "Starting P2P signal server on port %d\n",  /* SID:120 */
    [LA_F122] = "Unknown packet type 0x%02x from %s\n",  /* SID:122 */
    [LA_F181] = "WebSocket service listening on port %d\n",  /* SID:181 */
    [LA_F11] = "[%s] conn closed (EOF on recv)\n",  /* SID:11 */
    [LA_F182] = "[%s] conn closed (EOF on send)\n",  /* SID:182 */
    [LA_F224] = "[%s] conn closed during handshake(%d) (EOF on recv)\n",  /* SID:224 */
    [LA_F243] = "[%s] conn closed during handshake(%d) (EOF on send)\n",  /* SID:243 */
    [LA_F183] = "[%s] recv failed(%d) during handshake(%d) \n",  /* SID:183 */
    [LA_F225] = "[%s] recv failed(%d)\n",  /* SID:225 */
    [LA_F244] = "[%s] send failed(%d) during handshake(%d)\n",  /* SID:244 */
    [LA_F144] = "[%s] send failed(%d)\n",  /* SID:144 */
    [LA_F245] = "[CT] payload len(%u) overflow, max: %u\n",  /* SID:245 */
    [LA_F246] = "[CT] resolve payload len failed(%d)\n",  /* SID:246 */
    [LA_F264] = "[CT] send item refer invalid(%p)\n",  /* SID:264 */
    [LA_F265] = "[CT] send sess item refer invalid(%p)\n",  /* SID:265 */
    [LA_F123] = "[Relay] %s for ses_id=%u: peer unavailable (dropped)\n",  /* SID:123 */
    [LA_F124] = "[Relay] %s for unknown ses_id=%u (dropped)\n",  /* SID:124 */
    [LA_F125] = "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n",  /* SID:125 */
    [LA_F126] = "[Relay] %s seq=0 from client %s (server-only, dropped)\n",  /* SID:126 */
    [LA_F127] = "[Relay] %s: '%s' -> '%s' (ses_id=%u)\n",  /* SID:127 */
    [LA_F128] = "[Relay] %s: bad payload(len=%zu)\n",  /* SID:128 */
    [LA_F129] = "[Relay] %s: missing SESSION flag, dropped\n",  /* SID:129 */
    [LA_F130] = "% [TCP] Failed to set client socket to non-blocking mode\n",  /* SID:130 */
    [LA_F131] = "% [TCP] Max peers reached, rejecting connection\n",  /* SID:131 */
    [LA_F135] = "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:135 */
    [LA_F136] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:136 */
    [LA_F137] = "[UDP] %s send to %s:%d, len=%d\n",  /* SID:137 */
    [LA_F186] = "[UDP] REQ recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:186 */
    [LA_F187] = "[UDP] RSP recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:187 */
    [LA_F188] = "[UDP] RSP_ACK recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:188 */
    [LA_F189] = "[UDP] SYN0 recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:189 */
    [LA_F134] = "[UDP] SYN0_ACK recv from %s, len=%zu\n",  /* SID:134 */
    [LA_F190] = "[UDP] SYNC_ACK recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:190 */
    [LA_F248] = "% [WS] CONTINUATION frame without fragmentation going\n",  /* SID:248 */
    [LA_F249] = "% [WS] Client frame missing mask\n",  /* SID:249 */
    [LA_F143] = "[WS] HTTP accept rejected(%d)\n",  /* SID:143 */
    [LA_F250] = "% [WS] Invalid UTF-8 in TEXT frame\n",  /* SID:250 */
    [LA_F251] = "% [WS] Invalid UTF-8 in close reason\n",  /* SID:251 */
    [LA_F252] = "[WS] Invalid close code %u\n",  /* SID:252 */
    [LA_F253] = "[WS] Invalid control frame: opcode=%u fin=%u hdr_len=%u\n",  /* SID:253 */
    [LA_F254] = "[WS] New %s without fragmentation end\n",  /* SID:254 */
    [LA_F226] = "% [WS] OOM in fragment reassembly\n",  /* SID:226 */
    [LA_F227] = "% [WS] OOM: cannot allocate HTTP recv buffer\n",  /* SID:227 */
    [LA_F255] = "[WS] RSV bit set in opcode %u\n",  /* SID:255 */
    [LA_F256] = "[WS] Reserved opcode %u\n",  /* SID:256 */
    [LA_F228] = "[WS] bad payload pos(%u) < hdr_sz(%u)\n",  /* SID:228 */
    [LA_F192] = "% [WS] close timeout, force closing\n",  /* SID:192 */
    [LA_F257] = "% [WS] invalid last_reason frame\n",  /* SID:257 */
    [LA_F258] = "[WS] truncate last_reason payload_len=%u to 125\n",  /* SID:258 */
    [LA_F199] = "[W] RPC timeout: sid=%u (ses_id=%u)\n",  /* SID:199 */
    [LA_F266] = "% alloc buf failed(OOM)\n",  /* SID:266 */
    [LA_F179] = "handshake<%d> sent to '%s'\n",  /* SID:179 */
    [LA_F229] = "make err(%d) resp failed(OOM)\n",  /* SID:229 */
    [LA_F140] = "% net init failed\n",  /* SID:140 */
    [LA_F141] = "probe UDP bind failed(%d)\n",  /* SID:141 */
    [LA_F201] = "select failed(%d)\n",  /* SID:201 */
    [LA_F100] = "% send failed(OOM)\n",  /* SID:100 */
    [LA_F203] = "unknown msg from '%s': %.32s\n",  /* SID:203 */
    [LA_F204] = "unsupported type=%u (ses_id=%u)\n",  /* SID:204 */
    [LA_F233] = "%s: invalid recv layout for zero-copy forward\n",  /* SID:233 disabled */
    [LA_F234] = "%s: missing recv item for zero-copy forward\n",  /* SID:234 disabled */
    [LA_F238] = "%s: renew sess by peer %s",  /* SID:238 disabled */
    [LA_F239] = "%s: renew sess to peer %s",  /* SID:239 disabled */
    [LA_F66] = "%s: requester offline, discarding\n",  /* SID:66 disabled */
    [LA_F223] = "REG: '%s' new instance (old=%u, new=%u), resetting session\n",  /* SID:223 disabled */
    [LA_F95] = "REG: '%s' reconnected (inst=%u), migrating\n",  /* SID:95 disabled */
    [LA_F247] = "% [R] OOM: cannot allocate recv buffer\n",  /* SID:247 disabled */
    [LA_F133] = "% [TCP] OOM: cannot allocate recv buffer for new client\n",  /* SID:133 disabled */
    [LA_F27] = "%s: OOM for relay buffer\n",  /* SID:27 disabled */
    [LA_F41] = "%s: bad frame len=%u\n",  /* SID:41 disabled */
    [LA_F164] = "%s: send failed\n",  /* SID:164 disabled */
    [LA_F146] = "%s: ses_id=%u peer not connected\n",  /* SID:146 disabled */
    [LA_F169] = "'%s' recv closed\n",  /* SID:169 disabled */
    [LA_F79] = "% Client closed connection (EOF on recv during handshake)\n",  /* SID:79 disabled */
    [LA_F80] = "% Client recv closed (not yet reg)\n",  /* SID:80 disabled */
    [LA_F81] = "Duplicate session create blocked: '%s' -> '%s'\n",  /* SID:81 disabled */
    [LA_F173] = "% Failed to allocate buffer for new WebSocket client\n",  /* SID:173 disabled */
    [LA_F180] = "WebSocket recv callback error: errno=%d\n",  /* SID:180 disabled */
    [LA_F191] = "[WS] Client closed (slot %d)\n",  /* SID:191 disabled */
    [LA_F193] = "[WS] client closed during handshake (slot %d)\n",  /* SID:193 disabled */
    [LA_F194] = "[WS] conn closed during send: errno=%d (slot %d)\n",  /* SID:194 disabled */
    [LA_F195] = "[WS] queue close(%u) proto failed(%d)\n",  /* SID:195 disabled */
    [LA_F196] = "[WS] queue text data failed(%d)\n",  /* SID:196 disabled */
    [LA_F197] = "[WS] queue text msg failed(%d)\n",  /* SID:197 disabled */
    [LA_F198] = "[WS] recv failed(%d) (slot %d)\n",  /* SID:198 disabled */
    [LA_F139] = "bad payload(len=%u)\n",  /* SID:139 disabled */
    [LA_F200] = "recv failed during handshake: errno=%d\n",  /* SID:200 disabled */
    [LA_F202] = "send failed during handshake: errno=%d\n",  /* SID:202 disabled */
    [LA_F184] = "[UDP] ALIVE recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:184 disabled */
    [LA_F185] = "[UDP] OFF recv from %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:185 disabled */
    [LA_F12] = "% Client sent data before REG_ACK completed\n",  /* SID:12 disabled */
    [LA_F19] = "%s from '%.*s': new instance(old=%u new=%u), resetting\n",  /* SID:19 disabled */
    [LA_F23] = "%s: '%.*s' cleared stale peer marker, ready for re-pair\n",  /* SID:23 disabled */
    [LA_F54] = "%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n",  /* SID:54 disabled */
    [LA_F69] = "%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n",  /* SID:69 disabled */
    [LA_F105] = "SYN0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n",  /* SID:105 disabled */
    [LA_F106] = "SYN0_ACK queue busy for '%s', drop\n",  /* SID:106 disabled */
    [LA_F107] = "SYNC_ACK queue busy for '%s', drop\n",  /* SID:107 disabled */
    [LA_F121] = "Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n",  /* SID:121 disabled */
    [LA_F132] = "[TCP] New connection from %s:%d\n",  /* SID:132 disabled */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_server_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
