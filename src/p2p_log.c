
#include "p2p_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

static struct {
    p2p_log_level_t level;
    FILE *output;
    int use_timestamp;
    int use_color;
} log_state = {
    .level = P2P_LOG_INFO,
    .output = NULL,  /* NULL 代表使用 stdout */
    .use_timestamp = 1,
    .use_color = 1
};

void p2p_log_set_level(p2p_log_level_t level) {
    log_state.level = level;
}

p2p_log_level_t p2p_log_get_level(void) {
    return log_state.level;
}

void p2p_log_set_output(FILE *fp) {
    log_state.output = fp;
}

void p2p_log_set_timestamp(int enabled) {
    log_state.use_timestamp = enabled;
}

void p2p_log_set_color(int enabled) {
    log_state.use_color = enabled;
}

static const char *level_to_string(p2p_log_level_t level) {
    switch (level) {
        case P2P_LOG_ERROR: return "ERROR";
        case P2P_LOG_WARN:  return "WARN ";
        case P2P_LOG_INFO:  return "INFO ";
        case P2P_LOG_DEBUG: return "DEBUG";
        case P2P_LOG_TRACE: return "TRACE";
        default:            return "?????";
    }
}

static const char *level_to_color(p2p_log_level_t level) {
    switch (level) {
        case P2P_LOG_ERROR: return COLOR_RED;
        case P2P_LOG_WARN:  return COLOR_YELLOW;
        case P2P_LOG_INFO:  return COLOR_GREEN;
        case P2P_LOG_DEBUG: return COLOR_CYAN;
        case P2P_LOG_TRACE: return COLOR_GRAY;
        default:            return COLOR_RESET;
    }
}

void p2p_log(p2p_log_level_t level, const char *module, const char *fmt, ...) {

    if (level > log_state.level) {
        return;
    }

    FILE *out = log_state.output ? log_state.output : stdout;

    if (log_state.use_timestamp) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm *tm_info = localtime(&tv.tv_sec);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
        fprintf(out, "[%s.%03d] ", time_buf, (int)(tv.tv_usec / 1000));
    }

    if (log_state.use_color) {
        fprintf(out, "%s[%s]%s ", level_to_color(level), level_to_string(level), COLOR_RESET);
    } else {
        fprintf(out, "[%s] ", level_to_string(level));
    }

    if (module && module[0]) {
        fprintf(out, "[%s] ", module);
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);
}
