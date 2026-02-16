/*
 * p2p_server/protocol.h — Signaling protocol definitions
 */

#ifndef P2P_SERVER_PROTOCOL_H
#define P2P_SERVER_PROTOCOL_H

#include <stdint.h>

#define P2P_SIGNAL_MAGIC 0x50325030  /* "P2P0" */

typedef enum {
    MSG_LOGIN = 1,      /* 用户请求登录信令服务器：小一 -> Server: "我是小一" */
    MSG_LOGIN_ACK,      /* 服务器回复确认：Server -> 小一: "OK" */
    MSG_LIST,           /* 用户请求在线用户列表：小一 -> Server: "谁在线?" */
    MSG_LIST_RES,       /* 服务器返回在线用户列表：Server -> 小一: "除了你，小二、..." */
    MSG_CONNECT,        /* 用户向对方发起连接请求：小一 -> Server: "告诉小二我要连他，这是我的 SDP/ICE */
    MSG_SIGNAL,         /* 服务器转发连接请求给对方：Server -> 小二: "小一想要连你，这是他的 SDP/ICE */
    MSG_SIGNAL_ANS,     /* 对方向源方返回应答(answer)：小二 -> Server: "帮我回他 ok，顺带告诉他这是我 SDP/ICE" */
    MSG_SIGNAL_RELAY,   /* 服务器转发回复给源方：Server -> 小一: "这是小二让我转告你的他的 SDP/ICE" */
    MSG_HEARTBEAT       /* 用户心跳：小一 -> Server: 我还在哦 */
} p2p_msg_type_t;

#define P2P_MAX_NAME 32

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  type;
    uint32_t length;
} p2p_msg_hdr_t;

typedef struct {
    char name[P2P_MAX_NAME];
} p2p_msg_login_t;
#pragma pack(pop)

#endif /* P2P_SERVER_PROTOCOL_H */
