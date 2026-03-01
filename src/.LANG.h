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
    LA_W8,  /* "Attempt"  [p2p_signal_compact.c] */
    LA_W9,  /* "attempts, switching to RELAY"  [p2p_nat.c] */
    LA_W10,  /* "Authenticated successfully"  [p2p.c] */
    LA_W11,  /* "Authentication failed"  [p2p.c] */
    LA_W12,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_W13,  /* "Bound to"  [p2p_tcp_punch.c] */
    LA_W14,  /* "bytes"  [p2p_signal_relay.c] */
    LA_W15,  /* "cache"  [p2p_signal_compact.c] */
    LA_W16,  /* "candidate pairs"  [p2p_ice.c, p2p_signal_compact.c] */
    LA_W17,  /* "candidates"  [p2p_signal_relay.c, p2p_nat.c] */
    LA_W18,  /* "Cannot add Srflx candidate: realloc failed (OOM)"  [p2p_stun.c] */
    LA_W19,  /* "Connected to server"  [p2p_signal_relay.c] */
    LA_W20,  /* "Data stored in recv buffer"  [p2p_trans_reliable.c] */
    LA_W21,  /* "Detecting..."  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W22,  /* "Detection completed"  [p2p_signal_compact.c, p2p_stun.c] */
    LA_W23,  /* "Discarded"  [p2p_signal_relay.c] */
    LA_W24,  /* "Entered READY state, starting NAT punch and candidate sync"  [p2p_signal_compact.c] */
    LA_W25,  /* "Entered REGISTERED state"  [p2p_signal_compact.c] */
    LA_W26,  /* "expected"  [p2p_signal_relay.c] */
    LA_W27,  /* "Failed to allocate"  [p2p_signal_relay.c] */
    LA_W28,  /* "Failed to resolve"  [p2p_stun.c] */
    LA_W29,  /* "Failed to resolve TURN server:"  [p2p_turn.c] */
    LA_W30,  /* "Failed to send candidates, will retry"  [p2p_signal_relay.c] */
    LA_W31,  /* "Formed check list with"  [p2p_ice.c] */
    LA_W32,  /* "forwarded"  [p2p_signal_relay.c] */
    LA_W33,  /* "from"  [p2p_signal_relay.c] */
    LA_W34,  /* "Full Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W35,  /* "Gathered"  [p2p_ice.c] */
    LA_W36,  /* "Gathered Relay Candidate"  [p2p_turn.c] */
    LA_W37,  /* "Gathered Srflx Candidate"  [p2p_stun.c] */
    LA_W38,  /* "giving up"  [p2p_signal_relay.c] */
    LA_W39,  /* "Initialized:"  [p2p_signal_pubsub.c] */
    LA_W40,  /* "Invalid magic"  [p2p_signal_relay.c] */
    LA_W41,  /* "Invalid read state"  [p2p_signal_relay.c] */
    LA_W42,  /* "is now online"  [p2p_signal_relay.c] */
    LA_W43,  /* "LAN peer confirmed"  [p2p_route.c] */
    LA_W44,  /* "len"  [p2p_stun.c] */
    LA_W45,  /* "Local address detection done"  [p2p_route.c] */
    LA_W46,  /* "Mapped address"  [p2p_stun.c] */
    LA_W47,  /* "more pairs"  [p2p_ice.c] */
    LA_W48,  /* "NAT probe retry"  [p2p_signal_compact.c] */
    LA_W49,  /* "NAT probe sent to"  [p2p_signal_compact.c] */
    LA_W50,  /* "NAT probe timeout, type unknown"  [p2p_signal_compact.c] */
    LA_W51,  /* "NAT punch failed, no TURN server configured"  [p2p.c] */
    LA_W52,  /* "NAT punch failed, server has no relay support"  [p2p.c] */
    LA_W53,  /* "NAT punch failed, using server relay"  [p2p.c] */
    LA_W54,  /* "no (cached)"  [p2p_ice.c, p2p_signal_compact.c] */
    LA_W55,  /* "Nomination successful! Using"  [p2p_ice.c] */
    LA_W56,  /* "online"  [p2p_ice.c] */
    LA_W57,  /* "Open Internet (No NAT)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W58,  /* "Out-of-window packet discarded"  [p2p_trans_reliable.c] */
    LA_W59,  /* "Packet queued"  [p2p_trans_reliable.c] */
    LA_W60,  /* "Packet too large"  [p2p_trans_reliable.c] */
    LA_W61,  /* "Passive peer learned remote ID"  [p2p_signal_relay.c] */
    LA_W62,  /* "path"  [p2p_ice.c] */
    LA_W63,  /* "Peer"  [p2p_signal_relay.c] */
    LA_W64,  /* "Peer is on a different subnet"  [p2p_route.c] */
    LA_W65,  /* "Peer is on the same subnet as"  [p2p_route.c] */
    LA_W66,  /* "Peer offline"  [p2p_signal_relay.c] */
    LA_W67,  /* "Peer offline, waiting for peer to come online"  [p2p_signal_compact.c] */
    LA_W68,  /* "Peer online"  [p2p_signal_relay.c] */
    LA_W69,  /* "Peer online, waiting for PEER_INFO(seq=1)"  [p2p_signal_compact.c] */
    LA_W70,  /* "port busy, trying random port"  [p2p_tcp_punch.c] */
    LA_W71,  /* "Port Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W72,  /* "priority"  [p2p_turn.c] */
    LA_W73,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W74,  /* "Published"  [p2p_signal_pubsub.c] */
    LA_W75,  /* "PUNCHING: Attempt"  [p2p_nat.c] */
    LA_W76,  /* "Received ACK"  [p2p_signal_relay.c] */
    LA_W77,  /* "Received New Remote Candidate"  [p2p_ice.c] */
    LA_W78,  /* "Received remote candidate"  [p2p_signal_pubsub.c] */
    LA_W79,  /* "Received route probe from"  [p2p_route.c] */
    LA_W80,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W81,  /* "recv error"  [p2p_signal_relay.c] */
    LA_W82,  /* "Registering"  [p2p_signal_compact.c] */
    LA_W83,  /* "Reliable transport initialized"  [p2p_trans_reliable.c] */
    LA_W84,  /* "Requested"  [p2p_ice.c] */
    LA_W85,  /* "Resent"  [p2p_signal_pubsub.c] */
    LA_W86,  /* "resetting"  [p2p_signal_relay.c] */
    LA_W87,  /* "Restricted Cone NAT"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W88,  /* "RTT updated"  [p2p_trans_reliable.c] */
    LA_W89,  /* "Send window full, dropping packet"  [p2p_trans_reliable.c] */
    LA_W90,  /* "Sending"  [p2p_stun.c] */
    LA_W91,  /* "Sending Allocate Request to"  [p2p_turn.c] */
    LA_W92,  /* "Sent ANSWER"  [p2p_signal_compact.c] */
    LA_W93,  /* "Sent answer to"  [p2p_ice.c, p2p_signal_relay.c] */
    LA_W94,  /* "Sent candidates (cached, peer offline)"  [p2p_signal_relay.c] */
    LA_W95,  /* "Sent candidates, forwarded"  [p2p_signal_relay.c] */
    LA_W96,  /* "Sent connect"  [p2p_signal_relay.c] */
    LA_W97,  /* "Sent route probe to"  [p2p_route.c] */
    LA_W98,  /* "Server error"  [p2p_signal_compact.c] */
    LA_W99,  /* "Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_W100,  /* "Storage full"  [p2p_signal_relay.c] */
    LA_W101,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W102,  /* "Success"  [p2p_stun.c] */
    LA_W103,  /* "Symmetric NAT (port-random)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W104,  /* "Test"  [p2p_stun.c] */
    LA_W105,  /* "timed out"  [p2p_signal_relay.c] */
    LA_W106,  /* "Timeout"  [p2p_stun.c] */
    LA_W107,  /* "Timeout (no response)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W108,  /* "TIMEOUT: Punch failed after"  [p2p_nat.c] */
    LA_W109,  /* "to"  [p2p_signal_pubsub.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p_stun.c] */
    LA_W110,  /* "UDP Blocked (STUN unreachable)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W111,  /* "UDP hole-punch probing remote candidates"  [p2p_ice.c] */
    LA_W112,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W113,  /* "Unknown ACK status"  [p2p_signal_relay.c] */
    LA_W114,  /* "Unsupported (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_W115,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W116,  /* "Waiting for peer"  [p2p_signal_relay.c] */
    LA_W117,  /* "while discarding"  [p2p_signal_relay.c] */
    LA_W118,  /* "while reading payload"  [p2p_signal_relay.c] */
    LA_W119,  /* "while reading sender"  [p2p_signal_relay.c] */
    LA_W120,  /* "will be retried by p2p_update()"  [p2p_ice.c] */
    LA_W121,  /* "with server"  [p2p_signal_compact.c] */
    LA_W122,  /* "yes"  [p2p_ice.c, p2p_signal_compact.c] */

    /* Strings (LA_S) */
    LA_S0,  /* "1 candidate to"  [p2p_ice.c] */
    LA_S1,  /* "[lan_punch] 跳过 NAT_PROBE，直接标记 NAT=OPEN"  [p2p_signal_compact.c] */
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
    LA_S12,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_S13,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_S14,  /* "COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_S15,  /* "Connection closed by server"  [p2p_signal_relay.c] */
    LA_S16,  /* "Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_S17,  /* "Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_S18,  /* "Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_S19,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S20,  /* "Detection completed"  [p2p_stun.c] */
    LA_S21,  /* "DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_S22,  /* "ERROR: No remote candidates to punch"  [p2p_nat.c] */
    LA_S23,  /* "Error: p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_S24,  /* "Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_S25,  /* "Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_S26,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_S27,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_S28,  /* "Failed to apply addr update candidate"  [p2p_signal_compact.c] */
    LA_S29,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_S30,  /* "Failed to send header"  [p2p_signal_relay.c] */
    LA_S31,  /* "Failed to send payload"  [p2p_signal_relay.c] */
    LA_S32,  /* "Failed to send target name"  [p2p_signal_relay.c] */
    LA_S33,  /* "forwarded"  [p2p_signal_relay.c] */
    LA_S34,  /* "from"  [p2p_ice.c] */
    LA_S35,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S36,  /* "Handshake complete"  [p2p_trans_mbedtls.c] */
    LA_S37,  /* "Host Candidate"  [p2p_ice.c] */
    LA_S38,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S39,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_S40,  /* "Local candidates:"  [p2p_signal_compact.c] */
    LA_S41,  /* "Mapped address"  [p2p_signal_compact.c] */
    LA_S42,  /* "NAT connection timeout, downgrading to relay mode"  [p2p.c] */
    LA_S43,  /* "need"  [p2p_stun.c] */
    LA_S44,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_S45,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_S46,  /* "offer with"  [p2p_signal_pubsub.c] */
    LA_S47,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_S48,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S49,  /* "P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_S50,  /* "P2P connection established"  [p2p.c] */
    LA_S51,  /* "P2P punch failed, switching to relay mode"  [p2p.c] */
    LA_S52,  /* "P2P punching in progress ..."  [p2p.c] */
    LA_S53,  /* "payload of message type"  [p2p_signal_relay.c] */
    LA_S54,  /* "priority"  [p2p_stun.c] */
    LA_S55,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_S56,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_S57,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_S58,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_S59,  /* "received"  [p2p_signal_relay.c] */
    LA_S60,  /* "Received ALIVE_ACK from server"  [p2p_signal_compact.c] */
    LA_S61,  /* "Received first PEER_INFO with session_id, enter ICE phase"  [p2p_signal_compact.c] */
    LA_S62,  /* "Received REGISTER_ACK with session_id already set, directly enter ICE phase"  [p2p_signal_compact.c] */
    LA_S63,  /* "relay"  [p2p_signal_compact.c] */
    LA_S64,  /* "Relay Candidate"  [p2p_ice.c] */
    LA_S65,  /* "Relay packet received but relay not enabled"  [p2p_signal_compact.c] */
    LA_S66,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_S67,  /* "request to"  [p2p_signal_relay.c] */
    LA_S68,  /* "Resend remaining candidates and FIN to peer"  [p2p_signal_compact.c] */
    LA_S69,  /* "resuming"  [p2p_signal_relay.c] */
    LA_S70,  /* "Same subnet confirmed, switching to LAN path"  [p2p.c] */
    LA_S71,  /* "SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_S72,  /* "sending ACK"  [p2p_route.c] */
    LA_S73,  /* "Sending FIN packet to peer"  [p2p.c] */
    LA_S74,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    LA_S75,  /* "Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_S76,  /* "Sent authentication request to peer"  [p2p_ice.c] */
    LA_S77,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_S78,  /* "Skipping LAN shortcut on --disable-lan-shortcut"  [p2p.c] */
    LA_S79,  /* "Skipping local Host candidates on --public-only"  [p2p.c] */
    LA_S80,  /* "Srflx Candidate"  [p2p_ice.c] */
    LA_S81,  /* "Starting internal thread"  [p2p.c] */
    LA_S82,  /* "Stopping internal thread"  [p2p.c] */
    LA_S83,  /* "STUN server"  [p2p_stun.c] */
    LA_S84,  /* "Test"  [p2p_stun.c] */
    LA_S85,  /* "to"  [p2p_tcp_punch.c, p2p_nat.c] */
    LA_S86,  /* "type"  [p2p_signal_pubsub.c] */
    LA_S87,  /* "Using"  [p2p_ice.c] */
    LA_S88,  /* "via local"  [p2p_route.c] */
    LA_S89,  /* "waiting for peer to come online"  [p2p_signal_relay.c] */

    /* Formats (LA_F) - Format strings for validation */
    LA_F0,  /* "  ... %s %d %s" (%s,%d,%s)  [p2p_ice.c] */
    LA_F1,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F2,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F3,  /* "%s %d" (%s,%d)  [p2p_signal_relay.c] */
    LA_F4,  /* "%s %d %s" (%s,%d,%s)  [p2p_ice.c, p2p_signal_relay.c] */
    LA_F5,  /* "%s %d, %s" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F6,  /* "%s %s" (%s,%s)  [p2p_signal_pubsub.c, p2p_turn.c, p2p_stun.c] */
    LA_F7,  /* "%s %s %d, %s" (%s,%s,%d,%s)  [p2p_tcp_punch.c] */
    LA_F8,  /* "%s %s %d/%d %s (elapsed: %llu ms)" (%s,%s,%d,%d,%s,%l)  [p2p_nat.c] */
    LA_F9,  /* "%s %s %s" (%s,%s,%s)  [p2p_stun.c] */
    LA_F10,  /* "%s %s %s %s" (%s,%s,%s,%s)  [p2p_ice.c, p2p_route.c] */
    LA_F11,  /* "%s %s %s %s %s:%d (%s=%d)" (%s,%s,%s,%s,%s,%d,%s,%d)  [p2p_stun.c] */
    LA_F12,  /* "%s %s %s (%s %s %s)" (%s,%s,%s,%s,%s,%s)  [p2p_stun.c] */
    LA_F13,  /* "%s %s %s (%s=%s)" (%s,%s,%s,%s,%s)  [p2p_ice.c] */
    LA_F14,  /* "%s %s %s! %s %s" (%s,%s,%s,%s,%s)  [p2p_stun.c] */
    LA_F15,  /* "%s %s %s: %s:%d" (%s,%s,%s,%s,%d)  [p2p_stun.c] */
    LA_F16,  /* "%s %s %s:%d probe=%s:%d -> %s" (%s,%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F17,  /* "%s %s '%s' (%d %s)" (%s,%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F18,  /* "%s %s: %s:%d (priority=0x%08x)" (%s,%s,%s,%d)  [p2p_ice.c] */
    LA_F19,  /* "%s %s:%d" (%s,%s,%d)  [p2p_turn.c, p2p_route.c] */
    LA_F20,  /* "%s %s:%d %s '%s'" (%s,%s,%d,%s,%s)  [p2p_signal_relay.c] */
    LA_F21,  /* "%s %s:%d (candidate %d)" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F22,  /* "%s %s:%d, %s" (%s,%s,%d,%s)  [p2p_route.c] */
    LA_F23,  /* "%s %s:%u (%s=%u)" (%s,%s,%u,%s,%u)  [p2p_turn.c] */
    LA_F24,  /* "%s %u %s" (%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F25,  /* "%s '%s'" (%s,%s)  [p2p_ice.c] */
    LA_F26,  /* "%s '%s' %s (%dms), %s" (%s,%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F27,  /* "%s '%s' %s (%s FORWARD), %s" (%s,%s,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F28,  /* "%s '%s' %s OFFER" (%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F29,  /* "%s '%s' (%d %s)" (%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F30,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F31,  /* "%s (%d %s)" (%s,%d,%s)  [p2p_ice.c] */
    LA_F32,  /* "%s (%llu ms), %s" (%s,%l,%s)  [p2p_nat.c] */
    LA_F33,  /* "%s (ret=%d), %s" (%s,%d,%s)  [p2p_ice.c] */
    LA_F34,  /* "%s (sid=%llu)" (%s,%l)  [p2p_signal_compact.c] */
    LA_F35,  /* "%s (status=%d, candidates_acked=%d)" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F36,  /* "%s 0x%x (%s 0x%x), %s" (%s,%x,%s,%x,%s)  [p2p_signal_relay.c] */
    LA_F37,  /* "%s :%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F38,  /* "%s PEER_INFO_ACK(seq=%u)" (%s,%u)  [p2p_signal_compact.c] */
    LA_F39,  /* "%s ack_seq=%u send_base=%u inflight=%d" (%s,%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F40,  /* "%s len=%d max=%d" (%s,%d,%d)  [p2p_trans_reliable.c] */
    LA_F41,  /* "%s round %d/%d" (%s,%d,%d)  [p2p_ice.c] */
    LA_F42,  /* "%s rto=%d win=%d" (%s,%d,%d)  [p2p_trans_reliable.c] */
    LA_F43,  /* "%s rtt=%dms srtt=%d rttvar=%d rto=%d" (%s,%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F44,  /* "%s send_count=%d" (%s,%d)  [p2p_trans_reliable.c] */
    LA_F45,  /* "%s seq=%u base=%u" (%s,%u,%u)  [p2p_trans_reliable.c] */
    LA_F46,  /* "%s seq=%u len=%d base=%u" (%s,%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F47,  /* "%s seq=%u len=%d inflight=%d" (%s,%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F48,  /* "%s! %s %s %s %s:%d%s" (%s,%s,%s,%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F49,  /* "%s, %s" (%s,%s)  [p2p_signal_relay.c] */
    LA_F50,  /* "%s, %s %d %s" (%s,%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F51,  /* "%s: %d %s" (%s,%d,%s)  [p2p_route.c] */
    LA_F52,  /* "%s: %d -> %s:%d" (%s,%d,%s,%d)  [p2p_ice.c, p2p_signal_relay.c] */
    LA_F53,  /* "%s: %s" (%s,%s)  [p2p_signal_compact.c, p2p_route.c] */
    LA_F54,  /* "%s: %s=%d, %s=%s:%d" (%s,%s,%d,%s,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F55,  /* "%s: keepalive ALIVE sent to %s:%d" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F56,  /* "%s:%04d: %s" (%s,%s)  [p2p_trans_mbedtls.c] */
    LA_F57,  /* "Append Host candidate: %s:%d" (%s,%d)  [p2p.c] */
    LA_F58,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F59,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F60,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F61,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F62,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F63,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F64,  /* "Failed to realloc memory for local candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_F65,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_trans_mbedtls.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_trans_sctp.c, p2p_trans_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h] */
    LA_F66,  /* "Failed to reserve remote candidates (base=%u cnt=%d)" (%u,%d)  [p2p_signal_compact.c] */
    LA_F67,  /* "Failed to reserve remote candidates (cnt=%d)" (%d)  [p2p_signal_compact.c] */
    LA_F68,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F69,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_trans_mbedtls.c] */
    LA_F70,  /* "Ignore ALIVE_ACK in state=%d" (%d)  [p2p_signal_compact.c] */
    LA_F71,  /* "Ignore NAT_PROBE_ACK seq=%u (expect=%d)" (%u,%d)  [p2p_signal_compact.c] */
    LA_F72,  /* "Ignore PEER_INFO_ACK for sid=%llu (local sid=%llu)" (%l,%l)  [p2p_signal_compact.c] */
    LA_F73,  /* "Ignore REGISTER_ACK in state=%d" (%d)  [p2p_signal_compact.c] */
    LA_F74,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F75,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F76,  /* "Invalid NAT_PROBE_ACK len=%d" (%d)  [p2p_signal_compact.c] */
    LA_F77,  /* "Invalid PEER_INFO notify: base=%u cand_cnt=%d flags=0x%02x" (%u,%d)  [p2p_signal_compact.c] */
    LA_F78,  /* "Invalid PEER_INFO payload: len=%d cand_cnt=%d" (%d,%d)  [p2p_signal_compact.c] */
    LA_F79,  /* "Invalid PEER_INFO seq=%u" (%u)  [p2p_signal_compact.c] */
    LA_F80,  /* "Invalid PEER_INFO seq=0: cand_cnt=%d flags=0x%02x" (%d)  [p2p_signal_compact.c] */
    LA_F81,  /* "Invalid PEER_INFO: state=%d len=%d" (%d,%d)  [p2p_signal_compact.c] */
    LA_F82,  /* "Invalid PEER_INFO_ACK ack_seq=%u" (%u)  [p2p_signal_compact.c] */
    LA_F83,  /* "Invalid PEER_INFO_ACK len=%d" (%d)  [p2p_signal_compact.c] */
    LA_F84,  /* "Invalid PEER_OFF len=%d" (%d)  [p2p_signal_compact.c] */
    LA_F85,  /* "NAT_PROBE already started (retries=%d)" (%d)  [p2p_signal_compact.c] */
    LA_F86,  /* "NAT_PROBE: %s" (%s)  [p2p_signal_compact.c] */
    LA_F87,  /* "NAT_PROBE: %s %d/%d %s %s:%d" (%s,%d,%d,%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F88,  /* "NAT_PROBE: %s %s:%d (1/%d)" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F89,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F90,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F91,  /* "PEER_OFF: sid=%llu peer disconnected, reset to REGISTERED" (%l)  [p2p_signal_compact.c] */
    LA_F92,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F93,  /* "REGISTERING: %s #%d (%d %s)..." (%s,%d,%d,%s)  [p2p_signal_compact.c] */
    LA_F94,  /* "REGISTER_ACK error: %s (status=%d)" (%s,%d)  [p2p_signal_compact.c] */
    LA_F95,  /* "REGISTER_ACK payload too short: %d" (%d)  [p2p_signal_compact.c] */
    LA_F96,  /* "REGISTER_ACK: peer_online=%d, max_cands=%d (%s=%s), %s=%s, public_addr=%s:%d, probe_port=%d" (%d,%d,%s,%s,%s,%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F97,  /* "Received %s for sid=%llu, len=%d" (%s,%l,%d)  [p2p_signal_compact.c] */
    LA_F98,  /* "Received COMPACT signaling packet type 0x%02X in non-COMPACT mode"  [p2p.c] */
    LA_F99,  /* "Received NAT_PROBE_ACK: probe_mapped=%s:%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F100,  /* "Received PEER_INFO(seq=%u, cand_cnt=%d, flags=0x%02x)" (%u,%d)  [p2p_signal_compact.c] */
    LA_F101,  /* "Received PEER_INFO_ACK for seq=%u" (%u)  [p2p_signal_compact.c] */
    LA_F102,  /* "Received PEER_OFF for sid=%llu" (%l)  [p2p_signal_compact.c] */
    LA_F103,  /* "Received unknown packet type: 0x%02X"  [p2p.c] */
    LA_F104,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F105,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F106,  /* "Relay sid mismatch: local=%llu pkt=%llu" (%l,%l)  [p2p_signal_compact.c] */
    LA_F107,  /* "Remote candidate sync complete (mask=0x%04x)"  [p2p_signal_compact.c] */
    LA_F108,  /* "Resend REGISTER (attempt %d)" (%d)  [p2p_signal_compact.c] */
    LA_F109,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F110,  /* "START: %s '%s' -> '%s' %s %s:%d (%d %s)" (%s,%s,%s,%s,%s,%d,%d,%s)  [p2p_signal_compact.c] */
    LA_F111,  /* "Same subnet detecting, sent ROUTE_PROBE to %s:%d" (%s,%d)  [p2p.c] */
    LA_F112,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F113,  /* "Sent UNREGISTER Pkt for local_peer_id=%s, remote_peer_id=%s" (%s,%s)  [p2p.c] */
    LA_F114,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F115,  /* "Session mismatch in PEER_INFO: local=%llu pkt=%llu" (%l,%l)  [p2p_signal_compact.c] */
    LA_F116,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F117,  /* "TIMEOUT: Max register attempts reached (%d)" (%d)  [p2p_signal_compact.c] */
    LA_F118,  /* "Unexpected PEER_INFO_ACK ack_seq=%u mask=0x%04x" (%u)  [p2p_signal_compact.c] */
    LA_F119,  /* "Unknown packet type: %u" (%u)  [p2p_signal_compact.c] */
    LA_F120,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F121,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F122,  /* "[DEBUG] %s %d %s %s %d" (%s,%d,%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F123,  /* "[DEBUG] %s received (ice_state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F124,  /* "[DEBUG] First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F125,  /* "[DEBUG] relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F126,  /* "[SCTP] received encapsulated packet, length %d" (%d)  [p2p_trans_sctp.c] */
    LA_F127,  /* "[SCTP] sending %d bytes" (%d)  [p2p_trans_sctp.c] */
    LA_F128,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F129,  /* "[Trickle] Probing updated candidate %s:%d" (%s,%d)  [p2p_signal_compact.c] */
    LA_F130,  /* "[lan_punch] 启动 PUNCH 流程 (Host 候选 %d 个)" (%d)  [p2p_ice.c] */
    LA_F131,  /* "[prflx] %s %s:%d (Peer Reflexive - symmetric NAT)" (%s,%s,%d)  [p2p_ice.c] */
    LA_F132,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F133,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F134,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_trans_mbedtls.c] */
    LA_F135,  /* "✓ %s %s %s:%d (%s=%u)" (%s,%s,%s,%d,%s,%u)  [p2p_stun.c] */
    LA_F136,  /* "✗ %s" (%s)  [p2p_stun.c] */
    LA_F137,  /* "连通性检查超时（已发送 %d 轮），放弃" (%d)  [p2p_ice.c] */
    LA_F138,  /* "重传 seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */

    LA_NUM = 352
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F0

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
