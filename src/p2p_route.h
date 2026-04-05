
#ifndef P2P_ROUTE_H
#define P2P_ROUTE_H

#include "predefine.h"

///////////////////////////////////////////////////////////////////////////////

typedef struct {
    struct sockaddr_in* local_addrs;
    uint32_t*           local_masks;
    int                 addr_count;
} route_ctx_t;

/*
 * 全局共享路由模块生命周期（进程内共享）
 *
 * - route_shared_acquire: 引用+1；首次调用时执行本地网卡探测
 * - route_shared_release: 引用-1；归零时释放缓存
 * - route_shared_get: 获取只读共享上下文（未 acquire 时返回 NULL）
 */
ret_t route_shared_acquire(void);
void route_shared_release(void);
const route_ctx_t *route_shared_get(void);

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
