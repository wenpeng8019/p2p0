/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "alive",  /* SID:1 */
    [LA_W2] = "Detecting...",  /* SID:2 */
    [LA_W3] = "Full Cone NAT",  /* SID:3 */
    [LA_W4] = "Open Internet (No NAT)",  /* SID:4 */
    [LA_W5] = "Port Restricted Cone NAT",  /* SID:5 */
    [LA_W6] = "PUB",  /* SID:6 */
    [LA_W7] = "punch",  /* SID:7 */
    [LA_W8] = "Restricted Cone NAT",  /* SID:8 */
    [LA_W9] = "retry",  /* SID:9 */
    [LA_W10] = "SUB",  /* SID:10 */
    [LA_W11] = "Symmetric NAT (port-random)",  /* SID:11 */
    [LA_W12] = "Timeout (no response)",  /* SID:12 */
    [LA_W13] = "UDP Blocked (STUN unreachable)",  /* SID:13 */
    [LA_W14] = "Undetectable (no STUN/probe configured)",  /* SID:14 */
    [LA_W15] = "Unknown",  /* SID:15 */
    [LA_S16] = "%s: address exchange failed: peer OFFLINE",  /* SID:16 */
    [LA_S17] = "%s: address exchange success, sending UDP probe",  /* SID:17 */
    [LA_S18] = "%s: already running, cannot trigger again",  /* SID:18 */
    [LA_S19] = "%s: peer is OFFLINE",  /* SID:19 */
    [LA_S20] = "%s: peer is online, waiting echo",  /* SID:20 */
    [LA_S21] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:21 */
    [LA_S22] = "%s: TURN allocated, starting address exchange",  /* SID:22 */
    [LA_S23] = "[SCTP] association established",  /* SID:23 */
    [LA_S24] = "[SCTP] usrsctp initialized, connecting...",  /* SID:24 */
    [LA_S25] = "[SCTP] usrsctp_socket failed",  /* SID:25 */
    [LA_S26] = "Channel ID validation failed",  /* SID:26 */
    [LA_S27] = "Detecting local network addresses",  /* SID:27 */
    [LA_S28] = "Gist GET failed",  /* SID:28 */
    [LA_S29] = "Invalid channel_id format (security risk)",  /* SID:29 */
    [LA_S30] = "Out of memory",  /* SID:30 */
    [LA_S31] = "Push local cand<%s:%d> failed(OOM)\n",  /* SID:31 */
    [LA_S32] = "Push remote cand<%s:%d> failed(OOM)\n",  /* SID:32 */
    [LA_S33] = "resync candidates",  /* SID:33 */
    [LA_S34] = "sync candidates",  /* SID:34 */
    [LA_S35] = "waiting for peer",  /* SID:35 */
    [LA_F36] = "  [%d] %s/%d",  /* SID:36 */
    [LA_F37] = "%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n",  /* SID:37 */
    [LA_F38] = "%s NOTIFY: accepted\n",  /* SID:38 */
    [LA_F39] = "%s NOTIFY: ignored old notify base=%u (current=%u)\n",  /* SID:39 */
    [LA_F40] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n",  /* SID:40 */
    [LA_F41] = "%s NOTIFY: srflx addr update (disabled)\n",  /* SID:41 */
    [LA_F42] = "%s accepted (ses_id=%u), sid=%u code=%u len=%u\n",  /* SID:42 */
    [LA_F43] = "%s accepted (ses_id=%u), sid=%u msg=%u\n",  /* SID:43 */
    [LA_F44] = "%s accepted (ses_id=%u), sid=%u\n",  /* SID:44 */
    [LA_F45] = "%s accepted (ses_id=%u), waiting for response (sid=%u)\n",  /* SID:45 */
    [LA_F46] = "%s msg=0 accepted (ses_id=%u), echo reply sid=%u len=%d\n",  /* SID:46 */
    [LA_F47] = "%s msg=0: echo reply (sid=%u)\n",  /* SID:47 */
    [LA_F48] = "%s req (ses_id=%u), sid=%u msg=%u len=%d\n",  /* SID:48 */
    [LA_F49] = "%s req accepted (ses_id=%u), sid=%u msg=%u\n",  /* SID:49 */
    [LA_F50] = "%s resent (ses_id=%u), (total=%d, err=%d)/%d\n",  /* SID:50 */
    [LA_F51] = "%s resp (ses_id=%u), sid=%u code=%u len=%d\n",  /* SID:51 */
    [LA_F52] = "%s sent (ses_id=%u), seq=%u\n",  /* SID:52 */
    [LA_F53] = "%s sent (ses_id=%u), sid=%u msg=%u size=%d\n",  /* SID:53 */
    [LA_F54] = "%s sent (ses_id=%u), sid=%u\n",  /* SID:54 */
    [LA_F55] = "%s sent (ses_id=%u), total=%d, err=%d\n",  /* SID:55 */
    [LA_F56] = "%s sent (ses_id=%u)\n",  /* SID:56 */
    [LA_F57] = "%s sent to %s:%d",  /* SID:57 */
    [LA_F58] = "%s sent to %s:%d (writable), echo_seq=%u",  /* SID:58 */
    [LA_F59] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:59 */
    [LA_F60] = "%s sent via best path[%d] to %s:%d, echo_seq=%u",  /* SID:60 */
    [LA_F61] = "%s sent via signaling relay",  /* SID:61 */
    [LA_F62] = "%s sent, auth_key=%llu, remote='%.32s', cands=%d\n",  /* SID:62 */
    [LA_F63] = "%s sent, inst_id=%u\n",  /* SID:63 */
    [LA_F64] = "%s sent, name='%s' rid=%u\n",  /* SID:64 */
    [LA_F65] = "%s sent, retry=%u\n",  /* SID:65 */
    [LA_F66] = "%s sent, ses_id=%u cand_base=%d, cand_cnt=%d fin=%d\n",  /* SID:66 */
    [LA_F67] = "%s sent, ses_id=%u\n",  /* SID:67 */
    [LA_F68] = "%s sent, target='%s' cand=%u\n",  /* SID:68 */
    [LA_F69] = "%s sent\n",  /* SID:69 */
    [LA_F70] = "%s skipped: auth_key=0\n",  /* SID:70 */
    [LA_F71] = "%s throttled: awaiting READY\n",  /* SID:71 */
    [LA_F72] = "%s trickle (ses_id=%u), cnt=%d, seq=%u \n",  /* SID:72 */
    [LA_F73] = "%s, retry remaining candidates and FIN to peer\n",  /* SID:73 */
    [LA_F74] = "%s: %s timeout after %d retries (sid=%u)\n",  /* SID:74 */
    [LA_F75] = "%s: %s → %s (recv DATA)",  /* SID:75 */
    [LA_F76] = "%s: CONN ignored, upsert %s:%d failed",  /* SID:76 */
    [LA_F77] = "%s: CONN timeout after %llums",  /* SID:77 */
    [LA_F78] = "%s: CONNECTED → LOST (no response %llums)\n",  /* SID:78 */
    [LA_F79] = "%s: CONNECTING → %s (recv CONN)",  /* SID:79 */
    [LA_F80] = "%s: CONNECTING → %s (recv CONN_ACK)",  /* SID:80 */
    [LA_F81] = "%s: CONNECTING → CLOSED (timeout, no relay)",  /* SID:81 */
    [LA_F82] = "%s: CONN_ACK ignored, upsert %s:%d failed",  /* SID:82 */
    [LA_F83] = "%s: PUNCHING → %s",  /* SID:83 */
    [LA_F84] = "%s: PUNCHING → %s (peer CONNECTING)",  /* SID:84 */
    [LA_F85] = "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)",  /* SID:85 */
    [LA_F86] = "%s: PUNCHING → CONNECTING (%s%s)",  /* SID:86 */
    [LA_F87] = "%s: PUNCHING → RELAY (peer CONNECTING)",  /* SID:87 */
    [LA_F88] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n",  /* SID:88 */
    [LA_F89] = "%s: Peer addr changed -> %s:%d, retrying punch\n",  /* SID:89 */
    [LA_F90] = "%s: RELAY → CONNECTED (direct path recovered)",  /* SID:90 */
    [LA_F91] = "%s: RPC complete (sid=%u)\n",  /* SID:91 */
    [LA_F92] = "%s: RPC fail due to peer offline (sid=%u)\n",  /* SID:92 */
    [LA_F93] = "%s: RPC fail due to relay timeout (sid=%u)\n",  /* SID:93 */
    [LA_F94] = "%s: RPC finished (sid=%u)\n",  /* SID:94 */
    [LA_F95] = "%s: SIGNALING path enabled (server supports relay)\n",  /* SID:95 */
    [LA_F96] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:96 */
    [LA_F97] = "%s: TURN allocation failed: ret=%d",  /* SID:97 */
    [LA_F98] = "%s: TURN allocation request sent",  /* SID:98 */
    [LA_F99] = "%s: UDP timeout, retry %d/%d",  /* SID:99 */
    [LA_F100] = "%s: UDP timeout: peer not responding",  /* SID:100 */
    [LA_F101] = "%s: accepted",  /* SID:101 */
    [LA_F102] = "%s: accepted (ses_id=%u), peer=%s\n",  /* SID:102 */
    [LA_F103] = "%s: accepted (ses_id=%u)\n",  /* SID:103 */
    [LA_F104] = "%s: accepted as cand[%d], target=%s:%d",  /* SID:104 */
    [LA_F105] = "%s: accepted cand_cnt=%d\n",  /* SID:105 */
    [LA_F106] = "%s: accepted for ack_seq=%u\n",  /* SID:106 */
    [LA_F107] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n",  /* SID:107 */
    [LA_F108] = "%s: accepted, cand_max=%d%s relay=%s msg=%s\n",  /* SID:108 */
    [LA_F109] = "%s: accepted, probe_mapped=%s:%d\n",  /* SID:109 */
    [LA_F110] = "%s: accepted, public=%s:%d auth_key=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n",  /* SID:110 */
    [LA_F111] = "%s: accepted\n",  /* SID:111 */
    [LA_F112] = "%s: auth_key acquired, auto SYNC0 sent\n",  /* SID:112 */
    [LA_F113] = "%s: auth_key acquired, waiting stun pending\n",  /* SID:113 */
    [LA_F114] = "%s: bad FIN marker=0x%02x\n",  /* SID:114 */
    [LA_F115] = "%s: bad payload len=%d\n",  /* SID:115 */
    [LA_F116] = "%s: bad payload(%d)",  /* SID:116 */
    [LA_F117] = "%s: bad payload(%d)\n",  /* SID:117 */
    [LA_F118] = "%s: bad payload(len=%d cand_cnt=%d)\n",  /* SID:118 */
    [LA_F119] = "%s: bad payload(len=%d)\n",  /* SID:119 */
    [LA_F120] = "%s: batch punch skip (state=%d, use trickle)",  /* SID:120 */
    [LA_F121] = "%s: batch punch start (%d cands)",  /* SID:121 */
    [LA_F122] = "%s: batch punch: no cand, wait trickle",  /* SID:122 */
    [LA_F123] = "%s: cand[%d] payload too large for multi_session (%d)",  /* SID:123 */
    [LA_F124] = "%s: cand[%d]<%s:%d> send packet failed(%d)",  /* SID:124 */
    [LA_F125] = "%s: complete (ses_id=%u), sid=%u code=%u\n",  /* SID:125 */
    [LA_F126] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n",  /* SID:126 */
    [LA_F127] = "%s: duplicate remote cand<%s:%d> from signaling, skipped\n",  /* SID:127 */
    [LA_F128] = "%s: duplicate remote cand<%s:%d>, skipped\n",  /* SID:128 */
    [LA_F129] = "%s: duplicate request ignored (sid=%u)\n",  /* SID:129 */
    [LA_F130] = "%s: duplicate request ignored (sid=%u, already processing)\n",  /* SID:130 */
    [LA_F131] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n",  /* SID:131 */
    [LA_F132] = "%s: entered early, %s arrived before SYNC0\n",  /* SID:132 */
    [LA_F133] = "%s: entered, %s arrived\n",  /* SID:133 */
    [LA_F134] = "%s: entered, peer online in SYNC0_ACK\n",  /* SID:134 */
    [LA_F135] = "%s: exchange timeout, retry %d/%d",  /* SID:135 */
    [LA_F136] = "%s: exchange timeout: peer not responding",  /* SID:136 */
    [LA_F137] = "%s: fatal error code=%u, entering ERROR state\n",  /* SID:137 */
    [LA_F138] = "%s: ignored for duplicated seq=%u, already acked\n",  /* SID:138 */
    [LA_F139] = "%s: ignored for seq=%u (expect=%d)\n",  /* SID:139 */
    [LA_F140] = "%s: ignored for sid=%u (current sid=%u)\n",  /* SID:140 */
    [LA_F141] = "%s: ignored in invalid state=%d\n",  /* SID:141 */
    [LA_F142] = "%s: ignored in state=%d\n",  /* SID:142 */
    [LA_F143] = "%s: ignored, upsert %s:%d failed",  /* SID:143 */
    [LA_F144] = "%s: invalid ack_seq=%u\n",  /* SID:144 */
    [LA_F145] = "%s: invalid cand idx: %d (count: %d)",  /* SID:145 */
    [LA_F146] = "%s: invalid cand_cnt=0\n",  /* SID:146 */
    [LA_F147] = "%s: invalid for non-relay req\n",  /* SID:147 */
    [LA_F148] = "%s: invalid payload len=%d (need 6)",  /* SID:148 */
    [LA_F149] = "%s: invalid seq=%u\n",  /* SID:149 */
    [LA_F150] = "%s: invalid ses_id=%u\n",  /* SID:150 */
    [LA_F151] = "%s: invalid session_id=0\n",  /* SID:151 */
    [LA_F152] = "%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n",  /* SID:152 */
    [LA_F153] = "%s: keep-alive sent (%d cands)",  /* SID:153 */
    [LA_F154] = "%s: missing session_id in payload\n",  /* SID:154 */
    [LA_F155] = "%s: new request (sid=%u) overrides pending request (sid=%u)\n",  /* SID:155 */
    [LA_F156] = "%s: no DTLS context for CRYPTO pkt \n",  /* SID:156 */
    [LA_F157] = "%s: no pending request\n",  /* SID:157 */
    [LA_F158] = "%s: no rpc request\n",  /* SID:158 */
    [LA_F159] = "%s: no ses_id for multi session\n",  /* SID:159 */
    [LA_F160] = "%s: no session for peer_id=%.*s (req_type=%u)\n",  /* SID:160 */
    [LA_F161] = "%s: no session for peer_id=%.*s\n",  /* SID:161 */
    [LA_F162] = "%s: no session for session_id=%u (req_type=%u)\n",  /* SID:162 */
    [LA_F163] = "%s: no session for session_id=%u\n",  /* SID:163 */
    [LA_F164] = "%s: not connected, cannot send FIN",  /* SID:164 */
    [LA_F165] = "%s: not supported by server\n",  /* SID:165 */
    [LA_F166] = "%s: old request ignored (sid=%u <= last_sid=%u)\n",  /* SID:166 */
    [LA_F167] = "%s: path rx UP (%s:%d)",  /* SID:167 */
    [LA_F168] = "%s: path tx UP",  /* SID:168 */
    [LA_F169] = "%s: path tx UP (echo seq=%u)",  /* SID:169 */
    [LA_F170] = "%s: path[%d] UP (%s:%d)",  /* SID:170 */
    [LA_F171] = "%s: path[%d] UP (recv DATA)",  /* SID:171 */
    [LA_F172] = "%s: path[%d] relay UP",  /* SID:172 */
    [LA_F173] = "%s: peer disconnected (ses_id=%u), reset to WAIT_PEER\n",  /* SID:173 */
    [LA_F174] = "%s: peer offline (sid=%u)\n",  /* SID:174 */
    [LA_F175] = "%s: peer offline in SYNC0_ACK, waiting for peer to come online\n",  /* SID:175 */
    [LA_F176] = "%s: peer offline\n",  /* SID:176 */
    [LA_F177] = "%s: peer online, starting NAT punch\n",  /* SID:177 */
    [LA_F178] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:178 */
    [LA_F179] = "%s: pkt payload exceeds limit (%d > %d)\n",  /* SID:179 */
    [LA_F180] = "%s: pkt recv (ses_id=%u), inner type=%u\n",  /* SID:180 */
    [LA_F181] = "%s: processed, synced=%d\n",  /* SID:181 */
    [LA_F182] = "%s: promoted prflx cand[%d]<%s:%d> → %s\n",  /* SID:182 */
    [LA_F183] = "%s: protocol mismatch, recv PKT_ACK on trans=%s",  /* SID:183 */
    [LA_F184] = "%s: punch cand[%d] %s:%d (%s)",  /* SID:184 */
    [LA_F185] = "%s: punch remote cand[%d]<%s:%d> failed\n",  /* SID:185 */
    [LA_F186] = "%s: punch timeout, fallback punching using signaling relay",  /* SID:186 */
    [LA_F187] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:187 */
    [LA_F188] = "%s: push remote cand<%s:%d> failed(OOM)",  /* SID:188 */
    [LA_F189] = "%s: reaching alloc OOM",  /* SID:189 */
    [LA_F190] = "%s: reaching broadcast to %d cand(s), seq=%u",  /* SID:190 */
    [LA_F191] = "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u",  /* SID:191 */
    [LA_F192] = "%s: reaching cand[%d] via signaling relay, seq=%u",  /* SID:192 */
    [LA_F193] = "%s: reaching enqueued: cand[%d], seq=%u, priority=%u",  /* SID:193 */
    [LA_F194] = "%s: reaching relay via signaling FAILED (ret=%d), seq=%u",  /* SID:194 */
    [LA_F195] = "%s: reaching relay via signaling SUCCESS, seq=%u",  /* SID:195 */
    [LA_F196] = "%s: reaching updated: cand[%d], seq=%u->%u",  /* SID:196 */
    [LA_F197] = "%s: ready to start session\n",  /* SID:197 */
    [LA_F198] = "%s: recorded peer conn_seq=%u for future CONN_ACK",  /* SID:198 */
    [LA_F199] = "%s: recv (ses_id=%u), type=%u\n",  /* SID:199 */
    [LA_F200] = "%s: recv from cand[%d]",  /* SID:200 */
    [LA_F201] = "%s: relay busy, will retry\n",  /* SID:201 */
    [LA_F202] = "%s: relay ready, flow control released\n",  /* SID:202 */
    [LA_F203] = "%s: remote %s cand<%s:%d> (disabled)\n",  /* SID:203 */
    [LA_F204] = "%s: remote %s cand[%d]<%s:%d> (disabled)\n",  /* SID:204 */
    [LA_F205] = "%s: remote %s cand[%d]<%s:%d> accepted\n",  /* SID:205 */
    [LA_F206] = "%s: remote_cands[] full, skipped %d candidates\n",  /* SID:206 */
    [LA_F207] = "%s: renew session (local=%u pkt=%u)\n",  /* SID:207 */
    [LA_F208] = "%s: req_type=%u code=%u msg=%s\n",  /* SID:208 */
    [LA_F209] = "%s: req_type=%u code=%u\n",  /* SID:209 */
    [LA_F210] = "%s: restarting periodic check",  /* SID:210 */
    [LA_F211] = "%s: retry(%d/%d) probe\n",  /* SID:211 */
    [LA_F212] = "%s: retry(%d/%d) req (sid=%u)\n",  /* SID:212 */
    [LA_F213] = "%s: retry(%d/%d) resp (sid=%u)\n",  /* SID:213 */
    [LA_F214] = "%s: retry, (attempt %d/%d)\n",  /* SID:214 */
    [LA_F215] = "%s: send failed(%d)",  /* SID:215 */
    [LA_F216] = "%s: sent (ses_id=%u), sid=%u code=%u size=%d\n",  /* SID:216 */
    [LA_F217] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:217 */
    [LA_F218] = "%s: server rejected (no slot)\n",  /* SID:218 */
    [LA_F219] = "%s: sess_id=%u req_type=%u code=%u msg=%s\n",  /* SID:219 */
    [LA_F220] = "%s: sess_id=%u req_type=%u code=%u\n",  /* SID:220 */
    [LA_F221] = "%s: session established(st=%s peer=%s), %s\n",  /* SID:221 */
    [LA_F222] = "%s: session offer(st=%s peer=%s), waiting for peer\n",  /* SID:222 */
    [LA_F223] = "%s: session reset by peer(st=%s old=%u new=%u), %s\n",  /* SID:223 */
    [LA_F224] = "%s: session suspend(st=%s)\n",  /* SID:224 */
    [LA_F225] = "%s: session_id changed (old=%u new=%u)\n",  /* SID:225 */
    [LA_F226] = "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n",  /* SID:226 */
    [LA_F227] = "%s: started, sending first probe\n",  /* SID:227 */
    [LA_F228] = "%s: sync ack confirmed cnt=%d exceeds unacked cnt=%d\n",  /* SID:228 */
    [LA_F229] = "%s: sync busy, will retry\n",  /* SID:229 */
    [LA_F230] = "%s: sync complete (ses_id=%u)\n",  /* SID:230 */
    [LA_F231] = "%s: sync complete (ses_id=%u, mask=0x%04x)\n",  /* SID:231 */
    [LA_F232] = "%s: sync done, st=%s cands=%d\n",  /* SID:232 */
    [LA_F233] = "%s: sync done\n",  /* SID:233 */
    [LA_F234] = "%s: sync fin ack, but cand synced cnt not match sent cnt (cand=%d synced=%d)\n",  /* SID:234 */
    [LA_F235] = "%s: sync forwarded, confirmed=%d synced=%d\n",  /* SID:235 */
    [LA_F236] = "%s: sync0 srflx cand[%d]<%s:%d>%s\n",  /* SID:236 */
    [LA_F237] = "%s: syncable ready, auto SYNC0 sent\n",  /* SID:237 */
    [LA_F238] = "%s: timeout (sid=%u)\n",  /* SID:238 */
    [LA_F239] = "%s: timeout after %d retries , type unknown\n",  /* SID:239 */
    [LA_F240] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:240 */
    [LA_F241] = "%s: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:241 */
    [LA_F242] = "%s: timeout, peer did not respond",  /* SID:242 */
    [LA_F243] = "%s: timeout, retry %d/%d",  /* SID:243 */
    [LA_F244] = "%s: trickle punch start",  /* SID:244 */
    [LA_F245] = "%s: triggered via COMPACT msg echo",  /* SID:245 */
    [LA_F246] = "%s: triggered via RELAY TUNE echo",  /* SID:246 */
    [LA_F247] = "%s: unexpected ack_seq=%u mask=0x%04x\n",  /* SID:247 */
    [LA_F248] = "%s: unexpected non-srflx cand in sync0, treating as srflx\n",  /* SID:248 */
    [LA_F249] = "%s: unexpected remote cand type %d, skipped\n",  /* SID:249 */
    [LA_F250] = "%s: unexpected s->id=0\n",  /* SID:250 */
    [LA_F251] = "%s: unexpected type 0x%02x\n",  /* SID:251 */
    [LA_F252] = "%s: unknown target cand %s:%d",  /* SID:252 */
    [LA_F253] = "%s: unsupported type 0x%02x\n",  /* SID:253 */
    [LA_F254] = "%s: → CLOSED (recv FIN)",  /* SID:254 */
    [LA_F255] = "%s:%04d: %s",  /* SID:255 */
    [LA_F256] = "%s_ACK sent to %s:%d (try), echo_seq=%u",  /* SID:256 */
    [LA_F257] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:257 */
    [LA_F258] = "% Answer already present, skipping offer re-publish",  /* SID:258 */
    [LA_F259] = "Attempting Simultaneous Open to %s:%d",  /* SID:259 */
    [LA_F260] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:260 */
    [LA_F261] = "% BIO_new failed",  /* SID:261 */
    [LA_F262] = "% Base64 decode failed",  /* SID:262 */
    [LA_F263] = "% Bind failed",  /* SID:263 */
    [LA_F264] = "Bind failed to %d, port busy, trying random port",  /* SID:264 */
    [LA_F265] = "Bound to :%d",  /* SID:265 */
    [LA_F266] = "% Buffer size < 2048 may be insufficient for full SDP",  /* SID:266 */
    [LA_F267] = "% Close P2P UDP socket",  /* SID:267 */
    [LA_F268] = "% Closing TCP connection to RELAY signaling server",  /* SID:268 */
    [LA_F269] = "Connect to COMPACT signaling server failed(%d)",  /* SID:269 */
    [LA_F270] = "Connect to RELAY signaling server failed(%d)",  /* SID:270 */
    [LA_F271] = "Crypto layer '%s' init failed, continuing without encryption",  /* SID:271 */
    [LA_F272] = "% DTLS (MbedTLS) requested but library not linked",  /* SID:272 */
    [LA_F273] = "% DTLS handshake complete (MbedTLS)",  /* SID:273 */
    [LA_F274] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:274 */
    [LA_F275] = "Detect local network interfaces failed(%d)",  /* SID:275 */
    [LA_F276] = "Detection completed %s",  /* SID:276 */
    [LA_F277] = "Duplicate remote cand<%s:%d> from signaling, skipped",  /* SID:277 */
    [LA_F278] = "Exported %d candidates to SDP (%d bytes)",  /* SID:278 */
    [LA_F279] = "% Failed to allocate DTLS context",  /* SID:279 */
    [LA_F280] = "% Failed to allocate OpenSSL context",  /* SID:280 */
    [LA_F281] = "% Failed to allocate memory for candidate lists",  /* SID:281 */
    [LA_F282] = "% Failed to allocate memory for instance",  /* SID:282 */
    [LA_F283] = "% Failed to allocate memory for session",  /* SID:283 */
    [LA_F284] = "% Failed to build STUN request",  /* SID:284 */
    [LA_F285] = "Failed to parse SDP candidate line: %s",  /* SID:285 */
    [LA_F286] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:286 */
    [LA_F287] = "Failed to reserve remote candidates (base=%u cnt=%d)\n",  /* SID:287 */
    [LA_F288] = "Failed to reserve remote candidates (cnt=%d)\n",  /* SID:288 */
    [LA_F289] = "% Failed to reserve remote candidates (cnt=1)\n",  /* SID:289 */
    [LA_F290] = "Failed to resolve STUN server %s",  /* SID:290 */
    [LA_F291] = "Failed to resolve TURN server: %s",  /* SID:291 */
    [LA_F292] = "Failed to send Allocate Request: %d",  /* SID:292 */
    [LA_F293] = "Failed to send STUN request: %d",  /* SID:293 */
    [LA_F294] = "% Failed to send Test I(alt), continue to Test III",  /* SID:294 */
    [LA_F295] = "% Failed to send punch packet for new peer addr\n",  /* SID:295 */
    [LA_F296] = "% Failed to start TURN allocation",  /* SID:296 */
    [LA_F297] = "Field %s is empty or too short",  /* SID:297 */
    [LA_F298] = "% Full SDP generation requires ice_ufrag and ice_pwd",  /* SID:298 */
    [LA_F299] = "Gathered Host candidate: %s:%d (priority=0x%08x)",  /* SID:299 */
    [LA_F300] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:300 */
    [LA_F301] = "% Handshake complete",  /* SID:301 */
    [LA_F302] = "Handshake failed: %s (-0x%04x)",  /* SID:302 */
    [LA_F303] = "Ignore %s pkt from %s:%d, not connected",  /* SID:303 */
    [LA_F304] = "Ignore %s pkt from %s:%d, not connecting",  /* SID:304 */
    [LA_F305] = "Ignore %s pkt from %s:%d, state=%d (not connected yet)",  /* SID:305 */
    [LA_F306] = "Ignore %s pkt from %s:%d, valid state(%d)",  /* SID:306 */
    [LA_F307] = "Ignore %s pkt from unknown path %s:%d",  /* SID:307 */
    [LA_F308] = "Imported %d candidates from SDP",  /* SID:308 */
    [LA_F309] = "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)",  /* SID:309 */
    [LA_F310] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:310 */
    [LA_F311] = "Initialize network subsystem failed(%d)",  /* SID:311 */
    [LA_F312] = "Initialize signaling mode: %d",  /* SID:312 */
    [LA_F313] = "Initialized: %s",  /* SID:313 */
    [LA_F314] = "Invalid IP address: %s",  /* SID:314 */
    [LA_F315] = "Invalid remote_peer_id for %s mode",  /* SID:315 */
    [LA_F316] = "% Invalid signaling mode in configuration",  /* SID:316 */
    [LA_F317] = "% LOST recovery: NAT connected but no path available",  /* SID:317 */
    [LA_F318] = "Local address detection done: %d address(es)",  /* SID:318 */
    [LA_F319] = "Login to COMPACT signaling server at %s:%d",  /* SID:319 */
    [LA_F320] = "Login to RELAY signaling server at %s:%d",  /* SID:320 */
    [LA_F321] = "% MSG RPC not supported by server\n",  /* SID:321 */
    [LA_F322] = "% NAT connected but no available path in path manager",  /* SID:322 */
    [LA_F323] = "% NAT detection skipped (skip_stun_test=true), Srflx gathered",  /* SID:323 */
    [LA_F324] = "% No advanced transport layer enabled, using simple reliable layer",  /* SID:324 */
    [LA_F325] = "% No auth_key provided, using default key (insecure)",  /* SID:325 */
    [LA_F326] = "% No shared local route addresses available, host candidates skipped",  /* SID:326 */
    [LA_F327] = "% No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)",  /* SID:327 */
    [LA_F328] = "% ONLINE: auth_key acquired, auto SYNC0 sent\n",  /* SID:328 */
    [LA_F329] = "% ONLINE: auth_key acquired, waiting stun pending\n",  /* SID:329 */
    [LA_F330] = "Open P2P UDP socket on port %d",  /* SID:330 */
    [LA_F331] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:331 */
    [LA_F332] = "% OpenSSL requested but library not linked",  /* SID:332 */
    [LA_F333] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:333 */
    [LA_F334] = "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:334 */
    [LA_F335] = "% PUBSUB (SUB): waiting for offer from any peer",  /* SID:335 */
    [LA_F336] = "% PUBSUB mode requires gh_token and gist_id",  /* SID:336 */
    [LA_F337] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:337 */
    [LA_F338] = "Packet too large len=%d max=%d",  /* SID:338 */
    [LA_F339] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:339 */
    [LA_F340] = "% Path switch debounced, waiting for stability",  /* SID:340 */
    [LA_F341] = "Path switched to better route (idx=%d)",  /* SID:341 */
    [LA_F342] = "Processing (role=%s)",  /* SID:342 */
    [LA_F343] = "% PseudoTCP enabled as transport layer",  /* SID:343 */
    [LA_F344] = "% RELAY path but TURN not allocated",  /* SID:344 */
    [LA_F345] = "% RELAY path but TURN not allocated (dtls)",  /* SID:345 */
    [LA_F346] = "% RELAY recovery: NAT connected but no path available",  /* SID:346 */
    [LA_F347] = "RELAY sent (ses_id=%u), type=0x%02x seq=%u flags=0x%02x",  /* SID:347 */
    [LA_F348] = "% RELAY/COMPACT mode requires server_host",  /* SID:348 */
    [LA_F349] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:349 */
    [LA_F350] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:350 */
    [LA_F351] = "Received valid signal from '%s'",  /* SID:351 */
    [LA_F352] = "Recv %s pkt from %s:%d",  /* SID:352 */
    [LA_F353] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:353 */
    [LA_F354] = "Recv %s pkt from %s:%d seq=%u",  /* SID:354 */
    [LA_F355] = "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:355 */
    [LA_F356] = "Recv %s pkt from %s:%d, seq=%u, len=%d",  /* SID:356 */
    [LA_F357] = "Recv ICE-STUN Binding Request from candidate %d (%s:%d)",  /* SID:357 */
    [LA_F358] = "Recv ICE-STUN Binding Response from candidate %d (%s:%d)",  /* SID:358 */
    [LA_F359] = "Recv ICE-STUN from %s:%d, upsert prflx failed",  /* SID:359 */
    [LA_F360] = "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:360 */
    [LA_F361] = "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d",  /* SID:361 */
    [LA_F362] = "Reliable transport initialized rto=%d win=%d",  /* SID:362 */
    [LA_F363] = "Requested Relay Candidate from TURN %s",  /* SID:363 */
    [LA_F364] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:364 */
    [LA_F365] = "Reuse Relay Candidate %s:%u (priority=%u)",  /* SID:365 */
    [LA_F366] = "Reuse STUN Candidate %s:%u (priority=%u)",  /* SID:366 */
    [LA_F367] = "% SCTP (usrsctp) requested but library not linked",  /* SID:367 */
    [LA_F368] = "% SDP export buffer overflow",  /* SID:368 */
    [LA_F369] = "% SIGNALING path but signaling relay not available",  /* SID:369 */
    [LA_F370] = "% SIGNALING path enabled (server supports relay)\n",  /* SID:370 */
    [LA_F371] = "% SSL_CTX_new failed",  /* SID:371 */
    [LA_F372] = "% SSL_new failed",  /* SID:372 */
    [LA_F373] = "STUN collecting to %s:%d (len=%d)",  /* SID:373 */
    [LA_F374] = "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n",  /* SID:374 */
    [LA_F375] = "% SYNC(trickle): seq overflow, cannot trickle more\n",  /* SID:375 */
    [LA_F376] = "SYNC0: retry, (attempt %d/%d)\n",  /* SID:376 */
    [LA_F377] = "SYNC0: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:377 */
    [LA_F378] = "Send window full, dropping packet send_count=%d",  /* SID:378 */
    [LA_F379] = "Sending Allocate Request to %s:%d",  /* SID:379 */
    [LA_F380] = "% Sending FIN packet to peer before closing",  /* SID:380 */
    [LA_F381] = "% Sending OFFLINE packet to COMPACT signaling server",  /* SID:381 */
    [LA_F382] = "Sending Test I to %s:%d (len=%d)",  /* SID:382 */
    [LA_F383] = "% Sending Test I(alt) to CHANGED-ADDRESS",  /* SID:383 */
    [LA_F384] = "% Sending Test II with CHANGE-REQUEST(IP+PORT)",  /* SID:384 */
    [LA_F385] = "% Sending Test III with CHANGE-REQUEST(PORT only)",  /* SID:385 */
    [LA_F386] = "% Signal payload deserialization failed",  /* SID:386 */
    [LA_F387] = "% Skipping Host Candidate gathering (disabled)",  /* SID:387 */
    [LA_F388] = "Start COMPACT session failed(%d)",  /* SID:388 */
    [LA_F389] = "Start RELAY session failed(%d)",  /* SID:389 */
    [LA_F390] = "Start internal thread failed(%d)",  /* SID:390 */
    [LA_F391] = "Starting COMPACT session with %s",  /* SID:391 */
    [LA_F392] = "Starting RELAY session with %s",  /* SID:392 */
    [LA_F393] = "% Starting internal thread",  /* SID:393 */
    [LA_F394] = "State: LOST → CONNECTED, path=PUNCH[%d]",  /* SID:394 */
    [LA_F395] = "State: RELAY → CONNECTED, path=PUNCH[%d]",  /* SID:395 */
    [LA_F396] = "State: → CONNECTED, path[%d]",  /* SID:396 */
    [LA_F397] = "% State: → ERROR (punch timeout, no relay available)",  /* SID:397 */
    [LA_F398] = "% State: → LOST (all paths failed)",  /* SID:398 */
    [LA_F399] = "% State: → PUNCHING",  /* SID:399 */
    [LA_F400] = "State: → RELAY, path[%d]",  /* SID:400 */
    [LA_F401] = "% Stopping internal thread",  /* SID:401 */
    [LA_F402] = "TURN 401 Unauthorized (realm=%s), authenticating...",  /* SID:402 */
    [LA_F403] = "TURN Allocate failed with error %d",  /* SID:403 */
    [LA_F404] = "TURN Allocated relay %s:%u (lifetime=%us)",  /* SID:404 */
    [LA_F405] = "TURN CreatePermission failed (error=%d)",  /* SID:405 */
    [LA_F406] = "TURN CreatePermission for %s",  /* SID:406 */
    [LA_F407] = "TURN Data Indication from %s:%u (%d bytes)",  /* SID:407 */
    [LA_F408] = "TURN Refresh failed (error=%d)",  /* SID:408 */
    [LA_F409] = "TURN Refresh ok (lifetime=%us)",  /* SID:409 */
    [LA_F410] = "% TURN auth required but no credentials configured",  /* SID:410 */
    [LA_F411] = "Test I(alt): Mapped address: %s:%d",  /* SID:411 */
    [LA_F412] = "% Test I(alt): Timeout",  /* SID:412 */
    [LA_F413] = "Test I: Changed address: %s:%d",  /* SID:413 */
    [LA_F414] = "Test I: Mapped address: %s:%d",  /* SID:414 */
    [LA_F415] = "% Test I: Timeout",  /* SID:415 */
    [LA_F416] = "Test II: Success! Detection completed %s",  /* SID:416 */
    [LA_F417] = "% Test II: Timeout (need Test III)",  /* SID:417 */
    [LA_F418] = "Test III: Success! Detection completed %s",  /* SID:418 */
    [LA_F419] = "% Test III: Timeout",  /* SID:419 */
    [LA_F420] = "Transport layer '%s' init failed, falling back to simple reliable",  /* SID:420 */
    [LA_F421] = "Unknown candidate type: %s",  /* SID:421 */
    [LA_F422] = "Unknown signaling mode: %d",  /* SID:422 */
    [LA_F423] = "Updating Gist field '%s'...",  /* SID:423 */
    [LA_F424] = "% WebRTC candidate export buffer overflow",  /* SID:424 */
    [LA_F425] = "[C] %s recv, len=%d\n",  /* SID:425 */
    [LA_F426] = "[C] %s recv, seq=%u, flags=0x%02x, len=%d\n",  /* SID:426 */
    [LA_F427] = "[C] %s recv, seq=%u, len=%d\n",  /* SID:427 */
    [LA_F428] = "[C] %s recv\n",  /* SID:428 */
    [LA_F429] = "[C] %s send failed(%d)\n",  /* SID:429 */
    [LA_F430] = "[C] %s send to port:%d failed(%d)\n",  /* SID:430 */
    [LA_F431] = "[C] %s send to port:%d, seq=%u, flags=0, len=0\n",  /* SID:431 */
    [LA_F432] = "[C] %s send, seq=0, flags=0x%02x, len=%d\n",  /* SID:432 */
    [LA_F433] = "[C] Unknown pkt type 0x%02x, len=%d\n",  /* SID:433 */
    [LA_F434] = "[C] relay payload too large: %d",  /* SID:434 */
    [LA_F435] = "[MbedTLS] DTLS role: %s (mode=%s)",  /* SID:435 */
    [LA_F436] = "% [OpenSSL] DTLS handshake completed",  /* SID:436 */
    [LA_F437] = "[OpenSSL] DTLS role: %s (mode=%s)",  /* SID:437 */
    [LA_F438] = "[R] %s recv, len=%d\n",  /* SID:438 */
    [LA_F439] = "[R] %s timeout\n",  /* SID:439 */
    [LA_F440] = "[R] %s%s qsend failed(OOM)\n",  /* SID:440 */
    [LA_F441] = "[R] %s%s qsend(%d), len=%u\n",  /* SID:441 */
    [LA_F442] = "[R] Connecting to %s:%d\n",  /* SID:442 */
    [LA_F443] = "% [R] Disconnected, back to ONLINE state\n",  /* SID:443 */
    [LA_F444] = "% [R] Failed to create TCP socket\n",  /* SID:444 */
    [LA_F445] = "% [R] Failed to set socket non-blocking\n",  /* SID:445 */
    [LA_F446] = "[R] TCP connect failed(%d)\n",  /* SID:446 */
    [LA_F447] = "[R] TCP connect select failed(%d)\n",  /* SID:447 */
    [LA_F448] = "% [R] TCP connected immediately, sending ONLINE\n",  /* SID:448 */
    [LA_F449] = "% [R] TCP connected, sending ONLINE\n",  /* SID:449 */
    [LA_F450] = "% [R] TCP connection closed by peer\n",  /* SID:450 */
    [LA_F451] = "% [R] TCP connection closed during send\n",  /* SID:451 */
    [LA_F452] = "[R] TCP recv error(%d)\n",  /* SID:452 */
    [LA_F453] = "[R] TCP send error(%d)\n",  /* SID:453 */
    [LA_F454] = "[R] Unknown proto type %d\n",  /* SID:454 */
    [LA_F455] = "[R] payload size %u exceeds limit %u\n",  /* SID:455 */
    [LA_F456] = "[SCTP] association lost/shutdown (state=%u)",  /* SID:456 */
    [LA_F457] = "[SCTP] bind failed: %s",  /* SID:457 */
    [LA_F458] = "[SCTP] connect failed: %s",  /* SID:458 */
    [LA_F459] = "[SCTP] sendv failed: %s",  /* SID:459 */
    [LA_F460] = "[ST:%s] peer went offline, waiting for reconnect\n",  /* SID:460 */
    [LA_F461] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:461 */
    [LA_F462] = "% connection closed by peer",  /* SID:462 */
    [LA_F463] = "ctr_drbg_seed failed: -0x%x",  /* SID:463 */
    [LA_F464] = "retry seq=%u retx=%d rto=%d",  /* SID:464 */
    [LA_F465] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:465 */
    [LA_F466] = "ssl_config_defaults failed: -0x%x",  /* SID:466 */
    [LA_F467] = "ssl_setup failed: -0x%x",  /* SID:467 */
    [LA_F468] = "transport send_data failed, %d bytes dropped",  /* SID:468 */
    [LA_F469] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:469 */
    [LA_F470] = "% ✗ Add Srflx candidate failed(OOM)",  /* SID:470 */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
