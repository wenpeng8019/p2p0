
#ifdef P2P_THREADED

#include "p2p_thread.h"
#include "p2p_internal.h"

static void *p2p_thread_func(void *arg) {
    p2p_session_t *s = (p2p_session_t *)arg;

    while (!s->quit) {
        pthread_mutex_lock(&s->mtx);
        p2p_update(s);
        pthread_mutex_unlock(&s->mtx);

        int ms = s->cfg.update_interval_ms;
        if (ms <= 0) ms = 10;
        usleep(ms * 1000);
    }

    return NULL;
}

int p2p_thread_start(p2p_session_t *s) {
    s->quit = 0;
    if (pthread_mutex_init(&s->mtx, NULL) != 0)
        return -1;
    if (pthread_create(&s->thread, NULL, p2p_thread_func, s) != 0) {
        pthread_mutex_destroy(&s->mtx);
        return -1;
    }
    s->thread_running = 1;
    return 0;
}

void p2p_thread_stop(p2p_session_t *s) {
    if (!s->thread_running) return;

    s->quit = 1;
    pthread_join(s->thread, NULL);
    pthread_mutex_destroy(&s->mtx);
    s->thread_running = 0;
}

#endif /* P2P_THREADED */
