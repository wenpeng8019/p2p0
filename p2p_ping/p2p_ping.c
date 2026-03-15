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

#define MOD_TAG "P2P_PING"

#include <stdc.h>
#include <p2p.h>

#include "LANG.h"
#include "LANG.cn.h"

// 命令行参数定义（使用 stdc ARGS 宏）
ARGS_B(false, dtls,         0,   "dtls",         LA_CS("Enable DTLS (MbedTLS)", LA_S15, 15));
ARGS_B(false, openssl,      0,   "openssl",      LA_CS("Enable DTLS (OpenSSL)", LA_S16, 16));
ARGS_B(false, pseudo,       0,   "pseudo",       LA_CS("Enable PseudoTCP", LA_S17, 17));
ARGS_B(false, compact,      0,   "compact",      LA_CS("Use COMPACT mode (UDP signaling, default is ICE/TCP)", LA_S28, 28));
ARGS_B(false, disable_lan,  0,   "disable-lan",  LA_CS("Disable LAN shortcut (force NAT punch test)", LA_S14, 14));
ARGS_B(false, public_only,  0,   "public-only",  LA_CS("Skip host candidates", LA_S22, 22));
ARGS_B(false, cn,           0,   "cn",           LA_CS("Use Chinese language", LA_S27, 27));
ARGS_B(false, echo,         0,   "echo",         LA_CS("Auto-echo received messages back to sender", LA_S13, 13));
ARGS_S(false, server,       's', "server",       LA_CS("Signaling server IP[:PORT]", LA_S21, 21));
ARGS_S(false, github,       0,   "github",       LA_CS("GitHub Token for Public Signaling", LA_S19, 19));
ARGS_S(false, gist,         0,   "gist",         LA_CS("GitHub Gist ID for Public Signaling", LA_S18, 18));
ARGS_S(false, name,         'n', "name",         LA_CS("Your Peer Name", LA_S29, 29));
ARGS_S(false, to,           't', "to",           LA_CS("Target Peer Name (if specified: active role)", LA_S23, 23));
ARGS_S(false, turn,         0,   "turn",         LA_CS("TURN server address", LA_S25, 25));
ARGS_S(false, turn_user,    0,   "turn-user",    LA_CS("TURN username", LA_S26, 26));
ARGS_S(false, turn_pass,    0,   "turn-pass",    LA_CS("TURN password", LA_S24, 24));
ARGS_I(false, log,          'l', "log",          LA_CS("Log level (0-5)", LA_S20, 20));

/*
 * TUI 专有头文件（不适合移植到 p2p_platform.h，原因见下）：
 *
 * 【Windows】<conio.h>
 *   - _kbhit():  非阻塞键盘输入检测（仅真实控制台有效，ConPTY 需用 PeekNamedPipe）
 *   - _getch():  逐字符读取（无回显）
 *   用途：TUI 输入循环中检测和读取按键
 *
 * 【POSIX】<termios.h>
 *   - tcgetattr() / tcsetattr():  终端模式控制（设置 raw 模式，禁用 ICANON 和 ECHO）
 *   - termios 结构体:  包含 c_lflag, c_cc 等多字段配置
 *   用途：TUI 初始化时将 stdin 切换为逐字符输入模式
 *
 * 【结论】这些 API 高度应用相关，平台间语义差异大，不适合通用封装
 *         （终端尺寸获取已统一封装为 p2p_get_terminal_rows/cols）
 */
#ifdef _WIN32
#include <conio.h>     /* _kbhit, _getch */
#else
#include <termios.h>    /* termios, tcgetattr, tcsetattr */
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

/* 注：终端尺寸获取已移植到 p2p_platform.h，使用 p2p_get_terminal_rows() */

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
static void tui_log_callback(p2p_log_level_t level, const char *module, const char *message) {
    const char *lvl = "?????";
    switch (level) {
        case P2P_LOG_LEVEL_ERROR: lvl = "ERROR"; break;
        case P2P_LOG_LEVEL_WARN:  lvl = "WARN"; break;
        case P2P_LOG_LEVEL_INFO:  lvl = "INFO"; break;
        case P2P_LOG_LEVEL_DEBUG: lvl = "DEBUG"; break;
        case P2P_LOG_LEVEL_VERBOSE: lvl = "VERBOSE"; break;
        default: break;
    }
    char line[1024 + 64];
    if (module && module[0])
        snprintf(line, sizeof(line), "[%s] [%s] %s", lvl, module, message);
    else
        snprintf(line, sizeof(line), "[%s] %s", lvl, message);
    tui_println(line);
}

