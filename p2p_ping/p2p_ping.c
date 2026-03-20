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

// 命令行参数定义
ARGS_B(false, dtls,         0,   "dtls",         LA_CS("Enable DTLS (MbedTLS)", LA_S15, 15));
ARGS_B(false, openssl,      0,   "openssl",      LA_CS("Enable DTLS (OpenSSL)", LA_S16, 16));
ARGS_B(false, pseudo,       0,   "pseudo",       LA_CS("Enable PseudoTCP", LA_S17, 17));
ARGS_B(false, compact,      'c', "compact",      LA_CS("Use COMPACT mode (UDP signaling, default is ICE/TCP)", LA_S28, 28));
ARGS_B(false, echo,         0,   "echo",         LA_CS("Auto-echo received messages back to sender", LA_S13, 13));
ARGS_S(false, server,       's', "server",       LA_CS("Signaling server IP[:PORT]", LA_S21, 21));
ARGS_S(false, github,       0,   "github",       LA_CS("GitHub Token for Public Signaling", LA_S19, 19));
ARGS_S(false, gist,         0,   "gist",         LA_CS("GitHub Gist ID for Public Signaling", LA_S18, 18));
ARGS_S(false, name,         'n', "name",         LA_CS("Your Peer Name", LA_S29, 29));
ARGS_S(false, to,           't', "to",           LA_CS("Target Peer Name (if specified: active role)", LA_S23, 23));
ARGS_S(false, stun,         0,   "stun",         LA_CS("STUN server address", LA_S44, 44));
ARGS_S(false, turn,         0,   "turn",         LA_CS("TURN server address", LA_S25, 25));
ARGS_S(false, turn_user,    0,   "turn-user",    LA_CS("TURN username", LA_S26, 26));
ARGS_S(false, turn_pass,    0,   "turn-pass",    LA_CS("TURN password", LA_S24, 24));
ARGS_I(false, log,          'l', "log",          LA_CS("Log level (0-5)", LA_S20, 20));
ARGS_S(false, debugger,     0,   "debugger",     LA_CS("Debugger Name", LA_S40, 40));

static p2p_language_t s_lang = P2P_LANG_EN;
static void cb_cn(const char* argv) { (void)argv;  s_lang = P2P_LANG_CN; lang_cn(); }
ARGS_PRE(cb_cn, cn,         0,   "cn",           LA_CS("Use Chinese language", LA_S27, 27));

#undef LOG_CALLBACK
#define LOG_CALLBACK    p2p_log_callback
#undef LOG_LEVEL
#define LOG_LEVEL       p2p_log_level
#undef LOG_TAG_P
#define LOG_TAG_P       p2p_log_pre_tag

#undef printf

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

static int              g_term_height  = 0;         /* 终端行数， 0 表示未初始化 */
static P_term_ctx_t     g_term_ctx;                 /* 终端原始状态（P_term_init 保存） */
static char             g_buf_in[512] = {0};        /* 当前输入缓冲 */
static int              g_len_in      = 0;          /* 输入缓冲长度 */
static const char*      g_my_name = "me";           /* 本端显示名 */

