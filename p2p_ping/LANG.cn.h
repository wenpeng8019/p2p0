/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "CLOSED",  /* SID:1 */
    [LA_W3] = "CONNECTED",  /* SID:3 */
    [LA_W4] = "ERROR",  /* SID:4 */
    [LA_W5] = "INIT",  /* SID:5 */
    [LA_W6] = "PUNCHING",  /* SID:6 */
    [LA_W7] = "REGISTERING",  /* SID:7 */
    [LA_W8] = "RELAY",  /* SID:8 */
    [LA_W9] = "UNKNOWN",  /* SID:9 */
    [LA_S10] = "--- Connected ---",  /* SID:10 */
    [LA_S13] = "Auto-echo received messages back to sender",  /* SID:13 */
    [LA_S40] = "Debugger Name",  /* SID:40 */
    [LA_S50] = "[STATE] %s (%d) -> %s (%d)\n",  /* SID:50 */
    [LA_S51] = "Disable Relay candidates (for testing)",  /* SID:51 */
    [LA_S52] = "[EVENT] State: %s -> %s\n",  /* SID:52 */
    [LA_S15] = "Enable DTLS (MbedTLS)",  /* SID:15 */
    [LA_S16] = "Enable DTLS (OpenSSL)",  /* SID:16 */
    [LA_S17] = "Enable PseudoTCP",  /* SID:17 */
    [LA_S18] = "GitHub Gist ID for Public Signaling",  /* SID:18 */
    [LA_S19] = "GitHub Token for Public Signaling",  /* SID:19 */
    [LA_S20] = "Log level (0-5)",  /* SID:20 */
    [LA_S21] = "Signaling server IP[:PORT]",  /* SID:21 */
    [LA_S44] = "STUN server address",  /* SID:44 */
    [LA_S23] = "Target Peer Name (if specified: active role)",  /* SID:23 */
    [LA_S24] = "TURN password",  /* SID:24 */
    [LA_S25] = "TURN server address",  /* SID:25 */
    [LA_S26] = "TURN username",  /* SID:26 */
    [LA_S27] = "Use Chinese language",  /* SID:27 */
    [LA_S28] = "Use COMPACT mode (UDP signaling, default is ICE/TCP)",  /* SID:28 */
    [LA_S29] = "Your Peer Name",  /* SID:29 */
    [LA_F30] = "% === P2P Ping Diagnostic Tool ===\n",  /* SID:30 */
    [LA_F41] = "% Debugger connected, resuming execution.\n",  /* SID:41 */
    [LA_F31] = "% Failed to create sessions\n",  /* SID:31 */
    [LA_F32] = "% Failed to initialize connection\n",  /* SID:32 */
    [LA_F33] = "% No signaling mode.\nUse --server or --github\n",  /* SID:33 */
    [LA_F34] = "Running in %s mode (connecting to %s)...",  /* SID:34 */
    [LA_F35] = "Running in %s mode (waiting for connection)...",  /* SID:35 */
    [LA_F42] = "% Timeout waiting for debugger. Continuing without debugger.\n",  /* SID:42 */
    [LA_F43] = "Waiting Debugger(%s) connecting...\n",  /* SID:43 */
    [LA_F36] = "% [Chat] Echo mode enabled: received messages will be echoed back.\n",  /* SID:36 */
    [LA_F37] = "% [Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n",  /* SID:37 */
    [LA_F49] = "[EVENT] State: %s -> %s\n",  /* SID:49 new */
    [LA_F48] = "[STATE] %s (%d) -> %s (%d)\n",  /* SID:48 */
    [LA_S45] = "[STATE] %s (%d) -> %s (%d)\n",  /* SID:45 */
    [LA_S47] = "Disable Relay candidates (for testing)",  /* SID:47 */
    [LA_S46] = "[EVENT] State: %s -> %s\n",  /* SID:46 */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
