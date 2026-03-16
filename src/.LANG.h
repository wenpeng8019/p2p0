/*
 * 自动生成的语言 ID 枚举（由 i18n.sh 生成）
 *
 * 除「remove 操作」外请勿手动编辑，重新生成会覆盖所有改动。
 *
 * 条目状态:
 *   (无标记)  — active:   正常使用中，源文件中有对应的 LA_W/S/F 调用
 *   disabled  — disabled: 源文件扫描中未出现（如在未激活的 #ifdef 分支内），
 *                         ID 和字符串保留，宏重新启用后自动恢复为 active
 *   remove    — remove:   用户确认永久删除，下次生成时:
 *                           Debug  模式 → 该位置变为 _LA_N 占位空洞
 *                           Release 模式 → 该条目被完全移除
 *
 * 状态流转:
 *   active ──(扫描消失)──→ disabled ──(扫描重现)──→ active
 *                              │
 *                     (用户手动改为 remove)
 *                              ↓
 *                           remove ──(下次生成)──→ 删除
 *
 * 操作说明:
 *   若在枚举注释中看到 "disabled" 前缀，且确认该字符串不再需要，
 *   将注释中的 "disabled" 改为 "remove"，然后重新运行 i18n.sh 即可。
 *   示例:
 *     LA_F99,  // disabled "some old string"
 *     改为:
 *     LA_F99,  // remove "some old string"
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
    LA_W22,  /* "Undetectable (no STUN/probe configured)"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_W23,  /* "Unknown"  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
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
    LA_S33,  /* "Push remote cand<%s:%d> failed(OOM)\n"  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_S34,  /* "[SCTP] association established"  [p2p_trans_sctp.c] */
    LA_S35,  /* "[SCTP] usrsctp initialized, connecting..."  [p2p_trans_sctp.c] */
    LA_S36,  /* "[SCTP] usrsctp_socket failed"  [p2p_trans_sctp.c] */
    LA_S37,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S38,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S39,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S40,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S41,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S42,  /* "Push host cand<%s:%d> failed(OOM)\n"  [p2p_ice.c] */
    LA_S43,  /* "Push local cand<%s:%d> failed(OOM)\n"  [p2p.c] */

    /* Formats (LA_F) */
    LA_F44,  /* "  ... and %d more pairs" (%d)  [p2p_ice.c] */
    LA_F45,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F46,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F47,  /* "  [%d]<%s:%d> (type: %s)" (%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F48,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F49,  /* "%s NOTIFY: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F50,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F51,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F52,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F53,  /* "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F54,  /* "%s resent, %d/%d\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F55,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F56,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F57,  /* "%s sent to %s:%d, echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F58,  /* "%s sent, inst_id=%u, cands=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F59,  /* "%s sent, inst_id=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F60,  /* "%s sent, seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F61,  /* "%s sent, sid=%u, msg=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F62,  /* "%s sent, size=%d (ses_id=%llu)\n" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F63,  /* "%s sent, total=%d (ses_id=%llu)\n" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F64,  /* "%s seq=0: accepted cand_cnt=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F65,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F66,  /* "%s skipped: session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F67,  /* "%s, retry remaining candidates and FIN to peer\n" (%s)  [p2p_signal_compact.c] */
    LA_F68,  /* "%s, sent on %s\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F69,  /* "%s: %s timeout after %d retries (sid=%u)\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F70,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F71,  /* "%s: Peer addr changed -> %s:%d, retrying punch\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F72,  /* "%s: RPC complete (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F73,  /* "%s: RPC fail due to peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F74,  /* "%s: RPC fail due to relay timeout (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F75,  /* "%s: RPC finished (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F76,  /* "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F77,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F78,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F79,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F80,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F81,  /* "%s: accepted" (%s)  [p2p_nat.c] */
    LA_F82,  /* "%s: accepted (ses_id=%llu)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F83,  /* "%s: accepted (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F84,  /* "%s: accepted for ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F85,  /* "%s: accepted from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F86,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F87,  /* "%s: accepted sid=%u, msg=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F88,  /* "%s: accepted, len=%d (ses_id=%llu)\n" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F89,  /* "%s: accepted, probe_mapped=%s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F90,  /* "%s: accepted, public=%s:%d ses_id=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n" (%s,%s,%d,%l,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F91,  /* "%s: accepted, waiting for response (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F92,  /* "%s: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F93,  /* "%s: already connected, ignoring batch punch request" (%s)  [p2p_nat.c] */
    LA_F94,  /* "%s: bad payload(len=%d cand_cnt=%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F95,  /* "%s: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F96,  /* "%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F97,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F98,  /* "%s: discovered prflx cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F99,  /* "%s: duplicate request ignored (sid=%u, already processing)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F100,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F101,  /* "%s: entered, %s arrived after REGISTERED\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F102,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F103,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F104,  /* "%s: failed to RE-REGISTER after timeout\n" (%s)  [p2p_signal_compact.c] */
    LA_F105,  /* "%s: failed to send UNREGISTER before restart\n" (%s)  [p2p_signal_compact.c] */
    LA_F106,  /* "%s: ignored (relay not supported)\n" (%s)  [p2p_signal_compact.c] */
    LA_F107,  /* "%s: ignored for duplicated seq=%u, already acked\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F108,  /* "%s: ignored for seq=%u (expect=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F109,  /* "%s: ignored for ses_id=%llu (local ses_id=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F110,  /* "%s: ignored for sid=%u (current sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F111,  /* "%s: ignored in invalid state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F112,  /* "%s: ignored in state(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F113,  /* "%s: ignored in state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F114,  /* "%s: invalid ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F115,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F116,  /* "%s: invalid for non-relay req\n" (%s)  [p2p_signal_compact.c] */
    LA_F117,  /* "%s: invalid in non-COMPACT mode\n" (%s)  [p2p_signal_compact.c] */
    LA_F118,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F119,  /* "%s: invalid session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F120,  /* "%s: keep alive to %d reachable cand(s)" (%s,%d)  [p2p_nat.c] */
    LA_F121,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F122,  /* "%s: no remote candidates to punch" (%s)  [p2p_nat.c] */
    LA_F123,  /* "%s: no response for %llu ms, connection lost" (%s,%l)  [p2p_nat.c] */
    LA_F124,  /* "%s: old request ignored (sid=%u <= last_sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F125,  /* "%s: peer disconnected (ses_id=%llu), reset to REGISTERED\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F126,  /* "%s: peer online, proceeding to ICE\n" (%s)  [p2p_signal_compact.c] */
    LA_F127,  /* "%s: peer reachable via signaling (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F128,  /* "%s: punch remote cand[%d]<%s:%d> failed\n" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F129,  /* "%s: punching %d/%d candidates (elapsed: %llu ms)" (%s,%d,%d,%l)  [p2p_nat.c] */
    LA_F130,  /* "%s: punching additional cand<%s:%d>[%d] while connected" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F131,  /* "%s: punching remote cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F132,  /* "%s: received FIN from peer, marking NAT as CLOSED" (%s)  [p2p_nat.c] */
    LA_F133,  /* "%s: remote cand[%d]<%s:%d>, starting punch\n" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F134,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F135,  /* "%s: retry(%d/%d) probe\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F136,  /* "%s: retry(%d/%d) req (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F137,  /* "%s: retry(%d/%d) resp (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F138,  /* "%s: retry, (attempt %d/%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F139,  /* "%s: rx confirmed: peer->me path is UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F140,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F141,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F142,  /* "%s: sent, sid=%u, code=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F143,  /* "%s: session mismatch(local=%llu ack=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F144,  /* "%s: session mismatch(local=%llu pkt=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F145,  /* "%s: session mismatch(local=%llu, pkt=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F146,  /* "%s: session_id mismatch (recv=%llu, expect=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F147,  /* "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F148,  /* "%s: start punching all(%d) remote candidates" (%s,%d)  [p2p_nat.c] */
    LA_F149,  /* "%s: started, sending first probe\n" (%s)  [p2p_signal_compact.c] */
    LA_F150,  /* "%s: status error(%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F151,  /* "%s: sync complete (ses_id=%llu)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F152,  /* "%s: sync complete (ses_id=%llu, mask=0x%04x)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F153,  /* "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F154,  /* "%s: timeout after %d retries , type unknown\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F155,  /* "%s: timeout after %llu ms (ICE done), switching to RELAY" (%s,%l)  [p2p_nat.c] */
    LA_F156,  /* "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%s,%l,%d)  [p2p_nat.c] */
    LA_F157,  /* "%s: timeout, max(%d) attempts reached, reset to INIT\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F158,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F159,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F160,  /* "%s: push remote cand<%s:%d> failed(OOM), dropping" (%s,%s,%d)  [p2p_nat.c] */
    LA_F161,  /* "%s: trickled %d cand(s), seq=%u (ses_id=%llu)\n" (%s,%d,%u,%l)  [p2p_signal_compact.c] */
    LA_F162,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F163,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F164,  /* "%s: tx confirmed: me->peer path is UP (echoed seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F165,  /* "Duplicate remote cand<%s:%d> from signaling, skipped" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F166,  /* "%s: unexpected ack_seq=%u mask=0x%04x\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F167,  /* "%s: unexpected non-Srflx candidate at idx 0\n" (%s)  [p2p_signal_compact.c] */
    LA_F168,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F169,  /* "%s_ACK sent, seq=%u (ses_id=%llu)\n" (%s,%u,%l)  [p2p_signal_compact.c] */
    LA_F170,  /* "%s_ACK sent, sid=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F171,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F172,  /* "Added Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F173,  /* "% Added SIGNALING path to path manager"  [p2p.c] */
    LA_F174,  /* "% Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_F175,  /* disabled "Append Host candidate: %s:%d" */
    LA_F176,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F177,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F178,  /* "% BIO_new failed"  [p2p_dtls_openssl.c] */
    LA_F179,  /* "% Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_F180,  /* "% Bind failed"  [p2p_tcp_punch.c] */
    LA_F181,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F182,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F183,  /* "% COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_F184,  /* "% Close P2P UDP socket"  [p2p.c] */
    LA_F185,  /* "% Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_F186,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F187,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F188,  /* "Connected to server %s:%d as '%s'" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F189,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F190,  /* "% Connection closed by server"  [p2p_signal_relay.c] */
    LA_F191,  /* "% Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_F192,  /* "% Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_F193,  /* "% Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_F194,  /* "Connectivity checks timed out (sent %d rounds), giving up" (%d)  [p2p_ice.c] */
    LA_F195,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F196,  /* "% DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_F197,  /* "% DTLS handshake complete (MbedTLS)"  [p2p_dtls_mbedtls.c] */
    LA_F198,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F199,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F200,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F201,  /* "Discarded %d bytes payload of message type %d" (%d,%d)  [p2p_signal_relay.c] */
    LA_F202,  /* "Failed to allocate %u bytes" (%u)  [p2p_signal_relay.c] */
    LA_F203,  /* "% Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_F204,  /* "% Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_F205,  /* "% Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_F206,  /* "% Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_F207,  /* "% Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_F208,  /* "% Failed to allocate memory for session"  [p2p.c] */
    LA_F209,  /* "% Failed to build STUN request"  [p2p_stun.c] */
    LA_F210,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_udp.c, p2p_signal_pubsub.c, p2p_trans_pseudotcp.c, p2p_tcp_punch.c, p2p_thread.c, p2p_turn.c, p2p_ice.c, p2p_signal_compact.c, p2p_signal_relay.c, p2p.c, p2p_dtls_mbedtls.c, p2p_trans_sctp.c, p2p_path_manager.c, p2p_probe.c, p2p_dtls_openssl.c, p2p_crypto.c, p2p_route.c, p2p_stream.c, p2p_nat.c, p2p_trans_reliable.c, p2p_stun.c, p2p_internal.h, p2p_channel.c] */
    LA_F211,  /* "Failed to reserve remote candidates (base=%u cnt=%d)\n" (%u,%d)  [p2p_signal_compact.c] */
    LA_F212,  /* "Failed to reserve remote candidates (cnt=%d)\n" (%d)  [p2p_signal_compact.c] */
    LA_F213,  /* "% Failed to reserve remote candidates (cnt=1)\n"  [p2p_signal_compact.c] */
    LA_F214,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F215,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F216,  /* "% Failed to send header"  [p2p_signal_relay.c] */
    LA_F217,  /* "% Failed to send payload"  [p2p_signal_relay.c] */
    LA_F218,  /* "% Failed to send punch packet for new peer addr\n"  [p2p_signal_compact.c] */
    LA_F219,  /* "% Failed to send target name"  [p2p_signal_relay.c] */
    LA_F220,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F221,  /* "First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F222,  /* "Formed check list with %d candidate pairs" (%d)  [p2p_ice.c] */
    LA_F223,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F224,  /* "Gathered host cand<%s:%d> (priority=0x%08x)" (%s,%d)  [p2p_ice.c] */
    LA_F225,  /* "% Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_F226,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F227,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F228,  /* "Initialize network subsystem failed(%d)" (%d)  [p2p.c] */
    LA_F229,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F230,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F231,  /* "Invalid magic 0x%x (expected 0x%x), resetting" (%x,%x)  [p2p_signal_relay.c] */
    LA_F232,  /* "Invalid read state %d, resetting" (%d)  [p2p_signal_relay.c] */
    LA_F233,  /* "% Invalid signaling mode in configuration"  [p2p.c] */
    LA_F234,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F235,  /* "Marked old path (idx=%d) as FAILED due to addr change\n" (%d)  [p2p_signal_compact.c] */
    LA_F236,  /* "% NAT connection recovered, upgrading from RELAY to CONNECTED"  [p2p.c] */
    LA_F237,  /* "% NAT connection timeout, downgrading to relay mode"  [p2p.c] */
    LA_F238,  /* "% No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_F239,  /* "% No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_F240,  /* "Nomination successful! Using! Using %s path %s:%d%s" (%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F241,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F242,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F243,  /* "% OpenSSL requested but library not linked"  [p2p.c] */
    LA_F244,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F245,  /* "% P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_F246,  /* "% P2P connection established"  [p2p.c] */
    LA_F247,  /* "% P2P punch failed, adding relay path"  [p2p.c] */
    LA_F248,  /* "% P2P punching in progress ..."  [p2p.c] */
    LA_F249,  /* "PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u\n" (%d,%u)  [p2p_signal_compact.c] */
    LA_F250,  /* "% PEER_INFO(trickle): seq overflow, cannot trickle more\n"  [p2p_signal_compact.c] */
    LA_F251,  /* "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_F252,  /* "% PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_F253,  /* "% PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_F254,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F255,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F256,  /* "Passive peer learned remote ID '%s' from OFFER" (%s)  [p2p_signal_relay.c] */
    LA_F257,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F258,  /* "% Path recovered: switched to PUNCH"  [p2p.c] */
    LA_F259,  /* "% Path switch debounced, waiting for stability"  [p2p.c] */
    LA_F260,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F261,  /* "Peer '%s' is now online (FORWARD received), resuming" (%s)  [p2p_signal_relay.c] */
    LA_F262,  /* "Peer offline, cached %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F263,  /* "Peer online, forwarded %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F264,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F265,  /* "% PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_F266,  /* "REGISTERED: peer=%s\n" (%s)  [p2p_signal_compact.c] */
    LA_F267,  /* "% RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_F268,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F269,  /* "Received ACK (status=%d, candidates_acked=%d)" (%d,%d)  [p2p_signal_relay.c] */
    LA_F270,  /* "Received ACK pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%d,%u)  [p2p.c] */
    LA_F271,  /* "Received DATA pkt from %s:%d, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F272,  /* "% Received FIN packet, connection closed"  [p2p.c] */
    LA_F273,  /* "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F274,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F275,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F276,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F277,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F278,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F279,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F280,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F281,  /* "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)" (%s,%d)  [p2p_ice.c] */
    LA_F282,  /* "Recv New Remote Candidate<%s:%d> (type=%d)" (%s,%d,%d)  [p2p_ice.c] */
    LA_F283,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F284,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F285,  /* "Requested Relay Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F286,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p_signal_compact.c, p2p.c] */
    LA_F287,  /* "Requested Srflx Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F288,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F289,  /* "% SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_F290,  /* "% SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_F291,  /* "% SSL_new failed"  [p2p_dtls_openssl.c] */
    LA_F292,  /* "Selected path: PUNCH (idx=%d)" (%d)  [p2p.c] */
    LA_F293,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F294,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F295,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F296,  /* "% Sending FIN packet to peer before closing"  [p2p.c] */
    LA_F297,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F298,  /* "% Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_F299,  /* "Sent answer to '%s'" (%s)  [p2p_ice.c] */
    LA_F300,  /* "Sent answer to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F301,  /* "Sent connect request to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F302,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F303,  /* "% Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_F304,  /* "% Skipping local Host candidates on --public-only"  [p2p.c] */
    LA_F305,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F306,  /* "% Starting internal thread"  [p2p.c] */
    LA_F307,  /* "% Stopping internal thread"  [p2p.c] */
    LA_F308,  /* "% Storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_F309,  /* "% Switched to backup path: RELAY"  [p2p.c] */
    LA_F310,  /* "% Synced path after failover"  [p2p.c] */
    LA_F311,  /* "TURN 401 Unauthorized (realm=%s), authenticating..." (%s)  [p2p_turn.c] */
    LA_F312,  /* "TURN Allocate failed with error %d" (%d)  [p2p_turn.c] */
    LA_F313,  /* "TURN Allocated relay %s:%u (lifetime=%us)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F314,  /* "TURN CreatePermission failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F315,  /* "TURN CreatePermission for %s" (%s)  [p2p_turn.c] */
    LA_F316,  /* "TURN Data Indication from %s:%u (%d bytes)" (%s,%u,%d)  [p2p_turn.c] */
    LA_F317,  /* "TURN Refresh failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F318,  /* "TURN Refresh ok (lifetime=%us)" (%u)  [p2p_turn.c] */
    LA_F319,  /* "% TURN auth required but no credentials configured"  [p2p_turn.c] */
    LA_F320,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F321,  /* "% Test I: Timeout"  [p2p_stun.c] */
    LA_F322,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F323,  /* "% Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F324,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F325,  /* "% Test III: Timeout"  [p2p_stun.c] */
    LA_F326,  /* "Transport layer '%s' init failed, falling back to simple reliable" (%s)  [p2p.c] */
    LA_F327,  /* "UDP hole-punch probing remote candidates (%d candidates)" (%d)  [p2p_ice.c] */
    LA_F328,  /* "UDP hole-punch probing remote candidates round %d/%d" (%d,%d)  [p2p_ice.c] */
    LA_F329,  /* "Unknown ACK status %d" (%d)  [p2p_signal_relay.c] */
    LA_F330,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F331,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F332,  /* "Push remote candidate<%s:%d> (type=%d) failed(OOM)" (%s,%d,%d)  [p2p_ice.c] */
    LA_F333,  /* "% Using path: RELAY"  [p2p.c] */
    LA_F334,  /* "Waiting for peer '%s' timed out (%dms), giving up" (%s,%d)  [p2p_signal_relay.c] */
    LA_F335,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F336,  /* "% [OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    LA_F337,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    LA_F338,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F339,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F340,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F341,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F342,  /* "[SIGNALING] Failed to send candidates, will retry (ret=%d)" (%d)  [p2p_signal_relay.c] */
    LA_F343,  /* "[SIGNALING] Sent candidates (cached, peer offline) %d to %s" (%d,%s)  [p2p_signal_relay.c] */
    LA_F344,  /* "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)" (%d,%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F345,  /* "% [SIGNALING] Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_F346,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F347,  /* "[Trickle] Sent 1 candidate to %s (online=%s)" (%s,%s)  [p2p_ice.c] */
    LA_F348,  /* "% [Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_F349,  /* "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()" (%d)  [p2p_ice.c] */
    LA_F350,  /* "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F351,  /* "[UDP] %s recv from %s:%d, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F352,  /* "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F353,  /* "[UDP] %s recv from %s:%d, seq=%u, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F354,  /* "[UDP] %s recv from %s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F355,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F356,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F357,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F358,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F359,  /* "[UDP] %s_ACK send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F360,  /* "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F361,  /* "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F362,  /* "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F363,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F364,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F365,  /* "% p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_F366,  /* "recv error %d" (%d)  [p2p_signal_relay.c] */
    LA_F367,  /* "recv error %d while discarding" (%d)  [p2p_signal_relay.c] */
    LA_F368,  /* "recv error %d while reading payload" (%d)  [p2p_signal_relay.c] */
    LA_F369,  /* "recv error %d while reading sender" (%d)  [p2p_signal_relay.c] */
    LA_F370,  /* "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F371,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F372,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F373,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F374,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F375,  /* "starting NAT punch(Host candidate %d)" (%d)  [p2p_ice.c] */
    LA_F376,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    LA_F377,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F378,  /* "% ✗ Add Srflx candidate failed(OOM)"  [p2p_stun.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F44

#endif /* LANG_H__ */
