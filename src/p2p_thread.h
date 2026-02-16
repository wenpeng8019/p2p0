
#ifndef P2P_THREAD_H
#define P2P_THREAD_H

#ifdef P2P_THREADED

#include <p2p.h>

int  p2p_thread_start(p2p_session_t *s);
void p2p_thread_stop(p2p_session_t *s);

#endif /* P2P_THREADED */

#endif /* P2P_THREAD_H */
