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
    LA_W1,  /* "CLOSED"  [p2p_ping.c] */
    LA_W2,  /* "CLOSING"  [p2p_ping.c] */
    LA_W3,  /* "CONNECTED"  [p2p_ping.c] */
    LA_W4,  /* "ERROR"  [p2p_ping.c] */
    LA_W5,  /* "INIT"  [p2p_ping.c] */
    LA_W6,  /* "PUNCHING"  [p2p_ping.c] */
    LA_W7,  /* "REGISTERING"  [p2p_ping.c] */
    LA_W8,  /* "RELAY"  [p2p_ping.c] */
    LA_W9,  /* "UNKNOWN"  [p2p_ping.c] */

    /* Strings (LA_S) */
    LA_S10,  /* "--- Connected ---"  [p2p_ping.c] */
    LA_S11,  /* "--- Peer disconnected ---"  [p2p_ping.c] */
    LA_S12,  /* "[EVENT] Connection closed"  [p2p_ping.c] */
    LA_S13,  /* "Auto-echo received messages back to sender"  [p2p_ping.c] */
    _LA_14,
    LA_S15,  /* "Enable DTLS (MbedTLS)"  [p2p_ping.c] */
    LA_S16,  /* "Enable DTLS (OpenSSL)"  [p2p_ping.c] */
    LA_S17,  /* "Enable PseudoTCP"  [p2p_ping.c] */
    LA_S18,  /* "GitHub Gist ID for Public Signaling"  [p2p_ping.c] */
    LA_S19,  /* "GitHub Token for Public Signaling"  [p2p_ping.c] */
    LA_S20,  /* "Log level (0-5)"  [p2p_ping.c] */
    LA_S21,  /* "Signaling server IP[:PORT]"  [p2p_ping.c] */
    _LA_22,
    LA_S23,  /* "Target Peer Name (if specified: active role)"  [p2p_ping.c] */
    LA_S24,  /* "TURN password"  [p2p_ping.c] */
    LA_S25,  /* "TURN server address"  [p2p_ping.c] */
    LA_S26,  /* "TURN username"  [p2p_ping.c] */
    LA_S27,  /* "Use Chinese language"  [p2p_ping.c] */
    LA_S28,  /* "Use COMPACT mode (UDP signaling, default is ICE/TCP)"  [p2p_ping.c] */
    LA_S29,  /* "Your Peer Name"  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F30,  /* "% === P2P Ping Diagnostic Tool ===\n"  [p2p_ping.c] */
    LA_F31,  /* "% Failed to create sessions\n"  [p2p_ping.c] */
    LA_F32,  /* "% Failed to initialize connection\n"  [p2p_ping.c] */
    LA_F33,  /* "% No signaling mode.\nUse --server or --github\n"  [p2p_ping.c] */
    LA_F34,  /* "Running in %s mode (connecting to %s)..." (%s,%s)  [p2p_ping.c] */
    LA_F35,  /* "Running in %s mode (waiting for connection)..." (%s)  [p2p_ping.c] */
    LA_F36,  /* "% [Chat] Echo mode enabled: received messages will be echoed back.\n"  [p2p_ping.c] */
    LA_F37,  /* "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n"  [p2p_ping.c] */
    LA_F38,  /* "[STATE] %s (%d) -> %s (%d)" (%s,%d,%s,%d)  [p2p_ping.c] */
    _LA_39,

    /* Strings (LA_S) */
    LA_S40,  /* "Debugger Name"  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F41,  /* "% Debugger connected, resuming execution.\n"  [p2p_ping.c] */
    LA_F42,  /* "% Timeout waiting for debugger. Continuing without debugger.\n"  [p2p_ping.c] */
    LA_F43,  /* "Waiting Debugger(%s) connecting...\n" (%s)  [p2p_ping.c] */

    /* Strings (LA_S) */
    LA_S44,  /* "STUN server address"  [p2p_ping.c] */

    /* Formats (LA_F) */
    LA_F45,  /* "[STATE] %s (%d) -> %s (%d)\n" (%s,%d,%s,%d)  [p2p_ping.c] */

    LA_NUM
};

/* 格式字符串起始位置（用于验证） */
#define LA_FMT_START LA_F30

#endif /* LANG_H__ */
