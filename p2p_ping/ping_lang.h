/*
 * p2p_ping 多语言支持
 */
#ifndef PING_LANG_H
#define PING_LANG_H

#include <p2p.h>  /* 使用核心库的 p2p_language_t */

/* 消息 ID */
typedef enum {
    MSG_PING_TITLE = 0,          /* 工具标题 */
    MSG_PING_USAGE,              /* 使用说明 */
    MSG_PING_OPTIONS,            /* 选项标题 */
    MSG_PING_OPT_DTLS,           /* --dtls 选项 */
    MSG_PING_OPT_OPENSSL,        /* --openssl 选项 */
    MSG_PING_OPT_PSEUDO,         /* --pseudo 选项 */
    MSG_PING_OPT_SERVER,         /* --server 选项 */
    MSG_PING_OPT_COMPACT,        /* --compact 选项 */
    MSG_PING_OPT_GITHUB,         /* --github 选项 */
    MSG_PING_OPT_GIST,           /* --gist 选项 */
    MSG_PING_OPT_NAME,           /* --name 选项 */
    MSG_PING_OPT_TO,             /* --to 选项 */
    MSG_PING_OPT_DISABLE_LAN,    /* --disable-lan 选项 */
    MSG_PING_OPT_VERBOSE_PUNCH,  /* --verbose-punch 选项 */
    MSG_PING_OPT_CN,             /* --cn 选项 */
    MSG_PING_OPT_ECHO,           /* --echo 选项 */
    MSG_PING_STATE_CHANGE,       /* 状态变更 */
    MSG_PING_CONNECTED,          /* 连接建立 */
    MSG_PING_DISCONNECTED,       /* 连接断开 */
    MSG_PING_SENT,               /* 发送 PING */
    MSG_PING_RECEIVED,           /* 接收数据 */
    MSG_PING_CREATE_FAIL,        /* 创建会话失败 */
    MSG_PING_NO_MODE,            /* 未指定连接模式 */
    MSG_PING_USE_ONE_OF,         /* 使用以下之一 */
    MSG_PING_CONNECT_FAIL,       /* 连接初始化失败 */
    MSG_PING_MODE_CONNECTING,    /* 连接模式（主动）*/
    MSG_PING_MODE_WAITING,       /* 等待模式（被动）*/
    MSG_PING_LAN_DISABLED,       /* LAN 快捷方式已禁用 */
    MSG_PING_VERBOSE_ENABLED,    /* 详细日志已启用 */
    MSG_PING_CHAT_ENTER,         /* 进入聊天模式 */
    MSG_PING_CHAT_CONNECTED,     /* 聊天模式连接提示 */
    MSG_PING_CHAT_DISCONNECT,    /* 聊天中对端断开提示 */
    MSG_PING_CHAT_ECHO_ON,       /* echo 模式已启用 */
    MSG_PING_COUNT               /* 消息总数 */
} ping_msg_id_t;

/* 设置当前语言 */
void ping_set_language(p2p_language_t lang);

/* 获取当前语言 */
p2p_language_t ping_get_language(void);

/* 获取指定消息的文本 */
const char* ping_msg(ping_msg_id_t id);

#endif /* PING_LANG_H */
