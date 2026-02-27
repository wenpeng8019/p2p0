
#ifdef P2P_THREADED

#include "p2p_internal.h"

static int32_t p2p_thread_func(void *arg) {
    p2p_session_t *s = (p2p_session_t *)arg;

    while (!s->quit) {
        P_mutex_lock(&s->mtx);
        p2p_update(s);
        P_mutex_unlock(&s->mtx);

        int ms = s->cfg.update_interval_ms;
        if (ms <= 0) ms = 10;
        P_usleep((unsigned int)ms * 1000);
    }

    return 0;
}

int p2p_thread_start(p2p_session_t *s) {
    s->quit = 0;
    if (P_mutex_init(&s->mtx) != 0)
        return -1;
    ret_t ret = P_thread(&s->thread, p2p_thread_func, s, P_THD_NORMAL, 0);
    if (ret != E_NONE) {
        P_mutex_final(&s->mtx);
        return -1;
    }
    s->thread_running = 1;
    return 0;
}

void p2p_thread_stop(p2p_session_t *s) {
    if (!s->thread_running) return;

    s->quit = 1;
    P_join(s->thread, NULL);
    P_mutex_final(&s->mtx);
    s->thread_running = 0;
}

#endif /* P2P_THREADED */
