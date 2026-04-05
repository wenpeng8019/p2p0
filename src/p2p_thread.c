
#ifdef P2P_THREADED

#include "p2p_internal.h"

static int32_t p2p_thread_func(void *arg) {
    struct p2p_instance *inst = (struct p2p_instance *)arg;

    while (!inst->quit) {
        P_mutex_lock(&inst->mtx);
        p2p_update((p2p_handle_t)inst);
        P_mutex_unlock(&inst->mtx);

        int ms = inst->cfg.update_interval_ms;
        if (ms <= 0) ms = 10;
        P_usleep((unsigned int)ms * 1000);
    }

    return 0;
}

ret_t p2p_thread_start(struct p2p_instance *inst) {
    inst->quit = 0;
    if (P_mutex_init(&inst->mtx) != 0)
        return -1;
    ret_t ret = P_thread(&inst->thread, p2p_thread_func, inst, P_THD_NORMAL, 0);
    if (ret != E_NONE) {
        P_mutex_final(&inst->mtx);
        return ret;
    }
    inst->thread_running = 1;
    return E_NONE;
}

void p2p_thread_stop(struct p2p_instance *inst) {
    if (!inst->thread_running) return;

    inst->quit = 1;
    P_join(inst->thread, NULL);
    P_mutex_final(&inst->mtx);
    inst->thread_running = 0;
}

#endif /* P2P_THREADED */
