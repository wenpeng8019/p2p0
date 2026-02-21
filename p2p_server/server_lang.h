/*
 * p2p_server 多语言支持
 */
#ifndef SERVER_LANG_H
#define SERVER_LANG_H

#include <p2p.h>  /* 使用核心库的 p2p_language_t */

/* 消息 ID */
typedef enum {
    MSG_SERVER_USAGE = 0,        /* 用法标题 */
    MSG_SERVER_PARAMS,           /* 参数标题 */
    MSG_SERVER_PARAM_PORT,       /* 端口参数说明 */
    MSG_SERVER_PARAM_PORT_TCP,   /* TCP说明 */
    MSG_SERVER_PARAM_PORT_UDP,   /* UDP说明 */
    MSG_SERVER_PARAM_PROBE,      /* 探测端口参数说明 */
    MSG_SERVER_PARAM_PROBE_DESC, /* 探测端口描述 */
    MSG_SERVER_PARAM_RELAY,      /* relay参数说明 */
    MSG_SERVER_EXAMPLES,         /* 示例标题 */
    MSG_SERVER_EXAMPLE_DEFAULT,  /* 默认配置示例 */
    MSG_SERVER_EXAMPLE_PORT,     /* 指定端口示例 */
    MSG_SERVER_EXAMPLE_PROBE,    /* 带探测端口示例 */
    MSG_SERVER_EXAMPLE_RELAY,    /* 完整配置示例 */
    MSG_SERVER_ERR_INVALID_PORT, /* 无效端口错误 */
    MSG_SERVER_ERR_INVALID_PROBE,/* 无效探测端口错误 */
    MSG_SERVER_ERR_UNKNOWN_OPT,  /* 未知选项错误 */
    MSG_SERVER_ERR_TOO_MANY,     /* 参数过多错误 */
    MSG_SERVER_STARTING,         /* 正在启动 */
    MSG_SERVER_NAT_PROBE,        /* NAT探测 */
    MSG_SERVER_RELAY_SUPPORT,    /* 中继支持 */
    MSG_SERVER_ENABLED,          /* 已启用 */
    MSG_SERVER_DISABLED,         /* 已禁用 */
    MSG_SERVER_PROBE_BIND_FAILED,/* 探测端口绑定失败 */
    MSG_SERVER_PROBE_LISTENING,  /* 探测端口监听中 */
    MSG_SERVER_LISTENING,        /* 服务器监听中 */
    MSG_SERVER_SHUTDOWN_SIGNAL,  /* 收到关闭信号 */
    MSG_SERVER_SHUTTING_DOWN,    /* 正在关闭 */
    MSG_SERVER_GOODBYE,          /* 再见 */
    
    /* TCP/RELAY 日志消息 */
    MSG_TCP_PEER_DISCONNECTED,   /* 客户端断开 */
    MSG_TCP_INVALID_MAGIC,       /* 无效magic */
    MSG_TCP_PEER_LOGIN,          /* 客户端登录 */
    MSG_TCP_MERGED_PENDING,      /* 合并离线缓存 */
    MSG_TCP_FLUSHING_PENDING,    /* 转发缓存候选 */
    MSG_TCP_FORWARDED_OFFER,     /* 已转发OFFER */
    MSG_TCP_PENDING_FLUSHED,     /* 缓存已清空 */
    MSG_TCP_STORAGE_FULL_FLUSH,  /* 缓存满-转发意图 */
    MSG_TCP_SENT_EMPTY_OFFER,    /* 已发送空OFFER */
    MSG_TCP_STORAGE_FULL_FLUSHED,/* 缓存满标识已清空 */
    MSG_TCP_RECV_TARGET_FAILED,  /* 接收目标名失败 */
    MSG_TCP_PAYLOAD_TOO_LARGE,   /* 负载过大 */
    MSG_TCP_RECV_PAYLOAD_FAILED, /* 接收负载失败 */
    MSG_TCP_RELAYING,            /* 转发信令 */
    MSG_TCP_SENT_WITH_CANDS,     /* 已发送(含候选) */
    MSG_TCP_TARGET_OFFLINE,      /* 目标离线 */
    MSG_TCP_NEW_SENDER_REPLACE,  /* 新发送者替换旧缓存 */
    MSG_TCP_STORAGE_FULL_DROP,   /* 缓存满-丢弃 */
    MSG_TCP_STORAGE_INTENT_NOTED,/* 缓存满-记录意图 */
    MSG_TCP_CACHED_FULL,         /* 已缓存-缓存满 */
    MSG_TCP_CACHED_PARTIAL,      /* 已缓存-部分 */
    MSG_TCP_CANNOT_ALLOC_SLOT,   /* 无法分配槽位 */
    MSG_TCP_SEND_ACK_FAILED,     /* 发送ACK失败 */
    MSG_TCP_SENT_CONNECT_ACK,    /* 已发送CONNECT_ACK */
    MSG_TCP_LIST_TRUNCATED,      /* 用户列表截断 */
    MSG_TCP_UNKNOWN_MSG_TYPE,    /* 未知消息类型 */
    MSG_TCP_CLIENT_TIMEOUT,      /* 客户端超时 */
    MSG_TCP_NEW_CONNECTION,      /* 新连接 */
    MSG_TCP_MAX_PEERS,           /* 达到最大连接数 */
    
    /* UDP/COMPACT 日志消息 */
    MSG_UDP_REGISTER,            /* REGISTER消息 */
    MSG_UDP_REGISTER_INVALID,    /* 无效的REGISTER消息 */
    MSG_UDP_CANDIDATE_INFO,      /* 候选信息 */
    MSG_UDP_REGISTER_ACK_ERROR,  /* REGISTER_ACK错误 */
    MSG_UDP_REGISTER_ACK_OK,     /* REGISTER_ACK成功 */
    MSG_UDP_SENT_PEER_INFO,      /* 已发送PEER_INFO */
    MSG_UDP_SENT_PEER_INFO_ADDR, /* 已发送PEER_INFO(含地址) */
    MSG_UDP_TARGET_NOT_FOUND,    /* 目标未找到 */
    MSG_UDP_UNREGISTER,          /* UNREGISTER */
    MSG_UDP_UNREGISTER_INVALID,  /* 无效的UNREGISTER消息 */
    MSG_UDP_PAIR_TIMEOUT,        /* 配对超时 */
    MSG_UDP_UNKNOWN_SIG,         /* 未知信令类型 */
    
    /* DEBUG 和 PROBE 日志 */
    MSG_DEBUG_RECEIVED_BYTES,    /* 接收字节(调试) */
    MSG_PROBE_ACK,               /* NAT探测响应 */

    /* UDP/COMPACT 新增日志消息 */
    MSG_UDP_PEER_INFO_RETRANSMIT,      /* PEER_INFO 重传 */
    MSG_UDP_PEER_INFO_RETRANSMIT_FAIL, /* PEER_INFO 重传超时放弃 */
    MSG_UDP_SESSION_ASSIGNED,          /* 分配 session_id */
    MSG_UDP_PEER_OFF_SENT,             /* 发送 PEER_OFF 通知 */
    MSG_UDP_PEER_INFO_ACK_INVALID,     /* 无效 PEER_INFO_ACK */
    MSG_UDP_PEER_INFO_ACK_CONFIRMED,   /* 确认 PEER_INFO_ACK seq=0 */
    MSG_UDP_PEER_INFO_ACK_UNKNOWN,     /* 未知 session_id 的 ACK */
    MSG_UDP_PEER_INFO_ACK_RELAYED,     /* 中继 PEER_INFO_ACK */
    MSG_UDP_PEER_INFO_ACK_RELAY_FAIL,  /* 无法中继 ACK */
    MSG_UDP_RELAY_INVALID_SRC,         /* PEER_INFO seq=0 来自客户端（非法） */
    MSG_UDP_RELAY_PKT_INVALID,         /* relay 包过短 */
    MSG_UDP_RELAY_UNKNOWN_SESSION,     /* 未知 session_id 的 relay 包 */
    MSG_UDP_RELAY_NO_PEER,             /* relay session 对端不可用 */
    MSG_UDP_RELAY_PEER_INFO,           /* 中继 PEER_INFO */
    MSG_UDP_RELAY_DATA,                /* 中继 DATA */
    MSG_UDP_RELAY_ACK,                 /* 中继 ACK */

    /* 平台/初始化错误消息 */
    MSG_SERVER_WIN_CTRL_HANDLER_ERR,   /* Windows: SetConsoleCtrlHandler 失败 */
    MSG_SERVER_URANDOM_WARN,           /* Linux: /dev/urandom 打开失败 */
    MSG_SERVER_WINSOCK_ERR,            /* Windows: WSAStartup 失败 */

    MSG_SERVER_COUNT             /* 消息总数 */
} server_msg_id_t;

/* 设置当前语言 */
void server_set_language(p2p_language_t lang);

/* 获取消息字符串 */
const char* server_msg(server_msg_id_t msg_id);

#endif /* SERVER_LANG_H */