/* 初始化 TUI（连接建立后调用一次） */
static void tui_init(void) {
    /* 非交互终端（stdout 被重定向）时跳过 TUI，避免后台进程触发 SIGTTOU */
    if (!P_isatty(stdout)) return;
    g_rows = P_term_rows();

#if P_WIN
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
        SetConsoleMode(hin, (g_orig_in_mode | ENABLE_VIRTUAL_TERMINAL_INPUT) & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    }
#endif

    /* 将 p2p_log 输出重定向到 TUI 回调 */
    p2p_set_log_output(tui_log_callback);

    /* 设置 ANSI 滚动区域：行 1 ~ rows-1（VT 已启用后再发送）*/
    printf("\033[1;%dr", g_rows - 1);
    /* 清空输入行并显示提示符 */
    printf("\033[%d;1H\033[K> ", g_rows);
    fflush(stdout);

#if !P_WIN
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

    // 清除日志回调，恢复 p2p_log 默认输出（stdout）
    p2p_set_log_output(NULL);

    // 重置滚动区域，光标移到最后一行
    printf("\033[r");
    printf("\033[%d;1H\n", g_rows);
    fflush(stdout);

#if P_WIN
    if (!g_win_pty_mode)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), g_orig_in_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_orig_out_mode);
#else
    // 恢复终端模式
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
    fcntl(STDIN_FILENO, F_SETFL, 0);
#endif
}

