/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "alive",  /* SID:1 new */
    [LA_W2] = "bytes",  /* SID:2 new */
    [LA_W3] = "Detecting...",  /* SID:3 new */
    [LA_W4] = "Full Cone NAT",  /* SID:4 new */
    [LA_W5] = "no (cached)",  /* SID:5 new */
    [LA_W6] = "Open Internet (No NAT)",  /* SID:6 new */
    [LA_W7] = "Port Restricted Cone NAT",  /* SID:7 new */
    [LA_W8] = "PUB",  /* SID:8 new */
    [LA_W9] = "Published",  /* SID:9 new */
    [LA_W10] = "punch",  /* SID:10 new */
    [LA_W11] = "Received signal from",  /* SID:11 new */
    [LA_W12] = "Resent",  /* SID:12 new */
    [LA_W13] = "Restricted Cone NAT",  /* SID:13 new */
    [LA_W14] = "retry",  /* SID:14 new */
    [LA_W15] = "SUB",  /* SID:15 new */
    [LA_W16] = "Symmetric NAT (port-random)",  /* SID:16 new */
    [LA_W17] = "Timeout (no response)",  /* SID:17 new */
    [LA_W18] = "UDP Blocked (STUN unreachable)",  /* SID:18 new */
    [LA_W19] = "Undetectable (no STUN/probe configured)",  /* SID:19 new */
    [LA_W20] = "Unknown",  /* SID:20 new */
    [LA_W21] = "Waiting for incoming offer from any peer",  /* SID:21 new */
    [LA_W22] = "yes",  /* SID:22 new */
    [LA_S23] = "%s: address exchange failed: peer OFFLINE",  /* SID:23 new */
    [LA_S24] = "%s: address exchange success, sending UDP probe",  /* SID:24 new */
    [LA_S25] = "%s: already running, cannot trigger again",  /* SID:25 new */
    [LA_S26] = "%s: peer is OFFLINE",  /* SID:26 new */
    [LA_S27] = "%s: peer is online, waiting echo",  /* SID:27 new */
    [LA_S28] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:28 new */
    [LA_S29] = "%s: TURN allocated, starting address exchange",  /* SID:29 new */
    [LA_S30] = "[SCTP] association established",  /* SID:30 new */
    [LA_S31] = "[SCTP] usrsctp initialized, connecting...",  /* SID:31 new */
    [LA_S32] = "[SCTP] usrsctp_socket failed",  /* SID:32 new */
    [LA_S33] = "Channel ID validation failed",  /* SID:33 new */
    [LA_S34] = "Detecting local network addresses",  /* SID:34 new */
    [LA_S35] = "Gist GET failed",  /* SID:35 new */
    [LA_S36] = "Invalid channel_id format (security risk)",  /* SID:36 new */
    [LA_S37] = "Out of memory",  /* SID:37 new */
    [LA_S38] = "Push host cand<%s:%d> failed(OOM)\n",  /* SID:38 new */
    [LA_S39] = "Push local cand<%s:%d> failed(OOM)\n",  /* SID:39 new */
    [LA_S40] = "Push remote cand<%s:%d> failed(OOM)\n",  /* SID:40 new */
    [LA_F41] = "  ... and %d more pairs",  /* SID:41 new */
    [LA_F42] = "  [%d] %s/%d",  /* SID:42 new */
    [LA_F43] = "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx",  /* SID:43 new */
    [LA_F44] = "%s '%s' (%u %s)",  /* SID:44 new */
    [LA_F45] = "%s NOTIFY: accepted\n",  /* SID:45 new */
    [LA_F46] = "%s NOTIFY: ignored old notify base=%u (current=%u)\n",  /* SID:46 new */
    [LA_F47] = "%s NOTIFY: ignored srflx addr update due to instument\n",  /* SID:47 new */
    [LA_F48] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n",  /* SID:48 new */
    [LA_F49] = "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n",  /* SID:49 new */
    [LA_F50] = "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates",  /* SID:50 new */
    [LA_F51] = "%s resent, %d/%d\n",  /* SID:51 new */
    [LA_F52] = "%s sent to %s:%d",  /* SID:52 new */
    [LA_F53] = "%s sent to %s:%d (writable), echo_seq=%u",  /* SID:53 new */
    [LA_F54] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:54 new */
    [LA_F55] = "%s sent to %s:%d, echo_seq=0, path=%d",  /* SID:55 new */
    [LA_F56] = "%s sent to %s:%d, seq=0, path=%d",  /* SID:56 new */
    [LA_F57] = "%s sent via best path[%d] to %s:%d, echo_seq=%u",  /* SID:57 new */
    [LA_F58] = "%s sent, inst_id=%u, cands=%d\n",  /* SID:58 new */
    [LA_F59] = "%s sent, inst_id=%u\n",  /* SID:59 new */
    [LA_F60] = "%s sent, seq=%u\n",  /* SID:60 new */
    [LA_F61] = "%s sent, sid=%u, msg=%u, size=%d\n",  /* SID:61 new */
    [LA_F62] = "%s sent, total=%d (ses_id=%llu)\n",  /* SID:62 new */
    [LA_F63] = "%s seq=0: accepted cand_cnt=%d\n",  /* SID:63 new */
    [LA_F64] = "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n",  /* SID:64 new */
    [LA_F65] = "%s skipped: session_id=0\n",  /* SID:65 new */
    [LA_F66] = "%s, retry remaining candidates and FIN to peer\n",  /* SID:66 new */
    [LA_F67] = "%s, sent on %s\n",  /* SID:67 new */
    [LA_F68] = "%s: %s timeout after %d retries (sid=%u)\n",  /* SID:68 new */
    [LA_F69] = "%s: CONN timeout after %llums",  /* SID:69 new */
    [LA_F127] = "%s: CONNECTED → LOST (no response %llums)\n",  /* SID:127 new */
    [LA_F71] = "%s: CONNECTING → CLOSED (timeout, no relay)",  /* SID:71 new */
    [LA_F72] = "%s: CONNECTING → CONNECTED (recv CONN)",  /* SID:72 new */
    [LA_F73] = "%s: CONNECTING → CONNECTED (recv CONN_ACK)",  /* SID:73 new */
    [LA_F74] = "%s: CONNECTING → CONNECTED (recv DATA)",  /* SID:74 new */
    [LA_F70] = "%s: CONNECTING → RELAY (timeout, signaling)",  /* SID:70 new */
    [LA_F138] = "%s: PUNCHING → CLOSED (timeout %llums, no relay)",  /* SID:138 new */
    [LA_F103] = "%s: PUNCHING → CONNECTED (earlier CONN)",  /* SID:103 new */
    [LA_F102] = "%s: PUNCHING → CONNECTING (%s)",  /* SID:102 new */
    [LA_F182] = "%s: PUNCHING → RELAY (timeout %llums, signaling)",  /* SID:182 new */
    [LA_F75] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n",  /* SID:75 new */
    [LA_F76] = "%s: Peer addr changed -> %s:%d, retrying punch\n",  /* SID:76 new */
    [LA_F77] = "%s: RELAY → CONNECTED (direct path recovered)",  /* SID:77 new */
    [LA_F78] = "%s: RPC complete (sid=%u)\n",  /* SID:78 new */
    [LA_F79] = "%s: RPC fail due to peer offline (sid=%u)\n",  /* SID:79 new */
    [LA_F80] = "%s: RPC fail due to relay timeout (sid=%u)\n",  /* SID:80 new */
    [LA_F81] = "%s: RPC finished (sid=%u)\n",  /* SID:81 new */
    [LA_F82] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:82 new */
    [LA_F83] = "%s: TURN allocation failed: ret=%d",  /* SID:83 new */
    [LA_F84] = "%s: TURN allocation request sent",  /* SID:84 new */
    [LA_F85] = "%s: UDP timeout, retry %d/%d",  /* SID:85 new */
    [LA_F86] = "%s: UDP timeout: peer not responding",  /* SID:86 new */
    [LA_F87] = "%s: accepted",  /* SID:87 new */
    [LA_F88] = "%s: accepted (ses_id=%llu)\n",  /* SID:88 new */
    [LA_F89] = "%s: accepted (sid=%u)\n",  /* SID:89 new */
    [LA_F90] = "%s: accepted for ack_seq=%u\n",  /* SID:90 new */
    [LA_F92] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n",  /* SID:92 new */
    [LA_F93] = "%s: accepted sid=%u, msg=%u\n",  /* SID:93 new */
    [LA_F94] = "%s: accepted, probe_mapped=%s:%d\n",  /* SID:94 new */
    [LA_F95] = "%s: accepted, public=%s:%d ses_id=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n",  /* SID:95 new */
    [LA_F96] = "%s: accepted, waiting for response (sid=%u)\n",  /* SID:96 new */
    [LA_F97] = "%s: accepted\n",  /* SID:97 new */
    [LA_F99] = "%s: bad payload(len=%d cand_cnt=%d)\n",  /* SID:99 new */
    [LA_F100] = "%s: bad payload(len=%d)\n",  /* SID:100 new */
    [LA_F101] = "%s: bad payload(len=%d, need >=8)\n",  /* SID:101 new */
    [LA_F98] = "%s: batch punch skip (state=%d, use trickle)",  /* SID:98 new */
    [LA_F173] = "%s: batch punch start (%d cands)",  /* SID:173 new */
    [LA_F126] = "%s: batch punch: no cand, wait trickle",  /* SID:126 new */
    [LA_F104] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n",  /* SID:104 new */
    [LA_F105] = "%s: discovered prflx cand<%s:%d>[%d]",  /* SID:105 new */
    [LA_F106] = "%s: duplicate remote cand<%s:%d> from signaling, skipped\n",  /* SID:106 new */
    [LA_F107] = "%s: duplicate request ignored (sid=%u, already processing)\n",  /* SID:107 new */
    [LA_F108] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n",  /* SID:108 new */
    [LA_F109] = "%s: entered, %s arrived after REGISTERED\n",  /* SID:109 new */
    [LA_F110] = "%s: exchange timeout, retry %d/%d",  /* SID:110 new */
    [LA_F111] = "%s: exchange timeout: peer not responding",  /* SID:111 new */
    [LA_F112] = "%s: ignored for duplicated seq=%u, already acked\n",  /* SID:112 new */
    [LA_F113] = "%s: ignored for seq=%u (expect=%d)\n",  /* SID:113 new */
    [LA_F114] = "%s: ignored for ses_id=%llu (local ses_id=%llu)\n",  /* SID:114 new */
    [LA_F115] = "%s: ignored for sid=%u (current sid=%u)\n",  /* SID:115 new */
    [LA_F116] = "%s: ignored in invalid state=%d\n",  /* SID:116 new */
    [LA_F117] = "%s: ignored in state=%d\n",  /* SID:117 new */
    [LA_F118] = "%s: invalid ack_seq=%u\n",  /* SID:118 new */
    [LA_F119] = "%s: invalid cand idx: %d (count: %d)",  /* SID:119 new */
    [LA_F120] = "%s: invalid for non-relay req\n",  /* SID:120 new */
    [LA_F121] = "%s: invalid payload len=%d (need 6)",  /* SID:121 new */
    [LA_F122] = "%s: invalid seq=%u\n",  /* SID:122 new */
    [LA_F123] = "%s: invalid session_id=0\n",  /* SID:123 new */
    [LA_F124] = "%s: keep-alive sent (%d cands)",  /* SID:124 new */
    [LA_F125] = "%s: new request (sid=%u) overrides pending request (sid=%u)\n",  /* SID:125 new */
    [LA_F128] = "%s: no writable path available",  /* SID:128 new */
    [LA_F129] = "%s: not connected, cannot send FIN",  /* SID:129 new */
    [LA_F130] = "%s: not connected, unexpected ACK",  /* SID:130 new */
    [LA_F131] = "%s: old request ignored (sid=%u <= last_sid=%u)\n",  /* SID:131 new */
    [LA_F162] = "%s: path rx UP (%s:%d)",  /* SID:162 new */
    [LA_F190] = "%s: path tx UP (echo seq=%u)",  /* SID:190 new */
    [LA_F132] = "%s: path[%d] UP (%s:%d)",  /* SID:132 new */
    [LA_F152] = "%s: path[%d] relay UP",  /* SID:152 new */
    [LA_F133] = "%s: peer disconnected (ses_id=%llu), reset to REGISTERED\n",  /* SID:133 new */
    [LA_F134] = "%s: peer online, proceeding to ICE\n",  /* SID:134 new */
    [LA_F135] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:135 new */
    [LA_F136] = "%s: peer_info0 srflx cand[%d]<%s:%d>%s\n",  /* SID:136 new */
    [LA_F140] = "%s: punch cand[%d] %s:%d (%s)",  /* SID:140 new */
    [LA_F137] = "%s: punch remote cand[%d]<%s:%d> failed\n",  /* SID:137 new */
    [LA_F139] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:139 new */
    [LA_F142] = "%s: push remote cand<%s:%d> failed(OOM)",  /* SID:142 new */
    [LA_F143] = "%s: reaching alloc OOM",  /* SID:143 new */
    [LA_F144] = "%s: reaching broadcast to %d cand(s), seq=%u",  /* SID:144 new */
    [LA_F149] = "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u",  /* SID:149 new */
    [LA_F145] = "%s: reaching enqueued: cand[%d], seq=%u, priority=%u",  /* SID:145 new */
    [LA_F146] = "%s: reaching relay via signaling FAILED (ret=%d), seq=%u",  /* SID:146 new */
    [LA_F147] = "%s: reaching relay via signaling SUCCESS, seq=%u",  /* SID:147 new */
    [LA_F148] = "%s: reaching updated: cand[%d], seq=%u->%u",  /* SID:148 new */
    [LA_F151] = "%s: recorded peer conn_seq=%u for future CONN_ACK",  /* SID:151 new */
    [LA_F91] = "%s: recv from cand[%d]",  /* SID:91 new */
    [LA_F153] = "%s: remote %s cand[%d]<%s:%d> accepted\n",  /* SID:153 new */
    [LA_F154] = "%s: remote %s cand[%d]<%s:%d>, ignored due to instrument\n",  /* SID:154 new */
    [LA_F156] = "%s: renew session due to session_id changed by info0 (local=%llu pkt=%llu)\n",  /* SID:156 new */
    [LA_F157] = "%s: restarting periodic check",  /* SID:157 new */
    [LA_F158] = "%s: retry(%d/%d) probe\n",  /* SID:158 new */
    [LA_F159] = "%s: retry(%d/%d) req (sid=%u)\n",  /* SID:159 new */
    [LA_F160] = "%s: retry(%d/%d) resp (sid=%u)\n",  /* SID:160 new */
    [LA_F161] = "%s: retry, (attempt %d/%d)\n",  /* SID:161 new */
    [LA_F163] = "%s: send failed(%d)",  /* SID:163 new */
    [LA_F164] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:164 new */
    [LA_F165] = "%s: sent, sid=%u, code=%u, size=%d\n",  /* SID:165 new */
    [LA_F166] = "%s: session mismatch(local=%llu ack=%llu)\n",  /* SID:166 new */
    [LA_F167] = "%s: session mismatch(local=%llu pkt=%llu)\n",  /* SID:167 new */
    [LA_F168] = "%s: session mismatch(local=%llu, pkt=%llu)\n",  /* SID:168 new */
    [LA_F169] = "%s: session validated, len=%d (ses_id=%llu)\n",  /* SID:169 new */
    [LA_F170] = "%s: session_id mismatch (recv=%llu, expect=%llu)\n",  /* SID:170 new */
    [LA_F172] = "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n",  /* SID:172 new */
    [LA_F175] = "%s: started, sending first probe\n",  /* SID:175 new */
    [LA_F176] = "%s: status error(%d)\n",  /* SID:176 new */
    [LA_F177] = "%s: sync complete (ses_id=%llu)\n",  /* SID:177 new */
    [LA_F178] = "%s: sync complete (ses_id=%llu, mask=0x%04x)\n",  /* SID:178 new */
    [LA_F179] = "%s: target=%s:%d",  /* SID:179 new */
    [LA_F180] = "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n",  /* SID:180 new */
    [LA_F181] = "%s: timeout after %d retries , type unknown\n",  /* SID:181 new */
    [LA_F183] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:183 new */
    [LA_F184] = "%s: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:184 new */
    [LA_F185] = "%s: timeout, peer did not respond",  /* SID:185 new */
    [LA_F186] = "%s: timeout, retry %d/%d",  /* SID:186 new */
    [LA_F174] = "%s: trickle punch start",  /* SID:174 new */
    [LA_F171] = "%s: trickle punch: set peer_addr",  /* SID:171 new */
    [LA_F187] = "%s: trickled %d cand(s), seq=%u (ses_id=%llu)\n",  /* SID:187 new */
    [LA_F188] = "%s: triggered via COMPACT msg echo",  /* SID:188 new */
    [LA_F189] = "%s: triggered via RELAY TUNE echo",  /* SID:189 new */
    [LA_F191] = "%s: unexpected ack_seq=%u mask=0x%04x\n",  /* SID:191 new */
    [LA_F192] = "%s: unexpected non-srflx cand in peer_info0, treating as srflx\n",  /* SID:192 new */
    [LA_F193] = "%s: unexpected remote cand type %d, skipped\n",  /* SID:193 new */
    [LA_F150] = "%s: → CLOSED (recv FIN)",  /* SID:150 new */
    [LA_F194] = "%s:%04d: %s",  /* SID:194 new */
    [LA_F195] = "%s_ACK sent to %s:%d (try), echo_seq=%u",  /* SID:195 new */
    [LA_F196] = "%s_ACK sent, seq=%u (ses_id=%llu)\n",  /* SID:196 new */
    [LA_F197] = "%s_ACK sent, sid=%u\n",  /* SID:197 new */
    [LA_F198] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:198 new */
    [LA_F199] = "ACK: invalid payload length %d, expected at least 6",  /* SID:199 new */
    [LA_F200] = "ACK: protocol mismatch, trans=%s has on_packet but received P2P_PKT_ACK",  /* SID:200 new */
    [LA_F201] = "Added Remote Candidate: %d -> %s:%d",  /* SID:201 new */
    [LA_F202] = "% Answer already present, skipping offer re-publish",  /* SID:202 new */
    [LA_F203] = "Append Host candidate: %s:%d",  /* SID:203 new */
    [LA_F204] = "Attempting Simultaneous Open to %s:%d",  /* SID:204 new */
    [LA_F205] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:205 new */
    [LA_F206] = "% BIO_new failed",  /* SID:206 new */
    [LA_F207] = "% Base64 decode failed",  /* SID:207 new */
    [LA_F208] = "% Bind failed",  /* SID:208 new */
    [LA_F209] = "Bind failed to %d, port busy, trying random port",  /* SID:209 new */
    [LA_F210] = "Bound to :%d",  /* SID:210 new */
    [LA_F211] = "% COMPACT mode requires explicit remote_peer_id",  /* SID:211 new */
    [LA_F212] = "COMPACT relay payload too large: %d",  /* SID:212 new */
    [LA_F213] = "COMPACT relay send failed: type=0x%02x, ret=%d",  /* SID:213 new */
    [LA_F214] = "COMPACT relay: type=0x%02x, seq=%u (session_id=%llu)",  /* SID:214 new */
    [LA_F215] = "% Close P2P UDP socket",  /* SID:215 new */
    [LA_F216] = "% Closing TCP connection to RELAY signaling server",  /* SID:216 new */
    [LA_F217] = "Connect to COMPACT signaling server failed(%d)",  /* SID:217 new */
    [LA_F218] = "Connect to RELAY signaling server failed(%d)",  /* SID:218 new */
    [LA_F219] = "Connected to server %s:%d as '%s'",  /* SID:219 new */
    [LA_F220] = "Connecting to RELAY signaling server at %s:%d",  /* SID:220 new */
    [LA_F221] = "% Connection closed by server",  /* SID:221 new */
    [LA_F222] = "% Connection closed while discarding",  /* SID:222 new */
    [LA_F223] = "% Connection closed while reading payload",  /* SID:223 new */
    [LA_F224] = "% Connection closed while reading sender",  /* SID:224 new */
    [LA_F225] = "Connectivity checks timed out (sent %d rounds), giving up",  /* SID:225 new */
    [LA_F226] = "Crypto layer '%s' init failed, continuing without encryption",  /* SID:226 new */
    [LA_F227] = "% DTLS (MbedTLS) requested but library not linked",  /* SID:227 new */
    [LA_F228] = "% DTLS handshake complete (MbedTLS)",  /* SID:228 new */
    [LA_F229] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:229 new */
    [LA_F230] = "Detect local network interfaces failed(%d)",  /* SID:230 new */
    [LA_F231] = "Detection completed %s",  /* SID:231 new */
    [LA_F232] = "Discarded %d bytes payload of message type %d",  /* SID:232 new */
    [LA_F233] = "Duplicate remote cand<%s:%d> from signaling, skipped",  /* SID:233 new */
    [LA_F234] = "Duplicate remote candidate<%s:%d> from signaling, skipped",  /* SID:234 new */
    [LA_F235] = "Failed to allocate %u bytes",  /* SID:235 new */
    [LA_F236] = "% Failed to allocate ACK payload buffer",  /* SID:236 new */
    [LA_F237] = "% Failed to allocate DTLS context",  /* SID:237 new */
    [LA_F238] = "% Failed to allocate OpenSSL context",  /* SID:238 new */
    [LA_F239] = "% Failed to allocate discard buffer, closing connection",  /* SID:239 new */
    [LA_F240] = "% Failed to allocate memory for candidate lists",  /* SID:240 new */
    [LA_F241] = "% Failed to allocate memory for session",  /* SID:241 new */
    [LA_F242] = "% Failed to build STUN request",  /* SID:242 new */
    [LA_F243] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:243 new */
    [LA_F244] = "Failed to reserve remote candidates (base=%u cnt=%d)\n",  /* SID:244 new */
    [LA_F245] = "Failed to reserve remote candidates (cnt=%d)\n",  /* SID:245 new */
    [LA_F246] = "% Failed to reserve remote candidates (cnt=1)\n",  /* SID:246 new */
    [LA_F247] = "Failed to resolve STUN server %s",  /* SID:247 new */
    [LA_F248] = "Failed to resolve TURN server: %s",  /* SID:248 new */
    [LA_F249] = "% Failed to send header",  /* SID:249 new */
    [LA_F250] = "% Failed to send payload",  /* SID:250 new */
    [LA_F251] = "% Failed to send punch packet for new peer addr\n",  /* SID:251 new */
    [LA_F252] = "% Failed to send target name",  /* SID:252 new */
    [LA_F253] = "Field %s is empty or too short",  /* SID:253 new */
    [LA_F254] = "First offer, resetting ICE and clearing %d stale candidates",  /* SID:254 new */
    [LA_F255] = "Formed check list with %d candidate pairs",  /* SID:255 new */
    [LA_F256] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:256 new */
    [LA_F257] = "Gathered host cand<%s:%d> (priority=0x%08x)",  /* SID:257 new */
    [LA_F258] = "% Handshake complete",  /* SID:258 new */
    [LA_F259] = "Handshake failed: %s (-0x%04x)",  /* SID:259 new */
    [LA_F260] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:260 new */
    [LA_F261] = "Initialize network subsystem failed(%d)",  /* SID:261 new */
    [LA_F262] = "Initialize signaling mode: %d",  /* SID:262 new */
    [LA_F263] = "Initialized: %s",  /* SID:263 new */
    [LA_F264] = "Invalid magic 0x%x (expected 0x%x), resetting",  /* SID:264 new */
    [LA_F265] = "Invalid read state %d, resetting",  /* SID:265 new */
    [LA_F266] = "% Invalid signaling mode in configuration",  /* SID:266 new */
    [LA_F267] = "Local address detection done: %d address(es)",  /* SID:267 new */
    [LA_F268] = "Marked old path (idx=%d) as FAILED due to addr change\n",  /* SID:268 new */
    [LA_F269] = "% NAT connected but no available path in path manager",  /* SID:269 new */
    [LA_F270] = "% No advanced transport layer enabled, using simple reliable layer",  /* SID:270 new */
    [LA_F271] = "% No auth_key provided, using default key (insecure)",  /* SID:271 new */
    [LA_F272] = "Nomination successful! Using! Using %s path %s:%d%s",  /* SID:272 new */
    [LA_F273] = "Open P2P UDP socket on port %d",  /* SID:273 new */
    [LA_F274] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:274 new */
    [LA_F275] = "% OpenSSL requested but library not linked",  /* SID:275 new */
    [LA_F276] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:276 new */
    [LA_F277] = "% P2P connected, closing signaling TCP connection",  /* SID:277 new */
    [LA_F278] = "PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u\n",  /* SID:278 new */
    [LA_F279] = "% PEER_INFO(trickle): seq overflow, cannot trickle more\n",  /* SID:279 new */
    [LA_F280] = "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:280 new */
    [LA_F281] = "% PUBSUB (SUB): waiting for offer from any peer",  /* SID:281 new */
    [LA_F282] = "% PUBSUB mode requires gh_token and gist_id",  /* SID:282 new */
    [LA_F283] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:283 new */
    [LA_F284] = "Packet too large len=%d max=%d",  /* SID:284 new */
    [LA_F285] = "Passive peer learned remote ID '%s' from OFFER",  /* SID:285 new */
    [LA_F286] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:286 new */
    [LA_F287] = "% Path switch debounced, waiting for stability",  /* SID:287 new */
    [LA_F288] = "Path switched to better route (idx=%d)",  /* SID:288 new */
    [LA_F289] = "Peer '%s' is now online (FORWARD received), resuming",  /* SID:289 new */
    [LA_F290] = "Peer offline, cached %d candidates",  /* SID:290 new */
    [LA_F291] = "Peer online, forwarded %d candidates",  /* SID:291 new */
    [LA_F292] = "Processing (role=%s)",  /* SID:292 new */
    [LA_F293] = "% PseudoTCP enabled as transport layer",  /* SID:293 new */
    [LA_F294] = "Push prflx candidate<%s:%d> failed(OOM)",  /* SID:294 new */
    [LA_F295] = "Push remote candidate<%s:%d> (type=%d) failed(OOM)",  /* SID:295 new */
    [LA_F296] = "REGISTERED: peer=%s\n",  /* SID:296 new */
    [LA_F297] = "% RELAY/COMPACT mode requires server_host",  /* SID:297 new */
    [LA_F298] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:298 new */
    [LA_F299] = "Received ACK (status=%d, candidates_acked=%d)",  /* SID:299 new */
    [LA_F300] = "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:300 new */
    [LA_F301] = "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d",  /* SID:301 new */
    [LA_F302] = "Received UNKNOWN pkt type: 0x%02X",  /* SID:302 new */
    [LA_F303] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:303 new */
    [LA_F304] = "Received valid signal from '%s'",  /* SID:304 new */
    [LA_F305] = "Recv %s pkt from %s:%d",  /* SID:305 new */
    [LA_F306] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:306 new */
    [LA_F307] = "Recv %s pkt from %s:%d seq=%u",  /* SID:307 new */
    [LA_F308] = "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:308 new */
    [LA_F309] = "Recv %s pkt from %s:%d, seq=%u, len=%d",  /* SID:309 new */
    [LA_F310] = "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)",  /* SID:310 new */
    [LA_F311] = "Recv New Remote Candidate<%s:%d> (type=%d)",  /* SID:311 new */
    [LA_F312] = "Register to COMPACT signaling server at %s:%d",  /* SID:312 new */
    [LA_F313] = "Reliable transport initialized rto=%d win=%d",  /* SID:313 new */
    [LA_F314] = "Requested Relay Candidate from %s",  /* SID:314 new */
    [LA_F315] = "Requested Relay Candidate from TURN %s",  /* SID:315 new */
    [LA_F316] = "Requested Srflx Candidate from %s",  /* SID:316 new */
    [LA_F317] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:317 new */
    [LA_F318] = "% SCTP (usrsctp) requested but library not linked",  /* SID:318 new */
    [LA_F319] = "% SIGNALING path active but relay function not available",  /* SID:319 new */
    [LA_F320] = "% SIGNALING path enabled (server supports relay)\n",  /* SID:320 new */
    [LA_F321] = "% SSL_CTX_new failed",  /* SID:321 new */
    [LA_F322] = "% SSL_new failed",  /* SID:322 new */
    [LA_F323] = "Send offer to RELAY signaling server failed(%d)",  /* SID:323 new */
    [LA_F324] = "Send window full, dropping packet send_count=%d",  /* SID:324 new */
    [LA_F325] = "Sending Allocate Request to %s:%d",  /* SID:325 new */
    [LA_F326] = "% Sending FIN packet to peer before closing",  /* SID:326 new */
    [LA_F327] = "Sending Test I to %s:%d (len=%d)",  /* SID:327 new */
    [LA_F328] = "% Sending UNREGISTER packet to COMPACT signaling server",  /* SID:328 new */
    [LA_F329] = "Sent answer to '%s'",  /* SID:329 new */
    [LA_F330] = "Sent answer to '%s' (%d bytes)",  /* SID:330 new */
    [LA_F331] = "Sent connect request to '%s' (%d bytes)",  /* SID:331 new */
    [LA_F332] = "Sent initial offer(%d) to %s)",  /* SID:332 new */
    [LA_F333] = "% Signal payload deserialization failed",  /* SID:333 new */
    [LA_F334] = "% Skipping Host Candidate gathering due to instrumentation",  /* SID:334 new */
    [LA_F335] = "% Skipping local Host candidates due to instrument",  /* SID:335 new */
    [LA_F336] = "Start internal thread failed(%d)",  /* SID:336 new */
    [LA_F337] = "% Starting internal thread",  /* SID:337 new */
    [LA_F338] = "% State: CONNECTED → LOST (no relay)",  /* SID:338 new */
    [LA_F339] = "% State: CONNECTED → RELAY (path lost)",  /* SID:339 new */
    [LA_F340] = "% State: LOST → CONNECTED (legacy path)",  /* SID:340 new */
    [LA_F341] = "State: LOST → CONNECTED, path=PUNCH[%d]",  /* SID:341 new */
    [LA_F342] = "State: RELAY → CONNECTED, path=PUNCH[%d]",  /* SID:342 new */
    [LA_F343] = "State: → CONNECTED, path[%d]",  /* SID:343 new */
    [LA_F344] = "% State: → ERROR (punch timeout, no relay available)",  /* SID:344 new */
    [LA_F345] = "% State: → PUNCHING",  /* SID:345 new */
    [LA_F348] = "State: → RELAY, path[%d] (signaling)",  /* SID:348 new */
    [LA_F349] = "% Stopping internal thread",  /* SID:349 new */
    [LA_F350] = "% Storage full, waiting for peer to come online",  /* SID:350 new */
    [LA_F351] = "% Synced path after failover",  /* SID:351 new */
    [LA_F352] = "TURN 401 Unauthorized (realm=%s), authenticating...",  /* SID:352 new */
    [LA_F353] = "TURN Allocate failed with error %d",  /* SID:353 new */
    [LA_F354] = "TURN Allocated relay %s:%u (lifetime=%us)",  /* SID:354 new */
    [LA_F355] = "TURN CreatePermission failed (error=%d)",  /* SID:355 new */
    [LA_F356] = "TURN CreatePermission for %s",  /* SID:356 new */
    [LA_F357] = "TURN Data Indication from %s:%u (%d bytes)",  /* SID:357 new */
    [LA_F358] = "TURN Refresh failed (error=%d)",  /* SID:358 new */
    [LA_F359] = "TURN Refresh ok (lifetime=%us)",  /* SID:359 new */
    [LA_F360] = "% TURN auth required but no credentials configured",  /* SID:360 new */
    [LA_F361] = "Test I: Mapped address: %s:%d",  /* SID:361 new */
    [LA_F362] = "% Test I: Timeout",  /* SID:362 new */
    [LA_F363] = "Test II: Success! Detection completed %s",  /* SID:363 new */
    [LA_F364] = "% Test II: Timeout (need Test III)",  /* SID:364 new */
    [LA_F365] = "Test III: Success! Detection completed %s",  /* SID:365 new */
    [LA_F366] = "% Test III: Timeout",  /* SID:366 new */
    [LA_F367] = "Transport layer '%s' init failed, falling back to simple reliable",  /* SID:367 new */
    [LA_F368] = "UDP hole-punch probing remote candidates (%d candidates)",  /* SID:368 new */
    [LA_F369] = "UDP hole-punch probing remote candidates round %d/%d",  /* SID:369 new */
    [LA_F370] = "Unknown ACK status %d",  /* SID:370 new */
    [LA_F371] = "Unknown signaling mode: %d",  /* SID:371 new */
    [LA_F372] = "Updating Gist field '%s'...",  /* SID:372 new */
    [LA_F373] = "Waiting for peer '%s' timed out (%dms), giving up",  /* SID:373 new */
    [LA_F374] = "[MbedTLS] DTLS role: %s (mode=%s)",  /* SID:374 new */
    [LA_F375] = "% [OpenSSL] DTLS handshake completed",  /* SID:375 new */
    [LA_F376] = "[OpenSSL] DTLS role: %s (mode=%s)",  /* SID:376 new */
    [LA_F377] = "[SCTP] association lost/shutdown (state=%u)",  /* SID:377 new */
    [LA_F378] = "[SCTP] bind failed: %s",  /* SID:378 new */
    [LA_F379] = "[SCTP] connect failed: %s",  /* SID:379 new */
    [LA_F380] = "[SCTP] sendv failed: %s",  /* SID:380 new */
    [LA_F381] = "[SIGNALING] Failed to send candidates, will retry (ret=%d)",  /* SID:381 new */
    [LA_F382] = "[SIGNALING] Sent candidates (cached, peer offline) %d to %s",  /* SID:382 new */
    [LA_F383] = "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)",  /* SID:383 new */
    [LA_F384] = "% [SIGNALING] Server storage full, waiting for peer to come online",  /* SID:384 new */
    [LA_F385] = "[Trickle] Immediately probing new candidate %s:%d",  /* SID:385 new */
    [LA_F386] = "[Trickle] Sent 1 candidate to %s (online=%s)",  /* SID:386 new */
    [LA_F387] = "% [Trickle] TCP not connected, skipping single candidate send",  /* SID:387 new */
    [LA_F388] = "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()",  /* SID:388 new */
    [LA_F389] = "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n",  /* SID:389 new */
    [LA_F390] = "[UDP] %s recv from %s:%d, len=%d\n",  /* SID:390 new */
    [LA_F391] = "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:391 new */
    [LA_F392] = "[UDP] %s recv from %s:%d, seq=%u, len=%d\n",  /* SID:392 new */
    [LA_F393] = "[UDP] %s recv from %s:%d\n",  /* SID:393 new */
    [LA_F394] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:394 new */
    [LA_F395] = "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n",  /* SID:395 new */
    [LA_F396] = "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:396 new */
    [LA_F397] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:397 new */
    [LA_F398] = "[UDP] %s_ACK send to %s:%d failed(%d)\n",  /* SID:398 new */
    [LA_F399] = "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n",  /* SID:399 new */
    [LA_F400] = "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:400 new */
    [LA_F401] = "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:401 new */
    [LA_F402] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:402 new */
    [LA_F403] = "% connection closed by peer",  /* SID:403 new */
    [LA_F404] = "ctr_drbg_seed failed: -0x%x",  /* SID:404 new */
    [LA_F405] = "% p2p_ice_send_local_candidate called in non-RELAY mode",  /* SID:405 new */
    [LA_F406] = "recv error %d",  /* SID:406 new */
    [LA_F407] = "recv error %d while discarding",  /* SID:407 new */
    [LA_F408] = "recv error %d while reading payload",  /* SID:408 new */
    [LA_F409] = "recv error %d while reading sender",  /* SID:409 new */
    [LA_F410] = "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u",  /* SID:410 new */
    [LA_F411] = "retry seq=%u retx=%d rto=%d",  /* SID:411 new */
    [LA_F412] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:412 new */
    [LA_F413] = "ssl_config_defaults failed: -0x%x",  /* SID:413 new */
    [LA_F414] = "ssl_setup failed: -0x%x",  /* SID:414 new */
    [LA_F415] = "starting NAT punch(Host candidate %d)",  /* SID:415 new */
    [LA_F416] = "transport send_data failed, %d bytes dropped",  /* SID:416 new */
    [LA_F417] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:417 new */
    [LA_F418] = "% ✗ Add Srflx candidate failed(OOM)",  /* SID:418 new */
    [LA_F141] = "%s: punching remote [%d]cand<%s:%d> (type: %s)",  /* SID:141 new */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
