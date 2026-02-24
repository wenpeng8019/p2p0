
#include "p2p_internal.h"
#include <stdarg.h>

#define COLOR_RESET   P2P_COLOR_RESET
#define COLOR_RED     P2P_COLOR_RED
#define COLOR_YELLOW  P2P_COLOR_YELLOW
#define COLOR_GREEN   P2P_COLOR_GREEN
#define COLOR_CYAN    P2P_COLOR_CYAN
#define COLOR_GRAY    P2P_COLOR_GRAY

static struct {
    p2p_log_level_t level;
    FILE *output;
    int use_timestamp;
    int use_color;
    p2p_log_callback_t callback;
} log_state = {
    .level = P2P_LOG_LEVEL_INFO,
    .output = NULL,  /* NULL 代表使用 stdout */
    .use_timestamp = 1,
    .use_color = 1,
    .callback = NULL
};

void p2p_set_log_level(p2p_log_level_t level) {
    log_state.level = level;
}

p2p_log_level_t p2p_get_log_level(void) {
    return log_state.level;
}

void p2p_log_set_output(FILE *fp) {
    log_state.output = fp;
}

void p2p_set_log_output(p2p_log_callback_t cb) {
    log_state.callback = cb;
}

void p2p_log_set_timestamp(int enabled) {
    log_state.use_timestamp = enabled;
}

void p2p_log_set_color(int enabled) {
    log_state.use_color = enabled;
}

static const char *level_to_string(p2p_log_level_t level) {
    switch (level) {
        case P2P_LOG_LEVEL_ERROR: return "ERROR";
        case P2P_LOG_LEVEL_WARN:  return "WARN";
        case P2P_LOG_LEVEL_INFO:  return "INFO";
        case P2P_LOG_LEVEL_DEBUG: return "DEBUG";
        case P2P_LOG_LEVEL_VERBOSE: return "VERBOSE";
        default: return "UNDEF";
    }
}

static const char *level_to_color(p2p_log_level_t level) {
    switch (level) {
        case P2P_LOG_LEVEL_ERROR: return COLOR_RED;
        case P2P_LOG_LEVEL_WARN:  return COLOR_YELLOW;
        case P2P_LOG_LEVEL_INFO:  return COLOR_GREEN;
        case P2P_LOG_LEVEL_DEBUG: return COLOR_CYAN;
        case P2P_LOG_LEVEL_VERBOSE: return COLOR_GRAY;
        default: return COLOR_RESET;
    }
}

void p2p_log(p2p_log_level_t level, const char *module, const char *fmt, ...) {

    if (level > log_state.level) {
        return;
    }

    /* 回调路径：格式化正文后直接回调，不写 FILE */
    if (log_state.callback) {
        char msg[P2P_LOG_MSG_MAX];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
        log_state.callback(level, module, msg);
        return;
    }

    FILE *out = log_state.output ? log_state.output : stdout;

    if (log_state.use_timestamp) {
        uint64_t ms = p2p_time_ms();
        unsigned int hh = (unsigned int)((ms / 3600000) % 24);
        unsigned int mm = (unsigned int)((ms / 60000) % 60);
        unsigned int ss = (unsigned int)((ms / 1000) % 60);
        unsigned int ms3 = (unsigned int)(ms % 1000);
        fprintf(out, "[%02u:%02u:%02u.%03u] ", hh, mm, ss, ms3);
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
