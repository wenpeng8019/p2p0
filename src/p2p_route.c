
/*
 * 路由检测
 */

#include "p2p_route.h"
#include "p2p_udp.h"
#include "p2p_internal.h"

#include <string.h>
#include <ifaddrs.h>
#include <net/if.h>

void route_init(route_ctx_t *rt) {
    memset(rt, 0, sizeof(*rt));
}

// 检测获取本地所有有效的网络地址
int route_detect_local(route_ctx_t *rt) {

    // 读取本地网络接口列表
    struct ifaddrs *ifa_list, *ifa;
    if (getifaddrs(&ifa_list) < 0) return -1;

    rt->addr_count = 0;

    // 遍历所有地网络接口
    for (ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
        if (rt->addr_count >= 8) break;
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;      // 协议无效
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;            // 跳过 localhost
        if (!(ifa->ifa_flags & IFF_UP)) continue;               // 接口未启动

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;

        rt->local_addrs[rt->addr_count] = *sa;
        rt->local_masks[rt->addr_count] = mask->sin_addr.s_addr;
        rt->addr_count++;
    }

    freeifaddrs(ifa_list);
    return rt->addr_count;
}

// 探测对方内网 IP 和自己是否属于同一个子网段内
int route_check_same_subnet(route_ctx_t *rt, const struct sockaddr_in *peer_priv) {

    uint32_t peer_ip = peer_priv->sin_addr.s_addr;

    for (int i = 0; i < rt->addr_count; i++) {
        uint32_t local_ip = rt->local_addrs[i].sin_addr.s_addr;
        uint32_t mask = rt->local_masks[i];
        if ((local_ip & mask) == (peer_ip & mask))
            return 1;
    }
    return 0;
}

// 直接向对方内网地址发送 ROUTE_PROBE 消息，用于确认自己可以和对方直接通讯，即处于同一个子网内
int route_send_probe(route_ctx_t *rt, int sock,
                     const struct sockaddr_in *peer_priv,
                     uint16_t local_port) {

    // Payload: [ local_port: u16 ] 对方可以通过该信息知道自己的端口
    uint8_t payload[2];
    payload[0] = (uint8_t)(local_port >> 8);
    payload[1] = (uint8_t)(local_port & 0xFF);

    rt->probe_time = time_ms();
    return udp_send_packet(sock, peer_priv, P2P_PKT_ROUTE_PROBE, 0, 0, payload, 2);
}

// 处理对方直接发过来的 ROUTE_PROBE 消息，说明对方和自己处于同一个子网内
int route_on_probe(route_ctx_t *rt, const struct sockaddr_in *from, int sock) { (void)rt;

    // ROUTE_PROBE 回复应答消息
    return udp_send_packet(sock, from, P2P_PKT_ROUTE_PROBE_ACK, 0, 0, NULL, 0);
}

// 收到对方回复的 ROUTE_PROBE 应答消息，说明自己和对方处于同一个子网内
int route_on_probe_ack(route_ctx_t *rt, const struct sockaddr_in *from) {
    rt->lan_peer_addr = *from;
    rt->lan_confirmed = 1;
    return 0;
}
