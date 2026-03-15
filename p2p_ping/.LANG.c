/*
 * Auto-generated language strings
 */

#include "LANG.h"

int LA_ping;

/* 字符串表 */
static const char* s_lang_en[LA_NUM] = {
    [LA_W1] = "CLOSED",  /* SID:1 */
    [LA_W2] = "CLOSING",  /* SID:2 */
    [LA_W3] = "CONNECTED",  /* SID:3 */
    [LA_W4] = "ERROR",  /* SID:4 */
    [LA_W5] = "INIT",  /* SID:5 */
    [LA_W6] = "PUNCHING",  /* SID:6 */
    [LA_W7] = "REGISTERING",  /* SID:7 */
    [LA_W8] = "RELAY",  /* SID:8 */
    [LA_W9] = "UNKNOWN",  /* SID:9 */
    [LA_S10] = "--- Connected ---",  /* SID:10 */
    [LA_S11] = "--- Peer disconnected ---",  /* SID:11 */
    [LA_S12] = "[EVENT] Connection closed",  /* SID:12 */
    [LA_S13] = "Auto-echo received messages back to sender",  /* SID:13 */
    [LA_S14] = "Disable LAN shortcut (force NAT punch test)",  /* SID:14 */
    [LA_S15] = "Enable DTLS (MbedTLS)",  /* SID:15 */
    [LA_S16] = "Enable DTLS (OpenSSL)",  /* SID:16 */
    [LA_S17] = "Enable PseudoTCP",  /* SID:17 */
    [LA_S18] = "GitHub Gist ID for Public Signaling",  /* SID:18 */
    [LA_S19] = "GitHub Token for Public Signaling",  /* SID:19 */
    [LA_S20] = "Log level (0-5)",  /* SID:20 */
    [LA_S21] = "Signaling server IP[:PORT]",  /* SID:21 */
    [LA_S22] = "Skip host candidates",  /* SID:22 */
    [LA_S23] = "Target Peer Name (if specified: active role)",  /* SID:23 */
    [LA_S24] = "TURN password",  /* SID:24 */
    [LA_S25] = "TURN server address",  /* SID:25 */
    [LA_S26] = "TURN username",  /* SID:26 */
    [LA_S27] = "Use Chinese language",  /* SID:27 */
    [LA_S28] = "Use COMPACT mode (UDP signaling, default is ICE/TCP)",  /* SID:28 */
    [LA_S29] = "Your Peer Name",  /* SID:29 */
    [LA_F30] = "% === P2P Ping Diagnostic Tool ===\n",  /* SID:30 */
    [LA_F31] = "% Failed to create sessions\n",  /* SID:31 */
    [LA_F32] = "% Failed to initialize connection\n",  /* SID:32 */
    [LA_F33] = "% No signaling mode.\nUse --server or --github\n",  /* SID:33 */
    [LA_F34] = "Running in %s mode (connecting to %s)...",  /* SID:34 */
    [LA_F35] = "Running in %s mode (waiting for connection)...",  /* SID:35 */
    [LA_F36] = "% [Chat] Echo mode enabled: received messages will be echoed back.\n",  /* SID:36 */
    [LA_F37] = "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n",  /* SID:37 */
    [LA_F38] = "[STATE] %s (%d) -> %s (%d)",  /* SID:38 */
    [LA_F39] = "% [TEST] LAN shortcut disabled - forcing NAT punch\n",  /* SID:39 */
};

/* 语言初始化函数（自动生成，请勿修改）*/
void LA_ping_init(void) {
    LA_RID = lang_def(s_lang_en, sizeof(s_lang_en) / sizeof(s_lang_en[0]), LA_FMT_START);
}
