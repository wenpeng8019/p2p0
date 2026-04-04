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
    _LA_3,
    _LA_4,

    /* Strings (LA_S) */
    LA_S5,  /* "Enable data relay support (COMPACT mode fallback)"  [server.c] */
    LA_S6,  /* "Enable MSG RPC support"  [server.c] */
    LA_S7,  /* "NAT type detection port (0=disabled)"  [server.c] */
    LA_S8,  /* disabled "Received shutdown signal, exiting gracefully..." */
    LA_S9,  /* "Shutting down...\n"  [server.c] */
    LA_S10,  /* "Signaling server listen port (TCP+UDP)"  [server.c] */
    LA_S11,  /* "Use Chinese language"  [server.c] */

    /* Formats (LA_F) */
    LA_F12,  /* "%s accepted, '%s' -> '%s', ses_id=%u\n" (%s,%s,%s,%u)  [server.c] */
    LA_F13,  /* "%s accepted, peer='%s', auth_key=%llu\n" (%s,%s,%l)  [server.c] */
    LA_F14,  /* "%s accepted, seq=%u, ses_id=%u\n" (%s,%u,%u)  [server.c] */
    LA_F15,  /* "%s for unknown ses_id=%u\n" (%s,%u)  [server.c] */
    LA_F16,  /* "%s forwarded: '%s' -> '%s', sid=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u)  [server.c] */
    LA_F17,  /* "%s forwarded: '%s' -> '%s', sid=%u, msg=%u (ses_id=%u)\n" (%s,%s,%s,%u,%u,%u)  [server.c] */
    LA_F18,  /* "%s from '%.*s': new instance(old=%u new=%u), resetting\n" (%s,%u,%u)  [server.c] */
    LA_F19,  /* "%s new sid=%u > pending sid=%u (responding=%d), canceling old RPC (ses_id=%u)\n" (%s,%u,%u,%d,%u)  [server.c] */
    LA_F20,  /* "%s retransmit during RESP phase, ignoring, sid=%u (ses_id=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F21,  /* "%s retransmit, resend ACK, sid=%u (ses_id=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F22,  /* "%s: RPC complete for '%s', sid=%u (ses_id=%u)\n" (%s,%s,%u,%u)  [server.c] */
    LA_F23,  /* "%s: accepted, local='%.*s', inst_id=%u\n" (%s,%u)  [server.c] */
    LA_F24,  /* "%s: accepted, releasing slot for '%s'\n" (%s,%s)  [server.c] */
    LA_F25,  /* "%s: accepted, ses_id=%u, sid=%u, code=%u, len=%d\n" (%s,%u,%u,%u,%d)  [server.c] */
    LA_F26,  /* "%s: accepted, ses_id=%u, sid=%u, msg=%u, len=%d\n" (%s,%u,%u,%u,%d)  [server.c] */
    LA_F27,  /* "%s: accepted, ses_id=%u, sid=%u\n" (%s,%u,%u)  [server.c] */
    LA_F28,  /* "%s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F29,  /* "%s: addr-notify confirmed '%s' (ses_id=%u)\n" (%s,%s,%u)  [server.c] */
    LA_F30,  /* "%s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F31,  /* "%s: invalid auth_key=0 from %s\n" (%s,%s)  [server.c] */
    LA_F32,  /* "%s: invalid relay flag from client\n" (%s)  [server.c] */
    LA_F33,  /* "%s: auth_key=%llu, cands=%d from %s\n" (%s,%l,%d,%s)  [server.c] */
    LA_F34,  /* "%s: invalid session_id=%u or sid=%u\n" (%s,%u,%u)  [server.c] */
    LA_F35,  /* "%s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    LA_F36,  /* "%s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F37,  /* "%s: obsolete sid=%u (current=%u), ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F38,  /* "%s: obsolete sid=%u (last=%u) in IDLE state, ignoring\n" (%s,%u,%u)  [server.c] */
    LA_F39,  /* "%s: peer '%s' not online for session_id=%u\n" (%s,%s,%u)  [server.c] */
    LA_F40,  /* "%s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F41,  /* "%s: requester not found for ses_id=%u\n" (%s,%u)  [server.c] */
    _LA_42,
    LA_F43,  /* "%s: unknown session_id=%u\n" (%s,%u)  [server.c] */
    LA_F44,  /* "%s: auth_key=%llu assigned for '%.*s'\n" (%s,%l)  [server.c] */
    LA_F45,  /* "Addr changed for '%s', but first info packet was abandoned (ses_id=%u)\n" (%s,%u)  [server.c] */
    LA_F46,  /* "Addr changed for '%s', defer notification until first ACK (ses_id=%u)\n" (%s,%u)  [server.c] */
    LA_F47,  /* "Addr changed for '%s', deferred notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [server.c] */
    LA_F48,  /* "Addr changed for '%s', notifying '%s' (ses_id=%u)\n" (%s,%s,%u)  [server.c] */
    LA_F49,  /* "Cannot relay %s: ses_id=%u (peer unavailable)\n" (%s,%u)  [server.c] */
    LA_F50,  /* "% Goodbye!\n"  [server.c] */
    LA_F51,  /* "Invalid port number %d (range: 1-65535)\n" (%d)  [server.c] */
    LA_F52,  /* "Invalid probe port %d (range: 0-65535)\n" (%d)  [server.c] */
    LA_F53,  /* "MSG_REQ peer timeout after %d retries, sending timeout error to '%s', sid=%u (ses_id=%u)\n" (%d,%s,%u,%u)  [server.c] */
    LA_F54,  /* "MSG_REQ peer went offline, sending error to '%s', sid=%u (ses_id=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F55,  /* "MSG_REQ resent, '%s' -> '%s', sid=%u, attempt %d/%d (ses_id=%u)\n" (%s,%s,%u,%d,%d,%u)  [server.c] */
    LA_F56,  /* "MSG_RESP gave up after %d retries, sid=%u (ses_id=%u)\n" (%d,%u,%u)  [server.c] */
    LA_F57,  /* "MSG_RESP resent back to '%s', sid=%u, attempt %d/%d (ses_id=%u)\n" (%s,%u,%d,%d,%u)  [server.c] */
    LA_F58,  /* "% NAT probe disabled (bind failed)\n"  [server.c] */
    LA_F59,  /* "NAT probe socket listening on port %d\n" (%d)  [server.c] */
    LA_F60,  /* "NAT probe: %s (port %d)\n" (%s,%d)  [server.c] */
    LA_F61,  /* "P2P Signaling Server listening on port %d (TCP + UDP)...\n" (%d)  [server.c] */
    LA_F62,  /* "SYNC resent, %s <-> %s, attempt %d/%d (ses_id=%u)\n" (%s,%s,%d,%d,%u)  [server.c] */
    LA_F63,  /* "SYNC retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F64,  /* "SYNC0: candidates exchanged '%.*s'(%d) <-> '%.*s'(%d)\n" (%d,%d)  [server.c] */
    LA_F65,  /* "Relay %s seq=%u: '%s' -> '%s' (ses_id=%u)\n" (%s,%u,%s,%s,%u)  [server.c] */
    LA_F66,  /* "Relay support: %s\n" (%s)  [server.c] */
    LA_F67,  /* "Send %s: base_index=%u, cands=%d, ses_id=%u, peer='%s'\n" (%s,%u,%d,%u,%s)  [server.c] */
    LA_F68,  /* "Send %s: mapped=%s:%d\n" (%s,%s,%d)  [server.c] */
    LA_F69,  /* "Send %s: peer='%s', reason=%s, ses_id=%u\n" (%s,%s,%s,%u)  [server.c] */
    LA_F70,  /* "Send %s: auth_key=%llu, peer='%s'\n" (%s,%l,%s)  [server.c] */
    LA_F71,  /* "Send %s: ses_id=%u, sid=%u, msg=%u, data_len=%d, peer='%s'\n" (%s,%u,%u,%u,%d,%s)  [server.c] */
    LA_F72,  /* "Send %s: ses_id=%u, sid=%u, peer='%s', flags=0x%02x, code=%u, data_len=%d\n" (%s,%u,%u,%s,%u,%d)  [server.c] */
    LA_F73,  /* "Send %s: ses_id=%u, sid=%u, peer='%s'\n" (%s,%u,%u,%s)  [server.c] */
    LA_F74,  /* "Send %s: ses_id=%u, peer=%s\n" (%s,%u,%s)  [server.c] */
    LA_F75,  /* "Send %s: max_cands=%d, relay=%s, msg=%s, public=%s:%d, probe=%d, auth_key=%llu, inst_id=%u\n" (%s,%d,%s,%s,%s,%d,%d,%l,%u)  [server.c] */
    LA_F76,  /* "Send %s: rejected (no slot available)\n" (%s)  [server.c] */
    LA_F77,  /* "Starting P2P signal server on port %d\n" (%d)  [server.c] */
    LA_F78,  /* "Timeout & cleanup for client '%s' (inactive for %.1f seconds)\n" (%s)  [server.c] */
    LA_F79,  /* "Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
    LA_F80,  /* "[Relay] %s for ses_id=%u: peer unavailable (dropped)\n" (%s,%u)  [server.c] */
    LA_F81,  /* "[Relay] %s for unknown ses_id=%u (dropped)\n" (%s,%u)  [server.c] */
    LA_F82,  /* "[Relay] %s seq=%u: '%s' -> '%s' (ses_id=%u)\n" (%s,%u,%s,%s,%u)  [server.c] */
    LA_F83,  /* "[Relay] %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F84,  /* "[Relay] %s: '%s' -> '%s' (ses_id=%u)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F85,  /* "[Relay] %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    LA_F86,  /* "[Relay] %s: missing SESSION flag, dropped\n" (%s)  [server.c] */
    _LA_87,
    _LA_88,
    _LA_89,
    _LA_90,
    _LA_91,
    _LA_92,
    _LA_93,
    _LA_94,
    _LA_95,
    _LA_96,
    _LA_97,
    _LA_98,
    LA_F99,  /* "% [TCP] Max peers reached, rejecting connection\n"  [server.c] */
    _LA_100,
    LA_F101,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    _LA_102,
    _LA_103,
    _LA_104,
    _LA_105,
    _LA_106,
    _LA_107,
    _LA_108,
    _LA_109,
    _LA_110,
    _LA_111,
    _LA_112,
    _LA_113,
    _LA_114,
    _LA_115,
    _LA_116,
    LA_F117,  /* "[UDP] %s send to %s failed(%d)\n" (%s,%s,%d)  [server.c] */
    _LA_118,
    _LA_119,
    _LA_120,
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
    _LA_131,
    _LA_132,
    _LA_133,
    _LA_134,
    _LA_135,
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
    _LA_163,
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
    LA_F179,  /* "[UDP] %s recv from %s, len=%zu\n" (%s,%s)  [server.c] */
    LA_F180,  /* "[UDP] %s send to %s, seq=%u, flags=0x00, len=%d\n" (%s,%s,%u,%d)  [server.c] */
    LA_F181,  /* "[UDP] %s send to %s, seq=0, flags=0, len=%d\n" (%s,%s,%d)  [server.c] */
    LA_F182,  /* "[UDP] %s send to %s, seq=0, flags=0x%02x, len=%d\n" (%s,%s,%d)  [server.c] */
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
    _LA_196,
    _LA_197,
    _LA_198,
    _LA_199,
    _LA_200,
    _LA_201,
    _LA_202,
    _LA_203,
    _LA_204,
    _LA_205,
    _LA_206,
    _LA_207,
    _LA_208,
    _LA_209,
    _LA_210,
    _LA_211,
    _LA_212,
    LA_F213,  /* "% [TCP] Failed to set client socket to non-blocking mode\n"  [server.c] */
    _LA_214,
    _LA_215,
    _LA_216,
    LA_F217,  /* "% [TCP] OOM: cannot allocate recv buffer for new client\n"  [server.c] */
    _LA_218,
    _LA_219,
    _LA_220,
    _LA_221,
    _LA_222,
    _LA_223,
    _LA_224,
    _LA_225,
    _LA_226,
    _LA_227,
    _LA_228,
    _LA_229,
    _LA_230,
    _LA_231,
    _LA_232,
    _LA_233,
    _LA_234,
    _LA_235,
    _LA_236,
    _LA_237,
    _LA_238,
    _LA_239,
    _LA_240,
    _LA_241,
    _LA_242,
    _LA_243,
    _LA_244,
    _LA_245,
    _LA_246,
    _LA_247,
    _LA_248,
    LA_F249,  /* "% Client closed connection (EOF on recv)\n"  [server.c] */
    LA_F250,  /* "% Client disconnected (not yet logged in)\n"  [server.c] */
    LA_F251,  /* "% Client sent data before ONLINE_ACK completed\n"  [server.c] */
    LA_F252,  /* "%s: OOM for relay buffer\n" (%s)  [server.c] */
    LA_F253,  /* "%s: OOM for zero-copy recv buffer\n" (%s)  [server.c] */
    LA_F254,  /* "%s: bad FIN marker=0x%02x\n" (%s)  [server.c] */
    LA_F255,  /* "%s: bad payload(cnt=%d, len=%u, expected=%u)\n" (%s,%d,%u,%u)  [server.c] */
    LA_F256,  /* "%s: bad payload(cnt=%u, len=%u, expected=%u+1fin)\n" (%s,%u,%u,%u)  [server.c] */
    LA_F257,  /* "%s: '%s' sid=%u code=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [server.c] */
    LA_F258,  /* "%s: build_session failed for '%s'\n" (%s,%s)  [server.c] */
    LA_F259,  /* "%s: close ses_id=%u\n" (%s,%u)  [server.c] */
    LA_F260,  /* "%s: forwarded to peer, cands=%d\n" (%s,%d)  [server.c] */
    LA_F261,  /* "%s: local='%s', remote='%s', side=%d, peer_online=%d, cands=%d\n" (%s,%s,%s,%d,%d,%d)  [server.c] */
    LA_F262,  /* "%s: peer '%s' offline, cached cands=%d\n" (%s,%s,%d)  [server.c] */
    LA_F263,  /* "'%s' disconnected\n" (%s)  [server.c] */
    LA_F264,  /* "'%s' timeout (inactive for %.1f sec)\n" (%s)  [server.c] */
    LA_F265,  /* "Client closed connection (EOF on send, reason=%s)\n" (%s)  [server.c] */
    LA_F266,  /* "Duplicate session create blocked: '%s' -> '%s'\n" (%s,%s)  [server.c] */
    LA_F267,  /* "ONLINE: '%s' came online (inst=%u)\n" (%s,%u)  [server.c] */
    LA_F268,  /* "ONLINE: '%s' new instance (old=%u, new=%u), destroying old\n" (%s,%u,%u)  [server.c] */
    LA_F269,  /* "ONLINE: '%s' reconnected (inst=%u), migrating fd\n" (%s,%u)  [server.c] */
    LA_F270,  /* "ONLINE: bad payload(len=%u, expected=%u)\n" (%u,%u)  [server.c] */
    LA_F271,  /* "ONLINE: duplicate from '%s'\n" (%s)  [server.c] */
    LA_F272,  /* "ONLINE_ACK sent to '%s'\n" (%s)  [server.c] */
    LA_F273,  /* "SYNC0_ACK queue busy for '%s', drop\n" (%s)  [server.c] */
    LA_F274,  /* "SYNC_ACK queue busy for '%s', drop\n" (%s)  [server.c] */
    LA_F275,  /* "bad payload len %u (type=%u)\n" (%u,%u)  [server.c] */
    LA_F276,  /* "bad payload len %u\n" (%u)  [server.c] */
    LA_F277,  /* "recv() failed: errno=%d\n" (%d)  [server.c] */
    LA_F278,  /* "send(%s) failed: errno=%d\n" (%s,%d)  [server.c] */
    _LA_279,
    LA_F280,  /* "%s: peer offline, sending error resp\n" (%s)  [server.c] */
    LA_F281,  /* "type=%u rejected: client not logged in\n" (%u)  [server.c] */
    LA_F282,  /* "unknown ses_id=%u (type=%u)\n" (%u,%u)  [server.c] */
    LA_F283,  /* "unsupported type=%u (ses_id=%u)\n" (%u,%u)  [server.c] */
    _LA_284,
    _LA_285,
    _LA_286,
    _LA_287,
    LA_F288,  /* "%s: rpc busy (pending sid=%u)\n" (%s,%u)  [server.c] */
    _LA_289,
    _LA_290,
    _LA_291,
    _LA_292,
    _LA_293,
    LA_F294,  /* "%s: '%s' sid=%u msg=%u data_len=%d\n" (%s,%s,%u,%u,%d)  [server.c] */
    LA_F295,  /* "%s: bad payload(len=%u)\n" (%s,%u)  [server.c] */
    LA_F296,  /* "%s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    LA_F297,  /* "%s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F298,  /* "%s: '%.*s' cleared stale peer marker, ready for re-pair\n" (%s)  [server.c] */
    _LA_299,
    LA_F300,  /* "%s: requester offline, discarding\n" (%s)  [server.c] */
    LA_F301,  /* "%s: sid mismatch (got=%u, pending=%u), discarding\n" (%s,%u,%u)  [server.c] */
    LA_F302,  /* "%s: unknown auth_key=%llu from %s\n" (%s,%l,%s)  [server.c] */
    LA_F303,  /* "% RPC_ERR: OOM\n"  [server.c] */
    LA_F304,  /* "Send %s: ses_id=%u, sid=%u, status=%u\n" (%s,%u,%u,%u)  [server.c] */
    LA_F305,  /* "ses_id=%u busy (pending relay)\n" (%u)  [server.c] */
    LA_F306,  /* "ses_id=%u peer not connected (type=%u)\n" (%u,%u)  [server.c] */
    _LA_307,
    _LA_308,
    _LA_309,
    _LA_310,
    _LA_311,
    _LA_312,
    _LA_313,
    _LA_314,
    _LA_315,
    _LA_316,
    _LA_317,
    _LA_318,
    _LA_319,
    _LA_320,
    _LA_321,
    _LA_322,
    _LA_323,
    _LA_324,
    _LA_325,
    _LA_326,
    _LA_327,
    _LA_328,
    _LA_329,
    _LA_330,
    _LA_331,
    _LA_332,
    _LA_333,
    _LA_334,
    _LA_335,
    _LA_336,
    _LA_337,
    LA_F338,  /* "Send %s: cands=%d, ses_id=%u, peer='%s'\n" (%s,%d,%u,%s)  [server.c] */
    _LA_339,
    LA_F340,  /* "%s: confirmed '%s', retries=%d (ses_id=%u)\n" (%s,%s,%d,%u)  [server.c] */
    LA_F341,  /* "[UDP] %s recv from %s, seq=%u, flags=0x%02x, len=%zu\n" (%s,%s,%u)  [server.c] */
    _LA_342,
    _LA_343,
    _LA_344,
    _LA_345,
    _LA_346,
    _LA_347,
    _LA_348,
    _LA_349,
    LA_F350,  /* "%s: build_session failed for '%.*s'\n" (%s)  [server.c] */
    LA_F351,  /* "%s: late-paired '%.*s' <-> '%.*s' (waiting session found)\n" (%s)  [server.c] */
    LA_F352,  /* "%s: paired '%.*s' <-> '%.*s'\n" (%s)  [server.c] */
    LA_F353,  /* "%s: skip pairing '%.*s' with stale '%.*s' (peer_died, awaiting re-register)\n" (%s)  [server.c] */
    _LA_354,
    _LA_355,
    _LA_356,
    _LA_357,
    _LA_358,

    /* Strings (LA_S) */
    LA_S359,  /* "Description:\n  P2P signaling server supporting both COMPACT (UDP) and RELAY (TCP) modes.\n  - COMPACT: Stateless UDP signaling with integrated candidate exchange\n  - RELAY:   Stateful TCP signaling for ICE/STUN/TURN architecture\n\nExamples:\n  $0                              # Default: port 9333, no probe, no relay\n  $0 -p 8888                      # Listen on port 8888\n  $0 -p 8888 -P 8889              # Port 8888, probe port 8889\n  $0 -p 8888 -P 8889 --relay      # Full config with relay support\n  $0 --cn -p 8888                 # Chinese language\n\nNote: Run without arguments to use default configuration (port 9333)"  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F249

#endif /* LANG_H__ */
