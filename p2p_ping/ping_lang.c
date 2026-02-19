/*
 * p2p_ping 多语言实现
 */
#include "ping_lang.h"
#include <stddef.h>

/* 当前语言 */
static p2p_language_t current_language = P2P_LANG_EN;

/* 英文词表 */
static const char* messages_en[MSG_PING_COUNT] = {
    [MSG_PING_TITLE]            = "=== P2P Ping Diagnostic Tool ===",
    [MSG_PING_USAGE]            = "Usage: %s [options]",
    [MSG_PING_OPTIONS]          = "Options:",
    [MSG_PING_OPT_DTLS]         = "  --dtls            Enable DTLS (MbedTLS)",
    [MSG_PING_OPT_OPENSSL]      = "  --openssl         Enable DTLS (OpenSSL)",
    [MSG_PING_OPT_PSEUDO]       = "  --pseudo          Enable PseudoTCP",
    [MSG_PING_OPT_SERVER]       = "  --server IP       Standard Signaling Server IP",
    [MSG_PING_OPT_COMPACT]      = "  --compact         Use COMPACT mode (UDP signaling, default is ICE/TCP)",
    [MSG_PING_OPT_GITHUB]       = "  --github TOKEN    GitHub Token for Public Signaling",
    [MSG_PING_OPT_GIST]         = "  --gist ID         GitHub Gist ID for Public Signaling",
    [MSG_PING_OPT_NAME]         = "  --name NAME       Your Peer Name",
    [MSG_PING_OPT_TO]           = "  --to TARGET       Target Peer Name (if specified: active role; if omitted: passive role)",
    [MSG_PING_OPT_DISABLE_LAN]  = "  --disable-lan     Disable LAN shortcut (force NAT punch test)",
    [MSG_PING_OPT_VERBOSE_PUNCH]= "  --verbose-punch   Enable verbose NAT punch logging",
    [MSG_PING_OPT_CN]           = "  --cn              Use Chinese language",
    [MSG_PING_STATE_CHANGE]     = "[STATE] %s (%d) -> %s (%d)",
    [MSG_PING_CONNECTED]        = "[EVENT] Connection established!",
    [MSG_PING_DISCONNECTED]     = "[EVENT] Connection closed",
    [MSG_PING_SENT]             = "[DATA] Sent PING",
    [MSG_PING_RECEIVED]         = "[DATA] Received: %s",
    [MSG_PING_CREATE_FAIL]      = "Failed to create session",
    [MSG_PING_NO_MODE]          = "Error: No connection mode specified.",
    [MSG_PING_USE_ONE_OF]       = "Use one of: --server or --github",
    [MSG_PING_CONNECT_FAIL]     = "Failed to initialize connection",
    [MSG_PING_MODE_CONNECTING]  = "Running in %s mode (connecting to %s)...",
    [MSG_PING_MODE_WAITING]     = "Running in %s mode (waiting for connection)...",
    [MSG_PING_LAN_DISABLED]     = "[TEST] LAN shortcut disabled - forcing NAT punch",
    [MSG_PING_VERBOSE_ENABLED]  = "[TEST] Verbose NAT punch logging enabled",
};

/* 中文词表 */
static const char* messages_zh[MSG_PING_COUNT] = {
    [MSG_PING_TITLE]            = "=== P2P Ping 诊断工具 ===",
    [MSG_PING_USAGE]            = "用法: %s [选项]",
    [MSG_PING_OPTIONS]          = "选项:",
    [MSG_PING_OPT_DTLS]         = "  --dtls            启用 DTLS (MbedTLS)",
    [MSG_PING_OPT_OPENSSL]      = "  --openssl         启用 DTLS (OpenSSL)",
    [MSG_PING_OPT_PSEUDO]       = "  --pseudo          启用 PseudoTCP",
    [MSG_PING_OPT_SERVER]       = "  --server IP       标准信令服务器 IP",
    [MSG_PING_OPT_COMPACT]      = "  --compact         使用 COMPACT 模式 (UDP 信令，默认为 ICE/TCP)",
    [MSG_PING_OPT_GITHUB]       = "  --github TOKEN    用于公共信令的 GitHub Token",
    [MSG_PING_OPT_GIST]         = "  --gist ID         用于公共信令的 GitHub Gist ID",
    [MSG_PING_OPT_NAME]         = "  --name NAME       你的节点名称",
    [MSG_PING_OPT_TO]           = "  --to TARGET       目标节点名称（指定时为主动角色；省略时为被动角色）",
    [MSG_PING_OPT_DISABLE_LAN]  = "  --disable-lan     禁用内网快捷方式（强制 NAT 打洞测试）",
    [MSG_PING_OPT_VERBOSE_PUNCH]= "  --verbose-punch   启用详细 NAT 打洞日志",
    [MSG_PING_OPT_CN]           = "  --cn              使用中文语言",
    [MSG_PING_STATE_CHANGE]     = "[状态] %s (%d) -> %s (%d)",
    [MSG_PING_CONNECTED]        = "[事件] 连接已建立！",
    [MSG_PING_DISCONNECTED]     = "[事件] 连接已关闭",
    [MSG_PING_SENT]             = "[数据] 已发送 PING",
    [MSG_PING_RECEIVED]         = "[数据] 收到: %s",
    [MSG_PING_CREATE_FAIL]      = "创建会话失败",
    [MSG_PING_NO_MODE]          = "错误: 未指定连接模式。",
    [MSG_PING_USE_ONE_OF]       = "请使用以下之一: --server 或 --github",
    [MSG_PING_CONNECT_FAIL]     = "连接初始化失败",
    [MSG_PING_MODE_CONNECTING]  = "运行在 %s 模式（连接到 %s）...",
    [MSG_PING_MODE_WAITING]     = "运行在 %s 模式（等待连接）...",
    [MSG_PING_LAN_DISABLED]     = "[测试] 内网快捷方式已禁用 - 强制 NAT 打洞",
    [MSG_PING_VERBOSE_ENABLED]  = "[测试] 详细 NAT 打洞日志已启用",
};

/* 设置当前语言 */
void ping_set_language(p2p_language_t lang) {
    if (lang == P2P_LANG_EN || lang == P2P_LANG_ZH) {
        current_language = lang;
    }
}

/* 获取当前语言 */
p2p_language_t ping_get_language(void) {
    return current_language;
}

/* 获取指定消息的文本 */
const char* ping_msg(ping_msg_id_t id) {
    if (id >= MSG_PING_COUNT) {
        return "Invalid message ID";
    }
    
    if (current_language == P2P_LANG_ZH) {
        return messages_zh[id];
    }
    return messages_en[id];
}
