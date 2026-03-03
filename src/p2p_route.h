
#ifndef P2P_ROUTE_H
#define P2P_ROUTE_H

#include <stdc.h>

///////////////////////////////////////////////////////////////////////////////

typedef struct {
    struct sockaddr_in* local_addrs;
    uint32_t*           local_masks;
    int                 addr_count;
    struct sockaddr_in  lan_peer_addr;   /* confirmed LAN addr */
    int                 lan_confirmed;
    uint64_t            probe_time;
} route_ctx_t;

void route_init(route_ctx_t *rt);
void route_final(route_ctx_t *rt);

//-----------------------------------------------------------------------------

/*
 * 检测获取本地所有有效的网络地址
 * 
 * @return         0=成功，!0=失败
 */
ret_t route_detect_local(route_ctx_t *rt);

/*
* 检查对方内网地址是否和自己处于同一个子网
* 
* @param rt        路由上下文，包含本地地址列表
* @param peer_priv 远端内网地址
* @return         true=同一子网，false=不同子网
*/
bool route_check_same_subnet(route_ctx_t *rt, const struct sockaddr_in *peer_priv);

//-----------------------------------------------------------------------------

/*
 * 直接向对方内网地址发送 ROUTE_PROBE 消息，用于确认自己可以和对方直接通讯，即处于同一个子网内
 * 
 * @param rt        路由上下文
 * @param sock      UDP 套接字
 * @param peer_priv 对方内网地址
 * @param local_port 本地端口（告知对方）
 * @return         0=成功，!0=失败
 */
ret_t route_send_probe(route_ctx_t *rt, sock_t sock, const struct sockaddr_in *peer_priv, uint16_t local_port);

//-----------------------------------------------------------------------------

/*
 * 处理对方直接发过来的 ROUTE_PROBE 消息，说明对方和自己处于同一个子网内
 */
ret_t route_on_probe(route_ctx_t *rt, const struct sockaddr_in *from, sock_t sock);

/*
 * 收到对方回复的 ROUTE_PROBE 应答消息，说明自己和对方处于同一个子网内
 */
ret_t route_on_probe_ack(route_ctx_t *rt, const struct sockaddr_in *from);

///////////////////////////////////////////////////////////////////////////////

#endif /* P2P_ROUTE_H */
