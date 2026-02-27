/*
 * UDP socket 帮助函数和数据包编解码
 */

#ifndef P2P_UDP_H
#define P2P_UDP_H

#include <p2pp.h>               /* p2p_packet_hdr_t 定义 */
#include "../stdc/stdc.h"       /* cross-platform utilities */

sock_t  udp_open_socket(uint16_t port);
int  udp_send_to(sock_t sock, const struct sockaddr_in *addr,
                 const void *data, int len);
int  udp_recv_from(sock_t sock, struct sockaddr_in *from,
                   void *buf, int buf_size);
int  udp_send_packet(sock_t sock, const struct sockaddr_in *addr,
                     uint8_t type, uint8_t flags, uint16_t seq,
                     const void *payload, int payload_len);

void p2p_pkt_hdr_encode(uint8_t *buf, uint8_t type, uint8_t flags, uint16_t seq);
void p2p_pkt_hdr_decode(const uint8_t *buf, p2p_packet_hdr_t *hdr);

#endif /* P2P_UDP_H */
