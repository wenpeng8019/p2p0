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
    LA_F11,  /* "% Client closed connection (EOF on recv)\n"  [server.c] */
    LA_F12,  /* "% Client sent data before ONLINE_ACK completed\n"  [server.c] */
    LA_F13,  /* "%s accepted, '%s' -> '%s', ses_id=%u\n" (%s,%s,%s,%u)  [server.c] */
    LA_F14,  /* "%s accepted, peer='%s', auth_key=%llu\n" (%s,%s,%l)  [server.c] */
    LA_F15,  /* "%s accepted, seq=%u, ses_id=%u\n" (%s,%u,%u)  [server.c] */
    LA_F16,  /* "%s for unknown ses_id=%u\n" (%s,%u)  [server.c] */
    LA_F17,  /* "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [server.c] */
    LA_F18,  /* "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u,%u)  [server.c] */
    LA_F19,  /* "%s from '%.*s': new instance(old=%u new=%u), resetting\n" (%s,%u,%u)  [server.c] */
    LA_F20,  /* "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n" (%s,%u,%u,%d,%u)  [server.c] */
    LA_F21,  /* "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F22,  /* "%s retransmit, resend ACK, sid=%u (ses_id=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F23,  /* "%s: '%.*s' cleared stale peer marker, ready for re-pair\n" (%s)  [server.c] */
    LA_F24,  /* "%s: '%s' sid=%u code=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [server.c] */
    LA_F25,  /* "%s: '%s' sid=%u msg=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [server.c] */
    LA_F26,  /* "%s: OOM for relay buffer\n" (%s)  [server.c] */
    LA_F27,  /* "%s: OOM for zero-copy recv buffer\n" (%s)  [server.c] */
    LA_F28,  /* "%s: RPC complete for '%s', sid=%u (ses_id=%u)\n" (%s,%s,%u,%u)  [server.c] */
    LA_F29,  /* "%s: accepted, local='%.*s', inst_id=%u\n" (%s,%u)  [server.c] */
    LA_F30,  /* "%s: accepted, releasing slot for '%s'\n" (%s,%s)  [server.c] */
    LA_F31,  /* "%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n" (%s,%u,%u,%u,%d)  [server.c] */
    LA_F32,  /* "%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n" (%s,%u,%u,%u,%d)  [server.c] */
    LA_F33,  /* "%s: accepted, ses_id=%u, sid=%u\n" (%s,%u,%u)  [server.c] */
    LA_F34,  /* "%s: addr-notify confirmed '%s' (ses_id=%u)\n" (%s,%s,%u)  [server.c] */
    LA_F35,  /* "%s: auth_key=%llu assigned for '%.*s'\n" (%s,%l)  [server.c] */
    LA_F36,  /* "%s: auth_key=%llu, cands=%d from %s\n" (%s,%l,%d,%s)  [server.c] */
    LA_F37,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [server.c] */
    LA_F38,  /* "%s: bad payload(cnt=%d, len=%u, expected=%u)\n" (%s,%d,%u,%u)  [server.c] */
    LA_F39,  /* "%s: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n" (%s,%u,%u,%u)  [server.c] */
    LA_F40,  /* "%s: bad payload(len=%u)\n" (%s,%u)  [server.c] */
    LA_F41,  /* "%s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F42,  /* "%s: build_session failed for '%.*s'\n" (%s)  [server.c] */
    LA_F43,  /* "%s: build_session failed for '%s'\n" (%s,%s)  [server.c] */
    LA_F44,  /* "%s: close ses_id=%u\n" (%s,%u)  [server.c] */
    LA_F45,  /* "%s: confirmed '%s', retries=%d (ses_id=%u)\n" (%s,%s,%d,%u)  [server.c] */
    LA_F46,  /* "%s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F47,  /* "%s: forwarded to peer, cands=%d\n" (%s,%d)  [server.c] */
    LA_F48,  /* "%s: invalid auth_key=0 from %s\n" (%s,%s)  [server.c] */
    LA_F49,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F50,  /* "%s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F51,  /* "%s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F52,  /* "%s: invalid session_id=%u or sid=%u\n" (%s,%u,%u)  [server.c] */
    LA_F53,  /* "%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n" (%s)  [server.c] */
    LA_F54,  /* "%s: local='%s', remote='%s', side=%d, peer_online=%d, cands=%d\n" (%s,%s,%s,%d,%d,%d)  [server.c] */
    LA_F55,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F56,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F57,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F58,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F59,  /* "%s: paired '%.*s' <-> '%.*s'\n" (%s)  [server.c] */
    LA_F60,  /* "%s: peer '%s' not online for session_id=%u\n" (%s,%s,%u)  [server.c] */
    LA_F61,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F62,  /* "%s: peer '%s' offline, cached cands=%d\n" (%s,%s,%d)  [server.c] */
    LA_F63,  /* "%s: peer offline, sending error resp\n" (%s)  [server.c] */
    LA_F64,  /* "%s: requester not found for ses_id=%u\n" (%s,%u)  [server.c] */
    LA_F65,  /* "%s: requester offline, discarding\n" (%s)  [server.c] */
    LA_F66,  /* "%s: rpc busy (pending sid=%u)\n" (%s,%u)  [server.c] */
    LA_F67,  /* "%s: sid mismatch (got=%u, pending=%u), discarding\n" (%s,%u,%u)  [server.c] */
    LA_F68,  /* "%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n" (%s)  [server.c] */
    LA_F69,  /* "%s: unknown auth_key=%llu from %s\n" (%s,%l,%s)  [server.c] */
    LA_F70,  /* "%s: unknown session_id=%u\n" (%s,%u)  [server.c] */
    LA_F71,  /* "'%s' disconnected\n" (%s)  [server.c] */
    LA_F72,  /* "'%s' timeout (inactive for %.1f sec)\n" (%s)  [server.c] */
    LA_F73,  /* "Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n" (%s,%u)  [server.c] */
    LA_F74,  /* "Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n" (%s,%u)  [server.c] */
    LA_F75,  /* "Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [server.c] */
    LA_F76,  /* "Addr changed for '%s', notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [server.c] */
    LA_F77,  /* "Cannot relay %s: ses_id=%u (peer unavailable)\n" (%s,%u)  [server.c] */
    LA_F78,  /* "Client closed connection (EOF on send, reason=%s)\n" (%s)  [server.c] */
    LA_F79,  /* "Client disconnected (not yet logged in)\n"  [server.c] */
    LA_F80,  /* "Duplicate session create blocked: '%s' -> '%s'\n" (%s,%s)  [server.c] */
    LA_F81,  /* "Goodbye!\n"  [server.c] */
    LA_F82,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F83,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F84,  /* "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n" (%d,%s,%u,%u)  [server.c] */
    LA_F85,  /* "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F86,  /* "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n" (%s,%s,%u,%d,%d,%u)  [server.c] */
    LA_F87,  /* "MSG_RESP gave up after %d retries, sid=%u (ses_id=%u)\n" (%d,%u,%u)  [server.c] */
    LA_F88,  /* "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n" (%s,%u,%d,%d,%u)  [server.c] */
    LA_F89,  /* "NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F90,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F91,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F92,  /* "ONLINE: '%s' came online (inst=%u)\n" (%s,%u)  [server.c] */
    LA_F93,  /* "ONLINE: '%s' new instance (old=%u, new=%u), destroying old\n" (%s,%u,%u)  [server.c] */
    LA_F94,  /* "ONLINE: '%s' reconnected (inst=%u), migrating fd\n" (%s,%u)  [server.c] */
    LA_F95,  /* "ONLINE: bad payload(len=%u, expected=%u)\n" (%u,%u)  [server.c] */
    LA_F96,  /* "ONLINE: duplicate from '%s'\n" (%s)  [server.c] */
    LA_F97,  /* "ONLINE_ACK sent to '%s'\n" (%s)  [server.c] */
    LA_F98,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F99,  /* "RPC_ERR: OOM\n"  [server.c] */
    LA_F100,  /* "Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n" (%s,%u,%s,%s,%u)  [server.c] */
    LA_F101,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F102,  /* "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n" (%s,%s,%d,%d,%u)  [server.c] */
    LA_F103,  /* "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F104,  /* "SYNC0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n" (%d,%d)  [server.c] */
    LA_F105,  /* "SYNC0_ACK queue busy for '%s', drop\n" (%s)  [server.c] */
    LA_F106,  /* "SYNC_ACK queue busy for '%s', drop\n" (%s)  [server.c] */
    LA_F107,  /* "Send %s: auth_key=%llu, peer='%s'\n" (%s,%l,%s)  [server.c] */
    LA_F108,  /* "Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n" (%s,%u,%d,%u,%s)  [server.c] */
    LA_F109,  /* "Send %s: cands=%d, ses_id=%u, peer='%s'\n" (%s,%d,%u,%s)  [server.c] */
    LA_F110,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F111,  /* "Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n" (%s,%d,%s,%s,%s,%d,%d,%l,%u)  [server.c] */
    LA_F112,  /* "Send %s: peer='%s', reason=%s, ses_id=%u\n" (%s,%s,%s,%u)  [server.c] */
    LA_F113,  /* "Send %s: rejected (no slot available)\n" (%s)  [server.c] */
    LA_F114,  /* "Send %s: ses_id=%u, peer=%s\n" (%s,%u,%s)  [server.c] */
    LA_F115,  /* "Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s'\n" (%s,%u,%u,%u,%d,%s)  [server.c] */
    LA_F116,  /* "Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n" (%s,%u,%u,%s,%u,%d)  [server.c] */
    LA_F117,  /* "Send %s: ses_id=%u, sid=%u, peer='%s'\n" (%s,%u,%u,%s)  [server.c] */
    LA_F118,  /* "Send %s: ses_id=%u, sid=%u, status=%u\n" (%s,%u,%u,%u)  [server.c] */
    LA_F119,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F120,  /* "Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n" (%s)  [server.c] */
    LA_F121,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
    LA_F122,  /* "[Relay] %s for ses_id=%u: peer unavailable (dropped)\n" (%s,%u)  [server.c] */
    LA_F123,  /* "[Relay] %s for unknown ses_id=%u (dropped)\n" (%s,%u)  [server.c] */
    LA_F124,  /* "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n" (%s,%u,%s,%s,%u)  [server.c] */
    LA_F125,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F126,  /* "[Relay] %s: '%s' -> '%s' (ses_id=%u)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F127,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F128,  /* "[Relay] %s: missing SESSION flag, dropped\n" (%s)  [server.c] */
    LA_F129,  /* "[TCP] Failed to set client socket to non-blocking mode\n"  [server.c] */
    LA_F130,  /* "[TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_F131,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F132,  /* "[TCP] OOM: cannot allocate recv buffer for new client\n"  [server.c] */
    LA_F133,  /* "[UDP] %s recv from %s, len=%zu\n" (%s,%s)  [server.c] */
    LA_F134,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c] */
    LA_F135,  /* "[UDP] %s send to %s failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F136,  /* "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n" (%s,%s,%u,%d)  [server.c] */
    LA_F137,  /* "[UDP] %s send to %s, seq=0, flags=0, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F138,  /* "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F139,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [server.c] */
    LA_F140,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F141,  /* "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n" (%s,%s,%d,%d,%d)  [server.c] */
    LA_F142,  /* "bad payload len %u (type=%u)\n" (%u,%u)  [server.c] */
    LA_F143,  /* "bad payload len %u\n" (%u)  [server.c] */
    LA_F144,  /* "net init failed\n"  [server.c] */
    LA_F145,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F146,  /* "recv() failed: errno=%d\n" (%d)  [server.c] */
    LA_F147,  /* "select failed(%d)\n" (%d)  [server.c] */
    LA_F148,  /* "send(%s) failed: errno=%d\n" (%s,%d)  [server.c] */
    LA_F149,  /* "ses_id=%u busy (pending relay)\n" (%u)  [server.c] */
    LA_F150,  /* "ses_id=%u peer not connected (type=%u)\n" (%u,%u)  [server.c] */
    LA_F151,  /* "type=%u rejected: client not logged in\n" (%u)  [server.c] */
    LA_F152,  /* "unknown ses_id=%u (type=%u)\n" (%u,%u)  [server.c] */
    LA_F153,  /* "unsupported type=%u (ses_id=%u)\n" (%u,%u)  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F11

#endif /* LANG_H__ */
