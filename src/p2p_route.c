
/*
 * 路由检测
 */

#define MOD_TAG "ROUTE"

#include "p2p_internal.h"
#ifdef _WIN32
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

///////////////////////////////////////////////////////////////////////////////

static route_ctx_t      g_ctx;
static int              g_ref = 0;

static void route_final(route_ctx_t *rt) {
    if (rt->local_addrs) free(rt->local_addrs);
    memset(rt, 0, sizeof(*rt));
}

ret_t route_shared_acquire(void) {
    if (g_ref > 0) {
        g_ref++;
        return E_NONE;
    }

    ret_t ret = route_detect_local(&g_ctx);
    if (ret < 0) {
        route_final(&g_ctx);
        return ret;
    }

    g_ref = 1;
    return E_NONE;
}

void route_shared_release(void) {
    if (g_ref <= 0) return;
    g_ref--;
    if (g_ref == 0) {
        route_final(&g_ctx);
    }
}

const route_ctx_t *route_shared_get(void) {
    return g_ref > 0 ? &g_ctx : NULL;
}

// 检测获取本地所有有效的网络地址
ret_t route_detect_local(route_ctx_t *rt) {

    printf("%s", LA_S("Detecting local network addresses", LA_S34, 34));

    rt->addr_count = 0;

#if P_WIN
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
        if (!(ifa->ifa_flags & IFF_UP)) continue;               // 接口未启动
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;            // 跳过 localhost
        if (ifa->ifa_addr->sa_family != AF_INET) continue;      // 协议无效
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

    print("I:", LA_F("Local address detection done: %d address(es)", LA_F267, 267), rt->addr_count);
    if (p2p_log_level == P2P_LOG_LEVEL_VERBOSE) {
        for (i = 0; i < rt->addr_count; i++) {
            print("V:", LA_F("  [%d] %s/%d", LA_F42, 42), i,
                  inet_ntoa(rt->local_addrs[i].sin_addr), mask_to_prefix(rt->local_masks[i]));
        }
    }
    return rt->addr_count;
}

// 探测对方内网 IP 和自己是否属于同一个子网段内
bool route_check_same_subnet(route_ctx_t *rt, const struct sockaddr_in *peer_priv) {

    uint32_t peer_ip = peer_priv->sin_addr.s_addr;
    for (int i = 0; i < rt->addr_count; i++) {
        uint32_t local_ip = rt->local_addrs[i].sin_addr.s_addr;
        uint32_t mask = rt->local_masks[i];
        if ((local_ip & mask) == (peer_ip & mask)) return true;
    }

    return false;
}
