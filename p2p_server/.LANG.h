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
    LA_S6,  /* "NAT type detection port (0=disabled)"  [server.c] */
    LA_S7,  /* "Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S8,  /* "Shutting down...\n"  [server.c] */
    LA_S9,  /* "Signaling server listen port (TCP+UDP)"  [server.c] */
    LA_S10,  /* "Use Chinese language"  [server.c] */

    /* Formats (LA_F) */
    LA_F11,  /* "[%s] conn closed (EOF on recv)\n" (%s)  [server.c] */
    LA_F12,  /* disabled "% Client sent data before REG_ACK completed\n" */
    LA_F13,  /* "%s accepted, '%s' -> '%s', ses_id=%u\n" (%s,%s,%s,%u)  [p2p_compact.c] */
    LA_F14,  /* "%s accepted, peer='%s', auth_key=%llu\n" (%s,%s,%l)  [p2p_compact.c] */
    LA_F15,  /* "%s accepted, seq=%u, ses_id=%u\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F16,  /* "%s for unknown ses_id=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F17,  /* "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F18,  /* "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u,%u)  [p2p_compact.c] */
    LA_F19,  /* disabled "%s from '%.*s': new instance(old=%u new=%u), resetting\n" */
    LA_F20,  /* "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n" (%s,%u,%u,%d,%u)  [p2p_compact.c] */
    LA_F21,  /* "%s retransmit during RSP phase, ignoring, sid=%u (ses_id=%u)\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F22,  /* "%s retransmit, resend ACK, sid=%u (ses_id=%u)\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F23,  /* disabled "%s: '%.*s' cleared stale peer marker, ready for re-pair\n" */
    LA_F24,  /* "%s: '%s' sid=%u code=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [p2p_wss.c, p2p_relay.c] */
    LA_F25,  /* "%s: '%s' sid=%u msg=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [p2p_wss.c, p2p_relay.c] */
    LA_F26,  /* "%s: 2nd-ack confirmed '%s' (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F27,  /* disabled "%s: OOM for relay buffer\n" */
    LA_F28,  /* "%s: alloc buffer failed(OOM)\n" (%s)  [p2p_relay.c] */
    LA_F29,  /* "%s: RPC complete for '%s', sid=%u (ses_id=%u)\n" (%s,%s,%u,%u)  [p2p_compact.c] */
    LA_F30,  /* "%s: accepted, local='%.*s', inst_id=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F31,  /* "%s: accepted, releasing slot for '%s'\n" (%s,%s)  [p2p_compact.c] */
    LA_F32,  /* "%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n" (%s,%u,%u,%u,%d)  [p2p_compact.c] */
    LA_F33,  /* "%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n" (%s,%u,%u,%u,%d)  [p2p_compact.c] */
    LA_F34,  /* "%s: accepted, ses_id=%u, sid=%u\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F35,  /* "%s: addr-notify confirmed '%s' (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F36,  /* "%s: auth_key=%llu assigned for '%.*s'\n" (%s,%l)  [p2p_compact.c] */
    LA_F37,  /* "%s: OOM building session '%s' -> '%s'\n" (%s,%s,%s)  [p2p_wss.c] */
    LA_F38,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [p2p_relay.c] */
    LA_F39,  /* "%s: bad payload(cnt=%d, len=%u, expected=%u)\n" (%s,%d,%u,%u)  [p2p_relay.c] */
    LA_F40,  /* "%s: bad payload(sid=%u, cnt=%u, len=%u, expected=%u+1fin)\n" (%s,%u,%u,%u,%u)  [p2p_relay.c] */
    LA_F41,  /* disabled "%s: bad frame len=%u\n" */
    LA_F42,  /* "%s: bad payload(len=%zu)\n" (%s)  [p2p_compact.c] */
    LA_F43,  /* "%s: build_session failed for '%.*s'\n" (%s)  [p2p_compact.c] */
    LA_F44,  /* "%s: build session to '%s' failed(%d)\n" (%s,%s,%d)  [p2p_wss.c, p2p_relay.c] */
    LA_F45,  /* "%s: '%s' ses_id=%u\n" (%s,%s,%u)  [p2p_wss.c] */
    LA_F46,  /* "%s: confirmed '%s', retries=%d (ses_id=%u)\n" (%s,%s,%d,%u)  [p2p_compact.c] */
    LA_F47,  /* "%s: data too large (len=%d)\n" (%s,%d)  [p2p_compact.c] */
    LA_F48,  /* "%s: %s <-> %s forward\n" (%s,%s,%s)  [p2p_relay.c] */
    LA_F49,  /* "%s: invalid auth_key=0 from %s\n" (%s,%s)  [p2p_compact.c] */
    LA_F50,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [p2p_compact.c] */
    LA_F51,  /* "%s: alloc client for '%.*s' failed\n" (%s)  [p2p_compact.c] */
    LA_F52,  /* "%s: invalid seq=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F53,  /* "%s: invalid session_id=%u or sid=%u\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F54,  /* disabled "%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n" */
    LA_F55,  /* "%s: local='%s', remote='%s', online=%d, cands=%d\n" (%s,%s,%s,%d,%d)  [p2p_relay.c] */
    LA_F56,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [p2p_compact.c] */
    LA_F57,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F58,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F59,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F60,  /* "%s: paired '%.*s' <-> '%.*s'\n" (%s)  [p2p_compact.c] */
    LA_F61,  /* "%s: peer '%s' not online for session_id=%u\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F62,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F63,  /* "%s: '%s' -> '%s' created (id=%u, peer_zombie)\n" (%s,%s,%s,%u)  [p2p_wss.c] */
    LA_F64,  /* "%s: peer offline, sending error resp\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F65,  /* "%s: requester not found for ses_id=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F66,  /* "%s: requester offline, discarding\n" (%s)  [p2p_wss.c] */
    LA_F67,  /* "%s: rpc busy (pending sid=%u)\n" (%s,%u)  [p2p_wss.c] */
    LA_F68,  /* "%s: sid mismatch (got=%u, pending=%u), discarding\n" (%s,%u,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F69,  /* disabled "%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n" */
    LA_F70,  /* "%s: unknown auth_key=%llu from %s\n" (%s,%l,%s)  [p2p_compact.c] */
    LA_F71,  /* "%s: unknown session_id=%u\n" (%s,%u)  [p2p_compact.c] */
    LA_F72,  /* "%s: '%s'\n" (%s,%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F73,  /* "'%s' timeout & cleanup (inactive for %.1f sec)\n" (%s)  [server.c] */
    LA_F74,  /* "Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n" (%s,%u)  [p2p_compact.c] */
    LA_F75,  /* "Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n" (%s,%u)  [p2p_compact.c] */
    LA_F76,  /* "Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F77,  /* "Addr changed for '%s', notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [p2p_compact.c] */
    LA_F78,  /* "Cannot relay %s: ses_id=%u (peer unavailable)\n" (%s,%u)  [p2p_compact.c] */
    LA_F79,  /* disabled "% Client closed connection (EOF on recv during handshake)\n" */
    LA_F80,  /* disabled "% Client recv closed (not yet reg)\n" */
    LA_F81,  /* disabled "Duplicate session create blocked: '%s' -> '%s'\n" */
    LA_F82,  /* "Goodbye!\n"  [server.c] */
    LA_F83,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F84,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F85,  /* "REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n" (%d,%s,%u,%u)  [p2p_compact.c] */
    LA_F86,  /* "REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n" (%s,%u,%u)  [p2p_compact.c] */
    LA_F87,  /* "REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n" (%s,%s,%u,%d,%d,%u)  [p2p_compact.c] */
    LA_F88,  /* "RSP gave up after %d retries, sid=%u (ses_id=%u)\n" (%d,%u,%u)  [p2p_compact.c] */
    LA_F89,  /* "RSP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n" (%s,%u,%d,%d,%u)  [p2p_compact.c] */
    LA_F90,  /* "NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F91,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F92,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F93,  /* "%s: '%s' new REG (inst=%u)\n" (%s,%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F94,  /* "%s: invalid instance id\n" (%s)  [p2p_relay.c] */
    LA_F95,  /* "REG: '%s' reconnected (inst=%u), migrating\n" (%s,%u)  [server.c] */
    LA_F96,  /* "%s: invalid peer id\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F97,  /* "%s: duplicate from '%s'\n" (%s,%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F98,  /* "%s: '%s' reconnected & reactive (inst=%u)\n" (%s,%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F99,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F100,  /* "send failed(OOM)\n"  [p2p_relay.c] */
    LA_F101,  /* "Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n" (%s,%u,%s,%s,%u)  [p2p_compact.c] */
    LA_F102,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F103,  /* "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n" (%s,%s,%d,%d,%u)  [p2p_compact.c] */
    LA_F104,  /* "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [p2p_compact.c] */
    LA_F105,  /* disabled "SYN0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n" */
    LA_F106,  /* disabled "SYN0_ACK queue busy for '%s', drop\n" */
    LA_F107,  /* disabled "SYNC_ACK queue busy for '%s', drop\n" */
    LA_F108,  /* "Send %s: auth_key=%llu, peer='%s'\n" (%s,%l,%s)  [p2p_compact.c] */
    LA_F109,  /* "Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n" (%s,%u,%d,%u,%s)  [p2p_compact.c] */
    LA_F110,  /* "Send %s: cands=%d, ses_id=%u, peer='%s'\n" (%s,%d,%u,%s)  [p2p_compact.c] */
    LA_F111,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F112,  /* "Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n" (%s,%d,%s,%s,%s,%d,%d,%l,%u)  [p2p_compact.c] */
    LA_F113,  /* "Send %s: peer='%s', reason=%s, ses_id=%u\n" (%s,%s,%s,%u)  [p2p_compact.c] */
    LA_F114,  /* "Send %s: rejected (no slot available)\n" (%s)  [p2p_compact.c] */
    LA_F115,  /* "Send %s: ses_id=%u, peer=%s\n" (%s,%u,%s)  [p2p_compact.c] */
    LA_F116,  /* "Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s', retries=%d\n" (%s,%u,%u,%u,%d,%s,%d)  [p2p_compact.c] */
    LA_F117,  /* "Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d, retries=%d\n" (%s,%u,%u,%s,%u,%d,%d)  [p2p_compact.c] */
    LA_F118,  /* "Send %s: ses_id=%u, sid=%u, peer='%s'\n" (%s,%u,%u,%s)  [p2p_compact.c] */
    LA_F119,  /* "Send %s: ses_id=%u, sid=%u, status=%u\n" (%s,%u,%u,%u)  [p2p_compact.c] */
    LA_F120,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F121,  /* disabled "Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n" */
    LA_F122,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [p2p_compact.c] */
    LA_F123,  /* "[Relay] %s for ses_id=%u: peer unavailable (dropped)\n" (%s,%u)  [p2p_compact.c] */
    LA_F124,  /* "[Relay] %s for unknown ses_id=%u (dropped)\n" (%s,%u)  [p2p_compact.c] */
    LA_F125,  /* "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n" (%s,%u,%s,%s,%u)  [p2p_compact.c] */
    LA_F126,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [p2p_compact.c] */
    LA_F127,  /* "[Relay] %s: '%s' -> '%s' (ses_id=%u)\n" (%s,%s,%s,%u)  [p2p_compact.c] */
    LA_F128,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [p2p_compact.c] */
    LA_F129,  /* "[Relay] %s: missing SESSION flag, dropped\n" (%s)  [p2p_compact.c] */
    LA_F130,  /* "[TCP] Failed to set client socket to non-blocking mode\n"  [server.c] */
    LA_F131,  /* "[TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_F132,  /* disabled "[TCP] New connection from %s:%d\n" */
    LA_F133,  /* "[TCP] OOM: cannot allocate recv buffer for new client\n"  [custom_tcp.c] */
    LA_F134,  /* "[UDP] SYN0_ACK recv from %s, len=%zu\n" (%s)  [p2p_compact.c] */
    LA_F135,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c, p2p_compact.c] */
    LA_F136,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [server.c] */
    LA_F137,  /* "[UDP] %s send to %s:%d, len=%d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F138,  /* "%s: bad payload(%u)\n" (%s,%u)  [p2p_relay.c] */
    LA_F139,  /* disabled "bad payload(len=%u)\n" */
    LA_F140,  /* "net init failed\n"  [server.c] */
    LA_F141,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F142,  /* "%s: invalid remote id\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F143,  /* "[WS] HTTP handshake rejected\n"  [custom_ws.c] */
    LA_F144,  /* "[TCP]  send(%s) failed(%d)\n" (%s,%d)  [server.c] */
    LA_F145,  /* "%s: busy (ses_id=%u), pending\n" (%s,%u)  [p2p_relay.c] */
    LA_F146,  /* disabled "%s: ses_id=%u peer not connected\n" */
    LA_F147,  /* "%s: rejected for not reg\n" (%s)  [p2p_wss.c, p2p_relay.c] */
    LA_F148,  /* "%s: unknown ses_id=%u\n" (%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F149,  /* "BIN: unknown type=0x%02x from '%s'\n" (%s)  [p2p_wss.c] */

    /* Strings (LA_S) */
    LA_S150,  /* "Enable WebSocket service on same TCP port"  [server.c] */
    LA_S151,  /* "WebSocket dedicated port (also enables --ws)"  [server.c] */

    /* Formats (LA_F) */
    LA_F152,  /* "%s: '%s' <-> '%s' paired (ses=%u/%u)\n" (%s,%s,%s,%u,%u)  [p2p_wss.c] */
    LA_F153,  /* "%s: '%s' reconnected & renew (inst=%u)\n" (%s,%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F154,  /* "%s: auth_key=%llu, cands=%d from %s\n" (%s,%l,%d,%s)  [p2p_compact.c] */
    LA_F155,  /* "%s: bad frame len=%u\n" (%s,%u)  [p2p_wss.c] */
    LA_F156,  /* "%s: bad payload(len=%u)\n" (%s,%u)  [p2p_wss.c, p2p_relay.c] */
    LA_F157,  /* "%s: build session to '%s' failed(OOM)" (%s,%s)  [p2p_relay.c] */
    LA_F158,  /* "%s: close ses_id=%u\n" (%s,%u)  [p2p_relay.c] */
    LA_F159,  /* "%s: duplicate SYN0 with different candidates from '%s'\n" (%s,%s)  [p2p_compact.c] */
    LA_F160,  /* "%s: invalid REG format\n" (%s)  [p2p_wss.c] */
    LA_F161,  /* "%s: invalid relay flag from client\n" (%s)  [p2p_compact.c] */
    LA_F162,  /* "%s: local='%s', remote='%s', online=%d, sync_cache=%u\n" (%s,%s,%s,%d,%u)  [p2p_wss.c] */
    LA_F163,  /* "%s: peer '%s' offline, cached cands=%d\n" (%s,%s,%d)  [p2p_relay.c] */
    LA_F164,  /* disabled "%s: send failed\n" */
    LA_F165,  /* "%s: ses_id=%u, confirm sid=%u\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F166,  /* "%s: ses_id=%u, data_len=%u\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F167,  /* "%s: sid=%u -> peer_sid=%u (%zu bytes)\n" (%s,%u,%u)  [p2p_wss.c] */
    LA_F168,  /* "%s: sid=%u -> peer_sid=%u, data_len=%u\n" (%s,%u,%u,%u)  [p2p_wss.c] */
    LA_F169,  /* disabled "'%s' recv closed\n" */
    LA_F170,  /* "BIN 0x%02x: ses_id=%u peer not connected\n" (%u)  [p2p_wss.c] */
    LA_F171,  /* "BIN: unknown ses_id=%u type=0x%02x from '%s'\n" (%u,%s)  [p2p_wss.c] */
    LA_F172,  /* "Client closed during protocol detection (slot %d)\n" (%d)  [server.c] */
    LA_F173,  /* disabled "% Failed to allocate buffer for new WebSocket client\n" */
    LA_F174,  /* "Failed to initialize %s client\n" (%s)  [server.c] */
    LA_F175,  /* "Failed to initialize TCP/RELAY client for slot %d\n" (%d)  [server.c] */
    LA_F176,  /* "Failed to initialize WS/ICE client for slot %d\n" (%d)  [server.c] */
    LA_F177,  /* "Failed to peek client data for protocol detection (slot %d), errno=%d\n" (%d,%d)  [server.c] */
    LA_F178,  /* "New %s client connected from %s:%d, assigned slot %d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F179,  /* "handshake<%d> sent to '%s'\n" (%d,%s)  [custom_tcp.c] */
    LA_F180,  /* disabled "WebSocket recv callback error: errno=%d\n" */
    LA_F181,  /* "WebSocket service listening on port %d\n" (%d)  [server.c] */
    LA_F182,  /* "[TCP] conn closed (EOF on send, PROTO=%s)\n" (%s)  [server.c] */
    LA_F183,  /* "[%s] recv failed(%d) during handshake(%d) \n" (%s,%d,%d)  [server.c] */
    LA_F184,  /* disabled "[UDP] ALIVE recv from %s, seq=%u, flags=0x%02x, len=%zu\n" */
    LA_F185,  /* disabled "[UDP] OFF recv from %s, seq=%u, flags=0x%02x, len=%zu\n" */
    LA_F186,  /* "[UDP] REQ recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%u)  [p2p_compact.c] */
    LA_F187,  /* "[UDP] RSP recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%u)  [p2p_compact.c] */
    LA_F188,  /* "[UDP] RSP_ACK recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%u)  [p2p_compact.c] */
    LA_F189,  /* "[UDP] SYN0 recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%u)  [p2p_compact.c] */
    LA_F190,  /* "[UDP] SYNC_ACK recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%u)  [p2p_compact.c] */
    LA_F191,  /* disabled "[WS] Client closed (slot %d)\n" */
    LA_F192,  /* "[WS] close timeout, force closing\n"  [custom_ws.c] */
    LA_F193,  /* disabled "[WS] client closed during handshake (slot %d)\n" */
    LA_F194,  /* disabled "[WS] conn closed during send: errno=%d (slot %d)\n" */
    LA_F195,  /* disabled "[WS] queue close(%u) proto failed(%d)\n" */
    LA_F196,  /* disabled "[WS] queue text data failed(%d)\n" */
    LA_F197,  /* disabled "[WS] queue text msg failed(%d)\n" */
    LA_F198,  /* disabled "[WS] recv failed(%d) (slot %d)\n" */
    LA_F199,  /* "[W] RPC timeout: sid=%u (ses_id=%u)\n" (%u,%u)  [p2p_wss.c] */
    LA_F200,  /* disabled "recv failed during handshake: errno=%d\n" */
    LA_F201,  /* "select failed(%d)\n" (%d)  [server.c] */
    LA_F202,  /* disabled "send failed during handshake: errno=%d\n" */
    LA_F203,  /* "unknown msg from '%s': %.32s\n" (%s)  [p2p_wss.c] */
    LA_F204,  /* "unsupported type=%u (ses_id=%u)\n" (%u,%u)  [p2p_relay.c] */
    LA_F205,  /* "%s: bad payload(sid=0)\n" (%s)  [p2p_relay.c] */
    LA_F206,  /* "%s: busy (ses_id=%u, sid=%u), pending\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F207,  /* "%s: deprecated (ses_id=%u, sid=%u), drop\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F208,  /* "%s: deprecated (ses_id=%u, sid=%u, last=%u), discarding\n" (%s,%u,%u,%u)  [p2p_relay.c] */
    LA_F209,  /* "%s: deprecated (ses_id=%u, sid=%u, last=%u), drop\n" (%s,%u,%u,%u)  [p2p_relay.c] */
    LA_F210,  /* "%s: duplicate SYN0 (ses_id=%u), resend ACK\n" (%s,%u)  [p2p_relay.c] */
    LA_F211,  /* "%s: duplicate SYN0 (ses_id=%u), resend response\n" (%s,%u)  [p2p_wss.c] */
    LA_F212,  /* "%s: invalid sid=0\n" (%s)  [p2p_relay.c] */
    LA_F213,  /* "%s: missing payload\n" (%s)  [p2p_relay.c] */
    LA_F214,  /* "%s: pkt queue full, dropping\n" (%s)  [p2p_wss.c] */
    LA_F215,  /* "%s: prev ALV ACK still pending, skip\n" (%s)  [p2p_relay.c] */
    LA_F216,  /* "%s: request simultaneously for '%s'\n" (%s,%s)  [p2p_relay.c] */
    LA_F217,  /* "%s: ses_id=%u, dup sid=%u, resend confirm\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F218,  /* "%s: ses_id=%u, peer offline, drop pkt\n" (%s,%u)  [p2p_relay.c] */
    LA_F219,  /* "%s: ses_id=%u, peer offline, drop rsp\n" (%s,%u)  [p2p_relay.c] */
    LA_F220,  /* "%s: ses_id=%u, peer offline, drop sid=%u\n" (%s,%u,%u)  [p2p_relay.c] */
    LA_F221,  /* "%s: ses_id=%u, sid=%u, cands=%d\n" (%s,%u,%u,%d)  [p2p_relay.c] */
    LA_F222,  /* "%session: alloc buffer failed(OOM)\n" (%s)  [p2p_relay.c] */
    LA_F223,  /* "REG: '%s' new instance (old=%u, new=%u), resetting session\n" (%s,%u,%u)  [server.c] */
    LA_F224,  /* "[%s] conn closed during handshake(%d) (EOF on recv)\n" (%s,%d)  [server.c] */
    LA_F225,  /* "[%s] recv failed(%d)\n" (%s,%d)  [server.c] */
    LA_F226,  /* "[WS] OOM in fragment reassembly\n"  [custom_ws.c] */
    LA_F227,  /* "[WS] OOM: cannot allocate HTTP recv buffer\n"  [custom_ws.c] */
    LA_F228,  /* "[WS] send_frame: payload_pos(%u) < hdr_sz(%u)\n" (%u,%u)  [custom_ws.c] */
    LA_F229,  /* "make err(%d) resp failed(OOM)\n" (%d)  [custom_tcp.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F13

#endif /* LANG_H__ */
