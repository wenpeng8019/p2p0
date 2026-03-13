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
    LA_F6,  /* "%s from '%.*s': new instance(old=%u new=%u), resetting session\n" (%s,%u,%u)  [server.c] */
    LA_F7,  /* "%s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n" (%s,%u,%d)  [server.c] */
    LA_F8,  /* "%s: accepted, releasing slot for '%s' -> '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F9,  /* "%s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F10,  /* "%s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F11,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F12,  /* "%s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F13,  /* "%s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F14,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F15,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F16,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F17,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F18,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F19,  /* "%s: waiting for peer '%.*s' to register\n" (%s)  [server.c] */
    LA_F20,  /* "% Goodbye!\n"  [server.c] */
    LA_F21,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F22,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F23,  /* "% NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F24,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F25,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F26,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F27,  /* "PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F28,  /* "Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n" (%d,%d)  [server.c] */
    LA_F29,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F30,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F31,  /* "Send %s: status=error (no slot available)\n" (%s)  [server.c] */
    LA_F32,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F33,  /* "Timeout & cleanup for pair '%s' -> '%s' (inactive for %.1f seconds)\n" (%s,%s)  [server.c] */
    LA_F34,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
    LA_F35,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F36,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F37,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F38,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F39,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F40,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F41,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F42,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F43,  /* "[TCP] E: Invalid magic from peer '%s'\n" (%s)  [server.c] */
    LA_F44,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F45,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F46,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F47,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F48,  /* "[TCP] I: Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F49,  /* "% [TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_F50,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F51,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F52,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F53,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F54,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F55,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F56,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F57,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F58,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F59,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F60,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F61,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F62,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    LA_F63,  /* "[TCP] V: %s sent to '%s'\n" (%s,%s)  [server.c] */
    LA_F64,  /* "[TCP] V: Peer '%s' disconnected\n" (%s)  [server.c] */
    LA_F65,  /* "[TCP] W: Client '%s' timeout (inactive for %.1f seconds)\n" (%s)  [server.c] */
    LA_F66,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c] */
    LA_F67,  /* "[UDP] %s send to %s failed(%d)\n" (%s,%s,%d)  [server.c] */
    LA_F68,  /* "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n" (%s,%s,%u,%d)  [server.c] */
    LA_F69,  /* "[UDP] %s send to %s, seq=0, flags=0, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F70,  /* "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F71,  /* "[UDP] %s send to %s:%d failed(%d)\n" (%s,%s,%d,%d)  [server.c] */
    LA_F72,  /* "[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n" (%s,%s,%d,%d)  [server.c] */
    LA_F73,  /* "[UDP] %s send to %s:%d, seq=0, flags=0x%02x, len=%d, retries=%d\n" (%s,%s,%d,%d,%d)  [server.c] */
    LA_F74,  /* "% net init failed\n"  [server.c] */
    LA_F75,  /* "probe UDP bind failed(%d)\n" (%d)  [server.c] */
    LA_F76,  /* "select failed(%d)\n" (%d)  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F6

#endif /* LANG_H__ */