static void tui_println(p2p_log_level_t level, const char* line) {
    (void)level;

    /* 非交互模式：输出到 stdout 并通过 instrument 广播（供测试验证） */
    if (!g_term_height) {
        printf("%s\n", line);
        fflush(stdout);
        print("I:", "%s", line);  /* 通过 instrument 广播，供测试程序验证 */
        return;
    }

    // 将日志（滚动）区域向上滚动一行
    // > 先将光标移动到滚动区（即除了输入行的区域）的最后一行
    // > 发送 \n 触发滚动区整体向上滚动一行，光标仍停在最后一行（即输入行上方）
    // > 发送 \r 将光标移到行首
    // > P_CLEAR_EOL 清除当前行（行首光标后面的）内容
    printf(P_CURSOR_SAVE);                                      /* save cursor */
    printf(P_CURSOR_ROW "\n\r" P_CLEAR_EOL, g_term_height - 1);                           
    switch (level) {
        case P2P_LOG_LEVEL_FATAL: printf(P_PURPLE("%s"), line); break;
        case P2P_LOG_LEVEL_ERROR: printf(P_RED("%s"), line); break;
        case P2P_LOG_LEVEL_WARN:  printf(P_YELLOW("%s"), line); break;
        case P2P_LOG_LEVEL_VERBOSE: printf(P_GRAY("%s"), line); break;
        case P2P_LOG_LEVEL_DEBUG: printf(P_CYAN("%s"), line); break;
        default: printf("%s", line); break;
    }
    printf(P_CURSOR_RESTORE);

    // 将输入行内容重新绘制一遍，避免偶发的脏屏（如 ConPTY 双重回显）
    printf(P_CURSOR_ROW P_CLEAR_EOL "> %s", g_term_height, g_buf_in);
    fflush(stdout);
}

/* stdc日志回调：重定向 print() 输出到 TUI */
static void tui_log_callback(p2p_log_level_t level, const char* tag, char *txt, int len) {
    (void)tag;
    while (len > 0 && (txt[len - 1] == '\n' || txt[len - 1] == '\r')) txt[--len] = '\0';
    tui_println(level, txt);
}

static void tui_on_resize(void) {
    int h = P_term_rows(&g_term_ctx);
    if (h == g_term_height) return;
    g_term_height = h;
    printf(P_SCROLL_SET, 1, g_term_height - 1);
    printf(P_CURSOR_ROW P_CLEAR_EOL "> %s", g_term_height, g_buf_in);
    fflush(stdout);
}

/* SIGWINCH：终端窗口大小变化，更新滚动区域（仅 Unix） */
#if !P_WIN
static void on_sigwinch(int sig) { (void)sig;
    if (g_term_height) tui_on_resize();
}
#endif

/* 初始化 TUI（连接建立后调用一次） */
static void tui_init(void) {

    if (g_term_height) return;
    if (!P_term_init(&g_term_ctx)) return;  /* 非终端（管道/重定向）→ 跳过 */
    g_term_height = P_term_rows(&g_term_ctx);
    if (!g_term_height) return;

#if !P_WIN
    signal(SIGWINCH, on_sigwinch);
#endif

    // 设置终端    
    printf(P_SCROLL_SET, 1, g_term_height - 1);            // 设置日志滚动区域：行 1 ~ rows-1
    printf(P_CURSOR_ROW P_CLEAR_EOL "> ", g_term_height);  // 清空输入行并显示提示符
    fflush(stdout);

    // 将日志输出重定向到 TUI 回调
    p2p_log_callback = tui_log_callback;
    instrument_loggable((log_cb) tui_log_callback);
}

/* 退出 TUI，恢复终端状态 */
static void tui_cleanup(void) {

    if (!g_term_height) return;
    int rows = g_term_height;
    g_term_height = 0;

    // 清除日志回调，恢复默认输出（stdout）
    p2p_log_callback = (p2p_log_callback_t)-1;
    instrument_loggable((log_cb)-1);

#if !P_WIN
    signal(SIGWINCH, SIG_DFL);
#endif

    // 重置滚动区域，光标移到最后一行
    printf(P_SCROLL_RESET);
    printf(P_CURSOR_ROW "\n", rows);
    fflush(stdout);

    P_term_final(&g_term_ctx);
}

