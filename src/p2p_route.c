
/*
 * 路由检测
 */

#define MOD_TAG "ROUTE"

#include "p2p_internal.h"
#ifdef _WIN32
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <iphlpapi.h>
#   pragma comment(lib, "iphlpapi.lib")
#else
#   include <ifaddrs.h>
#   include <net/if.h>
#endif

/* 子网掩码前缀长度计算：GCC/Clang 使用内建指令，其他编译器使用位运算 */
#if defined(__GNUC__) || defined(__clang__)
#  define mask_to_prefix(mask_net) __builtin_popcount(ntohl(mask_net))
#else
static int mask_to_prefix(uint32_t mask_net) {
    uint32_t m = ntohl(mask_net);
    int n = 0;
    while (m & 0x80000000u) { n++; m <<= 1; }
    return n;
}
#endif

void route_init(route_ctx_t *rt) {
    memset(rt, 0, sizeof(*rt));
}

void route_final(route_ctx_t *rt) {
    if (rt->local_addrs) free(rt->local_addrs);
}

// 检测获取本地所有有效的网络地址
ret_t route_detect_local(route_ctx_t *rt) {

    printf("%s", LA_S("Detecting local network addresses", LA_S19, 168));

    rt->addr_count = 0;

#ifdef _WIN32
    /* Windows: 使用 GetAdaptersAddresses 枚举 IPv4 地址 */
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddrs = NULL;
    DWORD ret;

    do {
        pAddrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!pAddrs) return -1;
        ret = GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER,
                NULL, pAddrs, &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) { free(pAddrs); pAddrs = NULL; }
    } while (ret == ERROR_BUFFER_OVERFLOW);

    if (ret != NO_ERROR) { free(pAddrs); return -1; }

    for (PIP_ADAPTER_ADDRESSES a = pAddrs; a != NULL; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;                                              /* 接口未启动 */
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;                                       /* 跳过 localhost */
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
            if (((struct sockaddr_in *)ua->Address.lpSockaddr)->sin_family != AF_INET) continue;    /* 协议无效 */
            ++rt->addr_count;
        }
    }

    rt->local_addrs = malloc((sizeof(struct sockaddr_in) + sizeof(uint32_t)) * rt->addr_count);
    if (!rt->local_addrs) { free(pAddrs); return -1; }
    rt->local_masks = (uint32_t *)(rt->local_addrs + rt->addr_count);

    int i=0;
    for (PIP_ADAPTER_ADDRESSES a = pAddrs; a != NULL; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
            if (((struct sockaddr_in *)ua->Address.lpSockaddr)->sin_family != AF_INET) continue;

            rt->local_addrs[i] = *(struct sockaddr_in *)ua->Address.lpSockaddr;
            rt->local_masks[i++] = ua->OnLinkPrefixLength ? htonl(~((1u << (32 - ua->OnLinkPrefixLength)) - 1)) : 0;    // 计算子网掩码
        }
    }

    free(pAddrs);
#else
    /* POSIX: 使用 getifaddrs */
    struct ifaddrs *ifa_list, *ifa;
    if (getifaddrs(&ifa_list) < 0) return -1;

    for (ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;               /* 接口未启动 */
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;            /* 跳过 localhost */
        if (ifa->ifa_addr->sa_family != AF_INET) continue;      /* 协议无效 */
        ++rt->addr_count;
    }

    rt->local_addrs = malloc((sizeof(struct sockaddr_in) + sizeof(uint32_t)) * rt->addr_count);
    if (!rt->local_addrs) return -1;
    rt->local_masks = (uint32_t *)(rt->local_addrs + rt->addr_count);

    int i=0;
    for (ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        rt->local_addrs[i] = *(struct sockaddr_in *)ifa->ifa_addr;
        rt->local_masks[i++] = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;   // 计算子网掩码
    }

    freeifaddrs(ifa_list);
