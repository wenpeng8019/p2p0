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
    LA_W3,  /* "Added Host candidate"  [p2p.c] */
    LA_W4,  /* "Added Remote Candidate"  [p2p_signal_relay.c] */
    LA_W5,  /* "address"  [p2p_signal_pubsub.c] */
    LA_W6,  /* "address(es)"  [p2p_route.c] */
    LA_W7,  /* "and"  [p2p_ice.c] */
    LA_W8,  /* "as"  [p2p_signal_relay.c] */
    LA_W9,  /* "Attempt"  [p2p_signal_compact.c] */
    LA_W10,  /* "Attempting Simultaneous Open to"  [p2p_tcp_punch.c] */
    LA_W11,  /* "attempts, switching to RELAY"  [p2p_nat.c] */
    LA_W12,  /* "Authenticated successfully"  [p2p.c] */
    LA_W13,  /* "Authentication failed"  [p2p.c] */
    LA_W14,  /* "Auto-send answer"  [p2p_signal_pubsub.c] */
    LA_W15,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_W16,  /* "Bound to"  [p2p_tcp_punch.c] */
    LA_W17,  /* "bytes"  [p2p_signal_relay.c] */
    LA_W18,  /* "cache"  [p2p_signal_compact.c] */
    LA_W19,  /* "candidate pairs"  [p2p_ice.c, p2p_signal_compact.c] */
    LA_W20,  /* "candidates"  [p2p_signal_pubsub.c, p2p_signal_relay.c, p2p_nat.c] */
    LA_W21,  /* "Cannot add Srflx candidate: realloc failed (OOM)"  [p2p_stun.c] */
    LA_W22,  /* "COMPACT: registering"  [p2p.c] */
    LA_W23,  /* "Connected to server"  [p2p_signal_relay.c] */
    LA_W24,  /* "Data stored in recv buffer"  [p2p_trans_reliable.c] */
    LA_W25,  /* "Detecting..."  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W26,  /* "Detection completed"  [p2p_signal_compact.c, p2p_stun.c] */
    LA_W27,  /* "Discarded"  [p2p_signal_relay.c] */
    LA_W28,  /* "Entered READY state, starting NAT punch and candidate sync"  [p2p_signal_compact.c] */
    LA_W29,  /* "Entered REGISTERED state"  [p2p_signal_compact.c] */
    LA_W30,  /* "expected"  [p2p_signal_relay.c] */
    LA_W31,  /* "Failed to allocate"  [p2p_signal_relay.c] */
    LA_W32,  /* "Failed to create UDP socket on port"  [p2p.c] */
    LA_W33,  /* "Failed to resolve"  [p2p_stun.c] */
    LA_W34,  /* "Failed to resolve TURN server:"  [p2p_turn.c] */
    LA_W35,  /* "Failed to send candidates, will retry"  [p2p.c] */
    LA_W36,  /* "Field"  [p2p_signal_pubsub.c] */
    LA_W37,  /* "Formed check list with"  [p2p_ice.c] */
    LA_W38,  /* "forwarded"  [p2p.c] */
    LA_W39,  /* "from"  [p2p_signal_relay.c] */
    LA_W40,  /* "Full Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W41,  /* "Gathered"  [p2p_ice.c] */
    LA_W42,  /* "Gathered Relay Candidate"  [p2p_turn.c] */
    LA_W43,  /* "Gathered Srflx Candidate"  [p2p_stun.c] */
    LA_W44,  /* "giving up"  [p2p_signal_relay.c] */
    LA_W45,  /* "Initialized:"  [p2p_signal_pubsub.c] */
    LA_W46,  /* "Invalid magic"  [p2p_signal_relay.c] */
    LA_W47,  /* "Invalid read state"  [p2p_signal_relay.c] */
    LA_W48,  /* "is empty or too short"  [p2p_signal_pubsub.c] */
    LA_W49,  /* "is now online"  [p2p_signal_relay.c] */
    LA_W50,  /* "LAN peer confirmed"  [p2p_route.c] */
    LA_W51,  /* "len"  [p2p_stun.c] */
    LA_W52,  /* "Local address detection done"  [p2p_route.c] */
    LA_W53,  /* "Mapped address"  [p2p_stun.c] */
    LA_W54,  /* "Max register attempts reached"  [p2p_signal_compact.c] */
    LA_W55,  /* "more pairs"  [p2p_ice.c] */
    LA_W56,  /* "NAT probe retry"  [p2p_signal_compact.c] */
    LA_W57,  /* "NAT probe sent to"  [p2p_signal_compact.c] */
    LA_W58,  /* "NAT probe timeout, type unknown"  [p2p_signal_compact.c] */
    LA_W59,  /* "NAT punch failed, no TURN server configured"  [p2p.c] */
    LA_W60,  /* "NAT punch failed, server has no relay support"  [p2p.c] */
    LA_W61,  /* "NAT punch failed, using server relay"  [p2p.c] */
    LA_W62,  /* "no (cached)"  [p2p_ice.c, p2p_signal_compact.c] */
    LA_W63,  /* "Nomination successful! Using"  [p2p_ice.c] */
    LA_W64,  /* "online"  [p2p_ice.c] */
    LA_W65,  /* "Open Internet (No NAT)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W66,  /* "Out-of-window packet discarded"  [p2p_trans_reliable.c] */
    LA_W67,  /* "Packet queued"  [p2p_trans_reliable.c] */
    LA_W68,  /* "Packet too large"  [p2p_trans_reliable.c] */
    LA_W69,  /* "Passive peer learned remote ID"  [p2p_signal_relay.c] */
    LA_W70,  /* "path"  [p2p_ice.c] */
    LA_W71,  /* "Peer"  [p2p_signal_relay.c] */
    LA_W72,  /* "Peer is on a different subnet"  [p2p_route.c] */
    LA_W73,  /* "Peer is on the same subnet as"  [p2p_route.c] */
    LA_W74,  /* "Peer offline"  [p2p_signal_relay.c] */
    LA_W75,  /* "Peer offline, waiting for peer to come online"  [p2p_signal_compact.c] */
    LA_W76,  /* "Peer online"  [p2p_signal_relay.c] */
    LA_W77,  /* "Peer online, waiting for PEER_INFO(seq=1)"  [p2p_signal_compact.c] */
    LA_W78,  /* "port busy, trying random port"  [p2p_tcp_punch.c] */
    LA_W79,  /* "Port Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W80,  /* "priority"  [p2p_turn.c] */
    LA_W81,  /* "Processing"  [p2p_signal_pubsub.c] */
    LA_W82,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W83,  /* "Published"  [p2p.c] */
    LA_W84,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_W85,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_W86,  /* "PUNCH: Received from"  [p2p_nat.c] */
    LA_W87,  /* "PUNCH_ACK: Received from"  [p2p_nat.c] */
    LA_W88,  /* "PUNCHING: Attempt"  [p2p_nat.c] */
    LA_W89,  /* "Received ACK"  [p2p_signal_relay.c] */
    LA_W90,  /* "Received New Remote Candidate"  [p2p_ice.c] */
    LA_W91,  /* "Received remote candidate"  [p2p_signal_pubsub.c] */
    LA_W92,  /* "Received route probe from"  [p2p_route.c] */
    LA_W93,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W94,  /* "Received unknown packet type"  [p2p.c] */
    LA_W95,  /* "Received valid signal from"  [p2p_signal_pubsub.c] */
    LA_W96,  /* "recv error"  [p2p_signal_relay.c] */
    LA_W97,  /* "Registering"  [p2p_signal_compact.c] */
    LA_W98,  /* "RELAY: sent initial offer with"  [p2p.c] */
    LA_W99,  /* "RELAY: waiting for incoming offer from any peer"  [p2p.c] */
    LA_W100,  /* "Reliable transport initialized"  [p2p_trans_reliable.c] */
    LA_W101,  /* "Requested"  [p2p_ice.c] */
    LA_W102,  /* "Resent"  [p2p.c] */
    LA_W103,  /* "resetting"  [p2p_signal_relay.c] */
    LA_W104,  /* "Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W105,  /* "role"  [p2p_signal_pubsub.c] */
    LA_W106,  /* "RTT updated"  [p2p_trans_reliable.c] */
    LA_W107,  /* "Same subnet detected but LAN shortcut disabled"  [p2p.c] */
    LA_W108,  /* "Same subnet detected, sent ROUTE_PROBE to"  [p2p.c] */
    LA_W109,  /* "Send window full, dropping packet"  [p2p_trans_reliable.c] */
    LA_W110,  /* "Sending"  [p2p_stun.c] */
    LA_W111,  /* "Sending Allocate Request to"  [p2p_turn.c] */
    LA_W112,  /* "Sent ANSWER"  [p2p_signal_compact.c] */
    LA_W113,  /* "Sent answer to"  [p2p_ice.c, p2p_signal_relay.c] */
    LA_W114,  /* "Sent candidates (cached, peer offline)"  [p2p.c] */
    LA_W115,  /* "Sent candidates, forwarded"  [p2p.c] */
    LA_W116,  /* "Sent connect"  [p2p_signal_relay.c] */
    LA_W117,  /* "Sent route probe to"  [p2p_route.c] */
    LA_W118,  /* "Server error"  [p2p_signal_compact.c] */
    LA_W119,  /* "Server storage full, waiting for peer to come online"  [p2p.c] */
    LA_W120,  /* "Skipping local Host candidates"  [p2p.c] */
    LA_W121,  /* "START: Punching to"  [p2p_nat.c] */
    LA_W122,  /* "Storage full"  [p2p_signal_relay.c] */
    LA_W123,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W124,  /* "Success"  [p2p_stun.c] */
    LA_W125,  /* "SUCCESS: Hole punched! Connected to"  [p2p_nat.c] */
    LA_W126,  /* "Symmetric NAT (port-random)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W127,  /* "Test"  [p2p_stun.c] */
    LA_W128,  /* "Time:"  [p2p_nat.c] */
    LA_W129,  /* "timed out"  [p2p_signal_relay.c] */
    LA_W130,  /* "Timeout"  [p2p_stun.c] */
    LA_W131,  /* "Timeout (no response)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W132,  /* "TIMEOUT: Connection lost"  [p2p_nat.c] */
    LA_W133,  /* "TIMEOUT: Punch failed after"  [p2p_nat.c] */
    LA_W134,  /* "to"  [p2p_signal_compact.c, p2p.c, p2p_stun.c] */
    LA_W135,  /* "UDP Blocked (STUN unreachable)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W136,  /* "UDP hole-punch probing remote candidates"  [p2p_ice.c] */
    LA_W137,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W138,  /* "Unknown ACK status"  [p2p_signal_relay.c] */
    LA_W139,  /* "Unknown signaling mode"  [p2p.c] */
    LA_W140,  /* "Unsupported (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_log.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W141,  /* "Updating Gist field"  [p2p_signal_pubsub.c] */
    LA_W142,  /* "Waiting for peer"  [p2p_signal_relay.c] */
    LA_W143,  /* "while discarding"  [p2p_signal_relay.c] */
    LA_W144,  /* "while reading payload"  [p2p_signal_relay.c] */
    LA_W145,  /* "while reading sender"  [p2p_signal_relay.c] */
    LA_W146,  /* "will be retried by p2p_update()"  [p2p_ice.c] */
    LA_W147,  /* "with candidates"  [p2p.c] */
    LA_W148,  /* "with server"  [p2p_signal_compact.c] */
    LA_W149,  /* "yes"  [p2p_ice.c, p2p_signal_compact.c] */

    /* Strings (LA_S) */
    LA_S0,  /* "1 candidate to"  [p2p_ice.c] */
    LA_S1,  /* "[DTLS] Handshake complete"  [p2p_trans_mbedtls.c] */
    LA_S2,  /* "[OpenSSL] DTLS handshake completed"  [p2p_trans_openssl.c] */
    LA_S3,  /* "[SCTP] usrsctp wrapper initialized (skeleton)"  [p2p_trans_sctp.c] */
    LA_S4,  /* "[Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_S5,  /* "Added Remote Candidate"  [p2p_stun.c] */
    LA_S6,  /* "Allocation successful!"  [p2p_turn.c] */
    LA_S7,  /* "Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_S8,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_S9,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_S10,  /* "cached"  [p2p_signal_relay.c] */
    LA_S11,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S12,  /* "COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_S13,  /* "Connection closed by server"  [p2p_signal_relay.c] */
    LA_S14,  /* "Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_S15,  /* "Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_S16,  /* "Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_S17,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S18,  /* "Detection completed"  [p2p_stun.c] */
    LA_S19,  /* "ERROR: No remote candidates to punch"  [p2p_nat.c] */
    LA_S20,  /* "Error: p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_S21,  /* "Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_S22,  /* "Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_S23,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_S24,  /* "Failed to connect to signaling server"  [p2p.c] */
    LA_S25,  /* "Failed to send header"  [p2p_signal_relay.c] */
    LA_S26,  /* "Failed to send payload"  [p2p_signal_relay.c] */
    LA_S27,  /* "Failed to send target name"  [p2p_signal_relay.c] */
    LA_S28,  /* "forwarded"  [p2p_signal_relay.c] */
    LA_S29,  /* "from"  [p2p_ice.c] */
    LA_S30,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S31,  /* "Host Candidate"  [p2p_ice.c] */
    LA_S32,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S33,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_S34,  /* "Mapped address"  [p2p_signal_compact.c] */
    LA_S35,  /* "need"  [p2p_stun.c] */
    LA_S36,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_S37,  /* "no pong for"  [p2p_nat.c] */
    LA_S38,  /* "offer with"  [p2p.c] */
    LA_S39,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S40,  /* "payload of message type"  [p2p_signal_relay.c] */
    LA_S41,  /* "priority"  [p2p_stun.c] */
    LA_S42,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_S43,  /* "received"  [p2p_signal_relay.c] */
    LA_S44,  /* "relay"  [p2p_signal_compact.c] */
    LA_S45,  /* "Relay Candidate"  [p2p_ice.c] */
    LA_S46,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_S47,  /* "request to"  [p2p_signal_relay.c] */
    LA_S48,  /* "resuming"  [p2p_signal_relay.c] */
    LA_S49,  /* "SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_S50,  /* "sending ACK"  [p2p_route.c] */
    LA_S51,  /* "Sent authentication request to peer"  [p2p_ice.c] */
    LA_S52,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_S53,  /* "Srflx Candidate"  [p2p_ice.c] */
    LA_S54,  /* "STUN server"  [p2p_stun.c] */
    LA_S55,  /* "Test"  [p2p_stun.c] */
    LA_S56,  /* "to"  [p2p_tcp_punch.c, p2p_nat.c] */
    LA_S57,  /* "total sent"  [p2p_signal_pubsub.c] */
    LA_S58,  /* "type"  [p2p_signal_pubsub.c] */
    LA_S59,  /* "Using"  [p2p_ice.c] */
    LA_S60,  /* "via local"  [p2p_route.c] */
    LA_S61,  /* "waiting for"  [p2p.c] */
    LA_S62,  /* "waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_S63,  /* "with"  [p2p_signal_pubsub.c] */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "[DTLS] Handshake failed: %s (-0x%04x)" (%s)  [p2p_trans_mbedtls.c] */
    LA_F1,  /* "[DTLS] ssl_setup failed: -0x%x" (%x)  [p2p_trans_mbedtls.c] */
    LA_F2,  /* "[PseudoTCP] congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F3,  /* "[SCTP] received encapsulated packet, length %d" (%d)  [p2p_trans_sctp.c] */
    LA_F4,  /* "[SCTP] sending %d bytes" (%d)  [p2p_trans_sctp.c] */

    LA_NUM = 219
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