/* 处理 stdin 按键，维护输入缓冲，回车时发送 */
static void tui_process_input(p2p_handle_t hdl) {

    if (!g_term_height) return;  /* 非交互模式（重定向/后台）跳过 stdin 读取 */

#if P_WIN
    tui_on_resize();
#endif

    for (;;) {
        int ch = P_term_input(&g_term_ctx);
        if (ch < 0) break;
        char c = (char)ch;
        if (c == '\r' || c == '\n') {
            if (g_len_in > 0) {
                g_buf_in[g_len_in] = '\0';
                
                /* 在滚动区显示自己发出的消息 */
                char line[576];
                snprintf(line, sizeof(line), "%s: %s", g_my_name, g_buf_in);
                tui_println(P2P_LOG_LEVEL_INFO, line);

                /* 发送 */
                p2p_send(hdl, g_buf_in, g_len_in);

                /* 清空输入行 */
                g_buf_in[g_len_in = 0] = '\0';
                printf(P_CURSOR_ROW P_CLEAR_EOL "> ", g_term_height);
                fflush(stdout);
            }
        } 
        else if (c == 0x7f || c == '\b') {   /* Backspace / DEL */
            
            if (g_len_in > 0) { g_buf_in[--g_len_in] = '\0';
                printf(P_CURSOR_ROW P_CLEAR_EOL "> %s", g_term_height, g_buf_in);
                fflush(stdout);
            }
        }
        else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f
                 && g_len_in < (int)sizeof(g_buf_in) - 1) {

            /* 可打印 ASCII：追加并完整重绘输入行
             * 不用 putchar(c)，避免 ConPTY 双重回显 */
            g_buf_in[g_len_in++] = c;
            g_buf_in[g_len_in]   = '\0';
            printf(P_CURSOR_ROW P_CLEAR_EOL "> %s", g_term_height, g_buf_in);
            fflush(stdout);
        }

        /* 忽略方向键等控制序列 */
    }
}

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
        case P2P_STATE_CLOSED:      return LA_W("CLOSED", LA_W1, 1);
        case P2P_STATE_ERROR:       return LA_W("ERROR", LA_W4, 4);
        default:                    return LA_W("UNKNOWN", LA_W9, 9);
    }
}

static void log_state_change(p2p_handle_t s) {
    static int last_state = -1;
    int state = p2p_state(s);
    if (state != last_state) {
        print("I:", LA_F("[STATE] %s (%d) -> %s (%d)\n", LA_F45, 45),
              state_name(last_state), last_state,
              state_name(state), state);
        last_state = state;
    }
}

/* 状态变化回调 */
static void on_state(p2p_handle_t s, p2p_state_t old_state, p2p_state_t new_state, void *userdata) {
    (void)s; (void)userdata;
    print("I:", LA_F("[EVENT] State: %s -> %s\n", LA_F46, 46), state_name(old_state), state_name(new_state));
}

static bool             g_running;
static bool             g_connected_once = false;
static p2p_handle_t     g_hdl = NULL;           /* 全局句柄，供 instrument 回调使用 */

/* SIGINT / SIGTERM：优雅退出（跨平台） */
static void on_signal(int sig) { (void)sig;
    g_running = false;
}

/* ============================================================================
 * Instrument 被控接口
 *
 * 支持远程调用（通过 instrument_req）：
 *   - msg="send", content="<text>" : 发送消息给对端
 *   - msg="state"                   : 返回当前连接状态
 *   - msg="quit"                    : 退出程序
 * ============================================================================ */
static void on_instrument(uint16_t rid, uint8_t chn, const char* msg, char *content, int len) {
    (void)len;

    /* msg==NULL 表示普通日志/WAIT，不是 REQ 请求 */
    if (msg == NULL || chn != INSTRUMENT_CTRL) return;

    print("D:", "[CTRL] rid=%u msg=%s content=%.*s", rid, msg, len, content ? content : "");

    /* send:<message> - 发送消息给对端 */
    if (strcmp(msg, "send") == 0) {
        if (g_hdl && p2p_is_ready(g_hdl) && content && len > 0) {
            p2p_send(g_hdl, content, len);
            char line[576];
            snprintf(line, sizeof(line), "%s: %s", g_my_name, content);
            tui_println(P2P_LOG_LEVEL_INFO, line);
            instrument_resp(rid, "ok");
        } else {
            print("W:", "[CTRL] send failed: hdl=%p ready=%d content=%p len=%d",
                  (void*)g_hdl, g_hdl ? p2p_is_ready(g_hdl) : -1, (void*)content, len);
            instrument_resp(rid, "not_ready");
        }
        return;
    }

    /* state - 返回连接状态 */
    if (strcmp(msg, "state") == 0) {
        int st = g_hdl ? p2p_state(g_hdl) : -1;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", st);
        instrument_resp(rid, buf);
        return;
    }

    /* quit - 退出程序 */
    if (strcmp(msg, "quit") == 0) {
        instrument_resp(rid, "bye");
        g_running = false;
        return;
    }

    /* 未知命令 */
    instrument_resp(rid, "unknown");
}

