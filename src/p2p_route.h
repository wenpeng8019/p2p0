
#ifndef P2P_ROUTE_H
#define P2P_ROUTE_H

#include "predefine.h"

///////////////////////////////////////////////////////////////////////////////

typedef struct {
    struct sockaddr_in* local_addrs;
    uint32_t*           local_masks;
    int                 addr_count;
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

///////////////////////////////////////////////////////////////////////////////

#endif /* P2P_ROUTE_H */
