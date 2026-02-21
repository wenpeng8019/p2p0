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
#include <io.h>       /* _isatty / _fileno */
#define p2p_isatty(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#define p2p_isatty(f) isatty(fileno(f))
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

static int              g_tui_active  = 0;          /* TUI 是否已初始化 */
static int              g_first_connect_done = 0;   /* 首次连接已处理（非交互模式防重复打印）*/
static int              g_echo_mode   = 0;          /* --echo 模式 */
static char             g_buf_in[512] = {0};        /* 当前输入缓冲 */
static int              g_len_in      = 0;          /* 输入缓冲长度 */
static int              g_rows        = 24;         /* 终端行数 */
#ifdef _WIN32
static DWORD            g_orig_in_mode  = 0;        /* 原始控制台输入模式 */
static DWORD            g_orig_out_mode = 0;        /* 原始控制台输出模式 */
static int              g_win_pty_mode  = 0;        /* 1=ConPTY管道(VS Code)，0=真实控制台 */
#else
static struct termios   g_orig_term;                /* 原始终端配置 */
#endif
static const char*      g_my_name = "me";           /* 本端显示名 */

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
    if (!g_tui_active) {
        /* 非交互模式：普通换行输出，不发 ANSI 控制序列 */
        printf("%s\n", line);
        fflush(stdout);
        return;
    }
    printf("\0337");                               /* save cursor */
    printf("\033[%d;1H", g_rows - 1);             /* 移到滚动区末行 */
    printf("\n\r\033[K%s", line);                  /* 滚动 + 清行 + 写内容 */
    printf("\0338");                               /* restore cursor */
    printf("\033[%d;1H\033[K> %s", g_rows, g_buf_in); /* 重绘输入行 */
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
    /* 非交互终端（stdout 被重定向）时跳过 TUI，避免后台进程触发 SIGTTOU */
    if (!p2p_isatty(stdout)) return;
    g_rows = tui_get_rows();

#ifdef _WIN32
    /* Windows：先启用 ANSI VT 输出，再发送 ANSI 序列，否则第一屏乱码 */
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hout, &g_orig_out_mode);
    SetConsoleMode(hout, (g_orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                          | DISABLE_NEWLINE_AUTO_RETURN));
    /* 检测是否是管道模式（VS Code ConPTY / 重定向）
     * ConPTY 的 stdin 是 FILE_TYPE_PIPE，_kbhit() 对管道无效 */
    g_win_pty_mode = (GetFileType(hin) != FILE_TYPE_CHAR);
    if (!g_win_pty_mode) {
        /* 真实控制台：保留 ENABLE_PROCESSED_INPUT 使 Ctrl+C 能产生 SIGINT
         * 去掉 ENABLE_LINE_INPUT + ENABLE_ECHO_INPUT 实现逐字符读取 */
        GetConsoleMode(hin, &g_orig_in_mode);
        SetConsoleMode(hin, (g_orig_in_mode
                              | ENABLE_VIRTUAL_TERMINAL_INPUT)
                              & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    }
#endif

    /* 将 p2p_log 输出重定向到 TUI 回调 */
    p2p_set_log_output(tui_log_callback);

    /* 设置 ANSI 滚动区域：行 1 ~ rows-1（VT 已启用后再发送）*/
    printf("\033[1;%dr", g_rows - 1);
    /* 清空输入行并显示提示符 */
    printf("\033[%d;1H\033[K> ", g_rows);
    fflush(stdout);

#ifndef _WIN32
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
    if (!g_win_pty_mode)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), g_orig_in_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_orig_out_mode);
#else
    /* 恢复终端模式 */
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
    fcntl(STDIN_FILENO, F_SETFL, 0);
#endif
}

