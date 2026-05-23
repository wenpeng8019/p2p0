/*
 * test_ws_browser.c — WebSocket 浏览器 SDP 通知交换演示
 *
 * 用法：
 *   ./test/test_ws_browser           （自动选端口）
 *   ./test/test_ws_browser 9100       （relay/ws 共用端口）
 *
 * 流程：
 *   1. 启动 p2p_server（--ws，共用 relay 端口）
 *   2. 生成 HTML 页面写到 /tmp/p2p_ws_test.html
 *   3. 自动打开浏览器（macOS: open, Linux: xdg-open）
 *   4. 浏览器自动 REG 为 bob_browser，并在收到 SDP 后回发 SDP
 *   5. C 端 ws_client REG 为 alice_native，向浏览器发送 SDP 通知
 *   6. 验证：alice 收到 SDP OK + 收到来自 bob_browser 的 SDP 回传
 *   7. 按 Enter 结束
 *
 * 注意：需要编译时开启 WITH_WS
 */

#ifdef WITH_WS

#include "../src/ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#ifndef _WIN32
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/wait.h>
#  include <errno.h>
#  include <sys/stat.h>
#endif

/* -----------------------------------------------------------------------
 * 工具
 * -------------------------------------------------------------------- */
static void sleep_ms(int ms) {
#ifndef _WIN32
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#else
    Sleep(ms);
#endif
}

static uint16_t pick_free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return 0; }
    socklen_t l = sizeof(a);
    getsockname(fd, (struct sockaddr *)&a, &l);
    uint16_t port = ntohs(a.sin_port);
    close(fd);
    return port;
}

/* -----------------------------------------------------------------------
 * 查找 p2p_server 二进制（与 test_ws_server_integration.c 一致）
 * -------------------------------------------------------------------- */
