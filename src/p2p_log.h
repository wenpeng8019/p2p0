
#ifndef P2P_LOG_H
#define P2P_LOG_H

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    P2P_LOG_NONE  = 0,
    P2P_LOG_ERROR = 1,
    P2P_LOG_WARN  = 2,
    P2P_LOG_INFO  = 3,
    P2P_LOG_DEBUG = 4,
    P2P_LOG_TRACE = 5
} p2p_log_level_t;

void p2p_log_set_level(p2p_log_level_t level);
p2p_log_level_t p2p_log_get_level(void);
void p2p_log_set_output(FILE *fp);
void p2p_log_set_timestamp(int enabled);
void p2p_log_set_color(int enabled);

void p2p_log(p2p_log_level_t level, const char *module, const char *fmt, ...);

#define P2P_LOG_ERROR(module, ...) p2p_log(P2P_LOG_ERROR, module, __VA_ARGS__)
#define P2P_LOG_WARN(module, ...)  p2p_log(P2P_LOG_WARN,  module, __VA_ARGS__)
#define P2P_LOG_INFO(module, ...)  p2p_log(P2P_LOG_INFO,  module, __VA_ARGS__)
#define P2P_LOG_DEBUG(module, ...) p2p_log(P2P_LOG_DEBUG, module, __VA_ARGS__)
#define P2P_LOG_TRACE(module, ...) p2p_log(P2P_LOG_TRACE, module, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* P2P_LOG_H */
