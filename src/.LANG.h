/*
 * Auto-generated language IDs
 * 
 * DO NOT EDIT - Regenerate with: ./i18n/i18n.sh
 */

#ifndef LANG_H__
#define LANG_H__

#ifndef LA_PREDEFINED
#   define LA_PREDEFINED -1
#endif

enum {
    LA_PRED = LA_PREDEFINED,  /* 基础 ID，后续 ID 从此开始递增 */
    
    /* Words (LA_W) */
    LA_W0,  /* "alive"  [p2p_nat.c] */
    LA_W1,  /* "bytes"  [p2p_signal_relay.c] */
    LA_W2,  /* "Detecting..."  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W3,  /* "Full Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W4,  /* "NAT punch failed, no TURN server configured"  [p2p.c] */
    LA_W5,  /* "NAT punch failed, server has no relay support"  [p2p.c] */
    LA_W6,  /* "NAT punch failed, using COMPACT server relay"  [p2p.c] */
    LA_W7,  /* "no (cached)"  [p2p_ice.c] */
    LA_W8,  /* "Open Internet (No NAT)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W9,  /* "Port Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W10,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W11,  /* "Published"  [p2p_signal_pubsub.c] */
    LA_W12,  /* "punch"  [p2p_nat.c] */
    LA_W13,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W14,  /* "Resent"  [p2p_signal_pubsub.c] */
    LA_W15,  /* "Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W16,  /* "retry"  [p2p_nat.c] */
    LA_W17,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W18,  /* "Symmetric NAT (port-random)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W19,  /* "Timeout (no response)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W20,  /* "UDP Blocked (STUN unreachable)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W21,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W22,  /* "Unsupported (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W23,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W24,  /* "yes"  [p2p_ice.c] */

