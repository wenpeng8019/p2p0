/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "alive",  /* SID:1 new */
    [LA_W2] = "Detecting...",  /* SID:2 new */
    [LA_W3] = "Full Cone NAT",  /* SID:3 new */
    [LA_W4] = "Open Internet (No NAT)",  /* SID:4 new */
    [LA_W5] = "Port Restricted Cone NAT",  /* SID:5 new */
    [LA_W6] = "PUB",  /* SID:6 new */
    [LA_W7] = "punch",  /* SID:7 new */
    [LA_W8] = "Restricted Cone NAT",  /* SID:8 new */
    [LA_W9] = "retry",  /* SID:9 new */
    [LA_W10] = "SUB",  /* SID:10 new */
    [LA_W11] = "Symmetric NAT (port-random)",  /* SID:11 new */
    [LA_W12] = "Timeout (no response)",  /* SID:12 new */
    [LA_W13] = "UDP Blocked (STUN unreachable)",  /* SID:13 new */
    [LA_W14] = "Undetectable (no STUN/probe configured)",  /* SID:14 new */
    [LA_W15] = "Unknown",  /* SID:15 new */
    [LA_S16] = "%s: address exchange failed: peer OFFLINE",  /* SID:16 new */
    [LA_S17] = "%s: address exchange success, sending UDP probe",  /* SID:17 new */
    [LA_S18] = "%s: already running, cannot trigger again",  /* SID:18 new */
    [LA_S19] = "%s: peer is OFFLINE",  /* SID:19 new */
    [LA_S20] = "%s: peer is online, waiting echo",  /* SID:20 new */
    [LA_S21] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:21 new */
    [LA_S22] = "%s: TURN allocated, starting address exchange",  /* SID:22 new */
    [LA_S23] = "[SCTP] association established",  /* SID:23 new */
    [LA_S24] = "[SCTP] usrsctp initialized, connecting...",  /* SID:24 new */
    [LA_S25] = "[SCTP] usrsctp_socket failed",  /* SID:25 new */
    [LA_S26] = "Channel ID validation failed",  /* SID:26 new */
    [LA_S27] = "Detecting local network addresses",  /* SID:27 new */
    [LA_S28] = "Gist GET failed",  /* SID:28 new */
    [LA_S29] = "Invalid channel_id format (security risk)",  /* SID:29 new */
    [LA_S30] = "Out of memory",  /* SID:30 new */
    [LA_S31] = "Push local cand<%s:%d> failed(OOM)\n",  /* SID:31 new */
    [LA_S32] = "Push remote cand<%s:%d> failed(OOM)\n",  /* SID:32 new */
    [LA_S33] = "resync candidates",  /* SID:33 new */
    [LA_S34] = "sync candidates",  /* SID:34 new */
    [LA_S35] = "waiting for peer",  /* SID:35 new */
    [LA_F36] = "  [%d] %s/%d",  /* SID:36 new */
    [LA_F37] = "%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n",  /* SID:37 new */
    [LA_F38] = "%s NOTIFY: accepted\n",  /* SID:38 new */
    [LA_F39] = "%s NOTIFY: ignored old notify base=%u (current=%u)\n",  /* SID:39 new */
    [LA_F40] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n",  /* SID:40 new */
    [LA_F41] = "%s NOTIFY: srflx addr update (disabled)\n",  /* SID:41 new */
    [LA_F42] = "%s accepted (ses_id=%u), sid=%u code=%u len=%u\n",  /* SID:42 new */
    [LA_F43] = "%s accepted (ses_id=%u), sid=%u msg=%u\n",  /* SID:43 new */
    [LA_F44] = "%s accepted (ses_id=%u), sid=%u\n",  /* SID:44 new */
    [LA_F45] = "%s accepted (ses_id=%u), waiting for response (sid=%u)\n",  /* SID:45 new */
    [LA_F46] = "%s msg=0 accepted (ses_id=%u), echo reply sid=%u len=%d\n",  /* SID:46 new */
    [LA_F47] = "%s msg=0: echo reply (sid=%u)\n",  /* SID:47 new */
    [LA_F48] = "%s req (ses_id=%u), sid=%u msg=%u len=%d\n",  /* SID:48 new */
    [LA_F49] = "%s req accepted (ses_id=%u), sid=%u msg=%u\n",  /* SID:49 new */
    [LA_F50] = "%s resent (ses_id=%u), (total=%d, err=%d)/%d\n",  /* SID:50 new */
    [LA_F51] = "%s resp (ses_id=%u), sid=%u code=%u len=%d\n",  /* SID:51 new */
    [LA_F52] = "%s sent (ses_id=%u), seq=%u\n",  /* SID:52 new */
    [LA_F53] = "%s sent (ses_id=%u), sid=%u msg=%u size=%d\n",  /* SID:53 new */
    [LA_F54] = "%s sent (ses_id=%u), sid=%u\n",  /* SID:54 new */
    [LA_F55] = "%s sent (ses_id=%u), total=%d, err=%d\n",  /* SID:55 new */
    [LA_F56] = "%s sent (ses_id=%u)\n",  /* SID:56 new */
    [LA_F57] = "%s sent to %s:%d",  /* SID:57 new */
    [LA_F58] = "%s sent to %s:%d (writable), echo_seq=%u",  /* SID:58 new */
    [LA_F59] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:59 new */
    [LA_F60] = "%s sent via best path[%d] to %s:%d, echo_seq=%u",  /* SID:60 new */
    [LA_F61] = "%s sent via signaling relay",  /* SID:61 new */
    [LA_F62] = "%s sent, auth_key=%llu, remote='%.32s', cands=%d\n",  /* SID:62 new */
    [LA_F63] = "%s sent, inst_id=%u\n",  /* SID:63 new */
    [LA_F64] = "%s sent, name='%s' rid=%u\n",  /* SID:64 new */
    [LA_F65] = "%s sent, retry=%u\n",  /* SID:65 new */
    [LA_F66] = "%s sent, ses_id=%u cand_base=%d, cand_cnt=%d fin=%d\n",  /* SID:66 new */
    [LA_F67] = "%s sent, ses_id=%u\n",  /* SID:67 new */
    [LA_F68] = "%s sent, target='%s' cand=%u\n",  /* SID:68 new */
    [LA_F69] = "%s sent\n",  /* SID:69 new */
    [LA_F70] = "%s skipped: auth_key=0\n",  /* SID:70 new */
    [LA_F71] = "%s throttled: awaiting READY\n",  /* SID:71 new */
    [LA_F72] = "%s trickle (ses_id=%u), cnt=%d, seq=%u \n",  /* SID:72 new */
    [LA_F73] = "%s, retry remaining candidates and FIN to peer\n",  /* SID:73 new */
    [LA_F74] = "%s: %s timeout after %d retries (sid=%u)\n",  /* SID:74 new */
    [LA_F75] = "%s: %s → %s (recv DATA)",  /* SID:75 new */
    [LA_F76] = "%s: CONN ignored, upsert %s:%d failed",  /* SID:76 new */
    [LA_F77] = "%s: CONN timeout after %llums",  /* SID:77 new */
    [LA_F78] = "%s: CONNECTED → LOST (no response %llums)\n",  /* SID:78 new */
    [LA_F79] = "%s: CONNECTING → %s (recv CONN)",  /* SID:79 new */
    [LA_F80] = "%s: CONNECTING → %s (recv CONN_ACK)",  /* SID:80 new */
    [LA_F81] = "%s: CONNECTING → CLOSED (timeout, no relay)",  /* SID:81 new */
    [LA_F82] = "%s: CONN_ACK ignored, upsert %s:%d failed",  /* SID:82 new */
    [LA_F83] = "%s: PUNCHING → %s",  /* SID:83 new */
    [LA_F84] = "%s: PUNCHING → %s (peer CONNECTING)",  /* SID:84 new */
    [LA_F85] = "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)",  /* SID:85 new */
    [LA_F86] = "%s: PUNCHING → CONNECTING (%s%s)",  /* SID:86 new */
    [LA_F87] = "%s: PUNCHING → RELAY (peer CONNECTING)",  /* SID:87 new */
    [LA_F88] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n",  /* SID:88 new */
    [LA_F89] = "%s: Peer addr changed -> %s:%d, retrying punch\n",  /* SID:89 new */
    [LA_F90] = "%s: RELAY → CONNECTED (direct path recovered)",  /* SID:90 new */
    [LA_F91] = "%s: RPC complete (sid=%u)\n",  /* SID:91 new */
    [LA_F92] = "%s: RPC fail due to peer offline (sid=%u)\n",  /* SID:92 new */
    [LA_F93] = "%s: RPC fail due to relay timeout (sid=%u)\n",  /* SID:93 new */
    [LA_F94] = "%s: RPC finished (sid=%u)\n",  /* SID:94 new */
    [LA_F95] = "%s: SIGNALING path enabled (server supports relay)\n",  /* SID:95 new */
    [LA_F96] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:96 new */
    [LA_F97] = "%s: TURN allocation failed: ret=%d",  /* SID:97 new */
    [LA_F98] = "%s: TURN allocation request sent",  /* SID:98 new */
    [LA_F99] = "%s: UDP timeout, retry %d/%d",  /* SID:99 new */
    [LA_F100] = "%s: UDP timeout: peer not responding",  /* SID:100 new */
    [LA_F101] = "%s: accepted",  /* SID:101 new */
    [LA_F102] = "%s: accepted (ses_id=%u), peer=%s\n",  /* SID:102 new */
    [LA_F103] = "%s: accepted (ses_id=%u)\n",  /* SID:103 new */
    [LA_F104] = "%s: accepted as cand[%d], target=%s:%d",  /* SID:104 new */
    [LA_F105] = "%s: accepted cand_cnt=%d\n",  /* SID:105 new */
    [LA_F106] = "%s: accepted for ack_seq=%u\n",  /* SID:106 new */
    [LA_F107] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n",  /* SID:107 new */
    [LA_F108] = "%s: accepted, cand_max=%d%s relay=%s msg=%s\n",  /* SID:108 new */
    [LA_F109] = "%s: accepted, probe_mapped=%s:%d\n",  /* SID:109 new */
    [LA_F110] = "%s: accepted, public=%s:%d auth_key=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n",  /* SID:110 new */
    [LA_F111] = "%s: accepted\n",  /* SID:111 new */
    [LA_F112] = "%s: auth_key acquired, auto SYNC0 sent\n",  /* SID:112 new */
    [LA_F113] = "%s: auth_key acquired, waiting stun pending\n",  /* SID:113 new */
    [LA_F114] = "%s: bad FIN marker=0x%02x\n",  /* SID:114 new */
    [LA_F115] = "%s: bad payload len=%d\n",  /* SID:115 new */
    [LA_F116] = "%s: bad payload(%d)",  /* SID:116 new */
    [LA_F117] = "%s: bad payload(%d)\n",  /* SID:117 new */
    [LA_F118] = "%s: bad payload(len=%d cand_cnt=%d)\n",  /* SID:118 new */
    [LA_F119] = "%s: bad payload(len=%d)\n",  /* SID:119 new */
    [LA_F120] = "%s: batch punch skip (state=%d, use trickle)",  /* SID:120 new */
    [LA_F121] = "%s: batch punch start (%d cands)",  /* SID:121 new */
    [LA_F122] = "%s: batch punch: no cand, wait trickle",  /* SID:122 new */
    [LA_F123] = "%s: cand[%d] payload too large for multi_session (%d)",  /* SID:123 new */
    [LA_F124] = "%s: cand[%d]<%s:%d> send packet failed(%d)",  /* SID:124 new */
    [LA_F125] = "%s: complete (ses_id=%u), sid=%u code=%u\n",  /* SID:125 new */
    [LA_F126] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n",  /* SID:126 new */
    [LA_F127] = "%s: duplicate remote cand<%s:%d> from signaling, skipped\n",  /* SID:127 new */
    [LA_F128] = "%s: duplicate remote cand<%s:%d>, skipped\n",  /* SID:128 new */
    [LA_F129] = "%s: duplicate request ignored (sid=%u)\n",  /* SID:129 new */
    [LA_F130] = "%s: duplicate request ignored (sid=%u, already processing)\n",  /* SID:130 new */
    [LA_F131] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n",  /* SID:131 new */
    [LA_F132] = "%s: entered early, %s arrived before SYNC0\n",  /* SID:132 new */
    [LA_F133] = "%s: entered, %s arrived\n",  /* SID:133 new */
    [LA_F134] = "%s: entered, peer online in SYNC0_ACK\n",  /* SID:134 new */
    [LA_F135] = "%s: exchange timeout, retry %d/%d",  /* SID:135 new */
    [LA_F136] = "%s: exchange timeout: peer not responding",  /* SID:136 new */
    [LA_F137] = "%s: fatal error code=%u, entering ERROR state\n",  /* SID:137 new */
    [LA_F138] = "%s: ignored for duplicated seq=%u, already acked\n",  /* SID:138 new */
    [LA_F139] = "%s: ignored for seq=%u (expect=%d)\n",  /* SID:139 new */
    [LA_F140] = "%s: ignored for sid=%u (current sid=%u)\n",  /* SID:140 new */
    [LA_F141] = "%s: ignored in invalid state=%d\n",  /* SID:141 new */
    [LA_F142] = "%s: ignored in state=%d\n",  /* SID:142 new */
    [LA_F143] = "%s: ignored, upsert %s:%d failed",  /* SID:143 new */
    [LA_F144] = "%s: invalid ack_seq=%u\n",  /* SID:144 new */
    [LA_F145] = "%s: invalid cand idx: %d (count: %d)",  /* SID:145 new */
    [LA_F146] = "%s: invalid cand_cnt=0\n",  /* SID:146 new */
    [LA_F147] = "%s: invalid for non-relay req\n",  /* SID:147 new */
    [LA_F148] = "%s: invalid payload len=%d (need 6)",  /* SID:148 new */
    [LA_F149] = "%s: invalid seq=%u\n",  /* SID:149 new */
    [LA_F150] = "%s: invalid ses_id=%u\n",  /* SID:150 new */
    [LA_F151] = "%s: invalid session_id=0\n",  /* SID:151 new */
    [LA_F152] = "%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n",  /* SID:152 new */
    [LA_F153] = "%s: keep-alive sent (%d cands)",  /* SID:153 new */
    [LA_F154] = "%s: missing session_id in payload\n",  /* SID:154 new */
    [LA_F155] = "%s: new request (sid=%u) overrides pending request (sid=%u)\n",  /* SID:155 new */
    [LA_F156] = "%s: no DTLS context for CRYPTO pkt \n",  /* SID:156 new */
    [LA_F157] = "%s: no pending request\n",  /* SID:157 new */
    [LA_F158] = "%s: no rpc request\n",  /* SID:158 new */
    [LA_F159] = "%s: no ses_id for multi session\n",  /* SID:159 new */
    [LA_F160] = "%s: no session for peer_id=%.*s (req_type=%u)\n",  /* SID:160 new */
    [LA_F161] = "%s: no session for peer_id=%.*s\n",  /* SID:161 new */
    [LA_F162] = "%s: no session for session_id=%u (req_type=%u)\n",  /* SID:162 new */
    [LA_F163] = "%s: no session for session_id=%u\n",  /* SID:163 new */
    [LA_F164] = "%s: not connected, cannot send FIN",  /* SID:164 new */
    [LA_F165] = "%s: not supported by server\n",  /* SID:165 new */
    [LA_F166] = "%s: old request ignored (sid=%u <= last_sid=%u)\n",  /* SID:166 new */
    [LA_F167] = "%s: path rx UP (%s:%d)",  /* SID:167 new */
    [LA_F168] = "%s: path tx UP",  /* SID:168 new */
    [LA_F169] = "%s: path tx UP (echo seq=%u)",  /* SID:169 new */
    [LA_F170] = "%s: path[%d] UP (%s:%d)",  /* SID:170 new */
    [LA_F171] = "%s: path[%d] UP (recv DATA)",  /* SID:171 new */
    [LA_F172] = "%s: path[%d] relay UP",  /* SID:172 new */
    [LA_F173] = "%s: peer disconnected (ses_id=%u), reset to WAIT_PEER\n",  /* SID:173 new */
    [LA_F174] = "%s: peer offline (sid=%u)\n",  /* SID:174 new */
    [LA_F175] = "%s: peer offline in SYNC0_ACK, waiting for peer to come online\n",  /* SID:175 new */
    [LA_F176] = "%s: peer offline\n",  /* SID:176 new */
    [LA_F177] = "%s: peer online, starting NAT punch\n",  /* SID:177 new */
    [LA_F178] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:178 new */
    [LA_F179] = "%s: pkt payload exceeds limit (%d > %d)\n",  /* SID:179 new */
    [LA_F180] = "%s: pkt recv (ses_id=%u), inner type=%u\n",  /* SID:180 new */
    [LA_F181] = "%s: processed, synced=%d\n",  /* SID:181 new */
    [LA_F182] = "%s: promoted prflx cand[%d]<%s:%d> → %s\n",  /* SID:182 new */
    [LA_F183] = "%s: protocol mismatch, recv PKT_ACK on trans=%s",  /* SID:183 new */
    [LA_F184] = "%s: punch cand[%d] %s:%d (%s)",  /* SID:184 new */
    [LA_F185] = "%s: punch remote cand[%d]<%s:%d> failed\n",  /* SID:185 new */
    [LA_F186] = "%s: punch timeout, fallback punching using signaling relay",  /* SID:186 new */
    [LA_F187] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:187 new */
    [LA_F188] = "%s: push remote cand<%s:%d> failed(OOM)",  /* SID:188 new */
    [LA_F189] = "%s: reaching alloc OOM",  /* SID:189 new */
    [LA_F190] = "%s: reaching broadcast to %d cand(s), seq=%u",  /* SID:190 new */
    [LA_F191] = "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u",  /* SID:191 new */
    [LA_F192] = "%s: reaching cand[%d] via signaling relay, seq=%u",  /* SID:192 new */
    [LA_F193] = "%s: reaching enqueued: cand[%d], seq=%u, priority=%u",  /* SID:193 new */
    [LA_F194] = "%s: reaching relay via signaling FAILED (ret=%d), seq=%u",  /* SID:194 new */
    [LA_F195] = "%s: reaching relay via signaling SUCCESS, seq=%u",  /* SID:195 new */
    [LA_F196] = "%s: reaching updated: cand[%d], seq=%u->%u",  /* SID:196 new */
    [LA_F197] = "%s: ready to start session\n",  /* SID:197 new */
    [LA_F198] = "%s: recorded peer conn_seq=%u for future CONN_ACK",  /* SID:198 new */
    [LA_F199] = "%s: recv (ses_id=%u), type=%u\n",  /* SID:199 new */
    [LA_F200] = "%s: recv from cand[%d]",  /* SID:200 new */
    [LA_F201] = "%s: relay busy, will retry\n",  /* SID:201 new */
    [LA_F202] = "%s: relay ready, flow control released\n",  /* SID:202 new */
    [LA_F203] = "%s: remote %s cand<%s:%d> (disabled)\n",  /* SID:203 new */
    [LA_F204] = "%s: remote %s cand[%d]<%s:%d> (disabled)\n",  /* SID:204 new */
    [LA_F205] = "%s: remote %s cand[%d]<%s:%d> accepted\n",  /* SID:205 new */
    [LA_F206] = "%s: remote_cands[] full, skipped %d candidates\n",  /* SID:206 new */
    [LA_F207] = "%s: renew session (local=%u pkt=%u)\n",  /* SID:207 new */
    [LA_F208] = "%s: req_type=%u code=%u msg=%s\n",  /* SID:208 new */
    [LA_F209] = "%s: req_type=%u code=%u\n",  /* SID:209 new */
    [LA_F210] = "%s: restarting periodic check",  /* SID:210 new */
    [LA_F211] = "%s: retry(%d/%d) probe\n",  /* SID:211 new */
    [LA_F212] = "%s: retry(%d/%d) req (sid=%u)\n",  /* SID:212 new */
    [LA_F213] = "%s: retry(%d/%d) resp (sid=%u)\n",  /* SID:213 new */
    [LA_F214] = "%s: retry, (attempt %d/%d)\n",  /* SID:214 new */
    [LA_F215] = "%s: send failed(%d)",  /* SID:215 new */
    [LA_F216] = "%s: sent (ses_id=%u), sid=%u code=%u size=%d\n",  /* SID:216 new */
    [LA_F217] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:217 new */
    [LA_F218] = "%s: server rejected (no slot)\n",  /* SID:218 new */
    [LA_F219] = "%s: sess_id=%u req_type=%u code=%u msg=%s\n",  /* SID:219 new */
    [LA_F220] = "%s: sess_id=%u req_type=%u code=%u\n",  /* SID:220 new */
    [LA_F221] = "%s: session established(st=%s peer=%s), %s\n",  /* SID:221 new */
    [LA_F222] = "%s: session offer(st=%s peer=%s), waiting for peer\n",  /* SID:222 new */
    [LA_F223] = "%s: session reset by peer(st=%s old=%u new=%u), %s\n",  /* SID:223 new */
    [LA_F224] = "%s: session suspend(st=%s)\n",  /* SID:224 new */
    [LA_F225] = "%s: session_id changed (old=%u new=%u)\n",  /* SID:225 new */
    [LA_F226] = "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n",  /* SID:226 new */
    [LA_F227] = "%s: started, sending first probe\n",  /* SID:227 new */
    [LA_F228] = "%s: sync ack confirmed cnt=%d exceeds unacked cnt=%d\n",  /* SID:228 new */
    [LA_F229] = "%s: sync busy, will retry\n",  /* SID:229 new */
    [LA_F230] = "%s: sync complete (ses_id=%u)\n",  /* SID:230 new */
    [LA_F231] = "%s: sync complete (ses_id=%u, mask=0x%04x)\n",  /* SID:231 new */
    [LA_F232] = "%s: sync done, st=%s cands=%d\n",  /* SID:232 new */
    [LA_F233] = "%s: sync done\n",  /* SID:233 new */
    [LA_F234] = "%s: sync fin ack, but cand synced cnt not match sent cnt (cand=%d synced=%d)\n",  /* SID:234 new */
    [LA_F235] = "%s: sync forwarded, confirmed=%d synced=%d\n",  /* SID:235 new */
    [LA_F236] = "%s: sync0 srflx cand[%d]<%s:%d>%s\n",  /* SID:236 new */
    [LA_F237] = "%s: syncable ready, auto SYNC0 sent\n",  /* SID:237 new */
    [LA_F238] = "%s: timeout (sid=%u)\n",  /* SID:238 new */
    [LA_F239] = "%s: timeout after %d retries , type unknown\n",  /* SID:239 new */
    [LA_F240] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:240 new */
    [LA_F241] = "%s: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:241 new */
    [LA_F242] = "%s: timeout, peer did not respond",  /* SID:242 new */
    [LA_F243] = "%s: timeout, retry %d/%d",  /* SID:243 new */
    [LA_F244] = "%s: trickle punch start",  /* SID:244 new */
    [LA_F245] = "%s: triggered via COMPACT msg echo",  /* SID:245 new */
    [LA_F246] = "%s: triggered via RELAY TUNE echo",  /* SID:246 new */
    [LA_F247] = "%s: unexpected ack_seq=%u mask=0x%04x\n",  /* SID:247 new */
    [LA_F248] = "%s: unexpected non-srflx cand in sync0, treating as srflx\n",  /* SID:248 new */
    [LA_F249] = "%s: unexpected remote cand type %d, skipped\n",  /* SID:249 new */
    [LA_F250] = "%s: unexpected s->id=0\n",  /* SID:250 new */
    [LA_F251] = "%s: unexpected type 0x%02x\n",  /* SID:251 new */
    [LA_F252] = "%s: unknown target cand %s:%d",  /* SID:252 new */
    [LA_F253] = "%s: unsupported type 0x%02x\n",  /* SID:253 new */
    [LA_F254] = "%s: → CLOSED (recv FIN)",  /* SID:254 new */
    [LA_F255] = "%s:%04d: %s",  /* SID:255 new */
    [LA_F256] = "%s_ACK sent to %s:%d (try), echo_seq=%u",  /* SID:256 new */
    [LA_F257] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:257 new */
    [LA_F258] = "% Answer already present, skipping offer re-publish",  /* SID:258 new */
    [LA_F259] = "Attempting Simultaneous Open to %s:%d",  /* SID:259 new */
    [LA_F260] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:260 new */
    [LA_F261] = "% BIO_new failed",  /* SID:261 new */
    [LA_F262] = "% Base64 decode failed",  /* SID:262 new */
    [LA_F263] = "% Bind failed",  /* SID:263 new */
    [LA_F264] = "Bind failed to %d, port busy, trying random port",  /* SID:264 new */
    [LA_F265] = "Bound to :%d",  /* SID:265 new */
    [LA_F266] = "% Buffer size < 2048 may be insufficient for full SDP",  /* SID:266 new */
    [LA_F267] = "% Close P2P UDP socket",  /* SID:267 new */
    [LA_F268] = "% Closing TCP connection to RELAY signaling server",  /* SID:268 new */
    [LA_F269] = "Connect to COMPACT signaling server failed(%d)",  /* SID:269 new */
    [LA_F270] = "Connect to RELAY signaling server failed(%d)",  /* SID:270 new */
    [LA_F271] = "Crypto layer '%s' init failed, continuing without encryption",  /* SID:271 new */
    [LA_F272] = "% DTLS (MbedTLS) requested but library not linked",  /* SID:272 new */
    [LA_F273] = "% DTLS handshake complete (MbedTLS)",  /* SID:273 new */
    [LA_F274] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:274 new */
    [LA_F275] = "Detect local network interfaces failed(%d)",  /* SID:275 new */
    [LA_F276] = "Detection completed %s",  /* SID:276 new */
    [LA_F277] = "Duplicate remote cand<%s:%d> from signaling, skipped",  /* SID:277 new */
    [LA_F278] = "Exported %d candidates to SDP (%d bytes)",  /* SID:278 new */
    [LA_F279] = "% Failed to allocate DTLS context",  /* SID:279 new */
    [LA_F280] = "% Failed to allocate OpenSSL context",  /* SID:280 new */
    [LA_F281] = "% Failed to allocate memory for candidate lists",  /* SID:281 new */
    [LA_F282] = "% Failed to allocate memory for instance",  /* SID:282 new */
    [LA_F283] = "% Failed to allocate memory for session",  /* SID:283 new */
    [LA_F284] = "% Failed to build STUN request",  /* SID:284 new */
    [LA_F285] = "Failed to parse SDP candidate line: %s",  /* SID:285 new */
    [LA_F286] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:286 new */
    [LA_F287] = "Failed to reserve remote candidates (base=%u cnt=%d)\n",  /* SID:287 new */
    [LA_F288] = "Failed to reserve remote candidates (cnt=%d)\n",  /* SID:288 new */
    [LA_F289] = "% Failed to reserve remote candidates (cnt=1)\n",  /* SID:289 new */
    [LA_F290] = "Failed to resolve STUN server %s",  /* SID:290 new */
    [LA_F291] = "Failed to resolve TURN server: %s",  /* SID:291 new */
    [LA_F292] = "Failed to send Allocate Request: %d",  /* SID:292 new */
    [LA_F293] = "Failed to send STUN request: %d",  /* SID:293 new */
    [LA_F294] = "% Failed to send Test I(alt), continue to Test III",  /* SID:294 new */
    [LA_F295] = "% Failed to send punch packet for new peer addr\n",  /* SID:295 new */
    [LA_F296] = "% Failed to start TURN allocation",  /* SID:296 new */
    [LA_F297] = "Field %s is empty or too short",  /* SID:297 new */
    [LA_F298] = "% Full SDP generation requires ice_ufrag and ice_pwd",  /* SID:298 new */
    [LA_F299] = "Gathered Host candidate: %s:%d (priority=0x%08x)",  /* SID:299 new */
    [LA_F300] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:300 new */
    [LA_F301] = "% Handshake complete",  /* SID:301 new */
    [LA_F302] = "Handshake failed: %s (-0x%04x)",  /* SID:302 new */
    [LA_F303] = "Ignore %s pkt from %s:%d, not connected",  /* SID:303 new */
    [LA_F304] = "Ignore %s pkt from %s:%d, not connecting",  /* SID:304 new */
    [LA_F305] = "Ignore %s pkt from %s:%d, state=%d (not connected yet)",  /* SID:305 new */
    [LA_F306] = "Ignore %s pkt from %s:%d, valid state(%d)",  /* SID:306 new */
    [LA_F307] = "Ignore %s pkt from unknown path %s:%d",  /* SID:307 new */
    [LA_F308] = "Imported %d candidates from SDP",  /* SID:308 new */
    [LA_F309] = "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)",  /* SID:309 new */
    [LA_F310] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:310 new */
    [LA_F311] = "Initialize network subsystem failed(%d)",  /* SID:311 new */
    [LA_F312] = "Initialize signaling mode: %d",  /* SID:312 new */
    [LA_F313] = "Initialized: %s",  /* SID:313 new */
    [LA_F314] = "Invalid IP address: %s",  /* SID:314 new */
    [LA_F315] = "Invalid remote_peer_id for %s mode",  /* SID:315 new */
    [LA_F316] = "% Invalid signaling mode in configuration",  /* SID:316 new */
    [LA_F317] = "% LOST recovery: NAT connected but no path available",  /* SID:317 new */
    [LA_F318] = "Local address detection done: %d address(es)",  /* SID:318 new */
    [LA_F319] = "Login to COMPACT signaling server at %s:%d",  /* SID:319 new */
    [LA_F320] = "Login to RELAY signaling server at %s:%d",  /* SID:320 new */
    [LA_F321] = "% MSG RPC not supported by server\n",  /* SID:321 new */
    [LA_F322] = "% NAT connected but no available path in path manager",  /* SID:322 new */
    [LA_F323] = "% NAT detection skipped (skip_stun_test=true), Srflx gathered",  /* SID:323 new */
    [LA_F324] = "% No advanced transport layer enabled, using simple reliable layer",  /* SID:324 new */
    [LA_F325] = "% No auth_key provided, using default key (insecure)",  /* SID:325 new */
    [LA_F326] = "% No shared local route addresses available, host candidates skipped",  /* SID:326 new */
    [LA_F327] = "% No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)",  /* SID:327 new */
    [LA_F328] = "% ONLINE: auth_key acquired, auto SYNC0 sent\n",  /* SID:328 new */
    [LA_F329] = "% ONLINE: auth_key acquired, waiting stun pending\n",  /* SID:329 new */
    [LA_F330] = "Open P2P UDP socket on port %d",  /* SID:330 new */
    [LA_F331] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:331 new */
    [LA_F332] = "% OpenSSL requested but library not linked",  /* SID:332 new */
    [LA_F333] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:333 new */
    [LA_F334] = "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:334 new */
    [LA_F335] = "% PUBSUB (SUB): waiting for offer from any peer",  /* SID:335 new */
    [LA_F336] = "% PUBSUB mode requires gh_token and gist_id",  /* SID:336 new */
    [LA_F337] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:337 new */
    [LA_F338] = "Packet too large len=%d max=%d",  /* SID:338 new */
    [LA_F339] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:339 new */
    [LA_F340] = "% Path switch debounced, waiting for stability",  /* SID:340 new */
    [LA_F341] = "Path switched to better route (idx=%d)",  /* SID:341 new */
    [LA_F342] = "Processing (role=%s)",  /* SID:342 new */
    [LA_F343] = "% PseudoTCP enabled as transport layer",  /* SID:343 new */
    [LA_F344] = "% RELAY path but TURN not allocated",  /* SID:344 new */
    [LA_F345] = "% RELAY path but TURN not allocated (dtls)",  /* SID:345 new */
    [LA_F346] = "% RELAY recovery: NAT connected but no path available",  /* SID:346 new */
    [LA_F347] = "RELAY sent (ses_id=%u), type=0x%02x seq=%u flags=0x%02x",  /* SID:347 new */
    [LA_F348] = "% RELAY/COMPACT mode requires server_host",  /* SID:348 new */
    [LA_F349] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:349 new */
    [LA_F350] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:350 new */
    [LA_F351] = "Received valid signal from '%s'",  /* SID:351 new */
    [LA_F352] = "Recv %s pkt from %s:%d",  /* SID:352 new */
    [LA_F353] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:353 new */
    [LA_F354] = "Recv %s pkt from %s:%d seq=%u",  /* SID:354 new */
    [LA_F355] = "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:355 new */
    [LA_F356] = "Recv %s pkt from %s:%d, seq=%u, len=%d",  /* SID:356 new */
    [LA_F357] = "Recv ICE-STUN Binding Request from candidate %d (%s:%d)",  /* SID:357 new */
    [LA_F358] = "Recv ICE-STUN Binding Response from candidate %d (%s:%d)",  /* SID:358 new */
    [LA_F359] = "Recv ICE-STUN from %s:%d, upsert prflx failed",  /* SID:359 new */
    [LA_F360] = "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:360 new */
    [LA_F361] = "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d",  /* SID:361 new */
    [LA_F362] = "Reliable transport initialized rto=%d win=%d",  /* SID:362 new */
    [LA_F363] = "Requested Relay Candidate from TURN %s",  /* SID:363 new */
    [LA_F364] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:364 new */
    [LA_F365] = "Reuse Relay Candidate %s:%u (priority=%u)",  /* SID:365 new */
    [LA_F366] = "Reuse STUN Candidate %s:%u (priority=%u)",  /* SID:366 new */
    [LA_F367] = "% SCTP (usrsctp) requested but library not linked",  /* SID:367 new */
    [LA_F368] = "% SDP export buffer overflow",  /* SID:368 new */
    [LA_F369] = "% SIGNALING path but signaling relay not available",  /* SID:369 new */
    [LA_F370] = "% SIGNALING path enabled (server supports relay)\n",  /* SID:370 new */
    [LA_F371] = "% SSL_CTX_new failed",  /* SID:371 new */
    [LA_F372] = "% SSL_new failed",  /* SID:372 new */
    [LA_F373] = "STUN collecting to %s:%d (len=%d)",  /* SID:373 new */
    [LA_F374] = "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n",  /* SID:374 new */
    [LA_F375] = "% SYNC(trickle): seq overflow, cannot trickle more\n",  /* SID:375 new */
    [LA_F376] = "SYNC0: retry, (attempt %d/%d)\n",  /* SID:376 new */
    [LA_F377] = "SYNC0: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:377 new */
    [LA_F378] = "Send window full, dropping packet send_count=%d",  /* SID:378 new */
    [LA_F379] = "Sending Allocate Request to %s:%d",  /* SID:379 new */
    [LA_F380] = "% Sending FIN packet to peer before closing",  /* SID:380 new */
    [LA_F381] = "% Sending OFFLINE packet to COMPACT signaling server",  /* SID:381 new */
    [LA_F382] = "Sending Test I to %s:%d (len=%d)",  /* SID:382 new */
    [LA_F383] = "% Sending Test I(alt) to CHANGED-ADDRESS",  /* SID:383 new */
    [LA_F384] = "% Sending Test II with CHANGE-REQUEST(IP+PORT)",  /* SID:384 new */
    [LA_F385] = "% Sending Test III with CHANGE-REQUEST(PORT only)",  /* SID:385 new */
    [LA_F386] = "% Signal payload deserialization failed",  /* SID:386 new */
    [LA_F387] = "% Skipping Host Candidate gathering (disabled)",  /* SID:387 new */
    [LA_F388] = "Start COMPACT session failed(%d)",  /* SID:388 new */
    [LA_F389] = "Start RELAY session failed(%d)",  /* SID:389 new */
    [LA_F390] = "Start internal thread failed(%d)",  /* SID:390 new */
    [LA_F391] = "Starting COMPACT session with %s",  /* SID:391 new */
    [LA_F392] = "Starting RELAY session with %s",  /* SID:392 new */
    [LA_F393] = "% Starting internal thread",  /* SID:393 new */
    [LA_F394] = "State: LOST → CONNECTED, path=PUNCH[%d]",  /* SID:394 new */
    [LA_F395] = "State: RELAY → CONNECTED, path=PUNCH[%d]",  /* SID:395 new */
    [LA_F396] = "State: → CONNECTED, path[%d]",  /* SID:396 new */
    [LA_F397] = "% State: → ERROR (punch timeout, no relay available)",  /* SID:397 new */
    [LA_F398] = "% State: → LOST (all paths failed)",  /* SID:398 new */
    [LA_F399] = "% State: → PUNCHING",  /* SID:399 new */
    [LA_F400] = "State: → RELAY, path[%d]",  /* SID:400 new */
    [LA_F401] = "% Stopping internal thread",  /* SID:401 new */
    [LA_F402] = "TURN 401 Unauthorized (realm=%s), authenticating...",  /* SID:402 new */
    [LA_F403] = "TURN Allocate failed with error %d",  /* SID:403 new */
    [LA_F404] = "TURN Allocated relay %s:%u (lifetime=%us)",  /* SID:404 new */
    [LA_F405] = "TURN CreatePermission failed (error=%d)",  /* SID:405 new */
    [LA_F406] = "TURN CreatePermission for %s",  /* SID:406 new */
    [LA_F407] = "TURN Data Indication from %s:%u (%d bytes)",  /* SID:407 new */
    [LA_F408] = "TURN Refresh failed (error=%d)",  /* SID:408 new */
    [LA_F409] = "TURN Refresh ok (lifetime=%us)",  /* SID:409 new */
    [LA_F410] = "% TURN auth required but no credentials configured",  /* SID:410 new */
    [LA_F411] = "Test I(alt): Mapped address: %s:%d",  /* SID:411 new */
    [LA_F412] = "% Test I(alt): Timeout",  /* SID:412 new */
    [LA_F413] = "Test I: Changed address: %s:%d",  /* SID:413 new */
    [LA_F414] = "Test I: Mapped address: %s:%d",  /* SID:414 new */
    [LA_F415] = "% Test I: Timeout",  /* SID:415 new */
    [LA_F416] = "Test II: Success! Detection completed %s",  /* SID:416 new */
    [LA_F417] = "% Test II: Timeout (need Test III)",  /* SID:417 new */
    [LA_F418] = "Test III: Success! Detection completed %s",  /* SID:418 new */
    [LA_F419] = "% Test III: Timeout",  /* SID:419 new */
    [LA_F420] = "Transport layer '%s' init failed, falling back to simple reliable",  /* SID:420 new */
    [LA_F421] = "Unknown candidate type: %s",  /* SID:421 new */
    [LA_F422] = "Unknown signaling mode: %d",  /* SID:422 new */
    [LA_F423] = "Updating Gist field '%s'...",  /* SID:423 new */
    [LA_F424] = "% WebRTC candidate export buffer overflow",  /* SID:424 new */
    [LA_F425] = "[C] %s recv, len=%d\n",  /* SID:425 new */
    [LA_F426] = "[C] %s recv, seq=%u, flags=0x%02x, len=%d\n",  /* SID:426 new */
    [LA_F427] = "[C] %s recv, seq=%u, len=%d\n",  /* SID:427 new */
    [LA_F428] = "[C] %s recv\n",  /* SID:428 new */
    [LA_F429] = "[C] %s send failed(%d)\n",  /* SID:429 new */
    [LA_F430] = "[C] %s send to port:%d failed(%d)\n",  /* SID:430 new */
    [LA_F431] = "[C] %s send to port:%d, seq=%u, flags=0, len=0\n",  /* SID:431 new */
    [LA_F432] = "[C] %s send, seq=0, flags=0x%02x, len=%d\n",  /* SID:432 new */
    [LA_F433] = "[C] Unknown pkt type 0x%02x, len=%d\n",  /* SID:433 new */
    [LA_F434] = "[C] relay payload too large: %d",  /* SID:434 new */
    [LA_F435] = "[MbedTLS] DTLS role: %s (mode=%s)",  /* SID:435 new */
    [LA_F436] = "% [OpenSSL] DTLS handshake completed",  /* SID:436 new */
    [LA_F437] = "[OpenSSL] DTLS role: %s (mode=%s)",  /* SID:437 new */
    [LA_F438] = "[R] %s recv, len=%d\n",  /* SID:438 new */
    [LA_F439] = "[R] %s timeout\n",  /* SID:439 new */
    [LA_F440] = "[R] %s%s qsend failed(OOM)\n",  /* SID:440 new */
    [LA_F441] = "[R] %s%s qsend(%d), len=%u\n",  /* SID:441 new */
    [LA_F442] = "[R] Connecting to %s:%d\n",  /* SID:442 new */
    [LA_F443] = "% [R] Disconnected, back to ONLINE state\n",  /* SID:443 new */
    [LA_F444] = "% [R] Failed to create TCP socket\n",  /* SID:444 new */
    [LA_F445] = "% [R] Failed to set socket non-blocking\n",  /* SID:445 new */
    [LA_F446] = "[R] TCP connect failed(%d)\n",  /* SID:446 new */
    [LA_F447] = "[R] TCP connect select failed(%d)\n",  /* SID:447 new */
    [LA_F448] = "% [R] TCP connected immediately, sending ONLINE\n",  /* SID:448 new */
    [LA_F449] = "% [R] TCP connected, sending ONLINE\n",  /* SID:449 new */
    [LA_F450] = "% [R] TCP connection closed by peer\n",  /* SID:450 new */
    [LA_F451] = "% [R] TCP connection closed during send\n",  /* SID:451 new */
    [LA_F452] = "[R] TCP recv error(%d)\n",  /* SID:452 new */
    [LA_F453] = "[R] TCP send error(%d)\n",  /* SID:453 new */
    [LA_F454] = "[R] Unknown proto type %d\n",  /* SID:454 new */
    [LA_F455] = "[R] payload size %u exceeds limit %u\n",  /* SID:455 new */
    [LA_F456] = "[SCTP] association lost/shutdown (state=%u)",  /* SID:456 new */
    [LA_F457] = "[SCTP] bind failed: %s",  /* SID:457 new */
    [LA_F458] = "[SCTP] connect failed: %s",  /* SID:458 new */
    [LA_F459] = "[SCTP] sendv failed: %s",  /* SID:459 new */
    [LA_F460] = "[ST:%s] peer went offline, waiting for reconnect\n",  /* SID:460 new */
    [LA_F461] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:461 new */
    [LA_F462] = "% connection closed by peer",  /* SID:462 new */
    [LA_F463] = "ctr_drbg_seed failed: -0x%x",  /* SID:463 new */
    [LA_F464] = "retry seq=%u retx=%d rto=%d",  /* SID:464 new */
    [LA_F465] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:465 new */
    [LA_F466] = "ssl_config_defaults failed: -0x%x",  /* SID:466 new */
    [LA_F467] = "ssl_setup failed: -0x%x",  /* SID:467 new */
    [LA_F468] = "transport send_data failed, %d bytes dropped",  /* SID:468 new */
    [LA_F469] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:469 new */
    [LA_F470] = "% ✗ Add Srflx candidate failed(OOM)",  /* SID:470 new */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
