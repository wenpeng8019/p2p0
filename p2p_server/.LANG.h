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
    LA_S4,  /* "Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S5,  /* "Shutting down...\n"  [server.c] */

    /* Formats (LA_F) */
    LA_F6,  /* "%s accepted, '%s' -> '%s', ses_id=%llu\n" (%s,%s,%s,%l)  [server.c] */
    LA_F7,  /* "%s accepted, peer='%s', ses_id=%llu\n" (%s,%s,%l)  [server.c] */
    LA_F8,  /* "%s accepted, seq=%u, ses_id=%llu\n" (%s,%u,%l)  [server.c] */
    LA_F9,  /* "%s for unknown ses_id=%llu\n" (%s,%l)  [server.c] */
    LA_F10,  /* "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%llu)\n" (%s,%s,%s,%u,%l)  [server.c] */
    LA_F11,  /* "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%llu)\n" (%s,%s,%s,%u,%u,%l)  [server.c] */
    LA_F12,  /* "%s from '%.*s': new instance(old=%u new=%u), resetting session\n" (%s,%u,%u)  [server.c] */
    LA_F13,  /* "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%llu)\n" (%s,%u,%u,%d,%l)  [server.c] */
    LA_F14,  /* "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%llu)\n" (%s,%u,%l)  [server.c] */
    LA_F15,  /* "%s retransmit, resend ACK, sid=%u (ses_id=%llu)\n" (%s,%u,%l)  [server.c] */
    LA_F16,  /* "%s: RPC complete for '%s', sid=%u (ses_id=%llu)\n" (%s,%s,%u,%l)  [server.c] */
    LA_F17,  /* "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n" (%s,%u,%d)  [server.c] */
    LA_F18,  /* "%s: accepted, releasing slot for '%s' -> '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F19,  /* "%s: accepted, ses_id=%llu, sid=%u, code=%u, len=%d\n" (%s,%l,%u,%u,%d)  [server.c] */
    LA_F20,  /* "%s: accepted, ses_id=%llu, sid=%u, msg=%u, len=%d\n" (%s,%l,%u,%u,%d)  [server.c] */
    LA_F21,  /* "%s: accepted, ses_id=%llu, sid=%u\n" (%s,%l,%u)  [server.c] */
    LA_F22,  /* "%s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F23,  /* "%s: confirmed '%s', retries=%d (ses_id=%llu)\n" (%s,%s,%d,%l)  [server.c] */
    LA_F24,  /* "%s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F25,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F26,  /* "%s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F27,  /* "%s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F28,  /* "%s: invalid session_id=%llu or sid=%u\n" (%s,%l,%u)  [server.c] */
    LA_F29,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F30,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F31,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F32,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F33,  /* "%s: peer '%s' not online for session_id=%llu\n" (%s,%s,%l)  [server.c] */
    LA_F34,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F35,  /* "%s: requester not found for ses_id=%llu\n" (%s,%l)  [server.c] */
    LA_F36,  /* "%s: unknown session_id=%llu\n" (%s,%l)  [server.c] */
    LA_F37,  /* "%s: waiting for peer '%.*s' to register\n" (%s)  [server.c] */
    LA_F38,  /* "Addr changed for '%s', but first info packet was abandoned (ses_id=%llu)\n" (%s,%l)  [server.c] */
    LA_F39,  /* "Addr changed for '%s', defer notification until first ACK (ses_id=%llu)\n" (%s,%l)  [server.c] */
    LA_F40,  /* "Addr changed for '%s', deferred notifying '%s' (ses_id=%llu)\n" (%s,%s,%l)  [server.c] */
    LA_F41,  /* "Addr changed for '%s', notifying '%s' (ses_id=%llu)\n" (%s,%s,%l)  [server.c] */
    LA_F42,  /* "Cannot relay %s: ses_id=%llu (peer unavailable)\n" (%s,%l)  [server.c] */
    LA_F43,  /* "% Goodbye!\n"  [server.c] */
    LA_F44,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F45,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F46,  /* "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%llu)\n" (%d,%s,%u,%l)  [server.c] */
    LA_F47,  /* "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%llu)\n" (%s,%u,%l)  [server.c] */
    LA_F48,  /* "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%llu)\n" (%s,%s,%u,%d,%d,%l)  [server.c] */
    LA_F49,  /* "MSG_RESP gave up after %d retries, sid=%u (ses_id=%llu)\n" (%d,%u,%l)  [server.c] */
    LA_F50,  /* "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%llu)\n" (%s,%u,%d,%d,%l)  [server.c] */
    LA_F51,  /* "% NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F52,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F53,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F54,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F55,  /* "PEER_INFO resent, %s <-> %s, attempt %d/%d (ses_id=%llu)\n" (%s,%s,%d,%d,%l)  [server.c] */
    LA_F56,  /* "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F57,  /* "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n" (%d,%d)  [server.c] */
    LA_F58,  /* "Relay %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n" (%s,%u,%s,%s,%l)  [server.c] */
    LA_F59,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F60,  /* "Send %s: base_index=%u, cands=%d, ses_id=%llu, peer='%s'\n" (%s,%u,%d,%l,%s)  [server.c] */
    LA_F61,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F62,  /* "Send %s: peer='%s', reason=%s, ses_id=%llu\n" (%s,%s,%s,%l)  [server.c] */
    LA_F63,  /* "Send %s: ses_id=%llu, peer='%s'\n" (%s,%l,%s)  [server.c] */
    LA_F64,  /* "Send %s: ses_id=%llu, sid=%u, msg=%u, data_len=%d, peer='%s'\n" (%s,%l,%u,%u,%d,%s)  [server.c] */
    LA_F65,  /* "Send %s: ses_id=%llu, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n" (%s,%l,%u,%s,%u,%d)  [server.c] */
    LA_F66,  /* "Send %s: ses_id=%llu, sid=%u, peer='%s'\n" (%s,%l,%u,%s)  [server.c] */
    LA_F67,  /* "Send %s: ses_id=%llu, sid=%u, status=%u\n" (%s,%l,%u,%u)  [server.c] */
    LA_F68,  /* "Send %s: status=%s, max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, ses_id=%llu, inst_id=%u\n" (%s,%s,%d,%s,%s,%s,%d,%d,%l,%u)  [server.c] */
    LA_F69,  /* "Send %s: status=error (no slot available)\n" (%s)  [server.c] */
    LA_F70,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F71,  /* "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n" (%s,%s)  [server.c] */
    LA_F72,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
    LA_F73,  /* "[Relay] %s for ses_id=%llu: peer unavailable (dropped)\n" (%s,%l)  [server.c] */
    LA_F74,  /* "[Relay] %s for unknown ses_id=%llu (dropped)\n" (%s,%l)  [server.c] */
    LA_F75,  /* "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%llu)\n" (%s,%u,%s,%s,%l)  [server.c] */
    LA_F76,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F77,  /* "[Relay] %s: '%s' -> '%s' (ses_id=%llu)\n" (%s,%s,%s,%l)  [server.c] */
    LA_F78,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F79,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F80,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F81,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F82,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F83,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F84,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F85,  /* "[TCP] E: Invalid magic from peer '%s'\n" (%s)  [server.c] */
    LA_F86,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F87,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F88,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F89,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F90,  /* "[TCP] I: Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F91,  /* "% [TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_F92,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F93,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F94,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F95,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F96,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F97,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F98,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F99,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F100,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F101,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F102,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F103,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F104,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    LA_F105,  /* "[TCP] V: %s sent to '%s'\n" (%s,%s)  [server.c] */
    LA_F106,  /* "[TCP] V: Peer '%s' disconnected\n" (%s)  [server.c] */
    LA_F107,  /* "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n" (%s)  [server.c] */
    LA_F108,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c] */
    LA_F109,  /* "[UDP] %s send to %s failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F110,  /* "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n" (%s,%s,%u,%d)  [server.c] */
    LA_F111,  /* "[UDP] %s send to %s, seq=0, flags=0, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F112,  /* "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F113,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [server.c] */
    LA_F114,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F115,  /* "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n" (%s,%s,%d,%d,%d)  [server.c] */
    LA_F116,  /* "% net init failed\n"  [server.c] */
    LA_F117,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F118,  /* "select failed(%d)\n" (%d)  [server.c] */
    _LA_119,

    /* Strings (LA_S) */
    LA_S120,  /* "Enable data relay support (COMPACT mode fallback)"  [server.c] */
    LA_S121,  /* "Enable MSG RPC support"  [server.c] */
    LA_S122,  /* "NAT type detection port (0=disabled)"  [server.c] */
    LA_S123,  /* "Signaling server listen port (TCP+UDP)"  [server.c] */
    LA_S124,  /* "Use Chinese language"  [server.c] */
    _LA_125,
    _LA_126,
    _LA_127,
    _LA_128,
    _LA_129,
    _LA_130,
    _LA_131,
    _LA_132,
    _LA_133,
    _LA_134,

    /* Formats (LA_F) */
    LA_F135,  /* "%s: reset '%.*s'(disconnected) session for re-pairing\n" (%s)  [server.c] */
    _LA_136,
    _LA_137,
    _LA_138,
    _LA_139,
    _LA_140,
    _LA_141,
    _LA_142,
    _LA_143,
    _LA_144,
    _LA_145,
    _LA_146,
    _LA_147,
    _LA_148,
    _LA_149,
    _LA_150,
    _LA_151,
    _LA_152,
    _LA_153,
    _LA_154,
    _LA_155,
    _LA_156,
    _LA_157,
    _LA_158,
    _LA_159,
    _LA_160,
    _LA_161,
    _LA_162,
    LA_F163,  /* "[Relay] %s: missing SESSION flag, dropped\n" (%s)  [server.c] */
    _LA_164,
    _LA_165,
    _LA_166,
    _LA_167,
    _LA_168,
    _LA_169,
    _LA_170,
    _LA_171,
    _LA_172,
    _LA_173,
    _LA_174,
    _LA_175,
    _LA_176,
    _LA_177,
    _LA_178,
    _LA_179,
    _LA_180,
    _LA_181,
    _LA_182,
    _LA_183,
    _LA_184,
    _LA_185,
    _LA_186,
    _LA_187,
    _LA_188,
    _LA_189,
    _LA_190,
    _LA_191,
    _LA_192,
    _LA_193,
    _LA_194,
    _LA_195,

    /* Strings (LA_S) */
    LA_S196,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S197,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S198,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S199,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S200,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S201,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S202,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S203,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S204,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S205,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S206,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S207,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S208,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S209,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S210,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S211,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S212,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S213,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S214,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S215,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S216,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S217,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S218,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S219,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S220,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S221,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S222,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S223,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S224,  /* disabled "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)" */
    LA_S225,  /* "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)"  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F6

#endif /* LANG_H__ */
