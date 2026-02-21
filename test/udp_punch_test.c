/*
 * UDP打洞测试工具（带STUN探测）
 * 
 * 用法：./udp_punch_test [本地端口]
 * 
 * 功能：
 *   1. 向STUN服务器探测，获取NAT映射的公网IP:Port
 *   2. 打印映射地址，手动告知对方
 *   3. 输入对方的映射地址
 *   4. 开始双向UDP打洞测试
 * 
 * 示例（Alice和Bob同时运行）：
 *   Alice: ./udp_punch_test
 *   Bob:   ./udp_punch_test
 * 
 *   双方看到各自的映射后，交换并输入对方地址
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#endif

/* STUN协议常量 */
#define STUN_MAGIC 0x2112A442
#define STUN_BINDING_REQUEST  0x0001
#define STUN_BINDING_RESPONSE 0x0101
#define STUN_ATTR_XOR_MAPPED_ADDR 0x0020

/* STUN消息头（20字节） */
typedef struct {
    uint16_t type;
    uint16_t length;
    uint32_t magic;
    uint8_t  tsx_id[12];
} __attribute__((packed)) stun_hdr_t;

void print_time() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    printf("[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("用法: %s <本地端口> <目标IP> <目标端口> [send|recv|both]\n", argv[0]);
        printf("\n示例（双向打洞）:\n");
        printf("  Alice: %s 38113 175.18.158.132 20341\n", argv[0]);
        printf("  Bob:   %s 20341 139.214.247.234 38113\n", argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int local_port = atoi(argv[1]);
    const char *target_ip = argv[2];
    int target_port = atoi(argv[3]);
    const char *mode = (argc > 4) ? argv[4] : "both";

    int do_send = (strcmp(mode, "send") == 0 || strcmp(mode, "both") == 0);
    int do_recv = (strcmp(mode, "recv") == 0 || strcmp(mode, "both") == 0);

    // 创建UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // 绑定本地端口
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);

    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    // 设置非阻塞
#ifdef _WIN32
    u_long mode_nb = 1;
    ioctlsocket(sock, FIONBIO, &mode_nb);
#else
    fcntl(sock, F_SETFL, O_NONBLOCK);
#endif

    // 目标地址
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    inet_pton(AF_INET, target_ip, &target_addr.sin_addr);
    target_addr.sin_port = htons(target_port);

    print_time();
    printf("========== UDP打洞测试 ==========\n");
    printf("本地端口: %d\n", local_port);
    printf("目标地址: %s:%d\n", target_ip, target_port);
    printf("模式: %s\n", mode);
    printf("================================\n\n");

    int packets_sent = 0;
    int packets_recv = 0;
    time_t last_send = 0;

    while (1) {
        time_t now = time(NULL);

        // 发送打洞包（每秒1次）
        if (do_send && (now != last_send)) {
            last_send = now;
            char msg[64];
            snprintf(msg, sizeof(msg), "PUNCH-%d", ++packets_sent);
            
            int n = sendto(sock, msg, strlen(msg), 0,
                          (struct sockaddr*)&target_addr, sizeof(target_addr));
            
            if (n > 0) {
                print_time();
                printf(">> 发送: %s (长度=%d) -> %s:%d\n", 
                       msg, n, target_ip, target_port);
            } else {
                print_time();
                printf("发送失败: %s\n", strerror(errno));
            }
        }

        // 接收回包
        if (do_recv) {
            char buf[1024];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            
            int n = recvfrom(sock, buf, sizeof(buf)-1, 0,
                            (struct sockaddr*)&from, &fromlen);
            
            if (n > 0) {
                buf[n] = '\0';
                packets_recv++;
                
                char from_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, from_ip, sizeof(from_ip));
                
                print_time();
                printf("<< 收到: %s (长度=%d) <- %s:%d\n",
                       buf, n, from_ip, ntohs(from.sin_port));
                
                // 自动回复
                if (do_send && strncmp(buf, "PUNCH-", 6) == 0) {
                    char reply[64];
                    snprintf(reply, sizeof(reply), "PONG-%s", buf+6);
                    sendto(sock, reply, strlen(reply), 0,
                          (struct sockaddr*)&from, fromlen);
                    print_time();
                    printf(">> 回复: %s -> %s:%d\n",
                           reply, from_ip, ntohs(from.sin_port));
                }
            }
#ifdef _WIN32
            else if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
            else if (errno != EAGAIN && errno != EWOULDBLOCK) {
#endif
                print_time();
                printf("接收错误: %s\n", strerror(errno));
            }
        }

        // 统计信息（每10秒）
        static time_t last_stats = 0;
        if (now - last_stats >= 10) {
            last_stats = now;
            print_time();
            printf("==== 统计: 发送=%d, 接收=%d ====\n", packets_sent, packets_recv);
        }

        usleep(100000); // 100ms
    }

    close(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
