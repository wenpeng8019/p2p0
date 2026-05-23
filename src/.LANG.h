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
    LA_W6,  /* "punch"  [p2p_nat.c] */
    LA_W7,  /* "Restricted Cone NAT"  [p2p_internal.h] */
    LA_W8,  /* "retry"  [p2p_nat.c] */
    LA_W9,  /* "Symmetric NAT (port-random)"  [p2p_internal.h] */
    LA_W10,  /* "Timeout (no response)"  [p2p_internal.h] */
    LA_W11,  /* "UDP Blocked (STUN unreachable)"  [p2p_internal.h] */
    LA_W12,  /* "Undetectable (no STUN/probe configured)"  [p2p_internal.h] */
    LA_W13,  /* "Unknown"  [p2p_internal.h] */

    /* Strings (LA_S) */
    LA_S14,  /* "%s: address exchange failed: peer OFF"  [p2p_probe.c] */
    LA_S15,  /* "%s: address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S16,  /* "%s: already running, cannot trigger again"  [p2p_probe.c] */
    LA_S17,  /* "%s: peer is OFF"  [p2p_probe.c] */
    LA_S18,  /* "%s: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S19,  /* "%s: triggered on CONNECTED state (unnecessary)"  [p2p_probe.c] */
    LA_S20,  /* "%s: TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S21,  /* "[SCTP] association established"  [p2p_trans_sctp.c] */
    LA_S22,  /* "[SCTP] usrsctp initialized, connecting..."  [p2p_trans_sctp.c] */
    LA_S23,  /* "[SCTP] usrsctp_socket failed"  [p2p_trans_sctp.c] */
    LA_S24,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S25,  /* "Push local cand<%s:%d> failed(OOM)\n"  [p2p.c] */
    LA_S26,  /* "Push local IPv6 cand failed(OOM)\n"  [p2p.c] */
    LA_S27,  /* "resync for peer"  [p2p_signal_relay.c] */
    LA_S28,  /* "sync candidates"  [p2p_signal_relay.c] */
    LA_S29,  /* "waiting for peer"  [p2p_signal_relay.c] */
    LA_S30,  /* "waiting stun pending"  [p2p_signal_relay.c] */

    /* Formats (LA_F) */
    LA_F31,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F32,  /* "  [v6:%d] %s" (%d,%s)  [p2p_route.c] */
    LA_F33,  /* "%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n" (%s,%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F34,  /* "%s NOTIFY: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F35,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F36,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F37,  /* "%s NOTIFY: srflx addr update (disabled)\n" (%s)  [p2p_signal_compact.c] */
    LA_F38,  /* "%s accepted (ses_id=%u), sid=%u code=%u len=%u\n" (%s,%u,%u,%u,%u)  [p2p_signal_compact.c] */
    LA_F39,  /* "%s accepted (ses_id=%u), sid=%u msg=%u\n" (%s,%u,%u,%u)  [p2p_signal_compact.c] */
    LA_F40,  /* "%s accepted (ses_id=%u), sid=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F41,  /* "%s accepted (ses_id=%u), waiting for response (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F42,  /* "%s msg=0 accepted (ses_id=%u), echo reply sid=%u len=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F43,  /* "%s msg=0: echo reply (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F44,  /* "%s req (ses_id=%u), sid=%u msg=%u len=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_relay.c] */
    LA_F45,  /* "%s req accepted (ses_id=%u), sid=%u msg=%u\n" (%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F46,  /* "%s resent (ses_id=%u), (total=%d, err=%d)/%d\n" (%s,%u,%d,%d,%d)  [p2p_signal_compact.c] */
    LA_F47,  /* "%s resp (ses_id=%u), sid=%u code=%u len=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_relay.c] */
    LA_F48,  /* "%s sent (ses_id=%u), seq=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F49,  /* "%s sent (ses_id=%u), sid=%u msg=%u size=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F50,  /* "%s sent (ses_id=%u), sid=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F51,  /* "%s sent (ses_id=%u), total=%d, err=%d\n" (%s,%u,%d,%d)  [p2p_signal_compact.c] */
    LA_F52,  /* "%s sent (ses_id=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F53,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F54,  /* "%s sent to %s:%d (writable), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F55,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F56,  /* "%s sent via best path[%d] to %s:%d, echo_seq=%u" (%s,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F57,  /* "%s sent via signaling relay" (%s)  [p2p_nat.c] */
    LA_F58,  /* "%s sent, auth_key=%llu, remote='%.32s', cands=%d\n" (%s,%l,%d)  [p2p_signal_compact.c] */
    LA_F59,  /* "%s sent, inst_id=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F60,  /* "%s sent, name='%s' rid=%u\n" (%s,%s,%u)  [p2p_signal_relay.c] */
    LA_F61,  /* "%s sent, retry=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F62,  /* "%s sent, ses_id=%u sid=%u cand_base=%d, cand_cnt=%d fin=%d\n" (%s,%u,%u,%d,%d,%d)  [p2p_signal_relay.c] */
    LA_F63,  /* "%s sent, ses_id=%u\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F64,  /* "%s sent, target='%s' cand=%u\n" (%s,%s,%u)  [p2p_signal_relay.c] */
    LA_F65,  /* "%s sent\n" (%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F66,  /* "%s skipped: auth_key=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F67,  /* "%s throttled: awaiting READY\n" (%s)  [p2p_signal_relay.c] */
    LA_F68,  /* "%s trickle (ses_id=%u), cnt=%d, seq=%u \n" (%s,%u,%d,%u)  [p2p_signal_compact.c] */
    LA_F69,  /* "%s, retry remaining candidates and FIN to peer\n" (%s)  [p2p_signal_compact.c] */
    LA_F70,  /* "%s: %s timeout after %d retries (sid=%u)\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F71,  /* "%s: %s → %s (recv DATA)" (%s,%s,%s)  [p2p_nat.c] */
    LA_F72,  /* "%s: CONN ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F73,  /* "%s: CONN timeout after %llums" (%s,%l)  [p2p_nat.c] */
    LA_F74,  /* "%s: CONNECTED → LOST (no response %llums)\n" (%s,%l)  [p2p_nat.c] */
    LA_F75,  /* "%s: CONNECTING → %s (recv CONN)" (%s,%s)  [p2p_nat.c] */
    LA_F76,  /* "%s: CONNECTING → %s (recv CONN_ACK)" (%s,%s)  [p2p_nat.c] */
    LA_F77,  /* "%s: CONNECTING → CLOSED (timeout, no relay)" (%s)  [p2p_nat.c] */
    LA_F78,  /* "%s: CONN_ACK ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F79,  /* "%s: GET %s — empty or failed" (%s,%s)  [p2p_signal_pubsub.c] */
    LA_F80,  /* "%s: PATCH failed" (%s)  [p2p_signal_pubsub.c] */
    LA_F81,  /* "%s: PUNCHING → %s" (%s,%s)  [p2p_nat.c] */
    LA_F82,  /* "%s: PUNCHING → %s (peer CONNECTING)" (%s,%s)  [p2p_nat.c] */
    LA_F83,  /* "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)" (%s,%l,%s)  [p2p_nat.c] */
    LA_F84,  /* "%s: PUNCHING → CONNECTING (%s%s)" (%s,%s,%s)  [p2p_nat.c] */
    LA_F85,  /* "%s: PUNCHING → RELAY (peer CONN via signaling)" (%s)  [p2p_nat.c] */
    LA_F86,  /* "%s: PUNCHING → RELAY (peer CONNECTING)" (%s)  [p2p_nat.c] */
    LA_F87,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F88,  /* "%s: Peer addr changed -> %s:%d, retrying punch\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F89,  /* "%s: RELAY → CONNECTED (direct path recovered)" (%s)  [p2p_nat.c] */
    LA_F90,  /* "%s: RPC complete (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F91,  /* "%s: RPC fail due to peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F92,  /* "%s: RPC fail due to relay timeout (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F93,  /* "%s: RPC finished (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F94,  /* "%s: SIGNALING path enabled (server supports relay)\n" (%s)  [p2p_signal_relay.c] */
    LA_F95,  /* "%s: STUN ready → SYNCING" (%s)  [p2p_signal_pubsub.c] */
    LA_F96,  /* "%s: SUB gist empty, waiting" (%s)  [p2p_signal_pubsub.c] */
    LA_F97,  /* "%s: SUB gist not REG (content: %.20s...)" (%s)  [p2p_signal_pubsub.c] */
    LA_F98,  /* "%s: SUB heartbeat stale (%llds ago, threshold %ds), may be offline" (%s,%l,%d)  [p2p_signal_pubsub.c] */
    LA_F99,  /* "%s: SUB online (heartbeat %llds ago), early nat_punch" (%s,%l)  [p2p_signal_pubsub.c] */
    LA_F100,  /* "%s: SUB responded with %d candidates (ver=%d)" (%s,%d,%d)  [p2p_signal_pubsub.c] */
    LA_F101,  /* "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F102,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F103,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F104,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F105,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F106,  /* "%s: accepted" (%s)  [p2p_nat.c] */
    LA_F107,  /* "%s: accepted (ses_id=%u), peer=%s\n" (%s,%u,%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F108,  /* "%s: accepted (ses_id=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F109,  /* "%s: accepted as cand[%d], target=%s:%d" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F110,  /* "%s: accepted cand_cnt=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F111,  /* "%s: accepted for ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F112,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F113,  /* "%s: accepted, cand_max=%d%s relay=%s msg=%s\n" (%s,%d,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F114,  /* "%s: accepted, probe_mapped=%s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F115,  /* "%s: accepted, public=%s:%d auth_key=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n" (%s,%s,%d,%l,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F116,  /* "%s: accepted\n" (%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F117,  /* "%s: auth_key acquired, auto SYN0 sent\n" (%s)  [p2p_signal_relay.c] */
    LA_F118,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [p2p_signal_relay.c] */
    LA_F119,  /* "%s: bad payload len=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F120,  /* "%s: bad payload(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F121,  /* "%s: bad payload(%d)\n" (%s,%d)  [p2p_signal_relay.c, p2p_nat.c] */
    LA_F122,  /* "%s: bad payload(%d, type=%u)\n" (%s,%d,%u)  [p2p_signal_relay.c] */
    LA_F123,  /* "%s: bad payload(len=%d cand_cnt=%d)\n" (%s,%d,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F124,  /* "%s: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F125,  /* "%s: batch punch skip (state=%d, use trickle)" (%s,%d)  [p2p_nat.c] */
    LA_F126,  /* "%s: batch punch start (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F127,  /* "%s: batch punch: no cand, wait trickle" (%s)  [p2p_nat.c] */
    LA_F128,  /* "%s: cand[%d] payload too large for multi_session (%d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F129,  /* "%s: cand[%d]<%s:%d> send packet failed(%d)" (%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F130,  /* "%s: cannot read SUB gist %s" (%s,%s)  [p2p_signal_pubsub.c] */
    LA_F131,  /* "%s: complete (ses_id=%u), sid=%u code=%u\n" (%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F132,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F133,  /* "%s: duplicate remote cand<%s:%d> from signaling, skipped\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F134,  /* "%s: duplicate remote cand<%s:%d>, skipped\n" (%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F135,  /* "%s: duplicate request ignored (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F136,  /* "%s: duplicate request ignored (sid=%u, already processing)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F137,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F138,  /* "%s: entered early, %s arrived before SYN0\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F139,  /* "%s: entered, %s arrived\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F140,  /* "%s: entered, peer online in SYN0_ACK\n" (%s)  [p2p_signal_compact.c] */
    LA_F141,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F142,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F143,  /* "%s: fatal error code=%u, entering ERROR state\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F144,  /* "%s: heartbeat write failed" (%s)  [p2p_signal_pubsub.c] */
    LA_F145,  /* "%s: heartbeat written" (%s)  [p2p_signal_pubsub.c] */
    LA_F146,  /* "%s: ignored for duplicated seq=%u, already acked\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F147,  /* "%s: ignored for seq=%u (expect=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F148,  /* "%s: ignored for sid=%u (current sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F149,  /* "%s: ignored in invalid state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F150,  /* "%s: ignored in state=%d\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F151,  /* "%s: ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F152,  /* "%s: invalid ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F153,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F154,  /* "%s: invalid cand_cnt=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F155,  /* "%s: invalid for non-relay req\n" (%s)  [p2p_signal_compact.c] */
    LA_F156,  /* "%s: invalid payload len=%d (need 6)" (%s,%d)  [p2p_nat.c] */
    LA_F157,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F158,  /* "%s: invalid ses_id=%u\n" (%s,%u)  [p2p.c] */
    LA_F159,  /* "%s: invalid session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F160,  /* "%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_relay.c] */
    LA_F161,  /* "%s: keep-alive sent (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F162,  /* "%s: mailbox empty, waiting" (%s)  [p2p_signal_pubsub.c] */
    LA_F163,  /* "%s: missing session_id in payload\n" (%s)  [p2p_signal_relay.c] */
    LA_F164,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F165,  /* "%s: no DTLS context for CRYPTO pkt \n" (%s)  [p2p_nat.c] */
    LA_F166,  /* "%s: no pending request\n" (%s)  [p2p_signal_relay.c] */
    LA_F167,  /* "%s: no rpc request\n" (%s)  [p2p_signal_compact.c] */
    LA_F168,  /* "%s: no ses_id for multi session\n" (%s)  [p2p.c] */
    LA_F169,  /* "%s: no session for peer_id=%.*s (req_type=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F170,  /* "%s: no session for peer_id=%.*s\n" (%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F171,  /* "%s: no session for session_id=%u (req_type=%u)\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F172,  /* "%s: no session for session_id=%u\n" (%s,%u)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F173,  /* "%s: not connected, cannot send FIN" (%s)  [p2p_nat.c] */
    LA_F174,  /* "%s: not supported by server\n" (%s)  [p2p_signal_relay.c] */
    LA_F175,  /* "%s: offer %s (my gist=%s)" (%s,%s,%s)  [p2p_signal_pubsub.c] */
    LA_F176,  /* "%s: offer confirmed" (%s)  [p2p_signal_pubsub.c] */
    LA_F177,  /* "%s: offer overwritten by SUB heartbeat, resending" (%s)  [p2p_signal_pubsub.c] */
    LA_F178,  /* "%s: old request ignored (sid=%u <= last_sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F179,  /* "%s: path rx UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F180,  /* "%s: path tx UP" (%s)  [p2p_nat.c] */
    LA_F181,  /* "%s: path tx UP (echo seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F182,  /* "%s: path[%d] UP (%s:%d)" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F183,  /* "%s: path[%d] UP (recv DATA)" (%s,%d)  [p2p_nat.c] */
    LA_F184,  /* "%s: path[%d] relay UP" (%s,%d)  [p2p_nat.c] */
    LA_F185,  /* "%s: peer disconnected (ses_id=%u), reset to WAIT_PEER\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F186,  /* "%s: peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F187,  /* "%s: peer offline in SYN0_ACK, waiting for peer to come online\n" (%s)  [p2p_signal_compact.c] */
    LA_F188,  /* "%s: peer offline\n" (%s)  [p2p_signal_relay.c] */
    LA_F189,  /* "%s: peer online, starting NAT punch\n" (%s)  [p2p_signal_compact.c] */
    LA_F190,  /* "%s: peer reachable via signaling (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F191,  /* "%s: pkt payload exceeds limit (%d > %d)\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F192,  /* "%s: pkt recv (ses_id=%u), inner type=%u\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F193,  /* "%s: processed sid=%u synced=%d\n" (%s,%u,%d)  [p2p_signal_relay.c] */
    LA_F194,  /* "%s: promoted prflx cand[%d]<%s:%d> → %s\n" (%s,%d,%s,%d,%s)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F195,  /* "%s: protocol mismatch, recv PKT_ACK on trans=%s" (%s,%s)  [p2p_nat.c] */
    LA_F196,  /* "%s: published %d candidates (ver=%d)" (%s,%d,%d)  [p2p_signal_pubsub.c] */
    LA_F197,  /* "%s: publishing %d candidates (ver=%d) to local gist" (%s,%d,%d)  [p2p_signal_pubsub.c] */
    LA_F198,  /* "%s: punch cand[%d] %s:%d (%s)" (%s,%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F199,  /* "%s: punch remote cand[%d]<%s:%d> failed\n" (%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F200,  /* "%s: punch timeout, fallback punching using signaling relay" (%s)  [p2p_nat.c] */
    LA_F201,  /* "%s: punching %d/%d candidates (elapsed: %llu ms)" (%s,%d,%d,%l)  [p2p_nat.c] */
    LA_F202,  /* "%s: push remote cand<%s:%d> failed(OOM)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F203,  /* "%s: reaching alloc OOM" (%s)  [p2p_nat.c] */
    LA_F204,  /* "%s: reaching broadcast to %d cand(s), seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F205,  /* "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u" (%s,%d,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F206,  /* "%s: reaching cand[%d] via signaling relay, seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F207,  /* "%s: reaching enqueued: cand[%d], seq=%u, priority=%u" (%s,%d,%u,%u)  [p2p_nat.c] */
    LA_F208,  /* "%s: reaching relay via signaling FAILED (ret=%d), seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F209,  /* "%s: reaching relay via signaling SUCCESS, seq=%u" (%s,%u)  [p2p_nat.c] */
    LA_F210,  /* "%s: reaching updated: cand[%d], seq=%u->%u" (%s,%d,%u,%u)  [p2p_nat.c] */
    LA_F211,  /* "%s: ready to start session\n" (%s)  [p2p_signal_relay.c] */
    LA_F212,  /* "%s: received %d candidates (ver=%d) from %s" (%s,%d,%d,%s)  [p2p_signal_pubsub.c] */
    LA_F213,  /* "%s: received offer from %s (peer=%s) → SYNCING" (%s,%s,%s)  [p2p_signal_pubsub.c] */
    LA_F214,  /* "%s: received offer from %s (peer=%s) → WAIT_STUN" (%s,%s,%s)  [p2p_signal_pubsub.c] */
    LA_F215,  /* "%s: recorded peer conn_seq=%u for future CONN_ACK" (%s,%u)  [p2p_nat.c] */
    LA_F216,  /* "%s: recv (ses_id=%u), type=%u\n" (%s,%u,%u)  [p2p.c] */
    LA_F217,  /* "%s: recv from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F218,  /* "%s: relay busy, will retry\n" (%s)  [p2p_signal_relay.c] */
    LA_F219,  /* "%s: relay ready, flow control released\n" (%s)  [p2p_signal_relay.c] */
    LA_F220,  /* "%s: remote %s cand<%s:%d> (disabled)\n" (%s,%s,%s,%d)  [p2p_nat.c] */
    LA_F221,  /* "%s: remote %s cand[%d]<%s:%d> (disabled)\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F222,  /* "%s: remote %s cand[%d]<%s:%d> accepted\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c, p2p_nat.c] */
    LA_F223,  /* "%s: remote_cands[] full, skipped %d candidates\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F224,  /* "%s: renew session (local=%u pkt=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F225,  /* "%s: req_type=%u code=%u msg=%s\n" (%s,%u,%u,%s)  [p2p_signal_relay.c] */
    LA_F226,  /* "%s: req_type=%u code=%u\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F227,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F228,  /* "%s: retry(%d/%d) probe\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F229,  /* "%s: retry(%d/%d) req (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F230,  /* "%s: retry(%d/%d) resp (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F231,  /* "%s: retry, (attempt %d/%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F232,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F233,  /* "%s: send offer failed" (%s)  [p2p_signal_pubsub.c] */
    LA_F234,  /* "%s: sent (ses_id=%u), sid=%u code=%u size=%d\n" (%s,%u,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F235,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F236,  /* "%s: server rejected (no slot)\n" (%s)  [p2p_signal_compact.c] */
    LA_F237,  /* "%s: sess_id=%u req_type=%u code=%u msg=%s\n" (%s,%u,%u,%u,%s)  [p2p_signal_relay.c] */
    LA_F238,  /* "%s: sess_id=%u req_type=%u code=%u\n" (%s,%u,%u,%u)  [p2p_signal_relay.c] */
    LA_F239,  /* "%s: session established(st=%s peer=%s), %s\n" (%s,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F240,  /* "%s: session offer(st=%s peer=%s), %s\n" (%s,%s,%s,%s)  [p2p_signal_relay.c] */
    LA_F241,  /* "%s: session reset by peer(old=%u new=%u), %s\n" (%s,%u,%u,%s)  [p2p_signal_relay.c] */
    LA_F242,  /* "%s: session suspend(st=%s)\n" (%s,%s)  [p2p_signal_relay.c] */
    LA_F243,  /* "%s: session_id changed (old=%u new=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F244,  /* "%s: skip stale ver=0 from %s (previous session)" (%s,%s)  [p2p_signal_pubsub.c] */
    LA_F245,  /* "%s: skip stale ver=0 from SUB (previous session)" (%s)  [p2p_signal_pubsub.c] */
    LA_F246,  /* "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F247,  /* "%s: started, sending first probe\n" (%s)  [p2p_signal_compact.c] */
    LA_F248,  /* "%s: stun collection ready, auto SYNC sent\n" (%s)  [p2p_signal_relay.c] */
    LA_F249,  /* "%s: sync busy, will retry\n" (%s)  [p2p_signal_relay.c] */
    LA_F250,  /* "%s: sync complete (ses_id=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F251,  /* "%s: sync complete (ses_id=%u, mask=0x%04x)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F252,  /* "%s: sync confirm sid=%u synced=%d base=%d\n" (%s,%u,%d,%d)  [p2p_signal_relay.c] */
    LA_F253,  /* "%s: sync done, st=%s cands=%d\n" (%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F254,  /* "%s: sync done\n" (%s)  [p2p_signal_relay.c] */
    LA_F255,  /* "%s: sync0 srflx cand[%d]<%s:%d>%s\n" (%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F256,  /* "%s: timeout (sid=%u)\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F257,  /* "%s: timeout after %d retries , type unknown\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F258,  /* "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%s,%l,%d)  [p2p_nat.c] */
    LA_F259,  /* "%s: timeout, max(%d) attempts reached, reset to INIT\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F260,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F261,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F262,  /* "%s: trickle punch start" (%s)  [p2p_nat.c] */
    LA_F263,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F264,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F265,  /* "%s: unexpected ack_seq=%u mask=0x%04x\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F266,  /* "%s: unexpected non-srflx cand in sync0, treating as srflx\n" (%s)  [p2p_signal_compact.c] */
    LA_F267,  /* "%s: unexpected remote cand type %d, skipped\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F268,  /* "%s: unexpected s->id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F269,  /* "%s: unexpected type 0x%02x\n" (%s)  [p2p_nat.c] */
    LA_F270,  /* "%s: unknown target cand %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F271,  /* "%s: unsupported type 0x%02x\n" (%s)  [p2p_signal_relay.c] */
    LA_F272,  /* "%s: writing heartbeat (gist=%s) %s" (%s,%s,%s)  [p2p_signal_pubsub.c] */
    LA_F273,  /* "%s: → CLOSED (recv FIN)" (%s)  [p2p_nat.c] */
    LA_F274,  /* "%s: → READY" (%s)  [p2p_signal_pubsub.c] */
    LA_F275,  /* "%s: → SYNCING" (%s)  [p2p_signal_pubsub.c] */
    LA_F276,  /* "%s: → WAIT_STUN" (%s)  [p2p_signal_pubsub.c] */
    LA_F277,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F278,  /* "%s_ACK sent to %s:%d (try), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F279,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F280,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F281,  /* "BIO_new failed"  [p2p_dtls_openssl.c] */
    LA_F282,  /* "Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_F283,  /* "Bind failed"  [p2p_tcp_punch.c] */
    LA_F284,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F285,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F286,  /* "Buffer size < 2048 may be insufficient for full SDP"  [p2p_ice.c] */
    LA_F287,  /* "CONNECT PUB: target=%s/%s" (%s,%s)  [p2p_signal_pubsub.c] */
    LA_F288,  /* "CONNECT SUB: waiting for offer (gist=%s/%s)" (%s,%s)  [p2p_signal_pubsub.c] */
    LA_F289,  /* "CONNECT: instance not online yet"  [p2p_signal_pubsub.c] */
    LA_F290,  /* "Close P2P UDP socket"  [p2p.c] */
    LA_F291,  /* "Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_F292,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F293,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F294,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F295,  /* "DISCONNECT"  [p2p_signal_pubsub.c] */
    LA_F296,  /* "DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_F297,  /* "DTLS handshake complete (MbedTLS)"  [p2p_dtls_mbedtls.c] */
    LA_F298,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F299,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F300,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F301,  /* "Exported %d candidates to SDP (%d bytes)" (%d,%d)  [p2p_ice.c] */
    LA_F302,  /* "Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_F303,  /* "Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_F304,  /* "Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_F305,  /* "Failed to allocate memory for instance"  [p2p.c] */
    LA_F306,  /* "Failed to allocate memory for session"  [p2p.c] */
    LA_F307,  /* "Failed to build STUN request"  [p2p_stun.c] */
    LA_F308,  /* "Failed to parse SDP candidate line: %s" (%s)  [p2p_ice.c] */
    LA_F309,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_internal.h] */
    LA_F310,  /* "Failed to reserve remote candidates (base=%u cnt=%d)\n" (%u,%d)  [p2p_signal_compact.c] */
    LA_F311,  /* "Failed to reserve remote candidates (cnt=%d)\n" (%d)  [p2p_signal_compact.c] */
    LA_F312,  /* "Failed to reserve remote candidates (cnt=1)\n"  [p2p_signal_compact.c] */
    LA_F313,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F314,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F315,  /* "Failed to send Allocate Request: %d" (%d)  [p2p_turn.c] */
    LA_F316,  /* "Failed to send STUN request: %d" (%d)  [p2p_stun.c] */
    LA_F317,  /* "Failed to send Test I(alt), continue to Test III"  [p2p_stun.c] */
    LA_F318,  /* "Failed to send punch packet for new peer addr\n"  [p2p_signal_compact.c] */
    LA_F319,  /* "Failed to start TURN allocation"  [p2p.c] */
    LA_F320,  /* "Full SDP generation requires ice_ufrag and ice_pwd"  [p2p_ice.c] */
    LA_F321,  /* "Gathered Host candidate: %s:%d (priority=0x%08x)" (%s,%d)  [p2p.c] */
    LA_F322,  /* "Gathered Host6 candidate: %s:%d (priority=0x%08x)" (%s,%d)  [p2p.c] */
    LA_F323,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F324,  /* "Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_F325,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F326,  /* "ICE credentials generated: ufrag=%s" (%s)  [p2p.c] */
    LA_F327,  /* "ICE-STUN: no session for ufrag, dropped"  [p2p.c] */
    LA_F328,  /* "IPv6 socket open failed, IPv6 candidates disabled"  [p2p.c] */
    LA_F329,  /* "IPv6 socket opened [%s]:%d" (%s,%d)  [p2p.c] */
    LA_F330,  /* "Ignore %s pkt from %s:%d, not connected" (%s,%s,%d)  [p2p_nat.c] */
    LA_F331,  /* "Ignore %s pkt from %s:%d, not connecting" (%s,%s,%d)  [p2p_nat.c] */
    LA_F332,  /* "Ignore %s pkt from %s:%d, state=%d (not connected yet)" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F333,  /* "Ignore %s pkt from %s:%d, valid state(%d)" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F334,  /* "Ignore %s pkt from unknown path %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F335,  /* "Imported %d candidates from SDP" (%d)  [p2p_ice.c] */
    LA_F336,  /* "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)" (%s,%d,%s)  [p2p_ice.c] */
    LA_F337,  /* "Initialize network subsystem failed(%d)" (%d)  [p2p.c] */
    LA_F338,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F339,  /* "Invalid IP address: %s" (%s)  [p2p_ice.c] */
    LA_F340,  /* "Invalid remote_peer_id for %s mode" (%s)  [p2p.c] */
    LA_F341,  /* "Invalid signaling mode in configuration"  [p2p.c] */
    LA_F342,  /* "LOST recovery: NAT connected but no path available"  [p2p.c] */
    LA_F343,  /* "Local address detection done: %d IPv4, %d IPv6 address(es)" (%d,%d)  [p2p_route.c] */
    LA_F344,  /* "MSG RPC not supported by server\n"  [p2p_signal_compact.c] */
    LA_F345,  /* "NAT connected but no available path in path manager"  [p2p.c] */
    LA_F346,  /* "NAT detection skipped (skip_stun_test=true), Srflx gathered"  [p2p_stun.c] */
    LA_F347,  /* "No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_F348,  /* "No shared local route addresses available, host candidates skipped"  [p2p.c] */
    LA_F349,  /* "No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)"  [p2p_stun.c] */
    LA_F350,  /* "OFF"  [p2p_signal_pubsub.c] */
    LA_F351,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F352,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F353,  /* "OpenSSL requested but library not linked"  [p2p.c] */
    LA_F354,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F355,  /* "PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_F356,  /* "PUBSUB mode: all candidate types (host/srflx/relay) disabled, no candidates possible"  [p2p.c] */
    LA_F357,  /* "PUBSUB mode: both IPv4 and IPv6 disabled, no candidates possible"  [p2p.c] */
    LA_F358,  /* "PUBSUB online failed(%d)" (%d)  [p2p.c] */
    LA_F359,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F360,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F361,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F362,  /* "Path switch debounced, waiting for stability"  [p2p.c] */
    LA_F363,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F364,  /* "PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_F365,  /* "REG to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F366,  /* "REG to PUBSUB signaling (Gist: %s)" (%s)  [p2p.c] */
    LA_F367,  /* "REG to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F368,  /* "REG: auth_key acquired, auto SYN0 sent\n"  [p2p_signal_compact.c] */
    LA_F369,  /* "REG: local_gist=%s peer=%s" (%s,%s)  [p2p_signal_pubsub.c] */
    LA_F370,  /* "REG: peer_id cannot contain '/' (got \"%s\")" (%s)  [p2p_signal_pubsub.c] */
    LA_F371,  /* "RELAY path but TURN not allocated"  [p2p_channel.c] */
    LA_F372,  /* "RELAY path but TURN not allocated (dtls)"  [p2p_channel.c] */
    LA_F373,  /* "RELAY recovery: NAT connected but no path available"  [p2p.c] */
    LA_F374,  /* "RELAY sent (ses_id=%u), type=0x%02x seq=%u flags=0x%02x" (%u,%u)  [p2p_signal_compact.c] */
    LA_F375,  /* "RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_F376,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F377,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F378,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F379,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F380,  /* "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F381,  /* "Recv %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_nat.c] */
    LA_F382,  /* "Recv ICE-STUN Binding Request from candidate %d (%s:%d)" (%d,%s,%d)  [p2p_nat.c] */
    LA_F383,  /* "Recv ICE-STUN Binding Response from candidate %d (%s:%d)" (%d,%s,%d)  [p2p_nat.c] */
    LA_F384,  /* "Recv ICE-STUN from %s:%d, upsert prflx failed" (%s,%d)  [p2p_nat.c] */
    LA_F385,  /* "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F386,  /* "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d" (%s,%d)  [p2p_nat.c] */
    LA_F387,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F388,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p.c] */
    LA_F389,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F390,  /* "Reuse Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p.c] */
    LA_F391,  /* "Reuse STUN Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p.c] */
    LA_F392,  /* "SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_F393,  /* "SDP REMOTE: %s cand<%s:%d> (disabled)" (%s,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F394,  /* "SDP REMOTE: %s cand[%d]<%s:%d> accepted" (%s,%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F395,  /* "SDP export buffer overflow"  [p2p_ice.c] */
    LA_F396,  /* "SDP import failed or empty"  [p2p_signal_pubsub.c] */
    LA_F397,  /* "SIGNALING path but signaling relay not available"  [p2p_channel.c] */
    LA_F398,  /* "SIGNALING path enabled (server supports relay)\n"  [p2p_signal_compact.c] */
    LA_F399,  /* "SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_F400,  /* "SSL_new failed"  [p2p_dtls_openssl.c] */
    LA_F401,  /* "STUN collecting to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F402,  /* "STUN resources released (no active sessions)"  [p2p.c] */
    LA_F403,  /* "SYN0: retry, (attempt %d/%d)\n" (%d,%d)  [p2p_signal_compact.c] */
    LA_F404,  /* "SYN0: timeout, max(%d) attempts reached, reset to INIT\n" (%d)  [p2p_signal_compact.c] */
    LA_F405,  /* "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n" (%d,%u)  [p2p_signal_compact.c] */
    LA_F406,  /* "SYNC(trickle): seq overflow, cannot trickle more\n"  [p2p_signal_compact.c] */
    LA_F407,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F408,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F409,  /* "Sending FIN packet to peer before closing"  [p2p.c] */
    LA_F410,  /* "Sending OFF packet to COMPACT signaling server"  [p2p.c] */
    LA_F411,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F412,  /* "Sending Test I(alt) to CHANGED-ADDRESS"  [p2p_stun.c] */
    LA_F413,  /* "Sending Test II with CHANGE-REQUEST(IP+PORT)"  [p2p_stun.c] */
    LA_F414,  /* "Sending Test III with CHANGE-REQUEST(PORT only)"  [p2p_stun.c] */
    LA_F415,  /* "Skipping Host Candidate gathering (disabled)"  [p2p.c] */
    LA_F416,  /* "Start COMPACT session failed(%d)" (%d)  [p2p.c] */
    LA_F417,  /* "Start PUBSUB session failed(%d)" (%d)  [p2p.c] */
    LA_F418,  /* "Start RELAY session failed(%d)" (%d)  [p2p.c] */
    LA_F419,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F420,  /* "Starting COMPACT session with %s" (%s)  [p2p.c] */
    LA_F421,  /* "Starting RELAY session with %s" (%s)  [p2p.c] */
    LA_F422,  /* "Starting internal thread"  [p2p.c] */
    LA_F423,  /* "State: LOST → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F424,  /* "State: RELAY → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F425,  /* "State: → CONNECTED, path[%d]" (%d)  [p2p.c] */
    LA_F426,  /* "State: → ERROR (punch timeout, no relay available)"  [p2p.c] */
    LA_F427,  /* "State: → LOST (all paths failed)"  [p2p.c] */
    LA_F428,  /* "State: → PUNCHING"  [p2p.c] */
    LA_F429,  /* "State: → RELAY, path[%d]" (%d)  [p2p.c] */
    LA_F430,  /* "Stopping internal thread"  [p2p.c] */
    LA_F431,  /* "TURN 401 Unauthorized (realm=%s), authenticating..." (%s)  [p2p_turn.c] */
    LA_F432,  /* "TURN Allocate failed with error %d" (%d)  [p2p_turn.c] */
    LA_F433,  /* "TURN Allocated relay %s:%u (lifetime=%us)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F434,  /* "TURN CreatePermission failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F435,  /* "TURN CreatePermission for %s" (%s)  [p2p_turn.c] */
    LA_F436,  /* "TURN Data Indication from %s:%u (%d bytes)" (%s,%u,%d)  [p2p_turn.c] */
    LA_F437,  /* "TURN Refresh failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F438,  /* "TURN Refresh ok (lifetime=%us)" (%u)  [p2p_turn.c] */
    LA_F439,  /* "TURN auth required but no credentials configured"  [p2p_turn.c] */
    LA_F440,  /* "Test I(alt): Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F441,  /* "Test I(alt): Timeout"  [p2p_stun.c] */
    LA_F442,  /* "Test I: Changed address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F443,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F444,  /* "Test I: Timeout"  [p2p_stun.c] */
    LA_F445,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F446,  /* "Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F447,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F448,  /* "Test III: Timeout"  [p2p_stun.c] */
    LA_F449,  /* "Transport layer '%s' init failed, falling back to simple reliable" (%s)  [p2p.c] */
    LA_F450,  /* "Unknown candidate type: %s" (%s)  [p2p_ice.c] */
    LA_F451,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F452,  /* "WebRTC candidate export buffer overflow"  [p2p_ice.c] */
    LA_F453,  /* "[C] %s recv, len=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F454,  /* "[C] %s recv, seq=%u, flags=0x%02x, len=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F455,  /* "[C] %s recv, seq=%u, len=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F456,  /* "[C] %s recv\n" (%s)  [p2p_signal_compact.c] */
    LA_F457,  /* "[C] %s send failed(%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F458,  /* "[C] %s send to port:%d failed(%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F459,  /* "[C] %s send to port:%d, seq=%u, flags=0, len=0\n" (%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F460,  /* "[C] %s send, seq=0, flags=0x%02x, len=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F461,  /* "[C] Unknown pkt type 0x%02x, len=%d\n" (%d)  [p2p_signal_compact.c] */
    LA_F462,  /* "[C] relay payload too large: %d" (%d)  [p2p_signal_compact.c] */
    LA_F463,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F464,  /* "[OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    LA_F465,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    LA_F466,  /* "[R] %s recv, len=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F467,  /* "[R] %s timeout\n" (%s)  [p2p_signal_relay.c] */
    LA_F468,  /* "[R] %s%s qsend failed(OOM)\n" (%s,%s)  [p2p_signal_relay.c] */
    LA_F469,  /* "[R] %s%s qsend(%d), len=%u\n" (%s,%s,%d,%u)  [p2p_signal_relay.c] */
    LA_F470,  /* "[R] Connecting to %s:%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F471,  /* "[R] Disconnected, back to REG state\n"  [p2p_signal_relay.c] */
    LA_F472,  /* "[R] Failed to create TCP socket\n"  [p2p_signal_relay.c] */
    LA_F473,  /* "[R] Failed to set socket non-blocking\n"  [p2p_signal_relay.c] */
    LA_F474,  /* "[R] TCP connect failed(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F475,  /* "[R] TCP connect select failed(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F476,  /* "[R] TCP connected immediately, sending REG\n"  [p2p_signal_relay.c] */
    LA_F477,  /* "[R] TCP connected, sending REG\n"  [p2p_signal_relay.c] */
    LA_F478,  /* "[R] TCP connection closed by peer\n"  [p2p_signal_relay.c] */
    LA_F479,  /* "[R] TCP connection closed during send\n"  [p2p_signal_relay.c] */
    LA_F480,  /* "[R] TCP recv error(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F481,  /* "[R] TCP send error(%d)\n" (%d)  [p2p_signal_relay.c] */
    LA_F482,  /* "[R] Unknown proto type %d\n" (%d)  [p2p_signal_relay.c] */
    LA_F483,  /* "[R] payload size %u exceeds limit %u\n" (%u,%u)  [p2p_signal_relay.c] */
    LA_F484,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F485,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F486,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F487,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F488,  /* "[ST:%s] peer went offline, waiting for reconnect\n" (%s)  [p2p_signal_relay.c] */
    LA_F489,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F490,  /* "connection closed by peer"  [p2p.c] */
    LA_F491,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F492,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F493,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F494,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F495,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F496,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    LA_F497,  /* "✓ Gathered Srflx Candidate %s:%d, priority=%u (ses_id=%u)" (%s,%d,%u,%u)  [p2p_stun.c] */
    LA_F498,  /* "✗ Add Srflx candidate failed(OOM)"  [p2p_stun.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F31

#endif /* LANG_H__ */
