/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_p2p;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "alive",  /* SID:1 */
    [LA_W2] = "Detecting...",  /* SID:2 */
    [LA_W3] = "Full Cone NAT",  /* SID:3 */
    [LA_W4] = "Open Internet (No NAT)",  /* SID:4 */
    [LA_W5] = "Port Restricted Cone NAT",  /* SID:5 */
    [LA_W6] = "punch",  /* SID:6 */
    [LA_W7] = "Restricted Cone NAT",  /* SID:7 */
    [LA_W8] = "retry",  /* SID:8 */
    [LA_W9] = "Symmetric NAT (port-random)",  /* SID:9 */
    [LA_W10] = "Timeout (no response)",  /* SID:10 */
    [LA_W11] = "UDP Blocked (STUN unreachable)",  /* SID:11 */
    [LA_W12] = "Undetectable (no STUN/probe configured)",  /* SID:12 */
    [LA_W13] = "Unknown",  /* SID:13 */
    [LA_S14] = "%s: address exchange failed: peer OFF",  /* SID:14 */
    [LA_S15] = "%s: address exchange success, sending UDP probe",  /* SID:15 */
    [LA_S16] = "%s: already running, cannot trigger again",  /* SID:16 */
    [LA_S17] = "%s: peer is OFF",  /* SID:17 */
    [LA_S18] = "%s: peer is online, waiting echo",  /* SID:18 */
    [LA_S19] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:19 */
    [LA_S20] = "%s: TURN allocated, starting address exchange",  /* SID:20 */
    [LA_S21] = "[SCTP] association established",  /* SID:21 */
    [LA_S22] = "[SCTP] usrsctp initialized, connecting...",  /* SID:22 */
    [LA_S23] = "[SCTP] usrsctp_socket failed",  /* SID:23 */
    [LA_S24] = "Detecting local network addresses",  /* SID:24 */
    [LA_S25] = "Push local cand<%s:%d> failed(OOM)\n",  /* SID:25 */
    [LA_S26] = "Push local IPv6 cand failed(OOM)\n",  /* SID:26 */
    [LA_S27] = "resync for peer",  /* SID:27 */
    [LA_S28] = "sync candidates",  /* SID:28 */
    [LA_S29] = "waiting for peer",  /* SID:29 */
    [LA_S30] = "waiting stun pending",  /* SID:30 */
    [LA_F31] = "  [%d] %s/%d",  /* SID:31 */
    [LA_F32] = "  [v6:%d] %s",  /* SID:32 */
    [LA_F33] = "%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n",  /* SID:33 */
    [LA_F34] = "%s NOTIFY: accepted\n",  /* SID:34 */
    [LA_F35] = "%s NOTIFY: ignored old notify base=%u (current=%u)\n",  /* SID:35 */
    [LA_F36] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n",  /* SID:36 */
    [LA_F37] = "%s NOTIFY: srflx addr update (disabled)\n",  /* SID:37 */
    [LA_F38] = "%s accepted (ses_id=%u), sid=%u code=%u len=%u\n",  /* SID:38 */
    [LA_F39] = "%s accepted (ses_id=%u), sid=%u msg=%u\n",  /* SID:39 */
    [LA_F40] = "%s accepted (ses_id=%u), sid=%u\n",  /* SID:40 */
    [LA_F41] = "%s accepted (ses_id=%u), waiting for response (sid=%u)\n",  /* SID:41 */
    [LA_F42] = "%s msg=0 accepted (ses_id=%u), echo reply sid=%u len=%d\n",  /* SID:42 */
    [LA_F43] = "%s msg=0: echo reply (sid=%u)\n",  /* SID:43 */
    [LA_F44] = "%s req (ses_id=%u), sid=%u msg=%u len=%d\n",  /* SID:44 */
    [LA_F45] = "%s req accepted (ses_id=%u), sid=%u msg=%u\n",  /* SID:45 */
    [LA_F46] = "%s resent (ses_id=%u), (total=%d, err=%d)/%d\n",  /* SID:46 */
    [LA_F47] = "%s resp (ses_id=%u), sid=%u code=%u len=%d\n",  /* SID:47 */
    [LA_F48] = "%s sent (ses_id=%u), seq=%u\n",  /* SID:48 */
    [LA_F49] = "%s sent (ses_id=%u), sid=%u msg=%u size=%d\n",  /* SID:49 */
    [LA_F50] = "%s sent (ses_id=%u), sid=%u\n",  /* SID:50 */
    [LA_F51] = "%s sent (ses_id=%u), total=%d, err=%d\n",  /* SID:51 */
    [LA_F52] = "%s sent (ses_id=%u)\n",  /* SID:52 */
    [LA_F53] = "%s sent to %s:%d",  /* SID:53 */
    [LA_F54] = "%s sent to %s:%d (writable), echo_seq=%u",  /* SID:54 */
    [LA_F55] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:55 */
    [LA_F56] = "%s sent via best path[%d] to %s:%d, echo_seq=%u",  /* SID:56 */
    [LA_F57] = "%s sent via signaling relay",  /* SID:57 */
    [LA_F58] = "%s sent, auth_key=%llu, remote='%.32s', cands=%d\n",  /* SID:58 */
    [LA_F59] = "%s sent, inst_id=%u\n",  /* SID:59 */
    [LA_F60] = "%s sent, name='%s' rid=%u\n",  /* SID:60 */
    [LA_F61] = "%s sent, retry=%u\n",  /* SID:61 */
    [LA_F62] = "%s sent, ses_id=%u sid=%u cand_base=%d, cand_cnt=%d fin=%d\n",  /* SID:62 */
    [LA_F63] = "%s sent, ses_id=%u\n",  /* SID:63 */
    [LA_F64] = "%s sent, target='%s' cand=%u\n",  /* SID:64 */
    [LA_F65] = "%s sent\n",  /* SID:65 */
    [LA_F66] = "%s skipped: auth_key=0\n",  /* SID:66 */
    [LA_F67] = "%s throttled: awaiting READY\n",  /* SID:67 */
    [LA_F68] = "%s trickle (ses_id=%u), cnt=%d, seq=%u \n",  /* SID:68 */
    [LA_F69] = "%s, retry remaining candidates and FIN to peer\n",  /* SID:69 */
    [LA_F70] = "%s: %s timeout after %d retries (sid=%u)\n",  /* SID:70 */
    [LA_F71] = "%s: %s → %s (recv DATA)",  /* SID:71 */
    [LA_F72] = "%s: CONN ignored, upsert %s:%d failed",  /* SID:72 */
    [LA_F73] = "%s: CONN timeout after %llums",  /* SID:73 */
    [LA_F74] = "%s: CONNECTED → LOST (no response %llums)\n",  /* SID:74 */
    [LA_F75] = "%s: CONNECTING → %s (recv CONN)",  /* SID:75 */
    [LA_F76] = "%s: CONNECTING → %s (recv CONN_ACK)",  /* SID:76 */
    [LA_F77] = "%s: CONNECTING → CLOSED (timeout, no relay)",  /* SID:77 */
    [LA_F78] = "%s: CONN_ACK ignored, upsert %s:%d failed",  /* SID:78 */
    [LA_F79] = "%s: GET %s — empty or failed",  /* SID:79 */
    [LA_F80] = "%s: PATCH failed",  /* SID:80 */
    [LA_F81] = "%s: PUNCHING → %s",  /* SID:81 */
    [LA_F82] = "%s: PUNCHING → %s (peer CONNECTING)",  /* SID:82 */
    [LA_F83] = "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)",  /* SID:83 */
    [LA_F84] = "%s: PUNCHING → CONNECTING (%s%s)",  /* SID:84 */
    [LA_F85] = "%s: PUNCHING → RELAY (peer CONN via signaling)",  /* SID:85 */
    [LA_F86] = "%s: PUNCHING → RELAY (peer CONNECTING)",  /* SID:86 */
    [LA_F87] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n",  /* SID:87 */
    [LA_F88] = "%s: Peer addr changed -> %s:%d, retrying punch\n",  /* SID:88 */
    [LA_F89] = "%s: RELAY → CONNECTED (direct path recovered)",  /* SID:89 */
    [LA_F90] = "%s: RPC complete (sid=%u)\n",  /* SID:90 */
    [LA_F91] = "%s: RPC fail due to peer offline (sid=%u)\n",  /* SID:91 */
    [LA_F92] = "%s: RPC fail due to relay timeout (sid=%u)\n",  /* SID:92 */
    [LA_F93] = "%s: RPC finished (sid=%u)\n",  /* SID:93 */
    [LA_F94] = "%s: SIGNALING path enabled (server supports relay)\n",  /* SID:94 */
    [LA_F95] = "%s: STUN ready → SYNCING",  /* SID:95 */
    [LA_F96] = "%s: SUB gist empty, waiting",  /* SID:96 */
    [LA_F97] = "%s: SUB gist not REG (content: %.20s...)",  /* SID:97 */
    [LA_F98] = "%s: SUB heartbeat stale (%llds ago, threshold %ds), may be offline",  /* SID:98 */
    [LA_F99] = "%s: SUB online (heartbeat %llds ago), early nat_punch",  /* SID:99 */
    [LA_F100] = "%s: SUB responded with %d candidates (ver=%d)",  /* SID:100 */
    [LA_F101] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:101 */
    [LA_F102] = "%s: TURN allocation failed: ret=%d",  /* SID:102 */
    [LA_F103] = "%s: TURN allocation request sent",  /* SID:103 */
    [LA_F104] = "%s: UDP timeout, retry %d/%d",  /* SID:104 */
    [LA_F105] = "%s: UDP timeout: peer not responding",  /* SID:105 */
    [LA_F106] = "%s: accepted",  /* SID:106 */
    [LA_F107] = "%s: accepted (ses_id=%u), peer=%s\n",  /* SID:107 */
    [LA_F108] = "%s: accepted (ses_id=%u)\n",  /* SID:108 */
    [LA_F109] = "%s: accepted as cand[%d], target=%s:%d",  /* SID:109 */
    [LA_F110] = "%s: accepted cand_cnt=%d\n",  /* SID:110 */
    [LA_F111] = "%s: accepted for ack_seq=%u\n",  /* SID:111 */
    [LA_F112] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n",  /* SID:112 */
    [LA_F113] = "%s: accepted, cand_max=%d%s relay=%s msg=%s\n",  /* SID:113 */
    [LA_F114] = "%s: accepted, probe_mapped=%s:%d\n",  /* SID:114 */
    [LA_F115] = "%s: accepted, public=%s:%d auth_key=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n",  /* SID:115 */
    [LA_F116] = "%s: accepted\n",  /* SID:116 */
    [LA_F117] = "%s: auth_key acquired, auto SYN0 sent\n",  /* SID:117 */
    [LA_F118] = "%s: bad FIN marker=0x%02x\n",  /* SID:118 */
    [LA_F119] = "%s: bad payload len=%d\n",  /* SID:119 */
    [LA_F120] = "%s: bad payload(%d)",  /* SID:120 */
    [LA_F121] = "%s: bad payload(%d)\n",  /* SID:121 */
    [LA_F122] = "%s: bad payload(%d, type=%u)\n",  /* SID:122 */
    [LA_F123] = "%s: bad payload(len=%d cand_cnt=%d)\n",  /* SID:123 */
    [LA_F124] = "%s: bad payload(len=%d)\n",  /* SID:124 */
    [LA_F125] = "%s: batch punch skip (state=%d, use trickle)",  /* SID:125 */
    [LA_F126] = "%s: batch punch start (%d cands)",  /* SID:126 */
    [LA_F127] = "%s: batch punch: no cand, wait trickle",  /* SID:127 */
    [LA_F128] = "%s: cand[%d] payload too large for multi_session (%d)",  /* SID:128 */
    [LA_F129] = "%s: cand[%d]<%s:%d> send packet failed(%d)",  /* SID:129 */
    [LA_F130] = "%s: cannot read SUB gist %s",  /* SID:130 */
    [LA_F131] = "%s: complete (ses_id=%u), sid=%u code=%u\n",  /* SID:131 */
    [LA_F132] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n",  /* SID:132 */
    [LA_F133] = "%s: duplicate remote cand<%s:%d> from signaling, skipped\n",  /* SID:133 */
    [LA_F134] = "%s: duplicate remote cand<%s:%d>, skipped\n",  /* SID:134 */
    [LA_F135] = "%s: duplicate request ignored (sid=%u)\n",  /* SID:135 */
    [LA_F136] = "%s: duplicate request ignored (sid=%u, already processing)\n",  /* SID:136 */
    [LA_F137] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n",  /* SID:137 */
    [LA_F138] = "%s: entered early, %s arrived before SYN0\n",  /* SID:138 */
    [LA_F139] = "%s: entered, %s arrived\n",  /* SID:139 */
    [LA_F140] = "%s: entered, peer online in SYN0_ACK\n",  /* SID:140 */
    [LA_F141] = "%s: exchange timeout, retry %d/%d",  /* SID:141 */
    [LA_F142] = "%s: exchange timeout: peer not responding",  /* SID:142 */
    [LA_F143] = "%s: fatal error code=%u, entering ERROR state\n",  /* SID:143 */
    [LA_F144] = "%s: heartbeat write failed",  /* SID:144 */
    [LA_F145] = "%s: heartbeat written",  /* SID:145 */
    [LA_F146] = "%s: ignored for duplicated seq=%u, already acked\n",  /* SID:146 */
    [LA_F147] = "%s: ignored for seq=%u (expect=%d)\n",  /* SID:147 */
    [LA_F148] = "%s: ignored for sid=%u (current sid=%u)\n",  /* SID:148 */
    [LA_F149] = "%s: ignored in invalid state=%d\n",  /* SID:149 */
    [LA_F150] = "%s: ignored in state=%d\n",  /* SID:150 */
    [LA_F151] = "%s: ignored, upsert %s:%d failed",  /* SID:151 */
    [LA_F152] = "%s: invalid ack_seq=%u\n",  /* SID:152 */
    [LA_F153] = "%s: invalid cand idx: %d (count: %d)",  /* SID:153 */
    [LA_F154] = "%s: invalid cand_cnt=0\n",  /* SID:154 */
    [LA_F155] = "%s: invalid for non-relay req\n",  /* SID:155 */
    [LA_F156] = "%s: invalid payload len=%d (need 6)",  /* SID:156 */
    [LA_F157] = "%s: invalid seq=%u\n",  /* SID:157 */
    [LA_F158] = "%s: invalid ses_id=%u\n",  /* SID:158 */
    [LA_F159] = "%s: invalid session_id=0\n",  /* SID:159 */
    [LA_F160] = "%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n",  /* SID:160 */
    [LA_F161] = "%s: keep-alive sent (%d cands)",  /* SID:161 */
    [LA_F162] = "%s: mailbox empty, waiting",  /* SID:162 */
    [LA_F163] = "%s: missing session_id in payload\n",  /* SID:163 */
    [LA_F164] = "%s: new request (sid=%u) overrides pending request (sid=%u)\n",  /* SID:164 */
    [LA_F165] = "%s: no DTLS context for CRYPTO pkt \n",  /* SID:165 */
    [LA_F166] = "%s: no pending request\n",  /* SID:166 */
    [LA_F167] = "%s: no rpc request\n",  /* SID:167 */
    [LA_F168] = "%s: no ses_id for multi session\n",  /* SID:168 */
    [LA_F169] = "%s: no session for peer_id=%.*s (req_type=%u)\n",  /* SID:169 */
    [LA_F170] = "%s: no session for peer_id=%.*s\n",  /* SID:170 */
    [LA_F171] = "%s: no session for session_id=%u (req_type=%u)\n",  /* SID:171 */
    [LA_F172] = "%s: no session for session_id=%u\n",  /* SID:172 */
    [LA_F173] = "%s: not connected, cannot send FIN",  /* SID:173 */
    [LA_F174] = "%s: not supported by server\n",  /* SID:174 */
    [LA_F175] = "%s: offer %s (my gist=%s)",  /* SID:175 */
    [LA_F176] = "%s: offer confirmed",  /* SID:176 */
    [LA_F177] = "%s: offer overwritten by SUB heartbeat, resending",  /* SID:177 */
    [LA_F178] = "%s: old request ignored (sid=%u <= last_sid=%u)\n",  /* SID:178 */
    [LA_F179] = "%s: path rx UP (%s:%d)",  /* SID:179 */
    [LA_F180] = "%s: path tx UP",  /* SID:180 */
    [LA_F181] = "%s: path tx UP (echo seq=%u)",  /* SID:181 */
    [LA_F182] = "%s: path[%d] UP (%s:%d)",  /* SID:182 */
    [LA_F183] = "%s: path[%d] UP (recv DATA)",  /* SID:183 */
    [LA_F184] = "%s: path[%d] relay UP",  /* SID:184 */
    [LA_F185] = "%s: peer disconnected (ses_id=%u), reset to WAIT_PEER\n",  /* SID:185 */
    [LA_F186] = "%s: peer offline (sid=%u)\n",  /* SID:186 */
    [LA_F187] = "%s: peer offline in SYN0_ACK, waiting for peer to come online\n",  /* SID:187 */
    [LA_F188] = "%s: peer offline\n",  /* SID:188 */
    [LA_F189] = "%s: peer online, starting NAT punch\n",  /* SID:189 */
    [LA_F190] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:190 */
    [LA_F191] = "%s: pkt payload exceeds limit (%d > %d)\n",  /* SID:191 */
    [LA_F192] = "%s: pkt recv (ses_id=%u), inner type=%u\n",  /* SID:192 */
    [LA_F193] = "%s: processed sid=%u synced=%d\n",  /* SID:193 */
    [LA_F194] = "%s: promoted prflx cand[%d]<%s:%d> → %s\n",  /* SID:194 */
    [LA_F195] = "%s: protocol mismatch, recv PKT_ACK on trans=%s",  /* SID:195 */
    [LA_F196] = "%s: published %d candidates (ver=%d)",  /* SID:196 */
    [LA_F197] = "%s: publishing %d candidates (ver=%d) to local gist",  /* SID:197 */
    [LA_F198] = "%s: punch cand[%d] %s:%d (%s)",  /* SID:198 */
    [LA_F199] = "%s: punch remote cand[%d]<%s:%d> failed\n",  /* SID:199 */
    [LA_F200] = "%s: punch timeout, fallback punching using signaling relay",  /* SID:200 */
    [LA_F201] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:201 */
    [LA_F202] = "%s: push remote cand<%s:%d> failed(OOM)",  /* SID:202 */
    [LA_F203] = "%s: reaching alloc OOM",  /* SID:203 */
    [LA_F204] = "%s: reaching broadcast to %d cand(s), seq=%u",  /* SID:204 */
    [LA_F205] = "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u",  /* SID:205 */
    [LA_F206] = "%s: reaching cand[%d] via signaling relay, seq=%u",  /* SID:206 */
    [LA_F207] = "%s: reaching enqueued: cand[%d], seq=%u, priority=%u",  /* SID:207 */
    [LA_F208] = "%s: reaching relay via signaling FAILED (ret=%d), seq=%u",  /* SID:208 */
    [LA_F209] = "%s: reaching relay via signaling SUCCESS, seq=%u",  /* SID:209 */
    [LA_F210] = "%s: reaching updated: cand[%d], seq=%u->%u",  /* SID:210 */
    [LA_F211] = "%s: ready to start session\n",  /* SID:211 */
    [LA_F212] = "%s: received %d candidates (ver=%d) from %s",  /* SID:212 */
    [LA_F213] = "%s: received offer from %s (peer=%s) → SYNCING",  /* SID:213 */
    [LA_F214] = "%s: received offer from %s (peer=%s) → WAIT_STUN",  /* SID:214 */
    [LA_F215] = "%s: recorded peer conn_seq=%u for future CONN_ACK",  /* SID:215 */
    [LA_F216] = "%s: recv (ses_id=%u), type=%u\n",  /* SID:216 */
    [LA_F217] = "%s: recv from cand[%d]",  /* SID:217 */
    [LA_F218] = "%s: relay busy, will retry\n",  /* SID:218 */
    [LA_F219] = "%s: relay ready, flow control released\n",  /* SID:219 */
    [LA_F220] = "%s: remote %s cand<%s:%d> (disabled)\n",  /* SID:220 */
    [LA_F221] = "%s: remote %s cand[%d]<%s:%d> (disabled)\n",  /* SID:221 */
    [LA_F222] = "%s: remote %s cand[%d]<%s:%d> accepted\n",  /* SID:222 */
    [LA_F223] = "%s: remote_cands[] full, skipped %d candidates\n",  /* SID:223 */
    [LA_F224] = "%s: renew session (local=%u pkt=%u)\n",  /* SID:224 */
    [LA_F225] = "%s: req_type=%u code=%u msg=%s\n",  /* SID:225 */
    [LA_F226] = "%s: req_type=%u code=%u\n",  /* SID:226 */
    [LA_F227] = "%s: restarting periodic check",  /* SID:227 */
    [LA_F228] = "%s: retry(%d/%d) probe\n",  /* SID:228 */
    [LA_F229] = "%s: retry(%d/%d) req (sid=%u)\n",  /* SID:229 */
    [LA_F230] = "%s: retry(%d/%d) resp (sid=%u)\n",  /* SID:230 */
    [LA_F231] = "%s: retry, (attempt %d/%d)\n",  /* SID:231 */
    [LA_F232] = "%s: send failed(%d)",  /* SID:232 */
    [LA_F233] = "%s: send offer failed",  /* SID:233 */
    [LA_F234] = "%s: sent (ses_id=%u), sid=%u code=%u size=%d\n",  /* SID:234 */
    [LA_F235] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:235 */
    [LA_F236] = "%s: server rejected (no slot)\n",  /* SID:236 */
    [LA_F237] = "%s: sess_id=%u req_type=%u code=%u msg=%s\n",  /* SID:237 */
    [LA_F238] = "%s: sess_id=%u req_type=%u code=%u\n",  /* SID:238 */
    [LA_F239] = "%s: session established(st=%s peer=%s), %s\n",  /* SID:239 */
    [LA_F240] = "%s: session offer(st=%s peer=%s), %s\n",  /* SID:240 */
    [LA_F241] = "%s: session reset by peer(old=%u new=%u), %s\n",  /* SID:241 */
    [LA_F242] = "%s: session suspend(st=%s)\n",  /* SID:242 */
    [LA_F243] = "%s: session_id changed (old=%u new=%u)\n",  /* SID:243 */
    [LA_F244] = "%s: skip stale ver=0 from %s (previous session)",  /* SID:244 */
    [LA_F245] = "%s: skip stale ver=0 from SUB (previous session)",  /* SID:245 */
    [LA_F246] = "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n",  /* SID:246 */
    [LA_F247] = "%s: started, sending first probe\n",  /* SID:247 */
    [LA_F248] = "%s: stun collection ready, auto SYNC sent\n",  /* SID:248 */
    [LA_F249] = "%s: sync busy, will retry\n",  /* SID:249 */
    [LA_F250] = "%s: sync complete (ses_id=%u)\n",  /* SID:250 */
    [LA_F251] = "%s: sync complete (ses_id=%u, mask=0x%04x)\n",  /* SID:251 */
    [LA_F252] = "%s: sync confirm sid=%u synced=%d base=%d\n",  /* SID:252 */
    [LA_F253] = "%s: sync done, st=%s cands=%d\n",  /* SID:253 */
    [LA_F254] = "%s: sync done\n",  /* SID:254 */
    [LA_F255] = "%s: sync0 srflx cand[%d]<%s:%d>%s\n",  /* SID:255 */
    [LA_F256] = "%s: timeout (sid=%u)\n",  /* SID:256 */
    [LA_F257] = "%s: timeout after %d retries , type unknown\n",  /* SID:257 */
    [LA_F258] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:258 */
    [LA_F259] = "%s: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:259 */
    [LA_F260] = "%s: timeout, peer did not respond",  /* SID:260 */
    [LA_F261] = "%s: timeout, retry %d/%d",  /* SID:261 */
    [LA_F262] = "%s: trickle punch start",  /* SID:262 */
    [LA_F263] = "%s: triggered via COMPACT msg echo",  /* SID:263 */
    [LA_F264] = "%s: triggered via RELAY TUNE echo",  /* SID:264 */
    [LA_F265] = "%s: unexpected ack_seq=%u mask=0x%04x\n",  /* SID:265 */
    [LA_F266] = "%s: unexpected non-srflx cand in sync0, treating as srflx\n",  /* SID:266 */
    [LA_F267] = "%s: unexpected remote cand type %d, skipped\n",  /* SID:267 */
    [LA_F268] = "%s: unexpected s->id=0\n",  /* SID:268 */
    [LA_F269] = "%s: unexpected type 0x%02x\n",  /* SID:269 */
    [LA_F270] = "%s: unknown target cand %s:%d",  /* SID:270 */
    [LA_F271] = "%s: unsupported type 0x%02x\n",  /* SID:271 */
    [LA_F272] = "%s: writing heartbeat (gist=%s) %s",  /* SID:272 */
    [LA_F273] = "%s: → CLOSED (recv FIN)",  /* SID:273 */
    [LA_F274] = "%s: → READY",  /* SID:274 */
    [LA_F275] = "%s: → SYNCING",  /* SID:275 */
    [LA_F276] = "%s: → WAIT_STUN",  /* SID:276 */
    [LA_F277] = "%s:%04d: %s",  /* SID:277 */
    [LA_F278] = "%s_ACK sent to %s:%d (try), echo_seq=%u",  /* SID:278 */
    [LA_F279] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:279 */
    [LA_F280] = "Attempting Simultaneous Open to %s:%d",  /* SID:280 */
    [LA_F281] = "% BIO_new failed",  /* SID:281 */
    [LA_F282] = "% Base64 decode failed",  /* SID:282 */
    [LA_F283] = "% Bind failed",  /* SID:283 */
    [LA_F284] = "Bind failed to %d, port busy, trying random port",  /* SID:284 */
    [LA_F285] = "Bound to :%d",  /* SID:285 */
    [LA_F286] = "% Buffer size < 2048 may be insufficient for full SDP",  /* SID:286 */
    [LA_F287] = "CONNECT PUB: target=%s/%s",  /* SID:287 */
    [LA_F288] = "CONNECT SUB: waiting for offer (gist=%s/%s)",  /* SID:288 */
    [LA_F289] = "% CONNECT: instance not online yet",  /* SID:289 */
    [LA_F290] = "% Close P2P UDP socket",  /* SID:290 */
    [LA_F291] = "% Closing TCP connection to RELAY signaling server",  /* SID:291 */
    [LA_F292] = "Connect to COMPACT signaling server failed(%d)",  /* SID:292 */
    [LA_F293] = "Connect to RELAY signaling server failed(%d)",  /* SID:293 */
    [LA_F294] = "Crypto layer '%s' init failed, continuing without encryption",  /* SID:294 */
    [LA_F295] = "% DISCONNECT",  /* SID:295 */
    [LA_F296] = "% DTLS (MbedTLS) requested but library not linked",  /* SID:296 */
    [LA_F297] = "% DTLS handshake complete (MbedTLS)",  /* SID:297 */
    [LA_F298] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:298 */
    [LA_F299] = "Detect local network interfaces failed(%d)",  /* SID:299 */
    [LA_F300] = "Detection completed %s",  /* SID:300 */
    [LA_F301] = "Exported %d candidates to SDP (%d bytes)",  /* SID:301 */
    [LA_F302] = "% Failed to allocate DTLS context",  /* SID:302 */
    [LA_F303] = "% Failed to allocate OpenSSL context",  /* SID:303 */
    [LA_F304] = "% Failed to allocate memory for candidate lists",  /* SID:304 */
    [LA_F305] = "% Failed to allocate memory for instance",  /* SID:305 */
    [LA_F306] = "% Failed to allocate memory for session",  /* SID:306 */
    [LA_F307] = "% Failed to build STUN request",  /* SID:307 */
    [LA_F308] = "Failed to parse SDP candidate line: %s",  /* SID:308 */
    [LA_F309] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:309 */
    [LA_F310] = "Failed to reserve remote candidates (base=%u cnt=%d)\n",  /* SID:310 */
    [LA_F311] = "Failed to reserve remote candidates (cnt=%d)\n",  /* SID:311 */
    [LA_F312] = "% Failed to reserve remote candidates (cnt=1)\n",  /* SID:312 */
    [LA_F313] = "Failed to resolve STUN server %s",  /* SID:313 */
    [LA_F314] = "Failed to resolve TURN server: %s",  /* SID:314 */
    [LA_F315] = "Failed to send Allocate Request: %d",  /* SID:315 */
    [LA_F316] = "Failed to send STUN request: %d",  /* SID:316 */
    [LA_F317] = "% Failed to send Test I(alt), continue to Test III",  /* SID:317 */
    [LA_F318] = "% Failed to send punch packet for new peer addr\n",  /* SID:318 */
    [LA_F319] = "% Failed to start TURN allocation",  /* SID:319 */
    [LA_F320] = "% Full SDP generation requires ice_ufrag and ice_pwd",  /* SID:320 */
    [LA_F321] = "Gathered Host candidate: %s:%d (priority=0x%08x)",  /* SID:321 */
    [LA_F322] = "Gathered Host6 candidate: %s:%d (priority=0x%08x)",  /* SID:322 */
    [LA_F323] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:323 */
    [LA_F324] = "% Handshake complete",  /* SID:324 */
    [LA_F325] = "Handshake failed: %s (-0x%04x)",  /* SID:325 */
    [LA_F326] = "ICE credentials generated: ufrag=%s",  /* SID:326 */
    [LA_F327] = "% ICE-STUN: no session for ufrag, dropped",  /* SID:327 */
    [LA_F328] = "% IPv6 socket open failed, IPv6 candidates disabled",  /* SID:328 */
    [LA_F329] = "IPv6 socket opened [%s]:%d",  /* SID:329 */
    [LA_F330] = "Ignore %s pkt from %s:%d, not connected",  /* SID:330 */
    [LA_F331] = "Ignore %s pkt from %s:%d, not connecting",  /* SID:331 */
    [LA_F332] = "Ignore %s pkt from %s:%d, state=%d (not connected yet)",  /* SID:332 */
    [LA_F333] = "Ignore %s pkt from %s:%d, valid state(%d)",  /* SID:333 */
    [LA_F334] = "Ignore %s pkt from unknown path %s:%d",  /* SID:334 */
    [LA_F335] = "Imported %d candidates from SDP",  /* SID:335 */
    [LA_F336] = "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)",  /* SID:336 */
    [LA_F337] = "Initialize network subsystem failed(%d)",  /* SID:337 */
    [LA_F338] = "Initialize signaling mode: %d",  /* SID:338 */
    [LA_F339] = "Invalid IP address: %s",  /* SID:339 */
    [LA_F340] = "Invalid remote_peer_id for %s mode",  /* SID:340 */
    [LA_F341] = "% Invalid signaling mode in configuration",  /* SID:341 */
    [LA_F342] = "% LOST recovery: NAT connected but no path available",  /* SID:342 */
    [LA_F343] = "Local address detection done: %d IPv4, %d IPv6 address(es)",  /* SID:343 */
    [LA_F344] = "% MSG RPC not supported by server\n",  /* SID:344 */
    [LA_F345] = "% NAT connected but no available path in path manager",  /* SID:345 */
    [LA_F346] = "% NAT detection skipped (skip_stun_test=true), Srflx gathered",  /* SID:346 */
    [LA_F347] = "% No advanced transport layer enabled, using simple reliable layer",  /* SID:347 */
    [LA_F348] = "% No shared local route addresses available, host candidates skipped",  /* SID:348 */
    [LA_F349] = "% No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)",  /* SID:349 */
    [LA_F350] = "% OFF",  /* SID:350 */
    [LA_F351] = "Open P2P UDP socket on port %d",  /* SID:351 */
    [LA_F352] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:352 */
    [LA_F353] = "% OpenSSL requested but library not linked",  /* SID:353 */
    [LA_F354] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:354 */
    [LA_F355] = "% PUBSUB mode requires gh_token and gist_id",  /* SID:355 */
    [LA_F356] = "% PUBSUB mode: all candidate types (host/srflx/relay) disabled, no candidates possible",  /* SID:356 */
    [LA_F357] = "% PUBSUB mode: both IPv4 and IPv6 disabled, no candidates possible",  /* SID:357 */
    [LA_F358] = "PUBSUB online failed(%d)",  /* SID:358 */
    [LA_F359] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:359 */
    [LA_F360] = "Packet too large len=%d max=%d",  /* SID:360 */
    [LA_F361] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:361 */
    [LA_F362] = "% Path switch debounced, waiting for stability",  /* SID:362 */
    [LA_F363] = "Path switched to better route (idx=%d)",  /* SID:363 */
    [LA_F364] = "% PseudoTCP enabled as transport layer",  /* SID:364 */
    [LA_F365] = "REG to COMPACT signaling server at %s:%d",  /* SID:365 */
    [LA_F366] = "REG to PUBSUB signaling (Gist: %s)",  /* SID:366 */
    [LA_F367] = "REG to RELAY signaling server at %s:%d",  /* SID:367 */
    [LA_F368] = "% REG: auth_key acquired, auto SYN0 sent\n",  /* SID:368 */
    [LA_F369] = "REG: local_gist=%s peer=%s",  /* SID:369 */
    [LA_F370] = "REG: peer_id cannot contain '/' (got \"%s\")",  /* SID:370 */
    [LA_F371] = "% RELAY path but TURN not allocated",  /* SID:371 */
    [LA_F372] = "% RELAY path but TURN not allocated (dtls)",  /* SID:372 */
    [LA_F373] = "% RELAY recovery: NAT connected but no path available",  /* SID:373 */
    [LA_F374] = "RELAY sent (ses_id=%u), type=0x%02x seq=%u flags=0x%02x",  /* SID:374 */
    [LA_F375] = "% RELAY/COMPACT mode requires server_host",  /* SID:375 */
    [LA_F376] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:376 */
    [LA_F377] = "Recv %s pkt from %s:%d",  /* SID:377 */
    [LA_F378] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:378 */
    [LA_F379] = "Recv %s pkt from %s:%d seq=%u",  /* SID:379 */
    [LA_F380] = "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:380 */
    [LA_F381] = "Recv %s pkt from %s:%d, seq=%u, len=%d",  /* SID:381 */
    [LA_F382] = "Recv ICE-STUN Binding Request from candidate %d (%s:%d)",  /* SID:382 */
    [LA_F383] = "Recv ICE-STUN Binding Response from candidate %d (%s:%d)",  /* SID:383 */
    [LA_F384] = "Recv ICE-STUN from %s:%d, upsert prflx failed",  /* SID:384 */
    [LA_F385] = "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:385 */
    [LA_F386] = "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d",  /* SID:386 */
    [LA_F387] = "Reliable transport initialized rto=%d win=%d",  /* SID:387 */
    [LA_F388] = "Requested Relay Candidate from TURN %s",  /* SID:388 */
    [LA_F389] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:389 */
    [LA_F390] = "Reuse Relay Candidate %s:%u (priority=%u)",  /* SID:390 */
    [LA_F391] = "Reuse STUN Candidate %s:%u (priority=%u)",  /* SID:391 */
    [LA_F392] = "% SCTP (usrsctp) requested but library not linked",  /* SID:392 */
    [LA_F393] = "SDP REMOTE: %s cand<%s:%d> (disabled)",  /* SID:393 */
    [LA_F394] = "SDP REMOTE: %s cand[%d]<%s:%d> accepted",  /* SID:394 */
    [LA_F395] = "% SDP export buffer overflow",  /* SID:395 */
    [LA_F396] = "% SDP import failed or empty",  /* SID:396 */
    [LA_F397] = "% SIGNALING path but signaling relay not available",  /* SID:397 */
    [LA_F398] = "% SIGNALING path enabled (server supports relay)\n",  /* SID:398 */
    [LA_F399] = "% SSL_CTX_new failed",  /* SID:399 */
    [LA_F400] = "% SSL_new failed",  /* SID:400 */
    [LA_F401] = "STUN collecting to %s:%d (len=%d)",  /* SID:401 */
    [LA_F402] = "% STUN resources released (no active sessions)",  /* SID:402 */
    [LA_F403] = "SYN0: retry, (attempt %d/%d)\n",  /* SID:403 */
    [LA_F404] = "SYN0: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:404 */
    [LA_F405] = "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n",  /* SID:405 */
    [LA_F406] = "% SYNC(trickle): seq overflow, cannot trickle more\n",  /* SID:406 */
    [LA_F407] = "Send window full, dropping packet send_count=%d",  /* SID:407 */
    [LA_F408] = "Sending Allocate Request to %s:%d",  /* SID:408 */
    [LA_F409] = "% Sending FIN packet to peer before closing",  /* SID:409 */
    [LA_F410] = "% Sending OFF packet to COMPACT signaling server",  /* SID:410 */
    [LA_F411] = "Sending Test I to %s:%d (len=%d)",  /* SID:411 */
    [LA_F412] = "% Sending Test I(alt) to CHANGED-ADDRESS",  /* SID:412 */
    [LA_F413] = "% Sending Test II with CHANGE-REQUEST(IP+PORT)",  /* SID:413 */
    [LA_F414] = "% Sending Test III with CHANGE-REQUEST(PORT only)",  /* SID:414 */
    [LA_F415] = "% Skipping Host Candidate gathering (disabled)",  /* SID:415 */
    [LA_F416] = "Start COMPACT session failed(%d)",  /* SID:416 */
    [LA_F417] = "Start PUBSUB session failed(%d)",  /* SID:417 */
    [LA_F418] = "Start RELAY session failed(%d)",  /* SID:418 */
    [LA_F419] = "Start internal thread failed(%d)",  /* SID:419 */
    [LA_F420] = "Starting COMPACT session with %s",  /* SID:420 */
    [LA_F421] = "Starting RELAY session with %s",  /* SID:421 */
    [LA_F422] = "% Starting internal thread",  /* SID:422 */
    [LA_F423] = "State: LOST → CONNECTED, path=PUNCH[%d]",  /* SID:423 */
    [LA_F424] = "State: RELAY → CONNECTED, path=PUNCH[%d]",  /* SID:424 */
    [LA_F425] = "State: → CONNECTED, path[%d]",  /* SID:425 */
    [LA_F426] = "% State: → ERROR (punch timeout, no relay available)",  /* SID:426 */
    [LA_F427] = "% State: → LOST (all paths failed)",  /* SID:427 */
    [LA_F428] = "% State: → PUNCHING",  /* SID:428 */
    [LA_F429] = "State: → RELAY, path[%d]",  /* SID:429 */
    [LA_F430] = "% Stopping internal thread",  /* SID:430 */
    [LA_F431] = "TURN 401 Unauthorized (realm=%s), authenticating...",  /* SID:431 */
    [LA_F432] = "TURN Allocate failed with error %d",  /* SID:432 */
    [LA_F433] = "TURN Allocated relay %s:%u (lifetime=%us)",  /* SID:433 */
    [LA_F434] = "TURN CreatePermission failed (error=%d)",  /* SID:434 */
    [LA_F435] = "TURN CreatePermission for %s",  /* SID:435 */
    [LA_F436] = "TURN Data Indication from %s:%u (%d bytes)",  /* SID:436 */
    [LA_F437] = "TURN Refresh failed (error=%d)",  /* SID:437 */
    [LA_F438] = "TURN Refresh ok (lifetime=%us)",  /* SID:438 */
    [LA_F439] = "% TURN auth required but no credentials configured",  /* SID:439 */
    [LA_F440] = "Test I(alt): Mapped address: %s:%d",  /* SID:440 */
    [LA_F441] = "% Test I(alt): Timeout",  /* SID:441 */
    [LA_F442] = "Test I: Changed address: %s:%d",  /* SID:442 */
    [LA_F443] = "Test I: Mapped address: %s:%d",  /* SID:443 */
    [LA_F444] = "% Test I: Timeout",  /* SID:444 */
    [LA_F445] = "Test II: Success! Detection completed %s",  /* SID:445 */
    [LA_F446] = "% Test II: Timeout (need Test III)",  /* SID:446 */
    [LA_F447] = "Test III: Success! Detection completed %s",  /* SID:447 */
    [LA_F448] = "% Test III: Timeout",  /* SID:448 */
    [LA_F449] = "Transport layer '%s' init failed, falling back to simple reliable",  /* SID:449 */
    [LA_F450] = "Unknown candidate type: %s",  /* SID:450 */
    [LA_F451] = "Unknown signaling mode: %d",  /* SID:451 */
    [LA_F452] = "% WebRTC candidate export buffer overflow",  /* SID:452 */
    [LA_F453] = "[C] %s recv, len=%d\n",  /* SID:453 */
    [LA_F454] = "[C] %s recv, seq=%u, flags=0x%02x, len=%d\n",  /* SID:454 */
    [LA_F455] = "[C] %s recv, seq=%u, len=%d\n",  /* SID:455 */
    [LA_F456] = "[C] %s recv\n",  /* SID:456 */
    [LA_F457] = "[C] %s send failed(%d)\n",  /* SID:457 */
    [LA_F458] = "[C] %s send to port:%d failed(%d)\n",  /* SID:458 */
    [LA_F459] = "[C] %s send to port:%d, seq=%u, flags=0, len=0\n",  /* SID:459 */
    [LA_F460] = "[C] %s send, seq=0, flags=0x%02x, len=%d\n",  /* SID:460 */
    [LA_F461] = "[C] Unknown pkt type 0x%02x, len=%d\n",  /* SID:461 */
    [LA_F462] = "[C] relay payload too large: %d",  /* SID:462 */
    [LA_F463] = "[MbedTLS] DTLS role: %s (mode=%s)",  /* SID:463 */
    [LA_F464] = "% [OpenSSL] DTLS handshake completed",  /* SID:464 */
    [LA_F465] = "[OpenSSL] DTLS role: %s (mode=%s)",  /* SID:465 */
    [LA_F466] = "[R] %s recv, len=%d\n",  /* SID:466 */
    [LA_F467] = "[R] %s timeout\n",  /* SID:467 */
    [LA_F468] = "[R] %s%s qsend failed(OOM)\n",  /* SID:468 */
    [LA_F469] = "[R] %s%s qsend(%d), len=%u\n",  /* SID:469 */
    [LA_F470] = "[R] Connecting to %s:%d\n",  /* SID:470 */
    [LA_F471] = "% [R] Disconnected, back to REG state\n",  /* SID:471 */
    [LA_F472] = "% [R] Failed to create TCP socket\n",  /* SID:472 */
    [LA_F473] = "% [R] Failed to set socket non-blocking\n",  /* SID:473 */
    [LA_F474] = "[R] TCP connect failed(%d)\n",  /* SID:474 */
    [LA_F475] = "[R] TCP connect select failed(%d)\n",  /* SID:475 */
    [LA_F476] = "% [R] TCP connected immediately, sending REG\n",  /* SID:476 */
    [LA_F477] = "% [R] TCP connected, sending REG\n",  /* SID:477 */
    [LA_F478] = "% [R] TCP connection closed by peer\n",  /* SID:478 */
    [LA_F479] = "% [R] TCP connection closed during send\n",  /* SID:479 */
    [LA_F480] = "[R] TCP recv error(%d)\n",  /* SID:480 */
    [LA_F481] = "[R] TCP send error(%d)\n",  /* SID:481 */
    [LA_F482] = "[R] Unknown proto type %d\n",  /* SID:482 */
    [LA_F483] = "[R] payload size %u exceeds limit %u\n",  /* SID:483 */
    [LA_F484] = "[SCTP] association lost/shutdown (state=%u)",  /* SID:484 */
    [LA_F485] = "[SCTP] bind failed: %s",  /* SID:485 */
    [LA_F486] = "[SCTP] connect failed: %s",  /* SID:486 */
    [LA_F487] = "[SCTP] sendv failed: %s",  /* SID:487 */
    [LA_F488] = "[ST:%s] peer went offline, waiting for reconnect\n",  /* SID:488 */
    [LA_F489] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:489 */
    [LA_F490] = "% connection closed by peer",  /* SID:490 */
    [LA_F491] = "ctr_drbg_seed failed: -0x%x",  /* SID:491 */
    [LA_F492] = "retry seq=%u retx=%d rto=%d",  /* SID:492 */
    [LA_F493] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:493 */
    [LA_F494] = "ssl_config_defaults failed: -0x%x",  /* SID:494 */
    [LA_F495] = "ssl_setup failed: -0x%x",  /* SID:495 */
    [LA_F496] = "transport send_data failed, %d bytes dropped",  /* SID:496 */
    [LA_F497] = "✓ Gathered Srflx Candidate %s:%d, priority=%u (ses_id=%u)",  /* SID:497 */
    [LA_F498] = "% ✗ Add Srflx candidate failed(OOM)",  /* SID:498 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_p2p_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
