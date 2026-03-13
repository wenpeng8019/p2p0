/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_p2p;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "alive",  /* SID:1 */
    [LA_W2] = "bytes",  /* SID:2 */
    [LA_W3] = "Detecting...",  /* SID:3 */
    [LA_W4] = "Full Cone NAT",  /* SID:4 */
    [LA_W5] = "NAT punch failed, no TURN server configured",  /* SID:5 */
    [LA_W6] = "NAT punch failed, server has no relay support",  /* SID:6 */
    [LA_W7] = "NAT punch failed, using COMPACT server relay",  /* SID:7 */
    [LA_W8] = "no (cached)",  /* SID:8 */
    [LA_W9] = "Open Internet (No NAT)",  /* SID:9 */
    [LA_W10] = "Port Restricted Cone NAT",  /* SID:10 */
    [LA_W11] = "PUB",  /* SID:11 */
    [LA_W12] = "Published",  /* SID:12 */
    [LA_W13] = "punch",  /* SID:13 */
    [LA_W14] = "Received signal from",  /* SID:14 */
    [LA_W15] = "Resent",  /* SID:15 */
    [LA_W16] = "Restricted Cone NAT",  /* SID:16 */
    [LA_W17] = "retry",  /* SID:17 */
    [LA_W18] = "SUB",  /* SID:18 */
    [LA_W19] = "Symmetric NAT (port-random)",  /* SID:19 */
    [LA_W20] = "Timeout (no response)",  /* SID:20 */
    [LA_W21] = "UDP Blocked (STUN unreachable)",  /* SID:21 */
    [LA_W22] = "Unknown",  /* SID:22 */
    [LA_W23] = "Unsupported (no STUN/probe configured)",  /* SID:23 */
    [LA_W24] = "Waiting for incoming offer from any peer",  /* SID:24 */
    [LA_W25] = "yes",  /* SID:25 */
    [LA_S26] = "%s: address exchange failed: peer OFFLINE",  /* SID:26 */
    [LA_S27] = "%s: address exchange success, sending UDP probe",  /* SID:27 */
    [LA_S28] = "%s: already running, cannot trigger again",  /* SID:28 */
    [LA_S29] = "%s: peer is OFFLINE",  /* SID:29 */
    [LA_S30] = "%s: peer is online, waiting echo",  /* SID:30 */
    [LA_S31] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:31 */
    [LA_S32] = "%s: TURN allocated, starting address exchange",  /* SID:32 */
    [LA_S33] = "%s: unpack upsert remote cand<%s:%d> failed(OOM)\n",  /* SID:33 */
    [LA_S34] = "[SCTP] association established",  /* SID:34 */
    [LA_S35] = "[SCTP] usrsctp initialized, connecting...",  /* SID:35 */
    [LA_S36] = "[SCTP] usrsctp_socket failed",  /* SID:36 */
    [LA_S37] = "Channel ID validation failed",  /* SID:37 */
    [LA_S38] = "Detecting local network addresses",  /* SID:38 */
    [LA_S39] = "Gist GET failed",  /* SID:39 */
    [LA_S40] = "Invalid channel_id format (security risk)",  /* SID:40 */
    [LA_S41] = "Out of memory",  /* SID:41 */
    [LA_S42] = "Push host cand<%s:%d> failed(OOM)\n",  /* SID:42 */
    [LA_S43] = "Push local cand<%s:%d> failed(OOM)\n",  /* SID:43 */
    [LA_F44] = "  ... and %d more pairs",  /* SID:44 */
    [LA_F45] = "  [%d] %s/%d",  /* SID:45 */
    [LA_F46] = "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx",  /* SID:46 */
    [LA_F47] = "  [%d]<%s:%d> (type: %s)",  /* SID:47 */
    [LA_F48] = "%s '%s' (%u %s)",  /* SID:48 */
    [LA_F49] = "%s NOTIFY: accepted\n",  /* SID:49 */
    [LA_F50] = "%s NOTIFY: ignored old notify base=%u (current=%u)\n",  /* SID:50 */
    [LA_F51] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n",  /* SID:51 */
    [LA_F52] = "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n",  /* SID:52 */
    [LA_F53] = "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates",  /* SID:53 */
    [LA_F54] = "%s resent, %d/%d\n",  /* SID:54 */
    [LA_F55] = "%s sent to %s:%d",  /* SID:55 */
    [LA_F56] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:56 */
    [LA_F57] = "%s sent to %s:%d, echo_seq=%u",  /* SID:57 */
    [LA_F58] = "%s sent, inst_id=%u, cands=%d\n",  /* SID:58 */
    [LA_F59] = "%s sent, inst_id=%u\n",  /* SID:59 */
    [LA_F60] = "%s sent, seq=%u\n",  /* SID:60 */
    [LA_F61] = "%s sent, sid=%u, msg=%u, size=%d\n",  /* SID:61 */
    [LA_F62] = "%s seq=0: accepted cand_cnt=%d\n",  /* SID:62 */
    [LA_F63] = "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n",  /* SID:63 */
    [LA_F64] = "%s skipped: session_id=0\n",  /* SID:64 */
    [LA_F65] = "%s, retry remaining candidates and FIN to peer\n",  /* SID:65 */
    [LA_F66] = "%s, sent on %s\n",  /* SID:66 */
    [LA_F67] = "%s: %s timeout after %d retries (sid=%u)\n",  /* SID:67 */
    [LA_F68] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n",  /* SID:68 */
    [LA_F69] = "%s: Peer addr changed -> %s:%d, retrying punch\n",  /* SID:69 */
    [LA_F70] = "%s: RPC complete (sid=%u)\n",  /* SID:70 */
    [LA_F71] = "%s: RPC fail due to peer offline (sid=%u)\n",  /* SID:71 */
    [LA_F72] = "%s: RPC fail due to relay timeout (sid=%u)\n",  /* SID:72 */
    [LA_F73] = "%s: RPC finished (sid=%u)\n",  /* SID:73 */
    [LA_F74] = "%s: TURN allocation failed: ret=%d",  /* SID:74 */
    [LA_F75] = "%s: TURN allocation request sent",  /* SID:75 */
    [LA_F76] = "%s: UDP timeout, retry %d/%d",  /* SID:76 */
    [LA_F77] = "%s: UDP timeout: peer not responding",  /* SID:77 */
    [LA_F78] = "%s: accepted",  /* SID:78 */
    [LA_F79] = "%s: accepted (sid=%u)\n",  /* SID:79 */
    [LA_F80] = "%s: accepted for ack_seq=%u\n",  /* SID:80 */
    [LA_F81] = "%s: accepted from cand[%d]",  /* SID:81 */
    [LA_F82] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n",  /* SID:82 */
    [LA_F83] = "%s: accepted sid=%u, msg=%u\n",  /* SID:83 */
    [LA_F84] = "%s: accepted, probe_mapped=%s:%d\n",  /* SID:84 */
    [LA_F85] = "%s: accepted, waiting for response (sid=%u)\n",  /* SID:85 */
    [LA_F86] = "%s: accepted\n",  /* SID:86 */
    [LA_F87] = "%s: already connected, ignoring batch punch request",  /* SID:87 */
    [LA_F88] = "%s: bad payload(len=%d cand_cnt=%d)\n",  /* SID:88 */
    [LA_F89] = "%s: bad payload(len=%d)\n",  /* SID:89 */
    [LA_F90] = "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)",  /* SID:90 */
    [LA_F91] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n",  /* SID:91 */
    [LA_F92] = "%s: discovered unsynced prflx cand<%s:%d>[%d]",  /* SID:92 */
    [LA_F93] = "%s: duplicate request ignored (sid=%u, already processing)\n",  /* SID:93 */
    [LA_F94] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n",  /* SID:94 */
    [LA_F95] = "%s: entered, %s arrived after REGISTERED\n",  /* SID:95 */
    [LA_F96] = "%s: exchange timeout, retry %d/%d",  /* SID:96 */
    [LA_F97] = "%s: exchange timeout: peer not responding",  /* SID:97 */
    [LA_F98] = "%s: failed to RE-REGISTER after timeout\n",  /* SID:98 */
    [LA_F99] = "%s: failed to send UNREGISTER before restart\n",  /* SID:99 */
    [LA_F100] = "%s: ignored (relay not supported)\n",  /* SID:100 */
    [LA_F101] = "%s: ignored for duplicated seq=%u, already acked\n",  /* SID:101 */
    [LA_F102] = "%s: ignored for seq=%u (expect=%d)\n",  /* SID:102 */
    [LA_F103] = "%s: ignored for sid=%u (current sid=%u)\n",  /* SID:103 */
    [LA_F104] = "%s: ignored in invalid state=%d\n",  /* SID:104 */
    [LA_F105] = "%s: ignored in state(%d)",  /* SID:105 */
    [LA_F106] = "%s: ignored in state=%d\n",  /* SID:106 */
    [LA_F107] = "%s: invalid ack_seq=%u\n",  /* SID:107 */
    [LA_F108] = "%s: invalid cand idx: %d (count: %d)",  /* SID:108 */
    [LA_F109] = "%s: invalid for non-relay req\n",  /* SID:109 */
    [LA_F110] = "%s: invalid in non-COMPACT mode\n",  /* SID:110 */
    [LA_F111] = "%s: invalid seq=%u\n",  /* SID:111 */
    [LA_F112] = "%s: invalid session_id=0\n",  /* SID:112 */
    [LA_F113] = "%s: keep alive to %d reachable cand(s)",  /* SID:113 */
    [LA_F114] = "%s: new request (sid=%u) overrides pending request (sid=%u)\n",  /* SID:114 */
    [LA_F115] = "%s: no remote candidates to punch",  /* SID:115 */
    [LA_F116] = "%s: old request ignored (sid=%u <= last_sid=%u)\n",  /* SID:116 */
    [LA_F117] = "%s: peer online, proceeding to ICE\n",  /* SID:117 */
    [LA_F118] = "%s: punching additional cand<%s:%d>[%d] while connected",  /* SID:118 */
    [LA_F119] = "%s: punching remote cand<%s:%d>[%d]",  /* SID:119 */
    [LA_F120] = "%s: received FIN from peer, marking NAT as CLOSED",  /* SID:120 */
    [LA_F121] = "%s: remote cand[%d]<%s:%d>, starting punch\n",  /* SID:121 */
    [LA_F122] = "%s: restarting periodic check",  /* SID:122 */
    [LA_F123] = "%s: retry(%d/%d) probe\n",  /* SID:123 */
    [LA_F124] = "%s: retry(%d/%d) req (sid=%u)\n",  /* SID:124 */
    [LA_F125] = "%s: retry(%d/%d) resp (sid=%u)\n",  /* SID:125 */
    [LA_F126] = "%s: retry, (attempt %d/%d)\n",  /* SID:126 */
    [LA_F127] = "%s: rx confirmed: peer->me path is UP (%s:%d)",  /* SID:127 */
    [LA_F128] = "%s: send failed(%d)",  /* SID:128 */
    [LA_F129] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:129 */
    [LA_F130] = "%s: sent, sid=%u, code=%u, size=%d\n",  /* SID:130 */
    [LA_F131] = "%s: skip and mark NAT as OPEN (lan_punch enabled)\n",  /* SID:131 */
    [LA_F132] = "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n",  /* SID:132 */
    [LA_F133] = "%s: start punching all(%d) remote candidates",  /* SID:133 */
    [LA_F134] = "%s: started, sending first probe\n",  /* SID:134 */
    [LA_F135] = "%s: status error(%d)\n",  /* SID:135 */
    [LA_F136] = "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n",  /* SID:136 */
    [LA_F137] = "%s: timeout after %d retries , type unknown\n",  /* SID:137 */
    [LA_F138] = "%s: timeout, max(%d) attempts reached, reset to INIT\n",  /* SID:138 */
    [LA_F139] = "%s: timeout, peer did not respond",  /* SID:139 */
    [LA_F140] = "%s: timeout, retry %d/%d",  /* SID:140 */
    [LA_F141] = "%s: track upsert remote cand<%s:%d> failed(OOM), dropping",  /* SID:141 */
    [LA_F142] = "%s: triggered via COMPACT msg echo",  /* SID:142 */
    [LA_F143] = "%s: triggered via RELAY TUNE echo",  /* SID:143 */
    [LA_F144] = "%s: tx confirmed: me->peer path is UP (echoed seq=%u)",  /* SID:144 */
    [LA_F145] = "%s: unexpected ack_seq=%u mask=0x%04x\n",  /* SID:145 */
    [LA_F146] = "%s:%04d: %s",  /* SID:146 */
    [LA_F147] = "%s_ACK sent, sid=%u\n",  /* SID:147 */
    [LA_F148] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:148 */
    [LA_F149] = "Added Remote Candidate: %d -> %s:%d",  /* SID:149 */
    [LA_F150] = "% Added SIGNALING path to path manager",  /* SID:150 */
    [LA_F151] = "% Answer already present, skipping offer re-publish",  /* SID:151 */
    [LA_F152] = "Append Host candidate: %s:%d",  /* SID:152 */
    [LA_F153] = "Attempting Simultaneous Open to %s:%d",  /* SID:153 */
    [LA_F154] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:154 */
    [LA_F155] = "% BIO_new failed",  /* SID:155 */
    [LA_F156] = "% Base64 decode failed",  /* SID:156 */
    [LA_F157] = "% Bind failed",  /* SID:157 */
    [LA_F158] = "Bind failed to %d, port busy, trying random port",  /* SID:158 */
    [LA_F159] = "Bound to :%d",  /* SID:159 */
    [LA_F160] = "% COMPACT mode requires explicit remote_peer_id",  /* SID:160 */
    [LA_F161] = "% Close P2P UDP socket",  /* SID:161 */
    [LA_F162] = "% Closing TCP connection to RELAY signaling server",  /* SID:162 */
    [LA_F163] = "Connect to COMPACT signaling server failed(%d)",  /* SID:163 */
    [LA_F164] = "Connect to RELAY signaling server failed(%d)",  /* SID:164 */
    [LA_F165] = "Connected to server %s:%d as '%s'",  /* SID:165 */
    [LA_F166] = "Connecting to RELAY signaling server at %s:%d",  /* SID:166 */
    [LA_F167] = "% Connection closed by server",  /* SID:167 */
    [LA_F168] = "% Connection closed while discarding",  /* SID:168 */
    [LA_F169] = "% Connection closed while reading payload",  /* SID:169 */
    [LA_F170] = "% Connection closed while reading sender",  /* SID:170 */
    [LA_F171] = "Connectivity checks timed out (sent %d rounds), giving up",  /* SID:171 */
    [LA_F172] = "Crypto layer '%s' init failed, continuing without encryption",  /* SID:172 */
    [LA_F173] = "% DTLS (MbedTLS) enabled as encryption layer",  /* SID:173 */
    [LA_F174] = "% DTLS handshake complete (MbedTLS)",  /* SID:174 */
    [LA_F175] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:175 */
    [LA_F176] = "Detect local network interfaces failed(%d)",  /* SID:176 */
    [LA_F177] = "Detection completed %s",  /* SID:177 */
    [LA_F178] = "Discarded %d bytes payload of message type %d",  /* SID:178 */
    [LA_F179] = "Failed to allocate %u bytes",  /* SID:179 */
    [LA_F180] = "% Failed to allocate ACK payload buffer",  /* SID:180 */
    [LA_F181] = "% Failed to allocate DTLS context",  /* SID:181 */
    [LA_F182] = "% Failed to allocate OpenSSL context",  /* SID:182 */
    [LA_F183] = "% Failed to allocate discard buffer, closing connection",  /* SID:183 */
    [LA_F184] = "% Failed to allocate memory for candidate lists",  /* SID:184 */
    [LA_F185] = "% Failed to allocate memory for session",  /* SID:185 */
    [LA_F186] = "% Failed to build STUN request",  /* SID:186 */
    [LA_F187] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:187 */
    [LA_F188] = "Failed to reserve remote candidates (base=%u cnt=%d)\n",  /* SID:188 */
    [LA_F189] = "Failed to reserve remote candidates (cnt=%d)\n",  /* SID:189 */
    [LA_F190] = "% Failed to reserve remote candidates (cnt=1)\n",  /* SID:190 */
    [LA_F191] = "Failed to resolve STUN server %s",  /* SID:191 */
    [LA_F192] = "Failed to resolve TURN server: %s",  /* SID:192 */
    [LA_F193] = "% Failed to send header",  /* SID:193 */
    [LA_F194] = "% Failed to send payload",  /* SID:194 */
    [LA_F195] = "% Failed to send punch packet for new peer addr\n",  /* SID:195 */
    [LA_F196] = "% Failed to send target name",  /* SID:196 */
    [LA_F197] = "Field %s is empty or too short",  /* SID:197 */
    [LA_F198] = "First offer, resetting ICE and clearing %d stale candidates",  /* SID:198 */
    [LA_F199] = "Formed check list with %d candidate pairs",  /* SID:199 */
    [LA_F200] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:200 */
    [LA_F201] = "Gathered host cand<%s:%d> (priority=0x%08x)",  /* SID:201 */
    [LA_F202] = "% Handshake complete",  /* SID:202 */
    [LA_F203] = "Handshake failed: %s (-0x%04x)",  /* SID:203 */
    [LA_F204] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:204 */
    [LA_F205] = "Initialize network subsystem failed(%d)",  /* SID:205 */
    [LA_F206] = "Initialize signaling mode: %d",  /* SID:206 */
    [LA_F207] = "Initialized: %s",  /* SID:207 */
    [LA_F208] = "Invalid magic 0x%x (expected 0x%x), resetting",  /* SID:208 */
    [LA_F209] = "Invalid read state %d, resetting",  /* SID:209 */
    [LA_F210] = "% Invalid signaling mode in configuration",  /* SID:210 */
    [LA_F211] = "Local address detection done: %d address(es)",  /* SID:211 */
    [LA_F212] = "Marked old path (idx=%d) as FAILED due to addr change\n",  /* SID:212 */
    [LA_F213] = "% NAT connection recovered, upgrading from RELAY to CONNECTED",  /* SID:213 */
    [LA_F214] = "% NAT connection timeout, downgrading to relay mode",  /* SID:214 */
    [LA_F215] = "% No advanced transport layer enabled, using simple reliable layer",  /* SID:215 */
    [LA_F216] = "% No auth_key provided, using default key (insecure)",  /* SID:216 */
    [LA_F217] = "Nomination successful! Using! Using %s path %s:%d%s",  /* SID:217 */
    [LA_F218] = "Open P2P UDP socket on port %d",  /* SID:218 */
    [LA_F219] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:219 */
    [LA_F220] = "% OpenSSL requested but library not linked",  /* SID:220 */
    [LA_F221] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:221 */
    [LA_F222] = "% P2P connected, closing signaling TCP connection",  /* SID:222 */
    [LA_F223] = "% P2P connection established",  /* SID:223 */
    [LA_F224] = "% P2P punch failed, adding relay path",  /* SID:224 */
    [LA_F225] = "% P2P punching in progress ...",  /* SID:225 */
    [LA_F226] = "PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u\n",  /* SID:226 */
    [LA_F227] = "% PEER_INFO(trickle): seq overflow, cannot trickle more\n",  /* SID:227 */
    [LA_F228] = "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:228 */
    [LA_F229] = "% PUBSUB (SUB): waiting for offer from any peer",  /* SID:229 */
    [LA_F230] = "% PUBSUB mode requires gh_token and gist_id",  /* SID:230 */
    [LA_F231] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:231 */
    [LA_F232] = "Packet too large len=%d max=%d",  /* SID:232 */
    [LA_F233] = "Passive peer learned remote ID '%s' from OFFER",  /* SID:233 */
    [LA_F234] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:234 */
    [LA_F235] = "% Path recovered: switched to PUNCH",  /* SID:235 */
    [LA_F236] = "% Path switch debounced, waiting for stability",  /* SID:236 */
    [LA_F237] = "Path switched to better route (idx=%d)",  /* SID:237 */
    [LA_F238] = "Peer '%s' is now online (FORWARD received), resuming",  /* SID:238 */
    [LA_F239] = "Peer offline, cached %d candidates",  /* SID:239 */
    [LA_F240] = "Peer online, forwarded %d candidates",  /* SID:240 */
    [LA_F241] = "Processing (role=%s)",  /* SID:241 */
    [LA_F242] = "% PseudoTCP enabled as transport layer",  /* SID:242 */
    [LA_F243] = "REGISTERED: peer=%s\n",  /* SID:243 */
    [LA_F244] = "% RELAY/COMPACT mode requires server_host",  /* SID:244 */
    [LA_F245] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:245 */
    [LA_F246] = "Received ACK (status=%d, candidates_acked=%d)",  /* SID:246 */
    [LA_F247] = "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:247 */
    [LA_F248] = "Received DATA pkt from %s:%d, seq=%u, len=%d",  /* SID:248 */
    [LA_F249] = "% Received FIN packet, connection closed",  /* SID:249 */
    [LA_F250] = "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:250 */
    [LA_F251] = "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d",  /* SID:251 */
    [LA_F252] = "Received UNKNOWN pkt type: 0x%02X",  /* SID:252 */
    [LA_F253] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:253 */
    [LA_F254] = "Received valid signal from '%s'",  /* SID:254 */
    [LA_F255] = "Recv %s pkt from %s:%d",  /* SID:255 */
    [LA_F256] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:256 */
    [LA_F257] = "Recv %s pkt from %s:%d seq=%u",  /* SID:257 */
    [LA_F258] = "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)",  /* SID:258 */
    [LA_F259] = "Recv New Remote Candidate<%s:%d> (type=%d)",  /* SID:259 */
    [LA_F260] = "Register to COMPACT signaling server at %s:%d",  /* SID:260 */
    [LA_F261] = "Reliable transport initialized rto=%d win=%d",  /* SID:261 */
    [LA_F262] = "Requested Relay Candidate from %s",  /* SID:262 */
    [LA_F263] = "Requested Relay Candidate from TURN %s",  /* SID:263 */
    [LA_F264] = "Requested Srflx Candidate from %s",  /* SID:264 */
    [LA_F265] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:265 */
    [LA_F266] = "% SCTP (usrsctp) enabled as transport layer",  /* SID:266 */
    [LA_F267] = "% SSL_CTX_new failed",  /* SID:267 */
    [LA_F268] = "% SSL_new failed",  /* SID:268 */
    [LA_F269] = "Selected path: PUNCH (idx=%d)",  /* SID:269 */
    [LA_F270] = "Send offer to RELAY signaling server failed(%d)",  /* SID:270 */
    [LA_F271] = "Send window full, dropping packet send_count=%d",  /* SID:271 */
    [LA_F272] = "Sending Allocate Request to %s:%d",  /* SID:272 */
    [LA_F273] = "% Sending FIN packet to peer before closing",  /* SID:273 */
    [LA_F274] = "Sending Test I to %s:%d (len=%d)",  /* SID:274 */
    [LA_F275] = "% Sending UNREGISTER packet to COMPACT signaling server",  /* SID:275 */
    [LA_F276] = "Sent answer to '%s'",  /* SID:276 */
    [LA_F277] = "Sent answer to '%s' (%d bytes)",  /* SID:277 */
    [LA_F278] = "Sent connect request to '%s' (%d bytes)",  /* SID:278 */
    [LA_F279] = "Sent initial offer(%d) to %s)",  /* SID:279 */
    [LA_F280] = "% Signal payload deserialization failed",  /* SID:280 */
    [LA_F281] = "% Skipping local Host candidates on --public-only",  /* SID:281 */
    [LA_F282] = "Start internal thread failed(%d)",  /* SID:282 */
    [LA_F283] = "% Starting internal thread",  /* SID:283 */
    [LA_F284] = "% Stopping internal thread",  /* SID:284 */
    [LA_F285] = "% Storage full, waiting for peer to come online",  /* SID:285 */
    [LA_F286] = "% Switched to backup path: RELAY",  /* SID:286 */
    [LA_F287] = "% Synced path after failover",  /* SID:287 */
    [LA_F288] = "TURN 401 Unauthorized (realm=%s), authenticating...",  /* SID:288 */
    [LA_F289] = "TURN Allocate failed with error %d",  /* SID:289 */
    [LA_F290] = "TURN Allocated relay %s:%u (lifetime=%us)",  /* SID:290 */
    [LA_F291] = "TURN CreatePermission failed (error=%d)",  /* SID:291 */
    [LA_F292] = "TURN CreatePermission for %s",  /* SID:292 */
    [LA_F293] = "TURN Data Indication from %s:%u (%d bytes)",  /* SID:293 */
    [LA_F294] = "TURN Refresh failed (error=%d)",  /* SID:294 */
    [LA_F295] = "TURN Refresh ok (lifetime=%us)",  /* SID:295 */
    [LA_F296] = "% TURN auth required but no credentials configured",  /* SID:296 */
    [LA_F297] = "Test I: Mapped address: %s:%d",  /* SID:297 */
    [LA_F298] = "% Test I: Timeout",  /* SID:298 */
    [LA_F299] = "Test II: Success! Detection completed %s",  /* SID:299 */
    [LA_F300] = "% Test II: Timeout (need Test III)",  /* SID:300 */
    [LA_F301] = "Test III: Success! Detection completed %s",  /* SID:301 */
    [LA_F302] = "% Test III: Timeout",  /* SID:302 */
    [LA_F303] = "Transport layer '%s' init failed, falling back to simple reliable",  /* SID:303 */
    [LA_F304] = "UDP hole-punch probing remote candidates (%d candidates)",  /* SID:304 */
    [LA_F305] = "UDP hole-punch probing remote candidates round %d/%d",  /* SID:305 */
    [LA_F306] = "Unknown ACK status %d",  /* SID:306 */
    [LA_F307] = "Unknown signaling mode: %d",  /* SID:307 */
    [LA_F308] = "Updating Gist field '%s'...",  /* SID:308 */
    [LA_F309] = "Upsert remote candidate<%s:%d> (type=%d) failed(OOM)",  /* SID:309 */
    [LA_F310] = "% Using path: RELAY",  /* SID:310 */
    [LA_F311] = "Waiting for peer '%s' timed out (%dms), giving up",  /* SID:311 */
    [LA_F312] = "[MbedTLS] DTLS role: %s (mode=%s)",  /* SID:312 */
    [LA_F313] = "% [OpenSSL] DTLS handshake completed",  /* SID:313 */
    [LA_F314] = "[OpenSSL] DTLS role: %s (mode=%s)",  /* SID:314 */
    [LA_F315] = "[SCTP] association lost/shutdown (state=%u)",  /* SID:315 */
    [LA_F316] = "[SCTP] bind failed: %s",  /* SID:316 */
    [LA_F317] = "[SCTP] connect failed: %s",  /* SID:317 */
    [LA_F318] = "[SCTP] sendv failed: %s",  /* SID:318 */
    [LA_F319] = "[SIGNALING] Failed to send candidates, will retry (ret=%d)",  /* SID:319 */
    [LA_F320] = "[SIGNALING] Sent candidates (cached, peer offline) %d to %s",  /* SID:320 */
    [LA_F321] = "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)",  /* SID:321 */
    [LA_F322] = "% [SIGNALING] Server storage full, waiting for peer to come online",  /* SID:322 */
    [LA_F323] = "[Trickle] Immediately probing new candidate %s:%d",  /* SID:323 */
    [LA_F324] = "[Trickle] Sent 1 candidate to %s (online=%s)",  /* SID:324 */
    [LA_F325] = "% [Trickle] TCP not connected, skipping single candidate send",  /* SID:325 */
    [LA_F326] = "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()",  /* SID:326 */
    [LA_F327] = "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n",  /* SID:327 */
    [LA_F328] = "[UDP] %s recv from %s:%d, len=%d\n",  /* SID:328 */
    [LA_F329] = "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:329 */
    [LA_F330] = "[UDP] %s recv from %s:%d, seq=%u, len=%d\n",  /* SID:330 */
    [LA_F331] = "[UDP] %s recv from %s:%d\n",  /* SID:331 */
    [LA_F332] = "[UDP] %s send to %s:%d failed(%d)\n",  /* SID:332 */
    [LA_F333] = "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n",  /* SID:333 */
    [LA_F334] = "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:334 */
    [LA_F335] = "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:335 */
    [LA_F336] = "[UDP] %s_ACK send to %s:%d failed(%d)\n",  /* SID:336 */
    [LA_F337] = "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n",  /* SID:337 */
    [LA_F338] = "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n",  /* SID:338 */
    [LA_F339] = "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n",  /* SID:339 */
    [LA_F340] = "[lan_punch] starting NAT punch(Host candidate %d)",  /* SID:340 */
    [LA_F341] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:341 */
    [LA_F342] = "ctr_drbg_seed failed: -0x%x",  /* SID:342 */
    [LA_F343] = "% p2p_ice_send_local_candidate called in non-RELAY mode",  /* SID:343 */
    [LA_F344] = "recv error %d",  /* SID:344 */
    [LA_F345] = "recv error %d while discarding",  /* SID:345 */
    [LA_F346] = "recv error %d while reading payload",  /* SID:346 */
    [LA_F347] = "recv error %d while reading sender",  /* SID:347 */
    [LA_F348] = "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u",  /* SID:348 */
    [LA_F349] = "retry seq=%u retx=%d rto=%d",  /* SID:349 */
    [LA_F350] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:350 */
    [LA_F351] = "ssl_config_defaults failed: -0x%x",  /* SID:351 */
    [LA_F352] = "ssl_setup failed: -0x%x",  /* SID:352 */
    [LA_F353] = "transport send_data failed, %d bytes dropped",  /* SID:353 */
    [LA_F354] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:354 */
    [LA_F355] = "% ✗ Add Srflx candidate failed(OOM)",  /* SID:355 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_p2p_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
