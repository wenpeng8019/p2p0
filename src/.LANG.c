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
    [LA_S11] = "Allocation successful!",  /* SID:37 */
    [LA_S12] = "Answer already present, skipping offer re-publish",  /* SID:38 */
    [LA_S13] = "Base64 decode failed",  /* SID:39 */
    [LA_S14] = "Bind failed",  /* SID:40 */
    [LA_S15] = "Channel ID validation failed",  /* SID:41 */
    [LA_S16] = "Close P2P UDP socket",  /* SID:42 */
    [LA_S17] = "Closing TCP connection to RELAY signaling server",  /* SID:43 */
    [LA_S18] = "COMPACT mode requires explicit remote_peer_id",  /* SID:44 */
    [LA_S19] = "Connection closed by server",  /* SID:45 */
    [LA_S20] = "Connection closed while discarding",  /* SID:46 */
    [LA_S21] = "Connection closed while reading payload",  /* SID:47 */
    [LA_S22] = "Connection closed while reading sender",  /* SID:48 */
    [LA_S23] = "DTLS (MbedTLS) requested but library not linked",  /* SID:49 */
    [LA_S24] = "Failed to allocate ACK payload buffer",  /* SID:50 */
    [LA_S25] = "Failed to allocate discard buffer, closing connection",  /* SID:51 */
    [LA_S26] = "Failed to allocate memory for candidate lists",  /* SID:52 */
    [LA_S27] = "Failed to allocate memory for session",  /* SID:53 */
    [LA_S28] = "Failed to build STUN request",  /* SID:54 */
    [LA_S29] = "Failed to push remote candidate",  /* SID:55 */
    [LA_S30] = "Failed to reserve remote candidates (cnt=1)",  /* SID:56 */
    [LA_S31] = "Failed to send header",  /* SID:57 */
    [LA_S32] = "Failed to send payload",  /* SID:58 */
    [LA_S33] = "Failed to send punch packet for new peer addr",  /* SID:59 */
    [LA_S34] = "Failed to send target name",  /* SID:60 */
    [LA_S35] = "Handshake complete",  /* SID:61 */
    [LA_S36] = "Invalid channel_id format (security risk)",  /* SID:62 */
    [LA_S37] = "Invalid signaling mode in configuration",  /* SID:63 */
    [LA_S38] = "NAT connection recovered, upgrading from RELAY to CONNECTED",  /* SID:64 */
    [LA_S39] = "NAT connection timeout, downgrading to relay mode",  /* SID:65 */
    [LA_S40] = "No advanced transport layer enabled, using simple reliable layer",  /* SID:66 */
    [LA_S41] = "No auth_key provided, using default key (insecure)",  /* SID:67 */
    [LA_S42] = "OpenSSL requested but library not linked",  /* SID:68 */
    [LA_S43] = "Out of memory",  /* SID:69 */
    [LA_S44] = "P2P connected, closing signaling TCP connection",  /* SID:70 */
    [LA_S45] = "P2P connection established",  /* SID:71 */
    [LA_S46] = "P2P punch failed, adding relay path",  /* SID:72 */
    [LA_S47] = "P2P punching in progress ...",  /* SID:73 */
    [LA_S48] = "p2p_ice_send_local_candidate called in non-RELAY mode",  /* SID:74 */
    [LA_S49] = "Path switch debounced, waiting for stability",  /* SID:75 */
    [LA_S50] = "PseudoTCP enabled as transport layer",  /* SID:76 */
    [LA_S51] = "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing",  /* SID:77 */
    [LA_S52] = "PUBSUB (SUB): waiting for offer from any peer",  /* SID:78 */
    [LA_S53] = "PUBSUB mode requires gh_token and gist_id",  /* SID:79 */
    [LA_S54] = "Received FIN packet, connection closed",  /* SID:80 */
    [LA_S55] = "RELAY/COMPACT mode requires server_host",  /* SID:81 */
    [LA_S56] = "SCTP (usrsctp) requested but library not linked",  /* SID:82 */
    [LA_S57] = "Sending FIN packet to peer before closing",  /* SID:83 */
    [LA_S58] = "Sending FIN packet to peer on destroy",  /* SID:84 */
    [LA_S59] = "Sending UNREGISTER packet to COMPACT signaling server",  /* SID:85 */
    [LA_S60] = "Signal payload deserialization failed",  /* SID:86 */
    [LA_S61] = "Skipping local Host candidates on --public-only",  /* SID:87 */
    [LA_S62] = "Starting internal thread",  /* SID:88 */
    [LA_S63] = "Stopping internal thread",  /* SID:89 */
    [LA_S64] = "Storage full, waiting for peer to come online",  /* SID:90 */
    [LA_F0] = "  ... and %d more pairs",  /* SID:91 */
    [LA_F1] = "  [%d] %s/%d",  /* SID:92 */
    [LA_F2] = "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx",  /* SID:93 */
    [LA_F3] = "  [%d]<%s:%d> (type: %s)",  /* SID:94 */
    [LA_F4] = "%s '%s' (%u %s)",  /* SID:95 */
    [LA_F5] = "%s NOTIFY: accepted",  /* SID:96 */
    [LA_F6] = "%s NOTIFY: ignored old notify base=%u (current=%u)",  /* SID:97 */
    [LA_F7] = "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)",  /* SID:98 */
    [LA_F8] = "%s msg=0: accepted, echo reply (sid=%u, len=%d)",  /* SID:99 */
    [LA_F9] = "%s resent, %d/%d",  /* SID:100 */
    [LA_F10] = "%s sent to %s:%d",  /* SID:101 */
    [LA_F11] = "%s sent to %s:%d for %s, seq=%d, path=%d",  /* SID:102 */
    [LA_F12] = "%s sent to %s:%d, echo_seq=%u",  /* SID:103 */
    [LA_F13] = "%s sent, inst_id=%u",  /* SID:104 */
    [LA_F14] = "%s sent, inst_id=%u, cands=%d",  /* SID:105 */
    [LA_F15] = "%s sent, seq=%u",  /* SID:106 */
    [LA_F16] = "%s sent, sid=%u, msg=%u, size=%d",  /* SID:107 */
    [LA_F17] = "%s sent, size=%d (ses_id=%llu)",  /* SID:438 */
    [LA_F18] = "%s sent, total=%d (ses_id=%llu)",  /* SID:439 */
    [LA_F19] = "%s seq=0: accepted cand_cnt=%d",  /* SID:110 */
    [LA_F20] = "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)",  /* SID:111 */
    [LA_F21] = "%s, retry remaining candidates and FIN to peer",  /* SID:112 */
    [LA_F22] = "%s, sent on %s",  /* SID:113 */
    [LA_F23] = "%s: %s timeout after %d retries (sid=%u)",  /* SID:114 */
    [LA_F24] = "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)",  /* SID:115 */
    [LA_F25] = "%s: Peer addr changed -> %s:%d, retrying punch",  /* SID:116 */
    [LA_F26] = "%s: RPC fail due to peer offline (sid=%u)",  /* SID:117 */
    [LA_F27] = "%s: RPC fail due to relay timeout (sid=%u)",  /* SID:118 */
    [LA_F28] = "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)",  /* SID:440 */
    [LA_F29] = "%s: TURN allocation failed: ret=%d",  /* SID:120 */
    [LA_F30] = "%s: TURN allocation request sent",  /* SID:121 */
    [LA_F31] = "%s: UDP timeout, retry %d/%d",  /* SID:122 */
    [LA_F32] = "%s: UDP timeout: peer not responding",  /* SID:123 */
    [LA_F33] = "%s: accepted",  /* SID:124 */
    [LA_F34] = "%s: accepted (ses_id=%llu)",  /* SID:441 */
    [LA_F35] = "%s: accepted for ack_seq=%u",  /* SID:126 */
    [LA_F36] = "%s: accepted from cand[%d]",  /* SID:127 */
    [LA_F37] = "%s: accepted seq=%u cand_cnt=%d flags=0x%02x",  /* SID:128 */
    [LA_F38] = "%s: accepted sid=%u, msg=%u",  /* SID:129 */
    [LA_F39] = "%s: accepted, RPC complete (sid=%u)",  /* SID:130 */
    [LA_F40] = "%s: accepted, RPC finished (sid=%u)",  /* SID:131 */
    [LA_F41] = "%s: accepted, len=%d (ses_id=%llu)",  /* SID:442 */
    [LA_F42] = "%s: accepted, probe_mapped=%s:%d",  /* SID:133 */
    [LA_F43] = "%s: accepted, public=%s:%d max_cands=%d probe_port=%d relay=%s msg=%s",  /* SID:134 */
    [LA_F44] = "%s: accepted, waiting for response (sid=%u)",  /* SID:135 */
    [LA_F45] = "%s: already connected, ignoring batch punch request",  /* SID:136 */
    [LA_F46] = "%s: bad payload(len=%d cand_cnt=%d)",  /* SID:137 */
    [LA_F47] = "%s: bad payload(len=%d)",  /* SID:138 */
    [LA_F48] = "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)",  /* SID:139 */
    [LA_F49] = "%s: completed, mapped=%s:%d probe=%s:%d -> %s",  /* SID:140 */
    [LA_F50] = "%s: discovered unsynced prflx cand<%s:%d>[%d]",  /* SID:141 */
    [LA_F51] = "%s: duplicate request ignored (sid=%u, already processing)",  /* SID:142 */
    [LA_F52] = "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)",  /* SID:143 */
    [LA_F53] = "%s: entered, %s arrived after PEER_INFO",  /* SID:144 */
    [LA_F54] = "%s: entered, %s arrived after REGISTERED",  /* SID:145 */
    [LA_F55] = "%s: exchange timeout, retry %d/%d",  /* SID:146 */
    [LA_F56] = "%s: exchange timeout: peer not responding",  /* SID:147 */
    [LA_F57] = "%s: failed to RE-REGISTER after timeout",  /* SID:148 */
    [LA_F58] = "%s: failed to send UNREGISTER before restart",  /* SID:149 */
    [LA_F59] = "%s: failed to track cand<%s:%d>, dropping",  /* SID:150 */
    [LA_F60] = "%s: ignored (relay not supported)",  /* SID:151 */
    [LA_F61] = "%s: ignored for duplicated seq=%u, already acked",  /* SID:152 */
    [LA_F62] = "%s: ignored for seq=%u (expect=%d)",  /* SID:153 */
    [LA_F63] = "%s: ignored for ses_id=%llu (local ses_id=%llu)",  /* SID:443 */
    [LA_F64] = "%s: ignored for sid=%u (current sid=%u)",  /* SID:155 */
    [LA_F65] = "%s: ignored in invalid state=%d",  /* SID:156 */
    [LA_F66] = "%s: ignored in state(%d)",  /* SID:157 */
    [LA_F67] = "%s: ignored in state=%d",  /* SID:158 */
    [LA_F68] = "%s: invalid ack_seq=%u",  /* SID:159 */
    [LA_F69] = "%s: invalid cand idx: %d (count: %d)",  /* SID:160 */
    [LA_F70] = "%s: invalid for non-relay req",  /* SID:161 */
    [LA_F71] = "%s: invalid in non-COMPACT mode",  /* SID:162 */
    [LA_F72] = "%s: invalid seq=%u",  /* SID:163 */
    [LA_F73] = "%s: keep alive to %d reachable cand(s)",  /* SID:164 */
    [LA_F74] = "%s: new request (sid=%u) overrides pending request (sid=%u)",  /* SID:165 */
    [LA_F75] = "%s: no remote candidates to punch",  /* SID:166 */
    [LA_F76] = "%s: no response for %llu ms, connection lost",  /* SID:444 */
    [LA_F77] = "%s: old request ignored (sid=%u <= last_sid=%u)",  /* SID:168 */
    [LA_F78] = "%s: peer disconnected (ses_id=%llu), reset to REGISTERED",  /* SID:445 */
    [LA_F79] = "%s: peer reachable via signaling (RTT: %llu ms)",  /* SID:446 */
    [LA_F80] = "%s: punching %d/%d candidates (elapsed: %llu ms)",  /* SID:447 */
    [LA_F81] = "%s: punching additional cand<%s:%d>[%d] while connected",  /* SID:172 */
    [LA_F82] = "%s: punching remote cand<%s:%d>[%d]",  /* SID:173 */
    [LA_F83] = "%s: received FIN from peer, marking NAT as CLOSED",  /* SID:174 */
    [LA_F84] = "%s: remote candidate[%d] %s:%d, starting punch",  /* SID:175 */
    [LA_F85] = "%s: restarting periodic check",  /* SID:176 */
    [LA_F86] = "%s: retry(%d/%d) probe",  /* SID:177 */
    [LA_F87] = "%s: retry(%d/%d) req (sid=%u)",  /* SID:178 */
    [LA_F88] = "%s: retry(%d/%d) resp (sid=%u)",  /* SID:179 */
    [LA_F89] = "%s: retry, (attempt %d/%d)",  /* SID:180 */
    [LA_F90] = "%s: rx confirmed: peer->me path is UP (%s:%d)",  /* SID:181 */
    [LA_F91] = "%s: send failed(%d)",  /* SID:182 */
    [LA_F92] = "%s: sent MSG(msg=0, sid=%u)",  /* SID:183 */
    [LA_F93] = "%s: sent, sid=%u, code=%u, size=%d",  /* SID:184 */
    [LA_F94] = "%s: session mismatch(local=%llu pkt=%llu)",  /* SID:448 */
    [LA_F95] = "%s: session mismatch(local=%llu, pkt=%llu)",  /* SID:449 */
    [LA_F96] = "%s: skip and mark NAT as OPEN (lan_punch enabled)",  /* SID:187 */
    [LA_F97] = "%s: start punching all(%d) remote candidates",  /* SID:188 */
    [LA_F98] = "%s: started, sending first probe",  /* SID:189 */
    [LA_F99] = "%s: status error(%d)",  /* SID:190 */
    [LA_F100] = "%s: sync complete (ses_id=%llu)",  /* SID:450 */
    [LA_F101] = "%s: sync complete (ses_id=%llu, mask=0x%04x)",  /* SID:451 */
    [LA_F102] = "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)",  /* SID:193 */
    [LA_F103] = "%s: timeout after %d retries , type unknown",  /* SID:194 */
    [LA_F104] = "%s: timeout after %llu ms (ICE done), switching to RELAY",  /* SID:452 */
    [LA_F105] = "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates",  /* SID:453 */
    [LA_F106] = "%s: timeout, max(%d) attempts reached, reset to INIT",  /* SID:197 */
    [LA_F107] = "%s: timeout, peer did not respond",  /* SID:198 */
    [LA_F108] = "%s: timeout, retry %d/%d",  /* SID:199 */
    [LA_F109] = "%s: triggered via COMPACT msg echo",  /* SID:200 */
    [LA_F110] = "%s: triggered via RELAY TUNE echo",  /* SID:201 */
    [LA_F111] = "%s: tx confirmed: me->peer path is UP (echoed seq=%u)",  /* SID:202 */
    [LA_F112] = "%s: unexpected ack_seq=%u mask=0x%04x",  /* SID:203 */
    [LA_F113] = "%s:%04d: %s",  /* SID:204 */
    [LA_F114] = "%s_ACK sent, seq=%u (ses_id=%llu)",  /* SID:454 */
    [LA_F115] = "%s_ACK sent, sid=%u",  /* SID:206 */
    [LA_F116] = "Added RELAY path to path manager",  /* SID:207 */
    [LA_F117] = "Added Remote Candidate: %d -> %s:%d",  /* SID:208 */
    [LA_F118] = "Append Host candidate: %s:%d",  /* SID:209 */
    [LA_F119] = "Attempting Simultaneous Open to %s:%d",  /* SID:210 */
    [LA_F120] = "Auto-send answer (with %d candidates) total sent %s",  /* SID:211 */
    [LA_F121] = "Connect to COMPACT signaling server failed(%d)",  /* SID:212 */
    [LA_F122] = "Connect to RELAY signaling server failed(%d)",  /* SID:213 */
    [LA_F123] = "Connected to server %s:%d as '%s'",  /* SID:214 */
    [LA_F124] = "Connecting to RELAY signaling server at %s:%d",  /* SID:215 */
    [LA_F125] = "Connectivity checks timed out (sent %d rounds), giving up",  /* SID:216 */
    [LA_F126] = "Data stored in recv buffer seq=%u len=%d base=%u",  /* SID:217 */
    [LA_F127] = "Detect local network interfaces failed(%d)",  /* SID:218 */
    [LA_F128] = "Detection completed %s",  /* SID:219 */
    [LA_F129] = "Failed to allocate %u bytes",  /* SID:220 */
    [LA_F130] = "Failed to realloc memory for local candidates (capacity: %d)",  /* SID:221 */
    [LA_F131] = "Failed to realloc memory for remote candidates (capacity: %d)",  /* SID:222 */
    [LA_F132] = "Failed to reserve remote candidates (base=%u cnt=%d)",  /* SID:223 */
    [LA_F133] = "Failed to reserve remote candidates (cnt=%d)",  /* SID:224 */
    [LA_F134] = "Failed to resolve STUN server %s",  /* SID:225 */
    [LA_F135] = "Failed to resolve TURN server: %s",  /* SID:226 */
    [LA_F136] = "Formed check list with %d candidate pairs",  /* SID:227 */
    [LA_F137] = "Gathered Host Candidate: %s:%d (priority=0x%08x)",  /* SID:228 */
    [LA_F138] = "Gathered Relay Candidate %s:%u (priority=%u)",  /* SID:229 */
    [LA_F139] = "Handshake failed: %s (-0x%04x)",  /* SID:230 */
    [LA_F140] = "Initialize PUBSUB signaling context failed(%d)",  /* SID:231 */
    [LA_F141] = "Initialize signaling mode: %d",  /* SID:232 */
    [LA_F142] = "Initialized: %s",  /* SID:233 */
    [LA_F143] = "Invalid magic 0x%x (expected 0x%x), resetting",  /* SID:234 */
    [LA_F144] = "Invalid read state %d, resetting",  /* SID:235 */
    [LA_F145] = "Local address detection done: %d address(es)",  /* SID:236 */
    [LA_F146] = "Marked old path (idx=%d) as FAILED due to addr change",  /* SID:237 */
    [LA_F147] = "Nomination successful! Using! Using %s path %s:%d%s",  /* SID:238 */
    [LA_F148] = "Open P2P UDP socket on port %d",  /* SID:239 */
    [LA_F149] = "Open P2P UDP socket on port %d failed(%d)",  /* SID:240 */
    [LA_F150] = "Packet queued seq=%u len=%d inflight=%d",  /* SID:241 */
    [LA_F151] = "Packet too large len=%d max=%d",  /* SID:242 */
    [LA_F152] = "Passive peer learned remote ID '%s' from OFFER",  /* SID:243 */
    [LA_F153] = "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)",  /* SID:244 */
    [LA_F154] = "Path recovered: switched to PUNCH",  /* SID:245 */
    [LA_F155] = "Path switched to better route (idx=%d)",  /* SID:246 */
    [LA_F156] = "Peer '%s' is now online (FORWARD received), resuming",  /* SID:247 */
    [LA_F157] = "Peer offline, cached %d candidates",  /* SID:248 */
    [LA_F158] = "Peer online, forwarded %d candidates",  /* SID:249 */
    [LA_F159] = "Processing (role=%s)",  /* SID:250 */
    [LA_F160] = "REGISTERED: peer=%s",  /* SID:251 */
    [LA_F161] = "Received ACK (status=%d, candidates_acked=%d)",  /* SID:252 */
    [LA_F162] = "Received New Remote Candidate: %d -> %s:%d",  /* SID:253 */
    [LA_F163] = "Received UNKNOWN pkt type: 0x%02X",  /* SID:254 */
    [LA_F164] = "Received remote candidate: type=%d, address=%s:%d",  /* SID:255 */
    [LA_F165] = "Received valid signal from '%s'",  /* SID:256 */
    [LA_F166] = "Register to COMPACT signaling server at %s:%d",  /* SID:257 */
    [LA_F167] = "Requested Relay Candidate from %s",  /* SID:258 */
    [LA_F168] = "Requested Srflx Candidate from %s",  /* SID:259 */
    [LA_F169] = "Resolve COMPACT signaling server address: %s:%d failed(%d)",  /* SID:260 */
    [LA_F170] = "Selected path: PUNCH (idx=%d)",  /* SID:261 */
    [LA_F171] = "Send offer to RELAY signaling server failed(%d)",  /* SID:262 */
    [LA_F172] = "Send window full, dropping packet send_count=%d",  /* SID:263 */
    [LA_F173] = "Sending Allocate Request to %s:%d",  /* SID:264 */
    [LA_F174] = "Sending Test I to %s:%d (len=%d)",  /* SID:265 */
    [LA_F175] = "Sent answer to '%s'",  /* SID:266 */
    [LA_F176] = "Sent answer to '%s' (%d bytes)",  /* SID:267 */
    [LA_F177] = "Sent connect request to '%s' (%d bytes)",  /* SID:268 */
    [LA_F178] = "Sent initial offer(%d) to %s)",  /* SID:269 */
    [LA_F179] = "Start internal thread failed(%d)",  /* SID:270 */
    [LA_F180] = "Switched to backup path: RELAY",  /* SID:271 */
    [LA_F181] = "Synced path after failover",  /* SID:272 */
    [LA_F182] = "Test I: Mapped address: %s:%d",  /* SID:273 */
    [LA_F183] = "Test I: Timeout",  /* SID:274 */
    [LA_F184] = "Test II: Success! Detection completed %s",  /* SID:275 */
    [LA_F185] = "Test II: Timeout (need Test III)",  /* SID:276 */
    [LA_F186] = "Test III: Success! Detection completed %s",  /* SID:277 */
    [LA_F187] = "Test III: Timeout",  /* SID:278 */
    [LA_F188] = "UDP hole-punch probing remote candidates (%d candidates)",  /* SID:279 */
    [LA_F189] = "Unknown ACK status %d",  /* SID:280 */
    [LA_F190] = "Unknown signaling mode: %d",  /* SID:281 */
    [LA_F191] = "Updating Gist field '%s'...",  /* SID:282 */
    [LA_F192] = "Using path: RELAY",  /* SID:283 */
    [LA_F193] = "Waiting for peer '%s' timed out (%dms), giving up",  /* SID:284 */
    [LA_F194] = "[SCTP] received encapsulated packet, length %d",  /* SID:285 */
    [LA_F195] = "[SCTP] sending %d bytes",  /* SID:286 */
    [LA_F196] = "[SIGNALING] Failed to send candidates, will retry (ret=%d)",  /* SID:287 */
    [LA_F197] = "[SIGNALING] Sent candidates (cached, peer offline) %d to %s",  /* SID:288 */
    [LA_F198] = "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)",  /* SID:289 */
    [LA_F199] = "[Trickle] Immediately probing new candidate %s:%d",  /* SID:290 */
    [LA_F200] = "[Trickle] Sent 1 candidate to %s (online=%s)",  /* SID:291 */
    [LA_F201] = "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()",  /* SID:292 */
    [LA_F202] = "[lan_punch] starting NAT punch(Host candidate %d)",  /* SID:293 */
    [LA_F203] = "[prflx] Received New Remote Candidate %s:%d (Peer Reflexive - symmetric NAT)",  /* SID:294 */
    [LA_F204] = "congestion detected, new ssthresh: %u, cwnd: %u",  /* SID:295 */
    [LA_F205] = "recv error %d",  /* SID:296 */
    [LA_F206] = "recv error %d while discarding",  /* SID:297 */
    [LA_F207] = "recv error %d while reading payload",  /* SID:298 */
    [LA_F208] = "recv error %d while reading sender",  /* SID:299 */
    [LA_F209] = "retry seq=%u retx=%d rto=%d",  /* SID:300 */
    [LA_F210] = "ssl_setup failed: -0x%x",  /* SID:301 */
    [LA_F211] = "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)",  /* SID:302 */
    [LA_F212] = "✗ Cannot add Srflx candidate: realloc failed (OOM)",  /* SID:303 */
};
