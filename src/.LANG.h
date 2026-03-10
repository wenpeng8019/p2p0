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

    LA_W1,  /* "alive"  [p2p_nat.c] */
    LA_W2,  /* "bytes"  [p2p_signal_relay.c] */
    LA_W3,  /* "Detecting..."  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W4,  /* "Full Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W5,  /* "NAT punch failed, no TURN server configured"  [p2p.c] */
    LA_W6,  /* "NAT punch failed, server has no relay support"  [p2p.c] */
    LA_W7,  /* "NAT punch failed, using COMPACT server relay"  [p2p.c] */
    LA_W8,  /* "no (cached)"  [p2p_ice.c] */
    LA_W9,  /* "Open Internet (No NAT)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W10,  /* "Port Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W11,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W12,  /* "Published"  [p2p_signal_pubsub.c] */
    LA_W13,  /* "punch"  [p2p_nat.c] */
    LA_W14,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W15,  /* "Resent"  [p2p_signal_pubsub.c] */
    LA_W16,  /* "Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W17,  /* "retry"  [p2p_nat.c] */
    LA_W18,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W19,  /* "Symmetric NAT (port-random)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W20,  /* "Timeout (no response)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W21,  /* "UDP Blocked (STUN unreachable)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W22,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W23,  /* "Unsupported (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_W24,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W25,  /* "yes"  [p2p_ice.c] */
    LA_S26,  /* "%s: address exchange failed: peer OFFLINE"  [p2p_probe.c] */
    LA_S27,  /* "%s: address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S28,  /* "%s: already running, cannot trigger again"  [p2p_probe.c] */
    LA_S29,  /* "%s: peer is OFFLINE"  [p2p_probe.c] */
    LA_S30,  /* "%s: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S31,  /* "%s: triggered on CONNECTED state (unnecessary)"  [p2p_probe.c] */
    LA_S32,  /* "%s: TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S33,  /* "[OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    _LA_34,
    LA_S35,  /* "[SIGNALING] Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S36,  /* "[Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_S37,  /* "Added SIGNALING path to path manager"  [p2p.c] */
    LA_S38,  /* "Allocation successful!"  [p2p_turn.c] */
    LA_S39,  /* "Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_S40,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_S41,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_S42,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S43,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_S44,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_S45,  /* "COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_S46,  /* "Connection closed by server"  [p2p_signal_relay.c] */
    LA_S47,  /* "Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_S48,  /* "Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_S49,  /* "Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_S50,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S51,  /* "DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_S52,  /* "Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_S53,  /* "Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_S54,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_S55,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_S56,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_S57,  /* "Failed to push remote candidate"  [p2p_signal_compact.c] */
    LA_S58,  /* "Failed to reserve remote candidates (cnt=1)"  [p2p_signal_compact.c] */
    LA_S59,  /* "Failed to send header"  [p2p_signal_relay.c] */
    LA_S60,  /* "Failed to send payload"  [p2p_signal_relay.c] */
    LA_S61,  /* "Failed to send punch packet for new peer addr"  [p2p_signal_compact.c] */
    LA_S62,  /* "Failed to send target name"  [p2p_signal_relay.c] */
    LA_S63,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S64,  /* "Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_S65,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S66,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_S67,  /* "NAT connection recovered, upgrading from RELAY to CONNECTED"  [p2p.c] */
    LA_S68,  /* "NAT connection timeout, downgrading to relay mode"  [p2p.c] */
    LA_S69,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_S70,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_S71,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_S72,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S73,  /* "P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_S74,  /* "P2P connection established"  [p2p.c] */
    LA_S75,  /* "P2P punch failed, adding relay path"  [p2p.c] */
    LA_S76,  /* "P2P punching in progress ..."  [p2p.c] */
    LA_S77,  /* "p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_S78,  /* "Path switch debounced, waiting for stability"  [p2p.c] */
    LA_S79,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_S80,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_S81,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_S82,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_S83,  /* "Received FIN packet, connection closed"  [p2p.c] */
    LA_S84,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    _LA_85,
    LA_S86,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    _LA_87,
    LA_S88,  /* "Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_S89,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_S90,  /* "Skipping local Host candidates on --public-only"  [p2p.c] */
    LA_S91,  /* "Starting internal thread"  [p2p.c] */
    LA_S92,  /* "Stopping internal thread"  [p2p.c] */
    LA_S93,  /* "Storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S94,  /* "Switched to backup path: RELAY"  [p2p.c] */
    LA_S95,  /* "Using path: RELAY"  [p2p.c] */
    LA_F96,  /* "  ... and %d more pairs" (%d)  [p2p_ice.c] */
    LA_F97,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F98,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F99,  /* "  [%d]<%s:%d> (type: %s)" (%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F100,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F101,  /* "%s NOTIFY: accepted" (%s)  [p2p_signal_compact.c] */
    LA_F102,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F103,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F104,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F105,  /* "%s received (ice_state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F106,  /* "%s resent, %d/%d" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F107,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F108,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F109,  /* "%s sent to %s:%d, echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F110,  /* "%s sent, inst_id=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F111,  /* "%s sent, inst_id=%u, cands=%d" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F112,  /* "%s sent, seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F113,  /* "%s sent, sid=%u, msg=%u, size=%d" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    _LA_114,
    _LA_115,
    LA_F116,  /* "%s seq=0: accepted cand_cnt=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F117,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F118,  /* "%s, retry remaining candidates and FIN to peer" (%s)  [p2p_signal_compact.c] */
    LA_F119,  /* "%s, sent on %s" (%s,%s)  [p2p_signal_compact.c] */
    LA_F120,  /* "%s: %s timeout after %d retries (sid=%u)" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F121,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F122,  /* "%s: Peer addr changed -> %s:%d, retrying punch" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F123,  /* "%s: RPC fail due to peer offline (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F124,  /* "%s: RPC fail due to relay timeout (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    _LA_125,
    LA_F126,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F127,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F128,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F129,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F130,  /* "%s: accepted" (%s)  [p2p_signal_compact.c, p2p_nat.c] */
    _LA_131,
    LA_F132,  /* "%s: accepted for ack_seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F133,  /* "%s: accepted from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F134,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F135,  /* "%s: accepted sid=%u, msg=%u" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F136,  /* "%s: accepted, RPC complete (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F137,  /* "%s: accepted, RPC finished (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    _LA_138,
    LA_F139,  /* "%s: accepted, probe_mapped=%s:%d" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F140,  /* "%s: accepted, public=%s:%d max_cands=%d probe_port=%d relay=%s msg=%s" (%s,%s,%d,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F141,  /* "%s: accepted, waiting for response (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F142,  /* "%s: already connected, ignoring batch punch request" (%s)  [p2p_nat.c] */
    LA_F143,  /* "%s: bad payload(len=%d cand_cnt=%d)" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F144,  /* "%s: bad payload(len=%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F145,  /* "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F146,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F147,  /* "%s: discovered unsynced prflx cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F148,  /* "%s: duplicate request ignored (sid=%u, already processing)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F149,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F150,  /* "%s: entered, %s arrived after PEER_INFO" (%s,%s)  [p2p_signal_compact.c] */
    LA_F151,  /* "%s: entered, %s arrived after REGISTERED" (%s,%s)  [p2p_signal_compact.c] */
    LA_F152,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F153,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F154,  /* "%s: failed to RE-REGISTER after timeout" (%s)  [p2p_signal_compact.c] */
    LA_F155,  /* "%s: failed to send UNREGISTER before restart" (%s)  [p2p_signal_compact.c] */
    LA_F156,  /* "%s: failed to track cand<%s:%d>, dropping" (%s,%s,%d)  [p2p_nat.c] */
    LA_F157,  /* "%s: ignored (relay not supported)" (%s)  [p2p_signal_compact.c] */
    LA_F158,  /* "%s: ignored for duplicated seq=%u, already acked" (%s,%u)  [p2p_signal_compact.c] */
    LA_F159,  /* "%s: ignored for seq=%u (expect=%d)" (%s,%u,%d)  [p2p_signal_compact.c] */
    _LA_160,
    LA_F161,  /* "%s: ignored for sid=%u (current sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F162,  /* "%s: ignored in invalid state=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F163,  /* "%s: ignored in state(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F164,  /* "%s: ignored in state=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F165,  /* "%s: invalid ack_seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F166,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F167,  /* "%s: invalid for non-relay req" (%s)  [p2p_signal_compact.c] */
    LA_F168,  /* "%s: invalid in non-COMPACT mode" (%s)  [p2p_signal_compact.c] */
    LA_F169,  /* "%s: invalid seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F170,  /* "%s: keep alive to %d reachable cand(s)" (%s,%d)  [p2p_nat.c] */
    LA_F171,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F172,  /* "%s: no remote candidates to punch" (%s)  [p2p_nat.c] */
    _LA_173,
    LA_F174,  /* "%s: old request ignored (sid=%u <= last_sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    _LA_175,
    _LA_176,
    _LA_177,
    LA_F178,  /* "%s: punching additional cand<%s:%d>[%d] while connected" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F179,  /* "%s: punching remote cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F180,  /* "%s: received FIN from peer, marking NAT as CLOSED" (%s)  [p2p_nat.c] */
    LA_F181,  /* "%s: remote candidate[%d] %s:%d, starting punch" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F182,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F183,  /* "%s: retry(%d/%d) probe" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F184,  /* "%s: retry(%d/%d) req (sid=%u)" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F185,  /* "%s: retry(%d/%d) resp (sid=%u)" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F186,  /* "%s: retry, (attempt %d/%d)" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F187,  /* "%s: rx confirmed: peer->me path is UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F188,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F189,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F190,  /* "%s: sent, sid=%u, code=%u, size=%d" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    _LA_191,
    _LA_192,
    LA_F193,  /* "%s: skip and mark NAT as OPEN (lan_punch enabled)" (%s)  [p2p_signal_compact.c] */
    LA_F194,  /* "%s: start punching all(%d) remote candidates" (%s,%d)  [p2p_nat.c] */
    LA_F195,  /* "%s: started, sending first probe" (%s)  [p2p_signal_compact.c] */
    LA_F196,  /* "%s: status error(%d)" (%s,%d)  [p2p_signal_compact.c] */
    _LA_197,
    _LA_198,
    LA_F199,  /* "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F200,  /* "%s: timeout after %d retries , type unknown" (%s,%d)  [p2p_signal_compact.c] */
    _LA_201,
    _LA_202,
    LA_F203,  /* "%s: timeout, max(%d) attempts reached, reset to INIT" (%s,%d)  [p2p_signal_compact.c] */
    LA_F204,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F205,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F206,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F207,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F208,  /* "%s: tx confirmed: me->peer path is UP (echoed seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F209,  /* "%s: unexpected ack_seq=%u mask=0x%04x" (%s,%u)  [p2p_signal_compact.c] */
    LA_F210,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    _LA_211,
    LA_F212,  /* "%s_ACK sent, sid=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F213,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F214,  /* "Added Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F215,  /* "Append Host candidate: %s:%d" (%s,%d)  [p2p.c] */
    LA_F216,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F217,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F218,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F219,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F220,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F221,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F222,  /* "Connected to server %s:%d as '%s'" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F223,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F224,  /* "Connectivity checks timed out (sent %d rounds), giving up" (%d)  [p2p_ice.c] */
    LA_F225,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F226,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F227,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F228,  /* "Discarded %d bytes payload of message type %d" (%d,%d)  [p2p_signal_relay.c] */
    LA_F229,  /* "Failed to allocate %u bytes" (%u)  [p2p_signal_relay.c] */
    LA_F230,  /* "Failed to realloc memory for local candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_F231,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_dtls.c, p2p_stun.c, p2p_internal.h] */
    LA_F232,  /* "Failed to reserve remote candidates (base=%u cnt=%d)" (%u,%d)  [p2p_signal_compact.c] */
    LA_F233,  /* "Failed to reserve remote candidates (cnt=%d)" (%d)  [p2p_signal_compact.c] */
    LA_F234,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F235,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F236,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F237,  /* "First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F238,  /* "Formed check list with %d candidate pairs" (%d)  [p2p_ice.c] */
    LA_F239,  /* "Gathered Host Candidate: %s:%d (priority=0x%08x)" (%s,%d)  [p2p_ice.c] */
    LA_F240,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F241,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F242,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F243,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F244,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F245,  /* "Invalid magic 0x%x (expected 0x%x), resetting" (%x,%x)  [p2p_signal_relay.c] */
    LA_F246,  /* "Invalid read state %d, resetting" (%d)  [p2p_signal_relay.c] */
    LA_F247,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F248,  /* "Marked old path (idx=%d) as FAILED due to addr change" (%d)  [p2p_signal_compact.c] */
    LA_F249,  /* "Nomination successful! Using! Using %s path %s:%d%s" (%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F250,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F251,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F252,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F253,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F254,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F255,  /* "Passive peer learned remote ID '%s' from OFFER" (%s)  [p2p_signal_relay.c] */
    LA_F256,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F257,  /* "Path recovered: switched to PUNCH"  [p2p.c] */
    LA_F258,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F259,  /* "Peer '%s' is now online (FORWARD received), resuming" (%s)  [p2p_signal_relay.c] */
    LA_F260,  /* "Peer offline, cached %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F261,  /* "Peer online, forwarded %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F262,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F263,  /* "REGISTERED: peer=%s" (%s)  [p2p_signal_compact.c] */
    LA_F264,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F265,  /* "Received ACK (status=%d, candidates_acked=%d)" (%d,%d)  [p2p_signal_relay.c] */
    LA_F266,  /* "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%d,%u)  [p2p.c] */
    LA_F267,  /* "Received DATA pkt from %s:%d, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F268,  /* "Received New Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_ice.c] */
    LA_F269,  /* "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F270,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F271,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F272,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F273,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F274,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_signal_compact.c, p2p_nat.c] */
    LA_F275,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F276,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F277,  /* "Recv %s pkt from %s:%d, flags=0x%02x, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F278,  /* "Recv %s pkt from %s:%d, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F279,  /* "Recv %s pkt from %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F280,  /* "Recv %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F281,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F282,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F283,  /* "Requested Relay Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F284,  /* "Requested Srflx Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F285,  /* "Resend %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F286,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F287,  /* "Selected path: PUNCH (idx=%d)" (%d)  [p2p.c] */
    LA_F288,  /* "Send %s pkt to %s:%d, seq=%u, flags=0, len=0" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F289,  /* "Send %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F290,  /* "Send %s pkt to %s:%d, seq=0, flags=0, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F291,  /* "Send %s_ACK pkt to %s:%d, seq=%u, flags=0, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F292,  /* "Send %s_ACK pkt to %s:%d, seq=0, flags=0, len=2" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F293,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F294,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F295,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F296,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F297,  /* "Sent answer to '%s'" (%s)  [p2p_ice.c] */
    LA_F298,  /* "Sent answer to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F299,  /* "Sent connect request to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F300,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F301,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F302,  /* "Synced path after failover"  [p2p.c] */
    LA_F303,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F304,  /* "Test I: Timeout"  [p2p_stun.c] */
    LA_F305,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F306,  /* "Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F307,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F308,  /* "Test III: Timeout"  [p2p_stun.c] */
    LA_F309,  /* "UDP hole-punch probing remote candidates (%d candidates)" (%d)  [p2p_ice.c] */
    LA_F310,  /* "UDP hole-punch probing remote candidates round %d/%d" (%d,%d)  [p2p_ice.c] */
    LA_F311,  /* "Unknown ACK status %d" (%d)  [p2p_signal_relay.c] */
    LA_F312,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F313,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F314,  /* "Waiting for peer '%s' timed out (%dms), giving up" (%s,%d)  [p2p_signal_relay.c] */
    _LA_315,
    _LA_316,
    LA_F317,  /* "[SIGNALING] Failed to send candidates, will retry (ret=%d)" (%d)  [p2p_signal_relay.c] */
    LA_F318,  /* "[SIGNALING] Sent candidates (cached, peer offline) %d to %s" (%d,%s)  [p2p_signal_relay.c] */
    LA_F319,  /* "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)" (%d,%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F320,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F321,  /* "[Trickle] Sent 1 candidate to %s (online=%s)" (%s,%s)  [p2p_ice.c] */
    LA_F322,  /* "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()" (%d)  [p2p_ice.c] */
    LA_F323,  /* "[lan_punch] starting NAT punch(Host candidate %d)" (%d)  [p2p_ice.c] */
    LA_F324,  /* "[prflx] Received New Remote Candidate %s:%d (Peer Reflexive - symmetric NAT)" (%s,%d)  [p2p_ice.c] */
    LA_F325,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F326,  /* "recv error %d" (%d)  [p2p_signal_relay.c] */
    LA_F327,  /* "recv error %d while discarding" (%d)  [p2p_signal_relay.c] */
    LA_F328,  /* "recv error %d while reading payload" (%d)  [p2p_signal_relay.c] */
    LA_F329,  /* "recv error %d while reading sender" (%d)  [p2p_signal_relay.c] */
    LA_F330,  /* "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F331,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F332,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F333,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F334,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F335,  /* "✗ Cannot add Srflx candidate: realloc failed (OOM)"  [p2p_stun.c] */
    _LA_336,
    _LA_337,
    _LA_338,
    _LA_339,
    _LA_340,
    _LA_341,
    _LA_342,
    _LA_343,
    _LA_344,
    _LA_345,
    _LA_346,
    _LA_347,
    _LA_348,
    _LA_349,
    _LA_350,
    _LA_351,
    _LA_352,
    _LA_353,
    _LA_354,
    _LA_355,
    _LA_356,
    _LA_357,
    _LA_358,
    _LA_359,
    _LA_360,
    _LA_361,
    _LA_362,
    _LA_363,
    _LA_364,
    _LA_365,
    _LA_366,
    _LA_367,
    _LA_368,
    _LA_369,
    _LA_370,
    _LA_371,
    _LA_372,
    _LA_373,
    _LA_374,
    _LA_375,
    _LA_376,
    _LA_377,
    _LA_378,
    _LA_379,
    _LA_380,
    _LA_381,
    _LA_382,
    _LA_383,
    _LA_384,
    _LA_385,
    _LA_386,
    _LA_387,
    _LA_388,
    _LA_389,
    _LA_390,
    _LA_391,
    _LA_392,
    _LA_393,
    _LA_394,
    _LA_395,
    _LA_396,
    _LA_397,
    _LA_398,
    _LA_399,
    _LA_400,
    _LA_401,
    _LA_402,
    _LA_403,
    _LA_404,
    _LA_405,
    _LA_406,
    _LA_407,
    _LA_408,
    _LA_409,
    _LA_410,
    _LA_411,
    _LA_412,
    _LA_413,
    _LA_414,
    _LA_415,
    _LA_416,
    _LA_417,
    _LA_418,
    _LA_419,
    _LA_420,
    _LA_421,
    _LA_422,
    _LA_423,
    _LA_424,
    _LA_425,
    _LA_426,
    _LA_427,
    _LA_428,
    _LA_429,
    _LA_430,
    _LA_431,
    _LA_432,
    _LA_433,
    _LA_434,
    _LA_435,
    _LA_436,
    _LA_437,
    _LA_438,
    _LA_439,
    _LA_440,
    _LA_441,
    _LA_442,
    _LA_443,
    _LA_444,
    _LA_445,
    _LA_446,
    _LA_447,
    _LA_448,
    _LA_449,
    _LA_450,
    _LA_451,
    _LA_452,
    _LA_453,
    _LA_454,
    _LA_455,
    _LA_456,
    _LA_457,
    _LA_458,
    _LA_459,
    _LA_460,
    _LA_461,
    _LA_462,
    _LA_463,
    _LA_464,
    _LA_465,
    _LA_466,
    _LA_467,
    _LA_468,
    _LA_469,
    _LA_470,
    _LA_471,
    _LA_472,
    _LA_473,
    _LA_474,
    _LA_475,
    _LA_476,
    _LA_477,
    _LA_478,
    _LA_479,
    _LA_480,
    _LA_481,
    _LA_482,
    _LA_483,
    _LA_484,
    _LA_485,
    _LA_486,
    _LA_487,
    _LA_488,
    _LA_489,
    _LA_490,
    _LA_491,
    _LA_492,
    _LA_493,
    _LA_494,
    _LA_495,
    _LA_496,
    _LA_497,
    _LA_498,
    _LA_499,
    _LA_500,
    _LA_501,
    _LA_502,
    _LA_503,
    _LA_504,
    _LA_505,
    _LA_506,
    _LA_507,
    _LA_508,
    _LA_509,
    _LA_510,
    _LA_511,
    _LA_512,
    _LA_513,
    _LA_514,
    _LA_515,
    _LA_516,
    _LA_517,
    _LA_518,
    _LA_519,
    _LA_520,
    _LA_521,
    _LA_522,
    _LA_523,
    _LA_524,
    _LA_525,
    _LA_526,
    _LA_527,
    _LA_528,
    _LA_529,
    _LA_530,
    _LA_531,
    _LA_532,
    _LA_533,
    _LA_534,
    _LA_535,
    _LA_536,
    _LA_537,
    _LA_538,
    _LA_539,
    _LA_540,
    _LA_541,
    _LA_542,
    _LA_543,
    _LA_544,
    _LA_545,
    _LA_546,
    _LA_547,
    _LA_548,
    _LA_549,
    _LA_550,
    _LA_551,
    _LA_552,
    _LA_553,
    _LA_554,
    _LA_555,
    _LA_556,
    LA_S557,  /* "[SCTP] association established"  [p2p_trans_sctp.c] */
    LA_S558,  /* "[SCTP] usrsctp initialized, connecting..."  [p2p_trans_sctp.c] */
    LA_S559,  /* "[SCTP] usrsctp_socket failed"  [p2p_trans_sctp.c] */
    LA_S560,  /* "BIO_new failed"  [p2p_dtls_openssl.c] */
    LA_S561,  /* "Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_S562,  /* "Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_S563,  /* "SCTP (usrsctp) enabled as transport layer"  [p2p.c] */
    LA_S564,  /* "SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_S565,  /* "SSL_new failed"  [p2p_dtls_openssl.c] */
    _LA_566,
    _LA_567,
    _LA_568,
    _LA_569,
    _LA_570,
    _LA_571,
    _LA_572,
    _LA_573,
    _LA_574,
    _LA_575,
    _LA_576,
    _LA_577,
    _LA_578,
    _LA_579,
    _LA_580,
    _LA_581,
    _LA_582,
    LA_F583,  /* "Transport layer '%s' init failed, falling back to simple reliable" (%s)  [p2p.c] */
    LA_F584,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F585,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F586,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F587,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F588,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F589,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F590,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    _LA_591,
    _LA_592,
    _LA_593,
    _LA_594,
    _LA_595,
    _LA_596,
    _LA_597,
    _LA_598,
    _LA_599,
    _LA_600,
    _LA_601,
    _LA_602,
    _LA_603,
    _LA_604,
    _LA_605,
    _LA_606,
    _LA_607,
    LA_F608,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F609,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    _LA_610,
    _LA_611,
    _LA_612,
    _LA_613,
    _LA_614,
    _LA_615,
    _LA_616,
    _LA_617,
    _LA_618,
    _LA_619,
    _LA_620,
    _LA_621,
    _LA_622,
    _LA_623,
    _LA_624,
    LA_F625,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F626,  /* "PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u" (%d,%u)  [p2p_signal_compact.c] */
    LA_F627,  /* "PEER_INFO(trickle): seq overflow, cannot trickle more"  [p2p_signal_compact.c] */
    LA_F628,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F96

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
