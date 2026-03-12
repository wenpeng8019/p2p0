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
    LA_W1,  /* "alive"  [p2p_nat.c] */
    LA_W2,  /* "bytes"  [p2p_signal_relay.c] */
    LA_W3,  /* "Detecting..."  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W4,  /* "Full Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W5,  /* "NAT punch failed, no TURN server configured"  [p2p.c] */
    LA_W6,  /* "NAT punch failed, server has no relay support"  [p2p.c] */
    LA_W7,  /* "NAT punch failed, using COMPACT server relay"  [p2p.c] */
    LA_W8,  /* "no (cached)"  [p2p_ice.c] */
    LA_W9,  /* "Open Internet (No NAT)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W10,  /* "Port Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W11,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W12,  /* "Published"  [p2p_signal_pubsub.c] */
    LA_W13,  /* "punch"  [p2p_nat.c] */
    LA_W14,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W15,  /* "Resent"  [p2p_signal_pubsub.c] */
    LA_W16,  /* "Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W17,  /* "retry"  [p2p_nat.c] */
    LA_W18,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W19,  /* "Symmetric NAT (port-random)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W20,  /* "Timeout (no response)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W21,  /* "UDP Blocked (STUN unreachable)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W22,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W23,  /* "Unsupported (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W24,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W25,  /* "yes"  [p2p_ice.c] */

    /* Strings (LA_S) */
    LA_S26,  /* "%s: address exchange failed: peer OFFLINE"  [p2p_probe.c] */
    LA_S27,  /* "%s: address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S28,  /* "%s: already running, cannot trigger again"  [p2p_probe.c] */
    LA_S29,  /* "%s: peer is OFFLINE"  [p2p_probe.c] */
    LA_S30,  /* "%s: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S31,  /* "%s: triggered on CONNECTED state (unnecessary)"  [p2p_probe.c] */
    LA_S32,  /* "%s: TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S33,  /* "[OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    LA_S34,  /* "[SCTP] association established"  [p2p_trans_sctp.c] */
    LA_S35,  /* "[SCTP] usrsctp initialized, connecting..."  [p2p_trans_sctp.c] */
    LA_S36,  /* "[SCTP] usrsctp_socket failed"  [p2p_trans_sctp.c] */
    LA_S37,  /* "[SIGNALING] Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S38,  /* "[Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_S39,  /* "Added SIGNALING path to path manager"  [p2p.c] */
    LA_S40,  /* "Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_S41,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_S42,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_S43,  /* "BIO_new failed"  [p2p_dtls_openssl.c] */
    LA_S44,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S45,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_S46,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_S47,  /* "COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_S48,  /* "Connection closed by server"  [p2p_signal_relay.c] */
    LA_S49,  /* "Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_S50,  /* "Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_S51,  /* "Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_S52,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S53,  /* "DTLS (MbedTLS) enabled as encryption layer"  [p2p.c] */
    LA_S54,  /* "DTLS handshake complete (MbedTLS)"  [p2p_dtls_mbedtls.c] */
    LA_S55,  /* "Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_S56,  /* "Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_S57,  /* "Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_S58,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_S59,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_S60,  /* "Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_S61,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_S62,  /* "%s: unpack upsert remote cand<%s:%d> failed(OOM)\n"  [p2p_signal_pubsub.c, p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_S63,  /* "Failed to reserve remote candidates (cnt=1)\n"  [p2p_signal_compact.c] */
    LA_S64,  /* "Failed to send header"  [p2p_signal_relay.c] */
    LA_S65,  /* "Failed to send payload"  [p2p_signal_relay.c] */
    LA_S66,  /* "Failed to send punch packet for new peer addr\n"  [p2p_signal_compact.c] */
    LA_S67,  /* "Failed to send target name"  [p2p_signal_relay.c] */
    LA_S68,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S69,  /* "Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_S70,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S71,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_S72,  /* "NAT connection recovered, upgrading from RELAY to CONNECTED"  [p2p.c] */
    LA_S73,  /* "NAT connection timeout, downgrading to relay mode"  [p2p.c] */
    LA_S74,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_S75,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_S76,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_S77,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S78,  /* "P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_S79,  /* "P2P connection established"  [p2p.c] */
    LA_S80,  /* "P2P punch failed, adding relay path"  [p2p.c] */
    LA_S81,  /* "P2P punching in progress ..."  [p2p.c] */
    LA_S82,  /* "p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_S83,  /* "Path switch debounced, waiting for stability"  [p2p.c] */
    LA_S84,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_S85,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_S86,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_S87,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_S88,  /* "Received FIN packet, connection closed"  [p2p.c] */
    LA_S89,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_S90,  /* "SCTP (usrsctp) enabled as transport layer"  [p2p.c] */
    LA_S91,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    LA_S92,  /* "Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_S93,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_S94,  /* "Skipping local Host candidates on --public-only"  [p2p.c] */
    LA_S95,  /* "SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_S96,  /* "SSL_new failed"  [p2p_dtls_openssl.c] */
    LA_S97,  /* "Starting internal thread"  [p2p.c] */
    LA_S98,  /* "Stopping internal thread"  [p2p.c] */
    LA_S99,  /* "Storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S100,  /* "Switched to backup path: RELAY"  [p2p.c] */
    LA_S101,  /* "TURN auth required but no credentials configured"  [p2p_turn.c] */
    LA_S102,  /* "Using path: RELAY"  [p2p.c] */

    /* Formats (LA_F) */
    LA_F103,  /* "  ... and %d more pairs" (%d)  [p2p_ice.c] */
    LA_F104,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F105,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F106,  /* "  [%d]<%s:%d> (type: %s)" (%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F107,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F108,  /* "%s NOTIFY: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F109,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F110,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F111,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F112,  /* "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F113,  /* "%s resent, %d/%d\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F114,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F115,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F116,  /* "%s sent to %s:%d, echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F117,  /* "%s sent, inst_id=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F118,  /* "%s sent, inst_id=%u, cands=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F119,  /* "%s sent, seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F120,  /* "%s sent, sid=%u, msg=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F121,  /* "%s seq=0: accepted cand_cnt=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F122,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F123,  /* "%s, retry remaining candidates and FIN to peer\n" (%s)  [p2p_signal_compact.c] */
    LA_F124,  /* "%s, sent on %s\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F125,  /* "%s: %s timeout after %d retries (sid=%u)\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F126,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F127,  /* "%s: Peer addr changed -> %s:%d, retrying punch\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F128,  /* "%s: RPC fail due to peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F129,  /* "%s: RPC fail due to relay timeout (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F130,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F131,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F132,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F133,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F134,  /* "%s: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F135,  /* "%s: accepted for ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F136,  /* "%s: accepted from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F137,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F138,  /* "%s: accepted sid=%u, msg=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F139,  /* "%s: accepted (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F140,  /* "%s: RPC finished (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F141,  /* "%s: accepted, probe_mapped=%s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    _LA_142,
    LA_F143,  /* "%s: accepted, waiting for response (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F144,  /* "%s: already connected, ignoring batch punch request" (%s)  [p2p_nat.c] */
    LA_F145,  /* "%s: bad payload(len=%d cand_cnt=%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F146,  /* "%s: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F147,  /* "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F148,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F149,  /* "%s: discovered unsynced prflx cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F150,  /* "%s: duplicate request ignored (sid=%u, already processing)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F151,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    _LA_152,
    LA_F153,  /* "%s: peer online, proceeding to ICE\n" (%s)  [p2p_signal_compact.c] */
    LA_F154,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F155,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F156,  /* "%s: failed to RE-REGISTER after timeout\n" (%s)  [p2p_signal_compact.c] */
    LA_F157,  /* "%s: failed to send UNREGISTER before restart\n" (%s)  [p2p_signal_compact.c] */
    LA_F158,  /* "%s: track upsert remote cand<%s:%d> failed(OOM), dropping" (%s,%s,%d)  [p2p_nat.c] */
    LA_F159,  /* "%s: ignored (relay not supported)\n" (%s)  [p2p_signal_compact.c] */
    LA_F160,  /* "%s: ignored for duplicated seq=%u, already acked\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F161,  /* "%s: ignored for seq=%u (expect=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F162,  /* "%s: ignored for sid=%u (current sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F163,  /* "%s: ignored in invalid state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F164,  /* "%s: ignored in state(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F165,  /* "%s: ignored in state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F166,  /* "%s: invalid ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F167,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F168,  /* "%s: invalid for non-relay req\n" (%s)  [p2p_signal_compact.c] */
    LA_F169,  /* "%s: invalid in non-COMPACT mode\n" (%s)  [p2p_signal_compact.c] */
    LA_F170,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F171,  /* "%s: keep alive to %d reachable cand(s)" (%s,%d)  [p2p_nat.c] */
    LA_F172,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F173,  /* "%s: no remote candidates to punch" (%s)  [p2p_nat.c] */
    LA_F174,  /* "%s: old request ignored (sid=%u <= last_sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F175,  /* "%s: punching additional cand<%s:%d>[%d] while connected" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F176,  /* "%s: punching remote cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F177,  /* "%s: received FIN from peer, marking NAT as CLOSED" (%s)  [p2p_nat.c] */
    LA_F178,  /* "%s: remote cand[%d]<%s:%d>, starting punch\n" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F179,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F180,  /* "%s: retry(%d/%d) probe\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F181,  /* "%s: retry(%d/%d) req (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F182,  /* "%s: retry(%d/%d) resp (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F183,  /* "%s: retry, (attempt %d/%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F184,  /* "%s: rx confirmed: peer->me path is UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F185,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F186,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F187,  /* "%s: sent, sid=%u, code=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F188,  /* "%s: skip and mark NAT as OPEN (lan_punch enabled)\n" (%s)  [p2p_signal_compact.c] */
    LA_F189,  /* "%s: start punching all(%d) remote candidates" (%s,%d)  [p2p_nat.c] */
    LA_F190,  /* "%s: started, sending first probe\n" (%s)  [p2p_signal_compact.c] */
    LA_F191,  /* "%s: status error(%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F192,  /* "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F193,  /* "%s: timeout after %d retries , type unknown\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F194,  /* "%s: timeout, max(%d) attempts reached, reset to INIT\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F195,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F196,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F197,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F198,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F199,  /* "%s: tx confirmed: me->peer path is UP (echoed seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F200,  /* "%s: unexpected ack_seq=%u mask=0x%04x\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F201,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F202,  /* "%s_ACK sent, sid=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F203,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F204,  /* "Added Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F205,  /* "Append Host candidate: %s:%d" (%s,%d)  [p2p.c] */
    LA_F206,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F207,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F208,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F209,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F210,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F211,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F212,  /* "Connected to server %s:%d as '%s'" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F213,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F214,  /* "Connectivity checks timed out (sent %d rounds), giving up" (%d)  [p2p_ice.c] */
    LA_F215,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F216,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F217,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F218,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F219,  /* "Discarded %d bytes payload of message type %d" (%d,%d)  [p2p_signal_relay.c] */
    LA_F220,  /* "Failed to allocate %u bytes" (%u)  [p2p_signal_relay.c] */
    _LA_221,
    LA_F222,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_F223,  /* "Failed to reserve remote candidates (base=%u cnt=%d)\n" (%u,%d)  [p2p_signal_compact.c] */
    LA_F224,  /* "Failed to reserve remote candidates (cnt=%d)\n" (%d)  [p2p_signal_compact.c] */
    LA_F225,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F226,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F227,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F228,  /* "First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F229,  /* "Formed check list with %d candidate pairs" (%d)  [p2p_ice.c] */
    LA_F230,  /* "Gathered host cand<%s:%d> (priority=0x%08x)" (%s,%d)  [p2p_ice.c] */
    LA_F231,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F232,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F233,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F234,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F235,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F236,  /* "Invalid magic 0x%x (expected 0x%x), resetting" (%x,%x)  [p2p_signal_relay.c] */
    LA_F237,  /* "Invalid read state %d, resetting" (%d)  [p2p_signal_relay.c] */
    LA_F238,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F239,  /* "Marked old path (idx=%d) as FAILED due to addr change\n" (%d)  [p2p_signal_compact.c] */
    LA_F240,  /* "Nomination successful! Using! Using %s path %s:%d%s" (%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F241,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F242,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F243,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F244,  /* "PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u\n" (%d,%u)  [p2p_signal_compact.c] */
    LA_F245,  /* "PEER_INFO(trickle): seq overflow, cannot trickle more\n"  [p2p_signal_compact.c] */
    LA_F246,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F247,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F248,  /* "Passive peer learned remote ID '%s' from OFFER" (%s)  [p2p_signal_relay.c] */
    LA_F249,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F250,  /* "Path recovered: switched to PUNCH"  [p2p.c] */
    LA_F251,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F252,  /* "Peer '%s' is now online (FORWARD received), resuming" (%s)  [p2p_signal_relay.c] */
    LA_F253,  /* "Peer offline, cached %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F254,  /* "Peer online, forwarded %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F255,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F256,  /* "REGISTERED: peer=%s\n" (%s)  [p2p_signal_compact.c] */
    LA_F257,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F258,  /* "Received ACK (status=%d, candidates_acked=%d)" (%d,%d)  [p2p_signal_relay.c] */
    LA_F259,  /* "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%d,%u)  [p2p.c] */
    LA_F260,  /* "Received DATA pkt from %s:%d, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F261,  /* "Recv New Remote Candidate<%s:%d> (type=%d)" (%s,%d,%d)  [p2p_ice.c] */
    LA_F262,  /* "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F263,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F264,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F265,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F266,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F267,  /* "[UDP] %s recv from %s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F268,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F269,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F270,  /* "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F271,  /* "[UDP] %s recv from %s:%d, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F272,  /* "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F273,  /* "[UDP] %s recv from %s:%d, seq=%u, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F274,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F275,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F276,  /* "Requested Relay Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F277,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p.c] */
    LA_F278,  /* "Requested Srflx Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F279,  /* "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F280,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F281,  /* "Selected path: PUNCH (idx=%d)" (%d)  [p2p.c] */
    LA_F282,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F283,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F284,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F285,  /* "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F286,  /* "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F287,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F288,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F289,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F290,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F291,  /* "Sent answer to '%s'" (%s)  [p2p_ice.c] */
    LA_F292,  /* "Sent answer to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F293,  /* "Sent connect request to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F294,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F295,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F296,  /* "Synced path after failover"  [p2p.c] */
    LA_F297,  /* "TURN 401 Unauthorized (realm=%s), authenticating..." (%s)  [p2p_turn.c] */
    LA_F298,  /* "TURN Allocate failed with error %d" (%d)  [p2p_turn.c] */
    LA_F299,  /* "TURN Allocated relay %s:%u (lifetime=%us)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F300,  /* "TURN CreatePermission failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F301,  /* "TURN CreatePermission for %s" (%s)  [p2p_turn.c] */
    LA_F302,  /* "TURN Data Indication from %s:%u (%d bytes)" (%s,%u,%d)  [p2p_turn.c] */
    LA_F303,  /* "TURN Refresh failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F304,  /* "TURN Refresh ok (lifetime=%us)" (%u)  [p2p_turn.c] */
    LA_F305,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F306,  /* "Test I: Timeout"  [p2p_stun.c] */
    LA_F307,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F308,  /* "Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F309,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F310,  /* "Test III: Timeout"  [p2p_stun.c] */
    LA_F311,  /* "Transport layer '%s' init failed, falling back to simple reliable" (%s)  [p2p.c] */
    LA_F312,  /* "UDP hole-punch probing remote candidates (%d candidates)" (%d)  [p2p_ice.c] */
    LA_F313,  /* "UDP hole-punch probing remote candidates round %d/%d" (%d,%d)  [p2p_ice.c] */
    LA_F314,  /* "Unknown ACK status %d" (%d)  [p2p_signal_relay.c] */
    LA_F315,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F316,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F317,  /* "Waiting for peer '%s' timed out (%dms), giving up" (%s,%d)  [p2p_signal_relay.c] */
    LA_F318,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F319,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    LA_F320,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F321,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F322,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F323,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F324,  /* "[SIGNALING] Failed to send candidates, will retry (ret=%d)" (%d)  [p2p_signal_relay.c] */
    LA_F325,  /* "[SIGNALING] Sent candidates (cached, peer offline) %d to %s" (%d,%s)  [p2p_signal_relay.c] */
    LA_F326,  /* "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)" (%d,%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F327,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F328,  /* "[Trickle] Sent 1 candidate to %s (online=%s)" (%s,%s)  [p2p_ice.c] */
    LA_F329,  /* "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()" (%d)  [p2p_ice.c] */
    LA_F330,  /* "[lan_punch] starting NAT punch(Host candidate %d)" (%d)  [p2p_ice.c] */
    LA_F331,  /* "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)" (%s,%d)  [p2p_ice.c] */
    LA_F332,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F333,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F334,  /* "recv error %d" (%d)  [p2p_signal_relay.c] */
    LA_F335,  /* "recv error %d while discarding" (%d)  [p2p_signal_relay.c] */
    LA_F336,  /* "recv error %d while reading payload" (%d)  [p2p_signal_relay.c] */
    LA_F337,  /* "recv error %d while reading sender" (%d)  [p2p_signal_relay.c] */
    LA_F338,  /* "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F339,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F340,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F341,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F342,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F343,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    LA_F344,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F345,  /* "✗ Add Srflx candidate failed(OOM)"  [p2p_stun.c] */
    LA_F346,  /* "%s: invalid session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F347,  /* "%s skipped: session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F348,  /* "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F349,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F350,  /* "[UDP] %s_ACK send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */

    /* Strings (LA_S) */
    LA_S351,  /* "Push host cand<%s:%d> failed(OOM)\n"  [p2p_ice.c] */
    LA_S352,  /* "Push local cand<%s:%d> failed(OOM)\n"  [p2p.c] */

    /* Formats (LA_F) */
    LA_F353,  /* "Upsert remote candidate<%s:%d> (type=%d) failed(OOM)" (%s,%d,%d)  [p2p_ice.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F103

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
