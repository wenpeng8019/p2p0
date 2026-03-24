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
    LA_W1,  /* "disabled"  [server.c] */
    LA_W2,  /* "enabled"  [server.c] */

    /* Strings (LA_S) */
    LA_S3,  /* "[TCP] User list truncated (too many users)\n"  [server.c] */
    _LA_4,
    LA_S5,  /* "Enable data relay support (COMPACT mode fallback)"  [server.c] */
    LA_S6,  /* "Enable MSG RPC support"  [server.c] */
    LA_S7,  /* "NAT type detection port (0=disabled)"  [server.c] */
    LA_S8,  /* "Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S9,  /* "Shutting down...\n"  [server.c] */
    LA_S10,  /* "Signaling server listen port (TCP+UDP)"  [server.c] */
    LA_S11,  /* "Use Chinese language"  [server.c] */

    /* Formats (LA_F) */
    LA_F12,  /* "%s accepted, '%s' -> '%s', ses_id=%llu\n" (%s,%s,%s,%l)  [server.c] */
    LA_F13,  /* "%s accepted, peer='%s', ses_id=%llu\n" (%s,%s,%l)  [server.c] */
    LA_F14,  /* "%s accepted, seq=%u, ses_id=%llu\n" (%s,%u,%l)  [server.c] */
    LA_F15,  /* "%s for unknown ses_id=%llu\n" (%s,%l)  [server.c] */
    LA_F16,  /* "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%llu)\n" (%s,%s,%s,%u,%l)  [server.c] */
    LA_F17,  /* "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%llu)\n" (%s,%s,%s,%u,%u,%l)  [server.c] */
    LA_F18,  /* "%s from '%.*s': new instance(old=%u new=%u), resetting session\n" (%s,%u,%u)  [server.c] */
    LA_F19,  /* "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%llu)\n" (%s,%u,%u,%d,%l)  [server.c] */
    LA_F20,  /* "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%llu)\n" (%s,%u,%l)  [server.c] */
    LA_F21,  /* "%s retransmit, resend ACK, sid=%u (ses_id=%llu)\n" (%s,%u,%l)  [server.c] */
    LA_F22,  /* "%s: RPC complete for '%s', sid=%u (ses_id=%llu)\n" (%s,%s,%u,%l)  [server.c] */
    LA_F23,  /* "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n" (%s,%u,%d)  [server.c] */
    LA_F24,  /* "%s: accepted, releasing slot for '%s' -> '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F25,  /* "%s: accepted, ses_id=%llu, sid=%u, code=%u, len=%d\n" (%s,%l,%u,%u,%d)  [server.c] */
    LA_F26,  /* "%s: accepted, ses_id=%llu, sid=%u, msg=%u, len=%d\n" (%s,%l,%u,%u,%d)  [server.c] */
    LA_F27,  /* "%s: accepted, ses_id=%llu, sid=%u\n" (%s,%l,%u)  [server.c] */
    LA_F28,  /* "%s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F29,  /* "%s: confirmed '%s', retries=%d (ses_id=%llu)\n" (%s,%s,%d,%l)  [server.c] */
    LA_F30,  /* "%s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F31,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F32,  /* "%s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F33,  /* "%s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F34,  /* "%s: invalid session_id=%llu or sid=%u\n" (%s,%l,%u)  [server.c] */
    LA_F35,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F36,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F37,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F38,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F39,  /* "%s: peer '%s' not online for session_id=%llu\n" (%s,%s,%l)  [server.c] */
    LA_F40,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F41,  /* "%s: requester not found for ses_id=%llu\n" (%s,%l)  [server.c] */
    LA_F42,  /* "%s: reset '%.*s'(disconnected) session for re-pairing\n" (%s)  [server.c] */
    LA_F43,  /* "%s: unknown session_id=%llu\n" (%s,%l)  [server.c] */
    LA_F44,  /* "%s: waiting for peer '%.*s' to register\n" (%s)  [server.c] */
    LA_F45,  /* "Addr changed for '%s', but first info packet was abandoned (ses_id=%llu)\n" (%s,%l)  [server.c] */
    LA_F46,  /* "Addr changed for '%s', defer notification until first ACK (ses_id=%llu)\n" (%s,%l)  [server.c] */
    LA_F47,  /* "Addr changed for '%s', deferred notifying '%s' (ses_id=%llu)\n" (%s,%s,%l)  [server.c] */
    LA_F48,  /* "Addr changed for '%s', notifying '%s' (ses_id=%llu)\n" (%s,%s,%l)  [server.c] */
    LA_F49,  /* "Cannot relay %s: ses_id=%llu (peer unavailable)\n" (%s,%l)  [server.c] */
    LA_F50,  /* "% Goodbye!\n"  [server.c] */
    LA_F51,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F52,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F53,  /* "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%llu)\n" (%d,%s,%u,%l)  [server.c] */
    LA_F54,  /* "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%llu)\n" (%s,%u,%l)  [server.c] */
    LA_F55,  /* "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%llu)\n" (%s,%s,%u,%d,%d,%l)  [server.c] */
    LA_F56,  /* "MSG_RESP gave up after %d retries, sid=%u (ses_id=%llu)\n" (%d,%u,%l)  [server.c] */
    LA_F57,  /* "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%llu)\n" (%s,%u,%d,%d,%l)  [server.c] */
    LA_F58,  /* "% NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F59,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F60,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F61,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F62,  /* "PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%llu)\n" (%s,%s,%d,%d,%l)  [server.c] */
    LA_F63,  /* "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F64,  /* "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n" (%d,%d)  [server.c] */
    LA_F65,  /* "Relay %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n" (%s,%u,%s,%s,%l)  [server.c] */
    LA_F66,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F67,  /* "Send %s: base_index=%u, cands=%d, ses_id=%llu, peer='%s'\n" (%s,%u,%d,%l,%s)  [server.c] */
    LA_F68,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F69,  /* "Send %s: peer='%s', reason=%s, ses_id=%llu\n" (%s,%s,%s,%l)  [server.c] */
    LA_F70,  /* "Send %s: ses_id=%llu, peer='%s'\n" (%s,%l,%s)  [server.c] */
    LA_F71,  /* "Send %s: ses_id=%llu, sid=%u, msg=%u, data_len=%d, peer='%s'\n" (%s,%l,%u,%u,%d,%s)  [server.c] */
    LA_F72,  /* "Send %s: ses_id=%llu, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n" (%s,%l,%u,%s,%u,%d)  [server.c] */
    LA_F73,  /* "Send %s: ses_id=%llu, sid=%u, peer='%s'\n" (%s,%l,%u,%s)  [server.c] */
    LA_F74,  /* "Send %s: ses_id=%llu, sid=%u, status=%u\n" (%s,%l,%u,%u)  [server.c] */
    LA_F75,  /* "Send %s: status=%s, max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, ses_id=%llu, inst_id=%u\n" (%s,%s,%d,%s,%s,%s,%d,%d,%l,%u)  [server.c] */
    LA_F76,  /* "Send %s: status=error (no slot available)\n" (%s)  [server.c] */
    LA_F77,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F78,  /* "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n" (%s,%s)  [server.c] */
    LA_F79,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
    LA_F80,  /* "[Relay] %s for ses_id=%llu: peer unavailable (dropped)\n" (%s,%l)  [server.c] */
    LA_F81,  /* "[Relay] %s for unknown ses_id=%llu (dropped)\n" (%s,%l)  [server.c] */
    LA_F82,  /* "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n" (%s,%u,%s,%s,%l)  [server.c] */
    LA_F83,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F84,  /* "[Relay] %s: '%s' -> '%s' (ses_id=%llu)\n" (%s,%s,%s,%l)  [server.c] */
    LA_F85,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F86,  /* "[Relay] %s: missing SESSION flag, dropped\n" (%s)  [server.c] */
    LA_F87,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F88,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F89,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F90,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F91,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F92,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F93,  /* "[TCP] E: Invalid magic from peer '%s'\n" (%s)  [server.c] */
    LA_F94,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F95,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F96,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F97,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F98,  /* "[TCP] I: Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F99,  /* "% [TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_F100,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F101,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F102,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F103,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F104,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F105,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F106,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F107,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F108,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F109,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F110,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F111,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F112,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    LA_F113,  /* "[TCP] V: %s sent to '%s'\n" (%s,%s)  [server.c] */
    LA_F114,  /* "[TCP] V: Peer '%s' disconnected\n" (%s)  [server.c] */
    LA_F115,  /* "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n" (%s)  [server.c] */
    LA_F116,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c] */
    LA_F117,  /* "[UDP] %s send to %s failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F118,  /* "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n" (%s,%s,%u,%d)  [server.c] */
    LA_F119,  /* "[UDP] %s send to %s, seq=0, flags=0, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F120,  /* "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F121,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [server.c] */
    LA_F122,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F123,  /* "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n" (%s,%s,%d,%d,%d)  [server.c] */
    LA_F124,  /* "% net init failed\n"  [server.c] */
    LA_F125,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F126,  /* "select failed(%d)\n" (%d)  [server.c] */
    _LA_127,
    _LA_128,
    _LA_129,
    _LA_130,

    /* Strings (LA_S) */
    LA_S131,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S132,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S133,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S134,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S135,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S136,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S137,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S138,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S139,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S140,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S141,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S142,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S143,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S144,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S145,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S146,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S147,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S148,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S149,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S150,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S151,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S152,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S153,  /* "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)"  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F12

#endif /* LANG_H__ */
