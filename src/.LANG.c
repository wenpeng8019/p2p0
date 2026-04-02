/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_p2p;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "alive",  /* SID:1 */
    [LA_W3] = "Detecting...",  /* SID:3 */
    [LA_W4] = "Full Cone NAT",  /* SID:4 */
    [LA_W6] = "Open Internet (No NAT)",  /* SID:6 */
    [LA_W7] = "Port Restricted Cone NAT",  /* SID:7 */
    [LA_W8] = "PUB",  /* SID:8 */
    [LA_W10] = "punch",  /* SID:10 */
    [LA_W13] = "Restricted Cone NAT",  /* SID:13 */
    [LA_W14] = "retry",  /* SID:14 */
    [LA_W15] = "SUB",  /* SID:15 */
    [LA_W16] = "Symmetric NAT (port-random)",  /* SID:16 */
    [LA_W17] = "Timeout (no response)",  /* SID:17 */
    [LA_W18] = "UDP Blocked (STUN unreachable)",  /* SID:18 */
    [LA_W19] = "Undetectable (no STUN/probe configured)",  /* SID:19 */
    [LA_W20] = "Unknown",  /* SID:20 */
    [LA_W21] = "Waiting for incoming offer from any peer",  /* SID:21 */
    [LA_S23] = "%s: address exchange failed: peer OFFLINE",  /* SID:23 */
    [LA_S24] = "%s: address exchange success, sending UDP probe",  /* SID:24 */
    [LA_S25] = "%s: already running, cannot trigger again",  /* SID:25 */
    [LA_S26] = "%s: peer is OFFLINE",  /* SID:26 */
    [LA_S27] = "%s: peer is online, waiting echo",  /* SID:27 */
    [LA_S28] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:28 */
    [LA_S29] = "%s: TURN allocated, starting address exchange",  /* SID:29 */
    [LA_S30] = "[SCTP] association established",  /* SID:30 */
    [LA_S31] = "[SCTP] usrsctp initialized, connecting...",  /* SID:31 */
    [LA_S32] = "[SCTP] usrsctp_socket failed",  /* SID:32 */
    [LA_S33] = "Channel ID validation failed",  /* SID:33 */
    [LA_S34] = "Detecting local network addresses",  /* SID:34 */
    [LA_S35] = "Gist GET failed",  /* SID:35 */
    [LA_S36] = "Invalid channel_id format (security risk)",  /* SID:36 */
    [LA_S37] = "Out of memory",  /* SID:37 */
    [LA_S39] = "Push local cand<%s:%d> failed(OOM)\n",  /* SID:39 */
    [LA_S40] = "Push remote cand<%s:%d> failed(OOM)\n",  /* SID:40 */
    [LA_F42] = "  [%d] %s/%d",  /* SID:42 */
    [LA_F45] = "%s NOTIFY: accepted\n",  /* SID:45 */
    [LA_F46] = "%s NOTIFY: ignored old notify base=%u (current=%u)\n",  /* SID:46 */
    [LA_F48] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n",  /* SID:48 */
    [LA_F47] = "%s NOTIFY: srflx addr update (disabled)\n",  /* SID:47 */
    [LA_F491] = "%s enqueued, name='%s', rid=%u\n",  /* SID:491 */
    [LA_F490] = "%s enqueued, ses_id=%llu\n",  /* SID:490 */
    [LA_F492] = "%s enqueued, target='%s'\n",  /* SID:492 */
    [LA_F493] = "%s enqueued\n",  /* SID:493 */
    [LA_F49] = "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n",  /* SID:49 */
    [LA_F51] = "%s resent, %d/%d\n",  /* SID:51 */
    [LA_F494] = "%s sent %d candidates, next_idx=%d\n",  /* SID:494 */
    [LA_F495] = "%s sent FIN\n",  /* SID:495 */
    [LA_F56] = "%s sent to %s:%d",  /* SID:56 */
    [LA_F53] = "%s sent to %s:%d (writable), echo_seq=%u",  /* SID:53 */
    [LA_F54] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:54 */
    [LA_F57] = "%s sent via best path[%d] to %s:%d, echo_seq=%u",  /* SID:57 */
    [LA_F427] = "%s sent via signaling relay",  /* SID:427 */
    [LA_F58] = "%s sent, inst_id=%u, cands=%d\n",  /* SID:58 */
    [LA_F59] = "%s sent, inst_id=%u\n",  /* SID:59 */
    [LA_F60] = "%s sent, seq=%u\n",  /* SID:60 */
    [LA_F450] = "%s sent, sid=%u, msg=%u, size=%d\n",  /* SID:450 */
    [LA_F62] = "%s sent, total=%d (ses_id=%llu)\n",  /* SID:62 */
    [LA_F63] = "%s seq=0: accepted cand_cnt=%d\n",  /* SID:63 */
    [LA_F64] = "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n",  /* SID:64 */
    [LA_F65] = "%s skipped: session_id=0\n",  /* SID:65 */
    [LA_F519] = "%s timeout\n",  /* SID:519 */
    [LA_F66] = "%s, retry remaining candidates and FIN to peer\n",  /* SID:66 */
    [LA_F67] = "%s, sent on %s\n",  /* SID:67 */
    [LA_F68] = "%s: %s timeout after %d retries (sid=%u)\n",  /* SID:68 */
    [LA_F74] = "%s: %s → %s (recv DATA)",  /* SID:74 */
    [LA_F435] = "%s: CONN ignored, upsert %s:%d failed",  /* SID:435 */
    [LA_F69] = "%s: CONN timeout after %llums",  /* SID:69 */
    [LA_F127] = "%s: CONNECTED → LOST (no response %llums)\n",  /* SID:127 */
    [LA_F72] = "%s: CONNECTING → %s (recv CONN)",  /* SID:72 */
    [LA_F73] = "%s: CONNECTING → %s (recv CONN_ACK)",  /* SID:73 */
    [LA_F71] = "%s: CONNECTING → CLOSED (timeout, no relay)",  /* SID:71 */
    [LA_F436] = "%s: CONN_ACK ignored, upsert %s:%d failed",  /* SID:436 */
    [LA_F103] = "%s: PUNCHING → %s",  /* SID:103 */
    [LA_F548] = "%s: PUNCHING → %s (peer CONNECTING)",  /* SID:548 */
    [LA_F138] = "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)",  /* SID:138 */
    [LA_F102] = "%s: PUNCHING → CONNECTING (%s%s)",  /* SID:102 */
    [LA_F438] = "%s: PUNCHING → RELAY (peer CONNECTING)",  /* SID:438 */
    [LA_F75] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n",  /* SID:75 */
    [LA_F76] = "%s: Peer addr changed -> %s:%d, retrying punch\n",  /* SID:76 */
    [LA_F77] = "%s: RELAY → CONNECTED (direct path recovered)",  /* SID:77 */
    [LA_F78] = "%s: RPC complete (sid=%u)\n",  /* SID:78 */
    [LA_F79] = "%s: RPC fail due to peer offline (sid=%u)\n",  /* SID:79 */
    [LA_F80] = "%s: RPC fail due to relay timeout (sid=%u)\n",  /* SID:80 */
    [LA_F81] = "%s: RPC finished (sid=%u)\n",  /* SID:81 */
    [LA_F82] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:82 */
    [LA_F83] = "%s: TURN allocation failed: ret=%d",  /* SID:83 */
    [LA_F84] = "%s: TURN allocation request sent",  /* SID:84 */
    [LA_F85] = "%s: UDP timeout, retry %d/%d",  /* SID:85 */
    [LA_F86] = "%s: UDP timeout: peer not responding",  /* SID:86 */
    [LA_F87] = "%s: accepted",  /* SID:87 */
    [LA_F88] = "%s: accepted (ses_id=%llu)\n",  /* SID:88 */
    [LA_F89] = "%s: accepted (sid=%u)\n",  /* SID:89 */
    [LA_F179] = "%s: accepted as cand[%d], target=%s:%d",  /* SID:179 */
    [LA_F90] = "%s: accepted for ack_seq=%u\n",  /* SID:90 */
    [LA_F92] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n",  /* SID:92 */
    [LA_F93] = "%s: accepted sid=%u, msg=%u\n",  /* SID:93 */
    [LA_F94] = "%s: accepted, probe_mapped=%s:%d\n",  /* SID:94 */
    [LA_F453] = "%s: accepted, public=%s:%d ses_id=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n",  /* SID:453 */
    [LA_F496] = "%s: accepted, relay=%s msg=%s cand_max=%d\n",  /* SID:496 */
    [LA_F497] = "%s: accepted, ses_id=%llu peer=%s\n",  /* SID:497 */
    [LA_F96] = "%s: accepted, waiting for response (sid=%u)\n",  /* SID:96 */
    [LA_F97] = "%s: accepted\n",  /* SID:97 */
    [LA_F502] = "%s: all candidates delivered to peer (fwd=0 after FIN)\n",  /* SID:502 */
    [LA_F99] = "%s: bad FIN marker=0x%02x\n",  /* SID:99 */
    [LA_F498] = "%s: bad payload len=%d\n",  /* SID:498 */
    [LA_F121] = "%s: bad payload(%d)",  /* SID:121 */
    [LA_F455] = "%s: bad payload(%d)\n",  /* SID:455 */
    [LA_F561] = "%s: bad payload(len=%d cand_cnt=%d)\n",  /* SID:561 */
    [LA_F562] = "%s: bad payload(len=%d)\n",  /* SID:562 */
    [LA_F101] = "%s: bad payload(len=%d, need >=8)\n",  /* SID:101 */
    [LA_F98] = "%s: batch punch skip (state=%d, use trickle)",  /* SID:98 */
    [LA_F173] = "%s: batch punch start (%d cands)",  /* SID:173 */
    [LA_F126] = "%s: batch punch: no cand, wait trickle",  /* SID:126 */
    [LA_F426] = "%s: cand[%d]<%s:%d> send packet failed(%d)",  /* SID:426 */
    [LA_F104] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n",  /* SID:104 */
    [LA_F582] = "%s: data relay ready, flow control released\n",  /* SID:582 */
    [LA_F106] = "%s: duplicate remote cand<%s:%d> from signaling, skipped\n",  /* SID:106 */
    [LA_F500] = "%s: duplicate remote cand<%s:%d>, skipped\n",  /* SID:500 */
    [LA_F107] = "%s: duplicate request ignored (sid=%u, already processing)\n",  /* SID:107 */
    [LA_F108] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n",  /* SID:108 */
    [LA_F109] = "%s: entered, %s arrived after REGISTERED\n",  /* SID:109 */
    [LA_F110] = "%s: exchange timeout, retry %d/%d",  /* SID:110 */
    [LA_F111] = "%s: exchange timeout: peer not responding",  /* SID:111 */
    [LA_F531] = "%s: fatal error code=%u, entering ERROR state\n",  /* SID:531 */
    [LA_F563] = "%s: forwarded=%d, next_idx adjusted to %d\n",  /* SID:563 */
    [LA_F112] = "%s: ignored for duplicated seq=%u, already acked\n",  /* SID:112 */
    [LA_F113] = "%s: ignored for seq=%u (expect=%d)\n",  /* SID:113 */
    [LA_F114] = "%s: ignored for ses_id=%llu (local ses_id=%llu)\n",  /* SID:114 */
    [LA_F115] = "%s: ignored for sid=%u (current sid=%u)\n",  /* SID:115 */
    [LA_F116] = "%s: ignored in invalid state=%d\n",  /* SID:116 */
    [LA_F117] = "%s: ignored in state=%d\n",  /* SID:117 */
    [LA_F437] = "%s: ignored, upsert %s:%d failed",  /* SID:437 */
    [LA_F118] = "%s: invalid ack_seq=%u\n",  /* SID:118 */
    [LA_F119] = "%s: invalid cand idx: %d (count: %d)",  /* SID:119 */
    [LA_F120] = "%s: invalid for non-relay req\n",  /* SID:120 */
    [LA_F508] = "%s: invalid online=%u, normalized to 0\n",  /* SID:508 */
    [LA_F419] = "%s: invalid payload len=%d (need 6)",  /* SID:419 */
    [LA_F122] = "%s: invalid seq=%u\n",  /* SID:122 */
    [LA_F123] = "%s: invalid session_id=0\n",  /* SID:123 */
    [LA_F124] = "%s: keep-alive sent (%d cands)",  /* SID:124 */
    [LA_F125] = "%s: new request (sid=%u) overrides pending request (sid=%u)\n",  /* SID:125 */
    [LA_F446] = "%s: no rpc request\n",  /* SID:446 */
    [LA_F129] = "%s: not connected, cannot send FIN",  /* SID:129 */
    [LA_F131] = "%s: old request ignored (sid=%u <= last_sid=%u)\n",  /* SID:131 */
    [LA_F162] = "%s: path rx UP (%s:%d)",  /* SID:162 */
    [LA_F549] = "%s: path tx UP",  /* SID:549 */
    [LA_F190] = "%s: path tx UP (echo seq=%u)",  /* SID:190 */
    [LA_F132] = "%s: path[%d] UP (%s:%d)",  /* SID:132 */
    [LA_F133] = "%s: path[%d] UP (recv DATA)",  /* SID:133 */
    [LA_F152] = "%s: path[%d] relay UP",  /* SID:152 */
    [LA_F504] = "%s: peer closed session %llu\n",  /* SID:504 */
    [LA_F551] = "%s: peer disconnected (ses_id=%llu), reset to REGISTERED\n",  /* SID:551 */
    [LA_F134] = "%s: peer online, proceeding to ICE\n",  /* SID:134 */
    [LA_F424] = "%s: peer online, starting NAT punch\n",  /* SID:424 */
    [LA_F135] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:135 */
    [LA_F136] = "%s: sync0 srflx cand[%d]<%s:%d>%s\n",  /* SID:136 */
    [LA_F503] = "%s: processed, remote_cand_cnt=%d\n",  /* SID:503 */
    [LA_F140] = "%s: punch cand[%d] %s:%d (%s)",  /* SID:140 */
    [LA_F137] = "%s: punch remote cand[%d]<%s:%d> failed\n",  /* SID:137 */
    [LA_F425] = "%s: punch timeout, fallback punching using signaling relay",  /* SID:425 */
    [LA_F139] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:139 */
    [LA_F142] = "%s: push remote cand<%s:%d> failed(OOM)",  /* SID:142 */
    [LA_F143] = "%s: reaching alloc OOM",  /* SID:143 */
    [LA_F144] = "%s: reaching broadcast to %d cand(s), seq=%u",  /* SID:144 */
    [LA_F149] = "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u",  /* SID:149 */
    [LA_F428] = "%s: reaching cand[%d] via signaling relay, seq=%u",  /* SID:428 */
    [LA_F145] = "%s: reaching enqueued: cand[%d], seq=%u, priority=%u",  /* SID:145 */
    [LA_F146] = "%s: reaching relay via signaling FAILED (ret=%d), seq=%u",  /* SID:146 */
    [LA_F147] = "%s: reaching relay via signaling SUCCESS, seq=%u",  /* SID:147 */
    [LA_F148] = "%s: reaching updated: cand[%d], seq=%u->%u",  /* SID:148 */
    [LA_F564] = "%s: received FIN marker from peer\n",  /* SID:564 */
    [LA_F151] = "%s: recorded peer conn_seq=%u for future CONN_ACK",  /* SID:151 */
    [LA_F91] = "%s: recv from cand[%d]",  /* SID:91 */
    [LA_F154] = "%s: remote %s cand<%s:%d> (disabled)\n",  /* SID:154 */
    [LA_F423] = "%s: remote %s cand[%d]<%s:%d> (disabled)\n",  /* SID:423 */
    [LA_F153] = "%s: remote %s cand[%d]<%s:%d> accepted\n",  /* SID:153 */
    [LA_F505] = "%s: remote_cands[] full, skipped %d candidates\n",  /* SID:505 */
    [LA_F156] = "%s: renew session due to session_id changed by sync0 (local=%llu pkt=%llu)\n",  /* SID:156 */
    [LA_F530] = "%s: req_type=%u code=%u msg=%.*s\n",  /* SID:530 */
    [LA_F565] = "%s: req_type=%u code=%u\n",  /* SID:565 */
    [LA_F157] = "%s: restarting periodic check",  /* SID:157 */
    [LA_F158] = "%s: retry(%d/%d) probe\n",  /* SID:158 */
    [LA_F159] = "%s: retry(%d/%d) req (sid=%u)\n",  /* SID:159 */
    [LA_F160] = "%s: retry(%d/%d) resp (sid=%u)\n",  /* SID:160 */
    [LA_F161] = "%s: retry, (attempt %d/%d)\n",  /* SID:161 */
    [LA_F506] = "%s: send buffer busy, skip\n",  /* SID:506 */
    [LA_F507] = "%s: send buffer busy, will retry\n",  /* SID:507 */
    [LA_F163] = "%s: send failed(%d)",  /* SID:163 */
    [LA_F164] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:164 */
    [LA_F165] = "%s: sent, sid=%u, code=%u, size=%d\n",  /* SID:165 */
    [LA_F566] = "%s: session busy, will retry\n",  /* SID:566 */
    [LA_F166] = "%s: session mismatch(local=%llu ack=%llu)\n",  /* SID:166 */
    [LA_F167] = "%s: session mismatch(local=%llu pkt=%llu)\n",  /* SID:167 */
    [LA_F567] = "%s: session mismatch(local=%llu recv=%llu)\n",  /* SID:567 */
    [LA_F168] = "%s: session mismatch(local=%llu, pkt=%llu)\n",  /* SID:168 */
    [LA_F580] = "%s: session renewed by peer SYNC0 (local=%llu recv=%llu)\n",  /* SID:580 */
    [LA_F169] = "%s: session validated, len=%d (ses_id=%llu)\n",  /* SID:169 */
    [LA_F170] = "%s: session_id mismatch (recv=%llu, expect=%llu)\n",  /* SID:170 */
    [LA_F172] = "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n",  /* SID:172 */
    [LA_F175] = "%s: started, sending first probe\n",  /* SID:175 */
    [LA_F463] = "%s: status error(%d)\n",  /* SID:463 */
    [LA_F177] = "%s: sync complete (ses_id=%llu)\n",  /* SID:177 */
    [LA_F178] = "%s: sync complete (ses_id=%llu, mask=0x%04x)\n",  /* SID:178 */
    [LA_F180] = "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n",  /* SID:180 */
    [LA_F181] = "%s: timeout after %d retries , type unknown\n",  /* SID:181 */
    [LA_F183] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:183 */
    [LA_F184] = "%s: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:184 */
    [LA_F185] = "%s: timeout, peer did not respond",  /* SID:185 */
    [LA_F186] = "%s: timeout, retry %d/%d",  /* SID:186 */
    [LA_F174] = "%s: trickle punch start",  /* SID:174 */
    [LA_F187] = "%s: trickled %d cand(s), seq=%u (ses_id=%llu)\n",  /* SID:187 */
    [LA_F188] = "%s: triggered via COMPACT msg echo",  /* SID:188 */
    [LA_F189] = "%s: triggered via RELAY TUNE echo",  /* SID:189 */
    [LA_F191] = "%s: unexpected ack_seq=%u mask=0x%04x\n",  /* SID:191 */
    [LA_F568] = "%s: unexpected fwd=%d after FIN, ignored\n",  /* SID:568 */
    [LA_F192] = "%s: unexpected non-srflx cand in sync0, treating as srflx\n",  /* SID:192 */
    [LA_F193] = "%s: unexpected remote cand type %d, skipped\n",  /* SID:193 */
    [LA_F443] = "%s: unknown target cand %s:%d",  /* SID:443 */
    [LA_F569] = "%s: waiting for STUN candidates, stun_pending=%d\n",  /* SID:569 */
    [LA_F570] = "%s: waiting for initial STUN candidates before sending local queue\n",  /* SID:570 */
    [LA_F150] = "%s: → CLOSED (recv FIN)",  /* SID:150 */
    [LA_F194] = "%s:%04d: %s",  /* SID:194 */
    [LA_F195] = "%s_ACK sent to %s:%d (try), echo_seq=%u",  /* SID:195 */
    [LA_F196] = "%s_ACK sent, seq=%u (ses_id=%llu)\n",  /* SID:196 */
    [LA_F197] = "%s_ACK sent, sid=%u\n",  /* SID:197 */
    [LA_F198] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:198 */
    [LA_F199] = "ACK: invalid payload length %d, expected at least 6",  /* SID:199 */
    [LA_F200] = "ACK: protocol mismatch, trans=%s has on_packet but received P2P_PKT_ACK",  /* SID:200 */
    [LA_F202] = "% Answer already present, skipping offer re-publish",  /* SID:202 */
    [LA_F204] = "Attempting Simultaneous Open to %s:%d",  /* SID:204 */
    [LA_F205] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:205 */
    [LA_F206] = "% BIO_new failed",  /* SID:206 */
    [LA_F207] = "% Base64 decode failed",  /* SID:207 */
    [LA_F208] = "% Bind failed",  /* SID:208 */
    [LA_F209] = "Bind failed to %d, port busy, trying random port",  /* SID:209 */
    [LA_F210] = "Bound to :%d",  /* SID:210 */
    [LA_F543] = "% Buffer size < 2048 may be insufficient for full SDP",  /* SID:543 */
    [LA_F211] = "% COMPACT mode requires explicit remote_peer_id",  /* SID:211 */
    [LA_F212] = "COMPACT relay payload too large: %d",  /* SID:212 */
    [LA_F213] = "COMPACT relay send failed: type=0x%02x, ret=%d",  /* SID:213 */
    [LA_F214] = "COMPACT relay: type=0x%02x, seq=%u (session_id=%llu)",  /* SID:214 */
    [LA_F215] = "% Close P2P UDP socket",  /* SID:215 */
    [LA_F216] = "% Closing TCP connection to RELAY signaling server",  /* SID:216 */
    [LA_F217] = "Connect to COMPACT signaling server failed(%d)",  /* SID:217 */
    [LA_F218] = "Connect to RELAY signaling server failed(%d)",  /* SID:218 */
    [LA_F513] = "Connecting to %s:%d\n",  /* SID:513 */
    [LA_F220] = "Connecting to RELAY signaling server at %s:%d",  /* SID:220 */
    [LA_F226] = "Crypto layer '%s' init failed, continuing without encryption",  /* SID:226 */
    [LA_F227] = "% DTLS (MbedTLS) requested but library not linked",  /* SID:227 */
    [LA_F228] = "% DTLS handshake complete (MbedTLS)",  /* SID:228 */
    [LA_F229] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:229 */
    [LA_F230] = "Detect local network interfaces failed(%d)",  /* SID:230 */
    [LA_F231] = "Detection completed %s",  /* SID:231 */
    [LA_F536] = "% Disconnected, back to ONLINE state\n",  /* SID:536 */
    [LA_F233] = "Duplicate remote cand<%s:%d> from signaling, skipped",  /* SID:233 */
    [LA_F514] = "% EXCHANGING: first sync received, peer online\n",  /* SID:514 */
    [LA_F571] = "EXCHANGING: peer=%s, uploading candidates\n",  /* SID:571 */
    [LA_F581] = "% EXCHANGING: session reset by peer SYNC0\n",  /* SID:581 */
    [LA_F572] = "% EXCHANGING: waiting for initial STUN/TURN candidates before upload\n",  /* SID:572 */
    [LA_F410] = "Exported %d candidates to SDP (%d bytes)",  /* SID:410 */
    [LA_F237] = "% Failed to allocate DTLS context",  /* SID:237 */
    [LA_F238] = "% Failed to allocate OpenSSL context",  /* SID:238 */
    [LA_F240] = "% Failed to allocate memory for candidate lists",  /* SID:240 */
    [LA_F241] = "% Failed to allocate memory for session",  /* SID:241 */
    [LA_F242] = "% Failed to build STUN request",  /* SID:242 */
    [LA_F516] = "% Failed to create TCP socket\n",  /* SID:516 */
    [LA_F411] = "Failed to parse SDP candidate line: %s",  /* SID:411 */
    [LA_F243] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:243 */
    [LA_F244] = "Failed to reserve remote candidates (base=%u cnt=%d)\n",  /* SID:244 */
    [LA_F245] = "Failed to reserve remote candidates (cnt=%d)\n",  /* SID:245 */
    [LA_F246] = "% Failed to reserve remote candidates (cnt=1)\n",  /* SID:246 */
    [LA_F247] = "Failed to resolve STUN server %s",  /* SID:247 */
    [LA_F248] = "Failed to resolve TURN server: %s",  /* SID:248 */
    [LA_F557] = "% Failed to send Test I(alt), continue to Test III",  /* SID:557 */
    [LA_F251] = "% Failed to send punch packet for new peer addr\n",  /* SID:251 */
    [LA_F517] = "% Failed to set socket non-blocking\n",  /* SID:517 */
    [LA_F253] = "Field %s is empty or too short",  /* SID:253 */
    [LA_F542] = "% Full SDP generation requires ice_ufrag and ice_pwd",  /* SID:542 */
    [LA_F203] = "Gathered Host candidate: %s:%d (priority=0x%08x)",  /* SID:203 */
    [LA_F256] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:256 */
    [LA_F258] = "% Handshake complete",  /* SID:258 */
    [LA_F259] = "Handshake failed: %s (-0x%04x)",  /* SID:259 */
    [LA_F420] = "Ignore %s pkt from %s:%d, not connected",  /* SID:420 */
    [LA_F421] = "Ignore %s pkt from %s:%d, not connecting",  /* SID:421 */
    [LA_F439] = "Ignore %s pkt from %s:%d, valid state(%d)",  /* SID:439 */
    [LA_F552] = "Ignore %s pkt from unknown path %s:%d",  /* SID:552 */
    [LA_F406] = "Imported %d candidates from SDP",  /* SID:406 */
    [LA_F407] = "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)",  /* SID:407 */
    [LA_F260] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:260 */
    [LA_F261] = "Initialize network subsystem failed(%d)",  /* SID:261 */
    [LA_F262] = "Initialize signaling mode: %d",  /* SID:262 */
    [LA_F263] = "Initialized: %s",  /* SID:263 */
    [LA_F408] = "Invalid IP address: %s",  /* SID:408 */
    [LA_F266] = "% Invalid signaling mode in configuration",  /* SID:266 */
    [LA_F430] = "% LOST recovery: NAT connected but no path available",  /* SID:430 */
    [LA_F267] = "Local address detection done: %d address(es)",  /* SID:267 */
    [LA_F447] = "% MSG RPC not supported by server\n",  /* SID:447 */
    [LA_F440] = "% NAT connected but no available path in path manager",  /* SID:440 */
    [LA_F270] = "% No advanced transport layer enabled, using simple reliable layer",  /* SID:270 */
    [LA_F271] = "% No auth_key provided, using default key (insecure)",  /* SID:271 */
    [LA_F532] = "% No shared local route addresses available, host candidates skipped",  /* SID:532 */
    [LA_F558] = "% No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)",  /* SID:558 */
    [LA_F518] = "% ONLINE: ready to start session\n",  /* SID:518 */
    [LA_F273] = "Open P2P UDP socket on port %d",  /* SID:273 */
    [LA_F274] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:274 */
    [LA_F275] = "% OpenSSL requested but library not linked",  /* SID:275 */
    [LA_F276] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:276 */
    [LA_F278] = "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n",  /* SID:278 */
    [LA_F279] = "% SYNC(trickle): seq overflow, cannot trickle more\n",  /* SID:279 */
    [LA_F280] = "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:280 */
    [LA_F281] = "% PUBSUB (SUB): waiting for offer from any peer",  /* SID:281 */
    [LA_F282] = "% PUBSUB mode requires gh_token and gist_id",  /* SID:282 */
    [LA_F283] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:283 */
    [LA_F284] = "Packet too large len=%d max=%d",  /* SID:284 */
    [LA_F286] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:286 */
    [LA_F287] = "% Path switch debounced, waiting for stability",  /* SID:287 */
    [LA_F288] = "Path switched to better route (idx=%d)",  /* SID:288 */
    [LA_F292] = "Processing (role=%s)",  /* SID:292 */
    [LA_F293] = "% PseudoTCP enabled as transport layer",  /* SID:293 */
    [LA_F520] = "% READY: candidate exchange completed\n",  /* SID:520 */
    [LA_F475] = "REGISTERED: peer=%s\n",  /* SID:475 */
    [LA_F583] = "RELAY %s enqueued: ses_id=%llu seq=%u len=%d\n",  /* SID:583 */
    [LA_F584] = "RELAY %s recv: bad payload(len=%d)\n",  /* SID:584 */
    [LA_F585] = "RELAY %s recv: session mismatch(local=%llu recv=%llu)\n",  /* SID:585 */
    [LA_F586] = "RELAY %s: payload too large (%d)\n",  /* SID:586 */
    [LA_F587] = "RELAY %s: send buffer busy\n",  /* SID:587 */
    [LA_F588] = "RELAY ACK recv: ack_seq=%u sack=0x%08x\n",  /* SID:588 */
    [LA_F589] = "RELAY ACK recv: invalid payload len=%d\n",  /* SID:589 */
    [LA_F590] = "% RELAY CRYPTO recv: no DTLS context\n",  /* SID:590 */
    [LA_F591] = "RELAY DATA recv: seq=%u len=%d\n",  /* SID:591 */
    [LA_F592] = "% RELAY data throttled: awaiting READY\n",  /* SID:592 */
    [LA_F593] = "RELAY data: unsupported type 0x%02x\n",  /* SID:593 */
    [LA_F429] = "% RELAY path but TURN not allocated",  /* SID:429 */
    [LA_F434] = "% RELAY path but TURN not allocated (dtls)",  /* SID:434 */
    [LA_F441] = "% RELAY recovery: NAT connected but no path available",  /* SID:441 */
    [LA_F594] = "RELAY recv: unexpected inner type 0x%02x\n",  /* SID:594 */
    [LA_F297] = "% RELAY/COMPACT mode requires server_host",  /* SID:297 */
    [LA_F298] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:298 */
    [LA_F301] = "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d",  /* SID:301 */
    [LA_F302] = "Received UNKNOWN pkt type: 0x%02X",  /* SID:302 */
    [LA_F303] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:303 */
    [LA_F304] = "Received valid signal from '%s'",  /* SID:304 */
    [LA_F305] = "Recv %s pkt from %s:%d",  /* SID:305 */
    [LA_F306] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:306 */
    [LA_F307] = "Recv %s pkt from %s:%d seq=%u",  /* SID:307 */
    [LA_F308] = "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:308 */
    [LA_F309] = "Recv %s pkt from %s:%d, seq=%u, len=%d",  /* SID:309 */
    [LA_F422] = "Recv ICE-STUN Binding Request from candidate %d (%s:%d)",  /* SID:422 */
    [LA_F547] = "Recv ICE-STUN Binding Response from candidate %d (%s:%d)",  /* SID:547 */
    [LA_F544] = "Recv ICE-STUN from %s:%d, upsert prflx failed",  /* SID:544 */
    [LA_F300] = "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:300 */
    [LA_F550] = "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d",  /* SID:550 */
    [LA_F312] = "Register to COMPACT signaling server at %s:%d",  /* SID:312 */
    [LA_F313] = "Reliable transport initialized rto=%d win=%d",  /* SID:313 */
    [LA_F314] = "Requested Relay Candidate from TURN %s",  /* SID:314 */
    [LA_F316] = "Requested Srflx Candidate from %s",  /* SID:316 */
    [LA_F317] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:317 */
    [LA_F318] = "% SCTP (usrsctp) requested but library not linked",  /* SID:318 */
    [LA_F412] = "% SDP export buffer overflow",  /* SID:412 */
    [LA_F319] = "% SIGNALING path but signaling relay not available",  /* SID:319 */
    [LA_F320] = "% SIGNALING path enabled (server supports relay)\n",  /* SID:320 */
    [LA_F321] = "% SSL_CTX_new failed",  /* SID:321 */
    [LA_F322] = "% SSL_new failed",  /* SID:322 */
    [LA_F324] = "Send window full, dropping packet send_count=%d",  /* SID:324 */
    [LA_F325] = "Sending Allocate Request to %s:%d",  /* SID:325 */
    [LA_F326] = "% Sending FIN packet to peer before closing",  /* SID:326 */
    [LA_F327] = "Sending Test I to %s:%d (len=%d)",  /* SID:327 */
    [LA_F556] = "% Sending Test I(alt) to CHANGED-ADDRESS",  /* SID:556 */
    [LA_F553] = "% Sending Test II with CHANGE-REQUEST(IP+PORT)",  /* SID:553 */
    [LA_F554] = "% Sending Test III with CHANGE-REQUEST(PORT only)",  /* SID:554 */
    [LA_F328] = "% Sending UNREGISTER packet to COMPACT signaling server",  /* SID:328 */
    [LA_F333] = "% Signal payload deserialization failed",  /* SID:333 */
    [LA_F334] = "% Skipping Host Candidate gathering (disabled)",  /* SID:334 */
    [LA_F323] = "Start RELAY session failed(%d)",  /* SID:323 */
    [LA_F336] = "Start internal thread failed(%d)",  /* SID:336 */
    [LA_F332] = "Starting RELAY session with %s",  /* SID:332 */
    [LA_F337] = "% Starting internal thread",  /* SID:337 */
    [LA_F341] = "State: LOST → CONNECTED, path=PUNCH[%d]",  /* SID:341 */
    [LA_F342] = "State: RELAY → CONNECTED, path=PUNCH[%d]",  /* SID:342 */
    [LA_F343] = "State: → CONNECTED, path[%d]",  /* SID:343 */
    [LA_F344] = "% State: → ERROR (punch timeout, no relay available)",  /* SID:344 */
    [LA_F338] = "% State: → LOST (all paths failed)",  /* SID:338 */
    [LA_F345] = "% State: → PUNCHING",  /* SID:345 */
    [LA_F348] = "State: → RELAY, path[%d]",  /* SID:348 */
    [LA_F349] = "% Stopping internal thread",  /* SID:349 */
    [LA_F521] = "% TCP connect failed (select error)\n",  /* SID:521 */
    [LA_F522] = "% TCP connect failed\n",  /* SID:522 */
    [LA_F523] = "% TCP connected immediately, sending ONLINE\n",  /* SID:523 */
    [LA_F524] = "% TCP connected, sending ONLINE\n",  /* SID:524 */
    [LA_F525] = "% TCP connection closed by peer\n",  /* SID:525 */
    [LA_F527] = "% TCP connection closed during send\n",  /* SID:527 */
    [LA_F526] = "% TCP recv error\n",  /* SID:526 */
    [LA_F534] = "% TCP send error\n",  /* SID:534 */
    [LA_F352] = "TURN 401 Unauthorized (realm=%s), authenticating...",  /* SID:352 */
    [LA_F353] = "TURN Allocate failed with error %d",  /* SID:353 */
    [LA_F354] = "TURN Allocated relay %s:%u (lifetime=%us)",  /* SID:354 */
    [LA_F355] = "TURN CreatePermission failed (error=%d)",  /* SID:355 */
    [LA_F356] = "TURN CreatePermission for %s",  /* SID:356 */
    [LA_F357] = "TURN Data Indication from %s:%u (%d bytes)",  /* SID:357 */
    [LA_F358] = "TURN Refresh failed (error=%d)",  /* SID:358 */
    [LA_F359] = "TURN Refresh ok (lifetime=%us)",  /* SID:359 */
    [LA_F360] = "% TURN auth required but no credentials configured",  /* SID:360 */
    [LA_F555] = "Test I(alt): Mapped address: %s:%d",  /* SID:555 */
    [LA_F559] = "% Test I(alt): Timeout",  /* SID:559 */
    [LA_F560] = "Test I: Changed address: %s:%d",  /* SID:560 */
    [LA_F361] = "Test I: Mapped address: %s:%d",  /* SID:361 */
    [LA_F362] = "% Test I: Timeout",  /* SID:362 */
    [LA_F363] = "Test II: Success! Detection completed %s",  /* SID:363 */
    [LA_F364] = "% Test II: Timeout (need Test III)",  /* SID:364 */
    [LA_F365] = "Test III: Success! Detection completed %s",  /* SID:365 */
    [LA_F366] = "% Test III: Timeout",  /* SID:366 */
    [LA_F367] = "Transport layer '%s' init failed, falling back to simple reliable",  /* SID:367 */
    [LA_F573] = "Trickle TURN: batch full (%d cands), sending\n",  /* SID:573 */
    [LA_F574] = "Trickle TURN: batch timeout (%d cands), sending\n",  /* SID:574 */
    [LA_F409] = "Unknown candidate type: %s",  /* SID:409 */
    [LA_F529] = "Unknown message type %d\n",  /* SID:529 */
    [LA_F371] = "Unknown signaling mode: %d",  /* SID:371 */
    [LA_F372] = "Updating Gist field '%s'...",  /* SID:372 */
    [LA_F575] = "% WAIT_PEER: peer went offline, waiting for reconnect\n",  /* SID:575 */
    [LA_F576] = "% WAIT_PEER: session established, waiting for peer info\n",  /* SID:576 */
    [LA_F535] = "% Waiting for RELAY server ONLINE_ACK",  /* SID:535 */
    [LA_F413] = "% WebRTC candidate export buffer overflow",  /* SID:413 */
    [LA_F374] = "[MbedTLS] DTLS role: %s (mode=%s)",  /* SID:374 */
    [LA_F375] = "% [OpenSSL] DTLS handshake completed",  /* SID:375 */
    [LA_F376] = "[OpenSSL] DTLS role: %s (mode=%s)",  /* SID:376 */
    [LA_F377] = "[SCTP] association lost/shutdown (state=%u)",  /* SID:377 */
    [LA_F378] = "[SCTP] bind failed: %s",  /* SID:378 */
    [LA_F379] = "[SCTP] connect failed: %s",  /* SID:379 */
    [LA_F380] = "[SCTP] sendv failed: %s",  /* SID:380 */
    [LA_F577] = "[TCP] %s enqueue, name='%s', rid=%u\n",  /* SID:577 */
    [LA_F578] = "[TCP] %s enqueue, ses_id=%llu cand_cnt=%d fin=%d\n",  /* SID:578 */
    [LA_F539] = "[TCP] %s enqueue, ses_id=%llu\n",  /* SID:539 */
    [LA_F579] = "[TCP] %s enqueue, target='%s'\n",  /* SID:579 */
    [LA_F533] = "[TCP] %s recv, len=%d\n",  /* SID:533 */
    [LA_F389] = "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n",  /* SID:389 */
    [LA_F390] = "[UDP] %s recv from %s:%d, len=%d\n",  /* SID:390 */
    [LA_F391] = "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:391 */
    [LA_F392] = "[UDP] %s recv from %s:%d, seq=%u, len=%d\n",  /* SID:392 */
    [LA_F487] = "[UDP] %s recv from %s:%d\n",  /* SID:487 */
    [LA_F488] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:488 */
    [LA_F395] = "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n",  /* SID:395 */
    [LA_F396] = "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:396 */
    [LA_F489] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:489 */
    [LA_F398] = "[UDP] %s_ACK send to %s:%d failed(%d)\n",  /* SID:398 */
    [LA_F399] = "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n",  /* SID:399 */
    [LA_F400] = "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:400 */
    [LA_F401] = "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:401 */
    [LA_F402] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:402 */
    [LA_F403] = "% connection closed by peer",  /* SID:403 */
    [LA_F404] = "ctr_drbg_seed failed: -0x%x",  /* SID:404 */
    [LA_F538] = "payload size %u exceeds limit %u\n",  /* SID:538 */
    [LA_F540] = "retry seq=%u retx=%d rto=%d",  /* SID:540 */
    [LA_F541] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:541 */
    [LA_F545] = "ssl_config_defaults failed: -0x%x",  /* SID:545 */
    [LA_F414] = "ssl_setup failed: -0x%x",  /* SID:414 */
    [LA_F416] = "transport send_data failed, %d bytes dropped",  /* SID:416 */
    [LA_F417] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:417 */
    [LA_F418] = "% ✗ Add Srflx candidate failed(OOM)",  /* SID:418 */
    [LA_F499] = "%s: cached (peer offline)\n",  /* SID:499 disabled */
    [LA_F501] = "%s: error, target not found\n",  /* SID:501 disabled */
    [LA_F509] = "%s: storage full, stop uploading\n",  /* SID:509 disabled */
    [LA_F510] = "%s: unknown status %d\n",  /* SID:510 disabled */
    [LA_F528] = "% Trickle TURN: uploading new candidates\n",  /* SID:528 disabled */
    [LA_F537] = "disconnect: not in session (state=%d)\n",  /* SID:537 disabled */
    [LA_F546] = "ICE connectivity check: both directions confirmed (candidate %d)",  /* SID:546 disabled */
    [LA_F315] = "Requested Relay Candidate from TURN %s",  /* SID:315 disabled */
    [LA_F335] = "% Skipping local Host candidates (disabled)",  /* SID:335 disabled */
    [LA_W9] = "Published",  /* SID:9 disabled */
    [LA_W12] = "Resent",  /* SID:12 disabled */
    [LA_F41] = "  ... and %d more pairs",  /* SID:41 disabled */
    [LA_F43] = "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx",  /* SID:43 disabled */
    [LA_F254] = "First offer, resetting ICE and clearing %d stale candidates",  /* SID:254 disabled */
    [LA_F255] = "Formed check list with %d candidate pairs",  /* SID:255 disabled */
    [LA_F385] = "[Trickle] Immediately probing new candidate %s:%d",  /* SID:385 disabled */
    [LA_S38] = "Push host cand<%s:%d> failed(OOM)\n",  /* SID:38 disabled */
    [LA_F225] = "Connectivity checks timed out (sent %d rounds), giving up",  /* SID:225 disabled */
    [LA_F234] = "Duplicate remote candidate<%s:%d> from signaling, skipped",  /* SID:234 disabled */
    [LA_F257] = "Gathered host cand<%s:%d> (priority=0x%08x)",  /* SID:257 disabled */
    [LA_F272] = "Nomination successful! Using! Using %s path %s:%d%s",  /* SID:272 disabled */
    [LA_F294] = "Push prflx candidate<%s:%d> failed(OOM)",  /* SID:294 disabled */
    [LA_F295] = "Push remote candidate<%s:%d> (type=%d) failed(OOM)",  /* SID:295 disabled */
    [LA_F329] = "% RELAY mode: bidirectional candidate exchange, no explicit reply needed",  /* SID:329 disabled */
    [LA_F310] = "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)",  /* SID:310 disabled */
    [LA_F311] = "Recv New Remote Candidate<%s:%d> (type=%d)",  /* SID:311 disabled */
    [LA_F368] = "UDP hole-punch probing remote candidates (%d candidates)",  /* SID:368 disabled */
    [LA_F369] = "UDP hole-punch probing remote candidates round %d/%d",  /* SID:369 disabled */
    [LA_F386] = "% [Trickle] Candidate queued, will be uploaded by tick_send",  /* SID:386 disabled */
    [LA_F387] = "% [Trickle] RELAY not ready, skipping single candidate send",  /* SID:387 disabled */
    [LA_F405] = "% p2p_ice_send_local_candidate called in non-RELAY mode",  /* SID:405 disabled */
    [LA_F415] = "starting NAT punch(Host candidate %d)",  /* SID:415 disabled */
    [LA_F511] = "% CONNECT timeout, max retries reached\n",  /* SID:511 disabled */
    [LA_F512] = "CONNECT timeout, retrying (%d/%d)\n",  /* SID:512 disabled */
    [LA_F515] = "Failed to allocate %d bytes for payload\n",  /* SID:515 disabled */
    [LA_W2] = "bytes",  /* SID:2 disabled */
    [LA_W11] = "Received signal from",  /* SID:11 disabled */
    [LA_F44] = "%s '%s' (%u %s)",  /* SID:44 disabled */
    [LA_F50] = "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates",  /* SID:50 disabled */
    [LA_F61] = "%s sent %d candidates, next_idx=%d\n",  /* SID:61 disabled */
    [LA_F448] = "%s sent FIN\n",  /* SID:448 disabled */
    [LA_F449] = "%s sent, name='%s' target='%s'\n",  /* SID:449 disabled */
    [LA_F451] = "%s sent, target='%s'\n",  /* SID:451 disabled */
    [LA_F452] = "%s sent\n",  /* SID:452 disabled */
    [LA_F95] = "%s: accepted, forward=%s msg=%s\n",  /* SID:95 disabled */
    [LA_F454] = "%s: accepted, ses_id=%llu peer=%s\n",  /* SID:454 disabled */
    [LA_F100] = "%s: bad payload len=%d\n",  /* SID:100 disabled */
    [LA_F456] = "%s: cached (peer offline)\n",  /* SID:456 disabled */
    [LA_F457] = "%s: duplicate remote cand<%s:%d>, skipped\n",  /* SID:457 disabled */
    [LA_F176] = "%s: error, target not found\n",  /* SID:176 disabled */
    [LA_F458] = "%s: forwarded to peer\n",  /* SID:458 disabled */
    [LA_F459] = "%s: processed, remote_cand_cnt=%d\n",  /* SID:459 disabled */
    [LA_F460] = "%s: received FIN from peer\n",  /* SID:460 disabled */
    [LA_F461] = "%s: remote_cands[] full, skipped %d candidates\n",  /* SID:461 disabled */
    [LA_F462] = "%s: session mismatch(local=%llu recv=%llu)\n",  /* SID:462 disabled */
    [LA_F464] = "%s: storage full, stop uploading\n",  /* SID:464 disabled */
    [LA_F465] = "%s: unknown status %d\n",  /* SID:465 disabled */
    [LA_F201] = "Added Remote Candidate: %d -> %s:%d",  /* SID:201 disabled */
    [LA_F466] = "Bad magic 0x%08x\n",  /* SID:466 disabled */
    [LA_F467] = "% CONNECT timeout, max retries reached\n",  /* SID:467 disabled */
    [LA_F468] = "CONNECT timeout, retrying (%d/%d)\n",  /* SID:468 disabled */
    [LA_F219] = "Connected to server %s:%d as '%s'",  /* SID:219 disabled */
    [LA_F469] = "Connecting to %s:%d\n",  /* SID:469 disabled */
    [LA_F221] = "% Connection closed by server",  /* SID:221 disabled */
    [LA_F222] = "% Connection closed while discarding",  /* SID:222 disabled */
    [LA_F223] = "% Connection closed while reading payload",  /* SID:223 disabled */
    [LA_F224] = "% Connection closed while reading sender",  /* SID:224 disabled */
    [LA_F232] = "Discarded %d bytes payload of message type %d",  /* SID:232 disabled */
    [LA_F296] = "EXCHANGING: peer=%s, uploading candidates\n",  /* SID:296 disabled */
    [LA_F470] = "Failed to allocate %d bytes for payload\n",  /* SID:470 disabled */
    [LA_F235] = "Failed to allocate %u bytes",  /* SID:235 disabled */
    [LA_F236] = "% Failed to allocate ACK payload buffer",  /* SID:236 disabled */
    [LA_F239] = "% Failed to allocate discard buffer, closing connection",  /* SID:239 disabled */
    [LA_F471] = "% Failed to create TCP socket\n",  /* SID:471 disabled */
    [LA_F249] = "% Failed to send header",  /* SID:249 disabled */
    [LA_F250] = "% Failed to send payload",  /* SID:250 disabled */
    [LA_F252] = "% Failed to send target name",  /* SID:252 disabled */
    [LA_F264] = "Invalid magic 0x%x (expected 0x%x), resetting",  /* SID:264 disabled */
    [LA_F265] = "Invalid read state %d, resetting",  /* SID:265 disabled */
    [LA_F472] = "% ONLINE: ready to start session\n",  /* SID:472 disabled */
    [LA_F473] = "% ONLINE_ACK timeout\n",  /* SID:473 disabled */
    [LA_F277] = "% P2P connected, closing signaling TCP connection",  /* SID:277 disabled */
    [LA_F285] = "Passive peer learned remote ID '%s' from OFFER",  /* SID:285 disabled */
    [LA_F289] = "Peer '%s' is now online (FORWARD received), resuming",  /* SID:289 disabled */
    [LA_F290] = "Peer offline, cached %d candidates",  /* SID:290 disabled */
    [LA_F291] = "Peer online, forwarded %d candidates",  /* SID:291 disabled */
    [LA_F474] = "% READY: candidate exchange completed\n",  /* SID:474 disabled */
    [LA_F299] = "Received ACK (status=%d, candidates_acked=%d)",  /* SID:299 disabled */
    [LA_F330] = "Sent answer to '%s' (%d bytes)",  /* SID:330 disabled */
    [LA_F331] = "Sent connect request to '%s' (%d bytes)",  /* SID:331 disabled */
    [LA_F350] = "% Storage full, waiting for peer to come online",  /* SID:350 disabled */
    [LA_F476] = "% TCP connect failed (select error)\n",  /* SID:476 disabled */
    [LA_F477] = "% TCP connect failed\n",  /* SID:477 disabled */
    [LA_F478] = "% TCP connected, sending ONLINE\n",  /* SID:478 disabled */
    [LA_F479] = "% TCP connection closed by peer\n",  /* SID:479 disabled */
    [LA_F480] = "% TCP recv error\n",  /* SID:480 disabled */
    [LA_F481] = "% Trickle TURN: uploading new candidates\n",  /* SID:481 disabled */
    [LA_F370] = "Unknown ACK status %d",  /* SID:370 disabled */
    [LA_F482] = "Unknown message type %d\n",  /* SID:482 disabled */
    [LA_F373] = "Waiting for peer '%s' timed out (%dms), giving up",  /* SID:373 disabled */
    [LA_F381] = "[SIGNALING] Failed to send candidates, will retry (ret=%d)",  /* SID:381 disabled */
    [LA_F382] = "[SIGNALING] Sent candidates (cached, peer offline) %d to %s",  /* SID:382 disabled */
    [LA_F383] = "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)",  /* SID:383 disabled */
    [LA_F384] = "% [SIGNALING] Server storage full, waiting for peer to come online",  /* SID:384 disabled */
    [LA_F393] = "[TCP] %s recv, len=%d\n",  /* SID:393 disabled */
    [LA_F394] = "[TCP] %s send failed\n",  /* SID:394 disabled */
    [LA_F483] = "[TCP] %s send header failed\n",  /* SID:483 disabled */
    [LA_F484] = "[TCP] %s send payload failed\n",  /* SID:484 disabled */
    [LA_F397] = "[TCP] %s send to %s:%d, len=%zu\n",  /* SID:397 disabled */
    [LA_F485] = "[TCP] %s send to %s:%d, target='%s'\n",  /* SID:485 disabled */
    [LA_F486] = "[TCP] %s send, ses_id=%llu cand_cnt=%d fin=%d\n",  /* SID:486 disabled */
    [LA_W5] = "no (cached)",  /* SID:5 disabled */
    [LA_W22] = "yes",  /* SID:22 disabled */
    [LA_F388] = "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()",  /* SID:388 disabled */
    [LA_F445] = "s->active_path=%d, path_stats.state=%d",  /* SID:445 disabled */
    [LA_F444] = "best_path<0 && active_path=%d, signaling=%d, state=%d\n",  /* SID:444 disabled */
    [LA_F268] = "Marked old path (idx=%d) as FAILED due to addr change\n",  /* SID:268 disabled */
    [LA_F442] = "Invalid state (%d): LOST but active_path=%d, path_type=%d",  /* SID:442 disabled */
    [LA_F269] = "% NAT connected but no available path in path manager",  /* SID:269 disabled */
    [LA_F339] = "% State: CONNECTED → RELAY (path lost)",  /* SID:339 disabled */
    [LA_F433] = "path[%d] addr is NULL (RELAY recovery)",  /* SID:433 disabled */
    [LA_F431] = "path[%d] addr is NULL",  /* SID:431 disabled */
    [LA_F351] = "% Synced path after failover",  /* SID:351 disabled */
    [LA_F432] = "path[%d] addr is NULL (LOST recovery)",  /* SID:432 disabled */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_p2p_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
