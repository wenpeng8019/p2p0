/*
 * 自动生成的语言 ID 枚举（由 i18n 工具生成）
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
 *   将注释中的 "disabled" 改为 "remove"，然后重新运行 i18n 工具即可。
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
    LA_W2,  /* "Detecting..."  [p2p_internal.h] */
    LA_W3,  /* "Full Cone NAT"  [p2p_internal.h] */
    LA_W4,  /* "Open Internet (No NAT)"  [p2p_internal.h] */
    LA_W5,  /* "Port Restricted Cone NAT"  [p2p_internal.h] */
    LA_W6,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W7,  /* "punch"  [p2p_nat.c] */
    LA_W8,  /* "Restricted Cone NAT"  [p2p_internal.h] */
    LA_W9,  /* "retry"  [p2p_nat.c] */
    LA_W10,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W11,  /* "Symmetric NAT (port-random)"  [p2p_internal.h] */
    LA_W12,  /* "Timeout (no response)"  [p2p_internal.h] */
    LA_W13,  /* "UDP Blocked (STUN unreachable)"  [p2p_internal.h] */
    LA_W14,  /* "Undetectable (no STUN/probe configured)"  [p2p_internal.h] */
    LA_W15,  /* "Unknown"  [p2p_internal.h] */

    /* Strings (LA_S) */
    LA_S16,  /* "%s: address exchange failed: peer OFFLINE"  [p2p_probe.c] */
    LA_S17,  /* "%s: address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S18,  /* "%s: already running, cannot trigger again"  [p2p_probe.c] */
    LA_S19,  /* "%s: peer is OFFLINE"  [p2p_probe.c] */
    LA_S20,  /* "%s: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S21,  /* "%s: triggered on CONNECTED state (unnecessary)"  [p2p_probe.c] */
    LA_S22,  /* "%s: TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S23,  /* "[SCTP] association established"  [p2p_trans_sctp.c] */
    LA_S24,  /* "[SCTP] usrsctp initialized, connecting..."  [p2p_trans_sctp.c] */
    LA_S25,  /* "[SCTP] usrsctp_socket failed"  [p2p_trans_sctp.c] */
    LA_S26,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S27,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S28,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S29,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S30,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S31,  /* "Push local cand<%s:%d> failed(OOM)\n"  [p2p.c] */
    LA_S32,  /* "Push remote cand<%s:%d> failed(OOM)\n"  [p2p_signal_pubsub.c] */
    LA_S33,  /* "resync candidates"  [p2p_signal_relay.c] */
    LA_S34,  /* "sync candidates"  [p2p_signal_relay.c] */
    LA_S35,  /* "waiting for peer"  [p2p_signal_relay.c] */

    /* Formats (LA_F) */
    LA_F36,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F37,  /* "%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n" (%s,%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F38,  /* "%s NOTIFY: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F39,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F40,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F41,  /* "%s NOTIFY: srflx addr update (disabled)\n" (%s)  [p2p_signal_compact.c] */
    LA_F42,  /* "%s accepted (ses_id=%u), sid=%u code=%u len=%u\n" (%s,%u,%u,%u,%u)  [p2p_signal_compact.c] */
    LA_F43,  /* "%s accepted (ses_id=%u), sid=%u msg=%u\n" (%s,%u,%u,%u)  [p2p_signal_compact.c] */
    LA_F44,  /* "%s accepted (ses_id=%u), sid=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F45,  /* "%s accepted (ses_id=%u), waiting for response (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F46,  /* "%s msg=0 accepted (ses_id=%u), echo reply sid=%u len=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F47,  /* "%s msg=0: echo reply (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F48,  /* "%s req (ses_id=%u), sid=%u msg=%u len=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_relay.c] */
    LA_F49,  /* "%s req accepted (ses_id=%u), sid=%u msg=%u\n" (%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F50,  /* "%s resent (ses_id=%u), (total=%d, err=%d)/%d\n" (%s,%u,%d,%d,%d)  [p2p_signal_compact.c] */
    LA_F51,  /* "%s resp (ses_id=%u), sid=%u code=%u len=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_relay.c] */
    LA_F52,  /* "%s sent (ses_id=%u), seq=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F53,  /* "%s sent (ses_id=%u), sid=%u msg=%u size=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F54,  /* "%s sent (ses_id=%u), sid=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F55,  /* "%s sent (ses_id=%u), total=%d, err=%d\n" (%s,%u,%d,%d)  [p2p_signal_compact.c] */
    LA_F56,  /* "%s sent (ses_id=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F57,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F58,  /* "%s sent to %s:%d (writable), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F59,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F60,  /* "%s sent via best path[%d] to %s:%d, echo_seq=%u" (%s,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F61,  /* "%s sent via signaling relay" (%s)  [p2p_nat.c] */
    LA_F62,  /* "%s sent, auth_key=%llu, remote='%.32s', cands=%d\n" (%s,%l,%d)  [p2p_signal_compact.c] */
    LA_F63,  /* "%s sent, inst_id=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F64,  /* "%s sent, name='%s' rid=%u\n" (%s,%s,%u)  [p2p_signal_relay.c] */
    LA_F65,  /* "%s sent, retry=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F66,  /* "%s sent, ses_id=%u cand_base=%d, cand_cnt=%d fin=%d\n" (%s,%u,%d,%d,%d)  [p2p_signal_relay.c] */
    LA_F67,  /* "%s sent, ses_id=%u\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F68,  /* "%s sent, target='%s' cand=%u\n" (%s,%s,%u)  [p2p_signal_relay.c] */
    LA_F69,  /* "%s sent\n" (%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F70,  /* "%s skipped: auth_key=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F71,  /* "%s throttled: awaiting READY\n" (%s)  [p2p_signal_relay.c] */
    LA_F72,  /* "%s trickle (ses_id=%u), cnt=%d, seq=%u \n" (%s,%u,%d,%u)  [p2p_signal_compact.c] */
    LA_F73,  /* "%s, retry remaining candidates and FIN to peer\n" (%s)  [p2p_signal_compact.c] */
    LA_F74,  /* "%s: %s timeout after %d retries (sid=%u)\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F75,  /* "%s: %s → %s (recv DATA)" (%s,%s,%s)  [p2p_nat.c] */
    LA_F76,  /* "%s: CONN ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F77,  /* "%s: CONN timeout after %llums" (%s,%l)  [p2p_nat.c] */
    LA_F78,  /* "%s: CONNECTED → LOST (no response %llums)\n" (%s,%l)  [p2p_nat.c] */
    LA_F79,  /* "%s: CONNECTING → %s (recv CONN)" (%s,%s)  [p2p_nat.c] */
    LA_F80,  /* "%s: CONNECTING → %s (recv CONN_ACK)" (%s,%s)  [p2p_nat.c] */
    LA_F81,  /* "%s: CONNECTING → CLOSED (timeout, no relay)" (%s)  [p2p_nat.c] */
    LA_F82,  /* "%s: CONN_ACK ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F83,  /* "%s: PUNCHING → %s" (%s,%s)  [p2p_nat.c] */
    LA_F84,  /* "%s: PUNCHING → %s (peer CONNECTING)" (%s,%s)  [p2p_nat.c] */
    LA_F85,  /* "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)" (%s,%l,%s)  [p2p_nat.c] */
    LA_F86,  /* "%s: PUNCHING → CONNECTING (%s%s)" (%s,%s,%s)  [p2p_nat.c] */
    LA_F87,  /* "%s: PUNCHING → RELAY (peer CONNECTING)" (%s)  [p2p_nat.c] */
    LA_F88,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F89,  /* "%s: Peer addr changed -> %s:%d, retrying punch\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F90,  /* "%s: RELAY → CONNECTED (direct path recovered)" (%s)  [p2p_nat.c] */
    LA_F91,  /* "%s: RPC complete (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F92,  /* "%s: RPC fail due to peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F93,  /* "%s: RPC fail due to relay timeout (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F94,  /* "%s: RPC finished (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F95,  /* "%s: SIGNALING path enabled (server supports relay)\n" (%s)  [p2p_signal_relay.c] */
    LA_F96,  /* "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F97,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F98,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F99,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F100,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F101,  /* "%s: accepted" (%s)  [p2p_nat.c] */
    LA_F102,  /* "%s: accepted (ses_id=%u), peer=%s\n" (%s,%u,%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F103,  /* "%s: accepted (ses_id=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F104,  /* "%s: accepted as cand[%d], target=%s:%d" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F105,  /* "%s: accepted cand_cnt=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F106,  /* "%s: accepted for ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F107,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F108,  /* "%s: accepted, cand_max=%d%s relay=%s msg=%s\n" (%s,%d,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F109,  /* "%s: accepted, probe_mapped=%s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F110,  /* "%s: accepted, public=%s:%d auth_key=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n" (%s,%s,%d,%l,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F111,  /* "%s: accepted\n" (%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F112,  /* "%s: auth_key acquired, auto SYNC0 sent\n" (%s)  [p2p_signal_relay.c] */
    LA_F113,  /* "%s: auth_key acquired, waiting stun pending\n" (%s)  [p2p_signal_relay.c] */
    LA_F114,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [p2p_signal_relay.c] */
    LA_F115,  /* "%s: bad payload len=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F116,  /* "%s: bad payload(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F117,  /* "%s: bad payload(%d)\n" (%s,%d)  [p2p_signal_relay.c, p2p_nat.c] */
    LA_F118,  /* "%s: bad payload(len=%d cand_cnt=%d)\n" (%s,%d,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F119,  /* "%s: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F120,  /* "%s: batch punch skip (state=%d, use trickle)" (%s,%d)  [p2p_nat.c] */
    LA_F121,  /* "%s: batch punch start (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F122,  /* "%s: batch punch: no cand, wait trickle" (%s)  [p2p_nat.c] */
    LA_F123,  /* "%s: cand[%d] payload too large for multi_session (%d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F124,  /* "%s: cand[%d]<%s:%d> send packet failed(%d)" (%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F125,  /* "%s: complete (ses_id=%u), sid=%u code=%u\n" (%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F126,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F127,  /* "%s: duplicate remote cand<%s:%d> from signaling, skipped\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F128,  /* "%s: duplicate remote cand<%s:%d>, skipped\n" (%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F129,  /* "%s: duplicate request ignored (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F130,  /* "%s: duplicate request ignored (sid=%u, already processing)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F131,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F132,  /* "%s: entered early, %s arrived before SYNC0\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F133,  /* "%s: entered, %s arrived\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F134,  /* "%s: entered, peer online in SYNC0_ACK\n" (%s)  [p2p_signal_compact.c] */
    LA_F135,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F136,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F137,  /* "%s: fatal error code=%u, entering ERROR state\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F138,  /* "%s: ignored for duplicated seq=%u, already acked\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F139,  /* "%s: ignored for seq=%u (expect=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F140,  /* "%s: ignored for sid=%u (current sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F141,  /* "%s: ignored in invalid state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F142,  /* "%s: ignored in state=%d\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F143,  /* "%s: ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F144,  /* "%s: invalid ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F145,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F146,  /* "%s: invalid cand_cnt=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F147,  /* "%s: invalid for non-relay req\n" (%s)  [p2p_signal_compact.c] */
    LA_F148,  /* "%s: invalid payload len=%d (need 6)" (%s,%d)  [p2p_nat.c] */
    LA_F149,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F150,  /* "%s: invalid ses_id=%u\n" (%s,%u)  [p2p.c] */
    LA_F151,  /* "%s: invalid session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F152,  /* "%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_relay.c] */
    LA_F153,  /* "%s: keep-alive sent (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F154,  /* "%s: missing session_id in payload\n" (%s)  [p2p_signal_relay.c] */
    LA_F155,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F156,  /* "%s: no DTLS context for CRYPTO pkt \n" (%s)  [p2p_nat.c] */
    LA_F157,  /* "%s: no pending request\n" (%s)  [p2p_signal_relay.c] */
    LA_F158,  /* "%s: no rpc request\n" (%s)  [p2p_signal_compact.c] */
    LA_F159,  /* "%s: no ses_id for multi session\n" (%s)  [p2p.c] */
    LA_F160,  /* "%s: no session for peer_id=%.*s (req_type=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F161,  /* "%s: no session for peer_id=%.*s\n" (%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F162,  /* "%s: no session for session_id=%u (req_type=%u)\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F163,  /* "%s: no session for session_id=%u\n" (%s,%u)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F164,  /* "%s: not connected, cannot send FIN" (%s)  [p2p_nat.c] */
    LA_F165,  /* "%s: not supported by server\n" (%s)  [p2p_signal_relay.c] */
    LA_F166,  /* "%s: old request ignored (sid=%u <= last_sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F167,  /* "%s: path rx UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F168,  /* "%s: path tx UP" (%s)  [p2p_nat.c] */
    LA_F169,  /* "%s: path tx UP (echo seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F170,  /* "%s: path[%d] UP (%s:%d)" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F171,  /* "%s: path[%d] UP (recv DATA)" (%s,%d)  [p2p_nat.c] */
    LA_F172,  /* "%s: path[%d] relay UP" (%s,%d)  [p2p_nat.c] */
    LA_F173,  /* "%s: peer disconnected (ses_id=%u), reset to WAIT_PEER\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F174,  /* "%s: peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F175,  /* "%s: peer offline in SYNC0_ACK, waiting for peer to come online\n" (%s)  [p2p_signal_compact.c] */
    LA_F176,  /* "%s: peer offline\n" (%s)  [p2p_signal_relay.c] */
    LA_F177,  /* "%s: peer online, starting NAT punch\n" (%s)  [p2p_signal_compact.c] */
    LA_F178,  /* "%s: peer reachable via signaling (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F179,  /* "%s: pkt payload exceeds limit (%d > %d)\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F180,  /* "%s: pkt recv (ses_id=%u), inner type=%u\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F181,  /* "%s: processed, synced=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F182,  /* "%s: promoted prflx cand[%d]<%s:%d> → %s\n" (%s,%d,%s,%d,%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F183,  /* "%s: protocol mismatch, recv PKT_ACK on trans=%s" (%s,%s)  [p2p_nat.c] */
    LA_F184,  /* "%s: punch cand[%d] %s:%d (%s)" (%s,%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F185,  /* "%s: punch remote cand[%d]<%s:%d> failed\n" (%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F186,  /* "%s: punch timeout, fallback punching using signaling relay" (%s)  [p2p_nat.c] */
    LA_F187,  /* "%s: punching %d/%d candidates (elapsed: %llu ms)" (%s,%d,%d,%l)  [p2p_nat.c] */
    LA_F188,  /* "%s: push remote cand<%s:%d> failed(OOM)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F189,  /* "%s: reaching alloc OOM" (%s)  [p2p_nat.c] */
    LA_F190,  /* "%s: reaching broadcast to %d cand(s), seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F191,  /* "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u" (%s,%d,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F192,  /* "%s: reaching cand[%d] via signaling relay, seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F193,  /* "%s: reaching enqueued: cand[%d], seq=%u, priority=%u" (%s,%d,%u,%u)  [p2p_nat.c] */
    LA_F194,  /* "%s: reaching relay via signaling FAILED (ret=%d), seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F195,  /* "%s: reaching relay via signaling SUCCESS, seq=%u" (%s,%u)  [p2p_nat.c] */
    LA_F196,  /* "%s: reaching updated: cand[%d], seq=%u->%u" (%s,%d,%u,%u)  [p2p_nat.c] */
    LA_F197,  /* "%s: ready to start session\n" (%s)  [p2p_signal_relay.c] */
    LA_F198,  /* "%s: recorded peer conn_seq=%u for future CONN_ACK" (%s,%u)  [p2p_nat.c] */
    LA_F199,  /* "%s: recv (ses_id=%u), type=%u\n" (%s,%u,%u)  [p2p.c] */
    LA_F200,  /* "%s: recv from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F201,  /* "%s: relay busy, will retry\n" (%s)  [p2p_signal_relay.c] */
    LA_F202,  /* "%s: relay ready, flow control released\n" (%s)  [p2p_signal_relay.c] */
    LA_F203,  /* "%s: remote %s cand<%s:%d> (disabled)\n" (%s,%s,%s,%d)  [p2p_nat.c] */
    LA_F204,  /* "%s: remote %s cand[%d]<%s:%d> (disabled)\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F205,  /* "%s: remote %s cand[%d]<%s:%d> accepted\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c, p2p_nat.c] */
    LA_F206,  /* "%s: remote_cands[] full, skipped %d candidates\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F207,  /* "%s: renew session (local=%u pkt=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F208,  /* "%s: req_type=%u code=%u msg=%s\n" (%s,%u,%u,%s)  [p2p_signal_relay.c] */
    LA_F209,  /* "%s: req_type=%u code=%u\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F210,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F211,  /* "%s: retry(%d/%d) probe\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F212,  /* "%s: retry(%d/%d) req (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F213,  /* "%s: retry(%d/%d) resp (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F214,  /* "%s: retry, (attempt %d/%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F215,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F216,  /* "%s: sent (ses_id=%u), sid=%u code=%u size=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F217,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F218,  /* "%s: server rejected (no slot)\n" (%s)  [p2p_signal_compact.c] */
    LA_F219,  /* "%s: sess_id=%u req_type=%u code=%u msg=%s\n" (%s,%u,%u,%u,%s)  [p2p_signal_relay.c] */
    LA_F220,  /* "%s: sess_id=%u req_type=%u code=%u\n" (%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F221,  /* "%s: session established(st=%s peer=%s), %s\n" (%s,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F222,  /* "%s: session offer(st=%s peer=%s), waiting for peer\n" (%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F223,  /* "%s: session reset by peer(st=%s old=%u new=%u), %s\n" (%s,%s,%u,%u,%s)  [p2p_signal_relay.c] */
    LA_F224,  /* "%s: session suspend(st=%s)\n" (%s,%s)  [p2p_signal_relay.c] */
    LA_F225,  /* "%s: session_id changed (old=%u new=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F226,  /* "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F227,  /* "%s: started, sending first probe\n" (%s)  [p2p_signal_compact.c] */
    LA_F228,  /* "%s: sync ack confirmed cnt=%d exceeds unacked cnt=%d\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F229,  /* "%s: sync busy, will retry\n" (%s)  [p2p_signal_relay.c] */
    LA_F230,  /* "%s: sync complete (ses_id=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F231,  /* "%s: sync complete (ses_id=%u, mask=0x%04x)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F232,  /* "%s: sync done, st=%s cands=%d\n" (%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F233,  /* "%s: sync done\n" (%s)  [p2p_signal_relay.c] */
    LA_F234,  /* "%s: sync fin ack, but cand synced cnt not match sent cnt (cand=%d synced=%d)\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F235,  /* "%s: sync forwarded, confirmed=%d synced=%d\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F236,  /* "%s: sync0 srflx cand[%d]<%s:%d>%s\n" (%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F237,  /* "%s: syncable ready, auto SYNC0 sent\n" (%s)  [p2p_signal_relay.c] */
    LA_F238,  /* "%s: timeout (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F239,  /* "%s: timeout after %d retries , type unknown\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F240,  /* "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%s,%l,%d)  [p2p_nat.c] */
    LA_F241,  /* "%s: timeout, max(%d) attempts reached, reset to INIT\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F242,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F243,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F244,  /* "%s: trickle punch start" (%s)  [p2p_nat.c] */
    LA_F245,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F246,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F247,  /* "%s: unexpected ack_seq=%u mask=0x%04x\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F248,  /* "%s: unexpected non-srflx cand in sync0, treating as srflx\n" (%s)  [p2p_signal_compact.c] */
    LA_F249,  /* "%s: unexpected remote cand type %d, skipped\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F250,  /* "%s: unexpected s->id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F251,  /* "%s: unexpected type 0x%02x\n" (%s)  [p2p_nat.c] */
    LA_F252,  /* "%s: unknown target cand %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F253,  /* "%s: unsupported type 0x%02x\n" (%s)  [p2p_signal_relay.c] */
    LA_F254,  /* "%s: → CLOSED (recv FIN)" (%s)  [p2p_nat.c] */
    LA_F255,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F256,  /* "%s_ACK sent to %s:%d (try), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F257,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F258,  /* "Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_F259,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F260,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F261,  /* "BIO_new failed"  [p2p_dtls_openssl.c] */
    LA_F262,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_F263,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_F264,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F265,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F266,  /* "Buffer size < 2048 may be insufficient for full SDP"  [p2p_ice.c] */
    LA_F267,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_F268,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_F269,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F270,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F271,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F272,  /* "DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_F273,  /* "DTLS handshake complete (MbedTLS)"  [p2p_dtls_mbedtls.c] */
    LA_F274,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F275,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F276,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F277,  /* "Duplicate remote cand<%s:%d> from signaling, skipped" (%s,%d)  [p2p_signal_pubsub.c] */
    LA_F278,  /* "Exported %d candidates to SDP (%d bytes)" (%d,%d)  [p2p_ice.c] */
    LA_F279,  /* "Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_F280,  /* "Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_F281,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_F282,  /* "Failed to allocate memory for instance"  [p2p.c] */
    LA_F283,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_F284,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_F285,  /* "Failed to parse SDP candidate line: %s" (%s)  [p2p_ice.c] */
    LA_F286,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_internal.h] */
    LA_F287,  /* "Failed to reserve remote candidates (base=%u cnt=%d)\n" (%u,%d)  [p2p_signal_compact.c] */
    LA_F288,  /* "Failed to reserve remote candidates (cnt=%d)\n" (%d)  [p2p_signal_compact.c] */
    LA_F289,  /* "Failed to reserve remote candidates (cnt=1)\n"  [p2p_signal_compact.c] */
    LA_F290,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F291,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F292,  /* "Failed to send Allocate Request: %d" (%d)  [p2p_turn.c] */
    LA_F293,  /* "Failed to send STUN request: %d" (%d)  [p2p_stun.c] */
    LA_F294,  /* "Failed to send Test I(alt), continue to Test III"  [p2p_stun.c] */
    LA_F295,  /* "Failed to send punch packet for new peer addr\n"  [p2p_signal_compact.c] */
    LA_F296,  /* "Failed to start TURN allocation"  [p2p.c] */
    LA_F297,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F298,  /* "Full SDP generation requires ice_ufrag and ice_pwd"  [p2p_ice.c] */
    LA_F299,  /* "Gathered Host candidate: %s:%d (priority=0x%08x)" (%s,%d)  [p2p.c] */
    LA_F300,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F301,  /* "Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_F302,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F303,  /* "Ignore %s pkt from %s:%d, not connected" (%s,%s,%d)  [p2p_nat.c] */
    LA_F304,  /* "Ignore %s pkt from %s:%d, not connecting" (%s,%s,%d)  [p2p_nat.c] */
    LA_F305,  /* "Ignore %s pkt from %s:%d, state=%d (not connected yet)" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F306,  /* "Ignore %s pkt from %s:%d, valid state(%d)" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F307,  /* "Ignore %s pkt from unknown path %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F308,  /* "Imported %d candidates from SDP" (%d)  [p2p_ice.c] */
    LA_F309,  /* "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)" (%s,%d,%s)  [p2p_ice.c] */
    LA_F310,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F311,  /* "Initialize network subsystem failed(%d)" (%d)  [p2p.c] */
    LA_F312,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F313,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F314,  /* "Invalid IP address: %s" (%s)  [p2p_ice.c] */
    LA_F315,  /* "Invalid remote_peer_id for %s mode" (%s)  [p2p.c] */
    LA_F316,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_F317,  /* "LOST recovery: NAT connected but no path available"  [p2p.c] */
    LA_F318,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F319,  /* "Login to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F320,  /* "Login to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F321,  /* "MSG RPC not supported by server\n"  [p2p_signal_compact.c] */
    LA_F322,  /* "NAT connected but no available path in path manager"  [p2p.c] */
    LA_F323,  /* "NAT detection skipped (skip_stun_test=true), Srflx gathered"  [p2p_stun.c] */
    LA_F324,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_F325,  /* "No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_F326,  /* "No shared local route addresses available, host candidates skipped"  [p2p.c] */
    LA_F327,  /* "No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)"  [p2p_stun.c] */
    LA_F328,  /* "ONLINE: auth_key acquired, auto SYNC0 sent\n"  [p2p_signal_compact.c] */
    LA_F329,  /* "ONLINE: auth_key acquired, waiting stun pending\n"  [p2p_signal_compact.c] */
    LA_F330,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F331,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F332,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_F333,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F334,  /* "PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_F335,  /* "PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_F336,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_F337,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F338,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F339,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F340,  /* "Path switch debounced, waiting for stability"  [p2p.c] */
    LA_F341,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F342,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F343,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_F344,  /* "RELAY path but TURN not allocated"  [p2p_channel.c] */
    LA_F345,  /* "RELAY path but TURN not allocated (dtls)"  [p2p_channel.c] */
    LA_F346,  /* "RELAY recovery: NAT connected but no path available"  [p2p.c] */
    LA_F347,  /* "RELAY sent (ses_id=%u), type=0x%02x seq=%u flags=0x%02x" (%u,%u)  [p2p_signal_compact.c] */
    LA_F348,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_F349,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F350,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F351,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F352,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F353,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F354,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F355,  /* "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F356,  /* "Recv %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_nat.c] */
    LA_F357,  /* "Recv ICE-STUN Binding Request from candidate %d (%s:%d)" (%d,%s,%d)  [p2p_nat.c] */
    LA_F358,  /* "Recv ICE-STUN Binding Response from candidate %d (%s:%d)" (%d,%s,%d)  [p2p_nat.c] */
    LA_F359,  /* "Recv ICE-STUN from %s:%d, upsert prflx failed" (%s,%d)  [p2p_nat.c] */
    LA_F360,  /* "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F361,  /* "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d" (%s,%d)  [p2p_nat.c] */
    LA_F362,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F363,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p.c] */
    LA_F364,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F365,  /* "Reuse Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p.c] */
    LA_F366,  /* "Reuse STUN Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p.c] */
    LA_F367,  /* "SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_F368,  /* "SDP export buffer overflow"  [p2p_ice.c] */
    LA_F369,  /* "SIGNALING path but signaling relay not available"  [p2p_channel.c] */
    LA_F370,  /* "SIGNALING path enabled (server supports relay)\n"  [p2p_signal_compact.c] */
    LA_F371,  /* "SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_F372,  /* "SSL_new failed"  [p2p_dtls_openssl.c] */
    LA_F373,  /* "STUN collecting to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F374,  /* "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n" (%d,%u)  [p2p_signal_compact.c] */
    LA_F375,  /* "SYNC(trickle): seq overflow, cannot trickle more\n"  [p2p_signal_compact.c] */
    LA_F376,  /* "SYNC0: retry, (attempt %d/%d)\n" (%d,%d)  [p2p_signal_compact.c] */
    LA_F377,  /* "SYNC0: timeout, max(%d) attempts reached, reset to INIT\n" (%d)  [p2p_signal_compact.c] */
    LA_F378,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F379,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F380,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    LA_F381,  /* "Sending OFFLINE packet to COMPACT signaling server"  [p2p.c] */
    LA_F382,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F383,  /* "Sending Test I(alt) to CHANGED-ADDRESS"  [p2p_stun.c] */
    LA_F384,  /* "Sending Test II with CHANGE-REQUEST(IP+PORT)"  [p2p_stun.c] */
    LA_F385,  /* "Sending Test III with CHANGE-REQUEST(PORT only)"  [p2p_stun.c] */
    LA_F386,  /* "Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_F387,  /* "Skipping Host Candidate gathering (disabled)"  [p2p.c] */
    LA_F388,  /* "Start COMPACT session failed(%d)" (%d)  [p2p.c] */
    LA_F389,  /* "Start RELAY session failed(%d)" (%d)  [p2p.c] */
    LA_F390,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F391,  /* "Starting COMPACT session with %s" (%s)  [p2p.c] */
    LA_F392,  /* "Starting RELAY session with %s" (%s)  [p2p.c] */
    LA_F393,  /* "Starting internal thread"  [p2p.c] */
    LA_F394,  /* "State: LOST → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F395,  /* "State: RELAY → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F396,  /* "State: → CONNECTED, path[%d]" (%d)  [p2p.c] */
    LA_F397,  /* "State: → ERROR (punch timeout, no relay available)"  [p2p.c] */
    LA_F398,  /* "State: → LOST (all paths failed)"  [p2p.c] */
    LA_F399,  /* "State: → PUNCHING"  [p2p.c] */
    LA_F400,  /* "State: → RELAY, path[%d]" (%d)  [p2p.c] */
    LA_F401,  /* "Stopping internal thread"  [p2p.c] */
    LA_F402,  /* "TURN 401 Unauthorized (realm=%s), authenticating..." (%s)  [p2p_turn.c] */
    LA_F403,  /* "TURN Allocate failed with error %d" (%d)  [p2p_turn.c] */
    LA_F404,  /* "TURN Allocated relay %s:%u (lifetime=%us)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F405,  /* "TURN CreatePermission failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F406,  /* "TURN CreatePermission for %s" (%s)  [p2p_turn.c] */
    LA_F407,  /* "TURN Data Indication from %s:%u (%d bytes)" (%s,%u,%d)  [p2p_turn.c] */
    LA_F408,  /* "TURN Refresh failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F409,  /* "TURN Refresh ok (lifetime=%us)" (%u)  [p2p_turn.c] */
    LA_F410,  /* "TURN auth required but no credentials configured"  [p2p_turn.c] */
    LA_F411,  /* "Test I(alt): Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F412,  /* "Test I(alt): Timeout"  [p2p_stun.c] */
    LA_F413,  /* "Test I: Changed address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F414,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F415,  /* "Test I: Timeout"  [p2p_stun.c] */
    LA_F416,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F417,  /* "Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F418,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F419,  /* "Test III: Timeout"  [p2p_stun.c] */
    LA_F420,  /* "Transport layer '%s' init failed, falling back to simple reliable" (%s)  [p2p.c] */
    LA_F421,  /* "Unknown candidate type: %s" (%s)  [p2p_ice.c] */
    LA_F422,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F423,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F424,  /* "WebRTC candidate export buffer overflow"  [p2p_ice.c] */
    LA_F425,  /* "[C] %s recv, len=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F426,  /* "[C] %s recv, seq=%u, flags=0x%02x, len=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F427,  /* "[C] %s recv, seq=%u, len=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F428,  /* "[C] %s recv\n" (%s)  [p2p_signal_compact.c] */
    LA_F429,  /* "[C] %s send failed(%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F430,  /* "[C] %s send to port:%d failed(%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F431,  /* "[C] %s send to port:%d, seq=%u, flags=0, len=0\n" (%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F432,  /* "[C] %s send, seq=0, flags=0x%02x, len=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F433,  /* "[C] Unknown pkt type 0x%02x, len=%d\n" (%d)  [p2p_signal_compact.c] */
    LA_F434,  /* "[C] relay payload too large: %d" (%d)  [p2p_signal_compact.c] */
    LA_F435,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F436,  /* "[OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    LA_F437,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    LA_F438,  /* "[R] %s recv, len=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F439,  /* "[R] %s timeout\n" (%s)  [p2p_signal_relay.c] */
    LA_F440,  /* "[R] %s%s qsend failed(OOM)\n" (%s,%s)  [p2p_signal_relay.c] */
    LA_F441,  /* "[R] %s%s qsend(%d), len=%u\n" (%s,%s,%d,%u)  [p2p_signal_relay.c] */
    LA_F442,  /* "[R] Connecting to %s:%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F443,  /* "[R] Disconnected, back to ONLINE state\n"  [p2p_signal_relay.c] */
    LA_F444,  /* "[R] Failed to create TCP socket\n"  [p2p_signal_relay.c] */
    LA_F445,  /* "[R] Failed to set socket non-blocking\n"  [p2p_signal_relay.c] */
    LA_F446,  /* "[R] TCP connect failed(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F447,  /* "[R] TCP connect select failed(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F448,  /* "[R] TCP connected immediately, sending ONLINE\n"  [p2p_signal_relay.c] */
    LA_F449,  /* "[R] TCP connected, sending ONLINE\n"  [p2p_signal_relay.c] */
    LA_F450,  /* "[R] TCP connection closed by peer\n"  [p2p_signal_relay.c] */
    LA_F451,  /* "[R] TCP connection closed during send\n"  [p2p_signal_relay.c] */
    LA_F452,  /* "[R] TCP recv error(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F453,  /* "[R] TCP send error(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F454,  /* "[R] Unknown proto type %d\n" (%d)  [p2p_signal_relay.c] */
    LA_F455,  /* "[R] payload size %u exceeds limit %u\n" (%u,%u)  [p2p_signal_relay.c] */
    LA_F456,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F457,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F458,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F459,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F460,  /* "[ST:%s] peer went offline, waiting for reconnect\n" (%s)  [p2p_signal_relay.c] */
    LA_F461,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F462,  /* "connection closed by peer"  [p2p.c] */
    LA_F463,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F464,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F465,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F466,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F467,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F468,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    LA_F469,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F470,  /* "✗ Add Srflx candidate failed(OOM)"  [p2p_stun.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F36

#endif /* LANG_H__ */
