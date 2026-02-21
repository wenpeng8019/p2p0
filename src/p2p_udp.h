/*
 * UDP socket 帮助函数和数据包编解码
 */

#ifndef P2P_UDP_H
#define P2P_UDP_H

#include <p2p.h>
#include <p2pp.h>              /* p2p_packet_hdr_t 定义 */
#include "p2p_platform.h"   /* cross-platform socket headers */

/* UDP 传输层常量 */
#define P2P_HDR_SIZE      4                 /* 包头大小 */
#define P2P_MAX_PAYLOAD   (P2P_MTU - P2P_HDR_SIZE)  /* 1196 */

p2p_socket_t  udp_create_socket(uint16_t port);
int  udp_send_to(p2p_socket_t sock, const struct sockaddr_in *addr,
                 const void *buf, int len);
int  udp_recv_from(p2p_socket_t sock, struct sockaddr_in *from,
                   void *buf, int max_len);
int  udp_send_packet(p2p_socket_t sock, const struct sockaddr_in *addr,
                     uint8_t type, uint8_t flags, uint16_t seq,
                     const void *payload, int payload_len);

void p2p_pkt_hdr_encode(uint8_t *buf, uint8_t type, uint8_t flags, uint16_t seq);
void p2p_pkt_hdr_decode(const uint8_t *buf, p2p_packet_hdr_t *hdr);

#endif /* P2P_UDP_H */
