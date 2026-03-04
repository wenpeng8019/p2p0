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
    LA_W0,  /* "[Trickle] Sent"  [p2p_ice.c] */
    LA_W1,  /* "[Trickle] TCP send failed"  [p2p_ice.c] */
    LA_W2,  /* "ACK processed"  [p2p_trans_reliable.c] */
    LA_W3,  /* "Added Remote Candidate"  [p2p_signal_relay.c] */
    LA_W4,  /* "address"  [p2p_signal_pubsub.c] */
    LA_W5,  /* "address(es)"  [p2p_route.c] */
    LA_W6,  /* "and"  [p2p_ice.c] */
    LA_W7,  /* "as"  [p2p_signal_relay.c] */
    LA_W8,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_W9,  /* "Bound to"  [p2p_tcp_punch.c] */
    LA_W10,  /* "bytes"  [p2p_signal_relay.c] */
    LA_W11,  /* "candidate pairs"  [p2p_ice.c] */
    LA_W12,  /* "candidates"  [p2p_signal_relay.c] */
    LA_W13,  /* "Cannot add Srflx candidate: realloc failed (OOM)"  [p2p_stun.c] */
    LA_W14,  /* "Connected to server"  [p2p_signal_relay.c] */
    LA_W15,  /* "Data stored in recv buffer"  [p2p_trans_reliable.c] */
    LA_W16,  /* "Detecting..."  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W17,  /* "Detection completed"  [p2p_stun.c] */
    LA_W18,  /* "Discarded"  [p2p_signal_relay.c] */
    LA_W19,  /* "expected"  [p2p_signal_relay.c] */
    LA_W20,  /* "Failed to allocate"  [p2p_signal_relay.c] */
    LA_W21,  /* "Failed to resolve"  [p2p_stun.c] */
    LA_W22,  /* "Failed to resolve TURN server:"  [p2p_turn.c] */
    LA_W23,  /* "Failed to send candidates, will retry"  [p2p_signal_relay.c] */
    LA_W24,  /* "Formed check list with"  [p2p_ice.c] */
    LA_W25,  /* "forwarded"  [p2p_signal_relay.c] */
    LA_W26,  /* "from"  [p2p_signal_relay.c] */
    LA_W27,  /* "Full Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W28,  /* "Gathered"  [p2p_ice.c] */
    LA_W29,  /* "Gathered Relay Candidate"  [p2p_turn.c] */
    LA_W30,  /* "Gathered Srflx Candidate"  [p2p_stun.c] */
    LA_W31,  /* "giving up"  [p2p_signal_relay.c] */
    LA_W32,  /* "Initialized:"  [p2p_signal_pubsub.c] */
    LA_W33,  /* "Invalid magic"  [p2p_signal_relay.c] */
    LA_W34,  /* "Invalid read state"  [p2p_signal_relay.c] */
    LA_W35,  /* "is now online"  [p2p_signal_relay.c] */
    LA_W36,  /* "len"  [p2p_stun.c] */
    LA_W37,  /* "Local address detection done"  [p2p_route.c] */
    LA_W38,  /* "Mapped address"  [p2p_stun.c] */
    LA_W39,  /* "more pairs"  [p2p_ice.c] */
    LA_W40,  /* "NAT punch failed, no TURN server configured"  [p2p.c] */
    LA_W41,  /* "NAT punch failed, server has no relay support"  [p2p.c] */
    LA_W42,  /* "NAT punch failed, using COMPACT server relay"  [p2p.c] */
    LA_W43,  /* "no (cached)"  [p2p_ice.c] */
    LA_W44,  /* "Nomination successful! Using"  [p2p_ice.c] */
    LA_W45,  /* "online"  [p2p_ice.c] */
    LA_W46,  /* "Open Internet (No NAT)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W47,  /* "Out-of-window packet discarded"  [p2p_trans_reliable.c] */
    LA_W48,  /* "Packet queued"  [p2p_trans_reliable.c] */
    LA_W49,  /* "Packet too large"  [p2p_trans_reliable.c] */
    LA_W50,  /* "Passive peer learned remote ID"  [p2p_signal_relay.c] */
    LA_W51,  /* "path"  [p2p_ice.c] */
    LA_W52,  /* "Peer"  [p2p_signal_relay.c] */
    LA_W53,  /* "Peer offline"  [p2p_signal_relay.c] */
    LA_W54,  /* "Peer online"  [p2p_signal_relay.c] */
    LA_W55,  /* "port busy, trying random port"  [p2p_tcp_punch.c] */
    LA_W56,  /* "Port Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W57,  /* "priority"  [p2p_turn.c] */
    LA_W58,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W59,  /* "Published"  [p2p_signal_pubsub.c] */
    LA_W60,  /* "Received ACK"  [p2p_signal_relay.c] */
    LA_W61,  /* "Received New Remote Candidate"  [p2p_ice.c] */
    LA_W62,  /* "Received remote candidate"  [p2p_signal_pubsub.c] */
    LA_W63,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W64,  /* "recv error"  [p2p_signal_relay.c] */
    LA_W65,  /* "Reliable transport initialized"  [p2p_trans_reliable.c] */
    LA_W66,  /* "Requested"  [p2p_ice.c] */
    LA_W67,  /* "Resent"  [p2p_signal_pubsub.c] */
    LA_W68,  /* "resetting"  [p2p_signal_relay.c] */
    LA_W69,  /* "Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W70,  /* "RTT updated"  [p2p_trans_reliable.c] */
    LA_W71,  /* "Send window full, dropping packet"  [p2p_trans_reliable.c] */
    LA_W72,  /* "Sending"  [p2p_stun.c] */
    LA_W73,  /* "Sending Allocate Request to"  [p2p_turn.c] */
    LA_W74,  /* "Sent answer to"  [p2p_ice.c, p2p_signal_relay.c] */
    LA_W75,  /* "Sent candidates (cached, peer offline)"  [p2p_signal_relay.c] */
    LA_W76,  /* "Sent candidates, forwarded"  [p2p_signal_relay.c] */
    LA_W77,  /* "Sent connect"  [p2p_signal_relay.c] */
    LA_W78,  /* "Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_W79,  /* "Storage full"  [p2p_signal_relay.c] */
    LA_W80,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W81,  /* "Success"  [p2p_stun.c] */
    LA_W82,  /* "Symmetric NAT (port-random)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W83,  /* "Test"  [p2p_stun.c] */
    LA_W84,  /* "timed out"  [p2p_signal_relay.c] */
    LA_W85,  /* "Timeout"  [p2p_stun.c] */
    LA_W86,  /* "Timeout (no response)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W87,  /* "to"  [p2p_signal_pubsub.c, p2p_signal_relay.c, p2p_stun.c] */
    LA_W88,  /* "UDP Blocked (STUN unreachable)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W89,  /* "UDP hole-punch probing remote candidates"  [p2p_ice.c] */
    LA_W90,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W91,  /* "Unknown ACK status"  [p2p_signal_relay.c] */
    LA_W92,  /* "Unsupported (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W93,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W94,  /* "Waiting for peer"  [p2p_signal_relay.c] */
    LA_W95,  /* "while discarding"  [p2p_signal_relay.c] */
    LA_W96,  /* "while reading payload"  [p2p_signal_relay.c] */
    LA_W97,  /* "while reading sender"  [p2p_signal_relay.c] */
    LA_W98,  /* "will be retried by p2p_update()"  [p2p_ice.c] */
    LA_W99,  /* "yes"  [p2p_ice.c] */

    /* Strings (LA_S) */
    LA_S0,  /* "1 candidate to"  [p2p_ice.c] */
    LA_S1,  /* "[OpenSSL] DTLS handshake completed"  [p2p_trans_openssl.c] */
    LA_S2,  /* "[SCTP] usrsctp wrapper initialized (skeleton)"  [p2p_trans_sctp.c] */
    LA_S3,  /* "[Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_S4,  /* "Added Remote Candidate"  [p2p_stun.c] */
    LA_S5,  /* "Allocation successful!"  [p2p_turn.c] */
    LA_S6,  /* "Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_S7,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_S8,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_S9,  /* "cached"  [p2p_signal_relay.c] */
    LA_S10,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S11,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_S12,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_S13,  /* "COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_S14,  /* "Connection closed by server"  [p2p_signal_relay.c] */
    LA_S15,  /* "Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_S16,  /* "Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_S17,  /* "Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_S18,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S19,  /* "Detection completed"  [p2p_stun.c] */
    LA_S20,  /* "DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_S21,  /* "Error: p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_S22,  /* "Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_S23,  /* "Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_S24,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_S25,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_S26,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_S27,  /* "Failed to push remote candidate"  [p2p_signal_compact.c] */
    LA_S28,  /* "Failed to reserve remote candidates (cnt=1)"  [p2p_signal_compact.c] */
    LA_S29,  /* "Failed to send header"  [p2p_signal_relay.c] */
    LA_S30,  /* "Failed to send payload"  [p2p_signal_relay.c] */
    LA_S31,  /* "Failed to send punch packet for new peer addr"  [p2p_signal_compact.c] */
    LA_S32,  /* "Failed to send target name"  [p2p_signal_relay.c] */
    LA_S33,  /* "forwarded"  [p2p_signal_relay.c] */
    LA_S34,  /* "from"  [p2p_ice.c] */
    LA_S35,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S36,  /* "Handshake complete"  [p2p_trans_mbedtls.c] */
    LA_S37,  /* "Host Candidate"  [p2p_ice.c] */
    LA_S38,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S39,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_S40,  /* "NAT connection recovered, upgrading from RELAY to CONNECTED"  [p2p.c] */
    LA_S41,  /* "NAT connection timeout, downgrading to relay mode"  [p2p.c] */
    LA_S42,  /* "need"  [p2p_stun.c] */
    LA_S43,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_S44,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_S45,  /* "No remote candidates to punch"  [p2p_nat.c] */
    LA_S46,  /* "offer with"  [p2p_signal_pubsub.c] */
    LA_S47,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_S48,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S49,  /* "P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_S50,  /* "P2P connection established"  [p2p.c] */
    LA_S51,  /* "P2P punch failed, adding relay path"  [p2p.c] */
    LA_S52,  /* "P2P punching in progress ..."  [p2p.c] */
    LA_S53,  /* "payload of message type"  [p2p_signal_relay.c] */
    LA_S54,  /* "priority"  [p2p_stun.c] */
    LA_S55,  /* "probe(compact) already in progress"  [p2p_probe.c] */
    LA_S56,  /* "probe(compact) peer is OFFLINE (REQ_ACK status=1)"  [p2p_probe.c] */
    LA_S57,  /* "probe(compact) REQ_ACK: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S58,  /* "probe(compact) restarting periodic check"  [p2p_probe.c] */
    LA_S59,  /* "probe(compact) skipped: server does not support MSG"  [p2p_probe.c] */
    LA_S60,  /* "probe(compact) skipped: signaling not yet registered"  [p2p_probe.c] */
    LA_S61,  /* "probe(compact) timeout: server cannot reach peer"  [p2p_probe.c] */
    LA_S62,  /* "probe(compact) triggered: MSG echo via server"  [p2p_probe.c] */
    LA_S63,  /* "probe(relay) address exchange failed: peer OFFLINE"  [p2p_probe.c] */
    LA_S64,  /* "probe(relay) address exchange initiated"  [p2p_probe.c] */
    LA_S65,  /* "probe(relay) address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S66,  /* "probe(relay) already in progress"  [p2p_probe.c] */
    LA_S67,  /* "probe(relay) exchange timeout: peer offline?"  [p2p_probe.c] */
    LA_S68,  /* "probe(relay) restarting periodic check"  [p2p_probe.c] */
    LA_S69,  /* "probe(relay) skipped: relay signaling not connected"  [p2p_probe.c] */
    LA_S70,  /* "probe(relay) triggered: refreshing TURN allocation"  [p2p_probe.c] */
    LA_S71,  /* "probe(relay) TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S72,  /* "probe(relay) TURN allocation request sent"  [p2p_probe.c] */
    LA_S73,  /* "probe(relay) TURN allocation timeout"  [p2p_probe.c] */
    LA_S74,  /* "probe(relay) UDP probe packet sent"  [p2p_probe.c] */
    LA_S75,  /* "probe(relay) UDP timeout: network issue"  [p2p_probe.c] */
    LA_S76,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_S77,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_S78,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_S79,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_S80,  /* "received"  [p2p_signal_relay.c] */
    LA_S81,  /* "Received FIN packet, connection closed"  [p2p.c] */
    LA_S82,  /* "Relay Candidate"  [p2p_ice.c] */
    LA_S83,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_S84,  /* "request to"  [p2p_signal_relay.c] */
    LA_S85,  /* "resuming"  [p2p_signal_relay.c] */
    LA_S86,  /* "SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_S87,  /* "Sending FIN packet to peer"  [p2p.c] */
    LA_S88,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    LA_S89,  /* "Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_S90,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_S91,  /* "Skipping local Host candidates on --public-only"  [p2p.c] */
    LA_S92,  /* "Srflx Candidate"  [p2p_ice.c] */
    LA_S93,  /* "Starting internal thread"  [p2p.c] */
    LA_S94,  /* "Stopping internal thread"  [p2p.c] */
    LA_S95,  /* "STUN server"  [p2p_stun.c] */
    LA_S96,  /* "Test"  [p2p_stun.c] */
    LA_S97,  /* "to"  [p2p_tcp_punch.c] */
    LA_S98,  /* "type"  [p2p_signal_pubsub.c] */
    LA_S99,  /* "Using"  [p2p_ice.c] */
    LA_S100,  /* "waiting for peer to come online"  [p2p_signal_relay.c] */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "  ... %s %d %s" (%s,%d,%s)  [p2p_ice.c] */
    LA_F1,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F2,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F3,  /* "  [%d]: %s:%d (type: %s)" (%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F4,  /* "%s %d" (%s,%d)  [p2p_signal_relay.c] */
    LA_F5,  /* "%s %d %s" (%s,%d,%s)  [p2p_ice.c, p2p_signal_relay.c] */
    LA_F6,  /* "%s %d, %s" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F7,  /* "%s %s" (%s,%s)  [p2p_signal_pubsub.c, p2p_turn.c, p2p_stun.c] */
    LA_F8,  /* "%s %s %d, %s" (%s,%s,%d,%s)  [p2p_tcp_punch.c] */
    LA_F9,  /* "%s %s %s" (%s,%s,%s)  [p2p_stun.c] */
    LA_F10,  /* "%s %s %s %s" (%s,%s,%s,%s)  [p2p_ice.c] */
    LA_F11,  /* "%s %s %s %s %s:%d (%s=%d)" (%s,%s,%s,%s,%s,%d,%s,%d)  [p2p_stun.c] */
    LA_F12,  /* "%s %s %s (%s %s %s)" (%s,%s,%s,%s,%s,%s)  [p2p_stun.c] */
    LA_F13,  /* "%s %s %s (%s=%s)" (%s,%s,%s,%s,%s)  [p2p_ice.c] */
    LA_F14,  /* "%s %s %s! %s %s" (%s,%s,%s,%s,%s)  [p2p_stun.c] */
    LA_F15,  /* "%s %s %s: %s:%d" (%s,%s,%s,%s,%d)  [p2p_stun.c] */
    LA_F16,  /* "%s %s '%s' (%d %s)" (%s,%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F17,  /* "%s %s: %s:%d (priority=0x%08x)" (%s,%s,%s,%d)  [p2p_ice.c] */
    LA_F18,  /* "%s %s:%d" (%s,%s,%d)  [p2p_turn.c] */
    LA_F19,  /* "%s %s:%d %s '%s'" (%s,%s,%d,%s,%s)  [p2p_signal_relay.c] */
    LA_F20,  /* "%s %s:%u (%s=%u)" (%s,%s,%u,%s,%u)  [p2p_turn.c] */
    LA_F21,  /* "%s %u %s" (%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F22,  /* "%s '%s'" (%s,%s)  [p2p_ice.c] */
    LA_F23,  /* "%s '%s' %s (%dms), %s" (%s,%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F24,  /* "%s '%s' %s (%s FORWARD), %s" (%s,%s,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F25,  /* "%s '%s' %s OFFER" (%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F26,  /* "%s '%s' (%d %s)" (%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F27,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F28,  /* "%s (%d %s)" (%s,%d,%s)  [p2p_ice.c] */
    LA_F29,  /* "%s (ret=%d), %s" (%s,%d,%s)  [p2p_ice.c] */
    LA_F30,  /* "%s (status=%d, candidates_acked=%d)" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F31,  /* "%s 0x%x (%s 0x%x), %s" (%s,%x,%s,%x,%s)  [p2p_signal_relay.c] */
    LA_F32,  /* "%s :%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F33,  /* "%s NOTIFY: accepted" (%s)  [p2p_signal_compact.c] */
    LA_F34,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F35,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F36,  /* "%s ack_seq=%u send_base=%u inflight=%d" (%s,%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F37,  /* "%s bad payload(len=%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F38,  /* "%s len=%d max=%d" (%s,%d,%d)  [p2p_trans_reliable.c] */
    LA_F39,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F40,  /* "%s resent, %d/%d" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F41,  /* "%s round %d/%d" (%s,%d,%d)  [p2p_ice.c] */
    LA_F42,  /* "%s rto=%d win=%d" (%s,%d,%d)  [p2p_trans_reliable.c] */
    LA_F43,  /* "%s rtt=%dms srtt=%d rttvar=%d rto=%d" (%s,%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F44,  /* "%s send_count=%d" (%s,%d)  [p2p_trans_reliable.c] */
    LA_F45,  /* "%s sent" (%s)  [p2p_signal_compact.c] */
    LA_F46,  /* "%s sent, inst_id=%u, cands=%d" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F47,  /* "%s sent, seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F48,  /* "%s sent, sid=%u, msg=%u, size=%d" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F49,  /* "%s sent, size=%d (ses_id=%llu)" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F50,  /* "%s sent, total=%d (ses_id=%llu)" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F51,  /* "%s seq=%u base=%u" (%s,%u,%u)  [p2p_trans_reliable.c] */
    LA_F52,  /* "%s seq=%u len=%d base=%u" (%s,%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F53,  /* "%s seq=%u len=%d inflight=%d" (%s,%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F54,  /* "%s seq=0: accepted cand_cnt=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F55,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F56,  /* "%s! %s %s %s %s:%d%s" (%s,%s,%s,%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F57,  /* "%s, %s" (%s,%s)  [p2p_signal_relay.c] */
    LA_F58,  /* "%s, %s %d %s" (%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F59,  /* "%s, retry remaining candidates and FIN to peer" (%s)  [p2p_signal_compact.c] */
    LA_F60,  /* "%s, sent on %s" (%s,%s)  [p2p_signal_compact.c] */
    LA_F61,  /* "%s, timeout, max(%d) attempts reached" (%s,%d)  [p2p_signal_compact.c] */
    LA_F62,  /* "%s: %d %s" (%s,%d,%s)  [p2p_route.c] */
    LA_F63,  /* "%s: %d -> %s:%d" (%s,%d,%s,%d)  [p2p_ice.c, p2p_signal_relay.c] */
    LA_F64,  /* "%s: %s=%d, %s=%s:%d" (%s,%s,%d,%s,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F65,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F66,  /* "%s: Peer addr changed -> %s:%d, retrying punch" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F67,  /* "%s: RESP retry timeout after %d attempts (sid=%u)" (%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F68,  /* "%s: accepted" (%s)  [p2p_signal_compact.c] */
    LA_F69,  /* "%s: accepted (ses_id=%llu)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F70,  /* "%s: accepted for ack_seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F71,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F72,  /* "%s: accepted sid=%u, msg=%u" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F73,  /* "%s: accepted, RPC complete (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F74,  /* "%s: accepted, len=%d (ses_id=%llu)" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F75,  /* "%s: accepted, probe_mapped=%s:%d" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F76,  /* "%s: accepted, public=%s:%d max_cands=%d probe_port=%d relay=%s msg=%s" (%s,%s,%d,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F77,  /* "%s: accepted, waiting for response (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F78,  /* "%s: already started (retries=%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F79,  /* "%s: bad payload(len=%d cand_cnt=%d)" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F80,  /* "%s: bad payload(len=%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F81,  /* "%s: bad payload(len=%d, expect 14)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F82,  /* "%s: detection completed, mapped=%s:%d probe=%s:%d -> %s" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F83,  /* "%s: duplicate request ignored (sid=%u, already processing)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F84,  /* "%s: entered, %s arrived after PEER_INFO" (%s,%s)  [p2p_signal_compact.c] */
    LA_F85,  /* "%s: entered, %s arrived after REGISTERED" (%s,%s)  [p2p_signal_compact.c] */
    LA_F86,  /* "%s: ignored (relay not supported)" (%s)  [p2p_signal_compact.c] */
    LA_F87,  /* "%s: ignored for duplicated seq=%u, already acked" (%s,%u)  [p2p_signal_compact.c] */
    LA_F88,  /* "%s: ignored for seq=%u (expect=%d)" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F89,  /* "%s: ignored for ses_id=%llu (local ses_id=%llu)" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F90,  /* "%s: ignored for sid=%u (current sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F91,  /* "%s: ignored for sid=%u (current sid=%u) or invalid state=%d" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F92,  /* "%s: ignored in invalid state=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F93,  /* "%s: ignored in state=%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F94,  /* "%s: invalid ack_seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F95,  /* "%s: invalid for non-relay req" (%s)  [p2p_signal_compact.c] */
    LA_F96,  /* "%s: invalid in non-COMPACT mode" (%s)  [p2p_signal_compact.c] */
    LA_F97,  /* "%s: invalid seq=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F98,  /* "%s: local timeout after %d retries (sid=%u)" (%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F99,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F100,  /* "%s: old request ignored (sid=%u <= last_sid=%u)" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F101,  /* "%s: peer disconnected (ses_id=%llu), reset to REGISTERED" (%s,%l)  [p2p_signal_compact.c] */
    LA_F102,  /* "%s: peer offline (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F103,  /* "%s: remote candidate[%d] %s:%d, starting punch" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F104,  /* "%s: retry probe, %d/%d" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F105,  /* "%s: retry req, %d/%d (sid=%u)" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F106,  /* "%s: retry resp, %d/%d (sid=%u)" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F107,  /* "%s: retry, (attempt %d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F108,  /* "%s: sent, sid=%u, code=%u" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F109,  /* "%s: server forward timeout (sid=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F110,  /* "%s: session mismatch(local=%llu pkt=%llu)" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F111,  /* "%s: session mismatch(local=%llu, pkt=%llu)" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F112,  /* "%s: skip and mark NAT as OPEN (lan_punch enabled)" (%s)  [p2p_signal_compact.c] */
    LA_F113,  /* "%s: started, sending first probe" (%s)  [p2p_signal_compact.c] */
    LA_F114,  /* "%s: status error(%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F115,  /* "%s: sync complete (ses_id=%llu)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F116,  /* "%s: sync complete (ses_id=%llu, mask=0x%04x)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F117,  /* "%s: timeout after %d ms, forcing exit to REGISTERED" (%s,%d)  [p2p_signal_compact.c] */
    LA_F118,  /* "%s: timeout, type unknown" (%s)  [p2p_signal_compact.c] */
    LA_F119,  /* "%s: unexpected ack_seq=%u mask=0x%04x" (%s,%u)  [p2p_signal_compact.c] */
    LA_F120,  /* "%s:%04d: %s" (%s,%s)  [p2p_trans_mbedtls.c] */
    LA_F121,  /* "%s_ACK sent, seq=%u (ses_id=%llu)" (%s,%u,%l)  [p2p_signal_compact.c] */
    LA_F122,  /* "%s_ACK sent, sid=%u" (%s,%u)  [p2p_signal_compact.c] */
    LA_F123,  /* "ATTEMPT: punch to %d/%d candidates (elapsed: %llu ms)" (%d,%d,%l)  [p2p_nat.c] */
    LA_F124,  /* "Added PUNCH path to path manager, idx=%d" (%d)  [p2p.c] */
    LA_F125,  /* "Added RELAY path to path manager, idx=%d" (%d)  [p2p.c] */
    LA_F126,  /* "Append Host candidate: %s:%d" (%s,%d)  [p2p.c] */
    LA_F127,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F128,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F129,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F130,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F131,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F132,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F133,  /* "Failed to realloc memory for local candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_F134,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_probe.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_F135,  /* "Failed to reserve remote candidates (base=%u cnt=%d)" (%u,%d)  [p2p_signal_compact.c] */
    LA_F136,  /* "Failed to reserve remote candidates (cnt=%d)" (%d)  [p2p_signal_compact.c] */
    LA_F137,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F138,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_trans_mbedtls.c] */
    LA_F139,  /* "Ignoring punch request for candidate %d since already connected" (%d)  [p2p_nat.c] */
    LA_F140,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F141,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F142,  /* "Invalid candidate index: %d (count: %d)" (%d,%d)  [p2p_nat.c] */
    LA_F143,  /* "Keep-alive to %s:%d" (%s,%d)  [p2p_nat.c] */
    LA_F144,  /* "LAN path detected and activated: %s:%d" (%s,%d)  [p2p_nat.c] */
    LA_F145,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F146,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F147,  /* "PUNCH bidirectional confirmed: NAT_CONNECTED (%s:%d)" (%s,%d)  [p2p_nat.c] */
    LA_F148,  /* "PUNCH rx confirmed: peer->me path is UP (%s:%d)" (%s,%d)  [p2p_nat.c] */
    LA_F149,  /* "PUNCH tx confirmed: me->peer path is UP (peer echoed seq=%u)" (%u)  [p2p_nat.c] */
    LA_F150,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F151,  /* "Path recovered: switched to %s" (%s)  [p2p.c] */
    LA_F152,  /* "Path switched: %s -> %s (RTT: %u -> %u ms)" (%s,%s,%u,%u)  [p2p.c] */
    LA_F153,  /* "Peer IP %s does not match any local network segment" (%s)  [p2p_route.c] */
    LA_F154,  /* "Peer IP %s matches local network segment %s (connectivity pending probe)" (%s,%s)  [p2p_route.c] */
    LA_F155,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F156,  /* "Punch timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%l,%d)  [p2p_nat.c] */
    LA_F157,  /* "Punching remote candidate(%d) %s:%d" (%d,%s,%d)  [p2p_nat.c] */
    LA_F158,  /* "REGISTERED: peer=%s" (%s)  [p2p_signal_compact.c] */
    LA_F159,  /* "Received %s pkt from %s:%d" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F160,  /* "Received %s pkt from %s:%d, flags=0x%02x, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F161,  /* "Received %s pkt from %s:%d, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F162,  /* "Received %s pkt from %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F163,  /* "Received %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F164,  /* "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%d,%u)  [p2p.c] */
    LA_F165,  /* "Received DATA pkt from %s:%d, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F166,  /* "Received FIN pkt from %s:%d, NAT state will be closed" (%s,%d)  [p2p_nat.c] */
    LA_F167,  /* "Received PUNCH pkt from %s:%d, seq=%u, echo_seq=%u, len=%d" (%s,%d,%u,%u,%d)  [p2p_nat.c] */
    LA_F168,  /* "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F169,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F170,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F171,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F172,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F173,  /* "Resend %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F174,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F175,  /* "Selected path: %s (idx=%d)" (%s,%d)  [p2p.c] */
    LA_F176,  /* "Send %s pkt to %s:%d, seq=%u, flags=0, len=0" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F177,  /* "Send %s pkt to %s:%d, seq=%u, flags=0x%02x, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F178,  /* "Send %s pkt to %s:%d, seq=0, flags=0, len=%d" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F179,  /* "Send %s_ACK pkt to %s:%d, seq=%u, flags=0, len=%d" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F180,  /* "Send %s_ACK pkt to %s:%d, seq=0, flags=0, len=2" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F181,  /* "Send alive PUNCH pkt to %s:%d, seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F182,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F183,  /* "Send probe PUNCH pkt to %s:%d, echo_seq=%d" (%s,%d,%d)  [p2p_nat.c] */
    LA_F184,  /* "Send probe PUNCH pkt to %s:%d, seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F185,  /* "Send relay PUNCH pkt to %s:%d, echo_seq=%d" (%s,%d,%d)  [p2p_nat.c] */
    LA_F186,  /* "Sent UNREGISTER Pkt for local_peer_id=%s, remote_peer_id=%s" (%s,%s)  [p2p.c] */
    LA_F187,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F188,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F189,  /* "Start punching all(%d) remote candidates" (%d)  [p2p_nat.c] */
    LA_F190,  /* "Switched to backup path: %s" (%s)  [p2p.c] */
    LA_F191,  /* "TIMEOUT: No response from peer for (%llu ms), connection lost" (%l)  [p2p_nat.c] */
    LA_F192,  /* "TIMEOUT: Punch failed after %llu ms (ICE done), switching to RELAY" (%l)  [p2p_nat.c] */
    LA_F193,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F194,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F195,  /* "Using path: %s" (%s)  [p2p.c] */
    LA_F196,  /* "[DEBUG] %s %d %s %s %d" (%s,%d,%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F197,  /* "[DEBUG] %s received (ice_state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F198,  /* "[DEBUG] First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F199,  /* "[DEBUG] relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F200,  /* "[SCTP] received encapsulated packet, length %d" (%d)  [p2p_trans_sctp.c] */
    LA_F201,  /* "[SCTP] sending %d bytes" (%d)  [p2p_trans_sctp.c] */
    LA_F202,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F203,  /* "[lan_punch] 启动 PUNCH 流程 (Host 候选 %d 个)" (%d)  [p2p_ice.c] */
    LA_F204,  /* "[prflx] %s %s:%d (Peer Reflexive - symmetric NAT)" (%s,%s,%d)  [p2p_ice.c] */
    LA_F205,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F206,  /* "probe(compact) SUCCESS: peer reachable via server (RTT: % ms)"  [p2p_probe.c] */
    LA_F207,  /* "probe(compact) send failed: ret=%d" (%d)  [p2p_probe.c] */
    LA_F208,  /* "probe(compact) sent: MSG(msg=0, sid=%u)" (%u)  [p2p_probe.c] */
    LA_F209,  /* "probe(compact) timeout, retry %d/%d" (%d,%d)  [p2p_probe.c] */
    LA_F210,  /* "probe(relay) SUCCESS: UDP reachable via TURN (RTT: % ms)"  [p2p_probe.c] */
    LA_F211,  /* "probe(relay) TURN allocation failed: ret=%d" (%d)  [p2p_probe.c] */
    LA_F212,  /* "probe(relay) TURN timeout, retry %d/%d" (%d,%d)  [p2p_probe.c] */
    LA_F213,  /* "probe(relay) UDP timeout, retry %d/%d" (%d,%d)  [p2p_probe.c] */
    LA_F214,  /* "probe(relay) exchange timeout, retry %d/%d" (%d,%d)  [p2p_probe.c] */
    LA_F215,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F216,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_trans_mbedtls.c] */
    LA_F217,  /* "✓ %s %s %s:%d (%s=%u)" (%s,%s,%s,%d,%s,%u)  [p2p_stun.c] */
    LA_F218,  /* "✗ %s" (%s)  [p2p_stun.c] */
    LA_F219,  /* "连通性检查超时（已发送 %d 轮），放弃" (%d)  [p2p_ice.c] */
    LA_F220,  /* "重传 seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */

    LA_NUM = 422
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
