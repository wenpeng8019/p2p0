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
    LA_W2,  /* disabled "bytes" */
    LA_W3,  /* "Detecting..."  [p2p_internal.h] */
    LA_W4,  /* "Full Cone NAT"  [p2p_internal.h] */
    LA_W5,  /* disabled "no (cached)" */
    LA_W6,  /* "Open Internet (No NAT)"  [p2p_internal.h] */
    LA_W7,  /* "Port Restricted Cone NAT"  [p2p_internal.h] */
    LA_W8,  /* "PUB"  [p2p_signal_pubsub.c] */
    LA_W9,  /* disabled "Published" */
    LA_W10,  /* "punch"  [p2p_nat.c] */
    LA_W11,  /* disabled "Received signal from" */
    LA_W12,  /* disabled "Resent" */
    LA_W13,  /* "Restricted Cone NAT"  [p2p_internal.h] */
    LA_W14,  /* "retry"  [p2p_nat.c] */
    LA_W15,  /* "SUB"  [p2p_signal_pubsub.c] */
    LA_W16,  /* "Symmetric NAT (port-random)"  [p2p_internal.h] */
    LA_W17,  /* "Timeout (no response)"  [p2p_internal.h] */
    LA_W18,  /* "UDP Blocked (STUN unreachable)"  [p2p_internal.h] */
    LA_W19,  /* "Undetectable (no STUN/probe configured)"  [p2p_internal.h] */
    LA_W20,  /* "Unknown"  [p2p_internal.h] */
    LA_W21,  /* "Waiting for incoming offer from any peer"  [p2p.c] */
    LA_W22,  /* disabled "yes" */

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
    LA_S38,  /* disabled "Push host cand<%s:%d> failed(OOM)\n" */
    LA_S39,  /* "Push local cand<%s:%d> failed(OOM)\n"  [p2p.c] */
    LA_S40,  /* "Push remote cand<%s:%d> failed(OOM)\n"  [p2p_signal_pubsub.c] */

    /* Formats (LA_F) */
    LA_F41,  /* disabled "  ... and %d more pairs" */
    LA_F42,  /* "  [%d] %s/%d" (%d,%s,%d)  [p2p_route.c] */
    LA_F43,  /* disabled "  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx" */
    LA_F44,  /* disabled "%s '%s' (%u %s)" */
    LA_F45,  /* "%s NOTIFY: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F46,  /* "%s NOTIFY: ignored old notify base=%u (current=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F47,  /* "%s NOTIFY: srflx addr update (disabled)\n" (%s)  [p2p_signal_compact.c] */
    LA_F48,  /* "%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F49,  /* "%s msg=0: accepted, echo reply (sid=%u, len=%d)\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F50,  /* disabled "%s received (ice_ctx.state=%d), resetting ICE and clearing %d stale candidates" */
    LA_F51,  /* "%s resent, %d/%d\n" (%s,%d,%d)  [p2p_signal_compact.c] */
    _LA_52,
    LA_F53,  /* "%s sent to %s:%d (writable), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F54,  /* "%s sent to %s:%d for %s, seq=%d, path=%d" (%s,%s,%d,%s,%d,%d)  [p2p_nat.c] */
    _LA_55,
    LA_F56,  /* "%s sent to %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F57,  /* "%s sent via best path[%d] to %s:%d, echo_seq=%u" (%s,%d,%s,%d,%u)  [p2p_nat.c] */
    LA_F58,  /* "%s sent, inst_id=%u, cands=%d\n" (%s,%u,%d)  [p2p_signal_compact.c] */
    LA_F59,  /* "%s sent, inst_id=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F60,  /* "%s sent, seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F61,  /* disabled "%s sent %d candidates, next_idx=%d\n" */
    LA_F62,  /* "%s sent, total=%d (ses_id=%llu)\n" (%s,%d,%l)  [p2p_signal_compact.c] */
    LA_F63,  /* "%s seq=0: accepted cand_cnt=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F64,  /* "%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F65,  /* "%s skipped: session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F66,  /* "%s, retry remaining candidates and FIN to peer\n" (%s)  [p2p_signal_compact.c] */
    LA_F67,  /* "%s, sent on %s\n" (%s,%s)  [p2p_signal_compact.c] */
    LA_F68,  /* "%s: %s timeout after %d retries (sid=%u)\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F69,  /* "%s: CONN timeout after %llums" (%s,%l)  [p2p_nat.c] */
    _LA_70,
    LA_F71,  /* "%s: CONNECTING → CLOSED (timeout, no relay)" (%s)  [p2p_nat.c] */
    LA_F72,  /* "%s: CONNECTING → %s (recv CONN)" (%s,%s)  [p2p_nat.c] */
    LA_F73,  /* "%s: CONNECTING → %s (recv CONN_ACK)" (%s,%s)  [p2p_nat.c] */
    LA_F74,  /* "%s: %s → %s (recv DATA)" (%s,%s,%s)  [p2p_nat.c] */
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
    LA_F95,  /* disabled "%s: accepted, forward=%s msg=%s\n" */
    LA_F96,  /* "%s: accepted, waiting for response (sid=%u)\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F97,  /* "%s: accepted\n" (%s)  [p2p_signal_compact.c] */
    LA_F98,  /* "%s: batch punch skip (state=%d, use trickle)" (%s,%d)  [p2p_nat.c] */
    LA_F99,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [p2p_signal_relay.c] */
    LA_F100,  /* disabled "%s: bad payload len=%d\n" */
    LA_F101,  /* "%s: bad payload(len=%d, need >=8)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F102,  /* "%s: PUNCHING → CONNECTING (%s%s)" (%s,%s,%s)  [p2p_nat.c] */
    LA_F103,  /* "%s: PUNCHING → %s" (%s,%s)  [p2p_nat.c] */
    LA_F104,  /* "%s: completed, mapped=%s:%d probe=%s:%d -> %s\n" (%s,%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    _LA_105,
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
    LA_F117,  /* "%s: ignored in state=%d\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F118,  /* "%s: invalid ack_seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F119,  /* "%s: invalid cand idx: %d (count: %d)" (%s,%d,%d)  [p2p_nat.c] */
    LA_F120,  /* "%s: invalid for non-relay req\n" (%s)  [p2p_signal_compact.c] */
    LA_F121,  /* "%s: bad payload(%d)" (%s,%d)  [p2p_nat.c] */
    LA_F122,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F123,  /* "%s: invalid session_id=0\n" (%s)  [p2p_signal_compact.c] */
    LA_F124,  /* "%s: keep-alive sent (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F125,  /* "%s: new request (sid=%u) overrides pending request (sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F126,  /* "%s: batch punch: no cand, wait trickle" (%s)  [p2p_nat.c] */
    LA_F127,  /* "%s: CONNECTED → LOST (no response %llums)\n" (%s,%l)  [p2p_nat.c] */
    _LA_128,
    LA_F129,  /* "%s: not connected, cannot send FIN" (%s)  [p2p_nat.c] */
    _LA_130,
    LA_F131,  /* "%s: old request ignored (sid=%u <= last_sid=%u)\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F132,  /* "%s: path[%d] UP (%s:%d)" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F133,  /* "%s: path[%d] UP (recv DATA)" (%s,%d)  [p2p_nat.c] */
    LA_F134,  /* "%s: peer online, proceeding to ICE\n" (%s)  [p2p_signal_compact.c] */
    LA_F135,  /* "%s: peer reachable via signaling (RTT: %llu ms)" (%s,%l)  [p2p_probe.c] */
    LA_F136,  /* "%s: sync0 srflx cand[%d]<%s:%d>%s\n" (%s,%d,%s,%d,%s)  [p2p_signal_compact.c] */
    LA_F137,  /* "%s: punch remote cand[%d]<%s:%d> failed\n" (%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F138,  /* "%s: PUNCHING → CLOSED (timeout %llums, %s signaling relay)" (%s,%l,%s)  [p2p_nat.c] */
    LA_F139,  /* "%s: punching %d/%d candidates (elapsed: %llu ms)" (%s,%d,%d,%l)  [p2p_nat.c] */
    LA_F140,  /* "%s: punch cand[%d] %s:%d (%s)" (%s,%d,%s,%d,%s)  [p2p_nat.c] */
    _LA_141,
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
    LA_F153,  /* "%s: remote %s cand[%d]<%s:%d> accepted\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c, p2p_nat.c] */
    LA_F154,  /* "%s: remote %s cand<%s:%d> (disabled)\n" (%s,%s,%s,%d)  [p2p_nat.c] */
    _LA_155,
    LA_F156,  /* "%s: renew session due to session_id changed by sync0 (local=%llu pkt=%llu)\n" (%s,%l,%l)  [p2p_signal_compact.c] */
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
    _LA_171,
    LA_F172,  /* "%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n" (%s,%u,%u)  [p2p_signal_compact.c] */
    LA_F173,  /* "%s: batch punch start (%d cands)" (%s,%d)  [p2p_nat.c] */
    LA_F174,  /* "%s: trickle punch start" (%s)  [p2p_nat.c] */
    LA_F175,  /* "%s: started, sending first probe\n" (%s)  [p2p_signal_compact.c] */
    LA_F176,  /* disabled "%s: error, target not found\n" */
    LA_F177,  /* "%s: sync complete (ses_id=%llu)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F178,  /* "%s: sync complete (ses_id=%llu, mask=0x%04x)\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F179,  /* "%s: accepted as cand[%d], target=%s:%d" (%s,%d,%s,%d)  [p2p_nat.c] */
    LA_F180,  /* "%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F181,  /* "%s: timeout after %d retries , type unknown\n" (%s,%d)  [p2p_signal_compact.c] */
    _LA_182,
    LA_F183,  /* "%s: timeout but ICE exchange not done yet (%llu ms elapsed, mode=%d), waiting for more candidates" (%s,%l,%d)  [p2p_nat.c] */
    LA_F184,  /* "%s: timeout, max(%d) attempts reached, reset to INIT\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F185,  /* "%s: timeout, peer did not respond" (%s)  [p2p_probe.c] */
    LA_F186,  /* "%s: timeout, retry %d/%d" (%s,%d,%d)  [p2p_probe.c] */
    LA_F187,  /* "%s: trickled %d cand(s), seq=%u (ses_id=%llu)\n" (%s,%d,%u,%l)  [p2p_signal_compact.c] */
    LA_F188,  /* "%s: triggered via COMPACT msg echo" (%s)  [p2p_probe.c] */
    LA_F189,  /* "%s: triggered via RELAY TUNE echo" (%s)  [p2p_probe.c] */
    LA_F190,  /* "%s: path tx UP (echo seq=%u)" (%s,%u)  [p2p_nat.c] */
    LA_F191,  /* "%s: unexpected ack_seq=%u mask=0x%04x\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F192,  /* "%s: unexpected non-srflx cand in sync0, treating as srflx\n" (%s)  [p2p_signal_compact.c] */
    LA_F193,  /* "%s: unexpected remote cand type %d, skipped\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F194,  /* "%s:%04d: %s" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F195,  /* "%s_ACK sent to %s:%d (try), echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F196,  /* "%s_ACK sent, seq=%u (ses_id=%llu)\n" (%s,%u,%l)  [p2p_signal_compact.c] */
    LA_F197,  /* "%s_ACK sent, sid=%u\n" (%s,%u)  [p2p_signal_compact.c] */
    LA_F198,  /* "ACK processed ack_seq=%u send_base=%u inflight=%d" (%u,%u,%d)  [p2p_trans_reliable.c] */
    LA_F199,  /* "ACK: invalid payload length %d, expected at least 6" (%d)  [p2p.c] */
    LA_F200,  /* "ACK: protocol mismatch, trans=%s has on_packet but received P2P_PKT_ACK" (%s)  [p2p.c] */
    LA_F201,  /* disabled "Added Remote Candidate: %d -> %s:%d" */
    LA_F202,  /* "% Answer already present, skipping offer re-publish"  [p2p_signal_pubsub.c] */
    LA_F203,  /* "Gathered Host candidate: %s:%d (priority=0x%08x)" (%s,%d)  [p2p.c] */
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
    LA_F219,  /* disabled "Connected to server %s:%d as '%s'" */
    LA_F220,  /* "Connecting to RELAY signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F221,  /* disabled "% Connection closed by server" */
    LA_F222,  /* disabled "% Connection closed while discarding" */
    LA_F223,  /* disabled "% Connection closed while reading payload" */
    LA_F224,  /* disabled "% Connection closed while reading sender" */
    LA_F225,  /* disabled "Connectivity checks timed out (sent %d rounds), giving up" */
    LA_F226,  /* "Crypto layer '%s' init failed, continuing without encryption" (%s)  [p2p.c] */
    LA_F227,  /* "% DTLS (MbedTLS) requested but library not linked"  [p2p.c] */
    LA_F228,  /* "% DTLS handshake complete (MbedTLS)"  [p2p_dtls_mbedtls.c] */
    LA_F229,  /* "Data stored in recv buffer seq=%u len=%d base=%u" (%u,%d,%u)  [p2p_trans_reliable.c] */
    LA_F230,  /* "Detect local network interfaces failed(%d)" (%d)  [p2p.c] */
    LA_F231,  /* "Detection completed %s" (%s)  [p2p_stun.c] */
    LA_F232,  /* disabled "Discarded %d bytes payload of message type %d" */
    LA_F233,  /* "Duplicate remote cand<%s:%d> from signaling, skipped" (%s,%d)  [p2p_signal_pubsub.c] */
    LA_F234,  /* disabled "Duplicate remote candidate<%s:%d> from signaling, skipped" */
    LA_F235,  /* disabled "Failed to allocate %u bytes" */
    LA_F236,  /* disabled "% Failed to allocate ACK payload buffer" */
    LA_F237,  /* "% Failed to allocate DTLS context"  [p2p_dtls_mbedtls.c] */
    LA_F238,  /* "% Failed to allocate OpenSSL context"  [p2p_dtls_openssl.c] */
    LA_F239,  /* disabled "% Failed to allocate discard buffer, closing connection" */
    LA_F240,  /* "% Failed to allocate memory for candidate lists"  [p2p.c] */
    LA_F241,  /* "% Failed to allocate memory for session"  [p2p.c] */
    LA_F242,  /* "% Failed to build STUN request"  [p2p_stun.c] */
    LA_F243,  /* "Failed to realloc memory for remote candidates (capacity: %d)" (%d)  [p2p_internal.h] */
    LA_F244,  /* "Failed to reserve remote candidates (base=%u cnt=%d)\n" (%u,%d)  [p2p_signal_compact.c] */
    LA_F245,  /* "Failed to reserve remote candidates (cnt=%d)\n" (%d)  [p2p_signal_compact.c] */
    LA_F246,  /* "% Failed to reserve remote candidates (cnt=1)\n"  [p2p_signal_compact.c] */
    LA_F247,  /* "Failed to resolve STUN server %s" (%s)  [p2p_stun.c] */
    LA_F248,  /* "Failed to resolve TURN server: %s" (%s)  [p2p_turn.c] */
    LA_F249,  /* disabled "% Failed to send header" */
    LA_F250,  /* disabled "% Failed to send payload" */
    LA_F251,  /* "% Failed to send punch packet for new peer addr\n"  [p2p_signal_compact.c] */
    LA_F252,  /* disabled "% Failed to send target name" */
    LA_F253,  /* "Field %s is empty or too short" (%s)  [p2p_signal_pubsub.c] */
    LA_F254,  /* disabled "First offer, resetting ICE and clearing %d stale candidates" */
    LA_F255,  /* disabled "Formed check list with %d candidate pairs" */
    LA_F256,  /* "Gathered Relay Candidate %s:%u (priority=%u)" (%s,%u,%u)  [p2p_turn.c] */
    LA_F257,  /* disabled "Gathered host cand<%s:%d> (priority=0x%08x)" */
    LA_F258,  /* "% Handshake complete"  [p2p_dtls_mbedtls.c] */
    LA_F259,  /* "Handshake failed: %s (-0x%04x)" (%s)  [p2p_dtls_mbedtls.c] */
    LA_F260,  /* "Initialize PUBSUB signaling context failed(%d)" (%d)  [p2p.c] */
    LA_F261,  /* "Initialize network subsystem failed(%d)" (%d)  [p2p.c] */
    LA_F262,  /* "Initialize signaling mode: %d" (%d)  [p2p.c] */
    LA_F263,  /* "Initialized: %s" (%s)  [p2p_signal_pubsub.c] */
    LA_F264,  /* disabled "Invalid magic 0x%x (expected 0x%x), resetting" */
    LA_F265,  /* disabled "Invalid read state %d, resetting" */
    LA_F266,  /* "% Invalid signaling mode in configuration"  [p2p.c] */
    LA_F267,  /* "Local address detection done: %d address(es)" (%d)  [p2p_route.c] */
    LA_F268,  /* disabled "Marked old path (idx=%d) as FAILED due to addr change\n" */
    LA_F269,  /* disabled "% NAT connected but no available path in path manager" */
    LA_F270,  /* "% No advanced transport layer enabled, using simple reliable layer"  [p2p.c] */
    LA_F271,  /* "% No auth_key provided, using default key (insecure)"  [p2p_signal_pubsub.c] */
    LA_F272,  /* disabled "Nomination successful! Using! Using %s path %s:%d%s" */
    LA_F273,  /* "Open P2P UDP socket on port %d" (%d)  [p2p.c] */
    LA_F274,  /* "Open P2P UDP socket on port %d failed(%d)" (%d,%d)  [p2p.c] */
    LA_F275,  /* "% OpenSSL requested but library not linked"  [p2p.c] */
    LA_F276,  /* "Out-of-window packet discarded seq=%u base=%u" (%u,%u)  [p2p_trans_reliable.c] */
    LA_F277,  /* disabled "% P2P connected, closing signaling TCP connection" */
    LA_F278,  /* "SYNC(trickle): batching, queued %d cand(s) for seq=%u\n" (%d,%u)  [p2p_signal_compact.c] */
    LA_F279,  /* "% SYNC(trickle): seq overflow, cannot trickle more\n"  [p2p_signal_compact.c] */
    LA_F280,  /* "% PUBSUB (PUB): gathering candidates, waiting for STUN before publishing"  [p2p.c] */
    LA_F281,  /* "% PUBSUB (SUB): waiting for offer from any peer"  [p2p.c] */
    LA_F282,  /* "% PUBSUB mode requires gh_token and gist_id"  [p2p.c] */
    LA_F283,  /* "Packet queued seq=%u len=%d inflight=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F284,  /* "Packet too large len=%d max=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F285,  /* disabled "Passive peer learned remote ID '%s' from OFFER" */
    LA_F286,  /* "Path manager initialized with strategy: %d (0=conn,1=perf,2=hybrid)" (%d)  [p2p.c] */
    LA_F287,  /* "% Path switch debounced, waiting for stability"  [p2p.c] */
    LA_F288,  /* "Path switched to better route (idx=%d)" (%d)  [p2p.c] */
    LA_F289,  /* disabled "Peer '%s' is now online (FORWARD received), resuming" */
    LA_F290,  /* disabled "Peer offline, cached %d candidates" */
    LA_F291,  /* disabled "Peer online, forwarded %d candidates" */
    LA_F292,  /* "Processing (role=%s)" (%s)  [p2p_signal_pubsub.c] */
    LA_F293,  /* "% PseudoTCP enabled as transport layer"  [p2p.c] */
    LA_F294,  /* disabled "Push prflx candidate<%s:%d> failed(OOM)" */
    LA_F295,  /* disabled "Push remote candidate<%s:%d> (type=%d) failed(OOM)" */
    LA_F296,  /* disabled "EXCHANGING: peer=%s, uploading candidates\n" */
    LA_F297,  /* "% RELAY/COMPACT mode requires server_host"  [p2p.c] */
    LA_F298,  /* "RTT updated rtt=%dms srtt=%d rttvar=%d rto=%d" (%d,%d,%d,%d)  [p2p_trans_reliable.c] */
    LA_F299,  /* disabled "Received ACK (status=%d, candidates_acked=%d)" */
    LA_F300,  /* "Recv STUN/TURN pkt from %s:%d, type=0x%04x, len=%d" (%s,%d,%d)  [p2p.c] */
    LA_F301,  /* "Received UNKNOWN pkt from %s:%d, type=0x%02X, seq=%u, len=%d" (%s,%d,%u,%d)  [p2p.c] */
    LA_F302,  /* "Received UNKNOWN pkt type: 0x%02X"  [p2p.c] */
    LA_F303,  /* "Received remote candidate: type=%d, address=%s:%d" (%d,%s,%d)  [p2p_signal_pubsub.c] */
    LA_F304,  /* "Received valid signal from '%s'" (%s)  [p2p_signal_pubsub.c] */
    LA_F305,  /* "Recv %s pkt from %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F306,  /* "Recv %s pkt from %s:%d echo_seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F307,  /* "Recv %s pkt from %s:%d seq=%u" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F308,  /* "Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x" (%s,%s,%d,%u)  [p2p_nat.c] */
    LA_F309,  /* "Recv %s pkt from %s:%d, seq=%u, len=%d" (%s,%s,%d,%u,%d)  [p2p_nat.c] */
    LA_F310,  /* disabled "Recv New Remote Candidate<%s:%d> (Peer Reflexive - symmetric NAT)" */
    LA_F311,  /* disabled "Recv New Remote Candidate<%s:%d> (type=%d)" */
    LA_F312,  /* "Register to COMPACT signaling server at %s:%d" (%s,%d)  [p2p.c] */
    LA_F313,  /* "Reliable transport initialized rto=%d win=%d" (%d,%d)  [p2p_trans_reliable.c] */
    LA_F314,  /* "Requested Relay Candidate from TURN %s" (%s)  [p2p.c] */
    LA_F315,  /* disabled "Requested Relay Candidate from TURN %s" */
    LA_F316,  /* "Requested Srflx Candidate from %s" (%s)  [p2p.c] */
    LA_F317,  /* "Resolve COMPACT signaling server address: %s:%d failed(%d)" (%s,%d,%d)  [p2p.c] */
    LA_F318,  /* "% SCTP (usrsctp) requested but library not linked"  [p2p.c] */
    LA_F319,  /* "% SIGNALING path but signaling relay not available"  [p2p_channel.c] */
    LA_F320,  /* "% SIGNALING path enabled (server supports relay)\n"  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F321,  /* "% SSL_CTX_new failed"  [p2p_dtls_openssl.c] */
    LA_F322,  /* "% SSL_new failed"  [p2p_dtls_openssl.c] */
    LA_F323,  /* "Start RELAY session failed(%d)" (%d)  [p2p.c] */
    LA_F324,  /* "Send window full, dropping packet send_count=%d" (%d)  [p2p_trans_reliable.c] */
    LA_F325,  /* "Sending Allocate Request to %s:%d" (%s,%d)  [p2p_turn.c] */
    LA_F326,  /* "% Sending FIN packet to peer before closing"  [p2p.c] */
    LA_F327,  /* "Sending Test I to %s:%d (len=%d)" (%s,%d,%d)  [p2p_stun.c] */
    LA_F328,  /* "% Sending UNREGISTER packet to COMPACT signaling server"  [p2p.c] */
    LA_F329,  /* disabled "% RELAY mode: bidirectional candidate exchange, no explicit reply needed" */
    LA_F330,  /* disabled "Sent answer to '%s' (%d bytes)" */
    LA_F331,  /* disabled "Sent connect request to '%s' (%d bytes)" */
    LA_F332,  /* "Starting RELAY session with %s" (%s)  [p2p.c] */
    LA_F333,  /* "% Signal payload deserialization failed"  [p2p_signal_pubsub.c] */
    LA_F334,  /* "% Skipping Host Candidate gathering (disabled)"  [p2p.c] */
    LA_F335,  /* disabled "% Skipping local Host candidates (disabled)" */
    LA_F336,  /* "Start internal thread failed(%d)" (%d)  [p2p.c] */
    LA_F337,  /* "% Starting internal thread"  [p2p.c] */
    LA_F338,  /* "% State: → LOST (all paths failed)"  [p2p.c] */
    LA_F339,  /* disabled "% State: CONNECTED → RELAY (path lost)" */
    _LA_340,
    LA_F341,  /* "State: LOST → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F342,  /* "State: RELAY → CONNECTED, path=PUNCH[%d]" (%d)  [p2p.c] */
    LA_F343,  /* "State: → CONNECTED, path[%d]" (%d)  [p2p.c] */
    LA_F344,  /* "% State: → ERROR (punch timeout, no relay available)"  [p2p.c] */
    LA_F345,  /* "% State: → PUNCHING"  [p2p.c] */
    _LA_346,
    _LA_347,
    LA_F348,  /* "State: → RELAY, path[%d]" (%d)  [p2p.c] */
    LA_F349,  /* "% Stopping internal thread"  [p2p.c] */
    LA_F350,  /* disabled "% Storage full, waiting for peer to come online" */
    LA_F351,  /* disabled "% Synced path after failover" */
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
    LA_F368,  /* disabled "UDP hole-punch probing remote candidates (%d candidates)" */
    LA_F369,  /* disabled "UDP hole-punch probing remote candidates round %d/%d" */
    LA_F370,  /* disabled "Unknown ACK status %d" */
    LA_F371,  /* "Unknown signaling mode: %d" (%d)  [p2p.c] */
    LA_F372,  /* "Updating Gist field '%s'..." (%s)  [p2p_signal_pubsub.c] */
    LA_F373,  /* disabled "Waiting for peer '%s' timed out (%dms), giving up" */
    LA_F374,  /* "[MbedTLS] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_mbedtls.c] */
    LA_F375,  /* "% [OpenSSL] DTLS handshake completed"  [p2p_dtls_openssl.c] */
    LA_F376,  /* "[OpenSSL] DTLS role: %s (mode=%s)" (%s,%s)  [p2p_dtls_openssl.c] */
    LA_F377,  /* "[SCTP] association lost/shutdown (state=%u)" (%u)  [p2p_trans_sctp.c] */
    LA_F378,  /* "[SCTP] bind failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F379,  /* "[SCTP] connect failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F380,  /* "[SCTP] sendv failed: %s" (%s)  [p2p_trans_sctp.c] */
    LA_F381,  /* disabled "[SIGNALING] Failed to send candidates, will retry (ret=%d)" */
    LA_F382,  /* disabled "[SIGNALING] Sent candidates (cached, peer offline) %d to %s" */
    LA_F383,  /* disabled "[SIGNALING] Sent candidates, forwarded [%d-%d] to %s (forwarded=%d)" */
    LA_F384,  /* disabled "% [SIGNALING] Server storage full, waiting for peer to come online" */
    LA_F385,  /* disabled "[Trickle] Immediately probing new candidate %s:%d" */
    LA_F386,  /* disabled "% [Trickle] Candidate queued, will be uploaded by tick_send" */
    LA_F387,  /* disabled "% [Trickle] RELAY not ready, skipping single candidate send" */
    LA_F388,  /* disabled "[Trickle] TCP send failed (ret=%d), will be retried by p2p_update()" */
    LA_F389,  /* "[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F390,  /* "[UDP] %s recv from %s:%d, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F391,  /* "[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F392,  /* "[UDP] %s recv from %s:%d, seq=%u, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F393,  /* disabled "[TCP] %s recv, len=%d\n" */
    LA_F394,  /* disabled "[TCP] %s send failed\n" */
    LA_F395,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n" (%s,%s,%d,%u)  [p2p_signal_compact.c] */
    LA_F396,  /* "[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F397,  /* disabled "[TCP] %s send to %s:%d, len=%zu\n" */
    LA_F398,  /* "[UDP] %s_ACK send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F399,  /* "[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F400,  /* "[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F401,  /* "[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n" (%s,%s,%d,%u,%d)  [p2p_signal_compact.c] */
    LA_F402,  /* "congestion detected, new ssthresh: %u, cwnd: %u" (%u,%u)  [p2p_trans_pseudotcp.c] */
    LA_F403,  /* "% connection closed by peer"  [p2p.c] */
    LA_F404,  /* "ctr_drbg_seed failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F405,  /* disabled "% p2p_ice_send_local_candidate called in non-RELAY mode" */
    LA_F406,  /* "Imported %d candidates from SDP" (%d)  [p2p_ice.c] */
    LA_F407,  /* "Imported SDP candidate: %s:%d typ %s (priority=0x%08x)" (%s,%d,%s)  [p2p_ice.c] */
    LA_F408,  /* "Invalid IP address: %s" (%s)  [p2p_ice.c] */
    LA_F409,  /* "Unknown candidate type: %s" (%s)  [p2p_ice.c] */
    LA_F410,  /* "Exported %d candidates to SDP (%d bytes)" (%d,%d)  [p2p_ice.c] */
    LA_F411,  /* "Failed to parse SDP candidate line: %s" (%s)  [p2p_ice.c] */
    LA_F412,  /* "% SDP export buffer overflow"  [p2p_ice.c] */
    LA_F413,  /* "% WebRTC candidate export buffer overflow"  [p2p_ice.c] */
    LA_F414,  /* "ssl_setup failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F415,  /* disabled "starting NAT punch(Host candidate %d)" */
    LA_F416,  /* "transport send_data failed, %d bytes dropped" (%d)  [p2p.c] */
    LA_F417,  /* "✓ Gathered Srflx Candidate Added Remote Candidate %s:%d (priority=%u)" (%s,%d,%u)  [p2p_stun.c] */
    LA_F418,  /* "% ✗ Add Srflx candidate failed(OOM)"  [p2p_stun.c] */
    LA_F419,  /* "%s: invalid payload len=%d (need 6)" (%s,%d)  [p2p_nat.c] */
    LA_F420,  /* "Ignore %s pkt from %s:%d, not connected" (%s,%s,%d)  [p2p_nat.c] */
    LA_F421,  /* "Ignore %s pkt from %s:%d, not connecting" (%s,%s,%d)  [p2p_nat.c] */
    LA_F422,  /* "Recv ICE-STUN Binding Request from candidate %d (%s:%d)" (%d,%s,%d)  [p2p_nat.c] */
    LA_F423,  /* "%s: remote %s cand[%d]<%s:%d> (disabled)\n" (%s,%s,%d,%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F424,  /* "%s: peer online, starting NAT punch\n" (%s)  [p2p_signal_compact.c] */
    LA_F425,  /* "%s: punch timeout, fallback punching using signaling relay" (%s)  [p2p_nat.c] */
    LA_F426,  /* "%s: cand[%d]<%s:%d> send packet failed(%d)" (%s,%d,%s,%d,%d)  [p2p_nat.c] */
    LA_F427,  /* "%s sent via signaling relay" (%s)  [p2p_nat.c] */
    LA_F428,  /* "%s: reaching cand[%d] via signaling relay, seq=%u" (%s,%d,%u)  [p2p_nat.c] */
    LA_F429,  /* "% RELAY path but TURN not allocated"  [p2p_channel.c] */
    LA_F430,  /* "% LOST recovery: NAT connected but no path available"  [p2p.c] */
    LA_F431,  /* disabled "path[%d] addr is NULL" */
    LA_F432,  /* disabled "path[%d] addr is NULL (LOST recovery)" */
    LA_F433,  /* disabled "path[%d] addr is NULL (RELAY recovery)" */
    LA_F434,  /* "% RELAY path but TURN not allocated (dtls)"  [p2p_channel.c] */
    LA_F435,  /* "%s: CONN ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F436,  /* "%s: CONN_ACK ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F437,  /* "%s: ignored, upsert %s:%d failed" (%s,%s,%d)  [p2p_nat.c] */
    LA_F438,  /* "%s: PUNCHING → RELAY (peer CONNECTING)" (%s)  [p2p_nat.c] */
    LA_F439,  /* "Ignore %s pkt from %s:%d, valid state(%d)" (%s,%s,%d,%d)  [p2p_nat.c] */
    LA_F440,  /* "% NAT connected but no available path in path manager"  [p2p.c] */
    LA_F441,  /* "% RELAY recovery: NAT connected but no path available"  [p2p.c] */
    LA_F442,  /* disabled "Invalid state (%d): LOST but active_path=%d, path_type=%d" */
    LA_F443,  /* "%s: unknown target cand %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F444,  /* disabled "best_path<0 && active_path=%d, signaling=%d, state=%d\n" */
    LA_F445,  /* disabled "s->active_path=%d, path_stats.state=%d" */
    LA_F446,  /* "%s: no rpc request\n" (%s)  [p2p_signal_compact.c] */
    LA_F447,  /* "% MSG RPC not supported by server\n"  [p2p_signal_compact.c] */
    LA_F448,  /* disabled "%s sent FIN\n" */
    LA_F449,  /* disabled "%s sent, name='%s' target='%s'\n" */
    LA_F450,  /* "%s sent, sid=%u, msg=%u, size=%d\n" (%s,%u,%u,%d)  [p2p_signal_compact.c] */
    LA_F451,  /* disabled "%s sent, target='%s'\n" */
    LA_F452,  /* disabled "%s sent\n" */
    LA_F453,  /* "%s: accepted, public=%s:%d ses_id=%llu max_cands=%d probe_port=%d relay=%s msg=%s\n" (%s,%s,%d,%l,%d,%d,%s,%s)  [p2p_signal_compact.c] */
    LA_F454,  /* disabled "%s: accepted, ses_id=%llu peer=%s\n" */
    LA_F455,  /* "%s: bad payload(%d)\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F456,  /* disabled "%s: cached (peer offline)\n" */
    LA_F457,  /* disabled "%s: duplicate remote cand<%s:%d>, skipped\n" */
    LA_F458,  /* disabled "%s: forwarded to peer\n" */
    LA_F459,  /* disabled "%s: processed, remote_cand_cnt=%d\n" */
    LA_F460,  /* disabled "%s: received FIN from peer\n" */
    LA_F461,  /* disabled "%s: remote_cands[] full, skipped %d candidates\n" */
    LA_F462,  /* disabled "%s: session mismatch(local=%llu recv=%llu)\n" */
    LA_F463,  /* "%s: status error(%d)\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F464,  /* disabled "%s: storage full, stop uploading\n" */
    LA_F465,  /* disabled "%s: unknown status %d\n" */
    LA_F466,  /* disabled "Bad magic 0x%08x\n" */
    LA_F467,  /* disabled "% CONNECT timeout, max retries reached\n" */
    LA_F468,  /* disabled "CONNECT timeout, retrying (%d/%d)\n" */
    LA_F469,  /* disabled "Connecting to %s:%d\n" */
    LA_F470,  /* disabled "Failed to allocate %d bytes for payload\n" */
    LA_F471,  /* disabled "% Failed to create TCP socket\n" */
    LA_F472,  /* disabled "% ONLINE: ready to start session\n" */
    LA_F473,  /* disabled "% ONLINE_ACK timeout\n" */
    LA_F474,  /* disabled "% READY: candidate exchange completed\n" */
    LA_F475,  /* "REGISTERED: peer=%s\n" (%s)  [p2p_signal_compact.c] */
    LA_F476,  /* disabled "% TCP connect failed (select error)\n" */
    LA_F477,  /* disabled "% TCP connect failed\n" */
    LA_F478,  /* disabled "% TCP connected, sending ONLINE\n" */
    LA_F479,  /* disabled "% TCP connection closed by peer\n" */
    LA_F480,  /* disabled "% TCP recv error\n" */
    LA_F481,  /* disabled "% Trickle TURN: uploading new candidates\n" */
    LA_F482,  /* disabled "Unknown message type %d\n" */
    LA_F483,  /* disabled "[TCP] %s send header failed\n" */
    LA_F484,  /* disabled "[TCP] %s send payload failed\n" */
    LA_F485,  /* disabled "[TCP] %s send to %s:%d, target='%s'\n" */
    LA_F486,  /* disabled "[TCP] %s send, ses_id=%llu cand_cnt=%d fin=%d\n" */
    LA_F487,  /* "[UDP] %s recv from %s:%d\n" (%s,%s,%d)  [p2p_signal_compact.c] */
    LA_F488,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F489,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [p2p_signal_compact.c] */
    LA_F490,  /* "%s enqueued, ses_id=%llu\n" (%s,%l)  [p2p_signal_relay.c] */
    LA_F491,  /* "%s enqueued, name='%s', rid=%u\n" (%s,%s,%u)  [p2p_signal_relay.c] */
    LA_F492,  /* "%s enqueued, target='%s'\n" (%s,%s)  [p2p_signal_relay.c] */
    LA_F493,  /* "%s enqueued\n" (%s)  [p2p_signal_relay.c] */
    LA_F494,  /* "%s sent %d candidates, next_idx=%d\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F495,  /* "%s sent FIN\n" (%s)  [p2p_signal_relay.c] */
    LA_F496,  /* "%s: accepted, relay=%s msg=%s cand_max=%d\n" (%s,%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F497,  /* "%s: accepted, ses_id=%llu peer=%s\n" (%s,%l,%s)  [p2p_signal_relay.c] */
    LA_F498,  /* "%s: bad payload len=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F499,  /* disabled "%s: cached (peer offline)\n" */
    LA_F500,  /* "%s: duplicate remote cand<%s:%d>, skipped\n" (%s,%s,%d)  [p2p_signal_relay.c] */
    LA_F501,  /* disabled "%s: error, target not found\n" */
    LA_F502,  /* "%s: all candidates delivered to peer (fwd=0 after FIN)\n" (%s)  [p2p_signal_relay.c] */
    LA_F503,  /* "%s: processed, remote_cand_cnt=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F504,  /* "%s: peer closed session %llu\n" (%s,%l)  [p2p_signal_relay.c] */
    LA_F505,  /* "%s: remote_cands[] full, skipped %d candidates\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F506,  /* "%s: send buffer busy, skip\n" (%s)  [p2p_signal_relay.c] */
    LA_F507,  /* "%s: send buffer busy, will retry\n" (%s)  [p2p_signal_relay.c] */
    LA_F508,  /* "%s: invalid online=%u, normalized to 0\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F509,  /* disabled "%s: storage full, stop uploading\n" */
    LA_F510,  /* disabled "%s: unknown status %d\n" */
    LA_F511,  /* disabled "% CONNECT timeout, max retries reached\n" */
    LA_F512,  /* disabled "CONNECT timeout, retrying (%d/%d)\n" */
    LA_F513,  /* "Connecting to %s:%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F514,  /* "% EXCHANGING: first sync received, peer online\n"  [p2p_signal_relay.c] */
    LA_F515,  /* disabled "Failed to allocate %d bytes for payload\n" */
    LA_F516,  /* "% Failed to create TCP socket\n"  [p2p_signal_relay.c] */
    LA_F517,  /* "% Failed to set socket non-blocking\n"  [p2p_signal_relay.c] */
    LA_F518,  /* "% ONLINE: ready to start session\n"  [p2p_signal_relay.c] */
    LA_F519,  /* "%s timeout\n" (%s)  [p2p_signal_relay.c] */
    LA_F520,  /* "% READY: candidate exchange completed\n"  [p2p_signal_relay.c] */
    LA_F521,  /* "% TCP connect failed (select error)\n"  [p2p_signal_relay.c] */
    LA_F522,  /* "% TCP connect failed\n"  [p2p_signal_relay.c] */
    LA_F523,  /* "% TCP connected immediately, sending ONLINE\n"  [p2p_signal_relay.c] */
    LA_F524,  /* "% TCP connected, sending ONLINE\n"  [p2p_signal_relay.c] */
    LA_F525,  /* "% TCP connection closed by peer\n"  [p2p_signal_relay.c] */
    LA_F526,  /* "% TCP recv error\n"  [p2p_signal_relay.c] */
    LA_F527,  /* "% TCP connection closed during send\n"  [p2p_signal_relay.c] */
    LA_F528,  /* disabled "% Trickle TURN: uploading new candidates\n" */
    LA_F529,  /* "Unknown message type %d\n" (%d)  [p2p_signal_relay.c] */
    LA_F530,  /* "%s: req_type=%u code=%u msg=%.*s\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F531,  /* "%s: fatal error code=%u, entering ERROR state\n" (%s,%u)  [p2p_signal_relay.c] */
    LA_F532,  /* "% No shared local route addresses available, host candidates skipped"  [p2p.c] */
    LA_F533,  /* "[TCP] %s recv, len=%d\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F534,  /* "% TCP send error\n"  [p2p_signal_relay.c] */
    LA_F535,  /* "% Waiting for RELAY server ONLINE_ACK"  [p2p.c] */
    LA_F536,  /* "% Disconnected, back to ONLINE state\n"  [p2p_signal_relay.c] */
    LA_F537,  /* disabled "disconnect: not in session (state=%d)\n" */
    LA_F538,  /* "payload size %u exceeds limit %u\n" (%u,%u)  [p2p_signal_relay.c] */
    LA_F539,  /* "[TCP] %s enqueue, ses_id=%llu\n" (%s,%l)  [p2p_signal_relay.c] */
    LA_F540,  /* "retry seq=%u retx=%d rto=%d" (%u,%d,%d)  [p2p_trans_reliable.c] */
    LA_F541,  /* "send ACK ack_seq=%u sack=0x%08x recv_base=%u to %s:%d" (%u,%u,%s,%d)  [p2p_trans_reliable.c] */
    LA_F542,  /* "% Full SDP generation requires ice_ufrag and ice_pwd"  [p2p_ice.c] */
    LA_F543,  /* "% Buffer size < 2048 may be insufficient for full SDP"  [p2p_ice.c] */
    LA_F544,  /* "Recv ICE-STUN from %s:%d, upsert prflx failed" (%s,%d)  [p2p_nat.c] */
    LA_F545,  /* "ssl_config_defaults failed: -0x%x" (%x)  [p2p_dtls_mbedtls.c] */
    LA_F546,  /* disabled "ICE connectivity check: both directions confirmed (candidate %d)" */
    LA_F547,  /* "Recv ICE-STUN Binding Response from candidate %d (%s:%d)" (%d,%s,%d)  [p2p_nat.c] */
    LA_F548,  /* "%s: PUNCHING → %s (peer CONNECTING)" (%s,%s)  [p2p_nat.c] */
    LA_F549,  /* "%s: path tx UP" (%s)  [p2p_nat.c] */
    LA_F550,  /* "Recv unknown ICE-STUN msg_type=0x%04x from %s:%d" (%s,%d)  [p2p_nat.c] */
    LA_F551,  /* "%s: peer disconnected (ses_id=%llu), reset to REGISTERED\n" (%s,%l)  [p2p_signal_compact.c] */
    LA_F552,  /* "Ignore %s pkt from unknown path %s:%d" (%s,%s,%d)  [p2p_nat.c] */
    LA_F553,  /* "% Sending Test II with CHANGE-REQUEST(IP+PORT)"  [p2p_stun.c] */
    LA_F554,  /* "% Sending Test III with CHANGE-REQUEST(PORT only)"  [p2p_stun.c] */
    LA_F555,  /* "Test I(alt): Mapped address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F556,  /* "% Sending Test I(alt) to CHANGED-ADDRESS"  [p2p_stun.c] */
    LA_F557,  /* "% Failed to send Test I(alt), continue to Test III"  [p2p_stun.c] */
    LA_F558,  /* "% No valid CHANGED-ADDRESS provided by STUN server, skipping Test I(alt)"  [p2p_stun.c] */
    LA_F559,  /* "% Test I(alt): Timeout"  [p2p_stun.c] */
    LA_F560,  /* "Test I: Changed address: %s:%d" (%s,%d)  [p2p_stun.c] */
    LA_F561,  /* "%s: bad payload(len=%d cand_cnt=%d)\n" (%s,%d,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F562,  /* "%s: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_compact.c, p2p_signal_relay.c] */
    LA_F563,  /* "%s: forwarded=%d, next_idx adjusted to %d\n" (%s,%d,%d)  [p2p_signal_relay.c] */
    LA_F564,  /* "%s: received FIN marker from peer\n" (%s)  [p2p_signal_relay.c] */
    LA_F565,  /* "%s: req_type=%u code=%u\n" (%s,%u,%u)  [p2p_signal_relay.c] */
    LA_F566,  /* "%s: session busy, will retry\n" (%s)  [p2p_signal_relay.c] */
    LA_F567,  /* "%s: session mismatch(local=%llu recv=%llu)\n" (%s,%l,%l)  [p2p_signal_relay.c] */
    LA_F568,  /* "%s: unexpected fwd=%d after FIN, ignored\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F569,  /* "%s: waiting for STUN candidates, stun_pending=%d\n" (%s,%d)  [p2p_signal_compact.c] */
    LA_F570,  /* "%s: waiting for initial STUN candidates before sending local queue\n" (%s)  [p2p_signal_compact.c] */
    LA_F571,  /* "EXCHANGING: peer=%s, uploading candidates\n" (%s)  [p2p_signal_relay.c] */
    LA_F572,  /* "% EXCHANGING: waiting for initial STUN/TURN candidates before upload\n"  [p2p_signal_relay.c] */
    LA_F573,  /* "Trickle TURN: batch full (%d cands), sending\n" (%d)  [p2p_signal_relay.c] */
    LA_F574,  /* "Trickle TURN: batch timeout (%d cands), sending\n" (%d)  [p2p_signal_relay.c] */
    LA_F575,  /* "% WAIT_PEER: peer went offline, waiting for reconnect\n"  [p2p_signal_relay.c] */
    LA_F576,  /* "% WAIT_PEER: session established, waiting for peer info\n"  [p2p_signal_relay.c] */
    LA_F577,  /* "[TCP] %s enqueue, name='%s', rid=%u\n" (%s,%s,%u)  [p2p_signal_relay.c] */
    LA_F578,  /* "[TCP] %s enqueue, ses_id=%llu cand_cnt=%d fin=%d\n" (%s,%l,%d,%d)  [p2p_signal_relay.c] */
    LA_F579,  /* "[TCP] %s enqueue, target='%s'\n" (%s,%s)  [p2p_signal_relay.c] */
    LA_F580,  /* "%s: session renewed by peer SYNC0 (local=%llu recv=%llu)\n" (%s,%l,%l)  [p2p_signal_relay.c] */
    LA_F581,  /* "% EXCHANGING: session reset by peer SYNC0\n"  [p2p_signal_relay.c] */
    LA_F582,  /* "%s: data relay ready, flow control released\n" (%s)  [p2p_signal_relay.c] */
    LA_F583,  /* "RELAY %s enqueued: ses_id=%llu seq=%u len=%d\n" (%s,%l,%u,%d)  [p2p_signal_relay.c] */
    LA_F584,  /* "RELAY %s recv: bad payload(len=%d)\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F585,  /* "RELAY %s recv: session mismatch(local=%llu recv=%llu)\n" (%s,%l,%l)  [p2p_signal_relay.c] */
    LA_F586,  /* "RELAY %s: payload too large (%d)\n" (%s,%d)  [p2p_signal_relay.c] */
    LA_F587,  /* "RELAY %s: send buffer busy\n" (%s)  [p2p_signal_relay.c] */
    LA_F588,  /* "RELAY ACK recv: ack_seq=%u sack=0x%08x\n" (%u)  [p2p_signal_relay.c] */
    LA_F589,  /* "RELAY ACK recv: invalid payload len=%d\n" (%d)  [p2p_signal_relay.c] */
    LA_F590,  /* "% RELAY CRYPTO recv: no DTLS context\n"  [p2p_signal_relay.c] */
    LA_F591,  /* "RELAY DATA recv: seq=%u len=%d\n" (%u,%d)  [p2p_signal_relay.c] */
    LA_F592,  /* "% RELAY data throttled: awaiting READY\n"  [p2p_signal_relay.c] */
    LA_F593,  /* "RELAY data: unsupported type 0x%02x\n"  [p2p_signal_relay.c] */
    LA_F594,  /* "RELAY recv: unexpected inner type 0x%02x\n"  [p2p_signal_relay.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F42

#endif /* LANG_H__ */