static const char *find_server_binary(void) {
    static const char *candidates[] = {
        "build/p2p_server/p2p_server",
        "p2p_server/p2p_server",
        "../p2p_server/p2p_server",
        "./p2p_server",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && S_ISREG(st.st_mode) && access(candidates[i], X_OK) == 0)
            return candidates[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * 启动 / 停止服务器子进程
 * -------------------------------------------------------------------- */
typedef struct { pid_t pid; } server_proc_t;

static server_proc_t start_server(const char *bin, uint16_t relay_port, uint16_t ws_port) {
    server_proc_t sp = { -1 };
    char relay_str[8];
    snprintf(relay_str, sizeof(relay_str), "%u", (unsigned)relay_port);
    (void)ws_port;

    pid_t pid = fork();
    if (pid < 0) return sp;
    if (pid == 0) {
        /* 子进程：将 stdout/stderr 重定向到 /dev/null */
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execlp(bin, bin, "-p", relay_str, "--ws", (char *)NULL);
        _exit(1);
    }
    sp.pid = pid;

    /* 等待 TCP 端口就绪（最多 2s）*/
    int ready = 0;
    for (int i = 0; i < 100; i++) {
        sleep_ms(20);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(relay_port);
        if (connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
            ready = 1;
            close(fd);
            break;
        }
        close(fd);
    }

    if (!ready) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        sp.pid = -1;
    }

    return sp;
}

static void stop_server(server_proc_t *sp) {
    if (sp->pid <= 0) return;
    kill(sp->pid, SIGTERM);
    int st;
    waitpid(sp->pid, &st, 0);
    sp->pid = -1;
}

/* -----------------------------------------------------------------------
 * 生成并写入 HTML 页面
 * -------------------------------------------------------------------- */
/* 页面使用深色主题，连接后实时显示收到的 WS 消息 */
static const char HTML_FMT[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>P2P WebSocket SDP Demo</title>\n"
    "<style>\n"
    "* { box-sizing:border-box; margin:0; padding:0 }\n"
    "body { font-family:'SF Mono',Consolas,monospace; background:#0d1117;\n"
    "       color:#c9d1d9; padding:28px }\n"
    "h1  { color:#58a6ff; font-size:1.4em; margin-bottom:18px }\n"
    "#bar { display:flex; align-items:center; gap:10px; margin-bottom:20px }\n"
    "#dot { width:11px; height:11px; border-radius:50%%;\n"
    "       background:#6e7681; transition:background .3s }\n"
    "#dot.ok  { background:#3fb950 }\n"
    "#dot.err { background:#f85149 }\n"
    "#info { color:#8b949e; font-size:.9em }\n"
    "#log  { list-style:none }\n"
    "#log li { padding:9px 14px; margin:5px 0;\n"
    "           background:#161b22;\n"
    "           border-left:3px solid #58a6ff;\n"
    "           border-radius:0 6px 6px 0;\n"
    "           animation:fadein .25s ease }\n"
    "@keyframes fadein { from{opacity:0;transform:translateX(-6px)}\n"
    "                    to  {opacity:1;transform:translateX(0)} }\n"
    "#log li .t { color:#6e7681; margin-right:10px; font-size:.82em }\n"
    "#log li .msg { color:#e6edf3 }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>&#127760; P2P WebSocket SDP Demo</h1>\n"
    "<div id=\"bar\">\n"
    "  <div id=\"dot\"></div>\n"
    "  <div id=\"info\">Connecting to ws://127.0.0.1:%u ...</div>\n"
    "</div>\n"
    "<ul id=\"log\"></ul>\n"
    "<script>\n"
    "const WS_PORT = %u;\n"
    "const SELF_ID = 'bob_browser';\n"
    "const PEER_ID = 'alice_native';\n"
    "const INSTANCE_ID = 2001;\n"
    "let answered = false;\n"
    "const ws  = new WebSocket('ws://127.0.0.1:' + WS_PORT, 'p2p');\n"
    "const dot  = document.getElementById('dot');\n"
    "const info = document.getElementById('info');\n"
    "const log  = document.getElementById('log');\n"
    "function ts() {\n"
    "  const d = new Date();\n"
    "  return d.toTimeString().slice(0,8) + '.' +\n"
    "         String(d.getMilliseconds()).padStart(3,'0');\n"
    "}\n"
    "function esc(s) {\n"
    "  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');\n"
    "}\n"
    "ws.onopen = () => {\n"
    "  dot.className = 'ok';\n"
    "  info.textContent = 'Connected  ws://127.0.0.1:' + WS_PORT;\n"
    "  ws.send('REG ' + SELF_ID + ' ' + INSTANCE_ID + '\\n');\n"
    "  const li = document.createElement('li');\n"
    "  li.innerHTML = '<span class=\"t\">' + ts() + '</span>'\n"
    "               + '<span class=\"msg\">TX: REG ' + SELF_ID + ' ' + INSTANCE_ID + '</span>';\n"
    "  log.appendChild(li);\n"
    "};\n"
    "ws.onclose = () => {\n"
    "  dot.className = '';\n"
    "  info.textContent = 'Disconnected';\n"
    "};\n"
    "ws.onerror = () => {\n"
    "  dot.className = 'err';\n"
    "  info.textContent = 'Connection error';\n"
    "};\n"
    "ws.onmessage = (e) => {\n"
    "  const txt = String(e.data || '');\n"
    "  const li = document.createElement('li');\n"
    "  li.innerHTML = '<span class=\"t\">' + ts() + '</span>'\n"
    "               + '<span class=\"msg\">RX: ' + esc(txt) + '</span>';\n"
    "  log.appendChild(li);\n"
    "  if (!answered && txt.startsWith('SDP ' + PEER_ID + '\\n')) {\n"
    "    answered = true;\n"
    "    const ans = 'v=0\\r\\no=bob_browser 2 2 IN IP4 127.0.0.1\\r\\ns=P2P-Answer\\r\\nt=0 0\\r\\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\\r\\n';\n"
    "    ws.send('SDP ' + PEER_ID + '\\n' + ans);\n"
    "    const tx = document.createElement('li');\n"
    "    tx.innerHTML = '<span class=\"t\">' + ts() + '</span>'\n"
    "                 + '<span class=\"msg\">TX: SDP ' + PEER_ID + ' (answer)</span>';\n"
    "    log.appendChild(tx);\n"
    "  }\n"
    "  window.scrollTo(0, document.body.scrollHeight);\n"
    "};\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";

static const char *HTML_PATH = "/tmp/p2p_ws_test.html";

static int write_html(uint16_t ws_port) {
    FILE *f = fopen(HTML_PATH, "w");
    if (!f) return -1;
    fprintf(f, HTML_FMT, (unsigned)ws_port, (unsigned)ws_port);
    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * 打开浏览器
 * -------------------------------------------------------------------- */
static void open_browser(const char *path) {
#if defined(__APPLE__)
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "open '%s'", path);
    system(cmd);
#elif defined(__linux__)
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", path);
    system(cmd);
#else
    (void)path;
    printf("  [!] 请手动打开: file://%s\n", path);
#endif
}

/* -----------------------------------------------------------------------
 * ws_client 辅助
 * -------------------------------------------------------------------- */
static int g_connected = 0;
static int g_reg_ok = 0;
static int g_sdp_ok = 0;
static int g_sdp_from_browser = 0;

static void on_open(ws_client_t *c, void *ud) { (void)c; (void)ud; g_connected = 1; }

static void on_msg(ws_client_t *c, ws_msg_type_t type, const uint8_t *data, size_t len, void *ud) {
    (void)c; (void)ud;
    if (type != WS_MSG_TEXT || !data || len == 0) return;

    char txt[2048];
    size_t n = len < sizeof(txt) - 1 ? len : sizeof(txt) - 1;
    memcpy(txt, data, n);
    txt[n] = '\0';

    printf("    [RX] %s\n", txt);

    if (strncmp(txt, "REG OK ", 7) == 0) {
        g_reg_ok = 1;
        return;
    }
    if (strncmp(txt, "SDP OK bob_browser", 18) == 0) {
        g_sdp_ok = 1;
        return;
    }
    if (strncmp(txt, "SDP bob_browser\n", 16) == 0) {
        g_sdp_from_browser = 1;
        return;
    }
}

/* pump：驱动 ws_client 事件循环，最多等 ms 毫秒直到 flag 为真 */
static int pump_until(ws_client_t *c, int *flag, int ms) {
    int ticks = ms / 5;
    for (int i = 0; i < ticks; i++) {
        ws_client_update(c);
        if (*flag) return 1;
        sleep_ms(5);
    }
    return 0;
}

/* 发送一条消息并 pump 足够长时间让 wslay 把帧写出去 */
static void send_and_flush(ws_client_t *c, const char *msg) {
    ws_client_send_text(c, msg);
    for (int i = 0; i < 60; i++) {   /* ~300ms */
        ws_client_update(c);
        sleep_ms(5);
    }
}

/* -----------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------- */
int main(int argc, char **argv) {
    printf("=== P2P WebSocket Browser SDP Demo ===\n\n");

    /* ---- 1. 找 p2p_server 二进制 ---- */
    const char *bin = find_server_binary();
    if (!bin) {
        fprintf(stderr, "[错误] 找不到 p2p_server 可执行文件\n"
                        "       请先编译项目：cd build && make p2p_server\n");
        return 1;
    }
    printf("[1] 找到 p2p_server: %s\n", bin);

    /* ---- 2. 确定端口 ---- */
    uint16_t relay_port, ws_port;
    if (argc >= 2) {
        relay_port = (uint16_t)atoi(argv[1]);
        ws_port    = relay_port;
    } else {
        relay_port = pick_free_port();
        ws_port    = relay_port;
    }
    if (!relay_port || !ws_port) {
        fprintf(stderr, "[错误] 无法分配端口\n");
        return 1;
    }

    /* ---- 3. 启动 p2p_server ---- */
    printf("[2] 启动 p2p_server — relay:%u  ws:%u\n", relay_port, ws_port);
    server_proc_t sp = start_server(bin, relay_port, ws_port);
    if (sp.pid < 0) {
        fprintf(stderr, "[错误] 启动 p2p_server 失败\n");
        return 1;
    }
    printf("    PID=%d  端口已就绪\n", (int)sp.pid);

    /* ---- 4. 生成 HTML 页面 ---- */
    printf("[3] 生成 HTML → %s\n", HTML_PATH);
    if (write_html(ws_port) != 0) {
        fprintf(stderr, "[错误] 无法写 HTML 文件\n");
        stop_server(&sp);
        return 1;
    }

    /* ---- 5. 打开浏览器 ---- */
    printf("[4] 打开浏览器...\n");
    open_browser(HTML_PATH);

    /* ---- 6. 等浏览器一点时间建立连接，再连 ws_client ---- */
    printf("[5] 等待浏览器加载 (1.5s)...\n");
    sleep_ms(1500);

    /* ---- 7. ws_client 连接 ---- */
    printf("[6] ws_client 连接 ws://127.0.0.1:%u ...\n", ws_port);
    g_connected = 0;
    g_reg_ok = 0;
    g_sdp_ok = 0;
    g_sdp_from_browser = 0;
    ws_client_cfg_t cfg = {0};
    cfg.on_open = on_open;
    cfg.on_message = on_msg;
    cfg.extra_headers = "Sec-WebSocket-Protocol: p2p\r\n";
    ws_client_t *cli = ws_client_create(&cfg);
    if (!cli) { fprintf(stderr, "[错误] ws_client_create 失败\n"); stop_server(&sp); return 1; }

    if (ws_client_connect(cli, "127.0.0.1", ws_port, "/") != 0) {
        fprintf(stderr, "[错误] ws_client_connect 失败\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }
    if (!pump_until(cli, &g_connected, 3000)) {
        fprintf(stderr, "[错误] WS 握手超时（3s）\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }
    printf("    握手成功！\n\n");

    /* ---- 8. 协议演示：alice REG + SDP offer，等待 SDP OK 与浏览器回传 ---- */
    printf("[7] 发送 REG（alice_native）...\n");
    send_and_flush(cli, "REG alice_native 1001\n");
    if (!pump_until(cli, &g_reg_ok, 2000)) {
        fprintf(stderr, "[错误] 未收到 REG OK\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }

    printf("[8] 发送 SDP 通知到 bob_browser ...\n");
    const char *offer =
        "v=0\r\n"
        "o=alice_native 1 1 IN IP4 127.0.0.1\r\n"
        "s=P2P-Offer\r\n"
        "t=0 0\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n";
    char sdp_msg[1024];
    snprintf(sdp_msg, sizeof(sdp_msg), "SDP bob_browser\n%s", offer);
    send_and_flush(cli, sdp_msg);

    if (!pump_until(cli, &g_sdp_ok, 3000)) {
        fprintf(stderr, "[错误] 未收到 SDP OK bob_browser\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }
    if (!pump_until(cli, &g_sdp_from_browser, 5000)) {
        fprintf(stderr, "[错误] 未收到来自 bob_browser 的 SDP 回传\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }

    printf("[9] SDP 通知交换成功（alice -> bob_browser -> alice）\n\n");

    /* ---- 9. 等用户确认后退出 ---- */
    printf("[完成] 浏览器 SDP 交换演示已完成。\n");
    printf("       请在浏览器中查看 REG/SDP 收发日志，按 Enter 结束...\n");
    (void)getchar();

    /* ---- 10. 清理 ---- */
    ws_client_destroy(cli);
    stop_server(&sp);
    printf("[退出] 清理完毕。\n");
    return 0;
}

#else  /* !WITH_WS */

#include <stdio.h>
int main(void) {
    fprintf(stderr, "该演示需要 WITH_WS 支持，请用 -DWITH_WS=ON 重新编译。\n");
    return 1;
}

#endif /* WITH_WS */
