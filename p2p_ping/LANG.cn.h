/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "CLOSED",  /* SID:1 new */
    [LA_W2] = "CLOSING",  /* SID:2 new */
    [LA_W3] = "CONNECTED",  /* SID:3 new */
    [LA_W4] = "ERROR",  /* SID:4 new */
    [LA_W5] = "INIT",  /* SID:5 new */
    [LA_W6] = "PUNCHING",  /* SID:6 new */
    [LA_W7] = "REGISTERING",  /* SID:7 new */
    [LA_W8] = "RELAY",  /* SID:8 new */
    [LA_W9] = "UNKNOWN",  /* SID:9 new */
    [LA_S10] = "--- Connected ---",  /* SID:10 new */
    [LA_S11] = "--- Peer disconnected ---",  /* SID:11 new */
    [LA_S12] = "[EVENT] Connection closed",  /* SID:12 new */
    [LA_S13] = "Auto-echo received messages back to sender",  /* SID:13 new */
    [LA_S14] = "Disable LAN shortcut (force NAT punch test)",  /* SID:14 new */
    [LA_S15] = "Enable DTLS (MbedTLS)",  /* SID:15 new */
    [LA_S16] = "Enable DTLS (OpenSSL)",  /* SID:16 new */
    [LA_S17] = "Enable PseudoTCP",  /* SID:17 new */
    [LA_S18] = "GitHub Gist ID for Public Signaling",  /* SID:18 new */
    [LA_S19] = "GitHub Token for Public Signaling",  /* SID:19 new */
    [LA_S20] = "Log level (0-5)",  /* SID:20 new */
    [LA_S21] = "Signaling server IP[:PORT]",  /* SID:21 new */
    [LA_S22] = "Skip host candidates",  /* SID:22 new */
    [LA_S23] = "Target Peer Name (if specified: active role)",  /* SID:23 new */
    [LA_S24] = "TURN password",  /* SID:24 new */
    [LA_S25] = "TURN server address",  /* SID:25 new */
    [LA_S26] = "TURN username",  /* SID:26 new */
    [LA_S27] = "Use Chinese language",  /* SID:27 new */
    [LA_S28] = "Use COMPACT mode (UDP signaling, default is ICE/TCP)",  /* SID:28 new */
    [LA_S29] = "Your Peer Name",  /* SID:29 new */
    [LA_F30] = "% === P2P Ping Diagnostic Tool ===\n",  /* SID:30 new */
    [LA_F31] = "% Failed to create sessions\n",  /* SID:31 new */
    [LA_F32] = "% Failed to initialize connection\n",  /* SID:32 new */
    [LA_F33] = "% No signaling mode.\nUse --server or --github\n",  /* SID:33 new */
    [LA_F34] = "Running in %s mode (connecting to %s)...",  /* SID:34 new */
    [LA_F35] = "Running in %s mode (waiting for connection)...",  /* SID:35 new */
    [LA_F36] = "% [Chat] Echo mode enabled: received messages will be echoed back.\n",  /* SID:36 new */
    [LA_F37] = "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n",  /* SID:37 new */
    [LA_F38] = "[STATE] %s (%d) -> %s (%d)",  /* SID:38 new */
    [LA_F39] = "% [TEST] LAN shortcut disabled - forcing NAT punch\n",  /* SID:39 new */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
