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
    LA_W1,  /* "disabled"  [server.c] */
    LA_W2,  /* "enabled"  [server.c] */

    /* Strings (LA_S) */
    LA_S3,  /* "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)"  [server.c] */
    LA_S4,  /* "Enable data relay support (COMPACT mode fallback)"  [server.c] */
    LA_S5,  /* "Enable MSG RPC support"  [server.c] */
    LA_S6,  /* "Enable WebSocket service on same TCP port"  [server.c] */
    LA_S7,  /* "NAT type detection port (0=disabled)"  [server.c] */
    LA_S8,  /* "Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S9,  /* "Shutting down...\n"  [server.c] */
    LA_S10,  /* "Signaling server listen port (TCP+UDP)"  [server.c] */
    LA_S11,  /* "Use Chinese language"  [server.c] */
    LA_S12,  /* "WebSocket dedicated port (also enables --ws)"  [server.c] */

    /* Formats (LA_F) */
    LA_F13,  /* "%.*s %s, auth_key=%llu\n" (%s,%l)  [p2p_compact.c] */
    LA_F14,  /* "%.*s %s: accepted, inst_id=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F15,  /* "%.*s %s: alloc client failed\n" (%s)  [p2p_compact.c] */
    LA_F16,  /* "%.*s %s: invalid instance_id=0\n" (%s)  [p2p_compact.c] */
    LA_F17,  /* "%.*s %s: realloc client\n" (%s)  [p2p_compact.c] */
    LA_F18,  /* "%s %s accepted, auth_key=%llu\n" (%s,%s,%l)  [p2p_compact.c] */
    LA_F19,  /* "%s %s accepted, free slot\n" (%s,%s)  [p2p_compact.c] */
    LA_F20,  /* "%s %s accepted, seq=%u, ses_id=%u\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F21,  /* "%s %s bad payload(len=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F22,  /* "%s %s invalid auth_key=%llu\n" (%s,%s,%l)  [p2p_compact.c] */
    LA_F23,  /* "%s %s invalid auth_key=0\n" (%s,%s)  [p2p_compact.c] */
    LA_F24,  /* "%s %s: 2nd-ack confirmed (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F25,  /* "%s %s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n" (%s,%s,%u,%u,%u,%d)  [p2p_compact.c] */
    LA_F26,  /* "%s %s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n" (%s,%s,%u,%u,%u,%d)  [p2p_compact.c] */
    LA_F27,  /* "%s %s: accepted, ses_id=%u, sid=%u\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F28,  /* "%s %s: addr-notify confirmed (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F29,  /* "%s %s: auth_key=%llu, cands=%d\n" (%s,%s,%l,%d)  [p2p_compact.c] */
    LA_F30,  /* "%s %s: build session failed\n" (%s,%s)  [p2p_compact.c] */
    LA_F31,  /* "%s %s: complete, sid=%u (ses_id=%u)\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F32,  /* "%s %s: confirmed, retries=%d (ses_id=%u)\n" (%s,%s,%d,%u)  [p2p_compact.c] */
    LA_F33,  /* "%s %s: duplicate SYN0 with different candidates\n" (%s,%s)  [p2p_compact.c] */
    LA_F34,  /* "%s %s: no matching pending rpc (sid=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F35,  /* "%s %s: paired with '%.*s'\n" (%s,%s)  [p2p_compact.c] */
    LA_F36,  /* "%s %s: relay failed (peer unavailable), seq=%u (ses_id=%u)\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F37,  /* "%s %s: unavailable peer (sess_id=%u), dropped\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F38,  /* "%s %s: unavailable peer (sess_id=%u), rejected\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F39,  /* "%s -> %s %s (ses_id=%u)\n" (%s,%s,%s,%u)  [p2p_compact.c] */
    LA_F40,  /* "%s -> %s %s accepted, ses_id=%u\n" (%s,%s,%s,%u)  [p2p_compact.c] */
    LA_F41,  /* "%s -> %s %s resent %d/%d, sid=%u (ses_id=%u)\n" (%s,%s,%s,%d,%d,%u,%u)  [p2p_compact.c] */
    LA_F42,  /* "%s -> %s %s seq=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F43,  /* "%s -> %s %s timeout after %d retries, sid=%u (ses_id=%u)\n" (%s,%s,%s,%d,%u,%u)  [p2p_compact.c] */
    LA_F44,  /* "%s -> %s %s: forwarded, sid=%u, msg=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u,%u)  [p2p_compact.c] */
    LA_F45,  /* "%s -> %s %s: new sid=%u last=%u (responding=%d), canceling old RPC (ses_id=%u)\n" (%s,%s,%s,%u,%u,%d,%u)  [p2p_compact.c] */
    LA_F46,  /* "%s -> %s %s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F47,  /* "%s -> %s %s: obsolete sid=%u (last=%u), ignoring\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F48,  /* "%s -> %s %s: retransmit & resend ACK, sid=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F49,  /* "%s -> %s %s: retransmit during RSP phase, ignoring, sid=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F50,  /* "%s -> %s relay %s, seq=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F51,  /* "%s <- %s %s gave up after %d retries, sid=%u (ses_id=%u)\n" (%s,%s,%s,%d,%u,%u)  [p2p_compact.c] */
    LA_F52,  /* "%s <- %s %s resent %d/%d, sid=%u (ses_id=%u)\n" (%s,%s,%s,%d,%d,%u,%u)  [p2p_compact.c] */
    LA_F53,  /* "%s <- %s %s: backward, sid=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F54,  /* "%s <- %s %s: no matching pending rpc (sid=%u, expected=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F55,  /* "%s <> %s %s gave up after %d tries, (ses_id=%u)\n" (%s,%s,%s,%d,%u)  [p2p_compact.c] */
    LA_F56,  /* "%s <> %s %s resent %d/%d, (ses_id=%u)\n" (%s,%s,%s,%d,%d,%u)  [p2p_compact.c] */
    LA_F57,  /* "%s Unknown pkt type 0x%02x\n" (%s)  [p2p_compact.c] */
    LA_F58,  /* "%s addr changed, but first info packet was abandoned (ses_id=%u)\n" (%s,%u)  [p2p_compact.c] */
    LA_F59,  /* "%s addr changed, defer notification until first ACK (ses_id=%u)\n" (%s,%u)  [p2p_compact.c] */
    LA_F60,  /* "%s addr changed, deferred notifying (ses_id=%u)\n" (%s,%u)  [p2p_compact.c] */
    LA_F61,  /* "%s addr changed, notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F62,  /* "%s for unknown ses_id=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F63,  /* "%s send %s, auth_key=%llu\n" (%s,%s,%l)  [p2p_compact.c] */
    LA_F64,  /* "%s send %s: base_index=%u, cands=%d, ses_id=%u\n" (%s,%s,%u,%d,%u)  [p2p_compact.c] */
    LA_F65,  /* "%s send %s: cands=%d, ses_id=%u\n" (%s,%s,%d,%u)  [p2p_compact.c] */
    LA_F66,  /* "%s send %s: reason=%s, ses_id=%u\n" (%s,%s,%s,%u)  [p2p_compact.c] */
    LA_F67,  /* "%s send %s: ses_id=%u, peer=%s\n" (%s,%s,%u,%s)  [p2p_compact.c] */
    LA_F68,  /* "%s send %s: ses_id=%u, sid=%u, flags=0x%02x, code=%u, data_len=%d, retries=%d\n" (%s,%s,%u,%u,%u,%d,%d)  [p2p_compact.c] */
    LA_F69,  /* "%s send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, retries=%d\n" (%s,%s,%u,%u,%u,%d,%d)  [p2p_compact.c] */
    LA_F70,  /* "%s send %s: ses_id=%u, sid=%u, status=%u\n" (%s,%s,%u,%u,%u)  [p2p_compact.c] */
    LA_F71,  /* "%s send %s: ses_id=%u, sid=%u\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F72,  /* "%s: %s <-> %s forward\n" (%s,%s,%s)  [p2p_relay.c] */
    LA_F73,  /* "%s: '%.*s' new REG (inst=%u)\n" (%s,%u)  [p2p_relay.c] */
    LA_F74,  /* "%s: '%.*s' reconnected & reactive (inst=%u)\n" (%s,%u)  [p2p_relay.c] */
    LA_F75,  /* "%s: '%.*s' reconnected & renew (inst=%u)\n" (%s,%u)  [p2p_relay.c] */
    LA_F76,  /* "%s: '%s' -> '%s' (%zu bytes)\n" (%s,%s,%s)  [p2p_wss.c] */
    LA_F77,  /* "%s: '%s' -> '%s' created (id=%u, peer_zombie)\n" (%s,%s,%s,%u)  [p2p_wss.c] */
    LA_F78,  /* "%s: '%s' <-> '%s' paired (ses=%u/%u)\n" (%s,%s,%s,%u,%u)  [p2p_wss.c] */
    LA_F79,  /* "%s: '%s' new REG (inst=%u)\n" (%s,%s,%u)  [p2p_wss.c] */
    LA_F80,  /* "%s: '%s' reconnected & reactive (inst=%u)\n" (%s,%s,%u)  [p2p_wss.c] */
    LA_F81,  /* "%s: '%s' reconnected & renew (inst=%u)\n" (%s,%s,%u)  [p2p_wss.c] */
    LA_F82,  /* "%s: '%s' ses_id=%u\n" (%s,%s,%u)  [p2p_wss.c] */
    LA_F83,  /* "%s: '%s' sid=%u code=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [p2p_wss.c, p2p_relay.c] */
    LA_F84,  /* "%s: '%s' sid=%u msg=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [p2p_wss.c, p2p_relay.c] */
    LA_F85,  /* "%s: '%s' unreachable, pending\n" (%s,%s)  [p2p_wss.c] */
    LA_F86,  /* "%s: '%s'\n" (%s,%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F87,  /* "%s: OOM building session '%s' -> '%s'\n" (%s,%s,%s)  [p2p_wss.c] */
    LA_F88,  /* "%s: alloc buf failed(OOM)\n" (%s)  [p2p_wss.c] */
    LA_F89,  /* "%s: alloc buffer failed(OOM)\n" (%s)  [p2p_relay.c] */
    LA_F90,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [p2p_relay.c] */
    LA_F91,  /* "%s: bad frame len=%u\n" (%s,%u)  [p2p_wss.c] */
    LA_F92,  /* "%s: bad payload(%u)\n" (%s,%u)  [p2p_relay.c] */
    LA_F93,  /* "%s: bad payload(cnt=%d, len=%u, expected=%u)\n" (%s,%d,%u,%u)  [p2p_relay.c] */
    LA_F94,  /* "%s: bad payload(len=%u)\n" (%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F95,  /* "%s: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n" (%s,%u,%u,%u,%u)  [p2p_relay.c] */
    LA_F96,  /* "%s: bad payload(sid=0)\n" (%s)  [p2p_relay.c] */
    LA_F97,  /* "%s: build session to '%s' failed(%d)\n" (%s,%s,%d)  [p2p_wss.c, p2p_relay.c] */
    LA_F98,  /* "%s: build session to '%s' failed(OOM)" (%s,%s)  [p2p_relay.c] */
    LA_F99,  /* "%s: busy (ses_id=%u), pending\n" (%s,%u)  [p2p_relay.c] */
    LA_F100,  /* "%s: busy (ses_id=%u, sid=%u), pending\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F101,  /* "%s: close ses_id=%u\n" (%s,%u)  [p2p_relay.c] */
    LA_F102,  /* "%s: deprecated (ses_id=%u, sid=%u), drop\n" (%s,%u,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F103,  /* "%s: deprecated (ses_id=%u, sid=%u, last=%u), discarding\n" (%s,%u,%u,%u)  [p2p_relay.c] */
    LA_F104,  /* "%s: deprecated (ses_id=%u, sid=%u, last=%u), drop\n" (%s,%u,%u,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F105,  /* "%s: duplicate SYN0 (ses_id=%u), resend ACK\n" (%s,%u)  [p2p_relay.c] */
    LA_F106,  /* "%s: duplicate SYN0 (ses_id=%u), resend response\n" (%s,%u)  [p2p_wss.c] */
    LA_F107,  /* "%s: duplicate from '%s'\n" (%s,%s)  [p2p_relay.c] */
    LA_F108,  /* "%s: invalid REG format\n" (%s)  [p2p_wss.c] */
    LA_F109,  /* "%s: invalid instance id\n" (%s)  [p2p_relay.c] */
    LA_F110,  /* "%s: invalid peer id\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F111,  /* "%s: invalid remote id\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F112,  /* "%s: invalid sid=0\n" (%s)  [p2p_relay.c] */
    LA_F113,  /* "%s: local='%s', remote='%s', online=%d, cands=%d\n" (%s,%s,%s,%d,%d)  [p2p_relay.c] */
    LA_F114,  /* "%s: local='%s', remote='%s', online=%d, payload=%u\n" (%s,%s,%s,%d,%u)  [p2p_wss.c] */
    LA_F115,  /* "%s: missing SESSION flag, dropped\n" (%s)  [p2p_compact.c] */
    LA_F116,  /* "%s: missing payload\n" (%s)  [p2p_relay.c] */
    LA_F117,  /* "%s: peer '%s' offline, pending\n" (%s,%s)  [p2p_relay.c] */
    LA_F118,  /* "%s: peer '%s' unreachable, pending\n" (%s,%s)  [p2p_relay.c] */
    LA_F119,  /* "%s: peer offline, sending error resp\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F120,  /* "%s: pkt queue full, reply busy\n" (%s)  [p2p_wss.c] */
    LA_F121,  /* "%s: prev ALV ACK still pending, skip\n" (%s)  [p2p_relay.c] */
    LA_F122,  /* "%s: rejected for not reg(%.*s)\n" (%s)  [p2p_wss.c] */
    LA_F123,  /* "%s: rejected for not reg(%s)\n" (%s,%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F124,  /* "%s: rejected for not reg\n" (%s)  [p2p_wss.c] */
    LA_F125,  /* "%s: request simultaneously for '%s'\n" (%s,%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F126,  /* "%s: rpc busy (pending sid=%u)\n" (%s,%u)  [p2p_wss.c] */
    LA_F127,  /* "%s: ses_id=%u, confirm sid=%u\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F128,  /* "%s: ses_id=%u, data_len=%u\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F129,  /* "%s: ses_id=%u, dup sid=%u, resend confirm\n" (%s,%u,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F130,  /* "%s: ses_id=%u, peer offline, drop pkt\n" (%s,%u)  [p2p_relay.c] */
    LA_F131,  /* "%s: ses_id=%u, peer offline, drop rsp\n" (%s,%u)  [p2p_relay.c] */
    LA_F132,  /* "%s: ses_id=%u, peer offline, drop sid=%u\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F133,  /* "%s: ses_id=%u, sid=%u, cands=%d\n" (%s,%u,%u,%d)  [p2p_relay.c] */
    LA_F134,  /* "%s: sid mismatch (got=%u, pending=%u), discarding\n" (%s,%u,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F135,  /* "%s: sid=%u -> peer_sid=%u sync_sid=%u\n" (%s,%u,%u,%u)  [p2p_wss.c] */
    LA_F136,  /* "%s: sid=%u -> peer_sid=%u, data_len=%u\n" (%s,%u,%u,%u)  [p2p_wss.c] */
    LA_F137,  /* "%s: snprintf failed\n" (%s)  [p2p_wss.c] */
    LA_F138,  /* "%s: text frame overflow(32-bit)\n" (%s)  [p2p_wss.c] */
    LA_F139,  /* "%s: too many candidates(cnt=%d)\n" (%s,%d)  [p2p_relay.c] */
    LA_F140,  /* "%s: unknown ses_id=%u\n" (%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F141,  /* "%s:%d %s: bad payload(%u)\n" (%s,%d,%s,%u)  [p2p_compact.c] */
    LA_F142,  /* "%s:%d %s: data overflow (%d)\n" (%s,%d,%s,%d)  [p2p_compact.c] */
    LA_F143,  /* "%s:%d %s: invalid auth_key=0\n" (%s,%d,%s)  [p2p_compact.c] */
    LA_F144,  /* "%s:%d %s: invalid relay flag\n" (%s,%d,%s)  [p2p_compact.c] */
    LA_F145,  /* "%s:%d %s: invalid seq=%u\n" (%s,%d,%s,%u)  [p2p_compact.c] */
    LA_F146,  /* "%s:%d %s: invalid server-only seq=0, dropped\n" (%s,%d,%s)  [p2p_compact.c] */
    LA_F147,  /* "%s:%d %s: invalid sess_id=%u or sid=%u\n" (%s,%d,%s,%u,%u)  [p2p_compact.c] */
    LA_F148,  /* "%s:%d %s: unknown auth_key=%llu\n" (%s,%d,%s,%l)  [p2p_compact.c] */
    LA_F149,  /* "%s:%d %s: unknown ses_id=%u, dropped\n" (%s,%d,%s,%u)  [p2p_compact.c] */
    LA_F150,  /* "%s:%d %s: unknown sess_id=%u, dropped\n" (%s,%d,%s,%u)  [p2p_compact.c] */
    LA_F151,  /* "%s:%d %s: unknown sess_id=%u\n" (%s,%d,%s,%u)  [p2p_compact.c] */
    LA_F152,  /* "%s:%d send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n" (%s,%d,%s,%d,%s,%s,%s,%d,%d,%l,%u)  [p2p_compact.c] */
    LA_F153,  /* "%s:%d send %s: rejected (no slot available)\n" (%s,%d,%s)  [p2p_compact.c] */
    LA_F154,  /* "%session: alloc buffer failed(OOM)\n" (%s)  [p2p_relay.c] */
    LA_F155,  /* "'%s' timeout & cleanup (inactive for %.1f sec)\n" (%s)  [server.c] */
    LA_F156,  /* ": bad payload(%u)\n" (%u)  [p2p_wss.c] */
    LA_F157,  /* ": unknown ses_id=%u type=0x%02x from '%s'\n" (%u,%s)  [p2p_wss.c] */
    LA_F158,  /* "BIN 0x%02x: ses_id=%u peer not connected\n" (%u)  [p2p_wss.c] */
    LA_F159,  /* "BIN: unknown type=0x%02x from '%s'\n" (%s)  [p2p_wss.c] */
    LA_F160,  /* "Client closed during protocol detection (slot %d)\n" (%d)  [server.c] */
    LA_F161,  /* "FIN: sending to peer, ses_id=%08X\n"  [p2p_wss.c] */
    LA_F162,  /* "Failed to initialize %s client\n" (%s)  [server.c] */
    LA_F163,  /* "Failed to initialize TCP/RELAY client for slot %d\n" (%d)  [server.c] */
    LA_F164,  /* "Failed to initialize TCP/WSS client for slot %d\n" (%d)  [server.c] */
    LA_F165,  /* "Failed to peek client data for protocol detection (slot %d), errno=%d\n" (%d,%d)  [server.c] */
    LA_F166,  /* "Goodbye!\n"  [server.c] */
    LA_F167,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F168,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F169,  /* "NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F170,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F171,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F172,  /* "New %s client connected from %s:%d, assigned slot %d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F173,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F174,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F175,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F176,  /* "WebSocket service listening on port %d\n" (%d)  [server.c] */
    LA_F177,  /* "[%s] %s conn closed (EOF on recv)\n" (%s,%s)  [server.c] */
    LA_F178,  /* "[%s] %s conn closed (EOF on send)\n" (%s,%s)  [server.c] */
    LA_F179,  /* "[%s] %s recv failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F180,  /* "[%s] %s send failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F181,  /* "[%s] slot %d conn closed during handshake(%d) (EOF on recv)\n" (%s,%d,%d)  [server.c] */
    LA_F182,  /* "[%s] slot %d conn closed during handshake(%d) (EOF on send)\n" (%s,%d,%d)  [server.c] */
    LA_F183,  /* "[%s] slot %d recv failed(%d) during handshake(%d) \n" (%s,%d,%d,%d)  [server.c] */
    LA_F184,  /* "[%s] slot %d send failed(%d) during handshake(%d)\n" (%s,%d,%d,%d)  [server.c] */
    LA_F185,  /* "[CT] payload len(%u) overflow, max: %u\n" (%u,%u)  [custom_tcp.c] */
    LA_F186,  /* "[CT] resolve payload len failed(%d)\n" (%d)  [custom_tcp.c] */
    LA_F187,  /* "[CT] send item refer invalid(%p)\n"  [custom_tcp.c] */
    LA_F188,  /* "[CT] send sess item refer invalid(%p)\n"  [custom_tcp.c] */
    LA_F189,  /* "[T] Failed to set client socket to non-blocking mode\n"  [server.c] */
    LA_F190,  /* "[T] Max peers reached, rejecting connection\n"  [server.c] */
    LA_F191,  /* "[U] %s recv %s, len=%zu\n" (%s,%s)  [p2p_compact.c] */
    LA_F192,  /* "[U] %s recv %s, seq=%u, flags=0x%02x, len=%u\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F193,  /* "[U] %s recv %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F194,  /* "[U] %s:%d recv %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%d,%s,%u)  [server.c] */
    LA_F195,  /* "[U] %s:%d send %s failed(%d)\n" (%s,%d,%s,%d)  [server.c] */
    LA_F196,  /* "[U] %s:%d send %s, len=%d\n" (%s,%d,%s,%d)  [server.c] */
    LA_F197,  /* "[WS] CONTINUATION frame without fragmentation going\n"  [custom_ws.c] */
    LA_F198,  /* "[WS] Client frame missing mask\n"  [custom_ws.c] */
    LA_F199,  /* "[WS] HTTP accept rejected(%d)\n" (%d)  [custom_ws.c] */
    LA_F200,  /* "[WS] Invalid UTF-8 in TEXT frame\n"  [custom_ws.c] */
    LA_F201,  /* "[WS] Invalid UTF-8 in close reason\n"  [custom_ws.c] */
    LA_F202,  /* "[WS] Invalid close code %u\n" (%u)  [custom_ws.c] */
    LA_F203,  /* "[WS] Invalid control frame: opcode=%u fin=%u hdr_len=%u\n" (%u,%u,%u)  [custom_ws.c] */
    LA_F204,  /* "[WS] New %s without fragmentation end\n" (%s)  [custom_ws.c] */
    LA_F205,  /* "[WS] OOM in fragment reassembly\n"  [custom_ws.c] */
    LA_F206,  /* "[WS] OOM: cannot allocate HTTP recv buffer\n"  [custom_ws.c] */
    LA_F207,  /* "[WS] RSV bit set in opcode %u\n" (%u)  [custom_ws.c] */
    LA_F208,  /* "[WS] Reserved opcode %u\n" (%u)  [custom_ws.c] */
    LA_F209,  /* "[WS] bad payload pos(%u) < hdr_sz(%u)\n" (%u,%u)  [custom_ws.c] */
    LA_F210,  /* "[WS] close timeout, force closing\n"  [custom_ws.c] */
    LA_F211,  /* "[WS] invalid last_reason frame\n"  [custom_ws.c] */
    LA_F212,  /* "[WS] truncate last_reason payload_len=%u to 125\n" (%u)  [custom_ws.c] */
    LA_F213,  /* "[W] RPC timeout: sid=%u (ses_id=%u)\n" (%u,%u)  [p2p_wss.c] */
    LA_F214,  /* "alloc buf failed(OOM)\n"  [p2p_wss.c] */
    LA_F215,  /* "handshake<%d> sent to '%s'\n" (%d,%s)  [custom_tcp.c] */
    LA_F216,  /* "make err(%d) resp failed(OOM)\n" (%d)  [custom_tcp.c] */
    LA_F217,  /* "net init failed\n"  [server.c] */
    LA_F218,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F219,  /* "select failed(%d)\n" (%d)  [server.c] */
    LA_F220,  /* "send failed(OOM)\n"  [p2p_relay.c] */
    LA_F221,  /* "slot %d timeout & cleanup (inactive for %.1f sec)\n" (%d)  [server.c] */
    LA_F222,  /* "unknown msg from '%s': %.32s\n" (%s)  [p2p_wss.c] */
    LA_F223,  /* "unsupported type=%u (ses_id=%u)\n" (%u,%u)  [p2p_relay.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F13

#endif /* LANG_H__ */
