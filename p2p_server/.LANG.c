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
    [LA_S6] = "Enable WebSocket service on same TCP port",  /* SID:6 */
    [LA_S7] = "NAT type detection port (0=disabled)",  /* SID:7 */
    [LA_S8] = "Received shutdown signal, exiting gracefully...",  /* SID:8 */
    [LA_S9] = "Shutting down...\n",  /* SID:9 */
    [LA_S10] = "Signaling server listen port (TCP+UDP)",  /* SID:10 */
    [LA_S11] = "Use Chinese language",  /* SID:11 */
    [LA_S12] = "WebSocket dedicated port (also enables --ws)",  /* SID:12 */
    [LA_F13] = "%.*s %s, auth_key=%llu\n",  /* SID:13 */
    [LA_F14] = "%.*s %s: accepted, inst_id=%u\n",  /* SID:14 */
    [LA_F15] = "%.*s %s: alloc client failed\n",  /* SID:15 */
    [LA_F16] = "%.*s %s: invalid instance_id=0\n",  /* SID:16 */
    [LA_F17] = "%.*s %s: realloc client\n",  /* SID:17 */
    [LA_F18] = "%s %s accepted, auth_key=%llu\n",  /* SID:18 */
    [LA_F19] = "%s %s accepted, free slot\n",  /* SID:19 */
    [LA_F20] = "%s %s accepted, seq=%u, ses_id=%u\n",  /* SID:20 */
    [LA_F21] = "%s %s bad payload(len=%u)\n",  /* SID:21 */
    [LA_F22] = "%s %s invalid auth_key=%llu\n",  /* SID:22 */
    [LA_F23] = "%s %s invalid auth_key=0\n",  /* SID:23 */
    [LA_F24] = "%s %s: 2nd-ack confirmed (ses_id=%u)\n",  /* SID:24 */
    [LA_F25] = "%s %s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n",  /* SID:25 */
    [LA_F26] = "%s %s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n",  /* SID:26 */
    [LA_F27] = "%s %s: accepted, ses_id=%u, sid=%u\n",  /* SID:27 */
    [LA_F28] = "%s %s: addr-notify confirmed (ses_id=%u)\n",  /* SID:28 */
    [LA_F29] = "%s %s: auth_key=%llu, cands=%d\n",  /* SID:29 */
    [LA_F30] = "%s %s: build session failed\n",  /* SID:30 */
    [LA_F31] = "%s %s: complete, sid=%u (ses_id=%u)\n",  /* SID:31 */
    [LA_F32] = "%s %s: confirmed, retries=%d (ses_id=%u)\n",  /* SID:32 */
    [LA_F33] = "%s %s: duplicate SYN0 with different candidates\n",  /* SID:33 */
    [LA_F34] = "%s %s: no matching pending rpc (sid=%u)\n",  /* SID:34 */
    [LA_F35] = "%s %s: paired with '%.*s'\n",  /* SID:35 */
    [LA_F36] = "%s %s: relay failed (peer unavailable), seq=%u (ses_id=%u)\n",  /* SID:36 */
    [LA_F37] = "%s %s: unavailable peer (sess_id=%u), dropped\n",  /* SID:37 */
    [LA_F38] = "%s %s: unavailable peer (sess_id=%u), rejected\n",  /* SID:38 */
    [LA_F39] = "%s -> %s %s (ses_id=%u)\n",  /* SID:39 */
    [LA_F40] = "%s -> %s %s accepted, ses_id=%u\n",  /* SID:40 */
    [LA_F41] = "%s -> %s %s resent %d/%d, sid=%u (ses_id=%u)\n",  /* SID:41 */
    [LA_F42] = "%s -> %s %s seq=%u (ses_id=%u)\n",  /* SID:42 */
    [LA_F43] = "%s -> %s %s timeout after %d retries, sid=%u (ses_id=%u)\n",  /* SID:43 */
    [LA_F44] = "%s -> %s %s: forwarded, sid=%u, msg=%u (ses_id=%u)\n",  /* SID:44 */
    [LA_F45] = "%s -> %s %s: new sid=%u last=%u (responding=%d), canceling old RPC (ses_id=%u)\n",  /* SID:45 */
    [LA_F46] = "%s -> %s %s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n",  /* SID:46 */
    [LA_F47] = "%s -> %s %s: obsolete sid=%u (last=%u), ignoring\n",  /* SID:47 */
    [LA_F48] = "%s -> %s %s: retransmit & resend ACK, sid=%u (ses_id=%u)\n",  /* SID:48 */
    [LA_F49] = "%s -> %s %s: retransmit during RSP phase, ignoring, sid=%u (ses_id=%u)\n",  /* SID:49 */
    [LA_F50] = "%s -> %s relay %s, seq=%u (ses_id=%u)\n",  /* SID:50 */
    [LA_F51] = "%s <- %s %s gave up after %d retries, sid=%u (ses_id=%u)\n",  /* SID:51 */
    [LA_F52] = "%s <- %s %s resent %d/%d, sid=%u (ses_id=%u)\n",  /* SID:52 */
    [LA_F53] = "%s <- %s %s: backward, sid=%u (ses_id=%u)\n",  /* SID:53 */
    [LA_F54] = "%s <- %s %s: no matching pending rpc (sid=%u, expected=%u)\n",  /* SID:54 */
    [LA_F55] = "%s <> %s %s gave up after %d tries, (ses_id=%u)\n",  /* SID:55 */
    [LA_F56] = "%s <> %s %s resent %d/%d, (ses_id=%u)\n",  /* SID:56 */
    [LA_F57] = "%s Unknown pkt type 0x%02x\n",  /* SID:57 */
    [LA_F58] = "%s addr changed, but first info packet was abandoned (ses_id=%u)\n",  /* SID:58 */
    [LA_F59] = "%s addr changed, defer notification until first ACK (ses_id=%u)\n",  /* SID:59 */
    [LA_F60] = "%s addr changed, deferred notifying (ses_id=%u)\n",  /* SID:60 */
    [LA_F61] = "%s addr changed, notifying '%s' (ses_id=%u)\n",  /* SID:61 */
    [LA_F62] = "%s for unknown ses_id=%u\n",  /* SID:62 */
    [LA_F63] = "%s send %s, auth_key=%llu\n",  /* SID:63 */
    [LA_F64] = "%s send %s: base_index=%u, cands=%d, ses_id=%u\n",  /* SID:64 */
    [LA_F65] = "%s send %s: cands=%d, ses_id=%u\n",  /* SID:65 */
    [LA_F66] = "%s send %s: reason=%s, ses_id=%u\n",  /* SID:66 */
    [LA_F67] = "%s send %s: ses_id=%u, peer=%s\n",  /* SID:67 */
    [LA_F68] = "%s send %s: ses_id=%u, sid=%u, flags=0x%02x, code=%u, data_len=%d, retries=%d\n",  /* SID:68 */
    [LA_F69] = "%s send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, retries=%d\n",  /* SID:69 */
    [LA_F70] = "%s send %s: ses_id=%u, sid=%u, status=%u\n",  /* SID:70 */
    [LA_F71] = "%s send %s: ses_id=%u, sid=%u\n",  /* SID:71 */
    [LA_F72] = "%s: %s <-> %s forward\n",  /* SID:72 */
    [LA_F73] = "%s: '%.*s' new REG (inst=%u)\n",  /* SID:73 */
    [LA_F74] = "%s: '%.*s' reconnected & reactive (inst=%u)\n",  /* SID:74 */
    [LA_F75] = "%s: '%.*s' reconnected & renew (inst=%u)\n",  /* SID:75 */
    [LA_F76] = "%s: '%s' -> '%s' (%zu bytes)\n",  /* SID:76 */
    [LA_F77] = "%s: '%s' -> '%s' created (id=%u, peer_zombie)\n",  /* SID:77 */
    [LA_F78] = "%s: '%s' <-> '%s' paired (ses=%u/%u)\n",  /* SID:78 */
    [LA_F79] = "%s: '%s' new REG (inst=%u)\n",  /* SID:79 */
    [LA_F80] = "%s: '%s' reconnected & reactive (inst=%u)\n",  /* SID:80 */
    [LA_F81] = "%s: '%s' reconnected & renew (inst=%u)\n",  /* SID:81 */
    [LA_F82] = "%s: '%s' ses_id=%u\n",  /* SID:82 */
    [LA_F83] = "%s: '%s' sid=%u code=%u data_len=%d\n",  /* SID:83 */
    [LA_F84] = "%s: '%s' sid=%u msg=%u data_len=%d\n",  /* SID:84 */
    [LA_F85] = "%s: '%s' unreachable, pending\n",  /* SID:85 */
    [LA_F86] = "%s: '%s'\n",  /* SID:86 */
    [LA_F87] = "%s: OOM building session '%s' -> '%s'\n",  /* SID:87 */
    [LA_F88] = "%s: alloc buf failed(OOM)\n",  /* SID:88 */
    [LA_F89] = "%s: alloc buffer failed(OOM)\n",  /* SID:89 */
    [LA_F90] = "%s: bad FIN marker=0x%02x\n",  /* SID:90 */
    [LA_F91] = "%s: bad frame len=%u\n",  /* SID:91 */
    [LA_F92] = "%s: bad payload(%u)\n",  /* SID:92 */
    [LA_F93] = "%s: bad payload(cnt=%d, len=%u, expected=%u)\n",  /* SID:93 */
    [LA_F94] = "%s: bad payload(len=%u)\n",  /* SID:94 */
    [LA_F95] = "%s: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n",  /* SID:95 */
    [LA_F96] = "%s: bad payload(sid=0)\n",  /* SID:96 */
    [LA_F97] = "%s: build session to '%s' failed(%d)\n",  /* SID:97 */
    [LA_F98] = "%s: build session to '%s' failed(OOM)",  /* SID:98 */
    [LA_F99] = "%s: busy (ses_id=%u), pending\n",  /* SID:99 */
    [LA_F100] = "%s: busy (ses_id=%u, sid=%u), pending\n",  /* SID:100 */
    [LA_F101] = "%s: close ses_id=%u\n",  /* SID:101 */
    [LA_F102] = "%s: deprecated (ses_id=%u, sid=%u), drop\n",  /* SID:102 */
    [LA_F103] = "%s: deprecated (ses_id=%u, sid=%u, last=%u), discarding\n",  /* SID:103 */
    [LA_F104] = "%s: deprecated (ses_id=%u, sid=%u, last=%u), drop\n",  /* SID:104 */
    [LA_F105] = "%s: duplicate SYN0 (ses_id=%u), resend ACK\n",  /* SID:105 */
    [LA_F106] = "%s: duplicate SYN0 (ses_id=%u), resend response\n",  /* SID:106 */
    [LA_F107] = "%s: duplicate from '%s'\n",  /* SID:107 */
    [LA_F108] = "%s: invalid REG format\n",  /* SID:108 */
    [LA_F109] = "%s: invalid instance id\n",  /* SID:109 */
    [LA_F110] = "%s: invalid peer id\n",  /* SID:110 */
    [LA_F111] = "%s: invalid remote id\n",  /* SID:111 */
    [LA_F112] = "%s: invalid sid=0\n",  /* SID:112 */
    [LA_F113] = "%s: local='%s', remote='%s', online=%d, cands=%d\n",  /* SID:113 */
    [LA_F114] = "%s: local='%s', remote='%s', online=%d, payload=%u\n",  /* SID:114 */
    [LA_F115] = "%s: missing SESSION flag, dropped\n",  /* SID:115 */
    [LA_F116] = "%s: missing payload\n",  /* SID:116 */
    [LA_F117] = "%s: peer '%s' offline, pending\n",  /* SID:117 */
    [LA_F118] = "%s: peer '%s' unreachable, pending\n",  /* SID:118 */
    [LA_F119] = "%s: peer offline, sending error resp\n",  /* SID:119 */
    [LA_F120] = "%s: pkt queue full, reply busy\n",  /* SID:120 */
    [LA_F121] = "%s: prev ALV ACK still pending, skip\n",  /* SID:121 */
    [LA_F122] = "%s: rejected for not reg(%.*s)\n",  /* SID:122 */
    [LA_F123] = "%s: rejected for not reg(%s)\n",  /* SID:123 */
    [LA_F124] = "%s: rejected for not reg\n",  /* SID:124 */
    [LA_F125] = "%s: request simultaneously for '%s'\n",  /* SID:125 */
    [LA_F126] = "%s: rpc busy (pending sid=%u)\n",  /* SID:126 */
    [LA_F127] = "%s: ses_id=%u, confirm sid=%u\n",  /* SID:127 */
    [LA_F128] = "%s: ses_id=%u, data_len=%u\n",  /* SID:128 */
    [LA_F129] = "%s: ses_id=%u, dup sid=%u, resend confirm\n",  /* SID:129 */
    [LA_F130] = "%s: ses_id=%u, peer offline, drop pkt\n",  /* SID:130 */
    [LA_F131] = "%s: ses_id=%u, peer offline, drop rsp\n",  /* SID:131 */
    [LA_F132] = "%s: ses_id=%u, peer offline, drop sid=%u\n",  /* SID:132 */
    [LA_F133] = "%s: ses_id=%u, sid=%u, cands=%d\n",  /* SID:133 */
    [LA_F134] = "%s: sid mismatch (got=%u, pending=%u), discarding\n",  /* SID:134 */
    [LA_F135] = "%s: sid=%u -> peer_sid=%u sync_sid=%u\n",  /* SID:135 */
    [LA_F136] = "%s: sid=%u -> peer_sid=%u, data_len=%u\n",  /* SID:136 */
    [LA_F137] = "%s: snprintf failed\n",  /* SID:137 */
    [LA_F138] = "%s: text frame overflow(32-bit)\n",  /* SID:138 */
    [LA_F139] = "%s: too many candidates(cnt=%d)\n",  /* SID:139 */
    [LA_F140] = "%s: unknown ses_id=%u\n",  /* SID:140 */
    [LA_F141] = "%s:%d %s: bad payload(%u)\n",  /* SID:141 */
    [LA_F142] = "%s:%d %s: data overflow (%d)\n",  /* SID:142 */
    [LA_F143] = "%s:%d %s: invalid auth_key=0\n",  /* SID:143 */
    [LA_F144] = "%s:%d %s: invalid relay flag\n",  /* SID:144 */
    [LA_F145] = "%s:%d %s: invalid seq=%u\n",  /* SID:145 */
    [LA_F146] = "%s:%d %s: invalid server-only seq=0, dropped\n",  /* SID:146 */
    [LA_F147] = "%s:%d %s: invalid sess_id=%u or sid=%u\n",  /* SID:147 */
    [LA_F148] = "%s:%d %s: unknown auth_key=%llu\n",  /* SID:148 */
    [LA_F149] = "%s:%d %s: unknown ses_id=%u, dropped\n",  /* SID:149 */
    [LA_F150] = "%s:%d %s: unknown sess_id=%u, dropped\n",  /* SID:150 */
    [LA_F151] = "%s:%d %s: unknown sess_id=%u\n",  /* SID:151 */
    [LA_F152] = "%s:%d send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n",  /* SID:152 */
    [LA_F153] = "%s:%d send %s: rejected (no slot available)\n",  /* SID:153 */
    [LA_F154] = "%session: alloc buffer failed(OOM)\n",  /* SID:154 */
    [LA_F155] = "'%s' timeout & cleanup (inactive for %.1f sec)\n",  /* SID:155 */
    [LA_F156] = ": bad payload(%u)\n",  /* SID:156 */
    [LA_F157] = ": unknown ses_id=%u type=0x%02x from '%s'\n",  /* SID:157 */
    [LA_F158] = "BIN 0x%02x: ses_id=%u peer not connected\n",  /* SID:158 */
    [LA_F159] = "BIN: unknown type=0x%02x from '%s'\n",  /* SID:159 */
    [LA_F160] = "Client closed during protocol detection (slot %d)\n",  /* SID:160 */
    [LA_F161] = "FIN: sending to peer, ses_id=%08X\n",  /* SID:161 */
    [LA_F162] = "Failed to initialize %s client\n",  /* SID:162 */
    [LA_F163] = "Failed to initialize TCP/RELAY client for slot %d\n",  /* SID:163 */
    [LA_F164] = "Failed to initialize TCP/WSS client for slot %d\n",  /* SID:164 */
    [LA_F165] = "Failed to peek client data for protocol detection (slot %d), errno=%d\n",  /* SID:165 */
    [LA_F166] = "% Goodbye!\n",  /* SID:166 */
    [LA_F167] = "Invalid port number %d (range: 1-65535)\n",  /* SID:167 */
    [LA_F168] = "Invalid probe port %d (range: 0-65535)\n",  /* SID:168 */
    [LA_F169] = "% NAT probe disabled (bind failed)\n",  /* SID:169 */
    [LA_F170] = "NAT probe socket listening on port %d\n",  /* SID:170 */
    [LA_F171] = "NAT probe: %s (port %d)\n",  /* SID:171 */
    [LA_F172] = "New %s client connected from %s:%d, assigned slot %d\n",  /* SID:172 */
    [LA_F173] = "P2P Signaling Server listening on port %d (TCP + UDP)...\n",  /* SID:173 */
    [LA_F174] = "Relay support: %s\n",  /* SID:174 */
    [LA_F175] = "Starting P2P signal server on port %d\n",  /* SID:175 */
    [LA_F176] = "WebSocket service listening on port %d\n",  /* SID:176 */
    [LA_F177] = "[%s] %s conn closed (EOF on recv)\n",  /* SID:177 */
    [LA_F178] = "[%s] %s conn closed (EOF on send)\n",  /* SID:178 */
    [LA_F179] = "[%s] %s recv failed(%d)\n",  /* SID:179 */
    [LA_F180] = "[%s] %s send failed(%d)\n",  /* SID:180 */
    [LA_F181] = "[%s] slot %d conn closed during handshake(%d) (EOF on recv)\n",  /* SID:181 */
    [LA_F182] = "[%s] slot %d conn closed during handshake(%d) (EOF on send)\n",  /* SID:182 */
    [LA_F183] = "[%s] slot %d recv failed(%d) during handshake(%d) \n",  /* SID:183 */
    [LA_F184] = "[%s] slot %d send failed(%d) during handshake(%d)\n",  /* SID:184 */
    [LA_F185] = "[CT] payload len(%u) overflow, max: %u\n",  /* SID:185 */
    [LA_F186] = "[CT] resolve payload len failed(%d)\n",  /* SID:186 */
    [LA_F187] = "[CT] send item refer invalid(%p)\n",  /* SID:187 */
    [LA_F188] = "[CT] send sess item refer invalid(%p)\n",  /* SID:188 */
    [LA_F189] = "% [T] Failed to set client socket to non-blocking mode\n",  /* SID:189 */
    [LA_F190] = "% [T] Max peers reached, rejecting connection\n",  /* SID:190 */
    [LA_F191] = "[U] %s recv %s, len=%zu\n",  /* SID:191 */
    [LA_F192] = "[U] %s recv %s, seq=%u, flags=0x%02x, len=%u\n",  /* SID:192 */
    [LA_F193] = "[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:193 */
    [LA_F194] = "[U] %s:%d recv %s, seq=%u, flags=0x%02x, len=%zu\n",  /* SID:194 */
    [LA_F195] = "[U] %s:%d send %s failed(%d)\n",  /* SID:195 */
    [LA_F196] = "[U] %s:%d send %s, len=%d\n",  /* SID:196 */
    [LA_F197] = "% [WS] CONTINUATION frame without fragmentation going\n",  /* SID:197 */
    [LA_F198] = "% [WS] Client frame missing mask\n",  /* SID:198 */
    [LA_F199] = "[WS] HTTP accept rejected(%d)\n",  /* SID:199 */
    [LA_F200] = "% [WS] Invalid UTF-8 in TEXT frame\n",  /* SID:200 */
    [LA_F201] = "% [WS] Invalid UTF-8 in close reason\n",  /* SID:201 */
    [LA_F202] = "[WS] Invalid close code %u\n",  /* SID:202 */
    [LA_F203] = "[WS] Invalid control frame: opcode=%u fin=%u hdr_len=%u\n",  /* SID:203 */
    [LA_F204] = "[WS] New %s without fragmentation end\n",  /* SID:204 */
    [LA_F205] = "% [WS] OOM in fragment reassembly\n",  /* SID:205 */
    [LA_F206] = "% [WS] OOM: cannot allocate HTTP recv buffer\n",  /* SID:206 */
    [LA_F207] = "[WS] RSV bit set in opcode %u\n",  /* SID:207 */
    [LA_F208] = "[WS] Reserved opcode %u\n",  /* SID:208 */
    [LA_F209] = "[WS] bad payload pos(%u) < hdr_sz(%u)\n",  /* SID:209 */
    [LA_F210] = "% [WS] close timeout, force closing\n",  /* SID:210 */
    [LA_F211] = "% [WS] invalid last_reason frame\n",  /* SID:211 */
    [LA_F212] = "[WS] truncate last_reason payload_len=%u to 125\n",  /* SID:212 */
    [LA_F213] = "[W] RPC timeout: sid=%u (ses_id=%u)\n",  /* SID:213 */
    [LA_F214] = "% alloc buf failed(OOM)\n",  /* SID:214 */
    [LA_F215] = "handshake<%d> sent to '%s'\n",  /* SID:215 */
    [LA_F216] = "make err(%d) resp failed(OOM)\n",  /* SID:216 */
    [LA_F217] = "% net init failed\n",  /* SID:217 */
    [LA_F218] = "probe UDP bind failed(%d)\n",  /* SID:218 */
    [LA_F219] = "select failed(%d)\n",  /* SID:219 */
    [LA_F220] = "% send failed(OOM)\n",  /* SID:220 */
    [LA_F221] = "slot %d timeout & cleanup (inactive for %.1f sec)\n",  /* SID:221 */
    [LA_F222] = "unknown msg from '%s': %.32s\n",  /* SID:222 */
    [LA_F223] = "unsupported type=%u (ses_id=%u)\n",  /* SID:223 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_server_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
