/*
 * Auto-generated language strings
 */

#include ".LANG.h"

/* 字符串表 */
const char* lang_en[LA_NUM] = {
    [LA_W0] = "alive",  /* SID:1 */
    [LA_W1] = "bytes",  /* SID:2 */
    [LA_W2] = "Detecting...",  /* SID:3 */
    [LA_W3] = "Full Cone NAT",  /* SID:4 */
    [LA_W4] = "NAT punch failed, no TURN server configured",  /* SID:5 */
    [LA_W5] = "NAT punch failed, server has no relay support",  /* SID:6 */
    [LA_W6] = "NAT punch failed, using COMPACT server relay",  /* SID:7 */
    [LA_W7] = "no (cached)",  /* SID:8 */
    [LA_W8] = "Open Internet (No NAT)",  /* SID:9 */
    [LA_W9] = "Port Restricted Cone NAT",  /* SID:10 */
    [LA_W10] = "PUB",  /* SID:11 */
    [LA_W11] = "Published",  /* SID:12 */
    [LA_W12] = "punch",  /* SID:13 */
    [LA_W13] = "Received signal from",  /* SID:14 */
    [LA_W14] = "Resent",  /* SID:15 */
    [LA_W15] = "Restricted Cone NAT",  /* SID:16 */
    [LA_W16] = "retry",  /* SID:17 */
    [LA_W17] = "SUB",  /* SID:18 */
    [LA_W18] = "Symmetric NAT (port-random)",  /* SID:19 */
    [LA_W19] = "Timeout (no response)",  /* SID:20 */
    [LA_W20] = "UDP Blocked (STUN unreachable)",  /* SID:21 */
    [LA_W21] = "Unknown",  /* SID:22 */
    [LA_W22] = "Unsupported (no STUN/probe configured)",  /* SID:23 */
    [LA_W23] = "Waiting for incoming offer from any peer",  /* SID:24 */
    [LA_W24] = "yes",  /* SID:25 */
    [LA_S0] = "%s: address exchange failed: peer OFFLINE",  /* SID:26 */
    [LA_S1] = "%s: address exchange success, sending UDP probe",  /* SID:27 */
    [LA_S2] = "%s: already running, cannot trigger again",  /* SID:28 */
    [LA_S3] = "%s: peer is OFFLINE",  /* SID:29 */
    [LA_S4] = "%s: peer is online, waiting echo",  /* SID:30 */
    [LA_S5] = "%s: triggered on CONNECTED state (unnecessary)",  /* SID:31 */
    [LA_S6] = "%s: TURN allocated, starting address exchange",  /* SID:32 */
    [LA_S7] = "[OpenSSL] DTLS handshake completed",  /* SID:33 */
    [LA_S8] = "[SCTP] usrsctp wrapper initialized (skeleton)",  /* SID:34 */
    [LA_S9] = "[SIGNALING] Server storage full, waiting for peer to come online",  /* SID:35 */
    [LA_S10] = "[Trickle] TCP not connected, skipping single candidate send",  /* SID:36 */
    [LA_S11] = "Added RELAY path to path manager",  /* SID:37 */
    [LA_S12] = "Allocation successful!",  /* SID:38 */
    [LA_S13] = "Answer already present, skipping offer re-publish",  /* SID:39 */
    [LA_S14] = "Base64 decode failed",  /* SID:40 */
    [LA_S15] = "Bind failed",  /* SID:41 */
    [LA_S16] = "Channel ID validation failed",  /* SID:42 */
    [LA_S17] = "Close P2P UDP socket",  /* SID:43 */
    [LA_S18] = "Closing TCP connection to RELAY signaling server",  /* SID:44 */
    [LA_S19] = "COMPACT mode requires explicit remote_peer_id",  /* SID:45 */
    [LA_S20] = "Connection closed by server",  /* SID:46 */
    [LA_S21] = "Connection closed while discarding",  /* SID:47 */
    [LA_S22] = "Connection closed while reading payload",  /* SID:48 */
    [LA_S23] = "Connection closed while reading sender",  /* SID:49 */
    [LA_S24] = "Detecting local network addresses",  /* SID:50 */
    [LA_S25] = "DTLS (MbedTLS) requested but library not linked",  /* SID:51 */
    [LA_S26] = "Failed to allocate ACK payload buffer",  /* SID:52 */
    [LA_S27] = "Failed to allocate discard buffer, closing connection",  /* SID:53 */
    [LA_S28] = "Failed to allocate memory for candidate lists",  /* SID:54 */
    [LA_S29] = "Failed to allocate memory for session",  /* SID:55 */
    [LA_S30] = "Failed to build STUN request",  /* SID:56 */
    [LA_S31] = "Failed to push remote candidate",  /* SID:57 */
    [LA_S32] = "Failed to reserve remote candidates (cnt=1)",  /* SID:58 */
    [LA_S33] = "Failed to send header",  /* SID:59 */
    [LA_S34] = "Failed to send payload",  /* SID:60 */
    [LA_S35] = "Failed to send punch packet for new peer addr",  /* SID:61 */
    [LA_S36] = "Failed to send target name",  /* SID:62 */
    [LA_S37] = "Gist GET failed",  /* SID:63 */
    [LA_S38] = "Handshake complete",  /* SID:64 */
    [LA_S39] = "Invalid channel_id format (security risk)",  /* SID:65 */
    [LA_S40] = "Invalid signaling mode in configuration",  /* SID:66 */
    [LA_S41] = "NAT connection recovered, upgrading from RELAY to CONNECTED",  /* SID:67 */
    [LA_S42] = "NAT connection timeout, downgrading to relay mode",  /* SID:68 */
    [LA_S43] = "No advanced transport layer enabled, using simple reliable layer",  /* SID:69 */
    [LA_S44] = "No auth_key provided, using default key (insecure)",  /* SID:70 */
    [LA_S45] = "OpenSSL requested but library not linked",  /* SID:71 */
    [LA_S46] = "Out of memory",  /* SID:72 */
    [LA_S47] = "P2P connected, closing signaling TCP connection",  /* SID:73 */
    [LA_S48] = "P2P connection established",  /* SID:74 */
    [LA_S49] = "P2P punch failed, adding relay path",  /* SID:75 */
    [LA_S50] = "P2P punching in progress ...",  /* SID:76 */
    [LA_S51] = "p2p_ice_send_local_candidate called in non-RELAY mode",  /* SID:77 */
    [LA_S52] = "Path switch debounced, waiting for stability",  /* SID:78 */
    [LA_S53] = "PseudoTCP enabled as transport layer",  /* SID:79 */
    [LA_S54] = "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:80 */
    [LA_S55] = "PUBSUB (SUB): waiting for offer from any peer",  /* SID:81 */
    [LA_S56] = "PUBSUB mode requires gh_token and gist_id",  /* SID:82 */
    [LA_S57] = "Received FIN packet, connection closed",  /* SID:83 */
    [LA_S58] = "RELAY/COMPACT mode requires server_host",  /* SID:84 */
    [LA_S59] = "SCTP (usrsctp) requested but library not linked",  /* SID:85 */
    [LA_S60] = "Sending FIN packet to peer before closing",  /* SID:86 */
    [LA_S61] = "Sending FIN packet to peer on destroy",  /* SID:87 */
    [LA_S62] = "Sending UNREGISTER packet to COMPACT signaling server",  /* SID:88 */
    [LA_S63] = "Signal payload deserialization failed",  /* SID:89 */
    [LA_S64] = "Skipping local Host candidates on --public-only",  /* SID:90 */
    [LA_S65] = "Starting internal thread",  /* SID:91 */
    [LA_S66] = "Stopping internal thread",  /* SID:92 */
    [LA_S67] = "Storage full, waiting for peer to come online",  /* SID:93 */
    [LA_S68] = "Switched to backup path: RELAY",  /* SID:94 */
    [LA_S69] = "Using path: RELAY",  /* SID:95 */
    [LA_F0] = "  ... and %d more pairs",  /* SID:96 */
    [LA_F1] = "  [%d] %s/%d",  /* SID:97 */
    [LA_F2] = "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx",  /* SID:98 */
    [LA_F3] = "  [%d]<%s:%d> (type: %s)",  /* SID:99 */
    [LA_F4] = "%s '%s' (%u %s)",  /* SID:100 */
    [LA_F5] = "%s NOTIFY: accepted",  /* SID:101 */
    [LA_F6] = "%s NOTIFY: ignored old notify base=%u (current=%u)",  /* SID:102 */
    [LA_F7] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)",  /* SID:103 */
    [LA_F8] = "%s msg=0: accepted, echo reply (sid=%u, len=%d)",  /* SID:104 */
    [LA_F9] = "%s received (ice_state=%d), resetting ICE and clearing %d stale candidates",  /* SID:105 */
    [LA_F10] = "%s resent, %d/%d",  /* SID:106 */
    [LA_F11] = "%s sent to %s:%d",  /* SID:107 */
    [LA_F12] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:108 */
    [LA_F13] = "%s sent to %s:%d, echo_seq=%u",  /* SID:109 */
    [LA_F14] = "%s sent, inst_id=%u",  /* SID:110 */
    [LA_F15] = "%s sent, inst_id=%u, cands=%d",  /* SID:111 */
    [LA_F16] = "%s sent, seq=%u",  /* SID:112 */
    [LA_F17] = "%s sent, sid=%u, msg=%u, size=%d",  /* SID:113 */
    [LA_F18] = "%s sent, size=%d (ses_id=%llu)",  /* SID:540 */
    [LA_F19] = "%s sent, total=%d (ses_id=%llu)",  /* SID:541 */
    [LA_F20] = "%s seq=0: accepted cand_cnt=%d",  /* SID:116 */
    [LA_F21] = "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)",  /* SID:117 */
    [LA_F22] = "%s, retry remaining candidates and FIN to peer",  /* SID:118 */
    [LA_F23] = "%s, sent on %s",  /* SID:119 */
    [LA_F24] = "%s: %s timeout after %d retries (sid=%u)",  /* SID:120 */
    [LA_F25] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)",  /* SID:121 */
    [LA_F26] = "%s: Peer addr changed -> %s:%d, retrying punch",  /* SID:122 */
    [LA_F27] = "%s: RPC fail due to peer offline (sid=%u)",  /* SID:123 */
    [LA_F28] = "%s: RPC fail due to relay timeout (sid=%u)",  /* SID:124 */
    [LA_F29] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:542 */
    [LA_F30] = "%s: TURN allocation failed: ret=%d",  /* SID:126 */
    [LA_F31] = "%s: TURN allocation request sent",  /* SID:127 */
    [LA_F32] = "%s: UDP timeout, retry %d/%d",  /* SID:128 */
    [LA_F33] = "%s: UDP timeout: peer not responding",  /* SID:129 */
    [LA_F34] = "%s: accepted",  /* SID:130 */
    [LA_F35] = "%s: accepted (ses_id=%llu)",  /* SID:543 */
    [LA_F36] = "%s: accepted for ack_seq=%u",  /* SID:132 */
    [LA_F37] = "%s: accepted from cand[%d]",  /* SID:133 */
    [LA_F38] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x",  /* SID:134 */
    [LA_F39] = "%s: accepted sid=%u, msg=%u",  /* SID:135 */
    [LA_F40] = "%s: accepted, RPC complete (sid=%u)",  /* SID:136 */
    [LA_F41] = "%s: accepted, RPC finished (sid=%u)",  /* SID:137 */
    [LA_F42] = "%s: accepted, len=%d (ses_id=%llu)",  /* SID:544 */
    [LA_F43] = "%s: accepted, probe_mapped=%s:%d",  /* SID:139 */
    [LA_F44] = "%s: accepted, public=%s:%d max_cands=%d probe_port=%d relay=%s msg=%s",  /* SID:140 */
    [LA_F45] = "%s: accepted, waiting for response (sid=%u)",  /* SID:141 */
    [LA_F46] = "%s: already connected, ignoring batch punch request",  /* SID:142 */
    [LA_F47] = "%s: bad payload(len=%d cand_cnt=%d)",  /* SID:143 */
    [LA_F48] = "%s: bad payload(len=%d)",  /* SID:144 */
    [LA_F49] = "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)",  /* SID:145 */
    [LA_F50] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s",  /* SID:146 */
    [LA_F51] = "%s: discovered unsynced prflx cand<%s:%d>[%d]",  /* SID:147 */
    [LA_F52] = "%s: duplicate request ignored (sid=%u, already processing)",  /* SID:148 */
    [LA_F53] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)",  /* SID:149 */
    [LA_F54] = "%s: entered, %s arrived after PEER_INFO",  /* SID:150 */
    [LA_F55] = "%s: entered, %s arrived after REGISTERED",  /* SID:151 */
    [LA_F56] = "%s: exchange timeout, retry %d/%d",  /* SID:152 */
    [LA_F57] = "%s: exchange timeout: peer not responding",  /* SID:153 */
    [LA_F58] = "%s: failed to RE-REGISTER after timeout",  /* SID:154 */
    [LA_F59] = "%s: failed to send UNREGISTER before restart",  /* SID:155 */
    [LA_F60] = "%s: failed to track cand<%s:%d>, dropping",  /* SID:156 */
    [LA_F61] = "%s: ignored (relay not supported)",  /* SID:157 */
    [LA_F62] = "%s: ignored for duplicated seq=%u, already acked",  /* SID:158 */
    [LA_F63] = "%s: ignored for seq=%u (expect=%d)",  /* SID:159 */
    [LA_F64] = "%s: ignored for ses_id=%llu (local ses_id=%llu)",  /* SID:545 */
    [LA_F65] = "%s: ignored for sid=%u (current sid=%u)",  /* SID:161 */
    [LA_F66] = "%s: ignored in invalid state=%d",  /* SID:162 */
    [LA_F67] = "%s: ignored in state(%d)",  /* SID:163 */
    [LA_F68] = "%s: ignored in state=%d",  /* SID:164 */
    [LA_F69] = "%s: invalid ack_seq=%u",  /* SID:165 */
    [LA_F70] = "%s: invalid cand idx: %d (count: %d)",  /* SID:166 */
    [LA_F71] = "%s: invalid for non-relay req",  /* SID:167 */
    [LA_F72] = "%s: invalid in non-COMPACT mode",  /* SID:168 */
    [LA_F73] = "%s: invalid seq=%u",  /* SID:169 */
    [LA_F74] = "%s: keep alive to %d reachable cand(s)",  /* SID:170 */
    [LA_F75] = "%s: new request (sid=%u) overrides pending request (sid=%u)",  /* SID:171 */
    [LA_F76] = "%s: no remote candidates to punch",  /* SID:172 */
    [LA_F77] = "%s: no response for %llu ms, connection lost",  /* SID:546 */
    [LA_F78] = "%s: old request ignored (sid=%u <= last_sid=%u)",  /* SID:174 */
    [LA_F79] = "%s: peer disconnected (ses_id=%llu), reset to REGISTERED",  /* SID:547 */
    [LA_F80] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:548 */
    [LA_F81] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:549 */
    [LA_F82] = "%s: punching additional cand<%s:%d>[%d] while connected",  /* SID:178 */
    [LA_F83] = "%s: punching remote cand<%s:%d>[%d]",  /* SID:179 */
    [LA_F84] = "%s: received FIN from peer, marking NAT as CLOSED",  /* SID:180 */
    [LA_F85] = "%s: remote candidate[%d] %s:%d, starting punch",  /* SID:181 */
    [LA_F86] = "%s: restarting periodic check",  /* SID:182 */
    [LA_F87] = "%s: retry(%d/%d) probe",  /* SID:183 */
    [LA_F88] = "%s: retry(%d/%d) req (sid=%u)",  /* SID:184 */
    [LA_F89] = "%s: retry(%d/%d) resp (sid=%u)",  /* SID:185 */
    [LA_F90] = "%s: retry, (attempt %d/%d)",  /* SID:186 */
    [LA_F91] = "%s: rx confirmed: peer->me path is UP (%s:%d)",  /* SID:187 */
    [LA_F92] = "%s: send failed(%d)",  /* SID:188 */
    [LA_F93] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:189 */
    [LA_F94] = "%s: sent, sid=%u, code=%u, size=%d",  /* SID:190 */
    [LA_F95] = "%s: session mismatch(local=%llu pkt=%llu)",  /* SID:550 */
    [LA_F96] = "%s: session mismatch(local=%llu, pkt=%llu)",  /* SID:551 */
    [LA_F97] = "%s: skip and mark NAT as OPEN (lan_punch enabled)",  /* SID:193 */
    [LA_F98] = "%s: start punching all(%d) remote candidates",  /* SID:194 */
    [LA_F99] = "%s: started, sending first probe",  /* SID:195 */
    [LA_F100] = "%s: status error(%d)",  /* SID:196 */
    [LA_F101] = "%s: sync complete (ses_id=%llu)",  /* SID:552 */
    [LA_F102] = "%s: sync complete (ses_id=%llu, mask=0x%04x)",  /* SID:553 */
    [LA_F103] = "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)",  /* SID:199 */
    [LA_F104] = "%s: timeout after %d retries , type unknown",  /* SID:200 */
    [LA_F105] = "%s: timeout after %llu ms (ICE done), switching to RELAY",  /* SID:554 */
    [LA_F106] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:555 */
    [LA_F107] = "%s: timeout, max(%d) attempts reached, reset to INIT",  /* SID:203 */
    [LA_F108] = "%s: timeout, peer did not respond",  /* SID:204 */
    [LA_F109] = "%s: timeout, retry %d/%d",  /* SID:205 */
    [LA_F110] = "%s: triggered via COMPACT msg echo",  /* SID:206 */
    [LA_F111] = "%s: triggered via RELAY TUNE echo",  /* SID:207 */
    [LA_F112] = "%s: tx confirmed: me->peer path is UP (echoed seq=%u)",  /* SID:208 */
    [LA_F113] = "%s: unexpected ack_seq=%u mask=0x%04x",  /* SID:209 */
    [LA_F114] = "%s:%04d: %s",  /* SID:210 */
    [LA_F115] = "%s_ACK sent, seq=%u (ses_id=%llu)",  /* SID:556 */
    [LA_F116] = "%s_ACK sent, sid=%u",  /* SID:212 */
    [LA_F117] = "ACK processed ack_seq=%u send_base=%u inflight=%d",  /* SID:213 */
    [LA_F118] = "Added Remote Candidate: %d -> %s:%d",  /* SID:214 */
    [LA_F119] = "Append Host candidate: %s:%d",  /* SID:215 */
    [LA_F120] = "Attempting Simultaneous Open to %s:%d",  /* SID:216 */
    [LA_F121] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:217 */
    [LA_F122] = "Bind failed to %d, port busy, trying random port",  /* SID:218 */
    [LA_F123] = "Bound to :%d",  /* SID:219 */
    [LA_F124] = "Connect to COMPACT signaling server failed(%d)",  /* SID:220 */
    [LA_F125] = "Connect to RELAY signaling server failed(%d)",  /* SID:221 */
    [LA_F126] = "Connected to server %s:%d as '%s'",  /* SID:222 */
    [LA_F127] = "Connecting to RELAY signaling server at %s:%d",  /* SID:223 */
    [LA_F128] = "Connectivity checks timed out (sent %d rounds), giving up",  /* SID:224 */
    [LA_F129] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:225 */
    [LA_F130] = "Detect local network interfaces failed(%d)",  /* SID:226 */
    [LA_F131] = "Detection completed %s",  /* SID:227 */
    [LA_F132] = "Discarded %d bytes payload of message type %d",  /* SID:228 */
    [LA_F133] = "Failed to allocate %u bytes",  /* SID:229 */
    [LA_F134] = "Failed to realloc memory for local candidates (capacity: %d)",  /* SID:230 */
    [LA_F135] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:231 */
    [LA_F136] = "Failed to reserve remote candidates (base=%u cnt=%d)",  /* SID:232 */
    [LA_F137] = "Failed to reserve remote candidates (cnt=%d)",  /* SID:233 */
    [LA_F138] = "Failed to resolve STUN server %s",  /* SID:234 */
    [LA_F139] = "Failed to resolve TURN server: %s",  /* SID:235 */
    [LA_F140] = "Field %s is empty or too short",  /* SID:236 */
    [LA_F141] = "First offer, resetting ICE and clearing %d stale candidates",  /* SID:237 */
    [LA_F142] = "Formed check list with %d candidate pairs",  /* SID:238 */
    [LA_F143] = "Gathered Host Candidate: %s:%d (priority=0x%08x)",  /* SID:239 */
    [LA_F144] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:240 */
    [LA_F145] = "Handshake failed: %s (-0x%04x)",  /* SID:241 */
    [LA_F146] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:242 */
    [LA_F147] = "Initialize signaling mode: %d",  /* SID:243 */
    [LA_F148] = "Initialized: %s",  /* SID:244 */
    [LA_F149] = "Invalid magic 0x%x (expected 0x%x), resetting",  /* SID:245 */
    [LA_F150] = "Invalid read state %d, resetting",  /* SID:246 */
    [LA_F151] = "Local address detection done: %d address(es)",  /* SID:247 */
    [LA_F152] = "Marked old path (idx=%d) as FAILED due to addr change",  /* SID:248 */
    [LA_F153] = "Nomination successful! Using! Using %s path %s:%d%s",  /* SID:249 */
    [LA_F154] = "Open P2P UDP socket on port %d",  /* SID:250 */
    [LA_F155] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:251 */
    [LA_F156] = "Out-of-window packet discarded seq=%u base=%u",  /* SID:252 */
    [LA_F157] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:253 */
    [LA_F158] = "Packet too large len=%d max=%d",  /* SID:254 */
    [LA_F159] = "Passive peer learned remote ID '%s' from OFFER",  /* SID:255 */
    [LA_F160] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:256 */
    [LA_F161] = "Path recovered: switched to PUNCH",  /* SID:257 */
    [LA_F162] = "Path switched to better route (idx=%d)",  /* SID:258 */
    [LA_F163] = "Peer '%s' is now online (FORWARD received), resuming",  /* SID:259 */
    [LA_F164] = "Peer offline, cached %d candidates",  /* SID:260 */
    [LA_F165] = "Peer online, forwarded %d candidates",  /* SID:261 */
    [LA_F166] = "Processing (role=%s)",  /* SID:262 */
    [LA_F167] = "REGISTERED: peer=%s",  /* SID:263 */
    [LA_F168] = "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d",  /* SID:264 */
    [LA_F169] = "Received ACK (status=%d, candidates_acked=%d)",  /* SID:265 */
    [LA_F170] = "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x",  /* SID:266 */
    [LA_F171] = "Received DATA pkt from %s:%d, seq=%u, len=%d",  /* SID:267 */
    [LA_F172] = "Received New Remote Candidate: %d -> %s:%d",  /* SID:268 */
    [LA_F173] = "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d",  /* SID:269 */
    [LA_F174] = "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d",  /* SID:270 */
    [LA_F175] = "Received UNKNOWN pkt type: 0x%02X",  /* SID:271 */
    [LA_F176] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:272 */
    [LA_F177] = "Received valid signal from '%s'",  /* SID:273 */
    [LA_F178] = "Recv %s pkt from %s:%d",  /* SID:274 */
    [LA_F179] = "Recv %s pkt from %s:%d echo_seq=%u",  /* SID:275 */
    [LA_F180] = "Recv %s pkt from %s:%d seq=%u",  /* SID:276 */
    [LA_F181] = "Recv %s pkt from %s:%d, flags=0x%02x, len=%d",  /* SID:277 */
    [LA_F182] = "Recv %s pkt from %s:%d, len=%d",  /* SID:278 */
    [LA_F183] = "Recv %s pkt from %s:%d, seq=%u, flags=0x%02x, len=%d",  /* SID:279 */
    [LA_F184] = "Recv %s pkt from %s:%d, seq=%u, len=%d",  /* SID:280 */
    [LA_F185] = "Register to COMPACT signaling server at %s:%d",  /* SID:281 */
    [LA_F186] = "Reliable transport initialized rto=%d win=%d",  /* SID:282 */
    [LA_F187] = "Requested Relay Candidate from %s",  /* SID:283 */
    [LA_F188] = "Requested Srflx Candidate from %s",  /* SID:284 */
    [LA_F189] = "Resend %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d",  /* SID:285 */
    [LA_F190] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:286 */
    [LA_F191] = "Selected path: PUNCH (idx=%d)",  /* SID:287 */
    [LA_F192] = "Send %s pkt to %s:%d, seq=%u, flags=0, len=0",  /* SID:288 */
    [LA_F193] = "Send %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d",  /* SID:289 */
    [LA_F194] = "Send %s pkt to %s:%d, seq=0, flags=0, len=%d",  /* SID:290 */
    [LA_F195] = "Send %s_ACK pkt to %s:%d, seq=%u, flags=0, len=%d",  /* SID:291 */
    [LA_F196] = "Send %s_ACK pkt to %s:%d, seq=0, flags=0, len=2",  /* SID:292 */
    [LA_F197] = "Send offer to RELAY signaling server failed(%d)",  /* SID:293 */
    [LA_F198] = "Send window full, dropping packet send_count=%d",  /* SID:294 */
    [LA_F199] = "Sending Allocate Request to %s:%d",  /* SID:295 */
    [LA_F200] = "Sending Test I to %s:%d (len=%d)",  /* SID:296 */
    [LA_F201] = "Sent answer to '%s'",  /* SID:297 */
    [LA_F202] = "Sent answer to '%s' (%d bytes)",  /* SID:298 */
    [LA_F203] = "Sent connect request to '%s' (%d bytes)",  /* SID:299 */
    [LA_F204] = "Sent initial offer(%d) to %s)",  /* SID:300 */
    [LA_F205] = "Start internal thread failed(%d)",  /* SID:301 */
    [LA_F206] = "Synced path after failover",  /* SID:302 */
    [LA_F207] = "Test I: Mapped address: %s:%d",  /* SID:303 */
    [LA_F208] = "Test I: Timeout",  /* SID:304 */
    [LA_F209] = "Test II: Success! Detection completed %s",  /* SID:305 */
    [LA_F210] = "Test II: Timeout (need Test III)",  /* SID:306 */
    [LA_F211] = "Test III: Success! Detection completed %s",  /* SID:307 */
    [LA_F212] = "Test III: Timeout",  /* SID:308 */
    [LA_F213] = "UDP hole-punch probing remote candidates (%d candidates)",  /* SID:309 */
    [LA_F214] = "UDP hole-punch probing remote candidates round %d/%d",  /* SID:310 */
    [LA_F215] = "Unknown ACK status %d",  /* SID:311 */
    [LA_F216] = "Unknown signaling mode: %d",  /* SID:312 */
    [LA_F217] = "Updating Gist field '%s'...",  /* SID:313 */
    [LA_F218] = "Waiting for peer '%s' timed out (%dms), giving up",  /* SID:314 */
    [LA_F219] = "[SCTP] received encapsulated packet, length %d",  /* SID:315 */
    [LA_F220] = "[SCTP] sending %d bytes",  /* SID:316 */
    [LA_F221] = "[SIGNALING] Failed to send candidates, will retry (ret=%d)",  /* SID:317 */
    [LA_F222] = "[SIGNALING] Sent candidates (cached, peer offline) %d to %s",  /* SID:318 */
    [LA_F223] = "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)",  /* SID:319 */
    [LA_F224] = "[Trickle] Immediately probing new candidate %s:%d",  /* SID:320 */
    [LA_F225] = "[Trickle] Sent 1 candidate to %s (online=%s)",  /* SID:321 */
    [LA_F226] = "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()",  /* SID:322 */
    [LA_F227] = "[lan_punch] starting NAT punch(Host candidate %d)",  /* SID:323 */
    [LA_F228] = "[prflx] Received New Remote Candidate %s:%d (Peer Reflexive - symmetric NAT)",  /* SID:324 */
    [LA_F229] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:325 */
    [LA_F230] = "recv error %d",  /* SID:326 */
    [LA_F231] = "recv error %d while discarding",  /* SID:327 */
    [LA_F232] = "recv error %d while reading payload",  /* SID:328 */
    [LA_F233] = "recv error %d while reading sender",  /* SID:329 */
    [LA_F234] = "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u",  /* SID:330 */
    [LA_F235] = "retry seq=%u retx=%d rto=%d",  /* SID:331 */
    [LA_F236] = "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d",  /* SID:332 */
    [LA_F237] = "ssl_setup failed: -0x%x",  /* SID:333 */
    [LA_F238] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:334 */
    [LA_F239] = "✗ Cannot add Srflx candidate: realloc failed (OOM)",  /* SID:335 */
};
