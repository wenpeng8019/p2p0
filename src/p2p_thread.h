
#ifndef P2P_THREAD_H
#define P2P_THREAD_H

#ifdef P2P_THREADED

struct p2p_session;

int  p2p_thread_start(struct p2p_session *s);
void p2p_thread_stop(struct p2p_session *s);

#endif /* P2P_THREADED */

#endif /* P2P_THREAD_H */