    /* Strings (LA_S) */
    LA_S0,  /* "%s: address exchange failed: peer OFFLINE"  [p2p_probe.c] */
    LA_S1,  /* "%s: address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S2,  /* "%s: already running, cannot trigger again"  [p2p_probe.c] */
    LA_S3,  /* "%s: peer is OFFLINE"  [p2p_probe.c] */
    LA_S4,  /* "%s: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S5,  /* "%s: triggered on CONNECTED state (unnecessary)"  [p2p_probe.c] */
    LA_S6,  /* "%s: TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S7,  /* "[OpenSSL] DTLS handshake completed"  [p2p_trans_openssl.c] */
    LA_S8,  /* "[SCTP] usrsctp wrapper initialized (skeleton)"  [p2p_trans_sctp.c] */
    LA_S9,  /* "[SIGNALING] Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S10,  /* "[Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_S11,  /* "Added RELAY path to path manager"  [p2p.c] */
    LA_S12,  /* "Allocation successful!"  [p2p_turn.c] */
    LA_S13,  /* "Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_S14,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_S15,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_S16,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S17,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_S18,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_S19,  /* "COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_S20,  /* "Connection closed by server"  [p2p_signal_relay.c] */
    LA_S21,  /* "Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_S22,  /* "Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_S23,  /* "Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_S24,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S25,  /* "DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_S26,  /* "Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_S27,  /* "Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_S28,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_S29,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_S30,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_S31,  /* "Failed to push remote candidate"  [p2p_signal_compact.c] */
    LA_S32,  /* "Failed to reserve remote candidates (cnt=1)"  [p2p_signal_compact.c] */
    LA_S33,  /* "Failed to send header"  [p2p_signal_relay.c] */
    LA_S34,  /* "Failed to send payload"  [p2p_signal_relay.c] */
    LA_S35,  /* "Failed to send punch packet for new peer addr"  [p2p_signal_compact.c] */
    LA_S36,  /* "Failed to send target name"  [p2p_signal_relay.c] */
    LA_S37,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S38,  /* "Handshake complete"  [p2p_trans_mbedtls.c] */
    LA_S39,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S40,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_S41,  /* "NAT connection recovered, upgrading from RELAY to CONNECTED"  [p2p.c] */
    LA_S42,  /* "NAT connection timeout, downgrading to relay mode"  [p2p.c] */
    LA_S43,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_S44,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_S45,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_S46,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S47,  /* "P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_S48,  /* "P2P connection established"  [p2p.c] */
    LA_S49,  /* "P2P punch failed, adding relay path"  [p2p.c] */
    LA_S50,  /* "P2P punching in progress ..."  [p2p.c] */
    LA_S51,  /* "p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_S52,  /* "Path switch debounced, waiting for stability"  [p2p.c] */
    LA_S53,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_S54,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_S55,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_S56,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_S57,  /* "Received FIN packet, connection closed"  [p2p.c] */
    LA_S58,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_S59,  /* "SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_S60,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    LA_S61,  /* "Sending FIN packet to peer on destroy"  [p2p.c] */
    LA_S62,  /* "Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_S63,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_S64,  /* "Skipping local Host candidates on --public-only"  [p2p.c] */
    LA_S65,  /* "Starting internal thread"  [p2p.c] */
    LA_S66,  /* "Stopping internal thread"  [p2p.c] */
    LA_S67,  /* "Storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S68,  /* "Switched to backup path: RELAY"  [p2p.c] */
    LA_S69,  /* "Using path: RELAY"  [p2p.c] */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "  ... and %d more pairs" (%d)  [p2p_ice.c] */
    LA_F1,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F2,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F3,  /* "  [%d]<%s:%d> (type: %s)" (%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F4,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F5,  /* "%s NOTIFY: accepted" (%s)  [p2p_signal_compact.c] */
    LA_F6,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F7,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F8,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F9,  /* "%s received (ice_state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F10,  /* "%s resent, %d/%d" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F11,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F12,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F13,  /* "%s sent to %s:%d, echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F14,  /* "%s sent, inst_id=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F15,  /* "%s sent, inst_id=%u, cands=%d" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F16,  /* "%s sent, seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F17,  /* "%s sent, sid=%u, msg=%u, size=%d" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F18,  /* "%s sent, size=%d (ses_id=%llu)" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F19,  /* "%s sent, total=%d (ses_id=%llu)" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F20,  /* "%s seq=0: accepted cand_cnt=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F21,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F22,  /* "%s, retry remaining candidates and FIN to peer" (%s)  [p2p_signal_compact.c] */
    LA_F23,  /* "%s, sent on %s" (%s,%s)  [p2p_signal_compact.c] */
    LA_F24,  /* "%s: %s timeout after %d retries (sid=%u)" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F25,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F26,  /* "%s: Peer addr changed -> %s:%d, retrying punch" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F27,  /* "%s: RPC fail due to peer offline (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F28,  /* "%s: RPC fail due to relay timeout (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F29,  /* "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F30,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F31,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F32,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F33,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F34,  /* "%s: accepted" (%s)  [p2p_signal_compact.c, p2p_nat.c] */
    LA_F35,  /* "%s: accepted (ses_id=%llu)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F36,  /* "%s: accepted for ack_seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F37,  /* "%s: accepted from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F38,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F39,  /* "%s: accepted sid=%u, msg=%u" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F40,  /* "%s: accepted, RPC complete (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F41,  /* "%s: accepted, RPC finished (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F42,  /* "%s: accepted, len=%d (ses_id=%llu)" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F43,  /* "%s: accepted, probe_mapped=%s:%d" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F44,  /* "%s: accepted, public=%s:%d max_cands=%d probe_port=%d relay=%s msg=%s" (%s,%s,%d,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F45,  /* "%s: accepted, waiting for response (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F46,  /* "%s: already connected, ignoring batch punch request" (%s)  [p2p_nat.c] */
    LA_F47,  /* "%s: bad payload(len=%d cand_cnt=%d)" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F48,  /* "%s: bad payload(len=%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F49,  /* "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F50,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F51,  /* "%s: discovered unsynced prflx cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F52,  /* "%s: duplicate request ignored (sid=%u, already processing)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F53,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F54,  /* "%s: entered, %s arrived after PEER_INFO" (%s,%s)  [p2p_signal_compact.c] */
    LA_F55,  /* "%s: entered, %s arrived after REGISTERED" (%s,%s)  [p2p_signal_compact.c] */
    LA_F56,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F57,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F58,  /* "%s: failed to RE-REGISTER after timeout" (%s)  [p2p_signal_compact.c] */
    LA_F59,  /* "%s: failed to send UNREGISTER before restart" (%s)  [p2p_signal_compact.c] */
    LA_F60,  /* "%s: failed to track cand<%s:%d>, dropping" (%s,%s,%d)  [p2p_nat.c] */
    LA_F61,  /* "%s: ignored (relay not supported)" (%s)  [p2p_signal_compact.c] */
    LA_F62,  /* "%s: ignored for duplicated seq=%u, already acked" (%s,%u)  [p2p_signal_compact.c] */
    LA_F63,  /* "%s: ignored for seq=%u (expect=%d)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F64,  /* "%s: ignored for ses_id=%llu (local ses_id=%llu)" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F65,  /* "%s: ignored for sid=%u (current sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F66,  /* "%s: ignored in invalid state=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F67,  /* "%s: ignored in state(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F68,  /* "%s: ignored in state=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F69,  /* "%s: invalid ack_seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F70,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F71,  /* "%s: invalid for non-relay req" (%s)  [p2p_signal_compact.c] */
    LA_F72,  /* "%s: invalid in non-COMPACT mode" (%s)  [p2p_signal_compact.c] */
    LA_F73,  /* "%s: invalid seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F74,  /* "%s: keep alive to %d reachable cand(s)" (%s,%d)  [p2p_nat.c] */
    LA_F75,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F76,  /* "%s: no remote candidates to punch" (%s)  [p2p_nat.c] */
    LA_F77,  /* "%s: no response for %llu ms, connection lost" (%s,%l)  [p2p_nat.c] */
    LA_F78,  /* "%s: old request ignored (sid=%u <= last_sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F79,  /* "%s: peer disconnected (ses_id=%llu), reset to REGISTERED" (%s,%l)  [p2p_signal_compact.c] */
    LA_F80,  /* "%s: peer reachable via signaling (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F81,  /* "%s: punching %d/%d candidates (elapsed: %llu ms)" (%s,%d,%d,%l)  [p2p_nat.c] */
    LA_F82,  /* "%s: punching additional cand<%s:%d>[%d] while connected" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F83,  /* "%s: punching remote cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F84,  /* "%s: received FIN from peer, marking NAT as CLOSED" (%s)  [p2p_nat.c] */
    LA_F85,  /* "%s: remote candidate[%d] %s:%d, starting punch" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F86,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F87,  /* "%s: retry(%d/%d) probe" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F88,  /* "%s: retry(%d/%d) req (sid=%u)" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F89,  /* "%s: retry(%d/%d) resp (sid=%u)" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F90,  /* "%s: retry, (attempt %d/%d)" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F91,  /* "%s: rx confirmed: peer->me path is UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F92,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F93,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F94,  /* "%s: sent, sid=%u, code=%u, size=%d" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F95,  /* "%s: session mismatch(local=%llu pkt=%llu)" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F96,  /* "%s: session mismatch(local=%llu, pkt=%llu)" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F97,  /* "%s: skip and mark NAT as OPEN (lan_punch enabled)" (%s)  [p2p_signal_compact.c] */
    LA_F98,  /* "%s: start punching all(%d) remote candidates" (%s,%d)  [p2p_nat.c] */
    LA_F99,  /* "%s: started, sending first probe" (%s)  [p2p_signal_compact.c] */
    LA_F100,  /* "%s: status error(%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F101,  /* "%s: sync complete (ses_id=%llu)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F102,  /* "%s: sync complete (ses_id=%llu, mask=0x%04x)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F103,  /* "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F104,  /* "%s: timeout after %d retries , type unknown" (%s,%d)  [p2p_signal_compact.c] */
    LA_F105,  /* "%s: timeout after %llu ms (ICE done), switching to RELAY" (%s,%l)  [p2p_nat.c] */
    LA_F106,  /* "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%s,%l,%d)  [p2p_nat.c] */
    LA_F107,  /* "%s: timeout, max(%d) attempts reached, reset to INIT" (%s,%d)  [p2p_signal_compact.c] */
    LA_F108,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F109,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F110,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F111,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F112,  /* "%s: tx confirmed: me->peer path is UP (echoed seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F113,  /* "%s: unexpected ack_seq=%u mask=0x%04x" (%s,%u)  [p2p_signal_compact.c] */
    LA_F114,  /* "%s:%04d: %s" (%s,%s)  [p2p_trans_mbedtls.c] */
    LA_F115,  /* "%s_ACK sent, seq=%u (ses_id=%llu)" (%s,%u,%l)  [p2p_signal_compact.c] */
    LA_F116,  /* "%s_ACK sent, sid=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F117,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F118,  /* "Added Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F119,  /* "Append Host candidate: %s:%d" (%s,%d)  [p2p.c] */
    LA_F120,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F121,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F122,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F123,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F124,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F125,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F126,  /* "Connected to server %s:%d as '%s'" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F127,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F128,  /* "Connectivity checks timed out (sent %d rounds), giving up" (%d)  [p2p_ice.c] */
    LA_F129,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F130,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F131,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F132,  /* "Discarded %d bytes payload of message type %d" (%d,%d)  [p2p_signal_relay.c] */
    LA_F133,  /* "Failed to allocate %u bytes" (%u)  [p2p_signal_relay.c] */
    LA_F134,  /* "Failed to realloc memory for local candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_F135,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_F136,  /* "Failed to reserve remote candidates (base=%u cnt=%d)" (%u,%d)  [p2p_signal_compact.c] */
    LA_F137,  /* "Failed to reserve remote candidates (cnt=%d)" (%d)  [p2p_signal_compact.c] */
    LA_F138,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F139,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F140,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F141,  /* "First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F142,  /* "Formed check list with %d candidate pairs" (%d)  [p2p_ice.c] */
    LA_F143,  /* "Gathered Host Candidate: %s:%d (priority=0x%08x)" (%s,%d)  [p2p_ice.c] */
    LA_F144,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F145,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_trans_mbedtls.c] */
    LA_F146,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F147,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F148,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F149,  /* "Invalid magic 0x%x (expected 0x%x), resetting" (%x,%x)  [p2p_signal_relay.c] */
    LA_F150,  /* "Invalid read state %d, resetting" (%d)  [p2p_signal_relay.c] */
    LA_F151,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F152,  /* "Marked old path (idx=%d) as FAILED due to addr change" (%d)  [p2p_signal_compact.c] */
    LA_F153,  /* "Nomination successful! Using! Using %s path %s:%d%s" (%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F154,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F155,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F156,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F157,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F158,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F159,  /* "Passive peer learned remote ID '%s' from OFFER" (%s)  [p2p_signal_relay.c] */
    LA_F160,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F161,  /* "Path recovered: switched to PUNCH"  [p2p.c] */
    LA_F162,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F163,  /* "Peer '%s' is now online (FORWARD received), resuming" (%s)  [p2p_signal_relay.c] */
    LA_F164,  /* "Peer offline, cached %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F165,  /* "Peer online, forwarded %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F166,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F167,  /* "REGISTERED: peer=%s" (%s)  [p2p_signal_compact.c] */
    LA_F168,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F169,  /* "Received ACK (status=%d, candidates_acked=%d)" (%d,%d)  [p2p_signal_relay.c] */
    LA_F170,  /* "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%d,%u)  [p2p.c] */
    LA_F171,  /* "Received DATA pkt from %s:%d, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F172,  /* "Received New Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_ice.c] */
    LA_F173,  /* "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F174,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F175,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F176,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F177,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F178,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_signal_compact.c, p2p_nat.c] */
    LA_F179,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F180,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F181,  /* "Recv %s pkt from %s:%d, flags=0x%02x, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F182,  /* "Recv %s pkt from %s:%d, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F183,  /* "Recv %s pkt from %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F184,  /* "Recv %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F185,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F186,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F187,  /* "Requested Relay Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F188,  /* "Requested Srflx Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F189,  /* "Resend %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F190,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F191,  /* "Selected path: PUNCH (idx=%d)" (%d)  [p2p.c] */
    LA_F192,  /* "Send %s pkt to %s:%d, seq=%u, flags=0, len=0" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F193,  /* "Send %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F194,  /* "Send %s pkt to %s:%d, seq=0, flags=0, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F195,  /* "Send %s_ACK pkt to %s:%d, seq=%u, flags=0, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F196,  /* "Send %s_ACK pkt to %s:%d, seq=0, flags=0, len=2" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F197,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F198,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F199,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F200,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F201,  /* "Sent answer to '%s'" (%s)  [p2p_ice.c] */
    LA_F202,  /* "Sent answer to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F203,  /* "Sent connect request to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F204,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F205,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F206,  /* "Synced path after failover"  [p2p.c] */
    LA_F207,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F208,  /* "Test I: Timeout"  [p2p_stun.c] */
    LA_F209,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F210,  /* "Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F211,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F212,  /* "Test III: Timeout"  [p2p_stun.c] */
    LA_F213,  /* "UDP hole-punch probing remote candidates (%d candidates)" (%d)  [p2p_ice.c] */
    LA_F214,  /* "UDP hole-punch probing remote candidates round %d/%d" (%d,%d)  [p2p_ice.c] */
    LA_F215,  /* "Unknown ACK status %d" (%d)  [p2p_signal_relay.c] */
    LA_F216,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F217,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F218,  /* "Waiting for peer '%s' timed out (%dms), giving up" (%s,%d)  [p2p_signal_relay.c] */
    LA_F219,  /* "[SCTP] received encapsulated packet, length %d" (%d)  [p2p_trans_sctp.c] */
    LA_F220,  /* "[SCTP] sending %d bytes" (%d)  [p2p_trans_sctp.c] */
    LA_F221,  /* "[SIGNALING] Failed to send candidates, will retry (ret=%d)" (%d)  [p2p_signal_relay.c] */
    LA_F222,  /* "[SIGNALING] Sent candidates (cached, peer offline) %d to %s" (%d,%s)  [p2p_signal_relay.c] */
    LA_F223,  /* "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)" (%d,%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F224,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F225,  /* "[Trickle] Sent 1 candidate to %s (online=%s)" (%s,%s)  [p2p_ice.c] */
    LA_F226,  /* "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()" (%d)  [p2p_ice.c] */
    LA_F227,  /* "[lan_punch] starting NAT punch(Host candidate %d)" (%d)  [p2p_ice.c] */
    LA_F228,  /* "[prflx] Received New Remote Candidate %s:%d (Peer Reflexive - symmetric NAT)" (%s,%d)  [p2p_ice.c] */
    LA_F229,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F230,  /* "recv error %d" (%d)  [p2p_signal_relay.c] */
    LA_F231,  /* "recv error %d while discarding" (%d)  [p2p_signal_relay.c] */
    LA_F232,  /* "recv error %d while reading payload" (%d)  [p2p_signal_relay.c] */
    LA_F233,  /* "recv error %d while reading sender" (%d)  [p2p_signal_relay.c] */
    LA_F234,  /* "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F235,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F236,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F237,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_trans_mbedtls.c] */
    LA_F238,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F239,  /* "✗ Cannot add Srflx candidate: realloc failed (OOM)"  [p2p_stun.c] */

    LA_NUM = 335
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