/* 处理 stdin 按键，维护输入缓冲，回车时发送 */
static void tui_process_input(p2p_handle_t hdl) {
    if (!g_tui_active) return;  /* 非交互模式（重定向/后台）跳过 stdin 读取 */
    for (;;) {
        int ch;
#ifdef _WIN32
        if (g_win_pty_mode) {
            /* ConPTY / 管道模式：_kbhit() 对管道无效，改用 PeekNamedPipe */
            HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
            DWORD avail = 0;
            if (!PeekNamedPipe(hin, NULL, 0, NULL, &avail, NULL) || avail == 0) break;
            DWORD nr = 0; CHAR raw = 0;
            if (!ReadFile(hin, &raw, 1, &nr, NULL) || nr == 0) break;
            ch = (unsigned char)raw;
        } else {
            /* 真实控制台：使用 _kbhit / _getch */
            if (!_kbhit()) break;
            ch = _getch();
            if (ch == 0 || ch == 0xE0) { _getch(); continue; } /* 跳过扩展键 */
        }
#else
        {
            char tmp;
            if (read(STDIN_FILENO, &tmp, 1) != 1) break;
            ch = (unsigned char)tmp;
        }
#endif
        char c = (char)ch;
        if (c == '\r' || c == '\n') {
            if (g_len_in > 0) {
                g_buf_in[g_len_in] = '\0';
                /* 在滚动区显示自己发出的消息 */
                char line[576];
                snprintf(line, sizeof(line), "%s: %s", g_my_name, g_buf_in);
                tui_println(line);
                /* 发送 */
                p2p_send(hdl, g_buf_in, g_len_in);
                /* 清空输入行 */
                g_len_in = 0;
                g_buf_in[0] = '\0';
                printf("\033[%d;1H\033[K> ", g_rows);
                fflush(stdout);
            }
        } else if (c == 0x7f || c == '\b') {   /* Backspace / DEL */
            if (g_len_in > 0) {
                g_len_in--;
                g_buf_in[g_len_in] = '\0';
                printf("\033[%d;1H\033[K> %s", g_rows, g_buf_in);
                fflush(stdout);
            }
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f
                   && g_len_in < (int)sizeof(g_buf_in) - 1) {
            /* 可打印 ASCII：追加并完整重绘输入行
             * 不用 putchar(c)，避免 ConPTY 双重回显 */
            g_buf_in[g_len_in++] = c;
            g_buf_in[g_len_in]   = '\0';
            printf("\033[%d;1H\033[K> %s", g_rows, g_buf_in);
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
        printf("\033[%d;1H\033[K> %s", g_rows, g_buf_in);
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
    printf("%s\n", ping_msg(MSG_PING_OPT_LAN_PUNCH));
    printf("%s\n", ping_msg(MSG_PING_OPT_VERBOSE_PUNCH));
    printf("%s\n", ping_msg(MSG_PING_OPT_ECHO));
    printf("%s\n", ping_msg(MSG_PING_OPT_CN));
}

static const char* state_name(p2p_state_t state) {
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

static void log_state_change(p2p_handle_t s) {
    static int last_state = -1;
    int state = p2p_state(s);
    if (state != last_state) {
        if (g_tui_active) {
            char line[128];
            snprintf(line, sizeof(line), ping_msg(MSG_PING_STATE_CHANGE),
                     state_name(last_state), last_state,
                     state_name(state), state);
            tui_println(line);
        } else {
            printf(ping_msg(MSG_PING_STATE_CHANGE),
                   state_name(last_state), last_state,
                   state_name(state), state);
            printf("\n");
            fflush(stdout);
        }
        last_state = state;
    }
}

/* 连接断开回调 */
static void on_disconnected(p2p_handle_t s, void *userdata) {
    (void)s; (void)userdata;
    if (g_tui_active) {
        tui_println(ping_msg(MSG_PING_CHAT_DISCONNECT));
    } else {
        printf("%s\n", ping_msg(MSG_PING_DISCONNECTED));
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* UTF-8 输出，支持中文显示 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    int use_dtls = 0, use_openssl = 0, use_pseudo = 0, use_compact = 0;
    int disable_lan = 0, lan_punch = 0, skip_host = 0, verbose_punch = 0, use_chinese = 0, show_help = 0;
    const char *server_ip = NULL, *gh_token = NULL, *gist_id = NULL;
    const char *my_name = "unnamed", *target_name = NULL;
    const char *turn_server = NULL, *turn_user = NULL, *turn_pass = NULL;
    int server_port = 8888;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--dtls")          == 0) use_dtls = 1;
        else if (strcmp(argv[i], "--openssl")        == 0) use_openssl = 1;
        else if (strcmp(argv[i], "--pseudo")         == 0) use_pseudo = 1;
        else if (strcmp(argv[i], "--compact")        == 0) use_compact = 1;
        else if (strcmp(argv[i], "--disable-lan")    == 0) disable_lan = 1;
        else if (strcmp(argv[i], "--lan-punch")      == 0) lan_punch = 1;
        else if (strcmp(argv[i], "--public-only")    == 0) skip_host = 1;
        else if (strcmp(argv[i], "--verbose-punch")  == 0) verbose_punch = 1;
        else if (strcmp(argv[i], "--verbose")        == 0) verbose = 1;
        else if (strcmp(argv[i], "--cn")             == 0) use_chinese = 1;
        else if (strcmp(argv[i], "--echo")           == 0) g_echo_mode = 1;
        else if (strcmp(argv[i], "--server") == 0 && i+1 < argc) server_ip  = argv[++i];
        else if (strcmp(argv[i], "--github") == 0 && i+1 < argc) gh_token   = argv[++i];
        else if (strcmp(argv[i], "--gist")   == 0 && i+1 < argc) gist_id    = argv[++i];
        else if (strcmp(argv[i], "--name")   == 0 && i+1 < argc) my_name    = argv[++i];
        else if (strcmp(argv[i], "--to")     == 0 && i+1 < argc) target_name = argv[++i];
        else if (strcmp(argv[i], "--turn")      == 0 && i+1 < argc) turn_server = argv[++i];
        else if (strcmp(argv[i], "--turn-user") == 0 && i+1 < argc) turn_user   = argv[++i];
        else if (strcmp(argv[i], "--turn-pass") == 0 && i+1 < argc) turn_pass   = argv[++i];
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
                server_port  = (int)strtol(colon + 1, NULL, 10);
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
    cfg.turn_server    = turn_server;
    cfg.turn_port      = turn_server ? 3478 : 0;
    cfg.turn_user      = turn_user;
    cfg.turn_pass      = turn_pass;
    cfg.server_host    = server_host;
    cfg.server_port    = server_port;
    cfg.gh_token       = gh_token;
    cfg.gist_id        = gist_id;
    cfg.bind_port      = 0;
    cfg.language       = use_chinese ? P2P_LANG_ZH : P2P_LANG_EN;
    cfg.disable_lan_shortcut = disable_lan;
    cfg.lan_punch            = lan_punch;
    cfg.skip_host_candidates = skip_host;
    cfg.verbose_nat_punch    = verbose_punch;
    cfg.on_disconnected      = on_disconnected;
    cfg.userdata             = NULL;
    strncpy(cfg.local_peer_id, my_name, P2P_PEER_ID_MAX - 1);

    if (server_ip)
        cfg.signaling_mode = cfg.use_ice ? P2P_SIGNALING_MODE_RELAY : P2P_SIGNALING_MODE_COMPACT;
    else if (gh_token && gist_id)
        cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;

    p2p_handle_t hdl = p2p_create(&cfg);
    if (!hdl) { printf("%s\n", ping_msg(MSG_PING_CREATE_FAIL)); return 1; }

    const char *mode_name = NULL;
    if      (server_ip)         mode_name = cfg.use_ice ? "ICE" : "COMPACT";
    else if (gh_token && gist_id) mode_name = "PUBSUB";
    else {
        printf("%s\n%s\n", ping_msg(MSG_PING_NO_MODE), ping_msg(MSG_PING_USE_ONE_OF));
        print_help(argv[0]);
        return 1;
    }

    if (disable_lan)    printf("%s\n", ping_msg(MSG_PING_LAN_DISABLED));
    if (lan_punch)      printf("%s\n", ping_msg(MSG_PING_LAN_PUNCH));
    if (verbose_punch)  printf("%s\n", ping_msg(MSG_PING_VERBOSE_ENABLED));
    if (g_echo_mode)    printf("%s\n", ping_msg(MSG_PING_CHAT_ECHO_ON));

    if (p2p_connect(hdl, target_name) < 0) {
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
    for(;;) {

        p2p_update(hdl);
        log_state_change(hdl);

        if (p2p_is_ready(hdl)) {
            /* 首次连接成功：初始化 TUI，降低日志等级 */
            if (!g_first_connect_done) {
                g_first_connect_done = 1;
                printf("%s\n", ping_msg(MSG_PING_CHAT_ENTER));
                fflush(stdout);
                tui_init();
                p2p_set_log_level(verbose ? P2P_LOG_LEVEL_DEBUG : P2P_LOG_LEVEL_WARN);
                tui_println(ping_msg(MSG_PING_CHAT_CONNECTED));
            }

            /* 接收对端消息 */
            char data[512] = {0};
            int r = p2p_recv(hdl, data, (int)sizeof(data) - 1);
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
                    p2p_send(hdl, echo_msg, (int)strlen(echo_msg));
                }
            }

            /* 处理键盘输入 */
            tui_process_input(hdl);
        }

        // 间隔 ms
#ifdef _WIN32
        Sleep((DWORD)10);
#else
        usleep((unsigned int)10 * 1000);
#endif
    }

    tui_cleanup();
    return 0;
}