/* 处理 stdin 按键，维护输入缓冲，回车时发送 */
static void tui_process_input(p2p_handle_t hdl) {

    if (!g_tui_active) return;  /* 非交互模式（重定向/后台）跳过 stdin 读取 */
    for (;;) {
        int ch;
#if P_WIN
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
#if !P_WIN
static void on_sigwinch(int sig) {
    (void)sig;
    if (!g_tui_active) return;
    int new_rows = P_term_rows();
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

static const char* state_name(p2p_state_t state) {
    switch (state) {
        case P2P_STATE_INIT:        return LA_W("INIT", LA_W5, 5);
        case P2P_STATE_REGISTERING: return LA_W("REGISTERING", LA_W7, 7);
        case P2P_STATE_PUNCHING:    return LA_W("PUNCHING", LA_W6, 6);
        case P2P_STATE_CONNECTED:   return LA_W("CONNECTED", LA_W3, 3);
        case P2P_STATE_RELAY:       return LA_W("RELAY", LA_W8, 8);
        case P2P_STATE_CLOSING:     return LA_W("CLOSING", LA_W2, 2);
        case P2P_STATE_CLOSED:      return LA_W("CLOSED", LA_W1, 1);
        case P2P_STATE_ERROR:       return LA_W("ERROR", LA_W4, 4);
        default:                    return LA_W("UNKNOWN", LA_W9, 9);
    }
}

static void log_state_change(p2p_handle_t s) {
    static int last_state = -1;
    int state = p2p_state(s);
    if (state != last_state) {
        if (g_tui_active) {
            char line[128];
            snprintf(line, sizeof(line), LA_F("[STATE] %s (%d) -> %s (%d)", LA_F38, 38),
                     state_name(last_state), last_state,
                     state_name(state), state);
            tui_println(line);
        } else {
            printf(LA_F("[STATE] %s (%d) -> %s (%d)", LA_F38, 38),
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
        tui_println(LA_S("--- Peer disconnected ---", LA_S11, 11));
    } else {
        printf("%s\n", LA_S("[EVENT] Connection closed", LA_S12, 12));
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* UTF-8 输出，支持中文显示 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    /* 初始化语言系统 */
    LA_init();

    // 设置语言钩子
    P_lang = lang_cstr;

    /* 预扫描 --cn 参数（在 ARGS_parse 之前加载中文，使 --help 也能显示中文） */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cn") == 0) {
            lang_cn();
            break;
        }
    }

    /* 解析命令行参数 */
    ARGS_parse(argc, argv,
        &ARGS_DEF_dtls,
        &ARGS_DEF_openssl,
        &ARGS_DEF_pseudo,
        &ARGS_DEF_compact,
        &ARGS_DEF_disable_lan,
        &ARGS_DEF_public_only,
        &ARGS_DEF_cn,
        &ARGS_DEF_echo,
        &ARGS_DEF_server,
        &ARGS_DEF_github,
        &ARGS_DEF_gist,
        &ARGS_DEF_name,
        &ARGS_DEF_to,
        &ARGS_DEF_turn,
        &ARGS_DEF_turn_user,
        &ARGS_DEF_turn_pass,
        &ARGS_DEF_log,
        NULL);

    /* 设置日志级别 */
    if (ARGS_log.i64) p2p_set_log_level((int)ARGS_log.i64);

    /* Echo 模式 */
    g_echo_mode = ARGS_echo.i64 ? 1 : 0;

    /* 名称 */
    const char *my_name = ARGS_name.str ? ARGS_name.str : "unnamed";
    const char *target_name = ARGS_to.str;
    g_my_name = my_name;

    print("I:", LA_F("=== P2P Ping Diagnostic Tool ===\n", LA_F30, 30));

    /* 解析 IP:PORT 格式 */
    int server_port = 9333;
    char server_host_buf[256] = {0};
    const char *server_host = ARGS_server.str;
    if (ARGS_server.str) {
        const char *colon = strchr(ARGS_server.str, ':');
        if (colon) {
            size_t len = (size_t)(colon - ARGS_server.str);
            if (len < sizeof(server_host_buf)) {
                memcpy(server_host_buf, ARGS_server.str, len);
                server_host_buf[len] = '\0';
                server_host  = server_host_buf;
                server_port  = (int)strtol(colon + 1, NULL, 10);
            }
        }
    }

    /* DTLS backend: 1=MbedTLS, 2=OpenSSL */
    int dtls_backend = ARGS_dtls.i64 ? 1 : (ARGS_openssl.i64 ? 2 : 0);

    p2p_config_t cfg = {0};
    cfg.dtls_backend   = dtls_backend;
    cfg.use_pseudotcp  = ARGS_pseudo.i64 ? 1 : 0;
    cfg.use_ice        = !ARGS_compact.i64;
    cfg.stun_server    = "stun.l.google.com";
    cfg.stun_port      = 3478;
    cfg.turn_server    = ARGS_turn.str;
    cfg.turn_port      = ARGS_turn.str ? 3478 : 0;
    cfg.turn_user      = ARGS_turn_user.str;
    cfg.turn_pass      = ARGS_turn_pass.str;
    cfg.server_host    = server_host;
    cfg.server_port    = server_port;
    cfg.gh_token       = ARGS_github.str;
    cfg.gist_id        = ARGS_gist.str;
    cfg.bind_port      = 0;
    cfg.language       = ARGS_cn.i64 ? P2P_LANG_ZH : P2P_LANG_EN;
    cfg.skip_host_candidates = ARGS_public_only.i64 ? 1 : 0;
    cfg.on_disconnected      = on_disconnected;
    cfg.userdata             = NULL;

    if (ARGS_server.str)
        cfg.signaling_mode = cfg.use_ice ? P2P_SIGNALING_MODE_RELAY : P2P_SIGNALING_MODE_COMPACT;
    else if (ARGS_github.str && ARGS_gist.str)
        cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;

    p2p_handle_t hdl = p2p_create(my_name, &cfg);
    if (!hdl) { print("E:", LA_F("Failed to create sessions\n", LA_F31, 31)); return 1; }

    const char *mode_name = NULL;
    if (ARGS_server.str) mode_name = cfg.use_ice ? "RELAY" : "COMPACT";
    else if (ARGS_github.str && ARGS_gist.str) mode_name = "PUBSUB";
    else {
        print("E:", LA_F("No signaling mode.\nUse --server or --github\n" ,LA_F33, 33));
        ARGS_print(argv[0]);
        return 1;
    }

    if (ARGS_disable_lan.i64) print("I:", LA_F("[TEST] LAN shortcut disabled - forcing NAT punch\n", LA_F39, 39));
    if (g_echo_mode)          print("I:", LA_F("[Chat] Echo mode enabled: received messages will be echoed back.\n", LA_F36, 36));

    if (p2p_connect(hdl, target_name) < 0) {
        print("E:", LA_F("Failed to initialize connection\n", LA_F32, 32));
        return 1;
    }

    if (target_name) { print("I:", LA_F("Running in %s mode (connecting to %s)...", LA_F34, 34), mode_name, target_name); }
    else             { print("I:", LA_F("Running in %s mode (waiting for connection)...", LA_F35, 35), mode_name); }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
#if !P_WIN
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
                print("I:", LA_F("[Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n", LA_F37, 37));
                tui_init();
                tui_println(LA_S("--- Connected ---", LA_S10, 10));
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
