
#ifndef P2P_LOG_H
#define P2P_LOG_H

#include <p2p.h>    /* p2p_log_level_t */
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void p2p_log_set_output(FILE *fp);
void p2p_log_set_timestamp(int enabled);
void p2p_log_set_color(int enabled);

void p2p_log(p2p_log_level_t level, const char *module, const char *fmt, ...);

#define P2P_LOG_ERROR(module, ...) p2p_log(P2P_LOG_LEVEL_ERROR, module, __VA_ARGS__)
#define P2P_LOG_WARN(module, ...)  p2p_log(P2P_LOG_LEVEL_WARN,  module, __VA_ARGS__)
#define P2P_LOG_INFO(module, ...)  p2p_log(P2P_LOG_LEVEL_INFO,  module, __VA_ARGS__)
#define P2P_LOG_DEBUG(module, ...) p2p_log(P2P_LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define P2P_LOG_TRACE(module, ...) p2p_log(P2P_LOG_LEVEL_TRACE, module, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* P2P_LOG_H */
