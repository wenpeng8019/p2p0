/*
 * TCP 打洞实现
 */
#ifndef P2P_TCP_PUNCH_H
#define P2P_TCP_PUNCH_H

#include <netinet/in.h>

struct p2p_session;

int p2p_tcp_punch_connect(struct p2p_session *s, const struct sockaddr_in *remote);

#endif /* P2P_TCP_PUNCH_H */
