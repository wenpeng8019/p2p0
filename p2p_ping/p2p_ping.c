/*
 * P2P 诊断工具 / 聊天客户端
 *
 * 支持三种信令模式：
 *   1. COMPACT 模式 - 简单 UDP 信令
 *   2. RELAY 模式   - ICE/TCP 信令
 *   3. PUBSUB 模式  - GitHub Gist 信令
 *
 * 连接建立后进入聊天模式：
 *   - 输入行固定在终端底部（使用 ANSI 滚动区域）
 *   - 日志和收到的消息从上方滚动输出
 *   - --echo 选项可自动回复收到的消息
 */

#include <p2p.h>
#include "p2p_internal.h"
#include "p2p_signal_relay.h"
#include "p2p_signal_pubsub.h"
#include "p2p_log.h"
#include "ping_lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

/* ============================================================================
 * TUI：固定输入行 + 滚动日志区
 *
 * 布局：
 *   行 1 ~ (rows-1)  → ANSI 滚动区域（日志 / 收到的消息向上滚动）
 *   行 rows          → 固定输入行 "> 用户输入..."
 *
 * 实现要点：
 *   - 使用 \033[1;Nr 设置滚动区域（N = rows-1）
 *   - DEC 保存/恢复光标 (\0337 / \0338) 保护输入行
 *   - stdin 设为 raw + O_NONBLOCK，逐字符读取
 *   - p2p_log 输出重定向到管道，主循环轮询读取并通过 tui_println 打印
 * ============================================================================ */

static int            g_tui_active  = 0;          /* TUI 是否已初始化 */
static int            g_echo_mode   = 0;           /* --echo 模式 */
static char           g_ibuf[512]   = {0};         /* 当前输入缓冲 */
static int            g_ilen        = 0;           /* 输入缓冲长度 */
static int            g_rows        = 24;          /* 终端行数 */
#ifdef _WIN32
static DWORD          g_orig_in_mode  = 0;         /* 原始控制台输入模式 */
static DWORD          g_orig_out_mode = 0;         /* 原始控制台输出模式 */
#else
static struct termios g_orig_term;                 /* 原始终端配置 */
#endif
static const char    *g_my_name     = "me";        /* 本端显示名 */

/* 获取终端行数 */
static int tui_get_rows(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (rows > 4) return rows;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 4)
        return (int)ws.ws_row;
#endif
    return 24;
}

/*
 * 在滚动区域打印一行（不破坏输入行）
 *
 * 流程：
 *   1. 保存光标（DEC \0337）
 *   2. 移到滚动区底部行 (rows-1)
 *   3. \n → 触发滚动区向上滚动一行，光标停在 rows-1
 *   4. 清行并写内容
 *   5. 恢复光标（DEC \0338），光标回到输入行末尾
 *   6. 重绘输入行（防止偶发脏屏）
 */
static void tui_println(const char *line) {
    printf("\0337");                               /* save cursor */
    printf("\033[%d;1H", g_rows - 1);             /* 移到滚动区末行 */
    printf("\n\r\033[K%s", line);                  /* 滚动 + 清行 + 写内容 */
    printf("\0338");                               /* restore cursor */
    printf("\033[%d;1H\033[K> %s", g_rows, g_ibuf); /* 重绘输入行 */
    fflush(stdout);
}

/* 日志回调：在滚动区域打印一行（含等级与模块前缀） */
static void tui_log_callback(p2p_log_level_t level,
                             const char *module, const char *message) {
    const char *lvl = "?????";
    switch (level) {
        case P2P_LOG_LEVEL_ERROR: lvl = "ERROR"; break;
        case P2P_LOG_LEVEL_WARN:  lvl = "WARN "; break;
        case P2P_LOG_LEVEL_INFO:  lvl = "INFO "; break;
        case P2P_LOG_LEVEL_DEBUG: lvl = "DEBUG"; break;
        case P2P_LOG_LEVEL_TRACE: lvl = "TRACE"; break;
        default: break;
    }
    char line[P2P_LOG_MSG_MAX + 64];
    if (module && module[0])
        snprintf(line, sizeof(line), "[%s] [%s] %s", lvl, module, message);
    else
        snprintf(line, sizeof(line), "[%s] %s", lvl, message);
    tui_println(line);
}