#endif

    for (i = 0; i < rt->addr_count; i++) {
        printf(LA_F("  [%d] %s/%d", LA_F1, 257), i, inet_ntoa(rt->local_addrs[i].sin_addr), mask_to_prefix(rt->local_masks[i]));
    }
    print("I:", LA_F("%s: %d %s", LA_F49, 309), LA_W("Local address detection done", LA_W42, 53), rt->addr_count, LA_W("address(es)", LA_W5, 7));
    return rt->addr_count;
}

// 探测对方内网 IP 和自己是否属于同一个子网段内
bool route_check_same_subnet(route_ctx_t *rt, const struct sockaddr_in *peer_priv) {

    uint32_t peer_ip = peer_priv->sin_addr.s_addr;

    for (int i = 0; i < rt->addr_count; i++) {
        uint32_t local_ip = rt->local_addrs[i].sin_addr.s_addr;
        uint32_t mask = rt->local_masks[i];
        if ((local_ip & mask) == (peer_ip & mask)) {
            print("I:", LA_F("%s %s %s %s", LA_F10, 267), LA_W("Peer is on the same subnet as", LA_W62, 74),
                         inet_ntoa(peer_priv->sin_addr), LA_S("via local", LA_S90, 213),
                         inet_ntoa(rt->local_addrs[i].sin_addr));
            return true;
        }
    }
    printf(LA_F("%s: %s", LA_F51, 311), LA_W("Peer is on a different subnet", LA_W61, 73), inet_ntoa(peer_priv->sin_addr));
    return false;
}

// 直接向对方内网地址发送 ROUTE_PROBE 消息，用于确认自己可以和对方直接通讯，即处于同一个子网内
int route_send_probe(route_ctx_t *rt, sock_t sock,
                     const struct sockaddr_in *peer_priv,
                     uint16_t local_port) {

    // Payload: [ local_port: u16 ] 对方可以通过该信息知道自己的端口
    uint8_t payload[2];
    hstonb(local_port, payload);

    print("I:", LA_F("%s %s:%d", LA_F19, 276), LA_W("Sent route probe to", LA_W93, 118),
                 inet_ntoa(peer_priv->sin_addr), ntohs(peer_priv->sin_port));
    P_clock _clk; P_clock_now(&_clk);
    rt->probe_time = clock_ms(_clk);
    return udp_send_packet(sock, peer_priv, P2P_PKT_ROUTE_PROBE, 0, 0, payload, 2);
}

/*
 * 协议：P2P_PKT_ROUTE_PROBE (0x30)
 * 包头: [type=0x30 | flags=0 | seq=0]
 * 负载: [probe_seq(2B)] (用于匹配响应)
 * 
 * 处理对方直接发过来的 ROUTE_PROBE 消息，说明对方和自己处于同一个子网内
 */
int route_on_probe(route_ctx_t *rt, const struct sockaddr_in *from, sock_t sock) { (void)rt;

    printf(LA_F("Received ROUTE_PROBE pkt from %s:%d, len=0", LA_F123, 146),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    print("I:", LA_F("%s %s:%d, %s", LA_F21, 279), LA_W("Received route probe from", LA_W75, 93),
                 inet_ntoa(from->sin_addr), ntohs(from->sin_port),
                 LA_S("sending ACK", LA_S74, 203));

    // ROUTE_PROBE 回复应答消息
    return udp_send_packet(sock, from, P2P_PKT_ROUTE_PROBE_ACK, 0, 0, NULL, 0);
}

/*
 * 协议：P2P_PKT_ROUTE_PROBE_ACK (0x31)
 * 包头: [type=0x31 | flags=0 | seq=0]
 * 负载: 无
 * 
 * 收到对方回复的 ROUTE_PROBE 应答消息，说明自己和对方处于同一个子网内
 */
int route_on_probe_ack(route_ctx_t *rt, const struct sockaddr_in *from) {

    printf(LA_F("Received ROUTE_PROBE_ACK pkt from %s:%d, len=0", LA_F124, 147),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    rt->lan_peer_addr = *from;
    rt->lan_confirmed = 1;
    print("I:", LA_F("%s %s:%d", LA_F19, 276), LA_W("LAN peer confirmed", LA_W40, 51),
                 inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    return 0;
}
