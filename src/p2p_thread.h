
#ifndef P2P_THREAD_H
#define P2P_THREAD_H

#ifdef P2P_THREADED

struct p2p_instance;

ret_t  p2p_thread_start(struct p2p_instance *inst);
void p2p_thread_stop(struct p2p_instance *inst);

#endif /* P2P_THREADED */

#endif /* P2P_THREAD_H */
