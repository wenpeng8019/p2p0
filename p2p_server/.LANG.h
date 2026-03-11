/*
 * Auto-generated language IDs
 *
 * DO NOT EDIT - Regenerate with: ./i18n/i18n.sh
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
    LA_S3,  /* "               - TCP: RELAY mode signaling (stateful/long connection)"  [server.c] */
    LA_S4,  /* "               - UDP: COMPACT mode signaling (stateless)"  [server.c] */
    LA_S5,  /* "               Used to detect symmetric NAT (port consistency)"  [server.c] */
    LA_S6,  /* "  port         Signaling server listen port (default: 8888)"  [server.c] */
    LA_S7,  /* "  probe_port   NAT type detection port (default: 0=disabled)"  [server.c] */
    LA_S8,  /* "  relay        Enable data relay support (COMPACT mode fallback)"  [server.c] */
    LA_S9,  /* "[SERVER] Goodbye!"  [server.c] */
    LA_S10,  /* "[SERVER] NAT probe disabled (bind failed)"  [server.c] */
    LA_S11,  /* "[SERVER] Received shutdown signal, exiting gracefully..."  [server.c] */
    LA_S12,  /* "[SERVER] Shutting down..."  [server.c] */

    /* Formats (LA_F) */
    LA_F13,  /* "[TCP] E: Invalid magic from peer '%s'\n" (%s)  [server.c] */

    /* Strings (LA_S) */
    LA_S14,  /* "[TCP] Max peers reached, rejecting connection\n"  [server.c] */
    LA_S15,  /* "[TCP] User list truncated (too many users)\n"  [server.c] */
    LA_S16,  /* "Error: Too many arguments"  [server.c] */
    LA_S17,  /* "Examples:"  [server.c] */
    LA_S18,  /* "Parameters:"  [server.c] */
    _LA_19,

    /* Formats (LA_F) */
    LA_F20,  /* "  %s                    # Default config (port 8888, no probe, no relay)" (%s)  [server.c] */
    LA_F21,  /* "  %s 9000               # Listen on port 9000" (%s)  [server.c] */
    LA_F22,  /* "  %s 9000 9001          # Listen 9000, probe port 9001" (%s)  [server.c] */
    LA_F23,  /* "  %s 9000 9001 relay    # Listen 9000, probe 9001, enable relay" (%s)  [server.c] */
    LA_F24,  /* "Error: Invalid port number '%s' (range: 1-65535)" (%s)  [server.c] */
    LA_F25,  /* "Error: Invalid probe port '%s' (range: 0-65535)" (%s)  [server.c] */
    LA_F26,  /* "Error: Unknown option '%s' (expected: 'relay')" (%s)  [server.c] */
    LA_F27,  /* "P2P Signaling Server listening on port %d (TCP + UDP)..." (%d)  [server.c] */
    LA_F28,  /* "Usage: %s [port] [probe_port] [relay]" (%s)  [server.c] */
    _LA_29,
    LA_F30,  /* "[PROBE] NAT_PROBE_ACK -> %s:%d (seq=%u, mapped=%s:%d)\n" (%s,%d,%u,%s,%d)  [server.c] */
    LA_F31,  /* "[SERVER] NAT probe socket listening on port %d" (%d)  [server.c] */
    LA_F32,  /* "[SERVER] NAT probe: %s (port %d)" (%s,%d)  [server.c] */
    LA_F33,  /* "[SERVER] Relay support: %s" (%s)  [server.c] */
    LA_F34,  /* "[SERVER] Starting P2P signal server on port %d" (%d)  [server.c] */
    LA_F35,  /* "[TCP]   → Forwarded OFFER from '%s' (%d candidates, %d bytes)\n" (%s,%d,%d)  [server.c] */
    LA_F36,  /* "[TCP]   → Sent empty OFFER from '%s' (storage full, reverse connect)\n" (%s)  [server.c] */
    LA_F37,  /* "[TCP] All pending candidates flushed to '%s'\n" (%s)  [server.c] */
    LA_F38,  /* "[TCP] Cached %d candidates for offline user '%s' (total=%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F39,  /* "[TCP] Cached %d candidates for offline user '%s', storage now FULL (%d/%d)\n" (%d,%s,%d,%d)  [server.c] */
    LA_F40,  /* "[TCP] Cannot allocate slot for offline user '%s'\n" (%s)  [server.c] */
    LA_F41,  /* "[TCP] W: Client '%s' timeout (inactive for %ld seconds)\n" (%s,%l)  [server.c] */
    LA_F42,  /* "[TCP] Failed to receive payload from %s\n" (%s)  [server.c] */
    LA_F43,  /* "[TCP] Failed to receive target name from %s\n" (%s)  [server.c] */
    LA_F44,  /* "[TCP] Failed to send CONNECT_ACK to %s (sent_hdr=%d, sent_payload=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F45,  /* "[TCP] Flushing %d pending candidates from '%s' to '%s'...\n" (%d,%s,%s)  [server.c] */
    LA_F46,  /* "[TCP] Merged %d pending candidates from offline slot (sender='%s') into online slot for '%s'\n" (%d,%s,%s)  [server.c] */
    LA_F47,  /* "[TCP] New connection from %s:%d\n" (%s,%d)  [server.c] */
    LA_F48,  /* "[TCP] New sender '%s' replaces old sender '%s' (discarding %d old candidates)\n" (%s,%s,%d)  [server.c] */
    LA_F49,  /* "[TCP] Payload too large (%u bytes) from %s\n" (%u,%s)  [server.c] */
    LA_F50,  /* "[TCP] V: Peer '%s' disconnected\n" (%s)  [server.c] */
    LA_F51,  /* "[TCP] I: Peer '%s' logged in\n" (%s)  [server.c] */
    LA_F52,  /* "[TCP] Relaying %s from %s to %s (%u bytes)\n" (%s,%s,%s,%u)  [server.c] */
    LA_F53,  /* "[TCP] Sent %s with %d candidates to '%s' (from '%s')\n" (%s,%d,%s,%s)  [server.c] */
    LA_F54,  /* "[TCP] Sent CONNECT_ACK to %s (status=%d, candidates_acked=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F55,  /* "[TCP] Storage full for '%s' (cached=%d, dropped=%d)\n" (%s,%d,%d)  [server.c] */
    LA_F56,  /* "[TCP] Storage full indication flushed to '%s'\n" (%s)  [server.c] */
    LA_F57,  /* "[TCP] Storage full, connection intent from '%s' to '%s' noted\n" (%s,%s)  [server.c] */
    LA_F58,  /* "[TCP] Storage full, flushing connection intent from '%s' to '%s' (sending empty OFFER)...\n" (%s,%s)  [server.c] */
    LA_F59,  /* "[TCP] Target %s offline, caching candidates...\n" (%s)  [server.c] */
    LA_F60,  /* "[TCP] Unknown message type %d from %s\n" (%d,%s)  [server.c] */
    _LA_61,
    _LA_62,
    LA_F63,  /* "[UDP] E: %s: invalid seq=%u\n" (%s,%u)  [server.c] */
    LA_F64,  /* "[UDP] E: %s: invalid instance_id=0 from %s\n" (%s,%s)  [server.c] */
    _LA_65,
    LA_F66,  /* "[UDP] W: PEER_INFO retransmit failed: %s <-> %s (gave up after %d tries)\n" (%s,%s,%d)  [server.c] */
    LA_F67,  /* "[UDP] E: %s seq=0 from client %s (server-only, dropped)\n" (%s,%s)  [server.c] */
    LA_F68,  /* "[UDP] I: Pairing complete: '%.*s'(%d cands) <-> '%.*s'(%d cands)\n" (%d,%d)  [server.c] */
    _LA_69,
    _LA_70,
    _LA_71,
    LA_F72,  /* "[UDP] W: Timeout for pair '%s' -> '%s' (inactive for %ld seconds)\n" (%s,%s,%l)  [server.c] */
    LA_F73,  /* "[UDP] V: %s: accepted, local='%.*s', remote='%.*s', inst_id=%u, cands=%d\n" (%s,%u,%d)  [server.c] */
    LA_F74,  /* "[UDP] E: REGISTER_ACK sent, status=error (no slot available)\n"  [server.c] */
    LA_F75,  /* "[UDP] I: %s from '%.*s': new instance(old=%u new=%u), resetting session\n" (%s,%u,%u)  [server.c] */
    _LA_76,
    _LA_77,
    _LA_78,
    _LA_79,
    _LA_80,
    _LA_81,
    LA_F82,  /* "[UDP] E: Relay %s: bad payload(len=%zu)\n" (%s)  [server.c] */
    _LA_83,
    _LA_84,
    LA_F85,  /* "[UDP] V: %s: waiting for peer '%.*s' to register\n" (%s)  [server.c] */
    LA_F86,  /* "[UDP] V: %s: accepted, releasing slot for '%s' -> '%s'\n" (%s,%s,%s)  [server.c] */
    LA_F87,  /* "[UDP] W: Unknown packet type 0x%02x from %s\n" (%s)  [server.c] */
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
    _LA_99,
    _LA_100,
    _LA_101,
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
    _LA_117,
    _LA_118,
    _LA_119,
    _LA_120,
    _LA_121,
    _LA_122,
    _LA_123,
    _LA_124,
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
    _LA_213,
    _LA_214,
    _LA_215,
    _LA_216,
    _LA_217,
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
    _LA_249,
    _LA_250,
    _LA_251,
    _LA_252,
    _LA_253,
    _LA_254,
    _LA_255,
    _LA_256,
    _LA_257,
    _LA_258,
    _LA_259,
    _LA_260,
    _LA_261,
    _LA_262,
    _LA_263,
    _LA_264,
    _LA_265,
    _LA_266,
    _LA_267,
    _LA_268,
    _LA_269,
    _LA_270,
    _LA_271,
    _LA_272,
    _LA_273,
    _LA_274,
    _LA_275,
    _LA_276,
    _LA_277,
    _LA_278,
    _LA_279,
    LA_F280,  /* "[TCP] V: %s sent to '%s'\n" (%s,%s)  [server.c] */
    LA_F281,  /* "[UDP] E: %s: data too large (len=%d)\n" (%s,%d)  [server.c] */
    LA_F282,  /* "[UDP] E: %s: invalid relay flag from client\n" (%s)  [server.c] */
    _LA_283,
    _LA_284,
    _LA_285,
    _LA_286,
    _LA_287,
    LA_F288,  /* "[UDP] V: %s accepted from %s, sid=%u\n" (%s,%s,%u)  [server.c] */
    _LA_289,
    _LA_290,
    _LA_291,
    _LA_292,
    _LA_293,
    _LA_294,
    _LA_295,
    LA_F296,  /* "[UDP] V: %s: no matching pending msg (sid=%u)\n" (%s,%u)  [server.c] */
    _LA_297,
    _LA_298,
    _LA_299,
    _LA_300,
    LA_F301,  /* "[UDP] W: %s: already has pending msg, rejecting sid=%u\n" (%s,%u)  [server.c] */
    LA_F302,  /* "[UDP] W: %s: no matching pending msg (sid=%u, expected=%u)\n" (%s,%u,%u)  [server.c] */
    LA_F303,  /* "[UDP] W: %s: peer '%s' not online, rejecting sid=%u\n" (%s,%s,%u)  [server.c] */
    LA_F304,  /* "[UDP] W: %s: requester not found for %s\n" (%s,%s)  [server.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F20

/* 字符串表 */
extern const char* lang_en[LA_NUM];

#endif /* LANG_H__ */