/* 初始化 TUI（连接建立后调用一次） */
static void tui_init(void) {
    g_rows = tui_get_rows();

    /* 将 p2p_log 输出重定向到 TUI 回调 */
    p2p_set_log_output(tui_log_callback);

    /* 设置 ANSI 滚动区域：行 1 ~ rows-1 */
    printf("\033[1;%dr", g_rows - 1);
    /* 清空输入行并显示提示符 */
    printf("\033[%d;1H\033[K> ", g_rows);
    fflush(stdout);

#ifdef _WIN32
    /* Windows：启用 ANSI VT 输出，禁用 ECHO + 行输入 */
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hin,  &g_orig_in_mode);
    GetConsoleMode(hout, &g_orig_out_mode);
    SetConsoleMode(hin,  ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(hout, (g_orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                          | DISABLE_NEWLINE_AUTO_RETURN));
#else
    /* stdin：raw 模式（禁用行缓冲 + 回显） + 非阻塞 */
    struct termios t;
    tcgetattr(STDIN_FILENO, &g_orig_term);
    t = g_orig_term;
    t.c_lflag &= ~(unsigned)(ICANON | ECHO);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
#endif

    g_tui_active = 1;
}

/* 退出 TUI，恢复终端状态 */
static void tui_cleanup(void) {
    if (!g_tui_active) return;
    g_tui_active = 0;

    /* 清除日志回调，恢复 p2p_log 默认输出（stdout） */
    p2p_set_log_output(NULL);

    /* 重置滚动区域，光标移到最后一行 */
    printf("\033[r");
    printf("\033[%d;1H\n", g_rows);
    fflush(stdout);

#ifdef _WIN32
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),  g_orig_in_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_orig_out_mode);
#else
    /* 恢复终端模式 */
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
    fcntl(STDIN_FILENO, F_SETFL, 0);
#endif
}

