/*
 * UDP socket 帮助函数和数据包编解码
 */

#ifndef P2P_UDP_H
#define P2P_UDP_H

#include "predefine.h"
#include <p2pp.h>               /* p2p_packet_hdr_t 定义 */

struct p2p_instance;
struct p2p_session;

ret_t p2p_udp_open(struct p2p_instance *inst, uint16_t port);
void p2p_udp_close(struct p2p_instance *inst);

ret_t p2p_udp_send_to(struct p2p_instance *inst, const struct sockaddr_in *addr,
                      const void *data, int len);

ret_t p2p_udp_recv_from(struct p2p_instance *inst, struct sockaddr_in *from,
                        void *buf, int buf_size);

#endif /* P2P_UDP_H */