int main(int argc, char *argv[]) {

    /* 初始化语言系统 */
    LA_init();

    /* 设置语言钩子 */
    P_lang = lang_cstr;

    /* 解析命令行参数 */
    ARGS_parse(argc, argv,
        &ARGS_DEF_cn,
        &ARGS_DEF_dtls,
        &ARGS_DEF_openssl,
        &ARGS_DEF_pseudo,
        &ARGS_DEF_compact,
        &ARGS_DEF_echo,
        &ARGS_DEF_server,
        &ARGS_DEF_github,
        &ARGS_DEF_gist,
        &ARGS_DEF_name,
        &ARGS_DEF_to,
        &ARGS_DEF_stun,
        &ARGS_DEF_turn,
        &ARGS_DEF_turn_user,
        &ARGS_DEF_turn_pass,
        &ARGS_DEF_log,
        &ARGS_DEF_debugger,
        NULL);

    /* 设置日志级别 */
    if (ARGS_log.i64) {
        p2p_log_level = (p2p_log_level_t)ARGS_log.i64;
    }

    p2p_log_pre_tag = true;  /* 日志前置标签，显示在日志内容前面（而非行首） */

    /* 名称 */
    const char *my_name = ARGS_name.str ? ARGS_name.str : "unnamed";
    const char *target_name = ARGS_to.str;
    g_my_name = my_name;

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
    cfg.language        = s_lang;
    cfg.dtls_backend    = dtls_backend;
    cfg.use_pseudotcp   = ARGS_pseudo.i64 ? 1 : 0;
    cfg.use_ice         = !ARGS_compact.i64;
    cfg.stun_server     = ARGS_stun.str ? ARGS_stun.str : "stun.miwifi.com"; // 国内 STUN 服务器（小米）
    //cfg.stun_server     = "stun.cloudflare.com";
    //cfg.stun_server     = "stun.qq.com"; // 国内 STUN 服务器（腾讯）
    //cfg.stun_server     = "stun.l.google.com";
    cfg.stun_port       = 3478;
    cfg.turn_server     = ARGS_turn.str;
    cfg.turn_port       = ARGS_turn.str ? 3478 : 0;
    cfg.turn_user       = ARGS_turn_user.str;
    cfg.turn_pass       = ARGS_turn_pass.str;
    cfg.server_host     = server_host;
    cfg.server_port     = server_port;
    cfg.gh_token        = ARGS_github.str;
    cfg.gist_id         = ARGS_gist.str;
    cfg.bind_port       = 0;
    cfg.on_state        = on_state;
    cfg.userdata        = NULL;

    #ifndef NDEBUG
    p2p_instrument_base = 10;
    //instrument_loggable((log_cb)-1);
    #endif

    if (ARGS_server.str)
        cfg.signaling_mode = cfg.use_ice ? P2P_SIGNALING_MODE_RELAY : P2P_SIGNALING_MODE_COMPACT;
    else if (ARGS_github.str && ARGS_gist.str)
        cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;

    print("I:", LA_F("=== P2P Ping Diagnostic Tool ===\n", LA_F30, 30));

    if (ARGS_debugger.str) {
        /* 
         * 启用 instrument 被控模式：
         * 1. 先注册回调 + id 以接收 REQ 和发送 WAIT
         * 2. 等待 debugger 连接（WAIT/CONTINUE 交互）
         * 注：p2p_create 会调用 instrument_listen(cb, NULL) 覆盖回调，
         *     所以需要在 p2p_create 后再次注册
         */
        instrument_listen(on_instrument, ARGS_debugger.str);

        print("I:", LA_F("Waiting Debugger(%s) connecting...\n", LA_F43, 43), ARGS_debugger.str);

        ret_t r = instrument_wait(my_name, ARGS_debugger.str, 60 * 1000);
        if (r == E_NONE) {
            print("I:", LA_F("Debugger connected, resuming execution.\n", LA_F41, 41));
        } else {
            print("W:", LA_F("Timeout waiting for debugger. Continuing without debugger.\n", LA_F42, 42));
        }
    }

    p2p_handle_t hdl = p2p_create(my_name, &cfg);
    if (!hdl) { print("E:", LA_F("Failed to create sessions\n", LA_F31, 31)); return 1; }
    g_hdl = hdl;  /* 供 instrument 回调使用 */

    /* 重新注册 instrument 回调（p2p_create 会覆盖之前的设置） */
    if (ARGS_debugger.str) {
        instrument_listen(on_instrument, ARGS_debugger.str);
    }

    const char *mode_name = NULL;
    if (ARGS_server.str) mode_name = cfg.use_ice ? "RELAY" : "COMPACT";
    else if (ARGS_github.str && ARGS_gist.str) mode_name = "PUBSUB";
    else {
        print("E:", LA_F("No signaling mode.\nUse --server or --github\n" ,LA_F33, 33));
        ARGS_print(argv[0]);
        return 1;
    }

    // if (ARGS_disable_lan.i64) print("I:", LA_F("[TEST] LAN shortcut disabled - forcing NAT punch\n", LA_F39, 39));
    if (ARGS_echo.i64) print("I:", LA_F("[Chat] Echo mode enabled: received messages will be echoed back.\n", LA_F36, 36));

    if (p2p_connect(hdl, target_name) < 0) {
        print("E:", LA_F("Failed to initialize connection\n", LA_F32, 32));
        return 1;
    }

    if (target_name) { print("I:", LA_F("Running in %s mode (connecting to %s)...", LA_F34, 34), mode_name, target_name); }
    else             { print("I:", LA_F("Running in %s mode (waiting for connection)...", LA_F35, 35), mode_name); }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    g_running = true;

    /* ---- 主循环 ---- */
    while(g_running) {

        p2p_update(hdl);
        log_state_change(hdl);

        if (p2p_is_ready(hdl)) {

            /* 首次连接成功：初始化 TUI，降低日志等级 */
            if (!g_connected_once) { g_connected_once = true;
                print("I:", LA_F("[Chat] Entering message mode. Type and press Enter to send. Ctrl+C to quit.\n", LA_F37, 37));
                tui_init();
                if (g_term_height)
                    tui_println(P2P_LOG_LEVEL_INFO, LA_S("--- Connected ---", LA_S10, 10));
            }

            /* 接收对端消息 */
            char data[512] = {0};
            int r = p2p_recv(hdl, data, (int)sizeof(data) - 1);
            if (r > 0) {

                data[r] = '\0';
                char line[576];
                const char *peer = target_name ? target_name : "peer";
                snprintf(line, sizeof(line), "%s: %s", peer, data);
                tui_println(P2P_LOG_LEVEL_INFO, line);

                /* echo 模式：不对已是 echo 的消息再次回复（防循环）*/
                if (ARGS_echo.i64 && strncmp(data, "[echo] ", 7) != 0) {
                    char echo_msg[520];
                    snprintf(echo_msg, sizeof(echo_msg), "[echo] %s", data);
                    p2p_send(hdl, echo_msg, (int)strlen(echo_msg));
                }
            }

            /* 处理键盘输入 */
            tui_process_input(hdl);
        }

        // 间隔 ms
        P_usleep(10 * 1000);
    }

    tui_cleanup();
    return 0;
}