/* 处理 stdin 按键，维护输入缓冲，回车时发送 */
static void tui_process_input(p2p_session_t *s) {
    for (;;) {
        int ch;
#ifdef _WIN32
        if (!_kbhit()) break;
        ch = _getch();
        if (ch == 0 || ch == 0xE0) { _getch(); continue; } /* 跳过扩展键 */
#else
        {
            char tmp;
            if (read(STDIN_FILENO, &tmp, 1) != 1) break;
            ch = (unsigned char)tmp;
        }
#endif
        char c = (char)ch;
        if (c == '\r' || c == '\n') {
            if (g_ilen > 0) {
                g_ibuf[g_ilen] = '\0';
                /* 在滚动区显示自己发出的消息 */
                char line[576];
                snprintf(line, sizeof(line), "%s: %s", g_my_name, g_ibuf);
                tui_println(line);
                /* 发送 */
                p2p_send(s, g_ibuf, g_ilen);
                /* 清空输入行 */
                g_ilen = 0;
                g_ibuf[0] = '\0';
                printf("\033[%d;1H\033[K> ", g_rows);
                fflush(stdout);
            }
        } else if (c == 0x7f || c == '\b') {   /* Backspace / DEL */
            if (g_ilen > 0) {
                g_ilen--;
                g_ibuf[g_ilen] = '\0';
                printf("\033[%d;1H\033[K> %s", g_rows, g_ibuf);
                fflush(stdout);
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f
                   && g_ilen < (int)sizeof(g_ibuf) - 1) {
            /* 可打印 ASCII：直接追加并回显 */
            g_ibuf[g_ilen++] = c;
            g_ibuf[g_ilen]   = '\0';
            putchar(c);
            fflush(stdout);
        }
        /* 忽略方向键等控制序列 */
    }
}

/* SIGINT / SIGTERM：优雅退出（跨平台） */
static void on_signal(int sig) {
    (void)sig;
    tui_cleanup();
    exit(0);
}

/* SIGWINCH：终端窗口大小变化，更新滚动区域（仅 Unix） */
#ifndef _WIN32
static void on_sigwinch(int sig) {
    (void)sig;
    if (!g_tui_active) return;
    int new_rows = tui_get_rows();
    if (new_rows != g_rows) {
        g_rows = new_rows;
        printf("\033[1;%dr", g_rows - 1);
        printf("\033[%d;1H\033[K> %s", g_rows, g_ibuf);
        fflush(stdout);
    }
}
#endif

/* ============================================================================
 * 主程序
 * ============================================================================ */

static void print_help(const char *prog) {
    printf(ping_msg(MSG_PING_USAGE), prog);
    printf("\n");
    printf("%s\n", ping_msg(MSG_PING_OPTIONS));
    printf("%s\n", ping_msg(MSG_PING_OPT_DTLS));
    printf("%s\n", ping_msg(MSG_PING_OPT_OPENSSL));
    printf("%s\n", ping_msg(MSG_PING_OPT_PSEUDO));
    printf("%s\n", ping_msg(MSG_PING_OPT_SERVER));
    printf("%s\n", ping_msg(MSG_PING_OPT_COMPACT));
    printf("%s\n", ping_msg(MSG_PING_OPT_GITHUB));
    printf("%s\n", ping_msg(MSG_PING_OPT_GIST));
    printf("%s\n", ping_msg(MSG_PING_OPT_NAME));
    printf("%s\n", ping_msg(MSG_PING_OPT_TO));
    printf("%s\n", ping_msg(MSG_PING_OPT_DISABLE_LAN));
    printf("%s\n", ping_msg(MSG_PING_OPT_VERBOSE_PUNCH));
    printf("%s\n", ping_msg(MSG_PING_OPT_ECHO));
    printf("%s\n", ping_msg(MSG_PING_OPT_CN));
}

static const char* state_name(int state) {
    switch (state) {
        case P2P_STATE_IDLE:        return "IDLE";
        case P2P_STATE_REGISTERING: return "REGISTERING";
        case P2P_STATE_PUNCHING:    return "PUNCHING";
        case P2P_STATE_CONNECTED:   return "CONNECTED";
        case P2P_STATE_RELAY:       return "RELAY";
        case P2P_STATE_CLOSING:     return "CLOSING";
        case P2P_STATE_CLOSED:      return "CLOSED";
        case P2P_STATE_ERROR:       return "ERROR";
        default:                    return "UNKNOWN";
    }
}

static void log_state_change(p2p_session_t *s) {
    static int last_state = -1;
    if (s->state != last_state) {
        if (g_tui_active) {
            char line[128];
            snprintf(line, sizeof(line), ping_msg(MSG_PING_STATE_CHANGE),
                     state_name(last_state), last_state,
                     state_name(s->state), s->state);
            tui_println(line);
        } else {
            printf(ping_msg(MSG_PING_STATE_CHANGE),
                   state_name(last_state), last_state,
                   state_name(s->state), s->state);
            printf("\n");
            fflush(stdout);
        }
        last_state = s->state;
    }
}

/* 连接断开回调 */
static void on_disconnected(p2p_session_t *s, void *userdata) {
    (void)s; (void)userdata;
    if (g_tui_active) {
        tui_println(ping_msg(MSG_PING_CHAT_DISCONNECT));
    } else {
        printf("%s\n", ping_msg(MSG_PING_DISCONNECTED));
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    int use_dtls = 0, use_openssl = 0, use_pseudo = 0, use_compact = 0;
    int disable_lan = 0, verbose_punch = 0, use_chinese = 0, show_help = 0;
    const char *server_ip = NULL, *gh_token = NULL, *gist_id = NULL;
    const char *my_name = "unnamed", *target_name = NULL;
    int server_port = 8888;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--dtls")          == 0) use_dtls = 1;
        else if (strcmp(argv[i], "--openssl")        == 0) use_openssl = 1;
        else if (strcmp(argv[i], "--pseudo")         == 0) use_pseudo = 1;
        else if (strcmp(argv[i], "--compact")        == 0) use_compact = 1;
        else if (strcmp(argv[i], "--disable-lan")    == 0) disable_lan = 1;
        else if (strcmp(argv[i], "--verbose-punch")  == 0) verbose_punch = 1;
        else if (strcmp(argv[i], "--cn")             == 0) use_chinese = 1;
        else if (strcmp(argv[i], "--echo")           == 0) g_echo_mode = 1;
        else if (strcmp(argv[i], "--server") == 0 && i+1 < argc) server_ip  = argv[++i];
        else if (strcmp(argv[i], "--github") == 0 && i+1 < argc) gh_token   = argv[++i];
        else if (strcmp(argv[i], "--gist")   == 0 && i+1 < argc) gist_id    = argv[++i];
        else if (strcmp(argv[i], "--name")   == 0 && i+1 < argc) my_name    = argv[++i];
        else if (strcmp(argv[i], "--to")     == 0 && i+1 < argc) target_name = argv[++i];
        else if (strcmp(argv[i], "--help")   == 0) show_help = 1;
    }
    g_my_name = my_name;

    if (use_chinese) ping_set_language(P2P_LANG_ZH);
    if (show_help)  { print_help(argv[0]); return 0; }

    printf("%s\n\n", ping_msg(MSG_PING_TITLE));

    /* 解析 IP:PORT 格式 */
    char server_host_buf[256] = {0};
    const char *server_host = server_ip;
    if (server_ip) {
        const char *colon = strchr(server_ip, ':');
        if (colon) {
            size_t len = (size_t)(colon - server_ip);
            if (len < sizeof(server_host_buf)) {
                memcpy(server_host_buf, server_ip, len);
                server_host_buf[len] = '\0';
                server_host  = server_host_buf;
                server_port  = atoi(colon + 1);
            }
        }
    }

    p2p_config_t cfg = {0};
    cfg.use_dtls       = use_dtls;
    cfg.use_openssl    = use_openssl;
    cfg.use_pseudotcp  = use_pseudo;
    cfg.use_ice        = !use_compact;
    cfg.stun_server    = "stun.l.google.com";
    cfg.stun_port      = 3478;
    cfg.server_host    = server_host;
    cfg.server_port    = server_port;
    cfg.gh_token       = gh_token;
    cfg.gist_id        = gist_id;
    cfg.bind_port      = 0;
    cfg.language       = use_chinese ? P2P_LANG_ZH : P2P_LANG_EN;
    cfg.disable_lan_shortcut = disable_lan;
    cfg.verbose_nat_punch    = verbose_punch;
    cfg.on_disconnected      = on_disconnected;
    cfg.userdata             = NULL;
    strncpy(cfg.local_peer_id, my_name, P2P_PEER_ID_MAX - 1);

    if (server_ip)
        cfg.signaling_mode = cfg.use_ice ? P2P_SIGNALING_MODE_RELAY : P2P_SIGNALING_MODE_COMPACT;
    else if (gh_token && gist_id)
        cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;

    p2p_session_t *s = p2p_create(&cfg);
    if (!s) { printf("%s\n", ping_msg(MSG_PING_CREATE_FAIL)); return 1; }

    const char *mode_name = NULL;
    if      (server_ip)         mode_name = cfg.use_ice ? "ICE" : "COMPACT";
    else if (gh_token && gist_id) mode_name = "PUBSUB";
    else {
        printf("%s\n%s\n", ping_msg(MSG_PING_NO_MODE), ping_msg(MSG_PING_USE_ONE_OF));
        print_help(argv[0]);
        return 1;
    }

    if (disable_lan)    printf("%s\n", ping_msg(MSG_PING_LAN_DISABLED));
    if (verbose_punch)  printf("%s\n", ping_msg(MSG_PING_VERBOSE_ENABLED));
    if (g_echo_mode)    printf("%s\n", ping_msg(MSG_PING_CHAT_ECHO_ON));

    if (p2p_connect(s, target_name) < 0) {
        printf("%s\n", ping_msg(MSG_PING_CONNECT_FAIL));
        return 1;
    }

    if (target_name) { printf(ping_msg(MSG_PING_MODE_CONNECTING), mode_name, target_name); printf("\n\n"); }
    else             { printf(ping_msg(MSG_PING_MODE_WAITING),    mode_name);              printf("\n\n"); }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
#ifndef _WIN32
    signal(SIGWINCH, on_sigwinch);
#endif

    /* ---- 主循环 ---- */
    while (1) {
        p2p_update(s);
        log_state_change(s);

        if (p2p_is_ready(s)) {
            /* 首次连接成功：初始化 TUI，降低日志等级 */
            if (!g_tui_active) {
                printf("%s\n", ping_msg(MSG_PING_CHAT_ENTER));
                fflush(stdout);
                tui_init();
                p2p_set_log_level(P2P_LOG_LEVEL_WARN);
                tui_println(ping_msg(MSG_PING_CHAT_CONNECTED));
            }

            /* 接收对端消息 */
            char data[512] = {0};
            int r = p2p_recv(s, data, (int)sizeof(data) - 1);
            if (r > 0) {
                data[r] = '\0';
                char line[576];
                const char *peer = target_name ? target_name : "peer";
                snprintf(line, sizeof(line), "%s: %s", peer, data);
                tui_println(line);

                /* echo 模式：不对已是 echo 的消息再次回复（防循环）*/
                if (g_echo_mode && strncmp(data, "[echo] ", 7) != 0) {
                    char echo_msg[520];
                    snprintf(echo_msg, sizeof(echo_msg), "[echo] %s", data);
                    p2p_send(s, echo_msg, (int)strlen(echo_msg));
                }
            }

            /* 处理键盘输入 */
            tui_process_input(s);
        }

        p2p_sleep_ms(10);
    }

    tui_cleanup();
    return 0;
}
