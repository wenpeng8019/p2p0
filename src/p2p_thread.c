
#ifdef P2P_THREADED

#include "p2p_thread.h"
#include "p2p_internal.h"

#ifdef _WIN32
static DWORD WINAPI p2p_thread_func(LPVOID arg) {
#else
static void *p2p_thread_func(void *arg) {
#endif
    p2p_session_t *s = (p2p_session_t *)arg;

    while (!s->quit) {
        p2p_mutex_lock(&s->mtx);
        p2p_update(s);
        p2p_mutex_unlock(&s->mtx);

        int ms = s->cfg.update_interval_ms;
        if (ms <= 0) ms = 10;
        p2p_sleep_ms(ms);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int p2p_thread_start(p2p_session_t *s) {
    s->quit = 0;
    if (p2p_mutex_init(&s->mtx) != 0)
        return -1;
    if (p2p_thread_create(&s->thread, p2p_thread_func, s) != 0) {
        p2p_mutex_destroy(&s->mtx);
        return -1;
    }
    s->thread_running = 1;
    return 0;
}

void p2p_thread_stop(p2p_session_t *s) {
    if (!s->thread_running) return;

    s->quit = 1;
    p2p_thread_join(s->thread);
    p2p_mutex_destroy(&s->mtx);
    s->thread_running = 0;
}

#endif /* P2P_THREADED */
