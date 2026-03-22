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
    LA_W3,  /* "Detecting..."  [p2p_internal.h] */
    LA_W4,  /* "Full Cone NAT"  [p2p_internal.h] */
    LA_W5,  /* "no (cached)"  [p2p_ice.c] */
    LA_W6,  /* "Open Internet (No NAT)"  [p2p_internal.h] */
    LA_W7,  /* "Port Restricted Cone NAT"  [p2p_internal.h] */
    LA_W8,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W9,  /* "Published"  [p2p_signal_pubsub.c] */
    LA_W10,  /* "punch"  [p2p_nat.c] */
    LA_W11,  /* "Received signal from"  [p2p_signal_relay.c] */
    LA_W12,  /* "Resent"  [p2p_signal_pubsub.c] */
    LA_W13,  /* "Restricted Cone NAT"  [p2p_internal.h] */
    LA_W14,  /* "retry"  [p2p_nat.c] */
    LA_W15,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W16,  /* "Symmetric NAT (port-random)"  [p2p_internal.h] */
    LA_W17,  /* "Timeout (no response)"  [p2p_internal.h] */
    LA_W18,  /* "UDP Blocked (STUN unreachable)"  [p2p_internal.h] */
    LA_W19,  /* "Undetectable (no STUN/probe configured)"  [p2p_internal.h] */
    LA_W20,  /* "Unknown"  [p2p_internal.h] */
    LA_W21,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W22,  /* "yes"  [p2p_ice.c] */

    /* Strings (LA_S) */
    LA_S23,  /* "%s: address exchange failed: peer OFFLINE"  [p2p_probe.c] */
    LA_S24,  /* "%s: address exchange success, sending UDP probe"  [p2p_probe.c] */
    LA_S25,  /* "%s: already running, cannot trigger again"  [p2p_probe.c] */
    LA_S26,  /* "%s: peer is OFFLINE"  [p2p_probe.c] */
    LA_S27,  /* "%s: peer is online, waiting echo"  [p2p_probe.c] */
    LA_S28,  /* "%s: triggered on CONNECTED state (unnecessary)"  [p2p_probe.c] */
    LA_S29,  /* "%s: TURN allocated, starting address exchange"  [p2p_probe.c] */
    LA_S30,  /* "[SCTP] association established"  [p2p_trans_sctp.c] */
    LA_S31,  /* "[SCTP] usrsctp initialized, connecting..."  [p2p_trans_sctp.c] */
    LA_S32,  /* "[SCTP] usrsctp_socket failed"  [p2p_trans_sctp.c] */
    LA_S33,  /* "Channel ID validation failed"  [p2p_signal_pubsub.c] */
    LA_S34,  /* "Detecting local network addresses"  [p2p_route.c] */
    LA_S35,  /* "Gist GET failed"  [p2p_signal_pubsub.c] */
    LA_S36,  /* "Invalid channel_id format (security risk)"  [p2p_signal_pubsub.c] */
    LA_S37,  /* "Out of memory"  [p2p_signal_pubsub.c] */
    LA_S38,  /* "Push host cand<%s:%d> failed(OOM)\n"  [p2p_ice.c] */
    LA_S39,  /* "Push local cand<%s:%d> failed(OOM)\n"  [p2p.c] */
    LA_S40,  /* "Push remote cand<%s:%d> failed(OOM)\n"  [p2p_signal_pubsub.c, p2p_signal_relay.c] */

    /* Formats (LA_F) */
    LA_F41,  /* "  ... and %d more pairs" (%d)  [p2p_ice.c] */
    LA_F42,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F43,  /* "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" (%d,%s,%d,%s,%d)  [p2p_ice.c] */
    LA_F44,  /* "%s '%s' (%u %s)" (%s,%s,%u,%s)  [p2p_signal_relay.c] */
    LA_F45,  /* "%s NOTIFY: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F46,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F47,  /* "%s NOTIFY: srflx addr update (disabled)\n" (%s)  [p2p_signal_compact.c] */
    LA_F48,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F49,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F50,  /* "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F51,  /* "%s resent, %d/%d\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F52,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F53,  /* "%s sent to %s:%d (writable), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F54,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F55,  /* "%s sent to %s:%d, echo_seq=0, path=%d" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F56,  /* "%s sent to %s:%d, seq=0, path=%d" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F57,  /* "%s sent via best path[%d] to %s:%d, echo_seq=%u" (%s,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F58,  /* "%s sent, inst_id=%u, cands=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F59,  /* "%s sent, inst_id=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F60,  /* "%s sent, seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F61,  /* "%s sent, sid=%u, msg=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F62,  /* "%s sent, total=%d (ses_id=%llu)\n" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F63,  /* "%s seq=0: accepted cand_cnt=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F64,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F65,  /* "%s skipped: session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F66,  /* "%s, retry remaining candidates and FIN to peer\n" (%s)  [p2p_signal_compact.c] */
    LA_F67,  /* "%s, sent on %s\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F68,  /* "%s: %s timeout after %d retries (sid=%u)\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F69,  /* "%s: CONN timeout after %llums" (%s,%l)  [p2p_nat.c] */
    LA_F70,  /* "%s: CONNECTING → RELAY (timeout, signaling)" (%s)  [p2p_nat.c] */
    LA_F71,  /* "%s: CONNECTING → CLOSED (timeout, no relay)" (%s)  [p2p_nat.c] */
    LA_F72,  /* "%s: CONNECTING → CONNECTED (recv CONN)" (%s)  [p2p_nat.c] */
    LA_F73,  /* "%s: CONNECTING → CONNECTED (recv CONN_ACK)" (%s)  [p2p_nat.c] */
    LA_F74,  /* "%s: CONNECTING → CONNECTED (recv DATA)" (%s)  [p2p_nat.c] */
    LA_F75,  /* "%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F76,  /* "%s: Peer addr changed -> %s:%d, retrying punch\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F77,  /* "%s: RELAY → CONNECTED (direct path recovered)" (%s)  [p2p_nat.c] */
    LA_F78,  /* "%s: RPC complete (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F79,  /* "%s: RPC fail due to peer offline (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F80,  /* "%s: RPC fail due to relay timeout (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F81,  /* "%s: RPC finished (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F82,  /* "%s: SUCCESS: UDP reachable via TURN (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F83,  /* "%s: TURN allocation failed: ret=%d" (%s,%d)  [p2p_probe.c] */
    LA_F84,  /* "%s: TURN allocation request sent" (%s)  [p2p_probe.c] */
    LA_F85,  /* "%s: UDP timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F86,  /* "%s: UDP timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F87,  /* "%s: accepted" (%s)  [p2p_nat.c] */
    LA_F88,  /* "%s: accepted (ses_id=%llu)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F89,  /* "%s: accepted (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F90,  /* "%s: accepted for ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F91,  /* "%s: recv from cand[%d]" (%s,%d)  [p2p_nat.c] */
    LA_F92,  /* "%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F93,  /* "%s: accepted sid=%u, msg=%u\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F94,  /* "%s: accepted, probe_mapped=%s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F95,  /* "%s: accepted, public=%s:%d ses_id=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n" (%s,%s,%d,%l,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F96,  /* "%s: accepted, waiting for response (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F97,  /* "%s: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F98,  /* "%s: batch punch skip (state=%d, use trickle)" (%s,%d)  [p2p_nat.c] */
    LA_F99,  /* "%s: bad payload(len=%d cand_cnt=%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F100,  /* "%s: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F101,  /* "%s: bad payload(len=%d, need >=8)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F102,  /* "%s: PUNCHING → CONNECTING (%s)" (%s,%s)  [p2p_nat.c] */
    LA_F103,  /* "%s: PUNCHING → CONNECTED (earlier CONN)" (%s)  [p2p_nat.c] */
    LA_F104,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F105,  /* "%s: discovered prflx cand<%s:%d>[%d]" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F106,  /* "%s: duplicate remote cand<%s:%d> from signaling, skipped\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F107,  /* "%s: duplicate request ignored (sid=%u, already processing)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F108,  /* "%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F109,  /* "%s: entered, %s arrived after REGISTERED\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F110,  /* "%s: exchange timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F111,  /* "%s: exchange timeout: peer not responding" (%s)  [p2p_probe.c] */
    LA_F112,  /* "%s: ignored for duplicated seq=%u, already acked\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F113,  /* "%s: ignored for seq=%u (expect=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F114,  /* "%s: ignored for ses_id=%llu (local ses_id=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F115,  /* "%s: ignored for sid=%u (current sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F116,  /* "%s: ignored in invalid state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F117,  /* "%s: ignored in state=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F118,  /* "%s: invalid ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F119,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F120,  /* "%s: invalid for non-relay req\n" (%s)  [p2p_signal_compact.c] */
    LA_F121,  /* "%s: bad payload len=%d (need 6)" (%s,%d)  [p2p_nat.c] */
    LA_F122,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F123,  /* "%s: invalid session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F124,  /* "%s: keep-alive sent (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F125,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F126,  /* "%s: batch punch: no cand, wait trickle" (%s)  [p2p_nat.c] */
    LA_F127,  /* "%s: CONNECTED → LOST (no response %llums)\n" (%s,%l)  [p2p_nat.c] */
    LA_F128,  /* "%s: no writable path available" (%s)  [p2p_nat.c] */
    LA_F129,  /* "%s: not connected, cannot send FIN" (%s)  [p2p_nat.c] */
    LA_F130,  /* disabled "%s: not connected, unexpected ACK" */
    LA_F131,  /* "%s: old request ignored (sid=%u <= last_sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F132,  /* "%s: path[%d] UP (%s:%d)" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F133,  /* "%s: peer disconnected (ses_id=%llu), reset to REGISTERED\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F134,  /* "%s: peer online, proceeding to ICE\n" (%s)  [p2p_signal_compact.c] */
    LA_F135,  /* "%s: peer reachable via signaling (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F136,  /* "%s: peer_info0 srflx cand[%d]<%s:%d>%s\n" (%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F137,  /* "%s: punch remote cand[%d]<%s:%d> failed\n" (%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F138,  /* "%s: PUNCHING → CLOSED (timeout %llums, no relay)" (%s,%l)  [p2p_nat.c] */
    LA_F139,  /* "%s: punching %d/%d candidates (elapsed: %llu ms)" (%s,%d,%d,%l)  [p2p_nat.c] */
    LA_F140,  /* "%s: punch cand[%d] %s:%d (%s)" (%s,%d,%s,%d,%s)  [p2p_nat.c] */
    LA_F141,  /* disabled "%s: punching remote [%d]cand<%s:%d> (type: %s)" */
    LA_F142,  /* "%s: push remote cand<%s:%d> failed(OOM)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F143,  /* "%s: reaching alloc OOM" (%s)  [p2p_nat.c] */
    LA_F144,  /* "%s: reaching broadcast to %d cand(s), seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F145,  /* "%s: reaching enqueued: cand[%d], seq=%u, priority=%u" (%s,%d,%u,%u)  [p2p_nat.c] */
    LA_F146,  /* "%s: reaching relay via signaling FAILED (ret=%d), seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F147,  /* "%s: reaching relay via signaling SUCCESS, seq=%u" (%s,%u)  [p2p_nat.c] */
    LA_F148,  /* "%s: reaching updated: cand[%d], seq=%u->%u" (%s,%d,%u,%u)  [p2p_nat.c] */
    LA_F149,  /* "%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u" (%s,%d,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F150,  /* "%s: → CLOSED (recv FIN)" (%s)  [p2p_nat.c] */
    LA_F151,  /* "%s: recorded peer conn_seq=%u for future CONN_ACK" (%s,%u)  [p2p_nat.c] */
    LA_F152,  /* "%s: path[%d] relay UP" (%s,%d)  [p2p_nat.c] */
    LA_F153,  /* "%s: remote %s cand[%d]<%s:%d> accepted\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c] */
    LA_F154,  /* "%s: remote %s cand[%d]<%s:%d> (disabled)\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c] */
    _LA_155,
    LA_F156,  /* "%s: renew session due to session_id changed by info0 (local=%llu pkt=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F157,  /* "%s: restarting periodic check" (%s)  [p2p_probe.c] */
    LA_F158,  /* "%s: retry(%d/%d) probe\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F159,  /* "%s: retry(%d/%d) req (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F160,  /* "%s: retry(%d/%d) resp (sid=%u)\n" (%s,%d,%d,%u)  [p2p_signal_compact.c] */
    LA_F161,  /* "%s: retry, (attempt %d/%d)\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F162,  /* "%s: path rx UP (%s:%d)" (%s,%s,%d)  [p2p_nat.c] */
    LA_F163,  /* "%s: send failed(%d)" (%s,%d)  [p2p_probe.c] */
    LA_F164,  /* "%s: sent MSG(msg=0, sid=%u)" (%s,%u)  [p2p_probe.c] */
    LA_F165,  /* "%s: sent, sid=%u, code=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F166,  /* "%s: session mismatch(local=%llu ack=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F167,  /* "%s: session mismatch(local=%llu pkt=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F168,  /* "%s: session mismatch(local=%llu, pkt=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F169,  /* "%s: session validated, len=%d (ses_id=%llu)\n" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F170,  /* "%s: session_id mismatch (recv=%llu, expect=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
    LA_F171,  /* "%s: trickle punch: set peer_addr" (%s)  [p2p_nat.c] */
    LA_F172,  /* "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F173,  /* "%s: batch punch start (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F174,  /* "%s: trickle punch start" (%s)  [p2p_nat.c] */
    LA_F175,  /* "%s: started, sending first probe\n" (%s)  [p2p_signal_compact.c] */
    LA_F176,  /* "%s: status error(%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F177,  /* "%s: sync complete (ses_id=%llu)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F178,  /* "%s: sync complete (ses_id=%llu, mask=0x%04x)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F179,  /* "%s: target=%s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F180,  /* "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F181,  /* "%s: timeout after %d retries , type unknown\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F182,  /* "%s: PUNCHING → RELAY (timeout %llums, signaling)" (%s,%l)  [p2p_nat.c] */
    LA_F183,  /* "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%s,%l,%d)  [p2p_nat.c] */
    LA_F184,  /* "%s: timeout, max(%d) attempts reached, reset to INIT\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F185,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F186,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F187,  /* "%s: trickled %d cand(s), seq=%u (ses_id=%llu)\n" (%s,%d,%u,%l)  [p2p_signal_compact.c] */
    LA_F188,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F189,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F190,  /* "%s: path tx UP (echo seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F191,  /* "%s: unexpected ack_seq=%u mask=0x%04x\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F192,  /* "%s: unexpected non-srflx cand in peer_info0, treating as srflx\n" (%s)  [p2p_signal_compact.c] */
    LA_F193,  /* "%s: unexpected remote cand type %d, skipped\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F194,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F195,  /* "%s_ACK sent to %s:%d (try), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F196,  /* "%s_ACK sent, seq=%u (ses_id=%llu)\n" (%s,%u,%l)  [p2p_signal_compact.c] */
    LA_F197,  /* "%s_ACK sent, sid=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F198,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F199,  /* "ACK: invalid payload length %d, expected at least 6" (%d)  [p2p.c] */
    LA_F200,  /* "ACK: protocol mismatch, trans=%s has on_packet but received P2P_PKT_ACK" (%s)  [p2p.c] */
    LA_F201,  /* "Added Remote Candidate: %d -> %s:%d" (%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F202,  /* "% Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_F203,  /* "Append Host candidate: %s:%d" (%s,%d)  [p2p.c] */
    LA_F204,  /* "Attempting Simultaneous Open to %s:%d" (%s,%d)  [p2p_tcp_punch.c] */
    LA_F205,  /* "Auto-send answer (with %d candidates) total sent %s" (%d,%s)  [p2p_signal_pubsub.c] */
    LA_F206,  /* "% BIO_new failed"  [p2p_dtls_openssl.c] */
    LA_F207,  /* "% Base64 decode failed"  [p2p_signal_pubsub.c] */
    LA_F208,  /* "% Bind failed"  [p2p_tcp_punch.c] */
    LA_F209,  /* "Bind failed to %d, port busy, trying random port" (%d)  [p2p_tcp_punch.c] */
    LA_F210,  /* "Bound to :%d" (%d)  [p2p_tcp_punch.c] */
    LA_F211,  /* "% COMPACT mode requires explicit remote_peer_id"  [p2p.c] */
    LA_F212,  /* "COMPACT relay payload too large: %d" (%d)  [p2p_signal_compact.c] */
    LA_F213,  /* "COMPACT relay send failed: type=0x%02x, ret=%d" (%d)  [p2p_signal_compact.c] */
    LA_F214,  /* "COMPACT relay: type=0x%02x, seq=%u (session_id=%llu)" (%u,%l)  [p2p_signal_compact.c] */
    LA_F215,  /* "% Close P2P UDP socket"  [p2p.c] */
    LA_F216,  /* "% Closing TCP connection to RELAY signaling server"  [p2p.c] */
    LA_F217,  /* "Connect to COMPACT signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F218,  /* "Connect to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F219,  /* "Connected to server %s:%d as '%s'" (%s,%d,%s)  [p2p_signal_relay.c] */
    LA_F220,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F221,  /* "% Connection closed by server"  [p2p_signal_relay.c] */
    LA_F222,  /* "% Connection closed while discarding"  [p2p_signal_relay.c] */
    LA_F223,  /* "% Connection closed while reading payload"  [p2p_signal_relay.c] */
    LA_F224,  /* "% Connection closed while reading sender"  [p2p_signal_relay.c] */
    LA_F225,  /* "Connectivity checks timed out (sent %d rounds), giving up" (%d)  [p2p_ice.c] */
    LA_F226,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F227,  /* "% DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_F228,  /* "% DTLS handshake complete (MbedTLS)"  [p2p_dtls_mbedtls.c] */
    LA_F229,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F230,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F231,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F232,  /* "Discarded %d bytes payload of message type %d" (%d,%d)  [p2p_signal_relay.c] */
    LA_F233,  /* "Duplicate remote cand<%s:%d> from signaling, skipped" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F234,  /* "Duplicate remote candidate<%s:%d> from signaling, skipped" (%s,%d)  [p2p_ice.c] */
    LA_F235,  /* "Failed to allocate %u bytes" (%u)  [p2p_signal_relay.c] */
    LA_F236,  /* "% Failed to allocate ACK payload buffer"  [p2p_signal_relay.c] */
    LA_F237,  /* "% Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_F238,  /* "% Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_F239,  /* "% Failed to allocate discard buffer, closing connection"  [p2p_signal_relay.c] */
    LA_F240,  /* "% Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_F241,  /* "% Failed to allocate memory for session"  [p2p.c] */
    LA_F242,  /* "% Failed to build STUN request"  [p2p_stun.c] */
    LA_F243,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_internal.h] */
    LA_F244,  /* "Failed to reserve remote candidates (base=%u cnt=%d)\n" (%u,%d)  [p2p_signal_compact.c] */
    LA_F245,  /* "Failed to reserve remote candidates (cnt=%d)\n" (%d)  [p2p_signal_compact.c] */
    LA_F246,  /* "% Failed to reserve remote candidates (cnt=1)\n"  [p2p_signal_compact.c] */
    LA_F247,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F248,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F249,  /* "% Failed to send header"  [p2p_signal_relay.c] */
    LA_F250,  /* "% Failed to send payload"  [p2p_signal_relay.c] */
    LA_F251,  /* "% Failed to send punch packet for new peer addr\n"  [p2p_signal_compact.c] */
    LA_F252,  /* "% Failed to send target name"  [p2p_signal_relay.c] */
    LA_F253,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F254,  /* "First offer, resetting ICE and clearing %d stale candidates" (%d)  [p2p_signal_pubsub.c] */
    LA_F255,  /* "Formed check list with %d candidate pairs" (%d)  [p2p_ice.c] */
    LA_F256,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F257,  /* "Gathered host cand<%s:%d> (priority=0x%08x)" (%s,%d)  [p2p_ice.c] */
    LA_F258,  /* "% Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_F259,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F260,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F261,  /* "Initialize network subsystem failed(%d)" (%d)  [p2p.c] */
    LA_F262,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F263,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F264,  /* "Invalid magic 0x%x (expected 0x%x), resetting" (%x,%x)  [p2p_signal_relay.c] */
    LA_F265,  /* "Invalid read state %d, resetting" (%d)  [p2p_signal_relay.c] */
    LA_F266,  /* "% Invalid signaling mode in configuration"  [p2p.c] */
    LA_F267,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F268,  /* "Marked old path (idx=%d) as FAILED due to addr change\n" (%d)  [p2p_signal_compact.c] */
    LA_F269,  /* "% NAT connected but no available path in path manager"  [p2p.c] */
    LA_F270,  /* "% No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_F271,  /* "% No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_F272,  /* "Nomination successful! Using! Using %s path %s:%d%s" (%s,%s,%d,%s)  [p2p_ice.c] */
    LA_F273,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F274,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F275,  /* "% OpenSSL requested but library not linked"  [p2p.c] */
    LA_F276,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F277,  /* "% P2P connected, closing signaling TCP connection"  [p2p_signal_relay.c] */
    LA_F278,  /* "PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u\n" (%d,%u)  [p2p_signal_compact.c] */
    LA_F279,  /* "% PEER_INFO(trickle): seq overflow, cannot trickle more\n"  [p2p_signal_compact.c] */
    LA_F280,  /* "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_F281,  /* "% PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_F282,  /* "% PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_F283,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F284,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F285,  /* "Passive peer learned remote ID '%s' from OFFER" (%s)  [p2p_signal_relay.c] */
    LA_F286,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F287,  /* "% Path switch debounced, waiting for stability"  [p2p.c] */
    LA_F288,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F289,  /* "Peer '%s' is now online (FORWARD received), resuming" (%s)  [p2p_signal_relay.c] */
    LA_F290,  /* "Peer offline, cached %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F291,  /* "Peer online, forwarded %d candidates" (%d)  [p2p_signal_relay.c] */
    LA_F292,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F293,  /* "% PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_F294,  /* "Push prflx candidate<%s:%d> failed(OOM)" (%s,%d)  [p2p_ice.c] */
    LA_F295,  /* "Push remote candidate<%s:%d> (type=%d) failed(OOM)" (%s,%d,%d)  [p2p_ice.c] */
    LA_F296,  /* "REGISTERED: peer=%s\n" (%s)  [p2p_signal_compact.c] */
    LA_F297,  /* "% RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_F298,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F299,  /* "Received ACK (status=%d, candidates_acked=%d)" (%d,%d)  [p2p_signal_relay.c] */
    LA_F300,  /* "Received STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F301,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F302,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F303,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F304,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F305,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F306,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F307,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F308,  /* "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F309,  /* "Recv %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_nat.c] */
    LA_F310,  /* "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)" (%s,%d)  [p2p_ice.c] */
    LA_F311,  /* "Recv New Remote Candidate<%s:%d> (type=%d)" (%s,%d,%d)  [p2p_ice.c] */
    LA_F312,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F313,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F314,  /* "Requested Relay Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F315,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p.c] */
    LA_F316,  /* "Requested Srflx Candidate from %s" (%s)  [p2p_ice.c] */
    LA_F317,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F318,  /* "% SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_F319,  /* "% SIGNALING path active but relay function not available"  [p2p_channel.c] */
    LA_F320,  /* "% SIGNALING path enabled (server supports relay)\n"  [p2p_signal_compact.c] */
    LA_F321,  /* "% SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_F322,  /* "% SSL_new failed"  [p2p_dtls_openssl.c] */
    LA_F323,  /* "Send offer to RELAY signaling server failed(%d)" (%d)  [p2p.c] */
    LA_F324,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F325,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F326,  /* "% Sending FIN packet to peer before closing"  [p2p.c] */
    LA_F327,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F328,  /* "% Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_F329,  /* "Sent answer to '%s'" (%s)  [p2p_ice.c] */
    LA_F330,  /* "Sent answer to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F331,  /* "Sent connect request to '%s' (%d bytes)" (%s,%d)  [p2p_signal_relay.c] */
    LA_F332,  /* "Sent initial offer(%d) to %s)" (%d,%s)  [p2p.c] */
    LA_F333,  /* "% Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_F334,  /* "% Skipping Host Candidate gathering (disabled)"  [p2p_ice.c] */
    LA_F335,  /* "% Skipping local Host candidates (disabled)"  [p2p.c] */
    LA_F336,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F337,  /* "% Starting internal thread"  [p2p.c] */
    LA_F338,  /* "% State: CONNECTED → LOST (no relay)"  [p2p.c] */
    LA_F339,  /* "% State: CONNECTED → RELAY (path lost)"  [p2p.c] */
    LA_F340,  /* "% State: LOST → CONNECTED (legacy path)"  [p2p.c] */
    LA_F341,  /* "State: LOST → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F342,  /* "State: RELAY → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F343,  /* "State: → CONNECTED, path[%d]" (%d)  [p2p.c] */
    LA_F344,  /* "% State: → ERROR (punch timeout, no relay available)"  [p2p.c] */
    LA_F345,  /* "% State: → PUNCHING"  [p2p.c] */
    _LA_346,
    _LA_347,
    LA_F348,  /* "State: → RELAY, path[%d] (signaling)" (%d)  [p2p.c] */
    LA_F349,  /* "% Stopping internal thread"  [p2p.c] */
    LA_F350,  /* "% Storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_F351,  /* "% Synced path after failover"  [p2p.c] */
    LA_F352,  /* "TURN 401 Unauthorized (realm=%s), authenticating..." (%s)  [p2p_turn.c] */
    LA_F353,  /* "TURN Allocate failed with error %d" (%d)  [p2p_turn.c] */
    LA_F354,  /* "TURN Allocated relay %s:%u (lifetime=%us)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F355,  /* "TURN CreatePermission failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F356,  /* "TURN CreatePermission for %s" (%s)  [p2p_turn.c] */
    LA_F357,  /* "TURN Data Indication from %s:%u (%d bytes)" (%s,%u,%d)  [p2p_turn.c] */
    LA_F358,  /* "TURN Refresh failed (error=%d)" (%d)  [p2p_turn.c] */
    LA_F359,  /* "TURN Refresh ok (lifetime=%us)" (%u)  [p2p_turn.c] */
    LA_F360,  /* "% TURN auth required but no credentials configured"  [p2p_turn.c] */
    LA_F361,  /* "Test I: Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F362,  /* "% Test I: Timeout"  [p2p_stun.c] */
    LA_F363,  /* "Test II: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F364,  /* "% Test II: Timeout (need Test III)"  [p2p_stun.c] */
    LA_F365,  /* "Test III: Success! Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F366,  /* "% Test III: Timeout"  [p2p_stun.c] */
    LA_F367,  /* "Transport layer '%s' init failed, falling back to simple reliable" (%s)  [p2p.c] */
    LA_F368,  /* "UDP hole-punch probing remote candidates (%d candidates)" (%d)  [p2p_ice.c] */
    LA_F369,  /* "UDP hole-punch probing remote candidates round %d/%d" (%d,%d)  [p2p_ice.c] */
    LA_F370,  /* "Unknown ACK status %d" (%d)  [p2p_signal_relay.c] */
    LA_F371,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F372,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F373,  /* "Waiting for peer '%s' timed out (%dms), giving up" (%s,%d)  [p2p_signal_relay.c] */
    LA_F374,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F375,  /* "% [OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    LA_F376,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    LA_F377,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F378,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F379,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F380,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F381,  /* "[SIGNALING] Failed to send candidates, will retry (ret=%d)" (%d)  [p2p_signal_relay.c] */
    LA_F382,  /* "[SIGNALING] Sent candidates (cached, peer offline) %d to %s" (%d,%s)  [p2p_signal_relay.c] */
    LA_F383,  /* "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)" (%d,%d,%s,%d)  [p2p_signal_relay.c] */
    LA_F384,  /* "% [SIGNALING] Server storage full, waiting for peer to come online"  [p2p_signal_relay.c] */
    LA_F385,  /* "[Trickle] Immediately probing new candidate %s:%d" (%s,%d)  [p2p_signal_pubsub.c, p2p_signal_relay.c] */
    LA_F386,  /* "[Trickle] Sent 1 candidate to %s (online=%s)" (%s,%s)  [p2p_ice.c] */
    LA_F387,  /* "% [Trickle] TCP not connected, skipping single candidate send"  [p2p_ice.c] */
    LA_F388,  /* "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()" (%d)  [p2p_ice.c] */
    LA_F389,  /* "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F390,  /* "[UDP] %s recv from %s:%d, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F391,  /* "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F392,  /* "[UDP] %s recv from %s:%d, seq=%u, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F393,  /* "[UDP] %s recv from %s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F394,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F395,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F396,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F397,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F398,  /* "[UDP] %s_ACK send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F399,  /* "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F400,  /* "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F401,  /* "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F402,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F403,  /* "% connection closed by peer"  [p2p.c] */
    LA_F404,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F405,  /* "% p2p_ice_send_local_candidate called in non-RELAY mode"  [p2p_ice.c] */
    LA_F406,  /* "recv error %d" (%d)  [p2p_signal_relay.c] */
    LA_F407,  /* "recv error %d while discarding" (%d)  [p2p_signal_relay.c] */
    LA_F408,  /* "recv error %d while reading payload" (%d)  [p2p_signal_relay.c] */
    LA_F409,  /* "recv error %d while reading sender" (%d)  [p2p_signal_relay.c] */
    LA_F410,  /* "relay_tick: recv header complete, magic=0x%x, type=%d, length=%u" (%x,%d,%u)  [p2p_signal_relay.c] */
    LA_F411,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F412,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F413,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F414,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F415,  /* "starting NAT punch(Host candidate %d)" (%d)  [p2p_ice.c] */
    LA_F416,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    LA_F417,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F418,  /* "% ✗ Add Srflx candidate failed(OOM)"  [p2p_stun.c] */
    LA_F419,  /* "%s: invalid payload len=%d (need 6)" (%s,%d)  [p2p_nat.c] */
    LA_F420,  /* "Ignore %s pkt from %s:%d, not connected" (%s,%s,%d)  [p2p_nat.c] */
    LA_F421,  /* "Ignore %s pkt from %s:%d, not connecting" (%s,%s,%d)  [p2p_nat.c] */
    LA_F422,  /* "Ignore %s pkt from %s:%d, not connecting/connected" (%s,%s,%d)  [p2p_nat.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F41

#endif /* LANG_H__ */
