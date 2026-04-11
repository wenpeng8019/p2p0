/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_ping;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "CLOSED",  /* SID:1 */
    [LA_W2] = "CONNECTED",  /* SID:2 */
    [LA_W3] = "ERROR",  /* SID:3 */
    [LA_W4] = "INIT",  /* SID:4 */
    [LA_W5] = "ONLINE",  /* SID:5 */
    [LA_W6] = "PUNCHING",  /* SID:6 */
    [LA_W7] = "REGISTERING",  /* SID:7 */
    [LA_W8] = "RELAY",  /* SID:8 */
    [LA_W9] = "UNKNOWN",  /* SID:9 */
    [LA_S10] = "--- Connected ---",  /* SID:10 */
    [LA_S11] = "--- Disconnected ---",  /* SID:11 */
    [LA_S12] = "Auto-echo received messages back to sender",  /* SID:12 */
    [LA_S13] = "Debugger Name",  /* SID:13 */
    [LA_S14] = "Disable Host candidates (for testing)",  /* SID:14 */
    [LA_S15] = "Disable Prflx candidates (for testing)",  /* SID:15 */
    [LA_S16] = "Disable Relay candidates (for testing)",  /* SID:16 */
    [LA_S17] = "Disable Srflx candidates (for testing)",  /* SID:17 */
    [LA_S18] = "Enable DTLS (MbedTLS)",  /* SID:18 */
    [LA_S19] = "Enable DTLS (OpenSSL)",  /* SID:19 */
    [LA_S20] = "Enable PseudoTCP",  /* SID:20 */
    [LA_S21] = "GitHub Gist ID for Public Signaling",  /* SID:21 */
    [LA_S22] = "GitHub Token for Public Signaling",  /* SID:22 */
    [LA_S23] = "Log level (0-5)",  /* SID:23 */
    [LA_S24] = "Signaling server IP[:PORT]",  /* SID:24 */
    [LA_S25] = "Skip NAT type detection (RFC 3489 Test II/III)",  /* SID:25 */
    [LA_S26] = "STUN server address",  /* SID:26 */
    [LA_S27] = "Target Peer Name (if specified: active role)",  /* SID:27 */
    [LA_S28] = "TURN password",  /* SID:28 */
    [LA_S29] = "TURN server address",  /* SID:29 */
    [LA_S30] = "TURN username",  /* SID:30 */
    [LA_S31] = "Use Chinese language",  /* SID:31 */
    [LA_S32] = "Use COMPACT mode (UDP signaling, default is ICE/TCP)",  /* SID:32 */
    [LA_S33] = "Your Peer Name",  /* SID:33 */
    [LA_F34] = "% === P2P Ping Diagnostic Tool ===\n",  /* SID:34 */
    [LA_F35] = "% Debugger connected, resuming execution.\n",  /* SID:35 */
    [LA_F36] = "% Failed to create sessions\n",  /* SID:36 */
    [LA_F37] = "% Failed to initialize connection\n",  /* SID:37 */
    [LA_F38] = "% No signaling mode.\nUse --server or --github\n",  /* SID:38 */
    [LA_F39] = "Running in %s mode (connecting to %s)...",  /* SID:39 */
    [LA_F40] = "Running in %s mode (waiting for connection)...",  /* SID:40 */
    [LA_F41] = "% Timeout waiting for debugger. Continuing without debugger.\n",  /* SID:41 */
    [LA_F42] = "Waiting Debugger(%s) connecting...\n",  /* SID:42 */
    [LA_F43] = "% [Chat] Echo mode enabled: received messages will be echoed back.\n",  /* SID:43 */
    [LA_F44] = "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n",  /* SID:44 */
    [LA_F45] = "[EVENT] State: %s -> %s\n",  /* SID:45 */
    [LA_F46] = "[STATE] %s (%d) -> %s (%d)\n",  /* SID:46 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_ping_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
