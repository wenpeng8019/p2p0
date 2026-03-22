/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "已关闭",  /* SID:1 */
    [LA_W3] = "已连接",  /* SID:3 */
    [LA_W4] = "错误",  /* SID:4 */
    [LA_W5] = "初始化",  /* SID:5 */
    [LA_W6] = "打洞中",  /* SID:6 */
    [LA_W7] = "注册中",  /* SID:7 */
    [LA_W8] = "中继",  /* SID:8 */
    [LA_W9] = "未知",  /* SID:9 */
    [LA_S10] = "--- 已连接 ---",  /* SID:10 */
    [LA_S13] = "自动回显收到的消息给发送方",  /* SID:13 */
    [LA_S40] = "调试器名称",  /* SID:40 */
    [LA_S15] = "启用 DTLS (MbedTLS)",  /* SID:15 */
    [LA_S16] = "启用 DTLS (OpenSSL)",  /* SID:16 */
    [LA_S17] = "启用 PseudoTCP",  /* SID:17 */
    [LA_S18] = "GitHub Gist ID（公网信令）",  /* SID:18 */
    [LA_S19] = "GitHub Token（公网信令）",  /* SID:19 */
    [LA_S20] = "日志级别 (0-5)",  /* SID:20 */
    [LA_S21] = "信令服务器 IP[:端口]",  /* SID:21 */
    [LA_S44] = "STUN 服务器地址",  /* SID:44 */
    [LA_S23] = "目标节点名称（指定则为主动连接方）",  /* SID:23 */
    [LA_S24] = "TURN 密码",  /* SID:24 */
    [LA_S25] = "TURN 服务器地址",  /* SID:25 */
    [LA_S26] = "TURN 用户名",  /* SID:26 */
    [LA_S27] = "使用中文界面",  /* SID:27 */
    [LA_S28] = "使用 COMPACT 模式（UDP 信令，默认为 ICE/TCP）",  /* SID:28 */
    [LA_S29] = "本端节点名称",  /* SID:29 */
    [LA_F30] = "% === P2P Ping 诊断工具 ===\n",  /* SID:30 */
    [LA_F41] = "% 调试器已连接，继续执行。\n",  /* SID:41 */
    [LA_F31] = "% 创建会话失败\n",  /* SID:31 */
    [LA_F32] = "% 初始化连接失败\n",  /* SID:32 */
    [LA_F33] = "% 未指定信令模式。\n请使用 --server 或 --github\n",  /* SID:33 */
    [LA_F34] = "以 %s 模式运行（连接到 %s）...",  /* SID:34 */
    [LA_F35] = "以 %s 模式运行（等待连接）...",  /* SID:35 */
    [LA_F42] = "% 等待调试器超时，继续执行。\n",  /* SID:42 */
    [LA_F43] = "等待调试器(%s)连接中...\n",  /* SID:43 */
    [LA_F36] = "% [聊天] 回显模式已启用：收到的消息将自动回复。\n",  /* SID:36 */
    [LA_F37] = "% [聊天] 进入消息模式。输入文字后按回车发送，Ctrl+C 退出。\n",  /* SID:37 */
    [LA_F46] = "[EVENT] State: %s -> %s\n",  /* SID:46 new */
    [LA_F45] = "[STATE] %s (%d) -> %s (%d)\n",  /* SID:45 new */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
